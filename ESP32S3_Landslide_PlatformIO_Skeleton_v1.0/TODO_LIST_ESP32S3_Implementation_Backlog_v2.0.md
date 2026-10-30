# Firmware TODO list — Implementation Backlog v2.0

**Project:** ESP32-S3 Landslide Monitoring  
**Baseline:** PlatformIO + Arduino + C++17; MPU9250 over I2C; Edge Impulse inference intended to run offline on ESP32-S3.  
**Updated:** 2026-10-30  
**Purpose:** Convert the current skeleton and placeholder inventory into file-level coding, documentation, test, and hardware tasks.

## How to use this list

- Work from top to bottom unless a task is explicitly marked as parallel.
- `[NOW]` means it can be implemented/tested from the current source tree without inventing hardware/model values.
- `[WAIT]` means code structure or tests can be prepared, but production behavior is blocked by missing evidence or an approved artifact.
- `[HW]` means it requires the actual board/sensor/storage/alarm hardware.
- `[MODEL]` means it requires the actual Edge Impulse export and released FSS.
- `[DOC]` means update documentation and traceability.
- Do not mark a task complete merely because it compiles. Attach test evidence or a review record.
- Keep all unknown hardware, model, calibration, risk, timing, and storage parameters as `TBD`.
- A mock/test configuration must be visibly separated from production configuration.
- Do not enable operational alarm decisions or claim field readiness from host tests alone.

## 0. Immediate work package — do these first

These tasks are intentionally chosen because they can be done now without knowing the final GPIO map, sensor calibration, risk thresholds, or model dimensions.

### 0.1 Establish a clean baseline [NOW]

- [ ] Create a clean working branch or backup copy before changing implementation files.
- [ ] Record the current project tree and current TODO version in the change log.
- [ ] Run the existing native unit and integration test environments from a clean build directory.
- [ ] Record exact commands, compiler/platform versions, pass/fail counts, and test output in `docs/BASELINE_TEST_REPORT.md`.
- [ ] Confirm `platformio.ini` environment names and `build_project.bat` target names match; document any mismatch rather than silently renaming targets.
- [ ] Check that generated files under `build_output/` are not mistaken for source or release artifacts; keep generated outputs out of source control unless intentionally archived.
- [ ] Add a short `CHANGELOG.md` entry for each implementation batch.

**Acceptance:** A reproducible baseline report exists. Existing tests pass or known failures are recorded before implementation begins.

### 0.2 Replace ambiguous placeholder APIs with explicit contracts [NOW — design/code]

Current headers use `bool` or `void` for several operations. This hides the reason for failure and makes recovery policy difficult to test.

- [ ] Review `include/project/Models/DataContractTypes.h` (or the actual owning enum header) and identify the existing status/error enums before adding any new enum.
- [ ] Create a status/API contract table in `docs/MODULE_API_CONTRACTS.md`: method, preconditions, result/status, side effects, blocking behavior, task/ISR context, ownership, and thread-safety.
- [ ] Decide one consistent return convention for the manager/service layer. Preferred direction: use the project's existing `ApiStatus`/status type plus an output report where needed; do not add a second overlapping status system.
- [ ] For each placeholder header listed in sections 2.1–2.9, document the intended failure cases before changing the signature.
- [ ] Update all callers and tests in the same change when a signature changes.
- [ ] Ensure failure is distinguishable from valid `false`, empty data, or a valid zero-valued measurement.
- [ ] Do not add default values for TBD configuration fields to make `begin()` pass.

**Acceptance:** Every public method has a documented contract and no caller silently treats an error as a normal/empty result.

### 0.3 Add placeholder regression tests before implementing behavior [NOW]

- [ ] Add focused tests for current placeholder behavior so the test suite explicitly demonstrates which methods are not implemented.
- [ ] Use the existing Unity/native test structure under `test/test_system_contracts/` and `test/test_system_flow/`; do not create a second test framework.
- [ ] Add tests for invalid arguments, null buffers, zero-length buffers, insufficient buffers, and uninitialized module calls where relevant.
- [ ] Mark tests that require actual board/model/storage as hardware/model integration tests, not host PASS tests.
- [ ] Add a test naming convention: `test_<module>_<condition>_<expected_result>()`.

**Acceptance:** The test suite identifies each known placeholder and can detect accidental “success” behavior before the real implementation is added.

### 0.4 Documentation synchronization [DOC, NOW]

