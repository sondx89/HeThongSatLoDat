#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Models/DataContract.h"
#include "Queues/LogRecord.h"

namespace project::queues {

enum class QueueRegistryStatus : std::uint8_t {
    Ok,
    AlreadyInitialized,
    InitializationInProgress,
    NotInitialized,
    InvalidConfiguration,
    AllocationFailed,
    QueueFull,
    QueueEmpty,
    Timeout,
    InvalidArgument,
    UnknownQueue
};

enum class QueueId : std::uint8_t {
    Sensor,
    Feature,
    Model,
    Risk,
    // TODO(TBD): reconcile ICD's shared Q_EVENT/Q_LOG payload statement with
    // the LLD's separate event-priority and tagged-log queue descriptions.
    Event,
    Log
};

struct QueueDepths {
    // All depths intentionally default to zero. Set them only after sizing from
    // measured producer rates, worst-case consumer blocking, bursts and RAM budget.
    std::size_t sensor{0};
    std::size_t feature{0};
    std::size_t model{0};
    std::size_t risk{0};
    std::size_t event{0};
    std::size_t log{0};
};

struct QueueStats {
    std::uint32_t capacity{0};
    std::uint32_t depth{0};
    std::uint32_t highWaterMark{0};
    std::uint32_t droppedCount{0};
    std::uint32_t itemSizeBytes{0};
};

// Owns all queue handles for the lifetime of the application. Tasks borrow
// access through typed methods; they must never delete queues or retain handles.
class QueueRegistry final {
public:
    static QueueRegistry& instance() noexcept;

    QueueRegistryStatus initialize(const QueueDepths& depths) noexcept;
    bool isInitialized() const noexcept;

    QueueRegistryStatus sendSensor(const project::models::SensorFrame& item,
                                   TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus receiveSensor(project::models::SensorFrame& item,
                                      TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus sendFeature(const project::models::FeatureVector& item,
                                    TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus receiveFeature(project::models::FeatureVector& item,
                                       TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus sendModel(const project::models::ModelOutput& item,
                                  TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus receiveModel(project::models::ModelOutput& item,
                                     TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus sendRisk(const project::models::RiskAssessment& item,
                                 TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus receiveRisk(project::models::RiskAssessment& item,
                                    TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus sendEvent(const project::models::EventRecord& item,
                                  TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus receiveEvent(project::models::EventRecord& item,
                                     TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus sendLog(const LogRecord& item,
                                TickType_t waitTicks = 0) noexcept;
    QueueRegistryStatus receiveLog(LogRecord& item,
                                   TickType_t waitTicks = 0) noexcept;

    QueueRegistryStatus getStats(QueueId id, QueueStats& stats) const noexcept;

    QueueRegistry(const QueueRegistry&) = delete;
    QueueRegistry& operator=(const QueueRegistry&) = delete;
    QueueRegistry(QueueRegistry&&) = delete;
    QueueRegistry& operator=(QueueRegistry&&) = delete;

private:
    enum class State : std::uint8_t {
        Uninitialized,
        Initializing,
        Ready
    };

    template <typename T>
    struct QueueSlot {
        QueueHandle_t handle{nullptr};
        UBaseType_t capacity{0};
        std::atomic<std::uint32_t> highWaterMark{0};
        std::atomic<std::uint32_t> droppedCount{0};
    };

    QueueRegistry() noexcept = default;
    ~QueueRegistry() = default;

    template <typename T>
    QueueRegistryStatus send(QueueSlot<T>& slot, const T& item,
                             TickType_t waitTicks) noexcept;
    template <typename T>
    QueueRegistryStatus receive(QueueSlot<T>& slot, T& item,
                                TickType_t waitTicks) noexcept;

    bool validDepth(std::size_t depth, std::size_t itemSize) const noexcept;
    void releaseCreatedQueues() noexcept;

    std::atomic<State> state_{State::Uninitialized};
    QueueSlot<project::models::SensorFrame> sensor_{};
    QueueSlot<project::models::FeatureVector> feature_{};
    QueueSlot<project::models::ModelOutput> model_{};
    QueueSlot<project::models::RiskAssessment> risk_{};
    QueueSlot<project::models::EventRecord> event_{};
    QueueSlot<LogRecord> log_{};
};

}  // namespace project::queues
