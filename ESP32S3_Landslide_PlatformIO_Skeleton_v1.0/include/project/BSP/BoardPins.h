#pragma once

#include <cstdint>

namespace project::bsp {

struct GpioPin {
    static constexpr std::int16_t kUnconfigured = -1;
    static constexpr std::int16_t kMaxApiPinNumber = 255;

    std::int16_t number{kUnconfigured};

    constexpr bool isConfigured() const noexcept {
        return number >= 0;
    }

    constexpr bool isWellFormed() const noexcept {
        // Bound the conversion to Arduino's uint8_t pin API. The actual
        // ESP32-S3 package/board-available pin set remains a hardware-review TODO.
        return number == kUnconfigured ||
               (number >= 0 && number <= kMaxApiPinNumber);
    }
};

struct I2cPins {
    GpioPin sda{};
    GpioPin scl{};
};

struct AdcPins {
    GpioPin soilMoisture{};
    GpioPin rainSensor{};
};

struct SensorDigitalPins {
    GpioPin soilMoisture{};
    GpioPin rainSensor{};
};

struct Hx710Pins {
    GpioPin clock{};
    GpioPin data{};
};

struct SpiPins {
    GpioPin clock{};
    GpioPin miso{};
    GpioPin mosi{};
    GpioPin chipSelect{};
};

struct SdMmcPins {
    GpioPin clock{};
    GpioPin command{};
    GpioPin data0{};
    GpioPin data1{};
    GpioPin data2{};
    GpioPin data3{};
};

struct AlarmPins {
    GpioPin led{};
    GpioPin buzzer{};
    GpioPin relay{};
    GpioPin feedback{};
};

struct BoardPins {
    I2cPins imuI2c{};
    AdcPins adc{};
    SensorDigitalPins digitalSensors{};
    Hx710Pins hx710{};
    SpiPins storageSpi{};
    SdMmcPins storageSdMmc{};
    AlarmPins alarm{};
};

// Pin assignments remain unset until the board schematic/BOM and electrical
// review are approved. Keep every GPIO assignment in this file.
inline constexpr BoardPins kBoardPins{};

constexpr bool pinPairIsComplete(GpioPin first, GpioPin second) noexcept {
    return first.isConfigured() == second.isConfigured();
}

constexpr bool pinsDoNotConflict(const BoardPins& pins) noexcept {
    if (!pinPairIsComplete(pins.imuI2c.sda, pins.imuI2c.scl) ||
        !pinPairIsComplete(pins.hx710.clock, pins.hx710.data)) {
        return false;
    }

    const GpioPin allPins[] = {
        pins.imuI2c.sda,
        pins.imuI2c.scl,
        pins.adc.soilMoisture,
        pins.adc.rainSensor,
        pins.digitalSensors.soilMoisture,
        pins.digitalSensors.rainSensor,
        pins.hx710.clock,
        pins.hx710.data,
        pins.storageSpi.clock,
        pins.storageSpi.miso,
        pins.storageSpi.mosi,
        pins.storageSpi.chipSelect,
        pins.storageSdMmc.clock,
        pins.storageSdMmc.command,
        pins.storageSdMmc.data0,
        pins.storageSdMmc.data1,
        pins.storageSdMmc.data2,
        pins.storageSdMmc.data3,
        pins.alarm.led,
        pins.alarm.buzzer,
        pins.alarm.relay,
        pins.alarm.feedback,
    };

    for (std::uint32_t i = 0; i < sizeof(allPins) / sizeof(allPins[0]); ++i) {
        if (!allPins[i].isWellFormed()) {
            return false;
        }
        if (!allPins[i].isConfigured()) {
            continue;
        }
        for (std::uint32_t j = i + 1; j < sizeof(allPins) / sizeof(allPins[0]); ++j) {
            if (allPins[j].isConfigured() &&
                allPins[i].number == allPins[j].number) {
                return false;
            }
        }
    }
    return true;
}

bool validateBoardPins(const BoardPins& pins) noexcept;

static_assert(pinsDoNotConflict(kBoardPins),
              "Board pin mapping has incomplete pairs or conflicting GPIO assignments");

}  // namespace project::bsp
