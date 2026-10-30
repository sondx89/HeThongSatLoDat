# Module API Contracts

**Status:** Contract inventory for the current skeleton; not an implementation
or approval of hardware, storage, calibration, risk, or recovery policy.  
**Scope:** Public methods in `AppController`, `ConfigManager`,
`CalibrationService`, `SchemaRegistry`, `StorageManager`, `OutputController`,
and `RecoveryManager`.

## Contract and status conventions

- The current signatures are retained. `bool` and `void` cannot express the
  distinct failure causes required for these APIs. Do not interpret `false`,
  a cleared output, or a no-op as successful/valid data.
- For a future coordinated signature migration, reuse
  `project::models::ApiStatus` from `Models/DataContractTypes.h`; do not add
  module-specific enums that duplicate its lifecycle, argument, data, version,
  I/O, timeout, capacity, or cancellation states.
- `ApiStatus::Ok` means the requested operation completed. For
  `SchemaRegistry::compatible()`, `Ok` would mean compatible; a mismatch is
  `VersionMismatch`, not a successful `false`.
- Relevant existing statuses are `InvalidArgument`, `NotInitialized`,
  `Timeout`, `IoError`, `DataInvalid`, `StaleData`, `BufferFull`,
  `NotSupported`, `VersionMismatch`, `ResourceExhausted`, `InternalError`,
  `Busy`, and `Cancelled`. Use only the status that represents the observed
  failure; do not infer a cause the implementation cannot establish.
- A `bool`-to-`ApiStatus` migration is source-breaking: `enum class ApiStatus`
  is not implicitly usable as `bool`. Each caller must explicitly compare
  against `ApiStatus::Ok`; definitions, callers, and tests must move together.
- No method in these APIs is declared ISR-safe or internally synchronized.
  Until a concrete implementation specifies otherwise, call only from task
  context, serialize calls to each instance, and give the instance one owning
  application/task context. Do not share mutable output buffers concurrently.
- Inputs are borrowed for the duration of a call; output storage is caller-owned.
  An implementation must not retain an input pointer or reference beyond the
  call unless an explicitly reviewed ownership contract is added.
- Any operation that may touch hardware or storage must have a finite,
  configured bound. No timeout, retry count, queue capacity, or storage layout
  is established by this document.
- These classes currently have implicit public default constructors and no
  explicit destructor contract. Default construction does not mean initialized
  or ready; do not infer resource ownership or cleanup behavior from it.

## Current API and method contracts

The “Current result/evidence” column describes the checked-in source, not a
proposed future implementation. Where the source is comment-only or lacks a
definition, behavior is explicitly **undefined/unimplemented**; callers must
not rely on a success-shaped fallback.

### `AppController`

Header: `include/project/Managers/AppController.h`  
Current implementation: `src/Managers/AppController.cpp` is comment-only.

| Method | Preconditions | Current result / intended result status | Side effects | Blocking and context | Ownership, concurrency, error cases |
|---|---|---|---|---|---|
| `bool begin()` | No preconditions or dependencies are represented in the current API. Intended startup requires the approved runtime configuration and all startup dependencies to be supplied and valid; unknown configuration must keep startup blocked. | Current: no definition. Intended: `ApiStatus::Ok` only when startup completes; otherwise use the applicable `NotInitialized`, `InvalidArgument`, `DataInvalid`, `VersionMismatch`, `IoError`, `Timeout`, `ResourceExhausted`, `Busy`, or `NotSupported`. | Intended to initialize the application composition in a documented order; rollback/partial-start behavior is not yet specified. | Startup/task context only; every phase and rollback must be bounded. Never call from ISR. | Controller and dependencies are non-owning/caller-lifetime concerns until a composition contract is defined. One startup caller; no concurrent lifecycle calls. Startup phase, rollback and error precedence remain TBD. |
| `void runHealthChecks()` | Controller must be started and health-check dependencies available; neither is represented/enforced today. | Current: no definition and no result. A future status-returning API should use `ApiStatus`; health data/result type is not defined by this API. | Intended to inspect/report health; which checks and where findings are published remain TBD. | Must be bounded and must not perform unbounded storage I/O or block acquisition; task context only. | Same controller owner as startup. No thread-safety guarantee. A `void` result cannot report not-started, timeout, or check failure. |
| `void requestShutdown()` | Controller lifecycle state and shutdown policy are not represented. | Current: no definition and no result. A future API should report completion/failure using `ApiStatus` or an existing shutdown result contract; do not add a duplicate status enum. | Intended to request an ordered shutdown; task stop/drain, bounded flush, output safe-state, and completion semantics remain TBD. | Must be bounded; task context only. No ISR use. | Same lifecycle owner; serialize with `begin()` and health checks. Current signature cannot report partial shutdown, timeout, or failure. |

