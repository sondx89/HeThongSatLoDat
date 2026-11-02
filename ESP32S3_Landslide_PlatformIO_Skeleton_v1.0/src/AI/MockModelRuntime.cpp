#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1 && \
    defined(PROJECT_TEST_ONLY) && PROJECT_TEST_ONLY == 1

#include "project/AI/MockInferenceEngine.h"

#include <cmath>
#include <cstring>

namespace project::ai {
namespace {

bool validForcedStatus(project::models::InferenceStatus status) noexcept {
    switch (status) {
        case project::models::InferenceStatus::Ok:
        case project::models::InferenceStatus::ModelError:
        case project::models::InferenceStatus::NonFiniteOutput:
        case project::models::InferenceStatus::VersionMismatch:
        case project::models::InferenceStatus::Timeout:
            return true;
        case project::models::InferenceStatus::InvalidInput:
            return false;
    }
    return false;
}

}  // namespace

MockInferenceEngine::MockInferenceEngine(
    const MockInferenceConfig& config) noexcept
    : config_(config), metadata_(config.metadata) {
    metadata_.engineKind = InferenceEngineKind::TestOnly;
}

bool MockInferenceEngine::initialize() {
    initialized_ = false;
    if (!metadata_.isComplete() ||
        config_.outputCount != metadata_.outputCount ||
        config_.outputCount == 0U ||
        config_.outputCount > project::models::ModelOutput::kMaxOutputs ||
        !validForcedStatus(config_.forcedStatus)) {
        return false;
    }
    for (std::uint16_t i = 0U; i < config_.outputCount; ++i) {
        if (!std::isfinite(config_.rawOutputs[i])) {
            return false;
        }
    }
    initialized_ = true;
    return true;
}

bool MockInferenceEngine::run(
    const project::models::FeatureVector& input,
    project::models::ModelOutput& output) {
    output = {};
    output.bootId = input.bootId;
    output.relatedSequenceNo = input.relatedSequenceNo;
    output.sourceWindowStartMs = input.windowStartMs;
    output.sourceWindowEndMs = input.windowEndMs;
    output.sourceWindowSampleCount = input.windowSampleCount;
    output.sourceFeatureSchemaVersion = input.schemaVersion;
    output.sourceFeatureSchemaId = input.schemaId;
    std::memcpy(output.sourceFeatureSchemaHashSha256,
                input.schemaHashSha256, sizeof(input.schemaHashSha256));
    output.sourceFeatureSchemaHashValid = input.schemaHashValid;

    if (!initialized_) {
        output.status = project::models::InferenceStatus::ModelError;
        return false;
    }
    if (!matchesInferenceInput(metadata_, input)) {
        output.status = project::models::InferenceStatus::InvalidInput;
        return true;
    }
    if (config_.forcedStatus != project::models::InferenceStatus::Ok) {
        output.status = config_.forcedStatus;
        return true;
    }

    for (std::uint16_t i = 0U; i < config_.outputCount; ++i) {
        output.rawOutputs[i] = config_.rawOutputs[i];
    }
    output.outputCount = config_.outputCount;
    output.status = project::models::InferenceStatus::Ok;
    return true;
}

}  // namespace project::ai

#endif
