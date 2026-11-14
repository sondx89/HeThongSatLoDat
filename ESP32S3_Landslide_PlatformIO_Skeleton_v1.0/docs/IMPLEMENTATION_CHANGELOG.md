# Pilot Alignment Implementation Changelog

**Date:** 2026-11-14  
**Scope:** Align existing ESP32-S3 firmware skeleton with the supplied Pilot
baseline without adopting unapproved prototype hardware or model values.

## Changes in this alignment pass

- Added `tiltDefinitionApproved` to `Mpu9250DriverConfig`; Roll/Pitch
  calculation now remains disabled by default while raw IMU, accel/gyro and
  temperature acquisition can continue. Added explicit Roll/Pitch validity
  flags and the `TiltDefinitionNotApproved` status.
- Added host coverage that verifies non-approved tilt remains invalid without
  blocking other MPU data.
- Restricted production `RiskManager` configuration to AI model input; tilt
  rules are accepted only in `PROJECT_TEST_ONLY=1` builds. Added a production
  policy test environment.
- Added a Pilot alignment report, configuration register and AI-first risk ADR.
- Excluded the local prototype DOCX from Git and updated credential guidance
  after finding credential-like literals in that reference. No values were
  copied into source/configuration.

## Existing implementation confirmed by this audit

Three-channel soil acquisition, MPU9250/HX710B/rain driver contracts,
calibration/data-quality services, feature/schema validation, inference/risk
contracts, queue/task infrastructure, host CSV/dataset tooling and optional
display/alarm/Blynk service contracts predate this pass. Their existence does
not imply Pilot hardware approval or runtime composition; see
[HARDWARE_ALIGNMENT_REPORT.md](HARDWARE_ALIGNMENT_REPORT.md).

## Deliberately not changed

- No GPIO, I2C address, ADC attenuation, electrical polarity, calibration
  coefficient, geotechnical threshold, model feature/output meaning or task
  budget was inferred from the prototype.
- No runtime production composition was fabricated while BSP/configuration,
  storage/backend, model and target budget gates remain unset.
- The DRAFT DSD/FSS and other controlled documents were not edited or promoted.

## Build/test evidence

See [BUILD_TEST_REPORT.md](BUILD_TEST_REPORT.md) for exact commands and current
results. Physical-hardware, Edge Impulse export, target inference and release
qualification remain blocked/not run.
