#include "project/Diagnostics/SerialDiagnostics.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace project::diagnostics {
namespace {

class BoundedWriter final {
public:
    BoundedWriter(char* output, std::size_t capacity) noexcept
        : output_(output), capacity_(capacity) {
        if (output_ != nullptr && capacity_ > 0U) {
            output_[0] = '\0';
        }
    }

    bool append(const char* format, ...) noexcept {
        if (failed_ || output_ == nullptr || format == nullptr ||
            length_ >= capacity_) {
            failed_ = true;
            return false;
        }
        va_list args;
        va_start(args, format);
        const int written =
            std::vsnprintf(output_ + length_, capacity_ - length_, format,
                           args);
        va_end(args);
        if (written < 0 ||
            static_cast<std::size_t>(written) >= capacity_ - length_) {
            failed_ = true;
            return false;
        }
        length_ += static_cast<std::size_t>(written);
        return true;
    }

    std::size_t length() const noexcept { return length_; }
    bool good() const noexcept { return !failed_; }

private:
    char* output_{nullptr};
    std::size_t capacity_{0U};
    std::size_t length_{0U};
    bool failed_{false};
};

const char* healthName(project::models::HealthState state) noexcept {
    switch (state) {
        case project::models::HealthState::Unknown:
            return "unknown";
        case project::models::HealthState::Healthy:
            return "healthy";
        case project::models::HealthState::Degraded:
            return "degraded";
        case project::models::HealthState::Fault:
            return "fault";
    }
    return "unknown";
}

const char* sensorHealthName(project::models::SensorHealth state) noexcept {
    switch (state) {
        case project::models::SensorHealth::Unknown:
            return "unknown";
        case project::models::SensorHealth::Healthy:
            return "healthy";
        case project::models::SensorHealth::Degraded:
            return "degraded";
        case project::models::SensorHealth::Fault:
            return "fault";
    }
    return "unknown";
}

const char* riskName(project::models::RiskState state) noexcept {
    switch (state) {
        case project::models::RiskState::Startup:
            return "startup";
        case project::models::RiskState::Normal:
            return "normal";
        case project::models::RiskState::Monitor:
            return "monitor";
        case project::models::RiskState::DangerTest:
            return "high_risk_test";
        case project::models::RiskState::Unknown:
            return "unknown";
    }
    return "unknown";
}

const char* inferenceStatusName(
    project::models::InferenceStatus status) noexcept {
    switch (status) {
        case project::models::InferenceStatus::Ok:
            return "ok";
        case project::models::InferenceStatus::InvalidInput:
            return "invalid_input";
        case project::models::InferenceStatus::ModelError:
            return "model_error";
        case project::models::InferenceStatus::NonFiniteOutput:
            return "non_finite_output";
        case project::models::InferenceStatus::VersionMismatch:
            return "version_mismatch";
        case project::models::InferenceStatus::Timeout:
            return "timeout";
    }
    return "unknown";
}

const char* modeName(DiagnosticsRuntimeMode mode) noexcept {
    switch (mode) {
        case DiagnosticsRuntimeMode::Unknown:
            return "unknown";
        case DiagnosticsRuntimeMode::Diagnostic:
            return "diagnostic";
        case DiagnosticsRuntimeMode::Degraded:
            return "degraded";
        case DiagnosticsRuntimeMode::Operational:
            return "operational";
    }
    return "unknown";
}

std::uint64_t sensorAgeMs(std::uint64_t nowMs,
                          std::uint64_t sampleMs) noexcept {
    return sampleMs <= nowMs ? nowMs - sampleMs : 0U;
}

bool validSnapshot(const DiagnosticsSnapshot& snapshot) noexcept {
    return snapshot.health.sensorMetricCount <=
               project::models::HealthSnapshot::kMaxSensorMetrics &&
           snapshot.health.taskStackMetricCount <=
               project::models::HealthSnapshot::kMaxTaskMetrics &&
           snapshot.health.queueMetricCount <=
               project::models::HealthSnapshot::kMaxQueueMetrics &&
           snapshot.model.outputCount <=
               project::models::ModelOutput::kMaxOutputs;
}

}  // namespace

