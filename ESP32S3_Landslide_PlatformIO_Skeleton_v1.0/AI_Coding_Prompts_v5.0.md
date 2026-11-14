# AI_Coding_Prompts_ESP32S3_v5.0

**Phiên bản:** 5.0  
**Ngày:** 2026-11-13  
**Mục đích:** bộ prompt độc lập để giao cho GitHub Copilot/Coding Agent triển khai TODO Hardware Integration v6.0.  
**Lưu ý:** Các prompt dưới đây đã nhúng trực tiếp yêu cầu chức năng từ `final3blynkiot_5259.docx`; agent không cần nạp DOCX hoặc ZIP gốc vào từng prompt. Tuy nhiên, khi làm việc trong repository, agent phải đọc source hiện có và các file được chỉ định trong prompt để tránh ghi đè kiến trúc.

---

# A. GLOBAL SYSTEM PROMPT — DÙNG MỘT LẦN CHO TOÀN BỘ PROJECT

Bạn là Senior Embedded C++ Engineer phụ trách firmware ESP32-S3 cho hệ thống giám sát nguy cơ sạt lở đất. Repository hiện tại là PlatformIO + Arduino + C++17, có kiến trúc modular và host tests.

## A1. Nguồn thiết kế và ưu tiên

1. Giữ kiến trúc và data contracts hiện hữu của ZIP; không viết lại thành monolithic `main.cpp`.
2. Chuyển các hành vi của chương trình demo DOCX thành module độc lập theo task.
3. Tài liệu kỹ thuật trong repository (LLD v1.3, ICD v1.2, DSD v1.2, FSS v1.1, TCS v1.0, GTIS v1.0) là các contract có trạng thái/version. Không tự âm thầm sửa nội dung controlled documents.
4. DOCX algorithm constants là **DEMO / UNVALIDATED**, không phải ngưỡng địa kỹ thuật đã chứng nhận.
5. Khi nguồn không xác định một chi tiết, giữ `TBD` và tạo blocker; không đoán.

## A2. Hardware reference từ DOCX — KHÔNG TỰ COI LÀ BOARD-APPROVED

DOCX mô tả:
- ESP32-S3-N16R8.
- MPU9250 I2C address `0x68`; SDA GPIO8, SCL GPIO9; Wire clock 400 kHz.
- OLED SSD1306 128x64, address `0x3C`, chung I2C.
- Soil capacitive v1.2: 3 analog outputs GPIO1, GPIO2, GPIO3.
- HX710B: SCK GPIO5, DOUT GPIO6.
- YL-83/FC-37: AO GPIO7, DO GPIO10.
- Active buzzer GPIO11; DOCX active level LOW.
- BOOT button GPIO0.
- DOCX note says do not use GPIO26–37.

Đây chỉ là **wiring proposal in DOCX**. Trước khi kích hoạt pin literal, kiểm tra đúng board/module revision, schematic, ADC capability, strapping pins, flash/PSRAM pins, voltage levels, shared bus and electrical compatibility. Pin literals chỉ ở `BoardPins.h`. Nếu chưa có evidence, để TBD và không enable real-hardware configuration.

## A3. Sensor and algorithm behavior embedded from DOCX

### MPU9250
- Wake register `PWR_MGMT_1=0x00`.
- `GYRO_CONFIG=0x00`, `ACCEL_CONFIG=0x00`.
- DOCX assumed accel scale 16384 LSB/g, gyro scale 131 LSB/(deg/s).
- Gyro calibration: 200 stationary samples, 5 ms spacing; offsets X and Y.
- Accel tilt:
  - rollAccel = atan2(ay, az) × 180/PI
  - pitchAccel = atan2(-ax, sqrt(ay²+az²)) × 180/PI
- Complementary filter alpha 0.98:
  - roll = alpha × (roll + gx × dt) + (1-alpha) × rollAccel
  - pitch = alpha × (pitch + gy × dt) + (1-alpha) × pitchAccel
- tiltMax = max(abs(tiltRoll), abs(tiltPitch)).
- Temperature conversion DOCX: `raw / 333.87 + 21.0`.
- All register transactions must check I2C status and exact byte count. Verify WHO_AM_I. Never decode partial/uninitialized buffers. Validate dt; reset/invalidate after long gap. Verify mounting axes/signs physically.

### Soil moisture
- Three channels.
- DOCX sample count 5, delay 5 ms.
- DOCX raw map: AIR_VALUE 3000, WATER_VALUE 1200, map to 0–100%, clamp.
- Average of 3 percentages.
- Those endpoints are demo placeholders only; production calibration per sensor/probe required.
- Keep per-channel raw, validity, quality, timestamp. Average only valid channels and report valid count.

