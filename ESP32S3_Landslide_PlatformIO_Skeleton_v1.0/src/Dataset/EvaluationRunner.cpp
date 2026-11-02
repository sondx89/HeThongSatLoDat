#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/EvaluationRunner.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace project::dataset::evaluation_detail {

constexpr std::size_t kSummaryColumnCount = 21U;
constexpr std::size_t kFeatureColumnCount = 10U;

constexpr std::string_view kSummaryHeader[kSummaryColumnCount] = {
    "evaluation_schema_version", "target_contract_id",
    "target_contract_version", "data_origin", "evidence_class",
    "split_name", "window_count", "feature_eligible_count",
    "feature_rejected_count", "eligible_label_count", "excluded_label_count",
    "missing_prediction_count", "metric_scope", "metric_label_count",
    "prediction_pair_count", "target_min", "target_max", "mae", "rmse",
    "r_squared", "metrics_enabled"};

constexpr std::string_view kFeatureHeader[kFeatureColumnCount] = {
    "evaluation_schema_version", "feature_index", "observed_window_count",
    "missing_count", "non_finite_count", "finite_count", "minimum",
    "maximum", "mean", "variance"};

struct MetricAccumulator {
    std::uint64_t labelCount{0U};
    std::uint64_t pairCount{0U};
    double targetMinimum{0.0};
    double targetMaximum{0.0};
    double absoluteErrorSum{0.0};
    double squaredErrorSum{0.0};
    double pairTargetMean{0.0};
    double pairTargetM2{0.0};
};

std::size_t originIndex(DataOrigin origin) noexcept {
    return static_cast<std::size_t>(origin);
}

std::size_t evidenceIndex(EvidenceClass evidence) noexcept {
    return static_cast<std::size_t>(evidence);
}

std::size_t splitIndex(DatasetSplit split) noexcept {
    return static_cast<std::size_t>(split);
}

bool validSplit(DatasetSplit split) noexcept {
    return split == DatasetSplit::Train ||
           split == DatasetSplit::Validation || split == DatasetSplit::Test;
}

bool validOriginAndEvidence(DataProvenance provenance) noexcept {
    return provenance.isValid() &&
           originIndex(provenance.origin) < kEvaluationOriginCount &&
           evidenceIndex(provenance.evidenceClass) <
               kEvaluationEvidenceCount;
}

std::string_view originName(DataOrigin origin) noexcept {
    switch (origin) {
        case DataOrigin::Synthetic:
            return "SYNTHETIC";
        case DataOrigin::Bench:
            return "BENCH";
        case DataOrigin::Field:
            return "FIELD";
        case DataOrigin::Reference:
            return "REFERENCE";
        case DataOrigin::Replay:
            return "REPLAY";
        case DataOrigin::Unspecified:
            break;
    }
    return {};
}

std::string_view evidenceName(EvidenceClass evidence) noexcept {
    switch (evidence) {
        case EvidenceClass::GT1:
            return "GT1";
        case EvidenceClass::GT2:
            return "GT2";
        case EvidenceClass::GT3:
            return "GT3";
        case EvidenceClass::GT4:
            return "GT4";
        case EvidenceClass::GT5:
            return "GT5";
        case EvidenceClass::NotApplicable:
            return "NOT_APPLICABLE";
    }
    return {};
}

std::string_view splitName(DatasetSplit split) noexcept {
    switch (split) {
        case DatasetSplit::Train:
            return "TRAIN";
        case DatasetSplit::Validation:
            return "VALIDATION";
        case DatasetSplit::Test:
            return "TEST";
    }
    return {};
}

bool targetContractMatches(
    const AnnotationRecord& annotation,
    const AnnotationTargetContract& targetContract) noexcept {
    return targetContract.approved && !targetContract.id.empty() &&
           !targetContract.name.empty() && targetContract.version != 0U &&
           annotation.targetContractId.view() == targetContract.id &&
           annotation.targetName.view() == targetContract.name &&
           annotation.targetUnit.view() == targetContract.unit &&
           annotation.targetContractVersion == targetContract.version;
}

bool increment(std::uint64_t& value) noexcept {
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    ++value;
    return true;
}

bool addCount(std::uint64_t& value, std::uint64_t amount) noexcept {
    if (amount > std::numeric_limits<std::uint64_t>::max() - value) {
        return false;
    }
    value += amount;
    return true;
}

