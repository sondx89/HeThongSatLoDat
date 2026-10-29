#pragma once
#include "Models/SensorStatus.h"
#include "Models/SensorFrame.h"

namespace project::drivers {
class ISensor {
public:
    virtual ~ISensor() = default;
    virtual project::models::SensorStatus begin() = 0;
    virtual project::models::SensorStatus read(project::models::SensorFrame& output) = 0;
    virtual project::models::SensorStatus selfTest() = 0;
};
}  // namespace project::drivers
