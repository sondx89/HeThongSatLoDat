#pragma once
#include "Models/SensorFrame.h"
#include "Models/ProcessedFrame.h"

namespace project::services {
class DataQualityService {
public:
    bool validate(const project::models::SensorFrame& frame);
    bool validate(const project::models::ProcessedFrame& frame);
};
}  // namespace project::services
