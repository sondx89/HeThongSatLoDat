#include <unity.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "project/AI/IInferenceEngine.h"
#include "project/AI/MockModelRuntime.h"
#include "project/Drivers/MockCsvTelemetry.h"
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
#include "project/Storage/CsvLogger.h"
#include "project/Storage/CsvRecord.h"

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

struct IntegrationSensor final : project::drivers::ISensor {
    char kind;

    explicit IntegrationSensor(char sensorKind) noexcept : kind(sensorKind) {}

    SensorStatus begin() override {
        return {SensorHealth::Healthy, true, 0U, 0U, 0U};
    }

    SensorStatus read(project::models::SensorFrame& frame) override {
        switch (kind) {
            case 'I':
                frame.imu.accelX = 0;
                frame.imu.accelY = 0;
                frame.imu.accelZ = 100;
                frame.imuPhysical.accelXG = 0.0F;
                frame.imuPhysical.accelYG = 0.0F;
                frame.imuPhysical.accelZG = 1.0F;
                break;
            case 'S':
                frame.soilMoistureRaw = 550;
                frame.soilMoisturePct = 55.0F;
                frame.calibrationVersion = 1U;
                break;
            case 'R':
                frame.rainRaw = 250;
                frame.rainWetnessRelativePct = 25.0F;
                frame.rainWetnessValid = true;
                break;
            case 'H':
                frame.hx710bRaw = 123456;
                break;
            default:
                return {SensorHealth::Fault, false, 1U, 0U, 0U};
        }
        return {SensorHealth::Healthy, true, 0U, 0U, 0U};
    }

    SensorStatus selfTest() override {
        return {SensorHealth::Healthy, true, 0U, 0U, 0U};
    }
};

struct ScriptedTestModel final : project::ai::IModelRuntime {
    project::models::FeatureVector capturedInput{};
    float scriptedOutput{0.8F};
    bool initialized{false};

    bool initialize() override {
        initialized = true;
        return true;
    }

    bool run(const project::models::FeatureVector& input,
             project::models::ModelOutput& output) override {
        output = {};
        if (!initialized || !input.valid) {
            output.status = InferenceStatus::InvalidInput;
            return false;
        }
        capturedInput = input;
        output.bootId = input.bootId;
        output.relatedSequenceNo = input.relatedSequenceNo;
        output.sourceWindowStartMs = input.windowStartMs;
        output.sourceWindowEndMs = input.windowEndMs;
        output.sourceWindowSampleCount = input.windowSampleCount;
        output.sourceFeatureSchemaVersion = input.schemaVersion;
        output.sourceFeatureSchemaId = input.schemaId;
        for (std::size_t i = 0U; i < sizeof(input.schemaHashSha256); ++i) {
            output.sourceFeatureSchemaHashSha256[i] =
                input.schemaHashSha256[i];
        }
        output.sourceFeatureSchemaHashValid = input.schemaHashValid;
        output.status = InferenceStatus::Ok;
        output.outputCount = 1U;
        output.rawOutputs[0] = scriptedOutput;
        output.predictionValue = scriptedOutput;
        output.predictionValid = true;
        output.timestampMs = input.windowEndMs;
        return true;
    }
};

struct MemoryCsvFileSystem final : project::storage::ICsvFileSystem {
    static constexpr std::size_t kCapacity = 1024U;
    char filename[32]{};
    std::uint8_t contents[kCapacity]{};
    std::size_t length{0U};
    bool exists{false};
    std::uint32_t flushCount{0U};

    project::storage::CsvFileStatus begin() noexcept override {
        return project::storage::CsvFileStatus::Ok;
    }

    project::storage::CsvFileStatus findLatest(
        const char*, char* output, std::size_t capacity,
        std::uint32_t& fileIndex, std::uint64_t& fileSize) noexcept override {
        if (!exists) {
            return project::storage::CsvFileStatus::NotFound;
        }
        if (capacity <= std::strlen(filename)) {
            return project::storage::CsvFileStatus::IoError;
        }
        std::memcpy(output, filename, std::strlen(filename) + 1U);
        fileIndex = 0U;
        fileSize = length;
        return project::storage::CsvFileStatus::Ok;
    }

