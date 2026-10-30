#include "project/Managers/ConfigManager.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "AI/EdgeImpulseAdapter.h"
#include "Managers/RiskManager.h"
#include "Managers/SensorManager.h"
#include "Models/DataContractTypes.h"
#include "Models/FeatureVector.h"
#include "Models/ModelOutput.h"
#include "Models/SensorFrame.h"
#include "Services/DataQualityService.h"
#include "Services/FeaturePipeline.h"

namespace project::managers {
namespace {

constexpr std::size_t kSha256Size = 32U;

constexpr std::uint32_t kSha256RoundConstants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

std::uint32_t rotateRight(std::uint32_t value,
                          std::uint8_t amount) noexcept {
    return (value >> amount) | (value << (32U - amount));
}

void processSha256Block(const std::uint8_t* block,
                        std::uint32_t (&state)[8]) noexcept {
    std::uint32_t words[64]{};
    for (std::size_t i = 0U; i < 16U; ++i) {
        const std::size_t offset = i * 4U;
        words[i] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                   (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                   static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t i = 16U; i < 64U; ++i) {
        const std::uint32_t s0 =
            rotateRight(words[i - 15U], 7U) ^
            rotateRight(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
        const std::uint32_t s1 =
            rotateRight(words[i - 2U], 17U) ^
            rotateRight(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
        words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (std::size_t i = 0U; i < 64U; ++i) {
        const std::uint32_t sum1 =
            rotateRight(e, 6U) ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temp1 =
            h + sum1 + choice + kSha256RoundConstants[i] + words[i];
        const std::uint32_t sum0 =
            rotateRight(a, 2U) ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

bool calculateSha256(const ManifestArtifactView& artifact,
                     std::array<std::uint8_t, kSha256Size>& digest) noexcept {
    if (artifact.data == nullptr || artifact.size == 0U ||
        artifact.size > std::numeric_limits<std::uint64_t>::max() / 8U) {
        return false;
    }

    std::uint32_t state[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

    std::size_t offset = 0U;
    while (artifact.size - offset >= 64U) {
        processSha256Block(artifact.data + offset, state);
        offset += 64U;
    }

    std::uint8_t tail[128]{};
    const std::size_t remaining = artifact.size - offset;
    for (std::size_t i = 0U; i < remaining; ++i) {
        tail[i] = artifact.data[offset + i];
    }
    tail[remaining] = 0x80U;
    const std::size_t paddedSize = remaining < 56U ? 64U : 128U;
    const std::uint64_t bitLength =
        static_cast<std::uint64_t>(artifact.size) * 8U;
    for (std::size_t i = 0U; i < 8U; ++i) {
        tail[paddedSize - 1U - i] =
            static_cast<std::uint8_t>(bitLength >> (i * 8U));
    }
    processSha256Block(tail, state);
    if (paddedSize == 128U) {
        processSha256Block(tail + 64U, state);
    }

    for (std::size_t i = 0U; i < 8U; ++i) {
        digest[i * 4U] = static_cast<std::uint8_t>(state[i] >> 24U);
        digest[i * 4U + 1U] =
            static_cast<std::uint8_t>(state[i] >> 16U);
        digest[i * 4U + 2U] =
            static_cast<std::uint8_t>(state[i] >> 8U);
        digest[i * 4U + 3U] = static_cast<std::uint8_t>(state[i]);
    }
    return true;
}

void addIssue(ValidationReport& report, ManifestErrorCode code,
              const char* fieldPath) noexcept {
    if (report.issueCount >= report.issues.size()) {
        report.truncated = true;
        return;
    }
    report.issues[report.issueCount++] = {code, fieldPath};
}

bool hasNonZeroHash(const std::array<std::uint8_t, kSha256Size>& hash) noexcept {
    for (const std::uint8_t byte : hash) {
        if (byte != 0U) {
            return true;
        }
    }
    return false;
}

template <std::size_t N>
bool hasNonZeroHash(const std::array<std::uint8_t, N>& hash) noexcept {
    for (const std::uint8_t byte : hash) {
        if (byte != 0U) {
            return true;
        }
    }
    return false;
}

bool sameHash(const std::array<std::uint8_t, kSha256Size>& left,
              const std::array<std::uint8_t, kSha256Size>& right) noexcept {
    return std::memcmp(left.data(), right.data(), left.size()) == 0;
}

bool isKnownFeature(project::services::FssFeatureId id) noexcept {
    using project::services::FssFeatureId;
    switch (id) {
        case FssFeatureId::AccelXRaw:
        case FssFeatureId::AccelYRaw:
        case FssFeatureId::AccelZRaw:
        case FssFeatureId::AccelMagnitude:
        case FssFeatureId::TiltXDeg:
        case FssFeatureId::TiltYDeg:
        case FssFeatureId::SoilRaw:
        case FssFeatureId::Yl83Raw:
        case FssFeatureId::Hx710bRaw:
            return true;
    }
    return false;
}

bool isProcessedChannel(project::models::SensorChannel channel) noexcept {
    using project::models::SensorChannel;
    switch (channel) {
        case SensorChannel::AccelXG:
        case SensorChannel::AccelYG:
        case SensorChannel::AccelZG:
        case SensorChannel::GyroXDps:
        case SensorChannel::GyroYDps:
        case SensorChannel::GyroZDps:
        case SensorChannel::SoilMoisturePct:
        case SensorChannel::RainWetnessRelativePct:
            return true;
        case SensorChannel::ImuAccelXRaw:
        case SensorChannel::ImuAccelYRaw:
        case SensorChannel::ImuAccelZRaw:
        case SensorChannel::ImuGyroXRaw:
        case SensorChannel::ImuGyroYRaw:
        case SensorChannel::ImuGyroZRaw:
        case SensorChannel::TiltAngleDeg:
        case SensorChannel::SoilMoistureRaw:
        case SensorChannel::RainRaw:
        case SensorChannel::RainRateMmH:
        case SensorChannel::Hx710bRaw:
        case SensorChannel::Hx710bCalibratedValue:
        case SensorChannel::Count:
            return false;
    }
    return false;
}

bool artifactIsAbsent(const ManifestArtifactView& artifact) noexcept {
    return artifact.data == nullptr && artifact.size == 0U;
}

bool validateArtifact(const ManifestArtifactView& artifact,
                      const std::array<std::uint8_t, kSha256Size>& expected,
                      const char* fieldPath,
                      ValidationReport& report) noexcept {
    if (artifactIsAbsent(artifact)) {
        return true;
    }
    if (artifact.data == nullptr || artifact.size == 0U ||
        artifact.size >
            std::numeric_limits<std::uint64_t>::max() / 8U) {
        addIssue(report, ManifestErrorCode::InvalidArtifact, fieldPath);
        return false;
    }
    if (!hasNonZeroHash(expected)) {
        addIssue(report, ManifestErrorCode::MissingField, fieldPath);
        return false;
    }

    std::array<std::uint8_t, kSha256Size> actual{};
    if (!calculateSha256(artifact, actual)) {
        addIssue(report, ManifestErrorCode::InvalidArtifact, fieldPath);
        return false;
    }
    if (!sameHash(actual, expected)) {
        addIssue(report, ManifestErrorCode::ArtifactHashMismatch, fieldPath);
        return false;
    }
    return true;
}

void validateQualityConfig(const project::services::DataQualityConfig& config,
                           ValidationReport& report) noexcept {
    if (!config.requiredChannelsConfigured) {
        addIssue(report, ManifestErrorCode::MissingField,
                 "dataQuality.requiredChannels");
        return;
    }

    bool hasRequiredChannel = false;
    for (std::size_t i = 0U; i < config.requiredChannels.size(); ++i) {
        if (!config.requiredChannels[i]) {
            continue;
        }
        hasRequiredChannel = true;
        if (!isProcessedChannel(
                static_cast<project::models::SensorChannel>(i))) {
            addIssue(report, ManifestErrorCode::OutOfRange,
                     "dataQuality.requiredChannels");
        }
    }
    if (!hasRequiredChannel) {
        addIssue(report, ManifestErrorCode::MissingField,
                 "dataQuality.requiredChannels");
    }

    for (std::size_t i = 0U; i < config.ranges.size(); ++i) {
        const project::services::QualityRange& range = config.ranges[i];
        if (!range.configured) {
            continue;
        }
        if (!std::isfinite(range.minimum) ||
            !std::isfinite(range.maximum)) {
            addIssue(report, ManifestErrorCode::NonFinite,
                     "dataQuality.ranges");
        } else if (range.minimum > range.maximum) {
            addIssue(report, ManifestErrorCode::OutOfRange,
                     "dataQuality.ranges");
        }
    }

    for (std::size_t i = 0U; i < config.freshness.size(); ++i) {
        if (!config.freshness[i].configured) {
            addIssue(report, ManifestErrorCode::MissingField,
                     "dataQuality.freshness");
        }
    }
}

void validateSensorManagerConfig(
    const project::managers::SensorManagerConfig& config,
    ValidationReport& report) noexcept {
    if (config.samplePeriodMicros == 0U) {
        addIssue(report, ManifestErrorCode::MissingField,
                 "sensorManager.samplePeriodMicros");
    }
    for (const project::managers::SensorReadPolicy& policy :
         config.readPolicies) {
        if (policy.beginTimeoutMicros == 0U ||
            policy.timeoutMicros == 0U) {
            addIssue(report, ManifestErrorCode::MissingField,
                     "sensorManager.readPolicies.timeout");
        }
    }
}

}  // namespace

bool ConfigManager::loadLastValid() {
    return false;
}

bool ConfigManager::validateManifest() {
    return false;
}

bool ConfigManager::activateAtomically() {
    return false;
}

project::models::ApiStatus ConfigManager::validateManifest(
    const ConfigManifest& manifest, ValidationReport& report) noexcept {
    report = {};
    bool unsupportedVersion = false;

    if (manifest.formatVersion == 0U) {
        addIssue(report, ManifestErrorCode::MissingField,
                 "manifest.formatVersion");
    } else if (manifest.formatVersion != kSupportedManifestVersion) {
        addIssue(report, ManifestErrorCode::UnsupportedVersion,
                 "manifest.formatVersion");
        unsupportedVersion = true;
    }

    bool production = false;
    switch (manifest.purpose) {
        case ManifestPurpose::TestOnly:
            break;
        case ManifestPurpose::Production:
            production = true;
            break;
        case ManifestPurpose::Unspecified:
            addIssue(report, ManifestErrorCode::MissingField,
                     "manifest.purpose");
            break;
        default:
            addIssue(report, ManifestErrorCode::InvalidEnum,
                     "manifest.purpose");
            break;
    }

    if (manifest.featurePipeline == nullptr) {
        addIssue(report, ManifestErrorCode::MissingField,
                 "featurePipeline");
    }
    if (manifest.model == nullptr) {
        addIssue(report, ManifestErrorCode::MissingField, "model");
    }
    if (manifest.risk == nullptr) {
        addIssue(report, ManifestErrorCode::MissingField, "risk");
    }

    if (production) {
        if (!manifest.productionApprovalComplete) {
            addIssue(report, ManifestErrorCode::ApprovalRequired,
                     "manifest.productionApprovalComplete");
        }
        if (manifest.dataQuality == nullptr) {
            addIssue(report, ManifestErrorCode::MissingField, "dataQuality");
        }
        if (manifest.sensorManager == nullptr) {
            addIssue(report, ManifestErrorCode::MissingField,
                     "sensorManager");
        }
        if (artifactIsAbsent(manifest.featureSchemaArtifact)) {
            addIssue(report, ManifestErrorCode::MissingField,
                     "featureSchemaArtifact");
        }
        if (artifactIsAbsent(manifest.modelArtifact)) {
            addIssue(report, ManifestErrorCode::MissingField, "modelArtifact");
        }
        if (artifactIsAbsent(manifest.riskConfigArtifact)) {
            addIssue(report, ManifestErrorCode::MissingField,
                     "riskConfigArtifact");
        }
    }

    if (manifest.dataQuality != nullptr) {
        validateQualityConfig(*manifest.dataQuality, report);
    }
    if (manifest.sensorManager != nullptr) {
        validateSensorManagerConfig(*manifest.sensorManager, report);
    }

    if (manifest.featurePipeline != nullptr) {
        const project::services::FeaturePipelineConfig& feature =
            *manifest.featurePipeline;
        if (!std::isfinite(feature.sampleRateHz) ||
            !std::isfinite(feature.accelerationToMps2)) {
            addIssue(report, ManifestErrorCode::NonFinite,
                     "featurePipeline.numeric");
        }
        if (feature.featureCount >
                project::models::FeatureVector::kMaxFeatures ||
            feature.windowLengthSamples >
                project::services::FeaturePipeline::kMaxWindowSamples) {
            addIssue(report, ManifestErrorCode::OutOfRange,
                     "featurePipeline.dimensions");
        }
        const std::size_t configuredFeatureCount =
            feature.featureCount <=
                    project::models::FeatureVector::kMaxFeatures
                ? feature.featureCount
                : 0U;
        bool invalidFeatureEnum = false;
        for (std::size_t i = 0U; i < configuredFeatureCount; ++i) {
            if (!isKnownFeature(feature.features[i].id)) {
                invalidFeatureEnum = true;
                break;
            }
        }
        if (invalidFeatureEnum) {
            addIssue(report, ManifestErrorCode::InvalidEnum,
                     "featurePipeline.features.id");
        }
        const project::services::FeaturePipelineStatus featureStatus =
            project::services::FeaturePipeline::validateConfig(feature);
        if (featureStatus != project::services::FeaturePipelineStatus::Ok) {
            const ManifestErrorCode code =
                featureStatus ==
                        project::services::FeaturePipelineStatus::UnsupportedFeature
                    ? ManifestErrorCode::InvalidEnum
                    : ManifestErrorCode::OutOfRange;
            addIssue(report, code, "featurePipeline");
        }
    }

    if (manifest.model != nullptr) {
        const project::ai::EdgeImpulseModelConfig& model = *manifest.model;
        switch (model.outputKind) {
            case project::ai::EdgeImpulseOutputKind::Classification:
            case project::ai::EdgeImpulseOutputKind::Regression:
                break;
            default:
                addIssue(report, ManifestErrorCode::InvalidEnum,
                         "model.outputKind");
                break;
        }
        if (model.featureSchemaVersion == 0U ||
            model.featureSchemaId == 0U || model.inputDimension == 0U ||
            model.outputCount == 0U) {
            addIssue(report, ManifestErrorCode::MissingField, "model");
        }
        if (model.inputDimension >
                project::models::FeatureVector::kMaxFeatures ||
            model.outputCount > project::models::ModelOutput::kMaxOutputs) {
            addIssue(report, ManifestErrorCode::OutOfRange,
                     "model.dimensions");
        }
        if (manifest.featurePipeline != nullptr) {
            const project::services::FeaturePipelineConfig& feature =
                *manifest.featurePipeline;
            if (feature.schemaVersion != model.featureSchemaVersion ||
                feature.schemaId != model.featureSchemaId ||
                !sameHash(feature.schemaHash, model.featureSchemaHash)) {
                addIssue(report, ManifestErrorCode::CrossFieldMismatch,
                         "model.featureSchema");
            }
            if (feature.featureCount > 0U &&
                feature.featureCount <=
                    project::models::FeatureVector::kMaxFeatures &&
                feature.windowLengthSamples > 0U &&
                static_cast<std::size_t>(feature.featureCount) *
                        feature.windowLengthSamples !=
                    model.inputDimension) {
                addIssue(report, ManifestErrorCode::CrossFieldMismatch,
                         "model.inputDimension");
            }
        }
    }

    if (manifest.risk != nullptr) {
        const project::managers::RiskManagerConfig& risk = *manifest.risk;
        if (!std::isfinite(risk.scoreMinimum) ||
            !std::isfinite(risk.scoreMaximum) ||
            !std::isfinite(risk.scoreSafeBoundary) ||
            !std::isfinite(risk.scoreMonitorBoundary) ||
            !std::isfinite(risk.tiltSafeMaximumDeg) ||
            !std::isfinite(risk.tiltMonitorMaximumDeg)) {
            addIssue(report, ManifestErrorCode::NonFinite, "risk.numeric");
        }
        switch (risk.scoreDirection) {
            case project::managers::RiskScoreDirection::HigherIsMoreSevere:
            case project::managers::RiskScoreDirection::LowerIsMoreSevere:
                break;
            default:
                addIssue(report, ManifestErrorCode::InvalidEnum,
                         "risk.scoreDirection");
                break;
        }
        if (manifest.model != nullptr && risk.modelRuleEnabled) {
            if (risk.expectedModelOutputCount !=
                    manifest.model->outputCount ||
                manifest.model->outputKind !=
                    project::ai::EdgeImpulseOutputKind::Regression ||
                manifest.model->outputCount != 1U) {
                addIssue(report, ManifestErrorCode::CrossFieldMismatch,
                         "risk.expectedModelOutputCount");
            }
        }
        if (!project::managers::RiskManager::validateConfig(risk)) {
            addIssue(report, ManifestErrorCode::OutOfRange, "risk");
        }
    }

    if (production && manifest.model != nullptr &&
        (!manifest.model->featureSchemaApproved ||
         !manifest.model->modelMetadataVerified ||
         !manifest.model->modelHashVerified ||
         !manifest.model->outputContractApproved)) {
        addIssue(report, ManifestErrorCode::ApprovalRequired,
                 "model.approval");
    }
    if (production && manifest.featurePipeline != nullptr &&
        (!manifest.featurePipeline->schemaApproved ||
         !manifest.featurePipeline->modelMetadataVerified ||
         !manifest.featurePipeline->flatLayoutApproved ||
         !manifest.featurePipeline->preprocessingApproved)) {
        addIssue(report, ManifestErrorCode::ApprovalRequired,
                 "featurePipeline.approval");
    }
    if (production && manifest.risk != nullptr &&
        (!manifest.risk->approved || !manifest.risk->hashVerified)) {
        addIssue(report, ManifestErrorCode::ApprovalRequired,
                 "risk.approval");
    }

    if (manifest.featurePipeline != nullptr && manifest.model != nullptr) {
        if (!artifactIsAbsent(manifest.featureSchemaArtifact)) {
            std::array<std::uint8_t, kSha256Size> digest{};
            if (!calculateSha256(manifest.featureSchemaArtifact, digest)) {
                addIssue(report, ManifestErrorCode::InvalidArtifact,
                         "featureSchemaArtifact");
            } else if (!sameHash(digest,
                                 manifest.featurePipeline->schemaHash) ||
                       !sameHash(digest,
                                 manifest.model->featureSchemaHash)) {
                addIssue(report, ManifestErrorCode::ArtifactHashMismatch,
                         "featureSchemaArtifact");
            }
        }
    } else if (!artifactIsAbsent(manifest.featureSchemaArtifact)) {
        addIssue(report, ManifestErrorCode::MissingField,
                 "featureSchemaArtifact.expectedSchema");
    }

    if (manifest.model != nullptr) {
        validateArtifact(manifest.modelArtifact, manifest.model->modelHash,
                         "modelArtifact", report);
    } else if (!artifactIsAbsent(manifest.modelArtifact)) {
        addIssue(report, ManifestErrorCode::MissingField,
                 "modelArtifact.expectedHash");
    }
    if (manifest.risk != nullptr) {
        validateArtifact(manifest.riskConfigArtifact,
                         manifest.risk->hashSha256, "riskConfigArtifact",
                         report);
    } else if (!artifactIsAbsent(manifest.riskConfigArtifact)) {
        addIssue(report, ManifestErrorCode::MissingField,
                 "riskConfigArtifact.expectedHash");
    }

    if (report.valid()) {
        return project::models::ApiStatus::Ok;
    }
    return unsupportedVersion ? project::models::ApiStatus::VersionMismatch
                              : project::models::ApiStatus::DataInvalid;
}

}  // namespace project::managers
