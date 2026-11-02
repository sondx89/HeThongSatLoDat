#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/DatasetValidator.h"

#include <cstring>
#include <limits>

namespace project::dataset {
namespace {

constexpr std::size_t kDatasetId = 0U;
constexpr std::size_t kDeviceId = 2U;
constexpr std::size_t kSessionId = 4U;
constexpr std::size_t kBootId = 5U;
constexpr std::size_t kSequenceNo = 6U;
constexpr std::size_t kTimestampMs = 7U;
constexpr std::size_t kTimeStatus = 9U;
constexpr std::size_t kSensorStatusBits = 13U;
constexpr std::size_t kRawStart = 14U;
constexpr std::size_t kValidStart = 20U;
constexpr std::size_t kQualityStart = 26U;
constexpr std::size_t kValidMask = 32U;
constexpr std::size_t kQualityFlags = 33U;

struct PreviousStreamSample {
    std::array<char, kMaxRawCsvRecordBytes> deviceAndSession{};
    std::size_t keyLength{0U};
    std::uint64_t bootId{0U};
    std::uint64_t sequenceNo{0U};
    std::uint64_t timestampMs{0U};
    bool valid{false};
};

bool parseUnsigned(std::string_view text, std::uint64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    value = 0U;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') {
            return false;
        }
        const std::uint64_t number =
            static_cast<std::uint64_t>(digit - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - number) /
                        10U) {
            return false;
        }
        value = value * 10U + number;
    }
    return true;
}

bool parseSigned(std::string_view text, std::int64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    bool negative = text[0] == '-';
    std::size_t index = negative ? 1U : 0U;
    if (index == text.size()) {
        return false;
    }
    const std::uint64_t limit =
        negative
            ? static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max()) +
                  1U
            : static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max());
    std::uint64_t magnitude = 0U;
    for (; index < text.size(); ++index) {
        const char digit = text[index];
        if (digit < '0' || digit > '9') {
            return false;
        }
        const std::uint64_t number =
            static_cast<std::uint64_t>(digit - '0');
        if (magnitude > (limit - number) / 10U) {
            return false;
        }
        magnitude = magnitude * 10U + number;
    }
    if (!negative) {
        value = static_cast<std::int64_t>(magnitude);
    } else if (magnitude == limit) {
        value = std::numeric_limits<std::int64_t>::min();
    } else {
        value = -static_cast<std::int64_t>(magnitude);
    }
    return true;
}

bool parseBoolean(std::string_view text, bool& value) noexcept {
    if (text == "true") {
        value = true;
        return true;
    }
    if (text == "false") {
        value = false;
        return true;
    }
    return false;
}

bool validQuality(std::string_view value) noexcept {
    return value == "VALID" || value == "MISSING" || value == "STALE" ||
           value == "OUT_OF_RANGE" || value == "NON_FINITE" ||
           value == "SENSOR_ERROR" || value == "OUT_OF_ORDER" ||
           value == "CALIBRATION_INVALID" || value == "GAP_DETECTED" ||
           value == "NOT_READY";
}

