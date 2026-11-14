#include <unity.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

#include "project/AI/SchemaRegistry.h"
#include "project/AI/IInferenceEngine.h"
#include "project/AI/EdgeImpulseAdapter.h"
#include "project/AI/MockInferenceEngine.h"
#include "project/AI/MockModelRuntime.h"
#include "project/Dataset/CsvSchemas.h"
#include "project/Dataset/CsvWriter.h"
#include "project/Dataset/DsdRawSampleCsvParser.h"
#include "project/Dataset/DsdRawSampleCsvExporter.h"
#include "project/Dataset/DatasetValidator.h"
#include "project/Dataset/EvaluationRunner.h"
#include "project/Dataset/SyntheticSensorGenerator.h"
#include "project/Dataset/AnnotationManager.h"
#include "project/Drivers/MockCsvTelemetry.h"
#include "project/Drivers/ISensor.h"
#include "project/Managers/RiskManager.h"
#include "project/Managers/SensorManager.h"
#include "project/Services/CalibrationService.h"
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

struct MemoryDatasetCsvSource final : project::dataset::IDatasetCsvSource {
    const char* data{nullptr};
    std::size_t size{0U};
    std::size_t position{0U};

    MemoryDatasetCsvSource(const char* input,
                           std::size_t inputSize) noexcept
        : data(input), size(inputSize) {}

    project::dataset::DatasetCsvSourceStatus rewind() noexcept override {
        position = 0U;
        return project::dataset::DatasetCsvSourceStatus::Ok;
    }

    project::dataset::DatasetCsvSourceStatus read(
        std::uint8_t* output, std::size_t capacity,
        std::size_t& bytesRead) noexcept override {
        bytesRead = 0U;
        if (position == size) {
            return project::dataset::DatasetCsvSourceStatus::EndOfFile;
        }
        const std::size_t amount =
            std::min(capacity, size - position);
        std::memcpy(output, data + position, amount);
        position += amount;
        bytesRead = amount;
        return project::dataset::DatasetCsvSourceStatus::Ok;
    }
};

bool parseUnsignedField(std::string_view field,
                        std::uint64_t& value) noexcept {
    const auto result =
        std::from_chars(field.data(), field.data() + field.size(), value);
    return result.ec == std::errc{} &&
           result.ptr == field.data() + field.size();
}

bool parseSignedField(std::string_view field, std::int64_t& value) noexcept {
    const auto result =
        std::from_chars(field.data(), field.data() + field.size(), value);
    return result.ec == std::errc{} &&
           result.ptr == field.data() + field.size();
}

bool parseBooleanField(std::string_view field, bool& value) noexcept {
    if (field == "true") {
        value = true;
        return true;
    }
    if (field == "false") {
        value = false;
        return true;
    }
    return false;
}

project::models::SensorFrame sensorFrameFromRawCsv(
    const project::dataset::RawCsvRowView& row) noexcept {
    using project::models::SensorFrame;
    SensorFrame frame{};
    frame.schemaVersion = 1U;
    if (!parseUnsignedField(row.fields[5U], frame.bootId) ||
        !parseUnsignedField(row.fields[6U], frame.sequence) ||
        !parseUnsignedField(row.fields[7U], frame.timestampMs)) {
        return {};
    }
    frame.samplePeriodMs = 100U;
    std::int64_t raw[project::dataset::kSyntheticRawChannelCount]{};
    bool valid[project::dataset::kSyntheticRawChannelCount]{};
    for (std::size_t channel = 0U;
         channel < project::dataset::kSyntheticRawChannelCount; ++channel) {
        if (!parseSignedField(row.fields[14U + channel], raw[channel]) ||
            raw[channel] < std::numeric_limits<std::int32_t>::min() ||
            raw[channel] > std::numeric_limits<std::int32_t>::max() ||
            !parseBooleanField(row.fields[20U + channel], valid[channel])) {
            return {};
        }
    }
    frame.imu.accelX = static_cast<std::int32_t>(raw[0U]);
    frame.imu.accelY = static_cast<std::int32_t>(raw[1U]);
    frame.imu.accelZ = static_cast<std::int32_t>(raw[2U]);
    frame.soilMoistureRaw = static_cast<std::int32_t>(raw[3U]);
    frame.rainRaw = static_cast<std::int32_t>(raw[4U]);
    frame.hx710bRaw = static_cast<std::int32_t>(raw[5U]);
    const SensorStatus imuStatus{
        SensorHealth::Healthy, valid[0U] && valid[1U] && valid[2U],
        0U, 0U, frame.timestampMs};
    frame.imuStatus = imuStatus;
    frame.soilStatus = {SensorHealth::Healthy, valid[3U], 0U, 0U,
                        frame.timestampMs};
    frame.rainStatus = {SensorHealth::Healthy, valid[4U], 0U, 0U,
                        frame.timestampMs};
    frame.rainAoStatus = frame.rainStatus;
    frame.hx710bStatus = {SensorHealth::Healthy, valid[5U], 0U, 0U,
                          frame.timestampMs};
    return frame;
}

