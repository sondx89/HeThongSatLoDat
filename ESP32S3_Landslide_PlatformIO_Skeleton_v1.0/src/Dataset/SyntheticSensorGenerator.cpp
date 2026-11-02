#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/SyntheticSensorGenerator.h"

#include <limits>

namespace project::dataset {
namespace {

bool validScenario(SyntheticScenario scenario) noexcept {
    switch (scenario) {
        case SyntheticScenario::Stable:
        case SyntheticScenario::GradualTrend:
        case SyntheticScenario::StepChange:
        case SyntheticScenario::Oscillation:
        case SyntheticScenario::ImpulseNoise:
        case SyntheticScenario::Saturation:
        case SyntheticScenario::Dropout:
        case SyntheticScenario::Stale:
        case SyntheticScenario::OutOfOrder:
        case SyntheticScenario::SequenceGap:
        case SyntheticScenario::Reboot:
        case SyntheticScenario::CalibrationChange:
        case SyntheticScenario::MixedValidity:
            return true;
    }
    return false;
}

bool checkedAdd(std::int64_t left, std::int64_t right,
                std::int64_t& result) noexcept {
    if (right > 0 &&
        left > std::numeric_limits<std::int64_t>::max() - right) {
        return false;
    }
    if (right < 0 &&
        left < std::numeric_limits<std::int64_t>::min() - right) {
        return false;
    }
    result = left + right;
    return true;
}

bool checkedMultiply(std::int64_t value, std::uint64_t multiplier,
                     std::int64_t& result) noexcept {
    if (value == 0 || multiplier == 0U) {
        result = 0;
        return true;
    }
    const std::uint64_t magnitude =
        value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1U
                  : static_cast<std::uint64_t>(value);
    const std::uint64_t limit =
        value < 0
            ? static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max()) +
                  1U
            : static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max());
    if (multiplier > limit / magnitude) {
        return false;
    }
    const std::uint64_t product = magnitude * multiplier;
    if (value >= 0) {
        result = static_cast<std::int64_t>(product);
        return true;
    }
    if (product == limit) {
        result = std::numeric_limits<std::int64_t>::min();
        return true;
    }
    result = -static_cast<std::int64_t>(product);
    return true;
}

}  // namespace

SyntheticSensorGenerator::SyntheticSensorGenerator(
    const SyntheticGeneratorConfig& config) noexcept
    : config_(config), randomState_(config.seed) {
    manifest_.scenario = config.scenario;
    manifest_.seed = config.seed;
    manifest_.recordCount = config.recordCount;
    manifest_.timestampStepMs = config.timestampStepMs;
    manifest_.parameters = config.parameters;
    if (!validateConfig(config_)) {
        status_ = SyntheticGeneratorStatus::InvalidConfiguration;
    }
}

