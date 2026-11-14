# ESP32-S3 Landslide Monitoring — PlatformIO Skeleton

**Document revision:** 1.1

**Updated:** 2026-11-02

**Status:** Development skeleton / not release-ready. The repository contains
implemented drivers, managers, task/queue infrastructure, diagnostics, CSV
logging, and several host-tested pure policies. Configuration, physical
hardware, calibration, model/FSS, target runtime composition, and safety
acceptance are not complete. Host test success is not target or product
acceptance.

**Documentation audit:** 2026-11-02. This update corrects implementation
status and records traceability/release gates; it does not revise DOP, DSD,
FSS, ICD, LLD, TCS, or GTIS requirements.

## Fixed baseline
- MCU family: ESP32-S3
- Framework: Arduino
- Build system: PlatformIO
- Language standard: C++17
- IMU: MPU9250
- MPU9250 host interface: I²C

## Must remain TBD until verified
- Exact ESP32-S3 board/variant and PlatformIO board ID
- SDA/SCL GPIOs, I²C address and bus frequency
- MPU9250 module identity, register setup, axis/mounting transform, ranges, ODR, filter and calibration
- Tilt feature axes, reference pose, formula and filtering method
- Soil moisture and YL-83 output interface/calibration
- HX710B transducer/measurand and calibration
- SD/storage hardware and interface
- Edge Impulse export, model input dimension/order, target semantics and model artifact
- Task periods, queue lengths, stack sizes, timeouts and memory budgets

## Architecture / dependency direction

`BSP -> Drivers -> Services -> Managers/App orchestration -> AI / risk decisions`
with `Models` carrying data contracts, `Diagnostics` exposing health/events, and `Storage`
behind an interface. In concrete code, higher-level modules may depend on lower-level
interfaces and data models; lower-level modules must not include Managers, AI, or application
policy. Keep cross-task data immutable or transfer it through bounded queues/snapshots.

The main data lifecycle follows the design baseline:
`SensorFrame -> ProcessedFrame -> FeatureVector -> ModelOutput -> RiskAssessment -> CsvRecord`.

Only `AI/EdgeImpulseAdapter` may include generated Edge Impulse headers once those are added.
Do not convert invalid/stale/missing data into zero or NORMAL. Risk state and health state are
independent. A regression output is not a landslide probability unless separately validated.

## Production startup gate

The non-mock `setup()` performs early BSP/configuration/safe-bus checks and
remains fail-closed when production configuration is unavailable; it does not
start application tasks. `AppController` now implements and host-tests a
dependency-injected lifecycle protocol, but no concrete production lifecycle
adapter or composition wires it to the firmware modules. The controller's
inference gate and bounded-operation guarantees therefore describe its port
contract, not verified runtime enforcement. Do not treat this skeleton as a
deployable firmware image.

## Data contracts

The fixed-storage C++17 contracts are under `include/project/Models/`; include
`project/Models/DataContract.h` for the public contract set. Records are trivially copyable
for bounded FreeRTOS queue transfer and keep monotonic milliseconds, validity, quality,
raw/calibrated values, and health/risk state separate.

`RiskState`, `HealthState`, and `EventSeverity` numeric codes now match the ICD registry.
Internal aliases (`Safe`/`Normal`, `HighRiskTest`/`DangerTest`, `Warning`/`Warn`) do not add
serialized states. Fault-flag bits and production model/feature metadata remain TBD. Model
dimensions, feature ordering, and calibration units are not released. The
general serializer supports only the approved EventRecord JSONL v2 codec;
other record/format operations return an explicit unsupported/not-implemented
status. Do not persist a C++ struct memory dump. See
[docs/SERIALIZATION_SCHEMA.md](docs/SERIALIZATION_SCHEMA.md).

`ModelOutput` carries source boot/sequence, feature-window bounds/count and schema identity.
`RiskManager` rejects mismatched provenance; `HealthState::FAULT` suppresses model-derived
risk scores and yields `UNKNOWN` unless an independently approved severe sensor rule requires
`DANGER_TEST`.

## Data quality service

`project/Services/DataQualityService.h` classifies each known sensor channel as
`VALID`, `STALE`, `MISSING`, `OUT_OF_RANGE`, or `NON_FINITE`, and can produce a
`ProcessedFrame` without converting rejected values to zero. The integration
must explicitly configure approved per-sensor freshness limits, any verified
channel ranges, and at least one model-required channel. There are deliberately
no defaults for those TBD policy values. Only channels represented by the current
`ProcessedFrame` may be marked required. Tilt roll/pitch, rainfall rate, and the
HX710B physical value remain unavailable until their definitions are approved.
See [docs/DATA_QUALITY_SERVICE.md](docs/DATA_QUALITY_SERVICE.md).

