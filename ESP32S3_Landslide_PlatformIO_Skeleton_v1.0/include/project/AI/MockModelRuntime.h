#pragma once

#include "AI/IInferenceEngine.h"

namespace project::ai {

class MockModelRuntime final : public IInferenceEngine {
public:
    bool initialize() override;
    bool run(const project::models::FeatureVector& input,
             project::models::ModelOutput& output) override;

private:
    static bool isValidInput(
        const project::models::FeatureVector& input) noexcept;

    bool initialized_{false};
};

}  // namespace project::ai
