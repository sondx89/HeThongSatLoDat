# TODO v4.0 — AI-first implementation and training dataset pipeline

**Date:** 2026-11-02  
**Baseline:** ESP32-S3 / PlatformIO / Arduino / C++17 / MPU9250 over I2C selected; exact board and remaining sensor hardware TBD.  
**Priority change:** Until hardware is available, prioritize host-executable AI/data code, deterministic sensor simulation, replay, CSV dataset generation, feature validation, and model evaluation. Hardware-specific production work is explicitly deferred.

## 1. Updated project direction

### 1.1 What can be completed without hardware

The following work can proceed now using host-native C++ tests and deterministic offline data:
- Complete and test raw sample → quality → calibration-state → feature window → feature vector → model adapter interface → regression result validation → risk-policy interface.
- Build a deterministic sensor simulator that emits realistic *synthetic scenarios* with explicit units/assumptions, validity, timestamps, sequence IDs, and reproducible seeds.
- Build replay tooling that consumes CSV files and reproduces the same pipeline.
- Replace the current six-column operational CSV as the training source with a versioned, provenance-rich raw dataset export and separate window/feature/label/evaluation outputs.
- Add data validation, quality/eligibility reports, split-grouping and leakage checks.
- Add mock model adapters for pipeline testing only; do not treat their output as Edge Impulse inference or as evidence of model accuracy.
- Implement host-side feature golden tests using explicitly documented synthetic fixtures.
- Prepare Edge Impulse integration interfaces and metadata gates. Actual generated model integration remains blocked until the export is supplied.

### 1.2 What must remain deferred

Do not implement or claim production-ready:
- GPIO assignments, I2C address/clock, ADC configuration, actual sensor scale/range, board wiring.
- Calibration coefficients or physical unit conversions for unverified sensors.
- Physical tilt as a valid model feature until axis convention, reference pose, filter and validation are approved.
- Soil moisture as VWC, YL-83 as rainfall, or HX710B raw counts as pressure/stress without identified hardware and calibration.
- Actual Edge Impulse `run_classifier()` integration, model dimensions or regression semantics without the deployment export and model metadata.
- Real-world landslide probability, risk thresholds, SAFE/WATCH/DANGER labels, or claims of field accuracy without an approved target contract and independent ground truth.

## 2. Evidence from supplied requirement documents

This backlog is aligned with the supplied `Document/DSD_Dataset_Specification_ESP32S3_EdgeImpulse_v1.2.txt`, `DOP_Dataset_Operations_Governance_Plan_ESP32S3_EdgeImpulse_v1.0.txt`, `FSS_Feature_Schema_Specification_ESP32S3_EdgeImpulse_v1.1.txt` and `GTIS_Ground_Truth_Instrument_Specification_ESP32S3_EdgeImpulse_v1.0.txt`.

Important requirements to preserve:
- DSD defines UTF-8 CSV as the default exchange format; delimiter comma, quote `"`, LF record terminator, decimal dot; escape quote by doubling it.
- Invalid numeric values are empty fields with validity/quality, never NaN/Inf.
- Raw sample schema includes dataset/site/device/sensor-set/session/boot/sequence/time/version/calibration/status metadata; raw accelerometer/soil/YL-83/HX710B channels; per-channel validity/quality; masks/flags; event and annotation references.
- A channel that is not physically present must be marked absent, not represented as zero.
- Raw and calibrated values must have distinct names and calibrated data must reference calibration/transform version.
- Invalid/missing/stale/out-of-order/calibration-invalid records are retained for audit where appropriate, but are not silently made training-valid.
- Feature windows must not cross sequence gaps, boot changes, calibration changes or schema changes unless an approved policy explicitly permits it; default is invalidate/reject.
- Each record/window needs separate eligibility for training, validation, test, replay and safety analysis.
- Regression is only a provisional model family. The regression target, unit, range, alignment and independent ground-truth method are release blockers. Do not assume output is a landslide probability or a 0–1 risk score.
- GTIS distinguishes GT1–GT4 evidence from GT5 synthetic/simulated/heuristic/proxy labels. GT5 must be explicitly identified and cannot be represented as real-world ground truth.
- FSS candidate features are not automatically approved model inputs. Actual Edge Impulse export metadata must define the final model input contract.

