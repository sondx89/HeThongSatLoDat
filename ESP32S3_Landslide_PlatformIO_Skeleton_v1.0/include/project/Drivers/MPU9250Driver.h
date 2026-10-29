#pragma once

#include <array>
#include <cstdint>

#include <Wire.h>

#include "Drivers/ISensor.h"

namespace project::drivers {

struct Mpu9250DriverConfig {
    std::uint8_t i2cAddress{0};
    std::uint16_t transactionTimeoutMs{0};

    bool registerSetupConfigured{false};
    std::uint8_t powerManagement1{0};
    std::uint8_t configuration{0};
    std::uint8_t sampleRateDivider{0};
    std::uint8_t gyroConfiguration{0};
    std::uint8_t accelConfiguration{0};
    std::uint8_t accelConfiguration2{0};
    std::uint8_t calibrationSampleCount{0};
    std::uint16_t calibrationIntervalMs{0};

    bool verifyIdentity{true};
    std::uint8_t expectedWhoAmI{0};
    float accelGPerLsb{0.0F};
    float gyroDpsPerLsb{0.0F};
    std::array<float, 3> referenceAccelG{};
};

enum class Mpu9250Status : std::uint8_t {
    Ok,
    InvalidConfiguration,
    NotInitialized,
    NotCalibrated,
    I2cWriteFailed,
    I2cReadFailed,
    I2cTimeout,
    IdentityMismatch,
    CalibrationFailed,
    InvalidSample,
    TiltCalculationPending
};

struct Mpu9250Offsets {
    std::array<float, 3> accelRaw{};
    std::array<float, 3> gyroRaw{};
    bool valid{false};
};

struct Mpu9250Sample {
    std::uint64_t sequence{0};
    std::uint64_t timestampMs{0};
    project::models::ImuRawSample raw{};
    project::models::ImuPhysicalSample physical{};
    float tiltAngleDeg{0.0F};
    bool accelValid{false};
    bool gyroValid{false};
    bool tiltValid{false};
    Mpu9250Status tiltStatus{Mpu9250Status::TiltCalculationPending};
};

class MPU9250Driver final : public ISensor {
public:
    MPU9250Driver(TwoWire& bus,
                  const Mpu9250DriverConfig& config) noexcept;

    project::models::SensorStatus begin() override;
    project::models::SensorStatus read(
        project::models::SensorFrame& output) override;
    project::models::SensorStatus selfTest() override;

    Mpu9250Status initialize() noexcept;
    Mpu9250Status calibrateOffsets() noexcept;
    Mpu9250Status readSample(Mpu9250Sample& output) noexcept;
    Mpu9250Status lastStatus() const noexcept;
    Mpu9250Offsets offsets() const noexcept;

    MPU9250Driver(const MPU9250Driver&) = delete;
    MPU9250Driver& operator=(const MPU9250Driver&) = delete;

private:
    Mpu9250Status validateConfiguration() const noexcept;
    Mpu9250Status writeRegister(std::uint8_t reg,
                                std::uint8_t value) noexcept;
    Mpu9250Status readRegisters(std::uint8_t firstRegister,
                                std::uint8_t* output,
                                std::uint8_t length) noexcept;
    Mpu9250Status readRaw(project::models::ImuRawSample& output) noexcept;
    Mpu9250Status calculateTiltAngle(
        const project::models::ImuPhysicalSample& physical,
        float& tiltAngleDeg) const noexcept;
    project::models::SensorStatus sensorStatus(bool valid) const noexcept;

    TwoWire& bus_;
    const Mpu9250DriverConfig config_;
    Mpu9250Offsets offsets_{};
    Mpu9250Status lastStatus_{Mpu9250Status::NotInitialized};
    std::uint64_t sequence_{0};
    bool initialized_{false};
};

}  // namespace project::drivers
