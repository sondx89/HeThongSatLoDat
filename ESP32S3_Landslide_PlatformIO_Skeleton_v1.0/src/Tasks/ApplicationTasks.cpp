#include "project/Tasks/ApplicationTasks.h"

#include <cmath>

namespace project::tasks {
namespace {

TickType_t queueWait(const TaskRuntimeConfig& config) noexcept {
    return config.queueWaitTicks;
}

TaskStepResult queueTimeoutResult(
    project::queues::QueueRegistryStatus status) noexcept {
    if (status == project::queues::QueueRegistryStatus::Timeout ||
        status == project::queues::QueueRegistryStatus::QueueEmpty) {
        return TaskStepResult::Idle;
    }
    return TaskStepResult::Failure;
}

std::uint64_t tickToMillis(TickType_t ticks) noexcept {
    return (static_cast<std::uint64_t>(ticks) * 1000U) / configTICK_RATE_HZ;
}

bool modelOutputIsFinite(const project::models::ModelOutput& output) noexcept {
    if (output.outputCount > project::models::ModelOutput::kMaxOutputs) {
        return false;
    }
    for (std::uint16_t i = 0; i < output.outputCount; ++i) {
        if (!std::isfinite(output.rawOutputs[i])) {
            return false;
        }
    }
    return !output.predictionValid || std::isfinite(output.predictionValue);
}

}  // namespace

TaskSensor::TaskSensor(const TaskRuntimeConfig& config,
                       project::bsp::BoardSupportPackage& bsp,
                       project::managers::SensorManager& sensor,
                       project::queues::QueueRegistry& queues) noexcept
    : TaskBase(TaskId::Sensor, "TaskSensor", TaskSchedule::Periodic, config),
      bsp_(bsp), sensor_(sensor), queues_(queues) {}

bool TaskSensor::onStart() noexcept {
    return sensor_.beginAll();
}

TaskStepResult TaskSensor::runOne() noexcept {
    std::uint64_t nowMicros = 0;
    if (bsp_.monotonicMicros(nowMicros) != project::bsp::BspStatus::Ok) {
        return TaskStepResult::Failure;
    }
    if (!sensor_.pollDue(nowMicros)) {
        return TaskStepResult::Idle;
    }

    project::models::SensorFrame frame{};
    if (!sensor_.readSnapshot(frame)) {
        return TaskStepResult::Failure;
    }

    // The sample was acquired even if the bounded queue rejects it; queue drops
    // are counted by QueueRegistry, while this heartbeat reflects acquisition work.
    (void)queues_.sendSensor(frame, 0);
    return TaskStepResult::Progress;
}

TaskFeature::TaskFeature(
    const TaskRuntimeConfig& config,
    project::services::DataQualityService& quality,
    project::services::CalibrationService& calibration,
    project::services::FeaturePipeline& pipeline,
    ITaskEventReporter& events,
    project::queues::QueueRegistry& queues) noexcept
    : TaskBase(TaskId::Feature, "TaskFeature", TaskSchedule::QueueDriven, config),
      quality_(quality), calibration_(calibration), pipeline_(pipeline),
      events_(events), queues_(queues) {}

bool TaskFeature::onStart() noexcept {
    if (!calibration_.loadCalibration()) {
        return false;
    }
    pipeline_.resetWindow();
    return true;
}

TaskStepResult TaskFeature::runOne() noexcept {
    project::models::SensorFrame sensorFrame{};
    const project::queues::QueueRegistryStatus received =
        queues_.receiveSensor(sensorFrame, queueWait(config()));
    if (received != project::queues::QueueRegistryStatus::Ok) {
        return queueTimeoutResult(received);
    }

    if (!quality_.validate(sensorFrame)) {
        return events_.reportFeatureRejection(
                   FeatureRejectReason::SensorQuality, sensorFrame, nullptr)
                   ? TaskStepResult::Progress
                   : TaskStepResult::Failure;
    }

    project::models::ProcessedFrame processed{};
    if (!calibration_.process(sensorFrame, processed)) {
        return events_.reportFeatureRejection(
                   FeatureRejectReason::Calibration, sensorFrame, nullptr)
                   ? TaskStepResult::Progress
                   : TaskStepResult::Failure;
    }
    if (!quality_.validate(processed)) {
        return events_.reportFeatureRejection(
                   FeatureRejectReason::ProcessedQuality, sensorFrame, &processed)
                   ? TaskStepResult::Progress
                   : TaskStepResult::Failure;
    }
    if (!pipeline_.push(processed)) {
        return TaskStepResult::Failure;
    }
    if (!pipeline_.windowReady()) {
        return TaskStepResult::Progress;
    }

    project::models::FeatureVector features{};
    if (!pipeline_.build(features)) {
        return TaskStepResult::Failure;
    }
    (void)queues_.sendFeature(features, 0);
    return TaskStepResult::Progress;
}

TaskInference::TaskInference(const TaskRuntimeConfig& config,
                             project::ai::SchemaRegistry& schemas,
                             project::ai::IInferenceEngine& inference,
                             project::queues::QueueRegistry& queues) noexcept
    : TaskBase(TaskId::Inference, "TaskInference", TaskSchedule::QueueDriven,
               config),
      schemas_(schemas), inference_(inference), queues_(queues) {}

bool TaskInference::onStart() noexcept {
    schemaCompatible_ = schemas_.compatible();
    engineReady_ = schemaCompatible_ && inference_.initialize();
    return true;
}

void TaskInference::createFailureOutput(
    const project::models::FeatureVector& input,
    project::models::ModelOutput& output,
    project::models::InferenceStatus status) const noexcept {
    output = {};
    output.bootId = input.bootId;
    output.inferenceId = inferenceId_;
    output.timestampMs = tickToMillis(xTaskGetTickCount());
    output.status = status;
    output.predictionValid = false;
}

TaskStepResult TaskInference::runOne() noexcept {
    project::models::FeatureVector features{};
    const project::queues::QueueRegistryStatus received =
        queues_.receiveFeature(features, queueWait(config()));
    if (received != project::queues::QueueRegistryStatus::Ok) {
        return queueTimeoutResult(received);
    }

    ++inferenceId_;
    project::models::ModelOutput output{};
    if (!schemaCompatible_ || !schemas_.validate(features)) {
        createFailureOutput(features, output,
                            project::models::InferenceStatus::VersionMismatch);
    } else if (!engineReady_) {
        createFailureOutput(features, output,
                            project::models::InferenceStatus::ModelError);
    } else if (!inference_.run(features, output)) {
        const project::models::InferenceStatus failureStatus =
            output.status == project::models::InferenceStatus::InvalidInput
                ? project::models::InferenceStatus::ModelError
                : output.status;
        createFailureOutput(features, output, failureStatus);
    } else if (output.status != project::models::InferenceStatus::Ok) {
        if (output.status == project::models::InferenceStatus::InvalidInput) {
            output.status = project::models::InferenceStatus::ModelError;
        }
        output.predictionValid = false;
    } else if (!modelOutputIsFinite(output)) {
        output.status =
            output.outputCount > project::models::ModelOutput::kMaxOutputs
                ? project::models::InferenceStatus::ModelError
                : project::models::InferenceStatus::NonFiniteOutput;
        output.predictionValid = false;
    }

    if (output.bootId == 0) {
        output.bootId = features.bootId;
    }
    output.inferenceId = inferenceId_;
    if (output.timestampMs == 0) {
        output.timestampMs = tickToMillis(xTaskGetTickCount());
    }
    (void)queues_.sendModel(output, 0);
    return TaskStepResult::Progress;
}

TaskRisk::TaskRisk(const TaskRuntimeConfig& config,
                   project::managers::RiskManager& riskManager,
                   IRiskTaskContext& context,
                   ITaskEventReporter& events,
                   project::queues::QueueRegistry& queues) noexcept
    : TaskBase(TaskId::Risk, "TaskRisk", TaskSchedule::QueueDriven, config),
      riskManager_(riskManager), context_(context), events_(events),
      queues_(queues) {}

TaskStepResult TaskRisk::runOne() noexcept {
    project::models::ModelOutput model{};
    const project::queues::QueueRegistryStatus received =
        queues_.receiveModel(model, queueWait(config()));
    if (received != project::queues::QueueRegistryStatus::Ok) {
        return queueTimeoutResult(received);
    }

    project::models::ProcessedFrame frame{};
    project::models::HealthSnapshot health{};
    if (!context_.getContext(model, frame, health)) {
        return TaskStepResult::Failure;
    }

    project::models::RiskAssessment assessment{};
    if (!riskManager_.evaluate(model, frame, health, assessment)) {
        return TaskStepResult::Failure;
    }
    const bool eventReported = events_.reportRiskTransition(assessment);
    const bool riskPublished =
        queues_.sendRisk(assessment, 0) ==
        project::queues::QueueRegistryStatus::Ok;
    return eventReported && riskPublished ? TaskStepResult::Progress
                                          : TaskStepResult::Failure;
}

TaskLogger::TaskLogger(const TaskRuntimeConfig& config, ITaskLogWriter& writer,
                       project::queues::QueueRegistry& queues) noexcept
    : TaskBase(TaskId::Logger, "TaskLogger", TaskSchedule::QueueDriven, config),
      writer_(writer), queues_(queues) {}

TaskStepResult TaskLogger::runOne() noexcept {
    if (preferEvent_) {
        project::models::EventRecord event{};
        const project::queues::QueueRegistryStatus received =
            queues_.receiveEvent(event, 0);
        if (received == project::queues::QueueRegistryStatus::Ok) {
            preferEvent_ = false;
            return writer_.writeEvent(event) ? TaskStepResult::Progress
                                             : TaskStepResult::Failure;
        }
        if (received != project::queues::QueueRegistryStatus::QueueEmpty) {
            return TaskStepResult::Failure;
        }
    }

    project::queues::LogRecord record{};
    const project::queues::QueueRegistryStatus logReceived =
        queues_.receiveLog(record, 0);
    if (logReceived == project::queues::QueueRegistryStatus::Ok) {
        preferEvent_ = true;
        return writer_.writeRecord(record) ? TaskStepResult::Progress
                                            : TaskStepResult::Failure;
    }
    if (logReceived != project::queues::QueueRegistryStatus::QueueEmpty) {
        return TaskStepResult::Failure;
    }

    if (!preferEvent_) {
        project::models::EventRecord event{};
        const project::queues::QueueRegistryStatus eventReceived =
            queues_.receiveEvent(event, 0);
        if (eventReceived == project::queues::QueueRegistryStatus::Ok) {
            preferEvent_ = false;
            return writer_.writeEvent(event) ? TaskStepResult::Progress
                                             : TaskStepResult::Failure;
        }
        if (eventReceived != project::queues::QueueRegistryStatus::QueueEmpty) {
            return TaskStepResult::Failure;
        }
    }
    return writer_.service();
}

TaskDiagnostics::TaskDiagnostics(const TaskRuntimeConfig& config,
                                 ITaskHealthSource& source,
                                 ITaskHealthPublisher& publisher,
                                 project::queues::QueueRegistry& queues) noexcept
    : TaskBase(TaskId::Diagnostics, "TaskDiagnostics", TaskSchedule::Periodic,
               config),
      source_(source), publisher_(publisher), queues_(queues) {}

bool TaskDiagnostics::collect(
    project::models::HealthSnapshot& output) const noexcept {
    output = {};
    if (!source_.collectSystemMetrics(output)) {
        return false;
    }
    output.sequenceNo = snapshotSequence_ + 1U;
    output.uptimeMs = tickToMillis(xTaskGetTickCount());

    for (std::size_t i = 0; i < kTaskCount; ++i) {
        const TaskId taskId = static_cast<TaskId>(i);
        TaskHeartbeatSnapshot heartbeat{};
        if (!TaskHeartbeatRegistry::instance().snapshot(taskId, heartbeat)) {
            continue;
        }

        if (output.taskStackMetricCount >=
            project::models::HealthSnapshot::kMaxTaskMetrics) {
            return false;
        }
        auto& metric = output.taskStackMetrics[output.taskStackMetricCount++];
        // TODO(TBD): freeze task ID values in the released health schema.
        metric.taskId = static_cast<std::uint16_t>(taskId);
        metric.highWaterWords = heartbeat.stackHighWaterWords;
        metric.failureCount = heartbeat.failureCount;
        metric.heartbeatAgeMs = static_cast<std::uint32_t>(tickToMillis(
            heartbeat.hasProgress
                ? static_cast<TickType_t>(xTaskGetTickCount() -
                                          heartbeat.lastProgressTick)
                : static_cast<TickType_t>(xTaskGetTickCount() -
                                          heartbeat.createdTick)));
        metric.deadlineMissCount = heartbeat.deadlineMissCount;
        metric.valid = true;
        if ((heartbeat.stale || heartbeat.taskExited) &&
            (output.healthState == project::models::HealthState::Healthy ||
             output.healthState == project::models::HealthState::Unknown)) {
            output.healthState = project::models::HealthState::Degraded;
        }
    }

    constexpr project::queues::QueueId queueIds[] = {
        project::queues::QueueId::Sensor,
        project::queues::QueueId::Feature,
        project::queues::QueueId::Model,
        project::queues::QueueId::Risk,
        project::queues::QueueId::Event,
        project::queues::QueueId::Log};
    for (const project::queues::QueueId id : queueIds) {
        if (output.queueMetricCount >=
            project::models::HealthSnapshot::kMaxQueueMetrics) {
            return false;
        }

        project::queues::QueueStats queueStats{};
        if (queues_.getStats(id, queueStats) !=
            project::queues::QueueRegistryStatus::Ok) {
            return false;
        }
        auto& metric = output.queueMetrics[output.queueMetricCount++];
        metric.queueId = static_cast<std::uint16_t>(id);
        metric.capacity = queueStats.capacity;
        metric.depth = queueStats.depth;
        metric.highWaterMark = queueStats.highWaterMark;
        metric.dropCount = queueStats.droppedCount;
        metric.payloadSizeBytes = queueStats.itemSizeBytes;
        metric.valid = true;
    }
    return true;
}

TaskStepResult TaskDiagnostics::runOne() noexcept {
    project::models::HealthSnapshot snapshot{};
    if (!collect(snapshot)) {
        return TaskStepResult::Failure;
    }
    if (!publisher_.publish(snapshot)) {
        return TaskStepResult::Failure;
    }
    ++snapshotSequence_;
    return TaskStepResult::Progress;
}

}  // namespace project::tasks
