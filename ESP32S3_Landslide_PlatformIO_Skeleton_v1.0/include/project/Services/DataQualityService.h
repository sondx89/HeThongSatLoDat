#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Models/SensorFrame.h"
#include "Models/ProcessedFrame.h"

namespace project::services {

enum class QualitySensor : std::uint8_t {
    Imu,
    Soil,
    Rain,
    Hx710b,
    Count
};

struct SensorFreshnessLimit {
    std::uint64_t maximumAgeMs{0};
    bool configured{false};
};

struct QualityRange {
    double minimum{0.0};
    double maximum{0.0};
    bool configured{false};
};

struct DataQualityConfig {
    std::array<SensorFreshnessLimit,
               static_cast<std::size_t>(QualitySensor::Count)>
        freshness{};
    std::array<QualityRange,
               static_cast<std::size_t>(
                   project::models::SensorChannel::Count)>
        ranges{};
    std::array<bool,
               static_cast<std::size_t>(
                   project::models::SensorChannel::Count)>
        requiredChannels{};
    bool requiredChannelsConfigured{false};
};

enum class DataQualityServiceStatus : std::uint8_t {
    Ok,
    InvalidConfiguration
};

class DataQualityService {
public:
    explicit DataQualityService(
        const DataQualityConfig& config = DataQualityConfig{}) noexcept;

    DataQualityServiceStatus process(
        const project::models::SensorFrame& input,
        std::uint64_t nowMs,
        project::models::ProcessedFrame& output) const noexcept;
    bool process(const project::models::SensorFrame& input,
                 project::models::ProcessedFrame& output) const noexcept;

    bool validate(const project::models::SensorFrame& frame) const noexcept;
    bool validate(const project::models::SensorFrame& frame,
                  std::uint64_t nowMs) const noexcept;
    bool validate(
        project::models::ProcessedFrame& frame) const noexcept;
    bool validate(
        const project::models::ProcessedFrame& frame) const noexcept;

private:
    bool validateConfiguration() const noexcept;
    bool requiredChannelsAreValid(
        const project::models::ProcessedFrame& frame) const noexcept;
    project::models::DataQualityState classify(
        const project::models::SensorFrame& frame,
        project::models::SensorChannel channel,
        double value,
        bool fieldPresent,
        std::uint64_t nowMs) const noexcept;

    const DataQualityConfig config_;
};
}  // namespace project::services
