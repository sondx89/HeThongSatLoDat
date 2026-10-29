#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "project/Drivers/ISensor.h"
#include "project/Managers/RiskManager.h"
#include "project/Managers/SensorManager.h"
#include "project/Models/FeatureVector.h"
#include "project/Models/ModelOutput.h"
#include "project/Models/ProcessedFrame.h"
#include "project/Models/RiskAssessment.h"
#include "project/Models/SensorFrame.h"
#include "project/Replay/ReplayEngine.h"
#include "project/Services/DataQualityService.h"
#include "project/Services/FeaturePipeline.h"

namespace {

using project::models::DataQualityState;
using project::models::HealthState;
using project::models::InferenceStatus;
using project::models::RiskState;
using project::models::SensorChannel;
using project::models::SensorHealth;
using project::models::SensorStatus;

struct TestClock final : project::managers::ISensorManagerClock {
    std::uint64_t currentMicros{1000000U};

    std::uint64_t nowMicros() noexcept override { return currentMicros; }

    void delayMicros(std::uint32_t durationMicros) noexcept override {
        currentMicros += durationMicros;
    }
};

struct SensorCallLog {
    char values[32]{};
    std::size_t count{0U};

    void append(char value) noexcept {
        if (count < sizeof(values)) {
            values[count++] = value;
        }
    }
};

struct TestSensor final : project::drivers::ISensor {
    char id;
    SensorCallLog& calls;
    TestClock& clock;
    std::uint8_t failedReadsRemaining{0U};
    std::uint32_t readDurationMicros{0U};

    TestSensor(char sensorId, SensorCallLog& callLog,
               TestClock& testClock) noexcept
        : id(sensorId), calls(callLog), clock(testClock) {}

    SensorStatus begin() override {
        calls.append(id);
        return {SensorHealth::Healthy, true, 0U, 0U, 0U};
    }

    SensorStatus read(project::models::SensorFrame& output) override {
        calls.append(id);
        clock.currentMicros += readDurationMicros;
        if (failedReadsRemaining > 0U) {
            --failedReadsRemaining;
            return {SensorHealth::Fault, false, 1U, 0U, 0U};
        }

        switch (id) {
            case 'I':
                output.imu.accelZ = 100;
                output.imuPhysical.accelZG = 1.0F;
                break;
            case 'S':
                output.soilMoistureRaw = 512;
                output.soilMoisturePct = 50.0F;
                output.calibrationVersion = 2U;
                break;
            case 'R':
                output.rainRaw = 300;
                output.rainWetnessRelativePct = 25.0F;
                output.rainWetnessValid = true;
                break;
            case 'H':
                output.hx710bRaw = 123456;
                break;
            default:
                break;
        }
        return {SensorHealth::Healthy, true, 0U, 0U, 0U};
    }

