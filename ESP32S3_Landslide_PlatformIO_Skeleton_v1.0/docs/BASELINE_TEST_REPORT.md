# Baseline Test Report

**Audit date:** 2026-11-13

**Project:** ESP32-S3 Landslide Monitoring PlatformIO skeleton  
**Scope:** Clean host tests, ESP32-S3 compile-only build, and focused AI/data
inventory. This report is evidence for the current worktree, not release,
target/hardware acceptance, or model validation.

## Environment observed

| Item | Observed value |
|---|---|
| OS | Windows |
| Python | 3.11.9 |
| PlatformIO Core | 6.2.0 |
| Native PlatformIO platform | `native` 1.2.1 |
| Host compiler | MSYS2 UCRT64 `g++` 16.2.0; `gcc.exe` and `g++.exe` exist under `C:\msys64\ucrt64\bin` |
| ESP32 platform | `espressif32` 8.0.0, resolved from the project environment |
| Arduino framework | `framework-arduinoespressif32` 3.3.12 |
| ESP32-S3 compiler | `toolchain-xtensa-esp-elf` 14.2.0+20260121 |
| Project language/framework | C++17 / Arduino, as configured in `platformio.ini` |

`platformio.ini` does not pin a version for the `espressif32` platform. The
version above is what PlatformIO resolved in this environment on the audit date,
not a version constraint in the repository.

## P01 results snapshot — 2026-11-13

The commands were run from the repository root. Native tests ran sequentially
because the environments share `build_output`. For the native commands,
`C:\msys64\ucrt64\bin` was added to `PATH`; no dependency was installed.

| Environment | Exact command | Result |
|---|---|---|
| Native unit tests | `pio test -d ESP32S3_Landslide_PlatformIO_Skeleton_v1.0 -e native-unit-tests` | **PASS, exit 0** — 115/115 test cases succeeded. |
| Native integration tests | `pio test -d ESP32S3_Landslide_PlatformIO_Skeleton_v1.0 -e native-integration-tests` | **PASS, exit 0** — 7/7 test cases succeeded. |
| ESP32-S3 compile-only build | `pio run -d ESP32S3_Landslide_PlatformIO_Skeleton_v1.0 -e esp32-s3-devkitc-1` | **FAIL, exit 1** — `SerialDiagnostics::buildPayload` declaration/definition/return-type mismatch and five `HealthSnapshot` assignments from `{}` fail to compile. `BoardConfig.cpp` compiled; `issueMask` is no longer an error. |
| Worktree whitespace check | `git diff --check` | **PASS, exit 0** — no whitespace errors. |

PlatformIO resolved the ESP32-S3 environment and started target compilation.
No firmware upload or physical hardware test was attempted. Host test results
do not establish target or hardware behavior.

## Latest P02 results — 2026-11-13

After P02 corrected the source compile errors, both native suites were rerun
and the target build completed through a temporary ASCII drive alias. The
alias avoids a linker map-file path failure caused by the Japanese characters
in the workspace path; it was removed after the build.

| Environment | Exact command | Result |
|---|---|---|
| Native unit tests | `pio test -d ESP32S3_Landslide_PlatformIO_Skeleton_v1.0 -e native-unit-tests` | **PASS, exit 0** — 116/116 test cases succeeded. |
| Native integration tests | `pio test -d ESP32S3_Landslide_PlatformIO_Skeleton_v1.0 -e native-integration-tests` | **PASS, exit 0** — 7/7 test cases succeeded. |
| ESP32-S3 compile/link/image build | `subst Z: "<project path>"; pio run -d Z:\ -e esp32-s3-devkitc-1; subst Z: /d` | **PASS, exit 0** — target image generated. PlatformIO size report: 23,544 bytes RAM and 349,425 bytes Flash. These are build estimates, not runtime measurements. |

No firmware upload or physical hardware test was attempted. The configured
PlatformIO board ID is not evidence that the physical board has been verified.

## Execution scope distinctions

