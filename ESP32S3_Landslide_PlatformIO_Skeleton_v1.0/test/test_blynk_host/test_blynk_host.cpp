#include <unity.h>

#include "project/Tasks/TaskBlynk.h"

#include <cstring>

namespace {

class FakeBlynkClient final : public project::services::IBlynkClient {
public:
    bool connected() const noexcept override { return isConnected; }

    bool connect(std::uint32_t timeoutMs) noexcept override {
        ++connectCalls;
        lastTimeoutMs = timeoutMs;
        isConnected = connectResult;
        return connectResult;
    }

    bool poll(std::uint32_t timeoutMs) noexcept override {
        ++pollCalls;
        lastTimeoutMs = timeoutMs;
        return pollResult;
    }

    bool writeFloat(std::uint8_t pin, float value) noexcept override {
        if (pin >= floatWriteCounts.size() || !writeResult) {
            return false;
        }
        ++floatWriteCounts[pin];
        lastFloatValues[pin] = value;
        return true;
    }

    bool writeInteger(std::uint8_t pin, std::int32_t value) noexcept override {
        if (pin >= integerWriteCounts.size() || !writeResult) {
            return false;
        }
        ++integerWriteCounts[pin];
        lastIntegerValue = value;
        return true;
    }

    bool writeText(std::uint8_t pin, const char* value,
                   std::size_t length) noexcept override {
        if (pin >= textWriteCounts.size() || value == nullptr ||
            length >= lastText[pin].size() || !writeResult) {
            return false;
        }
        ++textWriteCounts[pin];
        std::memcpy(lastText[pin].data(), value, length);
        lastText[pin][length] = '\0';
        return true;
    }

    bool logEvent(const char* eventName, std::size_t length,
                  project::models::EventSeverity severity) noexcept override {
        if (eventName == nullptr || length >= lastEventName.size() ||
            !eventResult) {
            return false;
        }
        ++eventCalls;
        std::memcpy(lastEventName.data(), eventName, length);
        lastEventName[length] = '\0';
        lastSeverity = severity;
        return true;
    }

    bool notify(const char* message, std::size_t length) noexcept override {
        if (message == nullptr || length >= lastNotification.size() ||
            !notificationResult) {
            return false;
        }
        ++notificationCalls;
        std::memcpy(lastNotification.data(), message, length);
        lastNotification[length] = '\0';
        return true;
    }

    bool isConnected{false};
    bool connectResult{true};
    bool pollResult{true};
    bool writeResult{true};
    bool eventResult{true};
    bool notificationResult{true};
    std::uint32_t connectCalls{0U};
    std::uint32_t pollCalls{0U};
    std::uint32_t eventCalls{0U};
    std::uint32_t notificationCalls{0U};
    std::uint32_t lastTimeoutMs{0U};
    std::array<std::uint32_t, 8U> floatWriteCounts{};
    std::array<std::uint32_t, 8U> integerWriteCounts{};
    std::array<std::uint32_t, 8U> textWriteCounts{};
    std::array<float, 8U> lastFloatValues{};
    std::array<std::array<char, 32U>, 8U> lastText{};
    std::array<char, 32U> lastEventName{};
    std::array<char, 32U> lastNotification{};
    std::int32_t lastIntegerValue{0};
    project::models::EventSeverity lastSeverity{
        project::models::EventSeverity::Info};
};

class FakeSnapshotSource final : public project::tasks::IBlynkSnapshotSource {
public:
    bool capture(
        project::services::BlynkSnapshot& output) noexcept override {
        if (!captureResult) {
            return false;
        }
        output = value;
        return true;
    }