DiagnosticsParseStatus DiagnosticsCommandParser::parse(
    const char* line, std::size_t length,
    DiagnosticsCommand& output) noexcept {
    if (line == nullptr) {
        return DiagnosticsParseStatus::Malformed;
    }
    if (length > kMaximumCommandLength) {
        return DiagnosticsParseStatus::TooLong;
    }

    std::size_t start = 0U;
    while (start < length && (line[start] == ' ' || line[start] == '\t')) {
        ++start;
    }
    while (length > start &&
           (line[length - 1U] == ' ' || line[length - 1U] == '\t')) {
        --length;
    }
    if (start == length) {
        return DiagnosticsParseStatus::Empty;
    }

    const std::size_t firstStart = start;
    while (start < length && line[start] != ' ' && line[start] != '\t') {
        const unsigned char value =
            static_cast<unsigned char>(line[start]);
        if (value < 0x20U || value > 0x7EU) {
            return DiagnosticsParseStatus::Malformed;
        }
        ++start;
    }
    const std::size_t firstLength = start - firstStart;
    while (start < length && (line[start] == ' ' || line[start] == '\t')) {
        ++start;
    }
    if (start == length) {
        return DiagnosticsParseStatus::Malformed;
    }

    const std::size_t secondStart = start;
    while (start < length && line[start] != ' ' && line[start] != '\t') {
        const unsigned char value =
            static_cast<unsigned char>(line[start]);
        if (value < 0x20U || value > 0x7EU) {
            return DiagnosticsParseStatus::Malformed;
        }
        ++start;
    }
    const std::size_t secondLength = start - secondStart;
    while (start < length && (line[start] == ' ' || line[start] == '\t')) {
        ++start;
    }
    if (start != length ||
        firstLength != sizeof("diag") - 1U ||
        std::memcmp(line + firstStart, "diag", firstLength) != 0) {
        return DiagnosticsParseStatus::Malformed;
    }

    if (secondLength == sizeof("status") - 1U &&
        std::memcmp(line + secondStart, "status", secondLength) == 0) {
        output = DiagnosticsCommand::Status;
    } else if (secondLength == sizeof("health") - 1U &&
               std::memcmp(line + secondStart, "health", secondLength) == 0) {
        output = DiagnosticsCommand::Health;
    } else if ((secondLength == sizeof("sensor") - 1U &&
                std::memcmp(line + secondStart, "sensor", secondLength) ==
                    0) ||
               (secondLength == sizeof("sensors") - 1U &&
                std::memcmp(line + secondStart, "sensors", secondLength) ==
                    0)) {
        output = DiagnosticsCommand::Sensor;
    } else if (secondLength == sizeof("memory") - 1U &&
               std::memcmp(line + secondStart, "memory", secondLength) ==
                   0) {
        output = DiagnosticsCommand::Memory;
    } else if (secondLength == sizeof("risk") - 1U &&
               std::memcmp(line + secondStart, "risk", secondLength) == 0) {
        output = DiagnosticsCommand::Risk;
    } else {
        return DiagnosticsParseStatus::UnknownCommand;
    }
    return DiagnosticsParseStatus::Ok;
}

SerialDiagnostics::SerialDiagnostics(
    IDiagnosticsSerialPort& port, IDiagnosticsSnapshotProvider& snapshots,
    const SerialDiagnosticsConfig& config) noexcept
    : port_(port), snapshots_(snapshots), config_(config) {}

SerialDiagnosticsStatus SerialDiagnostics::begin() noexcept {
    initialized_ = false;
    lineLength_ = 0U;
    discardingOverlongLine_ = false;
    requestId_ = 0U;
    suppressedResponses_ = 0U;
    lastResponseMs_ = 0U;
    hasResponded_ = false;
    if (!config_.responseRateLimitConfigured ||
        config_.minimumResponseIntervalMs == 0U) {
        return SerialDiagnosticsStatus::InvalidConfiguration;
    }
    initialized_ = true;
    return SerialDiagnosticsStatus::Ok;
}