    SensorStatus selfTest() override {
        return {SensorHealth::Healthy, true, 0U, 0U, 0U};
    }
};

project::managers::SensorManagerConfig sensorManagerConfig(
    std::uint8_t retries = 0U) noexcept {
    project::managers::SensorManagerConfig config{};
    config.bootId = 42U;
    config.samplePeriodMicros = 100000U;
    for (auto& policy : config.readPolicies) {
        policy.beginTimeoutMicros = 100U;
        policy.timeoutMicros = 100U;
        policy.maxRetries = retries;
        policy.retryBackoffMicros = 7U;
    }
    return config;
}

void setAllSensorStatuses(project::models::SensorFrame& frame,
                          std::uint64_t timestamp) noexcept {
    const SensorStatus valid{SensorHealth::Healthy, true, 0U, 0U, timestamp};
    frame.imuStatus = valid;
    frame.soilStatus = valid;
    frame.rainStatus = valid;
    frame.hx710bStatus = valid;
}

project::services::DataQualityConfig qualityConfig() noexcept {
    project::services::DataQualityConfig config{};
    config.requiredChannelsConfigured = true;
    config.requiredChannels[static_cast<std::size_t>(
        SensorChannel::AccelXG)] = true;
    for (auto& freshness : config.freshness) {
        freshness.maximumAgeMs = 10U;
        freshness.configured = true;
    }
    auto& accelRange =
        config.ranges[static_cast<std::size_t>(SensorChannel::AccelXG)];
    accelRange.minimum = -1.0;
    accelRange.maximum = 1.0;
    accelRange.configured = true;
    return config;
}

project::models::SensorFrame goodSensorFrame() noexcept {
    project::models::SensorFrame frame{};
    frame.bootId = 7U;
    frame.sequence = 12U;
    frame.timestampMs = 95U;
    frame.imuPhysical.accelXG = 0.5F;
    frame.imuPhysical.accelYG = 0.0F;
    frame.imuPhysical.accelZG = 1.0F;
    setAllSensorStatuses(frame, 95U);
    return frame;
}

project::services::FeaturePipelineConfig featureConfig() noexcept {
    project::services::FeaturePipelineConfig config{};
    config.schemaVersion = 1U;
    config.schemaId = 2U;
    config.schemaHash[0] = 0xA5U;
    config.schemaHashValid = true;
    config.schemaApproved = true;
    config.modelMetadataVerified = true;
    config.flatLayoutApproved = true;
    config.preprocessingApproved = true;
    config.preprocessingVersion = 1U;
    config.sampleRateHz = 10.0F;
    config.sampleRateConfigured = true;
    config.samplePeriodToleranceMs = 0U;
    config.samplePeriodToleranceConfigured = true;
    config.featureCount = 2U;
    config.features[0] = {
        project::services::FssFeatureId::TiltXDeg, 0U, true};
    config.features[1] = {
        project::services::FssFeatureId::AccelMagnitude, 1U, true};
    config.windowLengthSamples = 2U;
    config.strideSamples = 1U;
    config.accelerationToMps2 = 9.81F;
    config.accelerationScaleConfigured = true;
    config.tiltFormulaApproved = true;
    return config;
}

project::models::ProcessedFrame goodProcessedFrame(
    std::uint64_t sequence, std::uint64_t timestamp,
    float accelX, float accelY, float accelZ) noexcept {
    project::models::ProcessedFrame frame{};
    frame.bootId = 5U;
    frame.sequence = sequence;
    frame.timestampMs = timestamp;
    frame.aggregateStatus = {SensorHealth::Healthy, true, 0U, 0U, timestamp};
    frame.calibrationVersion = 1U;
    frame.accelXG = accelX;
    frame.accelYG = accelY;
    frame.accelZG = accelZ;
    frame.channelQuality.fill(DataQualityState::VALID);
    return frame;
}

project::managers::RiskManagerConfig riskConfig() noexcept {
    project::managers::RiskManagerConfig config{};
    config.version = 3U;
    config.hashSha256[0] = 0x5AU;
    config.hashVerified = true;
    config.approved = true;
    config.modelRuleEnabled = true;
    config.modelOnlyFallbackApproved = true;
    config.expectedModelOutputCount = 1U;
    config.scoreMinimum = 0.0F;
    config.scoreMaximum = 1.0F;
    config.scoreSafeBoundary = 0.3F;
    config.scoreMonitorBoundary = 0.6F;
    config.tiltRuleEnabled = true;
    config.tiltSafeMaximumDeg = 5.0F;
    config.tiltMonitorMaximumDeg = 15.0F;
    config.hysteresisConfigured = true;
    config.promotionSamples = 1U;
    config.recoverySamples = 2U;
    config.recoveryHoldMs = 100U;
    return config;
}

project::models::ModelOutput modelOutput(float prediction) noexcept {
    project::models::ModelOutput model{};
    model.status = InferenceStatus::Ok;
    model.outputCount = 1U;
    model.predictionValue = prediction;
    model.predictionValid = true;
    model.rawOutputs[0] = prediction;
    return model;
}

project::replay::ReplayConfig replayConfig() noexcept {
    project::replay::ReplayConfig config{};
    config.csvSchemaVersion = 1U;
    config.featureSchemaVersion = 1U;
    config.featureSchemaId = 2U;
    config.featureSchemaHash[0] = 0xA5U;
    config.datasetSha256[0] = 0x5AU;
    config.featureSchemaHashValid = true;
    config.datasetHashVerified = true;
    config.datasetManifestVerified = true;
    config.featureSchemaApproved = true;
    config.modelMetadataVerified = true;
    config.flatLayoutApproved = true;
    config.columnMappingApproved = true;
    config.featureCount = 2U;
    config.features[0] = {
        project::replay::CsvColumn::Tilt, 0U, true};
    config.features[1] = {
        project::replay::CsvColumn::Soil, 1U, true};
    config.windowLengthSamples = 2U;
    config.strideSamples = 1U;
    config.modelInputElementCount = 4U;
    config.replaySessionId = 99U;
    config.sampleRateHz = 10.0F;
    config.sampleRateConfigured = true;
    config.samplePeriodToleranceMs = 0U;
    config.samplePeriodToleranceConfigured = true;
    return config;
}

struct MemoryCsvSource final : project::replay::ICsvReplaySource {
    const char* data;
    std::size_t size;
    std::size_t position{0U};
    std::size_t maxChunk{7U};
    bool rewindFails{false};

