#include "project/Managers/SensorManager.h"

#include <limits>

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_timer.h>
#else
#include <chrono>
#endif

namespace project::managers {
namespace {

constexpr std::size_t kSensorSlotCount =
    static_cast<std::size_t>(SensorSlot::Count);

void incrementSaturated(std::uint32_t& value) noexcept {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

bool validSlot(const SensorSlot slot) noexcept {
    return static_cast<std::size_t>(slot) < kSensorSlotCount;
}

void setSensorStatus(project::models::SensorFrame& frame,
                     const SensorSlot slot,
                     const project::models::SensorStatus& status) noexcept {
    switch (slot) {
        case SensorSlot::Imu:
            frame.imuStatus = status;
            break;
        case SensorSlot::Soil:
            frame.soilStatus = status;
            break;
        case SensorSlot::Rain:
            frame.rainStatus = status;
            break;
        case SensorSlot::Hx710b:
            frame.hx710bStatus = status;
            break;
        case SensorSlot::Count:
            break;
    }
}

}  // namespace

std::uint64_t PlatformSensorManagerClock::nowMicros() noexcept {
#if defined(ARDUINO)
    return static_cast<std::uint64_t>(esp_timer_get_time());
#else
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
#endif
}

void PlatformSensorManagerClock::delayMicros(
    const std::uint32_t durationMicros) noexcept {
    if (durationMicros == 0U) {
        return;
    }
#if defined(ARDUINO)
    if (durationMicros >= 1000U) {
        delay(durationMicros / 1000U);
        delayMicroseconds(durationMicros % 1000U);
    } else {
        delayMicroseconds(durationMicros);
    }
#else
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::microseconds(durationMicros);
    while (std::chrono::steady_clock::now() < deadline) {
    }
#endif
}

SensorManager::SensorManager(project::drivers::ISensor& imu,
                             project::drivers::ISensor& soil,
                             project::drivers::ISensor& rain,
                             project::drivers::ISensor& hx710b,
                             const SensorManagerConfig& config,
                             ISensorManagerClock& clock) noexcept
    : imu_(imu),
      soil_(soil),
      rain_(rain),
      hx710b_(hx710b),
      config_(config),
      clock_(clock) {}

bool SensorManager::validateConfig() const noexcept {
    if (config_.samplePeriodMicros == 0U) {
        return false;
    }
    for (const auto& policy : config_.readPolicies) {
        if (policy.beginTimeoutMicros == 0U ||
            policy.timeoutMicros == 0U) {
            return false;
        }
    }
    return true;
}

bool SensorManager::beginAll() noexcept {
    ready_ = false;
    sampleDue_ = false;
    firstPoll_ = true;
    nextSequence_ = 0;
    diagnostics_ = {};
    if (!validateConfig()) {
        return false;
    }

    const project::models::SensorStatus imuStatus =
        beginSensor(imu_, SensorSlot::Imu);
    const project::models::SensorStatus soilStatus =
        beginSensor(soil_, SensorSlot::Soil);
    const project::models::SensorStatus rainStatus =
        beginSensor(rain_, SensorSlot::Rain);
    const project::models::SensorStatus hx710bStatus =
        beginSensor(hx710b_, SensorSlot::Hx710b);

    ready_ = imuStatus.valid && soilStatus.valid && rainStatus.valid &&
             hx710bStatus.valid;
    return ready_;
}

project::models::SensorStatus SensorManager::beginSensor(
    project::drivers::ISensor& sensor,
    const SensorSlot slot) noexcept {
    auto& stats = mutableDiagnostics(slot);
    const auto& policy = config_.readPolicies[static_cast<std::size_t>(slot)];
    project::models::SensorStatus status{};

    for (std::uint16_t attempt = 0;
         attempt <= policy.maxRetries;
         ++attempt) {
        incrementSaturated(stats.beginAttempts);
        const std::uint64_t started = clock_.nowMicros();
        status = sensor.begin();
        const std::uint64_t finished = clock_.nowMicros();
        if (finished < started) {
            incrementSaturated(stats.clockErrors);
            status.valid = false;
            status.health = project::models::SensorHealth::Fault;
        } else {
            stats.lastElapsedMicros = finished - started;
            if (stats.lastElapsedMicros > policy.beginTimeoutMicros) {
                incrementSaturated(stats.timeouts);
                status.valid = false;
                status.health = project::models::SensorHealth::Fault;
            }
        }

        if (status.valid) {
            return status;
        }
        status.health = project::models::SensorHealth::Fault;
        if (attempt < policy.maxRetries) {
            incrementSaturated(stats.retries);
            clock_.delayMicros(policy.retryBackoffMicros);
        }
    }

    incrementSaturated(stats.beginFailures);
    return status;
}

bool SensorManager::pollDue(const std::uint64_t nowMicros) noexcept {
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

project::models::SensorStatus SensorManager::readSensor(
    project::drivers::ISensor& sensor,
    const SensorSlot slot,
    project::models::SensorFrame& sensorFrame) noexcept {
    auto& stats = mutableDiagnostics(slot);
    const auto& policy = config_.readPolicies[static_cast<std::size_t>(slot)];
    project::models::SensorStatus status{};

    for (std::uint16_t attempt = 0;
         attempt <= policy.maxRetries;
         ++attempt) {
        sensorFrame = {};
        incrementSaturated(stats.readAttempts);
        const std::uint64_t started = clock_.nowMicros();
        status = sensor.read(sensorFrame);
        const std::uint64_t finished = clock_.nowMicros();
        bool timedOut = false;

        if (finished < started) {
            incrementSaturated(stats.clockErrors);
            timedOut = true;
        } else {
            stats.lastElapsedMicros = finished - started;
            timedOut = stats.lastElapsedMicros > policy.timeoutMicros;
        }

        if (timedOut) {
            incrementSaturated(stats.timeouts);
            status.valid = false;
            status.health = project::models::SensorHealth::Fault;
            sensorFrame = {};
        } else if (!status.valid) {
            incrementSaturated(stats.readFailures);
            status.health = project::models::SensorHealth::Fault;
            sensorFrame = {};
        } else {
            setSensorStatus(sensorFrame, slot, status);
            return status;
        }

        if (attempt < policy.maxRetries) {
            incrementSaturated(stats.retries);
            clock_.delayMicros(policy.retryBackoffMicros);
        }
    }

    sensorFrame = {};
    setSensorStatus(sensorFrame, slot, status);
    return status;
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

    const project::models::SensorStatus imuStatus =
        readSensor(imu_, SensorSlot::Imu, imuFrame);
    const project::models::SensorStatus soilStatus =
        readSensor(soil_, SensorSlot::Soil, soilFrame);
    const project::models::SensorStatus rainStatus =
        readSensor(rain_, SensorSlot::Rain, rainFrame);
    const project::models::SensorStatus hx710bStatus =
        readSensor(hx710b_, SensorSlot::Hx710b, hx710bFrame);

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
        output.hx710bCalibrationFactor =
            hx710bFrame.hx710bCalibrationFactor;
        output.hx710bCalibrationFactorValid =
            hx710bFrame.hx710bCalibrationFactorValid;
        output.hx710bFactorScaledRaw =
            hx710bFrame.hx710bFactorScaledRaw;
        output.hx710bFactorScaledRawValid =
            hx710bFrame.hx710bFactorScaledRawValid;
        if (hx710bFrame.calibrationVersion != 0U) {
            output.calibrationVersion = hx710bFrame.calibrationVersion;
        }
    }

    const auto statusTimestamp = output.timestampMs;
    output.imuStatus.timestampMs = statusTimestamp;
    output.soilStatus.timestampMs = statusTimestamp;
    output.rainStatus.timestampMs = statusTimestamp;
    output.hx710bStatus.timestampMs = statusTimestamp;
    return true;
}

SensorManagerDiagnostics SensorManager::diagnostics(
    const SensorSlot sensor) const noexcept {
    if (!validSlot(sensor)) {
        return {};
    }
    return diagnostics_[static_cast<std::size_t>(sensor)];
}

SensorManagerDiagnostics& SensorManager::mutableDiagnostics(
    const SensorSlot sensor) noexcept {
    return diagnostics_[static_cast<std::size_t>(sensor)];
}

}  // namespace project::managers
