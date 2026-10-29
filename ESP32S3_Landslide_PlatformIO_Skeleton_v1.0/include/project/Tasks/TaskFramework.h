#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace project::tasks {

enum class TaskId : std::uint8_t {
    Sensor = 0,
    Feature,
    Inference,
    Risk,
    Logger,
    Diagnostics,
    Count
};

constexpr std::size_t kTaskCount = static_cast<std::size_t>(TaskId::Count);

enum class TaskStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    AlreadyStarted,
    CreateFailed,
    StartFailed
};

enum class TaskSchedule : std::uint8_t {
    Periodic,
    QueueDriven
};

enum class TaskStepResult : std::uint8_t {
    Idle,
    Progress,
    Failure
};

struct TaskRuntimeConfig {
    // Values must be supplied from target measurements/review; no defaults
    // represent an approved priority, stack, blocking time, or deadline.
    UBaseType_t priority{0};
    std::uint32_t stackWords{0};
    TickType_t cycleTicks{0};
    TickType_t queueWaitTicks{0};
    TickType_t idleWaitTicks{0};
    TickType_t heartbeatTimeoutTicks{0};
    bool priorityConfigured{false};
};

struct TaskHeartbeatSnapshot {
    std::uint32_t progressCount{0};
    std::uint32_t failureCount{0};
    std::uint32_t deadlineMissCount{0};
    TickType_t lastProgressTick{0};
    TickType_t createdTick{0};
    TickType_t heartbeatTimeoutTicks{0};
    UBaseType_t stackHighWaterWords{0};
    bool taskCreated{false};
    bool taskExited{false};
    bool hasProgress{false};
    bool stale{false};
};

class TaskHeartbeatRegistry final {
public:
    static TaskHeartbeatRegistry& instance() noexcept;

    void registerTask(TaskId id, TaskHandle_t handle,
                      TickType_t heartbeatTimeoutTicks) noexcept;
    void unregisterTask(TaskId id, TaskHandle_t handle) noexcept;
    void reportProgress(TaskId id) noexcept;
    void reportFailure(TaskId id) noexcept;
    bool snapshot(TaskId id, TaskHeartbeatSnapshot& output) const noexcept;

    TaskHeartbeatRegistry(const TaskHeartbeatRegistry&) = delete;
    TaskHeartbeatRegistry& operator=(const TaskHeartbeatRegistry&) = delete;

private:
    struct Entry {
        std::atomic<TaskHandle_t> handle{nullptr};
        std::atomic<std::uint32_t> progressCount{0};
        std::atomic<std::uint32_t> failureCount{0};
        std::atomic<std::uint32_t> deadlineMissCount{0};
        std::atomic<TickType_t> lastProgressTick{0};
        std::atomic<TickType_t> createdTick{0};
        std::atomic<TickType_t> heartbeatTimeoutTicks{0};
        std::atomic<bool> hasProgress{false};
        std::atomic<bool> registered{false};
        std::atomic<bool> exited{false};
        std::atomic<bool> stale{false};
        std::atomic<UBaseType_t> lastStackHighWaterWords{0};
    };

    TaskHeartbeatRegistry() noexcept = default;
    static bool validId(TaskId id) noexcept;

    std::array<Entry, kTaskCount> entries_{};
};

class TaskBase {
public:
    TaskBase(TaskId id, const char* name, TaskSchedule schedule,
             const TaskRuntimeConfig& config) noexcept;
    virtual ~TaskBase() = default;

    TaskBase(const TaskBase&) = delete;
    TaskBase& operator=(const TaskBase&) = delete;

    TaskId id() const noexcept { return id_; }
    const TaskRuntimeConfig& config() const noexcept { return config_; }
    const char* name() const noexcept { return name_; }
    TaskHandle_t handle() const noexcept { return handle_; }
    bool isPrepared() const noexcept { return prepared_; }
    bool isRunning() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

protected:
    virtual bool onStart() noexcept;
    virtual TaskStepResult runOne() noexcept = 0;

private:
    friend class TaskFramework;

    bool configIsValid() const noexcept;
    TaskStatus prepare() noexcept;
    void arm(bool run) noexcept;
    void release() noexcept;
    void execute() noexcept;
    static void entry(void* context) noexcept;

    TaskId id_;
    const char* name_;
    TaskSchedule schedule_;
    TaskRuntimeConfig config_;
    TaskHandle_t handle_{nullptr};
    std::atomic<bool> runEnabled_{false};
    std::atomic<bool> running_{false};
    bool prepared_{false};
};

class TaskFramework final {
public:
    explicit TaskFramework(const std::array<TaskBase*, kTaskCount>& tasks) noexcept;
    TaskStatus startAll() noexcept;
    bool isRunning(TaskId id) const noexcept;

    TaskFramework(const TaskFramework&) = delete;
    TaskFramework& operator=(const TaskFramework&) = delete;

private:
    bool definitionsAreValid() const noexcept;
    std::array<TaskBase*, kTaskCount> tasks_;
    bool started_{false};
};

}  // namespace project::tasks
