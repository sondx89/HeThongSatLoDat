#include "project/Diagnostics/HealthMonitor.h"

#include "project/Tasks/TaskFramework.h"

namespace project::diagnostics {
namespace {

HealthMonitorStatus mapBspStatus(project::bsp::BspStatus status) noexcept {
    switch (status) {
    case project::bsp::BspStatus::Ok:
        return HealthMonitorStatus::Ok;
    case project::bsp::BspStatus::UnsupportedPlatform:
        return HealthMonitorStatus::UnsupportedPlatform;
    case project::bsp::BspStatus::HardwareError:
        return HealthMonitorStatus::HardwareError;
    case project::bsp::BspStatus::InvalidArgument:
    case project::bsp::BspStatus::NotConfigured:
    case project::bsp::BspStatus::NotInitialized:
        return HealthMonitorStatus::HardwareError;
    }
    return HealthMonitorStatus::HardwareError;
}

HealthMonitorStatus mapQueueStatus(
    project::queues::QueueRegistryStatus status) noexcept {
    if (status == project::queues::QueueRegistryStatus::NotInitialized) {
        return HealthMonitorStatus::QueueUnavailable;
    }
    return status == project::queues::QueueRegistryStatus::Ok
               ? HealthMonitorStatus::Ok
               : HealthMonitorStatus::QueueError;
}

std::uint64_t ticksToMillis(TickType_t ticks) noexcept {
    return (static_cast<std::uint64_t>(ticks) * 1000U) / configTICK_RATE_HZ;
}

std::uint32_t saturatingUint32(std::uint64_t value) noexcept {
    return value > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(value);
}

void addSaturating(std::uint64_t value, std::uint64_t& total) noexcept {
    if (value > UINT64_MAX - total) {
        total = UINT64_MAX;
    } else {
        total += value;
    }
}

constexpr project::queues::QueueId kQueueIds[] = {
    project::queues::QueueId::Sensor,
    project::queues::QueueId::Feature,
    project::queues::QueueId::Model,
    project::queues::QueueId::Risk,
    project::queues::QueueId::Event,
    project::queues::QueueId::Log};

}  // namespace

HealthMonitor::HealthMonitor(project::bsp::BoardSupportPackage& bsp,
                             project::queues::QueueRegistry& queues,
                             std::uint64_t bootId) noexcept
    : bsp_(bsp), queues_(queues), bootId_(bootId) {}

bool HealthMonitor::collectSystemMetrics(
    project::models::HealthSnapshot& snapshot) noexcept {
    return collectSystemMetricsStatus(snapshot) == HealthMonitorStatus::Ok;
}

HealthMonitorStatus HealthMonitor::collectSystemMetricsStatus(
    project::models::HealthSnapshot& snapshot) noexcept {
    project::bsp::MemoryMetrics memory{};
    const project::bsp::BspStatus memoryStatus = bsp_.readMemoryMetrics(memory);
    if (memoryStatus != project::bsp::BspStatus::Ok) {
        return mapBspStatus(memoryStatus);
    }
    if (!memory.valid) {
        return HealthMonitorStatus::HardwareError;
    }

    std::uint64_t uptimeMs = 0;
    const project::bsp::BspStatus uptimeStatus = bsp_.monotonicMillis(uptimeMs);
    if (uptimeStatus != project::bsp::BspStatus::Ok) {
        return mapBspStatus(uptimeStatus);
    }

    std::uint32_t platformResetReason = 0;
    const project::bsp::BspStatus resetStatus =
        bsp_.resetReason(platformResetReason);
    if (resetStatus != project::bsp::BspStatus::Ok) {
        return mapBspStatus(resetStatus);
    }
    (void)platformResetReason;

    snapshot.bootId = bootId_;
    snapshot.uptimeMs = uptimeMs;
    snapshot.resetReason = project::models::ResetReason::PlatformSpecific;
    snapshot.heapFreeBytes = memory.freeHeapBytes;
    snapshot.heapMinFreeBytes = memory.minimumFreeHeapBytes;
    snapshot.largestFreeBlockBytes = memory.largestFreeBlockBytes;
    snapshot.internalHeapBytes = memory.internalHeapBytes;
    snapshot.watchdogCount = watchdogCount();
    snapshot.watchdogCountValid = true;
    return HealthMonitorStatus::Ok;
}

HealthMonitorStatus HealthMonitor::captureSnapshot(
    std::uint64_t sequenceNo,
    project::models::HealthSnapshot& output) noexcept {
    output = {};
    const HealthMonitorStatus systemStatus =
        collectSystemMetricsStatus(output);
    if (systemStatus != HealthMonitorStatus::Ok) {
        output = {};
        return systemStatus;
    }
    output.sequenceNo = sequenceNo;
    output.healthState = project::models::HealthState::Unknown;

    for (std::size_t index = 0; index < project::tasks::kTaskCount; ++index) {
        const auto taskId = static_cast<project::tasks::TaskId>(index);
        project::tasks::TaskHeartbeatSnapshot heartbeat{};
        if (!project::tasks::TaskHeartbeatRegistry::instance().snapshot(
                taskId, heartbeat)) {
            continue;
        }
        if (output.taskStackMetricCount >=
            project::models::HealthSnapshot::kMaxTaskMetrics) {
            output = {};
            return HealthMonitorStatus::MetricCapacityExceeded;
        }

        auto& metric =
            output.taskStackMetrics[output.taskStackMetricCount++];
        metric.taskId = static_cast<std::uint16_t>(taskId);
        metric.highWaterWords =
            static_cast<std::uint32_t>(heartbeat.stackHighWaterWords);
        const TickType_t now = xTaskGetTickCount();
        const TickType_t reference =
            heartbeat.hasProgress ? heartbeat.lastProgressTick
                                  : heartbeat.createdTick;
        metric.heartbeatAgeMs = saturatingUint32(
            ticksToMillis(static_cast<TickType_t>(now - reference)));
        metric.failureCount = heartbeat.failureCount;
        metric.deadlineMissCount = heartbeat.deadlineMissCount;
        metric.valid = true;

        if (heartbeat.stale || heartbeat.taskExited) {
            output.healthState = project::models::HealthState::Degraded;
        }
    }

    for (const project::queues::QueueId queueId : kQueueIds) {
        if (output.queueMetricCount >=
            project::models::HealthSnapshot::kMaxQueueMetrics) {
            output = {};
            return HealthMonitorStatus::MetricCapacityExceeded;
        }
        project::queues::QueueStats stats{};
        const project::queues::QueueRegistryStatus queueStatus =
            queues_.getStats(queueId, stats);
        if (queueStatus != project::queues::QueueRegistryStatus::Ok) {
            output = {};
            return mapQueueStatus(queueStatus);
        }

        auto& metric = output.queueMetrics[output.queueMetricCount++];
        metric.queueId = static_cast<std::uint16_t>(queueId);
        metric.capacity = stats.capacity;
        metric.depth = stats.depth;
        metric.highWaterMark = stats.highWaterMark;
        metric.dropCount = stats.droppedCount;
        metric.payloadSizeBytes = stats.itemSizeBytes;
        metric.valid = true;
        addSaturating(stats.droppedCount, output.queueOverflowCount);
    }

    output.watchdogCount = watchdogCount();
    output.watchdogCountValid = true;
    if (faultedTaskMask() != 0U) {
        output.healthState = project::models::HealthState::Fault;
    }
    return HealthMonitorStatus::Ok;
}

bool HealthMonitor::markTaskHeartbeatFault(
    project::tasks::TaskId taskId,
    const project::tasks::TaskHeartbeatSnapshot& heartbeat) noexcept {
    (void)heartbeat;
    const auto index = static_cast<std::uint8_t>(taskId);
    if (index >= static_cast<std::uint8_t>(project::tasks::TaskId::Count) ||
        index >= 32U) {
        return false;
    }

    const std::uint32_t taskBit = 1UL << index;
    const std::uint32_t previous =
        faultedTaskMask_.fetch_or(taskBit, std::memory_order_acq_rel);
    if ((previous & taskBit) == 0U) {
        std::uint64_t count = watchdogCount_.load(std::memory_order_relaxed);
        while (count != UINT64_MAX &&
               !watchdogCount_.compare_exchange_weak(
                   count, count + 1U, std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
    }
    return true;
}

std::uint64_t HealthMonitor::watchdogCount() const noexcept {
    return watchdogCount_.load(std::memory_order_acquire);
}

std::uint32_t HealthMonitor::faultedTaskMask() const noexcept {
    return faultedTaskMask_.load(std::memory_order_acquire);
}

}  // namespace project::diagnostics
