#pragma once
#include <cstdint>

namespace project::models {
struct ModelOutput {
    float modelValue{0.0F};
    std::uint64_t timestampMicros{0};
    bool valid{false};
    std::uint32_t statusCode{0};
    // Do not call this a probability until target semantics/calibration are validated.
};
}  // namespace project::models
