#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "project/Drivers/ISensor.h"
#include "project/AI/SchemaRegistry.h"
#include "project/Dataset/CsvSchemas.h"
#include "project/Dataset/DataProvenance.h"
#include "project/Dataset/AnnotationManager.h"
#include "project/Dataset/DatasetValidator.h"
#include "project/Dataset/DatasetSplitter.h"
#include "project/Dataset/EvaluationRunner.h"
#include "project/Dataset/DsdRawSampleCsvParser.h"
#include "project/Dataset/Sha256.h"
#include "project/Dataset/SyntheticSensorGenerator.h"
#include "project/Managers/AppController.h"
#include "project/Managers/OutputController.h"
#include "project/Managers/RecoveryManager.h"
#include "project/Managers/RiskManager.h"
#include "project/Managers/SensorManager.h"
#include "project/Managers/ConfigManager.h"
#include "project/Storage/IStorage.h"
#include "project/Storage/StorageManager.h"
#include "project/Dataset/CsvWriter.h"
#include "project/Models/FeatureVector.h"
#include "project/Models/ModelOutput.h"
#include "project/Models/ProcessedFrame.h"
#include "project/Models/RiskAssessment.h"
#include "project/Models/SensorFrame.h"
#include "project/Models/Serialization.h"
#include "project/Replay/ReplayEngine.h"
#include "project/Services/CalibrationService.h"
#include "project/Services/DataQualityService.h"
#include "project/Services/FeaturePipeline.h"

// TEST ONLY: the native-unit-tests source filter omits ConfigManager.cpp.
#include "../../src/Managers/ConfigManager.cpp"
// TEST ONLY: the native-unit-tests source filter omits AppController.cpp.
#include "../../src/Managers/AppController.cpp"
// TEST ONLY: the native-unit-tests source filter omits CalibrationService.cpp.
#include "../../src/Services/CalibrationService.cpp"
// TEST ONLY: these host-only dataset components are covered by this suite.
#include "../../src/Dataset/AnnotationManager.cpp"
#include "../../src/Dataset/DatasetSplitter.cpp"
// TEST ONLY: the native-unit-tests source filter omits SchemaRegistry.cpp.
#include "../../src/AI/SchemaRegistry.cpp"
// TEST ONLY: the native-unit-tests source filter omits OutputController.cpp.
#include "../../src/Managers/OutputController.cpp"
// TEST ONLY: the native-unit-tests source filter omits RecoveryManager.cpp.
#include "../../src/Managers/RecoveryManager.cpp"
// TEST ONLY: the native-unit-tests source filter omits Serialization.cpp.
#include "../../src/Models/Serialization.cpp"
// TEST ONLY: the native-unit-tests source filter omits StorageManager.cpp.
#include "../../src/Storage/StorageManager.cpp"

namespace {

using project::models::DataQualityState;
using project::models::HealthState;
using project::models::InferenceStatus;
using project::models::RiskState;
using project::models::SensorChannel;
using project::models::SensorHealth;
using project::models::SensorStatus;

project::models::EventRecord serializableEventRecord() {
    project::models::EventRecord event{};
    event.schemaVersion = 2U;
    event.eventId = UINT64_MAX;
    event.eventSequenceNo = UINT32_MAX;
    event.bootId = UINT64_MAX - 1U;
    event.timestampMs = UINT64_MAX - 2U;
    std::strcpy(event.eventName, "SERIALIZE_TEST");
    std::strcpy(event.eventCatalogId, "EVENT_TEST");
    event.severity = project::models::EventSeverity::Notice;
    std::strcpy(event.source, "test_source");
    std::strcpy(event.statusCode, "DATA_INVALID");
    std::strcpy(event.errorCode, "E_TEST");
    event.errorCodeValid = true;
    std::strcpy(event.faultCode, "FAULT_TEST");
    event.faultCodeValid = true;
    event.riskState = RiskState::DangerTest;
    event.healthState = HealthState::Degraded;
    std::strcpy(event.manifestId, "manifest-123");
    event.manifestIdValid = true;
    std::strcpy(event.firmwareVersion, "2.0.0-test");
    std::strcpy(event.modelVersion, "model-7");
    event.modelVersionValid = true;
    for (std::size_t index = 0U; index < 32U; ++index) {
        event.featureSchemaHashSha256[index] =
            static_cast<std::uint8_t>(index);
    }
    event.featureSchemaHashValid = true;
    std::strcpy(event.configVersion, "risk-config-3");
    event.configVersionValid = true;
    std::strcpy(event.calibrationVersion, "calibration-4");
    event.calibrationVersionValid = true;
    event.relatedSequenceNo = UINT64_MAX - 3U;
    event.relatedSequenceNoValid = true;
    std::strcpy(event.message, "quoted: \"text\"; slash: \\; line\nbreak");
    event.valueType = project::models::EventRecord::ValueType::Numeric;
    event.value = -123.456F;
    std::strcpy(event.valueText, "unused text");
    std::strcpy(event.unit, "mm/h");
    event.valueValid = true;
    event.droppedEventCount = UINT32_MAX;
    event.crc32 = 0xDEADBEEFU;
    event.crc32Valid = true;
    event.valid = true;
    event.sensorId = project::models::SensorId::Hx710b;
    event.sensorIdValid = true;
    std::strcpy(event.stateBefore, "MONITOR");
    event.stateBeforeValid = true;
    std::strcpy(event.stateAfter, "DANGER_TEST");
    event.stateAfterValid = true;
    return event;
}

void assertEventRecordEqual(const project::models::EventRecord& expected,
                            const project::models::EventRecord& actual) {
    TEST_ASSERT_EQUAL_UINT16(expected.schemaVersion, actual.schemaVersion);
    TEST_ASSERT_EQUAL_UINT64(expected.eventId, actual.eventId);
    TEST_ASSERT_EQUAL_UINT32(expected.eventSequenceNo,
                             actual.eventSequenceNo);
    TEST_ASSERT_EQUAL_UINT64(expected.bootId, actual.bootId);
    TEST_ASSERT_EQUAL_UINT64(expected.timestampMs, actual.timestampMs);
    TEST_ASSERT_EQUAL_STRING(expected.eventName, actual.eventName);
    TEST_ASSERT_EQUAL_STRING(expected.eventCatalogId, actual.eventCatalogId);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.severity),
                          static_cast<int>(actual.severity));
    TEST_ASSERT_EQUAL_STRING(expected.source, actual.source);
    TEST_ASSERT_EQUAL_STRING(expected.statusCode, actual.statusCode);
    TEST_ASSERT_EQUAL_STRING(expected.errorCode, actual.errorCode);
    TEST_ASSERT_EQUAL(expected.errorCodeValid, actual.errorCodeValid);
    TEST_ASSERT_EQUAL_STRING(expected.faultCode, actual.faultCode);
    TEST_ASSERT_EQUAL(expected.faultCodeValid, actual.faultCodeValid);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.riskState),
                          static_cast<int>(actual.riskState));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.healthState),
                          static_cast<int>(actual.healthState));
    TEST_ASSERT_EQUAL_STRING(expected.manifestId, actual.manifestId);
    TEST_ASSERT_EQUAL(expected.manifestIdValid, actual.manifestIdValid);
    TEST_ASSERT_EQUAL_STRING(expected.firmwareVersion,
                             actual.firmwareVersion);
    TEST_ASSERT_EQUAL_STRING(expected.modelVersion, actual.modelVersion);
    TEST_ASSERT_EQUAL(expected.modelVersionValid, actual.modelVersionValid);
    for (std::size_t index = 0U; index < 32U; ++index) {
        TEST_ASSERT_EQUAL_UINT8(expected.featureSchemaHashSha256[index],
                                actual.featureSchemaHashSha256[index]);
    }
    TEST_ASSERT_EQUAL(expected.featureSchemaHashValid,
                      actual.featureSchemaHashValid);
    TEST_ASSERT_EQUAL_STRING(expected.configVersion, actual.configVersion);
    TEST_ASSERT_EQUAL(expected.configVersionValid, actual.configVersionValid);
    TEST_ASSERT_EQUAL_STRING(expected.calibrationVersion,
                             actual.calibrationVersion);
    TEST_ASSERT_EQUAL(expected.calibrationVersionValid,
                      actual.calibrationVersionValid);
    TEST_ASSERT_EQUAL_UINT64(expected.relatedSequenceNo,
                             actual.relatedSequenceNo);
    TEST_ASSERT_EQUAL(expected.relatedSequenceNoValid,
                      actual.relatedSequenceNoValid);
    TEST_ASSERT_EQUAL_STRING(expected.message, actual.message);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.valueType),
                          static_cast<int>(actual.valueType));
    if (std::isfinite(expected.value)) {
        TEST_ASSERT_EQUAL_FLOAT(expected.value, actual.value);
    } else {
        TEST_ASSERT_TRUE(std::isnan(actual.value));
    }
    TEST_ASSERT_EQUAL_STRING(expected.valueText, actual.valueText);
    TEST_ASSERT_EQUAL_STRING(expected.unit, actual.unit);
    TEST_ASSERT_EQUAL(expected.valueValid, actual.valueValid);
    TEST_ASSERT_EQUAL_UINT32(expected.droppedEventCount,
                             actual.droppedEventCount);
    TEST_ASSERT_EQUAL_UINT32(expected.crc32, actual.crc32);
    TEST_ASSERT_EQUAL(expected.crc32Valid, actual.crc32Valid);
    TEST_ASSERT_EQUAL(expected.valid, actual.valid);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.sensorId),
                          static_cast<int>(actual.sensorId));
    TEST_ASSERT_EQUAL(expected.sensorIdValid, actual.sensorIdValid);
    TEST_ASSERT_EQUAL_STRING(expected.stateBefore, actual.stateBefore);
    TEST_ASSERT_EQUAL(expected.stateBeforeValid, actual.stateBeforeValid);
    TEST_ASSERT_EQUAL_STRING(expected.stateAfter, actual.stateAfter);
    TEST_ASSERT_EQUAL(expected.stateAfterValid, actual.stateAfterValid);
}

bool replaceJsonText(std::uint8_t* json, std::size_t size,
                     const char* from, const char* to) {
    const std::size_t fromLength = std::strlen(from);
    if (fromLength != std::strlen(to) || fromLength > size) {
        return false;
    }
    for (std::size_t start = 0U; start <= size - fromLength; ++start) {
        if (std::memcmp(json + start, from, fromLength) == 0) {
            std::memcpy(json + start, to, fromLength);
            return true;
        }
    }
    return false;
}

bool insertJsonByteBefore(std::uint8_t* json, std::size_t& size,
                          std::size_t capacity, const char* needle,
                          std::uint8_t value) {
    const std::size_t needleLength = std::strlen(needle);
    if (size >= capacity || needleLength > size) {
        return false;
    }
    for (std::size_t start = 0U; start <= size - needleLength; ++start) {
        if (std::memcmp(json + start, needle, needleLength) == 0) {
            std::memmove(json + start + 1U, json + start, size - start);
            json[start] = value;
            ++size;
            return true;
        }
    }
    return false;
}

class FakeStorage final : public project::storage::IStorage {
public:
    bool begin() override {
        ++beginCalls;
        return beginResult;
    }

    bool append(const std::uint8_t* data, std::size_t size) override {
        ++appendCalls;
        if (partialWriteBytes != 0U) {
            const std::size_t committed =
                partialWriteBytes < size ? partialWriteBytes : size;
            bytes.insert(bytes.end(), data, data + committed);
            partialWriteBytes = 0U;
            return false;
        }
        if (!appendResult) {
            return false;
        }
        bytes.insert(bytes.end(), data, data + size);
        return true;
    }

    bool flush() override {
        ++flushCalls;
        return flushResult;
    }

    bool beginResult{true};
    bool appendResult{true};
    bool flushResult{true};
    std::size_t partialWriteBytes{0U};
    std::uint32_t beginCalls{0U};
    std::uint32_t appendCalls{0U};
    std::uint32_t flushCalls{0U};
    std::vector<std::uint8_t> bytes{};
};

project::storage::StorageManager makeStorageManager(
    FakeStorage& backend, std::size_t maximumRecordBytes = 8U) {
    return project::storage::StorageManager(
        backend, project::storage::StorageManagerConfig{maximumRecordBytes});
}

constexpr std::uint8_t kTestArtifact[] = {'a', 'b', 'c'};
constexpr std::uint8_t kBoundaryArtifact[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::uint8_t kTestArtifactSha256[32] = {
    0xbaU, 0x78U, 0x16U, 0xbfU, 0x8fU, 0x01U, 0xcfU, 0xeaU,
    0x41U, 0x41U, 0x40U, 0xdeU, 0x5dU, 0xaeU, 0x22U, 0x23U,
    0xb0U, 0x03U, 0x61U, 0xa3U, 0x96U, 0x17U, 0x7aU, 0x9cU,
    0xb4U, 0x10U, 0xffU, 0x61U, 0xf2U, 0x00U, 0x15U, 0xadU};
constexpr std::uint8_t kBoundaryArtifactSha256[32] = {
    0xffU, 0xe0U, 0x54U, 0xfeU, 0x7aU, 0xe0U, 0xcbU, 0x6dU,
    0xc6U, 0x5cU, 0x3aU, 0xf9U, 0xb6U, 0x1dU, 0x52U, 0x09U,
    0xf4U, 0x39U, 0x85U, 0x1dU, 0xb4U, 0x3dU, 0x0bU, 0xa5U,
    0x99U, 0x73U, 0x37U, 0xdfU, 0x15U, 0x46U, 0x68U, 0xebU};
constexpr std::array<std::uint8_t, 120> kMultiBlockArtifact = [] {
    std::array<std::uint8_t, 120> artifact{};
    for (std::uint8_t& byte : artifact) {
        byte = static_cast<std::uint8_t>('a');
    }
    return artifact;
}();
constexpr std::uint8_t kMultiBlockArtifactSha256[32] = {
    0x2fU, 0x3dU, 0x33U, 0x54U, 0x32U, 0xc7U, 0x0bU, 0x58U,
    0x0aU, 0xf0U, 0xe8U, 0xe1U, 0xb3U, 0x67U, 0x4aU, 0x7cU,
    0x02U, 0x0dU, 0x68U, 0x3aU, 0xa5U, 0xf7U, 0x3aU, 0xaaU,
    0xedU, 0xfdU, 0xc5U, 0x5aU, 0xf9U, 0x04U, 0xc2U, 0x1cU};

project::managers::RiskManagerConfig riskConfig() noexcept;

struct TestManifestFixture {
    project::services::FeaturePipelineConfig feature{};
    project::ai::EdgeImpulseModelConfig model{};
    project::managers::RiskManagerConfig risk{};
    project::managers::ConfigManifest manifest{};

    void bind() noexcept {
        manifest.featurePipeline = &feature;
        manifest.model = &model;
        manifest.risk = &risk;
    }
};

TestManifestFixture testManifestFixture() noexcept {
    TestManifestFixture fixture{};
    fixture.feature.schemaVersion = 1U;
    fixture.feature.schemaId = 7U;
    for (std::size_t i = 0U; i < sizeof(kTestArtifactSha256); ++i) {
        fixture.feature.schemaHash[i] = kTestArtifactSha256[i];
    }
    fixture.feature.schemaHashValid = true;
    fixture.feature.schemaApproved = true;
    fixture.feature.modelMetadataVerified = true;
    fixture.feature.flatLayoutApproved = true;
    fixture.feature.preprocessingApproved = true;
    fixture.feature.preprocessingVersion = 1U;
    fixture.feature.sampleRateHz = 10.0F;
    fixture.feature.sampleRateConfigured = true;
    fixture.feature.samplePeriodToleranceConfigured = true;
    fixture.feature.featureCount = 1U;
    fixture.feature.features[0] = {
        project::services::FssFeatureId::AccelMagnitude, 0U, true};
    fixture.feature.windowLengthSamples = 1U;
    fixture.feature.strideSamples = 1U;
    fixture.feature.accelerationToMps2 = 9.81F;
    fixture.feature.accelerationScaleConfigured = true;

    fixture.model.featureSchemaVersion = fixture.feature.schemaVersion;
    fixture.model.featureSchemaId = fixture.feature.schemaId;
    for (std::size_t i = 0U; i < sizeof(kTestArtifactSha256); ++i) {
        fixture.model.featureSchemaHash[i] = kTestArtifactSha256[i];
        fixture.model.modelHash[i] = kTestArtifactSha256[i];
    }
    fixture.model.inputDimension = 1U;
    fixture.model.outputCount = 1U;
    fixture.model.outputKind = project::ai::EdgeImpulseOutputKind::Regression;
    fixture.model.featureSchemaApproved = true;
    fixture.model.modelMetadataVerified = true;
    fixture.model.outputContractApproved = true;

    fixture.risk = riskConfig();
    fixture.manifest.formatVersion =
        project::managers::ConfigManager::kSupportedManifestVersion;
    fixture.manifest.purpose = project::managers::ManifestPurpose::TestOnly;
    return fixture;
}

class FakeConfigManifestPersistence final
    : public project::managers::IConfigManifestPersistence {
public:
    enum class FailurePoint : std::uint8_t {
        None,
        WriteCandidate,
        FlushCandidate,
        ReadBack,
        WriteCommitMarker,
        FlushCommitMarker
    };

    struct Slot {
        project::managers::ConfigManifest manifest{};
        project::services::DataQualityConfig dataQuality{};
        project::managers::SensorManagerConfig sensorManager{};
        project::services::FeaturePipelineConfig featurePipeline{};
        project::ai::EdgeImpulseModelConfig model{};
        project::managers::RiskManagerConfig risk{};
        std::array<std::uint8_t, 512U> featureSchemaArtifact{};
        std::array<std::uint8_t, 512U> modelArtifact{};
        std::array<std::uint8_t, 512U> riskConfigArtifact{};
        std::uint64_t generation{0U};
        project::managers::ConfigSlotReadStatus status{
            project::managers::ConfigSlotReadStatus::Empty};
        bool markerPending{false};
    };

    static bool copyArtifact(
        const project::managers::ManifestArtifactView& source,
        std::array<std::uint8_t, 512U>& destination,
        project::managers::ManifestArtifactView& output) noexcept {
        if (source.data == nullptr && source.size == 0U) {
            output = {};
            return true;
        }
        if (source.data == nullptr || source.size == 0U ||
            source.size > destination.size()) {
            return false;
        }
        std::memcpy(destination.data(), source.data, source.size);
        output = {destination.data(), source.size};
        return true;
    }

    static bool copyManifest(
        Slot& slot,
        const project::managers::ConfigManifest& source) noexcept {
        slot.manifest = source;
        if (source.dataQuality != nullptr) {
            slot.dataQuality = *source.dataQuality;
            slot.manifest.dataQuality = &slot.dataQuality;
        }
        if (source.sensorManager != nullptr) {
            slot.sensorManager = *source.sensorManager;
            slot.manifest.sensorManager = &slot.sensorManager;
        }
        if (source.featurePipeline != nullptr) {
            slot.featurePipeline = *source.featurePipeline;
            slot.manifest.featurePipeline = &slot.featurePipeline;
        }
        if (source.model != nullptr) {
            slot.model = *source.model;
            slot.manifest.model = &slot.model;
        }
        if (source.risk != nullptr) {
            slot.risk = *source.risk;
            slot.manifest.risk = &slot.risk;
        }
        return copyArtifact(source.featureSchemaArtifact,
                            slot.featureSchemaArtifact,
                            slot.manifest.featureSchemaArtifact) &&
               copyArtifact(source.modelArtifact, slot.modelArtifact,
                            slot.manifest.modelArtifact) &&
               copyArtifact(source.riskConfigArtifact,
                            slot.riskConfigArtifact,
                            slot.manifest.riskConfigArtifact);
    }

    bool writeCandidate(
        std::uint8_t slot, std::uint64_t generation,
        const project::managers::ConfigManifest& manifest) noexcept override {
        if (slot >= slots.size()) {
            return false;
        }
        slots[slot].status = project::managers::ConfigSlotReadStatus::Empty;
        slots[slot].markerPending = false;
        if (!copyManifest(slots[slot], manifest)) {
            slots[slot].status =
                project::managers::ConfigSlotReadStatus::Corrupt;
            return false;
        }
        slots[slot].generation = generation;
        slots[slot].status =
            project::managers::ConfigSlotReadStatus::Candidate;
        return failurePoint != FailurePoint::WriteCandidate;
    }

    bool flushCandidate(std::uint8_t slot) noexcept override {
        return slot < slots.size() &&
               failurePoint != FailurePoint::FlushCandidate;
    }

    project::managers::ConfigSlotReadStatus readSlot(
        std::uint8_t slot, std::uint64_t& generation,
        project::managers::ConfigManifest& manifest) noexcept override {
        using project::managers::ConfigSlotReadStatus;
        if (slot >= slots.size()) {
            return ConfigSlotReadStatus::IoError;
        }
        const Slot& stored = slots[slot];
        if (stored.status == ConfigSlotReadStatus::Candidate &&
            failurePoint == FailurePoint::ReadBack) {
            return ConfigSlotReadStatus::IoError;
        }
        generation = stored.generation;
        manifest = stored.manifest;
        return stored.status;
    }

    bool writeCommitMarker(std::uint8_t slot,
                           std::uint64_t generation) noexcept override {
        if (slot >= slots.size() || slots[slot].generation != generation ||
            slots[slot].status !=
                project::managers::ConfigSlotReadStatus::Candidate ||
            failurePoint == FailurePoint::WriteCommitMarker) {
            return false;
        }
        slots[slot].markerPending = true;
        return true;
    }

    bool flushCommitMarker(std::uint8_t slot) noexcept override {
        if (slot >= slots.size() || !slots[slot].markerPending ||
            failurePoint == FailurePoint::FlushCommitMarker) {
            return false;
        }
        slots[slot].status = project::managers::ConfigSlotReadStatus::Committed;
        slots[slot].markerPending = false;
        return true;
    }

    void seed(std::uint8_t slot, std::uint64_t generation,
              const project::managers::ConfigManifest& manifest) noexcept {
        if (slot >= slots.size()) {
            return;
        }
        slots[slot].status = project::managers::ConfigSlotReadStatus::Empty;
        slots[slot].markerPending = false;
        if (!copyManifest(slots[slot], manifest)) {
            slots[slot].status =
                project::managers::ConfigSlotReadStatus::Corrupt;
            return;
        }
        slots[slot].generation = generation;
        slots[slot].status =
            project::managers::ConfigSlotReadStatus::Committed;
    }

    std::array<Slot, 2U> slots{};
    FailurePoint failurePoint{FailurePoint::None};
};

class FakeAppLifecycle final : public project::managers::IAppLifecycle {
public:
    enum class Call : std::uint8_t {
        InferenceGate,
        StartPhase,
        CleanupPhase,
        StopAcceptingWork,
        DrainAndStopTasks,
        FlushStorage,
        ConfigureOutputsSafe,
        HealthCheck
    };

    struct Entry {
        Call call{Call::InferenceGate};
        project::managers::AppStartupPhase phase{
            project::managers::AppStartupPhase::BspSafeInit};
        bool value{false};
        std::uint32_t timeoutMs{0U};
    };

    bool initializeInferenceGate(bool enabled) noexcept override {
        record({Call::InferenceGate, {}, enabled});
        if (enabled && failEnableInference) {
            return false;
        }
        inferenceEnabled = enabled;
        return true;
    }

    bool startPhase(project::managers::AppStartupPhase phase) noexcept override {
        record({Call::StartPhase, phase});
        inferenceWasEnabledDuringStart =
            inferenceWasEnabledDuringStart || inferenceEnabled;
        return phase != failPhase;
    }

    bool cleanupPhase(
        project::managers::AppStartupPhase phase) noexcept override {
        record({Call::CleanupPhase, phase});
        return true;
    }

    bool stopAcceptingWork() noexcept override {
        record({Call::StopAcceptingWork});
        return true;
    }

    bool drainAndStopTasks(std::uint32_t timeoutMs) noexcept override {
        record({Call::DrainAndStopTasks, {}, false, timeoutMs});
        return drainResult;
    }

    bool flushStorageBounded(std::uint32_t timeoutMs) noexcept override {
        record({Call::FlushStorage, {}, false, timeoutMs});
        return flushResult;
    }

    bool configureOutputsSafe() noexcept override {
        record({Call::ConfigureOutputsSafe});
        return safeOutputResult;
    }

    bool healthCheckBounded(std::uint32_t timeoutMs) noexcept override {
        record({Call::HealthCheck, {}, false, timeoutMs});
        return healthResult;
    }

    void record(const Entry& entry) noexcept {
        if (callCount < calls.size()) {
            calls[callCount++] = entry;
        }
    }

    std::array<Entry, 128U> calls{};
    std::size_t callCount{0U};
    project::managers::AppStartupPhase failPhase{
        project::managers::AppStartupPhase::Count};
    bool inferenceEnabled{false};
    bool inferenceWasEnabledDuringStart{false};
    bool failEnableInference{false};
    bool drainResult{true};
    bool flushResult{true};
    bool safeOutputResult{true};
    bool healthResult{true};
};

struct TestSchemaRegistryFixture {
    project::services::FeaturePipelineConfig schema{};
    project::ai::EdgeImpulseModelConfig model{};
    std::array<project::ai::FeatureUnit, 2U> schemaUnits{};
    std::array<project::services::FssFeatureId, 2U> modelOrder{};
    std::array<project::ai::FeatureUnit, 2U> modelUnits{};
    project::ai::ModelInputMetadata modelInput{};
    project::ai::SchemaRegistryConfig descriptor{};

    TestSchemaRegistryFixture() noexcept {
        schema.schemaVersion = 4U;
        schema.schemaId = project::ai::SchemaRegistry::kTestOnlySchemaId;
        schema.schemaHashValid = true;
        for (std::size_t i = 0U; i < schema.schemaHash.size(); ++i) {
            schema.schemaHash[i] = static_cast<std::uint8_t>(i + 1U);
        }
        schema.schemaApproved = true;
        schema.modelMetadataVerified = true;
        schema.flatLayoutApproved = true;
        schema.preprocessingApproved = true;
        schema.preprocessingVersion = 3U;
        schema.featureCount = 2U;
        schema.features[0] = {
            project::services::FssFeatureId::TiltXDeg, 0U, true};
        schema.features[1] = {
            project::services::FssFeatureId::TiltYDeg, 1U, true};
        schema.windowLengthSamples = 2U;
        schema.strideSamples = 1U;
        schema.sampleRateHz = 10.0F;
        schema.sampleRateConfigured = true;
        schema.samplePeriodToleranceConfigured = true;
        schema.tiltFormulaApproved = true;

        schemaUnits = {project::ai::FeatureUnit::Degrees,
                       project::ai::FeatureUnit::Degrees};
        modelOrder = {project::services::FssFeatureId::TiltXDeg,
                      project::services::FssFeatureId::TiltYDeg};
        modelUnits = schemaUnits;
        modelInput.featureOrder = modelOrder.data();
        modelInput.featureOrderCount = modelOrder.size();
        modelInput.featureUnits = modelUnits.data();
        modelInput.featureUnitCount = modelUnits.size();
        modelInput.preprocessingVersion = schema.preprocessingVersion;
        modelInput.windowLengthSamples = schema.windowLengthSamples;
        modelInput.strideSamples = schema.strideSamples;
        modelInput.sampleRateHz = schema.sampleRateHz;
        modelInput.verified = true;

        model.featureSchemaVersion = schema.schemaVersion;
        model.featureSchemaId = schema.schemaId;
        for (std::size_t i = 0U; i < schema.schemaHash.size(); ++i) {
            model.featureSchemaHash[i] = schema.schemaHash[i];
            model.modelHash[i] = static_cast<std::uint8_t>(0xa0U + i);
        }
        model.inputDimension = 4U;
        model.outputCount = 1U;
        model.featureSchemaApproved = true;
        model.modelMetadataVerified = true;
        model.modelHashVerified = true;
        model.outputContractApproved = true;
        bind();
    }

    void bind() noexcept {
        descriptor.kind = project::ai::SchemaDescriptorKind::TestOnly;
        descriptor.schema = &schema;
        descriptor.schemaUnits = schemaUnits.data();
        descriptor.schemaUnitCount = schemaUnits.size();
        descriptor.model = &model;
        descriptor.modelInput = &modelInput;
    }

    project::models::FeatureVector vector() const noexcept {
        project::models::FeatureVector result{};
        result.schemaVersion = schema.schemaVersion;
        result.schemaId = schema.schemaId;
        for (std::size_t i = 0U; i < schema.schemaHash.size(); ++i) {
            result.schemaHashSha256[i] = schema.schemaHash[i];
        }
        result.schemaHashValid = true;
        result.bootId = 1U;
        result.relatedSequenceNo = 2U;
        result.windowStartMs = 100U;
        result.windowEndMs = 200U;
        result.windowSampleCount = schema.windowLengthSamples;
        result.count = model.inputDimension;
        result.featureValidMask = (UINT32_C(1) << result.count) - 1U;
        result.valid = true;
        for (std::size_t i = 0U; i < result.count; ++i) {
            result.values[i] = static_cast<float>(i + 1U);
        }
        return result;
    }

    project::ai::SchemaRegistry registry() const noexcept {
        return project::ai::SchemaRegistry(descriptor);
    }
};

void assertSchemaMismatch(
    TestSchemaRegistryFixture& fixture,
    project::ai::SchemaMismatchReason expectedReason,
    project::ai::SchemaField expectedField) {
    project::ai::SchemaCompatibilityReport report{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::VersionMismatch),
        static_cast<int>(fixture.registry().compareDetailed(report)));
    TEST_ASSERT_FALSE(report.structurallyCompatible);
    TEST_ASSERT_FALSE(report.productionCompatible);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expectedReason),
                          static_cast<int>(report.reason));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expectedField),
                          static_cast<int>(report.field));
}