- [ ] Update `README.md`: state clearly that the current repository is a development skeleton, identify mock paths, and list what is not production-ready.
- [ ] Update `ARCHITECTURE.md`: show actual source modules separately from planned modules; do not draw a planned task/driver as implemented if it is not.
- [ ] Update `PROJECT_TREE.txt` if any source/header/test/doc is added or renamed.
- [ ] Add `docs/PLACEHOLDER_STATUS.md` with one row per placeholder: source file, header, current behavior, dependency/blocker, test file, completion evidence.
- [ ] Update `docs/TASK_RUNTIME_FLOW.md` to identify which task orchestration is implemented versus planned.
- [ ] Update `docs/EDGE_IMPULSE_ADAPTER.md` to distinguish existing fail-closed checks from the not-yet-integrated generated Edge Impulse library.
- [ ] Update `docs/CSV_LOGGER.md` to distinguish the operational logger, mock serial CSV, and DSD training dataset export.
- [ ] Keep DOP/DSD/FSS/ICD/LLD/TCS/GTIS version numbers unchanged unless their content is actually revised and reviewed. If revised, increment the relevant version and add a change history.

**Acceptance:** A new developer can tell what exists, what is a stub, what is mock-only, and what is blocked by TBD evidence.

---

## 1. File-level implementation plan

### 1.1 `AppController` — composition root [NOW for structure; HW/MODEL/WAIT for full startup]

**Current files**
- `include/project/Managers/AppController.h`
- `src/Managers/AppController.cpp` — currently only a skeleton comment.
- Related modules: BSP, ConfigManager, SensorManager, CalibrationService, FeaturePipeline, SchemaRegistry, EdgeImpulseAdapter, RiskManager, StorageManager, EventLogger, HealthMonitor, WatchdogSupervisor, ApplicationTasks.

**Tasks**
- [ ] Define an explicit `AppController` state enum: at minimum uninitialized, initializing, ready/degraded, shutting down, fault. Reuse existing project state types if already defined.
- [ ] Define dependencies as constructor-injected references/pointers or a documented composition structure. Avoid hidden global objects and avoid dynamic allocation unless the project explicitly approves it.
- [ ] Define startup phases as small private methods so each phase can be tested independently.
- [ ] Implement phase 1: BSP minimal init, read reset reason/boot identity, initialize monotonic time, force outputs to the approved boot-safe state.
- [ ] Implement phase 2: initialize diagnostic/event path best-effort without blocking startup indefinitely.
- [ ] Implement phase 3: load and validate configuration manifest; do not proceed to inference setup if configuration is invalid.
- [ ] Implement phase 4: initialize storage according to the approved failure policy; report degraded/fault if storage is unavailable.
- [ ] Implement phase 5: initialize sensor drivers and verify each required sensor. Optional/required sensor policy must come from config, not hard-coded assumptions.
- [ ] Implement phase 6: load calibration and initialize data-quality/feature pipeline.
- [ ] Implement phase 7: verify FSS/model compatibility; initialize Edge Impulse only if the actual export and schema are approved.
- [ ] Implement phase 8: initialize RiskManager, HealthMonitor, OutputController, queues, and tasks in the approved order.
- [ ] Implement phase 9: run self-checks and only then permit the system to report a normal/ready candidate.
- [ ] Implement rollback/cleanup in reverse order for resources initialized before a failure.
- [ ] Implement `requestShutdown()` as an ordered operation: prevent new work, drain/stop tasks, flush logger/storage with bounded timeout, set outputs to safe state, then mark shutdown complete.
- [ ] Implement `runHealthChecks()` as a bounded call; it must not perform long storage I/O or block sensor acquisition.
- [ ] Replace the current `bool begin()`/`void` APIs with the agreed status/result contract if section 0.2 approves it.
- [ ] Add dependency-injection tests using fake BSP/storage/sensor/model interfaces; no physical GPIO values are needed for these tests.
- [ ] Add failure-injection tests for failure at every startup phase and verify reverse cleanup and safe outputs.

**Documentation**
- [ ] Add startup/shutdown sequence diagram to `docs/TASK_RUNTIME_FLOW.md`.
- [ ] Add dependency ownership and startup failure table to `docs/PLACEHOLDER_STATUS.md`.

**Acceptance**
- Startup order is deterministic and documented.
- Every failure has a defined resulting system state.
- No inference or normal state is allowed before config, sensor/data, schema/model and self-check gates pass.
- Shutdown is bounded and attempts to flush pending critical records.

### 1.2 `ConfigManager` — manifest/config loading and atomic activation [NOW for parser/contracts; WAIT for persistent backend/approval]

**Current files**
- `include/project/Managers/ConfigManager.h`
- `src/Managers/ConfigManager.cpp` — `loadLastValid()`, `validateManifest()`, `activateAtomically()` currently return `false`.