project::dataset::DsdRawSampleExportRecord rawExportRecord(
    const project::dataset::SyntheticRawObservation& observation,
    const project::models::ProcessedFrame& quality) noexcept {
    using project::dataset::DsdRawSampleExportRecord;
    using project::models::SensorChannel;

    DsdRawSampleExportRecord record{};
    record.siteId = "test-site";
    record.deviceId = "test-device";
    record.sensorSetId = "test-sensor-set";
    record.sessionId = "test-session";
    record.bootId = observation.bootId;
    record.sequenceNo = observation.sequenceNo;
    record.timestampMs = observation.timestampMs;
    record.timeStatus = project::models::TimeStatus::Unsynced;
    record.firmwareVersion = "host-test";
    record.configVersion = "test-config-v1";
    record.calibrationId = "NONE";
    record.sensorStatusBits = 0U;
    record.rawValues = observation.rawValues;
    record.channelValid = observation.channelValid;
    constexpr SensorChannel rawQualityChannels[] = {
        SensorChannel::ImuAccelXRaw, SensorChannel::ImuAccelYRaw,
        SensorChannel::ImuAccelZRaw, SensorChannel::SoilMoistureRaw,
        SensorChannel::RainRaw, SensorChannel::Hx710bRaw};
    for (std::size_t index = 0U; index < record.channelQuality.size();
         ++index) {
        record.channelQuality[index] = quality.channelQuality[
            static_cast<std::size_t>(rawQualityChannels[index])];
    }
    return record;
}

project::ai::InferenceModelMetadata inferenceTestMetadata() noexcept {
    project::ai::InferenceModelMetadata metadata{};
    metadata.engineKind = project::ai::InferenceEngineKind::TestOnly;
    metadata.featureSchemaVersion = 1U;
    metadata.featureSchemaId = 1U;
    metadata.featureSchemaHash[0] = 0xA5U;
    metadata.inputDimension = 2U;
    metadata.outputCount = 1U;
    metadata.featureSchemaHashValid = true;
    metadata.featureSchemaVerified = true;
    return metadata;
}

project::models::FeatureVector inferenceTestInput() noexcept {
    project::models::FeatureVector input{};
    input.schemaVersion = 1U;
    input.schemaId = 1U;
    input.schemaHashSha256[0] = 0xA5U;
    input.schemaHashValid = true;
    input.bootId = 1U;
    input.relatedSequenceNo = 2U;
    input.windowStartMs = 100U;
    input.windowEndMs = 200U;
    input.windowSampleCount = 2U;
    input.count = 2U;
    input.featureValidMask = 3U;
    input.valid = true;
    input.values[0] = 0.25F;
    input.values[1] = -0.5F;
    return input;
}

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
                frame.rainAoStatus =
                    {SensorHealth::Healthy, true, 0U, 0U, 1000U};
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
    project::ai::InferenceModelMetadata modelMetadata{
        inferenceTestMetadata()};
    project::models::FeatureVector capturedInput{};
    float scriptedOutput{0.8F};
    bool initialized{false};

    const project::ai::InferenceModelMetadata& metadata()
        const noexcept override {
        return modelMetadata;
    }

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
    config.modelScoreSemanticsApproved = true;
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

