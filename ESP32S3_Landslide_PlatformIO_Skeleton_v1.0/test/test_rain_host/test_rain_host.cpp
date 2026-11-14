#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

#include <esp_timer.h>

#include "project/Drivers/RainSensorDriver.h"

std::int64_t fakeRainTimerNowUs = 0;

namespace {

using project::bsp::BoardPins;
using project::drivers::IAdcReader;
using project::drivers::IRainDigitalReader;
using project::drivers::RainCalibrationPoint;
using project::drivers::RainSensorDriver;
using project::drivers::RainSensorDriverConfig;
using project::drivers::RainSensorSample;
using project::drivers::RainSensorStatus;
using project::drivers::RainSensorStatus;

class FakeAdc final : public IAdcReader {
public:
    bool readRaw(std::uint8_t, std::int32_t& output) noexcept override {
        ++readCount;
        if (!succeeds) {
            return false;
        }
        output = value;
        return true;
    }

    std::int32_t value{4094};
    std::uint32_t readCount{0};
    bool succeeds{true};
};

class FakeDigital final : public IRainDigitalReader {
public:
    bool begin(std::uint8_t pin) noexcept override {
        configuredPin = pin;
        return beginSucceeds;
    }

    bool readLevel(std::uint8_t pin, bool& levelHigh) noexcept override {
        lastReadPin = pin;
        ++readCount;
        if (!succeeds) {
            return false;
        }
        levelHigh = levelHighValue;
        return true;
    }

