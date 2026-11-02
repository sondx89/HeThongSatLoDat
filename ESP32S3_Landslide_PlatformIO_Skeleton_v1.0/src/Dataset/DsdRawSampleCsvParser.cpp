#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/DsdRawSampleCsvParser.h"

#include <limits>

namespace project::dataset {

DsdRawSampleCsvParser::DsdRawSampleCsvParser(
    IDatasetCsvSource& source) noexcept
    : source_(source) {}

RawCsvParserStatus DsdRawSampleCsvParser::begin() noexcept {
    const DatasetCsvSourceStatus status = source_.rewind();
    if (status != DatasetCsvSourceStatus::Ok) {
        return RawCsvParserStatus::SourceError;
    }
    readPosition_ = 0U;
    readLength_ = 0U;
    bytesHashed_ = 0U;
    streamPosition_ = 0U;
    recordNumber_ = 0U;
    sourceEnded_ = false;
    digestFinished_ = false;
    begun_ = true;
    sha256_.reset();

    RawCsvRowView header{};
    const RawCsvParserStatus rowStatus = readRecord(header);
    if (rowStatus != RawCsvParserStatus::Row) {
        return rowStatus == RawCsvParserStatus::EndOfFile
                   ? RawCsvParserStatus::HeaderSchemaMismatch
                   : rowStatus;
    }
    if (header.fields.size() != kDsdV1_2RawSampleCoreFields.size()) {
        return RawCsvParserStatus::HeaderSchemaMismatch;
    }
    for (std::size_t index = 0U; index < header.fields.size(); ++index) {
        if (header.fields[index] != kDsdV1_2RawSampleCoreFields[index]) {
            return RawCsvParserStatus::HeaderSchemaMismatch;
        }
    }
    return RawCsvParserStatus::Row;
}

DsdRawSampleCsvParser::ReadStatus DsdRawSampleCsvParser::readByte(
    std::uint8_t& value) noexcept {
    if (readPosition_ == readLength_) {
        if (sourceEnded_) {
            return ReadStatus::EndOfFile;
        }
        std::size_t bytesRead = 0U;
        const DatasetCsvSourceStatus status = source_.read(
            readBuffer_.data(), readBuffer_.size(), bytesRead);
        if (bytesRead > readBuffer_.size() ||
            status == DatasetCsvSourceStatus::IoError ||
            (status == DatasetCsvSourceStatus::EndOfFile &&
             bytesRead != 0U)) {
            return ReadStatus::Error;
        }
        if (status == DatasetCsvSourceStatus::EndOfFile) {
            sourceEnded_ = true;
            return ReadStatus::EndOfFile;
        }
        if (bytesRead == 0U ||
            bytesRead >
                std::numeric_limits<std::uint64_t>::max() - bytesHashed_ ||
            !sha256_.update(readBuffer_.data(), bytesRead)) {
            return ReadStatus::Error;
        }
        bytesHashed_ += static_cast<std::uint64_t>(bytesRead);
        readPosition_ = 0U;
        readLength_ = bytesRead;
    }
    value = readBuffer_[readPosition_++];
    if (streamPosition_ == std::numeric_limits<std::uint64_t>::max()) {
        return ReadStatus::Error;
    }
    ++streamPosition_;
    return ReadStatus::Byte;
}

bool DsdRawSampleCsvParser::appendByte(char value,
                                      std::size_t& writePosition) noexcept {
    if (writePosition >= recordBuffer_.size()) {
        return false;
    }
    recordBuffer_[writePosition++] = value;
    return true;
}

bool DsdRawSampleCsvParser::validUtf8(std::string_view value) noexcept {
    std::size_t index = 0U;
    while (index < value.size()) {
        const auto first = static_cast<std::uint8_t>(value[index]);
        if (first == 0U) {
            return false;
        }
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        std::uint32_t codePoint = 0U;
        std::uint32_t minimum = 0U;
        std::size_t continuationCount = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            codePoint = first & 0x1FU;
            minimum = 0x80U;
            continuationCount = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            codePoint = first & 0x0FU;
            minimum = 0x800U;
            continuationCount = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            codePoint = first & 0x07U;
            minimum = 0x10000U;
            continuationCount = 3U;
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
        if (codePoint < minimum || codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
            return false;
        }
        index += continuationCount + 1U;
    }
    return true;
}

RawCsvParserStatus DsdRawSampleCsvParser::finishRecord(
    RawCsvRowView& output, std::size_t fieldCount,
    std::size_t fieldStart, std::size_t writePosition, bool tooLong,
    bool tooManyFields) noexcept {
    if (fieldCount < fieldStarts_.size()) {
        fieldStarts_[fieldCount] = fieldStart;
        fieldLengths_[fieldCount] = writePosition - fieldStart;
        ++fieldCount;
    } else {
        tooManyFields = true;
    }
    ++recordNumber_;
    output = {};
    output.recordNumber = recordNumber_;
    output.byteOffset = streamPosition_;
    if (tooLong) {
        return RawCsvParserStatus::RecordTooLong;
    }
    if (tooManyFields || fieldCount != output.fields.size()) {
        return RawCsvParserStatus::WrongColumnCount;
    }
    for (std::size_t index = 0U; index < fieldCount; ++index) {
        output.fields[index] = std::string_view(
            recordBuffer_.data() + fieldStarts_[index], fieldLengths_[index]);
        if (!validUtf8(output.fields[index])) {
            return RawCsvParserStatus::InvalidUtf8;
        }
    }
    return RawCsvParserStatus::Row;
}

RawCsvParserStatus DsdRawSampleCsvParser::discardMalformedRecord(
    RawCsvRowView& output, std::uint64_t byteOffset) noexcept {
    std::uint8_t value = 0U;
    for (;;) {
        const ReadStatus status = readByte(value);
        if (status == ReadStatus::Error) {
            return RawCsvParserStatus::SourceError;
        }
        if (status == ReadStatus::EndOfFile) {
            return RawCsvParserStatus::TruncatedRecord;
        }
        if (value == '\n') {
            ++recordNumber_;
            output = {};
            output.recordNumber = recordNumber_;
            output.byteOffset = byteOffset;
            return RawCsvParserStatus::MalformedQuote;
        }
    }
}

RawCsvParserStatus DsdRawSampleCsvParser::readRecord(
    RawCsvRowView& output) noexcept {
    if (!begun_ || digestFinished_) {
        return RawCsvParserStatus::InvalidState;
    }
    output = {};
    recordBuffer_.fill(0);
    fieldStarts_.fill(0U);
    fieldLengths_.fill(0U);
    std::size_t fieldCount = 0U;
    std::size_t writePosition = 0U;
    std::size_t fieldStart = 0U;
    std::uint64_t recordBytes = 0U;
    FieldState state = FieldState::Start;
    bool tooLong = false;
    bool tooManyFields = false;
    bool consumed = false;
    const std::uint64_t recordStart = streamPosition_;

    const auto endField = [&]() noexcept {
        if (fieldCount < fieldStarts_.size()) {
            fieldStarts_[fieldCount] = fieldStart;
            fieldLengths_[fieldCount] = writePosition - fieldStart;
            ++fieldCount;
        } else {
            tooManyFields = true;
        }
        fieldStart = writePosition;
        state = FieldState::Start;
    };
    const auto add = [&](std::uint8_t byte) noexcept {
        if (!appendByte(static_cast<char>(byte), writePosition)) {
            tooLong = true;
        }
    };

    for (;;) {
        std::uint8_t value = 0U;
        const ReadStatus readStatus = readByte(value);
        if (readStatus == ReadStatus::Error) {
            return RawCsvParserStatus::SourceError;
        }
        if (readStatus == ReadStatus::EndOfFile) {
            if (!consumed) {
                return RawCsvParserStatus::EndOfFile;
            }
            output = {};
            output.recordNumber = recordNumber_ + 1U;
            output.byteOffset = recordStart;
            return RawCsvParserStatus::TruncatedRecord;
        }
        consumed = true;
        ++recordBytes;
        if (recordBytes > kMaxRawCsvRecordBytes) {
            tooLong = true;
        }

        if (state == FieldState::Start) {
            if (value == '"') {
                state = FieldState::Quoted;
            } else if (value == ',') {
                endField();
            } else if (value == '\n') {
                const RawCsvParserStatus result =
                    finishRecord(output, fieldCount, fieldStart,
                                 writePosition, tooLong, tooManyFields);
                output.byteOffset = recordStart;
                return result;
            } else if (value == '\r') {
                return discardMalformedRecord(output, recordStart);
            } else {
                add(value);
                state = FieldState::Unquoted;
            }
        } else if (state == FieldState::Unquoted) {
            if (value == '"') {
                return discardMalformedRecord(output, recordStart);
            }
            if (value == ',') {
                endField();
            } else if (value == '\n') {
                const RawCsvParserStatus result =
                    finishRecord(output, fieldCount, fieldStart,
                                 writePosition, tooLong, tooManyFields);
                output.byteOffset = recordStart;
                return result;
            } else if (value == '\r') {
                return discardMalformedRecord(output, recordStart);
            } else {
                add(value);
            }
        } else if (state == FieldState::Quoted) {
            if (value == '"') {
                state = FieldState::AfterQuote;
            } else {
                add(value);
            }
        } else {
            if (value == '"') {
                add(value);
                state = FieldState::Quoted;
            } else if (value == ',') {
                endField();
            } else if (value == '\n') {
                const RawCsvParserStatus result =
                    finishRecord(output, fieldCount, fieldStart,
                                 writePosition, tooLong, tooManyFields);
                output.byteOffset = recordStart;
                return result;
            } else {
                return discardMalformedRecord(output, recordStart);
            }
        }
    }
}

RawCsvParserStatus DsdRawSampleCsvParser::next(
    RawCsvRowView& output) noexcept {
    return readRecord(output);
}

RawCsvParserStatus DsdRawSampleCsvParser::finishDigest(
    std::array<std::uint8_t, Sha256::kDigestSize>& digest,
    std::uint64_t& byteCount) noexcept {
    byteCount = 0U;
    if (!begun_ || digestFinished_ || !sourceEnded_ ||
        readPosition_ != readLength_) {
        return RawCsvParserStatus::InvalidState;
    }
    if (!sha256_.finish(digest)) {
        return RawCsvParserStatus::SourceError;
    }
    digestFinished_ = true;
    byteCount = bytesHashed_;
    return RawCsvParserStatus::DigestReady;
}

}  // namespace project::dataset

#endif