## Feature pipeline

`project/Services/FeaturePipeline.h` implements a fixed-capacity sliding-window
pipeline based on FSS v1.1. FSS remains DRAFT with null indexes, no eligible
features, and no released Edge Impulse input contract, so default configuration
fails closed and cannot produce valid inference input. The reviewed integration
must supply schema version/ID/hash, approved contiguous feature indexes, model
metadata, flat layout, and window/stride settings. The pipeline supports only
the FSS acceleration magnitude and candidate tilt formulae when their units and
verification gates are configured. Raw sensor channels unavailable in
`ProcessedFrame` are rejected, and HX710B remains excluded until its transducer
and feature are approved. See [docs/FEATURE_PIPELINE.md](docs/FEATURE_PIPELINE.md).

## Host-first AI/data pipeline

Until hardware and approved model artifacts are available, development is
host-first: deterministic synthetic raw observations, quality classification,
raw-only calibration, bounded DSD raw-core CSV export/validation, and replay
that remains fail-closed when physical calibration or model schemas are
unavailable. Synthetic records carry `SYNTHETIC`/`GT5`; they are not physical
sensor evidence, independent labels, field accuracy, or safety evidence.

`SyntheticSensorGenerator` exposes stable, trend, step, oscillation,
impulse/noise, saturation, dropout, stale, out-of-order, gap, reboot,
calibration-change, and mixed-validity scenarios. Its API is exercised by
native tests; there is no standalone synthetic dataset CLI at present.
`DsdRawSampleCsvExporter` writes the exact DSD v1.2 raw-core header to a
caller-owned bounded buffer and returns a typed manifest with uniform
provenance, channel presence, byte/row counts, and SHA-256.
`DsdRawSampleCsvParser` and `DatasetValidator` validate the raw archive and
reject malformed, inconsistent, duplicated, or discontinuous records.
Raw-only replay cannot make physical features or inference eligible.

There are two distinct replay surfaces:

- `project/Replay/ReplayEngine.h` replays the legacy operational six-column
  `timestamp,tilt,soil,rain,stress,risk` CSV through its own schema/mapping
  gates. It cannot recover original sample identity, raw channels, or
  per-channel quality.
- The DSD raw-core parser/validator handles the separate dataset artifact.
  Host integration replays those raw rows through the raw-only quality and
  calibration path. DSD v1.2 core does not include per-channel acquisition
  timestamps, so replay cannot reconstruct each channel's stale age.

Document versions and artifact schema versions are distinct. The 36-field
DSD v1.2 core order is preserved; uniform origin/evidence provenance stays in
the manifest rather than being inserted into that header. The exporter is
host-only; annotation and row-level evaluation exporters are not implemented.
Bounded calibrated-sample, window, and feature exporters are host-only; no
production physical calibration is configured.
Operational logger CSV and mock serial diagnostics are not DSD training
exports. See
[docs/CSV_DATASET_CONTRACT.md](docs/CSV_DATASET_CONTRACT.md),
[docs/SYNTHETIC_DATA_POLICY.md](docs/SYNTHETIC_DATA_POLICY.md), and
[docs/AI_PIPELINE_HOST_TESTING.md](docs/AI_PIPELINE_HOST_TESTING.md).

The existing `ReplayEngine` for operational telemetry is separately
documented in [docs/REPLAY_ENGINE.md](docs/REPLAY_ENGINE.md).

## Mock inference runtime

`project/AI/MockInferenceEngine.h` implements the metadata-bearing inference
interface only under `PROJECT_HOST_DATA_TOOLS=1` and `PROJECT_TEST_ONLY=1`.
Tests supply an explicit schema
and finite raw output vector; the mock copies those values deterministically
without assigning a target meaning or risk category. Production metadata,
target semantics, and model integration remain gated. See
[docs/MOCK_MODEL_RUNTIME.md](docs/MOCK_MODEL_RUNTIME.md).

## Edge Impulse adapter

`project/AI/EdgeImpulseAdapter.h` implements `IInferenceEngine` and calls the
generated Arduino `run_classifier()` API when an approved Edge Impulse export
header is configured. Without that export, initialization fails closed. Input
dimension, schema and output mapping must be populated from verified metadata;
classification values are not interpreted as risk scores. See
[docs/EDGE_IMPULSE_ADAPTER.md](docs/EDGE_IMPULSE_ADAPTER.md).

