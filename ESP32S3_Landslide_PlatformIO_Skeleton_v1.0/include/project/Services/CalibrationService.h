#pragma once

#include <cstdint>

#include "Drivers/HX710BDriver.h"
#include "Models/DataContractTypes.h"
#include "Models/ProcessedFrame.h"
#include "Models/SensorFrame.h"

namespace project::services {

enum class CalibrationMode : std::uint8_t {
    RawOnly,
    Hx710bPressureOnly
};

enum class Hx710bBaselineStatus : std::uint8_t {
    Disabled,
    InsufficientData,
    Ready,
    Stale
};

struct Hx710bBaselinePolicy {
    std::uint32_t windowDurationMs{15000};
    std::uint32_t minimumValidSamples{3};
    std::uint32_t maximumSampleAgeMs{0};
    bool configured{false};
};

struct Hx710bBaselineSnapshot {
    float pressureKpa{0.0F};
    std::uint32_t validSampleCount{0};
    std::uint64_t windowStartMs{0};
    std::uint64_t lastSampleMs{0};
    Hx710bBaselineStatus status{Hx710bBaselineStatus::Disabled};
    bool valid{false};
};

class CalibrationService {
public:
    CalibrationService() noexcept = default;
    CalibrationService(
        const project::drivers::Hx710bCalibration& hx710bCalibration,
        const Hx710bBaselinePolicy& baselinePolicy) noexcept;

    CalibrationMode mode() const noexcept { return mode_; }
    bool loadCalibration();
    project::models::ApiStatus loadCalibrationStatus() const noexcept;

    bool process(const project::models::SensorFrame& input,
                 project::models::ProcessedFrame& output);

    project::models::ApiStatus processStatus(
        const project::models::SensorFrame& input,
        project::models::ProcessedFrame& output) const noexcept;

    Hx710bBaselineStatus updateHx710bBaseline(
        const project::models::ProcessedFrame& frame,
        std::uint64_t nowMs) noexcept;
    Hx710bBaselineSnapshot hx710bBaseline(
        std::uint64_t nowMs) const noexcept;
    void resetHx710bBaseline() noexcept;

private:
    const project::drivers::Hx710bCalibration hx710bCalibration_{};
    const Hx710bBaselinePolicy baselinePolicy_{};
    CalibrationMode mode_{CalibrationMode::RawOnly};
    double hx710bBaselineSumKpa_{0.0};
    std::uint32_t hx710bBaselineSampleCount_{0};
    std::uint64_t hx710bBaselineStartMs_{0};
    std::uint64_t hx710bBaselineLastSampleMs_{0};
    bool hx710bBaselineReady_{false};
};
}  // namespace project::services
