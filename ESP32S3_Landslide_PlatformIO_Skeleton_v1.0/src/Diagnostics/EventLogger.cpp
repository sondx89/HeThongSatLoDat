#include "project/Diagnostics/EventLogger.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "project/Queues/QueueRegistry.h"

namespace project::diagnostics {
namespace {

std::atomic<std::uint32_t> gEventSequence{0};
std::atomic<std::uint64_t> gEventId{0};

constexpr char kCsvHeader[] =
    "schema_version,event_id,event_catalog_id,event_sequence_no,boot_id,"
    "timestamp_ms,event_name,severity,source,status_code,error_code,"
    "error_code_valid,fault_code,fault_code_valid,risk_state,health_state,"
    "manifest_id,manifest_id_valid,firmware_version,model_version,"
    "model_version_valid,feature_schema_hash,feature_schema_hash_valid,"
    "config_version,config_version_valid,calibration_version,"
    "calibration_version_valid,related_sequence_no,related_sequence_no_valid,"
    "message,value_type,value,value_text,unit,value_valid,dropped_event_count,"
    "crc32,crc32_valid,sensor_id,sensor_id_valid,state_before,"
    "state_before_valid,state_after,state_after_valid\n";

bool validUtf8(const char* text, std::size_t length) noexcept {
    std::size_t index = 0;
    while (index < length) {
        const auto first = static_cast<std::uint8_t>(text[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        std::size_t continuationCount = 0;
        std::uint8_t secondMin = 0x80U;
        std::uint8_t secondMax = 0xBFU;
        if (first >= 0xC2U && first <= 0xDFU) {
            continuationCount = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            continuationCount = 2U;
            if (first == 0xE0U) secondMin = 0xA0U;
            if (first == 0xEDU) secondMax = 0x9FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            continuationCount = 3U;
            if (first == 0xF0U) secondMin = 0x90U;
            if (first == 0xF4U) secondMax = 0x8FU;
        } else {
            return false;
        }

        if (continuationCount > length - index - 1U) {
            return false;
        }
        const auto second = static_cast<std::uint8_t>(text[index + 1U]);
        if (second < secondMin || second > secondMax) {
            return false;
        }
        for (std::size_t offset = 2U; offset <= continuationCount; ++offset) {
            const auto continuation =
                static_cast<std::uint8_t>(text[index + offset]);
            if (continuation < 0x80U || continuation > 0xBFU) {
                return false;
            }
        }
        index += continuationCount + 1U;
    }
    return true;
}

template <typename T>
bool incrementWithoutWrap(std::atomic<T>& counter, T& value) noexcept {
    T current = counter.load(std::memory_order_relaxed);
    for (;;) {
        if (current == static_cast<T>(~static_cast<T>(0))) {
            return false;
        }
        const T next = static_cast<T>(current + 1U);
        if (counter.compare_exchange_weak(current, next,
                                          std::memory_order_relaxed,
                                          std::memory_order_relaxed)) {
            value = next;
            return true;
        }
    }
}

class CsvWriter final {
public:
    CsvWriter(char* output, std::size_t capacity) noexcept
        : output_(output), capacity_(capacity) {}

    EventLoggerStatus append(char value) noexcept {
        if (size_ == capacity_) {
            return EventLoggerStatus::BufferTooSmall;
        }
        output_[size_++] = value;
        return EventLoggerStatus::Ok;
    }

    EventLoggerStatus append(const char* value) noexcept {
        if (value == nullptr) {
            return EventLoggerStatus::InvalidRecord;
        }
        while (*value != '\0') {
            const EventLoggerStatus status = append(*value++);
            if (status != EventLoggerStatus::Ok) {
                return status;
            }
        }
        return EventLoggerStatus::Ok;
    }

    template <std::size_t N>
    EventLoggerStatus appendQuoted(const char (&value)[N]) noexcept {
        std::size_t length = 0;
        while (length < N && value[length] != '\0') {
            ++length;
        }
        if (length == N) {
            return EventLoggerStatus::InvalidRecord;
        }
        if (!validUtf8(value, length)) {
            return EventLoggerStatus::InvalidRecord;
        }

        EventLoggerStatus status = append('"');
        if (status != EventLoggerStatus::Ok) {
            return status;
        }
        for (std::size_t index = 0; index < length; ++index) {
            if (value[index] == '"') {
                status = append('"');
                if (status != EventLoggerStatus::Ok) {
                    return status;
                }
            }
            status = append(value[index]);
            if (status != EventLoggerStatus::Ok) {
                return status;
            }
        }
        return append('"');
    }

    EventLoggerStatus appendQuoted(const char* value) noexcept {
        if (value == nullptr) {
            return EventLoggerStatus::InvalidRecord;
        }
        std::size_t length = 0;
        while (length < 64U && value[length] != '\0') {
            ++length;
        }
        if (length == 64U) {
            return EventLoggerStatus::InvalidRecord;
        }
        if (!validUtf8(value, length)) {
            return EventLoggerStatus::InvalidRecord;
        }

        EventLoggerStatus status = append('"');
        if (status != EventLoggerStatus::Ok) {
            return status;
        }
        for (std::size_t index = 0; index < length; ++index) {
            if (value[index] == '"') {
                status = append('"');
                if (status != EventLoggerStatus::Ok) {
                    return status;
                }
            }
            status = append(value[index]);
            if (status != EventLoggerStatus::Ok) {
                return status;
            }
        }
        return append('"');
    }

    EventLoggerStatus appendBool(bool value) noexcept {
        return append(value ? "true" : "false");
    }

    EventLoggerStatus appendUInt(std::uint64_t value) noexcept {
        char number[21]{};
        const int length = std::snprintf(number, sizeof(number), "%" PRIu64, value);
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(number)) {
            return EventLoggerStatus::InvalidRecord;
        }
        return append(number);
    }

    EventLoggerStatus appendUInt32(std::uint32_t value) noexcept {
        char number[11]{};
        const int length = std::snprintf(number, sizeof(number), "%" PRIu32, value);
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(number)) {
            return EventLoggerStatus::InvalidRecord;
        }
        return append(number);
    }

    EventLoggerStatus appendFloat(float value) noexcept {
        if (!std::isfinite(value)) {
            return EventLoggerStatus::InvalidRecord;
        }
        char number[24]{};
        const int length = std::snprintf(number, sizeof(number), "%.9g",
                                         static_cast<double>(value));
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(number)) {
            return EventLoggerStatus::InvalidRecord;
        }
        return append(number);
    }

