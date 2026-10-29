#pragma once
#include <cstdint>
#include "Models/SensorStatus.h"

namespace project::models {
struct ImuRawSample {
    std::int16_t accelX{0};
    std::int16_t accelY{0};
    std::int16_t accelZ{0};
    std::int16_t gyroX{0};
    std::int16_t gyroY{0};
    std::int16_t gyroZ{0};
    // Raw register counts only; scale and physical units remain TBD.
};

struct SensorFrame {
    std::uint64_t sequence{0};
    std::uint64_t timestampMicros{0};
    ImuRawSample imu{};
    SensorStatus imuStatus{};
    // TODO(TBD): soil moisture, YL-83 and HX710B fields/contracts after hardware verification.
};
}  // namespace project::models
