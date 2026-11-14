#include "project/Services/AlarmService.h"

namespace project::services {

AlarmService::AlarmService(project::drivers::BuzzerDriver& buzzer) noexcept
    : buzzer_(buzzer) {}

AlarmServiceStatus AlarmService::begin() noexcept {
    initialized_ = false;
    const project::drivers::BuzzerDriverStatus status = buzzer_.begin();
    if (status == project::drivers::BuzzerDriverStatus::NotPresent) {
        return AlarmServiceStatus::NotPresent;
    }
    if (status == project::drivers::BuzzerDriverStatus::InvalidConfiguration) {
        return AlarmServiceStatus::InvalidConfiguration;
    }
    if (status != project::drivers::BuzzerDriverStatus::Ok) {
        return AlarmServiceStatus::OutputFailure;
    }
    initialized_ = true;
    previousCommand_ = project::managers::OutputCommandKind::None;
    commandEpochMs_ = 0U;
    lastUpdateMs_ = 0U;
    return AlarmServiceStatus::Ok;
}

AlarmServiceStatus AlarmService::update(
    const project::models::RiskAssessment& assessment,
    std::uint64_t nowMs, AlarmServiceReport& output) noexcept {
    output = {};
    const project::drivers::BuzzerOutputReport buzzerReport = buzzer_.report();
    output.buzzerPresent = buzzerReport.present;
    output.feedback = buzzerReport.feedback;
    if (!initialized_) {
        output.status = buzzerReport.status ==
                                project::drivers::BuzzerDriverStatus::NotPresent
                            ? AlarmServiceStatus::NotPresent
                            : AlarmServiceStatus::NotInitialized;
        return output.status;
    }
    if (nowMs < lastUpdateMs_) {
        const project::drivers::BuzzerDriverStatus driverStatus =
            buzzer_.setActive(false);
        const project::drivers::BuzzerOutputReport safeReport =
            buzzer_.report();
        output.commandActive = safeReport.commandActive;
        output.feedback = safeReport.feedback;
        output.buzzerPresent = safeReport.present;
        output.status =
            driverStatus == project::drivers::BuzzerDriverStatus::OutputFailure
                ? AlarmServiceStatus::OutputFailure
                : AlarmServiceStatus::TimeWentBackwards;
        return output.status;
    }

    const project::managers::OutputCommand command =
        project::managers::OutputController::deriveCommand(assessment);
    output.command = command.kind;
    if (command.kind != previousCommand_) {
        commandEpochMs_ = nowMs;
        previousCommand_ = command.kind;
    }

    bool active = false;
    if (command.status == project::managers::OutputCommandStatus::Valid) {
        switch (command.kind) {
        case project::managers::OutputCommandKind::Monitor: {
            const std::uint64_t phase =
                ((nowMs - commandEpochMs_) % kWatchBuzzerCycleMs) /
                kWatchBuzzerCadenceMs;
            active = (phase & 1U) == 0U;
            break;
        }
        case project::managers::OutputCommandKind::DangerTest:
            active = true;
            break;
        case project::managers::OutputCommandKind::None:
        case project::managers::OutputCommandKind::Startup:
        case project::managers::OutputCommandKind::Normal:
        case project::managers::OutputCommandKind::Unknown:
            active = false;
            break;
        }
    }

    const project::drivers::BuzzerDriverStatus driverStatus =
        buzzer_.setActive(active);
    const project::drivers::BuzzerOutputReport finalReport = buzzer_.report();
    output.commandActive = finalReport.commandActive;
    output.feedback = finalReport.feedback;
    output.buzzerPresent = finalReport.present;
    lastUpdateMs_ = nowMs;
    if (driverStatus == project::drivers::BuzzerDriverStatus::OutputFailure) {
        output.status = AlarmServiceStatus::OutputFailure;
    } else if (driverStatus ==
               project::drivers::BuzzerDriverStatus::NotPresent) {
        output.status = AlarmServiceStatus::NotPresent;
    } else if (command.status ==
               project::managers::OutputCommandStatus::InvalidAssessment) {
        output.status = AlarmServiceStatus::InvalidAssessment;
    } else if (command.status ==
               project::managers::OutputCommandStatus::InvalidRiskState) {
        output.status = AlarmServiceStatus::InvalidRiskState;
    } else if (command.status ==
               project::managers::OutputCommandStatus::InvalidFeedbackState) {
        output.status = AlarmServiceStatus::InvalidFeedbackState;
    } else {
        output.status = AlarmServiceStatus::Ok;
    }
    return output.status;
}

}  // namespace project::services
