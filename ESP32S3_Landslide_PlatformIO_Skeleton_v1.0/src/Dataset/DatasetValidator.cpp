#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/DatasetValidator.h"

#include <charconv>
#include <cstring>
#include <limits>

#include "Dataset/CsvSchemas.h"

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

constexpr char kHexDigits[] = "0123456789abcdef";

bool validUtf8(std::string_view value) noexcept {
    std::size_t index = 0U;
    while (index < value.size()) {
        const auto first = static_cast<std::uint8_t>(value[index]);
        if (first < 0x80U) {
            ++index;
            continue;
        }

        std::size_t continuationCount = 0U;
        std::uint32_t codePoint = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            continuationCount = 1U;
            codePoint = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            continuationCount = 2U;
            codePoint = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            continuationCount = 3U;
            codePoint = first & 0x07U;
        } else {
            return false;
        }
        if (continuationCount > value.size() - index - 1U) {
            return false;
        }
        for (std::size_t offset = 1U; offset <= continuationCount; ++offset) {
            const auto next =
                static_cast<std::uint8_t>(value[index + offset]);
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }
        if ((continuationCount == 2U && codePoint < 0x800U) ||
            (continuationCount == 3U && codePoint < 0x10000U) ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU) ||
            codePoint > 0x10FFFFU) {
            return false;
        }
        index += continuationCount + 1U;
    }
    return true;
}

bool validText(std::string_view value) noexcept {
    return !value.empty() && value.find('\0') == std::string_view::npos &&
           validUtf8(value);
}

