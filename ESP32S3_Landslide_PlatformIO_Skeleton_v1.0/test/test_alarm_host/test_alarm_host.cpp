#include <unity.h>

#include "project/Services/AlarmService.h"

namespace {

class FakeBuzzerPin final : public project::drivers::IBuzzerPin {
public:
    bool configureOutput(bool inactiveHigh) noexcept override {
        ++configureCalls;
        configuredInactiveHigh = inactiveHigh;
        return configureResult;
    }

    bool writeLevel(bool high) noexcept override {
        ++writeCalls;
        lastElectricalHigh = high;
        if (!writeResult) {
            return false;
        }
        electricalHigh = high;
        return true;
    }

    bool hasFeedback() const noexcept override { return feedbackAvailable; }

    bool readFeedback(bool& active) noexcept override {
        if (!feedbackAvailable || !feedbackReadResult) {
            return false;
        }
        active = feedbackActive;
        return true;
    }

    bool configureResult{true};
    bool writeResult{true};
    bool feedbackAvailable{false};
    bool feedbackReadResult{true};
    bool feedbackActive{false};
    bool configuredInactiveHigh{false};
    bool lastElectricalHigh{false};
    bool electricalHigh{false};
    std::uint32_t configureCalls{0U};
    std::uint32_t writeCalls{0U};
};

project::drivers::BuzzerDriverConfig buzzerConfig(
    project::drivers::BuzzerActiveLevel activeLevel, bool feedback = false) {
    (void)feedback;
    project::drivers::BuzzerDriverConfig config{};
    config.enabled = true;
    config.pinConfigured = true;
    config.polarityVerified = true;
    config.activeLevel = activeLevel;
    return config;
}

project::models::RiskAssessment assessment(project::models::RiskState risk) {
    project::models::RiskAssessment value{};
    value.valid = true;
    value.risk = risk;
    return value;
}

void testBuzzerStartsSafeWithBothVerifiedPolarities() {
    FakeBuzzerPin activeHighPin;
    project::drivers::BuzzerDriver activeHigh(
        activeHighPin,
        buzzerConfig(project::drivers::BuzzerActiveLevel::High));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::drivers::BuzzerDriverStatus::Ok),
        static_cast<int>(activeHigh.begin()));
    TEST_ASSERT_FALSE(activeHighPin.configuredInactiveHigh);
    TEST_ASSERT_FALSE(activeHighPin.lastElectricalHigh);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::AlarmOutputState::NotVerifiable),
        static_cast<int>(activeHigh.report().feedback));

    FakeBuzzerPin activeLowPin;
    project::drivers::BuzzerDriver activeLow(
        activeLowPin,
        buzzerConfig(project::drivers::BuzzerActiveLevel::Low));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::drivers::BuzzerDriverStatus::Ok),
        static_cast<int>(activeLow.begin()));
    TEST_ASSERT_TRUE(activeLowPin.configuredInactiveHigh);
    TEST_ASSERT_TRUE(activeLowPin.lastElectricalHigh);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::drivers::BuzzerDriverStatus::Ok),
        static_cast<int>(activeLow.setActive(true)));
    TEST_ASSERT_FALSE(activeLowPin.lastElectricalHigh);
}

void testAlarmWatchCadenceDangerSafeAndInvalidAssessment() {
    FakeBuzzerPin pin;
    pin.feedbackAvailable = true;
    project::drivers::BuzzerDriver driver(
        pin, buzzerConfig(project::drivers::BuzzerActiveLevel::Low));
    project::services::AlarmService service(driver);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::AlarmServiceStatus::Ok),
        static_cast<int>(service.begin()));

    project::services::AlarmServiceReport report{};
    const auto watch = assessment(project::models::RiskState::Monitor);
    for (std::uint64_t now = 0U; now < 1200U; now += 200U) {
        pin.feedbackActive = (now % 400U) == 0U;
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(project::services::AlarmServiceStatus::Ok),
            static_cast<int>(service.update(watch, now, report)));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(pin.feedbackActive
                                 ? project::models::AlarmOutputState::On
                                 : project::models::AlarmOutputState::Off),
            static_cast<int>(report.feedback));
    }

    pin.feedbackActive = true;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::AlarmServiceStatus::Ok),
        static_cast<int>(service.update(
            assessment(project::models::RiskState::DangerTest), 1200U,
            report)));
    TEST_ASSERT_TRUE(report.commandActive);

    pin.feedbackActive = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::AlarmServiceStatus::Ok),
        static_cast<int>(service.update(
            assessment(project::models::RiskState::Normal), 1400U, report)));
    TEST_ASSERT_FALSE(report.commandActive);

    auto invalid = assessment(project::models::RiskState::DangerTest);
    invalid.valid = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::services::AlarmServiceStatus::InvalidAssessment),
        static_cast<int>(service.update(invalid, 1600U, report)));
    TEST_ASSERT_FALSE(report.commandActive);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::services::AlarmServiceStatus::TimeWentBackwards),
        static_cast<int>(service.update(watch, 1599U, report)));
    TEST_ASSERT_FALSE(report.commandActive);
}

void testAlarmReportsMissingPolarityAndOutputFault() {
    FakeBuzzerPin unconfiguredPin;
    auto invalidConfig = buzzerConfig(
        project::drivers::BuzzerActiveLevel::Unconfigured);
    invalidConfig.polarityVerified = false;
    project::drivers::BuzzerDriver unconfigured(unconfiguredPin,
                                                invalidConfig);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::drivers::BuzzerDriverStatus::InvalidConfiguration),
        static_cast<int>(unconfigured.begin()));
    TEST_ASSERT_EQUAL_UINT32(0U, unconfiguredPin.writeCalls);

    FakeBuzzerPin failedPin;
    failedPin.writeResult = false;
    project::drivers::BuzzerDriver failedDriver(
        failedPin,
        buzzerConfig(project::drivers::BuzzerActiveLevel::High));
    project::services::AlarmService failedService(failedDriver);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::AlarmServiceStatus::OutputFailure),
        static_cast<int>(failedService.begin()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::AlarmOutputState::Failed),
        static_cast<int>(failedDriver.report().feedback));

    FakeBuzzerPin absentPin;
    project::drivers::BuzzerDriverConfig absentConfig{};
    project::drivers::BuzzerDriver absent(absentPin, absentConfig);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::drivers::BuzzerDriverStatus::NotPresent),
        static_cast<int>(absent.begin()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::AlarmOutputState::NotPresent),
        static_cast<int>(absent.report().feedback));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testBuzzerStartsSafeWithBothVerifiedPolarities);
    RUN_TEST(testAlarmWatchCadenceDangerSafeAndInvalidAssessment);
    RUN_TEST(testAlarmReportsMissingPolarityAndOutputFault);
    return UNITY_END();
}
