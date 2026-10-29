#include "project/Tasks/TaskFramework.h"

#include <limits>

namespace project::tasks {

TaskHeartbeatRegistry& TaskHeartbeatRegistry::instance() noexcept {
    static TaskHeartbeatRegistry registry;
    return registry;
}

bool TaskHeartbeatRegistry::validId(TaskId id) noexcept {
    return static_cast<std::size_t>(id) < kTaskCount;
}

void TaskHeartbeatRegistry::registerTask(TaskId id,
                                        TaskHandle_t handle,
                                        TickType_t heartbeatTimeoutTicks) noexcept {
    if (!validId(id) || handle == nullptr) {
        return;
    }

    Entry& entry = entries_[static_cast<std::size_t>(id)];
    entry.registered.store(false, std::memory_order_relaxed);
    entry.createdTick.store(xTaskGetTickCount(), std::memory_order_relaxed);
    entry.heartbeatTimeoutTicks.store(heartbeatTimeoutTicks,
                                     std::memory_order_relaxed);
    entry.lastProgressTick.store(0, std::memory_order_relaxed);
    entry.progressCount.store(0, std::memory_order_relaxed);
    entry.failureCount.store(0, std::memory_order_relaxed);
    entry.deadlineMissCount.store(0, std::memory_order_relaxed);
    entry.hasProgress.store(false, std::memory_order_relaxed);
    entry.exited.store(false, std::memory_order_relaxed);
    entry.stale.store(false, std::memory_order_relaxed);
    entry.lastStackHighWaterWords.store(0, std::memory_order_relaxed);
    entry.handle.store(handle, std::memory_order_release);
    entry.lastStackHighWaterWords.store(
        static_cast<UBaseType_t>(uxTaskGetStackHighWaterMark(nullptr)),
        std::memory_order_relaxed);
    entry.registered.store(true, std::memory_order_release);
}

void TaskHeartbeatRegistry::unregisterTask(TaskId id,
                                           TaskHandle_t handle) noexcept {
    if (!validId(id)) {
        return;
    }

    Entry& entry = entries_[static_cast<std::size_t>(id)];
    if (entry.handle.load(std::memory_order_acquire) != handle) {
        return;
    }
    entry.lastStackHighWaterWords.store(
        static_cast<UBaseType_t>(uxTaskGetStackHighWaterMark(nullptr)),
        std::memory_order_relaxed);
    entry.exited.store(true, std::memory_order_release);
    entry.handle.store(nullptr, std::memory_order_release);
}

void TaskHeartbeatRegistry::reportProgress(TaskId id) noexcept {
    if (!validId(id)) {
        return;
    }

    Entry& entry = entries_[static_cast<std::size_t>(id)];
    entry.lastProgressTick.store(xTaskGetTickCount(), std::memory_order_relaxed);
    entry.lastStackHighWaterWords.store(
        static_cast<UBaseType_t>(uxTaskGetStackHighWaterMark(nullptr)),
        std::memory_order_relaxed);
    entry.hasProgress.store(true, std::memory_order_release);
    entry.progressCount.fetch_add(1, std::memory_order_relaxed);
    entry.stale.store(false, std::memory_order_release);
}

void TaskHeartbeatRegistry::reportFailure(TaskId id) noexcept {
    if (!validId(id)) {
        return;
    }

    Entry& entry = entries_[static_cast<std::size_t>(id)];
    entry.lastStackHighWaterWords.store(
        static_cast<UBaseType_t>(uxTaskGetStackHighWaterMark(nullptr)),
        std::memory_order_relaxed);
    entry.failureCount.fetch_add(1, std::memory_order_relaxed);
}

bool TaskHeartbeatRegistry::snapshot(
    TaskId id, TaskHeartbeatSnapshot& output) const noexcept {
    output = {};
    if (!validId(id)) {
        return false;
    }

    const Entry& entry = entries_[static_cast<std::size_t>(id)];
    if (!entry.registered.load(std::memory_order_acquire)) {
        return false;
    }
    output.progressCount = entry.progressCount.load(std::memory_order_relaxed);
    output.failureCount = entry.failureCount.load(std::memory_order_relaxed);
    output.deadlineMissCount =
        entry.deadlineMissCount.load(std::memory_order_relaxed);
    output.lastProgressTick = entry.lastProgressTick.load(std::memory_order_relaxed);
    output.createdTick = entry.createdTick.load(std::memory_order_relaxed);
    output.heartbeatTimeoutTicks =
        entry.heartbeatTimeoutTicks.load(std::memory_order_relaxed);
    output.hasProgress = entry.hasProgress.load(std::memory_order_acquire);
    const TickType_t now = xTaskGetTickCount();
    const TickType_t progressReference = output.hasProgress
                                            ? output.lastProgressTick
                                            : output.createdTick;
    output.stale =
        static_cast<TickType_t>(now - progressReference) >=
            output.heartbeatTimeoutTicks ||
        entry.exited.load(std::memory_order_acquire);
    const bool wasStale = entry.stale.exchange(output.stale,
                                               std::memory_order_acq_rel);
    if (output.stale && !wasStale) {
        output.deadlineMissCount =
            entry.deadlineMissCount.fetch_add(1, std::memory_order_relaxed) + 1U;
    } else {
        output.deadlineMissCount =
            entry.deadlineMissCount.load(std::memory_order_relaxed);
    }
    output.stackHighWaterWords =
        entry.lastStackHighWaterWords.load(std::memory_order_relaxed);
    output.taskCreated = true;
    output.taskExited = entry.exited.load(std::memory_order_acquire);
    return true;
}

TaskBase::TaskBase(TaskId id, const char* name, TaskSchedule schedule,
                   const TaskRuntimeConfig& config) noexcept
    : id_(id), name_(name), schedule_(schedule), config_(config) {}

bool TaskBase::configIsValid() const noexcept {
    if (name_ == nullptr || name_[0] == '\0' ||
        !config_.priorityConfigured || config_.stackWords == 0 ||
        config_.stackWords >
            static_cast<std::uint32_t>(std::numeric_limits<configSTACK_DEPTH_TYPE>::max()) ||
        config_.heartbeatTimeoutTicks == 0 ||
        config_.heartbeatTimeoutTicks == portMAX_DELAY ||
        config_.queueWaitTicks == portMAX_DELAY ||
        config_.idleWaitTicks == portMAX_DELAY ||
        config_.cycleTicks == portMAX_DELAY ||
        config_.priority >= configMAX_PRIORITIES) {
        return false;
    }

    if (schedule_ == TaskSchedule::Periodic) {
        return config_.cycleTicks > 0;
    }
    if (id_ == TaskId::Logger) {
        return config_.queueWaitTicks == 0 && config_.idleWaitTicks > 0;
    }
    return config_.queueWaitTicks > 0 || config_.idleWaitTicks > 0;
}

TaskStatus TaskBase::prepare() noexcept {
    if (prepared_) {
        return TaskStatus::AlreadyStarted;
    }
    if (!configIsValid()) {
        return TaskStatus::InvalidConfiguration;
    }

    const BaseType_t created = xTaskCreate(
        &TaskBase::entry, name_,
        static_cast<configSTACK_DEPTH_TYPE>(config_.stackWords), this,
        config_.priority, &handle_);
    if (created != pdPASS || handle_ == nullptr) {
        handle_ = nullptr;
        return TaskStatus::CreateFailed;
    }

    prepared_ = true;
    return TaskStatus::Ok;
}

void TaskBase::arm(bool run) noexcept {
    runEnabled_.store(run, std::memory_order_release);
}

void TaskBase::release() noexcept {
    if (!prepared_ || handle_ == nullptr) {
        return;
    }

    xTaskNotifyGive(handle_);
}

void TaskBase::entry(void* context) noexcept {
    auto* task = static_cast<TaskBase*>(context);
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (!task->runEnabled_.load(std::memory_order_acquire)) {
        vTaskDelete(nullptr);
        return;
    }

    task->running_.store(true, std::memory_order_release);
    TaskHeartbeatRegistry::instance().registerTask(
        task->id_, task->handle_, task->config_.heartbeatTimeoutTicks);
    task->execute();
    TaskHeartbeatRegistry::instance().unregisterTask(task->id_, task->handle_);
    task->running_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

void TaskBase::execute() noexcept {
    if (!onStart()) {
        TaskHeartbeatRegistry::instance().reportFailure(id_);
        return;
    }

    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        const TaskStepResult result = runOne();
        if (result == TaskStepResult::Progress) {
            TaskHeartbeatRegistry::instance().reportProgress(id_);
        } else if (result == TaskStepResult::Failure) {
            TaskHeartbeatRegistry::instance().reportFailure(id_);
        }

        if (schedule_ == TaskSchedule::Periodic) {
            vTaskDelayUntil(&lastWake, config_.cycleTicks);
        } else if (result != TaskStepResult::Progress &&
                   config_.queueWaitTicks == 0 &&
                   config_.idleWaitTicks > 0) {
            vTaskDelay(config_.idleWaitTicks);
        }
    }
}

bool TaskBase::onStart() noexcept {
    return true;
}

TaskFramework::TaskFramework(
    const std::array<TaskBase*, kTaskCount>& tasks) noexcept
    : tasks_(tasks) {}

bool TaskFramework::definitionsAreValid() const noexcept {
    std::array<bool, kTaskCount> seen{};
    for (const TaskBase* task : tasks_) {
        if (task == nullptr ||
            static_cast<std::size_t>(task->id()) >= kTaskCount ||
            !task->configIsValid()) {
            return false;
        }

        const std::size_t index = static_cast<std::size_t>(task->id());
        if (seen[index]) {
            return false;
        }
        seen[index] = true;
    }

    for (bool taskSeen : seen) {
        if (!taskSeen) {
            return false;
        }
    }
    return true;
}

TaskStatus TaskFramework::startAll() noexcept {
    if (started_) {
        return TaskStatus::AlreadyStarted;
    }
    if (!definitionsAreValid()) {
        return TaskStatus::InvalidConfiguration;
    }

    std::size_t preparedCount = 0;
    for (TaskBase* task : tasks_) {
        const TaskStatus status = task->prepare();
        if (status != TaskStatus::Ok) {
            vTaskSuspendAll();
            for (std::size_t i = 0; i < preparedCount; ++i) {
                tasks_[i]->arm(false);
            }
            for (std::size_t i = 0; i < preparedCount; ++i) {
                tasks_[i]->release();
            }
            (void)xTaskResumeAll();
            return status;
        }
        ++preparedCount;
    }

    // Each created task is held at its entry gate until all task creations succeed.
    vTaskSuspendAll();
    for (TaskBase* task : tasks_) {
        task->arm(true);
    }
    for (TaskBase* task : tasks_) {
        task->release();
    }
    started_ = true;
    (void)xTaskResumeAll();
    return TaskStatus::Ok;
}

bool TaskFramework::isRunning(TaskId id) const noexcept {
    if (static_cast<std::size_t>(id) >= kTaskCount) {
        return false;
    }
    for (const TaskBase* task : tasks_) {
        if (task != nullptr && task->id() == id) {
            return task->isRunning();
        }
    }
    return false;
}

}  // namespace project::tasks