bool updateMetric(MetricAccumulator& accumulator, double target,
                 const project::models::ModelOutput* prediction) noexcept {
    if (!std::isfinite(target) || !increment(accumulator.labelCount)) {
        return false;
    }
    if (accumulator.labelCount == 1U) {
        accumulator.targetMinimum = target;
        accumulator.targetMaximum = target;
    } else {
        if (target < accumulator.targetMinimum) {
            accumulator.targetMinimum = target;
        }
        if (target > accumulator.targetMaximum) {
            accumulator.targetMaximum = target;
        }
    }
    if (prediction == nullptr ||
        prediction->status != project::models::InferenceStatus::Ok ||
        prediction->outputCount != 1U || !prediction->predictionValid ||
        !std::isfinite(prediction->rawOutputs[0]) ||
        !std::isfinite(prediction->predictionValue) ||
        prediction->rawOutputs[0] != prediction->predictionValue) {
        return true;
    }

    const double residual =
        static_cast<double>(prediction->predictionValue) - target;
    const double absoluteError = std::fabs(residual);
    const double squaredError = residual * residual;
    if (!std::isfinite(residual) || !std::isfinite(absoluteError) ||
        !std::isfinite(squaredError) || !increment(accumulator.pairCount)) {
        return false;
    }
    const double delta = target - accumulator.pairTargetMean;
    const double mean =
        accumulator.pairTargetMean +
        delta / static_cast<double>(accumulator.pairCount);
    const double deltaAfterMean = target - mean;
    const double m2 = accumulator.pairTargetM2 + delta * deltaAfterMean;
    const double absSum = accumulator.absoluteErrorSum + absoluteError;
    const double squareSum = accumulator.squaredErrorSum + squaredError;
    if (!std::isfinite(mean) || !std::isfinite(m2) ||
        !std::isfinite(absSum) || !std::isfinite(squareSum)) {
        return false;
    }
    accumulator.pairTargetMean = mean;
    accumulator.pairTargetM2 = m2;
    accumulator.absoluteErrorSum = absSum;
    accumulator.squaredErrorSum = squareSum;
    return true;
}

void finalizeMetrics(const MetricAccumulator& source,
                     RegressionMetrics& destination) noexcept {
    destination = {};
    destination.eligibleLabelCount = source.labelCount;
    destination.predictionPairCount = source.pairCount;
    if (source.labelCount != 0U) {
        destination.targetMinimum = source.targetMinimum;
        destination.targetMaximum = source.targetMaximum;
        destination.targetRangeValid = true;
    }
    if (source.pairCount != 0U) {
        const double count = static_cast<double>(source.pairCount);
        destination.meanAbsoluteError = source.absoluteErrorSum / count;
        destination.rootMeanSquaredError =
            std::sqrt(source.squaredErrorSum / count);
        destination.meanAbsoluteErrorValid =
            std::isfinite(destination.meanAbsoluteError);
        destination.rootMeanSquaredErrorValid =
            std::isfinite(destination.rootMeanSquaredError);
        if (source.pairTargetM2 > 0.0) {
            destination.rSquared =
                1.0 - source.squaredErrorSum / source.pairTargetM2;
            destination.rSquaredValid = std::isfinite(destination.rSquared);
        }
    }
}

bool appendEvaluationHeader(CsvRowWriter& writer,
                            const std::string_view* fields,
                            std::size_t count) noexcept {
    for (std::size_t i = 0U; i < count; ++i) {
        if (writer.appendField(fields[i]) != CsvWriteStatus::Ok) {
            return false;
        }
    }
    return writer.finish() == CsvWriteStatus::Ok;
}

bool validInterval(const AnnotationWindowInterval& interval) noexcept {
    return !interval.datasetId.empty() && !interval.siteId.empty() &&
           !interval.sessionId.empty() && interval.bootId != 0U &&
           interval.startMs < interval.endMs;
}

