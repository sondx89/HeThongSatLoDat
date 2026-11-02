#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Models/DataContractTypes.h"
#include "Models/FeatureVector.h"
#include "Models/ModelOutput.h"

namespace project::ai {

enum class InferenceEngineKind : std::uint8_t {
    NotConfigured,
    TestOnly,
    EdgeImpulse
};

struct InferenceModelMetadata {
    static constexpr std::size_t kHashSize = 32U;

    InferenceEngineKind engineKind{InferenceEngineKind::NotConfigured};
    std::uint32_t featureSchemaVersion{0U};
    std::uint32_t featureSchemaId{0U};
    std::array<std::uint8_t, kHashSize> featureSchemaHash{};
    std::uint16_t inputDimension{0U};
    std::uint16_t outputCount{0U};
    std::array<std::uint8_t, kHashSize> modelHash{};
    bool featureSchemaHashValid{false};
    bool featureSchemaVerified{false};
    bool modelMetadataVerified{false};
    bool modelHashVerified{false};
    bool outputContractApproved{false};

    bool isComplete() const noexcept {
        bool hasSchemaHash = false;
        for (const std::uint8_t byte : featureSchemaHash) {
            hasSchemaHash = hasSchemaHash || byte != 0U;
        }
        const bool validEngineKind =
            engineKind == InferenceEngineKind::TestOnly ||
            engineKind == InferenceEngineKind::EdgeImpulse;
        return validEngineKind &&
               featureSchemaVersion != 0U && featureSchemaId != 0U &&
               inputDimension != 0U &&
               inputDimension <=
                   project::models::FeatureVector::kMaxFeatures &&
               outputCount != 0U &&
               outputCount <= project::models::ModelOutput::kMaxOutputs &&
               featureSchemaHashValid && hasSchemaHash &&
               featureSchemaVerified;
    }
};

bool matchesInferenceInput(
    const InferenceModelMetadata& metadata,
    const project::models::FeatureVector& input) noexcept;

project::models::InferenceStatus validateInferenceOutput(
    const InferenceModelMetadata& metadata,
    const project::models::ModelOutput& output) noexcept;

class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;
    virtual const InferenceModelMetadata& metadata() const noexcept = 0;
    virtual bool initialize() = 0;
    virtual bool run(const project::models::FeatureVector& input,
                     project::models::ModelOutput& output) = 0;
};

using IModelRuntime = IInferenceEngine;

}  // namespace project::ai
