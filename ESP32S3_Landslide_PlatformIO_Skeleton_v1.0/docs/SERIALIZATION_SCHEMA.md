# Serialization inventory and format decision

## Status

This document inventories the current C++ record members and identifies
format-use evidence. The EventRecord JSONL v2 contract below is approved and
implemented. The remaining six record schemas, CSV codecs, and binary layout
are not approved. Their generic functions remain `NotImplemented`; do not
treat their presence as evidence that a persistent/API format is required.

## User-selected initial scope

The user selected EventRecord JSONL only; the existing dedicated CSV
components remain separate, the other six record codecs are deferred, and no
binary codec is requested. The user approved the ICD text baseline: UTF-8,
stable snake_case keys, lowercase JSON booleans, `%.9g` text for finite
binary32 values, and non-finite values represented as `null` with explicit
validity/status. This approval does not resolve EventRecord's schema mismatch
or approve a complete field set/order.

The user subsequently approved a lossless EventRecord JSONL v2 codec, leaving
ICD v1 unchanged. Approved layout conventions are `record_type` followed by
`schema_version` and all current struct members in declaration order, with
snake_case keys; symbolic canonical enum tokens; all 32 hash bytes as lowercase
hex with their validity fields; and full UTF-8 bounded strings. The user also
approved preserving `crc32` and `crc32Valid` as opaque stored fields without
claiming they verify the encoded JSONL bytes. The codec requires
`schemaVersion == 2`. The other six codecs, CSV, and binary remain deferred.

The field lists below follow the C++ declarations only to make the inventory
auditable. They do not approve serialization in declaration order. Types use
the declared fixed-width integer type, `float32`, `bool`, enum type, or
bounded character/byte array. Nested `SensorStatus`, task, sensor, and queue
members are listed separately.

## Seven-record field inventory

### SensorFrame

Current `schemaVersion` default: `0` (TBD).

| Group | C++ fields and declared types |
|---|---|
| Identity/time | `schemaVersion:uint32`, `bootId:uint64`, `sequence:uint64`, `timestampMs:uint64`, `samplePeriodMs:uint32`, `timeStatus:TimeStatus` |
| IMU raw | `imu.accelX:int32`, `imu.accelY:int32`, `imu.accelZ:int32`, `imu.gyroX:int32`, `imu.gyroY:int32`, `imu.gyroZ:int32` |
| IMU physical | `imuPhysical.accelXG:float32`, `imuPhysical.accelYG:float32`, `imuPhysical.accelZG:float32`, `imuPhysical.gyroXDps:float32`, `imuPhysical.gyroYDps:float32`, `imuPhysical.gyroZDps:float32` |
| Tilt | `tiltAngleDeg:float32`, `tiltAngleValid:bool` |
| Soil | `soilMoistureRaw:int32`, `soilMoisturePct:float32` |
| Rain | `rainRaw:int32`, `rainWetnessRelativePct:float32`, `rainWetnessValid:bool`, `rainRateMmH:float32` |
| HX710B | `hx710bRaw:int32`, `hx710bCalibrationFactor:float32`, `hx710bCalibrationFactorValid:bool`, `hx710bFactorScaledRaw:float32`, `hx710bFactorScaledRawValid:bool`, `hx710bCalibratedValue:float32` |
| Masks/config | `validMask:uint32`, `qualityFlags:uint32`, `sensorStatusBits:uint32`, `calibrationVersion:uint32` |
| Per-sensor status (each of `imuStatus`, `soilStatus`, `rainStatus`, `hx710bStatus`) | `health:SensorHealth`, `valid:bool`, `errorCode:uint16`, `qualityFlags:uint32`, `timestampMs:uint64` |

Unresolved: valid-mask/quality/status bit assignments, channel availability,
per-field validity rules, calibration-factor semantics, and several units or
measurement definitions.

### ProcessedFrame

Current `schemaVersion` default: `0` (TBD).