bool eligibleAnnotation(
    const EvaluationConfig& config, const EvaluationWindow& window,
    bool regressionEnabled) noexcept {
    if (!regressionEnabled || window.annotation == nullptr ||
        !AnnotationManager::matchesWindow(*window.annotation,
                                          window.interval)) {
        return false;
    }
    const AnnotationRecord& annotation = *window.annotation;
    if (annotation.status != AnnotationStatus::Accepted ||
        !annotation.targetValueValid ||
        !std::isfinite(annotation.targetValue) ||
        !targetContractMatches(annotation, config.targetContract) ||
        annotation.provenance.origin != window.provenance.origin ||
        annotation.provenance.evidenceClass !=
            window.provenance.evidenceClass) {
        return false;
    }
    if (window.provenance.origin == DataOrigin::Synthetic) {
        return window.provenance.evidenceClass == EvidenceClass::GT5 &&
               annotation.kind == AnnotationKind::SyntheticProxy;
    }
    return annotation.kind == AnnotationKind::Independent &&
           (window.provenance.evidenceClass == EvidenceClass::GT1 ||
            window.provenance.evidenceClass == EvidenceClass::GT2 ||
            window.provenance.evidenceClass == EvidenceClass::GT3 ||
            window.provenance.evidenceClass == EvidenceClass::GT4);
}

bool exactFeatureSchema(const EvaluationConfig& config,
                        const project::models::FeatureVector& features) noexcept {
    return features.count == config.featureCount &&
           features.schemaVersion == config.featureSchemaVersion &&
           features.schemaId == config.featureSchemaId &&
           features.schemaHashValid &&
           std::memcmp(features.schemaHashSha256,
                       config.featureSchemaHash.data(),
                       config.featureSchemaHash.size()) == 0;
}

}  // namespace project::dataset::evaluation_detail