    project::storage::CsvFileStatus createExclusive(
        const char* newFilename) noexcept override {
        if (exists || std::strlen(newFilename) >= sizeof(filename)) {
            return exists ? project::storage::CsvFileStatus::Full
                          : project::storage::CsvFileStatus::IoError;
        }
        std::strcpy(filename, newFilename);
        length = 0U;
        exists = true;
        return project::storage::CsvFileStatus::Ok;
    }

    project::storage::CsvFileStatus readAt(
        const char* requestedFilename, std::uint64_t offset,
        std::uint8_t* output, std::size_t requested,
        std::size_t& bytesRead) noexcept override {
        bytesRead = 0U;
        if (!exists || std::strcmp(requestedFilename, filename) != 0 ||
            offset > length) {
            return project::storage::CsvFileStatus::IoError;
        }
        bytesRead = std::min(requested, length - static_cast<std::size_t>(offset));
        std::memcpy(output, contents + static_cast<std::size_t>(offset),
                    bytesRead);
        return project::storage::CsvFileStatus::Ok;
    }

    project::storage::CsvFileStatus append(
        const char* requestedFilename, const std::uint8_t* data,
        std::size_t dataLength, std::size_t& bytesWritten) noexcept override {
        bytesWritten = 0U;
        if (!exists || std::strcmp(requestedFilename, filename) != 0 ||
            dataLength > kCapacity - length) {
            return project::storage::CsvFileStatus::Full;
        }
        std::memcpy(contents + length, data, dataLength);
        length += dataLength;
        bytesWritten = dataLength;
        return project::storage::CsvFileStatus::Ok;
    }

    project::storage::CsvFileStatus truncate(
        const char* requestedFilename, std::uint64_t newLength) noexcept override {
        if (!exists || std::strcmp(requestedFilename, filename) != 0 ||
            newLength > length) {
            return project::storage::CsvFileStatus::IoError;
        }
        length = static_cast<std::size_t>(newLength);
        return project::storage::CsvFileStatus::Ok;
    }

    project::storage::CsvFileStatus flush(
        const char* requestedFilename) noexcept override {
        if (!exists || std::strcmp(requestedFilename, filename) != 0) {
            return project::storage::CsvFileStatus::IoError;
        }
        ++flushCount;
        return project::storage::CsvFileStatus::Ok;
    }

    bool contains(const char* text) const noexcept {
        const std::size_t textLength = std::strlen(text);
        if (textLength > length) {
            return false;
        }
        for (std::size_t start = 0U; start <= length - textLength; ++start) {
            if (std::memcmp(contents + start, text, textLength) == 0) {
                return true;
            }
        }
        return false;
    }

    bool equals(const char* expected) const noexcept {
        const std::size_t expectedLength = std::strlen(expected);
        return length == expectedLength &&
               std::memcmp(contents, expected, expectedLength) == 0;
    }
};

project::managers::SensorManagerConfig makeSensorConfig() noexcept {
    project::managers::SensorManagerConfig config{};
    config.bootId = 42U;
    config.samplePeriodMicros = 100000U;
    for (auto& policy : config.readPolicies) {
        policy.beginTimeoutMicros = 100U;
        policy.timeoutMicros = 100U;
    }
    return config;
}

project::services::DataQualityConfig makeQualityConfig() noexcept {
    project::services::DataQualityConfig config{};
    config.requiredChannelsConfigured = true;
    config.requiredChannels[static_cast<std::size_t>(
        SensorChannel::AccelXG)] = true;
    config.requiredChannels[static_cast<std::size_t>(
        SensorChannel::AccelYG)] = true;
    config.requiredChannels[static_cast<std::size_t>(
        SensorChannel::AccelZG)] = true;
    for (auto& freshness : config.freshness) {
        freshness.maximumAgeMs = 0U;
        freshness.configured = true;
    }
    return config;
}

