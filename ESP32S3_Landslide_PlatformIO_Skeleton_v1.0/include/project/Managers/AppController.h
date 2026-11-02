#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace project::bsp { class BoardSupportPackage; }

namespace project::managers {

enum class AppStartupPhase : std::uint8_t {
    BspSafeInit,
    Diagnostics,
    Configuration,
    Storage,
    Sensors,
    Calibration,
    SchemaModelGate,
    RiskOutputHealth,
    Tasks,
    SelfCheck,
    Count
};

enum class AppControllerState : std::uint8_t {
    Idle,
    Starting,
    Running,
    Faulted,
    ShuttingDown,
    Shutdown
};

enum class AppControllerStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    InvalidState,
    StartupPhaseFailed,
    InferenceGateFailed,
    HealthCheckFailed,
    ShutdownFailed
};

struct AppControllerConfig {
    // Select all limits from reviewed target policy; zero means unconfigured.
    std::uint32_t healthCheckTimeoutMs{0U};
    std::uint32_t taskDrainTimeoutMs{0U};
    std::uint32_t storageFlushTimeoutMs{0U};
};

class IAppLifecycle {
public:
    virtual ~IAppLifecycle() = default;

    virtual bool initializeInferenceGate(bool enabled) noexcept = 0;
    virtual bool startPhase(AppStartupPhase phase) noexcept = 0;
    virtual bool cleanupPhase(AppStartupPhase phase) noexcept = 0;
    virtual bool stopAcceptingWork() noexcept = 0;
    virtual bool drainAndStopTasks(
        std::uint32_t timeoutMs) noexcept = 0;
    virtual bool flushStorageBounded(std::uint32_t timeoutMs) noexcept = 0;
    virtual bool configureOutputsSafe() noexcept = 0;
    virtual bool healthCheckBounded(
        std::uint32_t timeoutMs) noexcept = 0;
};

class AppController final {
public:
    explicit AppController(IAppLifecycle& lifecycle,
                           const AppControllerConfig& config) noexcept;
    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;
    AppController(AppController&&) = delete;
    AppController& operator=(AppController&&) = delete;

    AppControllerStatus begin() noexcept;
    AppControllerStatus runHealthChecks() noexcept;
    AppControllerStatus requestShutdown() noexcept;

    AppControllerState state() const noexcept { return state_; }
    AppControllerStatus lastStatus() const noexcept { return lastStatus_; }
    bool inferenceEnabled() const noexcept { return inferenceEnabled_; }
    std::size_t completedPhaseCount() const noexcept {
        return completedPhaseCount_;
    }

    static constexpr std::size_t kPhaseCount =
        static_cast<std::size_t>(AppStartupPhase::Count);

private:
    bool configIsValid() const noexcept;
    bool disableInference() noexcept;
    bool cleanupStartedPhases(std::size_t phaseCount) noexcept;
    bool shutdownSequence(std::size_t phaseCount) noexcept;
    AppControllerStatus failStartup(AppControllerStatus status,
                                    std::size_t attemptedPhaseCount) noexcept;

    IAppLifecycle& lifecycle_;
    const AppControllerConfig config_;
    AppControllerState state_{AppControllerState::Idle};
    AppControllerStatus lastStatus_{AppControllerStatus::Ok};
    std::size_t completedPhaseCount_{0U};
    bool inferenceEnabled_{false};
};
}  // namespace project::managers
