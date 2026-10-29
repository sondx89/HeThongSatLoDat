#pragma once
#include "Models/SensorFrame.h"
#include "Models/SensorStatus.h"

namespace project::managers {
class SensorManager {
public:
    bool beginAll();
    bool pollDue(std::uint64_t nowMicros);
    bool readSnapshot(project::models::SensorFrame& output);
};
}  // namespace project::managers
