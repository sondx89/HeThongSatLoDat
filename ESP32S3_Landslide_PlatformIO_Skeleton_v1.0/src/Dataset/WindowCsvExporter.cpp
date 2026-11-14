#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/WindowCsvExporter.h"

#include <limits>

namespace project::dataset {
namespace {

constexpr CsvArtifactSchema kWindowArtifactSchema{
    "windows.csv", kWindowCsvSchemaVersion, kWindowCsvColumns.data(),
    kWindowCsvColumns.size()};

constexpr std::string_view kBootBoundary = "BOOT_BOUNDARY";
constexpr std::string_view kConfigBoundary = "CONFIG_BOUNDARY";
constexpr std::string_view kCalibrationBoundary = "CALIBRATION_BOUNDARY";
constexpr std::string_view kFeatureSchemaBoundary = "FEATURE_SCHEMA_BOUNDARY";
constexpr std::string_view kSequenceGap = "SEQUENCE_GAP";
constexpr std::string_view kIncompleteWindow = "INCOMPLETE_WINDOW";
constexpr std::string_view kSequenceOutOfOrder = "SEQUENCE_OUT_OF_ORDER";
constexpr std::string_view kTimestampOutOfOrder = "TIMESTAMP_OUT_OF_ORDER";
constexpr std::string_view kMissingRequiredChannel =
    "MISSING_REQUIRED_CHANNEL";
constexpr std::string_view kStaleRequiredChannel = "STALE_REQUIRED_CHANNEL";
constexpr std::string_view kCalibrationInvalid =
    "CALIBRATION_INVALID_REQUIRED_CHANNEL";
constexpr std::string_view kWindowNotEligible = "WINDOW_NOT_ELIGIBLE";
constexpr std::string_view kFeatureContractNotApproved =
    "FEATURE_CONTRACT_NOT_APPROVED";
constexpr std::string_view kCalibrationNotApproved =
    "CALIBRATION_NOT_APPROVED";

bool validText(std::string_view text) noexcept {
    return !text.empty() && text.find('\0') == std::string_view::npos;
}

bool validIdentity(std::string_view text) noexcept {
    return validText(text) && text != "UNKNOWN" &&
           text != "NOT_APPLICABLE";
}

bool validBoundary(const WindowBoundaryIdentity& boundary) noexcept {
    return validText(boundary.configId) &&
           validText(boundary.calibrationId) &&
           validText(boundary.featureSchemaId) &&
           validText(boundary.featureSchemaSha256);
}

bool validSha256OrExplicitState(std::string_view text) noexcept {
    if (text == "UNKNOWN" || text == "NOT_APPLICABLE") {
        return true;
    }
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
            return {};
    }
    return {};
}

std::string_view evidenceName(EvidenceClass evidence) noexcept {
    switch (evidence) {
        case EvidenceClass::NotApplicable:
            return "NOT_APPLICABLE";
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
    }
    return {};
}

std::string_view structuralRejectionReason(
    const WindowCsvRecord& record) noexcept {
    if (record.start.bootId != record.end.bootId) {
        return kBootBoundary;
    }
    if (record.start.configId != record.end.configId) {
        return kConfigBoundary;
    }
    if (record.start.calibrationId != record.end.calibrationId) {
        return kCalibrationBoundary;
    }
    if (record.start.featureSchemaId != record.end.featureSchemaId ||
        record.start.featureSchemaVersion !=
            record.end.featureSchemaVersion ||
        record.start.featureSchemaSha256 !=
            record.end.featureSchemaSha256) {
        return kFeatureSchemaBoundary;
    }
    if (record.startSequence > record.endSequence) {
        return kSequenceOutOfOrder;
    }
    if (record.gapCount != 0U) {
        return kSequenceGap;
    }
    if (record.observedSamples < record.expectedSamples) {
        return kIncompleteWindow;
    }
    const std::uint64_t sequenceSpan =
        record.endSequence - record.startSequence;
    if (sequenceSpan !=
        static_cast<std::uint64_t>(record.observedSamples - 1U)) {
        return kSequenceGap;
    }
    if (record.startTimestampMs > record.endTimestampMs) {
        return kTimestampOutOfOrder;
    }
    return {};
}

std::string_view qualityRejectionReason(
    const WindowCsvRecord& record) noexcept {
    if (record.missingRequiredChannelCount != 0U) {
        return kMissingRequiredChannel;
    }
    if (record.staleRequiredChannelCount != 0U) {
        return kStaleRequiredChannel;
    }
    if (record.calibrationInvalidRequiredChannelCount != 0U) {
        return kCalibrationInvalid;
    }
    return {};
}

bool validRecordIdentity(const WindowCsvRecord& record) noexcept {
    return validIdentity(record.datasetId) &&
           validIdentity(record.windowId) && validIdentity(record.siteId) &&
           validIdentity(record.deviceId) &&
           validIdentity(record.sensorSetId) &&
           validIdentity(record.sessionId) &&
           validBoundary(record.start) && validBoundary(record.end) &&
           validSha256OrExplicitState(record.start.featureSchemaSha256) &&
           validSha256OrExplicitState(record.end.featureSchemaSha256) &&
           record.provenance.isValid() && !originName(record.provenance.origin).empty() &&
           !evidenceName(record.provenance.evidenceClass).empty() &&
           record.windowLengthSamples != 0U &&
           record.strideSamples != 0U &&
           record.expectedSamples == record.windowLengthSamples &&
           record.observedSamples != 0U &&
           record.observedSamples <= record.expectedSamples &&
           validText(record.splitGroupId) && validText(record.splitName) &&
           validText(record.splitPolicyVersion) &&
           (!record.calibrationApproved ||
            (record.start.calibrationId != "UNKNOWN" &&
             record.start.calibrationId != "NOT_APPLICABLE")) &&
           (!record.featureContractApproved ||
            (record.start.featureSchemaId != "UNKNOWN" &&
             record.start.featureSchemaId != "NOT_APPLICABLE" &&
             record.start.featureSchemaVersion != 0U &&
             validSha256OrExplicitState(record.start.featureSchemaSha256) &&
             record.start.featureSchemaSha256 != "UNKNOWN" &&
             record.start.featureSchemaSha256 != "NOT_APPLICABLE"));
}

bool appendEligibility(CsvRowWriter& writer, bool eligible,
                       std::string_view reason) noexcept {
    return writer.appendBoolean(eligible) == CsvWriteStatus::Ok &&
           writer.appendField(reason) == CsvWriteStatus::Ok;
}

WindowCsvExportStatus mapArtifactStatus(
    CsvArtifactExportStatus status) noexcept {
    switch (status) {
        case CsvArtifactExportStatus::Ok:
            return WindowCsvExportStatus::Ok;
        case CsvArtifactExportStatus::InvalidArgument:
            return WindowCsvExportStatus::InvalidArgument;
        case CsvArtifactExportStatus::InvalidSchema:
        case CsvArtifactExportStatus::CsvWriteError:
            return WindowCsvExportStatus::CsvWriteError;
        case CsvArtifactExportStatus::InvalidState:
            return WindowCsvExportStatus::InvalidState;
        case CsvArtifactExportStatus::CapacityExceeded:
            return WindowCsvExportStatus::CapacityExceeded;
        case CsvArtifactExportStatus::HashError:
            return WindowCsvExportStatus::HashError;
        case CsvArtifactExportStatus::RowCountOverflow:
            return WindowCsvExportStatus::RowCountOverflow;
    }
    return WindowCsvExportStatus::CsvWriteError;
}

}  // namespace