## Host evaluation

`project/Dataset/EvaluationRunner.h` generates bounded quality/provenance and
feature-distribution reports. Regression metrics require an explicitly
approved target contract and regression-output approval; synthetic GT5 proxy
metrics remain separately labeled. See
[docs/EVALUATION_CONTRACT.md](docs/EVALUATION_CONTRACT.md).

## One-click mock upload and CSV capture

On Windows, run `build_project.bat` to run native tests, build and upload the
synthetic ESP32-S3 mock firmware, then open its 115200-baud serial monitor.
The serial stream is a diagnostic raw CSV and is explicitly not eligible for
Edge Impulse training. The six-column `CsvLogger` output remains a separate
runtime log contract. See [docs/MOCK_SERIAL_CAPTURE.md](docs/MOCK_SERIAL_CAPTURE.md)
and [docs/CSV_LOGGER.md](docs/CSV_LOGGER.md).

## Unit tests

The Unity native test suite covers SensorManager, DataQualityService,
FeaturePipeline, RiskManager, ReplayEngine, dataset parsing/validation,
annotations, splitting, and evaluation without physical hardware. From the
project directory on Windows, with PlatformIO and MSYS2 UCRT64 compiler
installed, run:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
pio test -e native-unit-tests
```

See
[docs/UNITY_UNIT_TESTS.md](docs/UNITY_UNIT_TESTS.md).

## Integration tests

The Unity native integration suite covers synthetic raw export → manifest
validation → DSD raw parser replay → quality/raw-only calibration, including
GT5 provenance and closed production inference/risk gates. It also retains a
separate legacy Sensor → Feature → test-only Model → Risk → operational CSV
logger wiring test. Run from the project directory:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
pio test -e native-integration-tests
```

Fixture thresholds and mappings are not production approvals. See
[docs/INTEGRATION_TESTS.md](docs/INTEGRATION_TESTS.md).

For exact current validation commands, build blockers, artifact status, and
evidence limitations, see
[docs/BASELINE_TEST_REPORT.md](docs/BASELINE_TEST_REPORT.md). The host-first
workflow description is in
[docs/AI_PIPELINE_HOST_TESTING.md](docs/AI_PIPELINE_HOST_TESTING.md).

## Risk manager

`project/Managers/RiskManager.h` fuses a configured model score, an explicitly
validated tilt deviation, and data quality into `RiskAssessment`. Its default
configuration fails closed to `UNKNOWN`; thresholds and hysteresis must come
from an approved, hash-verified risk configuration. The requested `SAFE` and
`HIGH_RISK_TEST` names are value-compatible aliases for the existing
`Normal` and `DangerTest` contract states. See
[docs/RISK_MANAGER.md](docs/RISK_MANAGER.md).

## Serial diagnostics CLI

`project/Diagnostics/SerialDiagnostics.h` parses bounded, read-only
`diag status`, `diag health`, `diag sensor`, `diag memory`, and `diag risk`
commands. The UART-owner task supplies serial and snapshot adapters; responses
are rate-limited JSON Lines, and missing values stay explicit. See
[docs/SERIAL_DIAGNOSTICS.md](docs/SERIAL_DIAGNOSTICS.md).

## Board Support Package

The BSP is under `include/project/BSP/` and `src/BSP/`. `BoardPins.h` is the single
GPIO mapping table; `BoardConfig.h` holds compile-time bus, ADC, electrical and capability
configuration; `BoardInfo.h` and `BoardVersion.h` describe the board without inventing an
unconfirmed variant or revision. Values marked TBD stay unset, and BSP hardware operations
return an explicit status until the board schematic/BOM and electrical review supply them.
Drivers must not hard-code GPIO assignments.

## MPU9250 driver

`project/Drivers/MPU9250Driver.h` and `src/Drivers/MPU9250Driver.cpp` provide
an I2C `ISensor` implementation using an injected Arduino `TwoWire` bus. The
driver validates required configuration rather than selecting the unresolved
address, register/range setup, scale factors, or calibration reference. Initialize
the bus through the BSP before passing it to the driver; the application must
populate all settings from hardware review and board configuration.

The driver reads raw accelerometer/gyroscope counts, calibrates offsets from a
stationary reference capture, and emits physical acceleration (g) and angular
rate (degrees/s). The requested scalar tilt field is present in `SensorFrame`,
but remains invalid and the driver reports `TiltCalculationPending`: the
authoritative documents specify roll/pitch features while their axes, formula,
reference pose and filter remain TBD. Do not consume the placeholder as a
measurement. See [docs/MPU9250_DRIVER.md](docs/MPU9250_DRIVER.md) for the class
diagram, configuration contract and remaining integration work.

