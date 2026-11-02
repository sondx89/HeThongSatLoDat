# Layered Architecture Rules

**Document revision:** 1.0

**Updated:** 2026-11-02
**Status:** Implementation inventory; not a system-requirement approval.

## Layer responsibilities and implementation status

| Layer | Responsibility | Current status |
|---|---|---|
| **BSP** | Board identity, safe GPIO setup, bus initialization, monotonic time and watchdog adaptation. | Implemented with configured/unconfigured checks. Board identity, pin/electrical review, hardware watchdog and power monitor remain partly or wholly unconfigured. |
| **Drivers** | Hardware-specific adapters; bounded transactions and explicit status. | Concrete sensor driver classes and mock adapters exist. Exact board/sensor configuration and target verification remain open. |
| **Services** | Calibration, data quality, preprocessing and feature-window/schema preparation. | Data-quality and feature-pipeline behavior is implemented and host-tested. Calibration propagates validity/raw data but does not apply calibration coefficients and does not report a loaded calibration. |
| **Managers** | Application orchestration, sensor scheduling, configuration validation/persistence policy, risk policy, recovery classification and output command policy. | SensorManager and RiskManager are implemented. ConfigManager validation and injected two-slot protocol, AppController lifecycle protocol, pure RecoveryManager classification, and pure OutputController policy have host tests. Production adapters/activation and physical output/recovery actions remain blocked. |
| **Models** | Plain data contracts passed between layers/tasks. | Fixed-storage records and validity/provenance fields exist. The serialization schema supports EventRecord JSONL v2 only; other codecs are unsupported. |
| **Diagnostics** | Health metrics, watchdog supervision, bounded serial diagnostics and event enqueue facade. | Implemented modules exist. Hardware watchdog operations remain `NotConfigured`; diagnostics/health are not composed into a released firmware runtime. |
| **Storage** | Storage abstraction and record contracts; producers must avoid blocking acquisition. | `IStorage` and `StorageManager` lifecycle are implemented and fake-backend tested. `CsvLogger` is a separate interface with its own filesystem adapter. No physical backend, finite hardware flush guarantee, or durability claim is verified. |
| **AI** | Feature-schema validation and inference abstraction. | SchemaRegistry and fail-closed adapter logic are implemented. No actual Edge Impulse deployment export is configured; production model compatibility/inference is blocked. |
| **Tasks/queues** | Typed bounded data transfer and task execution framework. | QueueRegistry, task classes and TaskFramework have implementations. Runtime queue/task sizing is unconfigured; TaskFramework does not expose stop/drain APIs required for AppController integration. |

## Dependency rules

1. `Models` must not depend on other project modules.
2. `BSP` must not depend on Drivers, Services, Managers, AI, Diagnostics or
   Storage policy.
3. `Drivers` may depend on BSP and Models only.
4. `Services` may depend on Models and lower-level interfaces; they must not
   access GPIO or include concrete hardware drivers.
5. `Managers` coordinate modules and may depend on service/driver interfaces,
   Models, Diagnostics and AI abstractions.
6. `AI` depends on Models and its schema/interface; generated Edge Impulse
   headers are isolated in the adapter.
7. `Diagnostics` and `Storage` must not call back into RiskManager or
   SensorManager. Use interfaces/queues.
8. Cross-task data is immutable or transferred via queues/snapshots. Queue
   depths, timeouts, task priorities and stack sizes remain TBD.
9. A failed/invalid/stale sensor read or inference result is not a valid zero
   and must not imply NORMAL.
10. `RiskState` and `HealthState` remain separate.
11. GPIO assignments live in `BSP/BoardPins.h`; drivers must not define pin
    numbers.
12. Unverified board identity, pins, bus/ADC settings, electrical levels and
    watchdog timing remain unconfigured and cause explicit status, never a
    guessed hardware setting.

## Composition and safety gates

`AppController` is a dependency-injected orchestration protocol with ordered
startup, reverse cleanup, an inference gate, bounded-port contracts, health
checks, and shutdown. Its host tests validate sequencing only. There is no
concrete adapter connecting the lifecycle port to BSP, diagnostics,
ConfigManager, StorageManager, sensors, calibration, schema/model, risk,
outputs, tasks, and self-check. Specifically, `TaskFramework` has no stop or
drain operation, StorageManager's interface has no timeout-bearing flush, and
the current production `main.cpp` does not use AppController. Do not describe
the application as fully composed or the controller's adapter contract as
enforced on hardware.

The desired data lifecycle is:
`SensorFrame -> ProcessedFrame -> FeatureVector -> ModelOutput -> RiskAssessment -> CsvRecord`.
Do not convert invalid/stale/missing data into zero or NORMAL. Risk state and
health state are independent. A regression output is not a landslide
probability unless separately validated.

Only `AI/EdgeImpulseAdapter` may include generated Edge Impulse headers when an
approved deployment is supplied. The actual generated export is absent.
`FeaturePipeline` and `SchemaRegistry` reject unapproved/incompatible schema
metadata; production inference remains blocked.

## Requirement conflicts and change control

No DOP, DSD, released FSS, ICD, LLD, TCS, or GTIS source document was found in
this repository during the 2026-11-02 documentation audit. This document does
not replace or modify those requirements. Any conflict between the code and
the authoritative controlled documents must be raised as a change request
before behavior or system requirements are changed. See
`docs/REQUIREMENT_TRACEABILITY.md`.

## Change history

| Revision | Date | Change |
|---|---|---|
| 1.0 | 2026-11-02 | Replaced stale blanket “nothing implemented” claims with a status-based layer inventory and explicit AppController integration gaps. |