project::services::DataQualityConfig makeRawOnlyQualityConfig() noexcept {
    project::services::DataQualityConfig config{};
    config.requiredChannelsConfigured = true;
    constexpr SensorChannel required[] = {
        SensorChannel::AccelXG, SensorChannel::AccelYG,
        SensorChannel::AccelZG};
    for (const SensorChannel channel : required) {
        const std::size_t index = static_cast<std::size_t>(channel);
        config.requiredChannels[index] = true;
        config.calibrationPolicies[index] = {0U, true, false};
    }
    for (auto& freshness : config.freshness) {
        freshness.maximumAgeMs = 1000U;
        freshness.configured = true;
    }
    return config;
}

project::models::SensorFrame sensorFrameFromSynthetic(
    const project::dataset::SyntheticRawObservation& observation) noexcept {
    project::models::SensorFrame frame{};
    frame.schemaVersion = 1U;
    frame.bootId = observation.bootId;
    frame.sequence = observation.sequenceNo;
    frame.timestampMs = observation.timestampMs;
    frame.samplePeriodMs = 100U;
    frame.calibrationVersion = observation.calibrationVersion;
    frame.imu.accelX = static_cast<std::int32_t>(observation.rawValues[0U]);
    frame.imu.accelY = static_cast<std::int32_t>(observation.rawValues[1U]);
    frame.imu.accelZ = static_cast<std::int32_t>(observation.rawValues[2U]);
    frame.soilMoistureRaw =
        static_cast<std::int32_t>(observation.rawValues[3U]);
    frame.rainRaw = static_cast<std::int32_t>(observation.rawValues[4U]);
    frame.hx710bRaw = static_cast<std::int32_t>(observation.rawValues[5U]);
    const bool imuValid = observation.channelValid[0U] &&
                          observation.channelValid[1U] &&
                          observation.channelValid[2U];
    frame.imuStatus = {SensorHealth::Healthy, imuValid, 0U, 0U,
                       observation.timestampMs};
    frame.soilStatus = {SensorHealth::Healthy, observation.channelValid[3U],
                        0U, 0U, observation.timestampMs};
    frame.rainStatus = {SensorHealth::Healthy, observation.channelValid[4U],
                        0U, 0U, observation.timestampMs};
    frame.rainAoStatus = frame.rainStatus;
    frame.hx710bStatus = {SensorHealth::Healthy,
                          observation.channelValid[5U], 0U, 0U,
                          observation.timestampMs};
    return frame;
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

void testSyntheticRawOnlyDataQualityCsvReplayFailsClosed() {
    using namespace project::dataset;
    SyntheticGeneratorConfig generatorConfig{};
    generatorConfig.scenario = SyntheticScenario::Stable;
    generatorConfig.seed = 1907U;
    generatorConfig.recordCount = 3U;
    generatorConfig.bootId = 17U;
    generatorConfig.firstSequence = 4U;
    generatorConfig.firstTimestampMs = 5000U;
    generatorConfig.timestampStepMs = 100U;
    SyntheticSensorGenerator generator(generatorConfig);
    TEST_ASSERT_TRUE(generator.manifest().provenance.isValid());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataOrigin::Synthetic),
        static_cast<int>(generator.manifest().provenance.origin));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvidenceClass::GT5),
        static_cast<int>(generator.manifest().provenance.evidenceClass));

    char csv[8192]{};
    DsdRawSampleExportConfig exportConfig{};
    exportConfig.datasetId = "host-synthetic";
    exportConfig.provenance = generator.manifest().provenance;
    exportConfig.channelPresence.fill(true);
    exportConfig.channelPresenceConfigured = true;
    DsdRawSampleCsvExporter exporter(csv, sizeof(csv), exportConfig);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DsdRawSampleExportStatus::Ok),
        static_cast<int>(exporter.begin()));

    char header[1024]{};
    std::size_t headerSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::Ok),
        static_cast<int>(writeDsdV1_2RawSampleCoreHeader(
            header, sizeof(header), headerSize)));

    project::services::DataQualityService quality(
        makeRawOnlyQualityConfig());
    project::services::CalibrationService calibration{};
    project::services::FeaturePipeline features(makeFeatureConfig());
    SyntheticRecord generated[3U]{};
    DataQualityState originalRequiredQuality[3U]{};
    for (std::size_t index = 0U; index < 3U; ++index) {
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SyntheticGeneratorStatus::Ok),
            static_cast<int>(generator.next(generated[index])));
        TEST_ASSERT_TRUE(generated[index].observation.provenance.isValid());
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(EvidenceClass::GT5),
            static_cast<int>(
                generated[index].observation.provenance.evidenceClass));

        const auto frame = sensorFrameFromSynthetic(
            generated[index].observation);
        project::models::ProcessedFrame classified{};
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(
                project::services::DataQualityServiceStatus::Ok),
            static_cast<int>(quality.process(
                frame, generated[index].observation.evaluationTimeMs,
                classified)));
        originalRequiredQuality[index] =
            classified.channelQuality[static_cast<std::size_t>(
                SensorChannel::AccelXG)];
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(DataQualityState::CALIBRATION_INVALID),
            static_cast<int>(originalRequiredQuality[index]));

        project::models::ProcessedFrame rawOnly{};
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(project::models::ApiStatus::NotInitialized),
            static_cast<int>(calibration.processStatus(frame, rawOnly)));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(DataQualityState::VALID),
            static_cast<int>(rawOnly.channelQuality[static_cast<std::size_t>(
                SensorChannel::ImuAccelXRaw)]));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(DataQualityState::MISSING),
            static_cast<int>(rawOnly.channelQuality[static_cast<std::size_t>(
                SensorChannel::AccelXG)]));
        TEST_ASSERT_FALSE(features.push(rawOnly));
        TEST_ASSERT_FALSE(features.windowReady());
        const auto exportRecord =
            rawExportRecord(generated[index].observation, classified);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(DsdRawSampleExportStatus::Ok),
            static_cast<int>(exporter.append(exportRecord)));
    }

    RawDatasetManifest exportManifest{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DsdRawSampleExportStatus::Ok),
        static_cast<int>(exporter.finish(exportManifest)));
    TEST_ASSERT_EQUAL_UINT64(3U, exportManifest.expectedRows);
    TEST_ASSERT_EQUAL_UINT64(exporter.bytesWritten(),
                             exportManifest.expectedBytes);

    DatasetValidationWorkspace workspace{};
    IdentityWorkspaceEntry identityEntries[3U]{};
    std::uint8_t identityBytes[512U]{};
    workspace.entries = identityEntries;
    workspace.entryCapacity = 3U;
    workspace.keyBytes = identityBytes;
    workspace.keyByteCapacity = sizeof(identityBytes);
    QuarantineReference quarantine[3U]{};
    DatasetValidationReport validationReport{};
    MemoryDatasetCsvSource validationSource(csv, exporter.bytesWritten());
    DatasetValidator validator{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetValidationStatus::Complete),
        static_cast<int>(validator.validateRawSamples(
            validationSource, exportManifest, workspace, quarantine,
            3U, validationReport)));
    TEST_ASSERT_TRUE(validationReport.hashVerified);
    TEST_ASSERT_TRUE(validationReport.rowCountVerified);
    TEST_ASSERT_EQUAL_UINT64(3U, validationReport.acceptedRows);

    char insufficient[1U]{};
    DsdRawSampleCsvExporter capacityFailure(
        insufficient, sizeof(insufficient), exportConfig);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DsdRawSampleExportStatus::CapacityExceeded),
        static_cast<int>(capacityFailure.begin()));
    TEST_ASSERT_EQUAL_UINT32(
        0U, static_cast<std::uint32_t>(capacityFailure.bytesWritten()));
    char invalidOutput[8192]{};
    DsdRawSampleCsvExporter invalidRecordExporter(
        invalidOutput, sizeof(invalidOutput), exportConfig);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DsdRawSampleExportStatus::Ok),
        static_cast<int>(invalidRecordExporter.begin()));
    auto invalidExportRecord = rawExportRecord(
        generated[0].observation,
        project::models::ProcessedFrame{});
    invalidExportRecord.channelValid[0U] = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DsdRawSampleExportStatus::InvalidRecord),
        static_cast<int>(invalidRecordExporter.append(invalidExportRecord)));
    RawDatasetManifest invalidManifest{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DsdRawSampleExportStatus::InvalidState),
        static_cast<int>(invalidRecordExporter.finish(invalidManifest)));

    const std::size_t csvSize = exporter.bytesWritten();
    MemoryDatasetCsvSource source(csv, csvSize);
    DsdRawSampleCsvParser parser(source);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RawCsvParserStatus::Row),
                          static_cast<int>(parser.begin()));
    RawCsvRowView row{};
    for (std::size_t index = 0U; index < 3U; ++index) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(RawCsvParserStatus::Row),
                              static_cast<int>(parser.next(row)));
        std::uint64_t parsedSequence = 0U;
        TEST_ASSERT_TRUE(parseUnsignedField(row.fields[6U], parsedSequence));
        TEST_ASSERT_EQUAL_UINT64(generated[index].observation.sequenceNo,
                                 parsedSequence);
        const auto replayFrame = sensorFrameFromRawCsv(row);
        TEST_ASSERT_EQUAL_UINT64(generated[index].observation.bootId,
                                 replayFrame.bootId);
        TEST_ASSERT_EQUAL_UINT64(generated[index].observation.timestampMs,
                                 replayFrame.timestampMs);
        TEST_ASSERT_EQUAL_INT32(
            static_cast<std::int32_t>(
                generated[index].observation.rawValues[0U]),
            replayFrame.imu.accelX);
        project::models::ProcessedFrame replayQuality{};
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(
                project::services::DataQualityServiceStatus::Ok),
            static_cast<int>(quality.process(
                replayFrame, replayFrame.timestampMs, replayQuality)));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(originalRequiredQuality[index]),
            static_cast<int>(replayQuality.channelQuality[
                static_cast<std::size_t>(SensorChannel::AccelXG)]));
        project::models::ProcessedFrame replayRawOnly{};
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(project::models::ApiStatus::NotInitialized),
            static_cast<int>(
                calibration.processStatus(replayFrame, replayRawOnly)));
        TEST_ASSERT_FALSE(features.push(replayRawOnly));
        TEST_ASSERT_FALSE(features.windowReady());
    }
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RawCsvParserStatus::EndOfFile),
        static_cast<int>(parser.next(row)));

    auto missingFrame =
        sensorFrameFromSynthetic(generated[0].observation);
    missingFrame.imuStatus.valid = false;
    project::models::ProcessedFrame faultOutput{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DataQualityServiceStatus::Ok),
        static_cast<int>(quality.process(
            missingFrame, missingFrame.timestampMs, faultOutput)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataQualityState::MISSING),
        static_cast<int>(faultOutput.channelQuality[
            static_cast<std::size_t>(SensorChannel::AccelXG)]));

    auto staleFrame = sensorFrameFromSynthetic(generated[0].observation);
    staleFrame.imuStatus.timestampMs -= 2000U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DataQualityServiceStatus::Ok),
        static_cast<int>(quality.process(
            staleFrame, staleFrame.timestampMs, faultOutput)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataQualityState::STALE),
        static_cast<int>(faultOutput.channelQuality[
            static_cast<std::size_t>(SensorChannel::AccelXG)]));

    auto calibrationMismatch =
        sensorFrameFromSynthetic(generated[0].observation);
    calibrationMismatch.calibrationVersion = 1U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::VersionMismatch),
        static_cast<int>(
            calibration.processStatus(calibrationMismatch, faultOutput)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::DataContinuityState::SequenceGap),
        static_cast<int>(project::services::DataQualityService::
                             classifyContinuity(7U, 1U, 1000U, 7U, 3U,
                                                1200U, 100.0, 0U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::DataContinuityState::BootChanged),
        static_cast<int>(project::services::DataQualityService::
                             classifyContinuity(7U, 1U, 1000U, 8U, 1U,
                                                100U, 100.0, 0U)));

    char malformedCsv[1200]{};
    std::memcpy(malformedCsv, header, headerSize);
    static constexpr char wrongColumnRow[] = "bad,row\n";
    std::memcpy(malformedCsv + headerSize, wrongColumnRow,
                sizeof(wrongColumnRow) - 1U);
    MemoryDatasetCsvSource malformedSource(
        malformedCsv, headerSize + sizeof(wrongColumnRow) - 1U);
    DsdRawSampleCsvParser malformedParser(malformedSource);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RawCsvParserStatus::Row),
                          static_cast<int>(malformedParser.begin()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RawCsvParserStatus::WrongColumnCount),
        static_cast<int>(malformedParser.next(row)));

    AnnotationRecord unlabeledAnnotation{};
    TEST_ASSERT_TRUE(unlabeledAnnotation.annotationId.assign("unlabeled-gt5"));
    TEST_ASSERT_TRUE(unlabeledAnnotation.datasetId.assign("host-synthetic"));
    TEST_ASSERT_TRUE(unlabeledAnnotation.siteId.assign("test-site"));
    TEST_ASSERT_TRUE(unlabeledAnnotation.sessionId.assign("test-session"));
    unlabeledAnnotation.bootId = generatorConfig.bootId;
    unlabeledAnnotation.intervalStartMs =
        generated[0].observation.timestampMs;
    unlabeledAnnotation.intervalEndMs =
        generated[2].observation.timestampMs;
    unlabeledAnnotation.status = AnnotationStatus::Unlabeled;
    unlabeledAnnotation.provenance = exportManifest.provenance;
    unlabeledAnnotation.kind = AnnotationKind::SyntheticProxy;
    AnnotationRecord annotationStorage[1U]{};
    AnnotationManager annotations(annotationStorage, 1U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::Ok),
        static_cast<int>(annotations.add(unlabeledAnnotation, {})));
    AnnotationWindowInterval annotationInterval{
        "host-synthetic", "test-site", "test-session",
        generatorConfig.bootId, generated[0].observation.timestampMs,
        generated[2].observation.timestampMs};
    TEST_ASSERT_TRUE(AnnotationManager::matchesWindow(
        *annotations.record(0U), annotationInterval));
    annotationInterval.bootId++;
    TEST_ASSERT_FALSE(AnnotationManager::matchesWindow(
        *annotations.record(0U), annotationInterval));
    annotationInterval.bootId = generatorConfig.bootId;

    project::ai::SchemaRegistry productionSchemaGate{};
    TEST_ASSERT_FALSE(productionSchemaGate.compatible());
    project::ai::EdgeImpulseAdapter productionInference{};
    TEST_ASSERT_FALSE(productionInference.initialize());
    project::managers::RiskManager unconfiguredRisk{};
    TEST_ASSERT_FALSE(unconfiguredRisk.configured());

    project::models::FeatureVector rejectedFeatures{};
    rejectedFeatures.schemaVersion = 1U;
    rejectedFeatures.schemaId = 1U;
    rejectedFeatures.schemaHashSha256[0] = 0xA5U;
    rejectedFeatures.schemaHashValid = true;
    rejectedFeatures.count = 2U;
    rejectedFeatures.quality.eligibility =
        project::models::WindowEligibilityState::Rejected;
    rejectedFeatures.quality.rejectionReason =
        project::models::WindowRejectionReason::RequiredChannelQuality;
    rejectedFeatures.quality.channelQuality.fill(DataQualityState::MISSING);

    EvaluationConfig evaluationConfig{};
    evaluationConfig.featureSchemaVersion = 1U;
    evaluationConfig.featureSchemaId = 1U;
    evaluationConfig.featureSchemaHash[0] = 0xA5U;
    evaluationConfig.featureCount = 2U;
    evaluationConfig.featureSchemaHashValid = true;
    evaluationConfig.featureSchemaApproved = true;
    EvaluationWindow evaluationWindow{};
    evaluationWindow.windowId = "synthetic-window-unavailable";
    evaluationWindow.interval = {
        "host-synthetic", "test-site", "test-session", generatorConfig.bootId,
        generated[0].observation.timestampMs,
        generated[2].observation.timestampMs};
    evaluationWindow.provenance = exportManifest.provenance;
    evaluationWindow.split = DatasetSplit::Test;
    evaluationWindow.features = &rejectedFeatures;
    evaluationWindow.annotation = annotations.record(0U);
    EvaluationRunner evaluation(evaluationConfig);
    EvaluationReport report{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(evaluation.evaluate(&evaluationWindow, 1U, report)));
    const auto& bucket = report.buckets
        [static_cast<std::size_t>(DataOrigin::Synthetic)]
        [static_cast<std::size_t>(EvidenceClass::GT5)]
        [static_cast<std::size_t>(DatasetSplit::Test)];
    TEST_ASSERT_EQUAL_UINT64(1U, bucket.windowCount);
    TEST_ASSERT_EQUAL_UINT64(1U, bucket.featureRejectedCount);
    TEST_ASSERT_EQUAL_UINT64(1U, bucket.excludedLabelCount);
    TEST_ASSERT_EQUAL_UINT64(0U, bucket.eligibleLabelCount);
    TEST_ASSERT_FALSE(report.regressionMetricsEnabled);
    char summaryRow[1024]{};
    std::size_t summaryBytes = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(EvaluationRunner::writeSummaryCsvRow(
            report, DataOrigin::Synthetic, EvidenceClass::GT5,
            DatasetSplit::Test, summaryRow, sizeof(summaryRow),
            summaryBytes)));
    TEST_ASSERT_TRUE(summaryBytes > 0U);
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
    project::ai::MockInferenceConfig config{};
    config.metadata = inferenceTestMetadata();
    config.rawOutputs[0] = 0.8F;
    config.outputCount = 1U;
    config.forcedStatus = InferenceStatus::ModelError;
    project::ai::MockInferenceEngine model{config};
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