    EventLoggerStatus appendHexHash(const std::uint8_t (&hash)[32]) noexcept {
        static constexpr char kHex[] = "0123456789abcdef";
        for (const std::uint8_t byte : hash) {
            EventLoggerStatus status = append(kHex[(byte >> 4U) & 0x0FU]);
            if (status != EventLoggerStatus::Ok) {
                return status;
            }
            status = append(kHex[byte & 0x0FU]);
            if (status != EventLoggerStatus::Ok) {
                return status;
            }
        }
        return EventLoggerStatus::Ok;
    }

    EventLoggerStatus appendFieldSeparator() noexcept { return append(','); }
    std::size_t size() const noexcept { return size_; }

private:
    char* output_;
    std::size_t capacity_;
    std::size_t size_{0};
};

const char* severityName(EventSeverity severity) noexcept {
    switch (severity) {
    case EventSeverity::Debug:
        return "DEBUG";
    case EventSeverity::Info:
        return "INFO";
    case EventSeverity::Notice:
        return "NOTICE";
    case EventSeverity::Warning:
        return "WARNING";
    case EventSeverity::Error:
        return "ERROR";
    case EventSeverity::Critical:
        return "CRITICAL";
    }
    return nullptr;
}

const char* riskStateName(project::models::RiskState state) noexcept {
    using project::models::RiskState;
    switch (state) {
    case RiskState::Startup:
        return "STARTUP";
    case RiskState::Normal:
        return "NORMAL";
    case RiskState::Watch:
        return "WATCH";
    case RiskState::Monitor:
        return "MONITOR";
    case RiskState::Warning:
        return "WARNING";
    case RiskState::DangerTest:
        return "DANGER_TEST";
    case RiskState::Alarm:
        return "ALARM";
    case RiskState::Critical:
        return "CRITICAL";
    case RiskState::Unknown:
        return "UNKNOWN";
    }
    return nullptr;
}

const char* healthStateName(project::models::HealthState state) noexcept {
    using project::models::HealthState;
    switch (state) {
    case HealthState::Unknown:
        return "UNKNOWN";
    case HealthState::Healthy:
        return "HEALTHY";
    case HealthState::Degraded:
        return "DEGRADED";
    case HealthState::Fault:
        return "FAULT";
    }
    return nullptr;
}

const char* valueTypeName(EventRecord::ValueType type) noexcept {
    switch (type) {
    case EventRecord::ValueType::None:
        return "NONE";
    case EventRecord::ValueType::Numeric:
        return "NUMERIC";
    case EventRecord::ValueType::Text:
        return "TEXT";
    }
    return nullptr;
}

const char* sensorName(project::models::SensorId sensor) noexcept {
    using project::models::SensorId;
    switch (sensor) {
    case SensorId::Unknown:
        return "UNKNOWN";
    case SensorId::Imu:
        return "IMU";
    case SensorId::SoilMoisture:
        return "SOIL_MOISTURE";
    case SensorId::Rain:
        return "RAIN";
    case SensorId::Hx710b:
        return "HX710B";
    }
    return nullptr;
}

template <std::size_t N>
EventLoggerStatus appendOptionalQuoted(CsvWriter& writer,
                                       const char (&text)[N],
                                       bool valid) noexcept {
    if (!valid) {
        return writer.append("\"\"");
    }
    return writer.appendQuoted(text);
}

EventLoggerStatus appendOptionalUInt(CsvWriter& writer, std::uint64_t value,
                                     bool valid) noexcept {
    return valid ? writer.appendUInt(value) : writer.append("\"\"");
}

template <std::size_t N>
bool equalsText(const char (&value)[N], const char* expected) noexcept {
    std::size_t index = 0;
    while (index < N && value[index] != '\0' && expected[index] != '\0') {
        if (value[index] != expected[index]) {
            return false;
        }
        ++index;
    }
    return index < N && value[index] == '\0' && expected[index] == '\0';
}

#define APPEND_CSV_FIELD(expression)                 \
    do {                                             \
        status = (expression);                       \
        if (status != EventLoggerStatus::Ok) {       \
            return status;                           \
        }                                            \
        status = writer.appendFieldSeparator();      \
        if (status != EventLoggerStatus::Ok) {       \
            return status;                           \
        }                                            \
    } while (false)

EventLoggerStatus serializeEvent(const EventRecord& event, char* output,
                                 std::size_t capacity,
                                 std::size_t& bytesWritten) noexcept {
    bytesWritten = 0;
    if (output == nullptr && capacity != 0) {
        return EventLoggerStatus::InvalidRecord;
    }

    CsvWriter writer(output, capacity);
    EventLoggerStatus status = EventLoggerStatus::Ok;
    const char* severity = severityName(event.severity);
    const char* risk = riskStateName(event.riskState);
    const char* health = healthStateName(event.healthState);
    const char* valueType = valueTypeName(event.valueType);
    const char* sensor = sensorName(event.sensorId);
    if (!event.valid || event.schemaVersion != 1U || event.eventId == 0U ||
        event.eventSequenceNo == 0U ||
        severity == nullptr || risk == nullptr || health == nullptr ||
        valueType == nullptr || sensor == nullptr ||
        (event.errorCodeValid &&
         (equalsText(event.errorCode, "ERR_OK") ||
          equalsText(event.errorCode, "0x0000"))) ||
        (event.valueValid &&
         (event.valueType == EventRecord::ValueType::None ||
          (event.valueType == EventRecord::ValueType::Numeric &&
           !std::isfinite(event.value))))) {
        return EventLoggerStatus::InvalidRecord;
    }

    status = writer.appendUInt(event.schemaVersion);
    if (status != EventLoggerStatus::Ok) return status;
    APPEND_CSV_FIELD(writer.appendUInt(event.eventId));
    APPEND_CSV_FIELD(writer.appendQuoted(event.eventCatalogId));
    APPEND_CSV_FIELD(writer.appendUInt32(event.eventSequenceNo));
    APPEND_CSV_FIELD(writer.appendUInt(event.bootId));
    APPEND_CSV_FIELD(writer.appendUInt(event.timestampMs));
    APPEND_CSV_FIELD(writer.appendQuoted(event.eventName));
    APPEND_CSV_FIELD(writer.appendQuoted(severity));
    APPEND_CSV_FIELD(writer.appendQuoted(event.source));
    APPEND_CSV_FIELD(writer.appendQuoted(event.statusCode));
    APPEND_CSV_FIELD(appendOptionalQuoted(writer, event.errorCode,
                                           event.errorCodeValid));
    APPEND_CSV_FIELD(writer.appendBool(event.errorCodeValid));
    APPEND_CSV_FIELD(appendOptionalQuoted(writer, event.faultCode,
                                           event.faultCodeValid));
    APPEND_CSV_FIELD(writer.appendBool(event.faultCodeValid));
    APPEND_CSV_FIELD(writer.appendQuoted(risk));
    APPEND_CSV_FIELD(writer.appendQuoted(health));
    APPEND_CSV_FIELD(appendOptionalQuoted(writer, event.manifestId,
                                           event.manifestIdValid));
    APPEND_CSV_FIELD(writer.appendBool(event.manifestIdValid));
    APPEND_CSV_FIELD(writer.appendQuoted(event.firmwareVersion));
    APPEND_CSV_FIELD(appendOptionalQuoted(writer, event.modelVersion,
                                           event.modelVersionValid));
    APPEND_CSV_FIELD(writer.appendBool(event.modelVersionValid));
    status = event.featureSchemaHashValid
                 ? writer.append('"')
                 : writer.append("\"\"");
    if (status != EventLoggerStatus::Ok) return status;
    if (event.featureSchemaHashValid) {
        status = writer.appendHexHash(event.featureSchemaHashSha256);
        if (status != EventLoggerStatus::Ok) return status;
        status = writer.append('"');
        if (status != EventLoggerStatus::Ok) return status;
    }
    APPEND_CSV_FIELD(writer.appendBool(event.featureSchemaHashValid));
    APPEND_CSV_FIELD(appendOptionalQuoted(writer, event.configVersion,
                                           event.configVersionValid));
    APPEND_CSV_FIELD(writer.appendBool(event.configVersionValid));
    APPEND_CSV_FIELD(appendOptionalQuoted(writer, event.calibrationVersion,
                                           event.calibrationVersionValid));
    APPEND_CSV_FIELD(writer.appendBool(event.calibrationVersionValid));
    APPEND_CSV_FIELD(appendOptionalUInt(writer, event.relatedSequenceNo,
                                         event.relatedSequenceNoValid));
    APPEND_CSV_FIELD(writer.appendBool(event.relatedSequenceNoValid));
    APPEND_CSV_FIELD(writer.appendQuoted(event.message));
    APPEND_CSV_FIELD(writer.appendQuoted(valueType));

    if (event.valueValid && event.valueType == EventRecord::ValueType::Numeric) {
        status = writer.appendFloat(event.value);
    } else {
        status = writer.append("\"\"");
    }
    if (status != EventLoggerStatus::Ok) return status;
    APPEND_CSV_FIELD(appendOptionalQuoted(writer, event.valueText,
                                           event.valueValid &&
                                               event.valueType == EventRecord::ValueType::Text));
    APPEND_CSV_FIELD(appendOptionalQuoted(writer, event.unit, event.valueValid));
    APPEND_CSV_FIELD(writer.appendBool(event.valueValid));
    APPEND_CSV_FIELD(writer.appendUInt32(event.droppedEventCount));

    if (event.crc32Valid) {
        status = writer.appendUInt32(event.crc32);
    } else {
        status = writer.append("\"\"");
    }
    if (status != EventLoggerStatus::Ok) return status;
    APPEND_CSV_FIELD(writer.appendBool(event.crc32Valid));
    APPEND_CSV_FIELD(writer.appendQuoted(sensor));
    APPEND_CSV_FIELD(writer.appendBool(event.sensorIdValid));
    APPEND_CSV_FIELD(appendOptionalQuoted(writer, event.stateBefore,
                                           event.stateBeforeValid));
    APPEND_CSV_FIELD(writer.appendBool(event.stateBeforeValid));
    APPEND_CSV_FIELD(appendOptionalQuoted(writer, event.stateAfter,
                                           event.stateAfterValid));
    status = writer.appendBool(event.stateAfterValid);
    if (status != EventLoggerStatus::Ok) return status;
    status = writer.append('\n');
    if (status != EventLoggerStatus::Ok) return status;
    bytesWritten = writer.size();
    return EventLoggerStatus::Ok;
}

#undef APPEND_CSV_FIELD

}  // namespace

