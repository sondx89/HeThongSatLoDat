# CSV Dataset Contract v1.1 — ESP32-S3 Landslide AI

**Status:** Host writer/schema contract aligned to supplied DSD v1.2. The
controlled DSD remains DRAFT; this document does not approve it. The bounded
row writer, raw-core header, host-only bounded parser, raw-sample validator,
bounded raw-core exporter, bounded v1 JSON manifest serializer/parser, bounded
window, feature, and calibrated-sample exporters are implemented. Host
evaluation summary and feature-distribution reports are implemented;
annotation exporters and row-level evaluation/residual export remain
unimplemented. Physical calibration remains unconfigured, so calibrated
production values are not available. The raw-only replay composition is
host-test-only and does not produce model-eligible features.
**Date:** 2026-11-02

## 1. Purpose and separation

Do not use the existing operational six-column stream (`timestamp,tilt,soil,rain,stress,risk`) as the canonical training dataset. Preserve it for existing operational/diagnostic consumers. Add a separate versioned dataset exporter.

The dataset pipeline emits distinct artifacts:
- `raw_samples.csv`
- `calibrated_samples.csv` (only when approved calibration exists)
- `windows.csv`
- `features.csv`
- `annotations.csv`
- `evaluation.csv`
- `evaluation_summary.csv`
- `feature_distributions.csv`
- `quality_report.json` or `.csv`
- `dataset_manifest.json`

Each artifact has its own schema version and exact hash in the manifest. A
field, order, type, meaning, or encoding change requires a schema version bump
and new hash. The DSD document version is not an artifact schema version.

## 2. Raw sample CSV

The DSD v1.2 core field order is:

```csv
dataset_id,site_id,device_id,sensor_set_id,session_id,boot_id,sequence_no,timestamp_ms,utc_time,time_status,firmware_version,config_version,calibration_id,sensor_status_bits,accel_x_raw,accel_y_raw,accel_z_raw,soil_raw,yl83_raw,hx710b_raw,accel_x_valid,accel_y_valid,accel_z_valid,soil_valid,yl83_valid,hx710b_valid,accel_x_quality,accel_y_quality,accel_z_quality,soil_quality,yl83_quality,hx710b_quality,valid_mask,quality_flags,event_id,annotation_id
```

This is the exact 36-field DSD v1.2 proposed raw-core order, not a statement
that every channel is physically installed or that the draft schema is
approved. A channel not present in a sensor set must be represented as absent
and invalid with its quality state; never fill it with zero. The machine
header is `project::dataset::kDsdV1_2RawSampleCoreFields`; its generated
header includes one LF terminator and no extra columns.

### Field semantics

- `timestamp_ms`: monotonic milliseconds since boot; not UTC.
- `utc_time`: nullable; populated only if trusted time synchronization exists.
- `time_status`: use the controlled dataset vocabulary once approved; do not
  infer UTC from monotonic time or local time.
- `boot_id`, `sequence_no`: preserve sample continuity and detect reboot/gaps.
- `calibration_id`: `NONE`/empty or exact approved artifact identifier; never a guessed ID.
- `*_raw`: direct driver representation, with native raw unit/count documented in manifest. Do not overwrite with calibrated values.
- `*_valid`: boolean validity.
- `*_quality`: stable quality code, including MISSING/STALE/SENSOR_ERROR/OUT_OF_ORDER/CALIBRATION_INVALID/NON_FINITE/GAP_DETECTED/NOT_READY as appropriate.
- `valid_mask`, `quality_flags`: bit definitions must be versioned and match firmware contract.
- `event_id`, `annotation_id`: optional references; labels remain in separate annotation artifact.

`data_origin` and `evidence_class` are deliberately not inserted into this
core header. Uniform provenance belongs in the manifest. If rows have
different provenance, record it in a sidecar keyed by the raw identity or add
columns only under a separately versioned schema. Synthetic samples must be
traceable as `SYNTHETIC`/`GT5`; the data dictionary defines and validates this
provenance pair.

