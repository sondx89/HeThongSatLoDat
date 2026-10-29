#pragma once

#include <cstdint>

namespace project::models {

enum class RiskState : std::uint8_t {
    Startup,
    Normal,
    Watch,      // Candidate names conflict across Data Dictionary/ICD/LLD.
    Monitor,    // TODO(TBD): resolve before policy or serialization is frozen.
    Warning,
    DangerTest,
    Alarm,
    Critical,
    Unknown
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
    Unknown,
    Healthy,
    Degraded,
    Fault
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
    Debug,
    Info,
    Notice,  // TODO(TBD): reconcile Data Dictionary NOTICE with ICD WARNING.
    Warning,
    Error,
    Critical
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
