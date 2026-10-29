#pragma once
#include "Models/SensorFrame.h"
#include "Models/ProcessedFrame.h"

namespace project::services {
class CalibrationService {
public:
    bool loadCalibration();
    bool process(const project::models::SensorFrame& input,
                 project::models::ProcessedFrame& output);
};
}  // namespace project::services
