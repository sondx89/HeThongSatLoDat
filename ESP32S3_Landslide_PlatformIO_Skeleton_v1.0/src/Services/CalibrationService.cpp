#include "project/Services/CalibrationService.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace project::services {
namespace {

using project::models::DataQualityState;
using project::models::SensorChannel;
using project::models::SensorFrame;
using project::models::SensorHealth;
using project::models::SensorStatus;

constexpr std::uint32_t kHx710bBaselineWindowMs = 15000U;
constexpr std::uint32_t kHx710bBaselineMinimumSamples = 3U;

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

bool pressureFromRaw(
    const std::int32_t rawValue,
    const project::drivers::Hx710bCalibration& calibration,
    float& pressureKpa) noexcept {
    const auto minimumRaw =
        std::min(calibration.rawZeroCount, calibration.rawFullScaleCount);
    const auto maximumRaw =
        std::max(calibration.rawZeroCount, calibration.rawFullScaleCount);
    if (rawValue < minimumRaw || rawValue > maximumRaw) {
        return false;
    }

    const double rawSpan =
        static_cast<double>(calibration.rawFullScaleCount) -
        static_cast<double>(calibration.rawZeroCount);
    const double fraction =
        (static_cast<double>(rawValue) -
         static_cast<double>(calibration.rawZeroCount)) /
        rawSpan;
    const double mapped =
        static_cast<double>(calibration.zeroPressureKpa) +
        fraction *
            (static_cast<double>(calibration.fullScalePressureKpa) -
             static_cast<double>(calibration.zeroPressureKpa));
    if (!std::isfinite(mapped) ||
        mapped < calibration.minimumPressureKpa ||
        mapped > calibration.maximumPressureKpa) {
        return false;
    }
    pressureKpa = static_cast<float>(mapped);
    return std::isfinite(pressureKpa);
}

bool baselinePolicyIsConfigured(
    const Hx710bBaselinePolicy& policy) noexcept {
    return policy.configured &&
           policy.windowDurationMs == kHx710bBaselineWindowMs &&
           policy.minimumValidSamples >=
               kHx710bBaselineMinimumSamples &&
           policy.maximumSampleAgeMs != 0U;
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
    return mode_ == CalibrationMode::Hx710bPressureOnly
               ? project::models::ApiStatus::Ok
               : project::models::ApiStatus::NotInitialized;
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
    output.hx710bTransducerIdentityId =
        input.hx710bTransducerIdentityId;
    output.hx710bCalibrationVersion =
        input.hx710bCalibrationVersion;
    output.hx710bSampleTimestampMs =
        input.hx710bStatus.timestampMs;

    propagateRawAndSourceQuality(input, output);
    output.hx710bCalibratedValue = 0.0F;
    output.hx710bCalibratedValueValid = false;
    output.hx710bBaselineKpa = 0.0F;
    output.hx710bBaselineValid = false;

    if (input.schemaVersion == 0U) {
        return project::models::ApiStatus::InvalidArgument;
    }
    if (mode_ == CalibrationMode::RawOnly &&
        input.calibrationVersion != 0U) {
        return project::models::ApiStatus::VersionMismatch;
    }

    if (mode_ == CalibrationMode::Hx710bPressureOnly) {
        const auto channelIndex =
            static_cast<std::size_t>(
                SensorChannel::Hx710bCalibratedValue);
        const bool sourceValid =
            sensorDataAvailable(input.hx710bStatus) &&
            input.hx710bRawSampleCount != 0U;
        if (!sourceValid) {
            output.channelQuality[channelIndex] =
                DataQualityState::MISSING;
            return project::models::ApiStatus::NotInitialized;
        }
        if (input.hx710bTransducerIdentityId !=
                hx710bCalibration_.transducerIdentityId ||
            input.hx710bCalibrationVersion !=
                hx710bCalibration_.version) {
            output.channelQuality[channelIndex] =
                DataQualityState::CALIBRATION_INVALID;
            return project::models::ApiStatus::VersionMismatch;
        }

        float pressureKpa = 0.0F;
        if (!pressureFromRaw(input.hx710bRaw, hx710bCalibration_,
                             pressureKpa)) {
            output.channelQuality[channelIndex] =
                DataQualityState::OUT_OF_RANGE;
            return project::models::ApiStatus::InvalidArgument;
        }

        output.hx710bCalibratedValue = pressureKpa;
        output.hx710bCalibratedValueValid = true;
        output.channelQuality[channelIndex] = DataQualityState::VALID;
        const auto baseline =
            hx710bBaseline(input.hx710bStatus.timestampMs);
        output.hx710bBaselineKpa = baseline.pressureKpa;
        output.hx710bBaselineSampleCount =
            baseline.validSampleCount;
        output.hx710bBaselineValid = baseline.valid;
        return project::models::ApiStatus::Ok;
    }

    return project::models::ApiStatus::NotInitialized;
}

CalibrationService::CalibrationService(
    const project::drivers::Hx710bCalibration& hx710bCalibration,
    const Hx710bBaselinePolicy& baselinePolicy) noexcept
    : hx710bCalibration_(hx710bCalibration),
      baselinePolicy_(baselinePolicy),
      mode_(project::drivers::isApprovedHx710bPressureCalibration(
                hx710bCalibration)
                ? CalibrationMode::Hx710bPressureOnly
                : CalibrationMode::RawOnly) {}

Hx710bBaselineStatus CalibrationService::updateHx710bBaseline(
    const project::models::ProcessedFrame& frame,
    const std::uint64_t nowMs) noexcept {
    if (mode_ != CalibrationMode::Hx710bPressureOnly ||
        !baselinePolicyIsConfigured(baselinePolicy_)) {
        return Hx710bBaselineStatus::Disabled;
    }
    if (!frame.hx710bCalibratedValueValid ||
        frame.channelQuality[static_cast<std::size_t>(
            SensorChannel::Hx710bCalibratedValue)] !=
            DataQualityState::VALID ||
        frame.hx710bTransducerIdentityId !=
            hx710bCalibration_.transducerIdentityId ||
        frame.hx710bCalibrationVersion != hx710bCalibration_.version ||
        !std::isfinite(frame.hx710bCalibratedValue) ||
        frame.hx710bCalibratedValue <
            hx710bCalibration_.minimumPressureKpa ||
        frame.hx710bCalibratedValue >
            hx710bCalibration_.maximumPressureKpa ||
        frame.hx710bSampleTimestampMs > nowMs ||
        nowMs - frame.hx710bSampleTimestampMs >
            baselinePolicy_.maximumSampleAgeMs) {
        return hx710bBaseline(nowMs).status ==
                       Hx710bBaselineStatus::Stale
                   ? Hx710bBaselineStatus::Stale
                   : Hx710bBaselineStatus::InsufficientData;
    }
    if (hx710bBaselineSampleCount_ != 0U &&
        frame.hx710bSampleTimestampMs <= hx710bBaselineLastSampleMs_) {
        return hx710bBaseline(nowMs).status;
    }
    if (hx710bBaselineSampleCount_ != 0U &&
        frame.hx710bSampleTimestampMs -
                hx710bBaselineLastSampleMs_ >
            baselinePolicy_.maximumSampleAgeMs) {
        resetHx710bBaseline();
    }
    if (hx710bBaselineReady_) {
        return hx710bBaseline(nowMs).status;
    }

    if (hx710bBaselineSampleCount_ == 0U) {
        hx710bBaselineStartMs_ = frame.hx710bSampleTimestampMs;
        hx710bBaselineSumKpa_ = 0.0;
    }
    hx710bBaselineSumKpa_ += frame.hx710bCalibratedValue;
    ++hx710bBaselineSampleCount_;
    hx710bBaselineLastSampleMs_ = frame.hx710bSampleTimestampMs;
    hx710bBaselineReady_ =
        hx710bBaselineSampleCount_ >=
            baselinePolicy_.minimumValidSamples &&
        frame.hx710bSampleTimestampMs >= hx710bBaselineStartMs_ &&
        frame.hx710bSampleTimestampMs - hx710bBaselineStartMs_ >=
            baselinePolicy_.windowDurationMs;
    return hx710bBaselineReady_ ? Hx710bBaselineStatus::Ready
                                : Hx710bBaselineStatus::InsufficientData;
}

Hx710bBaselineSnapshot CalibrationService::hx710bBaseline(
    const std::uint64_t nowMs) const noexcept {
    Hx710bBaselineSnapshot snapshot{};
    if (mode_ != CalibrationMode::Hx710bPressureOnly ||
        !baselinePolicyIsConfigured(baselinePolicy_)) {
        snapshot.status = Hx710bBaselineStatus::Disabled;
        return snapshot;
    }
    snapshot.validSampleCount = hx710bBaselineSampleCount_;
    snapshot.windowStartMs = hx710bBaselineStartMs_;
    snapshot.lastSampleMs = hx710bBaselineLastSampleMs_;
    if (!hx710bBaselineReady_ ||
        hx710bBaselineSampleCount_ == 0U) {
        snapshot.status = Hx710bBaselineStatus::InsufficientData;
        return snapshot;
    }
    if (nowMs < hx710bBaselineLastSampleMs_ ||
        nowMs - hx710bBaselineLastSampleMs_ >
            baselinePolicy_.maximumSampleAgeMs) {
        snapshot.status = Hx710bBaselineStatus::Stale;
        return snapshot;
    }
    snapshot.pressureKpa = static_cast<float>(
        hx710bBaselineSumKpa_ /
        static_cast<double>(hx710bBaselineSampleCount_));
    snapshot.valid = std::isfinite(snapshot.pressureKpa);
    snapshot.status = snapshot.valid ? Hx710bBaselineStatus::Ready
                                    : Hx710bBaselineStatus::InsufficientData;
    return snapshot;
}

void CalibrationService::resetHx710bBaseline() noexcept {
    hx710bBaselineSumKpa_ = 0.0;
    hx710bBaselineSampleCount_ = 0U;
    hx710bBaselineStartMs_ = 0U;
    hx710bBaselineLastSampleMs_ = 0U;
    hx710bBaselineReady_ = false;
}

}  // namespace project::services
