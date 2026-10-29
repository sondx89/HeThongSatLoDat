#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "BSP/BoardPins.h"
#include "Drivers/IAdcReader.h"
#include "Drivers/ISensor.h"

namespace project::drivers {

struct RainCalibrationPoint {
    std::uint16_t adcCount{0};
    float wetnessPercent{0.0F};
};

struct RainSensorDriverConfig {
    std::uint16_t adcMaximumCount{0};
    std::uint8_t filterWindowSamples{0};
    std::array<RainCalibrationPoint, 2> calibrationPoints{};
    std::uint32_t calibrationVersion{0};
    bool adcConfigurationVerified{false};
    bool wetnessSemanticsVerified{false};
    bool calibrationConfigured{false};
};

enum class RainQualityFlag : std::uint32_t {
    None = 0,
    AdcRangeValid = 1U << 0U,
    CalibrationValid = 1U << 1U,
    FilterWindowWarming = 1U << 2U,
    WetnessClampedLow = 1U << 3U,
    WetnessClampedHigh = 1U << 4U
};

constexpr std::uint32_t rainQualityMask(RainQualityFlag flag) noexcept {
    return static_cast<std::uint32_t>(flag);
}

enum class RainSensorStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    NotInitialized,
    AdcReadFailed,
    AdcValueOutOfRange,
    InvalidCalibration
};

struct RainSensorSample {
    std::int32_t rawAdcCount{0};
    float filteredAdcCount{0.0F};
    float wetnessRelativePercent{0.0F};
    std::uint8_t filterSampleCount{0};
    std::uint32_t calibrationVersion{0};
    std::uint32_t qualityFlags{0};
    bool valid{false};
};

class RainSensorDriver final : public ISensor {
public:
    static constexpr std::size_t kMaxFilterWindowSamples = 16;

    RainSensorDriver(IAdcReader& adcReader,
                     const project::bsp::BoardPins& boardPins,
                     const RainSensorDriverConfig& config) noexcept;

    project::models::SensorStatus begin() override;
    project::models::SensorStatus read(
        project::models::SensorFrame& output) override;
    project::models::SensorStatus selfTest() override;

    RainSensorStatus readSample(RainSensorSample& output) noexcept;
    RainSensorStatus lastStatus() const noexcept;

    RainSensorDriver(const RainSensorDriver&) = delete;
    RainSensorDriver& operator=(const RainSensorDriver&) = delete;

private:
    RainSensorStatus validateConfiguration() const noexcept;
    float mapAdcToWetness(float adcCount,
                          std::uint32_t& qualityFlags) const noexcept;
    void clearFilter() noexcept;
    project::models::SensorStatus makeSensorStatus(bool valid,
                                                   std::uint32_t qualityFlags = 0) const noexcept;

    IAdcReader& adcReader_;
    const project::bsp::BoardPins& boardPins_;
    const RainSensorDriverConfig config_;
    std::array<std::int32_t, kMaxFilterWindowSamples> samples_{};
    std::size_t nextSampleIndex_{0};
    std::size_t sampleCount_{0};
    std::uint64_t sampleSum_{0};
    RainSensorStatus lastStatus_{RainSensorStatus::NotInitialized};
    bool initialized_{false};
};

}  // namespace project::drivers
