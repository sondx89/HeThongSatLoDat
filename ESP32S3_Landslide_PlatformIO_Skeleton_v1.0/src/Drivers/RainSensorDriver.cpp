#include "project/Drivers/RainSensorDriver.h"

#include <algorithm>
#include <cmath>

#include <esp_timer.h>

namespace project::drivers {
namespace {

bool validPercent(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 100.0F;
}

}  // namespace

RainSensorDriver::RainSensorDriver(
    IAdcReader& adcReader,
    IRainDigitalReader& digitalReader,
    const project::bsp::BoardPins& boardPins,
    const RainSensorDriverConfig& config) noexcept
    : adcReader_(adcReader),
      digitalReader_(digitalReader),
      boardPins_(boardPins),
      config_(config) {}

RainSensorStatus RainSensorDriver::validateConfiguration() const noexcept {
    const auto& analogPin = boardPins_.adc.rainSensor;
    if (!analogPin.isConfigured() || !analogPin.isWellFormed() ||
        config_.adcMaximumCount == 0U ||
        config_.accumulationSamplePeriodMs <
            kMinimumAccumulationSamplePeriodMs ||
        static_cast<std::uint64_t>(config_.maximumSampleGapMs) <
            static_cast<std::uint64_t>(
                config_.accumulationSamplePeriodMs) +
                kAnalogBurstMinimumSpanMs ||
        config_.maximumSampleGapMs >= kAccumulationWindowMs ||
        config_.calibrationVersion == 0U ||
        !config_.adcConfigurationVerified ||
        !config_.wetnessSemanticsVerified ||
        !config_.calibrationConfigured) {
        return RainSensorStatus::InvalidConfiguration;
    }

    const auto& point0 = config_.calibrationPoints[0];
    const auto& point1 = config_.calibrationPoints[1];
    if (point0.adcCount > config_.adcMaximumCount ||
        point1.adcCount > config_.adcMaximumCount ||
        point0.adcCount == point1.adcCount ||
        !validPercent(point0.wetnessPercent) ||
        !validPercent(point1.wetnessPercent) ||
        !((point0.wetnessPercent == 0.0F &&
           point1.wetnessPercent == 100.0F) ||
          (point0.wetnessPercent == 100.0F &&
           point1.wetnessPercent == 0.0F))) {
        return RainSensorStatus::InvalidCalibration;
    }
    return RainSensorStatus::Ok;
}

project::models::SensorStatus RainSensorDriver::begin() {
    initialized_ = false;
    digitalReady_ = false;
    digitalConfigurationValid_ = !config_.digitalOutputEnabled;
    analogSampleCount_ = 0;
    analogSampleSum_ = 0;
    hasAnalogAttempt_ = false;
    hasAnalogSample_ = false;
    hasClockSample_ = false;
    lastAnalogTimestampMs_ = 0;
    lastAnalogAttemptMicros_ = 0;
    lastRawAdcCount_ = 0;
    lastFilteredAdcCount_ = 0.0F;
    lastWetnessPercent_ = 0.0F;
    lastAnalogQualityFlags_ = 0;
    lastAnalogError_ = RainSensorStatus::SamplePending;
    clearAccumulation();

    lastStatus_ = validateConfiguration();
    if (lastStatus_ != RainSensorStatus::Ok) {
        return makeSensorStatus(false, false, 0);
    }

    initialized_ = true;
    if (config_.digitalOutputEnabled) {
        const auto& digitalPin = boardPins_.digitalSensors.rainSensor;
        digitalConfigurationValid_ =
            digitalPin.isConfigured() && digitalPin.isWellFormed() &&
            digitalPin.number != boardPins_.adc.rainSensor.number &&
            config_.digitalActiveLowVerified;
        if (digitalConfigurationValid_) {
            const auto pin =
                static_cast<std::uint8_t>(digitalPin.number);
            digitalReady_ = digitalReader_.begin(pin);
        }
        if (!digitalConfigurationValid_ || !digitalReady_) {
            lastStatus_ = RainSensorStatus::DigitalReadFailed;
        }
    }
    return makeSensorStatus(true, !digitalReady_ &&
                                      config_.digitalOutputEnabled,
                            0);
}

project::models::SensorStatus RainSensorDriver::read(
    project::models::SensorFrame& output) {
    output = {};
    RainSensorSample sample{};
    lastStatus_ = readSample(sample);

    output.rainRaw = sample.rawAdcCount;
    output.rainWetnessRelativePct = sample.wetnessRelativePercent;
    output.rainWetnessValid = sample.valid;
    output.rainDigitalWet = sample.digitalWet;
    output.rainDigitalValid = sample.digitalValid;
    output.rainAccEquivalentWetSeconds =
        sample.rainAccEquivalentWetSeconds;
    output.rainAccCoverageMs = sample.rainAccCoverageMs;
    output.rainAccValid = sample.rainAccValid;
    output.rainAoStatus = sample.analogStatus;
    output.rainDoStatus = sample.digitalStatus;
    output.rainAccumulationStatus = sample.accumulationStatus;
    if (sample.calibrationVersion != 0U) {
        output.calibrationVersion = sample.calibrationVersion;
    }

    const bool anyValid = sample.valid || sample.digitalValid ||
                          sample.rainAccValid;
    const bool degraded =
        (sample.analogStatus.health != project::models::SensorHealth::Healthy) ||
        (config_.digitalOutputEnabled &&
         sample.digitalStatus.health !=
             project::models::SensorHealth::Healthy) ||
        !sample.rainAccValid;
    output.rainStatus =
        makeSensorStatus(anyValid, degraded, sample.timestampMs,
                         sample.qualityFlags);
    return output.rainStatus;
}

project::models::SensorStatus RainSensorDriver::selfTest() {
    if (!initialized_) {
        lastStatus_ = RainSensorStatus::NotInitialized;
        return makeSensorStatus(false, false, 0);
    }

    std::int32_t reading = 0;
    const auto analogPin =
        static_cast<std::uint8_t>(boardPins_.adc.rainSensor.number);
    const bool analogOk =
        adcReader_.readRaw(analogPin, reading) && reading >= 0 &&
        static_cast<std::uint32_t>(reading) <= config_.adcMaximumCount;

    bool digitalOk = false;
    if (config_.digitalOutputEnabled && digitalConfigurationValid_ &&
        digitalReady_) {
        bool levelHigh = false;
        digitalOk = digitalReader_.readLevel(
            static_cast<std::uint8_t>(
                boardPins_.digitalSensors.rainSensor.number),
            levelHigh);
    }

    const bool valid = analogOk || digitalOk;
    lastStatus_ = valid ? RainSensorStatus::Ok
                        : (config_.digitalOutputEnabled
                               ? RainSensorStatus::DigitalReadFailed
                               : RainSensorStatus::AdcReadFailed);
    return makeSensorStatus(valid, !analogOk ||
                                       (config_.digitalOutputEnabled &&
                                        !digitalOk),
                            static_cast<std::uint64_t>(
                                std::max<std::int64_t>(0, esp_timer_get_time()) /
                                1000));
}

RainSensorStatus RainSensorDriver::readSample(
    RainSensorSample& output) noexcept {
    output = {};
    if (!initialized_) {
        lastStatus_ = RainSensorStatus::NotInitialized;
        return lastStatus_;
    }

    const std::int64_t timerMicros = esp_timer_get_time();
    if (timerMicros < 0) {
        lastStatus_ = RainSensorStatus::ClockError;
        return lastStatus_;
    }
    const auto nowMicros = static_cast<std::uint64_t>(timerMicros);
    const auto nowMs = nowMicros / 1000U;
    if (hasClockSample_ && nowMicros < lastClockMicros_) {
        clearAnalogBurst();
        clearAccumulation();
        hasAnalogSample_ = false;
        hasAnalogAttempt_ = false;
        lastAnalogAttemptMicros_ = 0;
        lastAnalogTimestampMs_ = 0;
        lastAnalogError_ = RainSensorStatus::ClockError;
        lastStatus_ = RainSensorStatus::ClockError;
        lastClockMicros_ = nowMicros;
        output.timestampMs = nowMs;
        output.analogStatus = makeSensorStatus(
            false, true, nowMs);
        output.accumulationStatus = makeSensorStatus(
            false, true, nowMs,
            rainQualityMask(RainQualityFlag::AccumulationGapDetected));
        return lastStatus_;
    }
    lastClockMicros_ = nowMicros;
    hasClockSample_ = true;
    output.timestampMs = nowMs;

    if (config_.digitalOutputEnabled && digitalReady_) {
        bool levelHigh = false;
        const auto pin = static_cast<std::uint8_t>(
            boardPins_.digitalSensors.rainSensor.number);
        if (digitalReader_.readLevel(pin, levelHigh)) {
            output.digitalWet = !levelHigh;
            output.digitalValid = true;
            output.qualityFlags |=
                rainQualityMask(RainQualityFlag::DigitalLevelValid);
            output.digitalStatus =
                makeSensorStatus(true, false, nowMs);
            output.digitalStatus.qualityFlags =
                rainQualityMask(RainQualityFlag::DigitalLevelValid);
        } else {
            output.digitalStatus =
                makeSensorStatus(false, true, nowMs);
            output.digitalStatus.qualityFlags =
                rainQualityMask(RainQualityFlag::DigitalReadFailure);
            output.qualityFlags |=
                rainQualityMask(RainQualityFlag::DigitalReadFailure);
        }
    } else if (config_.digitalOutputEnabled) {
        output.digitalStatus =
            makeSensorStatus(false, true, nowMs);
        output.digitalStatus.qualityFlags =
            rainQualityMask(RainQualityFlag::DigitalReadFailure);
        output.qualityFlags |=
            rainQualityMask(RainQualityFlag::DigitalReadFailure);
    }

    const std::uint64_t analogPin =
        static_cast<std::uint64_t>(boardPins_.adc.rainSensor.number);
    const bool mayStartBurst =
        analogSampleCount_ != 0U ||
        !hasAnalogSample_ ||
        nowMs - lastAnalogTimestampMs_ >=
            config_.accumulationSamplePeriodMs;
    const bool spacingElapsed =
        !hasAnalogAttempt_ ||
        (nowMicros >= lastAnalogAttemptMicros_ &&
         nowMicros - lastAnalogAttemptMicros_ >=
             kMinimumAnalogSampleSpacingUs);
    if (mayStartBurst && spacingElapsed) {
        std::int32_t rawAdcCount = 0;
        hasAnalogAttempt_ = true;
        lastAnalogAttemptMicros_ = nowMicros;
        const bool readOk = adcReader_.readRaw(
            static_cast<std::uint8_t>(analogPin), rawAdcCount);
        if (!readOk) {
            clearAnalogBurst();
            lastAnalogError_ = RainSensorStatus::AdcReadFailed;
        } else if (rawAdcCount < 0 ||
                   static_cast<std::uint32_t>(rawAdcCount) >
                       config_.adcMaximumCount) {
            clearAnalogBurst();
            lastAnalogError_ = RainSensorStatus::AdcValueOutOfRange;
        } else {
            if (analogSampleCount_ == 0U) {
                lastAnalogError_ = RainSensorStatus::SamplePending;
            }
            ++analogSampleCount_;
            analogSampleSum_ += static_cast<std::uint32_t>(rawAdcCount);
            if (analogSampleCount_ == kAnalogSamplesPerReading) {
                const float filtered =
                    static_cast<float>(analogSampleSum_) /
                    static_cast<float>(kAnalogSamplesPerReading);
                std::uint32_t analogQuality =
                    rainQualityMask(RainQualityFlag::AdcRangeValid) |
                    rainQualityMask(RainQualityFlag::CalibrationValid);
                const float wetness =
                    mapAdcToWetness(filtered, analogQuality);
                if (!std::isfinite(wetness)) {
                    clearAnalogBurst();
                    lastAnalogError_ = RainSensorStatus::InvalidCalibration;
                } else {
                    lastRawAdcCount_ = rawAdcCount;
                    lastFilteredAdcCount_ = filtered;
                    lastWetnessPercent_ = wetness;
                    lastAnalogQualityFlags_ = analogQuality;
                    lastAnalogTimestampMs_ = nowMs;
                    hasAnalogSample_ = true;
                    lastAnalogError_ = RainSensorStatus::Ok;
                    addAccumulationPoint(nowMs, wetness);
                    output.qualityFlags |= analogQuality;
                    clearAnalogBurst();
                }
            }
        }
    }

    output.filterSampleCount = static_cast<std::uint8_t>(
        analogSampleCount_ != 0U
            ? analogSampleCount_
            : (hasAnalogSample_ &&
                       lastAnalogError_ == RainSensorStatus::Ok
                   ? kAnalogSamplesPerReading
                   : 0U));
    if (analogSampleCount_ != 0U) {
        output.qualityFlags |=
            rainQualityMask(RainQualityFlag::FilterWindowWarming);
    }
    output.rawAdcCount = hasAnalogSample_ ? lastRawAdcCount_ : 0;
    output.filteredAdcCount =
        hasAnalogSample_ ? lastFilteredAdcCount_ : 0.0F;
    output.wetnessRelativePercent =
        hasAnalogSample_ ? lastWetnessPercent_ : 0.0F;
    output.calibrationVersion = hasAnalogSample_
                                    ? config_.calibrationVersion
                                    : 0U;
    output.valid =
        hasAnalogSample_ && lastAnalogError_ == RainSensorStatus::Ok &&
        nowMs >= lastAnalogTimestampMs_ &&
        nowMs - lastAnalogTimestampMs_ <= config_.maximumSampleGapMs;
    if (output.valid) {
        output.qualityFlags |= lastAnalogQualityFlags_;
        output.analogStatus =
            makeSensorStatus(true, false, lastAnalogTimestampMs_,
                             lastAnalogQualityFlags_);
    } else {
        const bool stale =
            hasAnalogSample_ && nowMs >= lastAnalogTimestampMs_ &&
            nowMs - lastAnalogTimestampMs_ > config_.maximumSampleGapMs;
        const auto status = stale ? RainSensorStatus::Stale
                                  : lastAnalogError_;
        output.analogStatus = makeSensorStatus(
            false, true, hasAnalogSample_ ? lastAnalogTimestampMs_ : nowMs);
        if (status == RainSensorStatus::SamplePending) {
            output.analogStatus.qualityFlags =
                rainQualityMask(RainQualityFlag::FilterWindowWarming);
            output.qualityFlags |=
                rainQualityMask(RainQualityFlag::FilterWindowWarming);
        } else if (status == RainSensorStatus::AdcReadFailed) {
            output.analogStatus.qualityFlags =
                rainQualityMask(RainQualityFlag::AdcReadFailure);
            output.qualityFlags |=
                rainQualityMask(RainQualityFlag::AdcReadFailure);
        } else if (status == RainSensorStatus::AdcValueOutOfRange) {
            output.analogStatus.qualityFlags =
                rainQualityMask(RainQualityFlag::AdcValueOutOfRange);
            output.qualityFlags |=
                rainQualityMask(RainQualityFlag::AdcValueOutOfRange);
        } else if (status == RainSensorStatus::Stale) {
            output.analogStatus.qualityFlags =
                rainQualityMask(RainQualityFlag::AnalogStale);
            output.qualityFlags |=
                rainQualityMask(RainQualityFlag::AnalogStale);
        }
    }

    updateAccumulation(nowMs, output);
    if (output.rainAccValid) {
        output.qualityFlags |=
            rainQualityMask(RainQualityFlag::AccumulationValid);
    }

    if (output.valid || output.digitalValid || output.rainAccValid) {
        lastStatus_ = RainSensorStatus::Ok;
    } else if (lastAnalogError_ != RainSensorStatus::SamplePending) {
        lastStatus_ = lastAnalogError_;
    } else if (config_.digitalOutputEnabled &&
               !output.digitalValid) {
        lastStatus_ = RainSensorStatus::DigitalReadFailed;
    } else {
        lastStatus_ = RainSensorStatus::SamplePending;
    }
    return lastStatus_;
}

float RainSensorDriver::mapAdcToWetness(
    const float adcCount,
    std::uint32_t& qualityFlags) const noexcept {
    const auto& point0 = config_.calibrationPoints[0];
    const auto& point1 = config_.calibrationPoints[1];
    const float adc0 = static_cast<float>(point0.adcCount);
    const float adc1 = static_cast<float>(point1.adcCount);
    const float fraction = (adcCount - adc0) / (adc1 - adc0);
    const float mapped = point0.wetnessPercent +
                         fraction * (point1.wetnessPercent -
                                     point0.wetnessPercent);
    const float minWetness =
        std::min(point0.wetnessPercent, point1.wetnessPercent);
    const float maxWetness =
        std::max(point0.wetnessPercent, point1.wetnessPercent);
    if (mapped < minWetness) {
        qualityFlags |= rainQualityMask(RainQualityFlag::WetnessClampedLow);
        return minWetness;
    }
    if (mapped > maxWetness) {
        qualityFlags |= rainQualityMask(RainQualityFlag::WetnessClampedHigh);
        return maxWetness;
    }
    return mapped;
}

void RainSensorDriver::clearAnalogBurst() noexcept {
    analogSampleCount_ = 0;
    analogSampleSum_ = 0;
}

void RainSensorDriver::clearAccumulation() noexcept {
    accumulation_ = {};
    accumulationCount_ = 0;
    hasAccumulationSample_ = false;
    lastAccumulationTimestampMs_ = 0;
}

void RainSensorDriver::addAccumulationPoint(
    const std::uint64_t timestampMs,
    const float wetnessPercent) noexcept {
    if (hasAccumulationSample_) {
        if (timestampMs <= lastAccumulationTimestampMs_ ||
            timestampMs - lastAccumulationTimestampMs_ >
                config_.maximumSampleGapMs) {
            clearAccumulation();
        }
    }
    if (accumulationCount_ == kMaxAccumulationSamples) {
        for (std::size_t i = 1; i < accumulationCount_; ++i) {
            accumulation_[i - 1U] = accumulation_[i];
        }
        --accumulationCount_;
    }
    accumulation_[accumulationCount_++] =
        AccumulationPoint{timestampMs, wetnessPercent};
    lastAccumulationTimestampMs_ = timestampMs;
    hasAccumulationSample_ = true;
}

void RainSensorDriver::updateAccumulation(
    const std::uint64_t nowMs,
    RainSensorSample& output) noexcept {
    if (!hasAccumulationSample_ || accumulationCount_ == 0U ||
        nowMs < accumulation_[accumulationCount_ - 1U].timestampMs) {
        output.accumulationStatus =
            makeSensorStatus(false, true, nowMs,
                             rainQualityMask(
                                 RainQualityFlag::AccumulationWindowWarming));
        return;
    }

    const auto& latest = accumulation_[accumulationCount_ - 1U];
    const std::uint64_t latestAgeMs = nowMs - latest.timestampMs;
    const std::uint64_t windowStartMs =
        nowMs >= kAccumulationWindowMs
            ? nowMs - kAccumulationWindowMs
            : 0U;
    output.rainAccCoverageMs = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(kAccumulationWindowMs,
                                nowMs - accumulation_[0].timestampMs));
    if (latestAgeMs > config_.maximumSampleGapMs) {
        output.accumulationStatus =
            makeSensorStatus(false, true, latest.timestampMs,
                             rainQualityMask(
                                 RainQualityFlag::AccumulationGapDetected));
        return;
    }
    if (nowMs < kAccumulationWindowMs ||
        accumulation_[0].timestampMs > windowStartMs ||
        output.rainAccCoverageMs < kAccumulationWindowMs) {
        output.accumulationStatus =
            makeSensorStatus(false, true, latest.timestampMs,
                             rainQualityMask(
                                 RainQualityFlag::AccumulationWindowWarming));
        return;
    }

