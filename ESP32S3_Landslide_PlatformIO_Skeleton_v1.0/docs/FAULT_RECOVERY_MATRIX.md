# Fault classification and recovery decision

`RecoveryManager::classifyError()` is a pure policy function. It neither
executes recovery nor performs hardware, I2C, watchdog, task, or restart
operations.

## Catalog boundary

The function reuses the existing `models::ApiStatus`, `models::FaultKind`, and
`models::EventSeverity` enums. The repository does not yet contain a released
stable `FaultCode` catalog; sensor error codes are explicitly TBD. This policy
therefore introduces no numeric fault/error codes. The caller supplies the
existing `FaultKind` as context for a non-`Ok` status. Unknown status or fault
enum values return `recognized == false` and `ManualReview`, never a guessed
fault category.

## Status classification

| Existing `ApiStatus` | Severity | Retry classification |
|---|---|---|
| `Ok` | `Info` | `NoAction` |
| `Timeout` | `Warn` | Retry-eligible |
| `IoError` | `Error` | Retry-eligible |
| `Busy` | `Warn` | Retry-eligible |
| `StaleData` | `Warn` | No automatic retry |
| `InternalError` | `Critical` | No automatic retry |
| All other declared non-`Ok` values | `Error` | No automatic retry |
| Unknown enum value | `Error` default; unrecognized | `ManualReview` |

`VersionMismatch` always yields `InhibitInference`. The caller's
`inferenceSchemaOrModelMismatch` context flag also yields `InhibitInference`
regardless of status. No retry decision can override this inhibition.

## Bounded retry decision

Retry eligibility is limited to `Timeout`, `IoError`, and `Busy`. The caller
must inject a `RetryPolicy`; its default has `maxRetries == 0` and
`recoveryAvailable == false`, so it cannot recommend a retry. The configured
limit is the maximum number of retries (not total attempts).

For an eligible status, decisions are evaluated in this order:

1. `RecoveryUnavailable` if no recovery mechanism is available.
2. `RetryDisabled` if `maxRetries == 0`.
3. `RetryLimitReached` if `retriesAlreadyUsed >= maxRetries`.
4. `Retry` otherwise.

`Retry` is only a recommendation; no retry is attempted. Backoff, retry delay,
and concrete recovery implementation remain unconfigured/TBD. All other known
errors return `NoRetry`. The policy does not infer a fallback action for
unknown status or fault values.

## Validation

Native tests cover known and unknown catalog values, success/no-fault,
retry-disabled, retry within the configured budget, retry-limit reached,
recovery unavailable, non-retryable status, and schema/model mismatch
inference inhibition.
