#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace project::ai {
struct EdgeImpulseModelConfig;
}
namespace project::managers {
struct RiskManagerConfig;
struct SensorManagerConfig;
}
namespace project::services {
struct DataQualityConfig;
struct FeaturePipelineConfig;
}
namespace project::models {
enum class ApiStatus : std::uint8_t;
}

namespace project::managers {

enum class ManifestPurpose : std::uint8_t {
    Unspecified,
    TestOnly,
    Production
};

enum class ManifestErrorCode : std::uint8_t {
    MissingField,
    UnsupportedVersion,
    InvalidEnum,
    NonFinite,
    OutOfRange,
    CrossFieldMismatch,
    ApprovalRequired,
    ArtifactHashMismatch,
    InvalidArtifact
};

struct ManifestValidationIssue {
    ManifestErrorCode code{ManifestErrorCode::MissingField};
    const char* fieldPath{nullptr};
};

struct ValidationReport {
    static constexpr std::size_t kMaxIssues = 32U;

    std::array<ManifestValidationIssue, kMaxIssues> issues{};
    std::uint8_t issueCount{0U};
    bool truncated{false};

    bool valid() const noexcept { return issueCount == 0U && !truncated; }
};

struct ManifestArtifactView {
    const std::uint8_t* data{nullptr};
    std::size_t size{0U};
};

// Non-owning references to the existing module configurations. Referenced
// objects and any artifact buffers must outlive validateManifest().
struct ConfigManifest {
    std::uint32_t formatVersion{0U};
    ManifestPurpose purpose{ManifestPurpose::Unspecified};
    bool productionApprovalComplete{false};

    const project::services::DataQualityConfig* dataQuality{nullptr};
    const project::managers::SensorManagerConfig* sensorManager{nullptr};
    const project::services::FeaturePipelineConfig* featurePipeline{nullptr};
    const project::ai::EdgeImpulseModelConfig* model{nullptr};
    const project::managers::RiskManagerConfig* risk{nullptr};

    ManifestArtifactView featureSchemaArtifact{};
    ManifestArtifactView modelArtifact{};
    ManifestArtifactView riskConfigArtifact{};
};

// Current legacy signatures remain fail-closed. See docs/CONFIG_MANIFEST.md.
class ConfigManager {
public:
    static constexpr std::uint32_t kSupportedManifestVersion = 1U;

    bool loadLastValid();
    bool validateManifest();
    bool activateAtomically();

    static project::models::ApiStatus validateManifest(
        const ConfigManifest& manifest,
        ValidationReport& report) noexcept;
};
}  // namespace project::managers
