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
    std::uint16_t errorCode{0};  // TODO(TBD): bind to the released error catalog.
    std::uint32_t qualityFlags{0};
    std::uint64_t timestampMs{0};  // Monotonic milliseconds within boot.
};
}  // namespace project::models
