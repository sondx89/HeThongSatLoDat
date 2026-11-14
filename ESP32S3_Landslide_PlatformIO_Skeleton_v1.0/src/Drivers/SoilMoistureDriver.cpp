#include "project/Drivers/SoilMoistureDriver.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <esp_timer.h>

namespace project::drivers {
namespace {

bool validMoisturePercent(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 100.0F;
}

bool validEndpointPair(
    const std::array<SoilMoistureCalibrationPoint, 2>& points) noexcept {
    return points[0].adcCount != points[1].adcCount &&
           ((points[0].moisturePercent == 0.0F &&
             points[1].moisturePercent == 100.0F) ||
            (points[0].moisturePercent == 100.0F &&
             points[1].moisturePercent == 0.0F));
}

constexpr std::uint32_t kMinimumWindowSpanMs =
    static_cast<std::uint32_t>(
        (SoilMoistureDriver::kSamplesPerChannel - 1U) *
        SoilMoistureDriver::kMinimumSampleSpacingUs / 1000U);

}  // namespace

SoilMoistureDriver::SoilMoistureDriver(
    IAdcReader& adcReader,
    const project::bsp::BoardPins& boardPins,
    const SoilMoistureDriverConfig& config) noexcept
    : adcReader_(adcReader), boardPins_(boardPins), config_(config) {}

SoilMoistureStatus SoilMoistureDriver::validateConfiguration() const noexcept {
    if (config_.adcMaximumCount == 0U ||
        config_.requiredChannelCount == 0U ||
        config_.maximumSampleAgeMs < kMinimumWindowSpanMs ||
        !config_.adcConfigurationVerified ||
        !config_.calibrationConfigured ||
        config_.calibrationVersion == 0U) {
        return SoilMoistureStatus::InvalidConfiguration;
    }

    std::uint8_t presentCount = 0;
    std::uint8_t requiredCount = 0;
    for (std::size_t channelIndex = 0;
         channelIndex < project::models::kSoilMoistureProbeCount;
         ++channelIndex) {
        const auto& channel = config_.channels[channelIndex];
        const auto& pin = boardPins_.adc.soilMoisture[channelIndex];
        if (channel.required && !channel.present) {
            return SoilMoistureStatus::InvalidConfiguration;
        }
        if (!channel.present) {
            continue;
        }

        ++presentCount;
        requiredCount += channel.required ? 1U : 0U;
        if (!pin.isConfigured() || !pin.isWellFormed()) {
            return SoilMoistureStatus::InvalidConfiguration;
        }
        for (std::size_t previous = 0; previous < channelIndex; ++previous) {
            if (config_.channels[previous].present &&
                boardPins_.adc.soilMoisture[previous].number == pin.number) {
                return SoilMoistureStatus::InvalidConfiguration;
            }
        }

        const auto& points = channel.calibrationPoints;
        if (points[0].adcCount > config_.adcMaximumCount ||
            points[1].adcCount > config_.adcMaximumCount ||
            !validMoisturePercent(points[0].moisturePercent) ||
            !validMoisturePercent(points[1].moisturePercent) ||
            !validEndpointPair(points)) {
            return SoilMoistureStatus::InvalidCalibration;
        }
    }

    if (presentCount == 0U || config_.requiredChannelCount > presentCount ||
        config_.requiredChannelCount < requiredCount) {
        return SoilMoistureStatus::InvalidConfiguration;
    }
    return SoilMoistureStatus::Ok;
}

project::models::SensorStatus SoilMoistureDriver::begin() {
    initialized_ = false;
    channels_ = {};
    for (std::size_t channelIndex = 0;
         channelIndex < project::models::kSoilMoistureProbeCount;
         ++channelIndex) {
        channels_[channelIndex].quality =
            config_.channels[channelIndex].present
                ? project::models::SoilMoistureQuality::WarmingUp
                : project::models::SoilMoistureQuality::NotPresent;
    }

    lastStatus_ = validateConfiguration();
    initialized_ = lastStatus_ == SoilMoistureStatus::Ok;
    return makeSensorStatus(initialized_);
}

project::models::SensorStatus SoilMoistureDriver::read(
    project::models::SensorFrame& output) {
    output = {};
    SoilMoistureSample sample{};
    lastStatus_ = readSample(sample);

    output.soilMoistureProbes = sample.probes;
    output.soilMoistureValidChannelCount = sample.validChannelCount;
    output.soilMoistureRequiredChannelCount = sample.requiredChannelCount;
    output.soilMoistureRawChannelIndex = sample.rawChannelIndex;
    output.soilMoistureAverageValid = sample.valid;
    if (sample.rawChannelIndex != 0xFFU) {
        output.soilMoistureRaw = sample.rawAdcCount;
    }
    if (sample.valid) {
        output.soilMoisturePct = sample.moisturePercent;
        output.calibrationVersion = sample.calibrationVersion;
    }

    output.soilStatus = makeSensorStatus(sample.valid);
    if (!sample.valid &&
        (lastStatus_ == SoilMoistureStatus::SamplePending ||
         lastStatus_ == SoilMoistureStatus::AdcReadFailed ||
         lastStatus_ == SoilMoistureStatus::AdcValueOutOfRange)) {
        output.soilStatus.health = project::models::SensorHealth::Degraded;
    }
    return output.soilStatus;
}

project::models::SensorStatus SoilMoistureDriver::selfTest() {
    if (!initialized_) {
        lastStatus_ = SoilMoistureStatus::NotInitialized;
        return makeSensorStatus(false);
    }

    for (std::size_t channelIndex = 0;
         channelIndex < project::models::kSoilMoistureProbeCount;
         ++channelIndex) {
        if (!config_.channels[channelIndex].required) {
            continue;
        }

        std::int32_t reading = 0;
        const auto pin = static_cast<std::uint8_t>(
            boardPins_.adc.soilMoisture[channelIndex].number);
        if (!adcReader_.readRaw(pin, reading)) {
            lastStatus_ = SoilMoistureStatus::AdcReadFailed;
            return makeSensorStatus(false);
        }
        if (reading < 0 ||
            static_cast<std::uint32_t>(reading) > config_.adcMaximumCount) {
            lastStatus_ = SoilMoistureStatus::AdcValueOutOfRange;
            return makeSensorStatus(false);
        }
    }

    lastStatus_ = SoilMoistureStatus::Ok;
    return makeSensorStatus(true);
}

SoilMoistureStatus SoilMoistureDriver::readSample(
    SoilMoistureSample& output) noexcept {
    output = {};
    output.requiredChannelCount = config_.requiredChannelCount;
    if (!initialized_) {
        lastStatus_ = SoilMoistureStatus::NotInitialized;
        return lastStatus_;
    }

    const std::int64_t startTimeUs = esp_timer_get_time();
    if (startTimeUs < 0) {
        lastStatus_ = SoilMoistureStatus::ClockError;
        return lastStatus_;
    }

    SoilMoistureStatus channelError = SoilMoistureStatus::SamplePending;
    for (std::size_t channelIndex = 0;
         channelIndex < project::models::kSoilMoistureProbeCount;
         ++channelIndex) {
        const auto& channelConfig = config_.channels[channelIndex];
        ChannelState& channelState = channels_[channelIndex];
        if (!channelConfig.present) {
            continue;
        }

        const std::int64_t sampleStartUs = esp_timer_get_time();
        if (sampleStartUs < 0 ||
            (channelState.hasSampled &&
             static_cast<std::uint64_t>(sampleStartUs) <
                 channelState.lastSampleMicros)) {
            lastStatus_ = SoilMoistureStatus::ClockError;
            return lastStatus_;
        }
        if (channelState.hasSuccessfulSample &&
            static_cast<std::uint64_t>(sampleStartUs) -
                    channelState.lastSuccessfulMicros >
                static_cast<std::uint64_t>(config_.maximumSampleAgeMs) *
                    1000U) {
            const std::uint64_t lastSampleMicros =
                channelState.lastSampleMicros;
            const bool hasSampled = channelState.hasSampled;
            resetChannel(channelIndex);
            channelState.lastSampleMicros = lastSampleMicros;
            channelState.hasSampled = hasSampled;
            channelState.quality =
                project::models::SoilMoistureQuality::Stale;
            continue;
        }
        if (channelState.hasSampled &&
            static_cast<std::uint64_t>(sampleStartUs) -
                    channelState.lastSampleMicros <
                kMinimumSampleSpacingUs) {
            continue;
        }

        const auto pin = static_cast<std::uint8_t>(
            boardPins_.adc.soilMoisture[channelIndex].number);
        std::int32_t rawAdcCount = 0;
        const bool readSucceeded =
            adcReader_.readRaw(pin, rawAdcCount);
        const std::int64_t sampleEndUs = esp_timer_get_time();
        if (sampleEndUs < sampleStartUs) {
            lastStatus_ = SoilMoistureStatus::ClockError;
            return lastStatus_;
        }
        const auto sampleTimestampUs =
            static_cast<std::uint64_t>(sampleEndUs);
        channelState.lastSampleMicros = sampleTimestampUs;
        channelState.hasSampled = true;

        if (!readSucceeded) {
            resetChannel(channelIndex);
            channelState.lastSampleMicros = sampleTimestampUs;
            channelState.hasSampled = true;
            channelState.quality =
                project::models::SoilMoistureQuality::AdcReadFailed;
            channelError = SoilMoistureStatus::AdcReadFailed;
            continue;
        }
        if (rawAdcCount < 0 ||
            static_cast<std::uint32_t>(rawAdcCount) >
                config_.adcMaximumCount) {
            resetChannel(channelIndex);
            channelState.rawAdcCount = rawAdcCount;
            channelState.lastSampleMicros = sampleTimestampUs;
            channelState.hasSampled = true;
            channelState.quality =
                project::models::SoilMoistureQuality::AdcValueOutOfRange;
            channelError = SoilMoistureStatus::AdcValueOutOfRange;
            continue;
        }

        if (channelState.sampleCount == kSamplesPerChannel) {
            channelState.sampleSum -= static_cast<std::uint32_t>(
                channelState.samples[channelState.nextSampleIndex]);
        } else {
            ++channelState.sampleCount;
        }
        channelState.samples[channelState.nextSampleIndex] = rawAdcCount;
        channelState.sampleSum += static_cast<std::uint32_t>(rawAdcCount);
        channelState.nextSampleIndex =
            (channelState.nextSampleIndex + 1U) % kSamplesPerChannel;
        channelState.rawAdcCount = rawAdcCount;
        channelState.lastSuccessfulMicros = sampleTimestampUs;
        channelState.hasSuccessfulSample = true;
        channelState.quality =
            channelState.sampleCount == kSamplesPerChannel
                ? project::models::SoilMoistureQuality::Valid
                : project::models::SoilMoistureQuality::WarmingUp;
    }

    const std::int64_t endTimeUs = esp_timer_get_time();
    if (endTimeUs < 0 ||
        static_cast<std::uint64_t>(endTimeUs) <
            static_cast<std::uint64_t>(startTimeUs)) {
        lastStatus_ = SoilMoistureStatus::ClockError;
        return lastStatus_;
    }
    populateSample(static_cast<std::uint64_t>(endTimeUs), output);

    if (output.valid) {
        lastStatus_ = SoilMoistureStatus::Ok;
    } else {
        lastStatus_ = channelError;
    }
    return lastStatus_;
}

float SoilMoistureDriver::mapAdcToMoisture(
    const std::size_t channelIndex,
    const float adcCount) const noexcept {
    const auto& points = config_.channels[channelIndex].calibrationPoints;
    const float adc0 = static_cast<float>(points[0].adcCount);
    const float adc1 = static_cast<float>(points[1].adcCount);
    const float fraction = (adcCount - adc0) / (adc1 - adc0);
    const float mapped = points[0].moisturePercent +
                         fraction * (points[1].moisturePercent -
                                     points[0].moisturePercent);
    return std::clamp(mapped, 0.0F, 100.0F);
}

void SoilMoistureDriver::resetChannel(
    const std::size_t channelIndex) noexcept {
    channels_[channelIndex] = {};
    channels_[channelIndex].quality =
        config_.channels[channelIndex].present
            ? project::models::SoilMoistureQuality::WarmingUp
            : project::models::SoilMoistureQuality::NotPresent;
}

void SoilMoistureDriver::populateSample(
    const std::uint64_t nowMicros,
    SoilMoistureSample& output) noexcept {
    float moistureSum = 0.0F;
    bool requiredChannelsValid = true;

    for (std::size_t channelIndex = 0;
         channelIndex < project::models::kSoilMoistureProbeCount;
         ++channelIndex) {
        const auto& channelConfig = config_.channels[channelIndex];
        ChannelState& channelState = channels_[channelIndex];
        auto& probe = output.probes[channelIndex];
        if (!channelConfig.present) {
            probe.quality = project::models::SoilMoistureQuality::NotPresent;
            continue;
        }

        probe.rawAdcCount = channelState.rawAdcCount;
        probe.sampleCount =
            static_cast<std::uint8_t>(channelState.sampleCount);
        probe.timestampMs = channelState.lastSampleMicros / 1000U;
        probe.calibrationVersion =
            channelState.hasSuccessfulSample ? config_.calibrationVersion : 0U;
        probe.quality = channelState.quality;

        if (channelState.sampleCount > 0U) {
            probe.filteredAdcCount =
                static_cast<float>(channelState.sampleSum) /
                static_cast<float>(channelState.sampleCount);
            probe.moisturePercent =
                mapAdcToMoisture(channelIndex, probe.filteredAdcCount);
        }

        if (channelState.sampleCount == kSamplesPerChannel &&
            channelState.hasSuccessfulSample) {
            const std::uint64_t maxAgeUs =
                static_cast<std::uint64_t>(config_.maximumSampleAgeMs) *
                1000U;
            const bool timestampValid =
                nowMicros >= channelState.lastSuccessfulMicros;
            const std::uint64_t ageUs =
                timestampValid
                    ? nowMicros - channelState.lastSuccessfulMicros
                    : std::numeric_limits<std::uint64_t>::max();
            if (timestampValid && ageUs <= maxAgeUs) {
                probe.valid = true;
                probe.quality = project::models::SoilMoistureQuality::Valid;
            } else {
                probe.valid = false;
                probe.quality = project::models::SoilMoistureQuality::Stale;
            }
        }

        if (probe.valid) {
            ++output.validChannelCount;
            moistureSum += probe.moisturePercent;
            if (output.rawChannelIndex == 0xFFU) {
                output.rawAdcCount = probe.rawAdcCount;
                output.filteredAdcCount = probe.filteredAdcCount;
                output.filterSampleCount = probe.sampleCount;
                output.rawChannelIndex =
                    static_cast<std::uint8_t>(channelIndex);
            }
        }
        if (channelConfig.required && !probe.valid) {
            requiredChannelsValid = false;
        }
    }

    output.valid =
        output.validChannelCount >= config_.requiredChannelCount &&
        requiredChannelsValid;
    if (output.valid) {
        output.moisturePercent =
            moistureSum / static_cast<float>(output.validChannelCount);
        output.calibrationVersion = config_.calibrationVersion;
    }
}

project::models::SensorStatus SoilMoistureDriver::makeSensorStatus(
    const bool valid) const noexcept {
    project::models::SensorStatus status{};
    status.valid = valid;
    status.health = valid ? project::models::SensorHealth::Healthy
                          : project::models::SensorHealth::Fault;
    return status;
}

SoilMoistureStatus SoilMoistureDriver::lastStatus() const noexcept {
    return lastStatus_;
}

}  // namespace project::drivers
