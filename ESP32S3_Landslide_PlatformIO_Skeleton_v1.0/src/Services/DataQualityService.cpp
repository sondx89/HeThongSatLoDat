#include "project/Services/DataQualityService.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_timer.h>
#else
#include <chrono>
#endif

namespace project::services {
namespace {

using project::models::DataQualityState;
using project::models::SensorChannel;
using project::models::SensorFrame;
using project::models::SensorStatus;

constexpr std::size_t channelIndex(SensorChannel channel) noexcept {
    return static_cast<std::size_t>(channel);
}

constexpr std::size_t sensorIndex(QualitySensor sensor) noexcept {
    return static_cast<std::size_t>(sensor);
}

bool hasProcessedField(SensorChannel channel) noexcept {
    switch (channel) {
        case SensorChannel::AccelXG:
        case SensorChannel::AccelYG:
        case SensorChannel::AccelZG:
        case SensorChannel::GyroXDps:
        case SensorChannel::GyroYDps:
        case SensorChannel::GyroZDps:
        case SensorChannel::SoilMoisturePct:
        case SensorChannel::RainWetnessRelativePct:
            return true;
        case SensorChannel::ImuAccelXRaw:
        case SensorChannel::ImuAccelYRaw:
        case SensorChannel::ImuAccelZRaw:
        case SensorChannel::ImuGyroXRaw:
        case SensorChannel::ImuGyroYRaw:
        case SensorChannel::ImuGyroZRaw:
        case SensorChannel::TiltAngleDeg:
        case SensorChannel::SoilMoistureRaw:
        case SensorChannel::RainRaw:
        case SensorChannel::RainRateMmH:
        case SensorChannel::Hx710bRaw:
        case SensorChannel::Hx710bCalibratedValue:
        case SensorChannel::Count:
            return false;
    }
    return false;
}

std::uint64_t monotonicMilliseconds() noexcept {
#if defined(ARDUINO_ARCH_ESP32)
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
#else
    using Clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch())
            .count());
#endif
}

}  // namespace

DataQualityService::DataQualityService(
    const DataQualityConfig& config) noexcept
    : config_(config) {}

bool DataQualityService::validateConfiguration() const noexcept {
    if (!config_.requiredChannelsConfigured) {
        return false;
    }

    bool hasRequiredChannel = false;
    for (std::size_t i = 0; i < config_.requiredChannels.size(); ++i) {
        if (config_.requiredChannels[i]) {
            hasRequiredChannel = true;
            if (!hasProcessedField(static_cast<SensorChannel>(i))) {
                return false;
            }
        }
        const QualityRange& range = config_.ranges[i];
        if (range.configured &&
            (!std::isfinite(range.minimum) ||
             !std::isfinite(range.maximum) ||
             range.minimum > range.maximum)) {
            return false;
        }
    }
    if (!hasRequiredChannel) {
        return false;
    }

    for (const SensorFreshnessLimit& limit : config_.freshness) {
        if (!limit.configured) {
            return false;
        }
    }
    return true;
}

DataQualityState DataQualityService::classify(
    const SensorFrame& frame,
    SensorChannel channel,
    double value,
    bool fieldPresent,
    std::uint64_t nowMs) const noexcept {
    const SensorStatus* status = nullptr;
    QualitySensor sensor = QualitySensor::Imu;

    switch (channel) {
        case SensorChannel::ImuAccelXRaw:
        case SensorChannel::ImuAccelYRaw:
        case SensorChannel::ImuAccelZRaw:
        case SensorChannel::ImuGyroXRaw:
        case SensorChannel::ImuGyroYRaw:
        case SensorChannel::ImuGyroZRaw:
        case SensorChannel::AccelXG:
        case SensorChannel::AccelYG:
        case SensorChannel::AccelZG:
        case SensorChannel::GyroXDps:
        case SensorChannel::GyroYDps:
        case SensorChannel::GyroZDps:
        case SensorChannel::TiltAngleDeg:
            status = &frame.imuStatus;
            sensor = QualitySensor::Imu;
            break;
        case SensorChannel::SoilMoistureRaw:
        case SensorChannel::SoilMoisturePct:
            status = &frame.soilStatus;
            sensor = QualitySensor::Soil;
            break;
        case SensorChannel::RainRaw:
        case SensorChannel::RainWetnessRelativePct:
        case SensorChannel::RainRateMmH:
            status = &frame.rainStatus;
            sensor = QualitySensor::Rain;
            break;
        case SensorChannel::Hx710bRaw:
        case SensorChannel::Hx710bCalibratedValue:
            status = &frame.hx710bStatus;
            sensor = QualitySensor::Hx710b;
            break;
        case SensorChannel::Count:
            return DataQualityState::MISSING;
    }

    if (!fieldPresent || status == nullptr || !status->valid ||
        status->health == project::models::SensorHealth::Fault) {
        return DataQualityState::MISSING;
    }

    const SensorFreshnessLimit& freshness =
        config_.freshness[sensorIndex(sensor)];
    if (status->timestampMs > nowMs ||
        nowMs - status->timestampMs > freshness.maximumAgeMs) {
        return DataQualityState::STALE;
    }
    if (!std::isfinite(value)) {
        return DataQualityState::NON_FINITE;
    }

    const QualityRange& range = config_.ranges[channelIndex(channel)];
    if (range.configured &&
        (value < range.minimum || value > range.maximum)) {
        return DataQualityState::OUT_OF_RANGE;
    }
    return DataQualityState::VALID;
}

