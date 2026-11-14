#include "project/Drivers/MPU9250Driver.h"

#include <algorithm>
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
constexpr std::uint8_t kMpu9250WhoAmI = 0x71;
constexpr std::uint16_t kCalibrationSampleCount = 200;
constexpr std::uint16_t kCalibrationIntervalMs = 5;
constexpr float kAccelGPerLsbAt2G = 1.0F / 16384.0F;
constexpr float kGyroDpsPerLsbAt250Dps = 1.0F / 131.0F;
constexpr float kTemperatureLsbPerDegreeC = 333.87F;
constexpr float kTemperatureOffsetC = 21.0F;
constexpr float kComplementaryAlpha = 0.98F;
constexpr float kRadiansToDegrees = 57.29577951308232F;

std::int32_t decodeSigned16(const std::uint8_t high,
                            const std::uint8_t low) noexcept {
    const auto combined = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(high) << 8U) |
        static_cast<std::uint16_t>(low));
    const std::int32_t signedValue = static_cast<std::int32_t>(combined);
    return (combined & 0x8000U) != 0U
               ? signedValue - 0x10000
               : signedValue;
}

bool finitePositive(const float value) noexcept {
    return std::isfinite(value) && value > 0.0F;
}

bool nearScale(const float value, const float expected) noexcept {
    return std::isfinite(value) &&
           std::fabs(value - expected) <= expected * 0.0001F;
}

}  // namespace

MPU9250Driver::MPU9250Driver(TwoWire& bus,
                             const Mpu9250DriverConfig& config) noexcept
    : bus_(bus), config_(config) {}

Mpu9250Status MPU9250Driver::validateConfiguration() const noexcept {
    if (config_.i2cAddress == 0U || config_.i2cAddress > 0x7FU ||
        config_.transactionTimeoutMs == 0U ||
        !config_.registerSetupConfigured ||
        config_.calibrationSampleCount != kCalibrationSampleCount ||
        config_.calibrationIntervalMs != kCalibrationIntervalMs ||
        !config_.verifyIdentity ||
        config_.expectedWhoAmI != kMpu9250WhoAmI ||
        !config_.datasheetAssumptionsApproved ||
        config_.powerManagement1 != 0U ||
        config_.gyroConfiguration != 0U ||
        config_.accelConfiguration != 0U ||
        !finitePositive(config_.accelGPerLsb) ||
        !finitePositive(config_.gyroDpsPerLsb) ||
        !nearScale(config_.accelGPerLsb, kAccelGPerLsbAt2G) ||
        !nearScale(config_.gyroDpsPerLsb, kGyroDpsPerLsbAt250Dps) ||
        (config_.tiltDefinitionApproved &&
         config_.maximumFilterGapMs == 0U)) {
        return Mpu9250Status::InvalidConfiguration;
    }

    return Mpu9250Status::Ok;
}