void testMockInferenceEngineReturnsConfiguredRawOutputOnly() {
    project::ai::MockInferenceConfig config{};
    config.metadata = inferenceTestMetadata();
    config.rawOutputs[0] = 0.625F;
    config.outputCount = 1U;
    project::ai::MockInferenceEngine model{config};
    auto input = inferenceTestInput();
    project::models::ModelOutput output{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::ai::InferenceEngineKind::TestOnly),
        static_cast<int>(model.metadata().engineKind));
    TEST_ASSERT_TRUE(model.initialize());
    TEST_ASSERT_TRUE(model.run(input, output));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InferenceStatus::Ok),
                          static_cast<int>(output.status));
    TEST_ASSERT_EQUAL_UINT16(1U, output.outputCount);
    TEST_ASSERT_FLOAT_WITHIN(0.0001F, 0.625F, output.rawOutputs[0]);
    TEST_ASSERT_FALSE(output.predictionValid);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(InferenceStatus::Ok),
        static_cast<int>(project::ai::validateInferenceOutput(
            model.metadata(), output)));

    output.outputCount = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(InferenceStatus::ModelError),
        static_cast<int>(project::ai::validateInferenceOutput(
            model.metadata(), output)));
    output.outputCount = 1U;
    output.rawOutputs[0] = std::numeric_limits<float>::infinity();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(InferenceStatus::NonFiniteOutput),
        static_cast<int>(project::ai::validateInferenceOutput(
            model.metadata(), output)));
    output.status = static_cast<InferenceStatus>(255U);
    output.outputCount = 0U;
    output.predictionValid = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(InferenceStatus::ModelError),
        static_cast<int>(project::ai::validateInferenceOutput(
            model.metadata(), output)));
}

