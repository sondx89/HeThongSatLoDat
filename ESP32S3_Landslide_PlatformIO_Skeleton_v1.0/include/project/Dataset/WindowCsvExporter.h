#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "WindowCsvExporter is available only to host data tools"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Dataset/CsvArtifactExporter.h"
#include "Dataset/DataProvenance.h"

namespace project::dataset {

inline constexpr std::uint32_t kWindowCsvSchemaVersion = 1U;

inline constexpr std::array<std::string_view, 50U> kWindowCsvColumns{{
    "dataset_id",
    "window_id",
    "site_id",
    "device_id",
    "sensor_set_id",
    "session_id",
    "start_boot_id",
    "end_boot_id",
    "start_sequence",
    "end_sequence",
    "start_timestamp_ms",
    "end_timestamp_ms",
    "window_length_samples",
    "stride_samples",
    "expected_samples",
    "observed_samples",
    "gap_count",
    "start_config_id",
    "end_config_id",
    "start_calibration_id",
    "end_calibration_id",
    "start_feature_schema_id",
    "end_feature_schema_id",
    "start_feature_schema_version",
    "end_feature_schema_version",
    "start_feature_schema_sha256",
    "end_feature_schema_sha256",
    "missing_required_channel_count",
    "stale_required_channel_count",
    "calibration_invalid_required_channel_count",
    "data_origin",
    "evidence_class",
    "feature_contract_approved",
    "calibration_approved",
    "window_eligible",
    "window_rejection_reason",
    "training_eligible",
    "training_rejection_reason",
    "validation_eligible",
    "validation_rejection_reason",
    "test_eligible",
    "test_rejection_reason",
    "replay_eligible",
    "replay_rejection_reason",
    "safety_analysis_eligible",
    "safety_analysis_rejection_reason",
    "annotation_refs",
    "split_group_id",
    "split_name",
    "split_policy_version",
}};

struct WindowBoundaryIdentity {
    std::uint64_t bootId{0U};
    std::string_view configId{};
    std::string_view calibrationId{};
    std::string_view featureSchemaId{};
    std::uint32_t featureSchemaVersion{0U};
    std::string_view featureSchemaSha256{};
};

struct WindowEligibilityDecision {
    bool eligible{false};
    std::string_view rejectionReason{};
};

struct WindowCsvRecord {
    std::string_view datasetId{};
    std::string_view windowId{};
    std::string_view siteId{};
    std::string_view deviceId{};
    std::string_view sensorSetId{};
    std::string_view sessionId{};
    WindowBoundaryIdentity start{};
    WindowBoundaryIdentity end{};
    std::uint64_t startSequence{0U};
    std::uint64_t endSequence{0U};
    std::uint64_t startTimestampMs{0U};
    std::uint64_t endTimestampMs{0U};
    std::uint32_t windowLengthSamples{0U};
    std::uint32_t strideSamples{0U};
    std::uint32_t expectedSamples{0U};
    std::uint32_t observedSamples{0U};
    std::uint32_t gapCount{0U};
    std::uint32_t missingRequiredChannelCount{0U};
    std::uint32_t staleRequiredChannelCount{0U};
    std::uint32_t calibrationInvalidRequiredChannelCount{0U};
    DataProvenance provenance{};
    bool featureContractApproved{false};
    bool calibrationApproved{false};
    WindowEligibilityDecision window{};
    WindowEligibilityDecision training{};
    WindowEligibilityDecision validation{};
    WindowEligibilityDecision test{};
    WindowEligibilityDecision replay{};
    WindowEligibilityDecision safetyAnalysis{};
    std::string_view annotationRefs{};
    std::string_view splitGroupId{};
    std::string_view splitName{};
    std::string_view splitPolicyVersion{};
};

enum class WindowCsvExportStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    InvalidRecord,
    InvalidState,
    CsvWriteError,
    CapacityExceeded,
    HashError,
    RowCountOverflow
};

class WindowCsvExporter final {
public:
    WindowCsvExporter(char* output, std::size_t outputCapacity,
                      char* rowBuffer, std::size_t rowCapacity) noexcept;

    WindowCsvExportStatus begin() noexcept;
    WindowCsvExportStatus append(const WindowCsvRecord& record) noexcept;
    WindowCsvExportStatus finish(CsvArtifactExportReport& report) noexcept;

    std::size_t bytesWritten() const noexcept {
        return artifactWriter_.bytesWritten();
    }
    std::uint64_t rowsWritten() const noexcept {
        return artifactWriter_.rowsWritten();
    }

private:
    CsvArtifactWriter artifactWriter_;
};

}  // namespace project::dataset