bool SerialDiagnostics::responseAllowed(std::uint64_t nowMs) noexcept {
    return !hasResponded_ ||
           (nowMs >= lastResponseMs_ &&
            nowMs - lastResponseMs_ >=
                config_.minimumResponseIntervalMs);
}

void SerialDiagnostics::noteSuppressedResponse() noexcept {
    if (suppressedResponses_ !=
        std::numeric_limits<std::uint32_t>::max()) {
        ++suppressedResponses_;
    }
}

SerialDiagnosticsStatus SerialDiagnostics::sendError(
    ErrorCode error, const char* status) noexcept {
    BoundedWriter writer(response_, sizeof(response_));
    if (!writer.append(
            "{\"protocol_version\":%u,\"request_id\":%lu,"
            "\"suppressed_before\":%lu,\"status\":\"%s\","
            "\"error_code\":%u,\"data\":null}\r\n",
            static_cast<unsigned int>(kProtocolVersion),
            static_cast<unsigned long>(requestId_),
            static_cast<unsigned long>(suppressedResponses_), status,
            static_cast<unsigned int>(error))) {
        return SerialDiagnosticsStatus::OutputTooLarge;
    }
    if (!port_.write(response_, writer.length())) {
        return SerialDiagnosticsStatus::TransportError;
    }
    suppressedResponses_ = 0U;
    lastResponseMs_ = port_.monotonicMillis();
    hasResponded_ = true;
    return SerialDiagnosticsStatus::Ok;
}

