#pragma once
#include <cstdint>

namespace project::bsp {
class BoardSupportPackage {
public:
    bool initEarly();
    bool configureOutputsSafe();
    bool initBuses();
    std::uint32_t resetReason() const;
    std::uint64_t monotonicMicros() const;
    const char* boardIdentity() const;
    void watchdogInit();
    void watchdogFeed();
};
}  // namespace project::bsp
