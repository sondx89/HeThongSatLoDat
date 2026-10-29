#include "project/Drivers/MPU9250Driver.h"

#include <cmath>
#include <limits>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace project::drivers {
namespace {

constexpr std::uint8_t kRegisterPowerManagement1 = 0x6B;
constexpr std::uint8_t kRegisterConfiguration = 0x1A;
constexpr std::uint8_t kRegisterSampleRateDivider = 0x19;
constexpr std::uint8_t kRegisterGyroConfiguration = 0x1B;
constexpr std::uint8_t kRegisterAccelConfiguration = 0x1C;
constexpr std::uint8_t kRegisterAccelConfiguration2 = 0x1D;
constexpr std::uint8_t kRegisterAccelOutput = 0x3B;
constexpr std::uint8_t kRegisterWhoAmI = 0x75;
constexpr std::uint8_t kRawSampleLength = 14;
constexpr std::uint8_t kMaxCalibrationSamples = 100;

std::int16_t decodeSigned16(const std::uint8_t high,
                            const std::uint8_t low) noexcept {
    const auto combined = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(high) << 8U) |
        static_cast<std::uint16_t>(low));
    return static_cast<std::int16_t>(combined);
}

bool finitePositive(const float value) noexcept {
    return std::isfinite(value) && value > 0.0F;
}

}  // namespace

MPU9250Driver::MPU9250Driver(TwoWire& bus,
                             const Mpu9250DriverConfig& config) noexcept
    : bus_(bus), config_(config) {}

Mpu9250Status MPU9250Driver::validateConfiguration() const noexcept {
    if (config_.i2cAddress == 0U || config_.i2cAddress > 0x7FU ||
        config_.transactionTimeoutMs == 0U ||
        !config_.registerSetupConfigured ||
        config_.calibrationSampleCount == 0U ||
        config_.calibrationSampleCount > kMaxCalibrationSamples ||
        config_.calibrationIntervalMs == 0U ||
        (config_.verifyIdentity && config_.expectedWhoAmI == 0U) ||
        !finitePositive(config_.accelGPerLsb) ||
        !finitePositive(config_.gyroDpsPerLsb)) {
        return Mpu9250Status::InvalidConfiguration;
    }

    float referenceNormSquared = 0.0F;
    for (const float component : config_.referenceAccelG) {
        if (!std::isfinite(component)) {
            return Mpu9250Status::InvalidConfiguration;
        }
        referenceNormSquared += component * component;
    }
    return referenceNormSquared > 0.0F
               ? Mpu9250Status::Ok
               : Mpu9250Status::InvalidConfiguration;
}

Mpu9250Status MPU9250Driver::initialize() noexcept {
    initialized_ = false;
    offsets_ = {};
    lastStatus_ = validateConfiguration();
    if (lastStatus_ != Mpu9250Status::Ok) {
        return lastStatus_;
    }

    bus_.setTimeOut(config_.transactionTimeoutMs);
    if (config_.verifyIdentity) {
        std::uint8_t identity = 0;
        lastStatus_ = readRegisters(kRegisterWhoAmI, &identity, 1);
        if (lastStatus_ != Mpu9250Status::Ok) {
            return lastStatus_;
        }
        if (identity != config_.expectedWhoAmI) {
            lastStatus_ = Mpu9250Status::IdentityMismatch;
            return lastStatus_;
        }
    }

    const struct {
        std::uint8_t address;
        std::uint8_t value;
    } registerWrites[] = {
        {kRegisterPowerManagement1, config_.powerManagement1},
        {kRegisterConfiguration, config_.configuration},
        {kRegisterSampleRateDivider, config_.sampleRateDivider},
        {kRegisterGyroConfiguration, config_.gyroConfiguration},
        {kRegisterAccelConfiguration, config_.accelConfiguration},
        {kRegisterAccelConfiguration2, config_.accelConfiguration2},
    };

    for (const auto& entry : registerWrites) {
        lastStatus_ = writeRegister(entry.address, entry.value);
        if (lastStatus_ != Mpu9250Status::Ok) {
            return lastStatus_;
        }
    }

    initialized_ = true;
    lastStatus_ = Mpu9250Status::Ok;
    return lastStatus_;
}

