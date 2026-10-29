#pragma once
#include <cstdint>

namespace project::models {
enum class RiskState : std::uint8_t { Unknown, Normal, Watch, Warning, Critical };
enum class HealthState : std::uint8_t { Unknown, Healthy, Degraded, Fault };

struct RiskAssessment {
    RiskState risk{RiskState::Unknown};
    HealthState health{HealthState::Unknown};
    std::uint64_t timestampMicros{0};
    bool valid{false};
};
}  // namespace project::models