SerialDiagnosticsStatus SerialDiagnostics::buildPayload(
    DiagnosticsCommand command, const DiagnosticsSnapshot& snapshot,
    char* output, std::size_t capacity, std::size_t& length) const noexcept {
    BoundedWriter writer(output, capacity);

    switch (command) {
        case DiagnosticsCommand::Status:
            writer.append("{\"firmware_version_code\":");
            if (snapshot.firmwareVersionAvailable) {
                writer.append("%lu",
                              static_cast<unsigned long>(
                                  snapshot.firmwareVersionCode));
            } else {
                writer.append("null");
            }
            writer.append(",\"boot_id\":");
            if (snapshot.healthAvailable) {
                writer.append("%llu,\"uptime_ms\":%llu",
                              static_cast<unsigned long long>(
                                  snapshot.health.bootId),
                              static_cast<unsigned long long>(
                                  snapshot.health.uptimeMs));
            } else {
                writer.append("null,\"uptime_ms\":null");
            }
            writer.append(",\"mode\":\"%s\",\"risk_state\":",
                          modeName(snapshot.mode));
            if (snapshot.riskAvailable && snapshot.risk.valid) {
                writer.append("\"%s\"", riskName(snapshot.risk.risk));
            } else {
                writer.append("null");
            }
            writer.append(",\"health_state\":");
            if (snapshot.healthAvailable) {
                writer.append("\"%s\"",
                              healthName(snapshot.health.healthState));
            } else {
                writer.append("null");
            }
            writer.append("}");
            break;

        case DiagnosticsCommand::Health:
            if (!snapshot.healthAvailable) {
                writer.append("{\"available\":false}");
                break;
            }
            writer.append(
                "{\"available\":true,\"health_state\":\"%s\","
                "\"fault_flags\":%lu,\"watchdog_count\":",
                healthName(snapshot.health.healthState),
                static_cast<unsigned long>(snapshot.health.faultFlags));
            if (snapshot.health.watchdogCountValid) {
                writer.append("%llu",
                              static_cast<unsigned long long>(
                                  snapshot.health.watchdogCount));
            } else {
                writer.append("null");
            }
            writer.append(
                ",\"inference_errors\":%llu,\"sensor_read_errors\":%llu,"
                "\"queue_overflows\":%llu,\"deadline_misses\":%llu,"
                "\"queues\":[",
                static_cast<unsigned long long>(
                    snapshot.health.inferenceErrorCount),
                static_cast<unsigned long long>(
                    snapshot.health.sensorReadErrorCount),
                static_cast<unsigned long long>(
                    snapshot.health.queueOverflowCount),
                static_cast<unsigned long long>(
                    snapshot.health.deadlineMissCount));
            for (std::size_t i = 0;
                 i < snapshot.health.queueMetricCount; ++i) {
                const auto& metric = snapshot.health.queueMetrics[i];
                if (i != 0U) {
                    writer.append(",");
                }
                writer.append(
                    "{\"id\":%u,\"depth\":%lu,\"capacity\":%lu,"
                    "\"drops\":%lu,\"valid\":%s}",
                    static_cast<unsigned int>(metric.queueId),
                    static_cast<unsigned long>(metric.depth),
                    static_cast<unsigned long>(metric.capacity),
                    static_cast<unsigned long>(metric.dropCount),
                    metric.valid ? "true" : "false");
            }
            writer.append("]}");
            break;

        case DiagnosticsCommand::Sensor: {
            if (!snapshot.sensorsAvailable) {
                writer.append("{\"available\":false}");
                break;
            }
            const auto& frame = snapshot.sensors;
            const project::models::SensorStatus* const statuses[] = {
                &frame.imuStatus, &frame.soilStatus, &frame.rainStatus,
                &frame.hx710bStatus};
            const char* const names[] = {"imu", "soil", "rain", "hx710b"};
            const project::models::SensorId sensorIds[] = {
                project::models::SensorId::Imu,
                project::models::SensorId::SoilMoisture,
                project::models::SensorId::Rain,
                project::models::SensorId::Hx710b};
            const std::int32_t rawValues[] = {
                frame.imu.accelX, frame.soilMoistureRaw, frame.rainRaw,
                frame.hx710bRaw};
            writer.append(
                "{\"available\":true,\"sequence\":%llu,"
                "\"timestamp_ms\":%llu,\"sensors\":[",
                static_cast<unsigned long long>(frame.sequence),
                static_cast<unsigned long long>(frame.timestampMs));
            for (std::size_t i = 0; i < 4U; ++i) {
                if (i != 0U) {
                    writer.append(",");
                }
                const project::models::SensorHealthMetric* metric = nullptr;
                if (snapshot.healthAvailable) {
                    for (std::size_t metricIndex = 0;
                         metricIndex < snapshot.health.sensorMetricCount;
                         ++metricIndex) {
                        const auto& candidate =
                            snapshot.health.sensorMetrics[metricIndex];
                        if (candidate.valid &&
                            candidate.sensorId == sensorIds[i]) {
                            metric = &candidate;
                            break;
                        }
                    }
                }
                writer.append(
                    "{\"id\":\"%s\",\"health\":\"%s\","
                    "\"valid\":%s,\"error_code\":%u,"
                    "\"quality_flags\":%lu,\"timestamp_ms\":%llu,"
                    "\"age_ms\":",
                    names[i], sensorHealthName(statuses[i]->health),
                    statuses[i]->valid ? "true" : "false",
                    static_cast<unsigned int>(statuses[i]->errorCode),
                    static_cast<unsigned long>(statuses[i]->qualityFlags),
                    static_cast<unsigned long long>(statuses[i]->timestampMs));
                if (snapshot.healthAvailable &&
                    statuses[i]->timestampMs <= snapshot.health.uptimeMs) {
                    writer.append("%llu",
                                  static_cast<unsigned long long>(sensorAgeMs(
                                      snapshot.health.uptimeMs,
                                      statuses[i]->timestampMs)));
                } else {
                    writer.append("null");
                }
                writer.append(",\"read_error_count\":");
                if (metric != nullptr) {
                    writer.append("%llu",
                                  static_cast<unsigned long long>(
                                      metric->readErrorCount));
                } else {
                    writer.append("null");
                }
                writer.append(",\"out_of_range_count\":");
                if (metric != nullptr) {
                    writer.append("%llu",
                                  static_cast<unsigned long long>(
                                      metric->outOfRangeCount));
                } else {
                    writer.append("null");
                }
                writer.append(",\"raw\":");
                if (statuses[i]->valid && i == 0U) {
                    writer.append("[%ld,%ld,%ld,%ld,%ld,%ld]",
                                  static_cast<long>(frame.imu.accelX),
                                  static_cast<long>(frame.imu.accelY),
                                  static_cast<long>(frame.imu.accelZ),
                                  static_cast<long>(frame.imu.gyroX),
                                  static_cast<long>(frame.imu.gyroY),
                                  static_cast<long>(frame.imu.gyroZ));
                } else if (statuses[i]->valid) {
                    writer.append("%ld", static_cast<long>(rawValues[i]));
                } else {
                    writer.append("null");
                }
                writer.append("}");
            }
            writer.append("]}");
            break;
        }

        case DiagnosticsCommand::Memory:
            if (!snapshot.healthAvailable || !snapshot.memoryAvailable) {
                writer.append("{\"available\":false}");
                break;
            }
            writer.append(
                "{\"available\":true,\"free_heap_bytes\":%lu,"
                "\"minimum_free_heap_bytes\":%lu,"
                "\"largest_free_block_bytes\":%lu,"
                "\"internal_heap_bytes\":%lu,\"tasks\":[",
                static_cast<unsigned long>(snapshot.health.heapFreeBytes),
                static_cast<unsigned long>(snapshot.health.heapMinFreeBytes),
                static_cast<unsigned long>(
                    snapshot.health.largestFreeBlockBytes),
                static_cast<unsigned long>(
                    snapshot.health.internalHeapBytes));
            for (std::size_t i = 0;
                 i < snapshot.health.taskStackMetricCount; ++i) {
                const auto& metric = snapshot.health.taskStackMetrics[i];
                if (i != 0U) {
                    writer.append(",");
                }
                writer.append(
                    "{\"task_id\":%u,\"high_water_words\":%lu,"
                    "\"valid\":%s}",
                    static_cast<unsigned int>(metric.taskId),
                    static_cast<unsigned long>(metric.highWaterWords),
                    metric.valid ? "true" : "false");
            }
            writer.append("]}");
            break;

        case DiagnosticsCommand::Risk:
            if (!snapshot.riskAvailable || !snapshot.risk.valid) {
                writer.append("{\"available\":false}");
                break;
            }
            writer.append(
                "{\"available\":true,\"assessment_valid\":true,"
                "\"risk_state\":\"%s\",\"health_state\":\"%s\","
                "\"model_status\":",
                riskName(snapshot.risk.risk),
                healthName(snapshot.risk.health));
            if (snapshot.modelAvailable) {
                writer.append("\"%s\"",
                              inferenceStatusName(snapshot.model.status));
            } else {
                writer.append("null");
            }
            writer.append(",\"score\":");
            if (snapshot.risk.riskScoreValid &&
                std::isfinite(snapshot.risk.riskScore)) {
                writer.append("%.7g",
                              static_cast<double>(snapshot.risk.riskScore));
            } else {
                writer.append("null");
            }
            writer.append(
                ",\"score_valid\":%s,\"reason_code\":%u,"
                "\"active_fault_flags\":%lu,\"risk_config_version\":%lu}",
                snapshot.risk.riskScoreValid ? "true" : "false",
                static_cast<unsigned int>(
                    snapshot.risk.decisionReasonCode),
                static_cast<unsigned long>(
                    snapshot.risk.activeFaultFlags),
                static_cast<unsigned long>(
                    snapshot.risk.riskConfigVersion));
            break;
    }

    if (!writer.good()) {
        return SerialDiagnosticsStatus::OutputTooLarge;
    }
    length = writer.length();
    return SerialDiagnosticsStatus::Ok;
}

