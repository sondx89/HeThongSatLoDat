#include "project/Services/CalibrationService.h"

namespace project::services {

bool CalibrationService::loadCalibration() {
    return false;
}

bool CalibrationService::process(
    const project::models::SensorFrame& input,
    project::models::ProcessedFrame& output) {
    (void)input;
    output = {};
    return false;
}

}  // namespace project::services
