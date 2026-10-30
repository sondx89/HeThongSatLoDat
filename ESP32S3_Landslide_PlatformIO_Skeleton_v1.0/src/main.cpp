#if defined(PROJECT_MOCK_ENVIRONMENT)

#include <Arduino.h>
#include <esp_timer.h>

#include "project/Drivers/MockCsvTelemetry.h"
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

}  // namespace

void setup() {
    Serial.begin(115200);
    gSensorManagerReady = gSensors.beginAll();
    if (!gSensorManagerReady) {
        Serial.println("# MOCK_INIT_FAILED");
        return;
    }
    Serial.print(project::drivers::mockCsvTelemetryHeader());
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
            char row[192]{};
            std::size_t bytesWritten = 0U;
            if (project::drivers::serializeMockCsvTelemetry(
                    frame, row, sizeof(row), bytesWritten)) {
                Serial.write(reinterpret_cast<const std::uint8_t*>(row),
                             bytesWritten);
            } else {
                Serial.println("# MOCK_SERIALIZE_FAILED");
            }
        } else {
            Serial.println("# MOCK_SNAPSHOT_FAILED");
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
