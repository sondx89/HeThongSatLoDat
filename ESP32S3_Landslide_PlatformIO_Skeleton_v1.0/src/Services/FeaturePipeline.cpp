#include "project/Services/FeaturePipeline.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "project/Services/DataQualityService.h"

namespace project::services {
namespace {

using project::models::DataQualityState;
using project::models::ProcessedFrame;
using project::models::SensorChannel;

constexpr double kRadiansToDegrees =
    57.295779513082320876798154814105;

bool isKnownFeature(FssFeatureId id) noexcept {
    switch (id) {
        case FssFeatureId::AccelXRaw:
        case FssFeatureId::AccelYRaw:
        case FssFeatureId::AccelZRaw:
        case FssFeatureId::AccelMagnitude:
        case FssFeatureId::TiltXDeg:
        case FssFeatureId::TiltYDeg:
        case FssFeatureId::SoilRaw:
        case FssFeatureId::Yl83Raw:
        case FssFeatureId::Hx710bRaw:
            return true;
    }
    return false;
}

bool finiteImu(const ProcessedFrame& frame) noexcept {
    return std::isfinite(frame.accelXG) && std::isfinite(frame.accelYG) &&
           std::isfinite(frame.accelZG);
}

project::models::WindowRejectionReason rejectionForQuality(
    DataQualityState quality) noexcept {
    using project::models::WindowRejectionReason;
    switch (quality) {
        case DataQualityState::MISSING:
        case DataQualityState::STALE:
        case DataQualityState::OUT_OF_RANGE:
        case DataQualityState::NON_FINITE:
        case DataQualityState::CALIBRATION_INVALID:
        case DataQualityState::OUT_OF_ORDER:
        case DataQualityState::GAP_DETECTED:
        case DataQualityState::NOT_READY:
        case DataQualityState::SENSOR_ERROR:
            return WindowRejectionReason::RequiredChannelQuality;
        case DataQualityState::VALID:
            return WindowRejectionReason::None;
    }
    return WindowRejectionReason::RequiredChannelQuality;
}

}  // namespace

FeaturePipeline::FeaturePipeline(
    const FeaturePipelineConfig& config) noexcept
    : config_(config) {
    status_ = validateConfig(config_);
}

FeaturePipelineStatus FeaturePipeline::validateConfig(
    const FeaturePipelineConfig& config) noexcept {
    if (!config.schemaApproved || !config.modelMetadataVerified ||
        !config.flatLayoutApproved) {
        return FeaturePipelineStatus::SchemaNotApproved;
    }
    if (config.schemaVersion == 0U || config.schemaId == 0U ||
        !config.schemaHashValid || config.featureCount == 0U ||
        config.featureCount > project::models::FeatureVector::kMaxFeatures ||
        config.windowLengthSamples == 0U ||
        config.windowLengthSamples > kMaxWindowSamples ||
        config.strideSamples == 0U || !config.sampleRateConfigured ||
        !config.samplePeriodToleranceConfigured ||
        !config.preprocessingApproved ||
        config.preprocessingVersion == 0U ||
        !std::isfinite(config.sampleRateHz) || config.sampleRateHz <= 0.0F ||
        (1000.0 / static_cast<double>(config.sampleRateHz)) < 1.0) {
        return FeaturePipelineStatus::InvalidConfiguration;
    }

    bool hasNonZeroHashByte = false;
    for (const std::uint8_t byte : config.schemaHash) {
        hasNonZeroHashByte = hasNonZeroHashByte || byte != 0U;
    }
    if (!hasNonZeroHashByte) {
        return FeaturePipelineStatus::InvalidConfiguration;
    }

    const std::size_t outputCount =
        static_cast<std::size_t>(config.featureCount) *
        static_cast<std::size_t>(config.windowLengthSamples);
    if (outputCount > project::models::FeatureVector::kMaxFeatures) {
        return FeaturePipelineStatus::OutputTooLarge;
    }

    if (!std::isfinite(config.accelerationToMps2) ||
        (config.accelerationScaleConfigured &&
         config.accelerationToMps2 <= 0.0F)) {
        return FeaturePipelineStatus::InvalidConfiguration;
    }

    bool needsMagnitudeScale = false;
    bool needsTiltFormula = false;
    for (std::size_t i = 0; i < config.featureCount; ++i) {
        const FeatureDefinition& feature = config.features[i];
        if (!isKnownFeature(feature.id)) {
            return FeaturePipelineStatus::UnsupportedFeature;
        }
        if (!feature.modelInputEligible) {
            return FeaturePipelineStatus::SchemaNotApproved;
        }
        if (!featureSupported(feature.id)) {
            return FeaturePipelineStatus::UnsupportedFeature;
        }
        if (feature.index != i) {
            return FeaturePipelineStatus::InvalidConfiguration;
        }
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (config.features[previous].id == feature.id) {
                return FeaturePipelineStatus::InvalidConfiguration;
            }
        }
        needsMagnitudeScale =
            needsMagnitudeScale ||
            feature.id == FssFeatureId::AccelMagnitude;
        needsTiltFormula =
            needsTiltFormula || feature.id == FssFeatureId::TiltXDeg ||
            feature.id == FssFeatureId::TiltYDeg;
    }