### `ConfigManager`

Header: `include/project/Managers/ConfigManager.h`  
Current implementation: all three methods return `false`.

| Method | Preconditions | Current result / intended result status | Side effects | Blocking and context | Ownership, concurrency, error cases |
|---|---|---|---|---|---|
| `bool loadLastValid()` | Current API has no storage/manifest input or initialized-state query. Future use requires a configured source and a defined “last valid” record contract. | Current: always `false`. Intended: `Ok` only when a complete candidate is loaded; otherwise `NotInitialized`, `IoError`, `DataInvalid`, `VersionMismatch`, `Timeout`, or `NotSupported` as evidenced. | Future implementation may populate manager-owned candidate state; atomicity and what happens to a prior candidate on failure must be specified. | Task/startup context only. Persistence access must be finite; no infinite retries. | The candidate/config lifetime is currently unspecified. Serialize mutation on one owner; concurrent load/activation is not safe by contract. No caller-supplied output exists. |
| `bool validateManifest()` | A candidate manifest must have been loaded/provided; current API cannot identify which candidate. | Current: always `false`. Intended: `Ok` for a validated manifest; `InvalidArgument`, `DataInvalid`, or `VersionMismatch` for specific validation failures. | Intended to validate without activating or modifying persistent state; whether validation caches a report must be specified. | Pure validation should be bounded and task-context callable; no ISR use. | One owner serializes candidate mutation and validation. Missing/unknown fields must remain invalid/TBD, not be defaulted. |
| `bool activateAtomically()` | A loaded candidate must have passed validation; current API has no explicit candidate/version token. | Current: always `false`. Intended: `Ok` only after a complete atomic activation; otherwise `NotInitialized`, `DataInvalid`, `VersionMismatch`, `Busy`, `IoError`, or `ResourceExhausted`. | Intended to publish a complete active configuration or leave the previous active configuration unchanged. Exact atomic boundary and rollback are TBD. | Task/startup context only; finite execution. | Active/candidate ownership and reader synchronization are unspecified. Serialize activation and prevent readers from observing a partial update. |

### `CalibrationService`

Header: `include/project/Services/CalibrationService.h`  
Current implementation: `loadCalibration()` returns `false`; `process()`
clears `output` and returns `false`.

| Method | Preconditions | Current result / intended result status | Side effects | Blocking and context | Ownership, concurrency, error cases |
|---|---|---|---|---|---|
| `bool loadCalibration()` | Calibration source/configuration is not passed to this method and is not specified in the class API. | Current: always `false`. Intended: `Ok` only when a complete approved calibration is loaded; otherwise `NotInitialized`, `IoError`, `DataInvalid`, `VersionMismatch`, or `NotSupported`. | Current: none. Future successful call may replace calibration state; failed load must not expose a partial calibration. | Task/startup context; any persistence read must be bounded. No ISR use. | Calibration state is instance-owned by implication, but no synchronization is implemented. Intended single owner is `TaskFeature`/application composition. |
| `bool process(const SensorFrame&, ProcessedFrame&)` | `input` and `output` must refer to valid objects. The input may contain invalid, stale, missing, or non-finite fields; these are data conditions, not valid zeroes. | Current: always `false`; resets `output` to its default-invalid representation. Intended: `Ok` only for valid processing; use `InvalidArgument`, `NotInitialized`, `DataInvalid`, `StaleData`, or `VersionMismatch` when established. On any non-`Ok`, output must remain invalid and callers must ignore its numeric fields. | Current: clears caller-owned output. Intended: write only a complete result; never convert rejected/missing/non-finite input to a valid zero. | Bounded in-memory transform; task context. No I/O, waits, or ISR use. | Input is borrowed; output is caller-owned. Current task caller serializes access through `TaskFeature`; no general thread-safety guarantee. Coefficients, units, channel policy, and calibration version remain TBD. |

