# Three-channel soil moisture acquisition

## Board and calibration inputs

`BoardPins::adc.soilMoisture` contains three channel pin slots. All remain
unset in the default board profile; the DOCX GPIO1/2/3 values are reference
wiring only and are not enabled. The selected board's ADC-capable pins,
resolution, attenuation and electrical input range must be reviewed before
`adcConfigurationVerified` can be set.

Each present channel has its own two-point calibration profile. One point must
map to 0% and the other to 100%; ADC endpoints may be in either order. The
DOCX example endpoints (AIR=3000, WATER=1200) are not installed as defaults.
Each endpoint must be measured and stored in the applicable calibration
profile. This output is a relative calibrated percentage, not volumetric water
content (VWC).

## Cooperative sampling and validity

- Each call samples every present probe at most once and never inserts a
  blocking delay. A probe is sampled only when at least 5 ms has elapsed since
  its prior conversion. The configured sensor-task cadence determines the
  actual spacing.
- Each probe maintains its own five-conversion moving window. A probe remains
  `WarmingUp` and invalid until all five values are present. An ADC error or
  out-of-range value resets only that probe's window.
- `maximumSampleAgeMs` is required and must cover the expected sampling
  cadence and five-sample window. When a completed window ages beyond it, that
  probe becomes `Stale`, is invalidated, and must warm up again.
- Per-probe raw count, filtered count, calibrated percentage, validity,
  quality, timestamp, sample count and calibration version are retained in
  `SensorFrame::soilMoistureProbes`. An absent optional probe has
  `NotPresent` quality and is excluded from all averages.
- The aggregate is the arithmetic mean of valid probe percentages only. It is
  valid only when `validChannelCount` reaches `requiredChannelCount` and every
  explicitly required channel is valid. `SensorFrame::soilMoistureRaw` is a
  compatibility diagnostic from the first valid channel; its channel index is
  explicit. Per-probe readings are authoritative.
- Incomplete acquisition reports `Degraded`/invalid while preserving channel
  quality in the manager snapshot; it does not increment the manager's
  read-failure counter. Actual channel-level ADC errors remain visible in that
  probe's quality. A valid aggregate uses the configured calibration version.
  No invalid probe is zero-filled into the average.

## Host tests

The fake-ADC tests cover channel independence, reversed profiles, endpoints
and clamping, optional absence, required-count behavior, channel failures,
out-of-range recovery, stale invalidation, timestamps, nonblocking cadence and
calibration rejection. Run them with a C++17 compiler:

```powershell
g++ -std=c++17 -I test\test_soil_moisture_host\fakes -I include -I include\project `
  test\test_soil_moisture_host\test_soil_moisture_host.cpp `
  src\Drivers\SoilMoistureDriver.cpp -o soil_moisture_host_tests.exe
.\soil_moisture_host_tests.exe
```

These tests do not approve physical pins, ADC settings, sensor calibration or
substrate-specific interpretation.