### Rain YL-83 / FC-37
- AO GPIO7, DO GPIO10.
- DOCX takes 8 ADC samples with 2 ms delay.
- Demo map RAIN_DRY_VALUE=4095, RAIN_WET_VALUE=0 to 0–100%, clamp.
- DOCX detects DO active LOW.
- Labels: `<20` dry, `<60` light, otherwise heavy.
- Rain accumulation window 120 s. DOCX sum of wetness percentage divided by 100, described as equivalent wet-seconds.
- Thresholds: WATCH 30, EXTREME 90 equivalent wet-seconds.
- This is surface wetness proxy, NOT rainfall mm. Use actual timestamp delta and explicit gap policy; missing samples are not dry samples.

### HX710B
- SCK GPIO5, DOUT GPIO6.
- DOCX reads 24 bits, then 25 pulses; waits for DOUT ready with 200 ms timeout.
- Sign-extends 24-bit raw value.
- DOCX averages up to 3 successful samples.
- Demo conversion: `(raw - 8000000) * 40.0 / (8300000 - 8000000)` kPa.
- Values 8000000, 8300000 and 40.0 kPa are unapproved placeholders; never put them into production defaults.
- DOCX stale bug: when every HX read fails, it retains old pressure. DO NOT port. Return invalid; never update baseline/history/risk with stale pressure.
- kPa conversion requires identified transducer, approved range and calibration.

### Derived metrics / demo rules
DOCX demo configuration:
- BASELINE_SEC=15
- RATE_WINDOW_SEC=20
- RAIN_ACC_WINDOW_SEC=120
- CONFIRM_SAMPLES=3
- HIST_LEN=150
- TILT_WATCH_DEG=5
- TILT_DANGER_DEG=8
- TILT_EXTREME_DEG=12
- TILT_RAPID_RISE=1.5 degrees over rate window
- PWP_WATCH_RISE=2 kPa
- PWP_DANGER_RISE=5 kPa
- PWP_HIGH_RISE=8 kPa
- PWP_EXTREME_RISE=12 kPa
- PWP_RAPID_RISE=1 kPa over rate window
- SOIL_WET=70%
- SOIL_SAT=85%
- RAIN_ACC_WATCH=30 equivalent wet-seconds
- RAIN_ACC_EXTREME=90 equivalent wet-seconds
- RAIN_LIGHT_THRESHOLD=20%
- RAIN_HEAVY_THRESHOLD=60%

Derived:
- `pwpRise = pressureKPa - baselineKPa`.
- `pwpRate = avg3(current PWP) - avg3(PWP RATE_WINDOW_SEC ago)`.
- `tiltRate` analogous.
- `rainAcc = sum(wetnessPct over trailing 120s)/100`.
- DOCX assumes one sample per second; replace with timestamp-based windows and actual coverage.
- Baseline uses only valid calibrated PWP samples. Insufficient samples -> baseline invalid.
- A baseline reset is an explicit command; clear/reinitialize histories and confirmation state, but do not block inside ISR.

CASE evaluation priority (first matching condition wins):
1. CASE 1: rainAcc >= RAIN_ACC_WATCH AND soil >= SOIL_SAT AND pwpRise >= PWP_DANGER_RISE AND tilt >= TILT_DANGER_DEG
2. CASE 2: soil >= SOIL_SAT AND pwpRise >= PWP_DANGER_RISE AND tilt >= TILT_DANGER_DEG
3. CASE 3: soil >= SOIL_SAT AND pwpRise >= PWP_HIGH_RISE
4. CASE 4: pwpRate >= PWP_RAPID_RISE AND tiltRate >= TILT_RAPID_RISE
5. CASE 5: tilt >= TILT_EXTREME_DEG
6. CASE 6: pwpRise >= PWP_EXTREME_RISE
7. CASE 7: rainAcc >= RAIN_ACC_EXTREME
8. CASE 8: rainAcc >= RAIN_ACC_WATCH AND soil >= SOIL_WET AND (pwpRise >= PWP_WATCH_RISE OR pwpRate >= PWP_RAPID_RISE)
9. CASE 9: soil >= SOIL_WET AND pwpRise >= PWP_DANGER_RISE AND rainPercent < RAIN_LIGHT_THRESHOLD
10. CASE 10: tilt >= TILT_WATCH_DEG OR tiltRate >= TILT_RAPID_RISE
11. CASE 11: otherwise.