### `SchemaRegistry`

Header: `include/project/AI/SchemaRegistry.h`  
Current implementation: both methods return `false`.

| Method | Preconditions | Current result / intended result status | Side effects | Blocking and context | Ownership, concurrency, error cases |
|---|---|---|---|---|---|
| `bool validate(const FeatureVector&) const` | A released schema descriptor must be available; none is represented in the current constructor/API. Input validity, metadata and feature layout must be checked before compatibility is accepted. | Current: always `false`. Intended: `ApiStatus::Ok` for a valid compatible vector; `DataInvalid` for malformed/invalid content and `VersionMismatch` for schema identity/hash/layout mismatch. | Intended to be read-only; no mutation of the input or registry. | Bounded in-memory validation; task context. No ISR use. | The vector is borrowed. Registry data should be immutable after publication; current API provides no synchronization. `TaskInference` is the current intended single owner/caller. |
| `bool compatible() const` | Registry must contain a complete released schema and matching model contract; neither is represented in the current API. | Current: always `false`. Intended: `ApiStatus::Ok` means compatible; `NotInitialized`, `DataInvalid`, or `VersionMismatch` describes why compatibility is unavailable. | Read-only query. | Bounded in-memory query; task context. No ISR use. | Registry state is non-owning/ownership unspecified in the current API. Do not call concurrently with schema replacement; schema/version/hash remain TBD. |

### `StorageManager`

Header: `include/project/Storage/StorageManager.h`  
Current implementation: `src/Storage/StorageManager.cpp` is comment-only.
There is no caller in the current source/tests. `IStorage` exists separately
with `bool begin()`, `bool append(data, size)`, and `bool flush()`.

| Method | Preconditions | Current result / intended result status | Side effects | Blocking and context | Ownership, concurrency, error cases |
|---|---|---|---|---|---|
| `bool begin()` | A concrete storage backend and its selected hardware/configuration must be supplied; current class has no injection point. | Current: no definition. Intended: `Ok` only when the backend is ready; otherwise `NotInitialized`, `NotSupported`, `IoError`, `Timeout`, or `Busy`. | Intended to initialize/open the backend; repeat-call/idempotency and partial initialization behavior are TBD. | Task context only; backend operations require finite timeouts. Never call from ISR. | Current ownership of backend/resources is undefined. Proposed minimum test seam: inject an `IStorage&` and keep its lifetime outside the manager. `IStorage` only returns `bool`, so detailed backend errors cannot be preserved without a separate, coordinated interface change. |
| `bool appendRecord(const std::uint8_t*, std::size_t)` | For nonzero length, data must be non-null; bytes must already follow an approved record format. Null/zero-length semantics and maximum record size are not specified and must be decided before implementation. | Current: no definition. Intended: `Ok` only when the entire record is accepted; otherwise `InvalidArgument`, `NotInitialized`, `BufferFull`, `ResourceExhausted`, `IoError`, `Timeout`, or `NotSupported`. Never report partial acceptance as full success. | Intended to append bytes without interpreting them or serializing a C++ object. Persistence point/durability guarantee is TBD. | Must be bounded. If writes are asynchronous, record bytes must be copied into an explicitly bounded owned buffer before return; do not retain the caller pointer. Task context only. | Caller owns input storage; manager/backend must not retain the pointer past return. Single writer unless backend synchronization is explicitly implemented. No format/layout may be inferred. |
| `bool flush()` | Backend must have been successfully initialized; flush scope (all accepted records vs a selected record) is not specified. | Current: no definition. Intended: `Ok` only after the defined flush operation completes; otherwise `NotInitialized`, `IoError`, `Timeout`, `Busy`, or `NotSupported`. | Intended to request/complete backend flush; a successful return must not claim power-loss durability absent hardware evidence. | Finite blocking operation in task context; never ISR. | Uses the same externally owned backend. Serialize with writes unless backend documents safe concurrency. |

