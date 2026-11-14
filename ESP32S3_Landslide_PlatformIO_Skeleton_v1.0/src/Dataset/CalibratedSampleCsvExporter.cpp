#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/CalibratedSampleCsvExporter.h"

#include <cmath>
#include <cstring>

namespace project::dataset {
namespace {

using project::models::DataQualityState;

constexpr std::array<std::string_view, 32U> kCalibratedSampleCsvColumns{{
    "dataset_id",
    "site_id",
    "device_id",
    "sensor_set_id",
    "session_id",
    "boot_id",
    "sequence_no",
    "timestamp_ms",
    "sensor_id",
    "channel_id",
    "raw_value",
    "raw_quality",
    "calibrated_value",
    "calibrated_quality",
    "calibrated_unit",
    "calibration_status",
    "calibration_id",
    "calibration_version",
    "calibration_sha256",
    "calibration_sensor_id",
    "calibration_channel_id",
    "valid_minimum",
    "valid_maximum",
    "calibration_approved",
    "approval_evidence_accepted",
    "evidence_class",
    "approval_evidence_reference",
    "transform_id",
    "transform_version",
    "transform_status",
    "rejection_reason",
    "test_only",
}};

constexpr CsvArtifactSchema kCalibratedSampleArtifactSchema{
    "calibrated_samples.csv", kCalibratedSampleCsvSchemaVersion,
    kCalibratedSampleCsvColumns.data(),
    kCalibratedSampleCsvColumns.size()};

bool validText(std::string_view text) noexcept {
    return !text.empty() && text.find('\0') == std::string_view::npos;
}

bool validIdentity(std::string_view text) noexcept {
    return validText(text) && text != "UNKNOWN" &&
           text != "NOT_APPLICABLE";
}

bool isSha256(std::string_view text) noexcept {
    if (text.size() != 64U) {
        return false;
    }
    for (const char digit : text) {
        if (!((digit >= '0' && digit <= '9') ||
              (digit >= 'a' && digit <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::string_view qualityName(DataQualityState quality) noexcept {
    switch (quality) {
        case DataQualityState::MISSING: return "MISSING";
        case DataQualityState::VALID: return "VALID";
        case DataQualityState::STALE: return "STALE";
        case DataQualityState::OUT_OF_RANGE: return "OUT_OF_RANGE";
        case DataQualityState::NON_FINITE: return "NON_FINITE";
        case DataQualityState::CALIBRATION_INVALID:
            return "CALIBRATION_INVALID";
        case DataQualityState::OUT_OF_ORDER: return "OUT_OF_ORDER";
        case DataQualityState::GAP_DETECTED: return "GAP_DETECTED";
        case DataQualityState::NOT_READY: return "NOT_READY";
        case DataQualityState::SENSOR_ERROR: return "SENSOR_ERROR";
    }
    return {};
}

std::string_view evidenceName(EvidenceClass evidence) noexcept {
    switch (evidence) {
        case EvidenceClass::NotApplicable: return "NOT_APPLICABLE";
        case EvidenceClass::GT1: return "GT1";
        case EvidenceClass::GT2: return "GT2";
        case EvidenceClass::GT3: return "GT3";
        case EvidenceClass::GT4: return "GT4";
        case EvidenceClass::GT5: return "GT5";
    }
    return {};
}

std::string_view originName(DataOrigin origin) noexcept {
    switch (origin) {
        case DataOrigin::Unspecified: return {};
        case DataOrigin::Synthetic: return "SYNTHETIC";
        case DataOrigin::Bench: return "BENCH";
        case DataOrigin::Field: return "FIELD";
        case DataOrigin::Reference: return "REFERENCE";
        case DataOrigin::Replay: return "REPLAY";
    }
    return {};
}

bool validConfig(const CalibratedSampleCsvConfig& config) noexcept {
    if (!validIdentity(config.datasetId) || !validIdentity(config.siteId) ||
        !validIdentity(config.deviceId) ||
        !validIdentity(config.sensorSetId) ||
        !validIdentity(config.sessionId) || !config.provenance.isValid() ||
        originName(config.provenance.origin).empty() ||
        evidenceName(config.provenance.evidenceClass).empty()) {
        return false;
    }

    const CalibrationArtifactDescriptor& calibration = config.calibration;
    if (config.mode == CalibratedSampleMode::RawOnly) {
        return calibration.calibrationId.empty() &&
               calibration.version == 0U && calibration.sha256.empty() &&
               calibration.sensorId.empty() &&
               calibration.channelId.empty() && calibration.unit.empty() &&
               calibration.validMinimum == 0.0 &&
               calibration.validMaximum == 0.0 &&
               calibration.transformId.empty() &&
               calibration.transformVersion == 0U &&
               !calibration.approved &&
               !calibration.approvalEvidenceAccepted &&
               calibration.evidenceClass == EvidenceClass::NotApplicable &&
               calibration.approvalEvidenceReference.empty() &&
               !calibration.testOnly;
    }
    if (config.mode != CalibratedSampleMode::CalibrationArtifact) {
        return false;
    }
    return validIdentity(calibration.calibrationId) &&
           calibration.version != 0U && isSha256(calibration.sha256) &&
           validIdentity(calibration.sensorId) &&
           validIdentity(calibration.channelId) &&
           validIdentity(calibration.unit) &&
           std::isfinite(calibration.validMinimum) &&
           std::isfinite(calibration.validMaximum) &&
           calibration.validMinimum <= calibration.validMaximum &&
           validIdentity(calibration.transformId) &&
           calibration.transformVersion != 0U && calibration.approved &&
           calibration.approvalEvidenceAccepted &&
           !evidenceName(calibration.evidenceClass).empty() &&
           calibration.evidenceClass != EvidenceClass::NotApplicable &&
           (calibration.testOnly ||
            calibration.evidenceClass != EvidenceClass::GT5) &&
           validIdentity(calibration.approvalEvidenceReference);
}

bool validRecord(const CalibratedSampleCsvConfig& config,
                 const CalibratedSampleCsvRecord& record) noexcept {
    if (record.bootId == 0U || !validText(record.sensorId) ||
        !validText(record.channelId) ||
        qualityName(record.rawQuality).empty() ||
        qualityName(record.calibratedQuality).empty()) {
        return false;
    }
    if (record.rawQuality == DataQualityState::VALID &&
        !std::isfinite(record.rawValue)) {
        return false;
    }
    if (config.mode == CalibratedSampleMode::RawOnly) {
        return record.calibratedQuality != DataQualityState::VALID &&
               record.appliedCalibrationId.empty() &&
               record.appliedCalibrationVersion == 0U &&
               record.appliedCalibrationSha256.empty();
    }

    const CalibrationArtifactDescriptor& calibration = config.calibration;
    return record.sensorId == calibration.sensorId &&
           record.channelId == calibration.channelId &&
           record.appliedCalibrationId == calibration.calibrationId &&
           record.appliedCalibrationVersion == calibration.version &&
           record.appliedCalibrationSha256 == calibration.sha256;
}

CalibratedSampleCsvExportStatus mapArtifactStatus(
    CsvArtifactExportStatus status) noexcept {
    switch (status) {
        case CsvArtifactExportStatus::Ok:
            return CalibratedSampleCsvExportStatus::Ok;
        case CsvArtifactExportStatus::InvalidArgument:
            return CalibratedSampleCsvExportStatus::InvalidArgument;
        case CsvArtifactExportStatus::InvalidSchema:
            return CalibratedSampleCsvExportStatus::InvalidCalibration;
        case CsvArtifactExportStatus::InvalidState:
            return CalibratedSampleCsvExportStatus::InvalidState;
        case CsvArtifactExportStatus::CsvWriteError:
            return CalibratedSampleCsvExportStatus::CsvWriteError;
        case CsvArtifactExportStatus::CapacityExceeded:
            return CalibratedSampleCsvExportStatus::CapacityExceeded;
        case CsvArtifactExportStatus::HashError:
            return CalibratedSampleCsvExportStatus::HashError;
        case CsvArtifactExportStatus::RowCountOverflow:
            return CalibratedSampleCsvExportStatus::RowCountOverflow;
    }
    return CalibratedSampleCsvExportStatus::CsvWriteError;
}

}  // namespace

CalibratedSampleCsvExporter::CalibratedSampleCsvExporter(
    char* output, std::size_t outputCapacity,
    const CalibratedSampleCsvConfig& config) noexcept
    : config_(config),
      artifactWriter_(output, outputCapacity, rowBuffer_.data(),
                      rowBuffer_.size(), kCalibratedSampleArtifactSchema) {}

CalibratedSampleCsvExportStatus CalibratedSampleCsvExporter::begin() noexcept {
    if (!validConfig(config_)) {
        return CalibratedSampleCsvExportStatus::InvalidCalibration;
    }
    return mapArtifactStatus(artifactWriter_.begin());
}

CalibratedSampleCsvExportStatus CalibratedSampleCsvExporter::append(
    const CalibratedSampleCsvRecord& record) noexcept {
    if (!validRecord(config_, record)) {
        if (config_.mode == CalibratedSampleMode::CalibrationArtifact &&
            (record.sensorId != config_.calibration.sensorId ||
             record.channelId != config_.calibration.channelId ||
             record.appliedCalibrationId != config_.calibration.calibrationId ||
             record.appliedCalibrationVersion != config_.calibration.version ||
             record.appliedCalibrationSha256 != config_.calibration.sha256)) {
            return CalibratedSampleCsvExportStatus::CalibrationMismatch;
        }
        return CalibratedSampleCsvExportStatus::InvalidRecord;
    }

    const bool rawValid = record.rawQuality == DataQualityState::VALID;
    const bool calibrationConfigured =
        config_.mode == CalibratedSampleMode::CalibrationArtifact;
    DataQualityState calibratedQuality = record.calibratedQuality;
    bool calibratedValueValid =
        calibrationConfigured && rawValid &&
        calibratedQuality == DataQualityState::VALID;
    std::string_view calibrationStatus = "RAW_ONLY";
    std::string_view transformStatus = "NOT_APPLIED";
    std::string_view rejectionReason = "CALIBRATION_NOT_CONFIGURED";

    if (calibrationConfigured) {
        const CalibrationArtifactDescriptor& calibration = config_.calibration;
        calibrationStatus = calibration.testOnly
                                ? "TEST_ONLY_CALIBRATION"
                                : "CALIBRATED";
        transformStatus = "REJECTED";
        rejectionReason = rawValid ? "CALIBRATED_QUALITY_NOT_VALID"
                                   : "RAW_VALUE_NOT_VALID";
        if (!rawValid &&
            calibratedQuality == DataQualityState::VALID) {
            calibratedQuality = DataQualityState::CALIBRATION_INVALID;
        }
        if (calibratedQuality == DataQualityState::VALID && rawValid) {
            if (!std::isfinite(record.calibratedValue)) {
                calibratedValueValid = false;
                calibratedQuality = DataQualityState::NON_FINITE;
                rejectionReason = "CALIBRATED_VALUE_NON_FINITE";
            } else if (record.calibratedValue < calibration.validMinimum ||
                       record.calibratedValue > calibration.validMaximum) {
                calibratedValueValid = false;
                calibratedQuality = DataQualityState::OUT_OF_RANGE;
                rejectionReason = "CALIBRATED_VALUE_OUT_OF_RANGE";
            } else {
                calibratedValueValid = true;
                transformStatus =
                    calibration.testOnly ? "APPLIED_TEST_ONLY" : "APPLIED";
                rejectionReason = {};
            }
        }
    }

    const CalibrationArtifactDescriptor& calibration = config_.calibration;
    const auto appendStatus = artifactWriter_.appendRow(
        [this, &record, &calibration, rawValid, calibratedValueValid,
         calibratedQuality, calibrationConfigured, calibrationStatus,
         transformStatus, rejectionReason](CsvRowWriter& writer) noexcept {
            CsvWriteStatus status = writer.appendField(config_.datasetId);
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(config_.siteId);
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(config_.deviceId);
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(config_.sensorSetId);
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(config_.sessionId);
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
                status = writer.appendField(record.sensorId);
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(record.channelId);
            }
            if (status == CsvWriteStatus::Ok) {
                status = rawValid ? writer.appendNumber(record.rawValue)
                                  : writer.appendEmptyField();
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(qualityName(record.rawQuality));
            }
            if (status == CsvWriteStatus::Ok) {
                status = calibratedValueValid
                             ? writer.appendNumber(record.calibratedValue)
                             : writer.appendEmptyField();
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(qualityName(calibratedQuality));
            }
            if (status == CsvWriteStatus::Ok) {
                status = calibrationConfigured
                             ? writer.appendField(calibration.unit)
                             : writer.appendEmptyField();
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(calibrationStatus);
            }
            if (status == CsvWriteStatus::Ok) {
                status = calibrationConfigured
                             ? writer.appendField(calibration.calibrationId)
                             : writer.appendField("UNKNOWN");
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendUnsigned(
                    calibrationConfigured ? calibration.version : 0U);
            }
            if (status == CsvWriteStatus::Ok) {
                status = calibrationConfigured
                             ? writer.appendField(calibration.sha256)
                             : writer.appendField("UNKNOWN");
            }
            if (status == CsvWriteStatus::Ok) {
                status = calibrationConfigured
                             ? writer.appendField(calibration.sensorId)
                             : writer.appendField("UNKNOWN");
            }
            if (status == CsvWriteStatus::Ok) {
                status = calibrationConfigured
                             ? writer.appendField(calibration.channelId)
                             : writer.appendField("UNKNOWN");
            }
            if (status == CsvWriteStatus::Ok) {
                status = calibrationConfigured
                             ? writer.appendNumber(calibration.validMinimum)
                             : writer.appendEmptyField();
            }
            if (status == CsvWriteStatus::Ok) {
                status = calibrationConfigured
                             ? writer.appendNumber(calibration.validMaximum)
                             : writer.appendEmptyField();
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendBoolean(
                    calibrationConfigured && calibration.approved);
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendBoolean(
                    calibrationConfigured &&
                    calibration.approvalEvidenceAccepted);
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(
                    calibrationConfigured
                        ? evidenceName(calibration.evidenceClass)
                        : "NOT_APPLICABLE");
            }
            if (status == CsvWriteStatus::Ok) {
                status = calibrationConfigured
                             ? writer.appendField(
                                   calibration.approvalEvidenceReference)
                             : writer.appendEmptyField();
            }
            if (status == CsvWriteStatus::Ok) {
                status = calibrationConfigured
                             ? writer.appendField(calibration.transformId)
                             : writer.appendEmptyField();
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendUnsigned(
                    calibrationConfigured
                        ? calibration.transformVersion
                        : 0U);
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(transformStatus);
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendField(rejectionReason);
            }
            if (status == CsvWriteStatus::Ok) {
                status = writer.appendBoolean(
                    calibrationConfigured && calibration.testOnly);
            }
            return status;
        });
    return mapArtifactStatus(appendStatus);
}

CalibratedSampleCsvExportStatus CalibratedSampleCsvExporter::finish(
    CsvArtifactExportReport& report) noexcept {
    return mapArtifactStatus(artifactWriter_.finish(report));
}

}  // namespace project::dataset

#endif
