#pragma once
#include <cstdint>
#include "Models/RiskAssessment.h"

namespace project::models {
struct HealthSnapshot {
    HealthState state{HealthState::Unknown};
    std::uint64_t timestampMicros{0};
    std::uint32_t faultFlags{0};
};
}  // namespace project::models