void testMockInferenceEngineRejectsSchemaAndInvalidOutputConfiguration() {
    project::ai::MockInferenceConfig config{};
    config.metadata = inferenceTestMetadata();
    config.rawOutputs[0] = 0.5F;
    config.outputCount = 1U;
    project::ai::MockInferenceEngine model{config};
    auto input = inferenceTestInput();
    input.schemaId++;
    project::models::ModelOutput output{};

    TEST_ASSERT_TRUE(model.initialize());
    TEST_ASSERT_TRUE(model.run(input, output));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InferenceStatus::InvalidInput),
                          static_cast<int>(output.status));
    TEST_ASSERT_EQUAL_UINT16(0U, output.outputCount);

    config.outputCount = 2U;
    project::ai::MockInferenceEngine invalidCount{config};
    TEST_ASSERT_FALSE(invalidCount.initialize());
    config.outputCount = 1U;
    config.rawOutputs[0] = std::numeric_limits<float>::quiet_NaN();
    project::ai::MockInferenceEngine nonFinite{config};
    TEST_ASSERT_FALSE(nonFinite.initialize());
}

void testEdgeImpulseAdapterExposesMetadataAndFailsClosedWithoutExport() {
    project::ai::EdgeImpulseModelConfig config{};
    config.featureSchemaVersion = 1U;
    config.featureSchemaId = 1U;
    config.featureSchemaHash[0] = 0xA5U;
    config.modelHash[0] = 0x5AU;
    config.inputDimension = 2U;
    config.outputCount = 1U;
    config.featureSchemaApproved = true;
    config.modelMetadataVerified = true;
    config.modelHashVerified = true;
    config.outputContractApproved = true;
    project::ai::EdgeImpulseAdapter adapter{config};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::ai::InferenceEngineKind::EdgeImpulse),
        static_cast<int>(adapter.metadata().engineKind));
    TEST_ASSERT_TRUE(adapter.metadata().isComplete());
    TEST_ASSERT_FALSE(adapter.initialize());

    auto input = inferenceTestInput();
    project::models::ModelOutput output{};
    TEST_ASSERT_FALSE(adapter.run(input, output));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(InferenceStatus::ModelError),
                          static_cast<int>(output.status));
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
        "rain_valid,hx710b_valid,dataset_eligible,eligibility_reason,"
        "data_origin,evidence_class\n",
        project::drivers::mockCsvTelemetryHeader());
    TEST_ASSERT_EQUAL_STRING(
        "MOCK_RAW_V2,7,2500,-10,20,30,,0,1234,true,false,true,true,"
        "false,SYNTHETIC_MOCK,SYNTHETIC,GT5\n",
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
    RUN_TEST(testSyntheticRawOnlyDataQualityCsvReplayFailsClosed);
    RUN_TEST(testSensorFeatureScriptedModelRiskAndCsvIntegration);
    RUN_TEST(testMockRuntimeFailurePropagatesUnknownToLogger);
    RUN_TEST(testMockInferenceEngineReturnsConfiguredRawOutputOnly);
    RUN_TEST(testMockInferenceEngineRejectsSchemaAndInvalidOutputConfiguration);
    RUN_TEST(testEdgeImpulseAdapterExposesMetadataAndFailsClosedWithoutExport);
    RUN_TEST(testMockRawSerialCsvMarksMissingAndSyntheticData);
    return UNITY_END();
}
