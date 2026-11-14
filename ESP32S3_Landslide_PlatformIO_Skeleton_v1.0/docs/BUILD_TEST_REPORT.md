# Pilot Alignment Build and Test Report

**Run date:** 2026-11-14  
**Environment:** Windows, PlatformIO, MSYS2 UCRT64 compiler on `PATH`  
**Working directory:** `ESP32S3_Landslide_PlatformIO_Skeleton_v1.0`  
**Drive alias:** `Y:` mapped to the repository root to avoid non-ASCII path issues

## Results

| Command / check | Result | Interpretation |
|---|---|---|
| `pio test -e native-risk-production-tests` | PASS — 1/1 | A synthetic policy fixture verifies that production validation accepts model-only configuration and rejects tilt-rule configs without `PROJECT_TEST_ONLY`; it does not represent an approved Pilot manifest. |
| `g++ -std=c++17 -I test\test_mpu9250_host\fakes -I include -I include\project test\test_mpu9250_host\test_mpu9250_host.cpp src\Drivers\MPU9250Driver.cpp -o build_output\mpu9250_host_tests.exe` followed by executable | PASS — exit 0 | MPU host assertion suite passed, including not-approved tilt with valid raw/accel/gyro/temp data. The standalone test prints no case-count summary. |
| `pio test -e native-unit-tests` | PASS — 131/131 | Host unit suite passed. |
| `pio test -e native-integration-tests` | PASS — 7/7 | Host integration suite passed. |
| `pio test -e native-queue-tests` | PASS — 2/2 | Bounded queue suite passed. |
| `pio test -e native-display-tests` | PASS — 3/3 | Display-service contract suite passed. |
| `pio test -e native-alarm-tests` | PASS — 3/3 | Alarm-service contract suite passed. |
| `pio test -e native-blynk-tests` | PASS — 4/4 | Blynk-service suite, including event retry after transport failure, passed. |
| `pio run -e esp32-s3-devkitc-1` | PASS | Production-target compile/link and image generation succeeded. This does not identify or test the physical board. |
| `pio run -e esp32-s3-mock` | PASS | Explicit mock-target compile/link succeeded; it is not production evidence. |
| `git diff --check` | PASS | No diff whitespace errors. |
| `git check-ignore -v final3blynkiot_5259.docx` | PASS | Credential-bearing prototype reference is excluded from Git. |

Commands were run sequentially because PlatformIO environments share
`build_output`. No firmware was flashed. No physical peripheral, Blynk
connection, Edge Impulse model, storage durability, memory/latency margin, or
endurance behavior was verified.

## Release interpretation

Build/test status is **host and compile pass, Pilot runtime blocked**. The
production startup path remains fail-closed until board identity and
configuration, calibration, runtime adapters/task sizing, storage, and
approved model/schema gates are supplied. See
[HARDWARE_ALIGNMENT_REPORT.md](HARDWARE_ALIGNMENT_REPORT.md) and
[PRODUCTION_RELEASE_GATE.md](PRODUCTION_RELEASE_GATE.md).
