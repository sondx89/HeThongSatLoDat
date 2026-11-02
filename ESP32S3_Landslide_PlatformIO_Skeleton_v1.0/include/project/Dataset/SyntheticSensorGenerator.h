#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "SyntheticSensorGenerator is available only to host data tools"
#endif

#include <array>
#include <cstddef>
#include <cstdint>

#include "Dataset/DataProvenance.h"

namespace project::dataset {

inline constexpr std::uint32_t kSyntheticGeneratorVersion = 1U;
inline constexpr std::uint64_t kSyntheticDefaultSeed = 0x534E544843303031ULL;
inline constexpr std::size_t kSyntheticRawChannelCount = 6U;

enum class SyntheticScenario : std::uint8_t {
    Stable,
    GradualTrend,
    StepChange,
    Oscillation,
    ImpulseNoise,
    Saturation,
    Dropout,
    Stale,
    OutOfOrder,
    SequenceGap,
    Reboot,
    CalibrationChange,
    MixedValidity
};

enum class SyntheticRawChannel : std::uint8_t {
    AccelX,
    AccelY,
    AccelZ,
    Soil,
    Yl83,
    Hx710b
};

struct SyntheticScenarioParameters {
    std::int64_t baseline{100};
    std::array<std::int64_t, kSyntheticRawChannelCount> channelOffsets{};
    std::int64_t noiseAmplitude{0};
    std::int64_t trendPerSample{1};
    std::int64_t eventDelta{100};
    std::uint64_t eventSample{4U};
    std::uint64_t eventLength{1U};
    std::uint32_t oscillationPeriodSamples{8U};
    std::uint64_t sequenceGapSize{2U};
    std::uint64_t staleAgeMs{100U};
    std::int64_t saturationMinimum{-1000};
    std::int64_t saturationMaximum{1000};
    std::uint8_t affectedChannel{0U};
};

struct SyntheticGeneratorConfig {
    SyntheticScenario scenario{SyntheticScenario::Stable};
    std::uint64_t seed{kSyntheticDefaultSeed};
    std::uint64_t recordCount{0U};
    std::uint64_t bootId{1U};
    std::uint64_t firstSequence{0U};
    std::uint64_t firstTimestampMs{0U};
    std::uint32_t timestampStepMs{0U};
    SyntheticScenarioParameters parameters{};
};

struct SyntheticRawObservation {
    std::uint64_t bootId{0U};
    std::uint64_t sequenceNo{0U};
    std::uint64_t timestampMs{0U};
    std::uint64_t evaluationTimeMs{0U};
    std::uint32_t calibrationVersion{0U};
    std::array<std::int64_t, kSyntheticRawChannelCount> rawValues{};
    std::array<std::uint64_t, kSyntheticRawChannelCount>
        channelTimestampMs{};
    std::array<bool, kSyntheticRawChannelCount> channelValid{};
    DataProvenance provenance{};
};

struct SyntheticLatentState {
    SyntheticScenario scenario{SyntheticScenario::Stable};
    std::uint64_t sampleIndex{0U};
    std::int64_t generatedOffset{0};
    std::int64_t noiseOffset{0};
    std::uint8_t validChannelMask{0U};
    bool eventActive{false};
};

struct SyntheticRecord {
    SyntheticRawObservation observation{};
    SyntheticLatentState latent{};
};

struct SyntheticScenarioManifest {
    std::uint32_t generatorVersion{kSyntheticGeneratorVersion};
    SyntheticScenario scenario{SyntheticScenario::Stable};
    std::uint64_t seed{0U};
    std::uint64_t recordCount{0U};
    std::uint32_t timestampStepMs{0U};
    SyntheticScenarioParameters parameters{};
    DataProvenance provenance{DataOrigin::Synthetic, EvidenceClass::GT5};
    const char* rawValueDomain{"synthetic_raw_count"};
    bool proxyTargetPresent{false};
    bool eligibleForFieldClaim{false};
};

enum class SyntheticGeneratorStatus : std::uint8_t {
    Ok,
    EndOfData,
    InvalidConfiguration,
    NumericOverflow
};

class SyntheticSensorGenerator final {
public:
    explicit SyntheticSensorGenerator(
        const SyntheticGeneratorConfig& config) noexcept;

    SyntheticGeneratorStatus next(SyntheticRecord& output) noexcept;
    const SyntheticScenarioManifest& manifest() const noexcept {
        return manifest_;
    }

    static bool validateConfig(
        const SyntheticGeneratorConfig& config) noexcept;

private:
    std::uint64_t nextRandom() noexcept;
    bool generateOffset(std::uint64_t sampleIndex,
                        std::int64_t& offset,
                        std::int64_t& noiseOffset,
                        bool& eventActive) noexcept;

    const SyntheticGeneratorConfig config_;
    SyntheticScenarioManifest manifest_{};
    std::uint64_t randomState_{0U};
    std::uint64_t generatedCount_{0U};
    SyntheticGeneratorStatus status_{SyntheticGeneratorStatus::Ok};
};

}  // namespace project::dataset