If a requirement document conflicts with source code, create a decision/change item; do not silently rewrite the requirement.

## 3. New implementation architecture (host-first)

Recommended offline pipeline:

`SyntheticSensorSource / CsvReplaySource`
→ `RawSample`
→ `DataQualityService`
→ `CalibrationService (RAW_ONLY unless approved artifact exists)`
→ `WindowBuilder / FeaturePipeline`
→ `SchemaRegistry`
→ `IInferenceEngine`
→ `PredictionValidator`
→ `RiskManager (policy disabled unless approved config)`
→ `Evaluation / CSV exporters`

Responsibilities:
- `SyntheticSensorSource`: deterministic synthetic samples and scenario metadata only.
- `CsvReplaySource`: parse and validate raw dataset rows; never impute invalid data.
- `DataQualityService`: single owner of channel validity/quality classification.
- `CalibrationService`: preserve raw and validity; physical conversion only with approved calibration.
- `WindowBuilder/FeaturePipeline`: contiguous-window checks, feature calculation and feature provenance.
- `SchemaRegistry`: schema compatibility only.
- `IInferenceEngine`: abstract inference interface.
- `MockInferenceEngine`: test-only deterministic mock, never a production model.
- `EdgeImpulseAdapter`: actual generated API adapter, blocked until real export exists.
- `PredictionValidator`: finite output, dimension, status and target metadata checks; does not assign risk category.
- `RiskManager`: risk policy only when approved target semantics/config exist.
- `DatasetExporter`: canonical raw/feature/label/evaluation exports.
- `DatasetValidator`: schema, completeness, quality, provenance and eligibility checks.
- `DatasetSplitter`: group-aware split by site/session/event/physical experiment; never random-split overlapping windows from the same source group.
- `EvaluationRunner`: metrics only when labels are eligible and target contract is defined.

Do not duplicate CSV formatting in modules. All CSV output must use one shared bounded CSV writer/escaping utility or an explicitly justified independent tool-side implementation.

## 4. Revised work order

| Order | ID | Priority | Work item | Hardware needed? | Dependency |
|---:|---|---|---|---|---|
| 1 | A00 | P0 NOW | Refresh baseline and inventory AI/data code | No | None |
| 2 | A01 | P0 NOW | Freeze data contracts and test-only boundaries | No | A00 |
| 3 | A02 | P0 NOW | Dataset schema contract and canonical CSV writer | No | A01 + DSD review |
| 4 | A03 | P0 NOW | Deterministic synthetic sensor source and scenario catalog | No | A01 |
| 5 | A04 | P0 NOW | CSV replay/parser + dataset validator | No | A02 |
| 6 | A05 | P0 NOW | Quality/validity propagation and invalid-window rules | No | A01/A03/A04 |
| 7 | A06 | P0 NOW | Calibration raw-only mode and explicit calibration interface | No | A01 |
| 8 | A07 | P0 NOW | Feature window builder and FSS-gated feature pipeline | No | A05/A06 |
| 9 | A08 | P0 NOW | Feature/label/annotation/export separation | No | A02/A07 |
| 10 | A09 | P0 NOW | Group-aware dataset split and leakage tests | No | A08 |
| 11 | A10 | P1 NOW | Mock inference interface and prediction validation | No | A07/A01 |
| 12 | A11 | P1 NOW | Evaluation runner and quality report | No | A08/A09/A10 |
| 13 | A12 | P1 WAIT | Edge Impulse metadata gate and adapter contract | No, until actual export for production | A07/A10 |
| 14 | A13 | P1 NOW | RiskManager policy isolation and invalid-input behavior | No | A10 + approved policy decision |
| 15 | A14 | P1 NOW | End-to-end native integration/replay tests | No | A03-A13 |
| 16 | A15 | P1 DOC | AI/data documentation and prompt/backlog synchronization | No | A00-A14 |
| 17 | H00 | P2 HW | Hardware bring-up and sensor drivers | Yes | Board/sensor evidence |
| 18 | H01 | P2 HW | Physical calibration and unit conversion | Yes | H00 + calibration instruments |
| 19 | H02 | P2 MODEL | Actual Edge Impulse export + target inference | Actual model export; target build | A12 + approved FSS |
| 20 | H03 | P2 HW | Target performance, HIL, watchdog and output | Yes | H00-H02 |

