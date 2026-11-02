# AI Coding Prompts v3.0 — AI-first / CSV dataset implementation

**Companion backlog:** `TODO_LIST_ESP32S3_Implementation_Backlog_v4.0.md`  
**CSV contract:** `docs/CSV_DATASET_CONTRACT.md`

## PROMPT 00 — MASTER SESSION RULES

```text
You are implementing the AI/data pipeline of an ESP32-S3 landslide-monitoring
project, but exact physical hardware is not yet available.

Prioritize host-native C++17 code, deterministic synthetic data, replay,
feature/window validation, dataset export, CSV parsing/validation, leakage
checks and evaluation tooling. Hardware-dependent code must remain behind
interfaces and NotConfigured/fail-closed states.

Read the current repository and the controlled documents before editing.
Use DSD v1.2, DOP v1.0, FSS v1.1 and GTIS v1.0 as supplied inputs. Do not
silently alter them. If a version/approval conflict exists, record a decision.

NON-NEGOTIABLE:
- No guessed sensor pins, I2C address/clock, ADC settings, physical units,
  calibration coefficients, sample rate, window/stride, feature count/order,
  model dimensions, regression target, risk thresholds or watchdog timing.
- Synthetic data must carry data_origin=SYNTHETIC and evidence_class=GT5.
- Synthetic proxy labels are not real ground truth and cannot support field
  accuracy or safety claims.
- Do not create labels from risk thresholds and then claim independent
  ground truth.
- Invalid/missing/stale/nonfinite/out-of-order samples never become valid zero.
- No default hold-last/interpolation/imputation.
- Keep operational six-column CSV separate from DSD training exports.
- Use bounded CSV formatting: UTF-8, comma, double quote, LF, decimal dot.
- Invalid numeric fields are empty and accompanied by validity/quality.
- No raw C++ struct memcpy serialization.
- No unbounded allocation, retry, blocking, or silent truncation.
- FeatureVector capacity is not model dimension.
- Actual Edge Impulse integration requires actual generated export metadata.
- Every responsibility has one owner; do not duplicate CSV escaping, quality
  classification, calibration, feature-window logic, schema matching,
  inference, risk mapping or physical storage.
- Do not weaken/delete tests to pass.
- Report native tests, target build, HIL and model evidence separately.

Before editing:
1. Inspect current code and callers.
2. State current behavior and gaps.
3. List exact allowed files and ownership.
4. State contract/API impact and dependencies.
5. Implement only the assigned prompt.
6. Add normal, invalid, boundary and injected-failure tests.
7. Run exact commands and report exit codes.
8. Update task-owned docs and TODO only.
If another file/API is required, stop and ask before changing it.
```

## PROMPT A00 — Baseline and AI/data code inventory

```text
Read the current source and documents. Do not implement features.
Run clean native unit/integration tests and ESP32-S3 compile-only build.
Inventory FeaturePipeline, RiskManager, SchemaRegistry, EdgeImpulseAdapter,
CsvLogger, ReplayEngine, MockCsvTelemetry, DataQualityService,
CalibrationService, Serialization and tests.
Explain whether current six-column CSV is operational/diagnostic or suitable
for DSD raw training data. List existing code that can be reused and exact
missing behavior. Record commands, toolchain versions, exit codes and limits.
Allowed modifications: baseline/audit docs and TODO only.
```

## PROMPT A01 — Data contracts and test-only boundaries

```text
Create a data dictionary for raw sample, quality sample, calibrated sample,
window, feature vector, prediction, annotation, evaluation and manifest.
Define origin, evidence class, validity, timestamps, identity keys and schema
version semantics. Monotonic timestamp_ms is not UTC; utc_time is nullable.
Synthetic must be GT5. Separate mock inference from production target.
Do not invent physical units, ranges, target semantics or risk thresholds.
Allowed: model/data contract docs and directly related tests; no unrelated
module rewrites.
```

## PROMPT A02 — DSD-compliant CSV writer and schemas

```text
Implement a shared bounded CSV writer and create docs/CSV_DATASET_CONTRACT.md.
Preserve DSD v1.2 raw sample core header/order exactly. Any additional field
must be appended under a new schema version or placed in manifest/sidecar.
Keep raw_samples, calibrated_samples, windows, features, annotations,
evaluation and quality report as separate artifacts.
CSV: UTF-8, comma, quote, LF, decimal dot. Escape comma/quote/CR/LF; double
embedded quotes. Invalid numeric is empty plus validity/quality. Reject
NaN/Inf and overflow; never silently truncate. Exact column count required.
Do not change existing six-column operational CsvLogger contract.
Allowed: new shared CSV writer files, dataset schema/export headers and
their tests, CSV contract doc. If shared interfaces need change, propose first.
```

