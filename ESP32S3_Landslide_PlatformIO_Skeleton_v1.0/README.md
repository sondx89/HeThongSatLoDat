# ESP32-S3 Landslide Monitoring — PlatformIO Skeleton

**Status:** Skeleton only / DRAFT. No sensor, storage, risk, or inference logic is implemented.

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

## Board Support Package

The BSP is under `include/project/BSP/` and `src/BSP/`. `BoardPins.h` is the single
GPIO mapping table; `BoardConfig.h` holds compile-time bus, ADC, electrical and capability
configuration; `BoardInfo.h` and `BoardVersion.h` describe the board without inventing an
unconfirmed variant or revision. Values marked TBD stay unset, and BSP hardware operations
return an explicit status until the board schematic/BOM and electrical review supply them.
Drivers must not hard-code GPIO assignments.

## FreeRTOS queues

`project/Queues/QueueRegistry.h` provides a thread-safe singleton that owns the six
FreeRTOS queue handles. Producers and consumers use typed send/receive methods and never
own or delete queue handles. Queue payloads are copied by value and must remain trivially
copyable.

Queue depths are intentionally zero/unconfigured in `QueueDepths`; call `initialize()` with
measured, reviewed values before creating application tasks. Queue creation uses FreeRTOS
dynamic allocation and reports allocation failure without exposing a partial registry.
Choose finite wait times; indefinite waits are rejected. Send failures increment per-queue
drop counters, while `getStats()` provides current depth, capacity, payload size and drops.
Drop/coalescing policy, including preservation of critical events and risk transitions,
remains the responsibility of the owning producer/consumer until those policies are approved.

The queue payload mapping is: `Q_SENSOR` / `SensorFrame`, `Q_FEATURE` / `FeatureVector`,
`Q_MODEL` / `ModelOutput`, `Q_RISK` / `RiskAssessment`, `Q_EVENT` / `EventRecord`, and
`Q_LOG` / tagged `LogRecord`. The log catalog currently defines only the Event variant;
additional variants and final queue depths remain TODOs from the system documents.

## Build
1. Open this folder in VS Code.
2. Install PlatformIO IDE extension.
3. Confirm the physical board ID, then update `platformio.ini` if needed.
4. Run PlatformIO: **Build**.

This skeleton still contains declarations and placeholder translation units in other
subsystems. A successful compile only checks the project/toolchain setup, not hardware
operation or architecture acceptance.