bool reportHasIssue(const project::managers::ValidationReport& report,
                    project::managers::ManifestErrorCode code,
                    const char* fieldPath) noexcept {
    for (std::size_t i = 0U; i < report.issueCount; ++i) {
        const auto& issue = report.issues[i];
        if (issue.code == code && issue.fieldPath != nullptr &&
            std::strcmp(issue.fieldPath, fieldPath) == 0) {
            return true;
        }
    }
    return false;
}

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
    config.modelScoreSemanticsApproved = true;
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
    TEST_ASSERT_FLOAT_WITHIN(0.02F, 45.0F, output.values[2]);
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

    context.sequenceNo = 3U;
    invalidModel.status = InferenceStatus::VersionMismatch;
    invalidModel.predictionValue = 0.8F;
    invalidModel.rawOutputs[0] = 0.8F;
    invalidModel.predictionValid = true;
    TEST_ASSERT_TRUE(manager.evaluate(
        invalidModel, notApprovedTilt, DataQualityState::VALID,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Unknown),
                          static_cast<int>(result.risk));
    TEST_ASSERT_FALSE(result.riskScoreValid);
}

void testRiskManagerRequiresApprovedScoreSemanticsAndConsistentOutput() {
    auto unapprovedConfig = riskConfig();
    unapprovedConfig.modelScoreSemanticsApproved = false;
    project::managers::RiskManager unapproved(unapprovedConfig);
    TEST_ASSERT_FALSE(unapproved.configured());

    project::models::RiskAssessment result{};
    project::managers::RiskEvaluationContext context{
        7U, 1U, 1000U, HealthState::Healthy};
    const project::managers::TiltValidation notApprovedTilt{};
    TEST_ASSERT_TRUE(unapproved.evaluate(
        modelOutput(0.1F), notApprovedTilt, DataQualityState::VALID,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Unknown),
                          static_cast<int>(result.risk));
    TEST_ASSERT_FALSE(result.riskScoreValid);

    project::managers::RiskManager manager(riskConfig());
    auto inconsistentOutput = modelOutput(0.1F);
    inconsistentOutput.rawOutputs[0] = 0.9F;
    TEST_ASSERT_TRUE(manager.evaluate(
        inconsistentOutput, notApprovedTilt, DataQualityState::VALID,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Unknown),
                          static_cast<int>(result.risk));
    TEST_ASSERT_FALSE(result.riskScoreValid);

    auto nonFiniteRawOutput = modelOutput(0.1F);
    nonFiniteRawOutput.rawOutputs[0] =
        std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_TRUE(manager.evaluate(
        nonFiniteRawOutput, notApprovedTilt, DataQualityState::VALID,
        context, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Unknown),
                          static_cast<int>(result.risk));
    TEST_ASSERT_FALSE(result.riskScoreValid);
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

    model.sourceFeatureSchemaHashValid = false;
    TEST_ASSERT_TRUE(manager.evaluate(model, frame, health, result));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RiskState::Unknown),
                          static_cast<int>(result.risk));
    TEST_ASSERT_FALSE(result.riskScoreValid);
    model.sourceFeatureSchemaHashValid = true;

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

void testOutputPolicyMapsEveryRiskStateWithoutHardwareSemantics() {
    using project::managers::OutputCommandKind;
    using project::managers::OutputCommandStatus;
    using project::models::RiskAssessment;

    struct MappingCase {
        RiskState risk;
        OutputCommandKind command;
    };
    const MappingCase cases[] = {
        {RiskState::Startup, OutputCommandKind::Startup},
        {RiskState::Normal, OutputCommandKind::Normal},
        {RiskState::Safe, OutputCommandKind::Normal},
        {RiskState::Monitor, OutputCommandKind::Monitor},
        {RiskState::DangerTest, OutputCommandKind::DangerTest},
        {RiskState::HighRiskTest, OutputCommandKind::DangerTest},
        {RiskState::Unknown, OutputCommandKind::Unknown},
    };

    for (const auto& testCase : cases) {
        RiskAssessment assessment{};
        assessment.valid = true;
        assessment.risk = testCase.risk;
        const auto command =
            project::managers::OutputController::deriveCommand(assessment);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(OutputCommandStatus::Valid),
                              static_cast<int>(command.status));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(testCase.command),
                              static_cast<int>(command.kind));
    }
}

void testOutputPolicyRejectsInvalidAssessmentsAndRiskEnums() {
    using project::managers::OutputCommandKind;
    using project::managers::OutputCommandStatus;
    using project::models::RiskAssessment;

    RiskAssessment assessment{};
    assessment.risk = RiskState::Normal;
    auto command =
        project::managers::OutputController::deriveCommand(assessment);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(OutputCommandStatus::InvalidAssessment),
        static_cast<int>(command.status));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OutputCommandKind::None),
                          static_cast<int>(command.kind));

    assessment.valid = true;
    assessment.risk = static_cast<RiskState>(0xFFU);
    command = project::managers::OutputController::deriveCommand(assessment);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(OutputCommandStatus::InvalidRiskState),
        static_cast<int>(command.status));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OutputCommandKind::None),
                          static_cast<int>(command.kind));

    assessment.risk = RiskState::Normal;
    assessment.alarmOutputState =
        static_cast<project::models::AlarmOutputState>(0xFFU);
    command = project::managers::OutputController::deriveCommand(assessment);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(OutputCommandStatus::InvalidFeedbackState),
        static_cast<int>(command.status));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OutputCommandKind::None),
                          static_cast<int>(command.kind));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::AlarmOutputState::NotVerifiable),
        static_cast<int>(command.feedback));
}

void testOutputPolicyPreservesAbsentAndUnverifiableFeedback() {
    using project::managers::OutputCommandKind;
    using project::managers::OutputCommandStatus;
    using project::models::AlarmOutputState;
    using project::models::RiskAssessment;

    const AlarmOutputState feedbackStates[] = {
        AlarmOutputState::NotPresent,
        AlarmOutputState::NotVerifiable,
    };
    for (const auto feedback : feedbackStates) {
        RiskAssessment assessment{};
        assessment.valid = true;
        assessment.risk = RiskState::Monitor;
        assessment.alarmOutputState = feedback;
        const auto command =
            project::managers::OutputController::deriveCommand(assessment);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(OutputCommandStatus::Valid),
                              static_cast<int>(command.status));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(OutputCommandKind::Monitor),
                              static_cast<int>(command.kind));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(feedback),
                              static_cast<int>(command.feedback));
    }
}

void testOutputPolicyRepeatedInputProducesIdenticalCommand() {
    const project::models::RiskAssessment assessment = [] {
        project::models::RiskAssessment value{};
        value.valid = true;
        value.risk = RiskState::DangerTest;
        value.sequenceNo = 17U;
        return value;
    }();
    const auto first =
        project::managers::OutputController::deriveCommand(assessment);
    const auto repeated =
        project::managers::OutputController::deriveCommand(assessment);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(first.status),
                          static_cast<int>(repeated.status));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(first.kind),
                          static_cast<int>(repeated.kind));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(first.feedback),
                          static_cast<int>(repeated.feedback));
}

void testRecoveryManagerClassifiesKnownAndUnknownCatalogValues() {
    using project::managers::RecoveryDecision;
    using project::managers::RecoveryManager;
    using project::managers::RetryPolicy;
    using project::models::ApiStatus;
    using project::models::EventSeverity;
    using project::models::FaultKind;

    const project::managers::RecoveryContext sensorContext{
        FaultKind::SensorFault, false};
    const auto known = RecoveryManager::classifyError(
        ApiStatus::Timeout, sensorContext, RetryPolicy{2U, true});
    TEST_ASSERT_TRUE(known.recognized);
    TEST_ASSERT_TRUE(known.hasFault);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultKind::SensorFault),
                          static_cast<int>(known.faultKind));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventSeverity::Warn),
                          static_cast<int>(known.severity));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryDecision::Retry),
                          static_cast<int>(known.decision));

    const ApiStatus knownStatuses[] = {
        ApiStatus::Ok,
        ApiStatus::InvalidArgument,
        ApiStatus::NotInitialized,
        ApiStatus::Timeout,
        ApiStatus::IoError,
        ApiStatus::DataInvalid,
        ApiStatus::StaleData,
        ApiStatus::BufferFull,
        ApiStatus::NotSupported,
        ApiStatus::VersionMismatch,
        ApiStatus::ResourceExhausted,
        ApiStatus::InternalError,
        ApiStatus::Busy,
        ApiStatus::Cancelled,
    };
    for (const auto status : knownStatuses) {
        const auto classification = RecoveryManager::classifyError(
            status, sensorContext, RetryPolicy{2U, true});
        TEST_ASSERT_TRUE(classification.recognized);
    }

    const auto unknownStatus = RecoveryManager::classifyError(
        static_cast<ApiStatus>(0xFFU), sensorContext, RetryPolicy{2U, true});
    TEST_ASSERT_FALSE(unknownStatus.recognized);
    TEST_ASSERT_TRUE(unknownStatus.hasFault);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryDecision::ManualReview),
                          static_cast<int>(unknownStatus.decision));

    const project::managers::RecoveryContext unknownFaultContext{
        static_cast<FaultKind>(0xFFU), false};
    const auto unknownFault = RecoveryManager::classifyError(
        ApiStatus::Timeout, unknownFaultContext, RetryPolicy{2U, true});
    TEST_ASSERT_FALSE(unknownFault.recognized);
    TEST_ASSERT_TRUE(unknownFault.hasFault);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryDecision::ManualReview),
                          static_cast<int>(unknownFault.decision));

    const auto noFault = RecoveryManager::classifyError(
        ApiStatus::Ok, sensorContext, RetryPolicy{2U, true});
    TEST_ASSERT_TRUE(noFault.recognized);
    TEST_ASSERT_FALSE(noFault.hasFault);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryDecision::NoAction),
                          static_cast<int>(noFault.decision));
}

void testRecoveryManagerBoundsRetryByInjectedPolicy() {
    using project::managers::RecoveryDecision;
    using project::managers::RecoveryManager;
    using project::managers::RetryPolicy;
    using project::models::ApiStatus;
    using project::models::FaultKind;

    const project::managers::RecoveryContext context{
        FaultKind::SensorFault, false};
    const auto disabled = RecoveryManager::classifyError(
        ApiStatus::Timeout, context, RetryPolicy{0U, true});
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryDecision::RetryDisabled),
                          static_cast<int>(disabled.decision));

    const auto retry = RecoveryManager::classifyError(
        ApiStatus::Timeout, context, RetryPolicy{2U, true}, 1U);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryDecision::Retry),
                          static_cast<int>(retry.decision));

    const auto limitReached = RecoveryManager::classifyError(
        ApiStatus::Timeout, context, RetryPolicy{2U, true}, 2U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RecoveryDecision::RetryLimitReached),
        static_cast<int>(limitReached.decision));

    const auto unavailable = RecoveryManager::classifyError(
        ApiStatus::Timeout, context, RetryPolicy{2U, false});
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(RecoveryDecision::RecoveryUnavailable),
        static_cast<int>(unavailable.decision));

    const auto nonRetryable = RecoveryManager::classifyError(
        ApiStatus::DataInvalid, context, RetryPolicy{2U, true});
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryDecision::NoRetry),
                          static_cast<int>(nonRetryable.decision));
}

void testRecoveryManagerInhibitsInferenceForSchemaAndModelMismatch() {
    using project::managers::RecoveryDecision;
    using project::managers::RecoveryManager;
    using project::managers::RetryPolicy;
    using project::models::ApiStatus;
    using project::models::FaultKind;

    const RetryPolicy retryAvailable{3U, true};
    const project::managers::RecoveryContext schemaMismatch{
        FaultKind::ConfigError, true};
    const auto schemaResult = RecoveryManager::classifyError(
        ApiStatus::DataInvalid, schemaMismatch, retryAvailable);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryDecision::InhibitInference),
                          static_cast<int>(schemaResult.decision));

    const project::managers::RecoveryContext modelContext{
        FaultKind::ModelError, false};
    const auto modelResult = RecoveryManager::classifyError(
        ApiStatus::VersionMismatch, modelContext, retryAvailable);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryDecision::InhibitInference),
                          static_cast<int>(modelResult.decision));
}

void testEventRecordJsonLinesRoundTripsEveryMember() {
    using project::models::SerializationFormat;
    using project::models::SerializationStatus;

    const auto expected = serializableEventRecord();
    std::uint8_t encoded[4096]{};
    std::size_t encodedSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::Ok),
        static_cast<int>(project::models::serializeEventRecord(
            expected, SerializationFormat::JsonLines, encoded,
            sizeof(encoded), encodedSize)));
    TEST_ASSERT_TRUE(encodedSize > 1U);
    TEST_ASSERT_EQUAL_UINT8('\n', encoded[encodedSize - 1U]);

    std::uint8_t terminated[sizeof(encoded) + 1U]{};
    std::memcpy(terminated, encoded, encodedSize);
    terminated[encodedSize] = '\0';
    TEST_ASSERT_NOT_NULL(std::strstr(
        reinterpret_cast<const char*>(terminated),
        "\"record_type\":\"event_record\",\"schema_version\":2"));
    TEST_ASSERT_NOT_NULL(std::strstr(
        reinterpret_cast<const char*>(terminated),
        "\"severity\":\"NOTICE\""));
    TEST_ASSERT_NOT_NULL(std::strstr(
        reinterpret_cast<const char*>(terminated),
        "\"feature_schema_hash_sha256\":\"00010203"));

    project::models::EventRecord actual{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::Ok),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, encoded, encodedSize, actual)));
    assertEventRecordEqual(expected, actual);
}

void testEventRecordJsonLinesRejectsInvalidArgumentsAndSmallBuffers() {
    using project::models::SerializationFormat;
    using project::models::SerializationStatus;

    auto event = serializableEventRecord();
    std::size_t bytesWritten = 99U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, nullptr, 1U,
            bytesWritten)));
    TEST_ASSERT_EQUAL_UINT32(0U, bytesWritten);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::BufferTooSmall),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, nullptr, 0U,
            bytesWritten)));
    TEST_ASSERT_EQUAL_UINT32(0U, bytesWritten);

    std::uint8_t output[32];
    std::memset(output, 0xA5, sizeof(output));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::BufferTooSmall),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, output, sizeof(output),
            bytesWritten)));
    TEST_ASSERT_EQUAL_UINT32(0U, bytesWritten);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, output[0]);

    project::models::EventRecord decoded{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, nullptr, 0U, decoded)));
    std::uint8_t empty = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, &empty, 0U, decoded)));
}

void testEventRecordJsonLinesRejectsTruncationAndTrailingBytes() {
    using project::models::SerializationFormat;
    using project::models::SerializationStatus;

    const auto event = serializableEventRecord();
    std::uint8_t encoded[4096]{};
    std::size_t encodedSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::Ok),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, encoded, sizeof(encoded),
            encodedSize)));

    project::models::EventRecord decoded{};
    decoded.eventId = 123U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, encoded, encodedSize - 2U,
            decoded)));
    TEST_ASSERT_EQUAL_UINT64(123U, decoded.eventId);

    std::size_t multilineSize = encodedSize;
    TEST_ASSERT_TRUE(insertJsonByteBefore(
        encoded, multilineSize, sizeof(encoded), ",\"event_sequence_no\"",
        '\n'));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, encoded, multilineSize, decoded)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::Ok),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, encoded, sizeof(encoded),
            encodedSize)));
    encoded[encodedSize] = 'x';
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, encoded, encodedSize + 1U,
            decoded)));
    TEST_ASSERT_EQUAL_UINT64(123U, decoded.eventId);
}