    MemoryCsvSource(const char* csv, std::size_t length) noexcept
        : data(csv), size(length) {}

    project::replay::CsvReplaySourceStatus rewind() noexcept override {
        position = 0U;
        return rewindFails ? project::replay::CsvReplaySourceStatus::IoError
                           : project::replay::CsvReplaySourceStatus::Ok;
    }

    project::replay::CsvReplaySourceStatus read(
        std::uint8_t* output, std::size_t capacity,
        std::size_t& bytesRead) noexcept override {
        bytesRead = 0U;
        if (position == size) {
            return project::replay::CsvReplaySourceStatus::EndOfFile;
        }
        const std::size_t amount =
            std::min(capacity, std::min(maxChunk, size - position));
        std::memcpy(output, data + position, amount);
        position += amount;
        bytesRead = amount;
        return project::replay::CsvReplaySourceStatus::Ok;
    }
};

void testSensorManagerReadsSensorsInOrderAndRetries() {
    SensorCallLog calls{};
    TestClock clock{};
    TestSensor imu('I', calls, clock);
    TestSensor soil('S', calls, clock);
    TestSensor rain('R', calls, clock);
    TestSensor hx710b('H', calls, clock);
    soil.failedReadsRemaining = 1U;
    auto config = sensorManagerConfig(1U);
    project::managers::SensorManager manager(
        imu, soil, rain, hx710b, config, clock);

    TEST_ASSERT_TRUE(manager.beginAll());
    calls.count = 0U;
    TEST_ASSERT_TRUE(manager.pollDue(1000000U));
    TEST_ASSERT_FALSE(manager.pollDue(1000000U));

    project::models::SensorFrame frame{};
    TEST_ASSERT_TRUE(manager.readSnapshot(frame));
    TEST_ASSERT_EQUAL_UINT64(42U, frame.bootId);
    TEST_ASSERT_EQUAL_UINT64(1U, frame.sequence);
    TEST_ASSERT_EQUAL_UINT64(1000U, frame.timestampMs);
    TEST_ASSERT_TRUE(frame.imuStatus.valid);
    TEST_ASSERT_TRUE(frame.soilStatus.valid);
    TEST_ASSERT_TRUE(frame.rainStatus.valid);
    TEST_ASSERT_TRUE(frame.hx710bStatus.valid);
    TEST_ASSERT_EQUAL_INT(5, static_cast<int>(calls.count));
    TEST_ASSERT_EQUAL_CHAR('I', calls.values[0]);
    TEST_ASSERT_EQUAL_CHAR('S', calls.values[1]);
    TEST_ASSERT_EQUAL_CHAR('S', calls.values[2]);
    TEST_ASSERT_EQUAL_CHAR('R', calls.values[3]);
    TEST_ASSERT_EQUAL_CHAR('H', calls.values[4]);
    TEST_ASSERT_EQUAL_UINT32(
        1U, manager.diagnostics(project::managers::SensorSlot::Soil).retries);

    TEST_ASSERT_FALSE(manager.pollDue(1099999U));
    TEST_ASSERT_TRUE(manager.pollDue(1100000U));
}

void testSensorManagerMarksReadTimeoutAndContinuesSnapshot() {
    SensorCallLog calls{};
    TestClock clock{};
    TestSensor imu('I', calls, clock);
    TestSensor soil('S', calls, clock);
    TestSensor rain('R', calls, clock);
    TestSensor hx710b('H', calls, clock);
    rain.readDurationMicros = 101U;
    auto config = sensorManagerConfig();
    project::managers::SensorManager manager(
        imu, soil, rain, hx710b, config, clock);

    TEST_ASSERT_TRUE(manager.beginAll());
    calls.count = 0U;
    TEST_ASSERT_TRUE(manager.pollDue(1000000U));
    project::models::SensorFrame frame{};
    TEST_ASSERT_TRUE(manager.readSnapshot(frame));
    TEST_ASSERT_FALSE(frame.rainStatus.valid);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorHealth::Fault),
                          static_cast<int>(frame.rainStatus.health));
    TEST_ASSERT_EQUAL_INT(4, static_cast<int>(calls.count));
    TEST_ASSERT_EQUAL_CHAR('I', calls.values[0]);
    TEST_ASSERT_EQUAL_CHAR('S', calls.values[1]);
    TEST_ASSERT_EQUAL_CHAR('R', calls.values[2]);
    TEST_ASSERT_EQUAL_CHAR('H', calls.values[3]);
    TEST_ASSERT_EQUAL_UINT32(
        1U, manager.diagnostics(project::managers::SensorSlot::Rain).timeouts);
    TEST_ASSERT_TRUE(frame.hx710bStatus.valid);
}