namespace project::dataset {
using namespace evaluation_detail;

EvaluationRunner::EvaluationRunner(const EvaluationConfig& config) noexcept
    : config_(config) {}

EvaluationStatus EvaluationRunner::evaluate(
    const EvaluationWindow* windows, std::size_t windowCount,
    EvaluationReport& report) const noexcept {
    report = {};
    if (windowCount > kMaximumEvaluationWindows) {
        return EvaluationStatus::CapacityExceeded;
    }
    if (windowCount != 0U && windows == nullptr) {
        return EvaluationStatus::InvalidArgument;
    }
    if (!config_.featureSchemaApproved ||
        !config_.featureSchemaHashValid ||
        config_.featureSchemaVersion == 0U ||
        config_.featureSchemaId == 0U || config_.featureCount == 0U ||
        config_.featureCount > kEvaluationFeatureCount ||
        (config_.targetContract.approved &&
         (config_.targetContract.id.empty() ||
          config_.targetContract.id.size() >= sizeof(report.targetContractId) ||
          config_.targetContract.name.empty() ||
          config_.targetContract.version == 0U))) {
        return EvaluationStatus::InvalidConfiguration;
    }
    bool nonzeroHash = false;
    for (const std::uint8_t byte : config_.featureSchemaHash) {
        nonzeroHash = nonzeroHash || byte != 0U;
    }
    if (!nonzeroHash) {
        report = {};
        return EvaluationStatus::InvalidConfiguration;
    }
    if (config_.targetContract.approved) {
        std::memcpy(report.targetContractId, config_.targetContract.id.data(),
                    config_.targetContract.id.size());
        report.targetContractIdSize =
            static_cast<std::uint8_t>(config_.targetContract.id.size());
        report.targetContractVersion = config_.targetContract.version;
        report.targetContractDefined = true;
    }
    report.totalWindowCount = windowCount;
    report.regressionMetricsEnabled =
        config_.targetContract.approved &&
        config_.regressionMetricsApproved;
    if (!addCount(report.rejectedCount, config_.upstreamRejectedCount)) {
        report = {};
        return EvaluationStatus::NumericOverflow;
    }

    MetricAccumulator independent[kEvaluationOriginCount]
                                [kEvaluationEvidenceCount]
                                [kEvaluationSplitCount]{};
    MetricAccumulator syntheticProxy[kEvaluationSplitCount]{};

    for (std::size_t row = 0U; row < windowCount; ++row) {
        const EvaluationWindow& window = windows[row];
        if (!validOriginAndEvidence(window.provenance) ||
            !validSplit(window.split)) {
            if (!increment(report.invalidRecordCount) ||
                !increment(report.rejectedCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            continue;
        }
        const std::size_t origin = originIndex(window.provenance.origin);
        const std::size_t evidence =
            evidenceIndex(window.provenance.evidenceClass);
        const std::size_t split = splitIndex(window.split);
        EvaluationBucket& bucket = report.buckets[origin][evidence][split];
        if (!increment(bucket.windowCount)) {
            report = {};
            return EvaluationStatus::NumericOverflow;
        }
        if (window.windowId.empty() || !validInterval(window.interval) ||
            window.features == nullptr) {
            if (!increment(report.invalidRecordCount) ||
                !increment(report.rejectedCount) ||
                !increment(bucket.featureRejectedCount) ||
                (window.annotation != nullptr &&
                 !increment(bucket.excludedLabelCount))) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            continue;
        }

        const project::models::FeatureVector& features = *window.features;
        const std::size_t eligibility =
            static_cast<std::size_t>(features.quality.eligibility);
        bool invalidQualityState = false;
        if (eligibility < 4U &&
            !increment(report.qualityStateCounts[eligibility])) {
            report = {};
            return EvaluationStatus::NumericOverflow;
        } else if (eligibility >= 4U) {
            if (!increment(report.invalidQualityStateCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            invalidQualityState = true;
        }
        for (const project::models::DataQualityState state :
             features.quality.channelQuality) {
            const std::size_t stateIndex = static_cast<std::size_t>(state);
            if (stateIndex < kEvaluationChannelQualityCount) {
                if (!increment(report.channelQualityCounts[stateIndex])) {
                    report = {};
                    return EvaluationStatus::NumericOverflow;
                }
            } else {
                if (!increment(report.invalidQualityStateCount)) {
                    report = {};
                    return EvaluationStatus::NumericOverflow;
                }
                invalidQualityState = true;
            }
        }
        const std::size_t rejection =
            static_cast<std::size_t>(features.quality.rejectionReason);
        if (rejection < EvaluationReport::kRejectionReasonCount) {
            if (!increment(report.rejectionReasonCounts[rejection])) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
        } else {
            if (!increment(report.invalidQualityStateCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            invalidQualityState = true;
        }
        if (invalidQualityState) {
            if (!increment(report.invalidRecordCount) ||
                !increment(report.rejectedCount) ||
                !increment(bucket.featureRejectedCount) ||
                (window.annotation != nullptr &&
                 !increment(bucket.excludedLabelCount))) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            continue;
        }

        if (!exactFeatureSchema(config_, features)) {
            if (!increment(report.schemaMismatchCount) ||
                !increment(report.rejectedCount) ||
                !increment(bucket.featureRejectedCount) ||
                (window.annotation != nullptr &&
                 !increment(bucket.excludedLabelCount))) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            continue;
        }
        const std::uint32_t expectedMask =
            config_.featureCount == kEvaluationFeatureCount
                ? UINT32_MAX
                : (UINT32_C(1) << config_.featureCount) - 1U;
        if ((features.featureValidMask & ~expectedMask) != 0U) {
            if (!increment(report.schemaMismatchCount) ||
                !increment(report.rejectedCount) ||
                !increment(bucket.featureRejectedCount) ||
                (window.annotation != nullptr &&
                 !increment(bucket.excludedLabelCount))) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            continue;
        }
        for (std::size_t feature = 0U; feature < config_.featureCount;
             ++feature) {
            EvaluationFeatureDistribution& distribution =
                report.features[feature];
            if (!increment(distribution.observedWindowCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            const bool validBit =
                (features.featureValidMask & (UINT32_C(1) << feature)) != 0U;
            if (!validBit || !std::isfinite(features.values[feature])) {
                if (!increment(distribution.missingCount) ||
                    (validBit && !increment(distribution.nonFiniteCount))) {
                    report = {};
                    return EvaluationStatus::NumericOverflow;
                }
            } else if (features.valid &&
                       features.quality.eligibility ==
                           project::models::WindowEligibilityState::Eligible) {
                const double value =
                    static_cast<double>(features.values[feature]);
                const std::uint64_t nextCount =
                    distribution.finiteCount + 1U;
                if (nextCount == 0U) {
                    report = {};
                    return EvaluationStatus::NumericOverflow;
                }
                if (distribution.finiteCount == 0U) {
                    distribution.minimum = value;
                    distribution.maximum = value;
                    distribution.mean = value;
                    distribution.variance = 0.0;
                } else {
                    if (value < distribution.minimum) {
                        distribution.minimum = value;
                    }
                    if (value > distribution.maximum) {
                        distribution.maximum = value;
                    }
                    const double delta = value - distribution.mean;
                    const double mean =
                        distribution.mean +
                        delta / static_cast<double>(nextCount);
                    const double deltaAfterMean = value - mean;
                    const double m2 =
                        distribution.variance *
                            static_cast<double>(distribution.finiteCount) +
                        delta * deltaAfterMean;
                    if (!std::isfinite(mean) || !std::isfinite(m2)) {
                        report = {};
                        return EvaluationStatus::NumericOverflow;
                    }
                    distribution.mean = mean;
                    distribution.variance =
                        m2 / static_cast<double>(nextCount);
                }
                distribution.finiteCount = nextCount;
                distribution.statisticsValid = true;
            }
        }

        if (features.valid &&
            features.quality.eligibility ==
                project::models::WindowEligibilityState::Eligible) {
            if (!increment(bucket.featureEligibleCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
        } else {
            if (!increment(bucket.featureRejectedCount) ||
                !increment(report.rejectedCount) ||
                (window.annotation != nullptr &&
                 !increment(bucket.excludedLabelCount))) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            continue;
        }

        if (window.annotation == nullptr) {
            if (!increment(bucket.excludedLabelCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            continue;
        }
        if (!report.regressionMetricsEnabled) {
            if (!increment(report.targetExcludedCount) ||
                !increment(bucket.excludedLabelCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            continue;
        }
        if (!AnnotationManager::matchesWindow(*window.annotation,
                                              window.interval) ||
            window.annotation->provenance.origin !=
                window.provenance.origin ||
            window.annotation->provenance.evidenceClass !=
                window.provenance.evidenceClass) {
            if (!increment(report.targetJoinMismatchCount) ||
                !increment(bucket.excludedLabelCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            continue;
        }
        if (!eligibleAnnotation(config_, window,
                                report.regressionMetricsEnabled)) {
            if (!targetContractMatches(*window.annotation,
                                       config_.targetContract) &&
                !increment(report.targetExcludedCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            if (!increment(bucket.excludedLabelCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
            continue;
        }

        if (!increment(bucket.eligibleLabelCount)) {
            report = {};
            return EvaluationStatus::NumericOverflow;
        }
        const AnnotationRecord& annotation = *window.annotation;
        if (window.provenance.origin == DataOrigin::Synthetic) {
            if (!updateMetric(syntheticProxy[split],
                              annotation.targetValue, window.prediction)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
        } else if (!updateMetric(independent[origin][evidence][split],
                                 annotation.targetValue,
                                 window.prediction)) {
            report = {};
            return EvaluationStatus::NumericOverflow;
        }
        if (window.prediction == nullptr ||
            window.prediction->status !=
                project::models::InferenceStatus::Ok ||
            window.prediction->outputCount != 1U ||
            !window.prediction->predictionValid ||
            !std::isfinite(window.prediction->rawOutputs[0]) ||
            !std::isfinite(window.prediction->predictionValue) ||
            window.prediction->rawOutputs[0] !=
                window.prediction->predictionValue) {
            if (!increment(bucket.missingPredictionCount) ||
                !increment(report.unavailablePredictionCount)) {
                report = {};
                return EvaluationStatus::NumericOverflow;
            }
        }
    }

    for (std::size_t origin = 0U; origin < kEvaluationOriginCount; ++origin) {
        for (std::size_t evidence = 0U; evidence < kEvaluationEvidenceCount;
             ++evidence) {
            for (std::size_t split = 0U; split < kEvaluationSplitCount;
                 ++split) {
                finalizeMetrics(
                    independent[origin][evidence][split],
                    report.independentMetrics[origin][evidence][split]);
            }
        }
    }
    for (std::size_t split = 0U; split < kEvaluationSplitCount; ++split) {
        finalizeMetrics(syntheticProxy[split],
                        report.syntheticProxyMetrics[split]);
    }
    return EvaluationStatus::Ok;
}

EvaluationStatus EvaluationRunner::writeSummaryCsvHeader(
    char* output, std::size_t capacity,
    std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    if (output == nullptr || capacity == 0U) {
        return EvaluationStatus::InvalidArgument;
    }
    CsvRowWriter writer(output, capacity, kSummaryColumnCount);
    if (!appendEvaluationHeader(writer, kSummaryHeader,
                                kSummaryColumnCount)) {
        return EvaluationStatus::CsvWriteError;
    }
    bytesWritten = writer.size();
    return EvaluationStatus::Ok;
}

EvaluationStatus EvaluationRunner::writeSummaryCsvRow(
    const EvaluationReport& report, DataOrigin origin,
    EvidenceClass evidence, DatasetSplit split, char* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    const std::size_t originId = originIndex(origin);
    const std::size_t evidenceId = evidenceIndex(evidence);
    const std::size_t splitId = splitIndex(split);
    if (output == nullptr || capacity == 0U ||
        originId >= kEvaluationOriginCount ||
        evidenceId >= kEvaluationEvidenceCount || !validSplit(split) ||
        originName(origin).empty() || evidenceName(evidence).empty()) {
        return EvaluationStatus::InvalidArgument;
    }
    const EvaluationBucket& bucket =
        report.buckets[originId][evidenceId][splitId];
    const bool synthetic = origin == DataOrigin::Synthetic;
    const RegressionMetrics& metrics =
        synthetic ? report.syntheticProxyMetrics[splitId]
                  : report.independentMetrics[originId][evidenceId][splitId];
    CsvRowWriter writer(output, capacity, kSummaryColumnCount);
    CsvWriteStatus appendStatus = writer.appendUnsigned(1U);
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendField(
            report.targetContractDefined
                ? std::string_view(report.targetContractId,
                                   report.targetContractIdSize)
                : std::string_view{});
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(
            report.targetContractDefined ? report.targetContractVersion : 0U);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendField(originName(origin));
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendField(evidenceName(evidence));
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendField(splitName(split));
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(bucket.windowCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(bucket.featureEligibleCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(bucket.featureRejectedCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(bucket.eligibleLabelCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(bucket.excludedLabelCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(bucket.missingPredictionCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus =
            writer.appendField(synthetic ? "SYNTHETIC_PROXY" : "INDEPENDENT");
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(metrics.eligibleLabelCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(metrics.predictionPairCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendOptionalNumber(metrics.targetMinimum,
                                                   metrics.targetRangeValid);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendOptionalNumber(metrics.targetMaximum,
                                                   metrics.targetRangeValid);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus =
            writer.appendOptionalNumber(metrics.meanAbsoluteError,
                                        metrics.meanAbsoluteErrorValid);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus =
            writer.appendOptionalNumber(metrics.rootMeanSquaredError,
                                        metrics.rootMeanSquaredErrorValid);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus =
            writer.appendOptionalNumber(metrics.rSquared,
                                        metrics.rSquaredValid);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendBoolean(report.regressionMetricsEnabled);
    }
    if (appendStatus != CsvWriteStatus::Ok ||
        writer.finish() != CsvWriteStatus::Ok) {
        return EvaluationStatus::CsvWriteError;
    }
    bytesWritten = writer.size();
    return EvaluationStatus::Ok;
}

EvaluationStatus EvaluationRunner::writeFeatureCsvHeader(
    char* output, std::size_t capacity,
    std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    if (output == nullptr || capacity == 0U) {
        return EvaluationStatus::InvalidArgument;
    }
    CsvRowWriter writer(output, capacity, kFeatureColumnCount);
    if (!appendEvaluationHeader(writer, kFeatureHeader,
                                kFeatureColumnCount)) {
        return EvaluationStatus::CsvWriteError;
    }
    bytesWritten = writer.size();
    return EvaluationStatus::Ok;
}

EvaluationStatus EvaluationRunner::writeFeatureCsvRow(
    std::size_t featureIndex,
    const EvaluationFeatureDistribution& distribution, char* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    if (output == nullptr || capacity == 0U ||
        featureIndex >= kEvaluationFeatureCount) {
        return EvaluationStatus::InvalidArgument;
    }
    CsvRowWriter writer(output, capacity, kFeatureColumnCount);
    CsvWriteStatus appendStatus = writer.appendUnsigned(1U);
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(featureIndex);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus =
            writer.appendUnsigned(distribution.observedWindowCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(distribution.missingCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(distribution.nonFiniteCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendUnsigned(distribution.finiteCount);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendOptionalNumber(
            distribution.minimum, distribution.statisticsValid);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendOptionalNumber(
            distribution.maximum, distribution.statisticsValid);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendOptionalNumber(
            distribution.mean, distribution.statisticsValid);
    }
    if (appendStatus == CsvWriteStatus::Ok) {
        appendStatus = writer.appendOptionalNumber(
            distribution.variance, distribution.statisticsValid);
    }
    if (appendStatus != CsvWriteStatus::Ok ||
        writer.finish() != CsvWriteStatus::Ok) {
        return EvaluationStatus::CsvWriteError;
    }
    bytesWritten = writer.size();
    return EvaluationStatus::Ok;
}

}  // namespace project::dataset

#endif
