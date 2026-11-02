#include "project/Managers/RecoveryManager.h"

namespace project::managers {
namespace {

bool isKnownStatus(project::models::ApiStatus status) noexcept {
    using project::models::ApiStatus;
    switch (status) {
    case ApiStatus::Ok:
    case ApiStatus::InvalidArgument:
    case ApiStatus::NotInitialized:
    case ApiStatus::Timeout:
    case ApiStatus::IoError:
    case ApiStatus::DataInvalid:
    case ApiStatus::StaleData:
    case ApiStatus::BufferFull:
    case ApiStatus::NotSupported:
    case ApiStatus::VersionMismatch:
    case ApiStatus::ResourceExhausted:
    case ApiStatus::InternalError:
    case ApiStatus::Busy:
    case ApiStatus::Cancelled:
        return true;
    }
    return false;
}

bool isKnownFaultKind(project::models::FaultKind kind) noexcept {
    using project::models::FaultKind;
    switch (kind) {
    case FaultKind::SensorFault:
    case FaultKind::ModelError:
    case FaultKind::StorageError:
    case FaultKind::ConfigError:
    case FaultKind::TimeUnsync:
    case FaultKind::OutputFault:
    case FaultKind::DataInvalid:
    case FaultKind::QueueOverflow:
    case FaultKind::WatchdogRecovery:
        return true;
    }
    return false;
}

project::models::EventSeverity severityFor(
    project::models::ApiStatus status) noexcept {
    using project::models::ApiStatus;
    using project::models::EventSeverity;
    switch (status) {
    case ApiStatus::Ok:
        return EventSeverity::Info;
    case ApiStatus::Timeout:
    case ApiStatus::StaleData:
    case ApiStatus::Busy:
        return EventSeverity::Warn;
    case ApiStatus::InternalError:
        return EventSeverity::Critical;
    case ApiStatus::InvalidArgument:
    case ApiStatus::NotInitialized:
    case ApiStatus::IoError:
    case ApiStatus::DataInvalid:
    case ApiStatus::BufferFull:
    case ApiStatus::NotSupported:
    case ApiStatus::VersionMismatch:
    case ApiStatus::ResourceExhausted:
    case ApiStatus::Cancelled:
        return EventSeverity::Error;
    }
    return EventSeverity::Error;
}

bool isRetryableStatus(project::models::ApiStatus status) noexcept {
    using project::models::ApiStatus;
    return status == ApiStatus::Timeout || status == ApiStatus::IoError ||
           status == ApiStatus::Busy;
}

}  // namespace

FaultClassification RecoveryManager::classifyError(
    project::models::ApiStatus status, const RecoveryContext& context,
    const RetryPolicy& retryPolicy,
    std::uint16_t retriesAlreadyUsed) noexcept {
    FaultClassification result{};
    result.status = status;
    result.faultKind = context.faultKind;
    result.hasFault = status != project::models::ApiStatus::Ok;

    if (!isKnownStatus(status) ||
        (status != project::models::ApiStatus::Ok &&
         !isKnownFaultKind(context.faultKind))) {
        return result;
    }
    result.recognized = true;
    result.recognized = true;
    result.severity = severityFor(status);
    if (!result.hasFault) {
        result.decision = RecoveryDecision::NoAction;
        return result;
    }

    if (context.inferenceSchemaOrModelMismatch ||
        status == project::models::ApiStatus::VersionMismatch) {
        result.decision = RecoveryDecision::InhibitInference;
        return result;
    }

    if (!isRetryableStatus(status)) {
        result.decision = RecoveryDecision::NoRetry;
        return result;
    }
    if (!retryPolicy.recoveryAvailable) {
        result.decision = RecoveryDecision::RecoveryUnavailable;
        return result;
    }
    if (retryPolicy.maxRetries == 0U) {
        result.decision = RecoveryDecision::RetryDisabled;
        return result;
    }
    if (retriesAlreadyUsed >= retryPolicy.maxRetries) {
        result.decision = RecoveryDecision::RetryLimitReached;
        return result;
    }

    result.decision = RecoveryDecision::Retry;
    return result;
}

}  // namespace project::managers
