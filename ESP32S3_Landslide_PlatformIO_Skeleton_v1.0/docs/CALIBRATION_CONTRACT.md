# Calibration and raw-data contract

**Status:** The default constructor remains `RAW_ONLY`. An opt-in,
in-memory HX710B pressure profile is supported only when its transducer
identity, range, calibration version, and approval flags are explicitly
provided. No production profile is installed, and this document does not
approve production use.

## Current behavior

The default-constructed `CalibrationService::mode()` reports
`CalibrationMode::RawOnly`. Its `loadCalibrationStatus()` returns
`ApiStatus::NotInitialized`; the existing `loadCalibration()` bool facade
therefore returns `false`. A separate constructor accepts an explicitly
approved HX710B pressure profile and reports `Hx710bPressureOnly` only when
all profile checks pass. There is no configured identity calibration:
identity is a calibration choice and must not be inferred from absent data.

`CalibrationService::processStatus()` copies frame provenance and the opaque
quality and sensor-status bitfields, then classifies channel quality from the
matching `SensorStatus`. The default raw-only path does not modify the const
`SensorFrame`, apply coefficients, calculate tilt, or copy candidate
converted values into `ProcessedFrame`. The opt-in HX710B path maps only a
valid raw sample with a matching approved identity/version profile. Raw values
stay in the unchanged caller-owned `SensorFrame`; `ProcessedFrame` does not
duplicate raw storage. `validMask` remains uninterpreted until its bit mapping
is approved.

## Public method contract

| Method | Preconditions and result | Side effects / execution | Ownership and concurrency | Errors |
|---|---|---|---|---|
| `mode()` | Default instance reports `RawOnly`; an instance with a valid approved HX710B profile reports `Hx710bPressureOnly`. | No I/O, waiting, or allocation. | Reads immutable profile state. | Invalid or absent profile remains raw-only. |
| `loadCalibration()` | Default instance returns `false`; an instance constructed with a valid approved HX710B profile returns `true`. | No I/O, waiting, allocation, or state mutation. | Profile is copied into the service at construction. | See `loadCalibrationStatus()`. |
| `loadCalibrationStatus()` | Returns `NotInitialized` in raw-only mode and `Ok` for an explicitly approved HX710B profile. | No I/O, waiting, allocation, or state mutation. | `const`; concurrent calls are safe. | No persistence source or identity fallback exists. |
| `process(input, output)` | Requires live, distinct input/output objects. Returns `false` for raw-only, unsupported-version, or invalid-schema outcomes; returns `true` only for an accepted approved-profile conversion. | Resets and writes caller-owned `output`; does not modify `input`. Bounded in-memory work only. | Input is borrowed for the call; output is caller-owned. Concurrent use is safe only with distinct output objects. Task context; ISR use is not promised. | Use `processStatus()` to distinguish `InvalidArgument`, `VersionMismatch`, and `NotInitialized`. |
| `processStatus(input, output)` | Returns `InvalidArgument` for schema version zero or out-of-range pressure, `VersionMismatch` for a raw-only nonzero global version or HX710B identity/version mismatch, `NotInitialized` for unavailable data, and `Ok` only when an approved profile converts a matching valid sample. | Copies metadata, classifies source quality, and may map the HX710B pressure value; no I/O, waiting, allocation, or baseline mutation. Output is written even on non-`Ok`. | Input is borrowed; output is caller-owned. `const`; concurrent use requires distinct outputs. Task context; ISR use is not promised. | Mismatched profiles become `CALIBRATION_INVALID`; missing/faulted channels become `MISSING`; invalid pressure ranges fail closed. |

The current `TaskFeature` caller continues to use the bool facades, so its
signature and fail-closed startup/feature rejection behavior are unchanged.
Detailed status methods are available to future diagnostics without changing
that caller.

| SensorFrame source | ProcessedFrame destination/value | Channel quality | Current interpretation |
|---|---|---|---|
| `imu.accelX/Y/Z`, `imu.gyroX/Y/Z` | No raw-value destination | `Imu*Raw` | `VALID` only when `imuStatus.valid` and health is `Healthy` or `Degraded`; otherwise `MISSING`. |
| `imuPhysical.accelXG/YG/ZG` | `accelXG/YG/ZG` | `Accel*G` | Not copied. `MISSING` when the source is finite and status is usable; `NON_FINITE` for NaN/Inf. |
| `imuPhysical.gyroXDps/YDps/ZDps` | `gyroXDps/YDps/ZDps` | `Gyro*Dps` | Not copied. `MISSING` when finite and status is usable; `NON_FINITE` for NaN/Inf. |
| `tiltAngleDeg` / `tiltAngleValid` | No matching roll/pitch destination | `TiltAngleDeg` | Not used to derive roll or pitch. Invalid/not-present input is `MISSING`; NaN/Inf when valid is `NON_FINITE`. |
| `soilMoistureRaw` | No raw-value destination | `SoilMoistureRaw` | Raw quality only; no soil conversion. |
| `soilMoisturePct` | `soilMoisturePct` | `SoilMoisturePct` | Not copied or treated as calibrated by this service. Finite input remains `MISSING`; NaN/Inf is `NON_FINITE`. |
| `rainRaw` | No raw-value destination | `RainRaw` | Raw quality only; no rainfall or wetness conversion. |
| `rainWetnessRelativePct` / `rainWetnessValid` | `rainWetnessRelativePct` | `RainWetnessRelativePct` | Not copied. A finite source remains `MISSING`; NaN/Inf when marked valid is `NON_FINITE`. |
| `rainRateMmH` | `rainRateMmH` | `RainRateMmH` | Not copied or certified as rainfall rate. Finite source remains `MISSING`; NaN/Inf is `NON_FINITE`. |
| `hx710bRaw` | No raw-value destination | `Hx710bRaw` | Raw quality only; raw data remains in `SensorFrame`. |
| HX710B calibration/scaled/calibrated fields | `hx710bCalibratedValue` | `Hx710bCalibratedValue` | Default raw-only mode does not copy candidate values. The approved-profile constructor maps raw counts to kPa only for a valid sample with matching transducer identity/version and an in-range result. |

