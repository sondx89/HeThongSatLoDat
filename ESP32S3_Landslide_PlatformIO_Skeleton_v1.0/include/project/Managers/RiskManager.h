#pragma once
#include "Models/ModelOutput.h"
#include "Models/HealthSnapshot.h"
#include "Models/RiskAssessment.h"
#include "Models/ProcessedFrame.h"

namespace project::managers {
class RiskManager {
public:
    bool evaluate(const project::models::ModelOutput& model,
                  const project::models::ProcessedFrame& frame,
                  const project::models::HealthSnapshot& health,
                  project::models::RiskAssessment& output);
};
}  // namespace project::managers