bool parseSha256(std::string_view text,
                 std::array<std::uint8_t, Sha256::kDigestSize>& digest) noexcept {
    if (text.size() != digest.size() * 2U) {
        return false;
    }
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        const auto nibble = [](char value) -> int {
            if (value >= '0' && value <= '9') {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f') {
                return value - 'a' + 10;
            }
            return -1;
        };
        const int high = nibble(text[index * 2U]);
        const int low = nibble(text[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        digest[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

void formatSha256(
    const std::array<std::uint8_t, Sha256::kDigestSize>& digest,
    char* output) noexcept {
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        output[index * 2U] = kHexDigits[digest[index] >> 4U];
        output[index * 2U + 1U] = kHexDigits[digest[index] & 0x0FU];
    }
}

bool rawSchemaSha256(
    std::array<std::uint8_t, Sha256::kDigestSize>& digest) noexcept {
    char header[1024]{};
    std::size_t headerSize = 0U;
    if (writeDsdV1_2RawSampleCoreHeader(header, sizeof(header), headerSize) !=
        CsvWriteStatus::Ok) {
        return false;
    }
    Sha256 hash{};
    return hash.update(reinterpret_cast<const std::uint8_t*>(header),
                       headerSize) &&
           hash.finish(digest);
}

class JsonBufferWriter final {
public:
    JsonBufferWriter(char* output, std::size_t capacity) noexcept
        : output_(output), capacity_(capacity) {}

    bool append(std::string_view value) noexcept {
        if (!valid_ || value.size() > capacity_ - size_) {
            valid_ = false;
            return false;
        }
        if (!value.empty()) {
            std::memcpy(output_ + size_, value.data(), value.size());
            size_ += value.size();
        }
        return true;
    }

    bool append(char value) noexcept {
        return append(std::string_view(&value, 1U));
    }

    bool appendQuoted(std::string_view value) noexcept {
        if (!validUtf8(value) || !append('"')) {
            valid_ = false;
            return false;
        }
        for (const unsigned char byte : value) {
            switch (byte) {
                case '"':
                    if (!append("\\\"")) return false;
                    break;
                case '\\':
                    if (!append("\\\\")) return false;
                    break;
                case '\b':
                    if (!append("\\b")) return false;
                    break;
                case '\f':
                    if (!append("\\f")) return false;
                    break;
                case '\n':
                    if (!append("\\n")) return false;
                    break;
                case '\r':
                    if (!append("\\r")) return false;
                    break;
                case '\t':
                    if (!append("\\t")) return false;
                    break;
                default:
                    if (byte < 0x20U) {
                        const char escaped[] = {
                            '\\', 'u', '0', '0', kHexDigits[byte >> 4U],
                            kHexDigits[byte & 0x0FU]};
                        if (!append(std::string_view(escaped,
                                                     sizeof(escaped)))) {
                            return false;
                        }
                    } else if (!append(static_cast<char>(byte))) {
                        return false;
                    }
                    break;
            }
        }
        return append('"');
    }

    bool appendUnsigned(std::uint64_t value) noexcept {
        char number[32]{};
        const auto result =
            std::to_chars(number, number + sizeof(number), value);
        return result.ec == std::errc{} &&
               append(std::string_view(number,
                                       static_cast<std::size_t>(result.ptr -
                                                                number)));
    }

    bool valid() const noexcept { return valid_; }
    std::size_t size() const noexcept { return size_; }

private:
    char* const output_;
    const std::size_t capacity_;
    std::size_t size_{0U};
    bool valid_{true};
};

enum class JsonField : std::uint8_t {
    ManifestSchemaVersion,
    DatasetId,
    DsdDocumentVersion,
    ArtifactName,
    ArtifactSchemaVersion,
    ArtifactSchemaSha256,
    ArtifactBytes,
    ArtifactRows,
    ArtifactSha256,
    DataOrigin,
    EvidenceClass,
    ChannelPresence,
    SourceIds,
    FirmwareId,
    ConfigId,
    CalibrationId,
    FssId,
    ModelId,
    GeneratorVersion,
    GeneratorSeed,
    ReplaySourceSha256,
    CreatedAt,
    ToolVersion,
    AcceptedRows,
    RejectedRows,
    RejectionCounts,
    SplitPolicyVersion,
    TargetContractId,
    TargetContractState,
    Limitations,
    ApprovalState,
    Count
};

constexpr std::size_t kManifestJsonFieldCount =
    static_cast<std::size_t>(JsonField::Count);

JsonField identifyJsonField(std::string_view key) noexcept {
    constexpr std::array<std::string_view, kManifestJsonFieldCount> names{{
        "manifest_schema_version",
        "dataset_id",
        "dsd_document_version",
        "artifact_name",
        "artifact_schema_version",
        "artifact_schema_sha256",
        "artifact_bytes",
        "artifact_rows",
        "artifact_sha256",
        "data_origin",
        "evidence_class",
        "channel_presence",
        "source_ids",
        "firmware_id",
        "config_id",
        "calibration_id",
        "fss_id",
        "model_id",
        "generator_version",
        "generator_seed",
        "replay_source_sha256",
        "created_at",
        "tool_version",
        "accepted_rows",
        "rejected_rows",
        "rejection_counts",
        "split_policy_version",
        "target_contract_id",
        "target_contract_state",
        "limitations",
        "approval_state",
    }};
    for (std::size_t index = 0U; index < names.size(); ++index) {
        if (key == names[index]) {
            return static_cast<JsonField>(index);
        }
    }
    return JsonField::Count;
}

std::string_view dataOriginName(DataOrigin value) noexcept {
    switch (value) {
        case DataOrigin::Synthetic: return "SYNTHETIC";
        case DataOrigin::Bench: return "BENCH";
        case DataOrigin::Field: return "FIELD";
        case DataOrigin::Reference: return "REFERENCE";
        case DataOrigin::Replay: return "REPLAY";
        case DataOrigin::Unspecified: break;
    }
    return {};
}

bool parseDataOrigin(std::string_view text, DataOrigin& value) noexcept {
    constexpr std::array<std::string_view, 5U> names{{
        "SYNTHETIC", "BENCH", "FIELD", "REFERENCE", "REPLAY"}};
    constexpr std::array<DataOrigin, 5U> values{{
        DataOrigin::Synthetic, DataOrigin::Bench, DataOrigin::Field,
        DataOrigin::Reference, DataOrigin::Replay}};
    for (std::size_t index = 0U; index < names.size(); ++index) {
        if (text == names[index]) {
            value = values[index];
            return true;
        }
    }
    return false;
}

std::string_view evidenceClassName(EvidenceClass value) noexcept {
    switch (value) {
        case EvidenceClass::NotApplicable: return "NOT_APPLICABLE";
        case EvidenceClass::GT1: return "GT1";
        case EvidenceClass::GT2: return "GT2";
        case EvidenceClass::GT3: return "GT3";
        case EvidenceClass::GT4: return "GT4";
        case EvidenceClass::GT5: return "GT5";
    }
    return {};
}

bool parseEvidenceClass(std::string_view text,
                        EvidenceClass& value) noexcept {
    constexpr std::array<std::string_view, 6U> names{{
        "NOT_APPLICABLE", "GT1", "GT2", "GT3", "GT4", "GT5"}};
    constexpr std::array<EvidenceClass, 6U> values{{
        EvidenceClass::NotApplicable, EvidenceClass::GT1, EvidenceClass::GT2,
        EvidenceClass::GT3, EvidenceClass::GT4, EvidenceClass::GT5}};
    for (std::size_t index = 0U; index < names.size(); ++index) {
        if (text == names[index]) {
            value = values[index];
            return true;
        }
    }
    return false;
}

class JsonReader final {
public:
    JsonReader(std::string_view input, char* storage,
               std::size_t storageCapacity) noexcept
        : input_(input), storage_(storage), storageCapacity_(storageCapacity) {}

    DatasetManifestJsonStatus parse(DatasetManifestDocument& document) noexcept;
    DatasetManifestJsonStatus status() const noexcept { return status_; }

private:
    void skipWhitespace() noexcept;
    bool consume(char value) noexcept;
    bool parseString(char* output, std::size_t capacity,
                     std::size_t& length) noexcept;
    bool parseText(std::string_view& output) noexcept;
    bool parseUnsigned(std::uint64_t& value) noexcept;
    bool parseBoolean(bool& value) noexcept;
    bool parseSourceIds(DatasetManifestDocument& document) noexcept;
    bool parseChannelPresence(DatasetManifestDocument& document) noexcept;
    bool parseRejectionCounts(DatasetManifestDocument& document) noexcept;
    bool parseStringField(JsonField field,
                          DatasetManifestDocument& document) noexcept;
    bool parseValue(JsonField field,
                    DatasetManifestDocument& document) noexcept;
    bool appendCodePoint(std::uint32_t codePoint, char* output,
                         std::size_t capacity,
                         std::size_t& length) noexcept;
    bool fail(DatasetManifestJsonStatus status) noexcept;

    std::string_view input_;
    char* const storage_;
    const std::size_t storageCapacity_;
    std::size_t position_{0U};
    std::size_t storageUsed_{0U};
    DatasetManifestJsonStatus status_{DatasetManifestJsonStatus::Ok};
};

void JsonReader::skipWhitespace() noexcept {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\t' ||
            input_[position_] == '\r' || input_[position_] == '\n')) {
        ++position_;
    }
}

bool JsonReader::fail(DatasetManifestJsonStatus status) noexcept {
    status_ = status;
    return false;
}

bool JsonReader::consume(char value) noexcept {
    skipWhitespace();
    if (position_ >= input_.size() || input_[position_] != value) {
        return fail(DatasetManifestJsonStatus::MalformedJson);
    }
    ++position_;
    return true;
}

bool JsonReader::appendCodePoint(std::uint32_t codePoint, char* output,
                                 std::size_t capacity,
                                 std::size_t& length) noexcept {
    std::array<char, 4U> encoded{};
    std::size_t encodedLength = 0U;
    if (codePoint <= 0x7FU) {
        encoded[encodedLength++] = static_cast<char>(codePoint);
    } else if (codePoint <= 0x7FFU) {
        encoded[encodedLength++] =
            static_cast<char>(0xC0U | (codePoint >> 6U));
        encoded[encodedLength++] =
            static_cast<char>(0x80U | (codePoint & 0x3FU));
    } else if (codePoint <= 0xFFFFU &&
               !(codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
        encoded[encodedLength++] =
            static_cast<char>(0xE0U | (codePoint >> 12U));
        encoded[encodedLength++] =
            static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
        encoded[encodedLength++] =
            static_cast<char>(0x80U | (codePoint & 0x3FU));
    } else if (codePoint <= 0x10FFFFU) {
        encoded[encodedLength++] =
            static_cast<char>(0xF0U | (codePoint >> 18U));
        encoded[encodedLength++] =
            static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU));
        encoded[encodedLength++] =
            static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
        encoded[encodedLength++] =
            static_cast<char>(0x80U | (codePoint & 0x3FU));
    } else {
        return fail(DatasetManifestJsonStatus::MalformedJson);
    }
    if (encodedLength > capacity - length) {
        return fail(DatasetManifestJsonStatus::CapacityExceeded);
    }
    std::memcpy(output + length, encoded.data(), encodedLength);
    length += encodedLength;
    return true;
}