**Tasks**
- [ ] Inventory the configuration structs already present in `include/project/` and identify the authoritative owner of each field.
- [ ] Define `Manifest`/configuration data structures only for fields already specified by approved documents; leave unresolved fields explicitly absent/unknown/TBD.
- [ ] Define a `ValidationReport` containing a stable error code, field/path, and short diagnostic reason.
- [ ] Implement pure validation first, without filesystem dependency: required fields, supported schema version, enum validity, finite numeric values, min/max consistency, and cross-field dependencies.
- [ ] Validate model hash/schema hash/calibration version/risk config version only when those artifacts are actually supplied.
- [ ] Add tests for missing field, unsupported version, invalid enum, NaN/Inf, out-of-range values, inconsistent min/max, mismatched model/FSS hash, and missing approval.
- [ ] Define a storage abstraction for manifest slots; do not couple ConfigManager directly to Arduino `File` or SD APIs.
- [ ] Define two-slot or equivalent staging/active metadata, including generation number and integrity check.
- [ ] Implement load of the newest fully valid committed slot; if newest is corrupt, try the previous committed slot.
- [ ] Implement staged activation as: write candidate -> flush -> read back -> validate -> mark committed -> switch active pointer/record.
- [ ] Ensure power loss before commit leaves the previous active config usable.
- [ ] Ensure activation failure does not overwrite the last known-good config.
- [ ] Add fault-injection tests around each write/commit boundary using a fake storage backend.
- [ ] Do not add cryptographic signature verification unless the signing/key-management design is approved; a hash alone is integrity checking, not authenticity.

**Documentation**
- [ ] Create `docs/CONFIG_MANIFEST.md` with field table, versioning, validation rules, storage layout, commit sequence, rollback behavior, and error codes.
- [ ] Update `ARCHITECTURE.md` to show ConfigManager owns validation/activation, while storage backend owns physical I/O.

**Acceptance**
- Pure validation tests pass on host.
- Atomic activation and power-loss behavior pass fake-backend fault-injection tests.
- Production activation remains disabled until manifest schema and approval process are frozen.

### 1.3 `CalibrationService` — raw-to-processed conversion [NOW for validity propagation; HW/WAIT for coefficients]

**Current files**
- `include/project/Services/CalibrationService.h`
- `src/Services/CalibrationService.cpp` — `loadCalibration()` returns `false`; `process()` clears output and returns `false`.

**Tasks**
- [ ] Inspect `SensorFrame`, `ProcessedFrame`, sensor status/validity flags and current adapters; write a field mapping table before coding.
- [ ] Define the exact source field, destination field, unit, validity bit and calibration version for each supported channel.
- [ ] Implement initialization/state reporting so “no calibration loaded” is distinguishable from “valid identity calibration”.
- [ ] Implement raw-field copying and validity/status propagation without applying unapproved coefficients.
- [ ] Implement finite-value checks for all floating-point inputs; reject NaN/Inf.
- [ ] Ensure invalid/stale/missing input remains invalid in `ProcessedFrame`; never turn it into zero and mark it valid.
- [ ] Keep raw values available for logging/diagnostics.
- [ ] Add per-channel validity rather than a single global valid flag where the data contract supports it.
- [ ] Add calibration artifact version/hash/sensor identity checks.
- [ ] Implement conversion equations only after sensor-specific calibration data and units are approved.
- [ ] Reset/invalidate `FeaturePipeline` window when calibration version changes.
- [ ] Add tests for missing calibration, wrong sensor ID, wrong version, valid raw-only path, finite values, NaN/Inf, boundary values, and partial-channel failure.
- [ ] Do not implement soil/rain/HX710 physical conversion from assumed formulas. HX710B remains raw-only until the transducer and measurand are identified.

**Documentation**
- [ ] Create `docs/CALIBRATION_CONTRACT.md` with raw/processed mapping, units, validity semantics, calibration artifact format and recalibration procedure.
- [ ] Update FSS/LLD only if calibrated units or feature definitions actually change.

**Acceptance**
- Invalid data cannot become a valid zero.
- Raw values are preserved.
- Conversion is enabled only for channels with approved coefficients and units.

### 1.4 `SchemaRegistry` — runtime FSS/model compatibility [NOW for generic validation; MODEL for actual dimension/hash]

**Current files**
- `include/project/AI/SchemaRegistry.h`
- `src/AI/SchemaRegistry.cpp` — `validate()` and `compatible()` currently return `false`.
- Related: `FeatureVector`, FSS JSON/TXT, `EdgeImpulseAdapter`.