## 5. Detailed tasks — AI and data first

### A00 — Baseline refresh and focused inventory [NOW]
- [x] Run clean native unit and integration tests.
- [x] Run ESP32-S3 compile-only build and record whether failures are environment or code. Build reached compilation and failed on a source error in `BoardConfig.h:102` (`issueMask` undeclared); see audit report.
- [x] Inspect current `FeaturePipeline`, `RiskManager`, `SchemaRegistry`, `EdgeImpulseAdapter`, `CsvLogger`, `ReplayEngine`, `MockCsvTelemetry`, `DataQualityService`, `CalibrationService`, serialization and tests.
- [x] Record exact implemented behavior and TODOs; do not rely on previous audit without rerun.
- [x] Record PlatformIO environments, compiler/framework versions, commands and exit codes.
- [x] Identify existing CSV schemas and whether current CsvLogger's six columns are operational telemetry rather than DSD raw dataset.
- **Baseline snapshot:** initial A00 audit recorded 71 native unit and 3 integration cases passed; later A14 validation is recorded separately in `docs/BASELINE_TEST_REPORT.md` (103 unit / 7 integration cases).

### A01 — Freeze data contracts and production/test separation [NOW]
- [x] Create a canonical host-side data dictionary for raw sample, quality sample, calibrated sample, window, feature vector, prediction, annotation, evaluation and manifest.
- [x] Define stable field names, types, null/invalid representation and ownership.
- [x] Define `data_origin`: `SYNTHETIC`, `BENCH`, `FIELD`, `REFERENCE`, `REPLAY`; use exact enum values consistently.
- [x] Define `evidence_class`: GT1–GT5 and `label_status`; never infer label evidence from data origin.
- [x] Define test-only compile guards/target so mock inference and synthetic fixtures cannot be selected by firmware production configuration.
- [x] Keep timestamps: `timestamp_ms` is monotonic from boot; `utc_time` nullable and only populated with trusted UTC.
- [x] Define identity keys: dataset_id, site_id, device_id, sensor_set_id, session_id, boot_id, sequence_no.
- **Done:** `docs/DATA_CONTRACTS.md` and `DataProvenance` define contracts; native tests verify fail-closed provenance and mock guards/build flags.

### A02 — Canonical CSV schema and writer [NOW]
Maintain `docs/CSV_DATASET_CONTRACT.md` and implement a shared bounded writer.

**Exports must be separate files/artifacts:**
1. `raw_samples.csv`: one row per acquired/replayed/simulated sample, raw values plus provenance and per-channel quality.
2. `calibrated_samples.csv`: optional, separate output; only emits physical values if a calibration artifact is approved.
3. `windows.csv`: one row per window, with start/end sequence/time, completeness, gap count, reset reason, eligibility.
4. `features.csv`: one row per window, exact feature IDs/indexes/schema hash and values; only schema-approved features.
5. `annotations.csv`: independently authored labels, evidence class, interval, annotator/reviewer, uncertainty.
6. `evaluation.csv`: prediction versus eligible target, model/schema/config hashes and metric inputs.
7. `dataset_manifest.json`: schema versions, IDs, origin, generator/replay version, seeds, source hashes, split policy, feature schema hash and counts.
8. `quality_report.csv` or JSON: accepted/rejected counts and reason breakdown.

