#pragma once

#include <cstdint>

namespace project::models {

enum class RiskState : std::uint8_t {
    Startup = 0,
    Normal = 1,
    Safe = Normal,
    Monitor = 2,
    DangerTest = 3,
    HighRiskTest = DangerTest,
    Unknown = 4
};

enum class SensorId : std::uint16_t {
    Unknown,
    Imu,
    SoilMoisture,
    Rain,
    Hx710b
    // TODO(TBD): assign stable registry values after hardware is confirmed.
};

enum class FaultKind : std::uint8_t {
    SensorFault,
    ModelError,
    StorageError,
    ConfigError,
    TimeUnsync,
    OutputFault,
    DataInvalid,
    QueueOverflow,
    WatchdogRecovery
    // TODO(TBD): assign the released fault_flags bit mapping.
};

enum class HealthState : std::uint8_t {
    Unknown = 0,
    Healthy = 1,
    Degraded = 2,
    Fault = 3
};

enum class InferenceStatus : std::uint8_t {
    Ok,
    InvalidInput,
    ModelError,
    NonFiniteOutput,
    VersionMismatch,
    Timeout
};

enum class AlarmOutputState : std::uint8_t {
    Off,
    On,
    Failed,
    NotPresent,
    NotVerifiable
};

enum class EventSeverity : std::uint8_t {
    Debug = 0,
    Info = 1,
    Notice = 2,
    Warn = 3,
    Warning = Warn,
    Error = 4,
    Critical = 5
};

enum class DataQualityFlag : std::uint8_t {
    InvalidRange,
    InvalidFormat,
    SensorError,
    Stale,
    Missing,
    OutOfOrder,
    CalibrationInvalid,
    NonFinite,
    Saturated,
    GapDetected,
    NotReady,
    BusError
};

enum class TimeStatus : std::uint8_t {
    Unknown,
    Unsynced,
    Synced
};

enum class DataQualityState : std::uint8_t {
    MISSING,
    VALID,
    STALE,
    OUT_OF_RANGE,
    NON_FINITE
};

enum class ApiStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    NotInitialized,
    Timeout,
    IoError,
    DataInvalid,
    StaleData,
    BufferFull,
    NotSupported,
    VersionMismatch,
    ResourceExhausted,
    InternalError,
    Busy,
    Cancelled
};

enum class ResetReason : std::uint8_t {
    Unknown,
    PlatformSpecific
};

enum class SerializationStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    BufferTooSmall,
    UnsupportedVersion,
    NotImplemented
};

enum class SerializationFormat : std::uint8_t {
    Csv,
    JsonLines,
    Binary
};

}  // namespace project::models
