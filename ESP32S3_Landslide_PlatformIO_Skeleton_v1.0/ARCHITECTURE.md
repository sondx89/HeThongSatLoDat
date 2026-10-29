# Layered Architecture Rules

## Layer responsibilities
- **BSP**: board identity, safe GPIO setup, bus initialization, monotonic time and watchdog adaptation.
- **Drivers**: hardware-specific adapters (`MPU9250` over I²C, plus TBD sensor adapters); bounded transactions and explicit status.
- **Services**: calibration, data quality, preprocessing and feature-window/schema preparation.
- **Managers**: application orchestration, sensor scheduling, configuration activation, risk-state policy, recovery and outputs.
- **Models**: plain data contracts passed between layers/tasks; no hardware or policy dependencies.
- **Diagnostics**: health metrics, watchdog supervision, bounded serial diagnostics and event enqueue facade.
- **Storage**: storage abstraction and record contracts; producers must enqueue/write asynchronously rather than block sensor acquisition.
- **AI**: feature schema validation and inference abstraction; only `EdgeImpulseAdapter` may later include generated Edge Impulse headers.

## Dependency rules
1. `Models` must not depend on other project modules.
2. `BSP` must not depend on Drivers, Services, Managers, AI, Diagnostics or Storage policy.
3. `Drivers` may depend on BSP and Models only.
4. `Services` may depend on Models and lower-level interfaces; it must not access GPIO or include concrete hardware drivers.
5. `Managers` coordinate modules and may depend on service/driver interfaces, Models, Diagnostics and AI abstractions.
6. `AI` depends on Models and its own schema/interface; generated Edge Impulse headers are isolated in the adapter.
7. `Diagnostics` and `Storage` must not call back into RiskManager or SensorManager. Use interfaces/queues.
8. Cross-task data is immutable or transferred via queues/snapshots. Queue depths, timeouts, task priorities and stack sizes remain TBD.
9. A failed/invalid/stale sensor read or inference result is not a valid zero and must not imply NORMAL.
10. RiskState and HealthState remain separate.

## Important
This package is a compile-oriented skeleton, not a finished firmware. Some public method declarations are intentionally undefined; do not call them until implementation is added. No sensor driver, task creation, queue, file system, model inference, calibration, or risk algorithm is implemented.