void testSensorManagerKeepsPerSensorReadTimestamps() {
    SensorCallLog calls{};
    TestClock clock{};
    TestSensor imu('I', calls, clock);
    TestSensor soil('S', calls, clock);
    TestSensor rain('R', calls, clock);
    TestSensor hx710b('H', calls, clock);
    imu.readDurationMicros = 1500U;
    soil.readDurationMicros = 2500U;
    rain.readDurationMicros = 3500U;
    hx710b.readDurationMicros = 4500U;
    auto config = sensorManagerConfig();
    for (auto& policy : config.readPolicies) {
        policy.timeoutMicros = 5000U;
    }
    project::managers::SensorManager manager(
        imu, soil, rain, hx710b, config, clock);

    TEST_ASSERT_TRUE(manager.beginAll());
    TEST_ASSERT_TRUE(manager.pollDue(1000000U));
    project::models::SensorFrame frame{};
    TEST_ASSERT_TRUE(manager.readSnapshot(frame));
    TEST_ASSERT_EQUAL_UINT64(1000U, frame.timestampMs);
    TEST_ASSERT_EQUAL_UINT64(1001U, frame.imuStatus.timestampMs);
    TEST_ASSERT_EQUAL_UINT64(1004U, frame.soilStatus.timestampMs);
    TEST_ASSERT_EQUAL_UINT64(1007U, frame.rainStatus.timestampMs);
    TEST_ASSERT_EQUAL_UINT64(1012U, frame.hx710bStatus.timestampMs);
}

void testDataQualityClassifiesRequiredChannelStates() {
    project::services::DataQualityService service(qualityConfig());
    const auto source = goodSensorFrame();
    project::models::ProcessedFrame output{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DataQualityServiceStatus::Ok),
        static_cast<int>(service.process(source, 100U, output)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataQualityState::VALID),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::AccelXG)]));
    TEST_ASSERT_TRUE(output.aggregateStatus.valid);

    auto stale = source;
    stale.imuStatus.timestampMs = 89U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DataQualityServiceStatus::Ok),
        static_cast<int>(service.process(stale, 100U, output)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataQualityState::STALE),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::AccelXG)]));
    TEST_ASSERT_FALSE(output.aggregateStatus.valid);

    auto missing = source;
    missing.imuStatus.valid = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DataQualityServiceStatus::Ok),
        static_cast<int>(service.process(missing, 100U, output)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataQualityState::MISSING),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::AccelXG)]));

    auto outOfRange = source;
    outOfRange.imuPhysical.accelXG = 1.1F;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DataQualityServiceStatus::Ok),
        static_cast<int>(service.process(outOfRange, 100U, output)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataQualityState::OUT_OF_RANGE),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::AccelXG)]));

    auto nonFinite = source;
    nonFinite.imuPhysical.accelXG =
        std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DataQualityServiceStatus::Ok),
        static_cast<int>(service.process(nonFinite, 100U, output)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataQualityState::NON_FINITE),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::AccelXG)]));
}

void testDataQualityRejectsUnconfiguredPolicy() {
    project::services::DataQualityService service{};
    project::models::ProcessedFrame output{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::services::DataQualityServiceStatus::InvalidConfiguration),
        static_cast<int>(service.process(goodSensorFrame(), 100U, output)));
    TEST_ASSERT_FALSE(output.aggregateStatus.valid);
}

