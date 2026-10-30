# Firmware TODO list

This list tracks project work that is required before hardware deployment,
production logging, dataset release, or Edge Impulse inference. `TBD` values
must remain unset/fail-closed until supported by the referenced hardware
evidence or an approved project artifact. Test fixtures and mock profiles are
not production configuration.

## 1. Confirm the hardware baseline

- [ ] Record the exact ESP32-S3 board, module/variant, manufacturer, hardware
  revision, and PlatformIO board ID from the BOM and board markings.
- [ ] Review the schematic and electrical levels for the ESP32-S3, MPU9250,
  ADC sensors, HX710B, storage, and any alarm outputs.
- [ ] Fill the central pin map only from the reviewed schematic; check that
  pins support the selected peripheral and do not conflict with boot/USB/flash
  functions.
- [ ] Verify the MPU9250 module identity, I2C address, pull-ups, and voltage
  compatibility. Verify its register setup and calibration against the exact
  module/datasheet.
- [ ] Decide whether soil and rain are analog or digital on the actual hardware;
  verify ADC input range and calibrate before reporting physical/relative
  values.
- [ ] Identify the HX710B transducer and its measured quantity before defining
  any physical unit, conversion, or model feature. Keep the current raw-only
  contract until then.
- [ ] Select and verify storage hardware/filesystem and the physical board
  upload/serial port.
- [ ] Record presence/absence of RTC, PSRAM, and power monitor; unknown is not
  the same as absent.

## 2. Configure sensors and data quality

- [ ] Populate `Mpu9250DriverConfig`; validate axes, sign, units, scale, filter,
  output data rate, reference pose, calibration, and bounded I2C operations
  with bench evidence.
- [ ] Populate `SoilMoistureDriverConfig` and `RainSensorDriverConfig` from
  measured calibration points. Approve ADC resolution, maximum count, filter
  window, wetness semantics, and calibration version.
- [ ] Populate `Hx710bProtocolConfig` only from the selected device
  datasheet/protocol verification; set calibration factor/version only when
  calibration is supported. Do not assign a physical unit prematurely.
- [ ] Define `SensorManagerConfig` sampling period and per-sensor begin/read
  timeout, retry count, and retry backoff. Confirm driver calls are themselves
  bounded; elapsed-time checks do not preempt a blocked synchronous call.
- [ ] Define `DataQualityConfig`: required channels, per-sensor maximum age,
  and only evidence-based ranges. Keep missing, stale, invalid, and non-finite
  states distinct; never replace invalid data with zero.
- [ ] Define and version calibration artifacts; ensure calibration changes
  invalidate any in-progress feature window.

## 3. Approve feature and Edge Impulse contracts

- [ ] Approve a released FSS artifact and exact SHA-256; FSS v1.1 is currently
  DRAFT and its model feature indexes are not frozen.
- [ ] Obtain the actual Edge Impulse deployment/export and verify its SDK,
  generated header, input shape/dimension, channel/index order, sample rate,
  window, stride, preprocessing, and output count/kind.
- [ ] Decide whether the deployed model expects raw time-series or engineered
  features; do not assume the candidate tilt/magnitude formulas are approved.
- [ ] Record the model artifact SHA-256, deployment/version metadata, target
  semantics, and approved schema-to-model mapping.
- [ ] Approve golden test vectors and compare host and ESP32-S3 feature output
  against the export before enabling inference.
- [ ] Keep `EdgeImpulseAdapter` fail-closed until all schema, model, and output
  approval flags are supported by verified metadata. No cloud dataset upload
  is configured by the current serial mock workflow.

## 4. Approve risk, task, queue, and watchdog configuration

- [ ] Define and approve the `RiskManagerConfig` hash/version, score direction,
  score range/boundaries, expected output count, model/tilt rule enablement,
  fallback policy, promotion/recovery counts, and recovery hold time.
- [ ] Approve tilt validation definition, reference pose, axis/sign convention,
  and quality requirements before enabling tilt-based risk.
- [ ] Measure task worst-case execution time, stack high-water marks, and
  producer/consumer rates on the target. Then define each
  `TaskRuntimeConfig`: priority, stack words, cycle/queue/idle wait ticks, and
  heartbeat timeout ticks.
- [ ] Size `QueueDepths` from measured bursts, blocking, item sizes, and RAM
  budget. Resolve the ICD/LLD event/log queue payload disagreement before
  release.
- [ ] Wire the composition root to initialize approved config, BSP, queues,
  services, and tasks in order; roll back already-created resources on startup
  failure.
- [ ] Implement and verify the ESP32-S3 hardware watchdog, timeout/deadline and
  feed policy, supervisor monitoring, reset-loop detection, and recovery
  policy. A stub/unavailable watchdog must not be reported as healthy.
