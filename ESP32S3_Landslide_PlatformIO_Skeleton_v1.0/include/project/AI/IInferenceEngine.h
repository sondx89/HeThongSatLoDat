#pragma once
#include "Models/FeatureVector.h"
#include "Models/ModelOutput.h"

namespace project::ai {
class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;
    virtual bool initialize() = 0;
    virtual bool run(const project::models::FeatureVector& input,
                     project::models::ModelOutput& output) = 0;
};
}  // namespace project::ai