bool JsonReader::parseString(char* output, std::size_t capacity,
                             std::size_t& length) noexcept {
    skipWhitespace();
    if (position_ >= input_.size() || input_[position_] != '"') {
        return fail(DatasetManifestJsonStatus::MalformedJson);
    }
    ++position_;
    length = 0U;
    while (position_ < input_.size()) {
        const unsigned char value =
            static_cast<unsigned char>(input_[position_++]);
        if (value == '"') {
            if (length >= capacity) {
                return fail(DatasetManifestJsonStatus::CapacityExceeded);
            }
            output[length] = '\0';
            if (std::memchr(output, '\0', length) != nullptr ||
                !validUtf8(std::string_view(output, length))) {
                return fail(DatasetManifestJsonStatus::InvalidValue);
            }
            return true;
        }
        if (value < 0x20U) {
            return fail(DatasetManifestJsonStatus::MalformedJson);
        }
        if (value != '\\') {
            if (length == capacity) {
                return fail(DatasetManifestJsonStatus::CapacityExceeded);
            }
            output[length++] = static_cast<char>(value);
            continue;
        }
        if (position_ >= input_.size()) {
            return fail(DatasetManifestJsonStatus::MalformedJson);
        }
        const char escape = input_[position_++];
        if (escape != 'u' && length == capacity) {
            return fail(DatasetManifestJsonStatus::CapacityExceeded);
        }
        switch (escape) {
            case '"': output[length++] = '"'; break;
            case '\\': output[length++] = '\\'; break;
            case '/': output[length++] = '/'; break;
            case 'b': output[length++] = '\b'; break;
            case 'f': output[length++] = '\f'; break;
            case 'n': output[length++] = '\n'; break;
            case 'r': output[length++] = '\r'; break;
            case 't': output[length++] = '\t'; break;
            case 'u': {
                const auto parseHexQuad = [this](std::uint32_t& result) {
                    if (input_.size() - position_ < 4U) {
                        return false;
                    }
                    result = 0U;
                    for (std::size_t index = 0U; index < 4U; ++index) {
                        const char digit = input_[position_++];
                        int nibble = -1;
                        if (digit >= '0' && digit <= '9') {
                            nibble = digit - '0';
                        } else if (digit >= 'a' && digit <= 'f') {
                            nibble = digit - 'a' + 10;
                        } else if (digit >= 'A' && digit <= 'F') {
                            nibble = digit - 'A' + 10;
                        }
                        if (nibble < 0) {
                            return false;
                        }
                        result = (result << 4U) |
                                 static_cast<std::uint32_t>(nibble);
                    }
                    return true;
                };
                std::uint32_t codePoint = 0U;
                if (!parseHexQuad(codePoint)) {
                    return fail(DatasetManifestJsonStatus::MalformedJson);
                }
                if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
                    if (input_.size() - position_ < 6U ||
                        input_[position_] != '\\' ||
                        input_[position_ + 1U] != 'u') {
                        return fail(DatasetManifestJsonStatus::MalformedJson);
                    }
                    position_ += 2U;
                    std::uint32_t low = 0U;
                    if (!parseHexQuad(low) || low < 0xDC00U ||
                        low > 0xDFFFU) {
                        return fail(DatasetManifestJsonStatus::MalformedJson);
                    }
                    codePoint = 0x10000U +
                                ((codePoint - 0xD800U) << 10U) +
                                (low - 0xDC00U);
                } else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU) {
                    return fail(DatasetManifestJsonStatus::MalformedJson);
                }
                if (!appendCodePoint(codePoint, output, capacity, length)) {
                    return false;
                }
                continue;
            }
            default:
                return fail(DatasetManifestJsonStatus::MalformedJson);
        }
        if (length > capacity) {
            return fail(DatasetManifestJsonStatus::CapacityExceeded);
        }
    }
    return fail(DatasetManifestJsonStatus::MalformedJson);
}

