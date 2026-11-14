#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "BSP/BoardPins.h"
#include "Drivers/IAdcReader.h"
#include "Drivers/ISensor.h"

namespace project::drivers {

class IRainDigitalReader {
public:
    virtual ~IRainDigitalReader() = default;
    virtual bool begin(std::uint8_t pin) noexcept = 0;
    virtual bool readLevel(std::uint8_t pin, bool& levelHigh) noexcept = 0;
};

struct RainCalibrationPoint {
    std::uint16_t adcCount{0};
    float wetnessPercent{0.0F};
};

struct RainSensorDriverConfig {
    std::uint16_t adcMaximumCount{0};
    std::uint32_t accumulationSamplePeriodMs{0};
    std::uint32_t maximumSampleGapMs{0};
    std::array<RainCalibrationPoint, 2> calibrationPoints{};
    std::uint32_t calibrationVersion{0};
    bool adcConfigurationVerified{false};
    bool wetnessSemanticsVerified{false};
    bool calibrationConfigured{false};
    bool digitalOutputEnabled{false};
    bool digitalActiveLowVerified{false};
};

enum class RainQualityFlag : std::uint32_t {
    None = 0,
    AdcRangeValid = 1U << 0U,
    CalibrationValid = 1U << 1U,
    FilterWindowWarming = 1U << 2U,
    WetnessClampedLow = 1U << 3U,
    WetnessClampedHigh = 1U << 4U,
    DigitalLevelValid = 1U << 5U,
    AccumulationValid = 1U << 6U,
    AccumulationWindowWarming = 1U << 7U,
    AccumulationGapDetected = 1U << 8U,
    AdcReadFailure = 1U << 9U,
    AdcValueOutOfRange = 1U << 10U,
    DigitalReadFailure = 1U << 11U,
    AnalogStale = 1U << 12U
};

constexpr std::uint32_t rainQualityMask(RainQualityFlag flag) noexcept {
    return static_cast<std::uint32_t>(flag);
}

enum class RainSensorStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    NotInitialized,
    SamplePending,
    AdcReadFailed,
    AdcValueOutOfRange,
    DigitalReadFailed,
    Stale,
    ClockError,
    InvalidCalibration
};

struct RainSensorSample {
    std::int32_t rawAdcCount{0};
    float filteredAdcCount{0.0F};
    float wetnessRelativePercent{0.0F};
    std::uint8_t filterSampleCount{0};
    std::uint32_t calibrationVersion{0};
    std::uint32_t qualityFlags{0};
    std::uint64_t timestampMs{0};
    std::uint32_t rainAccCoverageMs{0};
    float rainAccEquivalentWetSeconds{0.0F};
    bool valid{false};
    bool digitalWet{false};
    bool digitalValid{false};
    bool rainAccValid{false};
    project::models::SensorStatus analogStatus{};
    project::models::SensorStatus digitalStatus{};
    project::models::SensorStatus accumulationStatus{};
};

class RainSensorDriver final : public ISensor {
public:
    static constexpr std::size_t kAnalogSamplesPerReading = 8;
    static constexpr std::size_t kMaxAccumulationSamples = 256;
    static constexpr std::uint64_t kMinimumAnalogSampleSpacingUs = 2000;
    static constexpr std::uint32_t kAccumulationWindowMs = 120000;
    static constexpr std::uint32_t kMinimumAccumulationSamplePeriodMs = 500;
    static constexpr std::uint32_t kAnalogBurstMinimumSpanMs =
        static_cast<std::uint32_t>(
            (kAnalogSamplesPerReading - 1U) *
            kMinimumAnalogSampleSpacingUs / 1000U);

    RainSensorDriver(IAdcReader& adcReader,
                     IRainDigitalReader& digitalReader,
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
    struct AccumulationPoint {
        std::uint64_t timestampMs{0};
        float wetnessPercent{0.0F};
    };

    RainSensorStatus validateConfiguration() const noexcept;
    float mapAdcToWetness(float adcCount,
                          std::uint32_t& qualityFlags) const noexcept;
    void clearAnalogBurst() noexcept;
    void clearAccumulation() noexcept;
    void addAccumulationPoint(std::uint64_t timestampMs,
                              float wetnessPercent) noexcept;
    void updateAccumulation(std::uint64_t nowMs,
                            RainSensorSample& output) noexcept;
    project::models::SensorStatus makeSensorStatus(
        bool valid, bool degraded, std::uint64_t timestampMs,
        std::uint32_t qualityFlags = 0) const noexcept;

    IAdcReader& adcReader_;
    IRainDigitalReader& digitalReader_;
    const project::bsp::BoardPins& boardPins_;
    const RainSensorDriverConfig config_;
    std::array<AccumulationPoint, kMaxAccumulationSamples> accumulation_{};
    std::size_t analogSampleCount_{0};
    std::size_t accumulationCount_{0};
    std::uint64_t analogSampleSum_{0};
    std::uint64_t lastAnalogAttemptMicros_{0};
    std::uint64_t lastAnalogTimestampMs_{0};
    std::uint64_t lastAccumulationTimestampMs_{0};
    std::uint64_t lastClockMicros_{0};
    std::int32_t lastRawAdcCount_{0};
    float lastFilteredAdcCount_{0.0F};
    float lastWetnessPercent_{0.0F};
    std::uint32_t lastAnalogQualityFlags_{0};
    RainSensorStatus lastAnalogError_{RainSensorStatus::SamplePending};
    RainSensorStatus lastStatus_{RainSensorStatus::NotInitialized};
    bool hasAnalogAttempt_{false};
    bool hasAnalogSample_{false};
    bool hasAccumulationSample_{false};
    bool hasClockSample_{false};
    bool digitalReady_{false};
    bool digitalConfigurationValid_{false};
    bool initialized_{false};
};

}  // namespace project::drivers
