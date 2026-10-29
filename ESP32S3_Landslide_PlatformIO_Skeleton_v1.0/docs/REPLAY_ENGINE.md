# ReplayEngine

`project/Replay/ReplayEngine.h` streams the compact CSV produced by
`CsvLogger`:

```csv
timestamp,tilt,soil,rain,stress,risk
```

It emits fixed-size `FeatureVector` windows for deterministic host/model
testing. `ICsvReplaySource` is the input adapter; it must rewind and read
bounded byte chunks. The engine uses fixed buffers and does not allocate
per row. It is single-consumer and not thread-safe; the source must outlive the
engine and must return `Ok` only when it provides at least one byte, or
`EndOfFile` with zero bytes at end of input.

## Schema and mapping gates

FSS v1.1 remains a DRAFT: feature indexes and the Edge Impulse input shape are
not released, and the six logger columns are not a canonical FSS feature list.
The six-column file also does not contain its own schema version, dataset hash,
boot ID, original sequence number, calibration ID, per-field status, or units.

Therefore replay requires externally verified dataset-manifest and dataset
SHA-256 flags, a nonzero CSV schema version, approved feature schema version/ID
and SHA-256, verified model metadata, an approved flat layout, and an explicit
approved mapping. The manifest-provided flat model input element count must
match `featureCount * windowLengthSamples`. A nonzero hash is copied to the
output; the caller/manifest loader is responsible for actually verifying the
hash and approval flags. Default configuration fails closed.

`ReplayFeatureMapping` binds one numeric CSV column (`tilt`, `soil`, `rain`, or
`stress`) to one contiguous model feature index. `timestamp` and `risk` cannot
be numeric features. The logger does not define the meaning or unit of these
columns, so the manifest must approve the interpretation. `risk` is validated
as a restricted text token and is not included in the numeric vector. The
engine does not infer a feature order from column names.

## Window and error behavior

- The timestamp must be a non-empty decimal monotonic millisecond value.
- CSV header must exactly match the six-column header; LF and CRLF line endings
  are accepted.
- Empty unmapped numeric values are allowed. A mapped empty value rejects that
  row and clears the active window. Non-finite, malformed, overflowed, quoted,
  or locale-formatted values are rejected.
- Timestamps must strictly increase at the configured sample rate within the
  approved tolerance. A discontinuity rejects the row and clears the window.
- An overlong complete row is discarded through its newline and reported as
  `InvalidRow`, so the next call begins at the next row.
- A partial final row is reported as `TruncatedRow`; it is not silently ignored.
- No zero-fill, interpolation, hold-last, risk-to-number conversion, or
  calibration is performed.

Every complete data line receives a replay-local row ordinal because the compact
CSV has no source sequence number. `FeatureVector::bootId` is the supplied
`replaySessionId`, not the original device boot ID. These identities are
replay-only and must not be presented as recovered capture metadata.
`ReplayStats::rowsRead` counts accepted rows, `rowsRejected` counts rejected
rows, and `lastRowSequence` is the latest complete-row replay ordinal.

The sample window is flattened as
`sample[0].feature[0..N], sample[1].feature[0..N], ...` only when explicitly
approved. The product of feature count and window size must fit the current
`FeatureVector::kMaxFeatures`. `next()` returns `VectorReady` for each output;
invalid rows and storage errors are returned explicitly so callers can record
them and continue or abort according to test policy.

## Integration sketch

```cpp
project::replay::ReplayConfig config{};
// Populate all fields from a verified dataset/release manifest.
project::replay::ReplayEngine replay(csvSource, config);
auto result = replay.begin();
if (result != project::replay::ReplayStatus::Ok) {
    // Fail the replay; do not run inference with an unverified schema.
}

project::models::FeatureVector input{};
for (;;) {
    result = replay.next(input);
    if (result == project::replay::ReplayStatus::VectorReady) {
        model.run(input, output);
    } else if (result == project::replay::ReplayStatus::EndOfFile) {
        break;
    } else if (result == project::replay::ReplayStatus::InvalidRow ||
               result ==
                   project::replay::ReplayStatus::TimestampDiscontinuity) {
        // Record rejected input and follow the test's explicit fault policy.
    } else {
        // Abort and report the input/schema failure.
    }
}
```

This compact CSV can replay only approved mappings of its available numeric
columns. It cannot reconstruct raw IMU axes, sensor status, original sequence
gaps, or per-sensor calibration. Use a richer dataset schema when the model
contract requires those inputs or provenance.
