# DataQualityService

`DataQualityService` assesses a `SensorFrame` and fills a fixed-storage
`ProcessedFrame`. It is used by `TaskFeature` before calibration and inference.
No heap allocation or schema bit assignments are introduced.

## Quality states

Every `SensorChannel` receives one state in `ProcessedFrame::channelQuality`:

- `MISSING`: sensor status is invalid/faulted, the field is not available, or
  calibration needed to produce the field is not present.
- `STALE`: the sensor timestamp is in the future or older than its configured
  freshness limit. The stale comparison is strictly `age > maximumAgeMs`.
- `NON_FINITE`: a present, fresh floating-point value is NaN or infinity.
- `OUT_OF_RANGE`: a present, fresh, finite value is outside a configured
  inclusive range. Endpoints are accepted.
- `VALID`: the field is present, sensor status is valid, freshness passes, and
  any configured range passes.

Checks are ordered as missing, stale, finite, then range. Range and freshness
settings are not inferred from examples in the documents. The ICD labels
`stale_after_ms=5000` as a proposed value, not an approved production value.

## Configuration and usage

The integration supplies a `DataQualityConfig` with all four sensor freshness
limits (`Imu`, `Soil`, `Rain`, `Hx710b`) explicitly configured. It also selects
the approved required channels and sets `requiredChannelsConfigured`. At least
one required channel must be selected, and required channels must have a current
`ProcessedFrame` field. Optional ranges are per-channel and must have finite
ordered bounds. A default-constructed configuration is intentionally invalid;
the service fails closed until policy values are supplied.

Use the deterministic overload in tests or a composition layer with an explicit
monotonic time:

```cpp
project::models::ProcessedFrame processed{};
const auto status = quality.process(sensorFrame, nowMs, processed);
if (status != project::services::DataQualityServiceStatus::Ok ||
    !quality.validate(processed)) {
    // Do not forward invalid or incompletely configured data.
}
```

The convenience `process(input, output)` and `validate(input)` overloads obtain
monotonic milliseconds from `esp_timer` on ESP32 or `steady_clock` on host builds.
`TaskFeature` uses the convenience processing overload, and preserves per-channel
quality results through calibration so a downstream stage cannot silently
promote an invalid required channel.

## Deliberate TBD handling

- `validMask` remains unchanged; its bit mapping is not approved.
- Raw values receive quality states but are not copied into the processed feature
  payload.
- A scalar tilt value is not mapped to roll/pitch.
- Rain wetness remains a relative indicator, not rainfall rate.
- HX710B raw/factor-scaled readings are not assigned a physical unit. Its
  calibrated physical field is always `MISSING` until the transducer and
  measurand are defined.
- Sensor timestamps are evaluated independently using the status timestamp and
  the configured sensor freshness limit. The frame-level timestamp is preserved
  as metadata and is not substituted for missing sensor timestamps.
