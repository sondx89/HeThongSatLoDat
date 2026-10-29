#include "project/Drivers/MockSensors.h"

#include <limits>

namespace project::drivers {
namespace {

bool validRawSignal(const MockRawSignal& signal) noexcept {
    if (signal.holdSamples == 0U || signal.excursion < 0) {
        return false;
    }
    const std::int64_t minimum =
        static_cast<std::int64_t>(signal.baseline) - signal.excursion;
    const std::int64_t maximum =
        static_cast<std::int64_t>(signal.baseline) + signal.excursion;
    return minimum >= std::numeric_limits<std::int32_t>::min() &&
           maximum <= std::numeric_limits<std::int32_t>::max();
}

}  // namespace

MockEnvironmentProfile MockEnvironmentProfile::demo() noexcept {
    MockEnvironmentProfile profile{};
    profile.imu.axes = {{
        {0, 120, 4},
        {0, 120, 5},
        {16000, 80, 8},
        {0, 20, 3},
        {0, 20, 4},
        {0, 20, 5}}};
    profile.soilMoisture = {2000, 400, 6};
    profile.rain = {1, 1, 10};
    profile.hx710b = {5000000, 12000, 8};
    return profile;
}

MockSensorBase::MockSensorBase(
    const MockSensorBehavior& behavior,
    const std::array<MockRawSignal, 6>& signals,
    std::uint8_t signalCount) noexcept
    : behavior_(behavior), signals_(signals), signalCount_(signalCount) {}

MockSensorBase::MockSensorBase(const MockSensorBehavior& behavior,
                               const MockRawSignal& signal) noexcept
    : behavior_(behavior), signals_{{signal}}, signalCount_(1U) {}

bool MockSensorBase::signalIsValid(const MockRawSignal& signal) noexcept {
    return validRawSignal(signal);
}

project::models::SensorStatus MockSensorBase::makeStatus(bool valid) noexcept {
    project::models::SensorStatus status{};
    status.valid = valid;
    status.health = valid ? project::models::SensorHealth::Healthy
                          : project::models::SensorHealth::Fault;
    return status;
}

project::models::SensorStatus MockSensorBase::begin() {
    initialized_ = signalCount_ != 0U &&
                   signalCount_ <= signals_.size();
    for (std::uint8_t index = 0; initialized_ && index < signalCount_; ++index) {
        initialized_ = signalIsValid(signals_[index]);
    }
    readCount_ = 0;
    return makeStatus(initialized_);
}

project::models::SensorStatus MockSensorBase::selfTest() {
    return makeStatus(initialized_);
}

bool MockSensorBase::nextRead(
    std::uint32_t& sampleIndex,
    project::models::SensorStatus& status) noexcept {
    sampleIndex = readCount_;
    if (!initialized_ || readCount_ == UINT32_MAX) {
        status = makeStatus(false);
        return false;
    }

    ++readCount_;
    if (behavior_.failEveryReads != 0U &&
        readCount_ % behavior_.failEveryReads == 0U) {
        status = makeStatus(false);
        return false;
    }
    status = makeStatus(true);
    return true;
}

std::int32_t MockSensorBase::signalValue(
    const MockRawSignal& signal, std::uint32_t sampleIndex) noexcept {
    const bool positive =
        ((sampleIndex / signal.holdSamples) & 1U) != 0U;
    const std::int64_t value =
        static_cast<std::int64_t>(signal.baseline) +
        (positive ? signal.excursion : -static_cast<std::int64_t>(signal.excursion));
    return static_cast<std::int32_t>(value);
}

MockImuSensor::MockImuSensor(
    const MockEnvironmentProfile& profile) noexcept
    : MockSensorBase(profile.imuBehavior, profile.imu.axes, 6U),
      profile_(profile.imu) {}

project::models::SensorStatus MockImuSensor::read(
    project::models::SensorFrame& output) {
    output = {};
    std::uint32_t sampleIndex = 0;
    project::models::SensorStatus status{};
    if (!nextRead(sampleIndex, status)) {
        output.imuStatus = status;
        return status;
    }

    output.imu.accelX = signalValue(profile_.axes[0], sampleIndex);
    output.imu.accelY = signalValue(profile_.axes[1], sampleIndex);
    output.imu.accelZ = signalValue(profile_.axes[2], sampleIndex);
    output.imu.gyroX = signalValue(profile_.axes[3], sampleIndex);
    output.imu.gyroY = signalValue(profile_.axes[4], sampleIndex);
    output.imu.gyroZ = signalValue(profile_.axes[5], sampleIndex);
    output.imuStatus = status;
    return status;
}

MockSoilSensor::MockSoilSensor(
    const MockEnvironmentProfile& profile) noexcept
    : MockSensorBase(profile.soilBehavior, profile.soilMoisture),
      signal_(profile.soilMoisture) {}

project::models::SensorStatus MockSoilSensor::read(
    project::models::SensorFrame& output) {
    output = {};
    std::uint32_t sampleIndex = 0;
    project::models::SensorStatus status{};
    if (!nextRead(sampleIndex, status)) {
        output.soilStatus = status;
        return status;
    }
    output.soilMoistureRaw = signalValue(signal_, sampleIndex);
    output.soilStatus = status;
    return status;
}

MockRainSensor::MockRainSensor(
    const MockEnvironmentProfile& profile) noexcept
    : MockSensorBase(profile.rainBehavior, profile.rain),
      signal_(profile.rain) {}

project::models::SensorStatus MockRainSensor::read(
    project::models::SensorFrame& output) {
    output = {};
    std::uint32_t sampleIndex = 0;
    project::models::SensorStatus status{};
    if (!nextRead(sampleIndex, status)) {
        output.rainStatus = status;
        return status;
    }
    output.rainRaw = signalValue(signal_, sampleIndex);
    output.rainStatus = status;
    return status;
}

MockHx710bSensor::MockHx710bSensor(
    const MockEnvironmentProfile& profile) noexcept
    : MockSensorBase(profile.hx710bBehavior, profile.hx710b),
      signal_(profile.hx710b) {}

project::models::SensorStatus MockHx710bSensor::read(
    project::models::SensorFrame& output) {
    output = {};
    std::uint32_t sampleIndex = 0;
    project::models::SensorStatus status{};
    if (!nextRead(sampleIndex, status)) {
        output.hx710bStatus = status;
        return status;
    }
    output.hx710bRaw = signalValue(signal_, sampleIndex);
    output.hx710bStatus = status;
    return status;
}

}  // namespace project::drivers