### `OutputController`

Header: `include/project/Managers/OutputController.h`  
Current implementation: `src/Managers/OutputController.cpp` is comment-only.
There is no caller in the current source/tests.

| Method | Preconditions | Current result / intended result status | Side effects | Blocking and context | Ownership, concurrency, error cases |
|---|---|---|---|---|---|
| `bool apply(const RiskAssessment&)` | Risk assessment must be valid and its risk/health fields must be interpreted only under an approved output policy. Pin mapping, electrical levels, and safe state must be verified before touching hardware. | Current: no definition. Intended: `Ok` only when the approved output command is applied/verified; otherwise `InvalidArgument`, `DataInvalid`, `NotInitialized`, `NotSupported`, `IoError`, or `Timeout`. `Unknown`/invalid input must not be silently mapped to a normal/off state unless that mapping is separately approved. | May change physical outputs; exact mapping and transition semantics are not approved. | Task context only, bounded hardware operation. No ISR use. | Controller must have a single output owner; no concurrent `apply()`/fault operation unless serialized. No output driver abstraction exists in the allowed/current API; do not invent GPIO assignments. |
| `void setFaultIndication()` | Fault indication and hardware safe-state mapping must be approved; current API carries no fault context/configuration. | Current: no definition and no result. A future implementation needs an observable `ApiStatus` to report unavailable or failed output. | Intended to request a fault indication; priority/interaction with `apply()` and safe output levels are TBD. | Bounded task-context operation; never ISR unless a specific safe ISR contract is added. | Same single output owner as `apply()`. Current signature cannot report not-configured, hardware failure, or timeout. |

### `RecoveryManager`

Header: `include/project/Managers/RecoveryManager.h`  
Current implementation: `src/Managers/RecoveryManager.cpp` is comment-only.
There is no caller in the current source/tests.

| Method | Preconditions | Current result / intended result status | Side effects | Blocking and context | Ownership, concurrency, error cases |
|---|---|---|---|---|---|
| `void classifyError()` | Current method has no error/fault input and no classification output. A fault source, classification result contract, and bounded policy must be agreed before it can be called meaningfully. | Current: no definition/result. No accurate status or classification can be represented by this signature. Reuse `FaultKind` for an established fault category and `ApiStatus` for operation failure; do not add an overlapping status enum. | Must be side-effect-free classification; recovery actions belong to `executeRecovery()` only after a reviewed decision exists. | Intended bounded, task-context computation only; no ISR use. | One recovery-policy owner; no concurrent mutation. API is blocked pending a fault-input/result contract. |
| `void executeRecovery()` | A classified fault/action and an approved bounded recovery policy must exist; current signature carries neither. | Current: no definition/result. A future operation should return `ApiStatus` for execution outcome; `Cancelled`, `Timeout`, `NotSupported`, `IoError`, or `Busy` may apply only when evidenced. | Recovery may restart/reinitialize resources or alter outputs; exact actions, retry limits, and safe state are unapproved. No action may be inferred from `FaultKind` alone without policy. | Task context, with finite attempts/timeouts; never ISR. No infinite retries or unconditional watchdog feeding. | Single owner of recovery state and serialized against shutdown/startup. Signature/policy is blocked until an action/result contract exists. |

## Existing caller impact