SerialDiagnosticsStatus SerialDiagnostics::processLine() noexcept {
    const bool overlong = discardingOverlongLine_;
    discardingOverlongLine_ = false;

    DiagnosticsCommand command{};
    DiagnosticsParseStatus parseStatus = DiagnosticsParseStatus::Ok;
    if (!overlong) {
        parseStatus =
            DiagnosticsCommandParser::parse(line_, lineLength_, command);
        if (parseStatus == DiagnosticsParseStatus::Empty) {
            return SerialDiagnosticsStatus::Idle;
        }
    }

    if (requestId_ == std::numeric_limits<std::uint32_t>::max()) {
        requestId_ = 1U;
    } else {
        ++requestId_;
    }

    const std::uint64_t nowMs = port_.monotonicMillis();
    if (!responseAllowed(nowMs)) {
        noteSuppressedResponse();
        return SerialDiagnosticsStatus::Idle;
    }

    if (overlong) {
        return sendError(ErrorCode::CommandTooLong, "error");
    }
    if (parseStatus == DiagnosticsParseStatus::Malformed) {
        return sendError(ErrorCode::MalformedCommand, "error");
    }
    if (parseStatus == DiagnosticsParseStatus::TooLong) {
        return sendError(ErrorCode::CommandTooLong, "error");
    }
    if (parseStatus == DiagnosticsParseStatus::UnknownCommand) {
        return sendError(ErrorCode::UnknownCommand, "error");
    }

    snapshot_ = {};
    if (!snapshots_.readSnapshot(snapshot_)) {
        return sendError(ErrorCode::SnapshotUnavailable, "unavailable");
    }
    if (!validSnapshot(snapshot_)) {
        return sendError(ErrorCode::InvalidSnapshot, "error");
    }

    std::size_t payloadLength = 0U;
    const SerialDiagnosticsStatus payloadStatus = buildPayload(
        command, snapshot_, payload_, sizeof(payload_), payloadLength);
    if (payloadStatus != SerialDiagnosticsStatus::Ok) {
        return sendError(ErrorCode::OutputTooLarge, "error");
    }

    BoundedWriter writer(response_, sizeof(response_));
    writer.append(
        "{\"protocol_version\":%u,\"request_id\":%lu,"
        "\"suppressed_before\":%lu,\"status\":\"ok\","
        "\"error_code\":0,\"data\":",
        static_cast<unsigned int>(kProtocolVersion),
        static_cast<unsigned long>(requestId_),
        static_cast<unsigned long>(suppressedResponses_));
    if (!writer.good() ||
        payloadLength >= sizeof(response_) - writer.length() ||
        !writer.append("%.*s}\r\n", static_cast<int>(payloadLength),
                       payload_)) {
        return SerialDiagnosticsStatus::OutputTooLarge;
    }
    if (!port_.write(response_, writer.length())) {
        return SerialDiagnosticsStatus::TransportError;
    }
    suppressedResponses_ = 0U;
    lastResponseMs_ = port_.monotonicMillis();
    hasResponded_ = true;
    return SerialDiagnosticsStatus::Ok;
}

