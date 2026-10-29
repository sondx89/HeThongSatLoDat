#include "project/Diagnostics/WatchdogSupervisor.h"

#include <cstddef>

#include "Models/DataContractTypes.h"

namespace project::diagnostics {
namespace {

const char* taskName(project::tasks::TaskId id) noexcept {
    switch (id) {
    case project::tasks::TaskId::Sensor:
        return "TaskSensor";
    case project::tasks::TaskId::Feature:
        return "TaskFeature";
    case project::tasks::TaskId::Inference:
        return "TaskInference";
    case project::tasks::TaskId::Risk:
        return "TaskRisk";
    case project::tasks::TaskId::Logger:
        return "TaskLogger";
    case project::tasks::TaskId::Diagnostics:
        return "TaskDiagnostics";
    case project::tasks::TaskId::Watchdog:
        return "TaskWatchdogSupervisor";
    case project::tasks::TaskId::Count:
        break;
    }
    return "UnknownTask";
}

template <std::size_t N>
void copyText(char (&destination)[N], const char* source) noexcept {
    std::size_t index = 0;
    while (index + 1U < N && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

}  // namespace

bool WatchdogFaultLatch::markTaskHeartbeatFault(
    project::tasks::TaskId taskId,
    const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept {
    (void)heartbeat;
    const auto index = static_cast<std::uint8_t>(taskId);
    if (index >= static_cast<std::uint8_t>(project::tasks::TaskId::Count)) {
        return false;
    }

    faultMask_.fetch_or(1UL << index, std::memory_order_release);
    return true;
}

bool WatchdogFaultLatch::isFaulted(project::tasks::TaskId taskId) const noexcept {
    const auto index = static_cast<std::uint8_t>(taskId);
    if (index >= static_cast<std::uint8_t>(project::tasks::TaskId::Count)) {
        return false;
    }
    return (faultMask_.load(std::memory_order_acquire) & (1UL << index)) != 0;
}

std::uint32_t WatchdogFaultLatch::faultMask() const noexcept {
    return faultMask_.load(std::memory_order_acquire);
}

WatchdogSupervisor::WatchdogSupervisor(
    project::bsp::BoardSupportPackage& bsp, EventLogger& events,
    IWatchdogFaultSink& faultSink) noexcept
    : bsp_(bsp), events_(events), faultSink_(faultSink) {}

WatchdogStatus WatchdogSupervisor::begin(std::uint64_t bootId) noexcept {
    State expected = State::Uninitialized;
    if (!state_.compare_exchange_strong(expected, State::Initializing,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
        return expected == State::Initializing || expected == State::Running ||
                       expected == State::ResetRequested
                   ? WatchdogStatus::AlreadyInitialized
                   : WatchdogStatus::NotInitialized;
    }

    if (!allRequiredTasksRegistered()) {
        state_.store(State::Uninitialized, std::memory_order_release);
        return WatchdogStatus::TasksNotRegistered;
    }

    bootId_ = bootId;
    faultedTaskMask_.store(0, std::memory_order_relaxed);
    failureFlags_.store(0, std::memory_order_relaxed);
    state_.store(State::Running, std::memory_order_release);
    return WatchdogStatus::Ready;
}

bool WatchdogSupervisor::allRequiredTasksRegistered() const noexcept {
    for (const project::tasks::TaskId id : kRequiredTasks) {
        project::tasks::TaskHeartbeatSnapshot heartbeat{};
        if (!project::tasks::TaskHeartbeatRegistry::instance().snapshot(
                id, heartbeat) ||
            !heartbeat.taskCreated ||
            heartbeat.heartbeatTimeoutTicks == 0) {
            return false;
        }
    }
    return true;
}

WatchdogStatus WatchdogSupervisor::checkTaskProgress() noexcept {
    const State currentState = state_.load(std::memory_order_acquire);
    if (currentState == State::Uninitialized ||
        currentState == State::Initializing) {
        return WatchdogStatus::NotInitialized;
    }
    if (currentState == State::ResetRequested) {
        (void)bsp_.restart();
        failureFlags_.fetch_or(
            static_cast<std::uint8_t>(WatchdogFailure::Reset),
            std::memory_order_release);
        return WatchdogStatus::ResetFailed;
    }

    for (const project::tasks::TaskId id : kRequiredTasks) {
        project::tasks::TaskHeartbeatSnapshot heartbeat{};
        if (!project::tasks::TaskHeartbeatRegistry::instance().snapshot(
                id, heartbeat)) {
            heartbeat.taskCreated = false;
            heartbeat.taskExited = true;
            return handleTimeout(id, heartbeat);
        }
        if (!heartbeat.taskCreated || heartbeat.taskExited || heartbeat.stale) {
            return handleTimeout(id, heartbeat);
        }
    }

    return WatchdogStatus::Healthy;
}

WatchdogStatus WatchdogSupervisor::handleTimeout(
    project::tasks::TaskId taskId,
    const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept {
    State expected = State::Running;
    if (!state_.compare_exchange_strong(expected, State::ResetRequested,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
        return WatchdogStatus::FaultLatched;
    }

    const std::uint32_t taskBit =
        1UL << static_cast<std::uint8_t>(taskId);
    faultedTaskMask_.fetch_or(taskBit, std::memory_order_release);

    const bool faultMarked =
        faultSink_.markTaskHeartbeatFault(taskId, heartbeat);
    const bool taskEventQueued =
        enqueueTaskHeartbeatEvent(taskId, heartbeat);
    const bool resetEventQueued =
        enqueueWatchdogResetEvent(taskId, heartbeat);

    std::uint8_t failures = 0;
    if (!faultMarked) {
        failures |= static_cast<std::uint8_t>(WatchdogFailure::FaultSink);
    }
    if (!taskEventQueued || !resetEventQueued) {
        failures |= static_cast<std::uint8_t>(WatchdogFailure::EventQueue);
    }
    failureFlags_.store(failures, std::memory_order_release);

    // Reset must not depend on RAM/queue-based reporting succeeding.
    (void)bsp_.restart();
    failures |= static_cast<std::uint8_t>(WatchdogFailure::Reset);
    failureFlags_.store(failures, std::memory_order_release);
    return WatchdogStatus::ResetFailed;
}

bool WatchdogSupervisor::enqueueTaskHeartbeatEvent(
    project::tasks::TaskId taskId,
    const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept {
    return enqueueEvent("EVT-SYS-0005", "TASK_HEARTBEAT_MISSED", "0x0601",
                        "ERR_TASK_HEARTBEAT", taskId, heartbeat);
}

bool WatchdogSupervisor::enqueueWatchdogResetEvent(
    project::tasks::TaskId taskId,
    const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept {
    return enqueueEvent("EVT-SYS-0004", "WATCHDOG_TIMEOUT", "0x0603",
                        "ERR_WATCHDOG_TIMEOUT", taskId, heartbeat);
}

bool WatchdogSupervisor::enqueueEvent(
    const char* catalogId, const char* name, const char* statusCode,
    const char* faultCode, project::tasks::TaskId taskId,
    const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept {
    project::models::EventRecord event{};
    event.bootId = bootId_;
    event.severity = project::models::EventSeverity::Critical;
    event.healthState = project::models::HealthState::Fault;
    event.valid = true;
    event.faultCodeValid = true;
    event.errorCodeValid = true;
    event.valueType = project::models::EventRecord::ValueType::Numeric;
    event.valueValid = true;
    event.relatedSequenceNo = heartbeat.progressCount;
    event.relatedSequenceNoValid = true;
    const TickType_t nowTicks = xTaskGetTickCount();
    const TickType_t referenceTicks =
        heartbeat.hasProgress ? heartbeat.lastProgressTick : heartbeat.createdTick;
    const TickType_t ageTicks = static_cast<TickType_t>(nowTicks - referenceTicks);
    event.value = static_cast<float>(
        (static_cast<std::uint64_t>(ageTicks) * 1000U) / configTICK_RATE_HZ);
    copyText(event.unit, "ms");

    event.timestampMs = 0;
    if (bsp_.monotonicMillis(event.timestampMs) != project::bsp::BspStatus::Ok) {
        return false;
    }

    copyText(event.eventCatalogId, catalogId);
    copyText(event.eventName, name);
    copyText(event.source, "WatchdogSupervisor");
    copyText(event.statusCode, statusCode);
    copyText(event.errorCode, faultCode);
    copyText(event.faultCode, faultCode);
    copyText(event.message, taskName(taskId));

    return events_.enqueue(event);
}

bool WatchdogSupervisor::isInitialized() const noexcept {
    const State state = state_.load(std::memory_order_acquire);
    return state == State::Running || state == State::ResetRequested;
}

bool WatchdogSupervisor::isFaultLatched() const noexcept {
    return faultedTaskMask_.load(std::memory_order_acquire) != 0;
}

std::uint32_t WatchdogSupervisor::faultedTaskMask() const noexcept {
    return faultedTaskMask_.load(std::memory_order_acquire);
}

std::uint8_t WatchdogSupervisor::failureFlags() const noexcept {
    return failureFlags_.load(std::memory_order_acquire);
}

}  // namespace project::diagnostics
