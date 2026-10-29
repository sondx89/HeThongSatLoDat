#pragma once
#include <cstddef>
#include <cstdint>

namespace project::models {
struct FeatureVector {
    static constexpr std::size_t kMaxFeatures = 32;  // Capacity only; actual model dimension is TBD.
    float values[kMaxFeatures]{};
    std::size_t count{0};
    std::uint32_t schemaVersion{0};
    bool valid{false};
    // Actual order/dimension/hash must match the exported Edge Impulse model contract.
};
}  // namespace project::models