    if (needsMagnitudeScale && !config.accelerationScaleConfigured) {
        return FeaturePipelineStatus::InvalidConfiguration;
    }
    if (needsTiltFormula && !config.tiltFormulaApproved) {
        return FeaturePipelineStatus::SchemaNotApproved;
    }
    return FeaturePipelineStatus::Ok;
}

bool FeaturePipeline::featureSupported(FssFeatureId id) noexcept {
    switch (id) {
        case FssFeatureId::AccelMagnitude:
        case FssFeatureId::TiltXDeg:
        case FssFeatureId::TiltYDeg:
            return true;
        case FssFeatureId::AccelXRaw:
        case FssFeatureId::AccelYRaw:
        case FssFeatureId::AccelZRaw:
        case FssFeatureId::SoilRaw:
        case FssFeatureId::Yl83Raw:
        case FssFeatureId::Hx710bRaw:
            return false;
    }
    return false;
}

FeaturePipelineStatus FeaturePipeline::extract(
    const ProcessedFrame& frame, FeatureSample& sample,
    SensorChannel& rejectedChannel, DataQualityState& rejectedQuality,
    project::models::WindowRejectionReason& rejectionReason) const noexcept {
    const SensorChannel requiredChannels[] = {
        SensorChannel::AccelXG, SensorChannel::AccelYG, SensorChannel::AccelZG};
    for (const SensorChannel channel : requiredChannels) {
        const std::size_t index = static_cast<std::size_t>(channel);
        const DataQualityState quality = frame.channelQuality[index];
        if (quality != DataQualityState::VALID) {
            rejectedChannel = channel;
            rejectedQuality = quality;
            rejectionReason = rejectionForQuality(quality);
            return FeaturePipelineStatus::InvalidFeatureData;
        }
    }
    if (!frame.aggregateStatus.valid) {
        rejectionReason =
            project::models::WindowRejectionReason::InvalidAggregateStatus;
        return FeaturePipelineStatus::InvalidFrame;
    }
    if (!finiteImu(frame)) {
        rejectionReason =
            project::models::WindowRejectionReason::NonFiniteFeature;
        return FeaturePipelineStatus::InvalidFeatureData;
    }

    const double ax = static_cast<double>(frame.accelXG);
    const double ay = static_cast<double>(frame.accelYG);
    const double az = static_cast<double>(frame.accelZG);
    for (std::size_t i = 0; i < config_.featureCount; ++i) {
        double value = 0.0;
        switch (config_.features[i].id) {
            case FssFeatureId::AccelMagnitude: {
                const double magnitude = std::sqrt(ax * ax + ay * ay + az * az);
                value = magnitude *
                        static_cast<double>(config_.accelerationToMps2);
                break;
            }
            case FssFeatureId::TiltXDeg:
                value = std::atan2(ay, std::sqrt(ax * ax + az * az)) *
                        kRadiansToDegrees;
                break;
            case FssFeatureId::TiltYDeg:
                value = std::atan2(-ax, std::sqrt(ay * ay + az * az)) *
                        kRadiansToDegrees;
                break;
            case FssFeatureId::AccelXRaw:
            case FssFeatureId::AccelYRaw:
            case FssFeatureId::AccelZRaw:
            case FssFeatureId::SoilRaw:
            case FssFeatureId::Yl83Raw:
            case FssFeatureId::Hx710bRaw:
                return FeaturePipelineStatus::UnsupportedFeature;
        }
        if (!std::isfinite(value) ||
            value > static_cast<double>(std::numeric_limits<float>::max()) ||
            value < -static_cast<double>(std::numeric_limits<float>::max())) {
            return FeaturePipelineStatus::InvalidFeatureData;
        }
        sample.values[i] = static_cast<float>(value);
    }

    sample.bootId = frame.bootId;
    sample.sequence = frame.sequence;
    sample.timestampMs = frame.timestampMs;
    sample.channelQuality = frame.channelQuality;
    return FeaturePipelineStatus::Ok;
}

