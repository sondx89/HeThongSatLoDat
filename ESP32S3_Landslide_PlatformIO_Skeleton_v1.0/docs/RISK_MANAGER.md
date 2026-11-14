# RiskManager

`project/Managers/RiskManager.h` validates an approved model output against
explicit data-quality and provenance state before producing a
`RiskAssessment`. Production classification is model-only; tilt rules are
available only in explicit test/replay builds. The existing risk states are:

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
integrator must provide an approved, hash-verified model config with:

- nonzero config version and SHA-256;
- the model rule enabled;
- explicit `modelScoreSemanticsApproved` approval when model scoring is
  enabled, confirming that the approved target contract and risk policy
  define how the single raw model output maps to the configured score;
- finite, strictly ordered model score bounds and safe/monitor boundaries,
  plus the score direction and expected model output count, if model scoring
  is enabled;
- explicitly configured promotion/recovery sample counts and recovery hold
  duration.

Test/replay builds may additionally exercise a tilt rule with finite,
ordered limits and an approved tilt definition. Those flags and fallback
behavior are rejected by production validation.

Production builds additionally require the model rule and reject all tilt-rule
configurations. Tilt-only or combined model/tilt behavior exists solely in
explicit `PROJECT_TEST_ONLY=1` builds for host testing/replay; it cannot be
enabled by a production manifest. A valid production model configuration still
requires approved target/output semantics, and unavailable or invalid model
results remain `UNKNOWN`.

Do not copy provisional numbers from older project documents into a released
configuration without validation and approval. The caller must perform the
actual manifest/hash/approval verification; the booleans in this C++ config
are not cryptographic verification.

In test-only configurations, the more severe candidate wins. The aggregate
quality state gates model evidence. Tilt is an independent rule only in those
test-only configurations.
An invalid config, zero boot/sequence identity, or invalid/stale/missing input
is never converted to `SAFE`. Invalid model evidence remains `UNKNOWN` in
production; no sensor-only rule may replace it.

The model rule is available only for one output and explicit
`modelScoreSemanticsApproved`. It classifies `ModelOutput::predictionValue`
only when status is `Ok`, the prediction and sole raw output are finite and
equal, output count is one, and the score is inside configured bounds.
`predictionValue` is not independently treated as a risk score, probability,
or target value. Boundary comparisons are inclusive at the configured
safe/monitor boundary; values outside the configured score range are rejected
as model evidence. Exact feature-schema matching remains owned by
`SchemaRegistry`/inference validation; non-`Ok` results such as
`VersionMismatch`, or missing model provenance in the compatibility path,
cannot produce a model-derived safe state. In test-only builds, the tilt rule
accepts only `TiltValidationStatus::Valid` with `definitionApproved == true`
and a finite deviation in degrees; it compares the absolute deviation. The
caller must validate its reference pose, axis/frame, formula, calibration and
units.

Promotions require the configured number of distinct input sequence samples.
Lower-severity recovery requires its configured sample count and hold time.
Duplicate sequence IDs do not advance hysteresis. A boot change or out-of-order
sequence clears the active state to `UNKNOWN`; invalid quality resets the
state. The manager is stateful and is intended to be owned and called by
`TaskRisk`, not shared concurrently.

`RiskAssessment::health` is copied separately from the risk level. Fault
flags and reason-code assignments remain TBD and are not fabricated here.
`riskScoreValid` is set only when a configured model score was valid. It does
not imply probability calibration.

## Integration example

```cpp
#include "project/Managers/RiskManager.h"

project::managers::RiskManagerConfig config{};
// Populate fields only from an approved risk config/manifest, including
// modelScoreSemanticsApproved only after the model target and score mapping
// are approved:
// version, hashSha256, hashVerified, approved, enabled rules, thresholds,
// output count, hysteresisConfigured, promotionSamples, recoverySamples,
// and recoveryHoldMs.
project::managers::RiskManager riskManager(config);

project::models::ModelOutput model{};
project::managers::TiltValidation tilt{};
project::managers::RiskEvaluationContext context{};
project::models::RiskAssessment assessment{};

// Supply the actual inference result and quality. Tilt input is test-only.
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
