#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "DatasetValidator is available only to host data tools"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Dataset/DataProvenance.h"
#include "Dataset/DsdRawSampleCsvParser.h"

namespace project::dataset {

struct RawDatasetManifest {
    std::string_view datasetId{};
    std::uint32_t rawSchemaVersion{0U};
    std::uint64_t expectedRows{0U};
    std::uint64_t expectedBytes{0U};
    std::array<std::uint8_t, Sha256::kDigestSize> expectedSha256{};
    DataProvenance provenance{};
    std::array<bool, kDsdRawChannelCount> channelPresence{};
    bool channelPresenceConfigured{false};
};

struct IdentityWorkspaceEntry {
    std::size_t offset{0U};
    std::size_t length{0U};
    std::uint64_t hash{0U};
};

struct DatasetValidationWorkspace {
    IdentityWorkspaceEntry* entries{nullptr};
    std::size_t entryCapacity{0U};
    std::size_t entryCount{0U};
    std::uint8_t* keyBytes{nullptr};
    std::size_t keyByteCapacity{0U};
    std::size_t keyBytesUsed{0U};
};

enum class RawRowRejectionReason : std::uint8_t {
    WrongColumnCount,
    MalformedQuote,
    InvalidUtf8,
    RecordTooLong,
    TruncatedRecord,
    InvalidIdentity,
    InvalidNumeric,
    InvalidToken,
    ChannelValidityMismatch,
    ChannelPresenceMismatch,
    DuplicateIdentity,
    OutOfOrder,
    SequenceGap,
    Count
};

struct QuarantineReference {
    std::uint64_t recordNumber{0U};
    std::uint64_t byteOffset{0U};
    RawRowRejectionReason reason{RawRowRejectionReason::InvalidToken};
};

enum class DatasetValidationStatus : std::uint8_t {
    Complete,
    CompleteWithRejectedRows,
    InvalidManifest,
    HeaderSchemaMismatch,
    SourceError,
    TruncatedRecord,
    QuarantineCapacityExceeded,
    IdentityWorkspaceExceeded,
    ManifestHashMismatch,
    ManifestByteCountMismatch,
    ManifestRowCountMismatch
};

struct DatasetValidationReport {
    DatasetValidationStatus status{DatasetValidationStatus::InvalidManifest};
    std::uint64_t dataRowsSeen{0U};
    std::uint64_t acceptedRows{0U};
    std::uint64_t rejectedRows{0U};
    std::uint64_t bytesRead{0U};
    std::array<std::uint64_t,
               static_cast<std::size_t>(RawRowRejectionReason::Count)>
        rejectionCounts{};
    std::array<std::uint8_t, Sha256::kDigestSize> computedSha256{};
    bool hashVerified{false};
    bool rowCountVerified{false};
};

class DatasetValidator final {
public:
    DatasetValidationStatus validateRawSamples(
        IDatasetCsvSource& source, const RawDatasetManifest& manifest,
        DatasetValidationWorkspace& workspace,
        QuarantineReference* quarantine, std::size_t quarantineCapacity,
        DatasetValidationReport& report) const noexcept;

    static bool validateManifest(const RawDatasetManifest& manifest) noexcept;
};

}  // namespace project::dataset