bool EventLogger::enqueue(const EventRecord& event) noexcept {
    return enqueueWithStatus(event) == EventLoggerStatus::Ok;
}

EventLoggerStatus EventLogger::enqueueWithStatus(
    const EventRecord& event) noexcept {
    if (!event.valid) {
        return EventLoggerStatus::InvalidEvent;
    }

    EventRecord queued = event;
    project::queues::QueueStats stats{};
    const project::queues::QueueRegistryStatus statsStatus =
        project::queues::QueueRegistry::instance().getStats(
            project::queues::QueueId::Event, stats);
    if (statsStatus == project::queues::QueueRegistryStatus::NotInitialized) {
        return EventLoggerStatus::QueueUnavailable;
    }
    if (statsStatus != project::queues::QueueRegistryStatus::Ok) {
        return EventLoggerStatus::QueueError;
    }
    queued.droppedEventCount = stats.droppedCount;

    if (!incrementWithoutWrap(gEventSequence, queued.eventSequenceNo) ||
        !incrementWithoutWrap(gEventId, queued.eventId)) {
        return EventLoggerStatus::IdentifierExhausted;
    }

    const project::queues::QueueRegistryStatus status =
        project::queues::QueueRegistry::instance().sendEvent(queued, 0);
    switch (status) {
    case project::queues::QueueRegistryStatus::Ok:
        return EventLoggerStatus::Ok;
    case project::queues::QueueRegistryStatus::QueueFull:
        return EventLoggerStatus::QueueFull;
    case project::queues::QueueRegistryStatus::NotInitialized:
        return EventLoggerStatus::QueueUnavailable;
    case project::queues::QueueRegistryStatus::InvalidArgument:
        return EventLoggerStatus::InvalidEvent;
    default:
        return EventLoggerStatus::QueueError;
    }
}

