# RiskManager

`project/Managers/RiskManager.h` fuses an approved scalar model output with an
independently validated tilt deviation and an explicit data-quality state. It
produces the four requested outcomes through the existing `RiskAssessment`
contract:

| Requested level | `RiskAssessment::risk` |
|---|---|
| `SAFE` | `RiskState::Safe` (value-compatible alias of `Normal`) |
| `MONITOR` | `RiskState::Monitor` |
| `HIGH_RISK_TEST` | `RiskState::HighRiskTest` (value-compatible alias of `DangerTest`) |
| `UNKNOWN` | `RiskState::Unknown` |

`HIGH_RISK_TEST` is a test/prototype state, not a certified geotechnical limit,
evacuation instruction, or landslide probability. The model score remains a
score in the configured model's domain; it is not called a probability.

## Approval gates and policy

The target contract and risk thresholds remain DRAFT/TBD. The default
`RiskManagerConfig` is intentionally invalid. Before classification, an
integrator must provide an approved, hash-verified config with:

- nonzero config version and SHA-256;
- at least one enabled evidence source (model or tilt);
- explicit approval flags for model-only or tilt-only fallback when both
  sources are enabled;
- finite, strictly ordered model score bounds and safe/monitor boundaries,
  plus the score direction and expected model output count, if model scoring
  is enabled;
- nonnegative, ordered absolute tilt-deviation limits, if the tilt rule is
  enabled; and
- explicitly configured promotion/recovery sample counts and recovery hold
  duration.

Do not copy provisional numbers from older project documents into a released
configuration without validation and approval. The caller must perform the
actual manifest/hash/approval verification; the booleans in this C++ config
are not cryptographic verification.

When both evidence sources are enabled and valid, the more severe candidate
wins. The aggregate quality state gates model evidence. Tilt is an independent
rule only when its `TiltValidation` is valid and its definition is approved.
If either enabled source is unavailable, the other may be used alone only when
its fallback is explicitly approved. The exception is an independently
validated `HIGH_RISK_TEST` tilt candidate, which can override invalid aggregate
quality or unavailable model evidence. With non-`VALID` quality, tilt `SAFE`
and `MONITOR` candidates are suppressed to `UNKNOWN`. An invalid config,
zero boot/sequence identity, or invalid/stale/missing input is never converted
to `SAFE`.

The model rule classifies `ModelOutput::predictionValue` only when status is
`Ok`, the prediction is valid and finite, output count matches the configured
count, and the score is inside configured bounds. Boundary comparisons are
inclusive at the configured safe/monitor boundary; values outside the
configured score range are rejected as model evidence. The tilt rule accepts
only `TiltValidationStatus::Valid` with `definitionApproved == true` and a
finite deviation in degrees; it compares the absolute deviation. The caller
must validate its reference pose, axis/frame, formula, calibration and units.

Promotions require the configured number of distinct input sequence samples.
Lower-severity recovery requires its configured sample count and hold time.
Duplicate sequence IDs do not advance hysteresis. A boot change or out-of-order
sequence clears the active state to `UNKNOWN`; invalid quality resets the
state unless the independent validated severe tilt rule is active. The manager
is stateful and is intended to be owned and called by `TaskRisk`, not shared
concurrently.

`RiskAssessment::health` is copied separately from the risk level. Fault
flags and reason-code assignments remain TBD and are not fabricated here.
`riskScoreValid` is set only when a configured model score was valid. It does
not imply probability calibration.

## Integration example

```cpp
#include "project/Managers/RiskManager.h"

project::managers::RiskManagerConfig config{};
// Populate fields only from an approved risk config/manifest:
// version, hashSha256, hashVerified, approved, enabled rules, thresholds,
// output count, hysteresisConfigured, promotionSamples, recoverySamples,
// and recoveryHoldMs.
project::managers::RiskManager riskManager(config);

project::models::ModelOutput model{};
project::managers::TiltValidation tilt{};
project::managers::RiskEvaluationContext context{};
project::models::RiskAssessment assessment{};

// Supply the actual inference result, approved tilt validation and quality.
// The zero-initialized policy above is not approved and yields UNKNOWN.
const bool produced = riskManager.evaluate(
    model, tilt, project::models::DataQualityState::MISSING,
    context, assessment);
if (produced && assessment.risk == project::models::RiskState::Unknown) {
    // Record/report that no defensible risk classification is available.
}
```

The existing `TaskRisk` compatibility overload consumes `ProcessedFrame` and
`HealthSnapshot`. `ProcessedFrame` currently has roll/pitch fields but no
approved single tilt-deviation contract, so that overload deliberately does
not substitute roll, pitch or an unapproved formula for `TiltValidation`.
Provide a validated tilt value through the explicit overload when the tilt
contract and its integration are approved.