void testEventRecordJsonLinesRejectsUnsupportedTypeVersionAndEnums() {
    using project::models::SerializationFormat;
    using project::models::SerializationStatus;

    const auto event = serializableEventRecord();
    std::uint8_t original[4096]{};
    std::size_t encodedSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::Ok),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, original, sizeof(original),
            encodedSize)));

    project::models::EventRecord decoded{};
    std::uint8_t mutated[4096]{};
    std::memcpy(mutated, original, encodedSize);
    TEST_ASSERT_TRUE(replaceJsonText(mutated, encodedSize, "event_record",
                                     "xvent_record"));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, mutated, encodedSize, decoded)));

    std::memcpy(mutated, original, encodedSize);
    TEST_ASSERT_TRUE(replaceJsonText(mutated, encodedSize,
                                     "\"schema_version\":2",
                                     "\"schema_version\":3"));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::UnsupportedVersion),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, mutated, encodedSize, decoded)));

    std::memcpy(mutated, original, encodedSize);
    TEST_ASSERT_TRUE(replaceJsonText(mutated, encodedSize,
                                     "\"severity\":\"NOTICE\"",
                                     "\"severity\":\"NOXICE\""));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, mutated, encodedSize, decoded)));

    std::memcpy(mutated, original, encodedSize);
    TEST_ASSERT_TRUE(replaceJsonText(mutated, encodedSize,
                                     "\"risk_state\":\"DANGER_TEST\"",
                                     "\"risk_state\":\"BAD_STATE__\""));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, mutated, encodedSize, decoded)));
}

void testEventRecordJsonLinesPreservesInvalidNonFiniteAsNull() {
    using project::models::SerializationFormat;
    using project::models::SerializationStatus;

    auto event = serializableEventRecord();
    event.value = std::numeric_limits<float>::quiet_NaN();
    event.valueValid = false;
    std::uint8_t encoded[4096]{};
    std::size_t encodedSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::Ok),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, encoded, sizeof(encoded),
            encodedSize)));
    std::uint8_t terminated[sizeof(encoded) + 1U]{};
    std::memcpy(terminated, encoded, encodedSize);
    terminated[encodedSize] = '\0';
    TEST_ASSERT_NOT_NULL(std::strstr(
        reinterpret_cast<const char*>(terminated), "\"value\":null"));

    project::models::EventRecord decoded{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::Ok),
        static_cast<int>(project::models::deserializeEventRecord(
            SerializationFormat::JsonLines, encoded, encodedSize, decoded)));
    TEST_ASSERT_FALSE(decoded.valueValid);
    TEST_ASSERT_TRUE(std::isnan(decoded.value));

    event.value = std::numeric_limits<float>::infinity();
    event.valueValid = true;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, encoded, sizeof(encoded),
            encodedSize)));
    TEST_ASSERT_EQUAL_UINT32(0U, encodedSize);
}

void testEventRecordJsonLinesRejectsInvalidModelValues() {
    using project::models::SerializationFormat;
    using project::models::SerializationStatus;

    auto event = serializableEventRecord();
    std::uint8_t encoded[4096]{};
    std::size_t encodedSize = 0U;

    event.schemaVersion = 1U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::UnsupportedVersion),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, encoded, sizeof(encoded),
            encodedSize)));
    TEST_ASSERT_EQUAL_UINT32(0U, encodedSize);

    event = serializableEventRecord();
    event.severity =
        static_cast<project::models::EventSeverity>(0xFFU);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, encoded, sizeof(encoded),
            encodedSize)));

    event = serializableEventRecord();
    std::memset(event.eventName, 'X', sizeof(event.eventName));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, encoded, sizeof(encoded),
            encodedSize)));

    event = serializableEventRecord();
    event.message[0] = static_cast<char>(0xFFU);
    event.message[1] = '\0';
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::JsonLines, encoded, sizeof(encoded),
            encodedSize)));
}

void testEventRecordJsonLinesRoundTripsEnumAndStringBoundaries() {
    using project::models::EventRecord;
    using project::models::SerializationFormat;
    using project::models::SerializationStatus;

    const project::models::EventSeverity severities[] = {
        project::models::EventSeverity::Debug,
        project::models::EventSeverity::Info,
        project::models::EventSeverity::Notice,
        project::models::EventSeverity::Warning,
        project::models::EventSeverity::Error,
        project::models::EventSeverity::Critical,
    };
    const RiskState riskStates[] = {
        RiskState::Startup, RiskState::Normal, RiskState::Monitor,
        RiskState::DangerTest, RiskState::Unknown};
    const HealthState healthStates[] = {
        HealthState::Unknown, HealthState::Healthy, HealthState::Degraded,
        HealthState::Fault};
    const EventRecord::ValueType valueTypes[] = {
        EventRecord::ValueType::None, EventRecord::ValueType::Numeric,
        EventRecord::ValueType::Text};
    const project::models::SensorId sensorIds[] = {
        project::models::SensorId::Unknown, project::models::SensorId::Imu,
        project::models::SensorId::SoilMoisture,
        project::models::SensorId::Rain, project::models::SensorId::Hx710b};

    std::uint8_t encoded[4096]{};
    std::size_t encodedSize = 0U;
    project::models::EventRecord decoded{};
    auto event = serializableEventRecord();
    std::memset(event.eventName, 'N',
                project::models::event_capacity::kEventNameBytes - 1U);
    event.eventName[project::models::event_capacity::kEventNameBytes - 1U] =
        '\0';
    event.value = std::numeric_limits<float>::max();
    for (const auto severity : severities) {
        event.severity = severity;
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SerializationStatus::Ok),
            static_cast<int>(project::models::serializeEventRecord(
                event, SerializationFormat::JsonLines, encoded,
                sizeof(encoded), encodedSize)));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SerializationStatus::Ok),
            static_cast<int>(project::models::deserializeEventRecord(
                SerializationFormat::JsonLines, encoded, encodedSize,
                decoded)));
        assertEventRecordEqual(event, decoded);
    }
    for (const auto risk : riskStates) {
        event.riskState = risk;
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SerializationStatus::Ok),
            static_cast<int>(project::models::serializeEventRecord(
                event, SerializationFormat::JsonLines, encoded,
                sizeof(encoded), encodedSize)));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SerializationStatus::Ok),
            static_cast<int>(project::models::deserializeEventRecord(
                SerializationFormat::JsonLines, encoded, encodedSize,
                decoded)));
        assertEventRecordEqual(event, decoded);
    }
    for (const auto health : healthStates) {
        event.healthState = health;
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SerializationStatus::Ok),
            static_cast<int>(project::models::serializeEventRecord(
                event, SerializationFormat::JsonLines, encoded,
                sizeof(encoded), encodedSize)));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SerializationStatus::Ok),
            static_cast<int>(project::models::deserializeEventRecord(
                SerializationFormat::JsonLines, encoded, encodedSize,
                decoded)));
        assertEventRecordEqual(event, decoded);
    }
    for (const auto valueType : valueTypes) {
        event.valueType = valueType;
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SerializationStatus::Ok),
            static_cast<int>(project::models::serializeEventRecord(
                event, SerializationFormat::JsonLines, encoded,
                sizeof(encoded), encodedSize)));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SerializationStatus::Ok),
            static_cast<int>(project::models::deserializeEventRecord(
                SerializationFormat::JsonLines, encoded, encodedSize,
                decoded)));
        assertEventRecordEqual(event, decoded);
    }
    for (const auto sensor : sensorIds) {
        event.sensorId = sensor;
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SerializationStatus::Ok),
            static_cast<int>(project::models::serializeEventRecord(
                event, SerializationFormat::JsonLines, encoded,
                sizeof(encoded), encodedSize)));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SerializationStatus::Ok),
            static_cast<int>(project::models::deserializeEventRecord(
                SerializationFormat::JsonLines, encoded, encodedSize,
                decoded)));
        assertEventRecordEqual(event, decoded);
    }
}

void testEventRecordJsonLinesKeepsFormatsAndOtherRecordsSeparate() {
    using project::models::SerializationFormat;
    using project::models::SerializationStatus;

    auto event = serializableEventRecord();
    std::uint8_t encoded[32]{};
    std::size_t bytesWritten = 123U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::NotImplemented),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::Csv, encoded, sizeof(encoded),
            bytesWritten)));
    TEST_ASSERT_EQUAL_UINT32(0U, bytesWritten);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::NotImplemented),
        static_cast<int>(project::models::serializeEventRecord(
            event, SerializationFormat::Binary, encoded, sizeof(encoded),
            bytesWritten)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::InvalidArgument),
        static_cast<int>(project::models::serializeEventRecord(
            event, static_cast<SerializationFormat>(0xFFU), encoded,
            sizeof(encoded), bytesWritten)));

    project::models::FeatureVector vector{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SerializationStatus::NotImplemented),
        static_cast<int>(project::models::serializeFeatureVector(
            vector, SerializationFormat::JsonLines, encoded, sizeof(encoded),
            bytesWritten)));
    TEST_ASSERT_EQUAL_UINT32(0U, bytesWritten);
}

void testStorageManagerBeginLifecycleAndRepeatedBegin() {
    using project::models::ApiStatus;
    using project::storage::StorageManagerState;

    FakeStorage backend;
    auto manager = makeStorageManager(backend);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(StorageManagerState::Uninitialized),
        static_cast<int>(manager.state()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.begin()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Ready),
                          static_cast<int>(manager.state()));
    TEST_ASSERT_EQUAL_UINT32(1U, backend.beginCalls);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.begin()));
    TEST_ASSERT_EQUAL_UINT32(1U, backend.beginCalls);
}

void testStorageManagerBeginFailureCanBeRetried() {
    using project::models::ApiStatus;
    using project::storage::StorageManagerState;

    FakeStorage backend;
    backend.beginResult = false;
    auto manager = makeStorageManager(backend);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::IoError),
                          static_cast<int>(manager.begin()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Faulted),
                          static_cast<int>(manager.state()));

    const std::uint8_t record[] = {1U};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::IoError),
                          static_cast<int>(
                              manager.appendRecord(record, sizeof(record))));

    backend.beginResult = true;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.begin()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Ready),
                          static_cast<int>(manager.state()));
    TEST_ASSERT_EQUAL_UINT32(2U, backend.beginCalls);
}

void testStorageManagerAppendValidatesAndBorrowsCompleteRecord() {
    using project::models::ApiStatus;
    using project::storage::StorageManagerState;

    FakeStorage backend;
    auto manager = makeStorageManager(backend, 4U);
    const std::uint8_t record[] = {0x10U, 0x20U, 0x30U, 0x40U};

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::NotInitialized),
                          static_cast<int>(
                              manager.appendRecord(record, sizeof(record))));
    TEST_ASSERT_EQUAL_UINT32(0U, backend.appendCalls);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.begin()));

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::InvalidArgument),
                          static_cast<int>(manager.appendRecord(nullptr, 1U)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::InvalidArgument),
                          static_cast<int>(manager.appendRecord(record, 0U)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::ResourceExhausted),
                          static_cast<int>(
                              manager.appendRecord(record, sizeof(record) + 1U)));
    TEST_ASSERT_EQUAL_UINT32(0U, backend.appendCalls);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(
                              manager.appendRecord(record, sizeof(record))));
    TEST_ASSERT_EQUAL_UINT32(1U, backend.appendCalls);
    TEST_ASSERT_EQUAL_UINT32(4U, backend.bytes.size());
    for (std::size_t index = 0U; index < sizeof(record); ++index) {
        TEST_ASSERT_EQUAL_UINT8(record[index], backend.bytes[index]);
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Ready),
                          static_cast<int>(manager.state()));
}

void testStorageManagerPartialWriteFaultsUntilSuccessfulBegin() {
    using project::models::ApiStatus;
    using project::storage::StorageManagerState;

    FakeStorage backend;
    auto manager = makeStorageManager(backend);
    const std::uint8_t record[] = {1U, 2U, 3U, 4U};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.begin()));

    backend.partialWriteBytes = 2U;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::IoError),
                          static_cast<int>(
                              manager.appendRecord(record, sizeof(record))));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Faulted),
                          static_cast<int>(manager.state()));
    TEST_ASSERT_EQUAL_UINT32(2U, backend.bytes.size());

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::IoError),
                          static_cast<int>(
                              manager.appendRecord(record, sizeof(record))));
    TEST_ASSERT_EQUAL_UINT32(1U, backend.appendCalls);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.begin()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Ready),
                          static_cast<int>(manager.state()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(
                              manager.appendRecord(record, sizeof(record))));
    TEST_ASSERT_EQUAL_UINT32(2U, backend.appendCalls);
}

void testStorageManagerFlushFailureAndRecovery() {
    using project::models::ApiStatus;
    using project::storage::StorageManagerState;

    FakeStorage backend;
    auto manager = makeStorageManager(backend);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::NotInitialized),
                          static_cast<int>(manager.flush()));
    TEST_ASSERT_EQUAL_UINT32(0U, backend.flushCalls);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.begin()));
    backend.flushResult = false;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::IoError),
                          static_cast<int>(manager.flush()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Faulted),
                          static_cast<int>(manager.state()));
    TEST_ASSERT_EQUAL_UINT32(1U, backend.flushCalls);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::IoError),
                          static_cast<int>(manager.flush()));
    TEST_ASSERT_EQUAL_UINT32(1U, backend.flushCalls);

    backend.flushResult = true;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.begin()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.flush()));
    TEST_ASSERT_EQUAL_UINT32(2U, backend.flushCalls);
}

void testStorageManagerAppendBackendFullAndZeroLimit() {
    using project::models::ApiStatus;
    using project::storage::StorageManagerState;

    FakeStorage backend;
    auto manager = makeStorageManager(backend, 0U);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::InvalidArgument),
                          static_cast<int>(manager.begin()));
    TEST_ASSERT_EQUAL_UINT32(0U, backend.beginCalls);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Uninitialized),
                          static_cast<int>(manager.state()));

    auto configured = makeStorageManager(backend, 4U);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(configured.begin()));
    backend.appendResult = false;
    const std::uint8_t record[] = {1U, 2U, 3U, 4U};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::IoError),
                          static_cast<int>(
                              configured.appendRecord(record, sizeof(record))));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Faulted),
                          static_cast<int>(configured.state()));
}

void testStorageManagerShutdownIsLogicalAndTerminal() {
    using project::models::ApiStatus;
    using project::storage::StorageManagerState;

    FakeStorage backend;
    auto manager = makeStorageManager(backend);
    const std::uint8_t record[] = {1U};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.begin()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.shutdown()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Shutdown),
                          static_cast<int>(manager.state()));

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Cancelled),
                          static_cast<int>(manager.begin()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Cancelled),
                          static_cast<int>(
                              manager.appendRecord(record, sizeof(record))));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Cancelled),
                          static_cast<int>(manager.flush()));
    TEST_ASSERT_EQUAL_UINT32(1U, backend.beginCalls);
    TEST_ASSERT_EQUAL_UINT32(0U, backend.appendCalls);
    TEST_ASSERT_EQUAL_UINT32(0U, backend.flushCalls);
}

void testStorageManagerBackendFailurePropagatesIoError() {
    using project::models::ApiStatus;
    using project::storage::StorageManagerState;

    FakeStorage backend;
    backend.beginResult = false;
    auto manager = makeStorageManager(backend);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::IoError),
                          static_cast<int>(manager.begin()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Faulted),
                          static_cast<int>(manager.state()));

    backend.beginResult = true;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::Ok),
                          static_cast<int>(manager.begin()));
    backend.appendResult = false;
    const std::uint8_t record[] = {0xAAU, 0x55U};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiStatus::IoError),
                          static_cast<int>(
                              manager.appendRecord(record, sizeof(record))));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StorageManagerState::Faulted),
                          static_cast<int>(manager.state()));
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

void testConfigManagerAcceptsValidTestOnlyManifestWithoutArtifacts() {
    auto fixture = testManifestFixture();
    fixture.bind();
    project::managers::ValidationReport report{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::Ok),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(report.valid());
}

void testConfigManagerReportsMissingManifestField() {
    auto fixture = testManifestFixture();
    fixture.bind();
    fixture.manifest.model = nullptr;
    project::managers::ValidationReport report{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(reportHasIssue(
        report, project::managers::ManifestErrorCode::MissingField, "model"));
}

void testConfigManagerRejectsUnsupportedManifestVersion() {
    auto fixture = testManifestFixture();
    fixture.bind();
    fixture.manifest.formatVersion =
        project::managers::ConfigManager::kSupportedManifestVersion + 1U;
    project::managers::ValidationReport report{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::VersionMismatch),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(reportHasIssue(
        report, project::managers::ManifestErrorCode::UnsupportedVersion,
        "manifest.formatVersion"));
}

void testConfigManagerRejectsInvalidEnum() {
    auto fixture = testManifestFixture();
    fixture.bind();
    fixture.model.outputKind =
        static_cast<project::ai::EdgeImpulseOutputKind>(0xffU);
    project::managers::ValidationReport report{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(reportHasIssue(
        report, project::managers::ManifestErrorCode::InvalidEnum,
        "model.outputKind"));

    fixture.model.outputKind =
        project::ai::EdgeImpulseOutputKind::Regression;
    fixture.risk.scoreDirection =
        static_cast<project::managers::RiskScoreDirection>(0xffU);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(reportHasIssue(
        report, project::managers::ManifestErrorCode::InvalidEnum,
        "risk.scoreDirection"));
}

void testConfigManagerRejectsNonFiniteValues() {
    auto fixture = testManifestFixture();
    fixture.bind();
    fixture.feature.sampleRateHz =
        std::numeric_limits<float>::quiet_NaN();
    fixture.risk.scoreMaximum =
        std::numeric_limits<float>::infinity();
    project::managers::ValidationReport report{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(reportHasIssue(
        report, project::managers::ManifestErrorCode::NonFinite,
        "featurePipeline.numeric"));
    TEST_ASSERT_TRUE(reportHasIssue(
        report, project::managers::ManifestErrorCode::NonFinite,
        "risk.numeric"));
}

void testConfigManagerRejectsOutOfRangeValues() {
    auto fixture = testManifestFixture();
    fixture.bind();
    fixture.feature.windowLengthSamples =
        project::services::FeaturePipeline::kMaxWindowSamples + 1U;
    project::managers::ValidationReport report{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(reportHasIssue(
        report, project::managers::ManifestErrorCode::OutOfRange,
        "featurePipeline.dimensions"));
}

void testConfigManagerRejectsModelFssMismatch() {
    auto fixture = testManifestFixture();
    fixture.bind();
    ++fixture.model.featureSchemaId;
    project::managers::ValidationReport report{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(reportHasIssue(
        report, project::managers::ManifestErrorCode::CrossFieldMismatch,
        "model.featureSchema"));
}

void testConfigManagerValidatesHashesOnlyForSuppliedArtifacts() {
    auto fixture = testManifestFixture();
    fixture.bind();
    project::managers::ValidationReport report{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::Ok),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));

    fixture.manifest.featureSchemaArtifact = {
        kTestArtifact, sizeof(kTestArtifact)};
    fixture.manifest.modelArtifact = {kTestArtifact, sizeof(kTestArtifact)};
    fixture.manifest.riskConfigArtifact = {kTestArtifact, sizeof(kTestArtifact)};
    for (std::size_t i = 0U; i < sizeof(kTestArtifactSha256); ++i) {
        fixture.risk.hashSha256[i] = kTestArtifactSha256[i];
    }

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::Ok),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(report.valid());

    fixture.model.modelHash[0] ^= 0x01U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(reportHasIssue(
        report, project::managers::ManifestErrorCode::ArtifactHashMismatch,
        "modelArtifact"));

    fixture.model.modelHash = {};
    for (std::size_t i = 0U; i < sizeof(kBoundaryArtifactSha256); ++i) {
        fixture.model.modelHash[i] = kBoundaryArtifactSha256[i];
    }
    fixture.manifest.modelArtifact = {
        kBoundaryArtifact, sizeof(kBoundaryArtifact) - 1U};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::Ok),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));

    fixture.model.modelHash = {};
    for (std::size_t i = 0U; i < sizeof(kMultiBlockArtifactSha256); ++i) {
        fixture.model.modelHash[i] = kMultiBlockArtifactSha256[i];
    }
    fixture.manifest.modelArtifact = {
        kMultiBlockArtifact.data(), kMultiBlockArtifact.size()};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::Ok),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
}

void testConfigManagerKeepsProductionActivationFailClosed() {
    auto fixture = testManifestFixture();
    fixture.bind();
    fixture.manifest.purpose = project::managers::ManifestPurpose::Production;
    project::managers::ValidationReport report{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(project::managers::ConfigManager::validateManifest(
            fixture.manifest, report)));
    TEST_ASSERT_TRUE(reportHasIssue(
        report, project::managers::ManifestErrorCode::ApprovalRequired,
        "manifest.productionApprovalComplete"));

    project::managers::ConfigManager manager{};
    TEST_ASSERT_FALSE(manager.activateAtomically());
    TEST_ASSERT_NULL(manager.activeManifest());
}

project::managers::AppControllerConfig appControllerTestConfig() noexcept {
    return {40U, 70U, 90U};
}

void testAppControllerStartsPhasesInOrderAndGatesInference() {
    FakeAppLifecycle lifecycle{};
    project::managers::AppController controller{
        lifecycle, appControllerTestConfig()};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerStatus::Ok),
        static_cast<int>(controller.begin()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerState::Running),
        static_cast<int>(controller.state()));
    TEST_ASSERT_TRUE(controller.inferenceEnabled());
    TEST_ASSERT_FALSE(lifecycle.inferenceWasEnabledDuringStart);
    const std::size_t callsAfterBegin = lifecycle.callCount;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerStatus::Ok),
        static_cast<int>(controller.begin()));
    TEST_ASSERT_EQUAL_UINT(callsAfterBegin, lifecycle.callCount);

    std::size_t phaseIndex = 0U;
    std::size_t enableIndex = lifecycle.callCount;
    for (std::size_t index = 0U; index < lifecycle.callCount; ++index) {
        const auto& call = lifecycle.calls[index];
        if (call.call == FakeAppLifecycle::Call::StartPhase) {
            TEST_ASSERT_EQUAL_UINT8(
                static_cast<std::uint8_t>(phaseIndex),
                static_cast<std::uint8_t>(call.phase));
            ++phaseIndex;
        } else if (call.call == FakeAppLifecycle::Call::InferenceGate &&
                   call.value) {
            enableIndex = index;
        }
    }
    TEST_ASSERT_EQUAL_UINT(
        project::managers::AppController::kPhaseCount, phaseIndex);
    TEST_ASSERT_TRUE(enableIndex > 0U);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(
            project::managers::AppStartupPhase::SelfCheck),
        static_cast<std::uint8_t>(
            lifecycle.calls[enableIndex - 1U].phase));
}