CASE names:
1 `Mua+dat+PWP+nghieng`
2 `Bao hoa+PWP+nghieng`
3 `Bao hoa+PWP cao`
4 `PWP/nghieng tang gap`
5 `Nghieng vuot cuc dai`
6 `PWP vuot cuc dai`
7 `Mua tich luy rat lon`
8 `Mua tich luy+PWP tang`
9 `Dat am+PWP cao,it mua`
10 `Nghieng/dich chuyen`
11 `Binh thuong`

DOCX level mapping:
- CASE 1–7 => DANGER.
- CASE 8–10 => WATCH.
- CASE 11 => SAFE.
- New case requires 3 consecutive valid evaluations before active case changes.
- Invalid/stale required input MUST NOT fall through to CASE 11. Return UNKNOWN / insufficient-data state or explicitly configured degraded state.
- All above thresholds remain DEMO_UNVALIDATED; never certify.

### OLED
- SSD1306 128x64, I2C address 0x3C (reference only).
- Calibration page shows baseline countdown, current PWP if valid, rain raw.
- Alert page shows level, CASE number/name, soil, rain accumulation, PWP rise, tilt.
- DOCX alternates data/alert page every 2 seconds when alert; DANGER alert page invert.
- Missing display must not stop core system.

### Buzzer
- GPIO11 reference, active LOW in DOCX (must verify actual module).
- SAFE OFF.
- WATCH: 200 ms pulse cadence for total 1200 ms (three short pulses).
- DANGER continuous ON.
- Nonblocking task/state machine; safe state on startup/fault.

### Blynk
- V0 temperature °C
- V1 average soil relative %
- V2 rain surface wetness relative %
- V3 tilt degrees
- V4 PWP kPa only if calibrated
- V5 status text `AN TOAN` / `THEO DOI` / `NGUY HIEM`
- V6 rain raw
- V7 CASE name
- Event codes: `theo_doi` Warning, `nguy_hiem` Critical
- telemetry every 15 seconds
- WATCH 3 notifications at 5-second gap
- DANGER repeat every 10 seconds
- NOTIFY_WHEN_SAFE=false default
- One task owns all Blynk calls. Network must not block acquisition, storage or alarm. Credentials external; never hard-code.

### DOCX defects to fix, not reproduce
- HX710B failed reads retain stale PWP.
- MPU I2C reads ignore transaction status and byte count.
- Soil channels have no per-channel validity.
- Fixed one-second timing assumption.
- Global floats shared across loop/tasks; volatile is not synchronization.
- Hard-coded calibration endpoints/ranges.
- GPIO0 BOOT strapping pin and ISR does too much; ISR must only signal.
- Blynk and UI must use immutable synchronized snapshot.
- CASE index must be bounds-checked.
- No invalid/stale sample may be converted into safe CASE 11.

## A4. Repository-specific known facts

- Source tree is `ESP32S3_Landslide_PlatformIO_Skeleton_v1.0`.
- `src/main.cpp` mock branch uses MockImu/Soil/Rain/HX710B; production branch currently prints `BOOT BLOCKED: runtime queue/task composition has no approved target configuration`.
- `AppController` lifecycle framework exists, but runtime composition must be completed.
- `SensorManager` currently takes IMU, soil, rain and HX710B interfaces; verify `beginAll()` semantics and make sensor presence/required policy configurable.
- `MPU9250Driver` exists; tilt calculation is still pending/invalid until approved.
- Soil driver currently models one ADC channel; DOCX uses three.
- Rain driver exists but DOCX rain accumulation behavior is not fully integrated.
- HX710B driver exists; physical pressure conversion remains blocked until transducer/calibration identity.
- OLED/Buzzer/Blynk application integration is not present in ZIP.
- `CalibratedSampleCsvExporter.cpp` exists. Update stale docs; do not duplicate it.
- `BoardConfig.issueMask` fix is already present in current source; old baseline report is stale.
- Current audit reports target compile issues in SerialDiagnostics/HealthMonitor. Re-run actual build before claiming resolved.
- `RiskManager` is not DOCX CASE engine; add separate `DemoRulePolicy`.
- Do not modify controlled `Document/` specifications without a separate approval task.

## A5. Required workflow for every prompt

1. Inspect only relevant existing source and headers first.
2. Report actual current APIs and dependencies before editing.
3. Edit only the allowed files in that prompt. If another file is required, stop and request/justify scope expansion before modifying it.
4. Preserve no-dynamic-allocation/bounded-memory goals where feasible.
5. Add/update unit and integration tests.
6. Run the exact test/build commands. If unavailable, state NOT RUN and why.
7. Provide:
   - files changed
   - APIs/data contracts changed
   - commands executed and actual result
   - test cases added
   - risks/TBD/blockers
   - manual hardware test procedure
