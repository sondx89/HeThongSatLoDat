#pragma once

#include <atomic>
#include <cstdint>

#include "BSP/BoardSupportPackage.h"
#include "Diagnostics/WatchdogSupervisor.h"
#include "Models/HealthSnapshot.h"
#include "Queues/QueueRegistry.h"

namespace project::diagnostics {

enum class HealthMonitorStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    UnsupportedPlatform,
    HardwareError,
    QueueUnavailable,
    QueueError,
    MetricCapacityExceeded
};

class ITaskHealthSource {
public:
    virtual ~ITaskHealthSource() = default;

    // Collect only board/system fields; TaskDiagnostics owns task and queue arrays.
    virtual bool collectSystemMetrics(
        project::models::HealthSnapshot& snapshot) noexcept = 0;
};

class HealthMonitor final : public ITaskHealthSource,
                            public IWatchdogFaultSink {
public:
    HealthMonitor(project::bsp::BoardSupportPackage& bsp,
                  project::queues::QueueRegistry& queues,
                  std::uint64_t bootId) noexcept;

    bool collectSystemMetrics(
        project::models::HealthSnapshot& snapshot) noexcept override;

    // Captures heap, registered task stack watermarks, queue depths and
    // watchdog faults into one fixed-size snapshot. Task-context only.
    HealthMonitorStatus captureSnapshot(
        std::uint64_t sequenceNo,
        project::models::HealthSnapshot& output) noexcept;

    // Used as WatchdogSupervisor's fault sink. Counts each task fault once
    // during this boot and latches its task bit.
    bool markTaskHeartbeatFault(
        project::tasks::TaskId taskId,
        const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept override;

    std::uint64_t watchdogCount() const noexcept;
    std::uint32_t faultedTaskMask() const noexcept;

private:
    HealthMonitorStatus collectSystemMetricsStatus(
        project::models::HealthSnapshot& snapshot) noexcept;

    project::bsp::BoardSupportPackage& bsp_;
    project::queues::QueueRegistry& queues_;
    const std::uint64_t bootId_;
    std::atomic<std::uint64_t> watchdogCount_{0};
    std::atomic<std::uint32_t> faultedTaskMask_{0};
};

}  // namespace project::diagnostics
