#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "BSP/BoardPins.h"
#include "Drivers/IAdcReader.h"
#include "Drivers/ISensor.h"

namespace project::drivers {

struct SoilMoistureCalibrationPoint {
    std::uint16_t adcCount{0};
    float moisturePercent{0.0F};
};

struct SoilMoistureChannelConfig {
    bool present{false};
    bool required{false};
    std::array<SoilMoistureCalibrationPoint, 2> calibrationPoints{};
};

struct SoilMoistureDriverConfig {
    std::uint16_t adcMaximumCount{0};
    std::array<SoilMoistureChannelConfig,
               project::models::kSoilMoistureProbeCount>
        channels{};
    std::uint8_t requiredChannelCount{0};
    std::uint32_t maximumSampleAgeMs{0};
    std::uint32_t calibrationVersion{0};
    bool adcConfigurationVerified{false};
    bool calibrationConfigured{false};
};

enum class SoilMoistureStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    NotInitialized,
    SamplePending,
    AdcReadFailed,
    AdcValueOutOfRange,
    InvalidCalibration,
    ClockError
};

struct SoilMoistureSample {
    std::int32_t rawAdcCount{0};
    float filteredAdcCount{0.0F};
    float moisturePercent{0.0F};
    std::uint8_t filterSampleCount{0};
    std::array<project::models::SoilMoistureProbeSample,
               project::models::kSoilMoistureProbeCount>
        probes{};
    std::uint8_t validChannelCount{0};
    std::uint8_t requiredChannelCount{0};
    std::uint8_t rawChannelIndex{0xFF};
    std::uint32_t calibrationVersion{0};
    bool valid{false};
};

class SoilMoistureDriver final : public ISensor {
public:
    static constexpr std::size_t kSamplesPerChannel = 5;
    static constexpr std::uint64_t kMinimumSampleSpacingUs = 5000;

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
    float mapAdcToMoisture(std::size_t channelIndex,
                           float adcCount) const noexcept;
    void resetChannel(std::size_t channelIndex) noexcept;
    void populateSample(std::uint64_t nowMicros,
                        SoilMoistureSample& output) noexcept;
    project::models::SensorStatus makeSensorStatus(bool valid) const noexcept;

    struct ChannelState {
        std::array<std::int32_t, kSamplesPerChannel> samples{};
        std::size_t nextSampleIndex{0};
        std::size_t sampleCount{0};
        std::uint64_t sampleSum{0};
        std::uint64_t lastSampleMicros{0};
        std::uint64_t lastSuccessfulMicros{0};
        std::int32_t rawAdcCount{0};
        project::models::SoilMoistureQuality quality{
            project::models::SoilMoistureQuality::NotPresent};
        bool hasSampled{false};
        bool hasSuccessfulSample{false};
    };

    IAdcReader& adcReader_;
    const project::bsp::BoardPins& boardPins_;
    const SoilMoistureDriverConfig config_;
    std::array<ChannelState, project::models::kSoilMoistureProbeCount>
        channels_{};
    SoilMoistureStatus lastStatus_{SoilMoistureStatus::NotInitialized};
    bool initialized_{false};
};

}  // namespace project::drivers
