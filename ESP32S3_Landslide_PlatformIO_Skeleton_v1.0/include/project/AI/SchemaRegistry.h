#pragma once

#include <cstddef>
#include <cstdint>

#include "AI/EdgeImpulseAdapter.h"
#include "Models/DataContractTypes.h"
#include "Services/FeaturePipeline.h"

namespace project::ai {

enum class SchemaDescriptorKind : std::uint8_t {
    Unspecified,
    TestOnly,
    Released
};

enum class FeatureUnit : std::uint8_t {
    Unspecified,
    RawCounts,
    MetersPerSecondSquared,
    Degrees,
    Percent,
    MillimetersPerHour,
    AdcCounts
};

enum class SchemaMismatchReason : std::uint8_t {
    None,
    MissingDescriptor,
    MissingMetadata,
    InvalidDescriptor,
    CapacityExceeded,
    InvalidVector,
    InvalidFeatureMask,
    NonFiniteValue,
    InvalidProvenance,
    VersionMismatch,
    HashMismatch,
    FeatureCountMismatch,
    ModelDimensionMismatch,
    FeatureOrderMismatch,
    FeatureUnitMismatch,
    PreprocessingMismatch,
    WindowMismatch,
    StrideMismatch,
    SampleRateMismatch,
    TestOnlyDescriptor,
    ApprovalRequired
};

enum class SchemaField : std::uint8_t {
    None,
    Descriptor,
    DescriptorKind,
    SchemaVersion,
    SchemaId,
    SchemaHash,
    FeatureCount,
    ModelDimension,
    FeatureOrder,
    FeatureUnit,
    Preprocessing,
    WindowLength,
    Stride,
    SampleRate,
    FeatureMask,
    FeatureValue,
    Provenance,
    Approval
};

struct ModelInputMetadata {
    const project::services::FssFeatureId* featureOrder{nullptr};
    std::size_t featureOrderCount{0U};
    const FeatureUnit* featureUnits{nullptr};
    std::size_t featureUnitCount{0U};
    std::uint32_t preprocessingVersion{0U};
    std::uint16_t windowLengthSamples{0U};
    std::uint16_t strideSamples{0U};
    float sampleRateHz{0.0F};
    bool verified{false};
};

// All pointers are borrowed and must outlive the registry. Unit values are
// explicit because FeaturePipelineConfig does not currently carry units.
struct SchemaRegistryConfig {
    SchemaDescriptorKind kind{SchemaDescriptorKind::Unspecified};
    const project::services::FeaturePipelineConfig* schema{nullptr};
    const FeatureUnit* schemaUnits{nullptr};
    std::size_t schemaUnitCount{0U};
    const EdgeImpulseModelConfig* model{nullptr};
    const ModelInputMetadata* modelInput{nullptr};
};

struct SchemaCompatibilityReport {
    static constexpr std::size_t kNoFeature =
        static_cast<std::size_t>(-1);

    project::models::ApiStatus status{
        project::models::ApiStatus::NotInitialized};
    SchemaMismatchReason reason{SchemaMismatchReason::MissingDescriptor};
    SchemaField field{SchemaField::Descriptor};
    std::size_t featureIndex{kNoFeature};
    bool structurallyCompatible{false};
    bool productionCompatible{false};
};

class SchemaRegistry {
public:
    static constexpr std::uint32_t kTestOnlySchemaId = 0x54455354U;

    SchemaRegistry() noexcept = default;
    explicit SchemaRegistry(const SchemaRegistryConfig& config) noexcept;

    bool validate(const project::models::FeatureVector& vector) const noexcept;
    bool compatible() const noexcept;

    project::models::ApiStatus compareDetailed(
        SchemaCompatibilityReport& report) const noexcept;
    project::models::ApiStatus validateDetailed(
        const project::models::FeatureVector& vector,
        SchemaCompatibilityReport& report) const noexcept;

private:
    SchemaRegistryConfig config_{};
};
}  // namespace project::ai
