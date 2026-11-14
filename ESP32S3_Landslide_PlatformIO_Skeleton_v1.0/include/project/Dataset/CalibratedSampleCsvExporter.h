#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "CalibratedSampleCsvExporter is available only to host data tools"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Dataset/CsvArtifactExporter.h"
#include "Dataset/DataProvenance.h"
#include "Models/DataContractTypes.h"

namespace project::dataset {

inline constexpr std::uint32_t kCalibratedSampleCsvSchemaVersion = 1U;

enum class CalibratedSampleMode : std::uint8_t {
    RawOnly,
    CalibrationArtifact
};

struct CalibrationArtifactDescriptor {
    std::string_view calibrationId{};
    std::uint32_t version{0U};
    std::string_view sha256{};
    std::string_view sensorId{};
    std::string_view channelId{};
    std::string_view unit{};
    double validMinimum{0.0};
    double validMaximum{0.0};
    std::string_view transformId{};
    std::uint32_t transformVersion{0U};
    bool approved{false};
    bool approvalEvidenceAccepted{false};
    EvidenceClass evidenceClass{EvidenceClass::NotApplicable};
    std::string_view approvalEvidenceReference{};
    bool testOnly{false};
};

struct CalibratedSampleCsvConfig {
    std::string_view datasetId{};
    std::string_view siteId{};
    std::string_view deviceId{};
    std::string_view sensorSetId{};
    std::string_view sessionId{};
    DataProvenance provenance{};
    CalibratedSampleMode mode{CalibratedSampleMode::RawOnly};
    CalibrationArtifactDescriptor calibration{};
};

struct CalibratedSampleCsvRecord {
    std::uint64_t bootId{0U};
    std::uint64_t sequenceNo{0U};
    std::uint64_t timestampMs{0U};
    std::string_view sensorId{};
    std::string_view channelId{};
    double rawValue{0.0};
    project::models::DataQualityState rawQuality{
        project::models::DataQualityState::MISSING};
    double calibratedValue{0.0};
    project::models::DataQualityState calibratedQuality{
        project::models::DataQualityState::MISSING};
    std::string_view appliedCalibrationId{};
    std::uint32_t appliedCalibrationVersion{0U};
    std::string_view appliedCalibrationSha256{};
};

enum class CalibratedSampleCsvExportStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    InvalidCalibration,
    InvalidRecord,
    InvalidState,
    CalibrationMismatch,
    CsvWriteError,
    CapacityExceeded,
    HashError,
    RowCountOverflow
};

class CalibratedSampleCsvExporter final {
public:
    static constexpr std::size_t kRowBufferCapacity = 4096U;

    CalibratedSampleCsvExporter(
        char* output, std::size_t outputCapacity,
        const CalibratedSampleCsvConfig& config) noexcept;

    CalibratedSampleCsvExportStatus begin() noexcept;
    CalibratedSampleCsvExportStatus append(
        const CalibratedSampleCsvRecord& record) noexcept;
    CalibratedSampleCsvExportStatus finish(
        CsvArtifactExportReport& report) noexcept;

    std::size_t bytesWritten() const noexcept {
        return artifactWriter_.bytesWritten();
    }
    std::uint64_t rowsWritten() const noexcept {
        return artifactWriter_.rowsWritten();
    }

private:
    const CalibratedSampleCsvConfig config_;
    std::array<char, kRowBufferCapacity> rowBuffer_{};
    CsvArtifactWriter artifactWriter_;
};

}  // namespace project::dataset
