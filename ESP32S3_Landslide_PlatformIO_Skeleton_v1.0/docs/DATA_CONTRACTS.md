# AI/Data Contracts v1.0

**Status:** Host-side working contract aligned to the supplied DSD v1.2,
DOP v1.0, FSS v1.1 and GTIS v1.0 inputs. Those documents remain drafts;
this dictionary does not approve their schemas or requirements.
**Date:** 2026-11-02

## 1. Common rules

- `data_origin` describes where a record came from. Its values are
  `SYNTHETIC`, `BENCH`, `FIELD`, `REFERENCE`, and `REPLAY`. Origin does not
  establish label quality.
- `evidence_class` describes the evidence supporting a label: `GT1` through
  `GT5`. It is independent of origin. Synthetic records carry `GT5`;
  synthetic/proxy labels are not real ground truth and cannot support field
  accuracy or safety claims.
- C++ `DataOrigin`, `EvidenceClass`, and `DataProvenance` are defined in
  `include/project/Dataset/DataProvenance.h`. An unspecified default is
  invalid; `isValid()` rejects unknown enum values and requires `GT5` for
  `SYNTHETIC`. Provenance is separate from `SensorFrame`; it belongs to
  dataset records, manifests, or a keyed provenance sidecar.
- `timestamp_ms` is monotonic milliseconds within a boot. It is not UTC.
  `utc_time` is nullable and is populated only from trusted UTC; a local time
  or timezone is never inferred.
- Validity is explicit. A value's initialization to zero in a C++ record does
  not make it measured or valid. Missing, stale, out-of-order, non-finite,
  calibration-invalid, or otherwise rejected data are not replaced with zero,
  a prior value, or an interpolated value.
- Numeric and schema ranges, units, sample rates, window/stride, feature and
  model dimensions, target meaning, and risk thresholds remain unset until
  approved evidence exists.
- A document version (for example DSD v1.2) is not an artifact schema
  version. Each exported artifact declares its own schema version and exact
  schema hash in the manifest. A field/order/type/meaning/encoding change
  requires a schema version change and new hash. Version values are not
  inferred from document versions.

## 2. Identity keys

| Record | Identity / join key |
|---|---|
| Raw sample | `(dataset_id, device_id, session_id, boot_id, sequence_no)` |
| Quality sample | Raw sample identity plus `quality_policy_version` |
| Calibrated sample | Raw sample identity plus calibration artifact ID/version |
| Window | `(dataset_id, session_id, boot_id, window_id)`; retain start/end sequence and monotonic time |
| Feature vector | Window identity plus feature-schema version, ID, and hash |
| Prediction | Inference identity plus source window identity and model identity/hash |
| Annotation | `annotation_id`; join to data by dataset/session/boot and explicit time interval |
| Evaluation | Evaluation identity plus prediction, annotation, split, and target-contract references |
| Manifest | `dataset_id` and immutable artifact paths, schema versions, hashes, and counts |

`site_id` and `sensor_set_id` identify collection context and hardware
configuration; neither substitutes for session/boot/sample identity. A
timestamp alone is never a join key.

## 3. Data dictionary

