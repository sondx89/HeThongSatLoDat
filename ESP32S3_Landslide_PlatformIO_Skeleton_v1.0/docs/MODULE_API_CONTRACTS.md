# Current Module API Contracts

**Document revision:** 1.0

**Updated:** 2026-11-02
**Scope:** Current public API/result, ownership and important limitations for
the modules changed or audited in this repository. This is not a hardware or
system-requirement approval.

## Conventions and boundaries

- The “current” column describes source behavior, not intended future work.
- Host tests exercise fakes and synthetic configurations. They do not prove
  target deadlines, storage durability, physical output state, model validity,
  or production approval.
- Unless explicitly stated, APIs are synchronous, task-context operations;
  ISR safety and concurrent access are not guaranteed.
- Non-owning references/views must outlive the operation and any persisted or
  runtime selection that uses them. Do not retain borrowed pointers unless the
  API specifies ownership.
- Do not infer success from a cleared output, default enum, empty buffer, or
  `false`. Preserve module-specific return contracts until a coordinated API
  migration is reviewed.
- Detailed behavior and evidence: `PLACEHOLDER_STATUS.md`,
  `REQUIREMENT_TRACEABILITY.md`, and the owning module document.

## Current API summary

| Module/API | Current contract | Evidence/limit |
|---|---|---|
| `AppController(IAppLifecycle&, AppControllerConfig)` | Non-owning lifecycle dependency. Config requires nonzero health-check, drain, and storage-flush timeout values. `begin()`, `runHealthChecks()`, `requestShutdown()` return `AppControllerStatus`. State/accessors expose protocol state and inference gate. Startup proceeds in fixed order; failed phase is included in reverse cleanup. Shutdown disables inference, stops accepting work, drains tasks, cleans up in reverse, flushes before Storage cleanup, and requests safe outputs before BSP cleanup. Repeated begin while running and repeated shutdown after shutdown are idempotent. | Host fake tests cover phase order/failure, gate, cleanup, health, shutdown and flush failure. Port implementations are synchronous and must enforce their own bounds. No concrete adapter; `TaskFramework` has no stop/drain API, `StorageManager::flush()` has no timeout, and `main.cpp` does not use AppController. |
| `ConfigManager()` | Default-constructed instance has no persistence adapter. Persistence calls fail closed. | Production `main.cpp` still default-constructs it; startup remains blocked. |
| `ConfigManager(IConfigManifestPersistence&)` | Non-owning adapter. `loadLastValid()` loads the newest valid committed slot. `storeCandidate()` validates before writing candidate → flush → read-back → validate → commit marker → marker flush. `validateManifest()` validates the selected candidate. `activateAtomically()` returns true only for valid `Production` purpose and exposes the selected view through `activeManifest()`; it does not apply data to runtime modules. Static `validateManifest(manifest, report)` is pure. | Host fake tests inject failures at write/flush/read-back/marker boundaries and test fallback. Adapter must explicitly encode fields and own storage for returned views. No concrete adapter, schema field encoder, hardware power-loss result, or durability claim. |
| `CalibrationService::loadCalibrationStatus()` | Returns `ApiStatus::NotInitialized`; no calibration source or identity calibration is configured. `loadCalibration()` is a bool facade and returns false. | Host tests exercise fail-closed validity/raw propagation only. No calibrated physical output. |
| `CalibrationService::processStatus(input, output)` | Copies provenance/raw-channel validity into caller-owned output and returns explicit invalid/schema/calibration status. It does not apply coefficients or make invalid values valid zero. `process()` returns true only on `ApiStatus::Ok`. | Bounded in-memory operation; input borrowed/output caller-owned. Calibration version with no configured calibration is rejected. |
| `SchemaRegistry::compareDetailed(report)` / `validateDetailed(vector, report)` | Compare configured descriptor/model metadata; detailed APIs report status and mismatch field/reason. `compatible()` and `validate()` are bool facades. Test-only schema IDs are not production-compatible. | Host tests use synthetic descriptors. Actual released FSS and Edge Impulse metadata are absent; no production inference gate is currently wired. |
| `OutputController::deriveCommand(assessment)` | Pure deterministic mapping; returns an `OutputCommand` with kind/status/feedback. No GPIO, active level, pin, duration, buzzer/relay, hysteresis, or debounce behavior. Unknown/invalid stays explicit. | Host tests cover states, invalid inputs, repeats, and absent/unverifiable feedback. No claim that an output was physically actuated. |
| `RecoveryManager::classifyError(status, context, retryPolicy, retriesAlreadyUsed)` | Pure classification over existing status/fault types. Retry requires an injected maximum and recovery availability; schema/model mismatch inhibits inference. No device action is executed. | Host tests cover known/unknown codes, retry limit/disabled/unavailable, and mismatch. No I2C recovery or restart. |
| `StorageManager(IStorage&, config)` | Non-owning backend. Maximum record size must be nonzero for `begin()`. `appendRecord()` is synchronous, rejects null/zero/oversized input, borrows input until return, and does not queue/copy. Backend false maps to `IoError` and Faulted; re-begin may recover. `shutdown()` is logical only. | Fake-backend host tests cover lifecycle, capacity, errors, partial-write limitation, flush and shutdown. `IStorage` has bool-only `begin/append/flush`, no timeout, byte count, close, or detailed error. Do not claim bounded physical I/O or durability. |
| `Serialization` record encode/decode functions | `EventRecord` JSONL v2 is the only implemented format, field-by-field with bounded buffers and validation. Other record/format combinations return `NotImplemented` (invalid pointers return explicit argument error). No raw struct memcpy. | Host tests cover round-trip, buffer/input errors, truncation/trailing bytes, type/version/enums, non-finite policy and boundaries. CSV and binary remain separate/unimplemented. |

## Cross-module data and lifecycle rules

- Inputs passed by pointer/reference are borrowed; mutable outputs belong to the
  caller unless an adapter explicitly documents a longer lifetime.
- ConfigManifest itself stores non-owning pointers. The
  `IConfigManifestPersistence` implementation must own decoded records and
  artifact bytes for the lifetime of returned views. Never persist pointers or
  raw C++ struct memory.
- Unknown/invalid sensor, risk, schema, model, or output state must not be
  normalized into valid zero, NORMAL, or successful physical actuation.
- RiskState and HealthState remain distinct.
- AppController timeout values are caller-provided policy inputs; no default
  target value is approved. A synchronous port cannot be forcibly interrupted
  by AppController.
- `StorageManager` and `CsvLogger` are distinct interfaces/owners. `EventLogger`
  owns event queue-facing behavior; no responsibility overlap is claimed.

## Open API/integration change requests

1. Task Framework owner: provide stop-new-work, bounded task drain, and stop
   operations suitable for AppController shutdown.
2. Storage owner: define genuinely bounded flush/completion semantics, backend
   error detail, and any required close behavior. Do not modify the current
   interface without coordinated review.
3. Config/integration owner: provide approved persistent field encoding,
   adapter ownership/lifetime, and production composition.
4. App integration owner: map each lifecycle phase to real module status and
   ensure inference cannot execute before the gate opens.
5. BSP/output owner: define and verify physical safe-output state and feedback
   before claiming actuation.
6. Requirement owner: provide controlled DOP/DSD/FSS/ICD/LLD/TCS/GTIS sources
   to resolve any conflict; this document does not revise them.

## Change history

| Revision | Date | Change |
|---|---|---|
| 1.0 | 2026-11-02 | Replaced stale skeleton-era API claims with current signatures, behavior, test limits, and integration blockers. |