project::services::FeaturePipelineConfig makeFeatureConfig() noexcept {
    project::services::FeaturePipelineConfig config{};
    config.schemaVersion = 1U;
    config.schemaId = 1U;
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
    config.windowLengthSamples = 1U;
    config.strideSamples = 1U;
    config.accelerationToMps2 = 9.81F;
    config.accelerationScaleConfigured = true;
    config.tiltFormulaApproved = true;
    return config;
}

project::managers::RiskManagerConfig makeRiskConfig() noexcept {
    project::managers::RiskManagerConfig config{};
    config.version = 1U;
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
    config.hysteresisConfigured = true;
    config.promotionSamples = 1U;
    config.recoverySamples = 1U;
    config.recoveryHoldMs = 0U;
    return config;
}

bool runSingleSampleTrace(project::ai::IModelRuntime& runtime,
                          project::models::ModelOutput& modelOutput,
                          project::models::RiskAssessment& riskOutput,
                          MemoryCsvFileSystem& storage) {
    TestClock clock{};
    IntegrationSensor imu('I');
    IntegrationSensor soil('S');
    IntegrationSensor rain('R');
    IntegrationSensor hx710b('H');
    project::managers::SensorManager sensors(
        imu, soil, rain, hx710b, makeSensorConfig(), clock);
    if (!sensors.beginAll() || !sensors.pollDue(1000000U)) {
        return false;
    }
    project::models::SensorFrame sensorFrame{};
    if (!sensors.readSnapshot(sensorFrame)) {
        return false;
    }

    project::services::DataQualityService quality(makeQualityConfig());
    project::models::ProcessedFrame processed{};
    if (quality.process(sensorFrame, sensorFrame.timestampMs, processed) !=
            project::services::DataQualityServiceStatus::Ok ||
        !processed.aggregateStatus.valid) {
        return false;
    }

    project::services::FeaturePipeline features(makeFeatureConfig());
    if (!features.push(processed) || !features.windowReady()) {
        return false;
    }
    project::models::FeatureVector vector{};
    if (!features.build(vector) || !runtime.initialize() ||
        !runtime.run(vector, modelOutput)) {
        return false;
    }

    project::managers::RiskManager risk(makeRiskConfig());
    const project::managers::RiskEvaluationContext context{
        vector.bootId, vector.relatedSequenceNo, vector.windowEndMs,
        HealthState::Healthy};
    const project::managers::TiltValidation tilt{};
    if (!risk.evaluate(modelOutput, tilt, DataQualityState::VALID, context,
                       riskOutput)) {
        return false;
    }

    project::storage::CsvLoggerConfig loggerConfig{};
    loggerConfig.maximumFileBytes = 512U;
    project::storage::CsvLogger logger(storage, loggerConfig);
    if (logger.begin() != project::storage::CsvLoggerStatus::Ok) {
        return false;
    }

    project::storage::CsvRecord record{};
    record.timestampMs = vector.windowEndMs;
    record.timestampValid = true;
    record.tilt = vector.values[0];
    record.tiltValid = vector.valid;
    record.soil = processed.soilMoisturePct;
    record.soilValid =
        processed.channelQuality[static_cast<std::size_t>(
            SensorChannel::SoilMoisturePct)] == DataQualityState::VALID;
    record.rain = processed.rainWetnessRelativePct;
    record.rainValid =
        processed.channelQuality[static_cast<std::size_t>(
            SensorChannel::RainWetnessRelativePct)] == DataQualityState::VALID;
    record.stress = modelOutput.predictionValue;
    record.stressValid = modelOutput.predictionValid;
    record.riskValid = riskOutput.valid;
    switch (riskOutput.risk) {
        case RiskState::Safe:
            std::strcpy(record.risk.data(), "SAFE");
            break;
        case RiskState::Monitor:
            std::strcpy(record.risk.data(), "MONITOR");
            break;
        case RiskState::HighRiskTest:
            std::strcpy(record.risk.data(), "HIGH_RISK_TEST");
            break;
        default:
            std::strcpy(record.risk.data(), "UNKNOWN");
            break;
    }
    return logger.appendRecord(record) == project::storage::CsvLoggerStatus::Ok;
}