**Tasks**
- [ ] Inspect `FeatureVector` fields and FSS v1.1 status; record that draft FSS is not a production-approved schema unless a released artifact says otherwise.
- [ ] Define a runtime schema descriptor containing only fields supported by the approved contract: schema ID/version/hash, feature count, ordered feature IDs, units, preprocessing/normalization identity, window/stride/sample-rate identity.
- [ ] Implement generic structural validation: feature count does not exceed storage capacity; required metadata present; values finite; valid mask consistent with active features.
- [ ] Implement compatibility comparison as an explicit field-by-field comparison with a mismatch reason.
- [ ] Add a mismatch enum/report for missing descriptor, version mismatch, hash mismatch, count mismatch, order mismatch, unit mismatch, preprocessing mismatch and timing/window mismatch.
- [ ] Add host tests using synthetic test descriptors with clearly marked TEST schema IDs; never reuse them as production metadata.
- [ ] Wire `EdgeImpulseAdapter` to ask SchemaRegistry before inference.
- [ ] Keep inference disabled if the production FSS hash or model metadata is missing.
- [ ] After obtaining the actual Edge Impulse export, extract its real input dimension, input order, sample rate, window/preprocessing and output metadata; update the released FSS and tests from that evidence.
- [ ] Do not infer model feature count from `FeatureVector::kMaxFeatures`.

**Documentation**
- [ ] Create `docs/SCHEMA_REGISTRY.md` with comparison matrix and fail-closed rules.
- [ ] Update `docs/EDGE_IMPULSE_ADAPTER.md` with exact source of model metadata and inference enablement gate.

**Acceptance**
- Synthetic host tests prove match/mismatch logic.
- Production compatibility remains false/blocked until actual model + released FSS metadata are supplied.

### 1.5 `Serialization` — explicit record formats [NOW for schema design/tests; WAIT for format freeze]

**Current files**
- `include/project/Models/Serialization.h` — `BoundedByteWriter` exists; seven serialize/deserialize pairs return `NotImplemented`.
- Record types: `SensorFrame`, `ProcessedFrame`, `FeatureVector`, `ModelOutput`, `RiskAssessment`, `EventRecord`, `HealthSnapshot`.

**Tasks**
- [ ] Inspect all seven model headers and create `docs/SERIALIZATION_SCHEMA.md` listing every field, type, optionality, validity semantics and version.
- [ ] Decide which format is required for each use case: operational CSV, event JSONL, or versioned binary. Do not make all formats mandatory without a real consumer.
- [ ] Keep the existing operational CSV contract unchanged unless a reviewed schema change is approved.
- [ ] Define stable textual enum names; do not serialize enum ordinal values as a long-term external contract.
- [ ] For binary, specify magic, format version, record type, length, byte order, integer widths, float representation, field order and checksum policy before implementation.
- [ ] Implement primitive bounded writer/reader helpers for fixed-width integers and IEEE-754 float32 only after the binary contract is approved.
- [ ] Check every append/read for bounds and overflow; avoid `capacity - size` underflow by maintaining the invariant `size <= capacity`.
- [ ] Implement one record type at a time, starting with the smallest stable record, then add round-trip tests.
- [ ] Implement explicit field-by-field encoding; never `memcpy` a C++ struct to persistent storage.
- [ ] Validate enum values, record version, length, finite-number policy and validity masks during deserialization.
- [ ] Add tests for null pointer, zero length, buffer too small, truncated record, trailing bytes, unknown version/type, invalid enum, NaN/Inf and round-trip equality.
- [ ] Ensure CSV escaping/decimal formatting is tested separately from binary serialization.
- [ ] Remove or rename `NotImplemented` helper only after all declared overloads have a defined behavior; do not make unimplemented formats appear successful.

**Documentation**
- [ ] Add format compatibility/version migration policy to `docs/SERIALIZATION_SCHEMA.md`.
- [ ] Link CSV schema to `docs/CSV_LOGGER.md`; keep dataset schema in DSD documents.

**Acceptance**
- Every implemented format has a frozen schema and round-trip/bounds tests.
- No raw struct memory is persisted.
- Unsupported format/version returns an explicit error.

### 1.6 `StorageManager` — storage lifecycle and bounded writes [NOW for interface/fake backend; HW for actual media]

**Current files**
- `include/project/Storage/StorageManager.h`
- `src/Storage/StorageManager.cpp` — skeleton only.
- Related: `IStorage`, `CsvLogger`, `EventLogger`, `CsvRecord`.

