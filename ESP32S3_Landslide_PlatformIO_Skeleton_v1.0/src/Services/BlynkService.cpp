#include "project/Services/BlynkService.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace project::services {
namespace {

void incrementSaturated(std::uint32_t& value) noexcept {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

bool validRisk(project::models::RiskState risk) noexcept {
    using project::models::RiskState;
    switch (risk) {
    case RiskState::Startup:
    case RiskState::Normal:
    case RiskState::Monitor:
    case RiskState::DangerTest:
    case RiskState::Unknown:
        return true;
    }
    return false;
}

const char* riskLabel(project::models::RiskState risk) noexcept {
    using project::models::RiskState;
    switch (risk) {
    case RiskState::Normal:
        return "AN TOAN";
    case RiskState::Monitor:
        return "THEO DOI";
    case RiskState::DangerTest:
        return "NGUY HIEM";
    case RiskState::Startup:
    case RiskState::Unknown:
        return "KHONG RO";
    }
    return "KHONG RO";
}

}  // namespace

BlynkService::BlynkService(IBlynkClient& client,
                           const BlynkServiceConfig& config) noexcept
    : client_(client), config_(config) {}

std::uint64_t BlynkService::addBounded(
    std::uint64_t value, std::uint32_t delta) noexcept {
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    return value > maximum - delta ? maximum : value + delta;
}

BlynkServiceStatus BlynkService::begin(std::uint64_t nowMs) noexcept {
    initialized_ = false;
    if (!config_.enabled || !config_.credentialsAvailable) {
        return BlynkServiceStatus::Disabled;
    }
    if (config_.operationTimeoutMs == 0U ||
        config_.telemetryIntervalMs == 0U ||
        config_.maximumSnapshotAgeMs == 0U ||
        config_.reconnectInitialMs == 0U ||
        config_.reconnectMaximumMs < config_.reconnectInitialMs ||
        config_.watchNotificationIntervalMs == 0U ||
        config_.watchNotificationCount == 0U ||
        config_.dangerNotificationIntervalMs == 0U ||
        config_.caseNameCount > config_.caseNames.size()) {
        return BlynkServiceStatus::InvalidConfiguration;
    }
    reconnectDelayMs_ = config_.reconnectInitialMs;
    nextReconnectMs_ = nowMs;
    nextTelemetryMs_ = nowMs;
    nextWatchNotificationMs_ = nowMs;
    nextDangerNotificationMs_ = nowMs;
    lastUpdateMs_ = nowMs;
    lastRisk_ = project::models::RiskState::Unknown;
    watchNotificationsRemaining_ = 0U;
    hasLastRisk_ = false;
    stats_ = {};
    initialized_ = true;
    return BlynkServiceStatus::Ok;
}

void BlynkService::scheduleReconnect(std::uint64_t nowMs) noexcept {
    nextReconnectMs_ = addBounded(nowMs, reconnectDelayMs_);
    reconnectDelayMs_ =
        reconnectDelayMs_ >= config_.reconnectMaximumMs / 2U
            ? config_.reconnectMaximumMs
            : reconnectDelayMs_ * 2U;
    if (reconnectDelayMs_ > config_.reconnectMaximumMs) {
        reconnectDelayMs_ = config_.reconnectMaximumMs;
    }
}

bool BlynkService::validCaseName(
    const std::array<char, kBlynkCaseNameCapacity>& name,
    std::size_t& length) const noexcept {
    length = 0U;
    for (const char value : name) {
        if (value == '\0') {
            return length != 0U;
        }
        if (value < 0x20 || value > 0x7E) {
            return false;
        }
        ++length;
    }
    return false;
}

bool BlynkService::sendTelemetry(
    const BlynkSnapshot& snapshot) noexcept {
    incrementSaturated(stats_.telemetryAttempts);
    bool ok = true;
    if (snapshot.imuTemperatureValid &&
        std::isfinite(snapshot.imuTemperatureC)) {
        ok = client_.writeFloat(0U, snapshot.imuTemperatureC) && ok;
    }
    if (snapshot.soilMoistureValid &&
        std::isfinite(snapshot.soilMoisturePercent)) {
        ok = client_.writeFloat(1U, snapshot.soilMoisturePercent) && ok;
    }
    if (snapshot.rainWetnessValid &&
        std::isfinite(snapshot.rainWetnessRelativePercent)) {
        ok = client_.writeFloat(2U, snapshot.rainWetnessRelativePercent) && ok;
    }
    if (snapshot.tiltValid && std::isfinite(snapshot.tiltDegrees)) {
        ok = client_.writeFloat(3U, snapshot.tiltDegrees) && ok;
    }
    if (snapshot.pwpValid && std::isfinite(snapshot.pwpKpa)) {
        ok = client_.writeFloat(4U, snapshot.pwpKpa) && ok;
    }
    if (snapshot.riskValid && validRisk(snapshot.risk)) {
        const char* const label = riskLabel(snapshot.risk);
        ok = client_.writeText(5U, label, std::strlen(label)) && ok;
    }
    if (snapshot.rainRawValid) {
        ok = client_.writeInteger(6U, snapshot.rainRaw) && ok;
    }

    if (snapshot.caseValid) {
        if (snapshot.caseNumber == 0U ||
            snapshot.caseNumber > config_.caseNameCount ||
            snapshot.caseNumber > config_.caseNames.size()) {
            incrementSaturated(stats_.invalidCaseIndices);
        } else {
            const auto& caseName = config_.caseNames[snapshot.caseNumber - 1U];
            std::size_t length = 0U;
            if (!validCaseName(caseName, length)) {
                incrementSaturated(stats_.invalidCaseIndices);
            } else {
                ok = client_.writeText(7U, caseName.data(), length) && ok;
            }
        }
    }
    if (!ok) {
        incrementSaturated(stats_.telemetryFailures);
    }
    return ok;
}

bool BlynkService::handleRiskTransition(
    const BlynkSnapshot& snapshot, std::uint64_t nowMs) noexcept {
    if (!snapshot.riskValid || !validRisk(snapshot.risk)) {
        watchNotificationsRemaining_ = 0U;
        hasLastRisk_ = false;
        lastRisk_ = project::models::RiskState::Unknown;
        return true;
    }
    if (hasLastRisk_ && snapshot.risk == lastRisk_) {
        return true;
    }

    const bool firstRisk = !hasLastRisk_;
    if (snapshot.risk == project::models::RiskState::Monitor) {
        if (!client_.logEvent(
                "theo_doi", sizeof("theo_doi") - 1U,
                project::models::EventSeverity::Warning)) {
            return false;
        }
        hasLastRisk_ = true;
        lastRisk_ = snapshot.risk;
        watchNotificationsRemaining_ = config_.watchNotificationCount;
        nextWatchNotificationMs_ = nowMs;
        return true;
    }
    if (snapshot.risk == project::models::RiskState::DangerTest) {
        if (!client_.logEvent(
                "nguy_hiem", sizeof("nguy_hiem") - 1U,
                project::models::EventSeverity::Critical)) {
            return false;
        }
        hasLastRisk_ = true;
        lastRisk_ = snapshot.risk;
        watchNotificationsRemaining_ = 0U;
        nextDangerNotificationMs_ = nowMs;
        return true;
    }
    if (!firstRisk && config_.notifyWhenSafe &&
        snapshot.risk == project::models::RiskState::Normal) {
        if (!client_.notify("AN TOAN", sizeof("AN TOAN") - 1U)) {
            incrementSaturated(stats_.notificationFailures);
            return false;
        }
    }
    hasLastRisk_ = true;
    lastRisk_ = snapshot.risk;
    watchNotificationsRemaining_ = 0U;
    return true;
}

bool BlynkService::sendDueNotification(std::uint64_t nowMs) noexcept {
    if (!hasLastRisk_) {
        return true;
    }
    if (lastRisk_ == project::models::RiskState::Monitor &&
        watchNotificationsRemaining_ > 0U &&
        nowMs >= nextWatchNotificationMs_) {
        nextWatchNotificationMs_ =
            addBounded(nowMs, config_.watchNotificationIntervalMs);
        if (!client_.notify("THEO DOI", sizeof("THEO DOI") - 1U)) {
            incrementSaturated(stats_.notificationFailures);
            return false;
        }
        --watchNotificationsRemaining_;
    } else if (lastRisk_ == project::models::RiskState::DangerTest &&
               nowMs >= nextDangerNotificationMs_) {
        nextDangerNotificationMs_ =
            addBounded(nowMs, config_.dangerNotificationIntervalMs);
        if (!client_.notify("NGUY HIEM", sizeof("NGUY HIEM") - 1U)) {
            incrementSaturated(stats_.notificationFailures);
            return false;
        }
    }
    return true;
}

BlynkServiceStatus BlynkService::update(
    const BlynkSnapshot& snapshot, std::uint64_t nowMs) noexcept {
    if (!config_.enabled || !config_.credentialsAvailable) {
        return BlynkServiceStatus::Disabled;
    }
    if (!initialized_) {
        return BlynkServiceStatus::NotInitialized;
    }
    if (nowMs < lastUpdateMs_) {
        return BlynkServiceStatus::TimeWentBackwards;
    }
    if (snapshot.timestampMs > nowMs ||
        nowMs - snapshot.timestampMs > config_.maximumSnapshotAgeMs) {
        return BlynkServiceStatus::SnapshotStale;
    }
    lastUpdateMs_ = nowMs;

    if (!client_.connected()) {
        if (nowMs < nextReconnectMs_) {
            return BlynkServiceStatus::Offline;
        }
        incrementSaturated(stats_.reconnectAttempts);
        if (!client_.connect(config_.operationTimeoutMs) ||
            !client_.connected()) {
            scheduleReconnect(nowMs);
            return BlynkServiceStatus::Connecting;
        }
        reconnectDelayMs_ = config_.reconnectInitialMs;
        nextTelemetryMs_ = nowMs;
    }

    if (!client_.poll(config_.operationTimeoutMs)) {
        scheduleReconnect(nowMs);
        return BlynkServiceStatus::TransportError;
    }
    if (!client_.connected()) {
        scheduleReconnect(nowMs);
        return BlynkServiceStatus::Offline;
    }
    if (!handleRiskTransition(snapshot, nowMs)) {
        incrementSaturated(stats_.eventFailures);
        return BlynkServiceStatus::TransportError;
    }
    if (!sendDueNotification(nowMs)) {
        return BlynkServiceStatus::TransportError;
    }

    if (nowMs >= nextTelemetryMs_) {
        nextTelemetryMs_ = addBounded(nowMs, config_.telemetryIntervalMs);
        if (!sendTelemetry(snapshot)) {
            return BlynkServiceStatus::TransportError;
        }
    }
    return BlynkServiceStatus::Ok;
}

}  // namespace project::services
