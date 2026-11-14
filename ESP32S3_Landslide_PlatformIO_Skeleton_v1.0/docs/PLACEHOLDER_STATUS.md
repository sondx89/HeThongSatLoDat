# Implementation and Blocker Status

**Document revision:** 1.0

**Audit date:** 2026-11-13
**Evidence:** Current headers/sources, callers, PlatformIO test filters, and
the native unit/integration test runs recorded below. A module is not called
production-ready merely because its source or host tests exist.

## Status definitions

- **Implemented:** source behavior is present.
- **Partial:** a defined subset exists; required behavior/artifacts remain.
- **Host-tested:** named tests exercise the listed behavior on the host.
- **Mock/test-only:** tests use synthetic fixtures or fakes, not field inputs.
- **Blocked:** a required artifact, approval, or API is absent.
- **Not configured:** required hardware/policy value remains unset.

## Module inventory

| Module/files | Current behavior | Tests/evidence | Status and blockers |
|---|---|---|---|
| `Managers/AppController` — `include/project/Managers/AppController.h`, `src/Managers/AppController.cpp` | Dependency-injected startup/shutdown state machine, ten ordered phases, reverse cleanup, inference gate, bounded-port config, health checks and status results. It has no production module adapter and is not used by `main.cpp`. | AppController tests in `test/test_system_contracts/test_system_contracts.cpp`: phase order, every startup-phase failure/reverse cleanup, inference-gate failure, shutdown order/repeat, flush failure, health failure/bounds, and unconfigured timeout. | **Implemented/host-tested protocol; production integration blocked.** TaskFramework has no stop/drain operation; StorageManager exposes no timeout-bearing flush. Adapter must enforce bounds; synchronous calls are not preempted. |
| `Managers/ConfigManager` — header/source | Pure manifest validation and injected two-slot logical persistence: write candidate, flush, read back, validate, commit marker, select newest valid committed generation. Default constructor remains without persistence. `activeManifest()` exposes a selected Production manifest only after manager-level gate; no runtime module application occurs. | ConfigManager tests in system-contract suite: validation errors, hash checks, production fail-closed, each write/commit fault boundary, invalid candidate, successful commit, newest-corrupt fallback. | **Implemented/host-tested protocol; production persistence blocked.** No concrete adapter/field encoder/media, production composition, or hardware durability proof. |
| `Services/CalibrationService` | Propagates frame metadata, raw values, and channel validity without applying calibration coefficients; calibration load remains not initialized. | CalibrationService tests in system-contract suite: raw/validity propagation, non-finite handling, version/schema rejection, partial-channel behavior. | **Partial/host-tested.** No approved calibration artifact, coefficients, or calibrated production output. |
| `Dataset/CalibratedSampleCsvExporter` — `include/project/Dataset/CalibratedSampleCsvExporter.h`, `src/Dataset/CalibratedSampleCsvExporter.cpp` | Bounded host-only artifact serializer keeps raw and calibrated fields separate; RAW_ONLY leaves calibrated values empty with an explicit status/reason. | `testCalibratedSampleCsvExporterKeepsRawOnlyValuesExplicit`, `testCalibratedSampleCsvExporterAcceptsTestOnlyCalibrationAndRangeChecks`, and `testCalibratedSampleCsvExporterRejectsCalibrationIdentityMismatches` in the system-contract suite. | **Implemented/host-tested serializer; physical calibration blocked.** A test-only fixture does not approve physical conversion or calibration. |
| `AI/SchemaRegistry` | Compares descriptors and vectors across schema identity/hash, feature count/order/units, preprocessing, window/stride/rate and validity. Production compatibility rejects test-only descriptors. | SchemaRegistry test group in system-contract suite. | **Implemented/host-tested generic validation.** Synthetic descriptors only; released FSS and actual model metadata absent, so production compatibility is blocked. |
| `Models/Serialization` — header/source | Field-by-field bounded EventRecord JSONL v2 encode/decode. Other record types and CSV/binary serializer operations report unsupported/not implemented. No raw struct dump. | EventRecord JSONL tests in system-contract suite: round-trip, invalid args/bounds, truncation/trailing bytes, invalid type/version/enums, non-finite policy and boundary values. | **Partial/host-tested.** Six record codecs and other formats are not implemented. See `SERIALIZATION_SCHEMA.md`. |
| `Storage/StorageManager` — header/source | Injected `IStorage` lifecycle; bounded by a configured maximum record length, synchronous borrowed input; backend false maps to I/O error and Faulted; logical shutdown. No storage layout or hardware operations. | StorageManager lifecycle/fake-backend tests in system-contract suite include begin failures/retry, append validation, partial-write error, flush failure, capacity, shutdown and write-after-shutdown. | **Implemented/host-tested lifecycle.** `IStorage` returns only bool, has no timeout/byte count/close; physical backend and durability remain blocked. |
| `Managers/OutputController` — header/source | Pure risk-assessment-to-output-command policy; no GPIO or electrical assumptions. Unknown/invalid stays explicit; feedback absent/unverifiable is not claimed active. | Output policy tests in system-contract suite cover all states, invalid inputs, feedback, and repeat behavior. | **Implemented/host-tested pure policy.** Physical actuation, pin/polarity, safe-state and feedback are not configured/verified. |
| `Managers/RecoveryManager` — header/source | Pure status/context classification and bounded retry decisions requiring injected retry policy. Schema/model mismatch inhibits inference. No device recovery/restart. | Recovery tests in system-contract suite cover known/unknown status, retry bounds, mismatch inhibit. | **Implemented/host-tested pure policy.** Hardware action dispatcher and target recovery validation are blocked. |
| `Services/DataQualityService` | Classifies configured channel state and produces processed frames without converting invalid values to valid zero. | Data-quality test group in system-contract suite. | **Implemented/host-tested.** Production ranges, freshness bounds and required-channel policy remain unconfigured. |
| `Services/FeaturePipeline` | Fixed-capacity window/stride extraction with schema/config/provenance checks; supports only approved/configured feature paths. | FeaturePipeline unit tests plus synthetic integration fixture. | **Partial/host-tested.** Production FSS, input ordering, units/preprocessing and model metadata absent. |
| `Managers/RiskManager` | Configured model/tilt classification, data-quality/health/provenance gates and hysteresis. Default configuration is invalid and does not infer a normal state. | RiskManager test group in system-contract suite; synthetic integration fixture. | **Implemented/host-tested with test configuration.** Production policy thresholds/fallback approval and validated model semantics remain blocked. |
| `AI/EdgeImpulseAdapter` | Conditional `run_classifier()` integration; checks configured metadata/input when generated header is enabled; without it initialization fails closed. | No direct adapter test and no generated model build evidence. | **Partial/blocked.** Actual deployment export, SDK, model metadata, released FSS, golden vectors, target build and resource measurements absent. |
| `AI/MockInferenceEngine`, `Drivers/MockSensors`, mock telemetry | Test-only inference copies explicitly configured finite raw outputs and supports injected failures; it has no target/risk meaning. | Native integration tests cover schema/output validation, injected model failure and Edge Impulse fail-closed behavior. | **Mock/test-only.** Never production inference, field model evidence, or dataset ground truth. |
| `Managers/SensorManager` and drivers | Sequential sensor manager with configured retry/time checks and invalid-channel reporting; concrete drivers have configuration/electrical gates. | SensorManager unit group and sensor-to-feature integration test. | **Implemented/host-tested; target configuration not verified.** Hardware identity, pin/bus settings and sensor calibration need target evidence. |
| `Tasks/TaskFramework`, `Queues/QueueRegistry`, `ApplicationTasks` | Task startup gate, typed queues, configured task classes and heartbeat framework exist. | Not included as a full production-runtime target integration in the present native evidence. | **Implemented/partial.** Queue depths/task priority/stack/timeouts remain unconfigured. No stop/drain API; runtime composition not wired. |
| `Storage/CsvLogger` | Separate six-column CSV logger with injected filesystem interface and CSV-specific ownership. | Native integration suite uses a fake/test logger path. | **Implemented/partial.** No production filesystem/media adapter or durability evidence; not the same interface as StorageManager. |
| BSP watchdog and power status | `watchdogInit()`, `watchdogFeed()`, and `readPowerStatus()` remain `NotConfigured` absent approved target configuration/hardware. | No target verification. | **Not configured/blocked.** Do not claim hardware watchdog or power monitoring is operational. |
| `main.cpp` production composition | Performs early BSP checks, then default-constructs ConfigManager and stays fail-closed when production config is unavailable. It does not construct AppController or start runtime tasks. | Source review. | **Partial, deliberately blocked.** Requires production persistence, adapters, measured runtime configuration, and approved model/FSS/hardware. |

