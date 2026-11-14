#pragma once

#include "Drivers/HX710BDriver.h"
#include "Drivers/ISensor.h"

namespace project::drivers {

class Hx710Adapter final : public ISensor {
public:
    explicit Hx710Adapter(HX710BDriver& driver) noexcept;

    project::models::SensorStatus begin() override;
    project::models::SensorStatus read(
        project::models::SensorFrame& output) override;
    project::models::SensorStatus selfTest() override;

private:
    HX710BDriver& driver_;
};

}  // namespace project::drivers
