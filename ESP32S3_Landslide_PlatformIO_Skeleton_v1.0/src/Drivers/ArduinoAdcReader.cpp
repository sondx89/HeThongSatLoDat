#include "project/Drivers/IAdcReader.h"

#include <Arduino.h>

namespace project::drivers {

bool ArduinoAdcReader::readRaw(const std::uint8_t pin,
                               std::int32_t& output) noexcept {
    const int value = analogRead(pin);
    if (value < 0) {
        return false;
    }
    output = static_cast<std::int32_t>(value);
    return true;
}

}  // namespace project::drivers
