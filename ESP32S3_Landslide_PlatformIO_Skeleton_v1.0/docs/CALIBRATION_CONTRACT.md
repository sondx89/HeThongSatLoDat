# Calibration and raw-data contract

**Status:** Validity propagation only. No calibration artifact, coefficients,
sensor identity registry, units approval, or physical conversion is configured.
This document does not approve production use.

## Current behavior

`CalibrationService::loadCalibrationStatus()` returns
`ApiStatus::NotInitialized`; the existing `loadCalibration()` bool facade
therefore returns `false`. There is no configured identity calibration:
identity is a calibration choice and must not be inferred from absent data.

`CalibrationService::processStatus()` copies frame provenance and the opaque
quality and sensor-status bitfields, then classifies channel quality from the
matching `SensorStatus`. It does not modify the const `SensorFrame`, apply
coefficients, calculate tilt, or copy any candidate converted value into
`ProcessedFrame`. With a nonzero frame schema and calibration version zero, it
returns `ApiStatus::NotInitialized` and exposes raw-channel quality while
converted channels remain ineligible. The existing `process()` bool facade
returns `true` only for `ApiStatus::Ok`, so remains fail-closed.

## Public method contract

| Method | Preconditions and result | Side effects / execution | Ownership and concurrency | Errors |
|---|---|---|---|---|
| `loadCalibration()` | No input; returns `false` because no calibration source or identity profile is configured. | No I/O, waiting, allocation, or state mutation. | Stateless; no shared resources. Task context; ISR use is not promised. | Use `loadCalibrationStatus()` for the explicit `NotInitialized` reason. |
| `loadCalibrationStatus()` | No input; returns `ApiStatus::NotInitialized`. | No I/O, waiting, allocation, or state mutation. | `const` and stateless; concurrent calls are safe. Task context; ISR use is not promised. | No persistence source exists; no success or identity fallback. |
| `process(input, output)` | Requires live, distinct input/output objects. Returns `false` for current no-calibration, unsupported-version, or invalid-schema outcomes. | Resets and writes caller-owned `output`; does not modify `input`. Bounded in-memory work only. | Input is borrowed for the call; output is caller-owned. Concurrent use is safe only with distinct output objects. Task context; ISR use is not promised. | Use `processStatus()` to distinguish `InvalidArgument`, `VersionMismatch`, and `NotInitialized`. |
| `processStatus(input, output)` | Same object preconditions; returns `InvalidArgument` when frame schema version is zero, `VersionMismatch` when input calibration version is nonzero, and `NotInitialized` otherwise. | Copies metadata and classifies source quality; no conversion, I/O, waiting, allocation, or state mutation. Output is written even on non-`Ok`. | Input is borrowed for the call; output is caller-owned. `const`/stateless; concurrent use requires distinct output objects. Task context; ISR use is not promised. | Missing/faulted channels become `MISSING`; available non-finite fields become `NON_FINITE`. No unsupported schema or sensor identity is guessed. |

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
| `hx710bRaw` | No raw-value destination | `Hx710bRaw` | Raw quality only. |
| HX710B calibration/scaled/calibrated fields | `hx710bCalibratedValue` | `Hx710bCalibratedValue` | No coefficients or measurand are approved; not copied. A non-finite value flagged as present is `NON_FINITE`; otherwise `MISSING`. |

`ProcessedFrame` currently contains no raw-value fields. Therefore raw values
remain in the caller-owned, unchanged `SensorFrame`; callers must retain that
frame for raw logging/diagnostics. This service does not claim that raw values
were copied into `ProcessedFrame`.

## Validity and result rules

- A sensor channel is source-available only when its status is `valid` and
  health is `Healthy` or `Degraded`. `Unknown`, `Fault`, and invalid health
  enum values fail closed to `MISSING`.
- This service does not classify staleness: no freshness limits are approved.
  `qualityFlags` and `sensorStatusBits` are copied but not decoded.
- Available floating-point sources are checked with `isfinite`. A NaN or
  infinity is `NON_FINITE`, never a valid zero.
- `DataQualityState::VALID` is emitted only for integer raw channels in this
  uncalibrated implementation. Candidate processed fields remain `MISSING`
  unless their source is non-finite, in which case they are `NON_FINITE`.
- `validMask` remains zero: its bit-to-channel mapping is explicitly TBD.
  The service copies `qualityFlags` and `sensorStatusBits` without interpreting
  their unapproved bit assignments. `aggregateStatus` remains unknown because
  no sensor-to-aggregate policy is defined.
- `loadCalibrationStatus()` returns `NotInitialized`; no identity calibration
  is activated. `processStatus()` returns `InvalidArgument` for schema version
  zero, `VersionMismatch` for any nonzero input calibration version because no
  calibration version is loaded, and `NotInitialized` otherwise. Existing
  bool facades continue returning `false` for these non-`Ok` outcomes.
- A nonzero schema version is not certified as supported: no released
  `SensorFrame` schema registry is available to this service. Zero is rejected
  as missing.

## Blockers

`SensorFrame` has no per-sensor identity field or released sensor-ID mapping.
Wrong-sensor identity cannot be detected or tested without an approved frame
contract; channel field names alone are not proof of attached hardware
identity. The current version-mismatch test covers a nonzero calibration
version against the service's explicitly absent calibration, not artifact
compatibility.

The `validMask` mapping, sensor identity, calibration artifact format/hash,
coefficients, units, and approved conversion equations remain TBD. MPU9250,
soil-moisture, rain, and HX710B values must not be physically converted until
their relevant definitions and evidence are approved.

## Feature-window invalidation proposal

Not implemented here. When `FeaturePipeline` is later approved for calibration
provenance, it should clear its buffered window and reset its sample count when
the active calibration version changes. A window should be emitted only when
all contributing samples have the same approved calibration version; a
version change mid-window should discard the mixed-version window rather than
combine its samples. Version zero must remain an explicit unconfigured state,
not an identity calibration.

## Host tests

CalibrationService coverage is part of the `test_system_contracts` native
suite. The test translation unit includes `CalibrationService.cpp` with a
`TEST ONLY` marker because the current native test source filter does not
compile that source file. These tests are not target-build or hardware tests.
