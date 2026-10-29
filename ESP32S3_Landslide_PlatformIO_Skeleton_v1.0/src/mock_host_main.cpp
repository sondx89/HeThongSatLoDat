#if defined(PROJECT_MOCK_NATIVE)

#include <cstdint>
#include <cstdio>

#include "project/Drivers/MockSensors.h"
#include "project/Managers/SensorManager.h"

int main() {
    constexpr std::uint32_t samplePeriodMicros = 1000000U;
    constexpr std::uint32_t sampleCount = 12U;

    auto profile = project::drivers::MockEnvironmentProfile::demo();
    profile.rainBehavior.failEveryReads = 4U;
    project::drivers::MockImuSensor imu(profile);
    project::drivers::MockSoilSensor soil(profile);
    project::drivers::MockRainSensor rain(profile);
    project::drivers::MockHx710bSensor hx710b(profile);
    project::managers::SensorManager sensors(
        imu, soil, rain, hx710b,
        project::managers::SensorManagerConfig{0U, samplePeriodMicros});

    if (!sensors.beginAll()) {
        std::fprintf(stderr, "mock sensor initialization failed\n");
        return 1;
    }

    for (std::uint32_t index = 0; index < sampleCount; ++index) {
        const auto nowMicros =
            static_cast<std::uint64_t>(index) * samplePeriodMicros;
        if (!sensors.pollDue(nowMicros)) {
            std::fprintf(stderr, "mock scheduler rejected sample %u\n", index);
            return 2;
        }

        project::models::SensorFrame frame{};
        if (!sensors.readSnapshot(frame)) {
            std::fprintf(stderr, "mock snapshot failed at sample %u\n", index);
            return 3;
        }
        const bool rainExpectedValid = ((index + 1U) % 4U) != 0U;
        if (frame.sequence != static_cast<std::uint64_t>(index) + 1U ||
            frame.timestampMs !=
                static_cast<std::uint64_t>(index) * 1000U ||
            frame.imuStatus.valid != true ||
            frame.soilStatus.valid != true ||
            frame.hx710bStatus.valid != true ||
            frame.rainStatus.valid != rainExpectedValid) {
            std::fprintf(stderr, "mock frame validation failed at sample %u\n",
                         index);
            return 4;
        }
        if ((index == 0U &&
             (frame.imu.accelX != -120 ||
              frame.soilMoistureRaw != 1600 ||
              frame.hx710bRaw != 4988000)) ||
            (index == 4U && frame.imu.accelX != 120) ||
            (index == 6U && frame.soilMoistureRaw != 2400)) {
            std::fprintf(stderr, "mock waveform mismatch at sample %u\n", index);
            return 5;
        }
        std::printf(
            "MOCK seq=%llu uptime_ms=%llu imu_raw=%d/%d/%d soil_raw=%d "
            "rain_raw=%d hx710b_raw=%d valid=%u/%u/%u/%u\n",
            static_cast<unsigned long long>(frame.sequence),
            static_cast<unsigned long long>(frame.timestampMs),
            frame.imu.accelX, frame.imu.accelY, frame.imu.accelZ,
            frame.soilMoistureRaw, frame.rainRaw, frame.hx710bRaw,
            frame.imuStatus.valid ? 1U : 0U,
            frame.soilStatus.valid ? 1U : 0U,
            frame.rainStatus.valid ? 1U : 0U,
            frame.hx710bStatus.valid ? 1U : 0U);
    }

    return 0;
}

#endif
