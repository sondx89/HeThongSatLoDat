#pragma once

#include <cstddef>
#include <cstdint>

namespace project::models {
struct FeatureVector {
    // Existing skeleton capacity only; this is not the Edge Impulse dimension.
    static constexpr std::size_t kMaxFeatures = 32;

    std::uint32_t schemaVersion{0};
    std::uint32_t schemaId{0};  // TODO(TBD): schema ID representation/registry.
    std::uint8_t schemaHashSha256[32]{};
    bool schemaHashValid{false};
    std::uint64_t bootId{0};
    std::uint64_t relatedSequenceNo{0};
    std::uint64_t windowStartMs{0};
    std::uint64_t windowEndMs{0};
    std::uint32_t windowSampleCount{0};
    float values[kMaxFeatures]{};
    std::uint16_t count{0};  // Actual count must match frozen model metadata.
    std::uint32_t featureValidMask{0};
    bool valid{false};
};
}  // namespace project::models
