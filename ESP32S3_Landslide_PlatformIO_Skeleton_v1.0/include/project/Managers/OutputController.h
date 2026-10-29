#pragma once
#include "Models/RiskAssessment.h"

namespace project::managers {
class OutputController {
public:
    bool apply(const project::models::RiskAssessment& risk);
    void setFaultIndication();
};
}  // namespace project::managers
