#include <unity.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "project/Drivers/MockSensors.h"
#include "project/Queues/QueueRegistry.h"

#include "../../src/Drivers/MockSensors.cpp"
#include "../../src/Queues/QueueRegistry.cpp"

namespace {

using project::models::SensorFrame;
using project::queues::QueueDepths;
using project::queues::QueueRegistry;
using project::queues::QueueRegistryStatus;

QueueDepths testQueueDepths() {
    QueueDepths depths{};
    depths.sensor = 4U;
    depths.feature = 1U;
    depths.model = 1U;
    depths.risk = 1U;
    depths.event = 1U;
    depths.log = 1U;
    return depths;
}

SensorFrame markedFrame(std::uint64_t sequence) {
    SensorFrame frame{};
    frame.bootId = 0xA55AU;
    frame.sequence = sequence;
    frame.timestampMs = sequence * 25U;
    frame.samplePeriodMs = 25U;
    frame.imu.accelX = static_cast<std::int32_t>(sequence * 3U);
    frame.soilMoistureRaw = -static_cast<std::int32_t>(sequence);
    frame.rainRaw = static_cast<std::int32_t>(sequence + 300U);
    frame.hx710bRaw = static_cast<std::int32_t>(sequence * 2U);
    frame.validMask = static_cast<std::uint32_t>(sequence ^ 0xA5A5U);
    frame.imuStatus.valid = sequence % 7U != 0U;
    frame.imuStatus.health =
        frame.imuStatus.valid ? project::models::SensorHealth::Healthy
                             : project::models::SensorHealth::Fault;
    frame.imuStatus.errorCode = static_cast<std::uint16_t>(sequence % 13U);
    frame.imuStatus.qualityFlags = static_cast<std::uint32_t>(sequence);
    frame.imuStatus.timestampMs = frame.timestampMs;
    frame.soilStatus.health = project::models::SensorHealth::Degraded;
    frame.soilStatus.valid = false;
    frame.soilStatus.timestampMs =
        frame.timestampMs > 100U ? frame.timestampMs - 100U : 0U;
    return frame;
}

void testQueueInitializationOverflowAndBoundedWaits() {
    QueueRegistry& queues = QueueRegistry::instance();
    QueueDepths invalidDepths{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(QueueRegistryStatus::InvalidConfiguration),
        static_cast<int>(queues.initialize(invalidDepths)));

    const QueueDepths depths = testQueueDepths();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(QueueRegistryStatus::Ok),
        static_cast<int>(queues.initialize(depths)));
    TEST_ASSERT_TRUE(queues.isInitialized());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(QueueRegistryStatus::AlreadyInitialized),
        static_cast<int>(queues.initialize(depths)));

    for (std::uint64_t sequence = 1U; sequence <= depths.sensor; ++sequence) {
        const SensorFrame frame = markedFrame(sequence);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(QueueRegistryStatus::Ok),
            static_cast<int>(queues.sendSensor(frame, 0U)));
    }

    const SensorFrame overflow = markedFrame(5U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(QueueRegistryStatus::QueueFull),
        static_cast<int>(queues.sendSensor(overflow, 0U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(QueueRegistryStatus::Timeout),
        static_cast<int>(queues.sendSensor(overflow, 2U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(QueueRegistryStatus::InvalidArgument),
        static_cast<int>(queues.sendSensor(overflow, portMAX_DELAY)));
    SensorFrame received{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(QueueRegistryStatus::InvalidArgument),
        static_cast<int>(queues.receiveSensor(received, portMAX_DELAY)));

    project::queues::QueueStats stats{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(QueueRegistryStatus::Ok),
        static_cast<int>(queues.getStats(project::queues::QueueId::Sensor,
                                         stats)));
    TEST_ASSERT_EQUAL_UINT32(4U, stats.capacity);
    TEST_ASSERT_EQUAL_UINT32(4U, stats.depth);
    TEST_ASSERT_EQUAL_UINT32(4U, stats.highWaterMark);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.droppedCount);

    for (std::uint64_t sequence = 1U; sequence <= depths.sensor; ++sequence) {
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(QueueRegistryStatus::Ok),
            static_cast<int>(queues.receiveSensor(received, 0U)));
        TEST_ASSERT_EQUAL_UINT64(sequence, received.sequence);
        TEST_ASSERT_EQUAL_UINT64(sequence * 25U, received.timestampMs);
    }
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(QueueRegistryStatus::QueueEmpty),
        static_cast<int>(queues.receiveSensor(received, 0U)));
}

