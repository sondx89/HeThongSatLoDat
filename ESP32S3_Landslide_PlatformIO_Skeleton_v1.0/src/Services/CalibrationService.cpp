#include "project/Services/CalibrationService.h"

#include <cmath>
#include <cstddef>

namespace project::services {
namespace {

using project::models::DataQualityState;
using project::models::SensorChannel;
using project::models::SensorFrame;
using project::models::SensorHealth;
using project::models::SensorStatus;

constexpr std::size_t channelIndex(SensorChannel channel) noexcept {
    return static_cast<std::size_t>(channel);
}

bool sensorDataAvailable(const SensorStatus& status) noexcept {
    if (!status.valid) {
        return false;
    }

    switch (status.health) {
        case SensorHealth::Healthy:
        case SensorHealth::Degraded:
            return true;
        case SensorHealth::Unknown:
        case SensorHealth::Fault:
            return false;
    }
    return false;
}

DataQualityState sourceQuality(const SensorStatus& status, double value,
                              bool present) noexcept {
    if (!present || !sensorDataAvailable(status)) {
        return DataQualityState::MISSING;
    }
    return std::isfinite(value) ? DataQualityState::VALID
                                : DataQualityState::NON_FINITE;
}

DataQualityState unconfiguredProcessedQuality(const SensorStatus& status,
                                              double value,
                                              bool present) noexcept {
    const DataQualityState quality = sourceQuality(status, value, present);
    return quality == DataQualityState::VALID ? DataQualityState::MISSING
                                              : quality;
}

void setQuality(project::models::ProcessedFrame& output,
                SensorChannel channel, DataQualityState quality) noexcept {
    output.channelQuality[channelIndex(channel)] = quality;
}

void propagateRawAndSourceQuality(const SensorFrame& input,
                                  project::models::ProcessedFrame& output)
    noexcept {
    output.channelQuality.fill(DataQualityState::MISSING);

    const SensorStatus& imu = input.imuStatus;
    setQuality(output, SensorChannel::ImuAccelXRaw,
               sourceQuality(imu, input.imu.accelX, true));
    setQuality(output, SensorChannel::ImuAccelYRaw,
               sourceQuality(imu, input.imu.accelY, true));
    setQuality(output, SensorChannel::ImuAccelZRaw,
               sourceQuality(imu, input.imu.accelZ, true));
    setQuality(output, SensorChannel::ImuGyroXRaw,
               sourceQuality(imu, input.imu.gyroX, true));
    setQuality(output, SensorChannel::ImuGyroYRaw,
               sourceQuality(imu, input.imu.gyroY, true));
    setQuality(output, SensorChannel::ImuGyroZRaw,
               sourceQuality(imu, input.imu.gyroZ, true));

    // These are candidate processed values. Without approved conversion
    // metadata they remain ineligible, even when the source is finite.
    setQuality(output, SensorChannel::AccelXG,
               unconfiguredProcessedQuality(imu, input.imuPhysical.accelXG,
                                            true));
    setQuality(output, SensorChannel::AccelYG,
               unconfiguredProcessedQuality(imu, input.imuPhysical.accelYG,
                                            true));
    setQuality(output, SensorChannel::AccelZG,
               unconfiguredProcessedQuality(imu, input.imuPhysical.accelZG,
                                            true));
    setQuality(output, SensorChannel::GyroXDps,
               unconfiguredProcessedQuality(imu, input.imuPhysical.gyroXDps,
                                            true));
    setQuality(output, SensorChannel::GyroYDps,
               unconfiguredProcessedQuality(imu, input.imuPhysical.gyroYDps,
                                            true));
    setQuality(output, SensorChannel::GyroZDps,
               unconfiguredProcessedQuality(imu, input.imuPhysical.gyroZDps,
                                            true));
    setQuality(output, SensorChannel::TiltAngleDeg,
               unconfiguredProcessedQuality(imu, input.tiltAngleDeg,
                                            input.tiltAngleValid));

    const SensorStatus& soil = input.soilStatus;
    setQuality(output, SensorChannel::SoilMoistureRaw,
               sourceQuality(soil, input.soilMoistureRaw, true));
    setQuality(output, SensorChannel::SoilMoisturePct,
               unconfiguredProcessedQuality(soil, input.soilMoisturePct,
                                            true));

    const SensorStatus& rain = input.rainStatus;
    setQuality(output, SensorChannel::RainRaw,
               sourceQuality(rain, input.rainRaw, true));
    setQuality(output, SensorChannel::RainWetnessRelativePct,
               unconfiguredProcessedQuality(rain,
                                            input.rainWetnessRelativePct,
                                            input.rainWetnessValid));
    setQuality(output, SensorChannel::RainRateMmH,
               unconfiguredProcessedQuality(rain, input.rainRateMmH, true));

    const SensorStatus& hx710b = input.hx710bStatus;
    setQuality(output, SensorChannel::Hx710bRaw,
               sourceQuality(hx710b, input.hx710bRaw, true));
    const DataQualityState hxInputQuality =
        unconfiguredProcessedQuality(hx710b,
                                     input.hx710bCalibratedValue, true);
    const bool validFactorIsNonFinite =
        sensorDataAvailable(hx710b) &&
        ((input.hx710bCalibrationFactorValid &&
          !std::isfinite(input.hx710bCalibrationFactor)) ||
         (input.hx710bFactorScaledRawValid &&
          !std::isfinite(input.hx710bFactorScaledRaw)));
    setQuality(output, SensorChannel::Hx710bCalibratedValue,
               validFactorIsNonFinite
                   ? DataQualityState::NON_FINITE
                   : hxInputQuality);
}

}  // namespace

bool CalibrationService::loadCalibration() {
    return loadCalibrationStatus() == project::models::ApiStatus::Ok;
}

project::models::ApiStatus CalibrationService::loadCalibrationStatus()
    const noexcept {
    return project::models::ApiStatus::NotInitialized;
}

bool CalibrationService::process(
    const project::models::SensorFrame& input,
    project::models::ProcessedFrame& output) {
    return processStatus(input, output) == project::models::ApiStatus::Ok;
}

project::models::ApiStatus CalibrationService::processStatus(
    const project::models::SensorFrame& input,
    project::models::ProcessedFrame& output) const noexcept {
    output = {};
    output.schemaVersion = input.schemaVersion;
    output.bootId = input.bootId;
    output.sequence = input.sequence;
    output.timestampMs = input.timestampMs;
    output.qualityFlags = input.qualityFlags;
    output.sensorStatusBits = input.sensorStatusBits;
    output.calibrationVersion = input.calibrationVersion;

    propagateRawAndSourceQuality(input, output);

    if (input.schemaVersion == 0U) {
        return project::models::ApiStatus::InvalidArgument;
    }
    if (input.calibrationVersion != 0U) {
        return project::models::ApiStatus::VersionMismatch;
    }
    return project::models::ApiStatus::NotInitialized;
}

}  // namespace project::services