void testFeaturePipelineBuildsApprovedWindow() {
    project::services::FeaturePipeline pipeline(featureConfig());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::FeaturePipelineStatus::Ok),
        static_cast<int>(pipeline.status()));
    TEST_ASSERT_TRUE(pipeline.push(
        goodProcessedFrame(1U, 1000U, 0.0F, 0.0F, 1.0F)));
    TEST_ASSERT_FALSE(pipeline.windowReady());
    TEST_ASSERT_TRUE(pipeline.push(
        goodProcessedFrame(2U, 1100U, 0.0F, 1.0F, 1.0F)));
    TEST_ASSERT_TRUE(pipeline.windowReady());

    project::models::FeatureVector output{};
    TEST_ASSERT_TRUE(pipeline.build(output));
    TEST_ASSERT_TRUE(output.valid);
    TEST_ASSERT_EQUAL_UINT32(1U, output.schemaVersion);
    TEST_ASSERT_EQUAL_UINT32(2U, output.schemaId);
    TEST_ASSERT_EQUAL_UINT64(5U, output.bootId);
    TEST_ASSERT_EQUAL_UINT64(1000U, output.windowStartMs);
    TEST_ASSERT_EQUAL_UINT64(1100U, output.windowEndMs);
    TEST_ASSERT_EQUAL_UINT32(2U, output.windowSampleCount);
    TEST_ASSERT_EQUAL_UINT16(4U, output.count);
    TEST_ASSERT_EQUAL_UINT32(0x0FU, output.featureValidMask);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, output.values[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 9.81F, output.values[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.02F, 35.264F, output.values[2]);
    TEST_ASSERT_FLOAT_WITHIN(0.02F, 13.873F, output.values[3]);
    TEST_ASSERT_FALSE(pipeline.windowReady());
}

void testFeaturePipelineFailsClosedForTbdRawFeature() {
    auto config = featureConfig();
    config.features[0].id = project::services::FssFeatureId::SoilRaw;
    project::services::FeaturePipeline pipeline(config);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::services::FeaturePipelineStatus::UnsupportedFeature),
        static_cast<int>(pipeline.status()));
    TEST_ASSERT_FALSE(pipeline.push(
        goodProcessedFrame(1U, 1000U, 0.0F, 0.0F, 1.0F)));
}

void testRiskManagerFusesApprovedScoreAndAppliesHysteresis() {
    project::managers::RiskManager manager(riskConfig());
    TEST_ASSERT_TRUE(manager.configured());

    project::models::RiskAssessment result{};
    project::managers::RiskEvaluationContext context{
        7U, 1U, 1000U, HealthState::Healthy};
    const project::managers::TiltValidation notApprovedTilt{};

    TEST_ASSERT_TRUE(manager.evaluate(
        modelOutput(0.8F), notApprovedTilt, DataQualityState::VALID,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::HighRiskTest),
                          static_cast<int>(result.risk));
    TEST_ASSERT_TRUE(result.riskScoreValid);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.8F, result.riskScore);

    context.sequenceNo = 2U;
    context.timestampMs = 1050U;
    TEST_ASSERT_TRUE(manager.evaluate(
        modelOutput(0.1F), notApprovedTilt, DataQualityState::VALID,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::HighRiskTest),
                          static_cast<int>(result.risk));

    context.sequenceNo = 3U;
    context.timestampMs = 1150U;
    TEST_ASSERT_TRUE(manager.evaluate(
        modelOutput(0.1F), notApprovedTilt, DataQualityState::VALID,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Safe),
                          static_cast<int>(result.risk));
    TEST_ASSERT_TRUE(result.valid);
}

void testRiskManagerReturnsUnknownForInvalidQualityOrModel() {
    project::managers::RiskManager manager(riskConfig());
    project::models::RiskAssessment result{};
    project::managers::RiskEvaluationContext context{
        7U, 1U, 1000U, HealthState::Degraded};
    const project::managers::TiltValidation notApprovedTilt{};
    TEST_ASSERT_TRUE(manager.evaluate(
        modelOutput(0.8F), notApprovedTilt, DataQualityState::STALE,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Unknown),
                          static_cast<int>(result.risk));
    TEST_ASSERT_FALSE(result.riskScoreValid);

    context.sequenceNo = 2U;
    auto invalidModel = modelOutput(0.8F);
    invalidModel.status = InferenceStatus::ModelError;
    TEST_ASSERT_TRUE(manager.evaluate(
        invalidModel, notApprovedTilt, DataQualityState::VALID,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Unknown),
                          static_cast<int>(result.risk));
}

