#include "project/Managers/AppController.h"

namespace project::managers {
namespace {

constexpr std::array<AppStartupPhase, AppController::kPhaseCount> kPhases = {
    AppStartupPhase::BspSafeInit,
    AppStartupPhase::Diagnostics,
    AppStartupPhase::Configuration,
    AppStartupPhase::Storage,
    AppStartupPhase::Sensors,
    AppStartupPhase::Calibration,
    AppStartupPhase::SchemaModelGate,
    AppStartupPhase::RiskOutputHealth,
    AppStartupPhase::Tasks,
    AppStartupPhase::SelfCheck};

}  // namespace

AppController::AppController(IAppLifecycle& lifecycle,
                             const AppControllerConfig& config) noexcept
    : lifecycle_(lifecycle), config_(config) {}

bool AppController::configIsValid() const noexcept {
    return config_.healthCheckTimeoutMs != 0U &&
           config_.taskDrainTimeoutMs != 0U &&
           config_.storageFlushTimeoutMs != 0U;
}

bool AppController::disableInference() noexcept {
    if (!lifecycle_.initializeInferenceGate(false)) {
        inferenceEnabled_ = false;
        return false;
    }
    inferenceEnabled_ = false;
    return true;
}

bool AppController::cleanupStartedPhases(
    std::size_t phaseCount) noexcept {
    bool succeeded = true;
    if (phaseCount > kPhaseCount) {
        phaseCount = kPhaseCount;
        succeeded = false;
    }

    for (std::size_t index = phaseCount; index > 0U; --index) {
        const AppStartupPhase phase = kPhases[index - 1U];
        if (phase == AppStartupPhase::Storage) {
            if (config_.storageFlushTimeoutMs == 0U ||
                !lifecycle_.flushStorageBounded(
                    config_.storageFlushTimeoutMs)) {
                succeeded = false;
            }
        }
        if (phase == AppStartupPhase::BspSafeInit &&
            !lifecycle_.configureOutputsSafe()) {
            succeeded = false;
        }
        if (!lifecycle_.cleanupPhase(phase)) {
            succeeded = false;
        }
    }
    return succeeded;
}

bool AppController::shutdownSequence(std::size_t phaseCount) noexcept {
    bool succeeded = disableInference();
    if (!lifecycle_.stopAcceptingWork()) {
        succeeded = false;
    }
    if (config_.taskDrainTimeoutMs == 0U ||
        !lifecycle_.drainAndStopTasks(config_.taskDrainTimeoutMs)) {
        succeeded = false;
    }

    if (!cleanupStartedPhases(phaseCount)) {
        succeeded = false;
    }
    if (phaseCount == 0U && !lifecycle_.configureOutputsSafe()) {
        succeeded = false;
    }
    return succeeded;
}

AppControllerStatus AppController::failStartup(
    AppControllerStatus status, std::size_t attemptedPhaseCount) noexcept {
    state_ = AppControllerState::Faulted;
    const bool disabled = disableInference();
    const bool stopped = lifecycle_.stopAcceptingWork();
    const bool drained =
        config_.taskDrainTimeoutMs != 0U &&
        lifecycle_.drainAndStopTasks(config_.taskDrainTimeoutMs);
    const bool cleaned = cleanupStartedPhases(attemptedPhaseCount);
    completedPhaseCount_ = 0U;
    lastStatus_ = status;
    if (!disabled || !stopped || !drained || !cleaned) {
        lastStatus_ = AppControllerStatus::ShutdownFailed;
    }
    return lastStatus_;
}

AppControllerStatus AppController::begin() noexcept {
    if (state_ == AppControllerState::Running) {
        return AppControllerStatus::Ok;
    }
    if (state_ != AppControllerState::Idle) {
        lastStatus_ = AppControllerStatus::InvalidState;
        return lastStatus_;
    }
    if (!configIsValid()) {
        state_ = AppControllerState::Faulted;
        lastStatus_ = disableInference()
                          ? AppControllerStatus::InvalidConfiguration
                          : AppControllerStatus::InferenceGateFailed;
        return lastStatus_;
    }

    state_ = AppControllerState::Starting;
    if (!disableInference()) {
        return failStartup(AppControllerStatus::InferenceGateFailed, 0U);
    }

    std::size_t attemptedPhaseCount = 0U;
    for (const AppStartupPhase phase : kPhases) {
        ++attemptedPhaseCount;
        if (!lifecycle_.startPhase(phase)) {
            return failStartup(AppControllerStatus::StartupPhaseFailed,
                               attemptedPhaseCount);
        }
        completedPhaseCount_ = attemptedPhaseCount;
    }

    if (!lifecycle_.initializeInferenceGate(true)) {
        return failStartup(AppControllerStatus::InferenceGateFailed,
                           attemptedPhaseCount);
    }
    inferenceEnabled_ = true;
    state_ = AppControllerState::Running;
    lastStatus_ = AppControllerStatus::Ok;
    return lastStatus_;
}

AppControllerStatus AppController::runHealthChecks() noexcept {
    if (state_ != AppControllerState::Running) {
        lastStatus_ = AppControllerStatus::InvalidState;
        return lastStatus_;
    }
    if (config_.healthCheckTimeoutMs == 0U) {
        failStartup(AppControllerStatus::InvalidConfiguration,
                    completedPhaseCount_);
        return lastStatus_;
    }
    if (!lifecycle_.healthCheckBounded(config_.healthCheckTimeoutMs)) {
        failStartup(AppControllerStatus::HealthCheckFailed,
                    completedPhaseCount_);
        return lastStatus_;
    }
    lastStatus_ = AppControllerStatus::Ok;
    return lastStatus_;
}

AppControllerStatus AppController::requestShutdown() noexcept {
    if (state_ == AppControllerState::Shutdown) {
        return lastStatus_;
    }
    if (state_ == AppControllerState::ShuttingDown) {
        lastStatus_ = AppControllerStatus::InvalidState;
        return lastStatus_;
    }

    state_ = AppControllerState::ShuttingDown;
    const bool succeeded = shutdownSequence(completedPhaseCount_);
    completedPhaseCount_ = 0U;
    inferenceEnabled_ = false;
    state_ = AppControllerState::Shutdown;
    lastStatus_ = succeeded ? AppControllerStatus::Ok
                            : AppControllerStatus::ShutdownFailed;
    return lastStatus_;
}

}  // namespace project::managers
