#pragma once

#include <cstddef>
#include <cstdint>

#include "Models/SensorStatus.h"
#include "Models/DataContractTypes.h"

namespace project::models {

// Bit positions and channel availability are schema-versioned; these IDs are
// logical channel identifiers, not a frozen serialization/valid_mask mapping.
enum class SensorChannel : std::uint8_t {
    ImuAccelXRaw,
    ImuAccelYRaw,
    ImuAccelZRaw,
    ImuGyroXRaw,
    ImuGyroYRaw,
    ImuGyroZRaw,
    AccelXG,
    AccelYG,
    AccelZG,
    GyroXDps,
    GyroYDps,
    GyroZDps,
    TiltAngleDeg,
    SoilMoistureRaw,
    SoilMoisturePct,
    RainRaw,
    RainRateMmH,
    Hx710bRaw,
    Hx710bCalibratedValue
};

struct ImuRawSample {
    std::int32_t accelX{0};
    std::int32_t accelY{0};
    std::int32_t accelZ{0};
    std::int32_t gyroX{0};
    std::int32_t gyroY{0};
    std::int32_t gyroZ{0};
};

struct ImuPhysicalSample {
    float accelXG{0.0F};
    float accelYG{0.0F};
    float accelZG{0.0F};
    float gyroXDps{0.0F};
    float gyroYDps{0.0F};
    float gyroZDps{0.0F};
};

struct SensorFrame {
    std::uint32_t schemaVersion{0};  // TODO(TBD): freeze record schema version.
    std::uint64_t bootId{0};
    std::uint64_t sequence{0};
    std::uint64_t timestampMs{0};    // Monotonic milliseconds within boot.
    std::uint32_t samplePeriodMs{0}; // TODO(TBD): semantics/configuration per sensor.
    TimeStatus timeStatus{TimeStatus::Unknown};
    // TODO(TBD): add a UTC value only after the clock source/encoding is approved.

    // All values are initialized for safe copying; validMask, not zero, decides
    // whether a channel contains a usable measurement.
    ImuRawSample imu{};
    ImuPhysicalSample imuPhysical{};
    float tiltAngleDeg{0.0F};  // TODO(TBD): confirm feature definition/reference.
    bool tiltAngleValid{false};
    std::int32_t soilMoistureRaw{0};
    float soilMoisturePct{0.0F};     // Valid only after approved calibration.
    std::int32_t rainRaw{0};
    float rainRateMmH{0.0F};         // Valid only if sensor/calibration supports it.
    std::int32_t hx710bRaw{0};
    float hx710bCalibratedValue{0.0F};  // Unit/measurand remain TBD.

    std::uint32_t validMask{0};       // TODO(TBD): freeze bit-to-field mapping.
    std::uint32_t qualityFlags{0};    // TODO(TBD): freeze quality bit assignments.
    std::uint32_t sensorStatusBits{0}; // TODO(TBD): freeze sensor bit assignments.
    std::uint32_t calibrationVersion{0};

    SensorStatus imuStatus{};
    SensorStatus soilStatus{};
    SensorStatus rainStatus{};
    SensorStatus hx710bStatus{};
};

}  // namespace project::models