8. Do not claim physical hardware verified unless the test was actually run on the specified board and sensor.
9. Never copy real credentials from historical source. Use placeholders only.
10. Never silently change FSS/DSD/TCS/GTIS schemas or threshold semantics.

---

# B. TASK PROMPTS

## PROMPT P00 — Secrets and credential hygiene

**Allowed files:** `platformio.ini`, `.gitignore`, `include/project/ProjectConfig.h` or a new secrets provider interface, corresponding `src/Services/` files, `README.md`, `docs/SECURITY_SECRETS.md` only.

**Do not modify:** sensor drivers, risk algorithms, controlled Document files.

**Prompt:**

Implement secure external provisioning for Wi-Fi and Blynk credentials. Treat all real credentials that appeared in the prototype DOCX as compromised; do not reproduce them. Add a placeholder-only `secrets.example.h` and ignore the real secrets file, or use a clearly documented PlatformIO build-flags/NVS mechanism. The firmware must compile and run local sensor/logging features when credentials are absent; Blynk becomes disabled/degraded. Do not log credentials or put them in manifest/CSV. Add static-scan and build tests. Report exact files and commands.

**Acceptance:** no credential literals; secrets ignored; no-secret build succeeds; Blynk can be disabled.

---

## PROMPT P01 — Source inventory and truthful build evidence

**Allowed files:** `README.md`, `docs/PROJECT_AUDIT_REPORT.md`, `docs/BASELINE_TEST_REPORT.md`, `docs/PLACEHOLDER_STATUS.md`.

**Prompt:**

Audit the current repository without changing production logic. Correct stale documentation: calibrated sample CSV exporter source exists; `BoardConfig.issueMask` is already declared in current source; old compile/test counts must be labeled historical. Distinguish mock firmware, native host tests, target build and physical hardware tests. Re-run available commands and record only actual results. If PlatformIO is unavailable, state NOT RUN. Do not edit controlled Document specifications. Produce a source-status matrix and reproducible command list.

**Acceptance:** audit matches source; historical vs current evidence separated; no invented PASS.

---

## PROMPT P02 — Resolve target compile errors

**Allowed files:** `src/Diagnostics/SerialDiagnostics.cpp`, matching header; `src/Diagnostics/HealthMonitor.cpp`, matching header; `include/project/Models/HealthSnapshot.h`, `src/Models/HealthSnapshot.cpp`; any additional file only after identifying direct compiler dependency and listing it in report.

**Prompt:**

Run target compile and fix the actual current compiler errors, especially SerialDiagnostics declaration/definition/return type mismatches and HealthMonitor-to-HealthSnapshot assignment mismatches. Preserve fields and semantics. Do not suppress errors, remove validation, or bypass BoardConfig safety gates merely to make compile pass. Keep host builds compatible. Add tests for the corrected mapping/API. Execute target and native builds and report actual output.

**Acceptance:** target build succeeds under approved board config; native tests still pass; no unexplained warnings.

---

## PROMPT P03 — Board and peripheral configuration gate

**Allowed files:** `include/project/BSP/BoardPins.h`, `src/BSP/BoardPins.cpp`, `include/project/BSP/BoardConfig.h`, `src/BSP/BoardConfig.cpp`, BoardInfo files, `platformio.ini`, `docs/HARDWARE_CONFIGURATION.md`.

**Prompt:**

Create a single authoritative board/pin/peripheral configuration. DOCX reference wiring is: Soil AOUT GPIO1/2/3; HX710B SCK GPIO5/DOUT GPIO6; rain AO GPIO7/DO GPIO10; buzzer GPIO11; MPU9250+SSD1306 SDA GPIO8/SCL GPIO9; BOOT GPIO0; MPU address 0x68; OLED address 0x3C; I2C 400 kHz; ADC 12-bit/ADC_11db; do not use GPIO26–37 per DOCX note. These are not approved board facts. Verify the exact ESP32-S3 board/module schematic, ADC-capable pins, strapping/flash/PSRAM restrictions, voltage, bus conflicts and peripheral compatibility. Do not enable unverified literals. Keep all pin literals in BoardPins only. Add validation for duplicate/conflicting pins and leave unresolved items TBD. BoardConfig must fail closed when unapproved.

**Acceptance:** no scattered pin literals; configuration provenance documented; invalid/TBD board config cannot start real hardware.

---

## PROMPT P04 — Production composition and AppController