    double areaWetMs = 0.0;
    for (std::size_t i = 0; i < accumulationCount_; ++i) {
        const auto segmentStart = accumulation_[i].timestampMs;
        const auto segmentEnd =
            i + 1U < accumulationCount_
                ? accumulation_[i + 1U].timestampMs
                : nowMs;
        if (segmentEnd < segmentStart ||
            segmentEnd - segmentStart > config_.maximumSampleGapMs) {
            output.accumulationStatus =
                makeSensorStatus(false, true, latest.timestampMs,
                                 rainQualityMask(
                                     RainQualityFlag::AccumulationGapDetected));
            return;
        }
        const auto clippedStart = std::max(segmentStart, windowStartMs);
        const auto clippedEnd = std::min(segmentEnd, nowMs);
        if (clippedEnd > clippedStart) {
            areaWetMs +=
                static_cast<double>(accumulation_[i].wetnessPercent) /
                100.0 * static_cast<double>(clippedEnd - clippedStart);
        }
    }

    output.rainAccEquivalentWetSeconds =
        static_cast<float>(areaWetMs / 1000.0);
    output.rainAccValid =
        std::isfinite(output.rainAccEquivalentWetSeconds);
    output.accumulationStatus = makeSensorStatus(
        output.rainAccValid, !output.rainAccValid, nowMs,
        output.rainAccValid
            ? rainQualityMask(RainQualityFlag::AccumulationValid)
            : 0U);
}

project::models::SensorStatus RainSensorDriver::makeSensorStatus(
    const bool valid,
    const bool degraded,
    const std::uint64_t timestampMs,
    const std::uint32_t qualityFlags) const noexcept {
    project::models::SensorStatus status{};
    status.valid = valid;
    status.health = valid
                        ? (degraded
                               ? project::models::SensorHealth::Degraded
                               : project::models::SensorHealth::Healthy)
                        : (degraded
                               ? project::models::SensorHealth::Degraded
                               : project::models::SensorHealth::Fault);
    status.qualityFlags = qualityFlags;
    status.timestampMs = timestampMs;
    return status;
}

RainSensorStatus RainSensorDriver::lastStatus() const noexcept {
    return lastStatus_;
}

}  // namespace project::drivers
