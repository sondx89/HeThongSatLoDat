#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <esp_timer.h>

#include "project/Drivers/SoilMoistureDriver.h"

namespace {

using project::bsp::BoardPins;
using project::drivers::IAdcReader;
using project::drivers::SoilMoistureCalibrationPoint;
using project::drivers::SoilMoistureChannelConfig;
using project::drivers::SoilMoistureDriver;
using project::drivers::SoilMoistureDriverConfig;
using project::drivers::SoilMoistureSample;
using project::drivers::SoilMoistureStatus;
using project::models::SoilMoistureQuality;

class FakeAdc final : public IAdcReader {
public:
    FakeAdc() {
        succeeds.fill(true);
    }

    bool readRaw(std::uint8_t pin, std::int32_t& output) noexcept override {
        ++readCounts[pin];
        if (!succeeds[pin]) {
            return false;
        }
        if (positions[pin] < scriptedValues[pin].size()) {
            output = scriptedValues[pin][positions[pin]++];
        } else {
            output = values[pin];
        }
        return true;
    }

    std::array<std::int32_t, 256> values{};
    std::array<bool, 256> succeeds{};
    std::array<std::uint32_t, 256> readCounts{};
    std::array<std::vector<std::int32_t>, 256> scriptedValues{};
    std::array<std::size_t, 256> positions{};
};

BoardPins configuredPins() {
    BoardPins pins{};
    pins.adc.soilMoisture[0].number = 1;
    pins.adc.soilMoisture[1].number = 2;
    pins.adc.soilMoisture[2].number = 3;
    return pins;
}

SoilMoistureChannelConfig calibration(
    bool required,
    std::uint16_t rawZero = 3000,
    std::uint16_t rawHundred = 1200) {
    SoilMoistureChannelConfig channel{};
    channel.present = true;
    channel.required = required;
    channel.calibrationPoints = {{
        SoilMoistureCalibrationPoint{rawZero, 0.0F},
        SoilMoistureCalibrationPoint{rawHundred, 100.0F},
    }};
    return channel;
}

SoilMoistureDriverConfig validConfig() {
    SoilMoistureDriverConfig config{};
    config.adcMaximumCount = 4095;
    config.channels[0] = calibration(true);
    config.channels[1] = calibration(true, 1200, 3000);
    config.requiredChannelCount = 2;
    config.maximumSampleAgeMs = 100;
    config.calibrationVersion = 7;
    config.adcConfigurationVerified = true;
    config.calibrationConfigured = true;
    return config;
}

void sampleAtFiveMillisecondSpacing(SoilMoistureDriver& driver,
                                    SoilMoistureSample& sample,
                                    std::uint8_t count) {
    for (std::uint8_t index = 0; index < count; ++index) {
        fakeSoilTimerNowUs += 5000;
        (void)driver.readSample(sample);
    }
}

void testIndependentCalibrationAndOptionalMissingProbe() {
    fakeSoilTimerNowUs = 1000000;
    auto pins = configuredPins();
    auto config = validConfig();
    FakeAdc adc;
    adc.values[1] = 2100;
    adc.values[2] = 2550;
    adc.values[3] = 1900;
    SoilMoistureDriver driver(adc, pins, config);
    assert(driver.begin().valid);

    SoilMoistureSample sample{};
    for (std::uint8_t index = 0; index < 5; ++index) {
        if (index != 0U) {
            fakeSoilTimerNowUs += 5000;
        }
        const auto status = driver.readSample(sample);
        assert(status == (index == 4U ? SoilMoistureStatus::Ok
                                      : SoilMoistureStatus::SamplePending));
        if (index < 4U) {
            assert(!sample.valid);
            assert(sample.probes[0].sampleCount == index + 1U);
        }
    }

    assert(sample.valid);
    assert(sample.validChannelCount == 2);
    assert(sample.requiredChannelCount == 2);
    assert(sample.rawChannelIndex == 0);
    assert(sample.probes[0].valid);
    assert(sample.probes[1].valid);
    assert(sample.probes[2].quality == SoilMoistureQuality::NotPresent);
    assert(!sample.probes[2].valid);
    assert(adc.readCounts[3] == 0U);
    assert(sample.probes[0].rawAdcCount == 2100);
    assert(sample.probes[1].rawAdcCount == 2550);
    assert(sample.probes[0].moisturePercent == 50.0F);
    assert(sample.probes[1].moisturePercent == 75.0F);
    assert(sample.moisturePercent == 62.5F);
    assert(sample.calibrationVersion == 7U);
    assert(sample.probes[0].timestampMs == 1020U);
}

void testPerChannelFailureIsExcludedFromAverage() {
    fakeSoilTimerNowUs = 0;
    auto pins = configuredPins();
    auto config = validConfig();
    config.channels[1] = calibration(false);
    config.channels[2] = calibration(true);
    FakeAdc adc;
    adc.values[1] = 2100;
    adc.values[2] = 0;
    adc.succeeds[2] = false;
    adc.values[3] = 2550;
    SoilMoistureDriver driver(adc, pins, config);
    assert(driver.begin().valid);

    SoilMoistureSample sample{};
    sampleAtFiveMillisecondSpacing(driver, sample, 5);
    assert(sample.valid);
    assert(sample.validChannelCount == 2);
    assert(sample.probes[1].quality == SoilMoistureQuality::AdcReadFailed);
    assert(!sample.probes[1].valid);
    assert(sample.probes[0].moisturePercent == 50.0F);
    assert(sample.probes[2].moisturePercent == 25.0F);
    assert(sample.moisturePercent == 37.5F);
}

void testEndpointsClampAndReversedCalibration() {
    fakeSoilTimerNowUs = 0;
    auto pins = configuredPins();
    auto config = validConfig();
    config.channels[1] = {};
    config.requiredChannelCount = 1;
    FakeAdc adc;
    adc.values[1] = 4095;
    SoilMoistureDriver driver(adc, pins, config);
    assert(driver.begin().valid);

    SoilMoistureSample sample{};
    sampleAtFiveMillisecondSpacing(driver, sample, 5);
    assert(sample.valid);
    assert(sample.moisturePercent == 0.0F);

    adc.values[1] = 1200;
    sampleAtFiveMillisecondSpacing(driver, sample, 5);
    assert(sample.valid);
    assert(sample.probes[0].moisturePercent == 100.0F);
    assert(sample.moisturePercent == 100.0F);
}

void testOutOfRangeSampleResetsOnlyThatProbeWindow() {
    fakeSoilTimerNowUs = 0;
    auto pins = configuredPins();
    auto config = validConfig();
    config.channels[1] = {};
    config.requiredChannelCount = 1;
    FakeAdc adc;
    adc.values[1] = 2100;
    adc.scriptedValues[1] = {2100, 2100, 2100, 2100, 5000};
    SoilMoistureDriver driver(adc, pins, config);
    assert(driver.begin().valid);

    SoilMoistureSample sample{};
    for (std::uint8_t index = 0; index < 4; ++index) {
        if (index != 0U) {
            fakeSoilTimerNowUs += 5000;
        }
        assert(driver.readSample(sample) == SoilMoistureStatus::SamplePending);
    }
    fakeSoilTimerNowUs += 5000;
    assert(driver.readSample(sample) ==
           SoilMoistureStatus::AdcValueOutOfRange);
    assert(!sample.valid);
    assert(sample.validChannelCount == 0U);
    assert(sample.probes[0].quality ==
           SoilMoistureQuality::AdcValueOutOfRange);

    for (std::uint8_t index = 0; index < 5; ++index) {
        fakeSoilTimerNowUs += 5000;
        assert(driver.readSample(sample) ==
               (index == 4U ? SoilMoistureStatus::Ok
                            : SoilMoistureStatus::SamplePending));
    }
    assert(sample.valid);
    assert(sample.probes[0].sampleCount == 5U);
}

void testStaleProbeIsInvalidatedAndWarmedAgain() {
    fakeSoilTimerNowUs = 0;
    auto pins = configuredPins();
    auto config = validConfig();
    config.channels[1] = {};
    config.requiredChannelCount = 1;
    config.maximumSampleAgeMs = 20;
    FakeAdc adc;
    adc.values[1] = 2100;
    SoilMoistureDriver driver(adc, pins, config);
    assert(driver.begin().valid);

    SoilMoistureSample sample{};
    sampleAtFiveMillisecondSpacing(driver, sample, 5);
    assert(sample.valid);

    fakeSoilTimerNowUs += 21000;
    assert(driver.readSample(sample) == SoilMoistureStatus::SamplePending);
    assert(!sample.probes[0].valid);
    assert(sample.probes[0].quality == SoilMoistureQuality::Stale);
    assert(sample.validChannelCount == 0U);

    assert(driver.readSample(sample) == SoilMoistureStatus::SamplePending);
    assert(sample.probes[0].sampleCount == 1U);
    assert(sample.probes[0].quality == SoilMoistureQuality::WarmingUp);
}

void testInvalidCalibrationAndPinConfigurationFailClosed() {
    fakeSoilTimerNowUs = 0;
    auto pins = configuredPins();
    auto config = validConfig();
    config.channels[0].calibrationPoints[0].moisturePercent = 20.0F;
    FakeAdc adc;
    SoilMoistureDriver driver(adc, pins, config);
    assert(!driver.begin().valid);
    assert(driver.lastStatus() == SoilMoistureStatus::InvalidCalibration);

    config = validConfig();
    config.requiredChannelCount = 3;
    SoilMoistureDriver missingRequired(adc, pins, config);
    assert(!missingRequired.begin().valid);
    assert(missingRequired.lastStatus() ==
           SoilMoistureStatus::InvalidConfiguration);

    config = validConfig();
    config.maximumSampleAgeMs = 19;
    SoilMoistureDriver insufficientAge(adc, pins, config);
    assert(!insufficientAge.begin().valid);
    assert(insufficientAge.lastStatus() ==
           SoilMoistureStatus::InvalidConfiguration);
}

}  // namespace

int main() {
    testIndependentCalibrationAndOptionalMissingProbe();
    testPerChannelFailureIsExcludedFromAverage();
    testEndpointsClampAndReversedCalibration();
    testOutOfRangeSampleResetsOnlyThatProbeWindow();
    testStaleProbeIsInvalidatedAndWarmedAgain();
    testInvalidCalibrationAndPinConfigurationFailClosed();
    return 0;
}
