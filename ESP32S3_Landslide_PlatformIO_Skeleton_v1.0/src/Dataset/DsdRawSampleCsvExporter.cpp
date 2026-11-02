#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/DsdRawSampleCsvExporter.h"

#include <cstring>
#include <limits>

#include "project/Dataset/CsvSchemas.h"
#include "project/Dataset/CsvWriter.h"
#include "project/Dataset/DsdRawSampleCsvParser.h"

namespace project::dataset {
namespace {

using project::models::DataQualityState;
using project::models::TimeStatus;

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

}  // namespace

DsdRawSampleCsvExporter::DsdRawSampleCsvExporter(
    char* output, std::size_t capacity,
    const DsdRawSampleExportConfig& config) noexcept
    : output_(output), capacity_(capacity), config_(config) {}

DsdRawSampleExportStatus DsdRawSampleCsvExporter::appendBytes(
    const char* bytes, std::size_t size) noexcept {
    if (bytesWritten_ > capacity_ ||
        size > capacity_ - bytesWritten_) {
        failed_ = true;
        return DsdRawSampleExportStatus::CapacityExceeded;
    }
    if (!sha256_.update(reinterpret_cast<const std::uint8_t*>(bytes), size)) {
        failed_ = true;
        return DsdRawSampleExportStatus::HashError;
    }
    std::memcpy(output_ + bytesWritten_, bytes, size);
    bytesWritten_ += size;
    return DsdRawSampleExportStatus::Ok;
}

DsdRawSampleExportStatus DsdRawSampleCsvExporter::begin() noexcept {
    if (begun_ || finished_ || failed_) {
        return DsdRawSampleExportStatus::InvalidState;
    }
    if (output_ == nullptr || capacity_ == 0U || !validConfig(config_)) {
        failed_ = true;
        return DsdRawSampleExportStatus::InvalidArgument;
    }

    char header[1024U]{};
    std::size_t headerSize = 0U;
    if (writeDsdV1_2RawSampleCoreHeader(
            header, sizeof(header), headerSize) != CsvWriteStatus::Ok) {
        failed_ = true;
        return DsdRawSampleExportStatus::CsvWriteError;
    }
    const DsdRawSampleExportStatus status = appendBytes(header, headerSize);
    if (status != DsdRawSampleExportStatus::Ok) {
        return status;
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
        rowsWritten_ == std::numeric_limits<std::uint64_t>::max()) {
        failed_ = true;
        return DsdRawSampleExportStatus::InvalidRecord;
    }

    std::array<char, kMaxRawCsvRecordBytes> row{};
    CsvRowWriter writer(row.data(), row.size(),
                        kDsdV1_2RawSampleCoreFields.size());
    if (writer.appendField(config_.datasetId) != CsvWriteStatus::Ok ||
        appendRecordFields(writer, record) != CsvWriteStatus::Ok ||
        writer.finish() != CsvWriteStatus::Ok) {
        failed_ = true;
        return DsdRawSampleExportStatus::CsvWriteError;
    }
    const DsdRawSampleExportStatus status =
        appendBytes(writer.data(), writer.size());
    if (status != DsdRawSampleExportStatus::Ok) {
        return status;
    }
    ++rowsWritten_;
    return DsdRawSampleExportStatus::Ok;
}

DsdRawSampleExportStatus DsdRawSampleCsvExporter::finish(
    RawDatasetManifest& manifest) noexcept {
    manifest = {};
    if (!begun_ || finished_ || failed_) {
        return DsdRawSampleExportStatus::InvalidState;
    }
    std::array<std::uint8_t, Sha256::kDigestSize> digest{};
    if (!sha256_.finish(digest)) {
        failed_ = true;
        return DsdRawSampleExportStatus::HashError;
    }
    manifest.datasetId = config_.datasetId;
    manifest.rawSchemaVersion = kRawSampleCoreSchemaVersion;
    manifest.expectedRows = rowsWritten_;
    manifest.expectedBytes = static_cast<std::uint64_t>(bytesWritten_);
    manifest.expectedSha256 = digest;
    manifest.provenance = config_.provenance;
    manifest.channelPresence = config_.channelPresence;
    manifest.channelPresenceConfigured = true;
    finished_ = true;
    return DsdRawSampleExportStatus::Ok;
}

}  // namespace project::dataset

#endif