| Group | C++ fields and declared types |
|---|---|
| Identity/time | `schemaVersion:uint32`, `bootId:uint64`, `sequence:uint64`, `timestampMs:uint64` |
| Measurements | `accelXG:float32`, `accelYG:float32`, `accelZG:float32`, `gyroXDps:float32`, `gyroYDps:float32`, `gyroZDps:float32`, `tiltRollDeg:float32`, `tiltPitchDeg:float32`, `accelMagnitude:float32`, `soilMoisturePct:float32`, `rainWetnessRelativePct:float32`, `rainRateMmH:float32`, `hx710bCalibratedValue:float32` |
| Per-channel quality | `channelQuality[SensorChannel::Count]:DataQualityState` |
| Masks/status/config | `validMask:uint32`, `qualityFlags:uint32`, `sensorStatusBits:uint32`, `calibrationVersion:uint32`, `aggregateStatus:SensorStatus` (nested members as listed under `SensorFrame`) |

Unresolved: measurement formulas/frames/units, frozen channel order and bit
mapping, and per-channel validity encoding.

### FeatureVector

Current `schemaVersion` default: `0` (TBD).

| Group | C++ fields and declared types |
|---|---|
| Schema/provenance | `schemaVersion:uint32`, `schemaId:uint32`, `schemaHashSha256[32]:uint8`, `schemaHashValid:bool`, `bootId:uint64`, `relatedSequenceNo:uint64` |
| Window | `windowStartMs:uint64`, `windowEndMs:uint64`, `windowSampleCount:uint32` |
| Values/validity | `values[kMaxFeatures=32]:float32`, `count:uint16`, `featureValidMask:uint32`, `valid:bool` |

Unresolved: schema ID registry, hash representation, actual feature dimension,
feature order, and whether unused array elements are omitted or serialized.

### ModelOutput

Current `schemaVersion` default: `0` (TBD).

| Group | C++ fields and declared types |
|---|---|
| Identity/provenance | `schemaVersion:uint32`, `bootId:uint64`, `inferenceId:uint64`, `relatedSequenceNo:uint64`, `sourceWindowStartMs:uint64`, `sourceWindowEndMs:uint64`, `sourceWindowSampleCount:uint32`, `sourceFeatureSchemaVersion:uint32`, `sourceFeatureSchemaId:uint32`, `sourceFeatureSchemaHashSha256[32]:uint8`, `sourceFeatureSchemaHashValid:bool` |
| Timing | `inferenceStartMs:uint64`, `inferenceDurationMs:uint32`, `inferenceLatencyUs:uint32`, `timestampMs:uint64` |
| Results | `rawOutputs[kMaxOutputs=32]:float32`, `outputCount:uint16`, `predictionValue:float32`, `predictionValid:bool`, `status:InferenceStatus` |
| Model identity | `modelHashSha256[32]:uint8`, `modelHashValid:bool` |

Unresolved: model output count/shape, canonical duration unit, hash
representation, schema version, and whether unused outputs are omitted.

### RiskAssessment

Current `schemaVersion` default: `0` (TBD).

| Group | C++ fields and declared types |
|---|---|
| Identity | `schemaVersion:uint32`, `bootId:uint64`, `sequenceNo:uint64`, `timestampMs:uint64` |
| Assessment | `risk:RiskState`, `health:HealthState`, `stateEnterMs:uint64`, `persistenceMs:uint32`, `riskScore:float32`, `riskScoreValid:bool`, `decisionReasonCode:uint16`, `activeFaultFlags:uint32` |
| Output/config | `alarmOutputState:AlarmOutputState`, `riskConfigVersion:uint32`, `activeConfigHashSha256[32]:uint8`, `activeConfigHashValid:bool`, `valid:bool` |

Unresolved: released reason-code catalog, fault-bit assignments, enum wire
names/codes, schema version, and risk-score validity/range policy.

### EventRecord

Current `schemaVersion` default: `1`; the ICD names `EVENT_SCHEMA_VERSION=1`
as the logical EventRecord schema. The ICD JSONL field list does not fully
match the current C++ struct; see the mismatch below.