`ProcessedFrame` currently contains no raw-value fields. Therefore raw values
remain in the caller-owned, unchanged `SensorFrame`; callers must retain that
frame for raw logging/diagnostics. This service does not claim that raw values
were copied into `ProcessedFrame`.

## Validity and result rules

- A sensor channel is source-available only when its status is `valid` and
  health is `Healthy` or `Degraded`. `Unknown`, `Fault`, and invalid health
  enum values fail closed to `MISSING`.
- `DataQualityService` does not classify staleness; the HX710B baseline API
  requires its own explicitly configured freshness/gap policy. `qualityFlags`
  and `sensorStatusBits` are copied but not decoded here.
- Available floating-point sources are checked with `isfinite`. A NaN or
  infinity is `NON_FINITE`, never a valid zero.
- In default `RAW_ONLY` mode, `DataQualityState::VALID` is emitted only for
  integer raw channels. An explicitly approved HX710B profile may also mark its
  matching, finite, in-range kPa output `VALID`.
- `validMask` remains zero: its bit-to-channel mapping is explicitly TBD.
  The service copies `qualityFlags` and `sensorStatusBits` without interpreting
  their unapproved bit assignments. `aggregateStatus` remains unknown because
  no sensor-to-aggregate policy is defined.
- The default constructor returns `NotInitialized` from
  `loadCalibrationStatus()` and never activates identity calibration. An
  explicitly approved HX710B profile is the only supported calibrated path.
- A nonzero schema version is not certified as supported: no released
  `SensorFrame` schema registry is available to this service. Zero is rejected
  as missing.

## Blockers

The HX710B fields carry transducer identity and calibration version for
profile matching, but there is no general sensor identity registry or
calibration-artifact persistence/activation path. These fields do not prove
the identity of attached hardware; that still requires external verification.

## HX710B pressure profile and baseline

The pressure conversion profile is accepted only with an explicit approval
flag, verified transducer identity and range, nonzero identity/version,
distinct signed-24-bit raw endpoints, finite pressure endpoints, and an
increasing pressure span inside the declared pressure range. No DOCX sample
raw counts, GPIO assignments, or 40 kPa full-scale value are compiled as
defaults. A mismatched frame identity/version is rejected; no previous
calibrated value is retained on an invalid frame.

Baseline tracking is separate and opt-in. The selected policy is a full
15-second sample span with at least three valid calibrated samples. A
nonzero maximum sample age/gap must also be explicitly configured; without it
the baseline is disabled. Missing, invalid, stale, out-of-order, or
profile-mismatched samples do not enter the baseline. A gap above the
configured limit starts a new coverage window. The baseline reports
`InsufficientData` until both coverage and count are met, and reports `Stale`
when the newest accepted sample exceeds the configured age. The baseline is
not wired to a risk threshold by this prompt.

The `validMask` mapping, sensor identity, calibration artifact format/hash,
coefficients, units, and approved conversion equations remain TBD. MPU9250,
soil-moisture, rain, and HX710B values must not be physically converted until
their relevant definitions and evidence are approved.

## Feature-window invalidation

`FeaturePipeline` owns the window reset when `ProcessedFrame.calibrationVersion`
changes. It clears buffered samples before accepting a new-version sample, so
no window combines calibration versions. In `RAW_ONLY`, version zero remains
unconfigured and is not an identity calibration. A nonzero input version is
rejected by this service until an exactly matching approved artifact is
available.

The firmware service has no calibration artifact model or activation path.
The host-only `CalibratedSampleCsvExporter` provides a bounded serializer
contract for `calibrated_samples.csv`; it does not change `CalibrationService`
or calculate physical values. The caller supplies the calibration descriptor
and already-produced sample value. The exporter requires artifact
ID/version/hash, matching sensor/channel, unit, finite valid range, transform
ID/version, and accepted approval/evidence; it rejects row identity/hash
mismatches and blanks calibrated values that are invalid, non-finite, or
outside the declared range. GT5 evidence is allowed only for a `testOnly`
fixture and is never production evidence.

In `RAW_ONLY`, the exporter retains a valid raw value, leaves calibrated value
and unit empty, and records calibration identity as `UNKNOWN`, version zero,
`RAW_ONLY`, and `CALIBRATION_NOT_CONFIGURED`. It does not activate identity
calibration or introduce conversion coefficients. A passing test fixture
verifies serialization only; the physical conversion and production artifact
remain blocked pending approved calibration metadata and evidence.

## Host tests

CalibrationService coverage is part of the `test_system_contracts` native
suite. The test translation unit includes `CalibrationService.cpp` with a
`TEST ONLY` marker because the current native test source filter does not
compile that source file. HX710B profile mapping, mismatch, baseline coverage,
freshness, and invalid-sample tests are included as well. A fake
`Arduino.h`/GPIO clock shim exercises bounded protocol behavior; these tests
are not target-build or hardware tests.
The host-only calibrated-sample exporter has separate RAW_ONLY, test-only
fixture/range, and calibration identity mismatch tests. Test-only values are
GT5 synthetic fixtures and do not support physical or field-accuracy claims.
