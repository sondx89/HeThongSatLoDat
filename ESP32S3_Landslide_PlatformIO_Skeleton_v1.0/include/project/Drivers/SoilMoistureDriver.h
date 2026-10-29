#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "BSP/BoardPins.h"
#include "Drivers/ISensor.h"

namespace project::drivers {

class IAdcReader {
public:
    virtual ~IAdcReader() = default;
    virtual bool readRaw(std::uint8_t pin,
                         std::int32_t& output) noexcept = 0;
};

class ArduinoAdcReader final : public IAdcReader {
public:
    bool readRaw(std::uint8_t pin,
                 std::int32_t& output) noexcept override;
};

struct SoilMoistureCalibrationPoint {
    std::uint16_t adcCount{0};
    float moisturePercent{0.0F};
};

struct SoilMoistureDriverConfig {
    std::uint16_t adcMaximumCount{0};
    std::uint8_t filterWindowSamples{0};
    std::array<SoilMoistureCalibrationPoint, 2> calibrationPoints{};
    std::uint32_t calibrationVersion{0};
    bool adcConfigurationVerified{false};
    bool calibrationConfigured{false};
};

enum class SoilMoistureStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    NotInitialized,
    AdcReadFailed,
    AdcValueOutOfRange,
    InvalidCalibration
};

struct SoilMoistureSample {
    std::int32_t rawAdcCount{0};
    float filteredAdcCount{0.0F};
    float moisturePercent{0.0F};
    std::uint8_t filterSampleCount{0};
    std::uint32_t calibrationVersion{0};
    bool valid{false};
};

class SoilMoistureDriver final : public ISensor {
public:
    static constexpr std::size_t kMaxFilterWindowSamples = 16;

    SoilMoistureDriver(IAdcReader& adcReader,
                       const project::bsp::BoardPins& boardPins,
                       const SoilMoistureDriverConfig& config) noexcept;

    project::models::SensorStatus begin() override;
    project::models::SensorStatus read(
        project::models::SensorFrame& output) override;
    project::models::SensorStatus selfTest() override;

    SoilMoistureStatus readSample(SoilMoistureSample& output) noexcept;
    SoilMoistureStatus lastStatus() const noexcept;

    SoilMoistureDriver(const SoilMoistureDriver&) = delete;
    SoilMoistureDriver& operator=(const SoilMoistureDriver&) = delete;

private:
    SoilMoistureStatus validateConfiguration() const noexcept;
    float mapAdcToMoisture(float adcCount) const noexcept;
    project::models::SensorStatus makeSensorStatus(bool valid) const noexcept;

    IAdcReader& adcReader_;
    const project::bsp::BoardPins& boardPins_;
    const SoilMoistureDriverConfig config_;
    std::array<std::int32_t, kMaxFilterWindowSamples> samples_{};
    std::size_t nextSampleIndex_{0};
    std::size_t sampleCount_{0};
    std::uint64_t sampleSum_{0};
    SoilMoistureStatus lastStatus_{SoilMoistureStatus::NotInitialized};
    bool initialized_{false};
};

}  // namespace project::drivers
