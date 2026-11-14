#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "CsvArtifactWriter is available only to host data tools"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "Dataset/CsvWriter.h"
#include "Dataset/Sha256.h"

namespace project::dataset {

struct CsvArtifactSchema {
    std::string_view artifactName{};
    std::uint32_t schemaVersion{0U};
    const std::string_view* columns{nullptr};
    std::size_t columnCount{0U};
};

enum class CsvArtifactExportStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    InvalidSchema,
    InvalidState,
    CsvWriteError,
    CapacityExceeded,
    RowCountOverflow,
    HashError
};

struct CsvArtifactExportReport {
    std::string_view artifactName{};
    std::uint32_t schemaVersion{0U};
    std::uint64_t rowCount{0U};
    std::uint64_t byteCount{0U};
    std::array<std::uint8_t, Sha256::kDigestSize> sha256{};
    bool complete{false};
};

class CsvArtifactWriter final {
public:
    CsvArtifactWriter(char* output, std::size_t outputCapacity,
                      char* rowBuffer, std::size_t rowCapacity,
                      const CsvArtifactSchema& schema) noexcept;

    CsvArtifactExportStatus begin() noexcept;

    template <typename AppendFields>
    CsvArtifactExportStatus appendRow(AppendFields&& appendFields) noexcept {
        static_assert(
            std::is_nothrow_invocable_r_v<CsvWriteStatus, AppendFields&,
                                          CsvRowWriter&>,
            "row field callback must be noexcept and return CsvWriteStatus");
        if (!begun_ || finished_ || failed_) {
            return CsvArtifactExportStatus::InvalidState;
        }
        if (rowCount_ == UINT64_MAX) {
            return fail(CsvArtifactExportStatus::RowCountOverflow);
        }

        CsvRowWriter row(rowBuffer_, rowCapacity_, schema_.columnCount);
        lastCsvStatus_ = appendFields(row);
        if (lastCsvStatus_ != CsvWriteStatus::Ok) {
            return fail(CsvArtifactExportStatus::CsvWriteError);
        }
        lastCsvStatus_ = row.finish();
        if (lastCsvStatus_ != CsvWriteStatus::Ok) {
            return fail(CsvArtifactExportStatus::CsvWriteError);
        }
        const CsvArtifactExportStatus commitStatus =
            commit(row.data(), row.size());
        if (commitStatus != CsvArtifactExportStatus::Ok) {
            return commitStatus;
        }
        ++rowCount_;
        return CsvArtifactExportStatus::Ok;
    }

    CsvArtifactExportStatus finish(CsvArtifactExportReport& report) noexcept;

    std::size_t bytesWritten() const noexcept { return bytesWritten_; }
    std::uint64_t rowsWritten() const noexcept { return rowCount_; }
    CsvWriteStatus lastCsvStatus() const noexcept { return lastCsvStatus_; }

private:
    CsvArtifactExportStatus fail(CsvArtifactExportStatus status) noexcept;
    CsvArtifactExportStatus commit(const char* bytes,
                                   std::size_t size) noexcept;

    char* const output_;
    const std::size_t outputCapacity_;
    char* const rowBuffer_;
    const std::size_t rowCapacity_;
    const CsvArtifactSchema schema_;
    Sha256 sha256_{};
    std::size_t bytesWritten_{0U};
    std::uint64_t rowCount_{0U};
    CsvWriteStatus lastCsvStatus_{CsvWriteStatus::Ok};
    CsvArtifactExportStatus failureStatus_{CsvArtifactExportStatus::Ok};
    bool begun_{false};
    bool finished_{false};
    bool failed_{false};
};

}  // namespace project::dataset
