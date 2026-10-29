#pragma once

#include <cstdint>

#include "BSP/BoardInfo.h"
#include "BSP/BoardPins.h"

namespace project::bsp {

enum class PeripheralInterface : std::uint8_t {
    Unknown,
    I2c,
    Adc,
    Gpio,
    Spi,
    SdMmc
};

enum class I2cController : std::uint8_t {
    Unknown,
    Controller0,
    Controller1
};

enum class AdcAttenuation : std::uint8_t {
    Unknown
    // TODO(TBD): add framework-supported settings after board/ADC verification.
};

enum class LogicLevel : std::uint8_t {
    Unknown,
    Low,
    High
};

template <typename T>
struct ConfiguredValue {
    T value{};
    bool configured{false};
};

struct I2cConfiguration {
    bool selected{true};  // MPU9250 is specified to use I2C.
    I2cController controller{I2cController::Unknown};
    ConfiguredValue<std::uint32_t> clockHz{};
    ConfiguredValue<std::uint16_t> imuAddress{};
    bool electricalLevelsVerified{false};
    bool pullUpsVerified{false};
};

struct AdcConfiguration {
    ConfiguredValue<std::uint8_t> resolutionBits{};
    AdcAttenuation attenuation{AdcAttenuation::Unknown};
    bool attenuationConfigured{false};
    bool inputRangeVerified{false};
};

struct OutputElectricalConfiguration {
    LogicLevel activeLevel{LogicLevel::Unknown};
    LogicLevel safeLevel{LogicLevel::Unknown};
    bool reviewed{false};
};

struct BoardConfig {
    BoardInfo info{kBoardInfo};

    I2cConfiguration imuI2c{};
    AdcConfiguration soilAdc{};
    AdcConfiguration rainAdc{};
    PeripheralInterface storageInterface{PeripheralInterface::Unknown};

    OutputElectricalConfiguration ledOutput{};
    OutputElectricalConfiguration buzzerOutput{};
    OutputElectricalConfiguration relayOutput{};

    // Unknown hardware presence must not be interpreted as absent hardware.
    ConfiguredValue<bool> rtcPresent{};
    ConfiguredValue<bool> psramPresent{};
    ConfiguredValue<bool> powerMonitorPresent{};
};

inline constexpr BoardConfig kBoardConfig{};

enum class BoardConfigIssue : std::uint32_t {
    None = 0,
    BoardIdentityUnknown = 1U << 0U,
    BoardVersionUnknown = 1U << 1U,
    PinMapInvalid = 1U << 2U,
    I2cNotSelected = 1U << 3U,
    I2cControllerUnknown = 1U << 4U,
    I2cPinsMissing = 1U << 5U,
    I2cClockUnknown = 1U << 6U,
    I2cAddressUnknown = 1U << 7U,
    I2cElectricalReviewMissing = 1U << 8U
};

struct BoardConfigReport {
    std::uint32_t issues{0};

    constexpr bool isReadyForEarlyBoardSetup() const noexcept {
        constexpr std::uint32_t boardIssues =
            issueMask(BoardConfigIssue::BoardIdentityUnknown) |
            issueMask(BoardConfigIssue::BoardVersionUnknown) |
            issueMask(BoardConfigIssue::PinMapInvalid);
        return (issues & boardIssues) == 0;
    }

    constexpr bool isReadyForImuBus() const noexcept {
        return issues == 0;
    }
};

constexpr std::uint32_t issueMask(BoardConfigIssue issue) noexcept {
    return static_cast<std::uint32_t>(issue);
}

constexpr void addIssue(std::uint32_t& issues, BoardConfigIssue issue) noexcept {
    issues |= issueMask(issue);
}

constexpr BoardConfigReport validateBoardConfig(const BoardConfig& config,
                                                const BoardPins& pins) noexcept {
    BoardConfigReport report{};

    if (!config.info.boardIdKnown || config.info.boardId == nullptr ||
        config.info.boardId[0] == '\0' || !config.info.variantKnown ||
        config.info.variant == nullptr || config.info.variant[0] == '\0') {
        addIssue(report.issues, BoardConfigIssue::BoardIdentityUnknown);
    }
    if (!config.info.hardwareVersion.known) {
        addIssue(report.issues, BoardConfigIssue::BoardVersionUnknown);
    }
    if (!pinsDoNotConflict(pins)) {
        addIssue(report.issues, BoardConfigIssue::PinMapInvalid);
    }
    if (!config.imuI2c.selected) {
        addIssue(report.issues, BoardConfigIssue::I2cNotSelected);
    }
    if (config.imuI2c.controller == I2cController::Unknown) {
        addIssue(report.issues, BoardConfigIssue::I2cControllerUnknown);
    }
    if (!pins.imuI2c.sda.isConfigured() || !pins.imuI2c.scl.isConfigured()) {
        addIssue(report.issues, BoardConfigIssue::I2cPinsMissing);
    }
    if (!config.imuI2c.clockHz.configured ||
        config.imuI2c.clockHz.value == 0) {
        addIssue(report.issues, BoardConfigIssue::I2cClockUnknown);
    }
    if (!config.imuI2c.imuAddress.configured) {
        addIssue(report.issues, BoardConfigIssue::I2cAddressUnknown);
    }
    if (!config.imuI2c.electricalLevelsVerified ||
        !config.imuI2c.pullUpsVerified) {
        addIssue(report.issues, BoardConfigIssue::I2cElectricalReviewMissing);
    }
    return report;
}

BoardConfigReport activeBoardConfigReport() noexcept;

}  // namespace project::bsp
