#pragma once

#include <cstdint>

#include "BSP/BoardConfig.h"

namespace project::bsp {

enum class BspStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    NotConfigured,
    NotInitialized,
    UnsupportedPlatform,
    HardwareError
};

struct PowerStatus {
    std::uint32_t supplyVoltageMv{0};
    bool valid{false};
};

struct MemoryMetrics {
    std::uint32_t freeHeapBytes{0};
    std::uint32_t minimumFreeHeapBytes{0};
    std::uint32_t largestFreeBlockBytes{0};
    std::uint32_t internalHeapBytes{0};
    bool valid{false};
};

class BoardSupportPackage {
public:
    BspStatus initEarly() noexcept;
    BspStatus configureOutputsSafe() noexcept;
    BspStatus initBuses() noexcept;
    BspStatus resetReason(std::uint32_t& platformReason) const noexcept;
    BspStatus monotonicMillis(std::uint64_t& uptimeMs) const noexcept;
    BspStatus monotonicMicros(std::uint64_t& uptimeUs) const noexcept;
    BspStatus boardIdentity(const BoardInfo*& info) const noexcept;
    BspStatus watchdogInit() noexcept;
    BspStatus watchdogFeed() noexcept;
    BspStatus restart() noexcept;
    BspStatus readMemoryMetrics(MemoryMetrics& metrics) const noexcept;
    BspStatus readPowerStatus(PowerStatus& status) const noexcept;

private:
    bool earlyInitialized_{false};
};

}  // namespace project::bsp
