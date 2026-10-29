#include "project/BSP/BoardSupportPackage.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#if defined(__has_include)
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif
#endif
#endif

#if defined(ARDUINO_ARCH_ESP32) && \
    (defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV))
#define PROJECT_BSP_ESP32S3_TARGET 1
#endif

namespace project::bsp {
namespace {

bool outputLineIsConfigured(GpioPin pin,
                            const OutputElectricalConfiguration& electrical) noexcept {
    return pin.isConfigured() && electrical.reviewed &&
           electrical.activeLevel != LogicLevel::Unknown &&
           electrical.safeLevel != LogicLevel::Unknown;
}

#if defined(PROJECT_BSP_ESP32S3_TARGET)
void configureOutputLine(GpioPin pin,
                         const OutputElectricalConfiguration& electrical) noexcept {
    const std::uint8_t safeLevel =
        electrical.safeLevel == LogicLevel::High ? HIGH : LOW;

    // Load the reviewed reset-safe level before enabling the output driver.
    digitalWrite(static_cast<std::uint8_t>(pin.number), safeLevel);
    pinMode(static_cast<std::uint8_t>(pin.number), OUTPUT);
}
#endif

}  // namespace

BspStatus BoardSupportPackage::initEarly() noexcept {
#if defined(PROJECT_BSP_ESP32S3_TARGET)
    const BoardConfigReport report = activeBoardConfigReport();
    if (!report.isReadyForEarlyBoardSetup()) {
        earlyInitialized_ = false;
        return BspStatus::NotConfigured;
    }

    earlyInitialized_ = true;
    return BspStatus::Ok;
#else
    earlyInitialized_ = false;
    return BspStatus::UnsupportedPlatform;
#endif
}

BspStatus BoardSupportPackage::configureOutputsSafe() noexcept {
    const bool ledConfigured = kBoardPins.alarm.led.isConfigured();
    const bool buzzerConfigured = kBoardPins.alarm.buzzer.isConfigured();
    const bool relayConfigured = kBoardPins.alarm.relay.isConfigured();

    if (!ledConfigured && !buzzerConfigured && !relayConfigured) {
        return BspStatus::NotConfigured;
    }
    if ((ledConfigured &&
         !outputLineIsConfigured(kBoardPins.alarm.led, kBoardConfig.ledOutput)) ||
        (buzzerConfigured &&
         !outputLineIsConfigured(kBoardPins.alarm.buzzer, kBoardConfig.buzzerOutput)) ||
        (relayConfigured &&
         !outputLineIsConfigured(kBoardPins.alarm.relay, kBoardConfig.relayOutput))) {
        return BspStatus::NotConfigured;
    }
#if defined(PROJECT_BSP_ESP32S3_TARGET)
    if (!earlyInitialized_) {
        return BspStatus::NotInitialized;
    }

    if (ledConfigured) {
        configureOutputLine(kBoardPins.alarm.led, kBoardConfig.ledOutput);
    }
    if (buzzerConfigured) {
        configureOutputLine(kBoardPins.alarm.buzzer, kBoardConfig.buzzerOutput);
    }
    if (relayConfigured) {
        configureOutputLine(kBoardPins.alarm.relay, kBoardConfig.relayOutput);
    }
    return BspStatus::Ok;
#else
    return BspStatus::UnsupportedPlatform;
#endif
}

BspStatus BoardSupportPackage::initBuses() noexcept {
    if (!earlyInitialized_) {
        return BspStatus::NotInitialized;
    }

    const BoardConfigReport report = activeBoardConfigReport();
    if (!report.isReadyForImuBus()) {
        return BspStatus::NotConfigured;
    }
#if defined(PROJECT_BSP_ESP32S3_TARGET)
    if (!kBoardConfig.imuI2c.selected) {
        return BspStatus::NotConfigured;
    }

    TwoWire* i2cBus = nullptr;
    switch (kBoardConfig.imuI2c.controller) {
    case I2cController::Controller0:
        i2cBus = &Wire;
        break;
    case I2cController::Controller1:
        i2cBus = &Wire1;
        break;
    case I2cController::Unknown:
        return BspStatus::NotConfigured;
    }
    if (i2cBus == nullptr) {
        return BspStatus::NotConfigured;
    }

    const bool started = i2cBus->begin(
        static_cast<int>(kBoardPins.imuI2c.sda.number),
        static_cast<int>(kBoardPins.imuI2c.scl.number),
        kBoardConfig.imuI2c.clockHz.value);
    if (!started) {
        return BspStatus::HardwareError;
    }

    return BspStatus::Ok;
#else
    return BspStatus::UnsupportedPlatform;
#endif
}

BspStatus BoardSupportPackage::resetReason(std::uint32_t& platformReason) const noexcept {
    platformReason = 0;
#if defined(PROJECT_BSP_ESP32S3_TARGET)
    platformReason = static_cast<std::uint32_t>(esp_reset_reason());
    return BspStatus::Ok;
#else
    return BspStatus::UnsupportedPlatform;
#endif
}

BspStatus BoardSupportPackage::monotonicMillis(std::uint64_t& uptimeMs) const noexcept {
    uptimeMs = 0;
    std::uint64_t uptimeUs = 0;
    const BspStatus status = monotonicMicros(uptimeUs);
    if (status != BspStatus::Ok) {
        return status;
    }
    uptimeMs = uptimeUs / 1000U;
    return BspStatus::Ok;
}

BspStatus BoardSupportPackage::monotonicMicros(std::uint64_t& uptimeUs) const noexcept {
    uptimeUs = 0;
#if defined(PROJECT_BSP_ESP32S3_TARGET)
    const std::int64_t elapsedUs = esp_timer_get_time();
    if (elapsedUs < 0) {
        return BspStatus::HardwareError;
    }
    uptimeUs = static_cast<std::uint64_t>(elapsedUs);
    return BspStatus::Ok;
#else
    return BspStatus::UnsupportedPlatform;
#endif
}

BspStatus BoardSupportPackage::boardIdentity(const BoardInfo*& info) const noexcept {
    info = nullptr;
    if (!boardIdentityIsKnown(kBoardConfig.info)) {
        return BspStatus::NotConfigured;
    }
    info = &kBoardConfig.info;
    return BspStatus::Ok;
}

BspStatus BoardSupportPackage::watchdogInit() noexcept {
    // TODO(TBD): bind to the framework/SoC watchdog only after task set,
    // measured deadlines, timeout, and feed policy are approved.
    return BspStatus::NotConfigured;
}

BspStatus BoardSupportPackage::watchdogFeed() noexcept {
    // TODO(TBD): only the reviewed WatchdogSupervisor may call the platform API.
    return BspStatus::NotConfigured;
}

BspStatus BoardSupportPackage::restart() noexcept {
#if defined(PROJECT_BSP_ESP32S3_TARGET)
    esp_restart();
    return BspStatus::HardwareError;
#else
    return BspStatus::UnsupportedPlatform;
#endif
}

BspStatus BoardSupportPackage::readMemoryMetrics(
    MemoryMetrics& metrics) const noexcept {
    metrics = {};
#if defined(PROJECT_BSP_ESP32S3_TARGET)
    constexpr std::uint32_t capabilities =
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    metrics.freeHeapBytes = static_cast<std::uint32_t>(
        heap_caps_get_free_size(capabilities));
    metrics.minimumFreeHeapBytes = static_cast<std::uint32_t>(
        heap_caps_get_minimum_free_size(capabilities));
    metrics.largestFreeBlockBytes = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(capabilities));
    metrics.internalHeapBytes = static_cast<std::uint32_t>(
        heap_caps_get_total_size(capabilities));
    metrics.valid = true;
    return BspStatus::Ok;
#else
    return BspStatus::UnsupportedPlatform;
#endif
}

BspStatus BoardSupportPackage::readPowerStatus(PowerStatus& status) const noexcept {
    status = {};
    // TODO(TBD): implement only if the confirmed board has a reviewed power monitor.
    return BspStatus::NotConfigured;
}

}  // namespace project::bsp