## PROMPT A03 — Deterministic synthetic sensor generator

```text
Implement a host-only deterministic synthetic source with fixed seed,
generator version and scenario manifest. Scenarios: stable, gradual trend,
step change, oscillation, impulse/noise, saturation, dropout, stale,
out-of-order, sequence gap, reboot, calibration change and mixed validity.
Make raw observations and latent scenario parameters separate.
All records carry data_origin=SYNTHETIC and evidence_class=GT5.
Do not assign real landslide labels. If a proxy target is explicitly approved,
name it synthetic_proxy_*, store formula/version and field_claim=false.
Never use a hidden generator rule as both feature and label shortcut.
Allowed: simulator module, test fixtures, synthetic data policy docs and tests.
No hardware drivers or physical conversion changes.
```

## PROMPT A04 — Strict CSV replay and dataset validator

```text
Implement bounded parser for raw_samples.csv using exact schema version and
header. Reject malformed quotes, wrong column count, invalid encoding/numeric,
duplicate identity key, truncated row and invalid sequence/time transitions.
Preserve rejected row reference and reason in quarantine/quality report.
Validate manifest hashes/counts, channel presence, validity/quality consistency,
boot/session and sequence continuity. Do not coerce or impute.
Same file + same config must replay deterministically.
Allowed: Replay parser, DatasetValidator, tests and owned docs.
Do not edit CsvLogger's operational format.
```

## PROMPT A05 — Quality propagation and window eligibility

```text
DataQualityService is the only owner of quality classification.
Propagate channel quality to sample, window, feature, prediction and evaluation.
Default: reject/invalidate windows with required missing/stale/invalid/nonfinite/
out-of-order/calibration-invalid channels or sequence gaps. No zero-fill,
hold-last or interpolation unless a versioned approved policy exists.
Invalid rows may remain in raw archive but must not silently enter training.
Allowed: DataQualityService and window eligibility integration only if API
owner approves; tests and docs. Do not reimplement quality checks elsewhere.
```

## PROMPT A06 — Calibration raw-only behavior

```text
Implement explicit RAW_ONLY mode. Preserve raw driver values and validity.
Emit physical values only when approved calibration artifact, sensor identity,
unit, transform version and hash are available. Reject mismatch.
Do not convert MPU counts to g, soil to VWC, YL-83 to rainfall or HX710B to
pressure/stress without verified evidence.
CalibrationService signals calibration version changes; FeaturePipeline owns
window reset. Do not duplicate ring buffer logic.
Allowed: CalibrationService, its tests and calibration contract doc.
```

## PROMPT A07 — Feature windows and FSS compatibility

```text
Harden contiguous window building: sample count, stride, sequence, boot,
timestamp, reset reason and quality. No window across gap/reboot/calibration/
config/schema/sensor restart. kMaxFeatures is capacity only.
Only approved FSS features may be model-eligible. Candidate tilt/magnitude
remain conditional; raw soil/YL83/HX710B diagnostic until approved.
Do not assume Edge Impulse wants engineered vector; actual export decides.
Add deterministic golden synthetic fixtures and tolerance tests.
Allowed: FeaturePipeline/window builder, tests and its doc only.
```

## PROMPT A08 — Annotation and label management

```text
Implement annotations as separate records. Store target name/value/unit,
interval, session/boot identity, label status, evidence class GT1-GT5,
evidence reference, annotator/reviewer, uncertainty/confidence and method
version. Keep labels independent of raw samples. Join labels to windows by
explicit time interval and identity, not timestamp alone.
Synthetic proxy labels remain GT5 and separately reported. Never create
ground truth from RiskManager thresholds. Target contract remains undefined
until approved; do not fabricate target values.
Allowed: annotation schema/manager/export/tests/docs only.
```

## PROMPT A09 — Group-aware split and leakage prevention

```text
Implement dataset splitting by independent site/session/experiment/event group
before overlapping windows are generated, or guarantee all windows from one
group remain in one split. Never random-split overlapping windows across
train/validation/test. Persist split_group_id, split_name, policy version and
seed. Report synthetic and field origins separately. Add automated tests for
group disjointness, overlapping-window leakage and duplicate source groups.
Synthetic-only test is not a field test.
Allowed: DatasetSplitter, tests and split policy docs.
```

