#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Models/FeatureVector.h"

namespace project::replay {

enum class CsvColumn : std::uint8_t {
    Tilt,
    Soil,
    Rain,
    Stress,
    Risk,
    Timestamp
};

struct ReplayFeatureMapping {
    CsvColumn source{CsvColumn::Tilt};
    std::uint16_t featureIndex{0};
    bool modelInputEligible{false};
};

struct ReplayConfig {
    static constexpr std::size_t kMaxWindowSamples = 32;
    static constexpr std::size_t kSchemaHashSize = 32;

    std::uint32_t csvSchemaVersion{0};
    std::uint32_t featureSchemaVersion{0};
    std::uint32_t featureSchemaId{0};
    std::array<std::uint8_t, kSchemaHashSize> featureSchemaHash{};
    std::array<std::uint8_t, kSchemaHashSize> datasetSha256{};
    bool featureSchemaHashValid{false};
    bool datasetHashVerified{false};
    bool datasetManifestVerified{false};
    bool featureSchemaApproved{false};
    bool modelMetadataVerified{false};
    bool flatLayoutApproved{false};
    bool columnMappingApproved{false};

    std::array<ReplayFeatureMapping,
               project::models::FeatureVector::kMaxFeatures>
        features{};
    std::uint16_t featureCount{0};
    std::uint16_t windowLengthSamples{0};
    std::uint16_t strideSamples{0};
    std::uint16_t modelInputElementCount{0};
    std::uint64_t replaySessionId{0};

    float sampleRateHz{0.0F};
    std::uint32_t samplePeriodToleranceMs{0};
    bool sampleRateConfigured{false};
    bool samplePeriodToleranceConfigured{false};
};

enum class CsvReplaySourceStatus : std::uint8_t {
    Ok,
    EndOfFile,
    IoError
};

class ICsvReplaySource {
public:
    virtual ~ICsvReplaySource() = default;
    virtual CsvReplaySourceStatus rewind() noexcept = 0;
    virtual CsvReplaySourceStatus read(std::uint8_t* output,
                                       std::size_t capacity,
                                       std::size_t& bytesRead) noexcept = 0;
};

enum class ReplayStatus : std::uint8_t {
    Ok,
    VectorReady,
    EndOfFile,
    NotInitialized,
    ManifestNotVerified,
    SchemaNotApproved,
    InvalidConfiguration,
    UnsupportedMapping,
    InvalidHeader,
    InvalidRow,
    TruncatedRow,
    TimestampDiscontinuity,
    SourceError,
    OutputTooLarge
};

struct ReplayStats {
    std::uint64_t rowsRead{0};
    std::uint64_t rowsRejected{0};
    std::uint64_t vectorsProduced{0};
    std::uint64_t lastRowSequence{0};
};

class ReplayEngine final {
public:
    static constexpr std::size_t kMaxWindowSamples =
        ReplayConfig::kMaxWindowSamples;

    ReplayEngine(ICsvReplaySource& source,
                 const ReplayConfig& config) noexcept;
    ReplayEngine(const ReplayEngine&) = delete;
    ReplayEngine& operator=(const ReplayEngine&) = delete;

    ReplayStatus begin() noexcept;
    ReplayStatus next(project::models::FeatureVector& output) noexcept;
    ReplayStatus status() const noexcept { return status_; }
    const ReplayStats& stats() const noexcept { return stats_; }
    void reset() noexcept;

    static ReplayStatus validateConfig(const ReplayConfig& config) noexcept;

private:
    struct ReplaySample {
        std::array<float, project::models::FeatureVector::kMaxFeatures> values{};
        std::uint64_t timestampMs{0};
        std::uint64_t rowSequence{0};
    };

    enum class LineStatus : std::uint8_t {
        Complete,
        EndOfFile,
        Truncated,
        TooLong,
        SourceError
    };

    LineStatus readLine(char* output, std::size_t capacity,
                        std::size_t& length) noexcept;
    ReplayStatus validateHeader() noexcept;
    ReplayStatus parseRow(const char* line, std::size_t length,
                          ReplaySample& sample) noexcept;
    ReplayStatus buildVector(
        project::models::FeatureVector& output) noexcept;
    void clearWindow() noexcept;

    ICsvReplaySource& source_;
    const ReplayConfig config_;
    std::array<std::uint8_t, 64> readBuffer_{};
    std::size_t bufferPosition_{0};
    std::size_t bufferLength_{0};
    bool discardingLine_{false};
    std::array<ReplaySample, kMaxWindowSamples> samples_{};
    std::size_t sampleCount_{0};
    std::size_t samplesSinceBuild_{0};
    std::uint64_t rowSequence_{0};
    std::uint64_t previousTimestampMs_{0};
    bool hasPreviousTimestamp_{false};
    bool initialized_{false};
    ReplayStats stats_{};
    ReplayStatus status_{ReplayStatus::NotInitialized};
};

}  // namespace project::replay
