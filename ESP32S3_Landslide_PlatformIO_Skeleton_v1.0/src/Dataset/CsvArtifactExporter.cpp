#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/CsvArtifactExporter.h"

#include <cstring>
#include <limits>

namespace project::dataset {
namespace {

bool validSchema(const CsvArtifactSchema& schema) noexcept {
    if (schema.artifactName.empty() ||
        schema.artifactName.find('\0') != std::string_view::npos ||
        schema.schemaVersion == 0U || schema.columns == nullptr ||
        schema.columnCount == 0U) {
        return false;
    }
    for (std::size_t index = 0U; index < schema.columnCount; ++index) {
        if (schema.columns[index].empty()) {
            return false;
        }
    }
    return true;
}

bool buffersOverlap(const char* left, std::size_t leftSize,
                    const char* right, std::size_t rightSize) noexcept {
    const std::uintptr_t leftStart =
        reinterpret_cast<std::uintptr_t>(left);
    const std::uintptr_t rightStart =
        reinterpret_cast<std::uintptr_t>(right);
    const std::uintptr_t maxValue =
        std::numeric_limits<std::uintptr_t>::max();
    if (leftSize > maxValue - leftStart ||
        rightSize > maxValue - rightStart) {
        return true;
    }
    const std::uintptr_t leftEnd = leftStart + leftSize;
    const std::uintptr_t rightEnd = rightStart + rightSize;
    return leftStart < rightEnd && rightStart < leftEnd;
}

}  // namespace

CsvArtifactWriter::CsvArtifactWriter(
    char* output, std::size_t outputCapacity, char* rowBuffer,
    std::size_t rowCapacity, const CsvArtifactSchema& schema) noexcept
    : output_(output),
      outputCapacity_(outputCapacity),
      rowBuffer_(rowBuffer),
      rowCapacity_(rowCapacity),
      schema_(schema) {}

CsvArtifactExportStatus CsvArtifactWriter::fail(
    CsvArtifactExportStatus status) noexcept {
    if (!failed_) {
        failed_ = true;
        failureStatus_ = status;
    }
    return failureStatus_;
}

CsvArtifactExportStatus CsvArtifactWriter::commit(
    const char* bytes, std::size_t size) noexcept {
    if (output_ == nullptr || bytes == nullptr ||
        bytesWritten_ > outputCapacity_ ||
        size > outputCapacity_ - bytesWritten_) {
        return fail(CsvArtifactExportStatus::CapacityExceeded);
    }
    if (!sha256_.update(reinterpret_cast<const std::uint8_t*>(bytes), size)) {
        return fail(CsvArtifactExportStatus::HashError);
    }
    std::memcpy(output_ + bytesWritten_, bytes, size);
    bytesWritten_ += size;
    return CsvArtifactExportStatus::Ok;
}

CsvArtifactExportStatus CsvArtifactWriter::begin() noexcept {
    if (begun_ || finished_ || failed_) {
        return CsvArtifactExportStatus::InvalidState;
    }
    if (output_ == nullptr || outputCapacity_ == 0U || rowBuffer_ == nullptr ||
        rowCapacity_ == 0U) {
        return fail(CsvArtifactExportStatus::InvalidArgument);
    }
    if (!validSchema(schema_)) {
        return fail(CsvArtifactExportStatus::InvalidSchema);
    }
    if (buffersOverlap(output_, outputCapacity_, rowBuffer_, rowCapacity_)) {
        return fail(CsvArtifactExportStatus::InvalidArgument);
    }

    CsvRowWriter header(rowBuffer_, rowCapacity_, schema_.columnCount);
    for (std::size_t index = 0U; index < schema_.columnCount; ++index) {
        lastCsvStatus_ = header.appendField(schema_.columns[index]);
        if (lastCsvStatus_ != CsvWriteStatus::Ok) {
            return fail(CsvArtifactExportStatus::CsvWriteError);
        }
    }
    lastCsvStatus_ = header.finish();
    if (lastCsvStatus_ != CsvWriteStatus::Ok) {
        return fail(CsvArtifactExportStatus::CsvWriteError);
    }
    const CsvArtifactExportStatus commitStatus =
        commit(header.data(), header.size());
    if (commitStatus != CsvArtifactExportStatus::Ok) {
        return commitStatus;
    }
    begun_ = true;
    return CsvArtifactExportStatus::Ok;
}

CsvArtifactExportStatus CsvArtifactWriter::finish(
    CsvArtifactExportReport& report) noexcept {
    report = {};
    if (failed_) {
        return failureStatus_;
    }
    if (!begun_ || finished_) {
        return CsvArtifactExportStatus::InvalidState;
    }

    std::array<std::uint8_t, Sha256::kDigestSize> digest{};
    if (!sha256_.finish(digest)) {
        return fail(CsvArtifactExportStatus::HashError);
    }
    report.artifactName = schema_.artifactName;
    report.schemaVersion = schema_.schemaVersion;
    report.rowCount = rowCount_;
    report.byteCount = static_cast<std::uint64_t>(bytesWritten_);
    report.sha256 = digest;
    report.complete = true;
    finished_ = true;
    return CsvArtifactExportStatus::Ok;
}

}  // namespace project::dataset

#endif
