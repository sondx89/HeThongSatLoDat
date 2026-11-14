#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Models/DataContractTypes.h"
#include "Models/SensorFrame.h"
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
    float rainWetnessRelativePct{0.0F};
    bool rainDigitalWet{false};
    bool rainDigitalWetValid{false};
    float rainAccEquivalentWetSeconds{0.0F};
    bool rainAccValid{false};
    float rainRateMmH{0.0F};
    float hx710bCalibratedValue{0.0F};  // kPa only with an approved profile.
    bool hx710bCalibratedValueValid{false};
    std::uint64_t hx710bSampleTimestampMs{0};
    std::uint32_t hx710bTransducerIdentityId{0};
    std::uint32_t hx710bCalibrationVersion{0};
    float hx710bBaselineKpa{0.0F};
    std::uint32_t hx710bBaselineSampleCount{0};
    bool hx710bBaselineValid{false};

    std::array<DataQualityState,
               static_cast<std::size_t>(SensorChannel::Count)>
        channelQuality{};
    std::uint32_t validMask{0};       // TODO(TBD): freeze bit-to-field mapping.
    std::uint32_t qualityFlags{0};
    std::uint32_t sensorStatusBits{0};
    std::uint32_t calibrationVersion{0};
    SensorStatus aggregateStatus{};
};
}  // namespace project::models
