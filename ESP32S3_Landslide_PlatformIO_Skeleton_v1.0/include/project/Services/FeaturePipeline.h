#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Models/FeatureVector.h"
#include "Models/ProcessedFrame.h"

namespace project::services {

enum class FssFeatureId : std::uint8_t {
    AccelXRaw,
    AccelYRaw,
    AccelZRaw,
    AccelMagnitude,
    TiltXDeg,
    TiltYDeg,
    SoilRaw,
    Yl83Raw,
    Hx710bRaw
};

struct FeatureDefinition {
    FssFeatureId id{FssFeatureId::AccelXRaw};
    std::uint16_t index{0};
    bool modelInputEligible{false};
};

struct FeaturePipelineConfig {
    static constexpr std::size_t kMaxWindowSamples = 32;
    static constexpr std::size_t kHashSize = 32;

    std::uint32_t schemaVersion{0};
    std::uint32_t schemaId{0};
    std::array<std::uint8_t, kHashSize> schemaHash{};
    bool schemaHashValid{false};

    std::array<FeatureDefinition, project::models::FeatureVector::kMaxFeatures>
        features{};
    std::uint16_t featureCount{0};
    std::uint16_t windowLengthSamples{0};
    std::uint16_t strideSamples{0};

    float sampleRateHz{0.0F};
    std::uint32_t samplePeriodToleranceMs{0};
    std::uint32_t preprocessingVersion{0};
    float accelerationToMps2{0.0F};
    bool accelerationScaleConfigured{false};
    bool sampleRateConfigured{false};
    bool samplePeriodToleranceConfigured{false};
    bool preprocessingApproved{false};
    bool schemaApproved{false};
    bool modelMetadataVerified{false};
    bool flatLayoutApproved{false};
    bool tiltFormulaApproved{false};
};

enum class FeaturePipelineStatus : std::uint8_t {
    Ok,
    NotConfigured,
    SchemaNotApproved,
    InvalidConfiguration,
    UnsupportedFeature,
    InvalidFrame,
    InvalidFeatureData,
    SequenceDiscontinuity,
    NotReady,
    OutputTooLarge
};

class FeaturePipeline final {
public:
    static constexpr std::size_t kMaxWindowSamples =
        FeaturePipelineConfig::kMaxWindowSamples;
    static constexpr std::size_t kMaxFeaturesPerSample =
        project::models::FeatureVector::kMaxFeatures;

    explicit FeaturePipeline(
        const FeaturePipelineConfig& config = FeaturePipelineConfig{}) noexcept;

    bool push(const project::models::ProcessedFrame& frame) noexcept;
    bool windowReady() const noexcept;
    bool build(project::models::FeatureVector& output) noexcept;
    void resetWindow() noexcept;
    FeaturePipelineStatus status() const noexcept { return status_; }

    static FeaturePipelineStatus validateConfig(
        const FeaturePipelineConfig& config) noexcept;

private:
    struct FeatureSample {
        std::array<float, kMaxFeaturesPerSample> values{};
        std::uint64_t bootId{0};
        std::uint64_t sequence{0};
        std::uint64_t timestampMs{0};
    };

    FeaturePipelineStatus extract(
        const project::models::ProcessedFrame& frame,
        FeatureSample& sample) const noexcept;
    static bool featureSupported(FssFeatureId id) noexcept;

    const FeaturePipelineConfig config_;
    std::array<FeatureSample, kMaxWindowSamples> samples_{};
    std::size_t nextSample_{0};
    std::size_t sampleCount_{0};
    std::size_t samplesSinceBuild_{0};
    std::uint32_t calibrationVersion_{0};
    bool hasCalibrationVersion_{false};
    FeaturePipelineStatus status_{FeaturePipelineStatus::NotConfigured};
};

}  // namespace project::services