void testAppControllerCleansUpEveryStartupFailureInReverse() {
    for (std::size_t failureIndex = 0U;
         failureIndex < project::managers::AppController::kPhaseCount;
         ++failureIndex) {
        FakeAppLifecycle lifecycle{};
        lifecycle.failPhase =
            static_cast<project::managers::AppStartupPhase>(failureIndex);
        project::managers::AppController controller{
            lifecycle, appControllerTestConfig()};

        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(
                project::managers::AppControllerStatus::StartupPhaseFailed),
            static_cast<int>(controller.begin()));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(project::managers::AppControllerState::Faulted),
            static_cast<int>(controller.state()));
        TEST_ASSERT_FALSE(controller.inferenceEnabled());
        TEST_ASSERT_FALSE(lifecycle.inferenceEnabled);

        std::size_t cleanupCount = 0U;
        for (std::size_t index = 0U; index < lifecycle.callCount; ++index) {
            const auto& call = lifecycle.calls[index];
            if (call.call != FakeAppLifecycle::Call::CleanupPhase) {
                continue;
            }
            const std::size_t expected =
                failureIndex - cleanupCount;
            TEST_ASSERT_EQUAL_UINT8(
                static_cast<std::uint8_t>(expected),
                static_cast<std::uint8_t>(call.phase));
            ++cleanupCount;
        }
        TEST_ASSERT_EQUAL_UINT(failureIndex + 1U, cleanupCount);
    }
}

void testAppControllerFailsClosedWhenInferenceGateCannotOpen() {
    FakeAppLifecycle lifecycle{};
    lifecycle.failEnableInference = true;
    project::managers::AppController controller{
        lifecycle, appControllerTestConfig()};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::managers::AppControllerStatus::InferenceGateFailed),
        static_cast<int>(controller.begin()));
    TEST_ASSERT_FALSE(controller.inferenceEnabled());
    TEST_ASSERT_FALSE(lifecycle.inferenceEnabled);
    TEST_ASSERT_EQUAL_UINT(
        0U,
        controller.completedPhaseCount());
    std::size_t startedPhaseCount = 0U;
    for (std::size_t index = 0U; index < lifecycle.callCount; ++index) {
        if (lifecycle.calls[index].call ==
            FakeAppLifecycle::Call::StartPhase) {
            ++startedPhaseCount;
        }
    }
    TEST_ASSERT_EQUAL_UINT(
        project::managers::AppController::kPhaseCount, startedPhaseCount);
}

void testAppControllerShutdownOrderAndRepeatedCalls() {
    FakeAppLifecycle lifecycle{};
    project::managers::AppController controller{
        lifecycle, appControllerTestConfig()};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerStatus::Ok),
        static_cast<int>(controller.begin()));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerStatus::Ok),
        static_cast<int>(controller.requestShutdown()));
    TEST_ASSERT_FALSE(controller.inferenceEnabled());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerState::Shutdown),
        static_cast<int>(controller.state()));

    std::size_t stopIndex = lifecycle.callCount;
    std::size_t drainIndex = lifecycle.callCount;
    std::size_t flushIndex = lifecycle.callCount;
    std::size_t safeIndex = lifecycle.callCount;
    for (std::size_t index = 0U; index < lifecycle.callCount; ++index) {
        switch (lifecycle.calls[index].call) {
            case FakeAppLifecycle::Call::StopAcceptingWork:
                stopIndex = index;
                break;
            case FakeAppLifecycle::Call::DrainAndStopTasks:
                drainIndex = index;
                TEST_ASSERT_EQUAL_UINT32(
                    70U, lifecycle.calls[index].timeoutMs);
                break;
            case FakeAppLifecycle::Call::FlushStorage:
                flushIndex = index;
                TEST_ASSERT_EQUAL_UINT32(
                    90U, lifecycle.calls[index].timeoutMs);
                break;
            case FakeAppLifecycle::Call::ConfigureOutputsSafe:
                safeIndex = index;
                break;
            default:
                break;
        }
    }
    TEST_ASSERT_TRUE(stopIndex < drainIndex);
    TEST_ASSERT_TRUE(drainIndex < flushIndex);
    TEST_ASSERT_TRUE(flushIndex < safeIndex);

    const std::size_t callsAfterShutdown = lifecycle.callCount;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerStatus::Ok),
        static_cast<int>(controller.requestShutdown()));
    TEST_ASSERT_EQUAL_UINT(callsAfterShutdown, lifecycle.callCount);
}

void testAppControllerFlushFailureStillMakesOutputsSafe() {
    FakeAppLifecycle lifecycle{};
    project::managers::AppController controller{
        lifecycle, appControllerTestConfig()};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerStatus::Ok),
        static_cast<int>(controller.begin()));
    lifecycle.flushResult = false;

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::managers::AppControllerStatus::ShutdownFailed),
        static_cast<int>(controller.requestShutdown()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerState::Shutdown),
        static_cast<int>(controller.state()));

    bool safeOutputCalled = false;
    for (std::size_t index = 0U; index < lifecycle.callCount; ++index) {
        safeOutputCalled = safeOutputCalled ||
            lifecycle.calls[index].call ==
                FakeAppLifecycle::Call::ConfigureOutputsSafe;
    }
    TEST_ASSERT_TRUE(safeOutputCalled);
}

void testAppControllerHealthChecksAreBoundedAndFailClosed() {
    FakeAppLifecycle lifecycle{};
    project::managers::AppController controller{
        lifecycle, appControllerTestConfig()};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerStatus::Ok),
        static_cast<int>(controller.begin()));

    lifecycle.healthResult = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::managers::AppControllerStatus::HealthCheckFailed),
        static_cast<int>(controller.runHealthChecks()));
    TEST_ASSERT_FALSE(controller.inferenceEnabled());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::AppControllerState::Faulted),
        static_cast<int>(controller.state()));

    bool boundedCheckCalled = false;
    for (std::size_t index = 0U; index < lifecycle.callCount; ++index) {
        if (lifecycle.calls[index].call ==
            FakeAppLifecycle::Call::HealthCheck) {
            TEST_ASSERT_EQUAL_UINT32(
                40U, lifecycle.calls[index].timeoutMs);
            boundedCheckCalled = true;
        }
    }
    TEST_ASSERT_TRUE(boundedCheckCalled);
}

void testAppControllerRejectsUnconfiguredTimeouts() {
    FakeAppLifecycle lifecycle{};
    project::managers::AppController controller{
        lifecycle, project::managers::AppControllerConfig{}};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::managers::AppControllerStatus::InvalidConfiguration),
        static_cast<int>(controller.begin()));
    TEST_ASSERT_EQUAL_UINT(1U, lifecycle.callCount);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(FakeAppLifecycle::Call::InferenceGate),
        static_cast<int>(lifecycle.calls[0U].call));
    TEST_ASSERT_FALSE(lifecycle.calls[0U].value);
}

void testConfigManagerCandidateFaultsPreservePreviousCommittedRecord() {
    using FailurePoint = FakeConfigManifestPersistence::FailurePoint;
    constexpr FailurePoint failures[] = {
        FailurePoint::WriteCandidate,
        FailurePoint::FlushCandidate,
        FailurePoint::ReadBack,
        FailurePoint::WriteCommitMarker,
        FailurePoint::FlushCommitMarker};

    for (const FailurePoint failure : failures) {
        auto previous = testManifestFixture();
        previous.bind();
        auto candidate = testManifestFixture();
        candidate.bind();
        candidate.manifest.productionApprovalComplete = true;

        FakeConfigManifestPersistence persistence{};
        persistence.seed(0U, 7U, previous.manifest);
        persistence.failurePoint = failure;

        project::managers::ConfigManager writer{persistence};
        TEST_ASSERT_FALSE(writer.storeCandidate(candidate.manifest));

        persistence.failurePoint = FailurePoint::None;
        project::managers::ConfigManager rebooted{persistence};
        TEST_ASSERT_TRUE(rebooted.loadLastValid());
        const auto* selected = rebooted.selectedManifest();
        TEST_ASSERT_NOT_NULL(selected);
        TEST_ASSERT_FALSE(selected->productionApprovalComplete);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(
                project::managers::ConfigSlotReadStatus::Committed),
            static_cast<int>(persistence.slots[0U].status));
    }
}

void testConfigManagerRejectsInvalidCandidateWithoutWriting() {
    auto previous = testManifestFixture();
    previous.bind();
    auto candidate = testManifestFixture();
    candidate.bind();
    candidate.model.featureSchemaId += 1U;

    FakeConfigManifestPersistence persistence{};
    persistence.seed(0U, 3U, previous.manifest);
    project::managers::ConfigManager manager{persistence};
    TEST_ASSERT_FALSE(manager.storeCandidate(candidate.manifest));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::ConfigSlotReadStatus::Empty),
        static_cast<int>(persistence.slots[1U].status));

    project::managers::ConfigManager rebooted{persistence};
    TEST_ASSERT_TRUE(rebooted.loadLastValid());
    TEST_ASSERT_FALSE(
        rebooted.selectedManifest()->productionApprovalComplete);
}

void testConfigManagerCommitsOnlyAfterValidReadBack() {
    auto previous = testManifestFixture();
    previous.bind();
    auto candidate = testManifestFixture();
    candidate.bind();
    candidate.manifest.productionApprovalComplete = true;

    FakeConfigManifestPersistence persistence{};
    persistence.seed(0U, 4U, previous.manifest);
    project::managers::ConfigManager writer{persistence};
    TEST_ASSERT_TRUE(writer.storeCandidate(candidate.manifest));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::managers::ConfigSlotReadStatus::Committed),
        static_cast<int>(persistence.slots[1U].status));
    TEST_ASSERT_EQUAL_UINT64(5U, persistence.slots[1U].generation);
    TEST_ASSERT_NOT_NULL(writer.selectedManifest());
    TEST_ASSERT_TRUE(
        writer.selectedManifest()->productionApprovalComplete);
    TEST_ASSERT_FALSE(writer.activateAtomically());
    TEST_ASSERT_NULL(writer.activeManifest());

    project::managers::ConfigManager rebooted{persistence};
    TEST_ASSERT_TRUE(rebooted.loadLastValid());
    TEST_ASSERT_TRUE(
        rebooted.selectedManifest()->productionApprovalComplete);
}

void testConfigManagerFallsBackWhenNewestRecordIsCorrupt() {
    auto previous = testManifestFixture();
    previous.bind();
    auto corruptNewest = testManifestFixture();
    corruptNewest.bind();
    corruptNewest.model.featureSchemaId += 1U;

    FakeConfigManifestPersistence persistence{};
    persistence.seed(0U, 9U, previous.manifest);
    persistence.seed(1U, 10U, corruptNewest.manifest);
    project::managers::ConfigManager manager{persistence};
    TEST_ASSERT_TRUE(manager.loadLastValid());
    TEST_ASSERT_NOT_NULL(manager.selectedManifest());
    TEST_ASSERT_FALSE(
        manager.selectedManifest()->productionApprovalComplete);
}

void testCalibrationServicePreservesRawInputAndRemainsUncalibrated() {
    project::services::CalibrationService service{};
    project::models::SensorFrame input{};
    input.schemaVersion = 1U;
    input.bootId = 12U;
    input.sequence = 34U;
    input.timestampMs = 56U;
    input.qualityFlags = 0x1234U;
    input.sensorStatusBits = 0x5678U;
    input.imu.accelX = -123;
    input.imu.gyroZ = 456;
    input.imuStatus = {project::models::SensorHealth::Healthy, true, 0U,
                       0U, 56U};
    input.imuPhysical.accelXG = 1.25F;

    project::models::ProcessedFrame output{};
    output.accelXG = 88.0F;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::CalibrationMode::RawOnly),
        static_cast<int>(service.mode()));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::NotInitialized),
        static_cast<int>(service.loadCalibrationStatus()));
    TEST_ASSERT_FALSE(service.loadCalibration());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::NotInitialized),
        static_cast<int>(service.processStatus(input, output)));

    TEST_ASSERT_EQUAL_INT32(-123, input.imu.accelX);
    TEST_ASSERT_EQUAL_INT32(456, input.imu.gyroZ);
    TEST_ASSERT_EQUAL_FLOAT(1.25F, input.imuPhysical.accelXG);
    TEST_ASSERT_EQUAL_UINT32(1U, output.schemaVersion);
    TEST_ASSERT_EQUAL_UINT64(12U, output.bootId);
    TEST_ASSERT_EQUAL_UINT64(34U, output.sequence);
    TEST_ASSERT_EQUAL_UINT64(56U, output.timestampMs);
    TEST_ASSERT_EQUAL_UINT32(0x1234U, output.qualityFlags);
    TEST_ASSERT_EQUAL_UINT32(0x5678U, output.sensorStatusBits);
    TEST_ASSERT_EQUAL_UINT32(0U, output.validMask);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, output.accelXG);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::VALID),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::ImuAccelXRaw)]));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::MISSING),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::AccelXG)]));
}

void testCalibrationServicePropagatesInvalidAndPartialChannels() {
    project::services::CalibrationService service{};
    project::models::SensorFrame input{};
    input.schemaVersion = 1U;
    input.imuStatus = {project::models::SensorHealth::Healthy, true, 0U,
                       0U, 10U};
    input.soilStatus = {project::models::SensorHealth::Fault, false, 1U,
                        0U, 10U};
    input.rainStatus = {project::models::SensorHealth::Degraded, true, 0U,
                        0U, 10U};
    input.hx710bStatus = {project::models::SensorHealth::Unknown, true, 2U,
                          0U, 10U};

    project::models::ProcessedFrame output{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::NotInitialized),
        static_cast<int>(service.processStatus(input, output)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::VALID),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::ImuAccelXRaw)]));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::MISSING),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::SoilMoistureRaw)]));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::VALID),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::RainRaw)]));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::MISSING),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::Hx710bRaw)]));
}

void testCalibrationServiceRejectsNonFiniteAndDoesNotMakeValidZero() {
    project::services::CalibrationService service{};
    project::models::SensorFrame input{};
    input.schemaVersion = 1U;
    input.imuStatus = {project::models::SensorHealth::Healthy, true, 0U,
                       0U, 10U};
    input.soilStatus = {project::models::SensorHealth::Healthy, true, 0U,
                        0U, 10U};
    input.rainStatus = {project::models::SensorHealth::Healthy, true, 0U,
                        0U, 10U};
    input.hx710bStatus = {project::models::SensorHealth::Healthy, true, 0U,
                          0U, 10U};
    input.imuPhysical.accelXG =
        std::numeric_limits<float>::quiet_NaN();
    input.imuPhysical.gyroYDps =
        std::numeric_limits<float>::infinity();
    input.tiltAngleValid = true;
    input.tiltAngleDeg = std::numeric_limits<float>::quiet_NaN();
    input.soilMoisturePct =
        -std::numeric_limits<float>::infinity();
    input.rainWetnessRelativePct =
        std::numeric_limits<float>::quiet_NaN();
    input.rainWetnessValid = true;
    input.rainRateMmH = std::numeric_limits<float>::infinity();
    input.hx710bCalibrationFactorValid = true;
    input.hx710bCalibrationFactor =
        std::numeric_limits<float>::infinity();

    project::models::ProcessedFrame output{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::NotInitialized),
        static_cast<int>(service.processStatus(input, output)));
    const auto channelQuality = [&output](SensorChannel channel) {
        return output.channelQuality[static_cast<std::size_t>(channel)];
    };
    TEST_ASSERT_EQUAL(static_cast<int>(DataQualityState::NON_FINITE),
                      static_cast<int>(channelQuality(SensorChannel::AccelXG)));
    TEST_ASSERT_EQUAL(static_cast<int>(DataQualityState::NON_FINITE),
                      static_cast<int>(channelQuality(SensorChannel::GyroYDps)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::NON_FINITE),
        static_cast<int>(channelQuality(SensorChannel::TiltAngleDeg)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::NON_FINITE),
        static_cast<int>(channelQuality(SensorChannel::SoilMoisturePct)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::NON_FINITE),
        static_cast<int>(
            channelQuality(SensorChannel::RainWetnessRelativePct)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::NON_FINITE),
        static_cast<int>(channelQuality(SensorChannel::RainRateMmH)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::NON_FINITE),
        static_cast<int>(
            channelQuality(SensorChannel::Hx710bCalibratedValue)));
    input.hx710bCalibrationFactorValid = false;
    input.hx710bCalibratedValue =
        std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::NotInitialized),
        static_cast<int>(service.processStatus(input, output)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::NON_FINITE),
        static_cast<int>(channelQuality(
            SensorChannel::Hx710bCalibratedValue)));
    TEST_ASSERT_EQUAL_UINT32(0U, output.validMask);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, output.accelXG);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, output.soilMoisturePct);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, output.rainWetnessRelativePct);

    TEST_ASSERT_FALSE(service.process(input, output));
}

void testCalibrationServiceRejectsUnmatchedCalibrationVersion() {
    project::services::CalibrationService service{};
    project::models::SensorFrame input{};
    input.schemaVersion = 1U;
    input.calibrationVersion = 7U;
    input.imuStatus = {project::models::SensorHealth::Healthy, true, 0U,
                       0U, 10U};

    project::models::ProcessedFrame output{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::VersionMismatch),
        static_cast<int>(service.processStatus(input, output)));
    TEST_ASSERT_EQUAL_UINT32(7U, output.calibrationVersion);
    TEST_ASSERT_EQUAL(
        static_cast<int>(DataQualityState::VALID),
        static_cast<int>(output.channelQuality[static_cast<std::size_t>(
            SensorChannel::ImuAccelXRaw)]));
    TEST_ASSERT_EQUAL_UINT32(0U, output.validMask);
}

void testCalibrationServiceRejectsMissingFrameSchemaVersion() {
    project::services::CalibrationService service{};
    project::models::SensorFrame input{};
    project::models::ProcessedFrame output{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::InvalidArgument),
        static_cast<int>(service.processStatus(input, output)));
    TEST_ASSERT_EQUAL_UINT32(0U, output.validMask);
}

void testSchemaRegistryMatchesTestOnlyStructureAndVector() {
    TestSchemaRegistryFixture fixture{};
    project::ai::SchemaCompatibilityReport report{};
    const auto registry = fixture.registry();

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::Ok),
        static_cast<int>(registry.compareDetailed(report)));
    TEST_ASSERT_TRUE(report.structurallyCompatible);
    TEST_ASSERT_FALSE(report.productionCompatible);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::ai::SchemaMismatchReason::TestOnlyDescriptor),
        static_cast<int>(report.reason));

    const auto vector = fixture.vector();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::Ok),
        static_cast<int>(registry.validateDetailed(vector, report)));
    TEST_ASSERT_TRUE(report.structurallyCompatible);
    TEST_ASSERT_FALSE(registry.compatible());
    TEST_ASSERT_FALSE(registry.validate(vector));
}

void testSchemaRegistryRejectsCapacityOverflowAndMissingMetadata() {
    TestSchemaRegistryFixture fixture{};
    fixture.schema.featureCount =
        project::models::FeatureVector::kMaxFeatures + 1U;
    project::ai::SchemaCompatibilityReport report{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(fixture.registry().compareDetailed(report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::ai::SchemaMismatchReason::CapacityExceeded),
        static_cast<int>(report.reason));

    project::ai::SchemaRegistry unconfigured{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::NotInitialized),
        static_cast<int>(unconfigured.compareDetailed(report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::ai::SchemaMismatchReason::MissingDescriptor),
        static_cast<int>(report.reason));

    TestSchemaRegistryFixture missingModelMetadata{};
    missingModelMetadata.modelInput.verified = false;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::NotInitialized),
        static_cast<int>(
            missingModelMetadata.registry().compareDetailed(report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::ai::SchemaMismatchReason::MissingMetadata),
        static_cast<int>(report.reason));
}

void testSchemaRegistryReportsVersionHashAndCountMismatches() {
    TestSchemaRegistryFixture versionMismatch{};
    ++versionMismatch.model.featureSchemaVersion;
    assertSchemaMismatch(
        versionMismatch, project::ai::SchemaMismatchReason::VersionMismatch,
        project::ai::SchemaField::SchemaVersion);

    TestSchemaRegistryFixture hashMismatch{};
    versionMismatch.bind();
    hashMismatch.model.featureSchemaHash[0] ^= 0x01U;
    assertSchemaMismatch(hashMismatch,
                         project::ai::SchemaMismatchReason::HashMismatch,
                         project::ai::SchemaField::SchemaHash);

    TestSchemaRegistryFixture countMismatch{};
    countMismatch.modelInput.featureOrderCount = 1U;
    assertSchemaMismatch(
        countMismatch, project::ai::SchemaMismatchReason::FeatureCountMismatch,
        project::ai::SchemaField::FeatureCount);
}

void testSchemaRegistryReportsOrderUnitAndPreprocessingMismatches() {
    TestSchemaRegistryFixture orderMismatch{};
    orderMismatch.modelOrder[0] =
        project::services::FssFeatureId::TiltYDeg;
    assertSchemaMismatch(
        orderMismatch, project::ai::SchemaMismatchReason::FeatureOrderMismatch,
        project::ai::SchemaField::FeatureOrder);

    TestSchemaRegistryFixture unitMismatch{};
    unitMismatch.modelUnits[0] = project::ai::FeatureUnit::RawCounts;
    assertSchemaMismatch(
        unitMismatch, project::ai::SchemaMismatchReason::FeatureUnitMismatch,
        project::ai::SchemaField::FeatureUnit);

    TestSchemaRegistryFixture preprocessingMismatch{};
    ++preprocessingMismatch.modelInput.preprocessingVersion;
    assertSchemaMismatch(
        preprocessingMismatch,
        project::ai::SchemaMismatchReason::PreprocessingMismatch,
        project::ai::SchemaField::Preprocessing);
}

void testSchemaRegistryReportsWindowStrideAndSampleRateMismatches() {
    TestSchemaRegistryFixture windowMismatch{};
    ++windowMismatch.modelInput.windowLengthSamples;
    assertSchemaMismatch(
        windowMismatch, project::ai::SchemaMismatchReason::WindowMismatch,
        project::ai::SchemaField::WindowLength);

    TestSchemaRegistryFixture strideMismatch{};
    ++strideMismatch.modelInput.strideSamples;
    assertSchemaMismatch(
        strideMismatch, project::ai::SchemaMismatchReason::StrideMismatch,
        project::ai::SchemaField::Stride);

    TestSchemaRegistryFixture rateMismatch{};
    rateMismatch.modelInput.sampleRateHz = 20.0F;
    assertSchemaMismatch(
        rateMismatch, project::ai::SchemaMismatchReason::SampleRateMismatch,
        project::ai::SchemaField::SampleRate);
}

void testSchemaRegistryRejectsTestIdAsProductionDescriptor() {
    TestSchemaRegistryFixture fixture{};
    fixture.descriptor.kind = project::ai::SchemaDescriptorKind::Released;
    project::ai::SchemaCompatibilityReport report{};

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::Ok),
        static_cast<int>(fixture.registry().compareDetailed(report)));
    TEST_ASSERT_TRUE(report.structurallyCompatible);
    TEST_ASSERT_FALSE(report.productionCompatible);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::ai::SchemaMismatchReason::TestOnlyDescriptor),
        static_cast<int>(report.reason));
    TEST_ASSERT_FALSE(fixture.registry().compatible());
}

