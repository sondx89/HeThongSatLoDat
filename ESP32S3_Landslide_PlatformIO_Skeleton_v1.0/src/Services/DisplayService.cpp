#include "project/Services/DisplayService.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace project::services {
namespace {

const char* riskLabel(project::models::RiskState risk) noexcept {
    using project::models::RiskState;
    switch (risk) {
    case RiskState::Startup:
        return "STARTUP";
    case RiskState::Normal:
        return "SAFE";
    case RiskState::Monitor:
        return "WATCH";
    case RiskState::DangerTest:
        return "DANGER";
    case RiskState::Unknown:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace

DisplayService::DisplayService(
    project::drivers::SSD1306DisplayDriver& driver,
    const DisplayServiceConfig& config) noexcept
    : driver_(driver), config_(config) {}

DisplayServiceStatus DisplayService::begin(std::uint64_t nowMs) noexcept {
    initialized_ = false;
    page_ = DisplayPage::Calibration;
    pageEpochMs_ = nowMs;
    lastUpdateMs_ = nowMs;
    if (!config_.enabled) {
        return DisplayServiceStatus::Disabled;
    }
    if (config_.pageIntervalMs == 0U ||
        config_.maximumSnapshotAgeMs == 0U) {
        return DisplayServiceStatus::InvalidConfiguration;
    }
    if (driver_.begin() != project::drivers::Ssd1306DisplayStatus::Ok) {
        return DisplayServiceStatus::DriverError;
    }
    initialized_ = true;
    return DisplayServiceStatus::Ok;
}

void DisplayService::setLine(
    project::drivers::Ssd1306Frame& frame, std::size_t index,
    const char* format, ...) noexcept {
    if (index >= frame.lines.size()) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    (void)std::vsnprintf(frame.lines[index].data(),
                         frame.lines[index].size(), format, arguments);
    va_end(arguments);
}

bool DisplayService::validCaseName(
    const std::array<char, 32U>& name) noexcept {
    if (name[0] == '\0') {
        return false;
    }
    for (const char value : name) {
        if (value == '\0') {
            return true;
        }
        if (value < 0x20 || value > 0x7E) {
            return false;
        }
    }
    return false;
}

void DisplayService::makeFrame(
    const DisplaySnapshot& snapshot, bool fresh,
    project::drivers::Ssd1306Frame& output) const noexcept {
    output = {};
    if (page_ == DisplayPage::Calibration) {
        setLine(output, 0U, "CALIBRATION");
        if (fresh && snapshot.baselineRemainingValid &&
            std::isfinite(snapshot.baselineRemainingSeconds) &&
            snapshot.baselineRemainingSeconds >= 0.0F) {
            setLine(output, 1U, "BASELINE %.0fs",
                    snapshot.baselineRemainingSeconds);
        } else {
            setLine(output, 1U, "BASELINE --");
        }
        if (fresh && snapshot.pwpValid && std::isfinite(snapshot.pwpKpa)) {
            setLine(output, 2U, "PWP %.1f kPa", snapshot.pwpKpa);
        } else {
            setLine(output, 2U, "PWP --");
        }
        if (fresh && snapshot.rainRawValid) {
            setLine(output, 3U, "RAIN RAW %ld",
                    static_cast<long>(snapshot.rainRaw));
        } else {
            setLine(output, 3U, "RAIN RAW --");
        }
    } else {
        setLine(output, 0U, "ALERT %s",
                fresh && snapshot.riskValid ? riskLabel(snapshot.risk)
                                           : "UNKNOWN");
        if (fresh && snapshot.caseValid && validCaseName(snapshot.caseName)) {
            setLine(output, 1U, "CASE %u %.15s",
                    static_cast<unsigned int>(snapshot.caseNumber),
                    snapshot.caseName.data());
        } else {
            setLine(output, 1U, "CASE --");
        }
        if (fresh && snapshot.soilMoistureValid &&
            std::isfinite(snapshot.soilMoisturePercent)) {
            setLine(output, 2U, "SOIL %.1f%%",
                    snapshot.soilMoisturePercent);
        } else {
            setLine(output, 2U, "SOIL --");
        }
        if (fresh && snapshot.rainAccumulationValid &&
            std::isfinite(snapshot.rainAccumulationWetSeconds)) {
            setLine(output, 3U, "RAIN ACC %.1fs",
                    snapshot.rainAccumulationWetSeconds);
        } else {
            setLine(output, 3U, "RAIN ACC --");
        }
        if (fresh && snapshot.pwpRiseValid &&
            std::isfinite(snapshot.pwpRiseKpa)) {
            setLine(output, 4U, "PWP RISE %.1f", snapshot.pwpRiseKpa);
        } else {
            setLine(output, 4U, "PWP RISE --");
        }
        if (fresh && snapshot.tiltValid && std::isfinite(snapshot.tiltDegrees)) {
            setLine(output, 5U, "TILT %.1f deg", snapshot.tiltDegrees);
        } else {
            setLine(output, 5U, "TILT --");
        }
        output.inverted =
            fresh && snapshot.riskValid &&
            snapshot.risk == project::models::RiskState::DangerTest;
    }

    if (!fresh) {
        setLine(output, 7U, "DATA STALE");
    }
}

DisplayServiceStatus DisplayService::update(
    const DisplaySnapshot& snapshot, std::uint64_t nowMs) noexcept {
    if (!config_.enabled) {
        return DisplayServiceStatus::Disabled;
    }
    if (!initialized_) {
        return DisplayServiceStatus::NotInitialized;
    }
    if (nowMs < lastUpdateMs_) {
        return DisplayServiceStatus::TimeWentBackwards;
    }

    const std::uint64_t elapsed = nowMs - pageEpochMs_;
    const std::uint64_t pageChanges = elapsed / config_.pageIntervalMs;
    if ((pageChanges & 1U) != 0U) {
        page_ = page_ == DisplayPage::Calibration
                    ? DisplayPage::Alert
                    : DisplayPage::Calibration;
    }
    pageEpochMs_ += pageChanges * config_.pageIntervalMs;
    lastUpdateMs_ = nowMs;

    const bool fresh =
        snapshot.timestampMs <= nowMs &&
        nowMs - snapshot.timestampMs <= config_.maximumSnapshotAgeMs;
    project::drivers::Ssd1306Frame frame{};
    makeFrame(snapshot, fresh, frame);
    return driver_.present(frame) == project::drivers::Ssd1306DisplayStatus::Ok
               ? DisplayServiceStatus::Ok
               : DisplayServiceStatus::DriverError;
}

}  // namespace project::services