**Tasks**
- [ ] Inspect `IStorage` and current `CsvLogger` to decide whether StorageManager is a physical backend manager, a record queue facade, or both. Do not duplicate responsibilities already owned by CsvLogger/EventLogger.
- [ ] Write the responsibility split in `docs/STORAGE_DESIGN.md`.
- [ ] Define storage state: uninitialized, ready, degraded, full, I/O fault, not present.
- [ ] Define bounded append semantics: input ownership, maximum record size, whether call blocks, timeout, and partial-write handling.
- [ ] Implement a fake in-memory IStorage backend for host tests if no fake already exists.
- [ ] Implement begin/state transitions against the interface without hard-coding SD/SPI/SDMMC pins.
- [ ] Implement append with explicit handling for null data, zero length, oversized record, not initialized, backend short write and backend error.
- [ ] Implement flush with bounded timeout semantics; do not claim durability unless backend/filesystem semantics are verified.
- [ ] Define whether a single logger task owns physical writes; if so, prohibit direct file I/O from sensor/inference tasks.
- [ ] Add tests for begin failure, append success, partial write, full media, flush failure, repeated begin, and write after shutdown.
- [ ] Add rotation/recovery only after storage hardware and file format are selected.
- [ ] On storage failure, raise a health/event condition and follow the approved policy; never discard critical events silently or report successful persistence.

**Documentation**
- [ ] Create `docs/STORAGE_DESIGN.md` with backend interface, task ownership, buffering, full-media policy, rotation, recovery and durability caveats.
- [ ] Update `docs/CSV_LOGGER.md` to define logger-vs-storage ownership.

**Acceptance**
- Host tests validate lifecycle and error propagation through a fake backend.
- Hardware file-system behavior remains pending until actual storage media is selected and tested.

### 1.7 `OutputController` — risk-to-output mapping [NOW for policy interface; HW/WAIT for GPIO/alarm]

**Current files**
- `include/project/Managers/OutputController.h`
- `src/Managers/OutputController.cpp` — skeleton only.
- Related: `RiskAssessment`, BSP safe-output functions, alarm hardware fields in BoardPins.

**Tasks**
- [ ] Define an `OutputPolicy` table for every risk/system state, including STARTUP, NORMAL, MONITOR, DANGER_TEST, UNKNOWN and FAULT where supported by the project contracts.
- [ ] Keep policy data separate from GPIO pin assignments and Arduino APIs.
- [ ] Define boot/reset/fault safe state from the reviewed electrical design; do not assume active-high/active-low.
- [ ] Implement pure mapping from `RiskAssessment` to an output command so it can be host-tested.
- [ ] Treat invalid/unknown assessment as unknown/fault policy, never silently as normal/off.
- [ ] Separate command status from physical readback. If no feedback circuit exists, return NOT_PRESENT/NOT_VERIFIABLE rather than claiming the alarm is active.
- [ ] Add output transition tests, repeated-command tests, invalid-input tests and command/readback mismatch tests.
- [ ] Add hardware driver integration only after buzzer/LED/relay presence, pin map, polarity, current and safe-state have been reviewed.
- [ ] Add startup self-test only if the physical output and test procedure are approved.

**Documentation**
- [ ] Create `docs/OUTPUT_POLICY.md` with state-to-command table, boot-safe state, fault behavior and feedback limitations.
- [ ] Update FMEA/ICD/LLD if physical alarm behavior or safety assumptions change.

**Acceptance**
- Pure policy mapping is testable without hardware.
- No hardware output is enabled from an unapproved pin/polarity or risk threshold.

### 1.8 `RecoveryManager` — fault classification and bounded recovery [NOW for fault catalog/pure policy; HW for recovery actions]

**Current files**
- `include/project/Managers/RecoveryManager.h`
- `src/Managers/RecoveryManager.cpp` — skeleton only.
- Related: status enums, EventRecord, HealthMonitor, WatchdogSupervisor, SensorManager, StorageManager.

**Tasks**
- [ ] Inventory existing error/status enums and consolidate duplicate meanings before adding new codes.
- [ ] Define stable `FaultCode`/`RecoveryAction` mapping in `docs/FAULT_RECOVERY_MATRIX.md`.
- [ ] Include at least configuration, sensor, I2C, calibration, data-quality, schema/model, queue, storage, output, task deadline and internal faults if represented by current architecture.
- [ ] Define severity, retry allowed, maximum attempts, backoff strategy, recovery action, escalation state and event to log for each fault.
- [ ] Implement pure `classify(error/status/context)` logic first; no hardware calls inside classification.
- [ ] Implement a bounded recovery dispatcher using injected interfaces/callbacks; do not hide I2C or SD operations inside a generic manager.
- [ ] Ensure schema/model mismatch locks inference until a valid configuration/model is loaded.
- [ ] Ensure retries cannot loop indefinitely and cannot block acquisition/critical monitoring.
- [ ] Record recovery attempt count and final result.
- [ ] Add tests for unknown code, retry limit, successful recovery, failed recovery, backoff boundary and repeated fault.
- [ ] Add hardware-specific recovery (sensor re-init, I2C bus recovery, controlled restart) only after each action is verified on target.

