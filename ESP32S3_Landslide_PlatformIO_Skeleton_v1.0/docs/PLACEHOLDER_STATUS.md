# Placeholder and Implementation Status

**Audit date:** 2026-10-30  
**Evidence:** Current headers, sources, callers, `platformio.ini` source filters,
and the two existing test suites. Absence of a caller/test means none was found
in the inspected project source/tests; it does not prove a future integration
cannot exist.

## Placeholder inventory

| Module / files | Current behavior in source | Callers | Tests | Dependencies and blockers |
|---|---|---|---|---|
| `Managers/AppController` — `include/project/Managers/AppController.h`, `src/Managers/AppController.cpp` | Header declares `begin()`, `runHealthChecks()`, and `requestShutdown()`; source is comment-only and defines none of them. | No caller found. Production `main.cpp` does not instantiate it. | None. | Only a forward declaration of BSP exists. Composition, lifecycle contract, approved startup configuration, dependencies, and tests are not implemented. |
| `Managers/ConfigManager` — `include/project/Managers/ConfigManager.h`, `src/Managers/ConfigManager.cpp` | `loadLastValid()`, `validateManifest()`, and `activateAtomically()` each return `false`. | Non-mock `setup()` in `src/main.cpp` calls them in a short-circuit chain; the first false result blocks startup, so later calls are not reached. | None. Neither test environment selects this source. | No manifest/persistence implementation or storage injection is present. The production startup path is deliberately fail-closed. |
| `Services/CalibrationService` — `include/project/Services/CalibrationService.h`, `src/Services/CalibrationService.cpp` | `loadCalibration()` returns false. `process()` clears the output to its default-invalid state and returns false. | `TaskFeature::onStart()` calls `loadCalibration()` and fails startup; `TaskFeature::runOne()` calls `process()` and reports a calibration rejection on failure. | None. It is not in either test source filter. | Uses `SensorFrame` and `ProcessedFrame`; calibration schema, units, coefficients, validity policy, and approved source are absent. |
| `AI/SchemaRegistry` — `include/project/AI/SchemaRegistry.h`, `src/AI/SchemaRegistry.cpp` | `validate()` and `compatible()` return false. | `TaskInference::onStart()` sets schema compatibility from `compatible()`; `runOne()` rejects input when `validate()` is false and produces a version-mismatch output. | None. It is not in either test source filter. | Takes `FeatureVector`; released FSS/schema descriptor and compatibility rules are missing. |
| `Models/Serialization` — `include/project/Models/Serialization.h` (header-only) | `BoundedByteWriter` performs bounded byte appends with argument/capacity checks. All seven record serialize/deserialize pairs return `NotImplemented` for otherwise valid arguments; serialization sets `bytesWritten` to zero. No struct-memory dump is performed. | No production caller found. | None. | Depends on the seven model record types and serialization status/format enums. Canonical field/byte order, encoding, versioning, and wire formats are not approved. |
| `Storage/StorageManager` — `include/project/Storage/StorageManager.h`, `src/Storage/StorageManager.cpp` | Header declares `begin()`, `appendRecord()`, and `flush()`; source is comment-only and defines none. | No caller found. | None. It is not in either test source filter. | Includes `IStorage`, whose interface uses `append()` rather than `appendRecord()`; no interface is injected into `StorageManager`. Storage hardware/layout, lifecycle, error policy, and ownership remain unresolved. `CsvLogger` is a separate implemented component and does not call `StorageManager`. |
| `Managers/OutputController` — `include/project/Managers/OutputController.h`, `src/Managers/OutputController.cpp` | `apply()` and `setFaultIndication()` are declared; source is comment-only. | No caller found. | None. | Accepts `RiskAssessment`; approved risk-to-output mapping, safe state, GPIO/electrical review, and output hardware are absent. |
| `Managers/RecoveryManager` — `include/project/Managers/RecoveryManager.h`, `src/Managers/RecoveryManager.cpp` | `classifyError()` and `executeRecovery()` are declared; source is comment-only. | No caller found. | None. | No fault/retry/recovery contract or implementation dependency is wired. Fault taxonomy and bounded recovery policy need approval. |
| BSP watchdog — `include/project/BSP/BoardSupportPackage.h`, `src/BSP/BoardSupportPackage.cpp` | `watchdogInit()` and `watchdogFeed()` return `BspStatus::NotConfigured`. | `WatchdogSupervisor::begin()` calls init; `checkTaskProgress()` calls feed only after required task heartbeats pass. A non-`Ok` result is treated as platform unavailable/failure. | None in configured test environments. | Framework/SoC integration, monitored task set, deadlines, timeout, and feed policy are explicitly TBD. The application supervisor is not a configured hardware watchdog. |
| BSP power status — same BSP header/source | `readPowerStatus()` clears the output to its default state (`valid == false`) and returns `NotConfigured`. | No caller found. | None in configured test environments. | No confirmed power-monitor hardware, measurement contract, or reviewed electrical setup. |
| `AI/EdgeImpulseAdapter` — `include/project/AI/EdgeImpulseAdapter.h`, `src/AI/EdgeImpulseAdapter.cpp` | This is not wholly a placeholder. It validates configured schema/model/output metadata and input provenance/shape/finiteness. Without `PROJECT_EDGE_IMPULSE_MODEL_HEADER`, initialization returns false and `run()` reports `ModelError`; with a generated export it conditionally calls `run_classifier()`. Config hash/approval provenance is trusted from the caller, not cryptographically verified here. | `TaskInference` accepts an injected `IInferenceEngine`; no current composition instantiates `EdgeImpulseAdapter`. | No direct adapter test; not in the host test source filters. | No generated export/header or verified deployment manifest is in the project configuration. Actual model dimensions, FSS, output semantics, and generated SDK memory behavior remain blocked. |
| `Services/FeaturePipeline` — `include/project/Services/FeaturePipeline.h`, `src/Services/FeaturePipeline.cpp` | Implemented fixed-capacity configuration checks, frame validation, sequence/time/calibration discontinuity handling, sliding window/stride, and `FeatureVector` metadata. Supported extraction is acceleration magnitude (explicit conversion required) and candidate tilt formulas (approval required). Raw accelerator, soil, YL-83, and HX710B feature IDs are rejected as unsupported. Default configuration is not ready. | `TaskFeature` owns/uses it after quality and calibration processing. | Unit: `testFeaturePipelineBuildsApprovedWindow`, `testFeaturePipelineFailsClosedForTbdRawFeature`. Integration exercises a test-only approved fixture. | `ProcessedFrame`/`FeatureVector`; production use blocked on released FSS/hash, eligible feature indexes/order, units/preprocessing, and approved sample/window/stride/model metadata. |
| `Managers/RiskManager` — `include/project/Managers/RiskManager.h`, `src/Managers/RiskManager.cpp` | Implemented risk-config validation, model/tilt candidate classification, quality/health/provenance gates, separate risk/health output, and promotion/recovery hysteresis. Invalid/unconfigured inputs do not become `SAFE`; the default config is invalid. No production thresholds are embedded. | `TaskRisk` calls the `ProcessedFrame`/`HealthSnapshot` overload. | Unit: `testRiskManagerFusesApprovedScoreAndAppliesHysteresis`, `testRiskManagerReturnsUnknownForInvalidQualityOrModel`, `testRiskManagerHealthFaultBlocksModelRisk`, `testRiskManagerRejectsMismatchedModelProvenance`, `testRiskManagerFusesApprovedTiltWithModelScore`. Integration uses test-only configurations and model output. | Depends on `ModelOutput`, `ProcessedFrame`, `HealthSnapshot`, and `RiskAssessment`. Production classification is blocked on approved/hash-verified target semantics, thresholds, fallback/tilt policy, and evidence. `OutputController` is not wired. |

