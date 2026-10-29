#pragma once
#include "Models/ProcessedFrame.h"
#include "Models/FeatureVector.h"

namespace project::services {
class FeaturePipeline {
public:
    bool push(const project::models::ProcessedFrame& frame);
    bool windowReady() const;
    bool build(project::models::FeatureVector& output);
    void resetWindow();
};
}  // namespace project::services
