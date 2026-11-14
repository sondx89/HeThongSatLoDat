# YL-83 / FC-37 rain sensing

## Board and calibration inputs

AO uses `BoardPins::adc.rainSensor`; DO uses the separate
`BoardPins::digitalSensors.rainSensor`. Both remain unset in the default board
profile. The DOCX GPIO7/GPIO10 values are wiring references only and are not
production assignments.

AO requires a verified ADC range, wetness semantics, calibration version and
two distinct measured ADC endpoints, one calibrated to 0% and the other to
100%. Calibration maps the filtered ADC count to a relative surface-wetness
proxy. The DOCX example endpoints
(dry=4095, wet=0) are not defaults. This proxy is not rainfall amount,
rainfall intensity or mm/h.

DO is optional and has a separate validity status. When enabled, its active-low
polarity must be explicitly verified in configuration and physically confirmed
before use; low means wet. AO can remain usable if DO is unavailable, and DO
can remain usable while AO is warming, stale or faulty.

## Acquisition and accumulation

- Each AO reading is the mean of exactly eight in-range ADC conversions. The
  driver performs at most one conversion per call, with at least 2 ms between
  conversions; it never blocks to insert a delay. A reading remains pending
  until the eight-conversion burst completes.
- `accumulationSamplePeriodMs` must be at least 500 ms. The rain window keeps a
  bounded history (256 timestamped points); `maximumSampleGapMs` must cover
  the sample period plus the 14 ms minimum burst span and remain below 120 s.
- Wetness is held constant from each completed AO reading until the next
  reading (zero-order hold). The driver integrates the actual elapsed
  milliseconds, clips the result to the trailing 120,000 ms, and expresses it
  as equivalent wet-seconds: `integral(wetnessPercent / 100 * dtSeconds)`.
- The accumulator is valid only when the history covers the complete trailing
  120 seconds and every observed interval, including the latest sample age, is
  no greater than `maximumSampleGapMs`. A larger gap invalidates and restarts
  the accumulation history. Missing data is never inserted as a dry/zero
  sample. The configured maximum gap defines how long the last known wetness
  may be held across irregular or missed polls.
- `rainAccCoverageMs`, `rainAccValid` and `rainAccumulationStatus` distinguish
  a warming, stale or incomplete window from a valid accumulation. AO, DO and
  accumulation have separate statuses in `SensorFrame`; `DataQualityService`
  preserves them independently in `ProcessedFrame`.

The DOCX watch/extreme values (30/90 equivalent wet-seconds) are demo
thresholds only. They are not rainfall measurements or geotechnically
validated limits. `rainRateMmH` remains unsupported and invalid.

## Host tests

The fake-ADC/digital tests cover dry/wet endpoints, active-low polarity,
independent AO/DO failures, eight-sample acquisition, irregular-interval
integration, trailing-window eviction, gap invalidation and fail-closed
configuration. From the project directory, run:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
$exe = Join-Path $env:TEMP 'rain_host_tests.exe'
g++ -std=c++17 -Wall -Wextra -Werror `
  -I test\test_rain_host\fakes -I include -I include\project `
  test\test_rain_host\test_rain_host.cpp `
  src\Drivers\RainSensorDriver.cpp -o $exe
if ($LASTEXITCODE -eq 0) { & $exe; Remove-Item -LiteralPath $exe -Force }
```

These tests do not approve GPIO assignments, ADC settings, sensor polarity,
calibration or physical rain interpretation.