bool FeaturePipeline::push(const ProcessedFrame& frame) noexcept {
    const FeaturePipelineStatus configurationStatus = validateConfig(config_);
    if (configurationStatus != FeaturePipelineStatus::Ok) {
        clearSamples();
        windowQuality_ = {};
        windowQuality_.eligibility =
            project::models::WindowEligibilityState::Rejected;
        windowQuality_.rejectionReason =
            project::models::WindowRejectionReason::InvalidConfiguration;
        status_ = configurationStatus;
        return false;
    }

    if (hasInputSchemaVersion_ &&
        inputSchemaVersion_ != frame.schemaVersion) {
        clearSamples();
        windowQuality_ = {};
        windowQuality_.eligibility =
            project::models::WindowEligibilityState::Rejected;
        windowQuality_.rejectionReason =
            project::models::WindowRejectionReason::InvalidConfiguration;
        status_ = FeaturePipelineStatus::InvalidConfiguration;
        return false;
    }

    const project::models::DataContinuityState calibrationBoundary =
        hasCalibrationVersion_ &&
                calibrationVersion_ != frame.calibrationVersion
            ? project::models::DataContinuityState::CalibrationChanged
            : project::models::DataContinuityState::NotEvaluated;
    if (hasCalibrationVersion_ &&
        calibrationVersion_ != frame.calibrationVersion) {
        clearSamples();
        windowQuality_ = {};
        windowQuality_.boundaryState =
            project::models::DataContinuityState::CalibrationChanged;
    }
    calibrationVersion_ = frame.calibrationVersion;
    hasCalibrationVersion_ = true;

    FeatureSample sample{};
    SensorChannel rejectedChannel = SensorChannel::Count;
    DataQualityState rejectedQuality = DataQualityState::MISSING;
    project::models::WindowRejectionReason rejectionReason =
        project::models::WindowRejectionReason::None;
    const FeaturePipelineStatus extractStatus =
        extract(frame, sample, rejectedChannel, rejectedQuality,
                rejectionReason);
    if (extractStatus != FeaturePipelineStatus::Ok) {
        clearSamples();
        windowQuality_ = {};
        windowQuality_.eligibility =
            project::models::WindowEligibilityState::Rejected;
        windowQuality_.rejectionReason = rejectionReason;
        windowQuality_.boundaryState = calibrationBoundary;
        windowQuality_.rejectedChannel = rejectedChannel;
        windowQuality_.rejectedChannelQuality = rejectedQuality;
        windowQuality_.rejectedChannelValid =
            rejectedChannel != SensorChannel::Count;
        status_ = extractStatus;
        return false;
    }

    if (hasLastAcceptedSample_) {
        const double expectedPeriodMs =
            1000.0 / static_cast<double>(config_.sampleRateHz);
        const project::models::DataContinuityState continuity =
            DataQualityService::classifyContinuity(
                lastAcceptedBootId_, lastAcceptedSequence_,
                lastAcceptedTimestampMs_, sample.bootId, sample.sequence,
                sample.timestampMs, expectedPeriodMs,
                config_.samplePeriodToleranceMs);
        if (continuity ==
            project::models::DataContinuityState::OutOfOrder) {
            clearSamples();
            windowQuality_ = {};
            windowQuality_.eligibility =
                project::models::WindowEligibilityState::Rejected;
            windowQuality_.rejectionReason =
                project::models::WindowRejectionReason::OutOfOrder;
            windowQuality_.boundaryState = continuity;
            status_ = FeaturePipelineStatus::SequenceDiscontinuity;
            return false;
        }
        if (continuity ==
                project::models::DataContinuityState::BootChanged ||
            continuity ==
                project::models::DataContinuityState::SequenceGap ||
            continuity ==
                project::models::DataContinuityState::TimestampDiscontinuity) {
            clearSamples();
            windowQuality_ = {};
            windowQuality_.boundaryState = continuity;
        }
    } else if (windowQuality_.boundaryState ==
               project::models::DataContinuityState::NotEvaluated) {
        windowQuality_.boundaryState =
            project::models::DataContinuityState::FirstSample;
    }

    calibrationVersion_ = frame.calibrationVersion;
    hasCalibrationVersion_ = true;
    samples_[nextSample_] = sample;
    nextSample_ = (nextSample_ + 1U) % kMaxWindowSamples;
    if (sampleCount_ < config_.windowLengthSamples) {
        ++sampleCount_;
    }
    if (samplesSinceBuild_ < config_.strideSamples) {
        ++samplesSinceBuild_;
    }
    lastAcceptedBootId_ = sample.bootId;
    lastAcceptedSequence_ = sample.sequence;
    lastAcceptedTimestampMs_ = sample.timestampMs;
    hasLastAcceptedSample_ = true;
    inputSchemaVersion_ = frame.schemaVersion;
    hasInputSchemaVersion_ = true;
    refreshWindowQuality();
    status_ = FeaturePipelineStatus::Ok;
    return true;
}

