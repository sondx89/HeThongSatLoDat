# Storage responsibilities and lifecycle

## Audit and responsibility split

The current storage components have separate contracts and do not call each
other:

| Component | Owns | Does not own |
|---|---|---|
| `IStorage` | Minimal synchronous backend seam: `begin`, opaque `append`, `flush`, each reported as `bool`. | Record encoding, paths/filenames, rotation, retention, recovery, detailed backend status, or a close operation. |
| `StorageManager` | Lifecycle gating and bounded forwarding of already-serialized opaque byte records to an injected `IStorage`. Maps backend success/failure to existing `ApiStatus`. | Record formats, queues, file layout/naming, rotation, retention, recovery, or hardware initialization details. |
| `CsvLogger` / `ICsvFileSystem` | CSV row serialization; CSV header, file naming/index policy, exclusive creation, rotation limit, recovery scan/truncation, per-file append and flush; owns its own filesystem adapter. | Generic opaque storage records or `StorageManager` lifecycle. It does not use `IStorage`. |
| `EventLogger` / `IEventOutput` | Event queue-facing API and event-to-output formatting; sends CSV output through its event output seam and owns bounded formatting behavior. | Generic file lifecycle, SD/filesystem behavior, rotation, or `StorageManager`. |

No ownership overlap was found that requires changing `CsvLogger` or
`EventLogger`. They remain untouched. Do not route their writes through
`StorageManager` without a separate design that replaces rather than duplicates
their backend ownership.

## StorageManager API

`StorageManager` takes a non-owning `IStorage&` and immutable
`StorageManagerConfig::maximumRecordBytes`. The backend must outlive the
manager. A maximum of zero is invalid for `begin()` and leaves the manager
uninitialized; no project-wide size is chosen here. An integration must inject
a limit derived from its approved memory/storage budget.

`appendRecord(data, size)` accepts only non-null, non-empty records whose size
does not exceed the injected limit. Bytes are opaque; the manager does not
serialize model structs, add framing, or copy into an internal queue. The
caller retains ownership and must keep the bytes alive and immutable until
the synchronous call returns. The backend must consume/copy them before
returning and must not retain the pointer.

The operation is synchronous and task-context only. Its blocking bound is
whatever the injected backend guarantees; `IStorage` has no timeout parameter,
so a finite timeout cannot be enforced by this manager. Do not call from an
ISR. The class is single-owner and provides no internal locking or concurrent
call guarantee.

## State and error semantics

| State before call | Operation/result | State after |
|---|---|---|
| `Uninitialized` | `begin()` with a nonzero record limit and backend success → `ApiStatus::Ok` | `Ready` |
| `Uninitialized` | `begin()` with limit zero → `InvalidArgument`; backend is not called | `Uninitialized` |
| `Uninitialized` | backend `begin()` false → `IoError` | `Faulted` |
| `Ready` | repeated `begin()` → `Ok`; backend is not called again | `Ready` |
| `Ready` | valid append / successful flush → `Ok` | `Ready` |
| `Ready` | backend append/flush false → `IoError` | `Faulted` |
| `Faulted` | append/flush → `IoError`; backend is not called | `Faulted` |
| `Faulted` | `begin()` success → `Ok` | `Ready` |
| Any non-shutdown state | `shutdown()` → `Ok` | `Shutdown` |
| `Shutdown` | `begin()`, append, or flush → `Cancelled`; backend is not called | `Shutdown` |

Null data and zero-length records return `InvalidArgument`. Records exceeding
the configured maximum return `ResourceExhausted`. Calls made before a
successful `begin()` return `NotInitialized` (after argument/size validation).
Backend `false` maps to `IoError` because the interface carries no finer error
status.

### Partial writes and backend failure

`IStorage::append()` returns only `bool`, so the manager cannot know how many
bytes were committed or distinguish a partial write from another backend
failure. `false` is never reported as success: it returns `IoError` and moves
the manager to `Faulted`, preventing subsequent writes until `begin()` reports
success. Re-beginning does not repair or roll back bytes already written. A
backend must therefore define how its own `begin()` behaves after partial
failure before production use.

### Flush, shutdown, and durability

`flush()` synchronously forwards to the backend only in `Ready`. Success means
only that `IStorage::flush()` returned true; this does not establish that bytes
survive power loss or that a physical medium has committed them durably.

`IStorage` has no close/shutdown operation. `shutdown()` only disables this
manager instance; it does not flush, close, release, or power down the
externally owned backend. A new manager/backend lifecycle is required to resume
after shutdown. No SD hardware backend, filenames, pins, rotation, retention,
or physical durability claim is defined here.

## Validation

Host tests use a fake `IStorage` to cover initialization, repeated begin,
begin failure/retry, append-before-begin, argument and size limits, successful
append, partial and generic backend failures, flush failure, fault recovery,
and write-after-shutdown. A fake partial append demonstrates that the manager
fails closed; it does not add a byte-count capability to the production
`IStorage` interface.

Hardware backend integration remains blocked until its ownership, timeout,
partial-write, flush/durability, storage layout, and platform-specific
requirements are reviewed.

## P14 runtime integration status

The P14 runtime path is not yet composed: `TaskLogger` delegates through
`ITaskLogWriter`, but there is no production writer joining Q_LOG/Q_EVENT to
`CsvLogger` or a physical storage adapter. `StorageManager` remains a separate
opaque-byte API and must not be presented as if it were the CsvLogger backend.
Host fakes validate component behavior only; they do not prove bounded SD-card
operations, durable flush, queue-to-file delivery, or recovery on the target.
