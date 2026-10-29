#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace project::storage {

struct CsvRecord {
    static constexpr std::size_t kRiskTokenCapacity = 16;

    std::uint64_t timestampMs{0};  // Monotonic uptime in milliseconds.
    float tilt{0.0F};
    float soil{0.0F};
    float rain{0.0F};
    float stress{0.0F};  // Model output value; meaning/unit remain integration-defined.
    std::array<char, kRiskTokenCapacity> risk{};

    bool timestampValid{false};
    bool tiltValid{false};
    bool soilValid{false};
    bool rainValid{false};
    bool stressValid{false};
    bool riskValid{false};
};

}  // namespace project::storage
