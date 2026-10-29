#pragma once

#include <cstddef>
#include <cstdint>

#include "Models/HealthSnapshot.h"
#include "Models/ModelOutput.h"
#include "Models/RiskAssessment.h"
#include "Models/SensorFrame.h"

namespace project::diagnostics {

enum class DiagnosticsCommand : std::uint8_t {
    Status,
    Health,
    Sensor,
    Memory,
    Risk
};

enum class DiagnosticsParseStatus : std::uint8_t {
    Ok,
    Empty,
    Malformed,
    TooLong,
    UnknownCommand
};

class DiagnosticsCommandParser final {
public:
    static constexpr std::size_t kMaximumCommandLength = 128U;

    static DiagnosticsParseStatus parse(const char* line,
                                        std::size_t length,
                                        DiagnosticsCommand& output) noexcept;
};

enum class DiagnosticsRuntimeMode : std::uint8_t {
    Unknown,
    Diagnostic,
    Degraded,
    Operational
};

struct DiagnosticsSnapshot {
    std::uint32_t firmwareVersionCode{0};
    bool firmwareVersionAvailable{false};
    DiagnosticsRuntimeMode mode{DiagnosticsRuntimeMode::Unknown};

    project::models::HealthSnapshot health{};
    project::models::SensorFrame sensors{};
    project::models::RiskAssessment risk{};
    project::models::ModelOutput model{};

    bool healthAvailable{false};
    bool memoryAvailable{false};
    bool sensorsAvailable{false};
    bool riskAvailable{false};
    bool modelAvailable{false};
};

class IDiagnosticsSnapshotProvider {
public:
    virtual ~IDiagnosticsSnapshotProvider() = default;

    // Must return a bounded, internally consistent copy of the latest
    // published diagnostics snapshots. It must not print to the serial port.
    virtual bool readSnapshot(DiagnosticsSnapshot& output) noexcept = 0;
};

class IDiagnosticsSerialPort {
public:
    virtual ~IDiagnosticsSerialPort() = default;
    virtual std::size_t available() noexcept = 0;
    virtual int readByte() noexcept = 0;
    // Must be bounded and return true only if the entire buffer was accepted.
    virtual bool write(const char* data, std::size_t length) noexcept = 0;
    virtual std::uint64_t monotonicMillis() noexcept = 0;
};

struct SerialDiagnosticsConfig {
    std::uint32_t minimumResponseIntervalMs{0};
    bool responseRateLimitConfigured{false};
};

enum class SerialDiagnosticsStatus : std::uint8_t {
    Ok,
    Idle,
    NotInitialized,
    InvalidConfiguration,
    SnapshotUnavailable,
    InvalidSnapshot,
    OutputTooLarge,
    TransportError
};

class SerialDiagnostics final {
public:
    static constexpr std::uint8_t kProtocolVersion = 1U;
    static constexpr std::size_t kResponseCapacity = 1536U;
    static constexpr std::size_t kMaximumBytesPerPoll = 64U;

    SerialDiagnostics(IDiagnosticsSerialPort& port,
                      IDiagnosticsSnapshotProvider& snapshots,
                      const SerialDiagnosticsConfig& config) noexcept;

    SerialDiagnosticsStatus begin() noexcept;
    SerialDiagnosticsStatus poll() noexcept;
    bool initialized() const noexcept { return initialized_; }

private:
    enum class ErrorCode : std::uint8_t {
        None = 0,
        MalformedCommand = 1,
        UnknownCommand = 2,
        CommandTooLong = 3,
        SnapshotUnavailable = 4,
        InvalidSnapshot = 5,
        OutputTooLarge = 6
    };

    SerialDiagnosticsStatus processLine() noexcept;
    SerialDiagnosticsStatus sendError(ErrorCode error,
                                      const char* status) noexcept;
    bool responseAllowed(std::uint64_t nowMs) noexcept;
    void noteSuppressedResponse() noexcept;
    bool buildPayload(DiagnosticsCommand command,
                      const DiagnosticsSnapshot& snapshot,
                      char* output, std::size_t capacity,
                      std::size_t& length) const noexcept;

    IDiagnosticsSerialPort& port_;
    IDiagnosticsSnapshotProvider& snapshots_;
    const SerialDiagnosticsConfig config_;
    DiagnosticsSnapshot snapshot_{};
    char line_[DiagnosticsCommandParser::kMaximumCommandLength + 2U]{};
    std::size_t lineLength_{0};
    bool discardingOverlongLine_{false};
    char response_[kResponseCapacity]{};
    char payload_[kResponseCapacity]{};
    std::uint32_t requestId_{0};
    std::uint32_t suppressedResponses_{0};
    std::uint64_t lastResponseMs_{0};
    bool hasResponded_{false};
    bool initialized_{false};
};

}  // namespace project::diagnostics
