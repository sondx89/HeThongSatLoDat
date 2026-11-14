#include <unity.h>

#include "project/Managers/RiskManager.h"

namespace {

project::managers::RiskManagerConfig modelOnlyConfig() {
    project::managers::RiskManagerConfig config{};
    config.version = 1U;
    config.hashSha256[0U] = 1U;
    config.hashVerified = true;
    config.approved = true;
    config.modelRuleEnabled = true;
    config.modelScoreSemanticsApproved = true;
    config.expectedModelOutputCount = 1U;
    config.scoreMinimum = 0.0F;
    config.scoreMaximum = 1.0F;
    config.scoreSafeBoundary = 0.3F;
    config.scoreMonitorBoundary = 0.6F;
    config.promotionSamples = 1U;
    config.recoverySamples = 1U;
    config.hysteresisConfigured = true;
    return config;
}

void testProductionRiskRequiresAiAndRejectsTiltRules() {
    auto config = modelOnlyConfig();
    TEST_ASSERT_TRUE(project::managers::RiskManager::validateConfig(config));

    config.tiltRuleEnabled = true;
    config.tiltSafeMaximumDeg = 3.0F;
    config.tiltMonitorMaximumDeg = 6.0F;
    TEST_ASSERT_FALSE(project::managers::RiskManager::validateConfig(config));

    config.modelRuleEnabled = false;
    TEST_ASSERT_FALSE(project::managers::RiskManager::validateConfig(config));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testProductionRiskRequiresAiAndRejectsTiltRules);
    return UNITY_END();
}