## PROMPT A10 — Inference interface and test-only mock engine

```text
Define IInferenceEngine metadata/input/output contract. Implement a deterministic
MockInferenceEngine only for host tests with explicit TEST_ONLY build guard.
Validate schema before inference and status/count/finite outputs afterward.
Return raw regression output separately from risk category. Do not invent
regression range, target unit or probability meaning. Production build must
not select mock engine.
Allowed: inference interface, mock engine, adapter tests and docs.
Do not integrate generated Edge Impulse code in this task.
```

## PROMPT A11 — Evaluation runner and reports

```text
Build evaluation tooling that reports data quality, feature distributions,
missingness, rejected counts, provenance and eligible label counts.
Compute regression metrics only if approved target contract and eligible labels
exist. Synthetic proxy metrics must be in a separate clearly named section,
not "landslide accuracy". Report origin, evidence class, split, sample count,
target range and exclusions. No metrics if target semantics are undefined.
Allowed: evaluation tool/module, tests and docs.
```

## PROMPT A12 — Edge Impulse actual model gate

```text
Search for actual Edge Impulse deployment export. If absent, stop production
integration and list required files/metadata. Do not guess API signatures,
input dimensions, feature order, sample rate, window/stride or regression
semantics. Keep metadata compatibility gate and fail-closed behavior.
When export exists, compare exact metadata to approved FSS/hash and add golden
vectors. Allowed: EdgeImpulseAdapter and actual generated export only after
provided, tests and adapter doc.
```

## PROMPT A13 — Risk policy isolation

```text
Review RiskManager. A numeric regression output is not automatically a risk
score or landslide probability. No thresholds/hysteresis until RiskConfig and
target semantics are approved. Invalid/stale/missing/nonfinite/schema mismatch
or model failure must not become SAFE/NORMAL. Keep raw model output distinct
from risk state. Allowed: RiskManager and tests/docs; do not edit model adapter.
```

## PROMPT A14 — End-to-end native AI/data integration

```text
Test deterministic synthetic source → quality → raw-only calibration →
window/features → schema gate → mock inference → prediction validation →
annotation join/evaluation → CSV export. Then replay the exported raw CSV
through the same pipeline and compare deterministic outputs.
Fault inject missing channel, stale, gap, reboot, malformed CSV, schema mismatch,
calibration mismatch and invalid model output. Assert GT5 provenance survives
and production inference/risk gates remain closed.
Allowed: integration tests and minimal approved composition adapter only.
```

## PROMPT A15 — Documentation and backlog sync

```text
Update README and AI/data docs for host-first development, simulator, replay,
CSV artifacts, schema versions, commands and limitations. Keep controlled
Document/* unchanged. Update TODO only with exact evidence. Mark a task DONE
only after tests and review. Field release remains NOT READY while hardware,
calibration, actual model, target and safety evidence are absent.
```

## PROMPT 24 — Independent anti-overlap review (reuse)

```text
Review the diff without editing. Check single ownership, duplicate validation,
CSV escaping duplication, operational-vs-training CSV separation, test-only
mock leakage, synthetic/GT5 provenance, invalid-to-zero, hidden imputation,
window leakage, schema/version compatibility, guessed units/thresholds/model
metadata, bounded operations and docs overclaim. Report Critical/Major/Minor
with file/line and exact owner. Do not fix.
```

## Required handover

```text
Task/prompt:
Status: DONE / PARTIAL / BLOCKED / FAILED
Commit:
Allowed files / modified files:
Current behavior before change:
Implementation summary:
Single responsibility owner:
Schema/API/persistence changes:
Synthetic provenance and GT5 handling:
Tests: exact commands, environment, exit codes and counts:
ESP32 target build:
Hardware/model evidence (or NOT RUN):
Docs/TODO updated:
Known limitations/blockers:
Next task and prerequisites:
```

## Execution rules

- One agent/branch per task.
- Do not run two agents on the same public header, test translation unit,
  `platformio.ini`, CSV schema, TODO or module guide concurrently.
- Merge A00→A01→A02/A03→A04/A05/A06→A07→A08/A09→A10/A11→A12/A13→A14.
- Run Prompt 24 after every task.
- Host pass is not hardware validation; synthetic metrics are not field accuracy.
