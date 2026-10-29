#pragma once
#include <cstdint>
#include "Models/SensorStatus.h"

namespace project::models {
struct ProcessedFrame {
    std::uint64_t sequence{0};
    std::uint64_t timestampMicros{0};
    // TODO(TBD): converted channels, units, calibration/schema versions, validity and quality flags.
    SensorStatus aggregateStatus{};
};
}  // namespace project::models
