#pragma once

#include <atomic>
#include <cstdint>

#include "BSP/BoardSupportPackage.h"
#include "Diagnostics/EventLogger.h"
#include "Tasks/TaskFramework.h"

namespace project::diagnostics {

enum class WatchdogStatus : std::uint8_t {
    Ready,
    Healthy,
    NotInitialized,
    AlreadyInitialized,
    TasksNotRegistered,
    FaultLatched,
    ResetFailed
};

enum class WatchdogFailure : std::uint8_t {
    FaultSink = 1U << 0U,
    EventQueue = 1U << 1U,
    Reset = 1U << 2U
};

class IWatchdogFaultSink {
public:
    virtual ~IWatchdogFaultSink() = default;

    // Must set a persistent/in-memory system fault without blocking. Do not
    // allocate or access slow storage in this callback.
    virtual bool markTaskHeartbeatFault(
        project::tasks::TaskId taskId,
        const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept = 0;
};

class WatchdogFaultLatch final : public IWatchdogFaultSink {
public:
    bool markTaskHeartbeatFault(
        project::tasks::TaskId taskId,
        const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept override;
    bool isFaulted(project::tasks::TaskId taskId) const noexcept;
    std::uint32_t faultMask() const noexcept;

private:
    std::atomic<std::uint32_t> faultMask_{0};
};

class WatchdogSupervisor final {
public:
    WatchdogSupervisor(project::bsp::BoardSupportPackage& bsp,
                       EventLogger& events,
                       IWatchdogFaultSink& faultSink) noexcept;

    // Call after the task framework starts. If tasks are not registered yet,
    // retry begin() later. checkTaskProgress() is task-context only and uses
    // each monitored task's measured heartbeat timeout.
    WatchdogStatus begin(std::uint64_t bootId) noexcept;
    WatchdogStatus checkTaskProgress() noexcept;
    bool isInitialized() const noexcept;
    bool isFaultLatched() const noexcept;
    std::uint32_t faultedTaskMask() const noexcept;
    std::uint8_t failureFlags() const noexcept;

    WatchdogSupervisor(const WatchdogSupervisor&) = delete;
    WatchdogSupervisor& operator=(const WatchdogSupervisor&) = delete;

private:
    enum class State : std::uint8_t {
        Uninitialized,
        Initializing,
        Running,
        ResetRequested
    };

    static constexpr project::tasks::TaskId kRequiredTasks[] = {
        project::tasks::TaskId::Sensor,
        project::tasks::TaskId::Feature,
        project::tasks::TaskId::Inference,
        project::tasks::TaskId::Risk,
        project::tasks::TaskId::Logger};

    bool allRequiredTasksRegistered() const noexcept;
    WatchdogStatus handleTimeout(
        project::tasks::TaskId taskId,
        const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept;
    bool enqueueTaskHeartbeatEvent(
        project::tasks::TaskId taskId,
        const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept;
    bool enqueueWatchdogResetEvent(
        project::tasks::TaskId taskId,
        const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept;
    bool enqueueEvent(const char* eventId, const char* eventName,
                      const char* statusCode, const char* faultCode,
                      project::tasks::TaskId taskId,
                      const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept;

    project::bsp::BoardSupportPackage& bsp_;
    EventLogger& events_;
    IWatchdogFaultSink& faultSink_;
    std::atomic<State> state_{State::Uninitialized};
    std::atomic<std::uint32_t> faultedTaskMask_{0};
    std::atomic<std::uint8_t> failureFlags_{0};
    std::uint64_t bootId_{0};
};

}  // namespace project::diagnostics
