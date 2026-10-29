#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Storage/CsvRecord.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#if !defined(configSUPPORT_STATIC_ALLOCATION) || \
    (configSUPPORT_STATIC_ALLOCATION != 1)
#error "CsvLogger requires configSUPPORT_STATIC_ALLOCATION=1"
#endif
#else
#include <mutex>
#endif

namespace project::storage {

enum class CsvFileStatus : std::uint8_t {
    Ok,
    NotFound,
    Full,
    IoError
};

class ICsvFileSystem {
public:
    virtual ~ICsvFileSystem() = default;

    virtual CsvFileStatus begin() noexcept = 0;
    // Must return the highest existing numeric index for this prefix.
    virtual CsvFileStatus findLatest(
        const char* prefix, char* filename, std::size_t filenameCapacity,
        std::uint32_t& fileIndex, std::uint64_t& fileSize) noexcept = 0;
    virtual CsvFileStatus createExclusive(const char* filename) noexcept = 0;
    virtual CsvFileStatus readAt(const char* filename, std::uint64_t offset,
                                 std::uint8_t* output,
                                 std::size_t requested,
                                 std::size_t& bytesRead) noexcept = 0;
    virtual CsvFileStatus append(const char* filename, const std::uint8_t* data,
                                 std::size_t length,
                                 std::size_t& bytesWritten) noexcept = 0;
    virtual CsvFileStatus truncate(const char* filename,
                                   std::uint64_t length) noexcept = 0;
    virtual CsvFileStatus flush(const char* filename) noexcept = 0;
};

enum class CsvLoggerStatus : std::uint8_t {
    Ok,
    Recovered,
    InvalidConfiguration,
    InvalidRecord,
    MutexUnavailable,
    NotInitialized,
    FileSystemError,
    StorageFull,
    CorruptFile,
    RecordTooLarge,
    FileIndexExhausted
};

struct CsvLoggerConfig {
    static constexpr std::size_t kPrefixCapacity = 16;

    char filePrefix[kPrefixCapacity]{"data"};
    std::uint64_t maximumFileBytes{0};  // Must be selected from storage budget.
};

struct CsvLoggerStats {
    std::uint32_t rotationCount{0};
    std::uint32_t recoveredTailCount{0};
    std::uint32_t corruptFileCount{0};
    std::uint32_t writeFailureCount{0};
    std::uint64_t recordsWritten{0};
    std::uint64_t currentFileBytes{0};
    std::uint32_t currentFileIndex{0};
};

class CsvLoggerMutex final {
public:
    CsvLoggerMutex() noexcept;
    ~CsvLoggerMutex() = default;
    CsvLoggerMutex(const CsvLoggerMutex&) = delete;
    CsvLoggerMutex& operator=(const CsvLoggerMutex&) = delete;

    bool lock() noexcept;
    void unlock() noexcept;

private:
#if defined(ARDUINO_ARCH_ESP32)
    StaticSemaphore_t storage_{};
    SemaphoreHandle_t handle_{nullptr};
#else
    std::mutex mutex_;
#endif
};

class CsvLogger final {
public:
    static constexpr const char* kHeader =
        "timestamp,tilt,soil,rain,stress,risk\n";

    CsvLogger(ICsvFileSystem& fileSystem,
              const CsvLoggerConfig& config) noexcept;
    ~CsvLogger() = default;
    CsvLogger(const CsvLogger&) = delete;
    CsvLogger& operator=(const CsvLogger&) = delete;

    CsvLoggerStatus begin() noexcept;
    CsvLoggerStatus appendRecord(const CsvRecord& record) noexcept;
    CsvLoggerStatus flush() noexcept;
    CsvLoggerStatus getStats(CsvLoggerStats& output) noexcept;

    static CsvLoggerStatus serializeRecord(
        const CsvRecord& record, char* output, std::size_t capacity,
        std::size_t& bytesWritten) noexcept;

private:
    class LockGuard final {
    public:
        explicit LockGuard(CsvLoggerMutex& mutex) noexcept;
        ~LockGuard();
        bool locked() const noexcept { return locked_; }

    private:
        CsvLoggerMutex& mutex_;
        bool locked_;
    };

    CsvLoggerStatus beginLocked() noexcept;
    CsvLoggerStatus recoverFileLocked(const char* filename,
                                      std::uint64_t fileSize,
                                      std::uint64_t& recoveredSize) noexcept;
    CsvLoggerStatus createFileLocked(std::uint32_t fileIndex,
                                     bool isRotation) noexcept;
    CsvLoggerStatus rotateIfNeededLocked(std::size_t recordBytes) noexcept;
    CsvLoggerStatus appendBytesLocked(const char* bytes,
                                      std::size_t length) noexcept;
    bool validConfiguration() const noexcept;
    bool makeFilename(std::uint32_t fileIndex, char* output,
                      std::size_t capacity) const noexcept;

    ICsvFileSystem& fileSystem_;
    const CsvLoggerConfig config_;
    CsvLoggerMutex mutex_{};
    CsvLoggerStats stats_{};
    std::array<char, 32> currentFilename_{};
    bool initialized_{false};
};

}  // namespace project::storage
