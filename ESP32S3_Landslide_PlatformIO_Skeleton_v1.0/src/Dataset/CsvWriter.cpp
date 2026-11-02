#include "project/Dataset/CsvWriter.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>

namespace project::dataset {
namespace {

bool validUtf8(std::string_view value) noexcept {
    std::size_t index = 0U;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
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
                static_cast<unsigned char>(value[index + offset]);
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

template <typename Number>
std::to_chars_result formatNumber(char* begin, char* end,
                                  Number value) noexcept {
    return std::to_chars(begin, end, value, std::chars_format::general);
}

}  // namespace

CsvRowWriter::CsvRowWriter(char* output, std::size_t capacity,
                           std::size_t expectedColumns) noexcept
    : output_(output),
      capacity_(capacity),
      expectedColumns_(expectedColumns) {
    if (output_ == nullptr || expectedColumns_ == 0U) {
        status_ = CsvWriteStatus::InvalidArgument;
    }
}

CsvWriteStatus CsvRowWriter::fail(CsvWriteStatus status) noexcept {
    if (status_ == CsvWriteStatus::Ok) {
        status_ = status;
    }
    return status_;
}

CsvWriteStatus CsvRowWriter::appendField(std::string_view value) noexcept {
    if (status_ != CsvWriteStatus::Ok) {
        return status_;
    }
    if (finished_) {
        return fail(CsvWriteStatus::AlreadyFinished);
    }
    if (columnsWritten_ >= expectedColumns_) {
        return fail(CsvWriteStatus::ColumnCountExceeded);
    }
    if (value.data() == nullptr && !value.empty()) {
        return fail(CsvWriteStatus::InvalidArgument);
    }
    if (!validUtf8(value)) {
        return fail(CsvWriteStatus::InvalidUtf8);
    }

    bool needsQuotes = false;
    std::size_t embeddedQuotes = 0U;
    for (const char character : value) {
        if (character == ',' || character == '"' || character == '\r' ||
            character == '\n') {
            needsQuotes = true;
        }
        if (character == '"') {
            ++embeddedQuotes;
        }
    }

    std::size_t required = value.size();
    if (needsQuotes) {
        const std::size_t maximum = std::numeric_limits<std::size_t>::max();
        if (embeddedQuotes > maximum - 2U ||
            required > maximum - embeddedQuotes - 2U) {
            return fail(CsvWriteStatus::CapacityExceeded);
        }
        required += embeddedQuotes + 2U;
    }
    if (columnsWritten_ != 0U) {
        if (required == std::numeric_limits<std::size_t>::max()) {
            return fail(CsvWriteStatus::CapacityExceeded);
        }
        ++required;
    }
    if (size_ > capacity_ || required > capacity_ - size_) {
        return fail(CsvWriteStatus::CapacityExceeded);
    }

    if (columnsWritten_ != 0U) {
        output_[size_++] = ',';
    }
    if (needsQuotes) {
        output_[size_++] = '"';
    }
    for (const char character : value) {
        if (character == '"') {
            output_[size_++] = '"';
        }
        output_[size_++] = character;
    }
    if (needsQuotes) {
        output_[size_++] = '"';
    }
    ++columnsWritten_;
    return CsvWriteStatus::Ok;
}

CsvWriteStatus CsvRowWriter::appendEmptyField() noexcept {
    return appendField({});
}

CsvWriteStatus CsvRowWriter::appendBoolean(bool value) noexcept {
    return appendField(value ? "true" : "false");
}

CsvWriteStatus CsvRowWriter::appendToken(const char* begin,
                                         const char* end) noexcept {
    if (begin == nullptr || end == nullptr || end < begin) {
        return fail(CsvWriteStatus::InvalidArgument);
    }
    return appendField(
        std::string_view(begin, static_cast<std::size_t>(end - begin)));
}

CsvWriteStatus CsvRowWriter::appendSigned(std::int64_t value) noexcept {
    char buffer[32]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) {
        return fail(CsvWriteStatus::NumberFormattingError);
    }
    return appendToken(buffer, result.ptr);
}

CsvWriteStatus CsvRowWriter::appendUnsigned(std::uint64_t value) noexcept {
    char buffer[32]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) {
        return fail(CsvWriteStatus::NumberFormattingError);
    }
    return appendToken(buffer, result.ptr);
}

CsvWriteStatus CsvRowWriter::appendNumber(float value) noexcept {
    if (!std::isfinite(value)) {
        return fail(CsvWriteStatus::NonFiniteNumber);
    }
    char buffer[64]{};
    const auto result = formatNumber(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) {
        return fail(CsvWriteStatus::NumberFormattingError);
    }
    return appendToken(buffer, result.ptr);
}

CsvWriteStatus CsvRowWriter::appendNumber(double value) noexcept {
    if (!std::isfinite(value)) {
        return fail(CsvWriteStatus::NonFiniteNumber);
    }
    char buffer[64]{};
    const auto result = formatNumber(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) {
        return fail(CsvWriteStatus::NumberFormattingError);
    }
    return appendToken(buffer, result.ptr);
}

CsvWriteStatus CsvRowWriter::appendOptionalNumber(float value,
                                                  bool valid) noexcept {
    return valid ? appendNumber(value) : appendEmptyField();
}

CsvWriteStatus CsvRowWriter::appendOptionalNumber(double value,
                                                  bool valid) noexcept {
    return valid ? appendNumber(value) : appendEmptyField();
}

CsvWriteStatus CsvRowWriter::finish() noexcept {
    if (status_ != CsvWriteStatus::Ok) {
        return status_;
    }
    if (finished_) {
        return fail(CsvWriteStatus::AlreadyFinished);
    }
    if (columnsWritten_ != expectedColumns_) {
        return fail(CsvWriteStatus::ColumnCountMismatch);
    }
    if (size_ >= capacity_) {
        return fail(CsvWriteStatus::CapacityExceeded);
    }
    output_[size_++] = '\n';
    finished_ = true;
    return CsvWriteStatus::Ok;
}

const char* CsvRowWriter::data() const noexcept {
    return status_ == CsvWriteStatus::Ok && finished_ ? output_ : nullptr;
}

}  // namespace project::dataset
