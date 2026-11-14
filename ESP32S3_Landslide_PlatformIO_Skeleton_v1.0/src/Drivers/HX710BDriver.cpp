#include "project/Drivers/HX710BDriver.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <Arduino.h>

namespace project::drivers {
namespace {

bool validProtocol(const Hx710bProtocolConfig& protocol,
                   const project::bsp::BoardPins& pins) noexcept {
    const std::uint32_t clockHalfPeriodUs =
        protocol.clockHalfPeriodUs;
    const std::uint32_t conversionOverrunBoundUs =
        2U * clockHalfPeriodUs;
    const std::uint32_t minimumConversionTimeUs =
        2U * (static_cast<std::uint32_t>(protocol.dataBitCount) +
              static_cast<std::uint32_t>(
                  protocol.trailingClockPulses)) *
        clockHalfPeriodUs;
    return pins.hx710.clock.isConfigured() &&
           pins.hx710.clock.isWellFormed() &&
           pins.hx710.data.isConfigured() &&
           pins.hx710.data.isWellFormed() &&
           pins.hx710.clock.number != pins.hx710.data.number &&
           protocol.readyTimeoutUs != 0U &&
           protocol.readyTimeoutUs <= kHx710bMaximumReadyTimeoutUs &&
           protocol.conversionTimeoutUs != 0U &&
           protocol.clockHalfPeriodUs != 0U &&
           protocol.dataBitCount == kHx710bDataBitCount &&
           protocol.trailingClockPulses == kHx710bTrailingClockPulses &&
           minimumConversionTimeUs <=
               kHx710bMaximumReadyTimeoutUs -
                   conversionOverrunBoundUs &&
           protocol.conversionTimeoutUs >= minimumConversionTimeUs &&
           protocol.conversionTimeoutUs <=
               kHx710bMaximumReadyTimeoutUs -
                   conversionOverrunBoundUs &&
           protocol.signedOutput &&
           protocol.verifiedAgainstDatasheet;
}

void incrementSaturated(std::uint32_t& value) noexcept {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

ArduinoHx710bTransport::ArduinoHx710bTransport(
    const project::bsp::BoardPins& boardPins,
    const Hx710bProtocolConfig& protocol) noexcept
    : boardPins_(boardPins), protocol_(protocol) {}

Hx710bTransportStatus ArduinoHx710bTransport::begin() noexcept {
    initialized_ = false;
    if (!validProtocol(protocol_, boardPins_)) {
        return Hx710bTransportStatus::InvalidConfiguration;
    }

    pinMode(static_cast<std::uint8_t>(boardPins_.hx710.clock.number),
            OUTPUT);
    pinMode(static_cast<std::uint8_t>(boardPins_.hx710.data.number), INPUT);
    digitalWrite(static_cast<std::uint8_t>(boardPins_.hx710.clock.number),
                 LOW);
    initialized_ = true;
    return Hx710bTransportStatus::Ok;
}

Hx710bTransportStatus ArduinoHx710bTransport::readRaw(
    std::int32_t& rawValue) noexcept {
    rawValue = 0;
    if (!initialized_) {
        return Hx710bTransportStatus::NotInitialized;
    }

    const auto clockPin =
        static_cast<std::uint8_t>(boardPins_.hx710.clock.number);
    const auto dataPin =
        static_cast<std::uint8_t>(boardPins_.hx710.data.number);
    const std::uint32_t waitStartedUs = micros();
    while ((digitalRead(dataPin) == HIGH) != protocol_.readyLevelHigh) {
        if (static_cast<std::uint32_t>(micros() - waitStartedUs) >=
            protocol_.readyTimeoutUs) {
            return Hx710bTransportStatus::NotReady;
        }
        yield();
    }

    const std::uint32_t conversionStartedUs = micros();
    std::uint32_t bits = 0;
    for (std::uint8_t bit = 0; bit < protocol_.dataBitCount; ++bit) {
        digitalWrite(clockPin, HIGH);
        delayMicroseconds(protocol_.clockHalfPeriodUs);
        if (static_cast<std::uint32_t>(micros() - conversionStartedUs) >
            protocol_.conversionTimeoutUs) {
            digitalWrite(clockPin, LOW);
            return Hx710bTransportStatus::Timeout;
        }
        bits = static_cast<std::uint32_t>(bits << 1U);
        if (digitalRead(dataPin) == HIGH) {
            bits |= 1U;
        }
        digitalWrite(clockPin, LOW);
        delayMicroseconds(protocol_.clockHalfPeriodUs);
        if (static_cast<std::uint32_t>(micros() - conversionStartedUs) >
            protocol_.conversionTimeoutUs) {
            return Hx710bTransportStatus::Timeout;
        }
    }

    for (std::uint8_t pulse = 0;
         pulse < protocol_.trailingClockPulses;
         ++pulse) {
        digitalWrite(clockPin, HIGH);
        delayMicroseconds(protocol_.clockHalfPeriodUs);
        digitalWrite(clockPin, LOW);
        delayMicroseconds(protocol_.clockHalfPeriodUs);
        if (static_cast<std::uint32_t>(micros() - conversionStartedUs) >
            protocol_.conversionTimeoutUs) {
            return Hx710bTransportStatus::Timeout;
        }
    }

    if (protocol_.signedOutput) {
        const std::uint32_t signBit =
            static_cast<std::uint32_t>(1UL) << (protocol_.dataBitCount - 1U);
        std::int64_t signedValue = static_cast<std::int64_t>(bits);
        if ((bits & signBit) != 0U) {
            signedValue -= static_cast<std::int64_t>(1ULL
                                                     << protocol_.dataBitCount);
        }
        if (signedValue < std::numeric_limits<std::int32_t>::min() ||
            signedValue > std::numeric_limits<std::int32_t>::max()) {
            return Hx710bTransportStatus::RawOutOfRange;
        }
        rawValue = static_cast<std::int32_t>(signedValue);
    } else {
        if (bits >
            static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
            return Hx710bTransportStatus::RawOutOfRange;
        }
        rawValue = static_cast<std::int32_t>(bits);
    }
    return Hx710bTransportStatus::Ok;
}

HX710BDriver::HX710BDriver(
    IHx710bTransport& transport,
    const Hx710bCalibration& calibration) noexcept
    : transport_(transport), calibration_(calibration) {}

project::models::SensorStatus HX710BDriver::begin() {
    initialized_ = false;
    diagnostics_.calibrationFactorValid = false;
    const Hx710bTransportStatus result = transport_.begin();
    if (result != Hx710bTransportStatus::Ok) {
        switch (result) {
            case Hx710bTransportStatus::InvalidConfiguration:
                lastStatus_ = Hx710bStatus::InvalidConfiguration;
                break;
            case Hx710bTransportStatus::NotInitialized:
                lastStatus_ = Hx710bStatus::NotInitialized;
                break;
            case Hx710bTransportStatus::NotReady:
                lastStatus_ = Hx710bStatus::NotReady;
                break;
            case Hx710bTransportStatus::Timeout:
                lastStatus_ = Hx710bStatus::Timeout;
                break;
            case Hx710bTransportStatus::ReadFailure:
                lastStatus_ = Hx710bStatus::ReadFailure;
                break;
            case Hx710bTransportStatus::RawOutOfRange:
                lastStatus_ = Hx710bStatus::RawOutOfRange;
                break;
            case Hx710bTransportStatus::Ok:
                break;
        }
        recordFailure(lastStatus_);
        return makeSensorStatus(false);
    }

    initialized_ = true;
    diagnostics_.calibrationFactorValid =
        calibration_.configured && std::isfinite(calibration_.factor) &&
        calibration_.factor != 0.0F && calibration_.version != 0U;
    if (calibration_.configured && !diagnostics_.calibrationFactorValid) {
        incrementSaturated(diagnostics_.calibrationErrorCount);
    }
    if (calibration_.pressureCalibrationConfigured &&
        !isApprovedHx710bPressureCalibration(calibration_)) {
        incrementSaturated(diagnostics_.calibrationErrorCount);
    }
    lastStatus_ = Hx710bStatus::Ok;
    diagnostics_.lastStatus = lastStatus_;
    return makeSensorStatus(true);
}

project::models::SensorStatus HX710BDriver::read(
    project::models::SensorFrame& output) {
    output = {};
    Hx710bSample sample{};
    lastStatus_ = readSample(sample);
    if (lastStatus_ != Hx710bStatus::Ok) {
        output.hx710bStatus =
            makeSensorStatus(false, output.hx710bStatus.qualityFlags);
        return output.hx710bStatus;
    }

    output.hx710bRaw = sample.rawValue;
    output.hx710bRawSampleCount = sample.sampleCount;
    output.hx710bTransducerIdentityId =
        sample.transducerIdentityId;
    output.hx710bCalibrationVersion =
        sample.pressureCalibrationVersion;
    output.hx710bCalibrationFactor = sample.calibrationFactor;
    output.hx710bCalibrationFactorValid =
        sample.calibrationFactorValid;
    output.hx710bFactorScaledRaw = sample.factorScaledRaw;
    output.hx710bFactorScaledRawValid = sample.factorScaledRawValid;
    output.hx710bStatus = makeSensorStatus(true, sample.qualityFlags);
    return output.hx710bStatus;
}

project::models::SensorStatus HX710BDriver::selfTest() {
    Hx710bSample sample{};
    const Hx710bStatus status = readSample(sample);
    return makeSensorStatus(status == Hx710bStatus::Ok,
                            sample.qualityFlags);
}

Hx710bStatus HX710BDriver::readSample(Hx710bSample& output) noexcept {
    output = {};
    if (!initialized_) {
        lastStatus_ = Hx710bStatus::NotInitialized;
        recordFailure(lastStatus_);
        return lastStatus_;
    }

    std::int64_t rawSum = 0;
    std::uint8_t sampleCount = 0;
    Hx710bStatus mostRecentFailure = Hx710bStatus::ReadFailure;
    for (std::size_t attempt = 0; attempt < kMaximumReadAttempts;
         ++attempt) {
        std::int32_t rawValue = 0;
        const Hx710bTransportStatus transportStatus =
            transport_.readRaw(rawValue);
        Hx710bStatus readStatus = Hx710bStatus::Ok;
        switch (transportStatus) {
            case Hx710bTransportStatus::Ok:
                if (rawValue < kHx710bSigned24Minimum ||
                    rawValue > kHx710bSigned24Maximum) {
                    readStatus = Hx710bStatus::RawOutOfRange;
                    break;
                }
                rawSum += rawValue;
                ++sampleCount;
                incrementSaturated(diagnostics_.successfulReads);
                if (!diagnostics_.hasRawRange) {
                    diagnostics_.minimumRaw = rawValue;
                    diagnostics_.maximumRaw = rawValue;
                    diagnostics_.hasRawRange = true;
                } else {
                    diagnostics_.minimumRaw =
                        std::min(diagnostics_.minimumRaw, rawValue);
                    diagnostics_.maximumRaw =
                        std::max(diagnostics_.maximumRaw, rawValue);
                }
                continue;
            case Hx710bTransportStatus::InvalidConfiguration:
                readStatus = Hx710bStatus::InvalidConfiguration;
                break;
            case Hx710bTransportStatus::NotInitialized:
                readStatus = Hx710bStatus::NotInitialized;
                break;
            case Hx710bTransportStatus::NotReady:
                readStatus = Hx710bStatus::NotReady;
                break;
            case Hx710bTransportStatus::Timeout:
                readStatus = Hx710bStatus::Timeout;
                break;
            case Hx710bTransportStatus::ReadFailure:
                readStatus = Hx710bStatus::ReadFailure;
                break;
            case Hx710bTransportStatus::RawOutOfRange:
                readStatus = Hx710bStatus::RawOutOfRange;
                break;
        }
        mostRecentFailure = readStatus;
        recordFailure(readStatus);
    }

    if (sampleCount == 0U) {
        lastStatus_ = mostRecentFailure;
        diagnostics_.lastStatus = lastStatus_;
        return lastStatus_;
    }

    const std::int64_t roundingOffset =
        rawSum >= 0 ? static_cast<std::int64_t>(sampleCount / 2U)
                    : -static_cast<std::int64_t>(sampleCount / 2U);
    output.rawValue = static_cast<std::int32_t>(
        (rawSum + roundingOffset) / static_cast<std::int64_t>(sampleCount));
    output.sampleCount = sampleCount;
    output.qualityFlags = hx710bQualityMask(Hx710bQualityFlag::RawValid);
    if (sampleCount < kMaximumReadAttempts) {
        output.qualityFlags |=
            hx710bQualityMask(Hx710bQualityFlag::PartialSampleCount);
    }
    output.valid = true;
    if (isApprovedHx710bPressureCalibration(calibration_)) {
        output.transducerIdentityId =
            calibration_.transducerIdentityId;
        output.pressureCalibrationVersion = calibration_.version;
        output.qualityFlags |= hx710bQualityMask(
            Hx710bQualityFlag::PressureCalibrationApproved);
    } else {
        output.qualityFlags |= hx710bQualityMask(
            Hx710bQualityFlag::PressureCalibrationUnavailable);
    }
    if (diagnostics_.calibrationFactorValid) {
        output.calibrationFactor = calibration_.factor;
        output.calibrationVersion = calibration_.version;
        output.calibrationFactorValid = true;
        output.qualityFlags |=
            hx710bQualityMask(Hx710bQualityFlag::CalibrationFactorValid);
        const double scaled =
            static_cast<double>(output.rawValue) * calibration_.factor;
        if (std::isfinite(scaled) &&
            scaled <= std::numeric_limits<float>::max() &&
            scaled >= -std::numeric_limits<float>::max()) {
            output.factorScaledRaw = static_cast<float>(scaled);
            output.factorScaledRawValid = true;
            output.qualityFlags |=
                hx710bQualityMask(Hx710bQualityFlag::FactorScaledRawValid);
        } else {
            output.qualityFlags |=
                hx710bQualityMask(Hx710bQualityFlag::FactorScaledRawOverflow);
            incrementSaturated(diagnostics_.calibrationErrorCount);
        }
    } else {
        output.qualityFlags |= hx710bQualityMask(
            calibration_.configured ? Hx710bQualityFlag::CalibrationInvalid
                                    : Hx710bQualityFlag::CalibrationUnavailable);
    }

    lastStatus_ = Hx710bStatus::Ok;
    diagnostics_.lastStatus = lastStatus_;
    return lastStatus_;
}

void HX710BDriver::recordFailure(const Hx710bStatus status) noexcept {
    switch (status) {
        case Hx710bStatus::InvalidConfiguration:
            incrementSaturated(diagnostics_.configurationErrorCount);
            break;
        case Hx710bStatus::NotReady:
            incrementSaturated(diagnostics_.notReadyCount);
            break;
        case Hx710bStatus::Timeout:
            incrementSaturated(diagnostics_.timeoutCount);
            break;
        case Hx710bStatus::ReadFailure:
            incrementSaturated(diagnostics_.readFailureCount);
            break;
        case Hx710bStatus::RawOutOfRange:
            incrementSaturated(diagnostics_.outOfRangeCount);
            break;
        case Hx710bStatus::Ok:
        case Hx710bStatus::NotInitialized:
            break;
    }
    lastStatus_ = status;
    diagnostics_.lastStatus = status;
}

project::models::SensorStatus HX710BDriver::makeSensorStatus(
    const bool valid,
    const std::uint32_t qualityFlags) const noexcept {
    project::models::SensorStatus status{};
    status.valid = valid;
    status.health =
        !valid ? project::models::SensorHealth::Fault
               : ((qualityFlags & hx710bQualityMask(
                                      Hx710bQualityFlag::PartialSampleCount)) !=
                          0U
                      ? project::models::SensorHealth::Degraded
                      : project::models::SensorHealth::Healthy);
    status.qualityFlags = qualityFlags;
    return status;
}

Hx710bStatus HX710BDriver::lastStatus() const noexcept {
    return lastStatus_;
}

Hx710bDiagnostics HX710BDriver::diagnostics() const noexcept {
    return diagnostics_;
}

}  // namespace project::drivers
