# Unity unit tests

The native PlatformIO Unity environment tests the firmware logic without
ESP32 hardware. It compiles the five target implementations directly and uses
fixed-size fake sensors, a fake clock, and an in-memory CSV source.

Run from this project directory:

```text
pio test -e native-unit-tests
```

The suite covers:

- `SensorManager`: ordered polling, retry, timeout reporting, sample metadata,
  and periodic scheduling.
- `DataQualityService`: required-channel aggregation, all five quality states,
  and invalid/unconfigured policy rejection.
- `FeaturePipeline`: approved fixed-window construction, feature ordering,
  mask/metadata, and rejection of an unresolved raw feature mapping.
- `RiskManager`: approved model score handling, fail-closed quality/model
  states, and promotion/recovery hysteresis.
- `ReplayEngine`: approved CSV replay, overlapping windows, malformed input,
  invalid header, and unverified manifest rejection.

The test fixtures provide explicit approved configurations only for exercising
the configured code paths; they do not establish or approve production
thresholds, feature mappings, schemas, model semantics, or dataset manifests.
