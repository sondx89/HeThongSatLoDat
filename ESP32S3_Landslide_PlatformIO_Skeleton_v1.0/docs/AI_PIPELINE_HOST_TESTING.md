# AI/Data Host-First Workflow

**Status:** Host software validation only; field release is NOT READY.  
**Updated:** 2026-11-02

## Scope and evidence boundary

The current host-first workflow validates deterministic data handling, bounded
CSV operations, explicit invalidity, and fail-closed interfaces. It does not
validate sensor physics, physical units, calibration, model accuracy, risk
thresholds, or field safety.

The synthetic generator is a host API, not a standalone CLI. It emits
deterministic raw observations and a separate latent scenario state with
`data_origin=SYNTHETIC` and `evidence_class=GT5`. The scenarios are
software fault/behavior fixtures, not field magnitudes or event frequencies.
No labels or proxy targets are generated.

The current A14 integration follows:

1. Generate deterministic synthetic raw records.
2. Classify data quality; exercise `CalibrationService` in `RAW_ONLY` mode.
3. Keep physical channels unavailable without approved calibration, so
   `FeaturePipeline` rejects the feature input/window.
4. Export DSD v1.2 raw-core CSV through `DsdRawSampleCsvExporter`, preserving
   exact core header/order and validity/quality fields.
5. Validate byte count, SHA-256, row count, identity, channel presence,
   validity/quality, and sequence continuity using `DatasetValidator`.
6. Parse the exported CSV and replay rows through the same raw-only quality
   and calibration behavior; compare raw identity, values and resulting
   quality states.
7. Verify annotation interval identity and evaluation reporting. Undefined
   target semantics leave regression metrics disabled.
8. Verify production schema compatibility, Edge Impulse inference, and the
   default risk policy remain closed.

This is raw-only deterministic replay, not an end-to-end model validation.
The DSD core has no per-channel acquisition timestamp, so archived rows cannot
reconstruct stale age for individual channels. A keyed, versioned sidecar is
needed if exact per-channel timing must be replayed.

## CSV artifacts and schema versions

- `raw_samples.csv`: bounded DSD v1.2 core export; the current artifact schema
  constant is `kRawSampleCoreSchemaVersion=1`. Provenance for a uniform-origin
  export is in the typed manifest, not an added CSV column.
- `calibrated_samples.csv`, `windows.csv`, `features.csv`, `annotations.csv`,
  and row-level `evaluation.csv`: separate artifact contracts; corresponding
  general-purpose exporters are not implemented.
- `evaluation_summary.csv` and `feature_distributions.csv`: bounded aggregate
  reports from `EvaluationRunner`, not substitutes for row-level evaluation.
- Operational six-column `CsvLogger` output and mock serial diagnostics stay
  separate; neither is the DSD raw training archive.

Controlled document versions (DSD v1.2, DOP v1.0, FSS v1.1, GTIS v1.0) are not
artifact schema versions. Do not edit the supplied `Document/*` sources or
infer approval from an implementation version.

## Commands and latest recorded evidence

Run commands from `ESP32S3_Landslide_PlatformIO_Skeleton_v1.0`. In the observed
Windows environment, PlatformIO Core 6.2.0 and MSYS2 UCRT64 GCC/G++ 16.2.0 were
used; prepend the compiler directory if it is not already on `PATH`:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
pio test -e native-unit-tests
pio test -e native-integration-tests
pio run -e esp32-s3-devkitc-1
```

Latest recorded validation for the A14 exporter/replay changes:

| Command | Exit | Result |
|---|---:|---|
| `pio test -e native-unit-tests` | 0 | 103 test cases passed |
| `pio test -e native-integration-tests` | 0 | 7 test cases passed |
| `git diff --check` | 0 | No whitespace errors |
| `pio run -e esp32-s3-devkitc-1` | 1 | Target compilation blocked at `include/project/BSP/BoardConfig.h:102`: `issueMask` undeclared |

The target environment resolved PlatformIO `espressif32` 8.0.0, Arduino
framework 3.3.12, and Xtensa toolchain 14.2.0+20260121. No firmware upload
was attempted. The compile blocker is not evidence that the A14 host code
failed; nor is it evidence that the target build otherwise passes.

`pio run -e mock-native -t exec` runs the separate sensor-only mock acquisition
demo. It is not a DSD exporter or the synthetic scenario pipeline.

## Outstanding prerequisites

- Actual Edge Impulse generated deployment package and verified metadata are
  absent; production inference remains fail-closed.
- Approved physical calibration, sensor identity/units, and eligible FSS
  feature contract are absent; feature-to-inference integration is blocked.
- The target/regression contract and approved `RiskConfig` are absent; no
  operational risk threshold or field-accuracy claim is supported.
- JSON manifest/sidecar persistence, calibrated/window/feature/annotation
  exporters, and row-level evaluation export are not implemented.
- No physical hardware, HIL, model golden-vector, target resource, or field
  safety evidence is available.

Field release remains **NOT READY** until hardware, calibration, actual model,
target semantics, and safety evidence are supplied and validated.