bool JsonReader::parseText(std::string_view& output) noexcept {
    if (storage_ == nullptr || storageUsed_ >= storageCapacity_) {
        return fail(DatasetManifestJsonStatus::CapacityExceeded);
    }
    const std::size_t start = storageUsed_;
    std::size_t length = 0U;
    if (!parseString(storage_ + start, storageCapacity_ - start, length)) {
        return false;
    }
    storageUsed_ += length + 1U;
    output = std::string_view(storage_ + start, length);
    return true;
}

bool JsonReader::parseUnsigned(std::uint64_t& value) noexcept {
    skipWhitespace();
    const std::size_t start = position_;
    if (position_ >= input_.size() || input_[position_] < '0' ||
        input_[position_] > '9') {
        return fail(DatasetManifestJsonStatus::MalformedJson);
    }
    if (input_[position_] == '0') {
        ++position_;
        if (position_ < input_.size() && input_[position_] >= '0' &&
            input_[position_] <= '9') {
            return fail(DatasetManifestJsonStatus::MalformedJson);
        }
    } else {
        while (position_ < input_.size() && input_[position_] >= '0' &&
               input_[position_] <= '9') {
            ++position_;
        }
    }
    const auto parsed =
        std::from_chars(input_.data() + start, input_.data() + position_, value);
    if (parsed.ec != std::errc{} || parsed.ptr != input_.data() + position_) {
        return fail(DatasetManifestJsonStatus::InvalidValue);
    }
    return true;
}

bool JsonReader::parseBoolean(bool& value) noexcept {
    skipWhitespace();
    if (input_.substr(position_, 4U) == "true") {
        value = true;
        position_ += 4U;
        return true;
    }
    if (input_.substr(position_, 5U) == "false") {
        value = false;
        position_ += 5U;
        return true;
    }
    return fail(DatasetManifestJsonStatus::MalformedJson);
}

bool JsonReader::parseSourceIds(
    DatasetManifestDocument& document) noexcept {
    if (!consume('[')) {
        return false;
    }
    for (std::size_t index = 0U; index < document.sourceIds.size(); ++index) {
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == ']') {
            if (index == 0U) {
                return fail(DatasetManifestJsonStatus::InvalidValue);
            }
            ++position_;
            document.sourceIdCount = index;
            return true;
        }
        if (index != 0U && !consume(',')) {
            return false;
        }
        if (!parseText(document.sourceIds[index])) {
            return false;
        }
    }
    skipWhitespace();
    if (position_ >= input_.size() || input_[position_] != ']') {
        return fail(DatasetManifestJsonStatus::CapacityExceeded);
    }
    ++position_;
    document.sourceIdCount = document.sourceIds.size();
    return true;
}

bool JsonReader::parseChannelPresence(
    DatasetManifestDocument& document) noexcept {
    if (!consume('[')) {
        return false;
    }
    for (std::size_t index = 0U; index < kDsdRawChannelCount; ++index) {
        if (index != 0U && !consume(',')) {
            return false;
        }
        if (!parseBoolean(document.raw.channelPresence[index])) {
            return false;
        }
    }
    if (!consume(']')) {
        return false;
    }
    document.raw.channelPresenceConfigured = true;
    return true;
}

bool JsonReader::parseRejectionCounts(
    DatasetManifestDocument& document) noexcept {
    if (!consume('[')) {
        return false;
    }
    for (std::size_t index = 0U;
         index < document.rejectionCounts.size(); ++index) {
        if (index != 0U && !consume(',')) {
            return false;
        }
        if (!parseUnsigned(document.rejectionCounts[index])) {
            return false;
        }
    }
    return consume(']');
}

