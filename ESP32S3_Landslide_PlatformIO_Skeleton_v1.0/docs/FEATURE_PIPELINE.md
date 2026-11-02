# FeaturePipeline

`project/Services/FeaturePipeline.h` converts valid `ProcessedFrame` samples to
a fixed-size `FeatureVector`. Its candidate feature catalogue follows
`Document/FSS_Feature_Schema_Specification_ESP32S3_EdgeImpulse_v1.1.txt`.

## FSS and fail-closed behavior

The current FSS is DRAFT: all feature indexes are null, every candidate has
`model_input_eligible=false`, and the actual Edge Impulse input shape, window,
stride, preprocessing, schema hash, and golden vectors are not released. The
JSON artifact's schema-document version and FSS document version are not treated
as a released runtime schema version. Accordingly, default construction is
unconfigured, `push()` rejects frames, and `build()` never emits a valid vector.

An integration may activate the pipeline only with a reviewed
`FeaturePipelineConfig` supplying:

- nonzero released `schemaVersion` and `schemaId`;
- the SHA-256 bytes of the exact approved schema artifact;
- a contiguous ordered list of approved features with `modelInputEligible`;
- verified Edge Impulse metadata and an approved flat-vector layout;
- measured/approved sample rate, sample-period tolerance, window and stride
  within static capacity;
- an approved preprocessing version (including an explicitly approved
  identity/no-normalization choice, if applicable);
- the verified acceleration-to-m/s² conversion for `accel_magnitude`;
- explicit approval/evidence for the FSS candidate tilt formula.

The pipeline does not assign feature indexes, schema IDs, schema hashes,
sampling rates, window/stride values, or model dimensions. A schema change
requires a new version and matching hash as required by FSS.

## Supported sources

The current `ProcessedFrame` does not carry the FSS raw accelerator counts,
soil raw ADC value, YL-83 raw ADC value, or HX710B raw count. The pipeline
therefore rejects these feature IDs as unsupported rather than substituting
calibrated or similarly named fields.

With explicit approved configuration, it can calculate:

- `accel_magnitude`: `sqrt(ax² + ay² + az²) * accelerationToMps2`; all three
  axes must be quality-valid and finite, and the conversion scale must be
  explicitly configured.
- `tilt_x_deg`: the FSS candidate formula using `atan2(ay, sqrt(ax² + az²))`.
- `tilt_y_deg`: the FSS candidate formula using `atan2(-ax, sqrt(ay² + az²))`.

Tilt remains blocked unless `tiltFormulaApproved` is set in a reviewed schema
configuration; this flag represents the required mounting/frame and
gravity-dominance verification. No value is held, interpolated, or zero-filled.
`hx710b_raw` remains unsupported and is not included in the model vector until
the transducer, semantics, source data, and model eligibility are approved.

## Window and vector layout

The caller supplies the approved `windowLengthSamples` and `strideSamples`.
Samples must have valid aggregate status and IMU axes; a boot change, sequence
gap/out-of-order record, non-increasing timestamp, calibration change, or
timestamp delta outside the configured sampling tolerance resets the active
window. The pipeline does not repair gaps.

The pipeline pins the source `ProcessedFrame.schemaVersion` from its first
accepted sample. A later source-schema change clears the active window and is
rejected with `InvalidConfiguration`; construct a new pipeline with the
reviewed configuration for a schema transition. The pinned source version is
only a change detector, not schema approval. `FeaturePipelineConfig` is
immutable for the pipeline lifetime, so configuration changes require a new
instance and cannot reuse buffered samples.

`SensorFrame`/`ProcessedFrame` currently have no sensor-restart generation
marker. The owning task must call `resetWindow()` when a sensor restart is
reported; this clears samples and continuity history before the next accepted
sample. Boot changes and failed/invalid samples also clear the prior window.
The pipeline cannot infer a restart that is not represented by input metadata
or an explicit reset call.

The vector uses the explicitly approved flat layout
`sample[0].feature[0..N], sample[1].feature[0..N], ...`. The product of sample
count and feature count must fit `FeatureVector::kMaxFeatures`. A completed
vector carries the schema version/ID/hash, boot ID, sequence and time range,
window sample count, and a valid bit for every populated value.

## Quality and eligibility propagation

`DataQualityService` remains the sole owner of per-channel quality
classification. `FeaturePipeline` consumes those states and records them in
`WindowQualitySummary`; it does not reclassify channels or make invalid values
usable. Required input channels must be quality-valid and finite, and aggregate
frame status must be valid. A rejected required channel or invalid aggregate
clears the active sample window and records a rejection reason/channel where
available.

The pipeline uses `DataQualityService::classifyContinuity` for boot, sequence,
ordering, and timestamp boundaries. Boot changes, sequence gaps, and timestamp
discontinuities reset the prior window; a valid sample at the new boundary may
start a fresh window. Out-of-order input is rejected. Calibration changes
reset the prior window. No window bridges a detected discontinuity, and the
pipeline performs no zero-fill, hold-last, interpolation, or imputation.

`windowQuality()` reports the active window's eligibility, boundary,
per-channel quality, and rejection details. A built `FeatureVector` carries
that summary. Only sample → window → feature propagation is implemented under
the approved API scope; prediction and evaluation APIs remain deferred.

Host tests use fixed synthetic numeric fixtures to verify feature ordering,
window count/stride, reset boundaries, and formula tolerances. Those fixtures
test implementation behavior only; they do not establish physical units,
sensor calibration, model eligibility, or field accuracy.

Example configuration (placeholders must be replaced with release-approved
values):

```cpp
project::services::FeaturePipelineConfig config{};
config.schemaVersion = approvedSchemaVersion;
config.schemaId = approvedSchemaId;
config.schemaHash = approvedSchemaSha256;
config.schemaHashValid = true;
config.schemaApproved = true;
config.modelMetadataVerified = true;
config.flatLayoutApproved = true;
config.sampleRateHz = approvedSampleRateHz;
config.sampleRateConfigured = true;
config.samplePeriodToleranceMs = approvedTimestampToleranceMs;
config.samplePeriodToleranceConfigured = true;
config.preprocessingVersion = approvedPreprocessingVersion;
config.preprocessingApproved = true;
config.windowLengthSamples = approvedWindowLength;
config.strideSamples = approvedStride;
config.featureCount = approvedFeatureCount;
config.features[0] = {
    project::services::FssFeatureId::AccelMagnitude, 0, true};
config.accelerationToMps2 = verifiedScale;
config.accelerationScaleConfigured = true;

project::services::FeaturePipeline pipeline(config);
```

In normal integration, every configured feature and approval flag must come
from the same versioned release manifest; do not copy the illustrative values
from tests or drafts into production.