void testSchemaRegistryRejectsInvalidVectorContents() {
    TestSchemaRegistryFixture fixture{};
    const auto registry = fixture.registry();
    auto vector = fixture.vector();
    vector.featureValidMask = 0U;
    project::ai::SchemaCompatibilityReport report{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(registry.validateDetailed(vector, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::ai::SchemaMismatchReason::InvalidFeatureMask),
        static_cast<int>(report.reason));

    vector = fixture.vector();
    vector.values[1] = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::ApiStatus::DataInvalid),
        static_cast<int>(registry.validateDetailed(vector, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::ai::SchemaMismatchReason::NonFiniteValue),
        static_cast<int>(report.reason));
    TEST_ASSERT_EQUAL_UINT32(1U, static_cast<std::uint32_t>(report.featureIndex));
}

void testDatasetProvenanceRequiresSyntheticGt5() {
    using project::dataset::DataOrigin;
    using project::dataset::DataProvenance;
    using project::dataset::EvidenceClass;

    TEST_ASSERT_FALSE(DataProvenance{}.isValid());
    TEST_ASSERT_TRUE(
        (DataProvenance{DataOrigin::Synthetic, EvidenceClass::GT5}.isValid()));
    TEST_ASSERT_FALSE(
        (DataProvenance{DataOrigin::Synthetic, EvidenceClass::GT4}.isValid()));
    TEST_ASSERT_TRUE((DataProvenance{
                         DataOrigin::Field, EvidenceClass::NotApplicable}
                         .isValid()));
    TEST_ASSERT_FALSE((DataProvenance{
                          static_cast<DataOrigin>(255U), EvidenceClass::GT1}
                          .isValid()));
}

void testCsvRowWriterEscapesFieldsAndValidatesUtf8() {
    using project::dataset::CsvRowWriter;
    using project::dataset::CsvWriteStatus;

    char output[64]{};
    CsvRowWriter writer(output, sizeof(output), 1U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::Ok),
        static_cast<int>(writer.appendField("comma,quote\"cr\rlf\n")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(writer.finish()));
    TEST_ASSERT_EQUAL_STRING("\"comma,quote\"\"cr\rlf\n\"\n", output);
    TEST_ASSERT_EQUAL_PTR(output, writer.data());

    char invalidUtf8[] = {static_cast<char>(0xC0), static_cast<char>(0xAF)};
    CsvRowWriter invalid(invalidUtf8, sizeof(invalidUtf8), 1U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::InvalidUtf8),
        static_cast<int>(invalid.appendField(
            std::string_view(invalidUtf8, sizeof(invalidUtf8)))));
    TEST_ASSERT_NULL(invalid.data());
}

void testCsvRowWriterFormatsFiniteNumbersAndInvalidAsEmpty() {
    using project::dataset::CsvRowWriter;
    using project::dataset::CsvWriteStatus;

    char output[128]{};
    CsvRowWriter writer(output, sizeof(output), 8U);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(writer.appendNumber(-12.5F)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(writer.appendNumber(0.1F)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(writer.appendNumber(1.0e20F)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::Ok),
        static_cast<int>(writer.appendOptionalNumber(
            std::numeric_limits<float>::quiet_NaN(), false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(writer.appendBoolean(true)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(writer.appendBoolean(false)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(writer.appendSigned(-42)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::Ok),
        static_cast<int>(writer.appendUnsigned(UINT64_MAX)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(writer.finish()));
    TEST_ASSERT_EQUAL_STRING(
        "-12.5,0.1,1e+20,,true,false,-42,18446744073709551615\n",
        output);
}

void testCsvRowWriterRejectsNonFiniteAndColumnCountErrors() {
    using project::dataset::CsvRowWriter;
    using project::dataset::CsvWriteStatus;

    char output[32]{};
    CsvRowWriter nanWriter(output, sizeof(output), 1U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::NonFiniteNumber),
        static_cast<int>(nanWriter.appendNumber(
            std::numeric_limits<float>::quiet_NaN())));
    TEST_ASSERT_NULL(nanWriter.data());

    CsvRowWriter infinityWriter(output, sizeof(output), 1U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::NonFiniteNumber),
        static_cast<int>(
            infinityWriter.appendNumber(std::numeric_limits<double>::infinity())));

    CsvRowWriter tooFew(output, sizeof(output), 2U);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(tooFew.appendField("only-one")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::ColumnCountMismatch),
        static_cast<int>(tooFew.finish()));

    CsvRowWriter tooMany(output, sizeof(output), 1U);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(tooMany.appendField("first")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::ColumnCountExceeded),
        static_cast<int>(tooMany.appendField("extra")));
}

void testCsvRowWriterHandlesExactCapacityAndOverflow() {
    using project::dataset::CsvRowWriter;
    using project::dataset::CsvWriteStatus;

    char exact[4]{};
    CsvRowWriter exactWriter(exact, sizeof(exact), 2U);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(exactWriter.appendField("a")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(exactWriter.appendField("b")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(exactWriter.finish()));
    TEST_ASSERT_EQUAL_UINT32(4U, static_cast<std::uint32_t>(exactWriter.size()));
    TEST_ASSERT_EQUAL_MEMORY("a,b\n", exact, sizeof(exact));

    char oneByteShort[3]{};
    CsvRowWriter shortWriter(oneByteShort, sizeof(oneByteShort), 2U);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(shortWriter.appendField("a")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(shortWriter.appendField("b")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::CapacityExceeded),
        static_cast<int>(shortWriter.finish()));
    TEST_ASSERT_NULL(shortWriter.data());

    char appendOverflow[2]{};
    CsvRowWriter overflowWriter(appendOverflow, sizeof(appendOverflow), 2U);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CsvWriteStatus::Ok),
                          static_cast<int>(overflowWriter.appendField("a")));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::CapacityExceeded),
        static_cast<int>(overflowWriter.appendField("b")));
    TEST_ASSERT_NULL(overflowWriter.data());
}

void testDsdV12RawHeaderMatchesCoreSchemaAndFitsBound() {
    using project::dataset::CsvWriteStatus;
    using project::dataset::kDsdV1_2RawSampleCoreFields;
    using project::dataset::writeDsdV1_2RawSampleCoreHeader;

    constexpr char expected[] =
        "dataset_id,site_id,device_id,sensor_set_id,session_id,boot_id,"
        "sequence_no,timestamp_ms,utc_time,time_status,firmware_version,"
        "config_version,calibration_id,sensor_status_bits,accel_x_raw,"
        "accel_y_raw,accel_z_raw,soil_raw,yl83_raw,hx710b_raw,accel_x_valid,"
        "accel_y_valid,accel_z_valid,soil_valid,yl83_valid,hx710b_valid,"
        "accel_x_quality,accel_y_quality,accel_z_quality,soil_quality,"
        "yl83_quality,hx710b_quality,valid_mask,quality_flags,event_id,"
        "annotation_id\n";
    static_assert(kDsdV1_2RawSampleCoreFields.size() == 36U,
                  "DSD v1.2 raw core field count changed");

    char output[sizeof(expected)]{};
    std::size_t bytesWritten = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::Ok),
        static_cast<int>(writeDsdV1_2RawSampleCoreHeader(
            output, sizeof(output), bytesWritten)));
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected) - 1U,
                             static_cast<std::uint32_t>(bytesWritten));
    TEST_ASSERT_EQUAL_MEMORY(expected, output, sizeof(expected) - 1U);

    bytesWritten = 99U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CsvWriteStatus::CapacityExceeded),
        static_cast<int>(writeDsdV1_2RawSampleCoreHeader(
            output, sizeof(output) - 2U, bytesWritten)));
    TEST_ASSERT_EQUAL_UINT32(0U, static_cast<std::uint32_t>(bytesWritten));
}

struct MemoryDatasetCsvSource final : project::dataset::IDatasetCsvSource {
    const std::uint8_t* data{nullptr};
    std::size_t size{0U};
    std::size_t position{0U};
    std::size_t maxChunk{13U};
    std::size_t failAt{std::numeric_limits<std::size_t>::max()};
    bool rewindFails{false};

    MemoryDatasetCsvSource(const std::uint8_t* input,
                           std::size_t inputSize) noexcept
        : data(input), size(inputSize) {}

    project::dataset::DatasetCsvSourceStatus rewind() noexcept override {
        position = 0U;
        return rewindFails ? project::dataset::DatasetCsvSourceStatus::IoError
                           : project::dataset::DatasetCsvSourceStatus::Ok;
    }

    project::dataset::DatasetCsvSourceStatus read(
        std::uint8_t* output, std::size_t capacity,
        std::size_t& bytesRead) noexcept override {
        bytesRead = 0U;
        if (position >= failAt) {
            return project::dataset::DatasetCsvSourceStatus::IoError;
        }
        if (position == size) {
            return project::dataset::DatasetCsvSourceStatus::EndOfFile;
        }
        const std::size_t remaining = size - position;
        const std::size_t beforeFailure = failAt - position;
        const std::size_t amount =
            std::min(capacity, std::min(maxChunk,
                                        std::min(remaining, beforeFailure)));
        if (amount == 0U) {
            return project::dataset::DatasetCsvSourceStatus::IoError;
        }
        std::memcpy(output, data + position, amount);
        position += amount;
        bytesRead = amount;
        return project::dataset::DatasetCsvSourceStatus::Ok;
    }
};

bool appendRawSampleCsvRow(
    char* output, std::size_t capacity, std::size_t& offset,
    std::uint64_t sequenceNo, std::uint64_t timestampMs,
    std::string_view firstRaw = "10",
    std::string_view eventId = {}) {
    char row[5000]{};
    project::dataset::CsvRowWriter writer(
        row, sizeof(row),
        project::dataset::kDsdV1_2RawSampleCoreFields.size());
    const auto append = [&writer](std::string_view value) {
        return writer.appendField(value) ==
               project::dataset::CsvWriteStatus::Ok;
    };
    const auto appendUnsigned = [&writer](std::uint64_t value) {
        return writer.appendUnsigned(value) ==
               project::dataset::CsvWriteStatus::Ok;
    };
    const auto appendBoolean = [&writer](bool value) {
        return writer.appendBoolean(value) ==
               project::dataset::CsvWriteStatus::Ok;
    };
    if (!append("synthetic-dataset") || !append("synthetic-site") ||
        !append("synthetic-device") || !append("synthetic-sensor-set") ||
        !append("synthetic-session") || !appendUnsigned(1U) ||
        !appendUnsigned(sequenceNo) || !appendUnsigned(timestampMs) ||
        !append("") || !append("UNSYNCED") || !append("host-generator") ||
        !append("synthetic-config-v1") || !append("NONE") ||
        !appendUnsigned(0U) || !append(firstRaw) || !append("20") ||
        !append("30") || !append("") || !append("") || !append("") ||
        !appendBoolean(true) || !appendBoolean(true) ||
        !appendBoolean(true) || !appendBoolean(false) ||
        !appendBoolean(false) || !appendBoolean(false) || !append("VALID") ||
        !append("VALID") || !append("VALID") || !append("MISSING") ||
        !append("MISSING") || !append("MISSING") || !appendUnsigned(0U) ||
        !appendUnsigned(0U) || !append(eventId) || !append("")) {
        return false;
    }
    if (writer.finish() != project::dataset::CsvWriteStatus::Ok ||
        writer.size() > capacity - offset) {
        return false;
    }
    std::memcpy(output + offset, writer.data(), writer.size());
    offset += writer.size();
    return true;
}

bool createRawSamplesCsv(char* output, std::size_t capacity,
                         std::size_t& size, std::uint64_t firstSequence = 0U,
                         std::uint64_t secondSequence = 1U,
                         std::uint64_t firstTimestamp = 1000U,
                         std::uint64_t secondTimestamp = 1100U) {
    size = 0U;
    char header[1024]{};
    std::size_t headerSize = 0U;
    if (project::dataset::writeDsdV1_2RawSampleCoreHeader(
            header, sizeof(header), headerSize) !=
        project::dataset::CsvWriteStatus::Ok ||
        headerSize > capacity) {
        return false;
    }
    std::memcpy(output, header, headerSize);
    size = headerSize;
    return appendRawSampleCsvRow(output, capacity, size, firstSequence,
                                 firstTimestamp) &&
           appendRawSampleCsvRow(output, capacity, size, secondSequence,
                                 secondTimestamp);
}

bool setRawManifestForCsv(const char* data, std::size_t size,
                          std::uint64_t rowCount,
                          project::dataset::RawDatasetManifest& manifest) {
    manifest = {};
    manifest.datasetId = "synthetic-dataset";
    manifest.rawSchemaVersion =
        project::dataset::kRawSampleCoreSchemaVersion;
    manifest.expectedRows = rowCount;
    manifest.expectedBytes = size;
    manifest.provenance = {project::dataset::DataOrigin::Synthetic,
                           project::dataset::EvidenceClass::GT5};
    manifest.channelPresence = {{true, true, true, false, false, false}};
    manifest.channelPresenceConfigured = true;
    project::dataset::Sha256 hash{};
    if (!hash.update(reinterpret_cast<const std::uint8_t*>(data), size)) {
        return false;
    }
    return hash.finish(manifest.expectedSha256);
}

void testSyntheticGeneratorIsDeterministicAndCarriesGt5() {
    using namespace project::dataset;
    SyntheticGeneratorConfig config{};
    config.scenario = SyntheticScenario::ImpulseNoise;
    config.seed = 1234567U;
    config.recordCount = 8U;
    config.firstTimestampMs = 1000U;
    config.timestampStepMs = 25U;
    config.parameters.eventSample = 3U;
    config.parameters.eventLength = 2U;
    config.parameters.eventDelta = 9;
    config.parameters.noiseAmplitude = 2;

    SyntheticSensorGenerator first(config);
    SyntheticSensorGenerator second(config);
    TEST_ASSERT_EQUAL_UINT32(kSyntheticGeneratorVersion,
                             first.manifest().generatorVersion);
    TEST_ASSERT_EQUAL_UINT64(config.seed, first.manifest().seed);
    TEST_ASSERT_FALSE(first.manifest().proxyTargetPresent);
    TEST_ASSERT_FALSE(first.manifest().eligibleForFieldClaim);
    TEST_ASSERT_TRUE(first.manifest().provenance.isValid());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvidenceClass::GT5),
        static_cast<int>(first.manifest().provenance.evidenceClass));

    for (std::uint64_t index = 0U; index < config.recordCount; ++index) {
        SyntheticRecord left{};
        SyntheticRecord right{};
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SyntheticGeneratorStatus::Ok),
            static_cast<int>(first.next(left)));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(SyntheticGeneratorStatus::Ok),
            static_cast<int>(second.next(right)));
        TEST_ASSERT_EQUAL_UINT64(left.observation.sequenceNo,
                                 right.observation.sequenceNo);
        TEST_ASSERT_EQUAL_UINT64(left.observation.timestampMs,
                                 right.observation.timestampMs);
        TEST_ASSERT_EQUAL_INT64(left.observation.rawValues[0],
                                right.observation.rawValues[0]);
        TEST_ASSERT_EQUAL_UINT32(left.latent.validChannelMask,
                                 right.latent.validChannelMask);
        TEST_ASSERT_TRUE(left.observation.provenance.isValid());
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(EvidenceClass::GT5),
            static_cast<int>(left.observation.provenance.evidenceClass));
    }
    SyntheticRecord exhausted{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SyntheticGeneratorStatus::EndOfData),
        static_cast<int>(first.next(exhausted)));
}

void testSyntheticGeneratorCoversScenarioBoundaries() {
    using namespace project::dataset;
    const SyntheticScenario scenarios[] = {
        SyntheticScenario::Stable,
        SyntheticScenario::GradualTrend,
        SyntheticScenario::StepChange,
        SyntheticScenario::Oscillation,
        SyntheticScenario::ImpulseNoise,
        SyntheticScenario::Saturation,
        SyntheticScenario::Dropout,
        SyntheticScenario::Stale,
        SyntheticScenario::OutOfOrder,
        SyntheticScenario::SequenceGap,
        SyntheticScenario::Reboot,
        SyntheticScenario::CalibrationChange,
        SyntheticScenario::MixedValidity};

    for (const SyntheticScenario scenario : scenarios) {
        SyntheticGeneratorConfig config{};
        config.scenario = scenario;
        config.seed = 7U;
        config.recordCount = 8U;
        config.bootId = 4U;
        config.firstTimestampMs = 1000U;
        config.timestampStepMs = 10U;
        config.parameters.eventSample = 4U;
        config.parameters.eventLength = 2U;
        config.parameters.eventDelta = 20;
        config.parameters.oscillationPeriodSamples = 4U;
        config.parameters.sequenceGapSize = 2U;
        config.parameters.staleAgeMs = 200U;
        config.parameters.saturationMinimum = -3;
        config.parameters.saturationMaximum = 3;
        config.parameters.baseline = 2;
        config.parameters.affectedChannel = 2U;
        SyntheticSensorGenerator generator(config);
        SyntheticRecord records[8]{};
        for (auto& record : records) {
            TEST_ASSERT_EQUAL_INT(
                static_cast<int>(SyntheticGeneratorStatus::Ok),
                static_cast<int>(generator.next(record)));
        }
        TEST_ASSERT_TRUE(records[0].observation.provenance.isValid());
        switch (scenario) {
            case SyntheticScenario::Saturation:
                TEST_ASSERT_EQUAL_INT64(
                    config.parameters.saturationMaximum,
                    records[4].observation.rawValues[0]);
                break;
            case SyntheticScenario::Dropout:
                TEST_ASSERT_FALSE(records[4].observation.channelValid[2U]);
                TEST_ASSERT_EQUAL_INT64(0,
                                        records[4].observation.rawValues[2U]);
                break;
            case SyntheticScenario::Stale:
                TEST_ASSERT_TRUE(
                    records[4].observation.channelTimestampMs[0U] <
                    records[4].observation.evaluationTimeMs);
                break;
            case SyntheticScenario::OutOfOrder:
                TEST_ASSERT_TRUE(records[4].observation.sequenceNo <=
                                 records[3].observation.sequenceNo);
                TEST_ASSERT_TRUE(records[4].observation.timestampMs <=
                                 records[3].observation.timestampMs);
                break;
            case SyntheticScenario::SequenceGap:
                TEST_ASSERT_TRUE(records[4].observation.sequenceNo >
                                 records[3].observation.sequenceNo + 1U);
                TEST_ASSERT_EQUAL_UINT64(
                    records[4].observation.sequenceNo + 1U,
                    records[5].observation.sequenceNo);
                break;
            case SyntheticScenario::Reboot:
                TEST_ASSERT_NOT_EQUAL(records[3].observation.bootId,
                                      records[4].observation.bootId);
                TEST_ASSERT_EQUAL_UINT64(0U,
                                         records[4].observation.sequenceNo);
                TEST_ASSERT_EQUAL_UINT64(0U,
                                         records[4].observation.timestampMs);
                break;
            case SyntheticScenario::CalibrationChange:
                TEST_ASSERT_NOT_EQUAL(
                    records[3].observation.calibrationVersion,
                    records[4].observation.calibrationVersion);
                break;
            case SyntheticScenario::MixedValidity:
                TEST_ASSERT_FALSE(records[0].observation.channelValid[0U]);
                TEST_ASSERT_TRUE(records[0].observation.channelValid[1U]);
                break;
            case SyntheticScenario::Stable:
            case SyntheticScenario::GradualTrend:
            case SyntheticScenario::StepChange:
            case SyntheticScenario::Oscillation:
            case SyntheticScenario::ImpulseNoise:
                TEST_ASSERT_TRUE(records[4].observation.channelValid[0U]);
                break;
        }
    }
}

void testSyntheticGeneratorRejectsInvalidAndOverflowingConfig() {
    using namespace project::dataset;
    SyntheticGeneratorConfig invalid{};
    invalid.recordCount = 1U;
    invalid.timestampStepMs = 1U;
    invalid.seed = 1U;
    invalid.scenario = static_cast<SyntheticScenario>(0xFFU);
    TEST_ASSERT_FALSE(SyntheticSensorGenerator::validateConfig(invalid));

    invalid.scenario = SyntheticScenario::Stable;
    invalid.timestampStepMs = 0U;
    TEST_ASSERT_FALSE(SyntheticSensorGenerator::validateConfig(invalid));

    SyntheticGeneratorConfig overflow{};
    overflow.scenario = SyntheticScenario::StepChange;
    overflow.seed = 1U;
    overflow.recordCount = 1U;
    overflow.timestampStepMs = 1U;
    overflow.parameters.baseline = INT64_MAX;
    overflow.parameters.eventSample = 0U;
    overflow.parameters.eventDelta = 1;
    SyntheticSensorGenerator generator(overflow);
    SyntheticRecord record{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SyntheticGeneratorStatus::NumericOverflow),
        static_cast<int>(generator.next(record)));
    TEST_ASSERT_FALSE(record.observation.provenance.isValid());
}

void testSha256MatchesKnownVectorAndBoundaryBlocks() {
    using project::dataset::Sha256;
    Sha256 hash{};
    const char input[] = "abc";
    TEST_ASSERT_TRUE(hash.update(
        reinterpret_cast<const std::uint8_t*>(input), sizeof(input) - 1U));
    std::array<std::uint8_t, Sha256::kDigestSize> digest{};
    TEST_ASSERT_TRUE(hash.finish(digest));
    const std::uint8_t expected[] = {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
        0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
        0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
        0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD};
    TEST_ASSERT_EQUAL_MEMORY(expected, digest.data(), sizeof(expected));

    std::uint8_t block[128]{};
    hash.reset();
    TEST_ASSERT_TRUE(hash.update(block, sizeof(block)));
    TEST_ASSERT_TRUE(hash.finish(digest));
    TEST_ASSERT_FALSE(hash.finish(digest));
}