bool FeaturePipeline::windowReady() const noexcept {
    return validateConfig(config_) == FeaturePipelineStatus::Ok &&
           sampleCount_ >= config_.windowLengthSamples &&
           samplesSinceBuild_ >= config_.strideSamples;
}

bool FeaturePipeline::build(
    project::models::FeatureVector& output) noexcept {
    output = {};
    const FeaturePipelineStatus configurationStatus = validateConfig(config_);
    if (configurationStatus != FeaturePipelineStatus::Ok) {
        status_ = configurationStatus;
        return false;
    }
    if (!windowReady()) {
        status_ = FeaturePipelineStatus::NotReady;
        return false;
    }

    const std::size_t valueCount =
        static_cast<std::size_t>(config_.featureCount) *
        static_cast<std::size_t>(config_.windowLengthSamples);
    if (valueCount > project::models::FeatureVector::kMaxFeatures) {
        status_ = FeaturePipelineStatus::OutputTooLarge;
        return false;
    }

    const std::size_t oldestIndex =
        (nextSample_ + kMaxWindowSamples - config_.windowLengthSamples) %
        kMaxWindowSamples;
    for (std::size_t sampleOffset = 0;
         sampleOffset < config_.windowLengthSamples; ++sampleOffset) {
        const FeatureSample& sample =
            samples_[(oldestIndex + sampleOffset) % kMaxWindowSamples];
        for (std::size_t featureIndex = 0;
             featureIndex < config_.featureCount; ++featureIndex) {
            const std::size_t outputIndex =
                sampleOffset * config_.featureCount + featureIndex;
            output.values[outputIndex] = sample.values[featureIndex];
            output.featureValidMask |=
                static_cast<std::uint32_t>(1UL << outputIndex);
        }
    }

    const FeatureSample& first = samples_[oldestIndex];
    const std::size_t newestIndex =
        (nextSample_ + kMaxWindowSamples - 1U) % kMaxWindowSamples;
    const FeatureSample& last = samples_[newestIndex];

    output.schemaVersion = config_.schemaVersion;
    output.schemaId = config_.schemaId;
    for (std::size_t i = 0; i < config_.schemaHash.size(); ++i) {
        output.schemaHashSha256[i] = config_.schemaHash[i];
    }
    output.schemaHashValid = config_.schemaHashValid;
    output.bootId = first.bootId;
    output.relatedSequenceNo = last.sequence;
    output.windowStartMs = first.timestampMs;
    output.windowEndMs = last.timestampMs;
    output.windowSampleCount = config_.windowLengthSamples;
    output.count = static_cast<std::uint16_t>(valueCount);
    output.quality = windowQuality_;
    output.quality.eligibility =
        project::models::WindowEligibilityState::Eligible;
    output.quality.rejectionReason =
        project::models::WindowRejectionReason::None;
    output.valid = true;
    samplesSinceBuild_ = 0U;
    status_ = FeaturePipelineStatus::Ok;
    return true;
}

