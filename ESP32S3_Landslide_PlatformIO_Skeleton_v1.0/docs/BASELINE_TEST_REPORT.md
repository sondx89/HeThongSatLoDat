# Baseline Test Report

**Audit date:** 2026-10-30  
**Project:** ESP32-S3 Landslide Monitoring PlatformIO skeleton  
**Scope:** Existing host tests and test/build configuration only. No source, header, or test files were changed.

## Environment observed

| Item | Observed value |
|---|---|
| OS | Windows |
| Python | 3.11.9 |
| PlatformIO Core | 6.2.0 |
| Native PlatformIO platform | `native` 1.2.1 |
| Host compiler | MSYS2 UCRT64 `g++` 16.2.0; `gcc.exe` and `g++.exe` exist under `C:\msys64\ucrt64\bin` |
| ESP32 platform | `espressif32` 8.0.0, resolved from the project environment |
| Project language/framework | C++17 / Arduino, as configured in `platformio.ini` |

`platformio.ini` does not pin a version for the `espressif32` platform. The
version above is what PlatformIO resolved in this environment on the audit date,
not a version constraint in the repository.

## Results

| Environment | Exact command | Result |
|---|---|---|
| Native unit tests | `Set-Location 'C:\Users\DINH XUAN SON\OneDrive\ドキュメント\GitHub\HeThongSatLoDat\ESP32S3_Landslide_PlatformIO_Skeleton_v1.0'; $env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH; pio test -e native-unit-tests` | **PASS** — 16 test cases succeeded; 0 failed. |
| Native integration tests | `Set-Location 'C:\Users\DINH XUAN SON\OneDrive\ドキュメント\GitHub\HeThongSatLoDat\ESP32S3_Landslide_PlatformIO_Skeleton_v1.0'; $env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH; pio test -e native-integration-tests` | **PASS** — 3 test cases succeeded; 0 failed. |
| Native unit tests, initial direct attempt | `pio test -e native-unit-tests` from the project directory without changing `PATH` | **ERROR before tests ran** — `gcc` and `g++` were not recognized. |
| Native integration tests, initial direct attempt | `pio test -e native-integration-tests` from the project directory without changing `PATH` | **ERROR before tests ran** — `gcc` and `g++` were not recognized. |

The initial errors were resolved by adding the already-installed MSYS2 UCRT64
compiler directory to `PATH`; no dependency was installed. `build_project.bat`
contains a check that adds this same directory when it exists.

## Test selection and coverage boundary

- `native-unit-tests` selects `test_system_contracts` and builds
  `SensorManager`, `RiskManager`, `DataQualityService`, `FeaturePipeline`, and
  `ReplayEngine`.
- `native-integration-tests` selects `test_system_flow` and builds
  `SensorManager`, `RiskManager`, `DataQualityService`, `FeaturePipeline`,
  `MockModelRuntime`, `MockCsvTelemetry`, and `CsvLogger`.
- Neither test environment directly tests the listed placeholders,
  `EdgeImpulseAdapter`, BSP watchdog/power calls, or the real generated Edge
  Impulse model.

## Not run

- **PlatformIO ESP32-S3 target build:** not run as part of this test baseline.
  The installed PlatformIO package listing showed the ESP32 platform but no
  target compiler/framework packages; a target build could require package
  retrieval. No target build result is claimed.
- **Hardware tests:** not run; no physical board/peripheral test is evidenced
  by these host results.
- `build_project.bat all`, upload, serial monitor, and `production-build` were
  not run. The default script action includes mock firmware upload/monitor and
  is not a host-only test command.

These results establish only the repeatability of the two native test
environments with the stated host toolchain. They do not establish ESP32-S3
compilation, peripheral operation, model compatibility, risk-policy approval,
or product safety.