`DsdRawSampleCsvExporter` writes this exact core into a caller-owned bounded
buffer through the shared `CsvRowWriter`. It rejects invalid provenance,
channel-presence/validity/quality mismatches and capacity exhaustion without
truncating a row. Its completed `RawDatasetManifest` carries uniform
provenance, channel-presence contract, exact byte/row counts and SHA-256 of
the emitted bytes. Pass the exported buffer and manifest to
`DatasetValidator` before replay. The API is host-only and does not assign
physical units, derive quality, or create labels.

### dataset_manifest.json v1

`DatasetManifestDocument` persists the typed raw manifest plus the metadata
needed to identify and qualify `raw_samples.csv`. Its JSON
`manifest_schema_version` is project-local version `1`, independent of the
`dsd_document_version` (`1.2`) and the raw artifact schema version (`1`).
`artifact_schema_sha256` is SHA-256 over the exact generated 36-column header
bytes, including the terminating LF. `artifact_sha256` is the SHA-256 over the exact CSV bytes, including header and record terminators; it
is not computed from parsed or normalized rows.

The v1 serializer emits a canonical compact key order and lowercase hexadecimal
SHA-256 strings, UTF-8 strings, unsigned decimal integers, fixed channel-
presence and rejection-count arrays, up to eight source IDs, and one trailing
LF. It records firmware/config/calibration/FSS/model IDs; generator version and
seed or replay-source hash; creation time and tool version; accepted/rejected
rows and per-reason counts; split policy; target-contract ID/state; limitations
and approval state. Values that are not known or do not apply must be supplied
explicitly as `UNKNOWN`, `UNDEFINED`, or `NOT_APPLICABLE` where the field
contract permits; empty/missing required values are rejected. Synthetic
provenance requires `GT5`, a generator version and seed, and no replay hash.
Replay provenance requires a valid replay-source SHA-256 and marks generator
metadata not applicable. A manifest does not approve any referenced ID or
contract. Do not serialize secrets.

Serialization writes to a caller-owned bounded buffer. Parsing accepts at
most `kMaxDatasetManifestJsonBytes` (8192) and uses caller-owned string
storage; all parsed `string_view` fields remain valid only while that storage
remains alive and unchanged. It permits up to
`kMaxDatasetManifestSourceIds` (8) source IDs. The serializer reports the
exact JSON byte count, including the final LF; the schema-hash helper writes
exactly 64 lowercase hexadecimal bytes without a NUL terminator. The parser
rejects malformed JSON,
duplicate/unknown/missing fields, unsupported versions, invalid UTF-8,
invalid provenance, and inconsistent counts. Call
`DatasetValidator::validateRawSamples` on the original artifact bytes, then
`validateDatasetManifestResult` with its report to verify the exact artifact
digest, byte/row counts, and accepted/rejected reason counts.

Backward compatibility is strict: v1 readers accept only the complete v1
field set and reject unknown fields or other versions. Any field or semantic
change requires a new manifest schema version; no missing field receives a
silent default. The existing typed `RawDatasetManifest` API remains available.

The raw core has no per-channel acquisition timestamp. Replay can preserve
the archived quality token but cannot reconstruct stale age for an individual
channel from this artifact alone; retain required timing detail in an
approved, versioned sidecar rather than inferring it from `timestamp_ms`.

Any additional columns beyond the DSD core list require a new schema version
or a sidecar; do not insert columns in the middle.

## 3. CSV encoding and numeric rules

- UTF-8.
- Delimiter: comma.
- Quote: double quote.
- Record terminator: LF.
- Decimal separator: `.` independent of host locale.
- Quote a field containing comma, quote, CR or LF; double every embedded quote.
- Invalid numeric: empty cell plus its separate validity/quality field.
  `CsvRowWriter::appendOptionalNumber(value, false)` emits an empty field.
  Non-finite values marked valid are rejected; never emit `NaN`, `Inf`,
  `Infinity`, or a valid zero substitute.
- A complete row must have exactly the schema's column count.
- No silent truncation; overflow returns an explicit writer status. The
  caller must surface it; the writer does not own logging/counters.
- The host-only parser/validator rejects malformed quoting, wrong column
  count, invalid UTF-8, duplicate identity keys, invalid numeric tokens,
  sequence/time transitions, channel-presence mismatches, and manifest
  hash/byte/row-count mismatches. Parser bounds are explicit; no row is
  truncated or coerced.
