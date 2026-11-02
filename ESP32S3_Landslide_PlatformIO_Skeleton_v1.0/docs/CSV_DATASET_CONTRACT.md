# CSV Dataset Contract v1.1 — ESP32-S3 Landslide AI

**Status:** Host writer/schema contract aligned to supplied DSD v1.2. The
controlled DSD remains DRAFT; this document does not approve it. The bounded
row writer, raw-core header, host-only bounded parser, raw-sample validator,
and bounded raw-core exporter are implemented. Host evaluation summary and
feature-distribution reports are implemented; calibrated/window/feature/
annotation exporters and row-level evaluation/residual export remain
unimplemented. The raw-only replay composition is host-test-only and does not
produce model-eligible features.  
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
One row per candidate/accepted window. Include dataset/session/boot, window ID, start/end sequence and timestamp, sample count, expected count, gap count, reset reason, quality summary, schema version and eligibility/reason fields. Final exact columns must be frozen before code implementation.

### features.csv
One row per window. Include window ID, schema ID/version/hash, model input kind, feature count, and features by stable feature ID/order. Do not assume candidate FSS features are approved. The actual Edge Impulse export determines whether model input is raw time-series or engineered features.

### annotations.csv
One row per label/annotation interval. Keep independent from raw sensor measurements. Include annotation ID, dataset/site/session/boot, interval, target name/value/unit, label status, evidence class GT1-GT5, evidence reference, annotator/reviewer, confidence/uncertainty and label method version. Do not fabricate target values.

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

Implemented API:
- `CsvRowWriter`: caller-owned bounded buffer; append UTF-8 fields, booleans,
  integers, finite numbers, or empty invalid numeric; enforce exact field
  count and append one LF terminator.
- `CsvSchemas.h`: exact DSD v1.2 raw-core field order/header only. It does not
  assign an artifact schema version/hash or approve the DSD.
- `DataProvenance`: origin/evidence pair with `SYNTHETIC` restricted to `GT5`.
- Raw artifact schema version `1` is project-local and is not the DSD document
  version `v1.2`. The current validator accepts a typed `RawDatasetManifest`;
  it does not parse a JSON manifest or write a quality-report file.
- The parser bounds each raw record at 4096 bytes. Validation uses caller-owned
  fixed-capacity identity and quarantine workspaces; capacity exhaustion is an
  explicit status, not a dropped row.

The writer uses `std::to_chars` for locale-independent numeric output; no
allocation, locale mutation, storage operation, or implicit quality
classification occurs. Field string views must refer to valid UTF-8 bytes for
the duration of the call. A row is consumable only after `finish()` returns
`Ok`; after any error `data()` returns null and no truncated row is exposed.

Not implemented here: `DatasetExporter`, calibrated/window/feature/annotation/
evaluation serializers, JSON manifest parsing, or end-to-end replay through
quality/window/inference stages. The parser hashes exact input bytes and the
validator checks that hash against the caller-provided typed manifest; this is
not a complete manifest-file workflow.

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