void testSensorFeatureScriptedModelRiskAndCsvIntegration() {
    ScriptedTestModel model{};
    MemoryCsvFileSystem storage{};
    project::models::ModelOutput modelOutput{};
    project::models::RiskAssessment riskOutput{};

    TEST_ASSERT_TRUE(runSingleSampleTrace(
        model, modelOutput, riskOutput, storage));
    TEST_ASSERT_TRUE(model.capturedInput.valid);
    TEST_ASSERT_EQUAL_UINT16(2U, model.capturedInput.count);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, model.capturedInput.values[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 9.81F, model.capturedInput.values[1]);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InferenceStatus::Ok),
                          static_cast<int>(modelOutput.status));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::HighRiskTest),
                          static_cast<int>(riskOutput.risk));
    TEST_ASSERT_EQUAL_UINT64(2U, storage.flushCount);
    TEST_ASSERT_TRUE(storage.equals(
        "timestamp,tilt,soil,rain,stress,risk\n"
        "1000,0,55,25,0.800000012,HIGH_RISK_TEST\n"));
}

void testMockRuntimeFailurePropagatesUnknownToLogger() {
    project::ai::MockModelRuntime model{};
    MemoryCsvFileSystem storage{};
    project::models::ModelOutput modelOutput{};
    project::models::RiskAssessment riskOutput{};

    TEST_ASSERT_TRUE(runSingleSampleTrace(
        model, modelOutput, riskOutput, storage));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InferenceStatus::ModelError),
                          static_cast<int>(modelOutput.status));
    TEST_ASSERT_FALSE(modelOutput.predictionValid);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Unknown),
                          static_cast<int>(riskOutput.risk));
    TEST_ASSERT_TRUE(storage.equals(
        "timestamp,tilt,soil,rain,stress,risk\n"
        "1000,0,55,25,,UNKNOWN\n"));
}

void testMockRawSerialCsvMarksMissingAndSyntheticData() {
    project::models::SensorFrame frame{};
    frame.sequence = 7U;
    frame.timestampMs = 2500U;
    frame.imu.accelX = -10;
    frame.imu.accelY = 20;
    frame.imu.accelZ = 30;
    frame.soilMoistureRaw = 999;
    frame.rainRaw = 0;
    frame.hx710bRaw = 1234;
    frame.imuStatus =
        {SensorHealth::Healthy, true, 0U, 0U, frame.timestampMs};
    frame.soilStatus =
        {SensorHealth::Fault, false, 1U, 0U, frame.timestampMs};
    frame.rainStatus =
        {SensorHealth::Healthy, true, 0U, 0U, frame.timestampMs};
    frame.hx710bStatus =
        {SensorHealth::Healthy, true, 0U, 0U, frame.timestampMs};

    char row[160]{};
    std::size_t bytesWritten = 0U;
    TEST_ASSERT_TRUE(project::drivers::serializeMockCsvTelemetry(
        frame, row, sizeof(row), bytesWritten));
    TEST_ASSERT_EQUAL_STRING(
        "record_type,sequence_no,timestamp_ms,accel_x_raw,accel_y_raw,"
        "accel_z_raw,soil_raw,rain_raw,hx710b_raw,imu_valid,soil_valid,"
        "rain_valid,hx710b_valid,dataset_eligible,eligibility_reason\n",
        project::drivers::mockCsvTelemetryHeader());
    TEST_ASSERT_EQUAL_STRING(
        "MOCK_RAW_V1,7,2500,-10,20,30,,0,1234,true,false,true,true,"
        "false,SYNTHETIC_MOCK\n",
        row);
    TEST_ASSERT_EQUAL_UINT32(std::strlen(row), bytesWritten);

    char undersized[1]{};
    bytesWritten = 1U;
    TEST_ASSERT_FALSE(project::drivers::serializeMockCsvTelemetry(
        frame, undersized, sizeof(undersized), bytesWritten));
    TEST_ASSERT_EQUAL_UINT32(0U, bytesWritten);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(testSensorFeatureScriptedModelRiskAndCsvIntegration);
    RUN_TEST(testMockRuntimeFailurePropagatesUnknownToLogger);
    RUN_TEST(testMockRawSerialCsvMarksMissingAndSyntheticData);
    return UNITY_END();
}
