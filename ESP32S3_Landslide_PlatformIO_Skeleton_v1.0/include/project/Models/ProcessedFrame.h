#pragma once

#include <cstdint>

#include "Models/SensorStatus.h"

namespace project::models {
struct ProcessedFrame {
    std::uint32_t schemaVersion{0};  // TODO(TBD): freeze record schema version.
    std::uint64_t bootId{0};
    std::uint64_t sequence{0};
    std::uint64_t timestampMs{0};    // Monotonic milliseconds within boot.

    float accelXG{0.0F};
    float accelYG{0.0F};
    float accelZG{0.0F};
    float gyroXDps{0.0F};
    float gyroYDps{0.0F};
    float gyroZDps{0.0F};
    float tiltRollDeg{0.0F};   // TODO(TBD): formula/frame and eligibility.
    float tiltPitchDeg{0.0F};  // TODO(TBD): formula/frame and eligibility.
    float accelMagnitude{0.0F}; // TODO(TBD): unit/formula and eligibility.
    float soilMoisturePct{0.0F};
    float rainRateMmH{0.0F};
    float hx710bCalibratedValue{0.0F};  // Unit/measurand remain TBD.

    std::uint32_t validMask{0};       // TODO(TBD): freeze bit-to-field mapping.
    std::uint32_t qualityFlags{0};
    std::uint32_t sensorStatusBits{0};
    std::uint32_t calibrationVersion{0};
    SensorStatus aggregateStatus{};
};
}  // namespace project::models