| Scope | Environment / command | P01 evidence |
|---|---|---|
| Native host unit tests | `native-unit-tests` | Latest P02 run: 116/116 passed. These use host compiler and test fixtures/fakes. |
| Native host integration tests | `native-integration-tests` | Run; 7/7 passed. Synthetic/mock fixtures are not physical sensor evidence. |
| Mock firmware | `esp32-s3-mock` | Not run in P01. Its test-only mock branch is not production sensor evidence and was not flashed. |
| Target firmware compile | `esp32-s3-devkitc-1` | Latest P02 compile/link/image generation passed through an ASCII alias. No upload. |
| Physical hardware / HIL | Board and sensors | Not run; no physical result is claimed. |

## Historical test and build evidence

The following results are retained as historical snapshots and are not the
latest counts:

| Historical run | Recorded result |
|---|---|
| 2026-11-02 A00 clean native rerun | 71 unit and 3 integration cases passed. |
| 2026-11-02 later A14 validation | 103 unit and 7 integration cases passed. |
| 2026-11-02 target build | Failed because `issueMask` was used before declaration in `BoardConfig.h`; the current source has that declaration before first use. |
| 2026-10-30 native unit run | 16 test cases passed at that earlier revision. |
| Initial direct native attempts without MSYS2 on `PATH` | Failed before tests ran because `gcc`/`g++` were not found; the compiler was installed and later runs used the explicit PATH addition. |

## Test selection and coverage boundary

- `native-unit-tests` selects `test_system_contracts` and compiles the configured
  source filter plus source files directly included by the test translation
  unit under `TEST ONLY` markers. This lets host tests exercise additional
  modules without changing the configured production source filter.
- `native-integration-tests` selects `test_system_flow` and builds
  `SensorManager`, `RiskManager`, `DataQualityService`, `FeaturePipeline`,
  `MockModelRuntime`, `MockCsvTelemetry`, and `CsvLogger`.
- The unit suite includes fake/test-only coverage of AppController,
  ConfigManager persistence protocol, CalibrationService, SchemaRegistry,
  OutputController, RecoveryManager, EventRecord JSONL, and StorageManager.
- Neither suite tests an actual EdgeImpulseAdapter generated-model build,
  BSP watchdog/power operation, production persistence backend, or physical
  hardware.

## Focused AI/data inventory

The following inventory and results capture the initial A00 baseline before
the subsequent A01-A14 implementation work. They are retained as a historical
baseline; the current A14 implementation/validation delta is recorded below.

| Module | Current behavior and owner | Evidence and gap |
|---|---|---|
| `Services/FeaturePipeline` | Owns fixed-capacity feature windows and extraction; validates configured schema, provenance, quality and continuity. Supports explicitly configured acceleration magnitude and candidate tilt formulas; unsupported raw channels are rejected. | Native unit and synthetic integration coverage. Default configuration is closed; no released FSS/model input, feature order, units, window or stride exists. `FeatureVector::kMaxFeatures` is capacity, not a model dimension. |
| `Managers/RiskManager` | Owns risk classification and hysteresis; requires explicit approved configuration and valid model/tilt/quality inputs. | Native unit and integration coverage. Default configuration does not produce SAFE; target semantics and operational thresholds are not approved. |
| `AI/SchemaRegistry` | Owns schema/model metadata compatibility checks, including identity/hash, count/order/units and preprocessing/window metadata. | Native tests use fixtures. No released FSS or actual model metadata exists for production compatibility. |
| `AI/EdgeImpulseAdapter` | Owns the conditional generated Edge Impulse runtime boundary. Without the generated model header it fails initialization closed. | `testEdgeImpulseAdapterExposesMetadataAndFailsClosedWithoutExport` covers the missing-export gate; no generated deployment export/build evidence exists. Actual model integration is blocked. |
| `Dataset/CalibratedSampleCsvExporter` | Bounded host-only calibrated artifact serializer; keeps raw/calibrated fields distinct and reports RAW_ONLY explicitly. | Unit tests cover RAW_ONLY, a test-only calibration fixture, range/non-finite values, and calibration identity/version/hash mismatch. It does not apply or approve physical calibration. |
| `Storage/CsvLogger` | Owns a bounded six-column operational/diagnostic CSV: `timestamp,tilt,soil,rain,stress,risk`; storage is injected. | Integration coverage uses an in-memory fake. It is not the DSD raw-sample schema and lacks its provenance/quality fields. |
| `Replay/ReplayEngine` | Owns bounded replay of that exact six-column CSV and emits vectors only with externally verified manifest/schema/mapping configuration. | Native unit tests cover replay. It cannot reconstruct original boot/sequence, raw channels, per-channel quality or calibration provenance. It is not a DSD raw CSV parser. |
| `Drivers/MockCsvTelemetry` | Owns the separate synthetic diagnostic serial stream; missing values are empty and `dataset_eligible=false`. | Integration tests cover the header/row/missing-value behavior. It is not a dataset export and does not provide the DSD provenance contract (`data_origin`, `evidence_class`). |
| `Services/DataQualityService` | Sole owner of configured channel-quality classification and propagation; invalid values are not promoted to valid zero. | Native unit tests. Production freshness/range/required-channel policy remains unconfigured. |
| `Services/CalibrationService` | Owns current raw/source-quality propagation; calibration loading returns `NotInitialized`, with no identity conversion or coefficients. | Native unit tests. No approved calibration artifact or physical conversion exists. |
| `Models/Serialization` | Owns field-by-field EventRecord JSONL v2 encode/decode; no raw C++ struct dump. Other record codecs and CSV/binary formats are unsupported. | Native unit tests cover EventRecord. It is not a general DSD dataset serializer. |