void testDsdRawDatasetValidatorAcceptsExactSchemaAndManifest() {
    char csv[4096]{};
    std::size_t csvSize = 0U;
    TEST_ASSERT_TRUE(createRawSamplesCsv(csv, sizeof(csv), csvSize));
    project::dataset::RawDatasetManifest manifest{};
    TEST_ASSERT_TRUE(setRawManifestForCsv(csv, csvSize, 2U, manifest));

    MemoryDatasetCsvSource source{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    project::dataset::DatasetValidator validator{};
    project::dataset::IdentityWorkspaceEntry entries[4]{};
    std::uint8_t keyBytes[1024]{};
    project::dataset::DatasetValidationWorkspace workspace{
        entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    project::dataset::QuarantineReference quarantine[2]{};
    project::dataset::DatasetValidationReport report{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::Complete),
        static_cast<int>(validator.validateRawSamples(
            source, manifest, workspace, quarantine, 2U, report)));
    TEST_ASSERT_EQUAL_UINT64(2U, report.acceptedRows);
    TEST_ASSERT_EQUAL_UINT64(0U, report.rejectedRows);
    TEST_ASSERT_TRUE(report.hashVerified);
    TEST_ASSERT_TRUE(report.rowCountVerified);
    TEST_ASSERT_EQUAL_UINT64(csvSize, report.bytesRead);

    MemoryDatasetCsvSource replaySource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    project::dataset::IdentityWorkspaceEntry replayEntries[4]{};
    std::uint8_t replayKeyBytes[1024]{};
    project::dataset::DatasetValidationWorkspace replayWorkspace{
        replayEntries, 4U, 0U, replayKeyBytes, sizeof(replayKeyBytes), 0U};
    project::dataset::QuarantineReference replayQuarantine[2]{};
    project::dataset::DatasetValidationReport replayReport{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::Complete),
        static_cast<int>(validator.validateRawSamples(
            replaySource, manifest, replayWorkspace, replayQuarantine, 2U,
            replayReport)));
    TEST_ASSERT_EQUAL_UINT64(report.acceptedRows, replayReport.acceptedRows);
    TEST_ASSERT_EQUAL_UINT64(report.rejectedRows, replayReport.rejectedRows);
    TEST_ASSERT_EQUAL_UINT64(report.bytesRead, replayReport.bytesRead);
    TEST_ASSERT_EQUAL_MEMORY(report.computedSha256.data(),
                             replayReport.computedSha256.data(),
                             report.computedSha256.size());
}

void testDsdRawDatasetValidatorQuarantinesDuplicatesAndSequenceGaps() {
    char csv[4096]{};
    std::size_t csvSize = 0U;
    char header[1024]{};
    std::size_t headerSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::CsvWriteStatus::Ok),
        static_cast<int>(project::dataset::writeDsdV1_2RawSampleCoreHeader(
            header, sizeof(header), headerSize)));
    std::memcpy(csv, header, headerSize);
    csvSize = headerSize;
    TEST_ASSERT_TRUE(appendRawSampleCsvRow(csv, sizeof(csv), csvSize, 1U,
                                           1000U));
    TEST_ASSERT_TRUE(appendRawSampleCsvRow(csv, sizeof(csv), csvSize, 3U,
                                           1200U));
    TEST_ASSERT_TRUE(appendRawSampleCsvRow(csv, sizeof(csv), csvSize, 1U,
                                           1300U));
    project::dataset::RawDatasetManifest manifest{};
    TEST_ASSERT_TRUE(setRawManifestForCsv(csv, csvSize, 3U, manifest));

    MemoryDatasetCsvSource source{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    project::dataset::DatasetValidator validator{};
    project::dataset::IdentityWorkspaceEntry entries[4]{};
    std::uint8_t keyBytes[1024]{};
    project::dataset::DatasetValidationWorkspace workspace{
        entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    project::dataset::QuarantineReference quarantine[4]{};
    project::dataset::DatasetValidationReport report{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::DatasetValidationStatus::CompleteWithRejectedRows),
        static_cast<int>(validator.validateRawSamples(
            source, manifest, workspace, quarantine, 4U, report)));
    TEST_ASSERT_EQUAL_UINT64(1U, report.acceptedRows);
    TEST_ASSERT_EQUAL_UINT64(2U, report.rejectedRows);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::RawRowRejectionReason::SequenceGap),
        static_cast<int>(quarantine[0U].reason));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::RawRowRejectionReason::DuplicateIdentity),
        static_cast<int>(quarantine[1U].reason));
}

void testDsdRawDatasetValidatorRejectsEncodingQuoteAndTruncation() {
    char csv[4096]{};
    std::size_t csvSize = 0U;
    TEST_ASSERT_TRUE(createRawSamplesCsv(csv, sizeof(csv), csvSize));
    const char* site = std::strstr(csv, "synthetic-site");
    TEST_ASSERT_NOT_NULL(site);
    const auto siteOffset = static_cast<std::size_t>(site - csv);
    csv[siteOffset] = static_cast<char>(0xC0);
    csv[siteOffset + 1U] = static_cast<char>(0xAF);

    project::dataset::RawDatasetManifest manifest{};
    TEST_ASSERT_TRUE(setRawManifestForCsv(csv, csvSize, 2U, manifest));
    MemoryDatasetCsvSource source{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    project::dataset::DatasetValidator validator{};
    project::dataset::IdentityWorkspaceEntry entries[4]{};
    std::uint8_t keyBytes[1024]{};
    project::dataset::DatasetValidationWorkspace workspace{
        entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    project::dataset::QuarantineReference quarantine[2]{};
    project::dataset::DatasetValidationReport report{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::DatasetValidationStatus::CompleteWithRejectedRows),
        static_cast<int>(validator.validateRawSamples(
            source, manifest, workspace, quarantine, 2U, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::RawRowRejectionReason::InvalidUtf8),
        static_cast<int>(quarantine[0U].reason));

    char malformed[4096]{};
    std::size_t malformedSize = 0U;
    TEST_ASSERT_TRUE(createRawSamplesCsv(malformed, sizeof(malformed),
                                        malformedSize));
    char* numeric = std::strstr(malformed, ",10,20");
    TEST_ASSERT_NOT_NULL(numeric);
    numeric[2] = '"';
    TEST_ASSERT_TRUE(setRawManifestForCsv(malformed, malformedSize, 2U,
                                          manifest));
    MemoryDatasetCsvSource malformedSource{
        reinterpret_cast<const std::uint8_t*>(malformed), malformedSize};
    workspace = {entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::DatasetValidationStatus::CompleteWithRejectedRows),
        static_cast<int>(validator.validateRawSamples(
            malformedSource, manifest, workspace, quarantine, 2U, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::RawRowRejectionReason::MalformedQuote),
        static_cast<int>(quarantine[0U].reason));

    char truncated[4096]{};
    std::size_t truncatedSize = 0U;
    TEST_ASSERT_TRUE(createRawSamplesCsv(truncated, sizeof(truncated),
                                        truncatedSize));
    --truncatedSize;
    TEST_ASSERT_TRUE(setRawManifestForCsv(truncated, truncatedSize, 2U,
                                          manifest));
    MemoryDatasetCsvSource truncatedSource{
        reinterpret_cast<const std::uint8_t*>(truncated), truncatedSize};
    workspace = {entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::TruncatedRecord),
        static_cast<int>(validator.validateRawSamples(
            truncatedSource, manifest, workspace, quarantine, 2U, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::RawRowRejectionReason::TruncatedRecord),
        static_cast<int>(quarantine[0U].reason));
}

void testDsdRawDatasetValidatorReportsManifestAndInjectedFailures() {
    char csv[4096]{};
    std::size_t csvSize = 0U;
    TEST_ASSERT_TRUE(createRawSamplesCsv(csv, sizeof(csv), csvSize));
    project::dataset::RawDatasetManifest manifest{};
    TEST_ASSERT_TRUE(setRawManifestForCsv(csv, csvSize, 2U, manifest));

    project::dataset::DatasetValidator validator{};
    project::dataset::IdentityWorkspaceEntry entries[4]{};
    std::uint8_t keyBytes[1024]{};
    project::dataset::DatasetValidationWorkspace workspace{
        entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    project::dataset::QuarantineReference quarantine[2]{};
    project::dataset::DatasetValidationReport report{};

    auto badHashManifest = manifest;
    badHashManifest.expectedSha256[0U] ^= 0x01U;
    MemoryDatasetCsvSource hashSource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::DatasetValidationStatus::ManifestHashMismatch),
        static_cast<int>(validator.validateRawSamples(
            hashSource, badHashManifest, workspace, quarantine, 2U, report)));

    auto badCountManifest = manifest;
    ++badCountManifest.expectedRows;
    workspace = {entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    MemoryDatasetCsvSource countSource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::DatasetValidationStatus::ManifestRowCountMismatch),
        static_cast<int>(validator.validateRawSamples(
            countSource, badCountManifest, workspace, quarantine, 2U,
            report)));

    workspace = {entries, 1U, 0U, keyBytes, sizeof(keyBytes), 0U};
    MemoryDatasetCsvSource workspaceSource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::IdentityWorkspaceExceeded),
        static_cast<int>(validator.validateRawSamples(
            workspaceSource, manifest, workspace, quarantine, 2U, report)));

    workspace = {entries, 4U, 0U, keyBytes, 1U, 0U};
    MemoryDatasetCsvSource keySource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::IdentityWorkspaceExceeded),
        static_cast<int>(validator.validateRawSamples(
            keySource, manifest, workspace, quarantine, 2U, report)));

    workspace = {entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    MemoryDatasetCsvSource ioSource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    ioSource.failAt = 30U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::SourceError),
        static_cast<int>(validator.validateRawSamples(
            ioSource, manifest, workspace, quarantine, 2U, report)));

    MemoryDatasetCsvSource rewindSource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    rewindSource.rewindFails = true;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::SourceError),
        static_cast<int>(validator.validateRawSamples(
            rewindSource, manifest, workspace, quarantine, 2U, report)));
}

void testDsdRawDatasetValidatorRejectsBadFieldsAndTimeOrder() {
    char csv[4096]{};
    std::size_t csvSize = 0U;
    TEST_ASSERT_TRUE(createRawSamplesCsv(csv, sizeof(csv), csvSize));
    char* numeric = std::strstr(csv, ",10,20");
    TEST_ASSERT_NOT_NULL(numeric);
    numeric[2] = 'x';
    project::dataset::RawDatasetManifest manifest{};
    TEST_ASSERT_TRUE(setRawManifestForCsv(csv, csvSize, 2U, manifest));
    project::dataset::DatasetValidator validator{};
    project::dataset::IdentityWorkspaceEntry entries[4]{};
    std::uint8_t keyBytes[1024]{};
    project::dataset::DatasetValidationWorkspace workspace{
        entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    project::dataset::QuarantineReference quarantine[4]{};
    project::dataset::DatasetValidationReport report{};
    MemoryDatasetCsvSource numericSource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::DatasetValidationStatus::CompleteWithRejectedRows),
        static_cast<int>(validator.validateRawSamples(
            numericSource, manifest, workspace, quarantine, 4U, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::RawRowRejectionReason::InvalidNumeric),
        static_cast<int>(quarantine[0U].reason));

    char outOfOrder[4096]{};
    std::size_t outOfOrderSize = 0U;
    char header[1024]{};
    std::size_t headerSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::CsvWriteStatus::Ok),
        static_cast<int>(project::dataset::writeDsdV1_2RawSampleCoreHeader(
            header, sizeof(header), headerSize)));
    std::memcpy(outOfOrder, header, headerSize);
    outOfOrderSize = headerSize;
    TEST_ASSERT_TRUE(appendRawSampleCsvRow(
        outOfOrder, sizeof(outOfOrder), outOfOrderSize, 0U, 1000U));
    TEST_ASSERT_TRUE(appendRawSampleCsvRow(
        outOfOrder, sizeof(outOfOrder), outOfOrderSize, 1U, 900U));
    TEST_ASSERT_TRUE(setRawManifestForCsv(outOfOrder, outOfOrderSize, 2U,
                                          manifest));
    workspace = {entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    MemoryDatasetCsvSource timeSource{
        reinterpret_cast<const std::uint8_t*>(outOfOrder), outOfOrderSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::DatasetValidationStatus::CompleteWithRejectedRows),
        static_cast<int>(validator.validateRawSamples(
            timeSource, manifest, workspace, quarantine, 4U, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::RawRowRejectionReason::OutOfOrder),
        static_cast<int>(quarantine[0U].reason));
    TEST_ASSERT_EQUAL_UINT64(1U, report.acceptedRows);
}

void testDsdRawDatasetValidatorReservesIdentityAndFailsClosed() {
    char csv[4096]{};
    std::size_t csvSize = 0U;
    char header[1024]{};
    std::size_t headerSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::CsvWriteStatus::Ok),
        static_cast<int>(project::dataset::writeDsdV1_2RawSampleCoreHeader(
            header, sizeof(header), headerSize)));
    std::memcpy(csv, header, headerSize);
    csvSize = headerSize;
    TEST_ASSERT_TRUE(appendRawSampleCsvRow(csv, sizeof(csv), csvSize, 0U,
                                           1000U, "invalid"));
    TEST_ASSERT_TRUE(appendRawSampleCsvRow(csv, sizeof(csv), csvSize, 0U,
                                           1000U));

    project::dataset::RawDatasetManifest manifest{};
    TEST_ASSERT_TRUE(setRawManifestForCsv(csv, csvSize, 2U, manifest));
    project::dataset::DatasetValidator validator{};
    project::dataset::IdentityWorkspaceEntry entries[4]{};
    std::uint8_t keyBytes[1024]{};
    project::dataset::DatasetValidationWorkspace workspace{
        entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    project::dataset::QuarantineReference quarantine[4]{};
    project::dataset::DatasetValidationReport report{};
    MemoryDatasetCsvSource duplicateSource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::
                             CompleteWithRejectedRows),
        static_cast<int>(validator.validateRawSamples(
            duplicateSource, manifest, workspace, quarantine, 4U, report)));
    TEST_ASSERT_EQUAL_UINT64(0U, report.acceptedRows);
    TEST_ASSERT_EQUAL_UINT64(2U, report.rejectedRows);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::RawRowRejectionReason::InvalidNumeric),
        static_cast<int>(quarantine[0U].reason));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::RawRowRejectionReason::DuplicateIdentity),
        static_cast<int>(quarantine[1U].reason));

    TEST_ASSERT_TRUE(createRawSamplesCsv(csv, sizeof(csv), csvSize));
    TEST_ASSERT_TRUE(setRawManifestForCsv(csv, csvSize, 2U, manifest));
    manifest.channelPresence[0U] = false;
    workspace = {entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    MemoryDatasetCsvSource channelSource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::
                             CompleteWithRejectedRows),
        static_cast<int>(validator.validateRawSamples(
            channelSource, manifest, workspace, quarantine, 4U, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::RawRowRejectionReason::ChannelPresenceMismatch),
        static_cast<int>(quarantine[0U].reason));

    TEST_ASSERT_TRUE(createRawSamplesCsv(csv, sizeof(csv), csvSize));
    std::size_t lastDelimiter = csvSize - 2U;
    while (lastDelimiter > 0U && csv[lastDelimiter] != ',') {
        --lastDelimiter;
    }
    TEST_ASSERT_TRUE(lastDelimiter > 0U);
    std::memmove(csv + lastDelimiter, csv + lastDelimiter + 1U,
                 csvSize - lastDelimiter - 1U);
    --csvSize;
    TEST_ASSERT_TRUE(setRawManifestForCsv(csv, csvSize, 2U, manifest));
    workspace = {entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    MemoryDatasetCsvSource columnSource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::
                             CompleteWithRejectedRows),
        static_cast<int>(validator.validateRawSamples(
            columnSource, manifest, workspace, quarantine, 4U, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::RawRowRejectionReason::WrongColumnCount),
        static_cast<int>(quarantine[0U].reason));

    TEST_ASSERT_TRUE(createRawSamplesCsv(csv, sizeof(csv), csvSize));
    char* numeric = std::strstr(csv, ",10,20");
    TEST_ASSERT_NOT_NULL(numeric);
    numeric[2] = 'x';
    TEST_ASSERT_TRUE(setRawManifestForCsv(csv, csvSize, 2U, manifest));
    workspace = {entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    MemoryDatasetCsvSource noQuarantineSource{
        reinterpret_cast<const std::uint8_t*>(csv), csvSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::
                             QuarantineCapacityExceeded),
        static_cast<int>(validator.validateRawSamples(
            noQuarantineSource, manifest, workspace, nullptr, 0U, report)));
}

void testDsdRawDatasetValidatorChecksHeaderAndRecordCapacity() {
    project::dataset::DatasetValidator validator{};
    project::dataset::IdentityWorkspaceEntry entries[4]{};
    std::uint8_t keyBytes[12000]{};
    project::dataset::DatasetValidationWorkspace workspace{
        entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    project::dataset::QuarantineReference quarantine[2]{};
    project::dataset::DatasetValidationReport report{};
    project::dataset::RawDatasetManifest manifest{};

    char wrongHeader[1024]{};
    std::size_t wrongHeaderSize = 0U;
    TEST_ASSERT_TRUE(createRawSamplesCsv(wrongHeader, sizeof(wrongHeader),
                                        wrongHeaderSize));
    wrongHeader[0U] = 'x';
    TEST_ASSERT_TRUE(setRawManifestForCsv(wrongHeader, wrongHeaderSize, 2U,
                                          manifest));
    MemoryDatasetCsvSource headerSource{
        reinterpret_cast<const std::uint8_t*>(wrongHeader), wrongHeaderSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::DatasetValidationStatus::HeaderSchemaMismatch),
        static_cast<int>(validator.validateRawSamples(
            headerSource, manifest, workspace, quarantine, 2U, report)));

    char exact[8192]{};
    std::size_t exactSize = 0U;
    char header[1024]{};
    std::size_t headerSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::CsvWriteStatus::Ok),
        static_cast<int>(project::dataset::writeDsdV1_2RawSampleCoreHeader(
            header, sizeof(header), headerSize)));
    std::memcpy(exact, header, headerSize);
    exactSize = headerSize;
    char baseRow[5000]{};
    std::size_t baseRowSize = 0U;
    TEST_ASSERT_TRUE(appendRawSampleCsvRow(baseRow, sizeof(baseRow),
                                           baseRowSize, 0U, 1000U));
    std::array<char, project::dataset::kMaxRawCsvRecordBytes> event{};
    const std::size_t eventLength =
        project::dataset::kMaxRawCsvRecordBytes - baseRowSize;
    event.fill('e');
    TEST_ASSERT_TRUE(appendRawSampleCsvRow(
        exact, sizeof(exact), exactSize, 0U, 1000U,
        "10", std::string_view(event.data(), eventLength)));
    TEST_ASSERT_EQUAL_UINT32(
        static_cast<std::uint32_t>(project::dataset::kMaxRawCsvRecordBytes),
        static_cast<std::uint32_t>(exactSize - headerSize));
    TEST_ASSERT_TRUE(setRawManifestForCsv(exact, exactSize, 1U, manifest));
    workspace = {entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    MemoryDatasetCsvSource exactSource{
        reinterpret_cast<const std::uint8_t*>(exact), exactSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::DatasetValidationStatus::Complete),
        static_cast<int>(validator.validateRawSamples(
            exactSource, manifest, workspace, quarantine, 2U, report)));

    char overlong[8192]{};
    std::size_t overlongSize = 0U;
    std::memcpy(overlong, header, headerSize);
    overlongSize = headerSize;
    TEST_ASSERT_TRUE(appendRawSampleCsvRow(
        overlong, sizeof(overlong), overlongSize, 0U, 1000U,
        "10", std::string_view(event.data(), eventLength + 1U)));
    TEST_ASSERT_TRUE(setRawManifestForCsv(overlong, overlongSize, 1U,
                                          manifest));
    workspace = {entries, 4U, 0U, keyBytes, sizeof(keyBytes), 0U};
    MemoryDatasetCsvSource overlongSource{
        reinterpret_cast<const std::uint8_t*>(overlong), overlongSize};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::dataset::DatasetValidationStatus::CompleteWithRejectedRows),
        static_cast<int>(validator.validateRawSamples(
            overlongSource, manifest, workspace, quarantine, 2U, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::dataset::RawRowRejectionReason::RecordTooLong),
        static_cast<int>(quarantine[0U].reason));
}

void testDataQualityClassifiesCalibrationInvalidAndContinuity() {
    auto config = qualityConfig();
    const auto channelIndex =
        static_cast<std::size_t>(SensorChannel::SoilMoisturePct);
    config.calibrationPolicies[channelIndex] = {0U, true, false};
    project::services::DataQualityService service(config);
    auto input = goodSensorFrame();
    project::models::ProcessedFrame output{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::DataQualityServiceStatus::Ok),
        static_cast<int>(service.process(input, 100U, output)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataQualityState::CALIBRATION_INVALID),
        static_cast<int>(output.channelQuality[channelIndex]));

    using project::models::DataContinuityState;
    const auto classify =
        project::services::DataQualityService::classifyContinuity;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataContinuityState::Contiguous),
        static_cast<int>(classify(1U, 4U, 100U, 1U, 5U, 200U, 100.0, 0U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataContinuityState::SequenceGap),
        static_cast<int>(classify(1U, 4U, 100U, 1U, 6U, 200U, 100.0, 0U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataContinuityState::OutOfOrder),
        static_cast<int>(classify(1U, 4U, 100U, 1U, 4U, 200U, 100.0, 0U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataContinuityState::BootChanged),
        static_cast<int>(classify(1U, 4U, 100U, 2U, 0U, 0U, 100.0, 0U)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataContinuityState::TimestampDiscontinuity),
        static_cast<int>(classify(1U, 4U, 100U, 1U, 5U, 201U, 100.0, 0U)));
}

void testFeaturePipelineRejectsInvalidQualityAndPropagatesWindowSummary() {
    project::services::FeaturePipeline invalidPipeline(featureConfig());
    auto invalidFrame = goodProcessedFrame(1U, 1000U, 0.0F, 0.0F, 1.0F);
    invalidFrame.channelQuality[static_cast<std::size_t>(
        SensorChannel::AccelYG)] = DataQualityState::CALIBRATION_INVALID;
    TEST_ASSERT_FALSE(invalidPipeline.push(invalidFrame));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::models::WindowEligibilityState::Rejected),
        static_cast<int>(invalidPipeline.windowQuality().eligibility));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::models::WindowRejectionReason::RequiredChannelQuality),
        static_cast<int>(invalidPipeline.windowQuality().rejectionReason));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(SensorChannel::AccelYG),
        static_cast<int>(invalidPipeline.windowQuality().rejectedChannel));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataQualityState::CALIBRATION_INVALID),
        static_cast<int>(
            invalidPipeline.windowQuality().rejectedChannelQuality));

    project::services::FeaturePipeline gapPipeline(featureConfig());
    TEST_ASSERT_TRUE(gapPipeline.push(
        goodProcessedFrame(1U, 1000U, 0.0F, 0.0F, 1.0F)));
    TEST_ASSERT_TRUE(gapPipeline.push(
        goodProcessedFrame(3U, 1200U, 0.0F, 1.0F, 1.0F)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::DataContinuityState::SequenceGap),
        static_cast<int>(gapPipeline.windowQuality().boundaryState));
    TEST_ASSERT_TRUE(gapPipeline.push(
        goodProcessedFrame(4U, 1300U, 0.0F, 2.0F, 1.0F)));
    project::models::FeatureVector features{};
    TEST_ASSERT_TRUE(gapPipeline.build(features));
    TEST_ASSERT_EQUAL_UINT64(1200U, features.windowStartMs);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::WindowEligibilityState::Eligible),
        static_cast<int>(features.quality.eligibility));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::DataContinuityState::SequenceGap),
        static_cast<int>(features.quality.boundaryState));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataQualityState::VALID),
        static_cast<int>(features.quality.channelQuality[static_cast<std::size_t>(
            SensorChannel::AccelXG)]));

    project::services::FeaturePipeline orderPipeline(featureConfig());
    TEST_ASSERT_TRUE(orderPipeline.push(
        goodProcessedFrame(2U, 1100U, 0.0F, 0.0F, 1.0F)));
    TEST_ASSERT_FALSE(orderPipeline.push(
        goodProcessedFrame(2U, 1200U, 0.0F, 1.0F, 1.0F)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::models::WindowRejectionReason::OutOfOrder),
        static_cast<int>(orderPipeline.windowQuality().rejectionReason));
    TEST_ASSERT_FALSE(orderPipeline.windowReady());
}