WindowCsvExporter::WindowCsvExporter(char* output,
                                     std::size_t outputCapacity,
                                     char* rowBuffer,
                                     std::size_t rowCapacity) noexcept
    : artifactWriter_(output, outputCapacity, rowBuffer, rowCapacity,
                      kWindowArtifactSchema) {}

WindowCsvExportStatus WindowCsvExporter::begin() noexcept {
    return mapArtifactStatus(artifactWriter_.begin());
}

WindowCsvExportStatus WindowCsvExporter::append(
    const WindowCsvRecord& record) noexcept {
    if (artifactWriter_.rowsWritten() == std::numeric_limits<std::uint64_t>::max()) {
        return WindowCsvExportStatus::RowCountOverflow;
    }
    if (!validRecordIdentity(record)) {
        return WindowCsvExportStatus::InvalidRecord;
    }
    if ((!record.window.eligible &&
         record.window.rejectionReason.empty()) ||
        (!record.training.eligible &&
         record.training.rejectionReason.empty()) ||
        (!record.validation.eligible &&
         record.validation.rejectionReason.empty()) ||
        (!record.test.eligible && record.test.rejectionReason.empty()) ||
        (!record.replay.eligible && record.replay.rejectionReason.empty()) ||
        (!record.safetyAnalysis.eligible &&
         record.safetyAnalysis.rejectionReason.empty())) {
        const std::string_view boundaryReason =
            structuralRejectionReason(record);
        const std::string_view qualityReason =
            qualityRejectionReason(record);
        const bool approvalGate =
            (!record.featureContractApproved || !record.calibrationApproved) &&
            (!record.training.eligible || !record.validation.eligible ||
             !record.test.eligible);
        if (boundaryReason.empty() && qualityReason.empty() &&
            record.window.eligible && !approvalGate) {
            return WindowCsvExportStatus::InvalidRecord;
        }
    }

    const std::string_view structuralReason =
        structuralRejectionReason(record);
    const std::string_view qualityReason =
        qualityRejectionReason(record);
    const std::string_view commonGateReason =
        !structuralReason.empty()
            ? structuralReason
            : (!qualityReason.empty()
                   ? qualityReason
                   : (!record.window.eligible
                          ? record.window.rejectionReason
                          : std::string_view{}));
    const bool outputWindowEligible =
        record.window.eligible && commonGateReason.empty();

    const auto outputDecision =
        [&](const WindowEligibilityDecision& decision,
            bool featureAndCalibrationRequired,
            bool& eligible, std::string_view& reason) noexcept {
            eligible = decision.eligible && outputWindowEligible;
            reason = decision.rejectionReason;
            if (!commonGateReason.empty() && !outputWindowEligible) {
                reason = commonGateReason;
            } else if (eligible && featureAndCalibrationRequired &&
                       !record.featureContractApproved) {
                eligible = false;
                reason = kFeatureContractNotApproved;
            } else if (eligible && featureAndCalibrationRequired &&
                       !record.calibrationApproved) {
                eligible = false;
                reason = kCalibrationNotApproved;
            } else if (!eligible && reason.empty() &&
                       featureAndCalibrationRequired &&
                       !record.featureContractApproved) {
                reason = kFeatureContractNotApproved;
            } else if (!eligible && reason.empty() &&
                       featureAndCalibrationRequired &&
                       !record.calibrationApproved) {
                reason = kCalibrationNotApproved;
            }
        };

    bool trainingEligible = false;
    bool validationEligible = false;
    bool testEligible = false;
    bool replayEligible = false;
    bool safetyAnalysisEligible = false;
    std::string_view trainingReason{};
    std::string_view validationReason{};
    std::string_view testReason{};
    std::string_view replayReason{};
    std::string_view safetyAnalysisReason{};
    outputDecision(record.training, true, trainingEligible, trainingReason);
    outputDecision(record.validation, true, validationEligible,
                   validationReason);
    outputDecision(record.test, true, testEligible, testReason);
    outputDecision(record.replay, false, replayEligible, replayReason);
    outputDecision(record.safetyAnalysis, false, safetyAnalysisEligible,
                   safetyAnalysisReason);

    if (!outputWindowEligible) {
        if (trainingReason.empty()) trainingReason = kWindowNotEligible;
        if (validationReason.empty()) validationReason = kWindowNotEligible;
        if (testReason.empty()) testReason = kWindowNotEligible;
        if (replayReason.empty()) replayReason = kWindowNotEligible;
        if (safetyAnalysisReason.empty()) {
            safetyAnalysisReason = kWindowNotEligible;
        }
    }
    if (trainingEligible || validationEligible || testEligible ||
        replayEligible || safetyAnalysisEligible) {
        if (!outputWindowEligible) {
            return WindowCsvExportStatus::InvalidRecord;
        }
    }
    if ((!trainingEligible && trainingReason.empty()) ||
        (!validationEligible && validationReason.empty()) ||
        (!testEligible && testReason.empty()) ||
        (!replayEligible && replayReason.empty()) ||
        (!safetyAnalysisEligible && safetyAnalysisReason.empty())) {
        return WindowCsvExportStatus::InvalidRecord;
    }

    const std::string_view outputWindowReason =
        outputWindowEligible ? std::string_view{}
                             : (!commonGateReason.empty()
                                    ? commonGateReason
                                    : (record.window.rejectionReason.empty()
                                           ? kWindowNotEligible
                                           : record.window.rejectionReason));
    const CsvArtifactExportStatus status = artifactWriter_.appendRow(
        [&record, outputWindowEligible, outputWindowReason,
         trainingEligible, trainingReason, validationEligible,
         validationReason, testEligible, testReason, replayEligible,
         replayReason, safetyAnalysisEligible,
         safetyAnalysisReason](CsvRowWriter& writer) noexcept {
            CsvWriteStatus appendStatus =
                writer.appendField(record.datasetId);
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.windowId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.siteId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.deviceId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.sensorSetId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.sessionId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(record.start.bootId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(record.end.bootId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(record.startSequence);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(record.endSequence);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(record.startTimestampMs);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(record.endTimestampMs);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus =
                    writer.appendUnsigned(record.windowLengthSamples);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(record.strideSamples);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(record.expectedSamples);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(record.observedSamples);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(record.gapCount);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.start.configId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.end.configId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus =
                    writer.appendField(record.start.calibrationId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.end.calibrationId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus =
                    writer.appendField(record.start.featureSchemaId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.end.featureSchemaId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(
                    record.start.featureSchemaVersion);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(
                    record.end.featureSchemaVersion);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus =
                    writer.appendField(record.start.featureSchemaSha256);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus =
                    writer.appendField(record.end.featureSchemaSha256);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(
                    record.missingRequiredChannelCount);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(
                    record.staleRequiredChannelCount);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendUnsigned(
                    record.calibrationInvalidRequiredChannelCount);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus =
                    writer.appendField(originName(record.provenance.origin));
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(
                    evidenceName(record.provenance.evidenceClass));
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus =
                    writer.appendBoolean(record.featureContractApproved);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendBoolean(record.calibrationApproved);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus =
                    appendEligibility(writer, outputWindowEligible,
                                      outputWindowReason)
                        ? CsvWriteStatus::Ok
                        : writer.status();
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = appendEligibility(
                                   writer, trainingEligible, trainingReason)
                                   ? CsvWriteStatus::Ok
                                   : writer.status();
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = appendEligibility(
                                   writer, validationEligible,
                                   validationReason)
                                   ? CsvWriteStatus::Ok
                                   : writer.status();
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = appendEligibility(writer, testEligible,
                                                  testReason)
                                   ? CsvWriteStatus::Ok
                                   : writer.status();
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = appendEligibility(writer, replayEligible,
                                                  replayReason)
                                   ? CsvWriteStatus::Ok
                                   : writer.status();
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = appendEligibility(
                                   writer, safetyAnalysisEligible,
                                   safetyAnalysisReason)
                                   ? CsvWriteStatus::Ok
                                   : writer.status();
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.annotationRefs);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.splitGroupId);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.splitName);
            }
            if (appendStatus == CsvWriteStatus::Ok) {
                appendStatus = writer.appendField(record.splitPolicyVersion);
            }
            return appendStatus;
        });
    return mapArtifactStatus(status);
}

WindowCsvExportStatus WindowCsvExporter::finish(
    CsvArtifactExportReport& report) noexcept {
    return mapArtifactStatus(artifactWriter_.finish(report));
}

}  // namespace project::dataset

#endif