| Entity | Core fields and semantics | Validity / ownership |
|---|---|---|
| Raw sample | DSD v1.2 raw core fields, in its exact ordered CSV header: dataset/site/device/sensor-set/session/boot/sequence identity; `timestamp_ms`, nullable `utc_time`, time status and software/config/calibration metadata; raw IMU/soil/YL-83/HX710B channels; per-channel validity and quality; masks/flags and optional event/annotation references. Raw channel units are unspecified until supported by approved sensor evidence. | `DataQualityService` owns firmware quality classification. Dataset provenance is carried by manifest or a sidecar keyed to the raw identity; it is not inserted into the DSD core header. A later schema may append fields only with a new schema version. |
| Quality sample | Raw sample identity, channel identifier, quality state, policy version, and any source/status reference needed to explain the classification. Existing firmware states include `VALID`, `MISSING`, `STALE`, `OUT_OF_RANGE`, and `NON_FINITE`; dataset quality vocabularies remain subject to the controlled contract. | `DataQualityService` is the single quality-classification owner. Preserve rejected rows and reasons where appropriate; do not silently make them training-eligible. |
| Calibrated sample | Raw sample identity; separate calibrated value(s), unit, calibration artifact ID/hash, and transform version. Each calibrated value refers to its source channel. | `CalibrationService` owns calibration application. The current service has no approved calibration artifact and remains raw-only/fail-closed. No physical unit or conversion is implied here. |
| Window | Window ID and source dataset/session/boot; start/end sequence and monotonic timestamp; observed/expected sample counts; quality summary; reset/discontinuity reason; applicable config, calibration, and feature-schema identities; per-use eligibility and reason. | `FeaturePipeline` owns firmware feature-window construction. A window must not span an unapproved continuity or configuration boundary. Sample count, stride, and rate remain TBD. |
| Feature vector | Source window identity and bounds; feature schema version/ID/hash; actual populated `count`; ordered values and validity mask. | `FeaturePipeline` creates it; `SchemaRegistry` validates compatibility. `FeatureVector::kMaxFeatures` is storage capacity, not model dimension. Only approved schema/model metadata may establish an inference input. |
| Prediction | Inference/model identity and hash; source window and feature schema provenance; inference status/time; raw output vector and output count; optional raw prediction value with validity. | `IInferenceEngine` owns inference; prediction validation is distinct from risk mapping. A regression output is not inherently a probability, risk score, or landslide target. Mock output is test-only and cannot be production inference evidence. |
| Annotation | Independent `annotation_id`; dataset/site/session/boot; explicit interval; target name/value/unit when defined; label status; evidence class; evidence reference; annotator/reviewer; uncertainty/confidence; method and target-contract versions. | Annotation authoring is separate from raw samples and model output. Proposed statuses in DOP include `UNLABELED`, `PENDING_REVIEW`, `ACCEPTED`, `UNCERTAIN`, `CONFLICT`, `REJECTED`, and `NOT_APPLICABLE`; they are not yet a released code registry. Never derive independent ground truth from RiskManager thresholds. |
| Evaluation | Prediction and annotation references; target-contract and model/schema/config identities; split/group and origin/evidence; eligibility/exclusion reason; metric inputs and results only when their contract is approved. | Evaluation tooling owns metric calculation. No field-accuracy or safety claim follows from synthetic-only or GT5 evidence. Do not compute target metrics while target semantics are undefined. |
| Manifest | Dataset identity, per-artifact schema versions/hashes/paths/counts, origin/evidence distributions, collection/session identities, firmware/config/calibration/schema/model references, generator or replay provenance, split policy/group/seed, target-contract state, quality/exclusion counts, and limitations/approval state. | Dataset tooling owns manifests. Hashes cover exact artifact bytes. No secrets; no schema approval is inferred from a manifest entry. |

## 4. CSV and provenance relationship

The DSD v1.2 raw core header contains 36 fields and does not contain
`data_origin` or `evidence_class`. Preserve that core and its order. Record
uniform provenance in the manifest. If rows in one raw artifact have different
origins/evidence, use a keyed sidecar or append fields under an explicitly new
raw schema version; never silently insert/reorder fields. Synthetic rows must
remain traceably `SYNTHETIC`/`GT5` in that provenance record.

Operational telemetry (`timestamp,tilt,soil,rain,stress,risk`), mock serial
diagnostics, and DSD dataset exports are separate formats with separate
owners. None is a substitute for another.

## 5. Production/test boundary

`MockInferenceEngine` is available only when both
`PROJECT_HOST_DATA_TOOLS=1` and `PROJECT_TEST_ONLY=1` and returns only
explicitly configured raw outputs for tests. Mock sensor and serial fixtures
require an explicit native-mock, test-only, or mock
firmware build flag. The production PlatformIO environment does not define
these flags; including their public headers from production code is a
compile-time error. `EdgeImpulseAdapter` remains the only production
Edge Impulse integration boundary and remains unavailable without its actual
generated export and verified metadata.

This boundary prevents mocks from being selected as production inference. It
does not make synthetic observations physical data, assign them ground truth,
or approve a production model.
