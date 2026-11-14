#pragma once

#include "Drivers/ISensor.h"
#include "Drivers/RainSensorDriver.h"

namespace project::drivers {

class ArduinoRainDigitalReader final : public IRainDigitalReader {
public:
    bool begin(std::uint8_t pin) noexcept override;
    bool readLevel(std::uint8_t pin, bool& levelHigh) noexcept override;
};

class RainAdapter final : public ISensor {
public:
    explicit RainAdapter(RainSensorDriver& driver) noexcept;

    project::models::SensorStatus begin() override;
    project::models::SensorStatus read(
        project::models::SensorFrame& output) override;
    project::models::SensorStatus selfTest() override;

private:
    RainSensorDriver& driver_;
};

}  // namespace project::drivers
