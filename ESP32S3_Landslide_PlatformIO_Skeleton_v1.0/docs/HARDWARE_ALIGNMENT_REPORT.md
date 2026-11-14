# Pilot Hardware Alignment Report

**Audit date:** 2026-11-14  
**Project:** ESP32-S3 / Arduino / PlatformIO / C++17  
**Result:** PARTIALLY IMPLEMENTED; PILOT DEPLOYMENT BLOCKED

This audit inspected the current workspace, PlatformIO environments, BSP,
sensor drivers/adapters, models, services, managers, AI adapter, queues/tasks,
storage and dataset contracts, tests, existing implementation notes, and the
local prototype DOCX. The DOCX was treated as a proposal only. No physical
configuration, calibration, threshold, model contract, or credential was
copied into production source.

## Executive summary

The repository is a layered firmware skeleton with host-tested sensor,
data-quality, calibration-boundary, schema, risk-policy, queue, display,
alarm, Blynk, and dataset components. It is not a composed Pilot firmware.
`BoardPins`/`BoardConfig` remain unset and unapproved; the production
`main.cpp` fails closed before it creates the task graph. The selected
PlatformIO board is a compile target only and does not establish physical
board identity.

One safety correction was made during this audit: the MPU driver now requires
an explicit tilt-definition approval before calculating Roll/Pitch. Without
it, raw IMU, datasheet-scaled accel/gyro, and temperature remain readable,
while Roll/Pitch and combined tilt are explicitly invalid. Production
`RiskManager` validation now requires AI model input and rejects tilt-rule
configurations; tilt-rule tests are confined to `PROJECT_TEST_ONLY` builds.

## Hardware alignment matrix

| Area | Status | Current evidence and gap |
|---|---|---|
| Board/module identity | TBD / NEED APPROVAL | `kBoardInfo` identifies the board/revision as unknown. PlatformIO selects `esp32-s3-devkitc-1` (N8 build target); the prototype describes an N16R8 module. Confirm the actual board, memory, schematic and BOM before enabling pins. |
| GPIO, I2C and ADC | TBD / NEED APPROVAL | Every `kBoardPins` assignment is unconfigured and `kBoardConfig.hardwareReviewApproved` is false. The prototype map, ADC capability/range, GPIO0 strapping, peripheral collisions, bus voltage and pull-ups have not been approved. See [PILOT_CONFIGURATION.md](PILOT_CONFIGURATION.md). |
| Soil moisture | PARTIALLY IMPLEMENTED | `SoilMoistureDriver` supports three independent channel configs, samples, validity/quality and a valid-channel average. It currently requires configured endpoints/calibration and verified ADC settings even to start, so there is no uncalibrated raw-only acquisition path. No approved per-probe calibration or production composition exists; prototype air/water endpoints are not installed. |
| MPU9250 | PARTIALLY IMPLEMENTED | I2C driver checks WHO_AM_I, transfer lengths, timeouts and read failures; raw, accel/gyro and temperature data are represented. Tilt calculation is now disabled unless `tiltDefinitionApproved` is true. Board mounting, axis transform and reference pose remain TBD. |
| HX710B / PWP | PARTIALLY IMPLEMENTED | Bounded raw acquisition, timeout/error states, identity/version-bound pressure-profile validation and optional conversion exist. No approved transducer identity, range, calibration artifact or coefficients are installed; PWP remains unavailable/not calibrated. |
| YL-83 / FC-37 | PARTIALLY IMPLEMENTED | Analog relative surface wetness, optional digital state and wet-duration coverage are modeled separately. The implementation/docs do not certify rainfall amount or `mm/h`; calibration and digital polarity remain unapproved. |
| BOOT / RESET DEMO | MISSING | No BOOT-button debounce or reset request/runtime owner exists. GPIO0 is not configured. The exact resettable demo state and strapping behavior require board and application-owner approval; persistent calibration/storage must not be reset. |
| SSD1306 OLED | PARTIALLY IMPLEMENTED | Fixed-buffer display service and injected transport contract have host tests. No concrete I2C display transport, shared-bus test, production task ownership, or board startup wiring exists. |
| Buzzer | PARTIALLY IMPLEMENTED | Safe logical OFF, explicit polarity, nonblocking alert cadence and fault reporting have host tests. No approved pin/electrical safe state, GPIO adapter, production task or physical feedback verification exists. |
| Data contracts | PARTIALLY IMPLEMENTED | Fixed-storage `SensorFrame`, `ProcessedFrame`, `FeatureVector`, `ModelOutput`, `RiskAssessment`, `EventRecord` and `HealthSnapshot` exist. Schema/bit mappings, some field semantics, complete raw-to-processed provenance and release identities remain TBD. Roll/Pitch validity is now explicit at the IMU physical-sample boundary. |
| AI-first risk | PARTIALLY IMPLEMENTED / BLOCKED | Feature/schema validation, inference status and provenance checks exist; missing model/export fails closed. Production `RiskManager` now rejects non-AI tilt rules. No owner-supplied Edge Impulse export, released FSS, approved output semantics or production runtime composition is present. |
| Dataset CSV | PARTIALLY IMPLEMENTED | Host raw-core, manifest, calibrated-sample, window, feature and evaluation tooling exists. The DSD v1.2 raw core is a draft and does not contain every Pilot field; operational `CsvLogger` remains a separate six-column contract. No production logger mapping/backend connects sensor/quality/model records. |
| Runtime integration | MISSING / BLOCKED | Task classes and bounded queues exist, and `AppController` is a host-tested lifecycle protocol. `main.cpp` deliberately blocks on unapproved board/configuration and does not instantiate the app controller, task graph, logger, OLED, buzzer or Blynk transport. |
| Storage/network | BLOCKED | No production filesystem backend with finite flush guarantees or production Blynk/Wi-Fi transport/composition is available. Credentials are not configured in source. |