Mpu9250Status MPU9250Driver::initialize() noexcept {
    initialized_ = false;
    offsets_ = {};
    resetFilter();
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

    std::array<double, 3> gyroSums{};
    for (std::uint16_t sampleIndex = 0;
         sampleIndex < config_.calibrationSampleCount;
         ++sampleIndex) {
        project::models::ImuRawSample raw{};
        lastStatus_ = readRaw(raw);
        if (lastStatus_ != Mpu9250Status::Ok) {
            offsets_.valid = false;
            lastStatus_ = Mpu9250Status::CalibrationFailed;
            return lastStatus_;
        }

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
        offsets_.gyroRaw[axis] =
            static_cast<float>(gyroSums[axis] / sampleCount);
    }
    offsets_.valid = true;
    resetFilter();
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

    output.physical.accelXG =
        static_cast<float>(output.raw.accelX) * config_.accelGPerLsb;
    output.physical.accelYG =
        static_cast<float>(output.raw.accelY) * config_.accelGPerLsb;
    output.physical.accelZG =
        static_cast<float>(output.raw.accelZ) * config_.accelGPerLsb;
    output.physical.gyroXDps =
        (static_cast<float>(output.raw.gyroX) - offsets_.gyroRaw[0]) *
        config_.gyroDpsPerLsb;
    output.physical.gyroYDps =
        (static_cast<float>(output.raw.gyroY) - offsets_.gyroRaw[1]) *
        config_.gyroDpsPerLsb;
    output.physical.gyroZDps =
        (static_cast<float>(output.raw.gyroZ) - offsets_.gyroRaw[2]) *
        config_.gyroDpsPerLsb;
    output.temperatureC =
        static_cast<float>(output.raw.temperatureRaw) /
            kTemperatureLsbPerDegreeC +
        kTemperatureOffsetC;

    const std::int64_t timestampUs = esp_timer_get_time();
    if (timestampUs < 0 ||
        !std::isfinite(output.physical.accelXG) ||
        !std::isfinite(output.physical.accelYG) ||
        !std::isfinite(output.physical.accelZG) ||
        !std::isfinite(output.physical.gyroXDps) ||
        !std::isfinite(output.physical.gyroYDps) ||
        !std::isfinite(output.physical.gyroZDps) ||
        !std::isfinite(output.temperatureC)) {
        resetFilter();
        lastStatus_ = Mpu9250Status::InvalidSample;
        return lastStatus_;
    }

    output.physical.temperatureC = output.temperatureC;
    output.physical.temperatureValid = true;
    output.sequence = ++sequence_;
    output.timestampMs = static_cast<std::uint64_t>(timestampUs) / 1000U;
    output.temperatureValid = true;
    output.accelValid = true;
    output.gyroValid = true;
    output.tiltStatus =
        config_.tiltDefinitionApproved
            ? calculateTiltAngle(
                  output.physical, static_cast<std::uint64_t>(timestampUs),
                  output.physical.tiltRollDeg, output.physical.tiltPitchDeg,
                  output.tiltAngleDeg)
            : Mpu9250Status::TiltDefinitionNotApproved;
    output.tiltValid = output.tiltStatus == Mpu9250Status::Ok;
    output.rollValid = output.tiltValid;
    output.pitchValid = output.tiltValid;
    output.physical.tiltRollValid = output.rollValid;
    output.physical.tiltPitchValid = output.pitchValid;

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
    const std::size_t registerBytesWritten = bus_.write(reg);
    const std::size_t valueBytesWritten = bus_.write(value);
    const std::uint8_t result = bus_.endTransmission(true);
    if (registerBytesWritten != 1U || valueBytesWritten != 1U ||
        result != 0U) {
        lastStatus_ = Mpu9250Status::I2cWriteFailed;
        return lastStatus_;
    }
    return Mpu9250Status::Ok;
}

Mpu9250Status MPU9250Driver::readRegisters(
    const std::uint8_t firstRegister,
    std::uint8_t* output,
    const std::uint8_t length) noexcept {
    if (output == nullptr || length == 0U || length > kRawSampleLength) {
        lastStatus_ = Mpu9250Status::InvalidConfiguration;
        return lastStatus_;
    }

    bus_.beginTransmission(config_.i2cAddress);
    const std::size_t registerBytesWritten = bus_.write(firstRegister);
    const std::uint8_t pointerStatus = bus_.endTransmission(false);
    if (registerBytesWritten != 1U || pointerStatus != 0U) {
        lastStatus_ = Mpu9250Status::I2cReadFailed;
        return lastStatus_;
    }

    const std::uint8_t received =
        bus_.requestFrom(config_.i2cAddress, length, true);
    if (received != length || bus_.available() != length) {
        while (bus_.available() > 0) {
            (void)bus_.read();
        }
        lastStatus_ = Mpu9250Status::I2cTimeout;
        return lastStatus_;
    }

    std::array<std::uint8_t, kRawSampleLength> bytes{};
    for (std::uint8_t index = 0; index < length; ++index) {
        const int byteValue = bus_.read();
        if (byteValue < 0) {
            while (bus_.available() > 0) {
                (void)bus_.read();
            }
            lastStatus_ = Mpu9250Status::I2cTimeout;
            return lastStatus_;
        }
        bytes[index] = static_cast<std::uint8_t>(byteValue);
    }
    if (bus_.available() != 0) {
        while (bus_.available() > 0) {
            (void)bus_.read();
        }
        lastStatus_ = Mpu9250Status::I2cReadFailed;
        return lastStatus_;
    }
    std::copy_n(bytes.begin(), length, output);
    return Mpu9250Status::Ok;
}

