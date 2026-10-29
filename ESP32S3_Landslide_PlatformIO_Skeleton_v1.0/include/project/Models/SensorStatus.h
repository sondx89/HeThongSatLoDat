#pragma once
#include <cstdint>

namespace project::models {
enum class SensorHealth : std::uint8_t {
    Unknown,
    Healthy,
    Degraded,
    Fault
};

struct SensorStatus {
    SensorHealth health{SensorHealth::Unknown};
    bool valid{false};
    std::uint32_t errorCode{0};
    std::uint64_t timestampMicros{0};
};
}  // namespace project::models