## Coverage and integration observations

- The native unit environment covers `FeaturePipeline` and `RiskManager`, but
  does not compile/test the placeholder modules or `EdgeImpulseAdapter`.
- The native integration environment covers the sensor/feature/test-model/risk/
  `CsvLogger` path. It does not exercise `CalibrationService`, `SchemaRegistry`,
  `ConfigManager`, `StorageManager`, `OutputController`, `RecoveryManager`, or
  the generated Edge Impulse runtime.
- The production `TaskFeature` path is currently blocked at calibration startup
  because `CalibrationService::loadCalibration()` returns false. The
  `TaskInference` schema gate is blocked because `SchemaRegistry::compatible()`
  returns false. The non-mock `setup()` is blocked by `ConfigManager`.
- `CsvLogger` is a separately implemented CSV component behind its own file
  system contract; it is not evidence that the generic `StorageManager` or a
  concrete production storage backend is implemented.

## API contract status

Method-level preconditions, current results, side effects, blocking/context,
ownership/thread-safety, error cases, and minimum dependency-injection seams
for the seven requested modules are recorded in
[MODULE_API_CONTRACTS.md](./MODULE_API_CONTRACTS.md).

The current `bool` and `void` signatures were retained because the definitions
for the used APIs (`ConfigManager`, `CalibrationService`, and `SchemaRegistry`)
and their callers are outside the header-only implementation scope for that
contract pass. A header-only `bool`-to-`ApiStatus` change would make those
declarations incompatible with their definitions and would break callers that
use boolean conditions. The contract document lists each affected call site
and the explicit `ApiStatus::Ok` comparison required in a future coordinated
signature migration.

No new status enum or model struct was introduced. The remaining signatures
whose methods have no inputs/results (`runHealthChecks`, `requestShutdown`,
`setFaultIndication`, `classifyError`, `executeRecovery`) and storage/output
failure reporting are documented as contract blockers where the existing API
cannot represent the required outcome.