Mpu9250Status MPU9250Driver::readRaw(
    project::models::ImuRawSample& output) noexcept {
    std::array<std::uint8_t, kRawSampleLength> bytes{};
    lastStatus_ = readRegisters(kRegisterAccelOutput, bytes.data(),
                                kRawSampleLength);
    if (lastStatus_ != Mpu9250Status::Ok) {
        return lastStatus_;
    }

    output.accelX = decodeSigned16(bytes[0], bytes[1]);
    output.accelY = decodeSigned16(bytes[2], bytes[3]);
    output.accelZ = decodeSigned16(bytes[4], bytes[5]);
    output.temperatureRaw = decodeSigned16(bytes[6], bytes[7]);
    output.gyroX = decodeSigned16(bytes[8], bytes[9]);
    output.gyroY = decodeSigned16(bytes[10], bytes[11]);
    output.gyroZ = decodeSigned16(bytes[12], bytes[13]);
    return Mpu9250Status::Ok;
}

Mpu9250Status MPU9250Driver::calculateTiltAngle(
    const project::models::ImuPhysicalSample& physical,
    const std::uint64_t timestampUs,
    float& rollDeg,
    float& pitchDeg,
    float& tiltMaxDeg) noexcept {
    rollDeg = 0.0F;
    pitchDeg = 0.0F;
    tiltMaxDeg = 0.0F;

    const float ax = physical.accelXG;
    const float ay = physical.accelYG;
    const float az = physical.accelZG;
    const float normSquared = ax * ax + ay * ay + az * az;
    if (!std::isfinite(normSquared) || normSquared <= 1.0e-6F ||
        !std::isfinite(physical.gyroXDps) ||
        !std::isfinite(physical.gyroYDps)) {
        resetFilter();
        return Mpu9250Status::InvalidSample;
    }

    const float rollAccelDeg = std::atan2(ay, az) * kRadiansToDegrees;
    const float pitchAccelDeg =
        std::atan2(-ax, std::sqrt(ay * ay + az * az)) *
        kRadiansToDegrees;
    if (!std::isfinite(rollAccelDeg) || !std::isfinite(pitchAccelDeg)) {
        resetFilter();
        return Mpu9250Status::InvalidSample;
    }

    if (filterInitialized_) {
        if (timestampUs <= lastFilterTimestampUs_) {
            resetFilter();
            return Mpu9250Status::InvalidSample;
        }
        const std::uint64_t elapsedUs = timestampUs - lastFilterTimestampUs_;
        if (elapsedUs >
            static_cast<std::uint64_t>(config_.maximumFilterGapMs) * 1000U) {
            resetFilter();
            return Mpu9250Status::InvalidSample;
        }
        const float dtSeconds = static_cast<float>(elapsedUs) / 1000000.0F;
        filteredRollDeg_ =
            kComplementaryAlpha *
                (filteredRollDeg_ + physical.gyroXDps * dtSeconds) +
            (1.0F - kComplementaryAlpha) * rollAccelDeg;
        filteredPitchDeg_ =
            kComplementaryAlpha *
                (filteredPitchDeg_ + physical.gyroYDps * dtSeconds) +
            (1.0F - kComplementaryAlpha) * pitchAccelDeg;
    } else {
        filteredRollDeg_ = rollAccelDeg;
        filteredPitchDeg_ = pitchAccelDeg;
        filterInitialized_ = true;
    }
    lastFilterTimestampUs_ = timestampUs;

    if (!std::isfinite(filteredRollDeg_) ||
        !std::isfinite(filteredPitchDeg_)) {
        resetFilter();
        return Mpu9250Status::InvalidSample;
    }
    rollDeg = filteredRollDeg_;
    pitchDeg = filteredPitchDeg_;
    tiltMaxDeg = std::max(std::fabs(rollDeg), std::fabs(pitchDeg));
    if (!std::isfinite(tiltMaxDeg)) {
        resetFilter();
        return Mpu9250Status::InvalidSample;
    }
    return Mpu9250Status::Ok;
}

void MPU9250Driver::resetFilter() noexcept {
    lastFilterTimestampUs_ = 0;
    filteredRollDeg_ = 0.0F;
    filteredPitchDeg_ = 0.0F;
    filterInitialized_ = false;
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
