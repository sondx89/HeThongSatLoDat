#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Models/DataContractTypes.h"

namespace project::services {

inline constexpr std::size_t kBlynkCaseCapacity = 11U;
inline constexpr std::size_t kBlynkCaseNameCapacity = 32U;

struct BlynkSnapshot {
    std::uint64_t timestampMs{0U};
    project::models::RiskState risk{project::models::RiskState::Unknown};
    std::uint8_t caseNumber{0U};
    float imuTemperatureC{0.0F};
    float soilMoisturePercent{0.0F};
    float rainWetnessRelativePercent{0.0F};
    float tiltDegrees{0.0F};
    float pwpKpa{0.0F};
    std::int32_t rainRaw{0};
    bool riskValid{false};
    bool caseValid{false};
    bool imuTemperatureValid{false};
    bool soilMoistureValid{false};
    bool rainWetnessValid{false};
    bool tiltValid{false};
    bool pwpValid{false};
    bool rainRawValid{false};
};

struct BlynkServiceConfig {
    bool enabled{false};
    bool credentialsAvailable{false};
    std::uint32_t operationTimeoutMs{0U};
    std::uint32_t telemetryIntervalMs{15000U};
    std::uint32_t maximumSnapshotAgeMs{0U};
    std::uint32_t reconnectInitialMs{1000U};
    std::uint32_t reconnectMaximumMs{30000U};
    std::uint32_t watchNotificationIntervalMs{5000U};
    std::uint8_t watchNotificationCount{3U};
    std::uint32_t dangerNotificationIntervalMs{10000U};
    bool notifyWhenSafe{false};
    std::uint8_t caseNameCount{0U};
    std::array<std::array<char, kBlynkCaseNameCapacity>, kBlynkCaseCapacity>
        caseNames{};
};

struct BlynkServiceStats {
    std::uint32_t reconnectAttempts{0U};
    std::uint32_t telemetryAttempts{0U};
    std::uint32_t telemetryFailures{0U};
    std::uint32_t notificationFailures{0U};
    std::uint32_t eventFailures{0U};
    std::uint32_t invalidCaseIndices{0U};
};

enum class BlynkServiceStatus : std::uint8_t {
    Ok,
    Disabled,
    Connecting,
    Offline,
    InvalidConfiguration,
    NotInitialized,
    SnapshotStale,
    SnapshotUnavailable,
    TimeWentBackwards,
    TransportError
};

class IBlynkClient {
public:
    virtual ~IBlynkClient() = default;
    virtual bool connected() const noexcept = 0;
    virtual bool connect(std::uint32_t timeoutMs) noexcept = 0;
    virtual bool poll(std::uint32_t timeoutMs) noexcept = 0;
    virtual bool writeFloat(std::uint8_t virtualPin,
                            float value) noexcept = 0;
    virtual bool writeInteger(std::uint8_t virtualPin,
                              std::int32_t value) noexcept = 0;
    virtual bool writeText(std::uint8_t virtualPin, const char* value,
                           std::size_t length) noexcept = 0;
    virtual bool logEvent(const char* eventName, std::size_t length,
                          project::models::EventSeverity severity) noexcept = 0;
    virtual bool notify(const char* message, std::size_t length) noexcept = 0;
};

class BlynkService final {
public:
    BlynkService(IBlynkClient& client,
                 const BlynkServiceConfig& config) noexcept;

    BlynkServiceStatus begin(std::uint64_t nowMs) noexcept;
    BlynkServiceStatus update(const BlynkSnapshot& snapshot,
                              std::uint64_t nowMs) noexcept;
    BlynkServiceStats stats() const noexcept { return stats_; }

private:
    bool sendTelemetry(const BlynkSnapshot& snapshot) noexcept;
    bool handleRiskTransition(const BlynkSnapshot& snapshot,
                              std::uint64_t nowMs) noexcept;
    bool sendDueNotification(std::uint64_t nowMs) noexcept;
    bool validCaseName(const std::array<char, kBlynkCaseNameCapacity>& name,
                       std::size_t& length) const noexcept;
    void scheduleReconnect(std::uint64_t nowMs) noexcept;
    static std::uint64_t addBounded(std::uint64_t value,
                                    std::uint32_t delta) noexcept;

    IBlynkClient& client_;
    const BlynkServiceConfig config_;
    BlynkServiceStats stats_{};
    std::uint64_t nextReconnectMs_{0U};
    std::uint64_t nextTelemetryMs_{0U};
    std::uint64_t nextWatchNotificationMs_{0U};
    std::uint64_t nextDangerNotificationMs_{0U};
    std::uint64_t lastUpdateMs_{0U};
    project::models::RiskState lastRisk_{project::models::RiskState::Unknown};
    std::uint32_t reconnectDelayMs_{0U};
    std::uint8_t watchNotificationsRemaining_{0U};
    bool hasLastRisk_{false};
    bool initialized_{false};
};

}  // namespace project::services
