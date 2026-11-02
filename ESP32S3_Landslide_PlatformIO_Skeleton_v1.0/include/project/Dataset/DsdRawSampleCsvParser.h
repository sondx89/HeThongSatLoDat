#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "DsdRawSampleCsvParser is available only to host data tools"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Dataset/CsvSchemas.h"
#include "Dataset/Sha256.h"

namespace project::dataset {

inline constexpr std::uint32_t kRawSampleCoreSchemaVersion = 1U;
inline constexpr std::size_t kMaxRawCsvRecordBytes = 4096U;

enum class DatasetCsvSourceStatus : std::uint8_t {
    Ok,
    EndOfFile,
    IoError
};

class IDatasetCsvSource {
public:
    virtual ~IDatasetCsvSource() = default;
    virtual DatasetCsvSourceStatus rewind() noexcept = 0;
    virtual DatasetCsvSourceStatus read(std::uint8_t* output,
                                        std::size_t capacity,
                                        std::size_t& bytesRead) noexcept = 0;
};

struct RawCsvRowView {
    std::array<std::string_view, kDsdV1_2RawSampleCoreFields.size()> fields{};
    std::uint64_t recordNumber{0U};
    std::uint64_t byteOffset{0U};
};

enum class RawCsvParserStatus : std::uint8_t {
    Row,
    EndOfFile,
    DigestReady,
    HeaderSchemaMismatch,
    WrongColumnCount,
    MalformedQuote,
    InvalidUtf8,
    RecordTooLong,
    TruncatedRecord,
    SourceError,
    InvalidState
};

class DsdRawSampleCsvParser final {
public:
    explicit DsdRawSampleCsvParser(IDatasetCsvSource& source) noexcept;

    RawCsvParserStatus begin() noexcept;
    RawCsvParserStatus next(RawCsvRowView& output) noexcept;
    RawCsvParserStatus finishDigest(
        std::array<std::uint8_t, Sha256::kDigestSize>& digest,
        std::uint64_t& byteCount) noexcept;

    std::uint64_t bytesConsumed() const noexcept { return bytesHashed_; }

private:
    enum class ReadStatus : std::uint8_t {
        Byte,
        EndOfFile,
        Error
    };

    enum class FieldState : std::uint8_t {
        Start,
        Unquoted,
        Quoted,
        AfterQuote
    };

    ReadStatus readByte(std::uint8_t& value) noexcept;
    RawCsvParserStatus readRecord(RawCsvRowView& output) noexcept;
    RawCsvParserStatus finishRecord(RawCsvRowView& output,
                                    std::size_t fieldCount,
                                    std::size_t fieldStart,
                                    std::size_t writePosition,
                                    bool tooLong,
                                    bool tooManyFields) noexcept;
    bool appendByte(char value, std::size_t& writePosition) noexcept;
    static bool validUtf8(std::string_view value) noexcept;
    RawCsvParserStatus discardMalformedRecord(RawCsvRowView& output,
                                              std::uint64_t byteOffset) noexcept;

    IDatasetCsvSource& source_;
    std::array<std::uint8_t, 128U> readBuffer_{};
    std::array<char, kMaxRawCsvRecordBytes> recordBuffer_{};
    std::array<std::size_t, kDsdV1_2RawSampleCoreFields.size()> fieldStarts_{};
    std::array<std::size_t, kDsdV1_2RawSampleCoreFields.size()> fieldLengths_{};
    Sha256 sha256_{};
    std::size_t readPosition_{0U};
    std::size_t readLength_{0U};
    std::uint64_t bytesHashed_{0U};
    std::uint64_t streamPosition_{0U};
    std::uint64_t recordNumber_{0U};
    bool sourceEnded_{false};
    bool begun_{false};
    bool digestFinished_{false};
};

}  // namespace project::dataset