The status migration is deliberately **not applied in this contract-only
change**. The implementations of three currently-used bool APIs are outside
the allowed edit list, so changing only their headers would break declaration/
definition consistency. No caller source was changed.

| Caller | Current use | Required update in a future planned `ApiStatus` change | Current test coverage |
|---|---|---|---|
| `src/main.cpp` — non-mock `setup()` | Short-circuits `loadLastValid()`, `validateManifest()`, `activateAtomically()` as bools; any false blocks startup. | Check each call explicitly against `ApiStatus::Ok`; preserve fail-closed startup and report the first specific status. Implementations and target build must migrate in the same change. | No direct ConfigManager test in current environments. |
| `src/Tasks/ApplicationTasks.cpp` — `TaskFeature::onStart()` | `!calibration_.loadCalibration()` fails feature-task startup. | Compare explicitly to `ApiStatus::Ok`; preserve task startup failure on any non-Ok. | No CalibrationService test in current environments. |
| `src/Tasks/ApplicationTasks.cpp` — `TaskFeature::runOne()` | `!calibration_.process(...)` reports calibration rejection and does not enqueue a feature. | Compare explicitly to `ApiStatus::Ok`; ensure non-Ok output remains invalid and is not consumed. | No CalibrationService test in current environments. |
| `src/Tasks/ApplicationTasks.cpp` — `TaskInference::onStart()` | Assigns `schemas_.compatible()` to a bool and gates inference initialization. | Treat only `ApiStatus::Ok` as compatible; retain a specific non-Ok status for diagnostics if the task API permits it. | No SchemaRegistry test in current environments. |
| `src/Tasks/ApplicationTasks.cpp` — `TaskInference::runOne()` | `!schemas_.validate(features)` produces `VersionMismatch`. | Compare explicitly to `ApiStatus::Ok`; distinguish `DataInvalid` from `VersionMismatch` in model output/event handling only when those existing contracts can carry the distinction. | No SchemaRegistry test in current environments. |
| `AppController`, `StorageManager`, `OutputController`, `RecoveryManager` | No callers found in source/tests. | Plan API and dependency changes before adding callers. | No direct tests in current environments. |

## Minimum dependency injection and unresolved decisions

| Module | Minimum test seam recommended | Do not add/assume |
|---|---|---|
| `AppController` | Composition-root references to already selected module instances; inject only dependencies it actually orchestrates once the startup graph is approved. | Do not add a generic service locator, owning heap graph, or speculative interface for every subsystem. Current dependencies/startup order are not sufficiently frozen to add fields now. |
| `ConfigManager` | Supply a manifest/candidate value to a pure validation operation. Add a persistence seam only when the actual manifest storage contract is selected. | Do not use global storage or infer a persistence format. |
| `CalibrationService` | Supply immutable calibration parameters/configuration to the service; keep processing over existing model values. | No calibration values, units, or persistence backend are approved. Do not add hardware dependencies. |
| `SchemaRegistry` | Supply an immutable released schema/model descriptor at construction or validation boundary. | Do not synthesize schema fields or derive compatibility from feature count alone. |
| `StorageManager` | Reuse existing `IStorage` by non-owning reference for a fakeable backend, if its `bool` error limitation is accepted. | Do not create a second storage backend interface or assume `IStorage` supports read/recovery/durability. |
| `OutputController` | No current BSP output-write API exists. Select/extend the reviewed BSP output contract before adding a seam. | Do not invent GPIOs, electrical levels, output semantics, or a mock production driver. |
| `RecoveryManager` | Pure fault/action policy input after an existing fault taxonomy and action contract are approved. | Do not create a new status/action enum merely to fill the current parameterless methods. |

Before signatures are changed, resolve the blocked error/report types for
`AppController` health/shutdown, `StorageManager` record acceptance/durability,
`OutputController` applied/verified state, and `RecoveryManager` fault-to-action
mapping. Preserve current caller fail-closed behavior and update implementation,
callers, and tests atomically in that later change.
