# Config Manifest and Pure Validation

## Scope and status

`ConfigManager::validateManifest(const ConfigManifest&, ValidationReport&)` is
a pure, host-testable validator. It reads caller-owned data and optional
caller-owned artifact buffers; it performs no file access, persistence,
activation, hardware access, allocation, or global-state mutation.

The format version currently supported by this in-memory validation contract
is `ConfigManager::kSupportedManifestVersion` (`1`). This is not a released
serialized manifest format and does not specify a storage layout.

The existing no-argument methods remain unchanged and fail closed:

- `loadLastValid()` returns `false`.
- `validateManifest()` returns `false`.
- `activateAtomically()` returns `false`.

The production caller still uses those methods, so production startup remains
blocked. A successful pure validation, including a `Production`-purpose
manifest, does not authorize or perform activation.

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

Even when these checks pass, `activateAtomically()` remains a stub returning
`false`. No persistence, two-slot activation, rollback, or power-loss behavior
is implemented by this task.

## Test evidence and limitations

The `TestManifestFixture` in
`test/test_system_contracts/test_system_contracts.cpp` is explicitly
test-only. It covers valid test input, missing fields, unsupported version,
invalid enum, NaN/Inf, out-of-range dimensions, model/FSS mismatch, supplied
artifact hashes, and the production activation gate.

`native-unit-tests` has an explicit source filter that does not include
`src/Managers/ConfigManager.cpp`. To exercise the real implementation without
changing the out-of-scope `platformio.ini`, that test translation unit includes
the `.cpp` under a `TEST ONLY` marker. Production firmware does not compile
tests. No hardware, persistent-storage, authenticity/signature, or production
approval claim follows from these host tests.