**Documentation**
- [ ] Create `docs/FAULT_RECOVERY_MATRIX.md`.
- [ ] Update `docs/TASK_RUNTIME_FLOW.md` with recovery ownership and interaction with watchdog.

**Acceptance**
- Fault classification and retry policy pass host tests.
- Hardware recovery remains disabled until the corresponding bus/device action has been validated.

### 1.9 BSP watchdog and power monitor [NOW for contract/tests; HW/framework-specific for implementation]

**Current files**
- `src/BSP/BoardSupportPackage.cpp`: `watchdogInit()` and `watchdogFeed()` return `NotConfigured`; `readPowerStatus()` is also not configured.
- Related: `WatchdogSupervisor`, task runtime configuration, PlatformIO framework/ESP-IDF version.

**Tasks**
- [ ] Record exact PlatformIO platform, Arduino core and ESP-IDF versions from the resolved build environment.
- [ ] Inspect the watchdog API available in that exact framework version; do not copy an API from a different ESP-IDF version.
- [ ] Define monitored task set and heartbeat semantics. A heartbeat must indicate real forward progress, not merely that a task executed.
- [ ] Measure worst-case task execution and scheduling delays on target before setting timeout values.
- [ ] Define supervisor decision table: required tasks healthy -> feed; missed progress/deadline -> do not feed and record/escalate according to policy.
- [ ] Ensure only WatchdogSupervisor can invoke the BSP feed operation.
- [ ] Add host tests for supervisor decision logic independent of hardware watchdog API.
- [ ] Implement BSP watchdog init/feed only after timeout and framework API are verified.
- [ ] Verify reset reason after forced watchdog timeout on target.
- [ ] Keep `readPowerStatus()` as NotConfigured unless a power monitor is physically present and mapped; do not synthesize voltage/brownout readings.

**Documentation**
- [ ] Create `docs/WATCHDOG_POLICY.md` with task list, progress source, deadline basis, timeout, feed ownership and reset verification.
- [ ] Update `docs/BASELINE_TEST_REPORT.md` with target timing and reset evidence when available.

**Acceptance**
- Host tests prove feed/no-feed decisions.
- Target fault injection proves watchdog reset and reset-reason reporting.
- Watchdog is not described as active/healthy while BSP returns NotConfigured.

---

## 2. Cross-module code tasks that can start now

### 2.1 Data contracts and enums [NOW]

- [ ] Review all files under `include/project/Models/` and produce a field inventory for each data structure.
- [ ] Check `RiskState` aliases and decide whether aliases are source-compatibility conveniences or ambiguous persisted states; never persist raw enum ordinals.
- [ ] Review `EventRecord` fields `errorCode`, `faultCode`, `statusCode`; document non-overlapping semantics or remove duplication through a reviewed migration.
- [ ] Define validity semantics for each sensor channel; invalid/missing must not be encoded as zero.
- [ ] Define monotonic timestamp and boot identity semantics; do not compare monotonic timestamps across boots without boot ID.
- [ ] Review `HealthSnapshot` task/queue identifiers and counters; reserve IDs centrally rather than assigning literals in unrelated modules.
- [ ] Add compile-time assertions for fixed-width assumptions and feature capacity where appropriate.
- [ ] Update `docs/DATA_CONTRACTS.md` (new) with field tables and ownership.

### 2.2 Error and event consistency [NOW]

- [ ] Build one error catalog linking API status -> fault code -> event type -> recovery action.
- [ ] Ensure every failure path has a stable code and does not collapse to `false` without a reason.
- [ ] Ensure EventLogger can record startup, config rejection, sensor failure, model/schema mismatch, storage failure, recovery attempt and recovery completion.
- [ ] Define event ID uniqueness/boot-scoped sequencing and timestamp source.
- [ ] Add tests for unknown/unmapped status values and duplicate code detection.

### 2.3 Memory and concurrency [NOW for audit; target measurements later]

- [ ] Audit all new implementation paths for heap allocation; prefer fixed-size buffers and caller-owned storage.
- [ ] Document task ownership of every queue and mutable object.
- [ ] Document whether each method can block and its maximum timeout.
- [ ] Verify ISR paths use only ISR-safe FreeRTOS APIs.
- [ ] Audit for data races between acquisition, inference, logger, output and supervisor tasks.
- [ ] Add static buffer/queue size assertions where sizes are known.
- [ ] Measure stack high-water and heap low-water on ESP32-S3 before freezing budgets.

