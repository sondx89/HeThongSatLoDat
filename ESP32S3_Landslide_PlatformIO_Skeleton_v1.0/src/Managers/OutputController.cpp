#include "project/Managers/OutputController.h"

namespace project::managers {
namespace {

bool isKnownFeedbackState(project::models::AlarmOutputState state) noexcept {
    using project::models::AlarmOutputState;
    switch (state) {
    case AlarmOutputState::Off:
    case AlarmOutputState::On:
    case AlarmOutputState::Failed:
    case AlarmOutputState::NotPresent:
    case AlarmOutputState::NotVerifiable:
        return true;
    }
    return false;
}

}  // namespace

OutputCommand OutputController::deriveCommand(
    const project::models::RiskAssessment& assessment) noexcept {
    OutputCommand result{};
    if (isKnownFeedbackState(assessment.alarmOutputState)) {
        result.feedback = assessment.alarmOutputState;
    }

    if (!assessment.valid) {
        result.status = OutputCommandStatus::InvalidAssessment;
        return result;
    }
    if (!isKnownFeedbackState(assessment.alarmOutputState)) {
        result.status = OutputCommandStatus::InvalidFeedbackState;
        return result;
    }

    using project::models::RiskState;
    switch (assessment.risk) {
    case RiskState::Startup:
        result.kind = OutputCommandKind::Startup;
        break;
    case RiskState::Normal:
        result.kind = OutputCommandKind::Normal;
        break;
    case RiskState::Monitor:
        result.kind = OutputCommandKind::Monitor;
        break;
    case RiskState::DangerTest:
        result.kind = OutputCommandKind::DangerTest;
        break;
    case RiskState::Unknown:
        result.kind = OutputCommandKind::Unknown;
        break;
    default:
        result.status = OutputCommandStatus::InvalidRiskState;
        return result;
    }

    result.status = OutputCommandStatus::Valid;
    return result;
}

}  // namespace project::managers
