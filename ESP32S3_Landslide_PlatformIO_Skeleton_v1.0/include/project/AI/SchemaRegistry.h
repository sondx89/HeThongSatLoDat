#pragma once
#include "Models/FeatureVector.h"

namespace project::ai {
class SchemaRegistry {
public:
    bool validate(const project::models::FeatureVector& vector) const;
    bool compatible() const;
};
}  // namespace project::ai