- [ ] Exercise queue-full/failure behavior for every pipeline stage; confirm
  gaps invalidate dependent feature windows/results and are surfaced according
  to the approved fault policy.
- [ ] Fault-inject sensor timeout/disconnect, queue saturation, storage failure,
  watchdog timeout, and restart/recovery behavior on target hardware.

## 5. Finalize logging and dataset CSV contracts

- [ ] Preserve the operational logger contract
  `timestamp,tilt,soil,rain,stress,risk` unless a separately reviewed schema
  change is approved. It is not the raw DSD dataset schema and omits provenance,
  channel validity/quality, and calibration metadata.
- [ ] Decide whether production auditability requires a companion metadata
  record/file or a versioned wider operational log. Do not mix the mock serial
  format, operational logger, and training dataset as if they were equivalent.
- [ ] If building a DSD dataset exporter, implement the approved raw schema
  version and fixed field order, including device/session/boot/sequence/time
  provenance, time status, firmware/config/calibration IDs, sensor status,
  raw channel values, per-channel validity/quality, masks/flags, event and
  annotation references, and eligibility/reason fields as applicable.
- [ ] Mark absent hardware channels explicitly; do not emit fabricated zeros.
  Leave invalid numeric fields empty with corresponding validity/quality.
- [ ] Define the dataset timestamp semantics and synchronization policy.
  Preserve monotonic uptime separately from UTC; do not infer UTC or silently
  hold/interpolate samples.
- [ ] Verify UTF-8, comma delimiter, quote escaping, LF terminator, decimal
  point, finite numeric values, bounded row length, rotation, torn-tail
  recovery, flush/durability, and behavior on full/failed storage.
- [ ] Keep the current mock serial CSV marked `dataset_eligible=false` with
  reason `SYNTHETIC_MOCK`; it is diagnostic-only, not Edge Impulse training
  data.
- [ ] Approve label/target definitions, annotation provenance, eligibility,
  dataset split/leakage policy, manifest fields, hashes, and acceptance gates
  before creating a training candidate.
- [ ] If dataset upload is later authorized, define the approved project,
  ingestion mechanism, authentication/storage handling, retry policy, and
  receipt/audit behavior outside the firmware source. Never commit tokens.

## 6. Complete build, upload, and acceptance evidence

- [ ] Keep `build_project.bat` development-only by default: native tests, mock
  build/upload, then serial capture. Do not treat the mock image as production.
- [ ] Install/verify PlatformIO, the host compiler used for native tests, and
  the ESP32 toolchain; build both `native-unit-tests`,
  `native-integration-tests`, and the selected ESP32-S3 environment from a
  clean checkout.
- [ ] Connect the intended ESP32-S3 board and verify the one-click upload,
  serial monitor, reset behavior, and captured CSV on the actual COM port.
- [ ] Run static analysis/MISRA review, full unit/integration tests,
  hardware-in-the-loop/fault-injection tests, and document all exceptions.
- [ ] Measure target heap/free memory, task stack high-water marks, watchdog
  timing, queue high-water marks, logger throughput, and flash/SD durability.
- [ ] Archive build/toolchain versions, configuration hashes, test reports,
  board identity, firmware hash, and release approval evidence.

## Variables that must be defined

Populate the existing fields listed below in the owning configuration or
manifest. These names identify code/configuration fields, not approved values.
Values marked `TBD` must not be copied from unit-test or mock examples.

### Board and peripherals

| Owning type/file | Fields to define | Evidence/source |
|---|---|---|
| `BoardInfo` / `BoardVersion` | `boardId`, `variant`, `manufacturer`, `boardIdKnown`, `variantKnown`, hardware version fields | Board SKU, markings, BOM |
| `BoardPins` | `imuI2c.sda`, `imuI2c.scl`, `adc.soilMoisture`, `adc.rainSensor`, `digitalSensors.*`, `hx710.clock`, `hx710.data`, selected `storageSpi.*` or `storageSdMmc.*`, `alarm.*` | Reviewed schematic and peripheral selection |
| `BoardConfig::imuI2c` | `controller`, `clockHz`, `imuAddress`, `electricalLevelsVerified`, `pullUpsVerified` | Board/module datasheet and electrical review |
| `BoardConfig::soilAdc`, `rainAdc` | `resolutionBits`, `attenuation`, `inputRangeVerified`, `attenuationConfigured` | ESP32-S3 ADC configuration and sensor output range |
| `BoardConfig` | `storageInterface`, `ledOutput`, `buzzerOutput`, `relayOutput`, `rtcPresent`, `psramPresent`, `powerMonitorPresent` and configured/verified flags | BOM, schematic, safe-state review |
| `ProjectConfig` | SDA/SCL, I2C address/clock, sensor ranges, ODR, filter, calibration references | Keep centralized and derived from the approved board/sensor configuration |