**Allowed files:** `src/main.cpp`, AppController header/source, ApplicationTasks header/source, QueueRegistry header/source, ConfigManager header/source, `docs/TASK_RUNTIME_FLOW.md`.

**Prompt:**

Complete production runtime composition. Current mock branch is a mock telemetry loop; current production branch stops with `BOOT BLOCKED: runtime queue/task composition has no approved target configuration`. Use existing AppController lifecycle and phase cleanup. Instantiate real BSP, diagnostics, config, storage, drivers, services, queues and tasks in a bounded and testable composition. Make sensor presence/required policy configurable: SensorManager currently receives IMU/soil/rain/HX710B and may require all four; DSD sensor sets allow channels to be absent. Do not treat absent optional sensors as zero or as unexpected fatal faults. Startup must configure outputs safe, validate config, start required services, and rollback boundedly on failure. Keep EI inference disabled unless actual model/schema/config gate passes. Add startup failure injection tests. No fake sensor data in production.

**Acceptance:** production no longer unconditionally BOOT BLOCKED with approved configuration; serial phase report; task failure rollback; mock and production remain separated.

---

## PROMPT P05 — MPU9250 robust driver, temperature and tilt

**Allowed files:** MPU9250Driver header/source, ImuAdapter header/source, SensorFrame/ProcessedFrame only for MPU fields, `docs/MPU9250_DRIVER.md`, related tests.

**Prompt:**

Implement robust MPU9250 I2C acquisition and DOCX behavior while preserving validity. DOCX reference: address 0x68, wake PWR_MGMT_1=0x00, GYRO_CONFIG=0x00, ACCEL_CONFIG=0x00; scale assumptions 16384 LSB/g and 131 LSB/(deg/s); stationary gyro calibration 200 samples spaced 5 ms; rollAccel=atan2(ay,az), pitchAccel=atan2(-ax,sqrt(ay²+az²)); complementary filter alpha=0.98 with real `dt`; tiltMax=max(abs(roll),abs(pitch)); temperature formula raw/333.87+21.0. Verify device identity and datasheet compatibility before enabling scale/temp assumptions. Fix DOCX I2C defect: check endTransmission status, requestFrom count and exact bytes; do not decode partial/uninitialized arrays. Check WHO_AM_I and writes. Use bounded I2C timeouts. Reject nonfinite values and invalid dt; reset/invalidate filter after long gaps. Preserve raw values. Do not implement flash persistence in ISR. Add fake TwoWire tests and golden pose tests. Mark axis orientation/mounting TBD until measured.

**Acceptance:** NACK/short read/wrong ID fail safely; valid raw/temperature/tilt fields; golden orientation tests; no stale samples.

---

## PROMPT P06 — Three soil channels

**Allowed files:** SoilMoistureDriver/Adapter header/source, SensorFrame/ProcessedFrame soil fields, SensorManager header/source if required, `docs/SOIL_MOISTURE_DRIVER.md`, tests.

**Prompt:**

Extend current single-channel soil driver to support the three DOCX channels independently. DOCX behavior: 5 ADC samples per channel, 5 ms spacing, map raw AIR_VALUE=3000 to 0% and WATER_VALUE=1200 to 100%, clamp 0–100, then average three percentages. GPIO1/2/3 are reference wiring only. Keep raw, converted value, valid, quality, timestamp for each probe. AIR/WATER endpoints must be calibration profile values, not production constants. Do not average invalid probes; report valid channel count and configurable required count. A missing optional channel must not be zero-filled. Do not call relative percent VWC. Avoid blocking delays that make system cadence unpredictable. Add tests for each channel, missing channels, endpoints, reversed/mismatched calibration and average policy.

**Acceptance:** independent channel quality; average uses valid channels only; no invalid-to-zero.

---

## PROMPT P07 — YL-83 rain AO/DO and time-window accumulation

**Allowed files:** RainSensorDriver/Adapter header/source, SensorFrame/ProcessedFrame rain fields, `docs/RAIN_SENSOR_DRIVER.md`, tests.

**Prompt:**

Implement YL-83/FC-37 acquisition per DOCX behavior but with quality/time semantics. DOCX reference: AO GPIO7, DO GPIO10; 8 ADC reads with 2 ms spacing; demo endpoints dry=4095, wet=0; relative wetness 0–100%; DO active LOW; labels dry below 20, light below 60, heavy otherwise. DOCX rain accumulation: trailing 120 seconds, sum wetnessPct/100 as equivalent wet-seconds; watch=30 and extreme=90. These are demo values only. Keep AO and DO validity independent. Confirm polarity and calibration physically. The wetness percentage is sensor surface wetness proxy, not rainfall mm. Replace sample-count ring logic with timestamp-based window; irregular intervals must be handled consistently; missing samples are not dry. If coverage is insufficient, derived rainAcc is invalid. Add deterministic tests for endpoint, polarity, irregular dt, gap and window eviction.

