#include "project/Services/FeaturePipeline.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

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

bool isFeatureValid(const ProcessedFrame& frame,
                    SensorChannel channel) noexcept {
    const std::size_t index = static_cast<std::size_t>(channel);
    return index < frame.channelQuality.size() &&
           frame.channelQuality[index] == DataQualityState::VALID;
}

bool finiteImu(const ProcessedFrame& frame) noexcept {
    return std::isfinite(frame.accelXG) && std::isfinite(frame.accelYG) &&
           std::isfinite(frame.accelZG);
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
    const ProcessedFrame& frame, FeatureSample& sample) const noexcept {
    if (!frame.aggregateStatus.valid ||
        !isFeatureValid(frame, SensorChannel::AccelXG) ||
        !isFeatureValid(frame, SensorChannel::AccelYG) ||
        !isFeatureValid(frame, SensorChannel::AccelZG) || !finiteImu(frame)) {
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
    return FeaturePipelineStatus::Ok;
}

bool FeaturePipeline::push(const ProcessedFrame& frame) noexcept {
    const FeaturePipelineStatus configurationStatus = validateConfig(config_);
    if (configurationStatus != FeaturePipelineStatus::Ok) {
        status_ = configurationStatus;
        return false;
    }

    if (hasCalibrationVersion_ &&
        calibrationVersion_ != frame.calibrationVersion) {
        resetWindow();
        status_ = FeaturePipelineStatus::SequenceDiscontinuity;
    }
    calibrationVersion_ = frame.calibrationVersion;
    hasCalibrationVersion_ = true;

    FeatureSample sample{};
    const FeaturePipelineStatus extractStatus = extract(frame, sample);
    if (extractStatus != FeaturePipelineStatus::Ok) {
        resetWindow();
        status_ = extractStatus;
        return false;
    }

    if (sampleCount_ > 0U) {
        const std::size_t previousIndex =
            (nextSample_ + kMaxWindowSamples - 1U) % kMaxWindowSamples;
        const FeatureSample& previous = samples_[previousIndex];
        if (sample.bootId != previous.bootId ||
            previous.sequence == std::numeric_limits<std::uint64_t>::max() ||
            sample.sequence != previous.sequence + 1U ||
            sample.timestampMs <= previous.timestampMs) {
            resetWindow();
            status_ = FeaturePipelineStatus::SequenceDiscontinuity;
        } else {
            const double expectedPeriodMs =
                1000.0 / static_cast<double>(config_.sampleRateHz);
            const double actualPeriodMs =
                static_cast<double>(sample.timestampMs - previous.timestampMs);
            if (std::fabs(actualPeriodMs - expectedPeriodMs) >
                static_cast<double>(config_.samplePeriodToleranceMs)) {
                resetWindow();
                status_ = FeaturePipelineStatus::SequenceDiscontinuity;
            }
        }
    }

    samples_[nextSample_] = sample;
    nextSample_ = (nextSample_ + 1U) % kMaxWindowSamples;
    if (sampleCount_ < config_.windowLengthSamples) {
        ++sampleCount_;
    }
    if (samplesSinceBuild_ < config_.strideSamples) {
        ++samplesSinceBuild_;
    }
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
    output.valid = true;
    samplesSinceBuild_ = 0U;
    status_ = FeaturePipelineStatus::Ok;
    return true;
}

void FeaturePipeline::resetWindow() noexcept {
    samples_ = {};
    nextSample_ = 0U;
    sampleCount_ = 0U;
    samplesSinceBuild_ = 0U;
    calibrationVersion_ = 0U;
    hasCalibrationVersion_ = false;
    status_ = FeaturePipelineStatus::NotReady;
}

}  // namespace project::services
