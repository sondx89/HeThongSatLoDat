#pragma once
#include "AI/IInferenceEngine.h"
#include "AI/SchemaRegistry.h"

namespace project::ai {
class EdgeImpulseAdapter final : public IInferenceEngine {
public:
    bool initialize() override;
    bool run(const project::models::FeatureVector& input,
             project::models::ModelOutput& output) override;
    bool getModelMetadata() const;
};
}  // namespace project::ai
