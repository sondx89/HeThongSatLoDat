# ADR: AI-first production risk pipeline

**Status:** Workspace implementation constraint; controlled-system approval
pending  
**Date:** 2026-11-14

## Context

The Pilot instruction requires AI inference to be the primary risk source and
prohibits CASE 1–11 or `DemoRulePolicy` from acting as production risk truth.
The repository contains schema/inference checks and a `RiskManager`, but no
owner-supplied Edge Impulse export, released FSS/output contract, approved
risk semantics, or production task composition. Sensor-derived risk thresholds
must not be invented.

## Decision

1. Production risk configuration must enable the approved model rule and may
   not enable the test tilt-rule path.
2. Model status, output shape/value, provenance and schema/model identity must
   pass their respective contracts before the result can be interpreted.
3. Missing, invalid, stale, mismatched or unavailable model evidence produces
   `UNKNOWN`/degraded behavior, never a fallback to CASE scenarios or raw
   sensor thresholds.
4. `PROJECT_TEST_ONLY=1` builds may retain tilt-rule behavior for unit tests
   and replay only. They are not production evidence.
5. `HIGH_RISK_TEST` and model scores are not certified landslide probabilities,
   evacuation decisions or geotechnical thresholds.
6. Alarm/event mapping remains separately gated by an approved output policy.

The C++ configuration booleans are validation gates, not cryptographic
approval. Production composition must verify the approved manifest and its
artifact hashes before constructing the policy.

## Consequences

- Production `RiskManager::validateConfig` rejects tilt-only and combined
  model/tilt configurations.
- Until the real model export and compatible approved schema/semantics exist,
  production inference remains disabled/invalid and risk remains `UNKNOWN`.
- Host tests exercise contract behavior only; they do not prove model or
  geotechnical validity.
- This ADR does not resolve any conflict in controlled DOP/DSD/FSS/ICD/LLD/TCS
  documents. Owner review is required before release baselines are frozen.

## Verification

`native-risk-production-tests` compiles `RiskManager.cpp` without
`PROJECT_TEST_ONLY`; it verifies that a model-only config can validate and
that tilt rules are rejected. The ordinary unit/integration environments
define `PROJECT_TEST_ONLY=1` for scenario/replay fixtures.
