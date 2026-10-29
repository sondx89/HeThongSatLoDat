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

The vector uses the explicitly approved flat layout
`sample[0].feature[0..N], sample[1].feature[0..N], ...`. The product of sample
count and feature count must fit `FeatureVector::kMaxFeatures`. A completed
vector carries the schema version/ID/hash, boot ID, sequence and time range,
window sample count, and a valid bit for every populated value.

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