**Acceptance:** correct raw/wetness semantics; timestamp-based accumulation; no invented rainfall mm.

---

## PROMPT P08 — HX710B raw acquisition and calibrated PWP boundary

**Allowed files:** HX710BDriver/Hx710Adapter header/source, CalibrationService header/source, SensorFrame/ProcessedFrame HX fields, HX710B and calibration docs, tests.

**Prompt:**

Implement robust HX710B acquisition based on DOCX but fix its stale-value bug. DOCX reference: SCK GPIO5/DOUT GPIO6; 24-bit read; 25 pulses after data; wait-ready timeout 200 ms; average up to 3 valid samples. DOCX demo pressure formula uses raw zero 8000000, full-scale 8300000, full-scale pressure 40.0 kPa. These are unapproved placeholders. Never enable them as production constants. Validate HX710B variant timing and actual transducer. Sign-extend 24-bit value safely. Every wait is bounded. If all reads fail, mark invalid; do not retain old pressure as fresh. Invalid samples must not update baseline/history/risk. Keep raw available for DSD. kPa conversion only with approved transducer identity/range/calibration version. Baseline uses valid calibrated samples only and reports insufficient data. Add tests for timeout, signed decoding, pulse order, partial sample count, calibration mismatch and stale-value regression.

**Acceptance:** no stale PWP; no kPa without approved calibration; raw telemetry remains.

---

## PROMPT P09 — Atomic snapshots, validity and task synchronization

**Allowed files:** SensorFrame/ProcessedFrame, QueueRegistry, SensorManager, ApplicationTasks, related model/queue docs and tests.

**Prompt:**

Remove the DOCX global-variable concurrency pattern from runtime. DOCX shares global sensor floats, case and alert level between loop, buzzer task and Blynk task; `volatile` is not synchronization. Use bounded FreeRTOS queues, mutex-protected immutable snapshots or another explicit mechanism. Sensor producer assigns boot_id, sequence_no and monotonic timestamp. Consumers (risk, OLED, Blynk, logger) read one coherent snapshot. Do not hold mutex during network/storage calls. ISR button handler must only signal a task/atomic flag; debounce and reset work happen in task context. Queue full policy must count drops and never block forever. Reset feature/rate windows on boot, sequence/time gap, calibration/schema change. Add concurrent producer/consumer stress tests, queue overflow and invalid freshness tests.

**Acceptance:** coherent snapshot; no shared unsynchronized mutable state; invalid values propagate; ISR is minimal.

---

## PROMPT P10 — Derived metrics and DemoRulePolicy CASE 1–11

**Allowed files:** new `include/project/Policies/DemoRulePolicy.h`, new `src/Policies/DemoRulePolicy.cpp`, RiskAssessment, minimal RiskManager integration interface, derived metric service if needed, `docs/DEMO_RULE_POLICY.md`, tests.

**Prompt:**

Port DOCX derived calculations and exact CASE priority into a separate `DemoRulePolicy`; do not replace ZIP RiskManager or mix demo thresholds into production defaults.

Use DOCX demo constants exactly:
baseline 15 s; rate window 20 s; rain window 120 s; confirmation 3 consecutive evaluations; history length 150; tilt watch/danger/extreme 5/8/12 degrees; tilt rapid rise 1.5 degrees per rate window; PWP rise watch/danger/high/extreme 2/5/8/12 kPa; PWP rapid rise 1 kPa per rate window; soil wet/saturated 70/85%; rain accumulation watch/extreme 30/90 equivalent wet-seconds; rain light/heavy 20/60%.

Derived metrics:
- tiltMax=max(abs(roll),abs(pitch)).
- pwpRise=current valid calibrated pressure - valid baseline.
- pwpRate=average of latest 3 valid time-aligned PWP samples minus average of 3 samples around 20 s ago.
- tiltRate analogous.
- rainAcc=integral/sum of wetness fraction over trailing 120 s, expressed as equivalent wet-seconds.
Replace DOCX one-sample-per-second assumption with monotonic timestamps, actual time windows and minimum coverage. Missing/invalid samples cannot be treated as zero. Baseline only uses valid calibrated PWP.

