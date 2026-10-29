#pragma once

#include <array>
#include <cstdint>

#include "Drivers/ISensor.h"

namespace project::drivers {

struct MockRawSignal {
    std::int32_t baseline{0};
    std::int32_t excursion{0};
    std::uint32_t holdSamples{1};
};

struct MockSensorBehavior {
    // Zero disables fault injection; otherwise every Nth read reports a fault.
    std::uint32_t failEveryReads{0};
};

struct MockImuProfile {
    // Axis order: accel X/Y/Z, then gyro X/Y/Z.
    std::array<MockRawSignal, 6> axes{};
};

struct MockEnvironmentProfile {
    MockImuProfile imu{};
    MockRawSignal soilMoisture{};
    MockRawSignal rain{};
    MockRawSignal hx710b{};
    MockSensorBehavior imuBehavior{};
    MockSensorBehavior soilBehavior{};
    MockSensorBehavior rainBehavior{};
    MockSensorBehavior hx710bBehavior{};

    // Deterministic raw-count fixture for development only. Values do not
    // represent released sensor ranges, units, calibration, or risk thresholds.
    static MockEnvironmentProfile demo() noexcept;
};

class MockSensorBase : public ISensor {
public:
    project::models::SensorStatus begin() override;
    project::models::SensorStatus selfTest() override;

protected:
    MockSensorBase(const MockSensorBehavior& behavior,
                   const std::array<MockRawSignal, 6>& signals,
                   std::uint8_t signalCount) noexcept;
    MockSensorBase(const MockSensorBehavior& behavior,
                   const MockRawSignal& signal) noexcept;
    bool nextRead(std::uint32_t& sampleIndex,
                  project::models::SensorStatus& status) noexcept;
    static bool signalIsValid(const MockRawSignal& signal) noexcept;
    static std::int32_t signalValue(const MockRawSignal& signal,
                                    std::uint32_t sampleIndex) noexcept;
    static project::models::SensorStatus makeStatus(bool valid) noexcept;

private:
    const MockSensorBehavior behavior_;
    std::array<MockRawSignal, 6> signals_{};
    const std::uint8_t signalCount_;
    std::uint32_t readCount_{0};
    bool initialized_{false};
};

class MockImuSensor final : public MockSensorBase {
public:
    explicit MockImuSensor(const MockEnvironmentProfile& profile) noexcept;
    project::models::SensorStatus read(
        project::models::SensorFrame& output) override;

private:
    const MockImuProfile profile_;
};

class MockSoilSensor final : public MockSensorBase {
public:
    explicit MockSoilSensor(const MockEnvironmentProfile& profile) noexcept;
    project::models::SensorStatus read(
        project::models::SensorFrame& output) override;

private:
    const MockRawSignal signal_;
};

class MockRainSensor final : public MockSensorBase {
public:
    explicit MockRainSensor(const MockEnvironmentProfile& profile) noexcept;
    project::models::SensorStatus read(
        project::models::SensorFrame& output) override;

private:
    const MockRawSignal signal_;
};

class MockHx710bSensor final : public MockSensorBase {
public:
    explicit MockHx710bSensor(const MockEnvironmentProfile& profile) noexcept;
    project::models::SensorStatus read(
        project::models::SensorFrame& output) override;

private:
    const MockRawSignal signal_;
};

}  // namespace project::drivers
