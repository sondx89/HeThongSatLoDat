#pragma once
#include "Drivers/ISensor.h"

namespace project::drivers {
class ImuAdapter final : public ISensor {
public:
    project::models::SensorStatus begin() override;
    project::models::SensorStatus read(project::models::SensorFrame& output) override;
    project::models::SensorStatus selfTest() override;
};
}  // namespace project::drivers
