#pragma once
#include "Models/RiskAssessment.h"

namespace project::managers {
// Current signatures and output-policy blockers are documented in
// docs/MODULE_API_CONTRACTS.md.
class OutputController {
public:
    bool apply(const project::models::RiskAssessment& risk);
    void setFaultIndication();
};
}  // namespace project::managers
