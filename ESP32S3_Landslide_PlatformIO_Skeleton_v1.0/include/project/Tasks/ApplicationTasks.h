#pragma once

#include <cstdint>

#include "AI/IInferenceEngine.h"
#include "AI/SchemaRegistry.h"
#include "BSP/BoardSupportPackage.h"
#include "Managers/RiskManager.h"
#include "Managers/SensorManager.h"
#include "Models/DataContract.h"
#include "Queues/QueueRegistry.h"
#include "Services/CalibrationService.h"
#include "Services/DataQualityService.h"
#include "Services/FeaturePipeline.h"
#include "Tasks/TaskFramework.h"

namespace project::tasks {

class IRiskTaskContext {
public:
    virtual ~IRiskTaskContext() = default;
    virtual bool getContext(const project::models::ModelOutput& model,
                            project::models::ProcessedFrame& frame,
                            project::models::HealthSnapshot& health) noexcept = 0;
};

class ITaskLogWriter {
public:
    virtual ~ITaskLogWriter() = default;
    // Each call is bounded; the sink owns any bounded retry buffer and must
    // return false/count a failure rather than claim an unsuccessful write.
    virtual bool writeEvent(const project::models::EventRecord& event) noexcept = 0;
    virtual bool writeRecord(const project::queues::LogRecord& record) noexcept = 0;
    // Must perform only bounded work (for example policy-driven flush/recovery);
    // return Progress only when the service made meaningful progress.
    virtual TaskStepResult service() noexcept = 0;
};

class ITaskHealthPublisher {
public:
    virtual ~ITaskHealthPublisher() = default;
    // Must copy or publish the snapshot within the configured diagnostics budget.
    virtual bool publish(const project::models::HealthSnapshot& snapshot) noexcept = 0;
};

class ITaskHealthSource {
public:
    virtual ~ITaskHealthSource() = default;
    // Populate platform/system counters only; leave task and queue metric arrays
    // and their counts empty for TaskDiagnostics to fill. Work must be bounded.
    virtual bool collectSystemMetrics(
        project::models::HealthSnapshot& snapshot) noexcept = 0;
};

enum class FeatureRejectReason : std::uint8_t {
    SensorQuality,
    Calibration,
    ProcessedQuality
};

class ITaskEventReporter {
public:
    virtual ~ITaskEventReporter() = default;
    // Must apply the approved event catalog, deduplication and bounded Q_EVENT
    // policy; the framework does not fabricate event IDs or messages.
    virtual bool reportFeatureRejection(
        FeatureRejectReason reason,
        const project::models::SensorFrame& source,
        const project::models::ProcessedFrame* processed) noexcept = 0;
    virtual bool reportRiskTransition(
        const project::models::RiskAssessment& assessment) noexcept = 0;
};

class TaskSensor final : public TaskBase {
public:
    TaskSensor(const TaskRuntimeConfig& config,
               project::bsp::BoardSupportPackage& bsp,
               project::managers::SensorManager& sensor,
               project::queues::QueueRegistry& queues) noexcept;

private:
    bool onStart() noexcept override;
    TaskStepResult runOne() noexcept override;

    project::bsp::BoardSupportPackage& bsp_;
    project::managers::SensorManager& sensor_;
    project::queues::QueueRegistry& queues_;
};

class TaskFeature final : public TaskBase {
public:
    TaskFeature(const TaskRuntimeConfig& config,
                project::services::DataQualityService& quality,
                project::services::CalibrationService& calibration,
                project::services::FeaturePipeline& pipeline,
                ITaskEventReporter& events,
                project::queues::QueueRegistry& queues) noexcept;

private:
    bool onStart() noexcept override;
    TaskStepResult runOne() noexcept override;

    project::services::DataQualityService& quality_;
    project::services::CalibrationService& calibration_;
    project::services::FeaturePipeline& pipeline_;
    ITaskEventReporter& events_;
    project::queues::QueueRegistry& queues_;
};

class TaskInference final : public TaskBase {
public:
    TaskInference(const TaskRuntimeConfig& config,
                  project::ai::SchemaRegistry& schemas,
                  project::ai::IInferenceEngine& inference,
                  project::queues::QueueRegistry& queues) noexcept;

private:
    bool onStart() noexcept override;
    TaskStepResult runOne() noexcept override;
    void createFailureOutput(const project::models::FeatureVector& input,
                             project::models::ModelOutput& output,
                             project::models::InferenceStatus status) const noexcept;

    project::ai::SchemaRegistry& schemas_;
    project::ai::IInferenceEngine& inference_;
    project::queues::QueueRegistry& queues_;
    std::uint64_t inferenceId_{0};
    bool schemaCompatible_{false};
    bool engineReady_{false};
};

class TaskRisk final : public TaskBase {
public:
    TaskRisk(const TaskRuntimeConfig& config,
             project::managers::RiskManager& riskManager,
             IRiskTaskContext& context,
             ITaskEventReporter& events,
             project::queues::QueueRegistry& queues) noexcept;

private:
    TaskStepResult runOne() noexcept override;

    project::managers::RiskManager& riskManager_;
    IRiskTaskContext& context_;
    ITaskEventReporter& events_;
    project::queues::QueueRegistry& queues_;
};

class TaskLogger final : public TaskBase {
public:
    TaskLogger(const TaskRuntimeConfig& config, ITaskLogWriter& writer,
               project::queues::QueueRegistry& queues) noexcept;

private:
    TaskStepResult runOne() noexcept override;

    ITaskLogWriter& writer_;
    project::queues::QueueRegistry& queues_;
    bool preferEvent_{true};
};

class TaskDiagnostics final : public TaskBase {
public:
    TaskDiagnostics(const TaskRuntimeConfig& config,
                    ITaskHealthSource& source,
                    ITaskHealthPublisher& publisher,
                    project::queues::QueueRegistry& queues) noexcept;

private:
    TaskStepResult runOne() noexcept override;
    bool collect(project::models::HealthSnapshot& output) const noexcept;

    ITaskHealthSource& source_;
    ITaskHealthPublisher& publisher_;
    project::queues::QueueRegistry& queues_;
    std::uint64_t snapshotSequence_{0};
};

}  // namespace project::tasks
