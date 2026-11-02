#pragma once

#include <cstdint>

#include "Models/DataContractTypes.h"

namespace project::managers {

enum class RecoveryDecision : std::uint8_t {
    NoAction,
    NoRetry,
    Retry,
    RetryDisabled,
    RetryLimitReached,
    RecoveryUnavailable,
    InhibitInference,
    ManualReview
};

struct RecoveryContext {
    project::models::FaultKind faultKind;
    bool inferenceSchemaOrModelMismatch{false};
};

struct RetryPolicy {
    std::uint16_t maxRetries{0U};
    bool recoveryAvailable{false};
};

struct FaultClassification {
    bool recognized{false};
    bool hasFault{false};
    project::models::ApiStatus status{
        project::models::ApiStatus::InternalError};
    project::models::FaultKind faultKind{
        project::models::FaultKind::DataInvalid};
    project::models::EventSeverity severity{
        project::models::EventSeverity::Error};
    RecoveryDecision decision{RecoveryDecision::ManualReview};
};

class RecoveryManager {
public:
    static FaultClassification classifyError(
        project::models::ApiStatus status, const RecoveryContext& context,
        const RetryPolicy& retryPolicy,
        std::uint16_t retriesAlreadyUsed = 0U) noexcept;
};

}  // namespace project::managers
