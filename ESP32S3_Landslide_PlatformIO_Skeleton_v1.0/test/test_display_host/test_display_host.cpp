#include <unity.h>

#include "project/Services/DisplayService.h"

#include <cstring>

namespace {

class FakeDisplayTransport final : public project::drivers::ISsd1306Transport {
public:
    bool begin(std::uint8_t address, std::uint32_t timeoutMs) noexcept override {
        ++beginCalls;
        lastAddress = address;
        lastTimeoutMs = timeoutMs;
        return beginResult;
    }

    bool present(
        const project::drivers::Ssd1306Frame& frame) noexcept override {
        ++presentCalls;
        lastFrame = frame;
        return presentResult;
    }

    bool beginResult{true};
    bool presentResult{true};
    std::uint32_t beginCalls{0U};
    std::uint32_t presentCalls{0U};
    std::uint8_t lastAddress{0U};
    std::uint32_t lastTimeoutMs{0U};
    project::drivers::Ssd1306Frame lastFrame{};
};

project::drivers::Ssd1306DisplayConfig displayConfig(bool enabled) {
    project::drivers::Ssd1306DisplayConfig config{};
    config.enabled = enabled;
    config.address = 0x3CU;
    config.transactionTimeoutMs = 20U;
    return config;
}

project::services::DisplaySnapshot snapshot() {
    project::services::DisplaySnapshot value{};
    value.timestampMs = 1000U;
    value.risk = project::models::RiskState::DangerTest;
    value.caseNumber = 5U;
    std::strncpy(value.caseName.data(), "TILT EXTREME",
                 value.caseName.size() - 1U);
    value.baselineRemainingSeconds = 12.0F;
    value.pwpKpa = 34.0F;
    value.soilMoisturePercent = 80.0F;
    value.rainAccumulationWetSeconds = 42.0F;
    value.pwpRiseKpa = 8.0F;
    value.tiltDegrees = 11.0F;
    value.rainRaw = 2048;
    value.riskValid = true;
    value.caseValid = true;
    value.baselineRemainingValid = true;
    value.pwpValid = true;
    value.soilMoistureValid = true;
    value.rainAccumulationValid = true;
    value.pwpRiseValid = true;
    value.tiltValid = true;
    value.rainRawValid = true;
    return value;
}

void testDisplayIsDisabledWithoutTransportCalls() {
    FakeDisplayTransport transport;
    project::drivers::SSD1306DisplayDriver driver(
        transport, displayConfig(false));
    project::services::DisplayServiceConfig config{};
    project::services::DisplayService service(driver, config);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayServiceStatus::Disabled),
        static_cast<int>(service.begin(0U)));
    TEST_ASSERT_EQUAL_UINT32(0U, transport.beginCalls);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayServiceStatus::Disabled),
        static_cast<int>(service.update(snapshot(), 1000U)));
    TEST_ASSERT_EQUAL_UINT32(0U, transport.presentCalls);
}

void testDisplayAlternatesPagesAndMarksDangerAndInvalidValues() {
    FakeDisplayTransport transport;
    project::drivers::SSD1306DisplayDriver driver(
        transport, displayConfig(true));
    project::services::DisplayServiceConfig config{};
    config.enabled = true;
    config.pageIntervalMs = 2000U;
    config.maximumSnapshotAgeMs = 2500U;
    project::services::DisplayService service(driver, config);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayServiceStatus::Ok),
        static_cast<int>(service.begin(0U)));
    TEST_ASSERT_EQUAL_UINT8(0x3CU, transport.lastAddress);
    TEST_ASSERT_EQUAL_UINT32(20U, transport.lastTimeoutMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayServiceStatus::Ok),
        static_cast<int>(service.update(snapshot(), 1000U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayPage::Calibration),
        static_cast<int>(service.currentPage()));
    TEST_ASSERT_EQUAL_STRING("PWP 34.0 kPa",
                            transport.lastFrame.lines[2].data());

    auto invalid = snapshot();
    invalid.pwpValid = false;
    invalid.rainRawValid = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayServiceStatus::Ok),
        static_cast<int>(service.update(invalid, 1100U)));
    TEST_ASSERT_EQUAL_STRING("PWP --", transport.lastFrame.lines[2].data());
    TEST_ASSERT_EQUAL_STRING("RAIN RAW --",
                            transport.lastFrame.lines[3].data());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayServiceStatus::Ok),
        static_cast<int>(service.update(invalid, 2000U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayPage::Alert),
        static_cast<int>(service.currentPage()));
    TEST_ASSERT_TRUE(transport.lastFrame.inverted);
    TEST_ASSERT_EQUAL_STRING("ALERT DANGER",
                            transport.lastFrame.lines[0].data());
    TEST_ASSERT_EQUAL_STRING("SOIL 80.0%",
                            transport.lastFrame.lines[2].data());

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayServiceStatus::Ok),
        static_cast<int>(service.update(snapshot(), 4000U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayPage::Calibration),
        static_cast<int>(service.currentPage()));
    TEST_ASSERT_FALSE(transport.lastFrame.inverted);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::services::DisplayServiceStatus::TimeWentBackwards),
        static_cast<int>(service.update(snapshot(), 3999U)));
}

void testDisplayShowsStaleStateAndReportsTransportFailure() {
    FakeDisplayTransport transport;
    project::drivers::SSD1306DisplayDriver driver(
        transport, displayConfig(true));
    project::services::DisplayServiceConfig config{};
    config.enabled = true;
    config.maximumSnapshotAgeMs = 100U;
    project::services::DisplayService service(driver, config);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayServiceStatus::Ok),
        static_cast<int>(service.begin(0U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayServiceStatus::Ok),
        static_cast<int>(service.update(snapshot(), 1200U)));
    TEST_ASSERT_EQUAL_STRING("DATA STALE",
                            transport.lastFrame.lines[7].data());
    TEST_ASSERT_EQUAL_STRING("PWP --", transport.lastFrame.lines[2].data());

    transport.presentResult = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DisplayServiceStatus::DriverError),
        static_cast<int>(service.update(snapshot(), 1201U)));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testDisplayIsDisabledWithoutTransportCalls);
    RUN_TEST(testDisplayAlternatesPagesAndMarksDangerAndInvalidValues);
    RUN_TEST(testDisplayShowsStaleStateAndReportsTransportFailure);
    return UNITY_END();
}
