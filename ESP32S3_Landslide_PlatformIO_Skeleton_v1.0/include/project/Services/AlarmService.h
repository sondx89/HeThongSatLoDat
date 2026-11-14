#pragma once

#include <cstdint>

#include "Drivers/BuzzerDriver.h"
#include "Managers/OutputController.h"

namespace project::services {

inline constexpr std::uint32_t kWatchBuzzerCadenceMs = 200U;
inline constexpr std::uint32_t kWatchBuzzerCycleMs = 1200U;

enum class AlarmServiceStatus : std::uint8_t {
    Ok,
    NotInitialized,
    TimeWentBackwards,
    InvalidAssessment,
    InvalidRiskState,
    InvalidFeedbackState,
    OutputFailure,
    NotPresent,
    InvalidConfiguration
};

struct AlarmServiceReport {
    project::managers::OutputCommandKind command{
        project::managers::OutputCommandKind::Unknown};
    bool buzzerPresent{false};
    bool commandActive{false};
    project::models::AlarmOutputState feedback{
        project::models::AlarmOutputState::NotPresent};
    AlarmServiceStatus status{AlarmServiceStatus::NotInitialized};
};

class AlarmService final {
public:
    explicit AlarmService(project::drivers::BuzzerDriver& buzzer) noexcept;

    AlarmServiceStatus begin() noexcept;
    AlarmServiceStatus update(
        const project::models::RiskAssessment& assessment,
        std::uint64_t nowMs, AlarmServiceReport& output) noexcept;

private:
    project::drivers::BuzzerDriver& buzzer_;
    project::managers::OutputCommandKind previousCommand_{
        project::managers::OutputCommandKind::None};
    std::uint64_t commandEpochMs_{0U};
    std::uint64_t lastUpdateMs_{0U};
    bool initialized_{false};
};

}  // namespace project::services
