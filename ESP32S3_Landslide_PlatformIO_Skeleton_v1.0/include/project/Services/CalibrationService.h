#pragma once

#include "Models/DataContractTypes.h"
#include "Models/ProcessedFrame.h"
#include "Models/SensorFrame.h"

namespace project::services {

class CalibrationService {
public:
    bool loadCalibration();
    project::models::ApiStatus loadCalibrationStatus() const noexcept;

    bool process(const project::models::SensorFrame& input,
                 project::models::ProcessedFrame& output);

    project::models::ApiStatus processStatus(
        const project::models::SensorFrame& input,
        project::models::ProcessedFrame& output) const noexcept;
};
}  // namespace project::services