Mpu9250Status MPU9250Driver::calibrateOffsets() noexcept {
    if (!initialized_) {
        lastStatus_ = Mpu9250Status::NotInitialized;
        return lastStatus_;
    }

    std::array<double, 3> accelSums{};
    std::array<double, 3> gyroSums{};
    for (std::uint8_t sampleIndex = 0;
         sampleIndex < config_.calibrationSampleCount;
         ++sampleIndex) {
        project::models::ImuRawSample raw{};
        lastStatus_ = readRaw(raw);
        if (lastStatus_ != Mpu9250Status::Ok) {
            offsets_.valid = false;
            lastStatus_ = Mpu9250Status::CalibrationFailed;
            return lastStatus_;
        }

        accelSums[0] += raw.accelX;
        accelSums[1] += raw.accelY;
        accelSums[2] += raw.accelZ;
        gyroSums[0] += raw.gyroX;
        gyroSums[1] += raw.gyroY;
        gyroSums[2] += raw.gyroZ;

        if (sampleIndex + 1U < config_.calibrationSampleCount) {
            vTaskDelay(pdMS_TO_TICKS(config_.calibrationIntervalMs));
        }
    }

    const double sampleCount =
        static_cast<double>(config_.calibrationSampleCount);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double expectedAccelRaw =
            static_cast<double>(config_.referenceAccelG[axis]) /
            static_cast<double>(config_.accelGPerLsb);
        offsets_.accelRaw[axis] =
            static_cast<float>(accelSums[axis] / sampleCount -
                               expectedAccelRaw);
        offsets_.gyroRaw[axis] =
            static_cast<float>(gyroSums[axis] / sampleCount);
    }
    offsets_.valid = true;
    lastStatus_ = Mpu9250Status::Ok;
    return lastStatus_;
}

Mpu9250Status MPU9250Driver::readSample(Mpu9250Sample& output) noexcept {
    output = {};
    if (!initialized_) {
        lastStatus_ = Mpu9250Status::NotInitialized;
        return lastStatus_;
    }
    if (!offsets_.valid) {
        lastStatus_ = Mpu9250Status::NotCalibrated;
        return lastStatus_;
    }
    if (sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        lastStatus_ = Mpu9250Status::InvalidSample;
        return lastStatus_;
    }

    lastStatus_ = readRaw(output.raw);
    if (lastStatus_ != Mpu9250Status::Ok) {
        return lastStatus_;
    }

    const std::array<float, 3> accelRaw{
        static_cast<float>(output.raw.accelX),
        static_cast<float>(output.raw.accelY),
        static_cast<float>(output.raw.accelZ)};
    const std::array<float, 3> gyroRaw{
        static_cast<float>(output.raw.gyroX),
        static_cast<float>(output.raw.gyroY),
        static_cast<float>(output.raw.gyroZ)};
    std::array<float, 3> accelG{};
    std::array<float, 3> gyroDps{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        accelG[axis] =
            (accelRaw[axis] - offsets_.accelRaw[axis]) *
            config_.accelGPerLsb;
        gyroDps[axis] =
            (gyroRaw[axis] - offsets_.gyroRaw[axis]) *
            config_.gyroDpsPerLsb;
    }

    output.physical.accelXG = accelG[0];
    output.physical.accelYG = accelG[1];
    output.physical.accelZG = accelG[2];
    output.physical.gyroXDps = gyroDps[0];
    output.physical.gyroYDps = gyroDps[1];
    output.physical.gyroZDps = gyroDps[2];
    output.sequence = ++sequence_;
    output.timestampMs =
        static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
    output.accelValid = true;
    output.gyroValid = true;
    output.tiltStatus =
        calculateTiltAngle(output.physical, output.tiltAngleDeg);
    output.tiltValid = output.tiltStatus == Mpu9250Status::Ok;

    lastStatus_ = Mpu9250Status::Ok;
    return lastStatus_;
}

project::models::SensorStatus MPU9250Driver::begin() {
    if (initialize() != Mpu9250Status::Ok ||
        calibrateOffsets() != Mpu9250Status::Ok) {
        return sensorStatus(false);
    }
    return sensorStatus(true);
}

project::models::SensorStatus MPU9250Driver::read(
    project::models::SensorFrame& output) {
    output = {};
    Mpu9250Sample sample{};
    if (readSample(sample) != Mpu9250Status::Ok) {
        output.imuStatus = sensorStatus(false);
        return output.imuStatus;
    }

    output.imu = sample.raw;
    output.imuPhysical = sample.physical;
    output.tiltAngleDeg = sample.tiltAngleDeg;
    output.tiltAngleValid = sample.tiltValid;
    output.imuStatus = sensorStatus(true);
    output.imuStatus.timestampMs = sample.timestampMs;
    return output.imuStatus;
}

