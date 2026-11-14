#pragma once

#include "Drivers/MPU9250Driver.h"
#include "Drivers/ISensor.h"

namespace project::drivers {
class ImuAdapter final : public ISensor {
public:
    explicit ImuAdapter(MPU9250Driver& driver) noexcept;

    project::models::SensorStatus begin() override;
    project::models::SensorStatus read(project::models::SensorFrame& output) override;
    project::models::SensorStatus selfTest() override;

private:
    MPU9250Driver& driver_;
};
}  // namespace project::drivers