---

## 3. Documentation update backlog

Create or update these documents as the code contracts become stable. Do not rewrite higher-level requirements merely to match incomplete code.

| Document | Action | Required content | Depends on |
|---|---|---|---|
| `README.md` | Update now | Project status, build/test commands, mock vs target, known blockers | Current repo |
| `ARCHITECTURE.md` | Update now | Actual vs planned modules, dependencies, task ownership, startup/shutdown | Current repo |
| `PROJECT_TREE.txt` | Update whenever tree changes | Exact file tree; mark planned files explicitly | Code changes |
| `docs/PLACEHOLDER_STATUS.md` | Create now | Placeholder inventory, file path, behavior, blocker, tests, evidence | Current repo |
| `docs/MODULE_API_CONTRACTS.md` | Create now | API preconditions, return status, task/ISR, blocking, ownership | Header review |
| `docs/DATA_CONTRACTS.md` | Create next | Model fields, units, validity, timestamp, enum semantics | Model headers |
| `docs/SERIALIZATION_SCHEMA.md` | Create before serializer implementation | Field order, format/version, encoding, bounds, migration | Data contracts |
| `docs/CONFIG_MANIFEST.md` | Create before ConfigManager persistence | Manifest fields, validation, atomic commit, rollback | Config structs |
| `docs/CALIBRATION_CONTRACT.md` | Create before physical conversion | Raw/processed mapping, units, calibration metadata | Sensor evidence |
| `docs/SCHEMA_REGISTRY.md` | Create before enabling inference | FSS/model comparison and fail-closed conditions | Released FSS/model |
| `docs/STORAGE_DESIGN.md` | Create before storage backend | IStorage ownership, queue, flush, rotation, recovery | Storage hardware |
| `docs/OUTPUT_POLICY.md` | Create before GPIO alarm | Risk-state mapping, safe state, feedback limitations | Electrical review |
| `docs/FAULT_RECOVERY_MATRIX.md` | Create before recovery actions | Fault severity, retry, action, escalation | Error catalog |
| `docs/WATCHDOG_POLICY.md` | Create before watchdog enablement | Task progress, timing evidence, timeout, feed ownership | Target timing |
| `docs/BASELINE_TEST_REPORT.md` | Create now; append results | Build environment, commands, test results, target evidence | Test runs |
| `docs/RELEASE_CHECKLIST.md` | Create before release | Build/model/config hashes, test evidence, release gates | All above |

**Document change rules**
- [ ] Every changed document has version/date/change summary and owner/reviewer fields where appropriate.
- [ ] A code change that changes a contract must update its owning document in the same work item.
- [ ] If an implementation contradicts DOP/DSD/FSS/ICD/LLD/TCS/GTIS, stop and raise a design change; do not silently make code the new authority.
- [ ] Keep operational CSV, mock serial CSV and training dataset schema as three explicitly different artifacts.
- [ ] Keep MPU9250 I2C as the current interface baseline; update pin/address/clock only from board/sensor evidence.

---

## 4. Recommended implementation sequence and exit gates

### Sprint A — repository and contract hygiene [NOW]
- [ ] Complete sections 0.1–0.4.
- [ ] Complete data contract/error catalog inventory.
- [ ] Add placeholder status and API contract documents.
- [ ] Exit gate: baseline test report + clear source-of-truth for data/status contracts.

### Sprint B — pure host-testable logic [NOW]
- [ ] Implement ConfigManager pure manifest validation.
- [ ] Implement CalibrationService validity/raw pass-through behavior without unapproved physical coefficients.
- [ ] Implement SchemaRegistry generic descriptor validation using TEST-only descriptors.
- [ ] Implement OutputController pure risk-to-command policy with no GPIO.
- [ ] Implement RecoveryManager pure fault classification and bounded retry decision logic.
- [ ] Exit gate: unit tests cover success, invalid input, boundary, mismatch and failure paths.

### Sprint C — persistence and serialization [WAIT for schema freeze; fake backend can start]
- [ ] Freeze data record field inventory and version policy.
- [ ] Freeze operational logging format and any additional record format.
- [ ] Implement serializers one record at a time.
- [ ] Implement StorageManager against fake backend, then selected hardware backend.
- [ ] Implement ConfigManager two-slot persistence/rollback.
- [ ] Exit gate: round-trip, truncation, full-media and power-loss fault-injection tests.

### Sprint D — target integration [HW]
- [ ] Confirm board BOM/pin map/electrical design.
- [ ] Verify MPU9250 I2C detection, identity, register setup, axes and timing.
- [ ] Verify selected storage and alarm hardware.
- [ ] Integrate BSP watchdog using the actual framework version.
- [ ] Measure task/queue/heap/stack/timing on target.
- [ ] Exit gate: target build and hardware fault-injection report.

