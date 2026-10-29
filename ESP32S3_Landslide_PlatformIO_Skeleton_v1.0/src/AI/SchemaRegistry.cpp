#include "project/AI/SchemaRegistry.h"

namespace project::ai {

bool SchemaRegistry::validate(
    const project::models::FeatureVector& vector) const {
    (void)vector;
    return false;
}

bool SchemaRegistry::compatible() const {
    return false;
}

}  // namespace project::ai
