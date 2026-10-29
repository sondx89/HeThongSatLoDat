#include "project/Drivers/RainSensorDriver.h"

#include <algorithm>
#include <cmath>

namespace project::drivers {
namespace {

bool validPercent(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 100.0F;
}

}  // namespace

RainSensorDriver::RainSensorDriver(
    IAdcReader& adcReader,
    const project::bsp::BoardPins& boardPins,
    const RainSensorDriverConfig& config) noexcept
    : adcReader_(adcReader), boardPins_(boardPins), config_(config) {}

RainSensorStatus RainSensorDriver::validateConfiguration() const noexcept {
    const auto& pin = boardPins_.adc.rainSensor;
    if (!pin.isConfigured() || !pin.isWellFormed() ||
        config_.adcMaximumCount == 0U ||
        config_.filterWindowSamples == 0U ||
        config_.filterWindowSamples > kMaxFilterWindowSamples ||
        config_.calibrationVersion == 0U ||
        !config_.adcConfigurationVerified ||
        !config_.wetnessSemanticsVerified ||
        !config_.calibrationConfigured) {
        return RainSensorStatus::InvalidConfiguration;
    }

    const auto& point0 = config_.calibrationPoints[0];
    const auto& point1 = config_.calibrationPoints[1];
    if (point0.adcCount > config_.adcMaximumCount ||
        point1.adcCount > config_.adcMaximumCount ||
        point0.adcCount == point1.adcCount ||
        !validPercent(point0.wetnessPercent) ||
        !validPercent(point1.wetnessPercent) ||
        point0.wetnessPercent == point1.wetnessPercent) {
        return RainSensorStatus::InvalidCalibration;
    }
    return RainSensorStatus::Ok;
}

project::models::SensorStatus RainSensorDriver::begin() {
    initialized_ = false;
    clearFilter();
    lastStatus_ = validateConfiguration();
    initialized_ = lastStatus_ == RainSensorStatus::Ok;
    return makeSensorStatus(initialized_);
}

project::models::SensorStatus RainSensorDriver::read(
    project::models::SensorFrame& output) {
    output = {};
    RainSensorSample sample{};
    lastStatus_ = readSample(sample);
    if (lastStatus_ != RainSensorStatus::Ok) {
        output.rainStatus = makeSensorStatus(false);
        return output.rainStatus;
    }

    output.rainRaw = sample.rawAdcCount;
    output.rainWetnessRelativePct = sample.wetnessRelativePercent;
    output.rainWetnessValid = sample.valid;
    output.calibrationVersion = sample.calibrationVersion;
    output.rainStatus =
        makeSensorStatus(sample.valid, sample.qualityFlags);
    return output.rainStatus;
}

project::models::SensorStatus RainSensorDriver::selfTest() {
    if (!initialized_) {
        lastStatus_ = RainSensorStatus::NotInitialized;
        return makeSensorStatus(false);
    }

    std::int32_t reading = 0;
    const auto& pin = boardPins_.adc.rainSensor;
    if (!adcReader_.readRaw(static_cast<std::uint8_t>(pin.number), reading)) {
        lastStatus_ = RainSensorStatus::AdcReadFailed;
        return makeSensorStatus(false);
    }
    if (reading < 0 ||
        static_cast<std::uint32_t>(reading) > config_.adcMaximumCount) {
        lastStatus_ = RainSensorStatus::AdcValueOutOfRange;
        return makeSensorStatus(false);
    }

    lastStatus_ = RainSensorStatus::Ok;
    return makeSensorStatus(true,
                            rainQualityMask(RainQualityFlag::AdcRangeValid));
}

RainSensorStatus RainSensorDriver::readSample(
    RainSensorSample& output) noexcept {
    output = {};
    if (!initialized_) {
        lastStatus_ = RainSensorStatus::NotInitialized;
        return lastStatus_;
    }

    std::int32_t rawAdcCount = 0;
    const auto& pin = boardPins_.adc.rainSensor;
    if (!adcReader_.readRaw(static_cast<std::uint8_t>(pin.number),
                            rawAdcCount)) {
        clearFilter();
        lastStatus_ = RainSensorStatus::AdcReadFailed;
        return lastStatus_;
    }
    if (rawAdcCount < 0 ||
        static_cast<std::uint32_t>(rawAdcCount) > config_.adcMaximumCount) {
        clearFilter();
        lastStatus_ = RainSensorStatus::AdcValueOutOfRange;
        return lastStatus_;
    }

    if (sampleCount_ == config_.filterWindowSamples) {
        sampleSum_ -= static_cast<std::uint32_t>(samples_[nextSampleIndex_]);
    } else {
        ++sampleCount_;
    }
    samples_[nextSampleIndex_] = rawAdcCount;
    sampleSum_ += static_cast<std::uint32_t>(rawAdcCount);
    nextSampleIndex_ =
        (nextSampleIndex_ + 1U) % config_.filterWindowSamples;

    output.rawAdcCount = rawAdcCount;
    output.filteredAdcCount =
        static_cast<float>(sampleSum_) / static_cast<float>(sampleCount_);
    output.filterSampleCount = static_cast<std::uint8_t>(sampleCount_);
    output.calibrationVersion = config_.calibrationVersion;
    output.qualityFlags = rainQualityMask(RainQualityFlag::AdcRangeValid) |
                          rainQualityMask(RainQualityFlag::CalibrationValid);
    if (sampleCount_ < config_.filterWindowSamples) {
        output.qualityFlags |=
            rainQualityMask(RainQualityFlag::FilterWindowWarming);
    }
    output.wetnessRelativePercent =
        mapAdcToWetness(output.filteredAdcCount, output.qualityFlags);
    output.valid = std::isfinite(output.wetnessRelativePercent);
    if (!output.valid) {
        output = {};
        clearFilter();
        lastStatus_ = RainSensorStatus::InvalidCalibration;
        return lastStatus_;
    }

    lastStatus_ = RainSensorStatus::Ok;
    return lastStatus_;
}

float RainSensorDriver::mapAdcToWetness(
    const float adcCount,
    std::uint32_t& qualityFlags) const noexcept {
    const auto& point0 = config_.calibrationPoints[0];
    const auto& point1 = config_.calibrationPoints[1];
    const float adc0 = static_cast<float>(point0.adcCount);
    const float adc1 = static_cast<float>(point1.adcCount);
    const float fraction = (adcCount - adc0) / (adc1 - adc0);
    const float mapped = point0.wetnessPercent +
                         fraction * (point1.wetnessPercent -
                                     point0.wetnessPercent);
    const float minWetness =
        std::min(point0.wetnessPercent, point1.wetnessPercent);
    const float maxWetness =
        std::max(point0.wetnessPercent, point1.wetnessPercent);
    if (mapped < minWetness) {
        qualityFlags |= rainQualityMask(RainQualityFlag::WetnessClampedLow);
        return minWetness;
    }
    if (mapped > maxWetness) {
        qualityFlags |= rainQualityMask(RainQualityFlag::WetnessClampedHigh);
        return maxWetness;
    }
    return mapped;
}

void RainSensorDriver::clearFilter() noexcept {
    samples_ = {};
    nextSampleIndex_ = 0;
    sampleCount_ = 0;
    sampleSum_ = 0;
}

project::models::SensorStatus RainSensorDriver::makeSensorStatus(
    const bool valid,
    const std::uint32_t qualityFlags) const noexcept {
    project::models::SensorStatus status{};
    status.valid = valid;
    status.health = valid ? project::models::SensorHealth::Healthy
                          : project::models::SensorHealth::Fault;
    status.qualityFlags = qualityFlags;
    return status;
}

RainSensorStatus RainSensorDriver::lastStatus() const noexcept {
    return lastStatus_;
}

}  // namespace project::drivers
