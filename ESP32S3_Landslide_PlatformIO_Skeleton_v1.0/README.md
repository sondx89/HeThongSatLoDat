# ESP32-S3 Landslide Monitoring — PlatformIO Skeleton

**Status:** Skeleton / DRAFT. MPU9250, soil-moisture, rain, and raw HX710B
acquisition drivers plus a synthetic sensor environment exist, but hardware
configuration and validation remain open; storage and inference logic are
incomplete, and risk policy thresholds/integration still require approval.

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

## Data contracts

The fixed-storage C++17 contracts are under `include/project/Models/`; include
`project/Models/DataContract.h` for the public contract set. Records are trivially copyable
for bounded FreeRTOS queue transfer and keep monotonic milliseconds, validity, quality,
raw/calibrated values, and health/risk state separate.

The document set in `../Document/` remains authoritative. Model dimensions, feature ordering,
calibration units, risk-state reconciliation, event string limits, and serializer wire formats
are not frozen. The serialization functions therefore return `NotImplemented` until those
items are approved; do not persist a C++ struct memory dump.

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

## Dataset replay

`project/Replay/ReplayEngine.h` streams six-column CSV logs through a bounded
reader and emits `FeatureVector` windows using manifest-supplied, approved
column-to-index mappings. Since the current log omits FSS/schema and capture
provenance metadata, replay requires externally verified dataset/schema hashes,
model metadata and layout; defaults fail closed. Empty mapped fields, malformed
rows and timestamp discontinuities are rejected without imputation. See
[docs/REPLAY_ENGINE.md](docs/REPLAY_ENGINE.md).

## Mock inference runtime

`project/AI/MockModelRuntime.h` implements the inference interface so the task
and queue path can run before an Edge Impulse export exists. It validates
basic `FeatureVector` integrity but deliberately emits `ModelError` with no
prediction for valid inputs: target/output semantics are still DRAFT, so it
does not fabricate a neutral value or risk score. See
[docs/MOCK_MODEL_RUNTIME.md](docs/MOCK_MODEL_RUNTIME.md).

## Risk manager

`project/Managers/RiskManager.h` fuses a configured model score, an explicitly
validated tilt deviation, and data quality into `RiskAssessment`. Its default
configuration fails closed to `UNKNOWN`; thresholds and hysteresis must come
from an approved, hash-verified risk configuration. The requested `SAFE` and
`HIGH_RISK_TEST` names are value-compatible aliases for the existing
`Normal` and `DangerTest` contract states. See
[docs/RISK_MANAGER.md](docs/RISK_MANAGER.md).

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
`src/Drivers/RainSensorDriver.cpp` read the configured rain ADC pin through the
shared `IAdcReader`, maintain a bounded moving average, and map two verified
calibration points to `rainWetnessRelativePct`. ADC range, wetness semantics,
filter size, and calibration must be provided by the integration layer. The
result is a relative wetness indicator, not rainfall amount or intensity.
Driver-local quality flags report ADC/calibration validity, filter warm-up, and
endpoint clamping. See [docs/RAIN_SENSOR_DRIVER.md](docs/RAIN_SENSOR_DRIVER.md).

## HX710B driver

`project/Drivers/HX710BDriver.h` and `src/Drivers/HX710BDriver.cpp` provide a
bounded GPIO transport and raw-count `ISensor` driver. GPIO mapping and all
protocol timing/format choices are supplied explicitly and rejected unless
marked verified against the selected device datasheet. The optional calibration
factor and its factor-scaled raw count carry no physical unit; the driver never
sets `hx710bCalibratedValue` or labels data as pressure/load/stress. Until a
transducer and measurand are approved, use raw values and diagnostics only.
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

This skeleton still contains declarations and placeholder translation units in other
subsystems. A successful compile only checks the project/toolchain setup, not hardware
operation or architecture acceptance.