Raw sample columns must follow the DSD v1.2 core ordering. Preserve the exact DSD fields; any additional fields must be appended under a new schema version or placed in a sidecar manifest. A channel absent from a particular sensor set is represented by validity=false/quality=MISSING or an explicit sensor/channel-presence field, never numeric zero.

Writer requirements:
- [x] UTF-8, comma, quote, LF, decimal dot.
- [x] RFC-style field escaping: quote fields containing comma, quote, CR or LF; double embedded quotes.
- [x] Bounded output; detect row overflow and return explicit error.
- [x] Invalid numeric → empty field plus validity/quality field.
- [x] Reject NaN/Inf and non-finite derived values.
- [x] Stable DSD raw-core header and exact column count; artifact schema versions/hashes remain manifest-owned and approval-gated.
- [x] No locale-dependent decimal separator (`std::to_chars`).
- [x] No silent truncation; row failures return explicit status.
- [x] Native tests cover escaping, UTF-8 rejection, numeric valid/invalid/boundary values, count mismatch, capacity and exact DSD core header.
- [x] Add CSV golden header fixture and SHA-256 manifest.
- [x] Keep parser/replay implementation and malformed-input tests in A04, not in the shared writer.
- **Partial:** `CsvRowWriter`, exact raw-core header, and the bounded host-only DSD raw-core exporter are implemented and tested. Other artifacts (calibrated samples, windows, features, annotations, row-level evaluation) do not yet have general-purpose exporters.

### A03 — Deterministic synthetic sensor source [NOW]
Implement a host-only simulator. It must not pretend to be actual physical sensor data.

- [x] Provide fixed seed and reproducible output.
- [x] Define scenario profiles as parameterized mathematical generators, not hand-coded “ground truth”.
- [x] Implement stable baseline; gradual trend; step change; oscillation; impulse/noise; saturation; dropout; stale; out-of-order; sequence gap; reboot; calibration-version change; and mixed-validity scenarios.
- [x] Record a synthetic raw-count domain and scenario parameters; do not assign physical units or sensor assumptions.
- [x] Separate generator inputs/latent scenario parameters from raw observations.
- [x] Use `data_origin=SYNTHETIC`, `evidence_class=GT5`.
- [x] Generate no landslide occurrence label, risk target, or proxy target.
- [x] No hidden scenario rule is used as an independent label shortcut.
- [x] Add deterministic output, overflow/configuration, and scenario behavior tests.
- **Done:** same seed and generator version yield repeatable observations/latent state; all outputs are SYNTHETIC/GT5 and field-claim-ineligible. See `docs/SYNTHETIC_DATA_POLICY.md`.

### A04 — CSV replay and dataset validation [NOW]
- [x] Parse raw_samples.csv using a bounded host parser and exact DSD-core header plus project artifact schema version.
- [x] Reject wrong column count, malformed quote, duplicate identity, invalid UTF-8/numeric token, truncated/overlong row, invalid channel quality/presence, and non-monotonic sequence/time where required.
- [x] Preserve rejected row number/byte offset/reason in caller-provided bounded quarantine references; do not coerce.
- [x] Validate per-channel validity/quality consistency and stream continuity across device/session/boot identities.
- [x] Check typed manifest dataset ID/schema/provenance, exact byte count/hash, and row count. JSON manifest parsing is not implemented.
- [x] Expose accepted/rejected totals and reason histogram in the in-memory validation report; per-channel missingness and report-file serialization remain deferred.
- [x] Repeated validation of identical input/configuration yields deterministic status, counts, and digest; A14 adds deterministic raw-only export/validate/parse/replay composition.
- **Done with limits:** bounded host raw-sample validation and A14 raw-only composition are tested. JSON-manifest parsing, general artifact exporters, and eligible feature-to-model evaluation are not implemented.