void FeaturePipeline::resetWindow() noexcept {
    clearSamples();
    lastAcceptedBootId_ = 0U;
    lastAcceptedSequence_ = 0U;
    lastAcceptedTimestampMs_ = 0U;
    hasLastAcceptedSample_ = false;
    windowQuality_ = {};
    windowQuality_.eligibility =
        project::models::WindowEligibilityState::Incomplete;
    windowQuality_.channelQuality.fill(DataQualityState::MISSING);
    status_ = FeaturePipelineStatus::NotReady;
}

void FeaturePipeline::clearSamples() noexcept {
    samples_ = {};
    nextSample_ = 0U;
    sampleCount_ = 0U;
    samplesSinceBuild_ = 0U;
    calibrationVersion_ = 0U;
    hasCalibrationVersion_ = false;
}

void FeaturePipeline::refreshWindowQuality() noexcept {
    windowQuality_.eligibility = windowReady()
                                     ? project::models::WindowEligibilityState::Eligible
                                     : project::models::WindowEligibilityState::Incomplete;
    windowQuality_.rejectionReason =
        project::models::WindowRejectionReason::None;
    windowQuality_.rejectedChannel =
        project::models::SensorChannel::Count;
    windowQuality_.rejectedChannelValid = false;
    windowQuality_.channelQuality.fill(DataQualityState::VALID);
    if (sampleCount_ == 0U) {
        windowQuality_.channelQuality.fill(DataQualityState::MISSING);
        return;
    }
    const std::size_t first =
        (nextSample_ + kMaxWindowSamples - sampleCount_) % kMaxWindowSamples;
    std::array<bool,
               static_cast<std::size_t>(SensorChannel::Count)>
        foundNonValid{};
    for (std::size_t offset = 0U; offset < sampleCount_; ++offset) {
        const FeatureSample& sample =
            samples_[(first + offset) % kMaxWindowSamples];
        for (std::size_t channel = 0U;
             channel < sample.channelQuality.size(); ++channel) {
            const DataQualityState quality = sample.channelQuality[channel];
            if (!foundNonValid[channel] &&
                quality != DataQualityState::VALID) {
                windowQuality_.channelQuality[channel] = quality;
                foundNonValid[channel] = true;
            }
        }
    }
}

}  // namespace project::services