void testFeaturePipelinePinsInputSchemaAndRejectsChanges() {
    project::services::FeaturePipeline pipeline(featureConfig());
    auto first = goodProcessedFrame(1U, 1000U, 0.0F, 0.0F, 1.0F);
    first.schemaVersion = 11U;
    TEST_ASSERT_TRUE(pipeline.push(first));

    auto changed = goodProcessedFrame(2U, 1100U, 0.0F, 1.0F, 1.0F);
    changed.schemaVersion = 12U;
    TEST_ASSERT_FALSE(pipeline.push(changed));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::services::FeaturePipelineStatus::
                             InvalidConfiguration),
        static_cast<int>(pipeline.status()));
    TEST_ASSERT_FALSE(pipeline.windowReady());

    auto sameSchema = goodProcessedFrame(2U, 1100U, 0.0F, 1.0F, 1.0F);
    sameSchema.schemaVersion = 11U;
    TEST_ASSERT_TRUE(pipeline.push(sameSchema));
    auto next = goodProcessedFrame(3U, 1200U, 0.0F, 1.0F, 1.0F);
    next.schemaVersion = 11U;
    TEST_ASSERT_TRUE(pipeline.push(next));

    project::models::FeatureVector output{};
    TEST_ASSERT_TRUE(pipeline.build(output));
    TEST_ASSERT_EQUAL_UINT64(1100U, output.windowStartMs);
}

void testFeaturePipelineResetsAtBootCalibrationAndSensorBoundaries() {
    project::models::FeatureVector output{};

    project::services::FeaturePipeline timestampPipeline(featureConfig());
    TEST_ASSERT_TRUE(timestampPipeline.push(
        goodProcessedFrame(1U, 1000U, 0.0F, 0.0F, 1.0F)));
    TEST_ASSERT_TRUE(timestampPipeline.push(
        goodProcessedFrame(2U, 1200U, 0.0F, 1.0F, 1.0F)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::models::DataContinuityState::TimestampDiscontinuity),
        static_cast<int>(timestampPipeline.windowQuality().boundaryState));
    TEST_ASSERT_TRUE(timestampPipeline.push(
        goodProcessedFrame(3U, 1300U, 0.0F, 1.0F, 1.0F)));
    TEST_ASSERT_TRUE(timestampPipeline.build(output));
    TEST_ASSERT_EQUAL_UINT64(1200U, output.windowStartMs);

    project::services::FeaturePipeline bootPipeline(featureConfig());
    TEST_ASSERT_TRUE(bootPipeline.push(
        goodProcessedFrame(10U, 1000U, 0.0F, 0.0F, 1.0F)));
    auto rebooted = goodProcessedFrame(0U, 0U, 0.0F, 1.0F, 1.0F);
    rebooted.bootId = 6U;
    TEST_ASSERT_TRUE(bootPipeline.push(rebooted));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(project::models::DataContinuityState::BootChanged),
        static_cast<int>(bootPipeline.windowQuality().boundaryState));
    auto postReboot = goodProcessedFrame(1U, 100U, 0.0F, 1.0F, 1.0F);
    postReboot.bootId = 6U;
    TEST_ASSERT_TRUE(bootPipeline.push(postReboot));
    TEST_ASSERT_TRUE(bootPipeline.build(output));
    TEST_ASSERT_EQUAL_UINT64(0U, output.windowStartMs);
    TEST_ASSERT_EQUAL_UINT64(6U, output.bootId);

    project::services::FeaturePipeline calibrationPipeline(featureConfig());
    TEST_ASSERT_TRUE(calibrationPipeline.push(
        goodProcessedFrame(1U, 1000U, 0.0F, 0.0F, 1.0F)));
    auto recalibrated = goodProcessedFrame(2U, 1100U, 0.0F, 1.0F, 1.0F);
    recalibrated.calibrationVersion = 2U;
    TEST_ASSERT_TRUE(calibrationPipeline.push(recalibrated));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            project::models::DataContinuityState::CalibrationChanged),
        static_cast<int>(calibrationPipeline.windowQuality().boundaryState));
    auto postCalibration = goodProcessedFrame(3U, 1200U, 0.0F, 1.0F, 1.0F);
    postCalibration.calibrationVersion = 2U;
    TEST_ASSERT_TRUE(calibrationPipeline.push(postCalibration));
    TEST_ASSERT_TRUE(calibrationPipeline.build(output));
    TEST_ASSERT_EQUAL_UINT64(1100U, output.windowStartMs);

    project::services::FeaturePipeline restartPipeline(featureConfig());
    TEST_ASSERT_TRUE(restartPipeline.push(
        goodProcessedFrame(1U, 1000U, 0.0F, 0.0F, 1.0F)));
    restartPipeline.resetWindow();
    TEST_ASSERT_FALSE(restartPipeline.windowReady());
    TEST_ASSERT_TRUE(restartPipeline.push(
        goodProcessedFrame(2U, 1100U, 0.0F, 1.0F, 1.0F)));
    TEST_ASSERT_TRUE(restartPipeline.push(
        goodProcessedFrame(3U, 1200U, 0.0F, 1.0F, 1.0F)));
    TEST_ASSERT_TRUE(restartPipeline.build(output));
    TEST_ASSERT_EQUAL_UINT64(1100U, output.windowStartMs);
}

void testFeaturePipelineGoldenSyntheticTiltFixture() {
    auto config = featureConfig();
    config.featureCount = 1U;
    config.features[0] = {
        project::services::FssFeatureId::TiltXDeg, 0U, true};
    config.windowLengthSamples = 2U;

    project::services::FeaturePipeline pipeline(config);
    TEST_ASSERT_TRUE(pipeline.push(
        goodProcessedFrame(20U, 2000U, 1.0F, 0.0F, 1.0F)));
    TEST_ASSERT_FALSE(pipeline.windowReady());
    TEST_ASSERT_TRUE(pipeline.push(
        goodProcessedFrame(21U, 2100U, 1.0F, 1.0F, 1.0F)));
    TEST_ASSERT_TRUE(pipeline.windowReady());

    project::models::FeatureVector output{};
    TEST_ASSERT_TRUE(pipeline.build(output));
    TEST_ASSERT_EQUAL_UINT16(2U, output.count);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, output.values[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.02F, 35.26439F, output.values[1]);
}

project::dataset::AnnotationTargetContract annotationTargetContract() {
    return {"test-contract", "test-target", "test-unit", 3U, true};
}

project::dataset::AnnotationRecord independentAnnotation(
    std::string_view annotationId = "annotation-1") {
    using namespace project::dataset;
    AnnotationRecord record{};
    record.annotationId.assign(annotationId);
    record.datasetId.assign("dataset-1");
    record.siteId.assign("site-1");
    record.sessionId.assign("session-1");
    record.bootId = 7U;
    record.intervalStartMs = 100U;
    record.intervalEndMs = 200U;
    record.targetName.assign("test-target");
    record.targetValue = 0.25;
    record.targetValueValid = true;
    record.targetUnit.assign("test-unit");
    record.targetContractId.assign("test-contract");
    record.targetContractVersion = 3U;
    record.status = AnnotationStatus::Accepted;
    record.provenance = {DataOrigin::Field, EvidenceClass::GT2};
    record.evidenceReference.assign("test-evidence-ref");
    record.annotator.assign("test-annotator");
    record.reviewer.assign("test-reviewer");
    record.confidence = 0.8;
    record.confidenceValid = true;
    record.uncertainty = 0.1;
    record.uncertaintyValid = true;
    record.labelMethodVersion.assign("test-method-v1");
    return record;
}

void testAnnotationManagerKeepsLabelsSeparateAndExportsContractRows() {
    using namespace project::dataset;
    AnnotationRecord records[2]{};
    AnnotationManager manager(records, 2U);
    const auto contract = annotationTargetContract();
    const auto annotation = independentAnnotation();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::Ok),
        static_cast<int>(manager.add(annotation, contract)));
    TEST_ASSERT_EQUAL_UINT64(1U, manager.size());
    TEST_ASSERT_EQUAL_STRING("annotation-1",
                             manager.record(0U)->annotationId.bytes.data());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DataOrigin::Field),
        static_cast<int>(manager.record(0U)->provenance.origin));

    char header[512]{};
    std::size_t headerSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::Ok),
        static_cast<int>(
            manager.writeCsvHeader(header, sizeof(header), headerSize)));
    TEST_ASSERT_TRUE(headerSize > 0U);
    TEST_ASSERT_EQUAL('\n', header[headerSize - 1U]);
    TEST_ASSERT_EQUAL_UINT32(
        static_cast<std::uint32_t>(kAnnotationCsvColumns - 1U),
        static_cast<std::uint32_t>(
            std::count(header, header + headerSize, ',')));

    char row[1024]{};
    std::size_t rowSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::Ok),
        static_cast<int>(manager.writeCsvRow(
            0U, row, sizeof(row), rowSize)));
    TEST_ASSERT_TRUE(std::strstr(row, "ACCEPTED,FIELD,GT2") != nullptr);
    TEST_ASSERT_TRUE(std::strstr(row, "0.25,true") != nullptr);
    TEST_ASSERT_EQUAL('\n', row[rowSize - 1U]);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::InvalidArgument),
        static_cast<int>(manager.writeCsvRow(1U, row, sizeof(row), rowSize)));

    const AnnotationWindowInterval overlap{
        "dataset-1", "site-1", "session-1", 7U, 199U, 250U};
    const AnnotationWindowInterval endBoundary{
        "dataset-1", "site-1", "session-1", 7U, 200U, 250U};
    const AnnotationWindowInterval wrongBoot{
        "dataset-1", "site-1", "session-1", 8U, 150U, 250U};
    TEST_ASSERT_TRUE(AnnotationManager::matchesWindow(annotation, overlap));
    TEST_ASSERT_FALSE(
        AnnotationManager::matchesWindow(annotation, endBoundary));
    TEST_ASSERT_FALSE(AnnotationManager::matchesWindow(annotation, wrongBoot));

    AnnotationWindowInterval timestampOnly{
        "dataset-1", "site-2", "session-1", 7U, 150U, 175U};
    TEST_ASSERT_FALSE(
        AnnotationManager::matchesWindow(annotation, timestampOnly));
}

void testAnnotationManagerRejectsUndefinedTargetAndReportsSyntheticProxy() {
    using namespace project::dataset;
    AnnotationRecord records[3]{};
    AnnotationManager manager(records, 3U);
    auto annotation = independentAnnotation("undefined-target");
    const auto unapproved =
        AnnotationTargetContract{"test-contract", "test-target", "test-unit",
                                 3U, false};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(
            AnnotationManagerStatus::TargetContractNotApproved),
        static_cast<int>(manager.add(annotation, unapproved)));
    TEST_ASSERT_EQUAL_UINT64(0U, manager.size());

    annotation = independentAnnotation("synthetic-proxy");
    annotation.provenance = {DataOrigin::Synthetic, EvidenceClass::GT5};
    annotation.kind = AnnotationKind::SyntheticProxy;
    annotation.evidenceReference.assign("synthetic-formula-version");
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::Ok),
        static_cast<int>(
            manager.add(annotation, annotationTargetContract())));
    TEST_ASSERT_EQUAL_UINT64(1U, manager.report().syntheticProxy);
    TEST_ASSERT_EQUAL_UINT64(1U, manager.report().gt5);

    auto invalidSynthetic = independentAnnotation("bad-synthetic");
    invalidSynthetic.provenance = {DataOrigin::Synthetic, EvidenceClass::GT4};
    invalidSynthetic.kind = AnnotationKind::SyntheticProxy;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::InvalidAnnotation),
        static_cast<int>(manager.add(invalidSynthetic,
                                     annotationTargetContract())));

    auto unlabeled = independentAnnotation("unlabeled");
    unlabeled.status = AnnotationStatus::Unlabeled;
    unlabeled.targetName = {};
    unlabeled.targetValue = 0.0;
    unlabeled.targetValueValid = false;
    unlabeled.targetUnit = {};
    unlabeled.targetContractId = {};
    unlabeled.targetContractVersion = 0U;
    unlabeled.provenance = {DataOrigin::Field, EvidenceClass::NotApplicable};
    unlabeled.evidenceReference = {};
    unlabeled.labelMethodVersion = {};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::Ok),
        static_cast<int>(manager.add(unlabeled, {})));
}

void testAnnotationManagerRejectsDuplicatesBoundsAndNonFiniteValues() {
    using namespace project::dataset;
    AnnotationRecord records[1]{};
    AnnotationManager manager(records, 1U);
    const auto contract = annotationTargetContract();
    auto annotation = independentAnnotation();
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::Ok),
        static_cast<int>(manager.add(annotation, contract)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::DuplicateAnnotation),
        static_cast<int>(manager.add(annotation, contract)));

    auto second = independentAnnotation("annotation-2");
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::CapacityExceeded),
        static_cast<int>(manager.add(second, contract)));

    AnnotationRecord bounded{};
    char oversizedId[65]{};
    std::memset(oversizedId, 'x', sizeof(oversizedId) - 1U);
    TEST_ASSERT_FALSE(bounded.annotationId.assign(
        std::string_view(oversizedId, sizeof(oversizedId) - 1U)));
    auto nonFinite = independentAnnotation("non-finite");
    nonFinite.targetValue = std::numeric_limits<double>::quiet_NaN();
    AnnotationRecord storage2[1]{};
    AnnotationManager nonFiniteManager(storage2, 1U);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(AnnotationManagerStatus::InvalidAnnotation),
        static_cast<int>(nonFiniteManager.add(nonFinite, contract)));
}

void testDatasetSplitterKeepsGroupsTogetherAndExportsAssignments() {
    using namespace project::dataset;
    const DatasetSplitPolicy policy{4U, 77U, 7000U, 1500U, 1500U};
    const DatasetWindowDescriptor windows[] = {
        {"window-1", "dataset", "site", "session-a", "group-a", 1U, 0U, 9U,
         {DataOrigin::Field, EvidenceClass::GT2}},
        {"window-2", "dataset", "site", "session-a", "group-a", 1U, 5U, 14U,
         {DataOrigin::Field, EvidenceClass::GT2}},
        {"window-3", "dataset", "site", "session-a", "group-b", 1U, 15U,
         24U, {DataOrigin::Field, EvidenceClass::GT2}},
        {"window-4", "dataset", "site", "session-b", "group-c", 1U, 0U, 9U,
         {DataOrigin::Synthetic, EvidenceClass::GT5}},
        {"window-5", "dataset", "site", "session-c", "group-d", 1U, 0U, 9U,
         {DataOrigin::Synthetic, EvidenceClass::GT5}}};
    DatasetSplitGroupEntry groups[5]{};
    DatasetSplitWorkspace workspace{groups, 5U, 0U};
    DatasetWindowSplitAssignment assignments[5]{};
    DatasetSplitReport report{};
    DatasetSplitter splitter{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::Ok),
        static_cast<int>(splitter.assign(policy, windows, 5U, workspace,
                                          assignments, 5U, report)));
    TEST_ASSERT_EQUAL_UINT64(5U, report.windowCount);
    TEST_ASSERT_EQUAL_UINT64(4U, report.groupCount);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(assignments[0U].split),
                          static_cast<int>(assignments[1U].split));
    TEST_ASSERT_EQUAL_UINT32(policy.version, assignments[0U].policyVersion);
    TEST_ASSERT_EQUAL_UINT64(policy.seed, assignments[0U].seed);
    std::uint64_t syntheticWindows = 0U;
    for (std::size_t split = 0U; split < 3U; ++split) {
        syntheticWindows += report.windowsBySplitAndOrigin[split]
                                                           [static_cast<std::size_t>(
                                                               DataOrigin::Synthetic)];
    }
    TEST_ASSERT_EQUAL_UINT64(2U, syntheticWindows);

    DatasetSplitGroupEntry repeatGroups[5]{};
    DatasetSplitWorkspace repeatWorkspace{repeatGroups, 5U, 0U};
    DatasetWindowSplitAssignment repeated[5]{};
    DatasetSplitReport repeatedReport{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::Ok),
        static_cast<int>(splitter.assign(policy, windows, 5U, repeatWorkspace,
                                          repeated, 5U, repeatedReport)));
    for (std::size_t i = 0U; i < 5U; ++i) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(assignments[i].split),
                              static_cast<int>(repeated[i].split));
    }

    char header[256]{};
    char row[256]{};
    std::size_t headerSize = 0U;
    std::size_t rowSize = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::Ok),
        static_cast<int>(DatasetSplitter::writeCsvHeader(
            header, sizeof(header), headerSize)));
    TEST_ASSERT_EQUAL_UINT32(
        static_cast<std::uint32_t>(kDatasetSplitCsvColumns - 1U),
        static_cast<std::uint32_t>(
            std::count(header, header + headerSize, ',')));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::Ok),
        static_cast<int>(DatasetSplitter::writeCsvRow(
            assignments[0U], row, sizeof(row), rowSize)));
    TEST_ASSERT_TRUE(std::strstr(row, "group-a,") != nullptr);
    TEST_ASSERT_TRUE(std::strstr(row, ",4,77,") != nullptr);
}

void testDatasetSplitterRejectsLeakageDuplicatesAndCapacityFailures() {
    using namespace project::dataset;
    const DatasetSplitPolicy policy{1U, 12U, 6000U, 2000U, 2000U};
    DatasetSplitGroupEntry groups[4]{};
    DatasetSplitWorkspace workspace{groups, 4U, 0U};
    DatasetWindowSplitAssignment assignments[4]{};
    DatasetSplitReport report{};
    DatasetSplitter splitter{};

    const DatasetWindowDescriptor overlapping[] = {
        {"w1", "d", "s", "session", "group-1", 1U, 0U, 10U,
         {DataOrigin::Field, EvidenceClass::GT1}},
        {"w2", "d", "s", "session", "group-2", 1U, 10U, 20U,
         {DataOrigin::Field, EvidenceClass::GT1}}};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::OverlappingWindowLeakage),
        static_cast<int>(splitter.assign(policy, overlapping, 2U, workspace,
                                          assignments, 4U, report)));

    const DatasetWindowDescriptor duplicate[] = {
        {"same", "d", "s", "session", "group-1", 1U, 0U, 10U,
         {DataOrigin::Field, EvidenceClass::GT1}},
        {"same", "d", "s", "session", "group-1", 1U, 11U, 20U,
         {DataOrigin::Field, EvidenceClass::GT1}}};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::DuplicateWindow),
        static_cast<int>(splitter.assign(policy, duplicate, 2U, workspace,
                                          assignments, 4U, report)));

    const DatasetWindowDescriptor conflictingGroup[] = {
        {"w1", "d", "s", "session-a", "shared-group", 1U, 0U, 10U,
         {DataOrigin::Field, EvidenceClass::GT1}},
        {"w2", "d", "s", "session-b", "shared-group", 1U, 0U, 10U,
         {DataOrigin::Synthetic, EvidenceClass::GT5}}};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::InconsistentGroup),
        static_cast<int>(splitter.assign(policy, conflictingGroup, 2U,
                                          workspace, assignments, 4U,
                                          report)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::AssignmentCapacityExceeded),
        static_cast<int>(splitter.assign(policy, overlapping, 2U, workspace,
                                          assignments, 1U, report)));
    DatasetSplitWorkspace noGroupSpace{nullptr, 0U, 0U};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::GroupCapacityExceeded),
        static_cast<int>(splitter.assign(policy, overlapping, 2U,
                                          noGroupSpace, assignments, 4U,
                                          report)));
    const DatasetSplitPolicy badPolicy{1U, 12U, 5000U, 2500U, 2501U};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::InvalidPolicy),
        static_cast<int>(splitter.assign(badPolicy, overlapping, 2U,
                                          workspace, assignments, 4U,
                                          report)));
}

void testDatasetSplitterReportsSyntheticTestSeparatelyFromFieldEvidence() {
    using namespace project::dataset;
    constexpr std::size_t count = 64U;
    DatasetWindowDescriptor synthetic[count]{};
    DatasetSplitGroupEntry syntheticGroups[count]{};
    DatasetWindowSplitAssignment syntheticAssignments[count]{};
    char syntheticSessions[count][24]{};
    char syntheticWindowIds[count][24]{};
    char syntheticGroupIds[count][24]{};
    for (std::size_t i = 0U; i < count; ++i) {
        std::snprintf(syntheticSessions[i], sizeof(syntheticSessions[i]),
                      "synthetic-session-%zu", i);
        std::snprintf(syntheticWindowIds[i], sizeof(syntheticWindowIds[i]),
                      "synthetic-window-%zu", i);
        std::snprintf(syntheticGroupIds[i], sizeof(syntheticGroupIds[i]),
                      "synthetic-group-%zu", i);
        synthetic[i] = {syntheticWindowIds[i], "dataset", "site",
                        syntheticSessions[i], syntheticGroupIds[i], 1U, 0U,
                        10U, {DataOrigin::Synthetic, EvidenceClass::GT5}};
    }
    const DatasetSplitPolicy testHeavyPolicy{2U, 5U, 1U, 1U, 9998U};
    DatasetSplitWorkspace syntheticWorkspace{syntheticGroups, count, 0U};
    DatasetSplitReport syntheticReport{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DatasetSplitStatus::Ok),
        static_cast<int>(DatasetSplitter{}.assign(
            testHeavyPolicy, synthetic, count, syntheticWorkspace,
            syntheticAssignments, count, syntheticReport)));
    TEST_ASSERT_TRUE(syntheticReport.hasTestGroups);
    TEST_ASSERT_TRUE(syntheticReport.testGroupsSyntheticGt5Only);
    TEST_ASSERT_FALSE(syntheticReport.testContainsFieldGt1ToGt4);

    DatasetWindowDescriptor field{};
    DatasetSplitGroupEntry fieldGroups[1]{};
    DatasetWindowSplitAssignment fieldAssignment[1]{};
    DatasetSplitWorkspace fieldWorkspace{fieldGroups, 1U, 0U};
    DatasetSplitReport fieldReport{};
    bool foundFieldTest = false;
    for (std::size_t i = 0U; i < count && !foundFieldTest; ++i) {
        char session[24]{};
        char window[24]{};
        char group[24]{};
        std::snprintf(session, sizeof(session), "field-session-%zu", i);
        std::snprintf(window, sizeof(window), "field-window-%zu", i);
        std::snprintf(group, sizeof(group), "field-group-%zu", i);
        field = {window, "field-dataset", "field-site", session, group, 1U,
                 0U, 10U, {DataOrigin::Field, EvidenceClass::GT2}};
        fieldWorkspace.groupCount = 0U;
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(DatasetSplitStatus::Ok),
            static_cast<int>(DatasetSplitter{}.assign(
                testHeavyPolicy, &field, 1U, fieldWorkspace, fieldAssignment,
                1U, fieldReport)));
        foundFieldTest =
            fieldAssignment[0U].split == DatasetSplit::Test;
    }
    TEST_ASSERT_TRUE(foundFieldTest);
    TEST_ASSERT_TRUE(fieldReport.testContainsFieldGt1ToGt4);
    TEST_ASSERT_FALSE(fieldReport.testGroupsSyntheticGt5Only);
}

