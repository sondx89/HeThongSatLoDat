#pragma once

#include "Drivers/SoilMoistureDriver.h"
#include "Drivers/ISensor.h"

namespace project::drivers {
class SoilMoistureAdapter final : public ISensor {
public:
    explicit SoilMoistureAdapter(SoilMoistureDriver& driver) noexcept;

    project::models::SensorStatus begin() override;
    project::models::SensorStatus read(project::models::SensorFrame& output) override;
    project::models::SensorStatus selfTest() override;

private:
    SoilMoistureDriver& driver_;
};
}  // namespace project::drivers