## Soil moisture driver

`project/Drivers/SoilMoistureDriver.h` and
`src/Drivers/SoilMoistureDriver.cpp` implement a bounded ADC acquisition and
fixed-capacity moving-average filter. The ADC pin comes from `BoardPins`, and
`ArduinoAdcReader` uses Arduino `analogRead()`. ADC resolution, attenuation,
input range and hardware calibration remain externally configured and must be
verified; the driver rejects an unverified configuration. Moisture percentage
is a piecewise-linear mapping between two caller-supplied calibration points,
clamped to their percentage range. It is a calibrated relative percentage, not
volumetric water content. See [docs/SOIL_MOISTURE_DRIVER.md](docs/SOIL_MOISTURE_DRIVER.md)
for the contract and calibration procedure.

## Rain sensor driver

`project/Drivers/RainSensorDriver.h` and
`src/Drivers/RainSensorDriver.cpp` read AO through the shared `IAdcReader` and
optionally read the independent active-low DO pin. Each AO value averages
eight cooperative conversions and maps verified dry/wet calibration endpoints
to `rainWetnessRelativePct`. A timestamped, time-weighted trailing 120-second
integral is reported as equivalent wet-seconds only with full configured-gap
coverage. Pins, ADC settings, wetness semantics, calibration and sampling
limits remain integration inputs; the DOCX GPIOs/endpoints are not defaults.
Wetness and accumulation are proxies, not rainfall amount or mm/h. See
[docs/RAIN_SENSOR_DRIVER.md](docs/RAIN_SENSOR_DRIVER.md).

## HX710B driver

`project/Drivers/HX710BDriver.h` and `src/Drivers/HX710BDriver.cpp` provide a
bounded GPIO transport and raw-count `ISensor` driver. GPIO mapping and all
protocol timing/format choices are supplied explicitly and rejected unless
marked verified against the selected device datasheet. The optional calibration
factor and its factor-scaled raw count carry no physical unit; the driver never
sets `hx710bCalibratedValue` or labels data as pressure/load/stress. Until a
transducer and measurand are approved, use raw values and diagnostics only.
The optional `CalibrationService` pressure path requires an explicitly
approved transducer/range/version profile and never installs the DOCX demo
coefficients. The optional 15-second baseline requires at least three valid
calibrated samples and an explicit freshness/gap limit; no baseline or risk
threshold is enabled by default.
See [docs/HX710B_DRIVER.md](docs/HX710B_DRIVER.md).

## FreeRTOS queues

`project/Queues/QueueRegistry.h` provides a thread-safe singleton that owns the six
FreeRTOS queue handles. Producers and consumers use typed send/receive methods and never
own or delete queue handles. Queue payloads are copied by value and must remain trivially
copyable.

Queue depths are intentionally zero/unconfigured in `QueueDepths`; call `initialize()` with
measured, reviewed values before creating application tasks. Queue creation uses FreeRTOS
dynamic allocation and reports allocation failure without exposing a partial registry.
Choose finite wait times; indefinite queue waits are rejected. Send failures increment
per-queue drop counters, while `getStats()` provides current depth, capacity, observed
high-water mark, payload size and drops.
Drop/coalescing policy, including preservation of critical events and risk transitions,
remains the responsibility of the owning producer/consumer until those policies are approved.

The queue payload mapping is: `Q_SENSOR` / `SensorFrame`, `Q_FEATURE` / `FeatureVector`,
`Q_MODEL` / `ModelOutput`, `Q_RISK` / `RiskAssessment`, `Q_EVENT` / `EventRecord`, and
`Q_LOG` / tagged `LogRecord`. The log catalog currently defines only the Event variant;
additional variants and final queue depths remain TODOs from the system documents.

## Event logger

`project/Diagnostics/EventLogger.h` implements a multi-producer `Q_EVENT`
facade. `enqueueWithStatus()` assigns a per-boot numeric occurrence ID and
sequence number (separate from the stable `eventCatalogId` such as
`EVT-SYS-0004`), then copies the fixed-size event into the queue without
blocking. Queue-full and uninitialized-queue failures are returned explicitly;
QueueRegistry tracks drops. Counters stop rather than wrap. Producers supply
`timestampMs` from the monotonic clock at event occurrence; EventLogger preserves
it and never substitutes wall-clock time. Numeric IDs/sequences can have gaps
when queue submission fails; each accepted event snapshots the queue's current
drop count.