EventLoggerStatus EventLogger::writeOutputs(
    const EventRecord& event, IEventOutput& output, char* csvBuffer,
    std::size_t csvCapacity, std::size_t& bytesWritten) const noexcept {
    const EventLoggerStatus serialized =
        serializeEvent(event, csvBuffer, csvCapacity, bytesWritten);
    if (serialized != EventLoggerStatus::Ok) {
        return serialized;
    }

    const bool serialWritten =
        output.writeSerialCsv(csvBuffer, bytesWritten);
    const bool csvWritten = output.writeCsv(csvBuffer, bytesWritten);
    if (serialWritten && csvWritten) {
        return EventLoggerStatus::Ok;
    }
    if (!serialWritten && !csvWritten) {
        return EventLoggerStatus::OutputsFailed;
    }
    return serialWritten ? EventLoggerStatus::CsvOutputFailed
                         : EventLoggerStatus::SerialOutputFailed;
}

EventLoggerStatus EventLogger::writeCsvHeader(
    char* output, std::size_t capacity, std::size_t& bytesWritten) noexcept {
    bytesWritten = 0;
    if (output == nullptr && capacity != 0) {
        return EventLoggerStatus::InvalidRecord;
    }
    constexpr std::size_t headerSize = sizeof(kCsvHeader) - 1U;
    if (headerSize > capacity) {
        return EventLoggerStatus::BufferTooSmall;
    }
    if (headerSize != 0) {
        std::memcpy(output, kCsvHeader, headerSize);
    }
    bytesWritten = headerSize;
    return EventLoggerStatus::Ok;
}

}  // namespace project::diagnostics
