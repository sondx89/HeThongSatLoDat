#pragma once
#include "Models/FeatureVector.h"
#include "Models/ModelOutput.h"

namespace project::ai {
class IModelRuntime {
public:
    virtual ~IModelRuntime() = default;
    virtual bool initialize() = 0;
    virtual bool run(const project::models::FeatureVector& input,
                     project::models::ModelOutput& output) = 0;
};

using IInferenceEngine = IModelRuntime;
}  // namespace project::ai
