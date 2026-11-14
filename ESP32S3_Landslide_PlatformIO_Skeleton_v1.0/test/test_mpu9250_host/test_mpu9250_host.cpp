#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

#include <esp_timer.h>
#include <freertos/task.h>

#include "project/Drivers/MPU9250Driver.h"
#include "Wire.h"

namespace {

using project::drivers::MPU9250Driver;
using project::drivers::Mpu9250DriverConfig;
using project::drivers::Mpu9250Sample;
using project::drivers::Mpu9250Status;

Mpu9250DriverConfig validConfig() {
    Mpu9250DriverConfig config{};
    config.i2cAddress = 0x68;
    config.transactionTimeoutMs = 50;
    config.registerSetupConfigured = true;
    config.powerManagement1 = 0x00;
    config.gyroConfiguration = 0x00;
    config.accelConfiguration = 0x00;
    config.calibrationSampleCount = 200;
    config.calibrationIntervalMs = 5;
    config.verifyIdentity = true;
    config.expectedWhoAmI = 0x71;
    config.datasheetAssumptionsApproved = true;
    config.tiltDefinitionApproved = true;
    config.accelGPerLsb = 1.0F / 16384.0F;
    config.gyroDpsPerLsb = 1.0F / 131.0F;
    config.maximumFilterGapMs = 500;
    return config;
}

void setWord(TwoWire& bus, std::uint8_t reg, std::int32_t value) {
    const auto encoded = static_cast<std::uint16_t>(value);
    bus.registers[reg] = static_cast<std::uint8_t>(encoded >> 8U);
    bus.registers[static_cast<std::uint8_t>(reg + 1U)] =
        static_cast<std::uint8_t>(encoded);
}

void setRaw(TwoWire& bus,
            std::int32_t ax,
            std::int32_t ay,
            std::int32_t az,
            std::int32_t temperature,
            std::int32_t gx,
            std::int32_t gy,
            std::int32_t gz) {
    setWord(bus, 0x3B, ax);
    setWord(bus, 0x3D, ay);
    setWord(bus, 0x3F, az);
    setWord(bus, 0x41, temperature);
    setWord(bus, 0x43, gx);
    setWord(bus, 0x45, gy);
    setWord(bus, 0x47, gz);
}

void prepare(TwoWire& bus, MPU9250Driver& driver) {
    bus.registers[0x75] = 0x71;
    setRaw(bus, 0, 0, 16384, 0, 0, 0, 0);
    const auto status = driver.begin();
    assert(status.valid);
}

void testConfigurationAndCalibration() {
    fakeEspTimerNowUs = 0;
    fakeDelayCount = 0;
    fakeDelayedMs = 0;
    TwoWire bus;
    bus.registers[0x75] = 0x71;
    setRaw(bus, 0, 0, 16384, 0, 0, 0, 131);
    auto config = validConfig();
    MPU9250Driver driver(bus, config);

    assert(driver.begin().valid);
    assert(bus.timeoutMs == 50);
    assert(bus.requestCalls == 201);
    assert(fakeDelayCount == 199);
    assert(fakeDelayedMs == 995);
    assert(bus.registers[0x6B] == 0x00);
    assert(bus.registers[0x1B] == 0x00);
    assert(bus.registers[0x1C] == 0x00);
    assert(driver.offsets().valid);
    assert(std::fabs(driver.offsets().gyroRaw[2] - 131.0F) < 0.001F);
}

void testIdentityAndWriteFailures() {
    {
        TwoWire bus;
        bus.registers[0x75] = 0x70;
        MPU9250Driver driver(bus, validConfig());
        assert(driver.initialize() == Mpu9250Status::IdentityMismatch);
    }
    {
        TwoWire bus;
        bus.registers[0x75] = 0x71;
        bus.failTransmissionAt = 2;
        MPU9250Driver driver(bus, validConfig());
        assert(driver.initialize() == Mpu9250Status::I2cWriteFailed);
    }
    {
        TwoWire bus;
        bus.registers[0x75] = 0x71;
        bus.shortWriteAt = 2;
        MPU9250Driver driver(bus, validConfig());
        assert(driver.initialize() == Mpu9250Status::I2cWriteFailed);
    }
    {
        TwoWire bus;
        auto config = validConfig();
        config.accelGPerLsb = 1.0F / 8192.0F;
        MPU9250Driver driver(bus, config);
        assert(driver.initialize() == Mpu9250Status::InvalidConfiguration);
        assert(bus.transmissionCount == 0);
    }
    {
        TwoWire bus;
        auto config = validConfig();
        config.datasheetAssumptionsApproved = false;
        MPU9250Driver driver(bus, config);
        assert(driver.initialize() == Mpu9250Status::InvalidConfiguration);
        assert(bus.transmissionCount == 0);
    }
}

void testSignedRawTemperatureAndGoldenPose() {
    fakeEspTimerNowUs = 0;
    TwoWire bus;
    MPU9250Driver driver(bus, validConfig());
    prepare(bus, driver);
    setRaw(bus, -16384, 0, 0, -3339, -131, 0, 0);

    Mpu9250Sample sample{};
    assert(driver.readSample(sample) == Mpu9250Status::Ok);
    assert(sample.raw.accelX == -16384);
    assert(sample.raw.temperatureRaw == -3339);
    assert(sample.raw.gyroX == -131);
    assert(std::fabs(sample.physical.accelXG + 1.0F) < 0.0001F);
    assert(std::fabs(sample.temperatureC - 11.0F) < 0.01F);
    assert(sample.temperatureValid);
    assert(sample.tiltValid);
    assert(std::fabs(sample.physical.tiltPitchDeg - 90.0F) < 0.01F);
    assert(std::fabs(sample.tiltAngleDeg - 90.0F) < 0.01F);
}

void testGoldenRollPose() {
    fakeEspTimerNowUs = 0;
    TwoWire bus;
    MPU9250Driver driver(bus, validConfig());
    prepare(bus, driver);
    setRaw(bus, 0, 16384, 0, 0, 0, 0, 0);

    Mpu9250Sample sample{};
    assert(driver.readSample(sample) == Mpu9250Status::Ok);
    assert(sample.tiltValid);
    assert(std::fabs(sample.physical.tiltRollDeg - 90.0F) < 0.01F);
    assert(std::fabs(sample.physical.tiltPitchDeg) < 0.01F);
}

void testSensorFrameCarriesMpuFieldsAndSeparateTiltValidity() {
    fakeEspTimerNowUs = 0;
    TwoWire bus;
    MPU9250Driver driver(bus, validConfig());
    prepare(bus, driver);
    setRaw(bus, 0, 0, 16384, 667, 0, 0, 0);

    project::models::SensorFrame frame{};
    const auto status = driver.read(frame);
    assert(status.valid);
    assert(frame.imuStatus.valid);
    assert(frame.imu.temperatureRaw == 667);
    assert(std::fabs(frame.imuPhysical.temperatureC - 23.0F) < 0.01F);
    assert(frame.imuPhysical.temperatureValid);
    assert(frame.tiltAngleValid);
    assert(frame.imuPhysical.tiltRollValid);
    assert(frame.imuPhysical.tiltPitchValid);
    assert(frame.imuPhysical.tiltRollDeg == 0.0F);
    assert(frame.imuPhysical.tiltPitchDeg == 0.0F);

    bus.requestAdvanceUs = 0;
    const auto invalidDtStatus = driver.read(frame);
    assert(invalidDtStatus.valid);
    assert(!frame.tiltAngleValid);
}

void testTiltRemainsUncalculatedWithoutApprovedDefinition() {
    fakeEspTimerNowUs = 0;
    TwoWire bus;
    auto config = validConfig();
    config.tiltDefinitionApproved = false;
    config.maximumFilterGapMs = 0;
    MPU9250Driver driver(bus, config);
    prepare(bus, driver);

    Mpu9250Sample sample{};
    assert(driver.readSample(sample) == Mpu9250Status::Ok);
    assert(sample.accelValid);
    assert(sample.gyroValid);
    assert(sample.temperatureValid);
    assert(sample.tiltStatus == Mpu9250Status::TiltDefinitionNotApproved);
    assert(!sample.rollValid);
    assert(!sample.pitchValid);
    assert(!sample.tiltValid);
    assert(!sample.physical.tiltRollValid);
    assert(!sample.physical.tiltPitchValid);
}

void testComplementaryFilterUsesElapsedTime() {
    fakeEspTimerNowUs = 0;
    TwoWire bus;
    MPU9250Driver driver(bus, validConfig());
    prepare(bus, driver);
    Mpu9250Sample first{};
    assert(driver.readSample(first) == Mpu9250Status::Ok);
    assert(first.tiltValid);

    setRaw(bus, 0, 0, 16384, 0, 131, 0, 0);
    bus.requestAdvanceUs = 100000;
    Mpu9250Sample second{};
    assert(driver.readSample(second) == Mpu9250Status::Ok);
    assert(second.tiltValid);
    assert(std::fabs(second.physical.tiltRollDeg - 0.098F) < 0.002F);
}

void testInvalidDtAndLongGapResetFilter() {
    fakeEspTimerNowUs = 0;
    TwoWire bus;
    MPU9250Driver driver(bus, validConfig());
    prepare(bus, driver);

    Mpu9250Sample sample{};
    bus.requestAdvanceUs = 0;
    assert(driver.readSample(sample) == Mpu9250Status::Ok);
    assert(sample.tiltValid);
    assert(driver.readSample(sample) == Mpu9250Status::Ok);
    assert(!sample.tiltValid);

    bus.requestAdvanceUs = 1000;
    assert(driver.readSample(sample) == Mpu9250Status::Ok);
    assert(sample.tiltValid);

    fakeEspTimerNowUs += 501000;
    bus.requestAdvanceUs = 0;
    assert(driver.readSample(sample) == Mpu9250Status::Ok);
    assert(!sample.tiltValid);
    bus.requestAdvanceUs = 1000;
    assert(driver.readSample(sample) == Mpu9250Status::Ok);
    assert(sample.tiltValid);
}

void testShortReadAndNackDoNotReturnStaleSample() {
    fakeEspTimerNowUs = 0;
    TwoWire bus;
    MPU9250Driver driver(bus, validConfig());
    prepare(bus, driver);

    Mpu9250Sample sample{};
    sample.raw.accelX = 1234;
    bus.requestCountOverride = 13;
    assert(driver.readSample(sample) == Mpu9250Status::I2cTimeout);
    assert(sample.raw.accelX == 0);

    bus.requestCountOverride = 0;
    bus.responseByteLimit = 13;
    assert(driver.readSample(sample) == Mpu9250Status::I2cTimeout);
    assert(sample.raw.accelX == 0);

    bus.responseByteLimit = std::numeric_limits<std::size_t>::max();
    bus.failTransmissionAt =
        static_cast<std::uint8_t>(bus.transmissionCount + 1U);
    assert(driver.readSample(sample) == Mpu9250Status::I2cReadFailed);
    assert(sample.raw.accelX == 0);
}

}  // namespace

int main() {
    testConfigurationAndCalibration();
    testIdentityAndWriteFailures();
    testSignedRawTemperatureAndGoldenPose();
    testGoldenRollPose();
    testSensorFrameCarriesMpuFieldsAndSeparateTiltValidity();
    testTiltRemainsUncalculatedWithoutApprovedDefinition();
    testComplementaryFilterUsesElapsedTime();
    testInvalidDtAndLongGapResetFilter();
    testShortReadAndNackDoNotReturnStaleSample();
    return 0;
}