### A05 — Quality propagation and window eligibility [NOW]
- [x] DataQualityService remains sole owner of channel quality classification.
- [x] Propagate channel quality and eligibility from processed sample → window → feature vector.
- [ ] Prediction/evaluation API propagation (API expansion not approved; contract is documented only).
- [x] Reject invalid required channels and reset windows on out-of-order input, sequence gap, boot/time discontinuity, or calibration change.
- [x] Do not hold-last, interpolate, or replace with zero.
- [x] Preserve quality/rejection details in window/feature summaries; raw dataset quarantine is owned by A04.
- [x] Test quality-state and continuity rejection paths.
- **Done for approved API scope:** invalid required input cannot enter a completed feature window as valid. Prediction/evaluation APIs remain deferred.

### A06 — Calibration raw-only mode [NOW]
- [ ] Make raw-only mode explicit and testable.
- [ ] Preserve raw count/value and source status.
- [ ] Physical columns absent unless approved calibration metadata exists.
- [ ] Reject mismatched calibration sensor/version/hash.
- [ ] Expose calibration state to downstream quality/window eligibility.
- [ ] Do not convert MPU raw counts to g, soil raw to VWC, YL-83 to rainfall or HX710B to pressure/stress without verified scale/calibration.
- **Done:** simulator/replay work without inventing physical values.

### A07 — Feature windows and FSS gating [NOW]
- [ ] Implement/verify fixed-capacity window builder with explicit count, stride, sequence continuity and reset reason.
- [ ] No window across boot ID, sequence gap, calibration change, config change, schema change or sensor restart.
- [ ] `FeatureVector::kMaxFeatures` is storage capacity only.
- [ ] Only features explicitly approved in FSS may be marked model-eligible.
- [ ] Candidate `accel_magnitude`, `tilt_x_deg`, `tilt_y_deg` remain conditional; raw soil/YL-83/HX710B remain diagnostic until approved.
- [ ] If model input is raw time-series, do not force an engineered vector. Wait for actual Edge Impulse export metadata.
- [ ] Add golden synthetic fixtures and per-feature tolerance tests.
- **Done:** deterministic feature windows with schema identity; production model gate still closed without actual model contract.

### A08 — Labels and export separation [NOW]
- [x] Implement annotations as separate records, not copied into raw sample rows as if directly measured.
- [x] Provide target-free working statuses and gated labeled values; approved target contract is still absent and must be supplied before assigning target values.
- [x] Store evidence class GT1–GT5, evidence reference, annotator/reviewer, confidence/uncertainty, label method version and target alignment.
- [x] For synthetic data, require GT5 and explicitly proxy/simulation-derived annotation kind.
- [x] Never infer label from risk policy thresholds and claim independent ground truth.
- [x] Join labels to windows by explicit half-open interval and dataset/site/session/boot identity; do not use timestamp-only join.
- [x] **Done:** no unlabeled or GT5 record silently becomes field-ground-truth training data. See `docs/ANNOTATION_CONTRACT.md`.

### A09 — Group-aware dataset split and leakage checks [NOW]
- [x] Preserve one caller-supplied independent source-group ID across every window from that group and assign all such windows together.
- [x] Reject overlapping sequence windows assigned to distinct groups in the same dataset/site/session/boot stream.
- [x] Report synthetic and field origins separately.
- [x] Store split_group_id, split_name, split_policy_version and seed in each assignment row.
- [x] Test deterministic group assignments, duplicate identities, cross-group overlap leakage and bounded-capacity failures.
- [x] Report synthetic-GT5-only test groups separately from field GT1–GT4 evidence; synthetic-only test is not a field test.
- **Done:** automated leakage audit passes. Caller must supply the correct independent group IDs and explicit basis-point ratios; see `docs/DATASET_SPLIT_POLICY.md`.