void testConcurrentMockProducerAndQueueConsumerSeeCoherentFrames() {
    constexpr std::uint64_t kFrameCount = 10000U;
    constexpr std::uint64_t kBootId = 0xB007U;
    QueueRegistry& queues = QueueRegistry::instance();

    project::drivers::MockEnvironmentProfile profile{};
    profile.imu.axes[0] = {100, 10, 4U};
    profile.imu.axes[1] = {200, 20, 3U};
    profile.imu.axes[2] = {300, 30, 2U};
    profile.imu.axes[3] = {400, 40, 4U};
    profile.imu.axes[4] = {500, 50, 3U};
    profile.imu.axes[5] = {600, 60, 2U};
    project::drivers::MockImuSensor sensor(profile);
    TEST_ASSERT_TRUE(sensor.begin().valid);

    std::atomic<bool> failed{false};
    std::thread producer([&]() {
        for (std::uint64_t sequence = 1U; sequence <= kFrameCount;
             ++sequence) {
            SensorFrame frame{};
            if (!sensor.read(frame).valid) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            frame.bootId = kBootId;
            frame.sequence = sequence;
            frame.timestampMs = sequence * 25U;
            frame.samplePeriodMs = 25U;
            frame.imuStatus.valid = sequence % 7U != 0U;
            frame.imuStatus.health =
                frame.imuStatus.valid
                    ? project::models::SensorHealth::Healthy
                    : project::models::SensorHealth::Fault;
            frame.imuStatus.qualityFlags =
                static_cast<std::uint32_t>(sequence);
            frame.imuStatus.timestampMs = frame.timestampMs;
            frame.soilStatus.health = project::models::SensorHealth::Degraded;
            frame.soilStatus.valid = false;
            frame.soilStatus.timestampMs =
                frame.timestampMs > 100U ? frame.timestampMs - 100U : 0U;
            frame.soilMoistureRaw = -static_cast<std::int32_t>(sequence);
            frame.rainRaw = static_cast<std::int32_t>(sequence + 300U);
            frame.hx710bRaw = static_cast<std::int32_t>(sequence * 2U);
            frame.validMask = static_cast<std::uint32_t>(sequence ^ 0xA5A5U);
            if (queues.sendSensor(frame, 1000U) !=
                QueueRegistryStatus::Ok) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
        }
    });

    std::thread consumer([&]() {
        for (std::uint64_t sequence = 1U; sequence <= kFrameCount;
             ++sequence) {
            SensorFrame frame{};
            if (queues.receiveSensor(frame, 2000U) !=
                QueueRegistryStatus::Ok) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            const std::int32_t expectedAccelX =
                (((sequence - 1U) / 4U) & 1U) != 0U ? 110 : 90;
            const bool expectedImuValid = sequence % 7U != 0U;
            if (frame.bootId != kBootId || frame.sequence != sequence ||
                frame.timestampMs != sequence * 25U ||
                frame.samplePeriodMs != 25U ||
                frame.imu.accelX != expectedAccelX ||
                frame.soilMoistureRaw != -static_cast<std::int32_t>(sequence) ||
                frame.rainRaw != static_cast<std::int32_t>(sequence + 300U) ||
                frame.hx710bRaw != static_cast<std::int32_t>(sequence * 2U) ||
                frame.validMask !=
                    static_cast<std::uint32_t>(sequence ^ 0xA5A5U) ||
                frame.imuStatus.valid != expectedImuValid ||
                frame.imuStatus.timestampMs != sequence * 25U ||
                frame.imuStatus.qualityFlags != sequence ||
                frame.soilStatus.valid ||
                frame.soilStatus.health !=
                    project::models::SensorHealth::Degraded ||
                frame.soilStatus.timestampMs !=
                    (sequence * 25U > 100U ? sequence * 25U - 100U : 0U)) {
                failed.store(true, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();

    TEST_ASSERT_FALSE(failed.load(std::memory_order_relaxed));
    project::queues::QueueStats stats{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(QueueRegistryStatus::Ok),
        static_cast<int>(queues.getStats(project::queues::QueueId::Sensor,
                                         stats)));
    TEST_ASSERT_EQUAL_UINT32(0U, stats.depth);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.droppedCount);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testQueueInitializationOverflowAndBoundedWaits);
    RUN_TEST(testConcurrentMockProducerAndQueueConsumerSeeCoherentFrames);
    return UNITY_END();
}