bool SyntheticSensorGenerator::validateConfig(
    const SyntheticGeneratorConfig& config) noexcept {
    if (!validScenario(config.scenario) || config.seed == 0U ||
        config.recordCount == 0U || config.timestampStepMs == 0U ||
        config.parameters.noiseAmplitude < 0 ||
        config.parameters.noiseAmplitude >
            (std::numeric_limits<std::int64_t>::max() - 1) / 2 ||
        config.parameters.eventLength == 0U ||
        config.parameters.affectedChannel >= kSyntheticRawChannelCount ||
        config.parameters.saturationMinimum >
            config.parameters.saturationMaximum) {
        return false;
    }

    const auto& parameters = config.parameters;
    switch (config.scenario) {
        case SyntheticScenario::Stable:
        case SyntheticScenario::GradualTrend:
        case SyntheticScenario::StepChange:
        case SyntheticScenario::Oscillation:
        case SyntheticScenario::ImpulseNoise:
        case SyntheticScenario::Saturation:
        case SyntheticScenario::Dropout:
        case SyntheticScenario::Stale:
        case SyntheticScenario::CalibrationChange:
        case SyntheticScenario::MixedValidity:
            break;
        case SyntheticScenario::OutOfOrder:
        case SyntheticScenario::Reboot:
            if (parameters.eventSample == 0U ||
                parameters.eventSample >= config.recordCount) {
                return false;
            }
            break;
        case SyntheticScenario::SequenceGap:
            if (parameters.eventSample == 0U ||
                parameters.eventSample >= config.recordCount ||
                parameters.sequenceGapSize == 0U) {
                return false;
            }
            break;
    }
    if ((config.scenario == SyntheticScenario::StepChange ||
         config.scenario == SyntheticScenario::ImpulseNoise ||
         config.scenario == SyntheticScenario::Dropout ||
         config.scenario == SyntheticScenario::CalibrationChange) &&
        parameters.eventSample >= config.recordCount) {
        return false;
    }
    if (config.scenario == SyntheticScenario::Oscillation &&
        (parameters.oscillationPeriodSamples < 2U ||
         parameters.eventDelta ==
             std::numeric_limits<std::int64_t>::min() ||
         (parameters.eventDelta < 0
              ? -parameters.eventDelta
              : parameters.eventDelta) >
             std::numeric_limits<std::int64_t>::max() /
                 parameters.oscillationPeriodSamples)) {
            return false;
    }
    if (config.scenario == SyntheticScenario::Saturation &&
        (parameters.baseline < parameters.saturationMinimum ||
         parameters.baseline > parameters.saturationMaximum)) {
        return false;
    }
    if (config.scenario == SyntheticScenario::Stale &&
        parameters.staleAgeMs == 0U) {
        return false;
    }
    if (config.recordCount - 1U >
        (std::numeric_limits<std::uint64_t>::max() -
         config.firstTimestampMs) /
            config.timestampStepMs) {
        return false;
    }
    const std::uint64_t finalSequenceOffset =
        config.recordCount - 1U;
    if (config.firstSequence >
        std::numeric_limits<std::uint64_t>::max() - finalSequenceOffset) {
        return false;
    }
    if (config.scenario == SyntheticScenario::SequenceGap &&
        config.parameters.sequenceGapSize >
            std::numeric_limits<std::uint64_t>::max() -
                (config.firstSequence + finalSequenceOffset)) {
        return false;
    }
    if (config.scenario == SyntheticScenario::Reboot &&
        config.bootId == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    return true;
}

std::uint64_t SyntheticSensorGenerator::nextRandom() noexcept {
    std::uint64_t value = randomState_;
    value ^= value >> 12U;
    value ^= value << 25U;
    value ^= value >> 27U;
    randomState_ = value;
    return value * 0x2545F4914F6CDD1DULL;
}

bool SyntheticSensorGenerator::generateOffset(
    std::uint64_t sampleIndex, std::int64_t& offset,
    std::int64_t& noiseOffset,
    bool& eventActive) noexcept {
    const auto& parameters = config_.parameters;
    offset = 0;
    noiseOffset = 0;
    eventActive = false;
    switch (config_.scenario) {
        case SyntheticScenario::Stable:
            break;
        case SyntheticScenario::GradualTrend:
            if (!checkedMultiply(parameters.trendPerSample, sampleIndex,
                                 offset)) {
                return false;
            }
            break;
        case SyntheticScenario::StepChange:
            eventActive = sampleIndex >= parameters.eventSample;
            if (eventActive) {
                offset = parameters.eventDelta;
            }
            break;
        case SyntheticScenario::Oscillation: {
            const std::uint64_t phase =
                sampleIndex % parameters.oscillationPeriodSamples;
            const std::uint64_t halfPeriod =
                parameters.oscillationPeriodSamples / 2U;
            const std::uint64_t scale =
                parameters.oscillationPeriodSamples -
                (phase < halfPeriod ? 2U * phase : 2U * (phase - halfPeriod));
            const std::uint64_t denominator =
                parameters.oscillationPeriodSamples;
            const std::int64_t amplitude = parameters.eventDelta;
            const std::uint64_t absoluteAmplitude =
                amplitude < 0
                    ? static_cast<std::uint64_t>(-(amplitude + 1)) + 1U
                    : static_cast<std::uint64_t>(amplitude);
            const std::uint64_t magnitude =
                denominator == 0U || scale == 0U
                    ? 0U
                    : absoluteAmplitude >
                              std::numeric_limits<std::uint64_t>::max() /
                                  scale
                          ? std::numeric_limits<std::uint64_t>::max()
                          : absoluteAmplitude * scale / denominator;
            offset = magnitude >
                             static_cast<std::uint64_t>(
                                 std::numeric_limits<std::int64_t>::max())
                         ? std::numeric_limits<std::int64_t>::max()
                         : static_cast<std::int64_t>(magnitude);
            if (amplitude < 0) {
                offset = -offset;
            }
            eventActive = offset != 0;
            break;
        }
        case SyntheticScenario::ImpulseNoise:
            eventActive = sampleIndex >= parameters.eventSample &&
                          sampleIndex - parameters.eventSample <
                              parameters.eventLength;
            if (eventActive) {
                offset = parameters.eventDelta;
            }
            break;
        case SyntheticScenario::Saturation:
            eventActive = true;
            break;
        case SyntheticScenario::Dropout:
        case SyntheticScenario::Stale:
        case SyntheticScenario::OutOfOrder:
        case SyntheticScenario::SequenceGap:
        case SyntheticScenario::Reboot:
        case SyntheticScenario::CalibrationChange:
        case SyntheticScenario::MixedValidity:
            break;
    }

    const std::int64_t amplitude = parameters.noiseAmplitude;
    if (amplitude > 0) {
        const std::uint64_t width =
            static_cast<std::uint64_t>(amplitude) * 2U + 1U;
        noiseOffset =
            static_cast<std::int64_t>(nextRandom() % width) - amplitude;
        std::int64_t updatedOffset = 0;
        if (!checkedAdd(offset, noiseOffset, updatedOffset)) {
            return false;
        }
        offset = updatedOffset;
    }
    return true;
}

SyntheticGeneratorStatus SyntheticSensorGenerator::next(
    SyntheticRecord& output) noexcept {
    output = {};
    if (status_ != SyntheticGeneratorStatus::Ok) {
        return status_;
    }
    if (generatedCount_ >= config_.recordCount) {
        return SyntheticGeneratorStatus::EndOfData;
    }

    std::int64_t generatedOffset = 0;
    std::int64_t noiseOffset = 0;
    bool eventActive = false;
    if (!generateOffset(generatedCount_, generatedOffset, noiseOffset,
                        eventActive)) {
        status_ = SyntheticGeneratorStatus::NumericOverflow;
        return status_;
    }

    SyntheticRawObservation observation{};
    SyntheticLatentState latent{};
    observation.provenance = {DataOrigin::Synthetic, EvidenceClass::GT5};
    observation.evaluationTimeMs =
        config_.firstTimestampMs + generatedCount_ * config_.timestampStepMs;
    observation.timestampMs = observation.evaluationTimeMs;
    observation.bootId = config_.bootId;
    observation.sequenceNo = config_.firstSequence + generatedCount_;
    observation.calibrationVersion = 0U;

    if (config_.scenario == SyntheticScenario::SequenceGap &&
        generatedCount_ >= config_.parameters.eventSample) {
        observation.sequenceNo += config_.parameters.sequenceGapSize;
    } else if (config_.scenario == SyntheticScenario::Reboot &&
               generatedCount_ >= config_.parameters.eventSample) {
        observation.bootId += 1U;
        observation.sequenceNo =
            generatedCount_ - config_.parameters.eventSample;
        observation.timestampMs =
            (generatedCount_ - config_.parameters.eventSample) *
            config_.timestampStepMs;
        observation.evaluationTimeMs = observation.timestampMs;
    } else if (config_.scenario == SyntheticScenario::OutOfOrder &&
               generatedCount_ == config_.parameters.eventSample) {
        observation.sequenceNo = config_.firstSequence +
                                 generatedCount_ - 1U;
        observation.timestampMs = observation.evaluationTimeMs -
                                  config_.timestampStepMs;
    }
    if (config_.scenario == SyntheticScenario::CalibrationChange &&
        generatedCount_ >= config_.parameters.eventSample) {
        observation.calibrationVersion = 1U;
    }

    const bool staleScenario = config_.scenario == SyntheticScenario::Stale;
    const bool dropoutScenario = config_.scenario == SyntheticScenario::Dropout;
    for (std::size_t channel = 0U; channel < kSyntheticRawChannelCount;
         ++channel) {
        bool valid = true;
        if (dropoutScenario && channel == config_.parameters.affectedChannel &&
            generatedCount_ >= config_.parameters.eventSample) {
            valid = false;
        } else if (config_.scenario == SyntheticScenario::MixedValidity) {
            valid = ((generatedCount_ + channel) % 3U) != 0U;
        }

        observation.channelValid[channel] = valid;
        if (valid) {
            std::int64_t value = 0;
            if (!checkedAdd(config_.parameters.baseline, generatedOffset,
                            value) ||
                !checkedAdd(value, config_.parameters.channelOffsets[channel],
                            value)) {
                status_ = SyntheticGeneratorStatus::NumericOverflow;
                output = {};
                return status_;
            }
            if (config_.scenario == SyntheticScenario::Saturation) {
                value = config_.parameters.saturationMaximum;
            }
            observation.rawValues[channel] = value;
            observation.channelTimestampMs[channel] =
                staleScenario &&
                        observation.timestampMs >=
                            config_.parameters.staleAgeMs
                    ? observation.timestampMs - config_.parameters.staleAgeMs
                    : (staleScenario ? 0U : observation.timestampMs);
        }
    }
    if (!validateConfig(config_) || !observation.provenance.isValid()) {
        status_ = SyntheticGeneratorStatus::InvalidConfiguration;
        return status_;
    }

    latent.scenario = config_.scenario;
    latent.sampleIndex = generatedCount_;
    latent.generatedOffset = generatedOffset;
    latent.noiseOffset = noiseOffset;
    latent.eventActive = eventActive;
    for (std::size_t channel = 0U; channel < kSyntheticRawChannelCount;
         ++channel) {
        if (observation.channelValid[channel]) {
            latent.validChannelMask |=
                static_cast<std::uint8_t>(1U << channel);
        }
    }
    output.observation = observation;
    output.latent = latent;
    ++generatedCount_;
    return SyntheticGeneratorStatus::Ok;
}

}  // namespace project::dataset

#endif