### A10 — Inference interface and mock engine [NOW]
- [x] Define `IInferenceEngine` metadata, input and output contracts, including schema identity, dimension, output count and verification state.
- [x] Add deterministic caller-configured `MockInferenceEngine` only when both `PROJECT_HOST_DATA_TOOLS=1` and `PROJECT_TEST_ONLY=1`; firmware cannot include it.
- [x] Validate exact input schema/mask/finiteness and successful output status/count/finiteness before publishing inference.
- [x] Do not invent regression output range, target unit or probability semantics.
- [x] Keep `ModelOutput::rawOutputs` separate from `RiskAssessment`.
- **Done:** host integration exercises configured raw output, schema rejection, invalid configuration, injected failure, and Edge Impulse fail-closed behavior. Actual generated model integration remains blocked.

### A11 — Evaluation runner [NOW]
- [x] Implement regression metrics only when an approved target contract and explicit regression-output approval are supplied, with accepted joined labels.
- [x] Report synthetic proxy metrics in a distinct `SYNTHETIC_PROXY` section; never call them field or landslide accuracy.
- [x] Include MAE/RMSE/R² where defined, label/pair counts, target range, origin/evidence/split, exclusions and feature/quality distributions.
- [x] With undefined target semantics, report quality and feature statistics only; metric fields remain empty.
- [x] Provide aggregate per-origin/evidence/split and quality-state/rejection summaries.
- [ ] Row-level residual inspection export remains deferred; `evaluation.csv` stays reserved for that artifact.
- **Done for aggregate host reports:** synthetic proxy output cannot be conflated with independent field evidence. See `docs/EVALUATION_CONTRACT.md`.

### A12 — Edge Impulse compatibility gate [WAIT/MODEL]
- [x] Search repository for generated deployment artifacts; none are present.
- [x] Keep adapter interface and metadata checks host-testable; no production integration without generated export.
- [x] Record exact deployment files, metadata, approvals and golden-vector evidence required in `docs/EDGE_IMPULSE_ADAPTER.md`.
- [ ] Compare actual export metadata to approved FSS/hash.
- [ ] Add target golden vectors and resource measurements only after export arrives.
**BLOCKED:** generated deployment package and approved model/FSS/target evidence have not been supplied.

### A13 — RiskManager isolation [NOW+APPROVAL]
- [x] Gate model score mapping on explicit target/output-semantics approval and a single finite raw output consistent with the raw prediction.
- [x] Ensure invalid prediction/data quality cannot map to NORMAL/SAFE.
- [x] Default/semantics-unapproved model risk policy remains NotConfigured; no operational threshold approval has been added.
- [x] Test unapproved score semantics, mismatched/nonfinite raw output, stale quality, model error/version mismatch, and missing model schema provenance.
- [ ] Add a concrete mismatched-schema inference fixture before any generated production model is supplied.
**Partial:** model-score mapping now fails closed without the explicit semantics gate; no production thresholds or target semantics are approved.

### A14 — End-to-end native integration [NOW]
- [x] Test synthetic source → quality → raw-only calibration → rejected feature window → evaluation summary export; GT5/unlabeled annotation identity joins are checked.
- [x] Export bounded DSD v1.2 raw-core CSV with shared quoting, exact header/order, SHA-256/count manifest, then validate and replay it through the raw-only quality/calibration path and compare outputs.
- [ ] Run mock inference on features from this path only after approved physical calibration, eligible FSS features, and compatible model metadata exist.
- [x] Inject missing/stale channel, sequence gap/boot change, malformed row, calibration mismatch, schema mismatch and nonfinite mock output across integration and contract suites.
- [x] Assert synthetic origin/GT5 survives the generator manifest and evaluation report; target metrics remain disabled.
- [x] Assert production schema, Edge Impulse inference, and risk gates remain closed.
- [ ] Add a JSON manifest/sidecar persistence format and preserve per-channel timing detail if stale-age replay is required; the DSD core has no channel acquisition timestamp.
**Partial:** deterministic host export/replay proves raw data-quality/calibration failure behavior. Full feature→model→annotation-target evaluation remains blocked by absent approved physical calibration/FSS/model target semantics; the exporter does not fabricate those contracts.