bool JsonReader::parseStringField(
    JsonField field, DatasetManifestDocument& document) noexcept {
    std::string_view* destination = nullptr;
    switch (field) {
        case JsonField::ArtifactName: destination = &document.artifactName; break;
        case JsonField::DsdDocumentVersion:
            destination = &document.dsdDocumentVersion;
            break;
        case JsonField::ArtifactSchemaSha256:
            destination = &document.artifactSchemaSha256;
            break;
        case JsonField::FirmwareId: destination = &document.firmwareId; break;
        case JsonField::ConfigId: destination = &document.configId; break;
        case JsonField::CalibrationId:
            destination = &document.calibrationId;
            break;
        case JsonField::FssId: destination = &document.fssId; break;
        case JsonField::ModelId: destination = &document.modelId; break;
        case JsonField::GeneratorVersion:
            destination = &document.generatorVersion;
            break;
        case JsonField::ReplaySourceSha256:
            destination = &document.replaySourceSha256;
            break;
        case JsonField::CreatedAt: destination = &document.createdAt; break;
        case JsonField::ToolVersion: destination = &document.toolVersion; break;
        case JsonField::SplitPolicyVersion:
            destination = &document.splitPolicyVersion;
            break;
        case JsonField::TargetContractId:
            destination = &document.targetContractId;
            break;
        case JsonField::TargetContractState:
            destination = &document.targetContractState;
            break;
        case JsonField::Limitations: destination = &document.limitations; break;
        case JsonField::ApprovalState:
            destination = &document.approvalState;
            break;
        default:
            return fail(DatasetManifestJsonStatus::InvalidValue);
    }
    return parseText(*destination);
}

bool JsonReader::parseValue(JsonField field,
                            DatasetManifestDocument& document) noexcept {
    std::uint64_t number = 0U;
    switch (field) {
        case JsonField::ManifestSchemaVersion:
            if (!parseUnsigned(number)) return false;
            if (number != kDatasetManifestJsonSchemaVersion) {
                return fail(DatasetManifestJsonStatus::UnsupportedVersion);
            }
            document.manifestSchemaVersion =
                static_cast<std::uint32_t>(number);
            return true;
        case JsonField::DatasetId:
            return parseText(document.raw.datasetId);
        case JsonField::DsdDocumentVersion:
            return parseStringField(field, document);
        case JsonField::ArtifactSchemaVersion:
            if (!parseUnsigned(number)) return false;
            if (number > std::numeric_limits<std::uint32_t>::max()) {
                return fail(DatasetManifestJsonStatus::InvalidValue);
            }
            document.artifactSchemaVersion =
                static_cast<std::uint32_t>(number);
            document.raw.rawSchemaVersion =
                document.artifactSchemaVersion;
            return true;
        case JsonField::ArtifactBytes:
            return parseUnsigned(document.raw.expectedBytes);
        case JsonField::ArtifactRows:
            return parseUnsigned(document.raw.expectedRows);
        case JsonField::ArtifactSha256: {
            std::string_view text{};
            if (!parseText(text)) return false;
            if (!parseSha256(text, document.raw.expectedSha256)) {
                return fail(DatasetManifestJsonStatus::InvalidValue);
            }
            return true;
        }
        case JsonField::DataOrigin:
        case JsonField::EvidenceClass: {
            std::string_view text{};
            if (!parseText(text)) return false;
            const bool valid =
                field == JsonField::DataOrigin
                    ? parseDataOrigin(text, document.raw.provenance.origin)
                    : parseEvidenceClass(
                          text, document.raw.provenance.evidenceClass);
            if (!valid) return fail(DatasetManifestJsonStatus::InvalidValue);
            return true;
        }
        case JsonField::ChannelPresence:
            return parseChannelPresence(document);
        case JsonField::SourceIds:
            return parseSourceIds(document);
        case JsonField::GeneratorSeed: {
            skipWhitespace();
            if (position_ < input_.size() && input_[position_] == '"') {
                std::string_view state{};
                if (!parseText(state)) return false;
                if (state == "UNKNOWN") {
                    document.generatorSeedState =
                        DatasetManifestValueState::Unknown;
                } else if (state == "NOT_APPLICABLE") {
                    document.generatorSeedState =
                        DatasetManifestValueState::NotApplicable;
                } else {
                    return fail(DatasetManifestJsonStatus::InvalidValue);
                }
                return true;
            }
            if (!parseUnsigned(document.generatorSeed)) return false;
            document.generatorSeedState = DatasetManifestValueState::Known;
            return true;
        }
        case JsonField::AcceptedRows:
            return parseUnsigned(document.acceptedRows);
        case JsonField::RejectedRows:
            return parseUnsigned(document.rejectedRows);
        case JsonField::RejectionCounts:
            return parseRejectionCounts(document);
        default:
            return parseStringField(field, document);
    }
}