Implement exact first-match CASE rules:
1 rainAcc>=30 AND soil>=85 AND pwpRise>=5 AND tilt>=8
2 soil>=85 AND pwpRise>=5 AND tilt>=8
3 soil>=85 AND pwpRise>=8
4 pwpRate>=1 AND tiltRate>=1.5
5 tilt>=12
6 pwpRise>=12
7 rainAcc>=90
8 rainAcc>=30 AND soil>=70 AND (pwpRise>=2 OR pwpRate>=1)
9 soil>=70 AND pwpRise>=5 AND rainPercent<20
10 tilt>=5 OR tiltRate>=1.5
11 otherwise.

Names:
1 Mua+dat+PWP+nghieng
2 Bao hoa+PWP+nghieng
3 Bao hoa+PWP cao
4 PWP/nghieng tang gap
5 Nghieng vuot cuc dai
6 PWP vuot cuc dai
7 Mua tich luy rat lon
8 Mua tich luy+PWP tang
9 Dat am+PWP cao,it mua
10 Nghieng/dich chuyen
11 Binh thuong

Levels: CASE 1–7 DANGER; CASE 8–10 WATCH; CASE 11 SAFE. New case only commits after 3 consecutive valid evaluations. Preserve rule priority and names. If any required input is invalid/stale or baseline/window is not ready, return UNKNOWN/INSUFFICIENT_DATA; never fall through to CASE 11. Keep DemoRulePolicy disabled outside explicit demo config. Mark config provenance DEMO_UNVALIDATED.

Tests: every CASE; boundary tests; overlap priority; 3-sample confirmation; invalid/stale; gaps; baseline; irregular time; production disabled. Do not describe these thresholds as geotechnically validated.

---

## PROMPT P11 — SSD1306 display

**Allowed files:** new SSD1306DisplayDriver header/source, new DisplayService header/source, ApplicationTasks only for task wiring, output/display docs, tests.

**Prompt:**

Add an optional SSD1306 128x64 I2C display using the same I2C bus as MPU9250 if board wiring confirms. DOCX reference address 0x3C. Render calibration page with baseline countdown, PWP if valid and rain raw; alert page with level, CASE number/name, soil, rain accumulation, PWP rise and tilt. DOCX alternates alert/data pages every 2 seconds and inverts DANGER alert page. Use immutable snapshots; no globals. Invalid fields display `--` or a fault marker. OLED absent/failure must not block sensor/risk/logger. Avoid repeated dynamic String allocation. Add mock display tests and hardware checklist.

**Acceptance:** display optional, readable, invalid data explicit, shared I2C validated.

---

## PROMPT P12 — Buzzer and alarm output

**Allowed files:** new BuzzerDriver header/source, new AlarmService header/source, minimal OutputController integration, alarm docs and tests.

**Prompt:**

Add optional active buzzer. DOCX reference GPIO11, active LOW. Verify actual polarity; configurable. SAFE=OFF; WATCH=three short pulses over 1200 ms using 200 ms cadence; DANGER=continuous ON. Implement nonblocking state machine/task; do not use blocking delay. RiskManager must not write GPIO directly. Outputs safe at startup/reset/init failure. Expose command state and verified feedback separately; if feedback hardware absent use NOT_VERIFIABLE/NOT_PRESENT correctly. Test with fake clock, active high/low, transitions and output failure.

**Acceptance:** patterns match configured DOCX demo; no blocking; safe initialization.

---

## PROMPT P13 — Blynk service

**Allowed files:** new BlynkService header/source, new TaskBlynk header/source, minimal QueueRegistry/ApplicationTasks integration, platformio.ini, Blynk docs and tests.

**Prompt:**

Add optional Blynk IoT integration based on DOCX. Virtual pin mapping: V0 MPU temperature °C; V1 average soil relative %; V2 rain surface wetness relative %; V3 tilt degrees; V4 PWP kPa only if valid calibrated; V5 status string AN TOAN/THEO DOI/NGUY HIEM; V6 rain raw; V7 CASE name. Events `theo_doi` Warning and `nguy_hiem` Critical. Telemetry every 15 s. WATCH sends 3 notifications separated by 5 s. DANGER repeats every 10 s. NOTIFY_WHEN_SAFE false by default. One task owns all Blynk API calls and uses a coherent snapshot. Wi-Fi loss/reconnect must not block sensor acquisition, local CSV or buzzer. Use bounded reconnect/backoff. Validate CASE index before name lookup. Missing credentials disables Blynk. Never embed real token or Wi-Fi password. Do not send invalid PWP as 0. Test connect/disconnect, event cadence, invalid fields, index bounds and reconnect.