## P01 test evidence snapshot (2026-11-13, before P02)

Latest run executed on 2026-11-13 from the repository root with MSYS2 UCRT64
GCC/G++ 16.2.0 added to `PATH`:

- `pio test -d ESP32S3_Landslide_PlatformIO_Skeleton_v1.0 -e native-unit-tests`:
  exit 0, 115/115 cases passed.
- `pio test -d ESP32S3_Landslide_PlatformIO_Skeleton_v1.0 -e native-integration-tests`:
  exit 0, 7/7 cases passed.
- `pio run -d ESP32S3_Landslide_PlatformIO_Skeleton_v1.0 -e esp32-s3-devkitc-1`:
  exit 1. Current compile errors are the `SerialDiagnostics::buildPayload`
  declaration/definition mismatch and `HealthSnapshot` brace-assignment
  mismatches. The prior `BoardConfig.h` `issueMask` error is fixed.
- Hardware-in-loop, power-loss-on-device, and deployed-model tests were not run.

The 2026-11-02 results of 71 unit and 3 integration cases are historical.
P01's 115/7 results and target failure preceded P02. See
`BASELINE_TEST_REPORT.md` for the latest results and
`REQUIREMENT_TRACEABILITY.md` for test-to-requirement links.

## Latest P02 test evidence (2026-11-13)

- Native unit tests: 116/116 passed, exit 0.
- Native integration tests: 7/7 passed, exit 0.
- ESP32-S3 compile/link/image build: passed, exit 0, through a temporary
  `Z:` alias used to avoid the non-ASCII workspace path issue.
- No upload, HIL, or physical hardware test was run.

## Change history

| Revision | Date | Change |
|---|---|---|
| 1.2 | 2026-11-13 | Recorded current P02 test/build evidence; retained P01 results as a dated snapshot. |
| 1.1 | 2026-11-13 | Added the calibrated-sample exporter inventory and current P01 native/target evidence; marked earlier counts historical. |
| 1.0 | 2026-11-02 | Replaced the stale 2026-10-30 skeleton inventory with source/test-backed implementation states and current blockers. |
