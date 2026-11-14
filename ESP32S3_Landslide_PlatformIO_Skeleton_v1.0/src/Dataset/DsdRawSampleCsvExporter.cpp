#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/DsdRawSampleCsvExporter.h"

#include <limits>

#include "project/Dataset/CsvSchemas.h"
#include "project/Dataset/DsdRawSampleCsvParser.h"

namespace project::dataset {
namespace {

using project::models::DataQualityState;
using project::models::TimeStatus;

constexpr CsvArtifactSchema kRawSampleArtifactSchema{
    "raw_samples.csv",
    kRawSampleCoreSchemaVersion,
    kDsdV1_2RawSampleCoreFields.data(),
    kDsdV1_2RawSampleCoreFields.size()};

std::string_view qualityName(DataQualityState quality) noexcept {
    switch (quality) {
        case DataQualityState::VALID:
            return "VALID";
        case DataQualityState::MISSING:
            return "MISSING";
        case DataQualityState::STALE:
            return "STALE";
        case DataQualityState::OUT_OF_RANGE:
            return "OUT_OF_RANGE";
        case DataQualityState::NON_FINITE:
            return "NON_FINITE";
        case DataQualityState::CALIBRATION_INVALID:
            return "CALIBRATION_INVALID";
        case DataQualityState::OUT_OF_ORDER:
            return "OUT_OF_ORDER";
        case DataQualityState::GAP_DETECTED:
            return "GAP_DETECTED";
        case DataQualityState::NOT_READY:
            return "NOT_READY";
        case DataQualityState::SENSOR_ERROR:
            return "SENSOR_ERROR";
    }
    return {};
}

std::string_view timeStatusName(TimeStatus status) noexcept {
    switch (status) {
        case TimeStatus::Unknown:
            return "UNKNOWN";
        case TimeStatus::Unsynced:
            return "UNSYNCED";
        case TimeStatus::Synced:
            return "SYNCED";
    }
    return {};
}

bool validConfig(const DsdRawSampleExportConfig& config) noexcept {
    return !config.datasetId.empty() && config.provenance.isValid() &&
           config.channelPresenceConfigured;
}

bool validRecord(const DsdRawSampleExportConfig& config,
                 const DsdRawSampleExportRecord& record) noexcept {
    if (record.siteId.empty() || record.deviceId.empty() ||
        record.sensorSetId.empty() || record.sessionId.empty() ||
        timeStatusName(record.timeStatus).empty()) {
        return false;
    }
    for (std::size_t channel = 0U; channel < kDsdRawChannelCount; ++channel) {
        const DataQualityState quality = record.channelQuality[channel];
        if (qualityName(quality).empty() ||
            record.channelValid[channel] !=
                (quality == DataQualityState::VALID)) {
            return false;
        }
        if (!config.channelPresence[channel] &&
            (record.channelValid[channel] ||
             quality != DataQualityState::MISSING)) {
            return false;
        }
    }
    return true;
}

CsvWriteStatus appendRecordFields(
    CsvRowWriter& writer, const DsdRawSampleExportRecord& record) noexcept {
    const auto append = [&writer](std::string_view value) {
        return writer.appendField(value);
    };
    CsvWriteStatus status = append(record.siteId);
    if (status == CsvWriteStatus::Ok) {
        status = append(record.deviceId);
    }
    if (status == CsvWriteStatus::Ok) {
        status = append(record.sensorSetId);
    }
    if (status == CsvWriteStatus::Ok) {
        status = append(record.sessionId);
    }
    if (status == CsvWriteStatus::Ok) {
        status = writer.appendUnsigned(record.bootId);
    }
    if (status == CsvWriteStatus::Ok) {
        status = writer.appendUnsigned(record.sequenceNo);
    }
    if (status == CsvWriteStatus::Ok) {
        status = writer.appendUnsigned(record.timestampMs);
    }
    if (status == CsvWriteStatus::Ok) {
        status = record.utcTime.empty()
                     ? writer.appendEmptyField()
                     : append(record.utcTime);
    }
    if (status == CsvWriteStatus::Ok) {
        status = append(timeStatusName(record.timeStatus));
    }
    if (status == CsvWriteStatus::Ok) {
        status = append(record.firmwareVersion);
    }
    if (status == CsvWriteStatus::Ok) {
        status = append(record.configVersion);
    }
    if (status == CsvWriteStatus::Ok) {
        status = append(record.calibrationId);
    }
    if (status == CsvWriteStatus::Ok) {
        status = writer.appendUnsigned(record.sensorStatusBits);
    }
    for (std::size_t channel = 0U;
         status == CsvWriteStatus::Ok && channel < kDsdRawChannelCount;
         ++channel) {
        status = record.channelValid[channel]
                     ? writer.appendSigned(record.rawValues[channel])
                     : writer.appendEmptyField();
    }
    for (std::size_t channel = 0U;
         status == CsvWriteStatus::Ok && channel < kDsdRawChannelCount;
         ++channel) {
        status = writer.appendBoolean(record.channelValid[channel]);
    }
    for (std::size_t channel = 0U;
         status == CsvWriteStatus::Ok && channel < kDsdRawChannelCount;
         ++channel) {
        status = append(qualityName(record.channelQuality[channel]));
    }
    if (status == CsvWriteStatus::Ok) {
        status = writer.appendUnsigned(record.validMask);
    }
    if (status == CsvWriteStatus::Ok) {
        status = writer.appendUnsigned(record.qualityFlags);
    }
    if (status == CsvWriteStatus::Ok) {
        status = append(record.eventId);
    }
    if (status == CsvWriteStatus::Ok) {
        status = append(record.annotationId);
    }
    return status;
}

DsdRawSampleExportStatus mapArtifactStatus(
    CsvArtifactExportStatus status) noexcept {
    switch (status) {
        case CsvArtifactExportStatus::Ok:
            return DsdRawSampleExportStatus::Ok;
        case CsvArtifactExportStatus::InvalidArgument:
            return DsdRawSampleExportStatus::InvalidArgument;
        case CsvArtifactExportStatus::InvalidState:
            return DsdRawSampleExportStatus::InvalidState;
        case CsvArtifactExportStatus::CapacityExceeded:
            return DsdRawSampleExportStatus::CapacityExceeded;
        case CsvArtifactExportStatus::HashError:
            return DsdRawSampleExportStatus::HashError;
        case CsvArtifactExportStatus::InvalidSchema:
        case CsvArtifactExportStatus::CsvWriteError:
            return DsdRawSampleExportStatus::CsvWriteError;
        case CsvArtifactExportStatus::RowCountOverflow:
            return DsdRawSampleExportStatus::InvalidRecord;
    }
    return DsdRawSampleExportStatus::CsvWriteError;
}

}  // namespace

DsdRawSampleCsvExporter::DsdRawSampleCsvExporter(
    char* output, std::size_t capacity,
    const DsdRawSampleExportConfig& config) noexcept
    : config_(config),
      artifactWriter_(output, capacity, rowBuffer_.data(), rowBuffer_.size(),
                      kRawSampleArtifactSchema) {}

DsdRawSampleExportStatus DsdRawSampleCsvExporter::begin() noexcept {
    if (begun_ || finished_ || failed_) {
        return DsdRawSampleExportStatus::InvalidState;
    }
    if (!validConfig(config_)) {
        failed_ = true;
        return DsdRawSampleExportStatus::InvalidArgument;
    }

    const CsvArtifactExportStatus status = artifactWriter_.begin();
    if (status != CsvArtifactExportStatus::Ok) {
        failed_ = true;
        return mapArtifactStatus(status);
    }
    begun_ = true;
    return DsdRawSampleExportStatus::Ok;
}

DsdRawSampleExportStatus DsdRawSampleCsvExporter::append(
    const DsdRawSampleExportRecord& record) noexcept {
    if (!begun_ || finished_ || failed_) {
        return DsdRawSampleExportStatus::InvalidState;
    }
    if (!validRecord(config_, record) ||
        artifactWriter_.rowsWritten() ==
            std::numeric_limits<std::uint64_t>::max()) {
        failed_ = true;
        return DsdRawSampleExportStatus::InvalidRecord;
    }

    const CsvArtifactExportStatus status = artifactWriter_.appendRow(
        [this, &record](CsvRowWriter& writer) noexcept {
            const CsvWriteStatus datasetStatus =
                writer.appendField(config_.datasetId);
            return datasetStatus == CsvWriteStatus::Ok
                       ? appendRecordFields(writer, record)
                       : datasetStatus;
        });
    if (status != CsvArtifactExportStatus::Ok) {
        failed_ = true;
        return mapArtifactStatus(status);
    }
    return DsdRawSampleExportStatus::Ok;
}

DsdRawSampleExportStatus DsdRawSampleCsvExporter::finish(
    RawDatasetManifest& manifest) noexcept {
    manifest = {};
    if (!begun_ || finished_ || failed_) {
        return DsdRawSampleExportStatus::InvalidState;
    }
    CsvArtifactExportReport report{};
    const CsvArtifactExportStatus status = artifactWriter_.finish(report);
    if (status != CsvArtifactExportStatus::Ok) {
        failed_ = true;
        return mapArtifactStatus(status);
    }
    manifest.datasetId = config_.datasetId;
    manifest.rawSchemaVersion = report.schemaVersion;
    manifest.expectedRows = report.rowCount;
    manifest.expectedBytes = report.byteCount;
    manifest.expectedSha256 = report.sha256;
    manifest.provenance = config_.provenance;
    manifest.channelPresence = config_.channelPresence;
    manifest.channelPresenceConfigured = true;
    finished_ = true;
    return DsdRawSampleExportStatus::Ok;
}

}  // namespace project::dataset

#endif