## Phase plan and disposition

| Phase | Disposition | Remaining work |
|---|---|---|
| 0 — Workspace audit | COMPLETE | Audit evidence is recorded here and in the build report. |
| 1 — Gap analysis | COMPLETE | Matrix above records implemented, partial, missing and TBD areas. |
| 2 — Data contracts | PARTIAL | Tilt approval/validity is explicit; freeze schema/version/bit maps only against approved controlled contracts. |
| 3 — Sensor drivers | PARTIAL | Host contracts exist; Pilot pins, ADC settings, sensor variants, calibration and target validation are blocked. |
| 4 — UI/alarm | PARTIAL | Host-tested services exist; BOOT handler and production transport/task integration are missing. |
| 5 — AI/CSV | BLOCKED | Actual model/FSS/output contract and runtime CSV mapping/backend are absent. Do not synthesize labels or physical values. |
| 6 — Runtime integration | BLOCKED | Requires approved board/configuration, storage/backend, task sizing, model gate and concrete adapters. |
| 7 — Verification | PARTIAL | Host suites and target compile can run; hardware, model, endurance and production runtime verification remain not run. |

## Security and source boundaries

The local prototype DOCX contains credential-like literals. It is ignored by
Git and was not copied into code, reports, build flags or test fixtures.
Treat the values as exposed and rotate/revoke them with their providers. The
prototype reference is not safe to redistribute until sanitized.

The supplied DOCX's code, thresholds, calibration examples, pin map and
network values are not approved Pilot configuration. Controlled `Document/*`
inputs were not edited.

## Evidence and limits

See [BUILD_TEST_REPORT.md](BUILD_TEST_REPORT.md) for exact commands and current
results. Host tests and successful firmware compilation do not establish
physical wiring, calibration accuracy, model correctness, risk limits,
network behavior, or release readiness.

## Required approvals before Pilot composition

1. Exact ESP32-S3 board/module/revision plus schematic and BOM.
2. GPIO, ADC, I2C, output, boot-strapping, voltage and pull-up review.
3. Sensor variant/identity and installation orientation.
4. Per-probe soil calibration and approved HX710B/piezometer calibration.
5. MPU mounting/axis/reference pose and approved Roll/Pitch definition.
6. Released feature/model schema, actual Edge Impulse export and output meaning.
7. Approved risk/event/alarm policy, runtime task/queue/stack bounds, and
   storage/network backend requirements.
