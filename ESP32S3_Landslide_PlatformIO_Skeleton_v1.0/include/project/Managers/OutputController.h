#pragma once

#include <cstdint>

#include "Models/RiskAssessment.h"

namespace project::managers {

enum class OutputCommandKind : std::uint8_t {
    None,
    Startup,
    Normal,
    Monitor,
    DangerTest,
    Unknown
};

enum class OutputCommandStatus : std::uint8_t {
    Valid,
    InvalidAssessment,
    InvalidRiskState,
    InvalidFeedbackState
};

struct OutputCommand {
    OutputCommandKind kind{OutputCommandKind::None};
    OutputCommandStatus status{OutputCommandStatus::InvalidAssessment};
    project::models::AlarmOutputState feedback{
        project::models::AlarmOutputState::NotVerifiable};
};

class OutputController {
public:
    static OutputCommand deriveCommand(
        const project::models::RiskAssessment& assessment) noexcept;
};

}  // namespace project::managers