bool DataQualityService::requiredChannelsAreValid(
    const project::models::ProcessedFrame& frame) const noexcept {
    if (!config_.requiredChannelsConfigured) {
        return false;
    }
    bool hasRequiredChannel = false;
    for (std::size_t i = 0; i < config_.requiredChannels.size(); ++i) {
        if (!config_.requiredChannels[i]) {
            continue;
        }
        hasRequiredChannel = true;
        if (frame.channelQuality[i] != DataQualityState::VALID) {
            return false;
        }
    }
    return hasRequiredChannel;
}

DataQualityServiceStatus DataQualityService::process(
    const SensorFrame& input,
    std::uint64_t nowMs,
    project::models::ProcessedFrame& output) const noexcept {
    output = {};
    output.schemaVersion = input.schemaVersion;
    output.bootId = input.bootId;
    output.sequence = input.sequence;
    output.timestampMs = input.timestampMs;
    output.qualityFlags = input.qualityFlags;
    output.sensorStatusBits = input.sensorStatusBits;
    output.calibrationVersion = input.calibrationVersion;
    output.channelQuality.fill(DataQualityState::MISSING);

    const auto assess = [this, &input, nowMs, &output](
                            SensorChannel channel, double value,
                            bool fieldPresent) noexcept {
        output.channelQuality[channelIndex(channel)] =
            classify(input, channel, value, fieldPresent, nowMs);
    };
    assess(SensorChannel::ImuAccelXRaw, input.imu.accelX,
           input.imuStatus.valid);
    assess(SensorChannel::ImuAccelYRaw, input.imu.accelY,
           input.imuStatus.valid);
    assess(SensorChannel::ImuAccelZRaw, input.imu.accelZ,
           input.imuStatus.valid);
    assess(SensorChannel::ImuGyroXRaw, input.imu.gyroX,
           input.imuStatus.valid);
    assess(SensorChannel::ImuGyroYRaw, input.imu.gyroY,
           input.imuStatus.valid);
    assess(SensorChannel::ImuGyroZRaw, input.imu.gyroZ,
           input.imuStatus.valid);

    const auto copyFloat = [&assess, &output](
                              SensorChannel channel, float value,
                              bool fieldPresent,
                              float& destination) noexcept {
        assess(channel, static_cast<double>(value), fieldPresent);
        if (output.channelQuality[channelIndex(channel)] ==
            DataQualityState::VALID) {
            destination = value;
        }
    };

    copyFloat(SensorChannel::AccelXG, input.imuPhysical.accelXG,
              input.imuStatus.valid, output.accelXG);
    copyFloat(SensorChannel::AccelYG, input.imuPhysical.accelYG,
              input.imuStatus.valid, output.accelYG);
    copyFloat(SensorChannel::AccelZG, input.imuPhysical.accelZG,
              input.imuStatus.valid, output.accelZG);
    copyFloat(SensorChannel::GyroXDps, input.imuPhysical.gyroXDps,
              input.imuStatus.valid, output.gyroXDps);
    copyFloat(SensorChannel::GyroYDps, input.imuPhysical.gyroYDps,
              input.imuStatus.valid, output.gyroYDps);
    copyFloat(SensorChannel::GyroZDps, input.imuPhysical.gyroZDps,
              input.imuStatus.valid, output.gyroZDps);

    assess(SensorChannel::TiltAngleDeg, input.tiltAngleDeg,
           input.tiltAngleValid);

    assess(SensorChannel::SoilMoistureRaw, input.soilMoistureRaw,
           input.soilStatus.valid);
    copyFloat(SensorChannel::SoilMoisturePct, input.soilMoisturePct,
              input.calibrationVersion != 0, output.soilMoisturePct);

    assess(SensorChannel::RainRaw, input.rainRaw, input.rainStatus.valid);
    copyFloat(SensorChannel::RainWetnessRelativePct,
              input.rainWetnessRelativePct, input.rainWetnessValid,
              output.rainWetnessRelativePct);
    assess(SensorChannel::RainRateMmH, input.rainRateMmH, false);

    assess(SensorChannel::Hx710bRaw, input.hx710bRaw,
           input.hx710bStatus.valid);
    // Transducer and physical measurand are TBD; raw scaling is not a
    // calibrated physical value.
    assess(SensorChannel::Hx710bCalibratedValue,
           input.hx710bCalibratedValue, false);

    const bool configurationValid = validateConfiguration();
    const bool requiredValid =
        configurationValid && requiredChannelsAreValid(output);
    output.aggregateStatus.valid = requiredValid;
    output.aggregateStatus.health =
        requiredValid ? project::models::SensorHealth::Healthy
                      : project::models::SensorHealth::Fault;
    output.aggregateStatus.timestampMs = nowMs;

    return configurationValid
               ? DataQualityServiceStatus::Ok
               : DataQualityServiceStatus::InvalidConfiguration;
}

