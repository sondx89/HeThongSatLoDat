#include "project/AI/SchemaRegistry.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace project::ai {
namespace {

using project::models::ApiStatus;
using project::models::FeatureVector;
using project::services::FeaturePipelineConfig;
using project::services::FssFeatureId;

bool hasNonZeroHash(const std::uint8_t* hash, std::size_t size) noexcept {
    for (std::size_t i = 0U; i < size; ++i) {
        if (hash[i] != 0U) {
            return true;
        }
    }
    return false;
}

bool validFeatureId(FssFeatureId id) noexcept {
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

bool validUnit(FeatureUnit unit) noexcept {
    switch (unit) {
        case FeatureUnit::RawCounts:
        case FeatureUnit::MetersPerSecondSquared:
        case FeatureUnit::Degrees:
        case FeatureUnit::Percent:
        case FeatureUnit::MillimetersPerHour:
        case FeatureUnit::AdcCounts:
            return true;
        case FeatureUnit::Unspecified:
            return false;
    }
    return false;
}

ApiStatus fail(SchemaCompatibilityReport& report, ApiStatus status,
               SchemaMismatchReason reason, SchemaField field,
               std::size_t featureIndex =
                   SchemaCompatibilityReport::kNoFeature) noexcept {
    report.status = status;
    report.reason = reason;
    report.field = field;
    report.featureIndex = featureIndex;
    report.structurallyCompatible = false;
    report.productionCompatible = false;
    return status;
}

bool approvalComplete(const SchemaRegistryConfig& config) noexcept {
    if (config.kind != SchemaDescriptorKind::Released ||
        config.schema == nullptr || config.model == nullptr ||
        config.modelInput == nullptr) {
        return false;
    }
    const FeaturePipelineConfig& schema = *config.schema;
    const EdgeImpulseModelConfig& model = *config.model;
    const ModelInputMetadata& modelInput = *config.modelInput;
    if (!schema.schemaApproved || !schema.modelMetadataVerified ||
        !schema.flatLayoutApproved || !schema.preprocessingApproved ||
        !schema.sampleRateConfigured ||
        !schema.samplePeriodToleranceConfigured ||
        schema.schemaId == SchemaRegistry::kTestOnlySchemaId ||
        !modelInput.verified || !model.featureSchemaApproved ||
        !model.modelMetadataVerified || !model.modelHashVerified ||
        !model.outputContractApproved ||
        !hasNonZeroHash(model.modelHash.data(), model.modelHash.size())) {
        return false;
    }
    for (std::size_t i = 0U; i < schema.featureCount; ++i) {
        if (!schema.features[i].modelInputEligible) {
            return false;
        }
    }
    return project::services::FeaturePipeline::validateConfig(schema) ==
           project::services::FeaturePipelineStatus::Ok;
}

}  // namespace

SchemaRegistry::SchemaRegistry(const SchemaRegistryConfig& config) noexcept
    : config_(config) {}

ApiStatus SchemaRegistry::compareDetailed(
    SchemaCompatibilityReport& report) const noexcept {
    report = {};
    if (config_.schema == nullptr || config_.model == nullptr ||
        config_.modelInput == nullptr) {
        return fail(report, ApiStatus::NotInitialized,
                    SchemaMismatchReason::MissingDescriptor,
                    SchemaField::Descriptor);
    }

    const FeaturePipelineConfig& schema = *config_.schema;
    const EdgeImpulseModelConfig& model = *config_.model;
    const ModelInputMetadata& modelInput = *config_.modelInput;

    if (config_.kind == SchemaDescriptorKind::Unspecified) {
        return fail(report, ApiStatus::NotInitialized,
                    SchemaMismatchReason::MissingMetadata,
                    SchemaField::DescriptorKind);
    }
    if (!modelInput.verified) {
        return fail(report, ApiStatus::NotInitialized,
                    SchemaMismatchReason::MissingMetadata,
                    SchemaField::Descriptor);
    }
    if (schema.schemaVersion == 0U || schema.schemaId == 0U ||
        !schema.schemaHashValid ||
        !hasNonZeroHash(schema.schemaHash.data(), schema.schemaHash.size())) {
        return fail(report, ApiStatus::NotInitialized,
                    SchemaMismatchReason::MissingMetadata,
                    SchemaField::SchemaHash);
    }
    if (model.featureSchemaVersion == 0U ||
        model.featureSchemaId == 0U ||
        !hasNonZeroHash(model.featureSchemaHash.data(),
                        model.featureSchemaHash.size())) {
        return fail(report, ApiStatus::NotInitialized,
                    SchemaMismatchReason::MissingMetadata,
                    SchemaField::SchemaHash);
    }
    if (schema.featureCount == 0U ||
        schema.featureCount > FeatureVector::kMaxFeatures) {
        return fail(report, ApiStatus::DataInvalid,
                    schema.featureCount > FeatureVector::kMaxFeatures
                        ? SchemaMismatchReason::CapacityExceeded
                        : SchemaMismatchReason::InvalidDescriptor,
                    SchemaField::FeatureCount);
    }
    if (schema.windowLengthSamples == 0U || schema.strideSamples == 0U ||
        !schema.sampleRateConfigured ||
        !std::isfinite(schema.sampleRateHz) || schema.sampleRateHz <= 0.0F ||
        schema.preprocessingVersion == 0U) {
        return fail(report, ApiStatus::NotInitialized,
                    SchemaMismatchReason::MissingMetadata,
                    SchemaField::Preprocessing);
    }
    if (config_.schemaUnits == nullptr || config_.schemaUnitCount == 0U ||
        modelInput.featureOrder == nullptr ||
        modelInput.featureOrderCount == 0U ||
        modelInput.featureUnits == nullptr ||
        modelInput.featureUnitCount == 0U ||
        modelInput.preprocessingVersion == 0U ||
        modelInput.windowLengthSamples == 0U ||
        modelInput.strideSamples == 0U ||
        !std::isfinite(modelInput.sampleRateHz) ||
        modelInput.sampleRateHz <= 0.0F) {
        SchemaField field = SchemaField::Descriptor;
        if (config_.schemaUnits == nullptr ||
            config_.schemaUnitCount == 0U ||
            modelInput.featureUnits == nullptr ||
            modelInput.featureUnitCount == 0U) {
            field = SchemaField::FeatureUnit;
        } else if (modelInput.featureOrder == nullptr ||
                   modelInput.featureOrderCount == 0U) {
            field = SchemaField::FeatureOrder;
        } else if (modelInput.preprocessingVersion == 0U) {
            field = SchemaField::Preprocessing;
        } else if (modelInput.windowLengthSamples == 0U) {
            field = SchemaField::WindowLength;
        } else if (modelInput.strideSamples == 0U) {
            field = SchemaField::Stride;
        } else {
            field = SchemaField::SampleRate;
        }
        return fail(report, ApiStatus::NotInitialized,
                    SchemaMismatchReason::MissingMetadata, field);
    }

    if (config_.schemaUnitCount != schema.featureCount ||
        modelInput.featureOrderCount != schema.featureCount ||
        modelInput.featureUnitCount != schema.featureCount) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::FeatureCountMismatch,
                    SchemaField::FeatureCount);
    }

    if (schema.schemaVersion != model.featureSchemaVersion) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::VersionMismatch,
                    SchemaField::SchemaVersion);
    }
    if (schema.schemaId != model.featureSchemaId) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::VersionMismatch,
                    SchemaField::SchemaId);
    }
    if (std::memcmp(schema.schemaHash.data(),
                    model.featureSchemaHash.data(),
                    schema.schemaHash.size()) != 0) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::HashMismatch,
                    SchemaField::SchemaHash);
    }

    const std::size_t outputCount =
        static_cast<std::size_t>(schema.featureCount) *
        static_cast<std::size_t>(schema.windowLengthSamples);
    if (outputCount > FeatureVector::kMaxFeatures) {
        return fail(report, ApiStatus::DataInvalid,
                    SchemaMismatchReason::CapacityExceeded,
                    SchemaField::ModelDimension);
    }
    if (model.inputDimension == 0U ||
        model.inputDimension > FeatureVector::kMaxFeatures) {
        return fail(report, ApiStatus::DataInvalid,
                    model.inputDimension > FeatureVector::kMaxFeatures
                        ? SchemaMismatchReason::CapacityExceeded
                        : SchemaMismatchReason::MissingMetadata,
                    SchemaField::ModelDimension);
    }
    if (schema.featureCount != modelInput.featureOrderCount) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::FeatureCountMismatch,
                    SchemaField::FeatureCount);
    }
    if (outputCount != model.inputDimension) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::ModelDimensionMismatch,
                    SchemaField::ModelDimension);
    }
    for (std::size_t i = 0U; i < schema.featureCount; ++i) {
        const auto& feature = schema.features[i];
        if (!validFeatureId(feature.id) || feature.index != i ||
            !feature.modelInputEligible) {
            return fail(report, ApiStatus::DataInvalid,
                        SchemaMismatchReason::InvalidDescriptor,
                        SchemaField::FeatureOrder, i);
        }
        for (std::size_t j = 0U; j < i; ++j) {
            if (schema.features[j].id == feature.id) {
                return fail(report, ApiStatus::DataInvalid,
                            SchemaMismatchReason::InvalidDescriptor,
                            SchemaField::FeatureOrder, i);
            }
        }
        if (!validUnit(config_.schemaUnits[i]) ||
            !validUnit(modelInput.featureUnits[i])) {
            return fail(report, ApiStatus::NotInitialized,
                        SchemaMismatchReason::MissingMetadata,
                        SchemaField::FeatureUnit, i);
        }
        if (feature.id != modelInput.featureOrder[i]) {
            return fail(report, ApiStatus::VersionMismatch,
                        SchemaMismatchReason::FeatureOrderMismatch,
                        SchemaField::FeatureOrder, i);
        }
        if (config_.schemaUnits[i] != modelInput.featureUnits[i]) {
            return fail(report, ApiStatus::VersionMismatch,
                        SchemaMismatchReason::FeatureUnitMismatch,
                        SchemaField::FeatureUnit, i);
        }
    }
    if (schema.preprocessingVersion != modelInput.preprocessingVersion) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::PreprocessingMismatch,
                    SchemaField::Preprocessing);
    }
    if (schema.windowLengthSamples != modelInput.windowLengthSamples) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::WindowMismatch,
                    SchemaField::WindowLength);
    }
    if (schema.strideSamples != modelInput.strideSamples) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::StrideMismatch, SchemaField::Stride);
    }
    if (schema.sampleRateHz != modelInput.sampleRateHz) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::SampleRateMismatch,
                    SchemaField::SampleRate);
    }

    report.status = ApiStatus::Ok;
    report.reason = SchemaMismatchReason::None;
    report.field = SchemaField::None;
    report.structurallyCompatible = true;
    report.productionCompatible = approvalComplete(config_);
    if (!report.productionCompatible) {
        const bool testDescriptor =
            config_.kind == SchemaDescriptorKind::TestOnly ||
            config_.schema->schemaId == kTestOnlySchemaId;
        report.reason = testDescriptor
                            ? SchemaMismatchReason::TestOnlyDescriptor
                            : SchemaMismatchReason::ApprovalRequired;
        report.field = testDescriptor
                           ? SchemaField::DescriptorKind
                           : SchemaField::Approval;
    }
    return ApiStatus::Ok;
}