The single Q_EVENT consumer formats the complete EventRecord v1 CSV row into
caller-owned bounded storage. `writeOutputs()` sends the same row to the CSV
sink and to a Serial sink, attempts both destinations, and reports partial
failures. The CSV header is available from `writeCsvHeader()`. Fields are
serialized individually, text is CSV-escaped, numeric values use float32
round-trip precision, and invalid/non-finite values are rejected rather than
written as NaN/Inf. No struct memory is persisted.

The application-provided `IEventOutput` implementation must keep output
ownership compliant: CSV writes belong to TaskLogger/T-LOG, while
`writeSerialCsv()` must route through the single TaskDiagnostics/T-DIAG UART
owner instead of writing `Serial` concurrently from another task. The serial
sink and concrete CSV storage remain injected because UART setup and storage
hardware are not finalized in the documents. The task's `ITaskLogWriter`
implementation should call `writeOutputs()` after receiving an event from
Q_EVENT and propagate any non-`Ok` status as an explicit handled/unhandled
write result.

## Periodic CSV logger

`project/Storage/CsvLogger.h` implements the requested six-column sample CSV:
`timestamp,tilt,soil,rain,stress,risk`. Invalid measurements serialize as empty
fields, not zero. The timestamp is monotonic uptime milliseconds; `stress` is
an untyped model output value and its unit/meaning remains the integration's
responsibility. The risk token is caller-supplied so the logger does not freeze
the unresolved enum-to-text mapping. A fixed-size `CsvRecord` keeps the logger
independent of unapproved model feature mappings.

Rotation is size-based, with a caller-provided maximum file size and names such
as `data_000000.csv`; old files are never deleted or overwritten. Startup
validates the header and each complete row, truncates a malformed/incomplete
tail to the last complete row, or starts a new numbered file if the latest
header is corrupt. Each header and record is flushed. The storage adapter must
implement `ICsvFileSystem`, including exclusive create, read, truncate and
flush. Its media/filesystem and the actual durability point remain TBD; a
successful flush is not claimed to survive power loss until verified on the
selected hardware. Logger operations are serialized with a static FreeRTOS
mutex on ESP32.

## FreeRTOS task framework

`project/Tasks/TaskFramework.h` and `ApplicationTasks.h` define the startup/heartbeat
framework and `TaskSensor`, `TaskFeature`, `TaskInference`, `TaskRisk`, `TaskLogger`,
`TaskDiagnostics`, and `TaskWatchdogSupervisor`. Each task's priority, stack size (in
FreeRTOS stack words), cycle/queue wait and heartbeat timeout must be supplied from
target measurements.
Unset values intentionally prevent task creation; do not copy illustrative values from
the documents as production settings.

`TaskFramework::startAll()` must be called from task context after the scheduler is
running. It validates every configuration and creates all tasks at a startup gate before
releasing any of them. QueueRegistry must already be initialized. The startup gate is
the only intentional indefinite task wait.
The framework uses `xTaskCreate`, so FreeRTOS allocates each task stack/TCB dynamically;
the total must be measured and reviewed against the board RAM budget. Tasks are
single-owner for their managers, inference engine, feature pipeline and storage sink.
TaskDiagnostics samples actual progress heartbeats, stack high-water marks and queue
depth/high-water/drop counts. Heartbeat does not advance on an idle timeout.
The watchdog task independently checks the five required pipeline/logger task
heartbeats. It latches the task fault, attempts to enqueue `EVT-SYS-0005` and
`EVT-SYS-0004`, then requests an ESP32-S3 restart. Event enqueue is nonblocking;
if Q_EVENT is full the reset still proceeds and the event may not be persisted.
The logger sink may report `Progress` when a storage failure was explicitly
recorded and handled, so a recognized storage fault alone does not force a reset.
The BSP hardware watchdog init/feed API remains TBD; this is not a substitute for
a configured hardware watchdog that can recover if the supervisor itself stalls.

Risk context acquisition, event catalog/deduplication, log persistence/encoding and
system health metric collection are injected interfaces because the existing subsystem
contracts do not fully specify those operations. Their required integration points and
the task sequence are shown in
[TASK_RUNTIME_FLOW.md](./docs/TASK_RUNTIME_FLOW.md). The framework does not feed a
hardware watchdog; the supervisor's BSP restart path is used for detected heartbeat
timeouts. Hardware watchdog initialization/feed remains an explicit BSP TODO.

