#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "DsdRawSampleCsvExporter is available only to host data tools"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Dataset/CsvArtifactExporter.h"
#include "Dataset/DatasetValidator.h"
#include "Models/DataContractTypes.h"

namespace project::dataset {

struct DsdRawSampleExportConfig {
    std::string_view datasetId{};
    DataProvenance provenance{};
    std::array<bool, kDsdRawChannelCount> channelPresence{};
    bool channelPresenceConfigured{false};
};

struct DsdRawSampleExportRecord {
    std::string_view siteId{};
    std::string_view deviceId{};
    std::string_view sensorSetId{};
    std::string_view sessionId{};
    std::uint64_t bootId{0U};
    std::uint64_t sequenceNo{0U};
    std::uint64_t timestampMs{0U};
    std::string_view utcTime{};
    project::models::TimeStatus timeStatus{
        project::models::TimeStatus::Unknown};
    std::string_view firmwareVersion{};
    std::string_view configVersion{};
    std::string_view calibrationId{};
    std::uint32_t sensorStatusBits{0U};
    std::array<std::int64_t, kDsdRawChannelCount> rawValues{};
    std::array<bool, kDsdRawChannelCount> channelValid{};
    std::array<project::models::DataQualityState, kDsdRawChannelCount>
        channelQuality{};
    std::uint32_t validMask{0U};
    std::uint32_t qualityFlags{0U};
    std::string_view eventId{};
    std::string_view annotationId{};
};

enum class DsdRawSampleExportStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    InvalidRecord,
    InvalidState,
    CsvWriteError,
    CapacityExceeded,
    HashError
};

class DsdRawSampleCsvExporter final {
public:
    DsdRawSampleCsvExporter(
        char* output, std::size_t capacity,
        const DsdRawSampleExportConfig& config) noexcept;

    DsdRawSampleExportStatus begin() noexcept;
    DsdRawSampleExportStatus append(
        const DsdRawSampleExportRecord& record) noexcept;
    DsdRawSampleExportStatus finish(
        RawDatasetManifest& manifest) noexcept;

    std::size_t bytesWritten() const noexcept {
        return artifactWriter_.bytesWritten();
    }
    std::uint64_t rowsWritten() const noexcept {
        return artifactWriter_.rowsWritten();
    }

private:
    const DsdRawSampleExportConfig config_;
    std::array<char, kMaxRawCsvRecordBytes> rowBuffer_{};
    CsvArtifactWriter artifactWriter_;
    bool begun_{false};
    bool finished_{false};
    bool failed_{false};
};

}  // namespace project::dataset
