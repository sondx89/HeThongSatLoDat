#include "project/AI/IInferenceEngine.h"

#include <cmath>
#include <cstring>

namespace project::ai {
namespace {

bool validInferenceStatus(
    project::models::InferenceStatus status) noexcept {
    switch (status) {
        case project::models::InferenceStatus::Ok:
        case project::models::InferenceStatus::InvalidInput:
        case project::models::InferenceStatus::ModelError:
        case project::models::InferenceStatus::NonFiniteOutput:
        case project::models::InferenceStatus::VersionMismatch:
        case project::models::InferenceStatus::Timeout:
            return true;
    }
    return false;
}

}  // namespace

bool matchesInferenceInput(
    const InferenceModelMetadata& metadata,
    const project::models::FeatureVector& input) noexcept {
    if (!metadata.isComplete() || !input.valid || input.count == 0U ||
        input.count != metadata.inputDimension ||
        input.count > project::models::FeatureVector::kMaxFeatures ||
        input.schemaVersion != metadata.featureSchemaVersion ||
        input.schemaId != metadata.featureSchemaId ||
        !input.schemaHashValid || input.bootId == 0U ||
        input.relatedSequenceNo == 0U ||
        input.windowStartMs > input.windowEndMs ||
        input.windowSampleCount == 0U ||
        std::memcmp(input.schemaHashSha256,
                    metadata.featureSchemaHash.data(),
                    metadata.featureSchemaHash.size()) != 0) {
        return false;
    }

    const std::uint32_t expectedMask =
        input.count == project::models::FeatureVector::kMaxFeatures
            ? UINT32_MAX
            : (UINT32_C(1) << input.count) - 1U;
    if (input.featureValidMask != expectedMask) {
        return false;
    }
    for (std::size_t i = 0U; i < input.count; ++i) {
        if (!std::isfinite(input.values[i])) {
            return false;
        }
    }
    return true;
}

project::models::InferenceStatus validateInferenceOutput(
    const InferenceModelMetadata& metadata,
    const project::models::ModelOutput& output) noexcept {
    using project::models::InferenceStatus;
    if (!metadata.isComplete() ||
        !validInferenceStatus(output.status) ||
        output.outputCount > project::models::ModelOutput::kMaxOutputs) {
        return InferenceStatus::ModelError;
    }
    if (output.status != InferenceStatus::Ok) {
        return output.outputCount == 0U && !output.predictionValid
                   ? output.status
                   : InferenceStatus::ModelError;
    }
    if (output.outputCount != metadata.outputCount) {
        return InferenceStatus::ModelError;
    }
    for (std::uint16_t i = 0U; i < output.outputCount; ++i) {
        if (!std::isfinite(output.rawOutputs[i])) {
            return InferenceStatus::NonFiniteOutput;
        }
    }
    if (output.predictionValid && !std::isfinite(output.predictionValue)) {
        return InferenceStatus::NonFiniteOutput;
    }
    return InferenceStatus::Ok;
}

}  // namespace project::ai