    bool captureResult{true};
    project::services::BlynkSnapshot value{};
};

project::services::BlynkServiceConfig config() {
    project::services::BlynkServiceConfig value{};
    value.enabled = true;
    value.credentialsAvailable = true;
    value.operationTimeoutMs = 100U;
    value.maximumSnapshotAgeMs = 1000U;
    value.reconnectInitialMs = 1000U;
    value.reconnectMaximumMs = 4000U;
    value.caseNameCount = 2U;
    std::strncpy(value.caseNames[0U].data(), "CASE ONE",
                 value.caseNames[0U].size() - 1U);
    std::strncpy(value.caseNames[1U].data(), "CASE TWO",
                 value.caseNames[1U].size() - 1U);
    return value;
}

project::services::BlynkSnapshot snapshot(
    project::models::RiskState risk, std::uint64_t timestampMs) {
    project::services::BlynkSnapshot value{};
    value.timestampMs = timestampMs;
    value.risk = risk;
    value.caseNumber = 1U;
    value.imuTemperatureC = 22.5F;
    value.soilMoisturePercent = 65.0F;
    value.rainWetnessRelativePercent = 40.0F;
    value.tiltDegrees = 6.0F;
    value.pwpKpa = 12.0F;
    value.rainRaw = 2010;
    value.riskValid = true;
    value.caseValid = true;
    value.imuTemperatureValid = true;
    value.soilMoistureValid = true;
    value.rainWetnessValid = true;
    value.tiltValid = true;
    value.pwpValid = true;
    value.rainRawValid = true;
    return value;
}

void testBlynkMissingCredentialsDisablesAllNetworkCalls() {
    FakeBlynkClient client;
    auto serviceConfig = config();
    serviceConfig.credentialsAvailable = false;
    project::services::BlynkService service(client, serviceConfig);
    const auto value = snapshot(project::models::RiskState::Normal, 100U);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Disabled),
        static_cast<int>(service.begin(100U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Disabled),
        static_cast<int>(service.update(value, 100U)));
    TEST_ASSERT_EQUAL_UINT32(0U, client.connectCalls);
    TEST_ASSERT_EQUAL_UINT32(0U, client.pollCalls);
}

void testBlynkReconnectBackoffMappingsAndInvalidValues() {
    FakeBlynkClient client;
    client.connectResult = false;
    auto serviceConfig = config();
    project::services::BlynkService service(client, serviceConfig);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(service.begin(100U)));

    auto value = snapshot(project::models::RiskState::Normal, 100U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Connecting),
        static_cast<int>(service.update(value, 100U)));
    TEST_ASSERT_EQUAL_UINT32(1U, client.connectCalls);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Offline),
        static_cast<int>(service.update(value, 1099U)));
    TEST_ASSERT_EQUAL_UINT32(1U, client.connectCalls);

    client.connectResult = true;
    value.timestampMs = 1100U;
    value.pwpValid = false;
    value.caseNumber = 3U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(service.update(value, 1100U)));
    TEST_ASSERT_EQUAL_UINT32(100U, client.lastTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(1U, client.floatWriteCounts[0U]);
    TEST_ASSERT_EQUAL_UINT32(1U, client.floatWriteCounts[1U]);
    TEST_ASSERT_EQUAL_UINT32(1U, client.floatWriteCounts[2U]);
    TEST_ASSERT_EQUAL_UINT32(1U, client.floatWriteCounts[3U]);
    TEST_ASSERT_EQUAL_UINT32(0U, client.floatWriteCounts[4U]);
    TEST_ASSERT_EQUAL_UINT32(1U, client.textWriteCounts[5U]);
    TEST_ASSERT_EQUAL_UINT32(1U, client.integerWriteCounts[6U]);
    TEST_ASSERT_EQUAL_UINT32(0U, client.textWriteCounts[7U]);
    TEST_ASSERT_EQUAL_STRING("AN TOAN", client.lastText[5U].data());
    TEST_ASSERT_EQUAL_INT32(2010, client.lastIntegerValue);
    TEST_ASSERT_EQUAL_UINT32(1U, service.stats().invalidCaseIndices);

    client.isConnected = false;
    client.connectResult = false;
    value.timestampMs = 1200U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Connecting),
        static_cast<int>(service.update(value, 1200U)));
    TEST_ASSERT_EQUAL_UINT32(3U, client.connectCalls);
    value.timestampMs = 2199U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Offline),
        static_cast<int>(service.update(value, 2199U)));
    TEST_ASSERT_EQUAL_UINT32(3U, client.connectCalls);

    client.connectResult = true;
    value.timestampMs = 2200U;
    value.pwpValid = true;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(service.update(value, 2200U)));
    TEST_ASSERT_EQUAL_UINT32(1U, client.floatWriteCounts[4U]);

    value.timestampMs = 17200U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(service.update(value, 17200U)));
    value.timestampMs = 17200U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::SnapshotStale),
        static_cast<int>(service.update(value, 18201U)));
    TEST_ASSERT_EQUAL_UINT32(2U, client.floatWriteCounts[4U]);
}