project::models::SensorStatus MPU9250Driver::selfTest() {
    if (!initialized_) {
        lastStatus_ = Mpu9250Status::NotInitialized;
        return sensorStatus(false);
    }

    if (config_.verifyIdentity) {
        std::uint8_t identity = 0;
        lastStatus_ = readRegisters(kRegisterWhoAmI, &identity, 1);
        if (lastStatus_ == Mpu9250Status::Ok &&
            identity != config_.expectedWhoAmI) {
            lastStatus_ = Mpu9250Status::IdentityMismatch;
        }
        return sensorStatus(lastStatus_ == Mpu9250Status::Ok);
    }
    lastStatus_ = Mpu9250Status::Ok;
    return sensorStatus(true);
}

Mpu9250Status MPU9250Driver::writeRegister(const std::uint8_t reg,
                                           const std::uint8_t value) noexcept {
    bus_.beginTransmission(config_.i2cAddress);
    bus_.write(reg);
    bus_.write(value);
    const std::uint8_t result = bus_.endTransmission(true);
    if (result != 0U) {
        lastStatus_ = Mpu9250Status::I2cWriteFailed;
        return lastStatus_;
    }
    return Mpu9250Status::Ok;
}

Mpu9250Status MPU9250Driver::readRegisters(
    const std::uint8_t firstRegister,
    std::uint8_t* output,
    const std::uint8_t length) noexcept {
    if (output == nullptr || length == 0U) {
        lastStatus_ = Mpu9250Status::InvalidConfiguration;
        return lastStatus_;
    }

    bus_.beginTransmission(config_.i2cAddress);
    bus_.write(firstRegister);
    if (bus_.endTransmission(false) != 0U) {
        lastStatus_ = Mpu9250Status::I2cReadFailed;
        return lastStatus_;
    }

    const std::uint8_t received =
        bus_.requestFrom(config_.i2cAddress, length, true);
    if (received != length) {
        while (bus_.available() > 0) {
            (void)bus_.read();
        }
        lastStatus_ = Mpu9250Status::I2cTimeout;
        return lastStatus_;
    }

    for (std::uint8_t index = 0; index < length; ++index) {
        const int byteValue = bus_.read();
        if (byteValue < 0) {
            lastStatus_ = Mpu9250Status::I2cTimeout;
            return lastStatus_;
        }
        output[index] = static_cast<std::uint8_t>(byteValue);
    }
    return Mpu9250Status::Ok;
}

Mpu9250Status MPU9250Driver::readRaw(
    project::models::ImuRawSample& output) noexcept {
    std::uint8_t bytes[kRawSampleLength]{};
    lastStatus_ = readRegisters(kRegisterAccelOutput, bytes, kRawSampleLength);
    if (lastStatus_ != Mpu9250Status::Ok) {
        return lastStatus_;
    }

    output.accelX = decodeSigned16(bytes[0], bytes[1]);
    output.accelY = decodeSigned16(bytes[2], bytes[3]);
    output.accelZ = decodeSigned16(bytes[4], bytes[5]);
    output.gyroX = decodeSigned16(bytes[8], bytes[9]);
    output.gyroY = decodeSigned16(bytes[10], bytes[11]);
    output.gyroZ = decodeSigned16(bytes[12], bytes[13]);
    return Mpu9250Status::Ok;
}

Mpu9250Status MPU9250Driver::calculateTiltAngle(
    const project::models::ImuPhysicalSample& physical,
    float& tiltAngleDeg) const noexcept {
    (void)physical;
    tiltAngleDeg = 0.0F;
    // TODO(TBD): implement only after the tilt axes, reference pose, formula,
    // and filtering method are approved in the system documents.
    return Mpu9250Status::TiltCalculationPending;
}

project::models::SensorStatus MPU9250Driver::sensorStatus(
    const bool valid) const noexcept {
    project::models::SensorStatus status{};
    status.health = valid ? project::models::SensorHealth::Healthy
                          : project::models::SensorHealth::Fault;
    status.valid = valid;
    return status;
}

Mpu9250Status MPU9250Driver::lastStatus() const noexcept {
    return lastStatus_;
}

Mpu9250Offsets MPU9250Driver::offsets() const noexcept {
    return offsets_;
}

}  // namespace project::drivers