## Mock sensor environment

`SensorManager` reads sensors sequentially in the required IMU → soil → rain →
HX710B order. Each slot has explicit initialization/read timeout, retry count
and optional backoff configuration; unconfigured timeout budgets prevent startup.
Timeouts are detected on return, so each concrete driver's blocking operation
must also have its own finite hardware timeout. A per-sensor failure does not
stop the remaining reads; `readSnapshot()` emits a frame with that channel's
status invalid and exposes saturating attempts/retry/timeout diagnostics.
See [docs/SENSOR_MANAGER.md](docs/SENSOR_MANAGER.md).

`project/Drivers/MockSensors.h` provides deterministic, fixed-storage mock
drivers for MPU9250 raw axes, soil raw count, rain raw count, and HX710B raw
count. `MockEnvironmentProfile::demo()` is a development fixture only: those
numbers are not hardware ranges, calibrated quantities, or risk thresholds.
Physical/calibrated channels and the schema-versioned `validMask` are left
unset; per-sensor status indicates whether a synthetic raw read succeeded.
`MockSensorBehavior::failEveryReads` can inject repeatable read failures.

`SensorManager` combines the four injected `ISensor` implementations into
timestamped `SensorFrame`s. Sampling period and boot ID are explicit inputs;
no production cadence or board ID is assumed.

Run the sensor acquisition simulation on a host without a board or attached
sensors with `pio run -e mock-native -t exec`. It compiles only the mock drivers,
SensorManager, and host runner, emits twelve deterministic frames, and verifies
that periodic rain-sensor failure injection reaches the frame status. To run
the same sensor-only demo on an ESP32-S3 without external sensor hardware, build
and upload `esp32-s3-mock`; the MCU board itself is still required.

This repository is still a firmware skeleton: other services and complete
application composition remain placeholders. These targets exercise the
sensor/mock acquisition boundary, not the full inference/risk/task firmware.
The mock classes can be injected into `TaskSensor`'s SensorManager in the
application composition when those remaining modules are implemented.

## Health monitoring

`project/Diagnostics/HealthMonitor.h` creates bounded `HealthSnapshot` data.
On the confirmed ESP32-S3 target, BSP samples internal 8-bit-capable heap
free/minimum/largest-block/total bytes; HealthMonitor adds monotonic uptime and
the platform reset-reason category. Task stack high-water values retain the
FreeRTOS unit of words, and queue metrics include current depth, capacity,
high-water mark, drops and payload size. No low-memory or stack safety threshold
is invented here.

Use the same `HealthMonitor` instance as the `WatchdogSupervisor` fault sink to
record `watchdogCount` and latch faulted task bits. The count is per-boot and
increments once for each distinct task heartbeat fault; `watchdogCountValid`
marks it as available. `captureSnapshot(sequenceNo, output)` returns an explicit
status and leaves no success-shaped partial snapshot when a required platform
or queue metric cannot be read. The monitor also implements
`ITaskHealthSource::collectSystemMetrics()` for `TaskDiagnostics`, which adds
task/queue arrays using the existing diagnostics flow. Heap figures are
ESP-IDF aggregate internal-heap statistics; external RAM-specific measurements
remain separate/TBD. The watchdog count is an in-RAM per-boot metric, not a
persistent count of watchdog-triggered resets; persistence and reset-count
semantics require an approved storage/reset policy.

## Build
1. Open this folder in VS Code.
2. Install PlatformIO IDE extension.
3. Confirm the physical board ID, then update `platformio.ini` if needed.
4. Run PlatformIO: **Build**.

The compile-only command is `pio run -e esp32-s3-devkitc-1`. In the latest
recorded run (2026-11-13), the native unit and integration suites passed
116/116 and 7/7 cases, and the ESP32-S3 compile/link completed successfully.
This Windows workspace's Japanese path caused a linker map-file failure when
building directly; the successful run used a temporary `subst Z:` drive alias.
The build is not a flash or physical hardware test. See
[docs/BASELINE_TEST_REPORT.md](docs/BASELINE_TEST_REPORT.md) for exact commands
and current evidence.

Several subsystems remain partial or blocked by missing configuration and
integration evidence. A successful compile only checks the project/toolchain
setup, not hardware operation or architecture acceptance.

## Change history

| Revision | Date | Change |
|---|---|---|
| 1.1 | 2026-11-02 | Reconciled implementation status and blockers with current source/test evidence; added traceability and release-gate references. |
