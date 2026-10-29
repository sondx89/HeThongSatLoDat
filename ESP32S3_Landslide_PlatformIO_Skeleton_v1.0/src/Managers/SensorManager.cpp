#include "project/Managers/SensorManager.h"

#include <limits>

namespace project::managers {

SensorManager::SensorManager(project::drivers::ISensor& imu,
                             project::drivers::ISensor& soil,
                             project::drivers::ISensor& rain,
                             project::drivers::ISensor& hx710b,
                             const SensorManagerConfig& config) noexcept
    : imu_(imu),
      soil_(soil),
      rain_(rain),
      hx710b_(hx710b),
      config_(config) {}

bool SensorManager::beginAll() noexcept {
    ready_ = config_.samplePeriodMicros != 0U;
    sampleDue_ = false;
    firstPoll_ = true;
    nextSequence_ = 0;
    if (!ready_) {
        return false;
    }

    const project::models::SensorStatus imuStatus = imu_.begin();
    const project::models::SensorStatus soilStatus = soil_.begin();
    const project::models::SensorStatus rainStatus = rain_.begin();
    const project::models::SensorStatus hx710bStatus = hx710b_.begin();
    ready_ = imuStatus.valid && soilStatus.valid && rainStatus.valid &&
             hx710bStatus.valid;
    return ready_;
}

bool SensorManager::pollDue(std::uint64_t nowMicros) noexcept {
    if (!ready_ || sampleDue_) {
        return false;
    }
    if (!firstPoll_) {
        if (nowMicros < dueTimestampMicros_ ||
            nowMicros - dueTimestampMicros_ < config_.samplePeriodMicros) {
            return false;
        }
    }

    dueTimestampMicros_ = nowMicros;
    firstPoll_ = false;
    sampleDue_ = true;
    return true;
}

bool SensorManager::readSnapshot(
    project::models::SensorFrame& output) noexcept {
    output = {};
    if (!ready_ || !sampleDue_ ||
        nextSequence_ == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    sampleDue_ = false;

    project::models::SensorFrame imuFrame{};
    project::models::SensorFrame soilFrame{};
    project::models::SensorFrame rainFrame{};
    project::models::SensorFrame hx710bFrame{};

    const project::models::SensorStatus imuStatus = imu_.read(imuFrame);
    const project::models::SensorStatus soilStatus = soil_.read(soilFrame);
    const project::models::SensorStatus rainStatus = rain_.read(rainFrame);
    const project::models::SensorStatus hx710bStatus = hx710b_.read(hx710bFrame);

    output.bootId = config_.bootId;
    output.sequence = ++nextSequence_;
    output.timestampMs = dueTimestampMicros_ / 1000U;
    output.samplePeriodMs =
        static_cast<std::uint32_t>(config_.samplePeriodMicros / 1000U);
    output.timeStatus = project::models::TimeStatus::Unsynced;
    output.imuStatus = imuStatus;
    output.soilStatus = soilStatus;
    output.rainStatus = rainStatus;
    output.hx710bStatus = hx710bStatus;

    if (imuStatus.valid) {
        output.imu = imuFrame.imu;
        output.imuPhysical = imuFrame.imuPhysical;
        output.tiltAngleDeg = imuFrame.tiltAngleDeg;
        output.tiltAngleValid = imuFrame.tiltAngleValid;
    }
    if (soilStatus.valid) {
        output.soilMoistureRaw = soilFrame.soilMoistureRaw;
        output.soilMoisturePct = soilFrame.soilMoisturePct;
        output.calibrationVersion = soilFrame.calibrationVersion;
    }
    if (rainStatus.valid) {
        output.rainRaw = rainFrame.rainRaw;
        output.rainWetnessRelativePct =
            rainFrame.rainWetnessRelativePct;
        output.rainWetnessValid = rainFrame.rainWetnessValid;
        output.rainRateMmH = rainFrame.rainRateMmH;
        if (rainFrame.calibrationVersion != 0U) {
            output.calibrationVersion = rainFrame.calibrationVersion;
        }
    }
    if (hx710bStatus.valid) {
        output.hx710bRaw = hx710bFrame.hx710bRaw;
        output.hx710bCalibratedValue = hx710bFrame.hx710bCalibratedValue;
    }

    const auto statusTimestamp = output.timestampMs;
    output.imuStatus.timestampMs = statusTimestamp;
    output.soilStatus.timestampMs = statusTimestamp;
    output.rainStatus.timestampMs = statusTimestamp;
    output.hx710bStatus.timestampMs = statusTimestamp;
    return true;
}

}  // namespace project::managers