void testRiskManagerHealthFaultBlocksModelRisk() {
    project::managers::RiskManager manager(riskConfig());
    project::models::RiskAssessment result{};
    project::managers::RiskEvaluationContext context{
        7U, 1U, 1000U, HealthState::Fault};
    const project::managers::TiltValidation notApprovedTilt{};

    TEST_ASSERT_TRUE(manager.evaluate(
        modelOutput(0.9F), notApprovedTilt, DataQualityState::VALID,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Unknown),
                          static_cast<int>(result.risk));
    TEST_ASSERT_FALSE(result.riskScoreValid);

    const project::managers::TiltValidation severeApprovedTilt{
        20.0F, project::managers::TiltValidationStatus::Valid, true};
    context.sequenceNo = 2U;
    TEST_ASSERT_TRUE(manager.evaluate(
        modelOutput(0.1F), severeApprovedTilt, DataQualityState::VALID,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::HighRiskTest),
                          static_cast<int>(result.risk));
    TEST_ASSERT_FALSE(result.riskScoreValid);
}

void testRiskManagerRejectsMismatchedModelProvenance() {
    project::managers::RiskManager manager(riskConfig());
    auto frame = goodProcessedFrame(1U, 1000U, 0.0F, 0.0F, 1.0F);
    project::models::HealthSnapshot health{};
    health.healthState = HealthState::Healthy;
    auto model = modelOutput(0.9F);
    model.bootId = frame.bootId;
    model.relatedSequenceNo = frame.sequence;
    model.sourceWindowStartMs = frame.timestampMs;
    model.sourceWindowEndMs = frame.timestampMs;
    model.sourceWindowSampleCount = 1U;
    model.sourceFeatureSchemaVersion = 1U;
    model.sourceFeatureSchemaId = 2U;
    model.sourceFeatureSchemaHashSha256[0] = 0xA5U;
    model.sourceFeatureSchemaHashValid = true;

    project::models::RiskAssessment result{};
    TEST_ASSERT_TRUE(manager.evaluate(model, frame, health, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::HighRiskTest),
                          static_cast<int>(result.risk));

    model.relatedSequenceNo++;
    TEST_ASSERT_TRUE(manager.evaluate(model, frame, health, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Unknown),
                          static_cast<int>(result.risk));
    TEST_ASSERT_FALSE(result.riskScoreValid);
}

void testRiskManagerFusesApprovedTiltWithModelScore() {
    project::managers::RiskManager manager(riskConfig());
    project::models::RiskAssessment result{};
    project::managers::RiskEvaluationContext context{
        7U, 1U, 1000U, HealthState::Healthy};
    project::managers::TiltValidation tilt{
        10.0F, project::managers::TiltValidationStatus::Valid, true};

    TEST_ASSERT_TRUE(manager.evaluate(
        modelOutput(0.1F), tilt, DataQualityState::VALID, context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Monitor),
                          static_cast<int>(result.risk));
    TEST_ASSERT_TRUE(result.riskScoreValid);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.1F, result.riskScore);
}

void testReplayEngineReplaysApprovedCsvWindow() {
    static constexpr char csv[] =
        "timestamp,tilt,soil,rain,stress,risk\n"
        "1000,1.5,50,0,0.2,SAFE\n"
        "1100,2.5,60,0,0.3,MONITOR\n"
        "1200,3.5,70,0,0.4,HIGH_RISK_TEST\n";
    MemoryCsvSource source(csv, sizeof(csv) - 1U);
    const auto config = replayConfig();
    project::replay::ReplayEngine replay(source, config);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::Ok),
        static_cast<int>(replay.begin()));

    project::models::FeatureVector output{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::VectorReady),
        static_cast<int>(replay.next(output)));
    TEST_ASSERT_TRUE(output.valid);
    TEST_ASSERT_EQUAL_UINT64(99U, output.bootId);
    TEST_ASSERT_EQUAL_UINT64(1000U, output.windowStartMs);
    TEST_ASSERT_EQUAL_UINT64(1100U, output.windowEndMs);
    TEST_ASSERT_EQUAL_UINT64(2U, output.relatedSequenceNo);
    TEST_ASSERT_EQUAL_UINT16(4U, output.count);
    TEST_ASSERT_EQUAL_UINT32(0x0FU, output.featureValidMask);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.5F, output.values[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 50.0F, output.values[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 2.5F, output.values[2]);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 60.0F, output.values[3]);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::VectorReady),
        static_cast<int>(replay.next(output)));
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 2.5F, output.values[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 60.0F, output.values[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 3.5F, output.values[2]);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 70.0F, output.values[3]);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::EndOfFile),
        static_cast<int>(replay.next(output)));
    TEST_ASSERT_EQUAL_UINT64(2U, replay.stats().vectorsProduced);
}

