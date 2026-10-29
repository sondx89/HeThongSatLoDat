#pragma once

#include <cstddef>
#include <cstdint>

#include "Models/DataContractTypes.h"
#include "Models/SensorStatus.h"

namespace project::models {

struct TaskStackMetric {
    std::uint16_t taskId{0};  // TODO(TBD): assign from the released task registry.
    std::uint32_t highWaterWords{0};
    std::uint32_t heartbeatAgeMs{0};
    // TODO(TBD): include this in the released HealthSnapshot schema/version.
    std::uint64_t failureCount{0};
    std::uint64_t deadlineMissCount{0};
    std::uint32_t restartCount{0};
    bool valid{false};
};

struct SensorHealthMetric {
    SensorId sensorId{SensorId::Unknown};
    SensorHealth health{SensorHealth::Unknown};
    std::uint32_t lastSampleAgeMs{0};
    std::uint64_t readErrorCount{0};
    std::uint64_t outOfRangeCount{0};
    std::uint32_t recoveryCount{0};
    bool valid{false};
};

struct QueueHealthMetric {
    std::uint16_t queueId{0};  // TODO(TBD): assign from the released queue registry.
    std::uint32_t capacity{0};
    std::uint32_t depth{0};
    std::uint32_t highWaterMark{0};
    std::uint32_t dropCount{0};
    std::uint32_t payloadSizeBytes{0};
    bool valid{false};
};

struct HealthSnapshot {
    // Capacities only; TODO(TBD): size from the released hardware/task/queue
    // registries and measured RAM budget.
    static constexpr std::size_t kMaxTaskMetrics = 8;
    static constexpr std::size_t kMaxSensorMetrics = 4;
    static constexpr std::size_t kMaxQueueMetrics = 8;

    std::uint32_t schemaVersion{0};
    std::uint64_t bootId{0};
    std::uint64_t sequenceNo{0};
    std::uint64_t uptimeMs{0};
    ResetReason resetReason{ResetReason::Unknown};
    std::uint32_t heapFreeBytes{0};
    std::uint32_t heapMinFreeBytes{0};
    std::uint32_t largestFreeBlockBytes{0};
    std::uint32_t internalHeapBytes{0};
    std::uint32_t psramTotalBytes{0};
    std::uint32_t psramFreeBytes{0};
    bool psramMetricsValid{false};
    TaskStackMetric taskStackMetrics[kMaxTaskMetrics]{};
    std::uint8_t taskStackMetricCount{0};
    SensorHealthMetric sensorMetrics[kMaxSensorMetrics]{};
    std::uint8_t sensorMetricCount{0};
    QueueHealthMetric queueMetrics[kMaxQueueMetrics]{};
    std::uint8_t queueMetricCount{0};

    std::uint64_t inferenceCount{0};
    std::uint64_t sensorReadErrorCount{0};
    std::uint64_t sensorOutOfRangeCount{0};
    std::uint64_t inferenceErrorCount{0};
    std::uint64_t modelOutputNanCount{0};
    std::uint64_t deadlineMissCount{0};
    std::uint64_t logWriteFailureCount{0};
    std::uint64_t queueOverflowCount{0};
    std::uint64_t taskRestartCount{0};
    std::uint64_t watchdogCount{0};  // Per-boot distinct task heartbeat faults.
    bool watchdogCountValid{false};
    std::uint64_t riskTransitionCount{0};
    std::uint64_t alarmDurationMs{0};
    std::uint64_t storageBytesFree{0};
    std::uint32_t storageRotationCount{0};
    bool storageMetricsValid{false};
    std::uint32_t lastSensorReadDurationMs{0};
    std::uint32_t lastInferenceDurationMs{0};
    std::uint32_t lastLoopPeriodMs{0};

    std::uint8_t activeManifestHashSha256[32]{};
    bool activeManifestHashValid{false};
    ApiStatus configValidationStatus{ApiStatus::NotInitialized};
    bool configValidationValid{false};
    std::uint32_t supplyVoltageMv{0};
    std::uint64_t brownoutCount{0};
    bool powerMetricsValid{false};

    std::uint32_t faultFlags{0};
    HealthState healthState{HealthState::Unknown};
};
}  // namespace project::models