    std::uint8_t configuredPin{0};
    std::uint8_t lastReadPin{0};
    std::uint32_t readCount{0};
    bool levelHighValue{true};
    bool beginSucceeds{true};
    bool succeeds{true};
};

BoardPins configuredPins() {
    BoardPins pins{};
    pins.adc.rainSensor.number = 5;
    pins.digitalSensors.rainSensor.number = 6;
    return pins;
}

RainSensorDriverConfig validConfig(bool enableDigital = true) {
    RainSensorDriverConfig config{};
    config.adcMaximumCount = 4095;
    config.accumulationSamplePeriodMs = 500;
    config.maximumSampleGapMs = 1000;
    config.calibrationPoints = {{
        RainCalibrationPoint{4094, 0.0F},
        RainCalibrationPoint{0, 100.0F},
    }};
    config.calibrationVersion = 4;
    config.adcConfigurationVerified = true;
    config.wetnessSemanticsVerified = true;
    config.calibrationConfigured = true;
    config.digitalOutputEnabled = enableDigital;
    config.digitalActiveLowVerified = enableDigital;
    return config;
}

void completeAnalogReading(RainSensorDriver& driver,
                           RainSensorSample& sample) {
    for (std::size_t i = 0;
         i < RainSensorDriver::kAnalogSamplesPerReading; ++i) {
        if (i != 0U) {
            fakeRainTimerNowUs += static_cast<std::int64_t>(
                RainSensorDriver::kMinimumAnalogSampleSpacingUs);
        }
        (void)driver.readSample(sample);
    }
}

void advanceAndComplete(RainSensorDriver& driver,
                        RainSensorSample& sample,
                        std::uint32_t intervalMs) {
    fakeRainTimerNowUs += static_cast<std::int64_t>(intervalMs) * 1000;
    completeAnalogReading(driver, sample);
}

void testAnalogEndpointsAndIndependentActiveLowDigital() {
    fakeRainTimerNowUs = 1000000;
    auto pins = configuredPins();
    auto config = validConfig();
    FakeAdc adc;
    FakeDigital digital;
    digital.levelHighValue = false;
    RainSensorDriver driver(adc, digital, pins, config);
    assert(driver.begin().valid);
    assert(digital.configuredPin == 6U);

    RainSensorSample sample{};
    completeAnalogReading(driver, sample);
    assert(sample.valid);
    assert(sample.rawAdcCount == 4094);
    assert(sample.filterSampleCount ==
           RainSensorDriver::kAnalogSamplesPerReading);
    assert(sample.wetnessRelativePercent == 0.0F);
    assert(sample.digitalValid);
    assert(sample.digitalWet);
    assert(sample.timestampMs == 1014U);
    assert(adc.readCount == 8U);
    assert(digital.readCount == 8U);

    adc.value = 4095;
    advanceAndComplete(driver, sample, 500U);
    assert(sample.valid);
    assert(sample.wetnessRelativePercent == 0.0F);
    assert((sample.qualityFlags &
            project::drivers::rainQualityMask(
                project::drivers::RainQualityFlag::WetnessClampedLow)) != 0U);

    auto wetConfig = validConfig(false);
    FakeAdc wetAdc;
    wetAdc.value = 0;
    FakeDigital unusedDigital;
    RainSensorDriver wetDriver(wetAdc, unusedDigital, pins, wetConfig);
    assert(wetDriver.begin().valid);
    fakeRainTimerNowUs += 1000000;
    completeAnalogReading(wetDriver, sample);
    assert(sample.valid);
    assert(sample.wetnessRelativePercent == 100.0F);
    assert(!sample.digitalValid);
}

void testIrregularTimeIntegrationAndTrailingWindowEviction() {
    fakeRainTimerNowUs = 1000000;
    auto pins = configuredPins();
    auto config = validConfig(false);
    config.calibrationPoints = {{
        RainCalibrationPoint{4094, 0.0F},
        RainCalibrationPoint{0, 100.0F},
    }};
    FakeAdc adc;
    adc.value = 2047;
    FakeDigital digital;
    RainSensorDriver driver(adc, digital, pins, config);
    assert(driver.begin().valid);

    RainSensorSample sample{};
    completeAnalogReading(driver, sample);
    assert(sample.wetnessRelativePercent == 50.0F);
    assert(!sample.rainAccValid);
    std::uint32_t iteration = 0;
    while (sample.timestampMs <
           1014U + RainSensorDriver::kAccumulationWindowMs) {
        const std::uint32_t intervals[] = {500U, 750U, 250U, 600U};
        advanceAndComplete(driver, sample,
                           intervals[iteration++ % 4U]);
    }
    assert(sample.rainAccValid);
    assert(sample.rainAccCoverageMs ==
           RainSensorDriver::kAccumulationWindowMs);
    assert(std::fabs(sample.rainAccEquivalentWetSeconds - 60.0F) < 0.02F);

    adc.value = 4094;
    advanceAndComplete(driver, sample, 750U);
    const auto dryWindowStart = sample.timestampMs;
    while (sample.timestampMs <
           dryWindowStart + RainSensorDriver::kAccumulationWindowMs) {
        advanceAndComplete(driver, sample, 750U);
    }
    assert(sample.rainAccValid);
    assert(sample.rainAccEquivalentWetSeconds < 0.02F);
}

void testLargeGapInvalidatesCoverageInsteadOfAssumingDry() {
    fakeRainTimerNowUs = 1000000;
    auto pins = configuredPins();
    auto config = validConfig(false);
    FakeAdc adc;
    adc.value = 0;
    FakeDigital digital;
    RainSensorDriver driver(adc, digital, pins, config);
    assert(driver.begin().valid);

    RainSensorSample sample{};
    completeAnalogReading(driver, sample);
    fakeRainTimerNowUs +=
        static_cast<std::int64_t>(config.maximumSampleGapMs + 1U) * 1000;
    (void)driver.readSample(sample);
    assert(!sample.rainAccValid);
    assert(!sample.valid);
    assert(sample.accumulationStatus.health ==
           project::models::SensorHealth::Degraded);
}

void testAnalogAndDigitalFailuresRemainIndependent() {
    fakeRainTimerNowUs = 1000000;
    auto pins = configuredPins();
    auto config = validConfig();
    FakeAdc adc;
    adc.succeeds = false;
    FakeDigital digital;
    digital.levelHighValue = true;
    RainSensorDriver driver(adc, digital, pins, config);
    assert(driver.begin().valid);

    RainSensorSample sample{};
    (void)driver.readSample(sample);
    assert(!sample.valid);
    assert(sample.digitalValid);
    assert(!sample.digitalWet);
    assert(sample.analogStatus.health ==
           project::models::SensorHealth::Degraded);

    fakeRainTimerNowUs += 1000000;
    adc.succeeds = true;
    digital.succeeds = false;
    completeAnalogReading(driver, sample);
    assert(sample.valid);
    assert(!sample.digitalValid);
    assert(sample.analogStatus.valid);
}

void testUnverifiedDigitalPolarityDoesNotDisableAnalog() {
    fakeRainTimerNowUs = 1000000;
    auto pins = configuredPins();
    auto config = validConfig();
    config.digitalActiveLowVerified = false;
    FakeAdc adc;
    FakeDigital digital;
    RainSensorDriver driver(adc, digital, pins, config);
    assert(driver.begin().valid);
    RainSensorSample sample{};
    completeAnalogReading(driver, sample);
    assert(sample.valid);
    assert(!sample.digitalValid);
    assert(sample.digitalStatus.health ==
           project::models::SensorHealth::Degraded);
    assert(digital.readCount == 0U);
}

void testUnverifiedAdcConfigurationFailsClosed() {
    auto pins = configuredPins();
    auto config = validConfig(false);
    config.adcConfigurationVerified = false;
    FakeAdc adc;
    FakeDigital digital;
    RainSensorDriver driver(adc, digital, pins, config);
    assert(!driver.begin().valid);
    assert(driver.lastStatus() ==
           RainSensorStatus::InvalidConfiguration);
}

void testCalibrationMustCoverRelativeWetnessEndpoints() {
    auto pins = configuredPins();
    auto config = validConfig(false);
    config.calibrationPoints[0].wetnessPercent = 10.0F;
    config.calibrationPoints[1].wetnessPercent = 90.0F;
    FakeAdc adc;
    FakeDigital digital;
    RainSensorDriver driver(adc, digital, pins, config);
    assert(!driver.begin().valid);
    assert(driver.lastStatus() == RainSensorStatus::InvalidCalibration);
}

}  // namespace

int main() {
    testAnalogEndpointsAndIndependentActiveLowDigital();
    testIrregularTimeIntegrationAndTrailingWindowEviction();
    testLargeGapInvalidatesCoverageInsteadOfAssumingDry();
    testAnalogAndDigitalFailuresRemainIndependent();
    testUnverifiedDigitalPolarityDoesNotDisableAnalog();
    testUnverifiedAdcConfigurationFailsClosed();
    testCalibrationMustCoverRelativeWetnessEndpoints();
    return 0;
}