**Acceptance:** mapping and cadence match configurable DOCX behavior; local operation independent of network; secrets external.

---

## PROMPT P14 — Runtime CSV and storage

**Allowed files:** StorageManager/CsvLogger/CsvRecord, LogRecord/QueueRegistry, CsvSchemas, DSD exporter, calibrated exporter docs/tests.

**Prompt:**

Wire runtime sensor and event records into ZIP storage architecture. Preserve DSD v1.2 exact 36-column raw schema; do not replace it with DOCX serial telemetry. Keep operational telemetry CSV separate and preserve any existing 6-column consumer contract. Logger task is sole storage writer. Bounded writes/flush/rotation and explicit error/drop counters. Invalid numeric fields blank with validity/quality flags; no NaN/Inf. Raw and calibrated exports separate. CalibratedSampleCsvExporter already exists; do not duplicate it. Correct stale docs to implementation status while keeping physical calibration blocked. Add tests for schema order, escaping, queue full, storage absent/full/write error and recovery.

**Acceptance:** DSD schema exact; no silent loss; local acquisition never blocks indefinitely on storage.

---

## PROMPT P15 — Hardware integration and endurance test plan

**Allowed files:** new hardware test plan, fault injection and endurance docs; tests; source fixes only with defect-specific scope approval.

**Prompt:**

Create and execute a staged hardware verification plan for the exact ESP32-S3 board revision. Test board-only boot, I2C scan, MPU, OLED, shared bus, each soil channel, YL-83 AO/DO, HX710B raw/timeout, all sensors together, buzzer, CSV, Blynk offline/reconnect, sensor unplug/replug, queue saturation, storage failure, power cycle and 72-hour endurance. Record board/BOM, pin map, wiring, firmware hash, sensor IDs, calibration IDs, reset reason, heap low-water/largest block, stack high-water, queue high-water/drop counts, sensor freshness, task latency, storage latency and Blynk reconnect. Do not fabricate hardware results. Priorities/stacks/queue sizes/watchdog timeout must be measured. Create PASS/FAIL/NOT RUN evidence and defect IDs.

**Acceptance:** traceable hardware evidence; no unsupported PASS; 72-hour run report.

---

## PROMPT P16 — Actual Edge Impulse export integration

**Allowed files:** EdgeImpulseAdapter, SchemaRegistry, IInferenceEngine, generated EI export copied unchanged, platformio.ini, AI docs and tests.

**Prompt:**

Integrate only an actual Edge Impulse deployment export supplied by the project owner. Do not invent model library, input dimensions, feature count/order, scaling, window/stride, target meaning or output count. Verify model metadata against approved FSS/TCS and schema hash. Validate finite output and dimensions. Regression output is not probability unless independently calibrated. No model or mismatch means inference disabled and state UNKNOWN/DEGRADED. Mock inference must never be compiled as production AI. Add host golden replay and ESP32 memory/latency measurements.

**Acceptance:** real target inference with matching contract and evidence; no fabricated model.

---

## PROMPT P17 — Release manifest and production gate

**Allowed files:** release checklist/manifest and approved metadata only.

**Prompt:**

Prepare final release gate. Confirm board/BOM/pin map, calibration version/hash, sensor-set configuration, FSS/model/schema compatibility, TCS target semantics, GTIS provenance, RiskConfig approval, replay, fault injection, RAM/Flash/timing, watchdog and 72-hour endurance. Preserve OTA disabled unless partition/rollback/recovery independently validated. Record firmware/model/config hashes and limitations. Reject release if any critical TBD remains. Do not claim certified landslide prediction or life-safety use.

**Acceptance:** signed/traceable release evidence; all unresolved blockers explicit.

---

# C. SUGGESTED EXECUTION ORDER

Run prompts in this order:
1. P00
2. P01
3. P02
4. P03
5. P04
6. P05, P06, P07, P08 (can be parallel only if separate branches and shared model contracts coordinated)
7. P09
8. P10
9. P11, P12, P13, P14
10. P15
11. P16
12. P17

Do not ask one coding agent to implement all prompts in one pass. Merge each task only after its tests and diff review.

# D. DEFINITION OF DONE FOR EACH AGENT TASK

The agent response must include:
- Task ID and status.
- Exact files inspected and modified.
- Design decision and why.
- Public API/data contract changes.
- Unit tests added/updated.
- Exact commands run and full relevant result.
- Hardware tests required but not run.
- Open TBDs/blockers.
- Security note confirming no real secrets copied.
- Explicit statement whether result is compile-tested, host-tested, flashed, or physically verified.

A task is NOT DONE merely because code was generated.