- Preserve rejected source rows in quarantine/report where safe; do not mutate the raw source.

## 4. Artifact-specific grain

### raw_samples.csv
One row per sample acquisition or replayed sample. Preserve raw values and
sample identity. Provenance is immutable and is recorded in the manifest for
uniform-origin files or in a keyed sidecar for mixed-origin files. Synthetic
samples require `data_origin=SYNTHETIC`, `evidence_class=GT5`, and explicit
generator ID/version/seed; GT5 does not establish independent ground truth.

### calibrated_samples.csv
One row per sample. Contains physical values only when calibration is approved. Every transformed channel references `calibration_id`, transform version and unit. Raw values remain in raw_samples.csv.

### windows.csv
`windows.csv` v1 has one row per identified candidate window. Its exact
ordered header is:

```csv
dataset_id,window_id,site_id,device_id,sensor_set_id,session_id,start_boot_id,end_boot_id,start_sequence,end_sequence,start_timestamp_ms,end_timestamp_ms,window_length_samples,stride_samples,expected_samples,observed_samples,gap_count,start_config_id,end_config_id,start_calibration_id,end_calibration_id,start_feature_schema_id,end_feature_schema_id,start_feature_schema_version,end_feature_schema_version,start_feature_schema_sha256,end_feature_schema_sha256,missing_required_channel_count,stale_required_channel_count,calibration_invalid_required_channel_count,data_origin,evidence_class,feature_contract_approved,calibration_approved,window_eligible,window_rejection_reason,training_eligible,training_rejection_reason,validation_eligible,validation_rejection_reason,test_eligible,test_rejection_reason,replay_eligible,replay_rejection_reason,safety_analysis_eligible,safety_analysis_rejection_reason,annotation_refs,split_group_id,split_name,split_policy_version
```

`window_length_samples` is the configured window length; `expected_samples`
must match it. `observed_samples` records actual samples, and `gap_count`
records known sequence gaps. The three quality counts refer to required
channels only. Every false eligibility field has a non-empty reason. One
identified but invalid candidate may be retained for audit, but cannot be
eligible for any use. Candidates without dataset/window/site/device/sensor-
set/session identity or usable start/end coordinates are rejected and not
written.

The exporter rejects eligibility when sequence order/gap/completeness fails,
timestamps are reversed, or start/end boot, config, calibration, or feature-
schema identities differ; it writes the candidate with a specific rejection
reason and all purpose eligibility false. Required-channel missing, stale,
or calibration-invalid counts likewise fail the common window gate.
Training/validation/test eligibility additionally requires explicit
`feature_contract_approved` and `calibration_approved` input; CSV parse
success alone never grants either approval. Replay and safety-analysis
decisions are supplied by the caller and still require an eligible window.
No sample rate or time tolerance is inferred. Unknown IDs/hashes must be
explicitly represented as `UNKNOWN` or `NOT_APPLICABLE`; an approved feature
contract requires a known schema ID, non-zero version, and SHA-256.

Schema version `1` is project-local and distinct from the DSD document
version and raw artifact schema. The exporter uses the shared bounded
`CsvArtifactWriter`, so its completion report (header+rows byte count, data
row count, exact-byte SHA-256) is available only after successful finalization.

### features.csv
Schema v1 writes one row per `FeatureVector`, with window identity/bounds,
feature-schema ID/version/hash, model-input kind, explicit approval/metadata/
compatibility flags, an export kind, eligibility state/reason, and each
feature's schema order, stable ID, name, unit, transform ID/version,
validity, quality, and value. The bounded host-only exporter uses
`CsvArtifactWriter` and `CsvRowWriter`; its completion report covers the exact
header and row bytes.

The caller supplies the ordered descriptors from its registered feature
schema and the approval/verification results. The exporter checks descriptor
order, vector dimension, schema ID/version/hash, validity-mask bounds, quality
state, and finite values. An invalid feature is represented by an empty value
and its explicit validity/quality fields, never by zero. A missing/unapproved
feature contract, unverified model metadata, incompatible schema, incomplete
feature metadata, ineligible window, or invalid feature set yields
`CANDIDATE_ONLY` with a reason; only when all gates pass is the row marked
`PRODUCTION_MODEL_INPUT`. These caller-provided gates do not establish that a
real FSS or Edge Impulse export has been approved. The actual Edge Impulse
export determines whether model input is raw time-series or engineered
features; this exporter infers neither feature semantics nor physical
conversions. Schema v1 is project-local and distinct from DSD and raw artifact
versions.