| Group | C++ fields and declared types |
|---|---|
| Identity/time | `schemaVersion:uint16`, `eventId:uint64`, `eventSequenceNo:uint32`, `bootId:uint64`, `timestampMs:uint64` |
| Event identity/classification | `eventName:char[32]`, `eventCatalogId:char[32]`, `severity:EventSeverity`, `source:char[24]`, `statusCode:char[32]`, `errorCode:char[32]`, `errorCodeValid:bool`, `faultCode:char[32]`, `faultCodeValid:bool` |
| State/context | `riskState:RiskState`, `healthState:HealthState`, `manifestId:char[64]`, `manifestIdValid:bool`, `firmwareVersion:char[65]`, `modelVersion:char[65]`, `modelVersionValid:bool`, `featureSchemaHashSha256[32]:uint8`, `featureSchemaHashValid:bool`, `configVersion:char[65]`, `configVersionValid:bool`, `calibrationVersion:char[65]`, `calibrationVersionValid:bool` |
| Relation/message/value | `relatedSequenceNo:uint64`, `relatedSequenceNoValid:bool`, `message:char[96]`, `valueType:ValueType`, `value:float32`, `valueText:char[96]`, `unit:char[16]`, `valueValid:bool` |
| Counters/integrity/validity | `droppedEventCount:uint32`, `crc32:uint32`, `crc32Valid:bool`, `valid:bool` |
| Additional logical state | `sensorId:SensorId`, `sensorIdValid:bool`, `stateBefore:char[16]`, `stateBeforeValid:bool`, `stateAfter:char[16]`, `stateAfterValid:bool` |

The ICD logical EventRecord v1 includes `event_catalog_id` and fields such as
`event_name`, `fault_code`, `crc32`, but the sample C++ declaration additionally
has `errorCode`, `valueType`, `valueText`, `sensorId`, `stateBefore`, and
`stateAfter`; other member validity flags and field-name details also need
reconciliation. The ICD says CRC is conditional and must be omitted unless its
covered bytes and calculation parameters are specified. Event string limits,
released catalogs, enum tokens, and null rules are not all frozen.

### HealthSnapshot

Current `schemaVersion` default: `0` (TBD).

| Group | C++ fields and declared types |
|---|---|
| Identity/time/reset | `schemaVersion:uint32`, `bootId:uint64`, `sequenceNo:uint64`, `uptimeMs:uint64`, `resetReason:ResetReason` |
| Memory | `heapFreeBytes:uint32`, `heapMinFreeBytes:uint32`, `largestFreeBlockBytes:uint32`, `internalHeapBytes:uint32`, `psramTotalBytes:uint32`, `psramFreeBytes:uint32`, `psramMetricsValid:bool` |
| Task metrics | `taskStackMetrics[kMaxTaskMetrics=8]:TaskStackMetric`, `taskStackMetricCount:uint8` |
| Sensor metrics | `sensorMetrics[kMaxSensorMetrics=4]:SensorHealthMetric`, `sensorMetricCount:uint8` |
| Queue metrics | `queueMetrics[kMaxQueueMetrics=8]:QueueHealthMetric`, `queueMetricCount:uint8` |
| Counters | `inferenceCount:uint64`, `sensorReadErrorCount:uint64`, `sensorOutOfRangeCount:uint64`, `inferenceErrorCount:uint64`, `modelOutputNanCount:uint64`, `deadlineMissCount:uint64`, `logWriteFailureCount:uint64`, `queueOverflowCount:uint64`, `taskRestartCount:uint64`, `watchdogCount:uint64`, `watchdogCountValid:bool`, `riskTransitionCount:uint64`, `alarmDurationMs:uint64` |
| Storage/timing | `storageBytesFree:uint64`, `storageRotationCount:uint32`, `storageMetricsValid:bool`, `lastSensorReadDurationMs:uint32`, `lastInferenceDurationMs:uint32`, `lastLoopPeriodMs:uint32` |
| Config/power/fault/health | `activeManifestHashSha256[32]:uint8`, `activeManifestHashValid:bool`, `configValidationStatus:ApiStatus`, `configValidationValid:bool`, `supplyVoltageMv:uint32`, `brownoutCount:uint64`, `powerMetricsValid:bool`, `faultFlags:uint32`, `healthState:HealthState` |

Nested metric fields:

| Nested type | C++ fields and declared types |
|---|---|
| `TaskStackMetric` | `taskId:uint16`, `highWaterWords:uint32`, `heartbeatAgeMs:uint32`, `failureCount:uint64`, `deadlineMissCount:uint64`, `restartCount:uint32`, `valid:bool` |
| `SensorHealthMetric` | `sensorId:SensorId`, `health:SensorHealth`, `lastSampleAgeMs:uint32`, `readErrorCount:uint64`, `outOfRangeCount:uint64`, `recoveryCount:uint32`, `valid:bool` |
| `QueueHealthMetric` | `queueId:uint16`, `capacity:uint32`, `depth:uint32`, `highWaterMark:uint32`, `dropCount:uint32`, `payloadSizeBytes:uint32`, `valid:bool` |