DatasetManifestJsonStatus JsonReader::parse(
    DatasetManifestDocument& document) noexcept {
    document = {};
    if (storage_ == nullptr && storageCapacity_ != 0U) {
        return DatasetManifestJsonStatus::InvalidArgument;
    }
    if (input_.empty()) {
        return DatasetManifestJsonStatus::MalformedJson;
    }
    if (input_.size() > kMaxDatasetManifestJsonBytes) {
        return DatasetManifestJsonStatus::CapacityExceeded;
    }
    if (!consume('{')) {
        return status_;
    }
    std::uint64_t seen = 0U;
    bool first = true;
    for (;;) {
        skipWhitespace();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            break;
        }
        if (!first && !consume(',')) {
            return status_;
        }
        first = false;
        char key[64]{};
        std::size_t keyLength = 0U;
        if (!parseString(key, sizeof(key), keyLength) || !consume(':')) {
            return status_;
        }
        const JsonField field =
            identifyJsonField(std::string_view(key, keyLength));
        if (field == JsonField::Count) {
            return DatasetManifestJsonStatus::UnknownField;
        }
        const std::size_t bitIndex = static_cast<std::size_t>(field);
        const std::uint64_t bit = std::uint64_t{1U} << bitIndex;
        if ((seen & bit) != 0U) {
            return DatasetManifestJsonStatus::DuplicateField;
        }
        seen |= bit;
        if (!parseValue(field, document)) {
            return status_;
        }
    }
    skipWhitespace();
    if (position_ != input_.size()) {
        return DatasetManifestJsonStatus::MalformedJson;
    }
    constexpr std::uint64_t kRequiredFields =
        (std::uint64_t{1U} << kManifestJsonFieldCount) - 1U;
    if (seen != kRequiredFields) {
        return DatasetManifestJsonStatus::MissingField;
    }
    return validateDatasetManifestDocument(document);
}

bool appendJsonKey(JsonBufferWriter& writer, std::string_view key,
                   bool& first) noexcept {
    if ((!first && !writer.append(',')) || !writer.appendQuoted(key) ||
        !writer.append(':')) {
        return false;
    }
    first = false;
    return true;
}

bool appendJsonStringField(JsonBufferWriter& writer, std::string_view key,
                           std::string_view value, bool& first) noexcept {
    return appendJsonKey(writer, key, first) && writer.appendQuoted(value);
}

