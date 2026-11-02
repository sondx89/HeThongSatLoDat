#include "Models/Serialization.h"

#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace project::models {
namespace {

constexpr std::uint32_t kEventRecordSchemaVersion = 2U;
constexpr char kEventRecordType[] = "event_record";

class JsonWriter {
public:
    JsonWriter(std::uint8_t* output, std::size_t capacity,
               bool measuring) noexcept
        : output_(output), capacity_(capacity), measuring_(measuring) {}

    SerializationStatus append(const char* bytes, std::size_t length) noexcept {
        if (status_ != SerializationStatus::Ok) {
            return status_;
        }
        if (length > std::numeric_limits<std::size_t>::max() - size_) {
            status_ = SerializationStatus::BufferTooSmall;
            return status_;
        }
        if (!measuring_) {
            if (length > capacity_ - size_) {
                status_ = SerializationStatus::BufferTooSmall;
                return status_;
            }
            if (length != 0U) {
                std::memcpy(output_ + size_, bytes, length);
            }
        }
        size_ += length;
        return status_;
    }

    SerializationStatus character(char value) noexcept {
        return append(&value, 1U);
    }

    std::size_t size() const noexcept { return size_; }
    SerializationStatus status() const noexcept { return status_; }

private:
    std::uint8_t* output_;
    std::size_t capacity_;
    std::size_t size_{0U};
    bool measuring_;
    SerializationStatus status_{SerializationStatus::Ok};
};

bool validUtf8(const char* text, std::size_t length) noexcept {
    std::size_t index = 0U;
    while (index < length) {
        const auto first = static_cast<std::uint8_t>(text[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        std::uint32_t codePoint = 0U;
        std::size_t continuationCount = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            codePoint = first & 0x1FU;
            continuationCount = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            codePoint = first & 0x0FU;
            continuationCount = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            codePoint = first & 0x07U;
            continuationCount = 3U;
        } else {
            return false;
        }
        if (continuationCount > length - index - 1U) {
            return false;
        }
        for (std::size_t offset = 1U; offset <= continuationCount; ++offset) {
            const auto next =
                static_cast<std::uint8_t>(text[index + offset]);
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }
        if ((continuationCount == 2U && codePoint < 0x800U) ||
            (continuationCount == 3U && codePoint < 0x10000U) ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU) ||
            codePoint > 0x10FFFFU) {
            return false;
        }
        index += continuationCount + 1U;
    }
    return true;
}

bool boundedStringLength(const char* text, std::size_t capacity,
                         std::size_t& length) noexcept {
    if (text == nullptr) {
        return false;
    }
    length = 0U;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length < capacity && validUtf8(text, length);
}

SerializationStatus writeJsonString(JsonWriter& writer, const char* text,
                                    std::size_t length) noexcept {
    static constexpr char kHex[] = "0123456789abcdef";
    if (!validUtf8(text, length)) {
        return SerializationStatus::InvalidArgument;
    }
    writer.character('"');
    for (std::size_t index = 0U; index < length; ++index) {
        const auto value = static_cast<std::uint8_t>(text[index]);
        switch (value) {
        case '"':
            writer.append("\\\"", 2U);
            break;
        case '\\':
            writer.append("\\\\", 2U);
            break;
        case '\b':
            writer.append("\\b", 2U);
            break;
        case '\f':
            writer.append("\\f", 2U);
            break;
        case '\n':
            writer.append("\\n", 2U);
            break;
        case '\r':
            writer.append("\\r", 2U);
            break;
        case '\t':
            writer.append("\\t", 2U);
            break;
        default:
            if (value < 0x20U) {
                const char escape[] = {'\\', 'u', '0', '0',
                                       kHex[value >> 4U],
                                       kHex[value & 0x0FU]};
                writer.append(escape, sizeof(escape));
            } else {
                writer.character(static_cast<char>(value));
            }
            break;
        }
        if (writer.status() != SerializationStatus::Ok) {
            return writer.status();
        }
    }
    writer.character('"');
    return writer.status();
}

SerializationStatus writeBoundedString(JsonWriter& writer, const char* key,
                                       const char* value,
                                       std::size_t capacity) noexcept {
    writer.character(',');
    writeJsonString(writer, key, std::strlen(key));
    writer.character(':');
    std::size_t length = 0U;
    if (!boundedStringLength(value, capacity, length)) {
        return SerializationStatus::InvalidArgument;
    }
    return writeJsonString(writer, value, length);
}

SerializationStatus writeKey(JsonWriter& writer, const char* key,
                             bool first = false) noexcept {
    if (!first) {
        writer.character(',');
    }
    writeJsonString(writer, key, std::strlen(key));
    return writer.character(':');
}

SerializationStatus writeUnsigned(JsonWriter& writer, std::uint64_t value,
                                  const char* key,
                                  bool first = false) noexcept {
    char digits[20];
    std::size_t count = 0U;
    do {
        digits[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    writeKey(writer, key, first);
    while (count != 0U) {
        writer.character(digits[--count]);
    }
    return writer.status();
}

SerializationStatus writeBoolean(JsonWriter& writer, bool value,
                                 const char* key,
                                 bool first = false) noexcept {
    writeKey(writer, key, first);
    return writer.append(value ? "true" : "false", value ? 4U : 5U);
}

SerializationStatus writeFloat(JsonWriter& writer, float value,
                               const char* key) noexcept {
    writeKey(writer, key);
    if (!std::isfinite(value)) {
        return writer.append("null", 4U);
    }

    char formatted[48];
    const int formattedLength =
        std::snprintf(formatted, sizeof(formatted), "%.9g",
                      static_cast<double>(value));
    if (formattedLength <= 0 ||
        static_cast<std::size_t>(formattedLength) >= sizeof(formatted)) {
        return SerializationStatus::InvalidArgument;
    }

    const std::lconv* locale = std::localeconv();
    const char* decimalPoint =
        (locale == nullptr || locale->decimal_point == nullptr)
            ? "."
            : locale->decimal_point;
    const std::size_t decimalLength = std::strlen(decimalPoint);
    if (decimalLength == 0U) {
        return SerializationStatus::InvalidArgument;
    }

    std::size_t index = 0U;
    const std::size_t length = static_cast<std::size_t>(formattedLength);
    while (index < length) {
        if (decimalLength <= length - index &&
            std::memcmp(formatted + index, decimalPoint, decimalLength) == 0) {
            writer.character('.');
            index += decimalLength;
        } else {
            writer.character(formatted[index++]);
        }
    }
    return writer.status();
}

SerializationStatus writeEnum(JsonWriter& writer, const char* key,
                              const char* token) noexcept {
    writeKey(writer, key);
    return writeJsonString(writer, token, std::strlen(token));
}

const char* eventSeverityToken(EventSeverity value) noexcept {
    switch (value) {
    case EventSeverity::Debug:
        return "DEBUG";
    case EventSeverity::Info:
        return "INFO";
    case EventSeverity::Notice:
        return "NOTICE";
    case EventSeverity::Warn:
        return "WARNING";
    case EventSeverity::Error:
        return "ERROR";
    case EventSeverity::Critical:
        return "CRITICAL";
    }
    return nullptr;
}

const char* riskStateToken(RiskState value) noexcept {
    switch (value) {
    case RiskState::Startup:
        return "STARTUP";
    case RiskState::Normal:
        return "NORMAL";
    case RiskState::Monitor:
        return "MONITOR";
    case RiskState::DangerTest:
        return "DANGER_TEST";
    case RiskState::Unknown:
        return "UNKNOWN";
    }
    return nullptr;
}

const char* healthStateToken(HealthState value) noexcept {
    switch (value) {
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

const char* valueTypeToken(EventRecord::ValueType value) noexcept {
    switch (value) {
    case EventRecord::ValueType::None:
        return "NONE";
    case EventRecord::ValueType::Numeric:
        return "NUMERIC";
    case EventRecord::ValueType::Text:
        return "TEXT";
    }
    return nullptr;
}

const char* sensorIdToken(SensorId value) noexcept {
    switch (value) {
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

bool isValidEventRecord(const EventRecord& event) noexcept {
    if (event.schemaVersion != kEventRecordSchemaVersion ||
        (event.valueValid && !std::isfinite(event.value)) ||
        eventSeverityToken(event.severity) == nullptr ||
        riskStateToken(event.riskState) == nullptr ||
        healthStateToken(event.healthState) == nullptr ||
        valueTypeToken(event.valueType) == nullptr ||
        sensorIdToken(event.sensorId) == nullptr) {
        return false;
    }

    const char* const strings[] = {
        event.eventName,      event.eventCatalogId, event.source,
        event.statusCode,     event.errorCode,      event.faultCode,
        event.manifestId,     event.firmwareVersion, event.modelVersion,
        event.configVersion,  event.calibrationVersion, event.message,
        event.valueText,      event.unit,           event.stateBefore,
        event.stateAfter};
    const std::size_t capacities[] = {
        event_capacity::kEventNameBytes,
        event_capacity::kCodeBytes,
        event_capacity::kSourceBytes,
        event_capacity::kCodeBytes,
        event_capacity::kCodeBytes,
        event_capacity::kCodeBytes,
        event_capacity::kIdentityBytes,
        event_capacity::kVersionBytes,
        event_capacity::kVersionBytes,
        event_capacity::kVersionBytes,
        event_capacity::kVersionBytes,
        event_capacity::kMessageBytes,
        event_capacity::kMessageBytes,
        event_capacity::kUnitBytes,
        event_capacity::kStateNameBytes,
        event_capacity::kStateNameBytes,
    };
    for (std::size_t index = 0U;
         index < sizeof(strings) / sizeof(strings[0]); ++index) {
        std::size_t length = 0U;
        if (!boundedStringLength(strings[index], capacities[index], length)) {
            return false;
        }
    }
    return true;
}

SerializationStatus writeHash(JsonWriter& writer, const char* key,
                              const std::uint8_t hash[32]) noexcept {
    static constexpr char kHex[] = "0123456789abcdef";
    writeKey(writer, key);
    writer.character('"');
    for (std::size_t index = 0U; index < 32U; ++index) {
        writer.character(kHex[hash[index] >> 4U]);
        writer.character(kHex[hash[index] & 0x0FU]);
    }
    writer.character('"');
    return writer.status();
}

SerializationStatus writeEventRecord(JsonWriter& writer,
                                    const EventRecord& event) noexcept {
    writer.character('{');
    writeKey(writer, "record_type", true);
    writeJsonString(writer, kEventRecordType, sizeof(kEventRecordType) - 1U);
    writeUnsigned(writer, event.schemaVersion, "schema_version");
    writeUnsigned(writer, event.eventId, "event_id");
    writeUnsigned(writer, event.eventSequenceNo, "event_sequence_no");
    writeUnsigned(writer, event.bootId, "boot_id");
    writeUnsigned(writer, event.timestampMs, "timestamp_ms");
    writeBoundedString(writer, "event_name", event.eventName,
                       event_capacity::kEventNameBytes);
    writeBoundedString(writer, "event_catalog_id", event.eventCatalogId,
                       event_capacity::kCodeBytes);
    writeEnum(writer, "severity", eventSeverityToken(event.severity));
    writeBoundedString(writer, "source", event.source,
                       event_capacity::kSourceBytes);
    writeBoundedString(writer, "status_code", event.statusCode,
                       event_capacity::kCodeBytes);
    writeBoundedString(writer, "error_code", event.errorCode,
                       event_capacity::kCodeBytes);
    writeBoolean(writer, event.errorCodeValid, "error_code_valid");
    writeBoundedString(writer, "fault_code", event.faultCode,
                       event_capacity::kCodeBytes);
    writeBoolean(writer, event.faultCodeValid, "fault_code_valid");
    writeEnum(writer, "risk_state", riskStateToken(event.riskState));
    writeEnum(writer, "health_state", healthStateToken(event.healthState));
    writeBoundedString(writer, "manifest_id", event.manifestId,
                       event_capacity::kIdentityBytes);
    writeBoolean(writer, event.manifestIdValid, "manifest_id_valid");
    writeBoundedString(writer, "firmware_version", event.firmwareVersion,
                       event_capacity::kVersionBytes);
    writeBoundedString(writer, "model_version", event.modelVersion,
                       event_capacity::kVersionBytes);
    writeBoolean(writer, event.modelVersionValid, "model_version_valid");
    writeHash(writer, "feature_schema_hash_sha256",
              event.featureSchemaHashSha256);
    writeBoolean(writer, event.featureSchemaHashValid,
                 "feature_schema_hash_valid");
    writeBoundedString(writer, "config_version", event.configVersion,
                       event_capacity::kVersionBytes);
    writeBoolean(writer, event.configVersionValid, "config_version_valid");
    writeBoundedString(writer, "calibration_version",
                       event.calibrationVersion,
                       event_capacity::kVersionBytes);
    writeBoolean(writer, event.calibrationVersionValid,
                 "calibration_version_valid");
    writeUnsigned(writer, event.relatedSequenceNo, "related_sequence_no");
    writeBoolean(writer, event.relatedSequenceNoValid,
                 "related_sequence_no_valid");
    writeBoundedString(writer, "message", event.message,
                       event_capacity::kMessageBytes);
    writeEnum(writer, "value_type", valueTypeToken(event.valueType));
    writeFloat(writer, event.value, "value");
    writeBoundedString(writer, "value_text", event.valueText,
                       event_capacity::kMessageBytes);
    writeBoundedString(writer, "unit", event.unit, event_capacity::kUnitBytes);
    writeBoolean(writer, event.valueValid, "value_valid");
    writeUnsigned(writer, event.droppedEventCount, "dropped_event_count");
    writeUnsigned(writer, event.crc32, "crc32");
    writeBoolean(writer, event.crc32Valid, "crc32_valid");
    writeBoolean(writer, event.valid, "valid");
    writeEnum(writer, "sensor_id", sensorIdToken(event.sensorId));
    writeBoolean(writer, event.sensorIdValid, "sensor_id_valid");
    writeBoundedString(writer, "state_before", event.stateBefore,
                       event_capacity::kStateNameBytes);
    writeBoolean(writer, event.stateBeforeValid, "state_before_valid");
    writeBoundedString(writer, "state_after", event.stateAfter,
                       event_capacity::kStateNameBytes);
    writeBoolean(writer, event.stateAfterValid, "state_after_valid");
    writer.character('}');
    writer.character('\n');
    return writer.status();
}

class JsonParser {
public:
    JsonParser(const std::uint8_t* input, std::size_t size) noexcept
        : input_(input), size_(size) {}

    bool beginObject() noexcept {
        skipWhitespace();
        return consume('{');
    }

    bool key(const char* expected, bool first = false) noexcept {
        if (first) {
            skipWhitespace();
        } else {
            skipWhitespace();
            if (!consume(',')) {
                return false;
            }
            skipWhitespace();
        }
        char actual[64];
        std::size_t length = 0U;
        if (!readString(actual, sizeof(actual), length) ||
            std::strcmp(actual, expected) != 0) {
            return false;
        }
        skipWhitespace();
        return consume(':');
    }

    bool string(char* output, std::size_t capacity) noexcept {
        std::size_t length = 0U;
        return readString(output, capacity, length);
    }

    bool boolean(bool& output) noexcept {
        skipWhitespace();
        if (consumeLiteral("true")) {
            output = true;
            return true;
        }
        if (consumeLiteral("false")) {
            output = false;
            return true;
        }
        return false;
    }

    template <typename T>
    bool unsignedInteger(T& output) noexcept {
        skipWhitespace();
        if (position_ >= size_ || input_[position_] < '0' ||
            input_[position_] > '9') {
            return false;
        }
        const std::size_t start = position_;
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < size_ && input_[position_] >= '0' &&
                input_[position_] <= '9') {
                return false;
            }
        } else {
            while (position_ < size_ && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
        }
        std::uint64_t value = 0U;
        for (std::size_t index = start; index < position_; ++index) {
            const std::uint64_t digit =
                static_cast<std::uint64_t>(input_[index] - '0');
            if (value > (std::numeric_limits<T>::max() - digit) / 10U) {
                return false;
            }
            value = value * 10U + digit;
        }
        output = static_cast<T>(value);
        return true;
    }

    bool floating(float& output, bool& isNull) noexcept {
        skipWhitespace();
        if (consumeLiteral("null")) {
            output = std::numeric_limits<float>::quiet_NaN();
            isNull = true;
            return true;
        }
        isNull = false;
        char token[64];
        std::size_t length = 0U;
        while (position_ < size_ && !isJsonDelimiter(input_[position_])) {
            if (length + 1U >= sizeof(token)) {
                return false;
            }
            token[length++] = static_cast<char>(input_[position_++]);
        }
        token[length] = '\0';
        if (!validJsonNumber(token, length)) {
            return false;
        }

        char localized[128];
        const std::lconv* locale = std::localeconv();
        const char* decimalPoint =
            (locale == nullptr || locale->decimal_point == nullptr)
                ? "."
                : locale->decimal_point;
        const std::size_t decimalLength = std::strlen(decimalPoint);
        if (decimalLength == 0U) {
            return false;
        }
        std::size_t source = 0U;
        std::size_t destination = 0U;
        while (source < length) {
            if (token[source] == '.') {
                if (decimalLength >= sizeof(localized) - destination) {
                    return false;
                }
                std::memcpy(localized + destination, decimalPoint,
                            decimalLength);
                destination += decimalLength;
                ++source;
            } else {
                if (destination + 1U >= sizeof(localized)) {
                    return false;
                }
                localized[destination++] = token[source++];
            }
        }
        localized[destination] = '\0';
        char* end = nullptr;
        errno = 0;
        const float parsed = std::strtof(localized, &end);
        if (end != localized + destination || !std::isfinite(parsed)) {
            return false;
        }
        output = parsed;
        return true;
    }

    bool hash(std::uint8_t output[32]) noexcept {
        char text[65];
        if (!string(text, sizeof(text)) || std::strlen(text) != 64U) {
            return false;
        }
        for (std::size_t index = 0U; index < 32U; ++index) {
            const int high = lowerHexValue(text[index * 2U]);
            const int low = lowerHexValue(text[index * 2U + 1U]);
            if (high < 0 || low < 0) {
                return false;
            }
            output[index] =
                static_cast<std::uint8_t>((high << 4) | low);
        }
        return true;
    }

    bool endObject() noexcept {
        skipWhitespace();
        return consume('}');
    }

    bool endLine() noexcept {
        while (position_ < size_ &&
               (input_[position_] == ' ' || input_[position_] == '\t')) {
            ++position_;
        }
        if (position_ == size_) {
            return true;
        }
        if (input_[position_] == '\r') {
            ++position_;
            if (position_ >= size_ || input_[position_] != '\n') {
                return false;
            }
            ++position_;
        } else if (input_[position_] == '\n') {
            ++position_;
        } else {
            return false;
        }
        return position_ == size_;
    }

private:
    static bool isJsonDelimiter(std::uint8_t value) noexcept {
        return value == ',' || value == '}' || value == ' ' || value == '\t' ||
               value == '\r' || value == '\n';
    }

    static bool validJsonNumber(const char* text,
                                std::size_t length) noexcept {
        std::size_t index = 0U;
        if (index < length && text[index] == '-') {
            ++index;
        }
        if (index >= length) {
            return false;
        }
        if (text[index] == '0') {
            ++index;
            if (index < length && text[index] >= '0' && text[index] <= '9') {
                return false;
            }
        } else if (text[index] >= '1' && text[index] <= '9') {
            while (index < length && text[index] >= '0' && text[index] <= '9') {
                ++index;
            }
        } else {
            return false;
        }
        if (index < length && text[index] == '.') {
            ++index;
            const std::size_t fractionStart = index;
            while (index < length && text[index] >= '0' &&
                   text[index] <= '9') {
                ++index;
            }
            if (index == fractionStart) {
                return false;
            }
        }
        if (index < length && (text[index] == 'e' || text[index] == 'E')) {
            ++index;
            if (index < length && (text[index] == '+' || text[index] == '-')) {
                ++index;
            }
            const std::size_t exponentStart = index;
            while (index < length && text[index] >= '0' &&
                   text[index] <= '9') {
                ++index;
            }
            if (index == exponentStart) {
                return false;
            }
        }
        return index == length;
    }

    static int lowerHexValue(char value) noexcept {
        if (value >= '0' && value <= '9') {
            return value - '0';
        }
        if (value >= 'a' && value <= 'f') {
            return value - 'a' + 10;
        }
        return -1;
    }

    void skipWhitespace() noexcept {
        while (position_ < size_ &&
               (input_[position_] == ' ' || input_[position_] == '\t')) {
            ++position_;
        }
    }

    bool consume(char value) noexcept {
        if (position_ >= size_ || input_[position_] !=
                                      static_cast<std::uint8_t>(value)) {
            return false;
        }
        ++position_;
        return true;
    }

    bool consumeLiteral(const char* literal) noexcept {
        const std::size_t length = std::strlen(literal);
        if (length > size_ - position_ ||
            std::memcmp(input_ + position_, literal, length) != 0) {
            return false;
        }
        position_ += length;
        return true;
    }

    bool appendCodePoint(char* output, std::size_t capacity,
                         std::size_t& length,
                         std::uint32_t codePoint) noexcept {
        if (codePoint == 0U) {
            return false;
        }
        char bytes[4];
        std::size_t count = 0U;
        if (codePoint <= 0x7FU) {
            bytes[count++] = static_cast<char>(codePoint);
        } else if (codePoint <= 0x7FFU) {
            bytes[count++] =
                static_cast<char>(0xC0U | (codePoint >> 6U));
            bytes[count++] =
                static_cast<char>(0x80U | (codePoint & 0x3FU));
        } else if (codePoint <= 0xFFFFU) {
            bytes[count++] =
                static_cast<char>(0xE0U | (codePoint >> 12U));
            bytes[count++] =
                static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
            bytes[count++] =
                static_cast<char>(0x80U | (codePoint & 0x3FU));
        } else if (codePoint <= 0x10FFFFU) {
            bytes[count++] =
                static_cast<char>(0xF0U | (codePoint >> 18U));
            bytes[count++] =
                static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU));
            bytes[count++] =
                static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
            bytes[count++] =
                static_cast<char>(0x80U | (codePoint & 0x3FU));
        } else {
            return false;
        }
        if (count >= capacity - length) {
            return false;
        }
        std::memcpy(output + length, bytes, count);
        length += count;
        return true;
    }

    bool hexQuad(std::uint32_t& value) noexcept {
        if (size_ - position_ < 4U) {
            return false;
        }
        value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const std::uint8_t digit = input_[position_++];
            value <<= 4U;
            if (digit >= '0' && digit <= '9') {
                value |= digit - '0';
            } else if (digit >= 'a' && digit <= 'f') {
                value |= digit - 'a' + 10U;
            } else if (digit >= 'A' && digit <= 'F') {
                value |= digit - 'A' + 10U;
            } else {
                return false;
            }
        }
        return true;
    }

    bool readString(char* output, std::size_t capacity,
                    std::size_t& length) noexcept {
        if (capacity == 0U) {
            return false;
        }
        skipWhitespace();
        if (!consume('"')) {
            return false;
        }
        length = 0U;
        while (position_ < size_) {
            const std::uint8_t value = input_[position_++];
            if (value == '"') {
                output[length] = '\0';
                return validUtf8(output, length);
            }
            if (value < 0x20U) {
                return false;
            }
            if (value == '\\') {
                if (position_ >= size_) {
                    return false;
                }
                const std::uint8_t escaped = input_[position_++];
                char plain = '\0';
                switch (escaped) {
                case '"':
                    plain = '"';
                    break;
                case '\\':
                    plain = '\\';
                    break;
                case '/':
                    plain = '/';
                    break;
                case 'b':
                    plain = '\b';
                    break;
                case 'f':
                    plain = '\f';
                    break;
                case 'n':
                    plain = '\n';
                    break;
                case 'r':
                    plain = '\r';
                    break;
                case 't':
                    plain = '\t';
                    break;
                case 'u': {
                    std::uint32_t codePoint = 0U;
                    if (!hexQuad(codePoint)) {
                        return false;
                    }
                    if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
                        if (size_ - position_ < 6U ||
                            input_[position_] != '\\' ||
                            input_[position_ + 1U] != 'u') {
                            return false;
                        }
                        position_ += 2U;
                        std::uint32_t low = 0U;
                        if (!hexQuad(low) || low < 0xDC00U ||
                            low > 0xDFFFU) {
                            return false;
                        }
                        codePoint = 0x10000U +
                                    ((codePoint - 0xD800U) << 10U) +
                                    (low - 0xDC00U);
                    } else if (codePoint >= 0xDC00U &&
                               codePoint <= 0xDFFFU) {
                        return false;
                    }
                    if (!appendCodePoint(output, capacity, length, codePoint)) {
                        return false;
                    }
                    continue;
                }
                default:
                    return false;
                }
                if (length + 1U >= capacity) {
                    return false;
                }
                output[length++] = plain;
            } else {
                if (length + 1U >= capacity) {
                    return false;
                }
                output[length++] = static_cast<char>(value);
            }
        }
        return false;
    }

    const std::uint8_t* input_;
    std::size_t size_;
    std::size_t position_{0U};
};

bool readEnum(JsonParser& parser, EventSeverity& value) noexcept {
    char token[16];
    if (!parser.string(token, sizeof(token))) {
        return false;
    }
    if (std::strcmp(token, "DEBUG") == 0) {
        value = EventSeverity::Debug;
    } else if (std::strcmp(token, "INFO") == 0) {
        value = EventSeverity::Info;
    } else if (std::strcmp(token, "NOTICE") == 0) {
        value = EventSeverity::Notice;
    } else if (std::strcmp(token, "WARNING") == 0) {
        value = EventSeverity::Warning;
    } else if (std::strcmp(token, "ERROR") == 0) {
        value = EventSeverity::Error;
    } else if (std::strcmp(token, "CRITICAL") == 0) {
        value = EventSeverity::Critical;
    } else {
        return false;
    }
    return true;
}

bool readEnum(JsonParser& parser, RiskState& value) noexcept {
    char token[16];
    if (!parser.string(token, sizeof(token))) {
        return false;
    }
    if (std::strcmp(token, "STARTUP") == 0) {
        value = RiskState::Startup;
    } else if (std::strcmp(token, "NORMAL") == 0) {
        value = RiskState::Normal;
    } else if (std::strcmp(token, "MONITOR") == 0) {
        value = RiskState::Monitor;
    } else if (std::strcmp(token, "DANGER_TEST") == 0) {
        value = RiskState::DangerTest;
    } else if (std::strcmp(token, "UNKNOWN") == 0) {
        value = RiskState::Unknown;
    } else {
        return false;
    }
    return true;
}

bool readEnum(JsonParser& parser, HealthState& value) noexcept {
    char token[16];
    if (!parser.string(token, sizeof(token))) {
        return false;
    }
    if (std::strcmp(token, "UNKNOWN") == 0) {
        value = HealthState::Unknown;
    } else if (std::strcmp(token, "HEALTHY") == 0) {
        value = HealthState::Healthy;
    } else if (std::strcmp(token, "DEGRADED") == 0) {
        value = HealthState::Degraded;
    } else if (std::strcmp(token, "FAULT") == 0) {
        value = HealthState::Fault;
    } else {
        return false;
    }
    return true;
}

bool readEnum(JsonParser& parser, EventRecord::ValueType& value) noexcept {
    char token[16];
    if (!parser.string(token, sizeof(token))) {
        return false;
    }
    if (std::strcmp(token, "NONE") == 0) {
        value = EventRecord::ValueType::None;
    } else if (std::strcmp(token, "NUMERIC") == 0) {
        value = EventRecord::ValueType::Numeric;
    } else if (std::strcmp(token, "TEXT") == 0) {
        value = EventRecord::ValueType::Text;
    } else {
        return false;
    }
    return true;
}

bool readEnum(JsonParser& parser, SensorId& value) noexcept {
    char token[20];
    if (!parser.string(token, sizeof(token))) {
        return false;
    }
    if (std::strcmp(token, "UNKNOWN") == 0) {
        value = SensorId::Unknown;
    } else if (std::strcmp(token, "IMU") == 0) {
        value = SensorId::Imu;
    } else if (std::strcmp(token, "SOIL_MOISTURE") == 0) {
        value = SensorId::SoilMoisture;
    } else if (std::strcmp(token, "RAIN") == 0) {
        value = SensorId::Rain;
    } else if (std::strcmp(token, "HX710B") == 0) {
        value = SensorId::Hx710b;
    } else {
        return false;
    }
    return true;
}

SerializationStatus readEventRecord(JsonParser& parser,
                                    EventRecord& event) noexcept {
    char recordType[32];
    if (!parser.beginObject() || !parser.key("record_type", true) ||
        !parser.string(recordType, sizeof(recordType))) {
        return SerializationStatus::InvalidArgument;
    }
    if (std::strcmp(recordType, kEventRecordType) != 0) {
        return SerializationStatus::InvalidArgument;
    }
    if (!parser.key("schema_version") ||
        !parser.unsignedInteger(event.schemaVersion)) {
        return SerializationStatus::InvalidArgument;
    }
    if (event.schemaVersion != kEventRecordSchemaVersion) {
        return SerializationStatus::UnsupportedVersion;
    }

#define READ_KEY(name) \
    if (!parser.key(name)) return SerializationStatus::InvalidArgument
#define READ_UINT(name, field)        \
    READ_KEY(name);                   \
    if (!parser.unsignedInteger(field)) \
        return SerializationStatus::InvalidArgument
#define READ_BOOL(name, field)        \
    READ_KEY(name);                   \
    if (!parser.boolean(field))       \
        return SerializationStatus::InvalidArgument
#define READ_STRING(name, field)                              \
    READ_KEY(name);                                           \
    if (!parser.string(field, sizeof(field)))                 \
        return SerializationStatus::InvalidArgument

    READ_UINT("event_id", event.eventId);
    READ_UINT("event_sequence_no", event.eventSequenceNo);
    READ_UINT("boot_id", event.bootId);
    READ_UINT("timestamp_ms", event.timestampMs);
    READ_STRING("event_name", event.eventName);
    READ_STRING("event_catalog_id", event.eventCatalogId);
    READ_KEY("severity");
    if (!readEnum(parser, event.severity)) {
        return SerializationStatus::InvalidArgument;
    }
    READ_STRING("source", event.source);
    READ_STRING("status_code", event.statusCode);
    READ_STRING("error_code", event.errorCode);
    READ_BOOL("error_code_valid", event.errorCodeValid);
    READ_STRING("fault_code", event.faultCode);
    READ_BOOL("fault_code_valid", event.faultCodeValid);
    READ_KEY("risk_state");
    if (!readEnum(parser, event.riskState)) {
        return SerializationStatus::InvalidArgument;
    }
    READ_KEY("health_state");
    if (!readEnum(parser, event.healthState)) {
        return SerializationStatus::InvalidArgument;
    }
    READ_STRING("manifest_id", event.manifestId);
    READ_BOOL("manifest_id_valid", event.manifestIdValid);
    READ_STRING("firmware_version", event.firmwareVersion);
    READ_STRING("model_version", event.modelVersion);
    READ_BOOL("model_version_valid", event.modelVersionValid);
    READ_KEY("feature_schema_hash_sha256");
    if (!parser.hash(event.featureSchemaHashSha256)) {
        return SerializationStatus::InvalidArgument;
    }
    READ_BOOL("feature_schema_hash_valid", event.featureSchemaHashValid);
    READ_STRING("config_version", event.configVersion);
    READ_BOOL("config_version_valid", event.configVersionValid);
    READ_STRING("calibration_version", event.calibrationVersion);
    READ_BOOL("calibration_version_valid", event.calibrationVersionValid);
    READ_UINT("related_sequence_no", event.relatedSequenceNo);
    READ_BOOL("related_sequence_no_valid", event.relatedSequenceNoValid);
    READ_STRING("message", event.message);
    READ_KEY("value_type");
    if (!readEnum(parser, event.valueType)) {
        return SerializationStatus::InvalidArgument;
    }
    bool valueIsNull = false;
    READ_KEY("value");
    if (!parser.floating(event.value, valueIsNull)) {
        return SerializationStatus::InvalidArgument;
    }
    READ_STRING("value_text", event.valueText);
    READ_STRING("unit", event.unit);
    READ_BOOL("value_valid", event.valueValid);
    if (valueIsNull && event.valueValid) {
        return SerializationStatus::InvalidArgument;
    }
    READ_UINT("dropped_event_count", event.droppedEventCount);
    READ_UINT("crc32", event.crc32);
    READ_BOOL("crc32_valid", event.crc32Valid);
    READ_BOOL("valid", event.valid);
    READ_KEY("sensor_id");
    if (!readEnum(parser, event.sensorId)) {
        return SerializationStatus::InvalidArgument;
    }
    READ_BOOL("sensor_id_valid", event.sensorIdValid);
    READ_STRING("state_before", event.stateBefore);
    READ_BOOL("state_before_valid", event.stateBeforeValid);
    READ_STRING("state_after", event.stateAfter);
    READ_BOOL("state_after_valid", event.stateAfterValid);

#undef READ_STRING
#undef READ_BOOL
#undef READ_UINT
#undef READ_KEY

    if (!parser.endObject() || !parser.endLine() ||
        !isValidEventRecord(event)) {
        return SerializationStatus::InvalidArgument;
    }
    return SerializationStatus::Ok;
}

SerializationStatus unsupportedSerialize(std::uint8_t* output,
                                         std::size_t capacity,
                                         std::size_t& bytesWritten) noexcept {
    return serialization_detail::notImplemented(output, capacity,
                                                bytesWritten);
}

SerializationStatus unsupportedDeserialize(const std::uint8_t* input,
                                           std::size_t size) noexcept {
    return serialization_detail::notImplemented(input, size);
}

}  // namespace

SerializationStatus serializeSensorFrame(
    const SensorFrame&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept {
    return unsupportedSerialize(output, capacity, bytesWritten);
}

SerializationStatus deserializeSensorFrame(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    SensorFrame&) noexcept {
    return unsupportedDeserialize(input, size);
}

SerializationStatus serializeProcessedFrame(
    const ProcessedFrame&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept {
    return unsupportedSerialize(output, capacity, bytesWritten);
}

SerializationStatus deserializeProcessedFrame(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    ProcessedFrame&) noexcept {
    return unsupportedDeserialize(input, size);
}

SerializationStatus serializeFeatureVector(
    const FeatureVector&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept {
    return unsupportedSerialize(output, capacity, bytesWritten);
}

SerializationStatus deserializeFeatureVector(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    FeatureVector&) noexcept {
    return unsupportedDeserialize(input, size);
}

SerializationStatus serializeModelOutput(
    const ModelOutput&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept {
    return unsupportedSerialize(output, capacity, bytesWritten);
}

SerializationStatus deserializeModelOutput(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    ModelOutput&) noexcept {
    return unsupportedDeserialize(input, size);
}

SerializationStatus serializeRiskAssessment(
    const RiskAssessment&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept {
    return unsupportedSerialize(output, capacity, bytesWritten);
}

SerializationStatus deserializeRiskAssessment(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    RiskAssessment&) noexcept {
    return unsupportedDeserialize(input, size);
}

SerializationStatus serializeEventRecord(
    const EventRecord& event, SerializationFormat format,
    std::uint8_t* output, std::size_t capacity,
    std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    if (format != SerializationFormat::JsonLines) {
        return format == SerializationFormat::Csv ||
                       format == SerializationFormat::Binary
                   ? SerializationStatus::NotImplemented
                   : SerializationStatus::InvalidArgument;
    }
    if (output == nullptr && capacity != 0U) {
        return SerializationStatus::InvalidArgument;
    }
    if (event.schemaVersion != kEventRecordSchemaVersion) {
        return SerializationStatus::UnsupportedVersion;
    }
    if (!isValidEventRecord(event)) {
        return SerializationStatus::InvalidArgument;
    }

    JsonWriter counter(nullptr, std::numeric_limits<std::size_t>::max(), true);
    const SerializationStatus measureStatus =
        writeEventRecord(counter, event);
    if (measureStatus != SerializationStatus::Ok) {
        return measureStatus;
    }
    if (counter.size() > capacity) {
        return SerializationStatus::BufferTooSmall;
    }

    JsonWriter writer(output, capacity, false);
    const SerializationStatus writeStatus = writeEventRecord(writer, event);
    if (writeStatus != SerializationStatus::Ok) {
        return writeStatus;
    }
    bytesWritten = writer.size();
    return SerializationStatus::Ok;
}

SerializationStatus deserializeEventRecord(
    SerializationFormat format, const std::uint8_t* input, std::size_t size,
    EventRecord& output) noexcept {
    if (format != SerializationFormat::JsonLines) {
        return format == SerializationFormat::Csv ||
                       format == SerializationFormat::Binary
                   ? SerializationStatus::NotImplemented
                   : SerializationStatus::InvalidArgument;
    }
    if (input == nullptr || size == 0U) {
        return SerializationStatus::InvalidArgument;
    }

    EventRecord parsed{};
    JsonParser parser(input, size);
    const SerializationStatus status = readEventRecord(parser, parsed);
    if (status != SerializationStatus::Ok) {
        return status;
    }
    output = parsed;
    return SerializationStatus::Ok;
}

SerializationStatus serializeHealthSnapshot(
    const HealthSnapshot&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept {
    return unsupportedSerialize(output, capacity, bytesWritten);
}

SerializationStatus deserializeHealthSnapshot(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    HealthSnapshot&) noexcept {
    return unsupportedDeserialize(input, size);
}

}  // namespace project::models