At the historical A00 baseline, no dataset-grade synthetic source/scenario manifest,
DSD raw exporter or strict DSD raw parser/validator, annotation manager,
leakage-safe splitter, or evaluation runner was found. Existing `MockSensors`
and mock serial output were test/diagnostic fixtures, not field evidence.
Subsequent implementation has added these host/data components; see the A14
delta below for exact current coverage and remaining limitations.

## Not run / evidence limits

- **Hardware tests:** not run; no physical board/peripheral test is evidenced
  by these host results.
- **Generated model tests:** not run; no actual Edge Impulse deployment export
  or approved model metadata is present.
- `build_project.bat all`, upload, serial monitor, and `production-build` were
  not run. The default script action includes mock firmware upload/monitor and
  is not a host-only test command.

Native tests establish host behavior only. The latest ESP32-S3 compile/link
passed after P02 fixes, but no firmware was flashed or exercised on hardware.
None of these results establish peripheral operation, model compatibility,
risk-policy approval, HIL, or product safety.

## Historical A14 host validation delta (2026-11-02)

After the A00 snapshot, A14 added a bounded host DSD raw-core exporter and a
deterministic raw-only export/validate/parse/replay integration path. The
latest recorded test run for that implementation used PlatformIO Core 6.2.0
and MSYS2 UCRT64 GCC/G++ 16.2.0:

| Exact command | Exit | Result |
|---|---:|---|
| `pio test -e native-unit-tests` | 0 | 103 test cases succeeded. |
| `pio test -e native-integration-tests` | 0 | 7 test cases succeeded, including raw-only synthetic export/replay. |
| `git diff --check` | 0 | No whitespace errors in the recorded validation. |

These are historical host-native results only. The target compile recorded at
that time failed because `issueMask` was undeclared; the latest target result
is reported above and fails on different diagnostics/health-monitor errors.
The A14 path does not perform eligible feature inference, does not validate an
actual model, and does not establish HIL, field, accuracy, or safety evidence.
Controlled `Document/*` sources remain inputs and were not modified.

## Change history

| Revision | Date | Change |
|---|---|---|
| 1.5 | 2026-11-13 | Added post-P02 116/7 native results and successful target compile/link via temporary ASCII drive alias. |
| 1.4 | 2026-11-13 | Added current P01 test/build results, marked older counts as historical, and updated the exporter and target-blocker inventory. |
| 1.3 | 2026-11-02 | Added A14 host exporter/replay test evidence and current implementation limits; retained the A00 clean test/build results as a historical snapshot. |
| 1.2 | 2026-11-02 | Added clean native rerun, ESP32-S3 compile-only failure, toolchain details and focused AI/data inventory for A00. |
| 1.1 | 2026-11-02 | Added current 71-unit/3-integration rerun and corrected unit source-filter coverage description. |
| 1.0 | 2026-10-30 | Initial host-test baseline report. |