ApiStatus SchemaRegistry::validateDetailed(
    const FeatureVector& vector,
    SchemaCompatibilityReport& report) const noexcept {
    const ApiStatus compatibilityStatus = compareDetailed(report);
    if (compatibilityStatus != ApiStatus::Ok) {
        return compatibilityStatus;
    }

    if (vector.count > FeatureVector::kMaxFeatures) {
        return fail(report, ApiStatus::DataInvalid,
                    SchemaMismatchReason::CapacityExceeded,
                    SchemaField::FeatureCount);
    }
    if (!vector.valid || vector.count == 0U) {
        return fail(report, ApiStatus::DataInvalid,
                    SchemaMismatchReason::InvalidVector,
                    SchemaField::FeatureCount);
    }
    if (vector.count != config_.model->inputDimension) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::FeatureCountMismatch,
                    SchemaField::FeatureCount);
    }
    if (vector.schemaVersion != config_.schema->schemaVersion) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::VersionMismatch,
                    SchemaField::SchemaVersion);
    }
    if (vector.schemaId != config_.schema->schemaId) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::VersionMismatch,
                    SchemaField::SchemaId);
    }
    if (!vector.schemaHashValid ||
        std::memcmp(vector.schemaHashSha256,
                    config_.schema->schemaHash.data(),
                    config_.schema->schemaHash.size()) != 0) {
        return fail(report, ApiStatus::VersionMismatch,
                    SchemaMismatchReason::HashMismatch,
                    SchemaField::SchemaHash);
    }
    const std::uint32_t expectedMask =
        vector.count == FeatureVector::kMaxFeatures
            ? UINT32_MAX
            : (UINT32_C(1) << vector.count) - 1U;
    if (vector.featureValidMask != expectedMask) {
        return fail(report, ApiStatus::DataInvalid,
                    SchemaMismatchReason::InvalidFeatureMask,
                    SchemaField::FeatureMask);
    }
    if (vector.bootId == 0U || vector.relatedSequenceNo == 0U ||
        vector.windowStartMs > vector.windowEndMs ||
        vector.windowSampleCount != config_.schema->windowLengthSamples) {
        return fail(report, ApiStatus::DataInvalid,
                    SchemaMismatchReason::InvalidProvenance,
                    SchemaField::Provenance);
    }
    for (std::size_t i = 0U; i < vector.count; ++i) {
        if (!std::isfinite(vector.values[i])) {
            return fail(report, ApiStatus::DataInvalid,
                        SchemaMismatchReason::NonFiniteValue,
                        SchemaField::FeatureValue, i);
        }
    }
    report.status = ApiStatus::Ok;
    report.structurallyCompatible = true;
    return ApiStatus::Ok;
}

bool SchemaRegistry::validate(const FeatureVector& vector) const noexcept {
    SchemaCompatibilityReport report{};
    return validateDetailed(vector, report) == ApiStatus::Ok &&
           report.productionCompatible;
}

bool SchemaRegistry::compatible() const noexcept {
    SchemaCompatibilityReport report{};
    return compareDetailed(report) == ApiStatus::Ok &&
           report.productionCompatible;
}

}  // namespace project::ai