bool DataQualityService::process(
    const SensorFrame& input,
    project::models::ProcessedFrame& output) const noexcept {
    return process(input, monotonicMilliseconds(), output) ==
               DataQualityServiceStatus::Ok &&
           requiredChannelsAreValid(output);
}

bool DataQualityService::validate(const SensorFrame& frame) const noexcept {
    project::models::ProcessedFrame output{};
    return process(frame, output);
}

bool DataQualityService::validate(const SensorFrame& frame,
                                  std::uint64_t nowMs) const noexcept {
    project::models::ProcessedFrame output{};
    return process(frame, nowMs, output) == DataQualityServiceStatus::Ok &&
           requiredChannelsAreValid(output);
}

bool DataQualityService::validate(
    project::models::ProcessedFrame& frame) const noexcept {
    if (!validateConfiguration()) {
        return false;
    }

    const auto assessProcessed =
        [this, &frame](SensorChannel channel, float value) noexcept {
        DataQualityState& state =
            frame.channelQuality[channelIndex(channel)];
        if (state != DataQualityState::VALID) {
            return;
        }
        if (!std::isfinite(value)) {
            state = DataQualityState::NON_FINITE;
            return;
        }
        const QualityRange& range = config_.ranges[channelIndex(channel)];
        if (range.configured &&
            (static_cast<double>(value) < range.minimum ||
             static_cast<double>(value) > range.maximum)) {
            state = DataQualityState::OUT_OF_RANGE;
        }
    };
    assessProcessed(SensorChannel::AccelXG, frame.accelXG);
    assessProcessed(SensorChannel::AccelYG, frame.accelYG);
    assessProcessed(SensorChannel::AccelZG, frame.accelZG);
    assessProcessed(SensorChannel::GyroXDps, frame.gyroXDps);
    assessProcessed(SensorChannel::GyroYDps, frame.gyroYDps);
    assessProcessed(SensorChannel::GyroZDps, frame.gyroZDps);
    assessProcessed(SensorChannel::SoilMoisturePct, frame.soilMoisturePct);
    assessProcessed(SensorChannel::RainWetnessRelativePct,
                    frame.rainWetnessRelativePct);

    const float values[] = {
        frame.accelXG,
        frame.accelYG,
        frame.accelZG,
        frame.gyroXDps,
        frame.gyroYDps,
        frame.gyroZDps,
        frame.tiltRollDeg,
        frame.tiltPitchDeg,
        frame.accelMagnitude,
        frame.soilMoisturePct,
        frame.rainWetnessRelativePct,
        frame.rainRateMmH,
        frame.hx710bCalibratedValue};
    for (float value : values) {
        if (!std::isfinite(value)) {
            frame.aggregateStatus.valid = false;
            frame.aggregateStatus.health =
                project::models::SensorHealth::Fault;
            return false;
        }
    }

    const bool valid = requiredChannelsAreValid(frame);
    frame.aggregateStatus.valid = valid;
    frame.aggregateStatus.health =
        valid ? project::models::SensorHealth::Healthy
              : project::models::SensorHealth::Fault;
    return valid;
}

bool DataQualityService::validate(
    const project::models::ProcessedFrame& frame) const noexcept {
    project::models::ProcessedFrame assessed = frame;
    return validate(assessed);
}

}  // namespace project::services
