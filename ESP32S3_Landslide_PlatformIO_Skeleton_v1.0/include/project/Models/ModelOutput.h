#pragma once

#include <cstdint>

#include "Models/DataContractTypes.h"

namespace project::models {
struct ModelOutput {
    // Capacity only; actual output count/shape comes from the Edge Impulse export.
    static constexpr std::uint8_t kMaxOutputs = 32;

    std::uint32_t schemaVersion{0};
    std::uint64_t bootId{0};
    std::uint64_t inferenceId{0};
    std::uint64_t relatedSequenceNo{0};
    std::uint64_t sourceWindowStartMs{0};
    std::uint64_t sourceWindowEndMs{0};
    std::uint32_t sourceWindowSampleCount{0};
    std::uint32_t sourceFeatureSchemaVersion{0};
    std::uint32_t sourceFeatureSchemaId{0};
    std::uint8_t sourceFeatureSchemaHashSha256[32]{};
    bool sourceFeatureSchemaHashValid{false};
    std::uint64_t inferenceStartMs{0};
    std::uint32_t inferenceDurationMs{0};
    std::uint32_t inferenceLatencyUs{0};  // TODO(TBD): choose canonical duration unit.
    float rawOutputs[kMaxOutputs]{};
    std::uint16_t outputCount{0};
    float predictionValue{0.0F};
    bool predictionValid{false};
    InferenceStatus status{InferenceStatus::InvalidInput};
    std::uint64_t timestampMs{0};
    std::uint8_t modelHashSha256[32]{};
    bool modelHashValid{false};
};
}  // namespace project::models
