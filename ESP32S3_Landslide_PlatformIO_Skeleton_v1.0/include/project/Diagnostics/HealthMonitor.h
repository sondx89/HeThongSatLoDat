#pragma once
#include "Models/HealthSnapshot.h"

namespace project::diagnostics {
class HealthMonitor {
public:
    project::models::HealthSnapshot snapshot() const;
    void recordMetric();
};
}  // namespace project::diagnostics