std::uint64_t hashKey(const std::uint8_t* key,
                      std::size_t length) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = 0U; index < length; ++index) {
        hash ^= key[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool appendKeyBytes(std::uint8_t* output, std::size_t capacity,
                    std::size_t& size, const std::uint8_t* input,
                    std::size_t length) noexcept {
    if (length > capacity - size) {
        return false;
    }
    if (length != 0U) {
        std::memcpy(output + size, input, length);
    }
    size += length;
    return true;
}

bool appendLengthPrefixed(std::uint8_t* output, std::size_t capacity,
                          std::size_t& size,
                          std::string_view value) noexcept {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const std::uint32_t length =
        static_cast<std::uint32_t>(value.size());
    const std::uint8_t prefix[] = {
        static_cast<std::uint8_t>(length >> 24U),
        static_cast<std::uint8_t>(length >> 16U),
        static_cast<std::uint8_t>(length >> 8U),
        static_cast<std::uint8_t>(length)};
    return appendKeyBytes(output, capacity, size, prefix, sizeof(prefix)) &&
           appendKeyBytes(
               output, capacity, size,
               reinterpret_cast<const std::uint8_t*>(value.data()),
               value.size());
}

bool buildIdentityKey(const RawCsvRowView& row,
                      DatasetValidationWorkspace& workspace,
                      std::uint64_t bootId, std::uint64_t sequenceNo,
                      bool& duplicate) noexcept {
    duplicate = false;
    if (workspace.entries == nullptr ||
        workspace.entryCount >= workspace.entryCapacity ||
        workspace.keyBytes == nullptr) {
        return false;
    }
    const std::size_t keyStart = workspace.keyBytesUsed;
    std::size_t keyLength = 0U;
    std::uint8_t* const keyBuffer =
        workspace.keyBytes == nullptr ? nullptr
                                      : workspace.keyBytes + keyStart;
    const std::size_t remaining =
        workspace.keyByteCapacity >= keyStart
            ? workspace.keyByteCapacity - keyStart
            : 0U;
    if (keyBuffer == nullptr ||
        !appendLengthPrefixed(
            keyBuffer, remaining, keyLength, row.fields[kDatasetId]) ||
        !appendLengthPrefixed(
            keyBuffer, remaining, keyLength, row.fields[kDeviceId]) ||
        !appendLengthPrefixed(
            keyBuffer, remaining, keyLength, row.fields[kSessionId])) {
        return false;
    }
    for (const std::uint64_t number : {bootId, sequenceNo}) {
        std::uint8_t encoded[8U]{};
        for (std::size_t index = 0U; index < 8U; ++index) {
            encoded[index] = static_cast<std::uint8_t>(
                number >> ((7U - index) * 8U));
        }
        if (!appendKeyBytes(keyBuffer, remaining, keyLength, encoded,
                            sizeof(encoded))) {
            return false;
        }
    }

    const std::uint64_t hash = hashKey(keyBuffer, keyLength);
    for (std::size_t index = 0U; index < workspace.entryCount; ++index) {
        const IdentityWorkspaceEntry& previous = workspace.entries[index];
        if (previous.hash == hash && previous.length == keyLength &&
            std::memcmp(workspace.keyBytes + previous.offset, keyBuffer,
                        keyLength) == 0) {
            duplicate = true;
            return true;
        }
    }

    workspace.entries[workspace.entryCount++] =
        {keyStart, keyLength, hash};
    workspace.keyBytesUsed += keyLength;
    return true;
}

bool makeStreamKey(const RawCsvRowView& row,
                   PreviousStreamSample& previous) noexcept {
    previous.keyLength = 0U;
    const std::string_view fields[] = {row.fields[kDeviceId],
                                       row.fields[kSessionId]};
    for (const std::string_view field : fields) {
        if (field.size() > previous.deviceAndSession.size() -
                               previous.keyLength) {
            return false;
        }
        const std::uint32_t length =
            static_cast<std::uint32_t>(field.size());
        if (previous.deviceAndSession.size() - previous.keyLength < 4U) {
            return false;
        }
        previous.deviceAndSession[previous.keyLength++] =
            static_cast<char>(length >> 24U);
        previous.deviceAndSession[previous.keyLength++] =
            static_cast<char>(length >> 16U);
        previous.deviceAndSession[previous.keyLength++] =
            static_cast<char>(length >> 8U);
        previous.deviceAndSession[previous.keyLength++] =
            static_cast<char>(length);
        if (field.size() > previous.deviceAndSession.size() -
                               previous.keyLength) {
            return false;
        }
        std::memcpy(previous.deviceAndSession.data() + previous.keyLength,
                    field.data(), field.size());
        previous.keyLength += field.size();
    }
    return true;
}

bool sameStream(const RawCsvRowView& row,
                const PreviousStreamSample& previous) noexcept {
    PreviousStreamSample current{};
    if (!makeStreamKey(row, current) ||
        current.keyLength != previous.keyLength) {
        return false;
    }
    return std::memcmp(current.deviceAndSession.data(),
                       previous.deviceAndSession.data(),
                       current.keyLength) == 0;
}

bool validIdentityFields(const RawCsvRowView& row,
                         const RawDatasetManifest& manifest,
                         std::uint64_t& bootId,
                         std::uint64_t& sequenceNo,
                         std::uint64_t& timestampMs,
                         RawRowRejectionReason& reason) noexcept {
    if (row.fields[kDatasetId].empty() ||
        row.fields[kDeviceId].empty() ||
        row.fields[kSessionId].empty() ||
        row.fields[kDatasetId] != manifest.datasetId) {
        reason = RawRowRejectionReason::InvalidIdentity;
        return false;
    }
    if (!parseUnsigned(row.fields[kBootId], bootId) ||
        !parseUnsigned(row.fields[kSequenceNo], sequenceNo) ||
        !parseUnsigned(row.fields[kTimestampMs], timestampMs)) {
        reason = RawRowRejectionReason::InvalidNumeric;
        return false;
    }
    return true;
}

bool validRowFields(const RawCsvRowView& row,
                    const RawDatasetManifest& manifest,
                    RawRowRejectionReason& reason) noexcept {
    std::uint64_t ignored = 0U;
    if (!parseUnsigned(row.fields[kSensorStatusBits], ignored) ||
        !parseUnsigned(row.fields[kValidMask], ignored) ||
        !parseUnsigned(row.fields[kQualityFlags], ignored)) {
        reason = RawRowRejectionReason::InvalidNumeric;
        return false;
    }
    const std::string_view timeStatus = row.fields[kTimeStatus];
    if (timeStatus != "UNKNOWN" && timeStatus != "UNSYNCED" &&
        timeStatus != "SYNCED") {
        reason = RawRowRejectionReason::InvalidToken;
        return false;
    }

    for (std::size_t channel = 0U; channel < kDsdRawChannelCount; ++channel) {
        bool isValid = false;
        if (!parseBoolean(row.fields[kValidStart + channel], isValid) ||
            !validQuality(row.fields[kQualityStart + channel])) {
            reason = RawRowRejectionReason::InvalidToken;
            return false;
        }
        const std::string_view raw = row.fields[kRawStart + channel];
        const std::string_view quality = row.fields[kQualityStart + channel];
        if (isValid != (quality == "VALID")) {
            reason = RawRowRejectionReason::ChannelValidityMismatch;
            return false;
        }
        if (!manifest.channelPresence[channel]) {
            if (isValid || !raw.empty() || quality != "MISSING") {
                reason = RawRowRejectionReason::ChannelPresenceMismatch;
                return false;
            }
        } else if (isValid) {
            std::int64_t rawValue = 0;
            if (raw.empty() || !parseSigned(raw, rawValue)) {
                reason = RawRowRejectionReason::InvalidNumeric;
                return false;
            }
        } else if (!raw.empty()) {
            reason = RawRowRejectionReason::ChannelValidityMismatch;
            return false;
        }
    }
    return true;
}

}  // namespace

bool DatasetValidator::validateManifest(
    const RawDatasetManifest& manifest) noexcept {
    if (manifest.datasetId.empty() ||
        manifest.rawSchemaVersion != kRawSampleCoreSchemaVersion ||
        !manifest.channelPresenceConfigured ||
        !manifest.provenance.isValid()) {
        return false;
    }
    bool hashNonZero = false;
    for (const std::uint8_t value : manifest.expectedSha256) {
        hashNonZero = hashNonZero || value != 0U;
    }
    return hashNonZero;
}

DatasetValidationStatus DatasetValidator::validateRawSamples(
    IDatasetCsvSource& source, const RawDatasetManifest& manifest,
    DatasetValidationWorkspace& workspace,
    QuarantineReference* quarantine, std::size_t quarantineCapacity,
    DatasetValidationReport& report) const noexcept {
    report = {};
    workspace.entryCount = 0U;
    workspace.keyBytesUsed = 0U;
    if (!validateManifest(manifest) ||
        (quarantine == nullptr && quarantineCapacity != 0U)) {
        report.status = DatasetValidationStatus::InvalidManifest;
        return report.status;
    }

    DsdRawSampleCsvParser parser(source);
    const RawCsvParserStatus beginStatus = parser.begin();
    if (beginStatus == RawCsvParserStatus::HeaderSchemaMismatch) {
        report.status = DatasetValidationStatus::HeaderSchemaMismatch;
        return report.status;
    }
    if (beginStatus != RawCsvParserStatus::Row) {
        report.status = beginStatus == RawCsvParserStatus::SourceError
                            ? DatasetValidationStatus::SourceError
                            : DatasetValidationStatus::HeaderSchemaMismatch;
        return report.status;
    }

    PreviousStreamSample previous{};
    for (;;) {
        RawCsvRowView row{};
        const RawCsvParserStatus rowStatus = parser.next(row);
        if (rowStatus == RawCsvParserStatus::EndOfFile) {
            break;
        }
        if (rowStatus == RawCsvParserStatus::SourceError) {
            report.status = DatasetValidationStatus::SourceError;
            return report.status;
        }
        if (report.dataRowsSeen !=
            std::numeric_limits<std::uint64_t>::max()) {
            ++report.dataRowsSeen;
        }

        RawRowRejectionReason rejection{};
        bool rowValid = rowStatus == RawCsvParserStatus::Row;
        switch (rowStatus) {
            case RawCsvParserStatus::Row:
                break;
            case RawCsvParserStatus::WrongColumnCount:
                rejection = RawRowRejectionReason::WrongColumnCount;
                break;
            case RawCsvParserStatus::MalformedQuote:
                rejection = RawRowRejectionReason::MalformedQuote;
                break;
            case RawCsvParserStatus::InvalidUtf8:
                rejection = RawRowRejectionReason::InvalidUtf8;
                break;
            case RawCsvParserStatus::RecordTooLong:
                rejection = RawRowRejectionReason::RecordTooLong;
                break;
            case RawCsvParserStatus::TruncatedRecord:
                rejection = RawRowRejectionReason::TruncatedRecord;
                break;
            case RawCsvParserStatus::HeaderSchemaMismatch:
            case RawCsvParserStatus::EndOfFile:
            case RawCsvParserStatus::DigestReady:
            case RawCsvParserStatus::SourceError:
            case RawCsvParserStatus::InvalidState:
                report.status = DatasetValidationStatus::SourceError;
                return report.status;
        }

        std::uint64_t bootId = 0U;
        std::uint64_t sequenceNo = 0U;
        std::uint64_t timestampMs = 0U;
        if (rowValid &&
            !validIdentityFields(row, manifest, bootId, sequenceNo,
                                 timestampMs, rejection)) {
            rowValid = false;
        }

        const bool identityParsed = rowValid;
        if (identityParsed) {
            bool duplicate = false;
            if (!buildIdentityKey(row, workspace, bootId, sequenceNo,
                                  duplicate)) {
                report.status =
                    DatasetValidationStatus::IdentityWorkspaceExceeded;
                return report.status;
            }
            if (duplicate) {
                rejection = RawRowRejectionReason::DuplicateIdentity;
                rowValid = false;
            }
        }

        if (rowValid && !validRowFields(row, manifest, rejection)) {
            rowValid = false;
        }

        PreviousStreamSample current{};
        if (rowValid && !makeStreamKey(row, current)) {
            rejection = RawRowRejectionReason::InvalidIdentity;
            rowValid = false;
        }
        if (rowValid && previous.valid && sameStream(row, previous) &&
            previous.bootId == bootId) {
            if (sequenceNo <= previous.sequenceNo ||
                timestampMs <= previous.timestampMs) {
                rejection = RawRowRejectionReason::OutOfOrder;
                rowValid = false;
            } else if (previous.sequenceNo ==
                           std::numeric_limits<std::uint64_t>::max() ||
                       sequenceNo != previous.sequenceNo + 1U) {
                rejection = RawRowRejectionReason::SequenceGap;
                rowValid = false;
            }
        }

        if (rowValid) {
            current.bootId = bootId;
            current.sequenceNo = sequenceNo;
            current.timestampMs = timestampMs;
            current.valid = true;
            previous = current;
            if (report.acceptedRows !=
                std::numeric_limits<std::uint64_t>::max()) {
                ++report.acceptedRows;
            }
        } else {
            const std::size_t reasonIndex =
                static_cast<std::size_t>(rejection);
            if (reasonIndex >= report.rejectionCounts.size() ||
                report.rejectedRows == quarantineCapacity) {
                report.status =
                    DatasetValidationStatus::QuarantineCapacityExceeded;
                return report.status;
            }
            quarantine[report.rejectedRows] =
                {row.recordNumber, row.byteOffset, rejection};
            ++report.rejectedRows;
            if (report.rejectionCounts[reasonIndex] !=
                std::numeric_limits<std::uint64_t>::max()) {
                ++report.rejectionCounts[reasonIndex];
            }
            if (rejection == RawRowRejectionReason::TruncatedRecord) {
                report.status = DatasetValidationStatus::TruncatedRecord;
                return report.status;
            }
        }
    }

    if (parser.finishDigest(report.computedSha256, report.bytesRead) !=
        RawCsvParserStatus::DigestReady) {
        report.status = DatasetValidationStatus::SourceError;
        return report.status;
    }
    if (report.bytesRead != manifest.expectedBytes) {
        report.status = DatasetValidationStatus::ManifestByteCountMismatch;
        return report.status;
    }
    if (report.computedSha256 != manifest.expectedSha256) {
        report.status = DatasetValidationStatus::ManifestHashMismatch;
        return report.status;
    }
    report.hashVerified = true;
    report.rowCountVerified = report.dataRowsSeen == manifest.expectedRows;
    if (!report.rowCountVerified) {
        report.status = DatasetValidationStatus::ManifestRowCountMismatch;
        return report.status;
    }
    report.status = report.rejectedRows == 0U
                        ? DatasetValidationStatus::Complete
                        : DatasetValidationStatus::CompleteWithRejectedRows;
    return report.status;
}

}  // namespace project::dataset

#endif
