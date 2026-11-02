#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/AnnotationManager.h"

#include <cmath>
#include <limits>

namespace project::dataset {
namespace {

constexpr std::string_view kHeader[] = {
    "annotation_schema_version", "annotation_id", "dataset_id", "site_id",
    "session_id", "boot_id", "interval_start_ms", "interval_end_ms",
    "target_name", "target_value", "target_value_valid", "target_unit",
    "target_contract_id", "target_contract_version", "label_status",
    "data_origin", "evidence_class", "evidence_reference", "annotator",
    "reviewer", "confidence", "uncertainty", "label_method_version",
    "annotation_kind"};

bool validStatus(AnnotationStatus status) noexcept {
    switch (status) {
        case AnnotationStatus::Unlabeled:
        case AnnotationStatus::PendingReview:
        case AnnotationStatus::Provisional:
        case AnnotationStatus::Accepted:
        case AnnotationStatus::Uncertain:
        case AnnotationStatus::Conflict:
        case AnnotationStatus::Rejected:
        case AnnotationStatus::NotApplicable:
            return true;
    }
    return false;
}

bool validKind(AnnotationKind kind) noexcept {
    return kind == AnnotationKind::Independent ||
           kind == AnnotationKind::SyntheticProxy;
}

bool noTargetStatus(AnnotationStatus status) noexcept {
    return status == AnnotationStatus::Unlabeled ||
           status == AnnotationStatus::PendingReview ||
           status == AnnotationStatus::NotApplicable;
}

bool isGt5(EvidenceClass evidence) noexcept {
    return evidence == EvidenceClass::GT5;
}

bool validateRecord(const AnnotationRecord& record) noexcept {
    if (record.annotationId.size == 0U || record.datasetId.size == 0U ||
        record.siteId.size == 0U || record.sessionId.size == 0U ||
        record.intervalStartMs >= record.intervalEndMs ||
        !validStatus(record.status) || !validKind(record.kind) ||
        !record.provenance.isValid() ||
        (record.confidenceValid && !std::isfinite(record.confidence)) ||
        (record.uncertaintyValid && !std::isfinite(record.uncertainty)) ||
        (record.targetValueValid && !std::isfinite(record.targetValue))) {
        return false;
    }

    const bool synthetic =
        record.provenance.origin == DataOrigin::Synthetic;
    if (synthetic != (record.kind == AnnotationKind::SyntheticProxy) ||
        (synthetic && record.provenance.evidenceClass != EvidenceClass::GT5)) {
        return false;
    }

    if (noTargetStatus(record.status)) {
        return !record.targetValueValid && record.targetName.size == 0U &&
               record.targetUnit.size == 0U &&
               record.targetContractId.size == 0U &&
               record.targetContractVersion == 0U;
    }
    return record.targetValueValid && record.targetName.size != 0U &&
           record.targetContractId.size != 0U &&
           record.targetContractVersion != 0U &&
           record.evidenceReference.size != 0U &&
           record.labelMethodVersion.size != 0U;
}

bool targetContractMatches(
    const AnnotationRecord& record,
    const AnnotationTargetContract& targetContract) noexcept {
    return targetContract.approved && !targetContract.id.empty() &&
           !targetContract.name.empty() && targetContract.version != 0U &&
           record.targetContractId.view() == targetContract.id &&
           record.targetName.view() == targetContract.name &&
           record.targetUnit.view() == targetContract.unit &&
           record.targetContractVersion == targetContract.version;
}

std::string_view statusName(AnnotationStatus status) noexcept {
    switch (status) {
        case AnnotationStatus::Unlabeled:
            return "UNLABELED";
        case AnnotationStatus::PendingReview:
            return "PENDING_REVIEW";
        case AnnotationStatus::Provisional:
            return "PROVISIONAL";
        case AnnotationStatus::Accepted:
            return "ACCEPTED";
        case AnnotationStatus::Uncertain:
            return "UNCERTAIN";
        case AnnotationStatus::Conflict:
            return "CONFLICT";
        case AnnotationStatus::Rejected:
            return "REJECTED";
        case AnnotationStatus::NotApplicable:
            return "NOT_APPLICABLE";
    }
    return {};
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

std::string_view kindName(AnnotationKind kind) noexcept {
    switch (kind) {
        case AnnotationKind::Independent:
            return "INDEPENDENT";
        case AnnotationKind::SyntheticProxy:
            return "SYNTHETIC_PROXY";
    }
    return {};
}

}  // namespace

AnnotationManager::AnnotationManager(AnnotationRecord* records,
                                     std::size_t capacity) noexcept
    : records_(records), capacity_(capacity) {}

AnnotationManagerStatus AnnotationManager::reject(
    AnnotationManagerStatus status) noexcept {
    if (report_.rejected != std::numeric_limits<std::uint64_t>::max()) {
        ++report_.rejected;
    }
    return status;
}

AnnotationManagerStatus AnnotationManager::add(
    const AnnotationRecord& record,
    const AnnotationTargetContract& targetContract) noexcept {
    if ((records_ == nullptr && capacity_ != 0U) ||
        (records_ != nullptr && capacity_ == 0U)) {
        return reject(AnnotationManagerStatus::InvalidArgument);
    }
    if (!validateRecord(record)) {
        return reject(AnnotationManagerStatus::InvalidAnnotation);
    }
    if (!noTargetStatus(record.status) &&
        !targetContractMatches(record, targetContract)) {
        return reject(AnnotationManagerStatus::TargetContractNotApproved);
    }
    for (std::size_t i = 0U; i < size_; ++i) {
        if (records_[i].annotationId.view() == record.annotationId.view()) {
            return reject(AnnotationManagerStatus::DuplicateAnnotation);
        }
    }
    if (size_ == capacity_) {
        return reject(AnnotationManagerStatus::CapacityExceeded);
    }

    records_[size_++] = record;
    if (report_.accepted != std::numeric_limits<std::uint64_t>::max()) {
        ++report_.accepted;
    }
    if (record.kind == AnnotationKind::SyntheticProxy &&
        report_.syntheticProxy != std::numeric_limits<std::uint64_t>::max()) {
        ++report_.syntheticProxy;
    }
    if (isGt5(record.provenance.evidenceClass) &&
        report_.gt5 != std::numeric_limits<std::uint64_t>::max()) {
        ++report_.gt5;
    }
    return AnnotationManagerStatus::Ok;
}

const AnnotationRecord* AnnotationManager::record(
    std::size_t index) const noexcept {
    return index < size_ ? &records_[index] : nullptr;
}

AnnotationManagerStatus AnnotationManager::writeCsvHeader(
    char* output, std::size_t capacity,
    std::size_t& bytesWritten) const noexcept {
    bytesWritten = 0U;
    if (output == nullptr || capacity == 0U) {
        return AnnotationManagerStatus::InvalidArgument;
    }
    CsvRowWriter writer(output, capacity, kAnnotationCsvColumns);
    for (const std::string_view field : kHeader) {
        if (writer.appendField(field) != CsvWriteStatus::Ok) {
            return AnnotationManagerStatus::CsvWriteError;
        }
    }
    if (writer.finish() != CsvWriteStatus::Ok) {
        return AnnotationManagerStatus::CsvWriteError;
    }
    bytesWritten = writer.size();
    return AnnotationManagerStatus::Ok;
}

AnnotationManagerStatus AnnotationManager::writeCsvRow(
    std::size_t index, char* output, std::size_t capacity,
    std::size_t& bytesWritten) const noexcept {
    bytesWritten = 0U;
    if (index >= size_ || output == nullptr || capacity == 0U) {
        return AnnotationManagerStatus::InvalidArgument;
    }
    const AnnotationRecord& value = records_[index];
    CsvRowWriter writer(output, capacity, kAnnotationCsvColumns);
    const auto appendText = [&writer](std::string_view field) {
        return writer.appendField(field) == CsvWriteStatus::Ok;
    };
    const auto appendUnsigned = [&writer](std::uint64_t field) {
        return writer.appendUnsigned(field) == CsvWriteStatus::Ok;
    };
    if (!appendUnsigned(kAnnotationSchemaVersion) ||
        !appendText(value.annotationId.view()) ||
        !appendText(value.datasetId.view()) ||
        !appendText(value.siteId.view()) ||
        !appendText(value.sessionId.view()) ||
        !appendUnsigned(value.bootId) ||
        !appendUnsigned(value.intervalStartMs) ||
        !appendUnsigned(value.intervalEndMs) ||
        !appendText(value.targetName.view()) ||
        writer.appendOptionalNumber(value.targetValue,
                                    value.targetValueValid) !=
            CsvWriteStatus::Ok ||
        writer.appendBoolean(value.targetValueValid) != CsvWriteStatus::Ok ||
        !appendText(value.targetUnit.view()) ||
        !appendText(value.targetContractId.view()) ||
        !appendUnsigned(value.targetContractVersion) ||
        !appendText(statusName(value.status)) ||
        !appendText(originName(value.provenance.origin)) ||
        !appendText(evidenceName(value.provenance.evidenceClass)) ||
        !appendText(value.evidenceReference.view()) ||
        !appendText(value.annotator.view()) ||
        !appendText(value.reviewer.view()) ||
        writer.appendOptionalNumber(value.confidence,
                                    value.confidenceValid) !=
            CsvWriteStatus::Ok ||
        writer.appendOptionalNumber(value.uncertainty,
                                    value.uncertaintyValid) !=
            CsvWriteStatus::Ok ||
        !appendText(value.labelMethodVersion.view()) ||
        !appendText(kindName(value.kind)) ||
        writer.finish() != CsvWriteStatus::Ok) {
        return AnnotationManagerStatus::CsvWriteError;
    }
    bytesWritten = writer.size();
    return AnnotationManagerStatus::Ok;
}

bool AnnotationManager::matchesWindow(
    const AnnotationRecord& annotation,
    const AnnotationWindowInterval& window) noexcept {
    return validateRecord(annotation) && !window.datasetId.empty() &&
           !window.siteId.empty() && !window.sessionId.empty() &&
           window.startMs < window.endMs &&
           annotation.datasetId.view() == window.datasetId &&
           annotation.siteId.view() == window.siteId &&
           annotation.sessionId.view() == window.sessionId &&
           annotation.bootId == window.bootId &&
           annotation.intervalStartMs < window.endMs &&
           window.startMs < annotation.intervalEndMs;
}

}  // namespace project::dataset

#endif
