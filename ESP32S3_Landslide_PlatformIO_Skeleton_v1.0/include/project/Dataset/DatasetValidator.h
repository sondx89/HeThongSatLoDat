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

inline constexpr std::uint32_t kDatasetManifestJsonSchemaVersion = 1U;
inline constexpr std::size_t kMaxDatasetManifestJsonBytes = 8192U;
inline constexpr std::size_t kMaxDatasetManifestSourceIds = 8U;
inline constexpr std::string_view kRawDatasetDsdDocumentVersion = "1.2";

enum class DatasetManifestValueState : std::uint8_t {
    Known,
    Unknown,
    NotApplicable
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

struct DatasetManifestDocument {
    std::uint32_t manifestSchemaVersion{kDatasetManifestJsonSchemaVersion};
    RawDatasetManifest raw{};
    std::string_view dsdDocumentVersion{kRawDatasetDsdDocumentVersion};
    std::uint32_t artifactSchemaVersion{0U};
    std::string_view artifactName{};
    std::string_view artifactSchemaSha256{};
    std::array<std::string_view, kMaxDatasetManifestSourceIds> sourceIds{};
    std::size_t sourceIdCount{0U};
    std::string_view firmwareId{};
    std::string_view configId{};
    std::string_view calibrationId{};
    std::string_view fssId{};
    std::string_view modelId{};
    std::string_view generatorVersion{};
    DatasetManifestValueState generatorSeedState{
        DatasetManifestValueState::Unknown};
    std::uint64_t generatorSeed{0U};
    std::string_view replaySourceSha256{};
    std::string_view createdAt{};
    std::string_view toolVersion{};
    std::uint64_t acceptedRows{0U};
    std::uint64_t rejectedRows{0U};
    std::array<std::uint64_t,
               static_cast<std::size_t>(RawRowRejectionReason::Count)>
        rejectionCounts{};
    std::string_view splitPolicyVersion{};
    std::string_view targetContractId{};
    std::string_view targetContractState{};
    std::string_view limitations{};
    std::string_view approvalState{};
};

enum class DatasetManifestJsonStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    InvalidManifest,
    UnsupportedVersion,
    MalformedJson,
    MissingField,
    DuplicateField,
    UnknownField,
    InvalidValue,
    CapacityExceeded,
    ArtifactValidationFailed,
    ArtifactHashMismatch,
    ArtifactByteCountMismatch,
    ArtifactRowCountMismatch,
    RejectionCountMismatch
};

DatasetManifestJsonStatus writeDatasetManifestJson(
    const DatasetManifestDocument& document, char* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept;

DatasetManifestJsonStatus readDatasetManifestJson(
    std::string_view input, char* stringStorage,
    std::size_t stringStorageCapacity,
    DatasetManifestDocument& document) noexcept;

DatasetManifestJsonStatus validateDatasetManifestDocument(
    const DatasetManifestDocument& document) noexcept;

DatasetManifestJsonStatus validateDatasetManifestResult(
    const DatasetManifestDocument& document,
    const DatasetValidationReport& report) noexcept;

DatasetManifestJsonStatus writeRawArtifactSchemaSha256(
    char* output, std::size_t capacity,
    std::size_t& bytesWritten) noexcept;

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
