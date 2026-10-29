#include "project/Queues/QueueRegistry.h"

#include <limits>
#include <type_traits>

namespace project::queues {

static_assert(std::is_trivially_copyable<project::models::SensorFrame>::value,
              "Q_SENSOR payload must be trivially copyable");
static_assert(std::is_trivially_copyable<project::models::FeatureVector>::value,
              "Q_FEATURE payload must be trivially copyable");
static_assert(std::is_trivially_copyable<project::models::ModelOutput>::value,
              "Q_MODEL payload must be trivially copyable");
static_assert(std::is_trivially_copyable<project::models::RiskAssessment>::value,
              "Q_RISK payload must be trivially copyable");
static_assert(std::is_trivially_copyable<project::models::EventRecord>::value,
              "Q_EVENT payload must be trivially copyable");

QueueRegistry& QueueRegistry::instance() noexcept {
    static QueueRegistry registry;
    return registry;
}

bool QueueRegistry::validDepth(std::size_t depth,
                               std::size_t itemSize) const noexcept {
    return depth > 0 &&
           depth <= static_cast<std::size_t>(
                        std::numeric_limits<UBaseType_t>::max()) &&
           itemSize > 0 &&
           itemSize <= static_cast<std::size_t>(
                           std::numeric_limits<UBaseType_t>::max()) &&
           depth <= std::numeric_limits<std::size_t>::max() / itemSize;
}

QueueRegistryStatus QueueRegistry::initialize(const QueueDepths& depths) noexcept {
    State expected = State::Uninitialized;
    if (!state_.compare_exchange_strong(expected, State::Initializing,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
        return expected == State::Ready
                   ? QueueRegistryStatus::AlreadyInitialized
                   : QueueRegistryStatus::InitializationInProgress;
    }

    if (!validDepth(depths.sensor, sizeof(project::models::SensorFrame)) ||
        !validDepth(depths.feature, sizeof(project::models::FeatureVector)) ||
        !validDepth(depths.model, sizeof(project::models::ModelOutput)) ||
        !validDepth(depths.risk, sizeof(project::models::RiskAssessment)) ||
        !validDepth(depths.event, sizeof(project::models::EventRecord)) ||
        !validDepth(depths.log, sizeof(LogRecord))) {
        state_.store(State::Uninitialized, std::memory_order_release);
        return QueueRegistryStatus::InvalidConfiguration;
    }

    sensor_.handle = xQueueCreate(static_cast<UBaseType_t>(depths.sensor),
                                  static_cast<UBaseType_t>(
                                      sizeof(project::models::SensorFrame)));
    feature_.handle = xQueueCreate(static_cast<UBaseType_t>(depths.feature),
                                   static_cast<UBaseType_t>(
                                       sizeof(project::models::FeatureVector)));
    model_.handle = xQueueCreate(static_cast<UBaseType_t>(depths.model),
                                 static_cast<UBaseType_t>(
                                     sizeof(project::models::ModelOutput)));
    risk_.handle = xQueueCreate(static_cast<UBaseType_t>(depths.risk),
                                static_cast<UBaseType_t>(
                                    sizeof(project::models::RiskAssessment)));
    event_.handle = xQueueCreate(static_cast<UBaseType_t>(depths.event),
                                 static_cast<UBaseType_t>(
                                     sizeof(project::models::EventRecord)));
    log_.handle = xQueueCreate(static_cast<UBaseType_t>(depths.log),
                               static_cast<UBaseType_t>(sizeof(LogRecord)));

    const bool createdAll = sensor_.handle != nullptr && feature_.handle != nullptr &&
                            model_.handle != nullptr && risk_.handle != nullptr &&
                            event_.handle != nullptr && log_.handle != nullptr;
    if (!createdAll) {
        releaseCreatedQueues();
        state_.store(State::Uninitialized, std::memory_order_release);
        return QueueRegistryStatus::AllocationFailed;
    }

    sensor_.capacity = static_cast<UBaseType_t>(depths.sensor);
    feature_.capacity = static_cast<UBaseType_t>(depths.feature);
    model_.capacity = static_cast<UBaseType_t>(depths.model);
    risk_.capacity = static_cast<UBaseType_t>(depths.risk);
    event_.capacity = static_cast<UBaseType_t>(depths.event);
    log_.capacity = static_cast<UBaseType_t>(depths.log);

    sensor_.droppedCount.store(0, std::memory_order_relaxed);
    feature_.droppedCount.store(0, std::memory_order_relaxed);
    model_.droppedCount.store(0, std::memory_order_relaxed);
    risk_.droppedCount.store(0, std::memory_order_relaxed);
    event_.droppedCount.store(0, std::memory_order_relaxed);
    log_.droppedCount.store(0, std::memory_order_relaxed);
    state_.store(State::Ready, std::memory_order_release);
    return QueueRegistryStatus::Ok;
}

void QueueRegistry::releaseCreatedQueues() noexcept {
    if (sensor_.handle != nullptr) {
        vQueueDelete(sensor_.handle);
        sensor_.handle = nullptr;
    }
    if (feature_.handle != nullptr) {
        vQueueDelete(feature_.handle);
        feature_.handle = nullptr;
    }
    if (model_.handle != nullptr) {
        vQueueDelete(model_.handle);
        model_.handle = nullptr;
    }
    if (risk_.handle != nullptr) {
        vQueueDelete(risk_.handle);
        risk_.handle = nullptr;
    }
    if (event_.handle != nullptr) {
        vQueueDelete(event_.handle);
        event_.handle = nullptr;
    }
    if (log_.handle != nullptr) {
        vQueueDelete(log_.handle);
        log_.handle = nullptr;
    }
}

bool QueueRegistry::isInitialized() const noexcept {
    return state_.load(std::memory_order_acquire) == State::Ready;
}

template <typename T>
QueueRegistryStatus QueueRegistry::send(QueueSlot<T>& slot, const T& item,
                                        TickType_t waitTicks) noexcept {
    if (state_.load(std::memory_order_acquire) != State::Ready) {
        return QueueRegistryStatus::NotInitialized;
    }
    if (waitTicks == portMAX_DELAY) {
        return QueueRegistryStatus::InvalidArgument;
    }
    if (xQueueSend(slot.handle, &item, waitTicks) == pdPASS) {
        return QueueRegistryStatus::Ok;
    }

    slot.droppedCount.fetch_add(1, std::memory_order_relaxed);
    return waitTicks == 0 ? QueueRegistryStatus::QueueFull
                          : QueueRegistryStatus::Timeout;
}

template <typename T>
QueueRegistryStatus QueueRegistry::receive(QueueSlot<T>& slot, T& item,
                                           TickType_t waitTicks) noexcept {
    if (state_.load(std::memory_order_acquire) != State::Ready) {
        return QueueRegistryStatus::NotInitialized;
    }
    if (waitTicks == portMAX_DELAY) {
        return QueueRegistryStatus::InvalidArgument;
    }
    if (xQueueReceive(slot.handle, &item, waitTicks) == pdPASS) {
        return QueueRegistryStatus::Ok;
    }

    return waitTicks == 0 ? QueueRegistryStatus::QueueEmpty
                          : QueueRegistryStatus::Timeout;
}

QueueRegistryStatus QueueRegistry::sendSensor(
    const project::models::SensorFrame& item, TickType_t waitTicks) noexcept {
    return send(sensor_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::receiveSensor(
    project::models::SensorFrame& item, TickType_t waitTicks) noexcept {
    return receive(sensor_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::sendFeature(
    const project::models::FeatureVector& item, TickType_t waitTicks) noexcept {
    return send(feature_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::receiveFeature(
    project::models::FeatureVector& item, TickType_t waitTicks) noexcept {
    return receive(feature_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::sendModel(
    const project::models::ModelOutput& item, TickType_t waitTicks) noexcept {
    return send(model_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::receiveModel(
    project::models::ModelOutput& item, TickType_t waitTicks) noexcept {
    return receive(model_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::sendRisk(
    const project::models::RiskAssessment& item, TickType_t waitTicks) noexcept {
    return send(risk_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::receiveRisk(
    project::models::RiskAssessment& item, TickType_t waitTicks) noexcept {
    return receive(risk_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::sendEvent(
    const project::models::EventRecord& item, TickType_t waitTicks) noexcept {
    return send(event_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::receiveEvent(
    project::models::EventRecord& item, TickType_t waitTicks) noexcept {
    return receive(event_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::sendLog(const LogRecord& item,
                                           TickType_t waitTicks) noexcept {
    return send(log_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::receiveLog(LogRecord& item,
                                              TickType_t waitTicks) noexcept {
    return receive(log_, item, waitTicks);
}

QueueRegistryStatus QueueRegistry::getStats(QueueId id,
                                            QueueStats& stats) const noexcept {
    stats = {};
    if (state_.load(std::memory_order_acquire) != State::Ready) {
        return QueueRegistryStatus::NotInitialized;
    }

    switch (id) {
    case QueueId::Sensor:
        stats.capacity = sensor_.capacity;
        stats.depth = static_cast<std::uint32_t>(
            uxQueueMessagesWaiting(sensor_.handle));
        stats.droppedCount = sensor_.droppedCount.load(std::memory_order_relaxed);
        stats.itemSizeBytes = static_cast<std::uint32_t>(
            sizeof(project::models::SensorFrame));
        break;
    case QueueId::Feature:
        stats.capacity = feature_.capacity;
        stats.depth = static_cast<std::uint32_t>(
            uxQueueMessagesWaiting(feature_.handle));
        stats.droppedCount = feature_.droppedCount.load(std::memory_order_relaxed);
        stats.itemSizeBytes = static_cast<std::uint32_t>(
            sizeof(project::models::FeatureVector));
        break;
    case QueueId::Model:
        stats.capacity = model_.capacity;
        stats.depth = static_cast<std::uint32_t>(
            uxQueueMessagesWaiting(model_.handle));
        stats.droppedCount = model_.droppedCount.load(std::memory_order_relaxed);
        stats.itemSizeBytes = static_cast<std::uint32_t>(
            sizeof(project::models::ModelOutput));
        break;
    case QueueId::Risk:
        stats.capacity = risk_.capacity;
        stats.depth = static_cast<std::uint32_t>(
            uxQueueMessagesWaiting(risk_.handle));
        stats.droppedCount = risk_.droppedCount.load(std::memory_order_relaxed);
        stats.itemSizeBytes = static_cast<std::uint32_t>(
            sizeof(project::models::RiskAssessment));
        break;
    case QueueId::Event:
        stats.capacity = event_.capacity;
        stats.depth = static_cast<std::uint32_t>(
            uxQueueMessagesWaiting(event_.handle));
        stats.droppedCount = event_.droppedCount.load(std::memory_order_relaxed);
        stats.itemSizeBytes = static_cast<std::uint32_t>(
            sizeof(project::models::EventRecord));
        break;
    case QueueId::Log:
        stats.capacity = log_.capacity;
        stats.depth = static_cast<std::uint32_t>(
            uxQueueMessagesWaiting(log_.handle));
        stats.droppedCount = log_.droppedCount.load(std::memory_order_relaxed);
        stats.itemSizeBytes = static_cast<std::uint32_t>(sizeof(LogRecord));
        break;
    default:
        return QueueRegistryStatus::UnknownQueue;
    }

    return QueueRegistryStatus::Ok;
}

}  // namespace project::queues
