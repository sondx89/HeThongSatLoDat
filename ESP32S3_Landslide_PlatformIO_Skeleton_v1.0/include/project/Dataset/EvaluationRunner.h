#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "EvaluationRunner is available only to host data tools"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Dataset/AnnotationManager.h"
#include "Dataset/CsvWriter.h"
#include "Dataset/DatasetSplitter.h"
#include "Models/FeatureVector.h"
#include "Models/ModelOutput.h"

namespace project::dataset {

inline constexpr std::size_t kMaximumEvaluationWindows = 4096U;
inline constexpr std::size_t kEvaluationOriginCount = kDataOriginCount;
inline constexpr std::size_t kEvaluationEvidenceCount = 6U;
inline constexpr std::size_t kEvaluationSplitCount = 3U;
inline constexpr std::size_t kEvaluationFeatureCount =
    project::models::FeatureVector::kMaxFeatures;
inline constexpr std::size_t kEvaluationChannelQualityCount = 10U;

struct EvaluationConfig {
    std::uint32_t featureSchemaVersion{0U};
    std::uint32_t featureSchemaId{0U};
    std::array<std::uint8_t, 32U> featureSchemaHash{};
    std::uint16_t featureCount{0U};
    bool featureSchemaHashValid{false};
    bool featureSchemaApproved{false};
    AnnotationTargetContract targetContract{};
    bool regressionMetricsApproved{false};
    std::uint64_t upstreamRejectedCount{0U};
};

struct EvaluationWindow {
    std::string_view windowId{};
    AnnotationWindowInterval interval{};
    DataProvenance provenance{};
    DatasetSplit split{DatasetSplit::Train};
    const project::models::FeatureVector* features{nullptr};
    const AnnotationRecord* annotation{nullptr};
    const project::models::ModelOutput* prediction{nullptr};
};

enum class EvaluationStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    InvalidConfiguration,
    CapacityExceeded,
    NumericOverflow,
    CsvWriteError
};

struct EvaluationBucket {
    std::uint64_t windowCount{0U};
    std::uint64_t featureEligibleCount{0U};
    std::uint64_t featureRejectedCount{0U};
    std::uint64_t eligibleLabelCount{0U};
    std::uint64_t excludedLabelCount{0U};
    std::uint64_t missingPredictionCount{0U};
};

struct RegressionMetrics {
    std::uint64_t eligibleLabelCount{0U};
    std::uint64_t predictionPairCount{0U};
    double targetMinimum{0.0};
    double targetMaximum{0.0};
    double meanAbsoluteError{0.0};
    double rootMeanSquaredError{0.0};
    double rSquared{0.0};
    bool targetRangeValid{false};
    bool meanAbsoluteErrorValid{false};
    bool rootMeanSquaredErrorValid{false};
    bool rSquaredValid{false};
};

struct EvaluationFeatureDistribution {
    std::uint64_t observedWindowCount{0U};
    std::uint64_t missingCount{0U};
    std::uint64_t nonFiniteCount{0U};
    std::uint64_t finiteCount{0U};
    double minimum{0.0};
    double maximum{0.0};
    double mean{0.0};
    double variance{0.0};
    bool statisticsValid{false};
};

struct EvaluationReport {
    static constexpr std::size_t kRejectionReasonCount = 10U;

    std::uint64_t totalWindowCount{0U};
    char targetContractId[64U]{};
    std::uint8_t targetContractIdSize{0U};
    std::uint32_t targetContractVersion{0U};
    std::uint64_t invalidRecordCount{0U};
    std::uint64_t invalidQualityStateCount{0U};
    std::uint64_t rejectedCount{0U};
    std::uint64_t schemaMismatchCount{0U};
    std::uint64_t targetExcludedCount{0U};
    std::uint64_t targetJoinMismatchCount{0U};
    std::uint64_t unavailablePredictionCount{0U};
    std::uint64_t qualityStateCounts[4U]{};
    std::uint64_t rejectionReasonCounts[kRejectionReasonCount]{};
    std::uint64_t channelQualityCounts[kEvaluationChannelQualityCount]{};
    EvaluationBucket buckets[kEvaluationOriginCount]
                            [kEvaluationEvidenceCount]
                            [kEvaluationSplitCount]{};
    RegressionMetrics independentMetrics[kEvaluationOriginCount]
                                        [kEvaluationEvidenceCount]
                                        [kEvaluationSplitCount]{};
    RegressionMetrics syntheticProxyMetrics[kEvaluationSplitCount]{};
    EvaluationFeatureDistribution features[kEvaluationFeatureCount]{};
    bool regressionMetricsEnabled{false};
    bool targetContractDefined{false};
};

class EvaluationRunner final {
public:
    explicit EvaluationRunner(const EvaluationConfig& config) noexcept;

    EvaluationStatus evaluate(const EvaluationWindow* windows,
                              std::size_t windowCount,
                              EvaluationReport& report) const noexcept;

    static EvaluationStatus writeSummaryCsvHeader(
        char* output, std::size_t capacity,
        std::size_t& bytesWritten) noexcept;
    static EvaluationStatus writeSummaryCsvRow(
        const EvaluationReport& report, DataOrigin origin,
        EvidenceClass evidence, DatasetSplit split, char* output,
        std::size_t capacity, std::size_t& bytesWritten) noexcept;
    static EvaluationStatus writeFeatureCsvHeader(
        char* output, std::size_t capacity,
        std::size_t& bytesWritten) noexcept;
    static EvaluationStatus writeFeatureCsvRow(
        std::size_t featureIndex,
        const EvaluationFeatureDistribution& distribution, char* output,
        std::size_t capacity, std::size_t& bytesWritten) noexcept;

private:
    const EvaluationConfig config_;
};

}  // namespace project::dataset
