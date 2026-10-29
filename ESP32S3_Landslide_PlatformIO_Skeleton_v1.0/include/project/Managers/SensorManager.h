#pragma once

#include <cstdint>

#include "Drivers/ISensor.h"
#include "Models/SensorFrame.h"
#include "Models/SensorStatus.h"

namespace project::managers {

struct SensorManagerConfig {
    std::uint64_t bootId{0};
    std::uint32_t samplePeriodMicros{0};
};

class SensorManager final {
public:
    SensorManager(project::drivers::ISensor& imu,
                  project::drivers::ISensor& soil,
                  project::drivers::ISensor& rain,
                  project::drivers::ISensor& hx710b,
                  const SensorManagerConfig& config) noexcept;

    bool beginAll() noexcept;
    bool pollDue(std::uint64_t nowMicros) noexcept;
    bool readSnapshot(project::models::SensorFrame& output) noexcept;

private:
    project::drivers::ISensor& imu_;
    project::drivers::ISensor& soil_;
    project::drivers::ISensor& rain_;
    project::drivers::ISensor& hx710b_;
    const SensorManagerConfig config_;
    std::uint64_t nextSequence_{0};
    std::uint64_t dueTimestampMicros_{0};
    bool ready_{false};
    bool firstPoll_{true};
    bool sampleDue_{false};
};
}  // namespace project::managers
