#pragma once

#include <type_traits>

#include "Models/EventRecord.h"
#include "Models/FeatureVector.h"
#include "Models/HealthSnapshot.h"
#include "Models/ModelOutput.h"
#include "Models/ProcessedFrame.h"
#include "Models/RiskAssessment.h"
#include "Models/SensorFrame.h"
#include "Models/Serialization.h"

namespace project::models {
static_assert(std::is_trivially_copyable<SensorFrame>::value,
              "SensorFrame must be safe to copy by value into a bounded RTOS queue");
static_assert(std::is_trivially_copyable<ProcessedFrame>::value,
              "ProcessedFrame must be safe to copy by value into a bounded RTOS queue");
static_assert(std::is_trivially_copyable<FeatureVector>::value,
              "FeatureVector must be safe to copy by value into a bounded RTOS queue");
static_assert(std::is_trivially_copyable<ModelOutput>::value,
              "ModelOutput must be safe to copy by value into a bounded RTOS queue");
static_assert(std::is_trivially_copyable<RiskAssessment>::value,
              "RiskAssessment must be safe to copy by value into a bounded RTOS queue");
static_assert(std::is_trivially_copyable<EventRecord>::value,
              "EventRecord must be safe to copy by value into a bounded RTOS queue");
static_assert(std::is_trivially_copyable<HealthSnapshot>::value,
              "HealthSnapshot must be safe to copy by value into a bounded RTOS queue");
}  // namespace project::models