void testReplayEngineRejectsBadHeaderAndManifest() {
    static constexpr char badHeader[] =
        "time,tilt,soil,rain,stress,risk\n";
    MemoryCsvSource badSource(badHeader, sizeof(badHeader) - 1U);
    project::replay::ReplayEngine badReplay(badSource, replayConfig());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::InvalidHeader),
        static_cast<int>(badReplay.begin()));

    MemoryCsvSource source("", 0U);
    auto unverified = replayConfig();
    unverified.datasetManifestVerified = false;
    project::replay::ReplayEngine failClosedReplay(source, unverified);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::ManifestNotVerified),
        static_cast<int>(failClosedReplay.begin()));
}

void testReplayEngineRejectsMalformedRow() {
    static constexpr char csv[] =
        "timestamp,tilt,soil,rain,stress,risk\n"
        "1000,not-a-number,50,0,0.2,SAFE\n";
    MemoryCsvSource source(csv, sizeof(csv) - 1U);
    project::replay::ReplayEngine replay(source, replayConfig());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::Ok),
        static_cast<int>(replay.begin()));
    project::models::FeatureVector output{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::InvalidRow),
        static_cast<int>(replay.next(output)));
    TEST_ASSERT_EQUAL_UINT64(1U, replay.stats().rowsRejected);
}

void testReplayMalformedRowDoesNotAdvanceTimestampBaseline() {
    static constexpr char csv[] =
        "timestamp,tilt,soil,rain,stress,risk\n"
        "1000,1.5,50,0,0.2,SAFE\n"
        "1100,invalid,60,0,0.3,MONITOR\n"
        "1200,2.5,70,0,0.4,MONITOR\n";
    MemoryCsvSource source(csv, sizeof(csv) - 1U);
    project::replay::ReplayEngine replay(source, replayConfig());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::Ok),
        static_cast<int>(replay.begin()));

    project::models::FeatureVector output{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::InvalidRow),
        static_cast<int>(replay.next(output)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::replay::ReplayStatus::TimestampDiscontinuity),
        static_cast<int>(replay.next(output)));
    TEST_ASSERT_EQUAL_UINT64(1U, replay.stats().rowsRead);
    TEST_ASSERT_EQUAL_UINT64(2U, replay.stats().rowsRejected);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(testSensorManagerReadsSensorsInOrderAndRetries);
    RUN_TEST(testSensorManagerMarksReadTimeoutAndContinuesSnapshot);
    RUN_TEST(testSensorManagerKeepsPerSensorReadTimestamps);
    RUN_TEST(testDataQualityClassifiesRequiredChannelStates);
    RUN_TEST(testDataQualityRejectsUnconfiguredPolicy);
    RUN_TEST(testFeaturePipelineBuildsApprovedWindow);
    RUN_TEST(testFeaturePipelineFailsClosedForTbdRawFeature);
    RUN_TEST(testRiskManagerFusesApprovedScoreAndAppliesHysteresis);
    RUN_TEST(testRiskManagerReturnsUnknownForInvalidQualityOrModel);
    RUN_TEST(testRiskManagerHealthFaultBlocksModelRisk);
    RUN_TEST(testRiskManagerRejectsMismatchedModelProvenance);
    RUN_TEST(testRiskManagerFusesApprovedTiltWithModelScore);
    RUN_TEST(testReplayEngineReplaysApprovedCsvWindow);
    RUN_TEST(testReplayEngineRejectsBadHeaderAndManifest);
    RUN_TEST(testReplayEngineRejectsMalformedRow);
    RUN_TEST(testReplayMalformedRowDoesNotAdvanceTimestampBaseline);
    return UNITY_END();
}
