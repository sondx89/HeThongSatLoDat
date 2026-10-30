# CsvLogger

`project/Storage/CsvLogger.h` writes sample rows with this exact header:

```csv
timestamp,tilt,soil,rain,stress,risk
```

Each record is a fixed-size `CsvRecord`; the storage layer does not guess which
model field is meant by `tilt`, `soil`, `rain`, or `stress`.

This is the compact six-column format requested for this logger. The authoritative
system CSV proposal also calls for boot/sequence IDs and firmware/model/
calibration metadata. They are intentionally not appended to this header; decide
whether to add a companion metadata file or adopt the wider schema before using
these rows as the only production/audit log.

The `esp32-s3-mock` serial stream is a separate raw diagnostic format with its
own header and an explicit `dataset_eligible=false` field. It is not emitted by
`CsvLogger`, is not the DSD raw dataset contract, and must not be merged with or
treated as this six-column operational log.

## Column contract

| Column | Representation |
|---|---|
| `timestamp` | Monotonic uptime in milliseconds, or empty when unavailable. It is not UTC. |
| `tilt` | Finite numeric value when `tiltValid`; otherwise empty. The integration must choose the approved tilt field. |
| `soil` | Finite numeric value when `soilValid`; otherwise empty. The integration must provide the approved calibrated soil value. |
| `rain` | Finite numeric value when `rainValid`; otherwise empty. The integration must distinguish relative wetness from rainfall amount/rate. |
| `stress` | Finite model output value when `stressValid`; no probability/unit/meaning is assumed. |
| `risk` | Caller-supplied approved risk token when `riskValid`; otherwise empty. The enum-to-token mapping is deliberately not fixed here. |

Non-finite values marked valid and malformed risk tokens are rejected. The
serializer emits only numbers and a restricted risk token, so no free-form CSV
escaping is needed for these six typed fields. The risk token must be a
NUL-terminated ASCII string of letters, digits, `_`, or `-` and fit the fixed
record buffer. The integration owns conversion from `RiskState` to the approved
token. Floats use `%.9g`.

## Rotation and recovery

- `CsvLoggerConfig::maximumFileBytes` is required; rotation occurs before a
  record would exceed the configured size.
- `filePrefix` is restricted to ASCII letters, digits, `_`, and `-`; files use
  zero-padded monotonically increasing indexes.
- Files are created exclusively. Existing files are never overwritten or
  automatically deleted; retention/cleanup is an external backup policy.
- On startup, the logger verifies the fixed header and scans the latest file
  for complete six-field rows. A partial or malformed tail is truncated to the
  last valid newline, then flushed. If the newest file has a damaged header, it
  is preserved and logging starts in the next numbered file.
- Each row is appended as one operation and flushed. A partial write or failed
  flush returns an error and forces reinitialization/recovery before the next
  write. Status codes also report mutex unavailability, invalid configuration,
  full storage, malformed records, and exhausted file indexes.

The `ICsvFileSystem` adapter must implement bounded filesystem operations,
report actual bytes written/read, create files exclusively, and provide
`truncate` and `flush`. The project documents leave the SD module, bus,
filesystem, rotation budget, flush semantics, and durability point TBD. The
logger therefore detects and repairs a torn CSV tail but does not claim that a
successful `flush()` guarantees persistence across sudden power loss; that
requires verification on the selected card/filesystem/driver. Filesystem-level
damage is reported as an error rather than silently treated as recovered.

## Thread and task ownership

Logger methods are protected by a static FreeRTOS mutex on ESP32 (no mutex heap
allocation). The mutex is held only around bounded storage calls; the selected
adapter must therefore ensure those calls terminate. Route physical writes
through TaskLogger/T-LOG, not sensor or inference tasks. A writer adapter can
translate non-`Ok` `CsvLoggerStatus` values into the existing task logger's
explicit handled/unhandled result.

## Integration example

```cpp
project::storage::CsvLoggerConfig config{};
config.maximumFileBytes = approvedMaximumFileBytes;
project::storage::CsvLogger logger(fileSystem, config);

const auto startup = logger.begin();
if (startup != project::storage::CsvLoggerStatus::Ok &&
    startup != project::storage::CsvLoggerStatus::Recovered) {
    // Report storage failure through the approved diagnostics path.
}

project::storage::CsvRecord record{};
record.timestampMs = uptimeMs;
record.timestampValid = true;
record.tilt = approvedTilt;
record.tiltValid = tiltIsValid;
record.soil = approvedSoil;
record.soilValid = soilIsValid;
record.rain = approvedRain;
record.rainValid = rainIsValid;
record.stress = modelOutputValue;
record.stressValid = modelOutputIsValid;
copyApprovedRiskToken(riskAssessment, record.risk);
record.riskValid = riskAssessment.valid;
const auto writeStatus = logger.appendRecord(record);
```

`maximumFileBytes`, value mapping, required validity policy, and whether a given
`RiskState` label is an approved external representation must be set by the
integration from reviewed project decisions.
