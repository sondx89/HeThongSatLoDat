# Config Manifest and Persistence

**Document revision:** 1.1

**Updated:** 2026-11-02
**Status:** Host-tested protocol; no production persistence adapter.

## Scope and status

`ConfigManager::validateManifest(const ConfigManifest&, ValidationReport&)` is
a pure, host-testable validator. `ConfigManager` also supports two-slot
candidate persistence through the injected `IConfigManifestPersistence`
abstraction. The manager performs no file access, hardware access, allocation,
or mutation of module-owned configuration objects.

The supported manifest schema version is
`ConfigManager::kSupportedManifestVersion` (`1`). The persistence adapter's
logical record is a nonzero generation plus the complete version-1
`ConfigManifest`: purpose and production approval, the values of each
referenced module configuration, and the exact bytes of each supplied
artifact. The adapter must encode each field explicitly; it must not persist
C++ struct bytes or pointers. The format is specific to ConfigManager and
does not change the general `Serialization` schema.

The record body and commit marker are distinct. A body is not selectable until
the adapter reports it as `Committed`; slot selection never relies on a
separate mutable active-slot pointer. Physical encoding and durability remain
the responsibility of a concrete adapter and have not been verified for
hardware.

The default-constructed manager has no adapter and all persistence/activation
methods fail closed. The production startup caller still uses this constructor,
so production startup remains blocked until an approved adapter is integrated.

`activeManifest()` exposes the selected manifest only after
`activateAtomically()` accepts a valid `Production` manifest. This is a
ConfigManager-level selection only; it does not apply configuration to runtime
modules or authorize hardware operation.

## Persistence adapter contract

`IConfigManifestPersistence` exposes two slots and separates these operations:

1. `writeCandidate(slot, generation, manifest)` invalidates any old marker in
   the target slot before changing its body. The caller chooses only the slot
   opposite the newest valid committed record.
2. `flushCandidate(slot)` completes the candidate body write.
3. `readSlot(...)` reads the body back. A candidate is returned as `Candidate`
   before commit and as `Committed` only after a valid marker. `Corrupt` is
   distinct from `IoError`; only known-corrupt records may be skipped during
   fallback. The adapter owns decoded configurations and artifact bytes for
   the lifetime of returned views.
4. The manager validates the read-back manifest with the same pure validator.
   Invalid input or read-back never receives a commit marker.
5. `writeCommitMarker(slot, generation)` followed by
   `flushCommitMarker(slot)` completes the commit boundary. The adapter must
   bind the marker to the slot generation and make torn/partial markers
   unreadable as committed.
6. On load, the manager selects the highest-generation committed record that
   passes validation. A tie, adapter I/O error, or absence of a valid record
   fails closed. Empty, uncommitted, and corrupt slots are ignored, allowing a
   valid previous slot to be selected after an interrupted or corrupt newer
   candidate.

Before commit, the selected slot is never the write target, so failures while
writing, flushing, reading back, validating, or writing/flushing the marker
leave the prior valid record eligible. A power loss during the commit boundary
may leave either the prior record or the fully committed candidate selectable;
the adapter must never expose a partial marker as committed. These guarantees
are protocol requirements, not durability claims about a hardware medium.

`storeCandidate()` rejects a candidate that fails validation before writing,
then executes the full write/flush/read-back/validate/commit sequence. A valid
candidate can be stored without being activated. `loadLastValid()` and
`storeCandidate()` clear any previous runtime activation when changing the
selected manifest. `activateAtomically()` requires a selected, valid
`Production` manifest; the existing validator enforces production approval,
model approval/verification, artifact hashes, and FSS/model compatibility.
Test-only manifests can be validated and persisted for host tests but cannot be
activated.

## Configuration ownership

`ConfigManifest` is a non-owning aggregate of pointers to existing module
configuration types. It does not duplicate their fields:

| Manifest member | Existing owner |
|---|---|
| `featurePipeline` | `services::FeaturePipelineConfig` |
| `model` | `ai::EdgeImpulseModelConfig` |
| `risk` | `managers::RiskManagerConfig` |
| `dataQuality` | `services::DataQualityConfig` |
| `sensorManager` | `managers::SensorManagerConfig` |

`featurePipeline`, `model`, and `risk` are required for both manifest purposes.
`dataQuality` and `sensorManager` are required for `Production`; a `TestOnly`
fixture may omit them to exercise the pure compatibility checks without
claiming a complete production configuration. All referenced objects and
artifact bytes must remain alive and unchanged for the duration of validation.

Board configuration remains owned by BSP and is not copied into
`ConfigManifest`. The existing production startup path checks BSP board/bus
configuration before reaching `ConfigManager`; this validator does not replace
that check.

## Validation behavior

