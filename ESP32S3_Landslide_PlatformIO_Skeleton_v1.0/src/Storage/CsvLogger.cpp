#include "project/Storage/CsvLogger.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace project::storage {
namespace {

constexpr std::size_t kHeaderLength =
    sizeof("timestamp,tilt,soil,rain,stress,risk\n") - 1U;
constexpr std::size_t kReadChunkSize = 64U;
constexpr std::size_t kMaximumRowLength = 192U;
void incrementSaturated(std::uint32_t& value) noexcept {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

void incrementSaturated(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

bool validRiskToken(const char* value, std::size_t length) noexcept {
    if (length == 0U || length >= CsvRecord::kRiskTokenCapacity) {
        return false;
    }
    for (std::size_t i = 0; i < length; ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool validNumber(const char* value, std::size_t length) noexcept {
    if (length == 0) {
        return true;
    }
    if (length >= 32U) {
        return false;
    }

    std::size_t index = 0;
    if (value[index] == '-' || value[index] == '+') {
        ++index;
    }
    bool hasMantissaDigit = false;
    while (index < length && value[index] >= '0' && value[index] <= '9') {
        hasMantissaDigit = true;
        ++index;
    }
    if (index < length && value[index] == '.') {
        ++index;
        while (index < length && value[index] >= '0' && value[index] <= '9') {
            hasMantissaDigit = true;
            ++index;
        }
    }
    if (!hasMantissaDigit) {
        return false;
    }
    if (index < length && (value[index] == 'e' || value[index] == 'E')) {
        ++index;
        if (index < length && (value[index] == '-' || value[index] == '+')) {
            ++index;
        }
        const std::size_t exponentStart = index;
        while (index < length && value[index] >= '0' && value[index] <= '9') {
            ++index;
        }
        if (index == exponentStart) {
            return false;
        }
    }
    if (index != length) {
        return false;
    }

    char number[32]{};
    std::memcpy(number, value, length);
    char* end = nullptr;
    const double parsed = std::strtod(number, &end);
    return end == number + length && std::isfinite(parsed);
}

bool validTimestamp(const char* value, std::size_t length) noexcept {
    if (length == 0U) {
        return true;
    }
    if (length > 20U) {
        return false;
    }
    for (std::size_t i = 0; i < length; ++i) {
        if (value[i] < '0' || value[i] > '9') {
            return false;
        }
    }
    constexpr char kMaximumTimestamp[] = "18446744073709551615";
    if (length == sizeof(kMaximumTimestamp) - 1U &&
        std::memcmp(value, kMaximumTimestamp, length) > 0) {
        return false;
    }
    return true;
}

bool validCsvRow(const char* row, std::size_t length) noexcept {
    if (length < 7U || row[length - 1U] != '\n') {
        return false;
    }

    std::size_t fieldStart = 0;
    std::size_t fieldIndex = 0;
    for (std::size_t i = 0; i < length; ++i) {
        if (row[i] != ',' && row[i] != '\n') {
            continue;
        }
        const std::size_t fieldLength = i - fieldStart;
        if (fieldIndex == 0U) {
            if (!validTimestamp(row + fieldStart, fieldLength)) {
                return false;
            }
        } else if (fieldIndex <= 4U) {
            if (!validNumber(row + fieldStart, fieldLength)) {
                return false;
            }
        } else if (fieldIndex == 5U) {
            if (fieldLength != 0U &&
                !validRiskToken(row + fieldStart, fieldLength)) {
                return false;
            }
        } else {
            return false;
        }
        ++fieldIndex;
        fieldStart = i + 1U;
    }
    return fieldIndex == 6U && fieldStart == length;
}

bool validFloat(const float value, bool valid) noexcept {
    return !valid || std::isfinite(value);
}

}  // namespace

CsvLoggerMutex::CsvLoggerMutex() noexcept {
#if defined(ARDUINO_ARCH_ESP32)
#if defined(configSUPPORT_STATIC_ALLOCATION) && \
    (configSUPPORT_STATIC_ALLOCATION == 1)
    handle_ = xSemaphoreCreateMutexStatic(&storage_);
#endif
#endif
}

bool CsvLoggerMutex::lock() noexcept {
#if defined(ARDUINO_ARCH_ESP32)
    return handle_ != nullptr &&
           xSemaphoreTake(handle_, portMAX_DELAY) == pdTRUE;
#else
    mutex_.lock();
    return true;
#endif
}

void CsvLoggerMutex::unlock() noexcept {
#if defined(ARDUINO_ARCH_ESP32)
    if (handle_ != nullptr) {
        (void)xSemaphoreGive(handle_);
    }
#else
    mutex_.unlock();
#endif
}

CsvLogger::LockGuard::LockGuard(CsvLoggerMutex& mutex) noexcept
    : mutex_(mutex), locked_(mutex_.lock()) {}

CsvLogger::LockGuard::~LockGuard() {
    if (locked_) {
        mutex_.unlock();
    }
}

CsvLogger::CsvLogger(ICsvFileSystem& fileSystem,
                     const CsvLoggerConfig& config) noexcept
    : fileSystem_(fileSystem), config_(config) {}

bool CsvLogger::validConfiguration() const noexcept {
    if (config_.maximumFileBytes <= kHeaderLength) {
        return false;
    }
    std::size_t prefixLength = 0;
    while (prefixLength < sizeof(config_.filePrefix) &&
           config_.filePrefix[prefixLength] != '\0') {
        const char value = config_.filePrefix[prefixLength];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' ||
              value == '-')) {
            return false;
        }
        ++prefixLength;
    }
    return prefixLength > 0U && prefixLength < sizeof(config_.filePrefix);
}

bool CsvLogger::makeFilename(std::uint32_t fileIndex, char* output,
                             std::size_t capacity) const noexcept {
    if (output == nullptr || capacity == 0U) {
        return false;
    }
    const int written = std::snprintf(output, capacity, "%s_%06lu.csv",
                                      config_.filePrefix,
                                      static_cast<unsigned long>(fileIndex));
    return written > 0 && static_cast<std::size_t>(written) < capacity;
}

CsvLoggerStatus CsvLogger::begin() noexcept {
    LockGuard guard(mutex_);
    if (!guard.locked()) {
        return CsvLoggerStatus::MutexUnavailable;
    }
    return beginLocked();
}

CsvLoggerStatus CsvLogger::beginLocked() noexcept {
    initialized_ = false;
    if (!validConfiguration()) {
        return CsvLoggerStatus::InvalidConfiguration;
    }

    const CsvFileStatus storageBegin = fileSystem_.begin();
    if (storageBegin != CsvFileStatus::Ok) {
        return storageBegin == CsvFileStatus::Full
                   ? CsvLoggerStatus::StorageFull
                   : CsvLoggerStatus::FileSystemError;
    }

    std::array<char, 32> latestFilename{};
    std::uint32_t latestIndex = 0;
    std::uint64_t latestSize = 0;
    const CsvFileStatus latestStatus =
        fileSystem_.findLatest(config_.filePrefix, latestFilename.data(),
                               sizeof(latestFilename), latestIndex, latestSize);
    if (latestStatus == CsvFileStatus::NotFound) {
        const CsvLoggerStatus created = createFileLocked(0U, false);
        if (created == CsvLoggerStatus::Ok) {
            initialized_ = true;
        }
        return created;
    }
    if (latestStatus != CsvFileStatus::Ok ||
        latestFilename[0] == '\0' ||
        std::memchr(latestFilename.data(), '\0', latestFilename.size()) ==
            nullptr) {
        return latestStatus == CsvFileStatus::Full
                   ? CsvLoggerStatus::StorageFull
                   : CsvLoggerStatus::FileSystemError;
    }
    std::array<char, 32> expectedFilename{};
    if (!makeFilename(latestIndex, expectedFilename.data(),
                      expectedFilename.size()) ||
        std::strcmp(latestFilename.data(), expectedFilename.data()) != 0) {
        return CsvLoggerStatus::FileSystemError;
    }

    std::uint64_t recoveredSize = 0;
    CsvLoggerStatus recovery =
        recoverFileLocked(latestFilename.data(), latestSize, recoveredSize);
    if (recovery == CsvLoggerStatus::CorruptFile) {
        incrementSaturated(stats_.corruptFileCount);
        if (latestIndex == std::numeric_limits<std::uint32_t>::max()) {
            return CsvLoggerStatus::FileIndexExhausted;
        }
        const CsvLoggerStatus created = createFileLocked(latestIndex + 1U, true);
        if (created == CsvLoggerStatus::Ok) {
            initialized_ = true;
            return CsvLoggerStatus::Recovered;
        }
        return created;
    }
    if (recovery != CsvLoggerStatus::Ok &&
        recovery != CsvLoggerStatus::Recovered) {
        return recovery;
    }

    std::memcpy(currentFilename_.data(), latestFilename.data(),
                std::strlen(latestFilename.data()) + 1U);
    stats_.currentFileIndex = latestIndex;
    stats_.currentFileBytes = recoveredSize;
    initialized_ = true;
    if (recovery == CsvLoggerStatus::Recovered) {
        return CsvLoggerStatus::Recovered;
    }
    return CsvLoggerStatus::Ok;
}

CsvLoggerStatus CsvLogger::recoverFileLocked(
    const char* filename, std::uint64_t fileSize,
    std::uint64_t& recoveredSize) noexcept {
    recoveredSize = 0;
    if (fileSize < kHeaderLength) {
        return CsvLoggerStatus::CorruptFile;
    }

    std::array<std::uint8_t, kHeaderLength> header{};
    std::size_t headerBytes = 0;
    if (fileSystem_.readAt(filename, 0, header.data(), header.size(),
                           headerBytes) != CsvFileStatus::Ok) {
        return CsvLoggerStatus::FileSystemError;
    }
    if (headerBytes != header.size() ||
        std::memcmp(header.data(), kHeader, kHeaderLength) != 0) {
        return CsvLoggerStatus::CorruptFile;
    }

    std::array<std::uint8_t, kReadChunkSize> chunk{};
    std::array<char, kMaximumRowLength> row{};
    std::size_t rowLength = 0;
    std::uint64_t offset = kHeaderLength;
    std::uint64_t lastGoodOffset = kHeaderLength;
    bool malformed = false;

    while (offset < fileSize && !malformed) {
        const std::uint64_t remaining = fileSize - offset;
        const std::size_t request =
            remaining < chunk.size() ? static_cast<std::size_t>(remaining)
                                     : chunk.size();
        std::size_t bytesRead = 0;
        if (fileSystem_.readAt(filename, offset, chunk.data(), request,
                               bytesRead) != CsvFileStatus::Ok ||
            bytesRead != request) {
            return CsvLoggerStatus::FileSystemError;
        }
        for (std::size_t i = 0; i < bytesRead; ++i) {
            const char value = static_cast<char>(chunk[i]);
            if (rowLength >= row.size()) {
                malformed = true;
                break;
            }
            row[rowLength++] = value;
            if (value == '\n') {
                if (!validCsvRow(row.data(), rowLength)) {
                    malformed = true;
                    break;
                }
                lastGoodOffset = offset + i + 1U;
                rowLength = 0;
            }
        }
        offset += bytesRead;
    }

    if (rowLength != 0U) {
        malformed = true;
    }
    if (malformed) {
        if (fileSystem_.truncate(filename, lastGoodOffset) != CsvFileStatus::Ok ||
            fileSystem_.flush(filename) != CsvFileStatus::Ok) {
            return CsvLoggerStatus::FileSystemError;
        }
        incrementSaturated(stats_.recoveredTailCount);
        recoveredSize = lastGoodOffset;
        return CsvLoggerStatus::Recovered;
    }

    recoveredSize = lastGoodOffset;
    return CsvLoggerStatus::Ok;
}

CsvLoggerStatus CsvLogger::createFileLocked(std::uint32_t fileIndex,
                                             bool isRotation) noexcept {
    std::array<char, 32> filename{};
    if (!makeFilename(fileIndex, filename.data(), filename.size())) {
        return CsvLoggerStatus::InvalidConfiguration;
    }
    const CsvFileStatus createStatus =
        fileSystem_.createExclusive(filename.data());
    if (createStatus != CsvFileStatus::Ok) {
        incrementSaturated(stats_.writeFailureCount);
        initialized_ = false;
        return createStatus == CsvFileStatus::Full
                   ? CsvLoggerStatus::StorageFull
                   : CsvLoggerStatus::FileSystemError;
    }

    std::size_t bytesWritten = 0;
    const CsvFileStatus appendStatus = fileSystem_.append(
        filename.data(),
        reinterpret_cast<const std::uint8_t*>(kHeader), kHeaderLength,
        bytesWritten);
    if (appendStatus != CsvFileStatus::Ok || bytesWritten != kHeaderLength ||
        fileSystem_.flush(filename.data()) != CsvFileStatus::Ok) {
        incrementSaturated(stats_.writeFailureCount);
        initialized_ = false;
        return appendStatus == CsvFileStatus::Full
                   ? CsvLoggerStatus::StorageFull
                   : CsvLoggerStatus::FileSystemError;
    }

    currentFilename_ = filename;
    stats_.currentFileIndex = fileIndex;
    stats_.currentFileBytes = kHeaderLength;
    if (isRotation) {
        incrementSaturated(stats_.rotationCount);
    }
    return CsvLoggerStatus::Ok;
}

CsvLoggerStatus CsvLogger::rotateIfNeededLocked(
    std::size_t recordBytes) noexcept {
    if (recordBytes > config_.maximumFileBytes - kHeaderLength) {
        return CsvLoggerStatus::RecordTooLarge;
    }
    if (stats_.currentFileBytes <=
            config_.maximumFileBytes - recordBytes &&
        initialized_) {
        return CsvLoggerStatus::Ok;
    }
    if (stats_.currentFileIndex == std::numeric_limits<std::uint32_t>::max()) {
        return CsvLoggerStatus::FileIndexExhausted;
    }
    return createFileLocked(stats_.currentFileIndex + 1U, true);
}

CsvLoggerStatus CsvLogger::appendBytesLocked(const char* bytes,
                                              std::size_t length) noexcept {
    std::size_t bytesWritten = 0;
    const CsvFileStatus appendStatus = fileSystem_.append(
        currentFilename_.data(),
        reinterpret_cast<const std::uint8_t*>(bytes), length, bytesWritten);
    if (appendStatus != CsvFileStatus::Ok || bytesWritten != length) {
        incrementSaturated(stats_.writeFailureCount);
        initialized_ = false;
        return appendStatus == CsvFileStatus::Full
                   ? CsvLoggerStatus::StorageFull
                   : CsvLoggerStatus::FileSystemError;
    }
    if (fileSystem_.flush(currentFilename_.data()) != CsvFileStatus::Ok) {
        incrementSaturated(stats_.writeFailureCount);
        initialized_ = false;
        return CsvLoggerStatus::FileSystemError;
    }
    stats_.currentFileBytes += length;
    incrementSaturated(stats_.recordsWritten);
    return CsvLoggerStatus::Ok;
}

CsvLoggerStatus CsvLogger::appendRecord(const CsvRecord& record) noexcept {
    LockGuard guard(mutex_);
    if (!guard.locked()) {
        return CsvLoggerStatus::MutexUnavailable;
    }
    if (!initialized_) {
        const CsvLoggerStatus beginStatus = beginLocked();
        if (beginStatus != CsvLoggerStatus::Ok &&
            beginStatus != CsvLoggerStatus::Recovered) {
            return beginStatus;
        }
    }

    std::array<char, kMaximumRowLength> row{};
    std::size_t rowLength = 0;
    const CsvLoggerStatus serialized =
        serializeRecord(record, row.data(), row.size(), rowLength);
    if (serialized != CsvLoggerStatus::Ok) {
        return serialized;
    }

    const CsvLoggerStatus rotation = rotateIfNeededLocked(rowLength);
    if (rotation != CsvLoggerStatus::Ok) {
        return rotation;
    }
    return appendBytesLocked(row.data(), rowLength);
}

CsvLoggerStatus CsvLogger::flush() noexcept {
    LockGuard guard(mutex_);
    if (!guard.locked()) {
        return CsvLoggerStatus::MutexUnavailable;
    }
    if (!initialized_) {
        return CsvLoggerStatus::NotInitialized;
    }
    const CsvFileStatus status =
        fileSystem_.flush(currentFilename_.data());
    if (status != CsvFileStatus::Ok) {
        incrementSaturated(stats_.writeFailureCount);
    }
    return status == CsvFileStatus::Ok
               ? CsvLoggerStatus::Ok
               : (status == CsvFileStatus::Full
                      ? CsvLoggerStatus::StorageFull
                      : CsvLoggerStatus::FileSystemError);
}

CsvLoggerStatus CsvLogger::getStats(CsvLoggerStats& output) noexcept {
    LockGuard guard(mutex_);
    if (!guard.locked()) {
        return CsvLoggerStatus::MutexUnavailable;
    }
    output = stats_;
    return CsvLoggerStatus::Ok;
}

CsvLoggerStatus CsvLogger::serializeRecord(
    const CsvRecord& record, char* output, std::size_t capacity,
    std::size_t& bytesWritten) noexcept {
    bytesWritten = 0;
    if (output == nullptr || capacity == 0U ||
        !validFloat(record.tilt, record.tiltValid) ||
        !validFloat(record.soil, record.soilValid) ||
        !validFloat(record.rain, record.rainValid) ||
        !validFloat(record.stress, record.stressValid)) {
        return CsvLoggerStatus::InvalidRecord;
    }
    std::size_t riskLength = 0;
    while (riskLength < record.risk.size() &&
           record.risk[riskLength] != '\0') {
        ++riskLength;
    }
    if (record.riskValid &&
        !validRiskToken(record.risk.data(), riskLength)) {
        return CsvLoggerStatus::InvalidRecord;
    }
    std::size_t cursor = 0;
    if (record.timestampValid) {
        const int timestampLength = std::snprintf(
            output, capacity, "%llu", static_cast<unsigned long long>(
                                            record.timestampMs));
        if (timestampLength <= 0 ||
            static_cast<std::size_t>(timestampLength) >= capacity) {
            return CsvLoggerStatus::RecordTooLarge;
        }
        cursor = static_cast<std::size_t>(timestampLength);
    }
    if (cursor >= capacity) {
        return CsvLoggerStatus::RecordTooLarge;
    }
    output[cursor++] = ',';

    const float values[] = {record.tilt, record.soil, record.rain, record.stress};
    const bool valid[] = {record.tiltValid, record.soilValid,
                          record.rainValid, record.stressValid};
    for (std::size_t i = 0; i < 4U; ++i) {
        if (valid[i]) {
            const int valueLength = std::snprintf(
                output + cursor, capacity - cursor, "%.9g",
                static_cast<double>(values[i]));
            if (valueLength <= 0 ||
                static_cast<std::size_t>(valueLength) >= capacity - cursor) {
                return CsvLoggerStatus::RecordTooLarge;
            }
            for (std::size_t j = cursor;
                 j < cursor + static_cast<std::size_t>(valueLength); ++j) {
                if (output[j] == ',') {
                    output[j] = '.';
                }
            }
            cursor += static_cast<std::size_t>(valueLength);
        }
        if (cursor >= capacity) {
            return CsvLoggerStatus::RecordTooLarge;
        }
        output[cursor++] = ',';
    }
    if (riskLength + 1U > capacity - cursor) {
        return CsvLoggerStatus::RecordTooLarge;
    }
    if (record.riskValid) {
        std::memcpy(output + cursor, record.risk.data(), riskLength);
    }
    cursor += riskLength;
    output[cursor++] = '\n';
    bytesWritten = cursor;
    return CsvLoggerStatus::Ok;
}

}  // namespace project::storage
