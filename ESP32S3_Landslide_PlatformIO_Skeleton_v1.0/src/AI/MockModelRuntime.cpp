#include "project/AI/MockModelRuntime.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace project::ai {

bool MockModelRuntime::initialize() {
    initialized_ = true;
    return true;
}

bool MockModelRuntime::isValidInput(
    const project::models::FeatureVector& input) noexcept {
    if (!input.valid || input.count == 0U ||
        input.count > project::models::FeatureVector::kMaxFeatures) {
        return false;
    }

    const std::uint32_t requiredMask =
        input.count == project::models::FeatureVector::kMaxFeatures
            ? std::numeric_limits<std::uint32_t>::max()
            : (std::uint32_t{1} << input.count) - 1U;
    if (input.featureValidMask != requiredMask) {
        return false;
    }

    for (std::size_t i = 0; i < input.count; ++i) {
        if (!std::isfinite(input.values[i])) {
            return false;
        }
    }
    return true;
}

bool MockModelRuntime::run(
    const project::models::FeatureVector& input,
    project::models::ModelOutput& output) {
    output = {};
    output.bootId = input.bootId;

    if (!initialized_) {
        output.status = project::models::InferenceStatus::ModelError;
        return false;
    }
    if (!isValidInput(input)) {
        output.status = project::models::InferenceStatus::InvalidInput;
        return true;
    }

    output.status = project::models::InferenceStatus::ModelError;
    return true;
}

}  // namespace project::ai