The validator resets the supplied report on entry and records up to 32
fixed-capacity issues. If more issues are found, `truncated` is set and the
report is still invalid. Each issue includes a stable `ManifestErrorCode` and
field path. The returned status uses existing `models::ApiStatus`:

- `Ok`: no validation issues.
- `VersionMismatch`: at least one unsupported manifest format version.
- `DataInvalid`: missing fields, invalid values, failed approvals, or
  inconsistent configuration.

Issue codes are diagnostic detail, not a second API status enum:

| Code | Meaning |
|---|---|
| `MissingField` | Required pointer/value/hash is absent or not configured. |
| `UnsupportedVersion` | Manifest format version is not supported. |
| `InvalidEnum` | An enum contains a value not declared by its owning type. |
| `NonFinite` | A configured float/double is NaN or infinity. |
| `OutOfRange` | A value violates a capacity, positive-value, or ordered-bound requirement. |
| `CrossFieldMismatch` | Two existing module configurations disagree. |
| `ApprovalRequired` | Production approval or module approval is incomplete. |
| `ArtifactHashMismatch` | SHA-256 of supplied artifact bytes differs from the declared hash. |
| `InvalidArtifact` | Artifact pointer/length is inconsistent or cannot be hashed. |

The validator reuses `FeaturePipeline::validateConfig()` and
`RiskManager::validateConfig()` rather than reproducing those modules' policy
checks. It additionally checks:

- manifest version and purpose enum;
- finite feature, data-quality range, and risk numeric values;
- feature ID/output-kind enum values and configured capacity bounds;
- equality of FSS schema version, ID, and hash between feature and model
  configuration;
- `featureCount * windowLengthSamples == model.inputDimension`;
- risk expected model output count against model output metadata; and
- the current risk implementation's scalar-regression dependency when its
  model rule is enabled.

It does not invent board pins, addresses, bus clocks, feature definitions,
sampling/window values, calibration coefficients, risk thresholds, storage
layout, or task sizing. The existing configuration validators require their
relevant configured/approved flags; unresolved fields stay unset and prevent
validation.

## Artifact hash policy

Each `ManifestArtifactView` is absent only when both `data == nullptr` and
`size == 0`. If either indicates presence, the view must have a non-null data
pointer and nonzero size. The validator computes SHA-256 over exactly the
provided bytes using fixed local storage and compares against the existing
owner's declared hash:

- `featureSchemaArtifact` against both `FeaturePipelineConfig::schemaHash` and
  `EdgeImpulseModelConfig::featureSchemaHash`;
- `modelArtifact` against `EdgeImpulseModelConfig::modelHash`; and
- `riskConfigArtifact` against `RiskManagerConfig::hashSha256`.

No artifact hash is computed or required for an absent view. Production-purpose
validation requires all three artifact views. A `TestOnly` fixture may omit
them; this is not evidence that a production artifact exists or is approved.
Hash equality is an integrity comparison, not signature/authenticity
verification. Artifact selection, provenance, approval, and secure loading
remain outside this validator.

## Production gate

Production-purpose validation additionally requires:

- `productionApprovalComplete == true`;
- non-null quality and sensor-manager configs;
- all model, feature pipeline, and risk approval/verification flags; and
- actual schema, model, and risk artifact bytes whose SHA-256 values match the
  hashes in their existing owner configs.

`activateAtomically()` returns true only for a selected manifest that passes
all these checks. It only exposes the selected view through `activeManifest()`;
it does not copy values into live module configuration or provide atomic
runtime reconfiguration.

## Test evidence and limitations

The `TestManifestFixture` in
`test/test_system_contracts/test_system_contracts.cpp` is explicitly
test-only. It covers valid test input, missing fields, unsupported version,
invalid enum, NaN/Inf, out-of-range dimensions, model/FSS mismatch, supplied
artifact hashes, and the production activation gate. A fake
`IConfigManifestPersistence` additionally injects failures at candidate write,
candidate flush, read-back, commit-marker write, and commit-marker flush;
reboot-style manager recreation verifies the old committed record remains
selectable. Tests also cover invalid candidates, successful commit, and
fallback from an invalid newest record.

`native-unit-tests` has an explicit source filter that does not include
`src/Managers/ConfigManager.cpp`. To exercise the real implementation without
changing the out-of-scope `platformio.ini`, that test translation unit includes
the `.cpp` under a `TEST ONLY` marker. Production firmware does not compile
tests. No concrete persistent-storage adapter, hardware durability,
authenticity/signature, or production activation claim follows from these host
tests.

## Change history

| Revision | Date | Change |
|---|---|---|
| 1.1 | 2026-11-02 | Added audit status/date and clarified production adapter remains absent. |
| 1.0 | 2026-11-02 | Documented ConfigManager logical two-slot protocol and fake fault-injection evidence. |
