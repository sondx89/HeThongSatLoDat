#pragma once

#include <cstdint>

#include "Models/DataContractTypes.h"

namespace project::models {
struct RiskAssessment {
    std::uint32_t schemaVersion{0};
    std::uint64_t bootId{0};
    std::uint64_t sequenceNo{0};
    RiskState risk{RiskState::Unknown};
    HealthState health{HealthState::Unknown};
    std::uint64_t timestampMs{0};
    std::uint64_t stateEnterMs{0};
    std::uint32_t persistenceMs{0};
    float riskScore{0.0F};
    bool riskScoreValid{false};
    std::uint16_t decisionReasonCode{0};  // TODO(TBD): define released reason catalog.
    std::uint32_t activeFaultFlags{0};
    AlarmOutputState alarmOutputState{AlarmOutputState::NotVerifiable};
    std::uint32_t riskConfigVersion{0};
    std::uint8_t activeConfigHashSha256[32]{};
    bool activeConfigHashValid{false};
    bool valid{false};
};
}  // namespace project::models
