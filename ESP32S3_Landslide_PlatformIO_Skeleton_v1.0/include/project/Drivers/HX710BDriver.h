#pragma once

#include <cstdint>

#include "BSP/BoardPins.h"
#include "Drivers/ISensor.h"

namespace project::drivers {

struct Hx710bProtocolConfig {
    std::uint32_t readyTimeoutUs{0};
    std::uint32_t conversionTimeoutUs{0};
    std::uint16_t clockHalfPeriodUs{0};
    std::uint8_t dataBitCount{0};
    std::uint8_t trailingClockPulses{0};
    bool readyLevelHigh{false};
    bool signedOutput{false};
    bool verifiedAgainstDatasheet{false};
};

enum class Hx710bTransportStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    NotInitialized,
    NotReady,
    Timeout,
    ReadFailure,
    RawOutOfRange
};

class IHx710bTransport {
public:
    virtual ~IHx710bTransport() = default;
    virtual Hx710bTransportStatus begin() noexcept = 0;
    virtual Hx710bTransportStatus readRaw(
        std::int32_t& rawValue) noexcept = 0;
};

class ArduinoHx710bTransport final : public IHx710bTransport {
public:
    ArduinoHx710bTransport(
        const project::bsp::BoardPins& boardPins,
        const Hx710bProtocolConfig& protocol) noexcept;

    Hx710bTransportStatus begin() noexcept override;
    Hx710bTransportStatus readRaw(std::int32_t& rawValue) noexcept override;

    ArduinoHx710bTransport(const ArduinoHx710bTransport&) = delete;
    ArduinoHx710bTransport& operator=(const ArduinoHx710bTransport&) = delete;

private:
    const project::bsp::BoardPins& boardPins_;
    const Hx710bProtocolConfig protocol_;
    bool initialized_{false};
};

struct Hx710bCalibration {
    float factor{0.0F};
    std::uint32_t version{0};
    bool configured{false};
};

enum class Hx710bQualityFlag : std::uint32_t {
    None = 0,
    RawValid = 1U << 0U,
    CalibrationFactorValid = 1U << 1U,
    CalibrationUnavailable = 1U << 2U,
    CalibrationInvalid = 1U << 3U,
    FactorScaledRawValid = 1U << 4U,
    FactorScaledRawOverflow = 1U << 5U
};

constexpr std::uint32_t hx710bQualityMask(
    Hx710bQualityFlag flag) noexcept {
    return static_cast<std::uint32_t>(flag);
}

enum class Hx710bStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    NotInitialized,
    NotReady,
    Timeout,
    ReadFailure,
    RawOutOfRange
};

struct Hx710bDiagnostics {
    std::uint32_t successfulReads{0};
    std::uint32_t configurationErrorCount{0};
    std::uint32_t calibrationErrorCount{0};
    std::uint32_t notReadyCount{0};
    std::uint32_t timeoutCount{0};
    std::uint32_t readFailureCount{0};
    std::uint32_t outOfRangeCount{0};
    std::int32_t minimumRaw{0};
    std::int32_t maximumRaw{0};
    Hx710bStatus lastStatus{Hx710bStatus::NotInitialized};
    bool calibrationFactorValid{false};
    bool hasRawRange{false};
};

struct Hx710bSample {
    std::int32_t rawValue{0};
    float calibrationFactor{0.0F};
    float factorScaledRaw{0.0F};
    std::uint32_t calibrationVersion{0};
    std::uint32_t qualityFlags{0};
    bool calibrationFactorValid{false};
    bool factorScaledRawValid{false};
    bool valid{false};
};

class HX710BDriver final : public ISensor {
public:
    HX710BDriver(IHx710bTransport& transport,
                 const Hx710bCalibration& calibration) noexcept;

    project::models::SensorStatus begin() override;
    project::models::SensorStatus read(
        project::models::SensorFrame& output) override;
    project::models::SensorStatus selfTest() override;

    Hx710bStatus readSample(Hx710bSample& output) noexcept;
    Hx710bStatus lastStatus() const noexcept;
    Hx710bDiagnostics diagnostics() const noexcept;

    HX710BDriver(const HX710BDriver&) = delete;
    HX710BDriver& operator=(const HX710BDriver&) = delete;

private:
    void recordFailure(Hx710bStatus status) noexcept;
    project::models::SensorStatus makeSensorStatus(bool valid,
                                                   std::uint32_t qualityFlags = 0) const noexcept;

    IHx710bTransport& transport_;
    const Hx710bCalibration calibration_;
    Hx710bDiagnostics diagnostics_{};
    Hx710bStatus lastStatus_{Hx710bStatus::NotInitialized};
    bool initialized_{false};
};

}  // namespace project::drivers