### Sprint E — Edge Impulse and risk validation [MODEL + field evidence]
- [ ] Obtain actual Edge Impulse deployment export.
- [ ] Release FSS matching exact model input.
- [ ] Implement actual inference wrapper and model hash/version checks.
- [ ] Approve regression target/scaling/output semantics.
- [ ] Approve RiskConfig and independent sensor rule policy.
- [ ] Run golden/replay tests and approved validation dataset.
- [ ] Exit gate: model/schema compatibility, reproducibility and risk-policy review complete.

### Sprint F — deployment acceptance
- [ ] Complete fault injection, endurance, storage recovery, watchdog and output tests.
- [ ] Complete traceability and release manifest.
- [ ] Review FMEA critical actions and safety case.
- [ ] Confirm field procedure, operator instructions and limitations.
- [ ] Exit gate: all release gates have evidence; no unresolved critical TBD.

---

## 5. Current known placeholder inventory (verified against source tree on 2026-10-30)

| Module | Current source behavior | Immediate next action |
|---|---|---|
| `AppController` | `src/Managers/AppController.cpp` is comment-only skeleton | Define dependency contract and startup state machine; add fake-dependency tests |
| `ConfigManager` | Three methods return `false` | Implement pure manifest validation before storage activation |
| `CalibrationService` | `loadCalibration()` returns false; `process()` clears output and returns false | Implement validity propagation/raw preservation; wait for coefficients |
| `SchemaRegistry` | `validate()` and `compatible()` return false | Implement descriptor comparison; keep production inference locked |
| `Serialization` | Seven record serializer/deserializer pairs return `NotImplemented` | Inventory fields and freeze wire schema before encoding |
| `StorageManager` | Source is comment-only skeleton | Clarify role vs CsvLogger/EventLogger; implement fake-backend lifecycle |
| `OutputController` | Source is comment-only skeleton | Implement pure state-to-command policy; wait for GPIO/electrical review |
| `RecoveryManager` | Source is comment-only skeleton | Implement pure fault classification/retry matrix |
| BSP watchdog | `watchdogInit()` and `watchdogFeed()` return `NotConfigured` | Verify framework version and task timing before hardware API |
| BSP power status | `readPowerStatus()` returns `NotConfigured` | Leave unimplemented unless power monitor hardware is confirmed |

**Important distinction:** `EdgeImpulseAdapter` is not wholly a placeholder. It contains fail-closed checks, but the actual generated Edge Impulse deployment/model integration still needs to be supplied and verified. `FeaturePipeline` and `RiskManager` already contain logic; they require contract/model/risk-policy review rather than being treated as empty stubs.

---

## 6. Definition of Done for each work item

A task can be checked complete only when all applicable evidence exists:

- [ ] Source/header changes are committed together.
- [ ] API and data contract documentation is updated.
- [ ] Unit tests cover nominal, invalid, boundary and failure paths.
- [ ] Integration tests cover the module boundary and injected failures.
- [ ] No unbounded heap allocation or unbounded retry/blocking was introduced.
- [ ] No TBD value was silently filled in.
- [ ] Native tests pass.
- [ ] ESP32-S3 build passes when the task affects target code.
- [ ] Hardware evidence exists when the task affects a peripheral, timing, watchdog, storage or output.
- [ ] Test report records build hash, environment, test ID, expected/actual result and reviewer.
- [ ] Related FMEA/ICD/LLD/FSS/TCS/DSD/DOP change is reviewed where a system contract changed.

---

## 7. Hardware/model configuration checklist



---

# Existing hardware/model configuration checklist (carried forward from TODO v1.x)



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

## 7.2 Configure sensors and data quality

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

## 7.3 Approve feature and Edge Impulse contracts

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

## 7.4 Approve risk, task, queue, and watchdog configuration

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

## 7.5 Finalize logging and dataset CSV contracts

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

## 7.6 Complete build, upload, and acceptance evidence

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

## 7.7 Variables that must be defined

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

## 7.8 Current development-only behavior

- The Windows batch script defaults to tests, mock firmware upload, and serial
  capture. `production-build` only attempts the fail-closed production target;
  it does not make the application deployable.
- The mock serial CSV is synthetic and ineligible for training. The operational
  six-column logger remains unchanged.
- Edge Impulse Studio dataset upload, production inference, production risk
  thresholds, board pin mapping, and FreeRTOS task/queue sizing remain
  unconfigured until their respective approvals/evidence exist.