SerialDiagnosticsStatus SerialDiagnostics::poll() noexcept {
    if (!initialized_) {
        return SerialDiagnosticsStatus::NotInitialized;
    }

    std::size_t bytesProcessed = 0U;
    while (bytesProcessed < kMaximumBytesPerPoll &&
           port_.available() > 0U) {
        const int read = port_.readByte();
        if (read < 0 || read > 0xFF) {
            return SerialDiagnosticsStatus::TransportError;
        }
        ++bytesProcessed;
        const char value = static_cast<char>(read);
        if (value == '\n') {
            if (lineLength_ > 0U && line_[lineLength_ - 1U] == '\r') {
                --lineLength_;
            }
            const SerialDiagnosticsStatus result = processLine();
            lineLength_ = 0U;
            if (result != SerialDiagnosticsStatus::Idle) {
                return result;
            }
            continue;
        }
        if (discardingOverlongLine_) {
            continue;
        }
        if (lineLength_ ==
                DiagnosticsCommandParser::kMaximumCommandLength &&
            value == '\r') {
            line_[lineLength_++] = value;
            continue;
        }
        if (lineLength_ >=
            DiagnosticsCommandParser::kMaximumCommandLength) {
            lineLength_ = 0U;
            discardingOverlongLine_ = true;
            continue;
        }
        const unsigned char byte = static_cast<unsigned char>(value);
        if (byte == 0U || (byte < 0x20U && byte != '\t' && byte != '\r') ||
            byte > 0x7EU) {
            line_[lineLength_++] = '\x01';
            continue;
        }
        line_[lineLength_++] = value;
    }
    return bytesProcessed == 0U ? SerialDiagnosticsStatus::Idle
                                : SerialDiagnosticsStatus::Ok;
}

}  // namespace project::diagnostics
