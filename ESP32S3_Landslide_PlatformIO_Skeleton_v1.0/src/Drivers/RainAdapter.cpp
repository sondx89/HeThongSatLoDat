#include "project/Drivers/RainAdapter.h"

#include <Arduino.h>

namespace project::drivers {

bool ArduinoRainDigitalReader::begin(const std::uint8_t pin) noexcept {
    pinMode(pin, INPUT);
    return true;
}

bool ArduinoRainDigitalReader::readLevel(
    const std::uint8_t pin,
    bool& levelHigh) noexcept {
    levelHigh = digitalRead(pin) == HIGH;
    return true;
}

RainAdapter::RainAdapter(RainSensorDriver& driver) noexcept
    : driver_(driver) {}

project::models::SensorStatus RainAdapter::begin() {
    return driver_.begin();
}

project::models::SensorStatus RainAdapter::read(
    project::models::SensorFrame& output) {
    return driver_.read(output);
}

project::models::SensorStatus RainAdapter::selfTest() {
    return driver_.selfTest();
}

}  // namespace project::drivers
