#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1 || \
    !defined(PROJECT_TEST_ONLY) || PROJECT_TEST_ONLY != 1
#error "MockInferenceEngine is available only in explicitly test-only builds"
#endif

#include "AI/IInferenceEngine.h"

namespace project::ai {

struct MockInferenceConfig {
    InferenceModelMetadata metadata{};
    float rawOutputs[project::models::ModelOutput::kMaxOutputs]{};
    std::uint16_t outputCount{0U};
    project::models::InferenceStatus forcedStatus{
        project::models::InferenceStatus::Ok};
};

class MockInferenceEngine final : public IInferenceEngine {
public:
    explicit MockInferenceEngine(
        const MockInferenceConfig& config = MockInferenceConfig{}) noexcept;

    const InferenceModelMetadata& metadata() const noexcept override {
        return metadata_;
    }
    bool initialize() override;
    bool run(const project::models::FeatureVector& input,
             project::models::ModelOutput& output) override;

private:
    const MockInferenceConfig config_;
    InferenceModelMetadata metadata_{};
    bool initialized_{false};
};

}  // namespace project::ai