void testBlynkWatchAndDangerCadenceAndTaskSnapshotCapture() {
    FakeBlynkClient client;
    client.isConnected = true;
    auto serviceConfig = config();
    project::services::BlynkService service(client, serviceConfig);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(service.begin(0U)));

    FakeSnapshotSource source;
    source.value = snapshot(project::models::RiskState::Monitor, 0U);
    project::tasks::TaskBlynk task(service, source);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(task.runOnce(0U)));
    TEST_ASSERT_EQUAL_STRING("theo_doi", client.lastEventName.data());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::EventSeverity::Warning),
        static_cast<int>(client.lastSeverity));
    TEST_ASSERT_EQUAL_UINT32(1U, client.notificationCalls);

    source.value.timestampMs = 4999U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(task.runOnce(4999U)));
    TEST_ASSERT_EQUAL_UINT32(1U, client.notificationCalls);
    source.value.timestampMs = 5000U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(task.runOnce(5000U)));
    source.value.timestampMs = 10000U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(task.runOnce(10000U)));
    TEST_ASSERT_EQUAL_UINT32(3U, client.notificationCalls);

    source.value.risk = project::models::RiskState::DangerTest;
    source.value.timestampMs = 11000U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(task.runOnce(11000U)));
    TEST_ASSERT_EQUAL_STRING("nguy_hiem", client.lastEventName.data());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::EventSeverity::Critical),
        static_cast<int>(client.lastSeverity));
    TEST_ASSERT_EQUAL_UINT32(4U, client.notificationCalls);

    source.value.timestampMs = 20999U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(task.runOnce(20999U)));
    TEST_ASSERT_EQUAL_UINT32(4U, client.notificationCalls);
    source.value.timestampMs = 21000U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(task.runOnce(21000U)));
    TEST_ASSERT_EQUAL_UINT32(5U, client.notificationCalls);

    source.captureResult = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::services::BlynkServiceStatus::SnapshotUnavailable),
        static_cast<int>(task.runOnce(21001U)));
}

void testBlynkRetriesRiskEventAfterTransportFailure() {
    FakeBlynkClient client;
    client.isConnected = true;
    client.eventResult = false;
    project::services::BlynkService service(client, config());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(service.begin(0U)));

    auto value = snapshot(project::models::RiskState::Monitor, 0U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::TransportError),
        static_cast<int>(service.update(value, 0U)));
    TEST_ASSERT_EQUAL_UINT32(1U, service.stats().eventFailures);
    TEST_ASSERT_EQUAL_UINT32(0U, client.eventCalls);

    client.eventResult = true;
    value.timestampMs = 1U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::BlynkServiceStatus::Ok),
        static_cast<int>(service.update(value, 1U)));
    TEST_ASSERT_EQUAL_UINT32(1U, client.eventCalls);
    TEST_ASSERT_EQUAL_STRING("theo_doi", client.lastEventName.data());
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testBlynkMissingCredentialsDisablesAllNetworkCalls);
    RUN_TEST(testBlynkReconnectBackoffMappingsAndInvalidValues);
    RUN_TEST(testBlynkWatchAndDangerCadenceAndTaskSnapshotCapture);
    RUN_TEST(testBlynkRetriesRiskEventAfterTransportFailure);
    return UNITY_END();
}
