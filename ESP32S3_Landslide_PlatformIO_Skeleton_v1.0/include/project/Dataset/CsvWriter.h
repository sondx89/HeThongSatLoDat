#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace project::dataset {

enum class CsvWriteStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    InvalidUtf8,
    ColumnCountExceeded,
    ColumnCountMismatch,
    CapacityExceeded,
    NonFiniteNumber,
    NumberFormattingError,
    AlreadyFinished
};

class CsvRowWriter final {
public:
    CsvRowWriter(char* output, std::size_t capacity,
                 std::size_t expectedColumns) noexcept;

    CsvRowWriter(const CsvRowWriter&) = delete;
    CsvRowWriter& operator=(const CsvRowWriter&) = delete;

    CsvWriteStatus appendField(std::string_view value) noexcept;
    CsvWriteStatus appendEmptyField() noexcept;
    CsvWriteStatus appendBoolean(bool value) noexcept;
    CsvWriteStatus appendSigned(std::int64_t value) noexcept;
    CsvWriteStatus appendUnsigned(std::uint64_t value) noexcept;
    CsvWriteStatus appendNumber(float value) noexcept;
    CsvWriteStatus appendNumber(double value) noexcept;
    CsvWriteStatus appendOptionalNumber(float value, bool valid) noexcept;
    CsvWriteStatus appendOptionalNumber(double value, bool valid) noexcept;
    CsvWriteStatus finish() noexcept;

    CsvWriteStatus status() const noexcept { return status_; }
    std::size_t size() const noexcept { return size_; }
    std::size_t columnsWritten() const noexcept { return columnsWritten_; }
    const char* data() const noexcept;

private:
    CsvWriteStatus appendToken(const char* begin, const char* end) noexcept;
    CsvWriteStatus fail(CsvWriteStatus status) noexcept;

    char* const output_;
    const std::size_t capacity_;
    const std::size_t expectedColumns_;
    std::size_t size_{0U};
    std::size_t columnsWritten_{0U};
    CsvWriteStatus status_{CsvWriteStatus::Ok};
    bool finished_{false};
};

}  // namespace project::dataset