bool appendJsonUnsignedField(JsonBufferWriter& writer, std::string_view key,
                             std::uint64_t value, bool& first) noexcept {
    return appendJsonKey(writer, key, first) && writer.appendUnsigned(value);
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

DatasetManifestJsonStatus writeRawArtifactSchemaSha256(
    char* output, std::size_t capacity,
    std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    if (output == nullptr || capacity < Sha256::kDigestSize * 2U) {
        return DatasetManifestJsonStatus::InvalidArgument;
    }
    std::array<std::uint8_t, Sha256::kDigestSize> digest{};
    if (!rawSchemaSha256(digest)) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    formatSha256(digest, output);
    bytesWritten = digest.size() * 2U;
    return DatasetManifestJsonStatus::Ok;
}

DatasetManifestJsonStatus validateDatasetManifestDocument(
    const DatasetManifestDocument& document) noexcept {
    if (document.generatorSeedState != DatasetManifestValueState::Known &&
        document.generatorSeedState != DatasetManifestValueState::Unknown &&
        document.generatorSeedState !=
            DatasetManifestValueState::NotApplicable) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    if (document.manifestSchemaVersion !=
            kDatasetManifestJsonSchemaVersion ||
        !DatasetValidator::validateManifest(document.raw) ||
        document.dsdDocumentVersion !=
            kRawDatasetDsdDocumentVersion ||
        document.artifactSchemaVersion !=
            document.raw.rawSchemaVersion ||
        document.artifactName != "raw_samples.csv" ||
        !validText(document.raw.datasetId) ||
        document.raw.datasetId == "UNKNOWN" ||
        document.raw.datasetId == "NOT_APPLICABLE" ||
        !validText(document.artifactName) ||
        !validText(document.firmwareId) || !validText(document.configId) ||
        !validText(document.calibrationId) || !validText(document.fssId) ||
        !validText(document.modelId) ||
        !validText(document.generatorVersion) ||
        !validText(document.replaySourceSha256) ||
        !validText(document.createdAt) || !validText(document.toolVersion) ||
        !validText(document.splitPolicyVersion) ||
        !validText(document.targetContractId) ||
        !validText(document.targetContractState) ||
        !validText(document.limitations) ||
        !validText(document.approvalState)) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    if (document.sourceIdCount == 0U ||
        document.sourceIdCount > document.sourceIds.size()) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    for (std::size_t index = 0U; index < document.sourceIdCount; ++index) {
        if (!validText(document.sourceIds[index])) {
            return DatasetManifestJsonStatus::InvalidManifest;
        }
    }

    std::array<std::uint8_t, Sha256::kDigestSize> schemaHash{};
    if (!parseSha256(document.artifactSchemaSha256, schemaHash)) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    std::array<std::uint8_t, Sha256::kDigestSize> expectedSchemaHash{};
    if (!rawSchemaSha256(expectedSchemaHash) ||
        schemaHash != expectedSchemaHash) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }

    const bool synthetic =
        document.raw.provenance.origin == DataOrigin::Synthetic;
    const bool replay =
        document.raw.provenance.origin == DataOrigin::Replay;
    if (synthetic) {
        if (document.generatorVersion == "UNKNOWN" ||
            document.generatorVersion == "NOT_APPLICABLE" ||
            document.generatorSeedState != DatasetManifestValueState::Known ||
            document.replaySourceSha256 != "NOT_APPLICABLE") {
            return DatasetManifestJsonStatus::InvalidManifest;
        }
    } else if (replay) {
        if (document.generatorVersion != "NOT_APPLICABLE" ||
            document.generatorSeedState !=
                DatasetManifestValueState::NotApplicable) {
            return DatasetManifestJsonStatus::InvalidManifest;
        }
    } else if (document.generatorSeedState ==
                   DatasetManifestValueState::Known ||
               (document.generatorSeedState ==
                    DatasetManifestValueState::NotApplicable &&
                document.generatorVersion != "NOT_APPLICABLE")) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    if (replay) {
        std::array<std::uint8_t, Sha256::kDigestSize> replayHash{};
        if (!parseSha256(document.replaySourceSha256, replayHash)) {
            return DatasetManifestJsonStatus::InvalidManifest;
        }
    } else if (document.replaySourceSha256 != "NOT_APPLICABLE" &&
               document.replaySourceSha256 != "UNKNOWN") {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    if (document.generatorSeedState ==
            DatasetManifestValueState::Unknown &&
        synthetic) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    if (document.generatorSeedState != DatasetManifestValueState::Known &&
        document.generatorSeed != 0U) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    if (document.targetContractState != "UNKNOWN" &&
        document.targetContractState != "NOT_APPLICABLE" &&
        document.targetContractState != "UNDEFINED" &&
        document.targetContractState != "UNAPPROVED" &&
        document.targetContractState != "APPROVED") {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    if (document.targetContractState == "APPROVED" &&
        (document.targetContractId == "UNKNOWN" ||
         document.targetContractId == "NOT_APPLICABLE" ||
         document.targetContractId == "UNDEFINED")) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    if (document.approvalState != "UNKNOWN" &&
        document.approvalState != "NOT_APPLICABLE" &&
        document.approvalState != "NOT_APPROVED" &&
        document.approvalState != "APPROVED") {
        return DatasetManifestJsonStatus::InvalidManifest;
    }

    if (document.acceptedRows >
        std::numeric_limits<std::uint64_t>::max() - document.rejectedRows) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    if (document.acceptedRows + document.rejectedRows !=
        document.raw.expectedRows) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    std::uint64_t rejectionSum = 0U;
    for (const std::uint64_t count : document.rejectionCounts) {
        if (count > std::numeric_limits<std::uint64_t>::max() -
                        rejectionSum) {
            return DatasetManifestJsonStatus::InvalidManifest;
        }
        rejectionSum += count;
    }
    if (rejectionSum != document.rejectedRows) {
        return DatasetManifestJsonStatus::InvalidManifest;
    }
    return DatasetManifestJsonStatus::Ok;
}

DatasetManifestJsonStatus writeDatasetManifestJson(
    const DatasetManifestDocument& document, char* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    if (output == nullptr) {
        return DatasetManifestJsonStatus::InvalidArgument;
    }
    const DatasetManifestJsonStatus validationStatus =
        validateDatasetManifestDocument(document);
    if (validationStatus != DatasetManifestJsonStatus::Ok) {
        return validationStatus;
    }

    JsonBufferWriter writer(output, capacity);
    bool first = true;
    char digest[Sha256::kDigestSize * 2U]{};
    formatSha256(document.raw.expectedSha256, digest);
    if (!writer.append('{') ||
        !appendJsonUnsignedField(
            writer, "manifest_schema_version", document.manifestSchemaVersion,
            first) ||
        !appendJsonStringField(writer, "dataset_id", document.raw.datasetId,
                               first) ||
        !appendJsonStringField(writer, "dsd_document_version",
                               document.dsdDocumentVersion, first) ||
        !appendJsonStringField(writer, "artifact_name",
                               document.artifactName, first) ||
        !appendJsonUnsignedField(writer, "artifact_schema_version",
                                 document.artifactSchemaVersion, first) ||
        !appendJsonStringField(writer, "artifact_schema_sha256",
                               document.artifactSchemaSha256, first) ||
        !appendJsonUnsignedField(writer, "artifact_bytes",
                                 document.raw.expectedBytes, first) ||
        !appendJsonUnsignedField(writer, "artifact_rows",
                                 document.raw.expectedRows, first) ||
        !appendJsonStringField(
            writer, "artifact_sha256",
            std::string_view(digest, sizeof(digest)), first) ||
        !appendJsonStringField(
            writer, "data_origin",
            dataOriginName(document.raw.provenance.origin), first) ||
        !appendJsonStringField(
            writer, "evidence_class",
            evidenceClassName(document.raw.provenance.evidenceClass), first) ||
        !appendJsonKey(writer, "channel_presence", first) ||
        !writer.append('[')) {
        return writer.valid() ? DatasetManifestJsonStatus::InvalidManifest
                              : DatasetManifestJsonStatus::CapacityExceeded;
    }
    for (std::size_t index = 0U; index < kDsdRawChannelCount; ++index) {
        if ((index != 0U && !writer.append(',')) ||
            !writer.append(document.raw.channelPresence[index] ? "true"
                                                               : "false")) {
            return DatasetManifestJsonStatus::CapacityExceeded;
        }
    }
    if (!writer.append(']') ||
        !appendJsonKey(writer, "source_ids", first) ||
        !writer.append('[')) {
        return DatasetManifestJsonStatus::CapacityExceeded;
    }
    for (std::size_t index = 0U; index < document.sourceIdCount; ++index) {
        if ((index != 0U && !writer.append(',')) ||
            !writer.appendQuoted(document.sourceIds[index])) {
            return DatasetManifestJsonStatus::CapacityExceeded;
        }
    }
    if (!writer.append(']') ||
        !appendJsonStringField(writer, "firmware_id", document.firmwareId,
                               first) ||
        !appendJsonStringField(writer, "config_id", document.configId,
                               first) ||
        !appendJsonStringField(writer, "calibration_id",
                               document.calibrationId, first) ||
        !appendJsonStringField(writer, "fss_id", document.fssId, first) ||
        !appendJsonStringField(writer, "model_id", document.modelId, first) ||
        !appendJsonStringField(writer, "generator_version",
                               document.generatorVersion, first) ||
        !appendJsonKey(writer, "generator_seed", first)) {
        return DatasetManifestJsonStatus::CapacityExceeded;
    }
    if (document.generatorSeedState == DatasetManifestValueState::Known) {
        if (!writer.appendUnsigned(document.generatorSeed)) {
            return DatasetManifestJsonStatus::CapacityExceeded;
        }
    } else if (!writer.appendQuoted(
                   document.generatorSeedState ==
                           DatasetManifestValueState::Unknown
                       ? "UNKNOWN"
                       : "NOT_APPLICABLE")) {
        return DatasetManifestJsonStatus::CapacityExceeded;
    }
    if (!appendJsonStringField(writer, "replay_source_sha256",
                               document.replaySourceSha256, first) ||
        !appendJsonStringField(writer, "created_at", document.createdAt,
                               first) ||
        !appendJsonStringField(writer, "tool_version", document.toolVersion,
                               first) ||
        !appendJsonUnsignedField(writer, "accepted_rows",
                                 document.acceptedRows, first) ||
        !appendJsonUnsignedField(writer, "rejected_rows",
                                 document.rejectedRows, first) ||
        !appendJsonKey(writer, "rejection_counts", first) ||
        !writer.append('[')) {
        return DatasetManifestJsonStatus::CapacityExceeded;
    }
    for (std::size_t index = 0U; index < document.rejectionCounts.size();
         ++index) {
        if ((index != 0U && !writer.append(',')) ||
            !writer.appendUnsigned(document.rejectionCounts[index])) {
            return DatasetManifestJsonStatus::CapacityExceeded;
        }
    }
    if (!writer.append(']') ||
        !appendJsonStringField(writer, "split_policy_version",
                               document.splitPolicyVersion, first) ||
        !appendJsonStringField(writer, "target_contract_id",
                               document.targetContractId, first) ||
        !appendJsonStringField(writer, "target_contract_state",
                               document.targetContractState, first) ||
        !appendJsonStringField(writer, "limitations", document.limitations,
                               first) ||
        !appendJsonStringField(writer, "approval_state",
                               document.approvalState, first) ||
        !writer.append("}\n")) {
        return DatasetManifestJsonStatus::CapacityExceeded;
    }
    bytesWritten = writer.size();
    return DatasetManifestJsonStatus::Ok;
}

DatasetManifestJsonStatus readDatasetManifestJson(
    std::string_view input, char* stringStorage,
    std::size_t stringStorageCapacity,
    DatasetManifestDocument& document) noexcept {
    JsonReader reader(input, stringStorage, stringStorageCapacity);
    return reader.parse(document);
}

DatasetManifestJsonStatus validateDatasetManifestResult(
    const DatasetManifestDocument& document,
    const DatasetValidationReport& report) noexcept {
    const DatasetManifestJsonStatus documentStatus =
        validateDatasetManifestDocument(document);
    if (documentStatus != DatasetManifestJsonStatus::Ok) {
        return documentStatus;
    }
    if (report.status != DatasetValidationStatus::Complete &&
        report.status != DatasetValidationStatus::CompleteWithRejectedRows) {
        return DatasetManifestJsonStatus::ArtifactValidationFailed;
    }
    if (!report.hashVerified ||
        report.computedSha256 != document.raw.expectedSha256) {
        return DatasetManifestJsonStatus::ArtifactHashMismatch;
    }
    if (report.bytesRead != document.raw.expectedBytes) {
        return DatasetManifestJsonStatus::ArtifactByteCountMismatch;
    }
    if (!report.rowCountVerified ||
        report.dataRowsSeen != document.raw.expectedRows ||
        report.acceptedRows >
            std::numeric_limits<std::uint64_t>::max() - report.rejectedRows ||
        report.acceptedRows + report.rejectedRows != report.dataRowsSeen) {
        return DatasetManifestJsonStatus::ArtifactRowCountMismatch;
    }
    if (report.acceptedRows != document.acceptedRows ||
        report.rejectedRows != document.rejectedRows ||
        report.rejectionCounts != document.rejectionCounts) {
        return DatasetManifestJsonStatus::RejectionCountMismatch;
    }
    return DatasetManifestJsonStatus::Ok;
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