### annotations.csv
One row per label/annotation interval. Keep independent from raw sensor measurements. Include annotation ID, dataset/site/session/boot, interval, target name/value/unit, label status, evidence class GT1-GT5, evidence reference, annotator/reviewer, confidence/uncertainty and label method version. Do not fabricate target values.

### calibrated_samples.csv
Schema v1 writes one row per supplied sensor/channel sample. Raw value/quality
and calibrated value/quality have separate columns. The row carries sample
identity, calibration ID/version/SHA-256, calibration sensor/channel, unit,
declared valid range, approval/evidence fields, transform ID/version/status,
test-only marker, and rejection reason. CSV output uses the shared bounded
`CsvArtifactWriter` and `CsvRowWriter`.

In `RAW_ONLY`, the raw value is emitted only when its quality is `VALID`;
calibrated value and unit are empty, calibration identity/hash are `UNKNOWN`,
version is zero, and status/reason explicitly say calibration is not
configured. No identity calibration or conversion is inferred.

The configured-artifact path accepts a caller-supplied result only when the
descriptor has non-empty identity, version, lowercase SHA-256, sensor/channel,
unit, finite ordered range, transform identity/version, accepted approval and
evidence. Each row must match calibration ID/version/hash and sensor/channel.
Only finite `VALID` calibrated values within the declared range are written;
non-finite and out-of-range values are blanked with corresponding quality and
rejection state. The exporter does not apply or verify a conversion itself;
the caller must supply a value produced by the matching approved transform.
GT5 can be used only with an explicit test-only calibration fixture and is
never accepted as non-test calibration evidence. A `test_only` fixture is
marked `TEST_ONLY_CALIBRATION` / `APPLIED_TEST_ONLY`; it is not production or
field calibration evidence. Schema v1 is project-local; no controlled DSD
schema or physical conversion is approved by this exporter.

### evaluation.csv
Reserved for one row per prediction-window comparison, including prediction/
model/schema/config IDs, raw regression output, target/label reference,
eligibility, and exclusion reason. Row-level evaluation and residual export
are not implemented. A regression output is not automatically a landslide
probability.

The host `evaluation_summary.csv` and `feature_distributions.csv` are separate
aggregate report artifacts, not replacements for the DSD raw schema or
row-level `evaluation.csv`. Regression metrics remain empty unless an approved
target contract and explicit regression-output approval are supplied. GT5
synthetic-proxy metrics use a separate `SYNTHETIC_PROXY` scope and cannot be
reported as field accuracy. See `EVALUATION_CONTRACT.md`.

## 5. Dataset manifest

At minimum:
- dataset_id and dataset_schema_version;
- raw/window/feature/annotation schema versions and hashes;
- data_origin and evidence_class distribution;
- site/device/sensor_set/session IDs;
- firmware/config/calibration/FSS/model IDs and hashes where applicable;
- generator name/version/seed or replay source file hash;
- creation timestamp and tool version;
- sample/window/feature/annotation counts;
- accepted/rejected counts and rejection reason counts;
- split policy version, split seed and group key;
- target contract ID/version or `UNDEFINED`;
- limitations and approval state.

Do not put secrets or credentials in manifest.

## 6. Eligibility model

Keep separate booleans/states:
- eligible_for_training
- eligible_for_validation
- eligible_for_test
- eligible_for_replay
- eligible_for_safety_analysis

Every false state must have an eligibility reason. `GT5` synthetic/proxy labels must be reported separately and cannot support field-accuracy claims. Do not assume GT5 is eligible for real-world validation/test.

## 7. Leakage-safe split

Split by independent source group (site/session/physical experiment/event) before deriving overlapping windows, or guarantee every overlapping window from one group remains in one split. Never randomly split windows from one session across train/validation/test. Store `split_group_id`, `split_name`, `split_policy_version` and seed.

Synthetic and field origins must be separately reported. A synthetic-only test split is not a field test.