### A15 — Documentation and release status [DOC]
- [x] Update README with host-first workflow, separate operational vs DSD replay surfaces, artifact status, and Windows native/target commands.
- [x] Maintain `docs/CSV_DATASET_CONTRACT.md`, `docs/SYNTHETIC_DATA_POLICY.md`; add `docs/AI_PIPELINE_HOST_TESTING.md` with current commands, versions, results and limits.
- [x] Keep controlled `Document/*` sources unchanged; no requirement/version approval is inferred.
- [x] Sync this backlog to A12-A14 code/test evidence; no prompt wording or controlled requirements were changed.
- [x] Keep release disposition NOT READY while hardware, calibration, actual model, target and safety evidence are absent.
- **Done:** documentation and A12-A14 anti-overlap review completed; review found no issues in the requested categories. Final native runs passed (103 unit / 7 integration), `git diff --check` passed, and controlled `Document/*` stayed unchanged. See `docs/AI_PIPELINE_HOST_TESTING.md` for evidence limits.

## 6. CSV file design — recommended artifact set

| File | Grain | Purpose | Training eligibility |
|---|---|---|---|
| raw_samples.csv | One row/sample | Immutable acquisition/replay/simulation raw data + quality/provenance | Not directly assumed; filtered/windowed later |
| calibrated_samples.csv | One row/sample | Physical values with calibration ID/version | Only if calibration approved |
| windows.csv | One row/window | Window continuity, boundaries, quality and eligibility | Metadata/control |
| features.csv | One row/window | Exact model feature IDs/index/order/schema hash | Only if schema-approved and label join valid |
| annotations.csv | One row/label interval | Independent target and evidence | Depends on label/evidence policy |
| evaluation.csv | One row/prediction-window | Model output vs eligible target | Evaluation only |
| quality_report.json/csv | One report/dataset or reason row | Validation/rejection/missingness summary | Never a training feature |
| dataset_manifest.json | One per dataset release | Provenance, schema, generator, hashes, split and counts | Metadata |

**Important:** Keep the current six-column `timestamp,tilt,soil,rain,stress,risk` logger as an operational/diagnostic format if existing consumers depend on it. Do not silently repurpose it as the raw training dataset. The bounded host-only DSD raw-core exporter is separate; its manifest is typed in-memory, not persisted as JSON.

## 7. Definition of Done

- [ ] Exact task scope and allowed files respected.
- [ ] Deterministic tests for valid, invalid, boundary and failure paths.
- [ ] No guessed hardware/model/physical unit/label/risk parameters.
- [ ] Synthetic provenance and GT5 evidence class survive the full pipeline.
- [ ] No invalid-to-zero, no hidden imputation, no raw-struct serialization.
- [ ] CSV conforms to DSD quoting, LF, decimal, invalid numeric and schema-version rules.
- [ ] Dataset split prevents session/event/window leakage.
- [ ] Native tests and ESP32 target compile reported separately.
- [ ] Documentation and TODO updated with evidence.
- [ ] Field safety/model accuracy claims remain blocked until independent evidence exists.

## 8. Release disposition

<badge>HOST AI DEVELOPMENT: CAN PROCEED</badge>

<badge>FIELD MODEL / HARDWARE VALIDATION: BLOCKED</badge>

A deterministic synthetic pipeline can validate software behavior, data handling, feature calculations and experiment workflow. It cannot validate sensor calibration, physical target validity, landslide prediction accuracy or field safety.