project::dataset::EvaluationConfig evaluationConfig(
    bool targetApproved = true) {
    using namespace project::dataset;
    EvaluationConfig config{};
    config.featureSchemaVersion = 1U;
    config.featureSchemaId = 1U;
    config.featureSchemaHash[0] = 0xA5U;
    config.featureCount = 2U;
    config.featureSchemaHashValid = true;
    config.featureSchemaApproved = true;
    config.targetContract = annotationTargetContract();
    config.targetContract.approved = targetApproved;
    config.regressionMetricsApproved = true;
    return config;
}

project::models::FeatureVector evaluationFeature(float first,
                                                float second) {
    project::models::FeatureVector feature{};
    feature.schemaVersion = 1U;
    feature.schemaId = 1U;
    feature.schemaHashSha256[0] = 0xA5U;
    feature.schemaHashValid = true;
    feature.bootId = 7U;
    feature.relatedSequenceNo = 1U;
    feature.windowStartMs = 100U;
    feature.windowEndMs = 200U;
    feature.windowSampleCount = 1U;
    feature.count = 2U;
    feature.featureValidMask = 3U;
    feature.values[0] = first;
    feature.values[1] = second;
    feature.quality.eligibility =
        project::models::WindowEligibilityState::Eligible;
    feature.quality.rejectionReason =
        project::models::WindowRejectionReason::None;
    feature.quality.channelQuality.fill(
        project::models::DataQualityState::VALID);
    feature.valid = true;
    return feature;
}

project::models::ModelOutput evaluationPrediction(float value) {
    project::models::ModelOutput prediction{};
    prediction.status = InferenceStatus::Ok;
    prediction.outputCount = 1U;
    prediction.rawOutputs[0] = value;
    prediction.predictionValue = value;
    prediction.predictionValid = true;
    return prediction;
}

project::dataset::EvaluationWindow evaluationWindow(
    std::string_view id, const project::models::FeatureVector& features,
    const project::dataset::AnnotationRecord* annotation,
    const project::models::ModelOutput* prediction,
    project::dataset::DataProvenance provenance =
        {project::dataset::DataOrigin::Field,
         project::dataset::EvidenceClass::GT2}) {
    return {id,
            {"dataset-1", "site-1", "session-1", 7U, 100U, 200U},
            provenance,
            project::dataset::DatasetSplit::Test,
            &features,
            annotation,
            prediction};
}

void testEvaluationRunnerReportsFeatureAndApprovedRegressionMetrics() {
    using namespace project::dataset;
    auto featuresA = evaluationFeature(2.0F, 4.0F);
    auto featuresB = evaluationFeature(4.0F, 6.0F);
    auto annotationA = independentAnnotation("eval-label-a");
    auto annotationB = independentAnnotation("eval-label-b");
    annotationA.targetValue = 1.0;
    annotationB.targetValue = 3.0;
    auto predictionA = evaluationPrediction(0.0F);
    auto predictionB = evaluationPrediction(4.0F);
    const EvaluationWindow windows[] = {
        evaluationWindow("eval-window-a", featuresA, &annotationA,
                         &predictionA),
        evaluationWindow("eval-window-b", featuresB, &annotationB,
                         &predictionB)};

    EvaluationReport report{};
    EvaluationRunner runner(evaluationConfig());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(runner.evaluate(windows, 2U, report)));
    const auto& bucket = report.buckets
        [static_cast<std::size_t>(DataOrigin::Field)]
        [static_cast<std::size_t>(EvidenceClass::GT2)]
        [static_cast<std::size_t>(DatasetSplit::Test)];
    const auto& metrics = report.independentMetrics
        [static_cast<std::size_t>(DataOrigin::Field)]
        [static_cast<std::size_t>(EvidenceClass::GT2)]
        [static_cast<std::size_t>(DatasetSplit::Test)];
    TEST_ASSERT_EQUAL_UINT64(2U, bucket.windowCount);
    TEST_ASSERT_EQUAL_UINT64(2U, bucket.featureEligibleCount);
    TEST_ASSERT_EQUAL_UINT64(2U, bucket.eligibleLabelCount);
    TEST_ASSERT_EQUAL_UINT64(2U, metrics.predictionPairCount);
    TEST_ASSERT_TRUE(metrics.targetRangeValid);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001F, 1.0F, static_cast<float>(metrics.targetMinimum));
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001F, 3.0F, static_cast<float>(metrics.targetMaximum));
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001F, 1.0F, static_cast<float>(metrics.meanAbsoluteError));
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001F, 1.0F,
        static_cast<float>(metrics.rootMeanSquaredError));
    TEST_ASSERT_TRUE(metrics.rSquaredValid);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001F, 0.0F, static_cast<float>(metrics.rSquared));
    TEST_ASSERT_EQUAL_UINT64(2U, report.features[0U].finiteCount);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001F, 3.0F, static_cast<float>(report.features[0U].mean));
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001F, 1.0F, static_cast<float>(report.features[0U].variance));

    char header[512]{};
    std::size_t bytesWritten = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(EvaluationRunner::writeSummaryCsvHeader(
            header, sizeof(header), bytesWritten)));
    TEST_ASSERT_EQUAL('\n', header[bytesWritten - 1U]);
    TEST_ASSERT_EQUAL_UINT32(
        20U, static_cast<std::uint32_t>(
                 std::count(header, header + bytesWritten, ',')));
    char summaryRow[1024]{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(EvaluationRunner::writeSummaryCsvRow(
            report, DataOrigin::Field, EvidenceClass::GT2, DatasetSplit::Test,
            summaryRow, sizeof(summaryRow), bytesWritten)));
    TEST_ASSERT_TRUE(std::strstr(summaryRow, "INDEPENDENT,2,2,1,3,1,1,0,true") !=
                     nullptr);
    char featureHeader[256]{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(EvaluationRunner::writeFeatureCsvHeader(
            featureHeader, sizeof(featureHeader), bytesWritten)));
    TEST_ASSERT_EQUAL_UINT32(
        9U, static_cast<std::uint32_t>(
                std::count(featureHeader,
                           featureHeader + bytesWritten, ',')));
    char featureRow[256]{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(EvaluationRunner::writeFeatureCsvRow(
            0U, report.features[0U], featureRow, sizeof(featureRow),
            bytesWritten)));
    TEST_ASSERT_TRUE(std::strstr(featureRow, ",2,0,0,2,2,4,3,1\n") != nullptr);
}

void testEvaluationRunnerDisablesUndefinedTargetsAndSeparatesGt5Proxy() {
    using namespace project::dataset;
    auto features = evaluationFeature(1.0F, 2.0F);
    auto annotation = independentAnnotation("eval-unapproved-label");
    auto prediction = evaluationPrediction(0.5F);
    const auto window = evaluationWindow(
        "eval-window-unapproved", features, &annotation, &prediction);
    EvaluationReport report{};
    EvaluationRunner unapproved(evaluationConfig(false));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(unapproved.evaluate(&window, 1U, report)));
    TEST_ASSERT_FALSE(report.regressionMetricsEnabled);
    TEST_ASSERT_EQUAL_UINT64(0U, report.independentMetrics
                                     [static_cast<std::size_t>(DataOrigin::Field)]
                                     [static_cast<std::size_t>(EvidenceClass::GT2)]
                                     [static_cast<std::size_t>(DatasetSplit::Test)]
                                         .predictionPairCount);
    TEST_ASSERT_EQUAL_UINT64(1U, report.targetExcludedCount);

    annotation.annotationId.assign("eval-synthetic-proxy");
    annotation.provenance = {DataOrigin::Synthetic, EvidenceClass::GT5};
    annotation.kind = AnnotationKind::SyntheticProxy;
    annotation.evidenceReference.assign("test-proxy-formula-v1");
    auto synthetic = evaluationWindow(
        "eval-window-synthetic", features, &annotation, &prediction,
        {DataOrigin::Synthetic, EvidenceClass::GT5});
    EvaluationRunner approved(evaluationConfig());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(approved.evaluate(&synthetic, 1U, report)));
    const auto splitId = static_cast<std::size_t>(DatasetSplit::Test);
    TEST_ASSERT_EQUAL_UINT64(
        1U, report.syntheticProxyMetrics[splitId].eligibleLabelCount);
    TEST_ASSERT_EQUAL_UINT64(
        1U, report.syntheticProxyMetrics[splitId].predictionPairCount);
    TEST_ASSERT_EQUAL_UINT64(
        0U, report.independentMetrics
               [static_cast<std::size_t>(DataOrigin::Synthetic)]
               [static_cast<std::size_t>(EvidenceClass::GT5)]
               [splitId]
                   .predictionPairCount);
    char row[1024]{};
    std::size_t bytesWritten = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(EvaluationRunner::writeSummaryCsvRow(
            report, DataOrigin::Synthetic, EvidenceClass::GT5,
            DatasetSplit::Test, row, sizeof(row), bytesWritten)));
    TEST_ASSERT_TRUE(std::strstr(row, "SYNTHETIC_PROXY,1,1") != nullptr);

    synthetic.prediction = nullptr;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(approved.evaluate(&synthetic, 1U, report)));
    TEST_ASSERT_EQUAL_UINT64(
        1U, report.buckets[static_cast<std::size_t>(DataOrigin::Synthetic)]
                            [static_cast<std::size_t>(EvidenceClass::GT5)]
                            [splitId]
                                .missingPredictionCount);
    TEST_ASSERT_EQUAL_UINT64(
        0U, report.syntheticProxyMetrics[splitId].predictionPairCount);
}

void testEvaluationRunnerCountsMissingRejectedAndJoinFailures() {
    using namespace project::dataset;
    auto validFeatures = evaluationFeature(2.0F, 4.0F);
    auto invalidFeatures = evaluationFeature(
        std::numeric_limits<float>::infinity(), 0.0F);
    invalidFeatures.featureValidMask = 1U;
    invalidFeatures.valid = false;
    invalidFeatures.quality.eligibility =
        project::models::WindowEligibilityState::Rejected;
    invalidFeatures.quality.rejectionReason =
        project::models::WindowRejectionReason::RequiredChannelQuality;
    auto validAnnotation = independentAnnotation("eval-good");
    auto mismatchedAnnotation = independentAnnotation("eval-wrong-boot");
    mismatchedAnnotation.bootId = 8U;
    auto prediction = evaluationPrediction(1.0F);
    const EvaluationWindow windows[] = {
        evaluationWindow("eval-good-window", validFeatures, &validAnnotation,
                         &prediction),
        evaluationWindow("eval-rejected-window", invalidFeatures,
                         &validAnnotation, &prediction),
        evaluationWindow("eval-join-window", validFeatures,
                         &mismatchedAnnotation, &prediction)};
    EvaluationConfig config = evaluationConfig();
    config.upstreamRejectedCount = 2U;
    EvaluationReport report{};
    EvaluationRunner runner(config);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::Ok),
        static_cast<int>(runner.evaluate(windows, 3U, report)));
    TEST_ASSERT_EQUAL_UINT64(3U, report.totalWindowCount);
    TEST_ASSERT_EQUAL_UINT64(3U, report.rejectedCount);
    TEST_ASSERT_EQUAL_UINT64(0U, report.schemaMismatchCount);
    TEST_ASSERT_EQUAL_UINT64(1U, report.targetJoinMismatchCount);
    TEST_ASSERT_EQUAL_UINT64(
        1U, report.qualityStateCounts[static_cast<std::size_t>(
                project::models::WindowEligibilityState::Rejected)]);
    TEST_ASSERT_EQUAL_UINT64(
        1U, report.rejectionReasonCounts[static_cast<std::size_t>(
                project::models::WindowRejectionReason::RequiredChannelQuality)]);
    TEST_ASSERT_EQUAL_UINT64(1U, report.features[0U].missingCount);
    TEST_ASSERT_EQUAL_UINT64(1U, report.features[0U].nonFiniteCount);
    TEST_ASSERT_EQUAL_UINT64(1U, report.features[1U].missingCount);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::CapacityExceeded),
        static_cast<int>(runner.evaluate(
            windows, kMaximumEvaluationWindows + 1U, report)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::InvalidConfiguration),
        static_cast<int>(EvaluationRunner(EvaluationConfig{}).evaluate(
            windows, 1U, report)));
    char tiny[1]{};
    std::size_t bytesWritten = 0U;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::CsvWriteError),
        static_cast<int>(EvaluationRunner::writeFeatureCsvHeader(
            tiny, sizeof(tiny), bytesWritten)));

    auto overflowLabel = independentAnnotation("eval-overflow");
    overflowLabel.targetValue = 1.0e308;
    auto overflowPrediction =
        evaluationPrediction(-std::numeric_limits<float>::max());
    const auto overflowWindow = evaluationWindow(
        "eval-overflow-window", validFeatures, &overflowLabel,
        &overflowPrediction);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EvaluationStatus::NumericOverflow),
        static_cast<int>(runner.evaluate(&overflowWindow, 1U, report)));
    TEST_ASSERT_EQUAL_UINT64(0U, report.totalWindowCount);
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
    RUN_TEST(testRiskManagerRequiresApprovedScoreSemanticsAndConsistentOutput);
    RUN_TEST(testRiskManagerHealthFaultBlocksModelRisk);
    RUN_TEST(testRiskManagerRejectsMismatchedModelProvenance);
    RUN_TEST(testRiskManagerFusesApprovedTiltWithModelScore);
    RUN_TEST(testOutputPolicyMapsEveryRiskStateWithoutHardwareSemantics);
    RUN_TEST(testOutputPolicyRejectsInvalidAssessmentsAndRiskEnums);
    RUN_TEST(testOutputPolicyPreservesAbsentAndUnverifiableFeedback);
    RUN_TEST(testOutputPolicyRepeatedInputProducesIdenticalCommand);
    RUN_TEST(testRecoveryManagerClassifiesKnownAndUnknownCatalogValues);
    RUN_TEST(testRecoveryManagerBoundsRetryByInjectedPolicy);
    RUN_TEST(testRecoveryManagerInhibitsInferenceForSchemaAndModelMismatch);
    RUN_TEST(testEventRecordJsonLinesRoundTripsEveryMember);
    RUN_TEST(testEventRecordJsonLinesRejectsInvalidArgumentsAndSmallBuffers);
    RUN_TEST(testEventRecordJsonLinesRejectsTruncationAndTrailingBytes);
    RUN_TEST(testEventRecordJsonLinesRejectsUnsupportedTypeVersionAndEnums);
    RUN_TEST(testEventRecordJsonLinesPreservesInvalidNonFiniteAsNull);
    RUN_TEST(testEventRecordJsonLinesRejectsInvalidModelValues);
    RUN_TEST(testEventRecordJsonLinesRoundTripsEnumAndStringBoundaries);
    RUN_TEST(testEventRecordJsonLinesKeepsFormatsAndOtherRecordsSeparate);
    RUN_TEST(testStorageManagerBeginLifecycleAndRepeatedBegin);
    RUN_TEST(testStorageManagerBeginFailureCanBeRetried);
    RUN_TEST(testStorageManagerAppendValidatesAndBorrowsCompleteRecord);
    RUN_TEST(testStorageManagerPartialWriteFaultsUntilSuccessfulBegin);
    RUN_TEST(testStorageManagerFlushFailureAndRecovery);
    RUN_TEST(testStorageManagerAppendBackendFullAndZeroLimit);
    RUN_TEST(testStorageManagerShutdownIsLogicalAndTerminal);
    RUN_TEST(testStorageManagerBackendFailurePropagatesIoError);
    RUN_TEST(testReplayEngineReplaysApprovedCsvWindow);
    RUN_TEST(testReplayEngineRejectsBadHeaderAndManifest);
    RUN_TEST(testReplayEngineRejectsMalformedRow);
    RUN_TEST(testReplayMalformedRowDoesNotAdvanceTimestampBaseline);
    RUN_TEST(testConfigManagerAcceptsValidTestOnlyManifestWithoutArtifacts);
    RUN_TEST(testConfigManagerReportsMissingManifestField);
    RUN_TEST(testConfigManagerRejectsUnsupportedManifestVersion);
    RUN_TEST(testConfigManagerRejectsInvalidEnum);
    RUN_TEST(testConfigManagerRejectsNonFiniteValues);
    RUN_TEST(testConfigManagerRejectsOutOfRangeValues);
    RUN_TEST(testConfigManagerRejectsModelFssMismatch);
    RUN_TEST(testConfigManagerValidatesHashesOnlyForSuppliedArtifacts);
    RUN_TEST(testConfigManagerKeepsProductionActivationFailClosed);
    RUN_TEST(testConfigManagerCandidateFaultsPreservePreviousCommittedRecord);
    RUN_TEST(testConfigManagerRejectsInvalidCandidateWithoutWriting);
    RUN_TEST(testConfigManagerCommitsOnlyAfterValidReadBack);
    RUN_TEST(testConfigManagerFallsBackWhenNewestRecordIsCorrupt);
    RUN_TEST(testAppControllerStartsPhasesInOrderAndGatesInference);
    RUN_TEST(testAppControllerCleansUpEveryStartupFailureInReverse);
    RUN_TEST(testAppControllerFailsClosedWhenInferenceGateCannotOpen);
    RUN_TEST(testAppControllerShutdownOrderAndRepeatedCalls);
    RUN_TEST(testAppControllerFlushFailureStillMakesOutputsSafe);
    RUN_TEST(testAppControllerHealthChecksAreBoundedAndFailClosed);
    RUN_TEST(testAppControllerRejectsUnconfiguredTimeouts);
    RUN_TEST(testCalibrationServicePreservesRawInputAndRemainsUncalibrated);
    RUN_TEST(testCalibrationServicePropagatesInvalidAndPartialChannels);
    RUN_TEST(testCalibrationServiceRejectsNonFiniteAndDoesNotMakeValidZero);
    RUN_TEST(testCalibrationServiceRejectsUnmatchedCalibrationVersion);
    RUN_TEST(testCalibrationServiceRejectsMissingFrameSchemaVersion);
    RUN_TEST(testSchemaRegistryMatchesTestOnlyStructureAndVector);
    RUN_TEST(testSchemaRegistryRejectsCapacityOverflowAndMissingMetadata);
    RUN_TEST(testSchemaRegistryReportsVersionHashAndCountMismatches);
    RUN_TEST(testSchemaRegistryReportsOrderUnitAndPreprocessingMismatches);
    RUN_TEST(testSchemaRegistryReportsWindowStrideAndSampleRateMismatches);
    RUN_TEST(testSchemaRegistryRejectsTestIdAsProductionDescriptor);
    RUN_TEST(testSchemaRegistryRejectsInvalidVectorContents);
    RUN_TEST(testDatasetProvenanceRequiresSyntheticGt5);
    RUN_TEST(testCsvRowWriterEscapesFieldsAndValidatesUtf8);
    RUN_TEST(testCsvRowWriterFormatsFiniteNumbersAndInvalidAsEmpty);
    RUN_TEST(testCsvRowWriterRejectsNonFiniteAndColumnCountErrors);
    RUN_TEST(testCsvRowWriterHandlesExactCapacityAndOverflow);
    RUN_TEST(testDsdV12RawHeaderMatchesCoreSchemaAndFitsBound);
    RUN_TEST(testSyntheticGeneratorIsDeterministicAndCarriesGt5);
    RUN_TEST(testSyntheticGeneratorCoversScenarioBoundaries);
    RUN_TEST(testSyntheticGeneratorRejectsInvalidAndOverflowingConfig);
    RUN_TEST(testSha256MatchesKnownVectorAndBoundaryBlocks);
    RUN_TEST(testDsdRawDatasetValidatorAcceptsExactSchemaAndManifest);
    RUN_TEST(testDsdRawDatasetValidatorQuarantinesDuplicatesAndSequenceGaps);
    RUN_TEST(testDsdRawDatasetValidatorRejectsEncodingQuoteAndTruncation);
    RUN_TEST(testDsdRawDatasetValidatorReportsManifestAndInjectedFailures);
    RUN_TEST(testDsdRawDatasetValidatorRejectsBadFieldsAndTimeOrder);
    RUN_TEST(testDsdRawDatasetValidatorReservesIdentityAndFailsClosed);
    RUN_TEST(testDsdRawDatasetValidatorChecksHeaderAndRecordCapacity);
    RUN_TEST(testDataQualityClassifiesCalibrationInvalidAndContinuity);
    RUN_TEST(testFeaturePipelineRejectsInvalidQualityAndPropagatesWindowSummary);
    RUN_TEST(testFeaturePipelinePinsInputSchemaAndRejectsChanges);
    RUN_TEST(testFeaturePipelineResetsAtBootCalibrationAndSensorBoundaries);
    RUN_TEST(testFeaturePipelineGoldenSyntheticTiltFixture);
    RUN_TEST(testAnnotationManagerKeepsLabelsSeparateAndExportsContractRows);
    RUN_TEST(testAnnotationManagerRejectsUndefinedTargetAndReportsSyntheticProxy);
    RUN_TEST(testAnnotationManagerRejectsDuplicatesBoundsAndNonFiniteValues);
    RUN_TEST(testDatasetSplitterKeepsGroupsTogetherAndExportsAssignments);
    RUN_TEST(testDatasetSplitterRejectsLeakageDuplicatesAndCapacityFailures);
    RUN_TEST(testDatasetSplitterReportsSyntheticTestSeparatelyFromFieldEvidence);
    RUN_TEST(testEvaluationRunnerReportsFeatureAndApprovedRegressionMetrics);
    RUN_TEST(testEvaluationRunnerDisablesUndefinedTargetsAndSeparatesGt5Proxy);
    RUN_TEST(testEvaluationRunnerCountsMissingRejectedAndJoinFailures);
    return UNITY_END();
}