## 8. Shared writer and parser design

Use one bounded writer for all dataset CSV escaping and numeric formatting.
`CsvLogger` continues to own operational telemetry file lifecycle and its
six-column contract; it does not own DSD dataset semantics. A future
`DatasetExporter` maps domain records to artifact schemas and delegates field
escaping/number formatting to the shared writer. The host-only
`DsdRawSampleCsvParser` and `DatasetValidator` own strict parsing and raw
dataset checks; neither changes the operational replay format.

`CsvArtifactWriter` is the host-only bounded artifact lifecycle coordinator.
Its schema descriptor supplies the artifact name, artifact schema version,
and ordered column names; the schema version is independent of any source
document version. It writes the header and stages each complete row with
`CsvRowWriter`, then commits complete row bytes to a caller-owned output
buffer while calculating byte count, data-row count, and SHA-256. The caller
supplies a separate bounded row buffer; it must not overlap the output buffer.
The schema column array and strings must remain valid through `begin()`, and
the artifact-name storage must remain valid while its report is used.
A failed row or capacity/hash error is terminal, does not commit a partial
row, and cannot produce a complete report. Consumers must publish/use an
artifact only after `finish()` succeeds and returns a report marked complete.

Implemented API:
- `CsvRowWriter`: caller-owned bounded buffer; append UTF-8 fields, booleans,
  integers, finite numbers, or empty invalid numeric; enforce exact field
  count and append one LF terminator.
- `CsvArtifactWriter`: caller-owned output and row buffers; emits a schema
  header, atomically appends complete rows, and returns schema version,
  counts, and digest only after successful finalization. The raw-core exporter
  uses this coordinator without changing its public API or schema.
- `CsvSchemas.h`: exact DSD v1.2 raw-core field order/header only. It does not
  assign an artifact schema version/hash or approve the DSD.
- `DataProvenance`: origin/evidence pair with `SYNTHETIC` restricted to `GT5`.
- Raw artifact schema version `1` is project-local and is not the DSD document
  version `v1.2`. `DatasetManifestDocument` has a separate versioned JSON
  representation for raw artifact identity and provenance; it does not write
  a quality-report file.
- The parser bounds each raw record at 4096 bytes. Validation uses caller-owned
  fixed-capacity identity and quarantine workspaces; capacity exhaustion is an
  explicit status, not a dropped row.

The writer uses `std::to_chars` for locale-independent numeric output; no
allocation, locale mutation, storage operation, or implicit quality
classification occurs. Field string views must refer to valid UTF-8 bytes for
the duration of the call. A row is consumable only after `finish()` returns
`Ok`; after any error `data()` returns null and no truncated row is exposed.

Not implemented here: `DatasetExporter`, calibrated/window/feature/annotation/
evaluation serializers, quality-report file output, or end-to-end replay
through quality/window/inference stages. The JSON manifest workflow applies to
the current raw-sample artifact only.

No component should independently reimplement quoting or decimal formatting.

## 9. Implemented and deferred tests

The A02 native tests cover:
- comma/quote/CR/LF escaping and invalid UTF-8 rejection;
- empty invalid numeric versus finite numeric output;
- decimal-dot and exponent formatting independent of locale;
- NaN/Inf rejection for valid numeric fields;
- exact-capacity success, one-byte-short and append overflow;
- too few/many columns;
- exact DSD raw-core header/count and its golden fixture SHA-256.

A04 host tests cover exact header, malformed quote/encoding/truncation,
wrong column count, invalid numeric, duplicate identity, sequence/time
transitions, channel presence, manifest hashes/counts, workspace failures,
record-size boundaries, and deterministic repeated validation. A05/A08/A09
own downstream quality eligibility, annotation provenance and leakage tests;
none is claimed by this writer.

T02 manifest tests cover deterministic JSON output, round-trip parsing,
required-field/version/JSON rejection, bounded-storage failures, provenance
and schema-hash validation, and exact artifact hash/byte/row/rejection-count
comparison against the validator report. Executed validation: `pio test -e
native-unit-tests` — exit 0, 104/104 test cases passed; `pio test -e
native-integration-tests` — exit 0, 7/7 test cases passed.