### Sensor, quality, and feature parameters

| Owning type/file | Fields to define | Evidence/source |
|---|---|---|
| `Mpu9250DriverConfig` | `i2cAddress`, `transactionTimeoutMs`, `registerSetupConfigured`, register values, `calibrationSampleCount`, `calibrationIntervalMs`, `expectedWhoAmI`, `accelGPerLsb`, `gyroDpsPerLsb`, `referenceAccelG` | Exact MPU9250 module, datasheet, bench calibration |
| `SoilMoistureDriverConfig` | `adcMaximumCount`, `filterWindowSamples`, both `calibrationPoints`, `calibrationVersion`, ADC/calibration verified flags | Selected sensor and recorded dry/wet calibration |
| `RainSensorDriverConfig` | `adcMaximumCount`, `filterWindowSamples`, both calibration points, `calibrationVersion`, `wetnessSemanticsVerified`, ADC/calibration flags | Sensor characterization; do not label wetness as rainfall/rate without evidence |
| `Hx710bProtocolConfig` / `Hx710bCalibration` | `readyTimeoutUs`, `conversionTimeoutUs`, `clockHalfPeriodUs`, `dataBitCount`, `trailingClockPulses`, `readyLevelHigh`, `signedOutput`, `verifiedAgainstDatasheet`, calibration `factor`, `version`, `configured` | HX710B datasheet and identified transducer; no physical unit until approved |
| `SensorManagerConfig` | `bootId`, `samplePeriodMicros`, each sensor's `beginTimeoutMicros`, `timeoutMicros`, `maxRetries`, `retryBackoffMicros` | Approved sampling requirements and measured bounded driver timing |
| `DataQualityConfig` | `requiredChannelsConfigured`, `requiredChannels[]`, each sensor `freshness[].maximumAgeMs`, channel `ranges[].minimum/maximum/configured` | Approved quality policy and channel characterization |
| `FeaturePipelineConfig` | schema version/ID/hash, feature IDs/index/eligibility, `featureCount`, `windowLengthSamples`, `strideSamples`, `sampleRateHz`, `samplePeriodToleranceMs`, `preprocessingVersion`, scale/approval flags | Released FSS hash and actual model input contract |

### Model, risk, runtime, storage, and tooling

| Owning type/file | Fields to define | Evidence/source |
|---|---|---|
| `EdgeImpulseModelConfig` | `featureSchemaVersion/Id/Hash`, `modelHash`, `inputDimension`, `outputCount`, `outputKind`, schema/model/hash/output approval flags | Exact generated Edge Impulse export and verified deployment manifest |
| Build configuration | `PROJECT_EDGE_IMPULSE_MODEL_HEADER` and generated model include paths/libraries | Approved export only; leave undefined while no artifact is released |
| `RiskManagerConfig` | version/hash/approval; output count; score direction/range/boundaries; rule/fallback flags; tilt limits; hysteresis samples and hold time | Approved target/output semantics and risk policy |
| `TaskRuntimeConfig` | per-task `priority`, `stackWords`, `cycleTicks`, `queueWaitTicks`, `idleWaitTicks`, `heartbeatTimeoutTicks`, `priorityConfigured` | Target measurements and scheduler review |
| `QueueDepths` | `sensor`, `feature`, `model`, `risk`, `event`, `log` | Measured rates/bursts/blocking, item sizes, and RAM budget |
| Watchdog/BSP policy | hardware watchdog timeout, feed/deadline policy, monitored task set, supervisor interval, reset-loop/recovery limits | ESP32-S3 watchdog implementation and fault-injection results |
| `CsvLoggerConfig` / storage adapter | `maximumFileBytes`, `filePrefix`, selected filesystem, capacity/retention, flush/durability and recovery behavior | Selected media/driver tests and storage budget |
| Dataset/CSV manifest | schema/version/hash, dataset/site/device/sensor/session IDs, firmware/config/calibration IDs, time/UTC policy, labels, eligibility/reason, split and lineage metadata | DSD/DOP approval; separate from operational and mock CSV |
| `build_project.bat` environment | optional `PIO_UPLOAD_PORT`, `PIO_MONITOR_PORT` | USB enumeration; set only when auto-detection is ambiguous |

## Current development-only behavior

- The Windows batch script defaults to tests, mock firmware upload, and serial
  capture. `production-build` only attempts the fail-closed production target;
  it does not make the application deployable.
- The mock serial CSV is synthetic and ineligible for training. The operational
  six-column logger remains unchanged.
- Edge Impulse Studio dataset upload, production inference, production risk
  thresholds, board pin mapping, and FreeRTOS task/queue sizing remain
  unconfigured until their respective approvals/evidence exist.