Unresolved: metric registry IDs, placeholder capacities, meaning/validity of
metrics, nested enum encodings, and count/array serialization rules.

## Existing format use and approved scope

- No production/runtime code currently calls the generic serializer functions;
  the native unit tests exercise the EventRecord v2 codec.
- The dedicated `CsvLogger` currently writes its own six-column sample CSV;
  `EventLogger` separately writes event CSV. Neither calls the generic
  serializers, and neither is in scope for this task.
- The ICD declares JSON Lines canonical for logical `EventRecord` v1, but the
  C++/ICD field mismatch and unresolved catalog, limits, and CRC details block
  treating the current C++ struct as a lossless v1 representation.
- No current caller or approved layout requires binary serialization. The ICD
  explicitly prohibits raw struct dumps and requires a separately approved
  binary contract before introducing binary.

### EventRecord JSONL v2 wire schema

`record_type` is the envelope discriminator and is encoded as `event_record`.
`schema_version` is `2`; input model records with another `schemaVersion` are
rejected as unsupported. Following those two envelope fields, the stable JSON
key sequence is:

`event_id`, `event_sequence_no`, `boot_id`, `timestamp_ms`, `event_name`,
`event_catalog_id`, `severity`, `source`, `status_code`, `error_code`,
`error_code_valid`, `fault_code`, `fault_code_valid`, `risk_state`,
`health_state`, `manifest_id`, `manifest_id_valid`, `firmware_version`,
`model_version`, `model_version_valid`, `feature_schema_hash_sha256`,
`feature_schema_hash_valid`, `config_version`, `config_version_valid`,
`calibration_version`, `calibration_version_valid`, `related_sequence_no`,
`related_sequence_no_valid`, `message`, `value_type`, `value`, `value_text`,
`unit`, `value_valid`, `dropped_event_count`, `crc32`, `crc32_valid`, `valid`,
`sensor_id`, `sensor_id_valid`, `state_before`, `state_before_valid`,
`state_after`, `state_after_valid`.

Enum token mapping:

| Enum | JSON strings |
|---|---|
| `EventSeverity` | `DEBUG`, `INFO`, `NOTICE`, `WARNING`, `ERROR`, `CRITICAL` (`Warn` alias uses `WARNING`) |
| `RiskState` | `STARTUP`, `NORMAL`, `MONITOR`, `DANGER_TEST`, `UNKNOWN` (`Safe` and `HighRiskTest` aliases use their canonical values) |
| `HealthState` | `UNKNOWN`, `HEALTHY`, `DEGRADED`, `FAULT` |
| `EventRecord::ValueType` | `NONE`, `NUMERIC`, `TEXT` |
| `SensorId` | `UNKNOWN`, `IMU`, `SOIL_MOISTURE`, `RAIN`, `HX710B` |

Fixed 32-byte hashes are always encoded as 64 lowercase hexadecimal
characters; their validity booleans are preserved separately. Every C string
must contain a NUL within its declared capacity and valid UTF-8 before that
terminator. JSON escaping is applied to quotes, backslashes, and control
characters. A non-finite `value` may be encoded as `null` only while
`valueValid` is false; on decode it becomes a canonical quiet NaN. A finite
value is emitted with `%.9g`. The `crc32` numeric field and `crc32Valid` flag
are preserved as opaque data and are not calculated or verified against the
serialized record.

The JSONL writer emits exactly one object followed by LF. The reader accepts
one single-line object with an optional final LF or CRLF and rejects unknown,
missing, duplicate, reordered, multiline, or trailing fields/bytes. It decodes
transactionally: the destination record is unchanged on failure.

The v2 mapping preserves every logical `EventRecord` member without changing
its struct. Character arrays are C strings: bytes after the first NUL are not
part of the serialized value and are zeroed when decoding. A null non-finite
float decodes to canonical quiet NaN, not the original NaN payload or infinity
sign. The ICD v1 logical schema remains unchanged; a v1 record is not accepted
by the v2 codec. CSV and binary remain unimplemented. The other six records
remain inventory-only until a concrete consumer and per-record schema/version
are approved.
