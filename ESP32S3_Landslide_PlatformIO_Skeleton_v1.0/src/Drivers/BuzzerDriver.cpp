#include "project/Drivers/BuzzerDriver.h"

namespace project::drivers {

BuzzerDriver::BuzzerDriver(IBuzzerPin& pin,
                           const BuzzerDriverConfig& config) noexcept
    : pin_(pin), config_(config) {
    report_.present = config.enabled;
    report_.feedback = config.enabled
                           ? project::models::AlarmOutputState::NotVerifiable
                           : project::models::AlarmOutputState::NotPresent;
    report_.status = config.enabled ? BuzzerDriverStatus::NotInitialized
                                    : BuzzerDriverStatus::NotPresent;
}

bool BuzzerDriver::electricalLevel(bool active) const noexcept {
    const bool activeHigh = config_.activeLevel == BuzzerActiveLevel::High;
    return active ? activeHigh : !activeHigh;
}

project::models::AlarmOutputState BuzzerDriver::readFeedback() noexcept {
    if (!pin_.hasFeedback()) {
        return project::models::AlarmOutputState::NotVerifiable;
    }
    bool active = false;
    if (!pin_.readFeedback(active) || active != report_.commandActive) {
        return project::models::AlarmOutputState::Failed;
    }
    return active ? project::models::AlarmOutputState::On
                  : project::models::AlarmOutputState::Off;
}

BuzzerDriverStatus BuzzerDriver::begin() noexcept {
    initialized_ = false;
    report_.commandActive = false;
    if (!config_.enabled) {
        report_.present = false;
        report_.feedback = project::models::AlarmOutputState::NotPresent;
        report_.status = BuzzerDriverStatus::NotPresent;
        return report_.status;
    }
    report_.present = true;
    if (!config_.pinConfigured || !config_.polarityVerified ||
        config_.activeLevel == BuzzerActiveLevel::Unconfigured) {
        report_.feedback = project::models::AlarmOutputState::NotVerifiable;
        report_.status = BuzzerDriverStatus::InvalidConfiguration;
        return report_.status;
    }
    if (!pin_.configureOutput(electricalLevel(false)) ||
        !pin_.writeLevel(electricalLevel(false))) {
        report_.feedback = project::models::AlarmOutputState::Failed;
        report_.status = BuzzerDriverStatus::OutputFailure;
        return report_.status;
    }

    initialized_ = true;
    report_.feedback = readFeedback();
    report_.status = report_.feedback ==
                             project::models::AlarmOutputState::Failed
                         ? BuzzerDriverStatus::OutputFailure
                         : BuzzerDriverStatus::Ok;
    return report_.status;
}

BuzzerDriverStatus BuzzerDriver::setActive(bool active) noexcept {
    if (!config_.enabled) {
        report_.present = false;
        report_.commandActive = false;
        report_.feedback = project::models::AlarmOutputState::NotPresent;
        report_.status = BuzzerDriverStatus::NotPresent;
        return report_.status;
    }
    if (!initialized_) {
        report_.commandActive = false;
        report_.feedback = project::models::AlarmOutputState::NotVerifiable;
        report_.status = BuzzerDriverStatus::NotInitialized;
        return report_.status;
    }

    report_.commandActive = active;
    if (!pin_.writeLevel(electricalLevel(active))) {
        report_.feedback = project::models::AlarmOutputState::Failed;
        report_.status = BuzzerDriverStatus::OutputFailure;
        return report_.status;
    }
    report_.feedback = readFeedback();
    report_.status = report_.feedback ==
                             project::models::AlarmOutputState::Failed
                         ? BuzzerDriverStatus::OutputFailure
                         : BuzzerDriverStatus::Ok;
    return report_.status;
}

}  // namespace project::drivers
