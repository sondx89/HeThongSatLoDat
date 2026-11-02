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

enum class ConfigSlotReadStatus : std::uint8_t {
    Empty,
    Candidate,
    Committed,
    Corrupt,
    IoError
};

// Adapters encode manifest fields explicitly and own decoded config/artifact
// memory. Views returned by readSlot stay valid until that slot is rewritten
// or the adapter is destroyed. All operations are synchronous.
class IConfigManifestPersistence {
public:
    virtual ~IConfigManifestPersistence() = default;

    // Must invalidate the target slot's old commit marker before changing it.
    virtual bool writeCandidate(std::uint8_t slot, std::uint64_t generation,
                                const ConfigManifest& manifest) noexcept = 0;
    virtual bool flushCandidate(std::uint8_t slot) noexcept = 0;
    virtual ConfigSlotReadStatus readSlot(
        std::uint8_t slot, std::uint64_t& generation,
        ConfigManifest& manifest) noexcept = 0;
    virtual bool writeCommitMarker(std::uint8_t slot,
                                   std::uint64_t generation) noexcept = 0;
    virtual bool flushCommitMarker(std::uint8_t slot) noexcept = 0;
};

class ConfigManager {
public:
    static constexpr std::uint32_t kSupportedManifestVersion = 1U;

    ConfigManager() noexcept = default;
    explicit ConfigManager(
        IConfigManifestPersistence& persistence) noexcept;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;

    bool loadLastValid();
    bool storeCandidate(const ConfigManifest& candidate);
    bool validateManifest();
    bool activateAtomically();
    const ConfigManifest* selectedManifest() const noexcept;
    const ConfigManifest* activeManifest() const noexcept;

    static project::models::ApiStatus validateManifest(
        const ConfigManifest& manifest,
        ValidationReport& report) noexcept;

private:
    IConfigManifestPersistence* persistence_{nullptr};
    ConfigManifest selectedManifest_{};
    bool hasSelection_{false};
    bool activated_{false};
};
}  // namespace project::managers
