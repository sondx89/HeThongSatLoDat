#include "project/Drivers/SoilMoistureDriver.h"

#include <algorithm>
#include <cmath>

namespace project::drivers {
namespace {

bool validMoisturePercent(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 100.0F;
}

}  // namespace

SoilMoistureDriver::SoilMoistureDriver(
    IAdcReader& adcReader,
    const project::bsp::BoardPins& boardPins,
    const SoilMoistureDriverConfig& config) noexcept
    : adcReader_(adcReader), boardPins_(boardPins), config_(config) {}

SoilMoistureStatus SoilMoistureDriver::validateConfiguration() const noexcept {
    const auto& pin = boardPins_.adc.soilMoisture;
    if (!pin.isConfigured() || !pin.isWellFormed() ||
        config_.adcMaximumCount == 0U ||
        config_.filterWindowSamples == 0U ||
        config_.filterWindowSamples > kMaxFilterWindowSamples ||
        !config_.adcConfigurationVerified ||
        !config_.calibrationConfigured ||
        config_.calibrationVersion == 0U) {
        return SoilMoistureStatus::InvalidConfiguration;
    }

    const auto& point0 = config_.calibrationPoints[0];
    const auto& point1 = config_.calibrationPoints[1];
    if (point0.adcCount > config_.adcMaximumCount ||
        point1.adcCount > config_.adcMaximumCount ||
        point0.adcCount == point1.adcCount ||
        !validMoisturePercent(point0.moisturePercent) ||
        !validMoisturePercent(point1.moisturePercent) ||
        point0.moisturePercent == point1.moisturePercent) {
        return SoilMoistureStatus::InvalidCalibration;
    }
    return SoilMoistureStatus::Ok;
}

project::models::SensorStatus SoilMoistureDriver::begin() {
    initialized_ = false;
    samples_ = {};
    nextSampleIndex_ = 0;
    sampleCount_ = 0;
    sampleSum_ = 0;
    lastStatus_ = validateConfiguration();
    initialized_ = lastStatus_ == SoilMoistureStatus::Ok;
    return makeSensorStatus(initialized_);
}

project::models::SensorStatus SoilMoistureDriver::read(
    project::models::SensorFrame& output) {
    output = {};
    SoilMoistureSample sample{};
    lastStatus_ = readSample(sample);
    if (lastStatus_ != SoilMoistureStatus::Ok) {
        output.soilStatus = makeSensorStatus(false);
        return output.soilStatus;
    }

    output.soilMoistureRaw = sample.rawAdcCount;
    output.soilMoisturePct = sample.moisturePercent;
    output.calibrationVersion = sample.calibrationVersion;
    output.soilStatus = makeSensorStatus(sample.valid);
    return output.soilStatus;
}

project::models::SensorStatus SoilMoistureDriver::selfTest() {
    if (!initialized_) {
        lastStatus_ = SoilMoistureStatus::NotInitialized;
        return makeSensorStatus(false);
    }

    std::int32_t reading = 0;
    const auto& pin = boardPins_.adc.soilMoisture;
    if (!adcReader_.readRaw(static_cast<std::uint8_t>(pin.number), reading)) {
        lastStatus_ = SoilMoistureStatus::AdcReadFailed;
        return makeSensorStatus(false);
    }
    if (reading < 0 ||
        static_cast<std::uint32_t>(reading) > config_.adcMaximumCount) {
        lastStatus_ = SoilMoistureStatus::AdcValueOutOfRange;
        return makeSensorStatus(false);
    }

    lastStatus_ = SoilMoistureStatus::Ok;
    return makeSensorStatus(true);
}

SoilMoistureStatus SoilMoistureDriver::readSample(
    SoilMoistureSample& output) noexcept {
    output = {};
    if (!initialized_) {
        lastStatus_ = SoilMoistureStatus::NotInitialized;
        return lastStatus_;
    }

    const auto& pin = boardPins_.adc.soilMoisture;
    std::int32_t rawAdcCount = 0;
    if (!adcReader_.readRaw(static_cast<std::uint8_t>(pin.number),
                            rawAdcCount)) {
        samples_ = {};
        nextSampleIndex_ = 0;
        sampleCount_ = 0;
        sampleSum_ = 0;
        lastStatus_ = SoilMoistureStatus::AdcReadFailed;
        return lastStatus_;
    }
    if (rawAdcCount < 0 ||
        static_cast<std::uint32_t>(rawAdcCount) > config_.adcMaximumCount) {
        samples_ = {};
        nextSampleIndex_ = 0;
        sampleCount_ = 0;
        sampleSum_ = 0;
        lastStatus_ = SoilMoistureStatus::AdcValueOutOfRange;
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

    const float filteredAdcCount =
        static_cast<float>(sampleSum_) / static_cast<float>(sampleCount_);
    output.rawAdcCount = rawAdcCount;
    output.filteredAdcCount = filteredAdcCount;
    output.moisturePercent = mapAdcToMoisture(filteredAdcCount);
    output.filterSampleCount = static_cast<std::uint8_t>(sampleCount_);
    output.calibrationVersion = config_.calibrationVersion;
    output.valid = std::isfinite(output.moisturePercent);
    if (!output.valid) {
        output = {};
        lastStatus_ = SoilMoistureStatus::InvalidCalibration;
        return lastStatus_;
    }

    lastStatus_ = SoilMoistureStatus::Ok;
    return lastStatus_;
}

float SoilMoistureDriver::mapAdcToMoisture(
    const float adcCount) const noexcept {
    const auto& point0 = config_.calibrationPoints[0];
    const auto& point1 = config_.calibrationPoints[1];
    const float adc0 = static_cast<float>(point0.adcCount);
    const float adc1 = static_cast<float>(point1.adcCount);
    const float fraction = (adcCount - adc0) / (adc1 - adc0);
    const float mapped = point0.moisturePercent +
                         fraction * (point1.moisturePercent -
                                     point0.moisturePercent);
    const float minPercent =
        std::min(point0.moisturePercent, point1.moisturePercent);
    const float maxPercent =
        std::max(point0.moisturePercent, point1.moisturePercent);
    return std::clamp(mapped, minPercent, maxPercent);
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
