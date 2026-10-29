#if defined(PROJECT_MOCK_ENVIRONMENT)

#include <Arduino.h>
#include <esp_timer.h>

#include "project/Drivers/MockSensors.h"
#include "project/Managers/SensorManager.h"

namespace {

constexpr std::uint32_t kMockSamplePeriodMicros = 1000000U;
project::managers::SensorManagerConfig makeMockSensorManagerConfig() {
    project::managers::SensorManagerConfig config{};
    config.samplePeriodMicros = kMockSamplePeriodMicros;
    for (auto& policy : config.readPolicies) {
        policy.beginTimeoutMicros = 100000U;
        policy.timeoutMicros = 100000U;
        policy.maxRetries = 1U;
    }
    return config;
}

const project::drivers::MockEnvironmentProfile kMockProfile =
    project::drivers::MockEnvironmentProfile::demo();
project::drivers::MockImuSensor gImu(kMockProfile);
project::drivers::MockSoilSensor gSoil(kMockProfile);
project::drivers::MockRainSensor gRain(kMockProfile);
project::drivers::MockHx710bSensor gHx710b(kMockProfile);
project::managers::PlatformSensorManagerClock gSensorClock;
project::managers::SensorManager gSensors(
    gImu, gSoil, gRain, gHx710b,
    makeMockSensorManagerConfig(), gSensorClock);
bool gSensorManagerReady = false;

void printSample(const project::models::SensorFrame& frame) {
    Serial.printf(
        "MOCK seq=%llu uptime_ms=%llu imu_raw=%ld/%ld/%ld soil_raw=%ld "
        "rain_raw=%ld hx710b_raw=%ld valid=%u/%u/%u/%u\r\n",
        static_cast<unsigned long long>(frame.sequence),
        static_cast<unsigned long long>(frame.timestampMs),
        static_cast<long>(frame.imu.accelX),
        static_cast<long>(frame.imu.accelY),
        static_cast<long>(frame.imu.accelZ),
        static_cast<long>(frame.soilMoistureRaw),
        static_cast<long>(frame.rainRaw),
        static_cast<long>(frame.hx710bRaw),
        frame.imuStatus.valid ? 1U : 0U,
        frame.soilStatus.valid ? 1U : 0U,
        frame.rainStatus.valid ? 1U : 0U,
        frame.hx710bStatus.valid ? 1U : 0U);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    gSensorManagerReady = gSensors.beginAll();
    Serial.println(gSensorManagerReady
                       ? "Mock sensor environment ready (synthetic raw data)"
                       : "Mock sensor environment initialization failed");
}

void loop() {
    if (!gSensorManagerReady) {
        delay(1000);
        return;
    }

    const auto nowMicros = static_cast<std::uint64_t>(esp_timer_get_time());
    if (gSensors.pollDue(nowMicros)) {
        project::models::SensorFrame frame{};
        if (gSensors.readSnapshot(frame)) {
            printSample(frame);
        } else {
            Serial.println("Mock sensor snapshot failed");
        }
    }
    delay(1);
}

#else

#include <Arduino.h>

#include "project/BSP/BoardConfig.h"
#include "project/BSP/BoardSupportPackage.h"
#include "project/Managers/ConfigManager.h"

void setup() {
    Serial.begin(115200);

    project::bsp::BoardSupportPackage bsp{};
    const project::bsp::BspStatus earlyStatus = bsp.initEarly();
    if (earlyStatus != project::bsp::BspStatus::Ok) {
        const project::bsp::BoardConfigReport report =
            project::bsp::activeBoardConfigReport();
        Serial.printf(
            "BOOT BLOCKED: board configuration is not approved "
            "(status=%u, issues=0x%08lx)\r\n",
            static_cast<unsigned int>(earlyStatus),
            static_cast<unsigned long>(report.issues));
        return;
    }

    if (bsp.configureOutputsSafe() != project::bsp::BspStatus::Ok ||
        bsp.initBuses() != project::bsp::BspStatus::Ok) {
        Serial.println(
            "BOOT BLOCKED: safe outputs or verified peripheral buses unavailable");
        return;
    }

    project::managers::ConfigManager configuration{};
    if (!configuration.loadLastValid() ||
        !configuration.validateManifest() ||
        !configuration.activateAtomically()) {
        Serial.println(
            "BOOT BLOCKED: validated active configuration is unavailable");
        return;
    }

    Serial.println(
        "BOOT BLOCKED: runtime queue/task composition has no approved "
        "target configuration");
}

void loop() {
    delay(1000);
}

#endif
