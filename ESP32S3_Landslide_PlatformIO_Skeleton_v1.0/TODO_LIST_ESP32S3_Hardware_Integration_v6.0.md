# TODO_LIST_ESP32S3_Hardware_Integration_v6.0

**Phiên bản:** 6.0  
**Ngày lập:** 2026-11-13  
**Cơ sở:** `HeThongSatLoDat_20261113.zip`, `final3blynkiot_5259.docx`, LLD v1.3, ICD v1.2, DSD v1.2, FSS v1.1, TCS v1.0, GTIS v1.0 và TODO AI-First v5.0.  
**Mục tiêu:** chuyển ZIP skeleton thành firmware ESP32-S3 chạy được trên phần cứng thật, trước tiên ở chế độ Hardware Demo; giữ đường nâng cấp sang AI Production.

---

## 0. Quy tắc bắt buộc cho toàn bộ task

### 0.1 Phân biệt trạng thái

- **IMPLEMENTED:** có code và test hiện hữu; agent phải kiểm tra trước khi sửa, không viết lại.
- **TO IMPLEMENT:** chức năng cần triển khai.
- **TBD / BLOCKED:** chưa có dữ liệu được phê duyệt; agent không được tự điền.
- **DEMO ONLY:** có thể chạy thử với cấu hình DOCX nhưng không được mô tả là ngưỡng địa kỹ thuật/chứng nhận an toàn.
- **PRODUCTION GATE:** chưa được bật trong firmware production khi chưa có model, calibration, config và approval thực tế.

### 0.2 Không được tự suy diễn

Không tự đặt hoặc suy ra:
- Board/module revision, GPIO ngoài pin map đã xác nhận, ADC range/attenuation, I2C clock, địa chỉ thiết bị ngoài giá trị DOCX ghi là cấu hình tham khảo.
- Đầu đo HX710B, đơn vị vật lý, pressure range, raw zero/full-scale, hệ số kPa.
- Hiệu chuẩn đất/mưa, chu kỳ lấy mẫu, timeout, filter/window nếu chưa được đo/duyệt.
- Risk threshold mới, feature count/order, regression target/scaling, model dimensions, EI model output meaning.
- Task priority/stack/queue depth/watchdog timeout khi chưa có đo resource/timing.

Giá trị trong DOCX được giữ nguyên như **cấu hình demo tham khảo**, không tự nâng thành production calibration.

### 0.3 Các quy tắc kiến trúc

1. ZIP là kiến trúc nền; DOCX là nguồn chức năng tham khảo, không port nguyên khối.
2. Không sửa API công khai/contract nếu chưa ghi nhận migration và test.
3. Không đưa logic risk vào driver; không để driver điều khiển OLED/Buzzer/Blynk.
4. Không dùng biến global `volatile` làm cơ chế đồng bộ task.
5. Mẫu invalid/stale/non-finite/out-of-range không được biến thành 0 hoặc tái sử dụng như mẫu mới.
6. HX710B lỗi đọc: không cập nhật pressure, baseline, history, PWP rise/rate; đánh dấu invalid.
7. Mọi thời gian dẫn xuất dùng timestamp thực, không giả định vòng lặp luôn đúng 1 giây.
8. Không dùng `memcpy` struct để serialize; không ghi secret vào source, manifest, CSV hay log.
9. Production AI chỉ chạy khi EI export và model/feature/target contract đã được xác nhận.
10. `GT5` synthetic/proxy không phải ground truth thực địa.
11. Không dùng thiết bị làm căn cứ duy nhất cho sơ tán/ra quyết định an toàn sinh mạng.
12. Mọi task phải kết thúc bằng diff summary, build/test commands, kết quả thực thi thực tế và danh sách TBD còn mở. Không tuyên bố test PASS nếu chưa chạy.

### 0.4 Chế độ firmware

- `native-unit-tests`, `native-integration-tests`: host tests.
- `esp32-s3-mock`: dữ liệu giả/mock, không được gắn nhãn sensor thật.
- `esp32-s3-devkitc-1`: target hardware build; chỉ dùng sau khi board ID/BOM/pin map được xác nhận.
- **Hardware Demo:** sensor thật + DemoRulePolicy + OLED/Buzzer/Blynk/CSV; EI inference có thể disabled.
- **AI Production:** chỉ bật sau P16 và P17.

---

## 1. Tóm tắt dependency graph

```text
P00 Security/secret rotation
  └─ P01 Baseline + source audit + build reproducibility
      ├─ P02 Fix target compile blockers
      ├─ P03 Board/pin/ADC/I2C configuration gate
      └─ P04 AppController + production composition
          ├─ P05 MPU9250 robust driver + tilt/temperature
          ├─ P06 3-channel soil acquisition
          ├─ P07 YL-83 AO/DO + time-based wetness
          ├─ P08 HX710B validity + calibrated PWP boundary
          ├─ P09 Unified SensorFrame/snapshot/task synchronization
          ├─ P10 DOCX derived calculations + DemoRulePolicy CASE 1–11
          ├─ P11 OLED display service
          ├─ P12 Buzzer/alarm service
          ├─ P13 Blynk service
          └─ P14 Storage/CSV runtime integration
              └─ P15 Hardware integration + fault-injection + endurance
                  ├─ P16 EI model integration (blocked by real export)
                  └─ P17 Production release gate
```

P05–P08 có thể phát triển độc lập sau P03; tích hợp vào P04/P09 theo dependency. P10 phụ thuộc P05–P09. P11–P14 phụ thuộc output/data contracts. P15 chỉ bắt đầu khi P00–P14 đã tích hợp được trên target.

---

## 2. Task chi tiết

## P00 — Security: rotate credentials and remove secrets

**Priority:** P0 / trước khi chia sẻ hoặc commit source  
**Status:** TO DO  
**Dependency:** none

**File được phép sửa**
- `platformio.ini`
- `.gitignore` (tạo nếu chưa có)
- `include/project/ProjectConfig.h` hoặc file config mới được AppController sử dụng
- `include/project/Services/` và `src/Services/` chỉ khi cần thêm provider interface cho secrets
- `README.md`, `docs/SECURITY_SECRETS.md` (tạo mới)

**Không được sửa**
- Không ghi lại token/password đã xuất hiện trong DOCX.
- Không đưa secret vào prompt, log, manifest, CSV, test fixture hoặc Git.

**Triển khai**
- Coi mọi credential thật đã lộ trong DOCX là compromised: người quản trị phải rotate Blynk Auth Token và Wi-Fi password ngoài code.
- Tạo cơ chế inject credentials từ `include/secrets.h` bị ignore, build flags hoặc NVS provisioning; chọn một cơ chế phù hợp PlatformIO và mô tả rõ.
- Cung cấp `secrets.example.h` chỉ có placeholder.
- Khi thiếu credential: Blynk disabled/degraded, sensor/local logging vẫn chạy.
- Log chỉ ghi `BLYNK_CREDENTIALS_MISSING`, không ghi giá trị.

**Acceptance**
- `git grep` không tìm thấy credential/token/password thật.
- File secrets bị ignore.
- Build không cần credential để chạy local Hardware Demo.
- Blynk có thể disabled có chủ đích.

**Tests**
- Static scan source/config/docs.
- Build với secrets absent.
- Build với dummy credentials trong môi trường test; không dùng credential thật.

---

## P01 — Baseline inventory, reproducible commands, test evidence

**Priority:** P0  
**Dependency:** none

**Files được phép sửa**
- `README.md`
- `docs/PROJECT_AUDIT_REPORT.md`
- `docs/BASELINE_TEST_REPORT.md`
- `docs/PLACEHOLDER_STATUS.md`
- `TODO_LIST_ESP32S3_AI_First_v5.0.md` chỉ khi cần cross-reference
- Không sửa code production trừ khi phát hiện lỗi nhỏ được tách task.

**Triển khai**
- Ghi rõ revision/hash của source ZIP và ngày audit.
- Tách lịch sử test cũ khỏi kết quả mới.
- Kiểm tra `platformio.ini`, các environment, `build_project.bat`.
- Ghi chính xác target nào là mock, host, production.
- Sửa stale statements: `CalibratedSampleCsvExporter.cpp` đã tồn tại và có test; physical calibration vẫn BLOCKED. `BoardConfig.issueMask` đã được khai báo trong source hiện tại; không lặp lại lỗi cũ.
- Ghi rõ calibrated exporter là bounded host serializer/implementation; không đồng nghĩa cảm biến đã hiệu chuẩn.
- Đếm lại test case thực tế từ source; không ghi số test đã chạy nếu chưa thực thi.
- Xác định PIO executable/version và lệnh tái lập.

**Acceptance**
- Một bảng source status: implemented / partial / absent / blocked.
- Một bảng test evidence: command, environment, date, result, limitation.
- Không còn báo cáo hiện tại mâu thuẫn với source hiện tại.

**Tests**
- Chạy tất cả lệnh có thể chạy trong môi trường dev.
- Nếu môi trường không có PlatformIO, ghi NOT RUN và lý do; không thay bằng suy đoán.

---

## P02 — Fix ESP32-S3 target compile blockers

**Priority:** P0 / gate build  
**Dependency:** P01

**Files được phép sửa**
- `src/Diagnostics/SerialDiagnostics.cpp`
- `include/project/Diagnostics/SerialDiagnostics.h`
- `src/Diagnostics/HealthMonitor.cpp`
- `include/project/Diagnostics/HealthMonitor.h`
- `include/project/Models/HealthSnapshot.h`
- `src/Models/HealthSnapshot.cpp`
- Chỉ sửa thêm header/source được compiler chỉ ra trực tiếp; liệt kê trước trong report.

**Triển khai**
- Sửa declaration/definition mismatch trong SerialDiagnostics.
- Sửa assignment/field mapping HealthSnapshot.
- Giữ API semantics, không xóa field để làm build qua.
- Xử lý conversion warnings có nguy cơ truncation.
- Không dùng host-only code trong target branch.

**Acceptance**
- `pio run -e esp32-s3-devkitc-1` thành công khi BoardConfig gate đã được cấu hình hợp lệ.
- Native unit/integration build không regression.
- Compiler warnings mới được giải thích hoặc sửa.

**Tests**
- Target build.
- `pio test -e native-unit-tests`.
- `pio test -e native-integration-tests`.
- Nếu board config hiện vẫn intentionally unapproved, dùng compile-only configuration phù hợp nhưng không bypass production safety gate.

---

## P03 — Board, pin map, I2C and ADC hardware configuration

**Priority:** P0  
**Dependency:** P01

**Files được phép sửa**
- `include/project/BSP/BoardPins.h`
- `src/BSP/BoardPins.cpp`
- `include/project/BSP/BoardConfig.h`
- `src/BSP/BoardConfig.cpp`
- `include/project/BSP/BoardInfo.h`
- `src/BSP/BoardInfo.cpp`
- `platformio.ini`
- `docs/HARDWARE_CONFIGURATION.md` (tạo mới)

**Triển khai**
- Tạo một nguồn duy nhất cho pin mapping.
- DOCX pin map tham khảo: soil AOUT GPIO1/2/3; HX710B SCK GPIO5, DOUT GPIO6; rain AO GPIO7, DO GPIO10; buzzer GPIO11; MPU9250 + OLED SDA GPIO8, SCL GPIO9; BOOT GPIO0. Đây không phải xác nhận board-level.
- Chỉ kích hoạt literal pin sau khi đối chiếu chính xác ESP32-S3 board/module schematic, ADC-capable pin, strapping pin, flash/PSRAM restrictions và điện áp.
- Không dùng GPIO26–37 theo ghi chú DOCX; xác minh lại trên board cụ thể, không coi ghi chú này là thay thế datasheet.
- Khai báo I2C bus dùng chung cho MPU9250/OLED nếu đúng wiring.
- Không tự chốt I2C address/clock ngoài giá trị DOCX tham khảo MPU address 0x68, OLED 0x3C, I2C 400 kHz; kiểm tra thực tế.
- ADC resolution/attenuation và ADC linearity phải được xác nhận cho board/framework; DOCX dùng 12-bit và ADC_11db như cấu hình tham khảo.
- BoardConfig fail-closed khi chưa có approval.

**Acceptance**
- `BoardPins` không còn pin literal trùng lặp trong driver.
- Mọi pin được kiểm tra capability/conflict.
- Hardware config ghi board model/revision, pin, voltage, bus, evidence source.
- Không có pin TBD bị thay bằng giá trị đoán.

**Tests**
- Host tests cho duplicate/conflicting pin validation.
- Target compile.
- Hardware continuity/voltage check và I2C scan theo checklist; ghi kết quả thực đo.

---

## P04 — Production AppController composition and startup

**Priority:** P0  
**Dependency:** P02, P03

**Files được phép sửa**
- `src/main.cpp`
- `include/project/Managers/AppController.h`
- `src/Managers/AppController.cpp`
- `include/project/Tasks/ApplicationTasks.h`
- `src/Tasks/ApplicationTasks.cpp`
- `include/project/Queues/QueueRegistry.h`
- `src/Queues/QueueRegistry.cpp`
- `include/project/Managers/ConfigManager.h`
- `src/Managers/ConfigManager.cpp`
- `docs/TASK_RUNTIME_FLOW.md`

**Triển khai**
- Thay production `BOOT BLOCKED: runtime queue/task composition has no approved target configuration` bằng composition thật sau khi P03 config approved.
- `setup()` chỉ init serial, BSP safe state, diagnostics, ConfigManager, storage/sensor/service composition và gọi AppController.
- AppController sở hữu startup order, rollback/cleanup và fail-safe outputs.
- Tạo queue/task qua bounded static allocation nếu phù hợp; không cấp phát không giới hạn.
- SensorManager hiện nhận 4 sensor interface IMU/soil/rain/HX710B và `beginAll()` có thể yêu cầu cả 4. Thiết kế optional/required sensor theo `SensorSetConfig`; không ép sensor absent thành sensor lỗi nếu DSD sensor set cho phép absent.
- Tạo runtime state rõ ràng: STARTING, RUNNING_DEMO, RUNNING_AI, DEGRADED, FAULT, SHUTTING_DOWN.
- Không enable inference nếu schema/model/config gate không pass.
- Startup failure: outputs safe, inference off, cleanup bounded, log reason.
- Không feed watchdog vô điều kiện.

**Acceptance**
- Production branch chạy được startup path và hiển thị trạng thái rõ ràng.
- Không còn unconditional BOOT BLOCKED khi cấu hình hợp lệ.
- Không tự enable AI/mock trong production.
- Sensor absent theo cấu hình optional không làm crash.
- Task creation failure được phát hiện và rollback.

**Tests**
- AppController startup success/failure ở từng phase.
- Task creation failure injection.
- Config invalid/missing.
- Sensor optional/required matrix.
- Target flash + serial boot log.

---

## P05 — MPU9250 robust I2C, temperature and DOCX tilt algorithm

**Priority:** P1  
**Dependency:** P03

**Files được phép sửa**
- `include/project/Drivers/MPU9250Driver.h`
- `src/Drivers/MPU9250Driver.cpp`
- `include/project/Drivers/ImuAdapter.h`
- `src/Drivers/ImuAdapter.cpp`
- `include/project/Models/SensorFrame.h`
- `src/Models/SensorFrame.cpp`
- `include/project/Models/ProcessedFrame.h`
- `src/Models/ProcessedFrame.cpp`
- `docs/MPU9250_DRIVER.md`
- tests liên quan trong `test/`

**DOCX behavior phải port**
- MPU9250 I2C address trong DOCX: 0x68 (tham khảo).
- Wake via `PWR_MGMT_1=0x00`; gyro config `0x00`; accel config `0x00`.
- Accel scale trong DOCX: 16384 LSB/g; gyro scale 131 LSB/(deg/s), tương ứng các range mặc định mà DOCX giả định.
- Gyro stationary calibration: 200 samples, delay 5 ms; offsets X/Y.
- Accelerometer angles:
  - `rollAccel = atan2(ay, az) * 180 / PI`
  - `pitchAccel = atan2(-ax, sqrt(ay*ay + az*az)) * 180 / PI`
- Complementary filter alpha = 0.98:
  - `roll = alpha * (roll + gx*dt) + (1-alpha)*rollAccel`
  - `pitch = alpha * (pitch + gy*dt) + (1-alpha)*pitchAccel`
- `dt` phải tính từ timestamp monotonic thực, validate `dt > 0` và upper bound; gap lớn thì invalidate/reset filter theo policy.
- `tiltMax = max(abs(tiltRoll), abs(tiltPitch))`.
- MPU internal temperature formula in DOCX: `T = raw / 333.87 + 21.0`; chỉ áp dụng khi datasheet/config sensor revision xác nhận.

**Bắt buộc sửa lỗi DOCX**
- `mpuReadRegisters()` hiện không kiểm tra `endTransmission(false)`, `requestFrom()` count hoặc exact bytes; raw arrays có thể không được điền đủ. Driver ZIP phải trả IO_ERROR/TIMEOUT và không decode partial/uninitialized buffer.
- Kiểm tra WHO_AM_I; không assume sensor responded.
- Register write phải kiểm tra status.
- Decode signed int16 an toàn.
- Không thực hiện calibration nếu sensor sample invalid.
- Lưu offset/version qua CalibrationService; không ghi Preferences trực tiếp trong ISR.

**TBD**
- Axis/mounting orientation, address, bus speed, sensor clone identity, actual full-scale and filter settings need physical verification.
- Tilt formula DOCX là candidate, phải được golden-test trên các tư thế chuẩn trước khi chọn làm canonical.

**Acceptance**
- I2C NACK/short read/timeout -> invalid, không trả sample cũ.
- WHO_AM_I mismatch -> init fail.
- Temp/accel/gyro có validity riêng.
- Tilt đúng chiều và sai số nằm trong tolerance được project phê duyệt.
- Filter reset khi gap/calibration change.

**Tests**
- Fake TwoWire: NACK, short read, wrong ID, register write failure.
- Golden static poses: level, positive/negative roll/pitch.
- dt boundary, rollover, gap, non-finite.
- Hardware test MPU only + MPU/OLED shared I2C.

---

## P06 — Three independent soil ADC channels

**Priority:** P1  
**Dependency:** P03, P09 data contract

**Files được phép sửa**
- `include/project/Drivers/SoilMoistureDriver.h`
- `src/Drivers/SoilMoistureDriver.cpp`
- `include/project/Drivers/SoilMoistureAdapter.h`
- `src/Drivers/SoilMoistureAdapter.cpp`
- `include/project/Models/SensorFrame.h`
- `src/Models/SensorFrame.cpp`
- `include/project/Models/ProcessedFrame.h`
- `src/Models/ProcessedFrame.cpp`
- `include/project/Managers/SensorManager.h`
- `src/Managers/SensorManager.cpp`
- `docs/SOIL_MOISTURE_DRIVER.md`
- tests liên quan

**DOCX behavior phải port**
- 3 AOUT channels: GPIO1, GPIO2, GPIO3 theo pin map tham khảo.
- `SOIL_SAMPLES=5`, delay 5 ms giữa sample.
- DOCX mapping: `map(raw, AIR_VALUE=3000, WATER_VALUE=1200, 0, 100)`, clamp 0..100.
- Mean = `(p1+p2+p3)/3.0`.

**Chuyển đổi bắt buộc**
- 3 channel phải giữ raw, converted value, valid, quality, timestamp độc lập.
- AIR_VALUE/WATER_VALUE không được mặc định production; chuyển thành calibration profile riêng từng probe.
- Hỗ trợ số channel present/required theo SensorSetConfig.
- Mean chỉ tính từ các channel valid theo policy; phải lưu `valid_channel_count`; không giả định luôn đủ 3.
- Detect ADC saturation/open/short chỉ khi có tiêu chí đo được; không tự bịa raw range.
- Không chặn sensor acquisition bằng `delay()` lặp dài; dùng bounded acquisition schedule.

**Acceptance**
- Mỗi kênh có raw + validity + quality riêng.
- Một kênh lỗi không làm hai kênh còn lại mất dữ liệu.
- Average không bao gồm invalid.
- Calibration profile mismatch -> relative percent invalid.
- Không gọi giá trị relative percent là volumetric water content nếu chưa hiệu chuẩn vật lý.

**Tests**
- 3 channel normal.
- Một/hai/ba channel invalid.
- Raw ở hai calibration endpoints và đảo endpoint.
- ADC out-of-range/saturation policy.
- Average valid count và missing channels.
- Hardware channel-by-channel.

---

## P07 — YL-83 AO/DO and timestamp-based wetness accumulation

**Priority:** P1  
**Dependency:** P03, P09

**Files được phép sửa**
- `include/project/Drivers/RainSensorDriver.h`
- `src/Drivers/RainSensorDriver.cpp`
- `include/project/Drivers/RainAdapter.h`
- `src/Drivers/RainAdapter.cpp`
- `include/project/Models/SensorFrame.h`
- `src/Models/SensorFrame.cpp`
- `include/project/Models/ProcessedFrame.h`
- `src/Models/ProcessedFrame.cpp`
- `docs/RAIN_SENSOR_DRIVER.md`
- tests liên quan

**DOCX behavior phải port**
- AO GPIO7, DO GPIO10 (tham khảo).
- `RAIN_SAMPLES=8`, delay 2 ms.
- Raw-to-relative wetness map `RAIN_DRY_VALUE=4095`, `RAIN_WET_VALUE=0`, clamp 0..100.
- `g_rainDetected = (digitalRead(DO) == LOW)`.
- Wetness labels: dry if `<20`, light if `<60`, heavy otherwise.
- Rain accumulation window 120 s; sum(wetness percent)/100 => equivalent wet-seconds.
- Thresholds DOCX: watch 30, extreme 90 equivalent wet-seconds.

**Chuyển đổi bắt buộc**
- Giữ raw AO, DO state, validity và quality.
- Mapping endpoints phải là calibration per sensor board, không hard-code production.
- DO polarity configurable/verified.
- Wetness percentage is sensor surface wetness proxy, NOT rainfall mm.
- Accumulation dùng `delta_t` thực tế; xử lý gap/missing sample; không coi missing là dry.
- Sliding time window bằng timestamp; eviction theo age, không theo giả định one sample = one second.
- `rainAcc` invalid nếu coverage/window không đủ theo policy.

**Acceptance**
- AO/DO lỗi được biểu diễn riêng.
- DO active-low chỉ được dùng sau khi xác nhận board module.
- Accumulation đúng với irregular intervals và gaps.
- Không biến sensor wetness thành mm rainfall.

**Tests**
- ADC dry/wet endpoints.
- DO HIGH/LOW polarity.
- Missing sample and long gap.
- Irregular dt accumulation.
- 120-second window eviction.
- Hardware dry/wet test.

---

## P08 — HX710B robust read, raw-only first, calibrated PWP only after approval

**Priority:** P1  
**Dependency:** P03, P09

**Files được phép sửa**
- `include/project/Drivers/HX710BDriver.h`
- `src/Drivers/HX710BDriver.cpp`
- `include/project/Drivers/Hx710Adapter.h`
- `src/Drivers/Hx710Adapter.cpp`
- `include/project/Services/CalibrationService.h`
- `src/Services/CalibrationService.cpp`
- `include/project/Models/SensorFrame.h`
- `src/Models/SensorFrame.cpp`
- `include/project/Models/ProcessedFrame.h`
- `src/Models/ProcessedFrame.cpp`
- `docs/HX710B_DRIVER.md`
- `docs/CALIBRATION_CONTRACT.md`
- tests liên quan

**DOCX behavior phải port**
- SCK GPIO5, DOUT GPIO6 (tham khảo).
- 24-bit data read; sign extend if bit 23 set.
- pulse count after data = 25.
- wait-ready timeout 200 ms.
- average up to 3 successful reads (`HX_SAMPLES=3`).
- DOCX conversion:
  `kPa = (raw - RAW_ZERO_PRESSURE) * PRESSURE_FULL_SCALE_KPA / (RAW_FULL_SCALE - RAW_ZERO_PRESSURE)`.
- DOCX sample failure currently prints message but retains old `g_pressureKPa`; this is a known stale-data defect and MUST NOT be ported.

**Bắt buộc**
- Validate exact conversion timing and pulse count against actual HX710B variant/datasheet and load-cell/piezometer board.
- No indefinite busy wait.
- On all reads failed: output invalid; no stale fallback.
- Partial successful reads: record valid count and quality; policy decides whether average is accepted.
- Signed 24-bit conversion must be unit-tested.
- Raw-to-kPa converter remains disabled unless transducer type, range, excitation, wiring, zero/full-scale calibration, temperature/linearity and unit are approved.
- No `40.0 kPa`, raw zero `8000000`, raw full scale `8300000` in production defaults; these are DOCX demo placeholders.
- PWP baseline accepts only valid calibrated pressure samples; minimum sample count and duration must be configured/approved.
- Re-zero request triggers controlled state transition, not direct global mutation.

**Acceptance**
- timeout/invalid -> stale pressure not emitted as valid.
- baseline/history ignore invalid samples.
- no kPa output when calibration missing/invalid.
- calibration identity/version included.
- raw output can still be logged to DSD v1.2.

**Tests**
- DOUT never ready, timeout.
- 24-bit positive/negative raw decode.
- edge/pulse sequence fake clock.
- 0/1/2/3 valid sample policy.
- calibration absent, mismatch, reversed range, nonfinite.
- stale-value regression test.

---

## P09 — Unified sample snapshot, validity and FreeRTOS concurrency contract

**Priority:** P1 / architecture critical  
**Dependency:** P04, P05–P08 interfaces

**Files được phép sửa**
- `include/project/Models/SensorFrame.h`
- `src/Models/SensorFrame.cpp`
- `include/project/Models/ProcessedFrame.h`
- `src/Models/ProcessedFrame.cpp`
- `include/project/Queues/QueueRegistry.h`
- `src/Queues/QueueRegistry.cpp`
- `include/project/Managers/SensorManager.h`
- `src/Managers/SensorManager.cpp`
- `include/project/Tasks/ApplicationTasks.h`
- `src/Tasks/ApplicationTasks.cpp`
- `docs/DATA_CONTRACTS.md`
- `docs/TASK_RUNTIME_FLOW.md`
- tests liên quan

**DOCX behavior cần thay thế**
- DOCX global floats (`g_soilPercentAvg`, `g_pressureKPa`, `g_tiltMax`, `g_rainPercent`, `g_caseNo`, `g_alertLevel`) được nhiều task đọc/ghi.
- `volatile` không đảm bảo atomic multi-field snapshot hoặc mutual exclusion.
- ISR chỉ nên phát event/request nhẹ; không gọi Preferences, I2C, logging hoặc xử lý nặng.

**Triển khai**
- Dùng FreeRTOS Queue / mutex / critical section / immutable snapshot; chọn một mô hình nhất quán.
- Blynk, OLED, buzzer và risk task chỉ nhận snapshot.
- Sensor acquisition gán boot_id, sequence_no, monotonic timestamp.
- Mỗi sensor channel có status, freshness, validity, quality.
- Một lỗi field không làm invalid toàn frame nếu contract cho phép field độc lập; downstream policy quyết định required channels.
- ISR button chỉ set task notification/atomic flag và debounce ở task context.
- Không cho task Blynk giữ mutex trong network call.
- Không block vô hạn trên queue.
- Queue overflow có counter/event và policy rõ ràng.
- Reset window khi boot, sequence gap, timestamp gap, calibration/schema change.

**Acceptance**
- Không có shared mutable global sensor state giữa tasks.
- Snapshot đồng nhất và có timestamp.
- No data race theo review và stress test.
- Invalid/stale không biến thành safe state.
- Queue overflow có telemetry.

**Tests**
- Snapshot consistency under concurrent producer/consumers.
- Queue full/drop policy.
- sequence/timestamp gap resets windows.
- ISR event handling via task context.
- Stress test host/target.

---

## P10 — Port DOCX derived values and CASE 1–11 as DemoRulePolicy

**Priority:** P1  
**Dependency:** P05–P09

**Files được phép sửa**
- `include/project/Policies/DemoRulePolicy.h` (new)
- `src/Policies/DemoRulePolicy.cpp` (new)
- `include/project/Models/RiskAssessment.h`
- `src/Models/RiskAssessment.cpp`
- `include/project/Managers/RiskManager.h`
- `src/Managers/RiskManager.cpp` only for policy integration interface; do not replace existing validation/hysteresis
- `include/project/Services/` and `src/Services/` only for derived-metric service if required
- `platformio.ini` source filter if needed
- `docs/DEMO_RULE_POLICY.md` (new)
- tests `test/test_system_contracts/` or new `test/test_demo_rule_policy/`

**DOCX constants — preserve as demo defaults only**
- `BASELINE_SEC=15`
- `RATE_WINDOW_SEC=20`
- `RAIN_ACC_WINDOW_SEC=120`
- `CONFIRM_SAMPLES=3`
- `HIST_LEN=150`
- `TILT_WATCH_DEG=5.0`
- `TILT_DANGER_DEG=8.0`
- `TILT_EXTREME_DEG=12.0`
- `TILT_RAPID_RISE=1.5 deg / RATE_WINDOW_SEC`
- `PWP_WATCH_RISE=2.0 kPa`
- `PWP_DANGER_RISE=5.0 kPa`
- `PWP_HIGH_RISE=8.0 kPa`
- `PWP_EXTREME_RISE=12.0 kPa`
- `PWP_RAPID_RISE=1.0 kPa / RATE_WINDOW_SEC`
- `SOIL_WET=70.0%`
- `SOIL_SAT=85.0%`
- `RAIN_ACC_WATCH=30.0 equivalent wet-seconds`
- `RAIN_ACC_EXTREME=90.0 equivalent wet-seconds`
- DOCX rain light threshold = 20%; heavy threshold = 60%.
- `ALPHA=0.98` belongs to MPU filter, not rule policy.

**DOCX derived calculations**
- `tiltMax = max(abs(tiltRoll), abs(tiltPitch))`.
- `pwpRise = currentPressureKPa - baselinePressureKPa`.
- `pwpRate = avg3(current PWP) - avg3(PWP at RATE_WINDOW_SEC ago)`, only when sufficient history exists.
- `tiltRate` same difference of 3-sample averages.
- `rainAcc = sum(wetnessPct in trailing 120s)/100` as equivalent wet-seconds.
- Replace sample-index windows with timestamp-based windows and actual coverage; never include invalid samples as zero.
- Baseline resets only through explicit authorized command; valid pressure samples only.
- `g_tiltMax` uses max absolute roll/pitch after zero offset.

**Exact DOCX CASE priority order**
1. CASE 1: `rainAcc >= RAIN_ACC_WATCH && soil >= SOIL_SAT && pwpRise >= PWP_DANGER_RISE && tilt >= TILT_DANGER_DEG`
2. CASE 2: `soil >= SOIL_SAT && pwpRise >= PWP_DANGER_RISE && tilt >= TILT_DANGER_DEG`
3. CASE 3: `soil >= SOIL_SAT && pwpRise >= PWP_HIGH_RISE`
4. CASE 4: `pwpRate >= PWP_RAPID_RISE && tiltRate >= TILT_RAPID_RISE`
5. CASE 5: `tilt >= TILT_EXTREME_DEG`
6. CASE 6: `pwpRise >= PWP_EXTREME_RISE`
7. CASE 7: `rainAcc >= RAIN_ACC_EXTREME`
8. CASE 8: `rainAcc >= RAIN_ACC_WATCH && soil >= SOIL_WET && (pwpRise >= PWP_WATCH_RISE || pwpRate >= PWP_RAPID_RISE)`
9. CASE 9: `soil >= SOIL_WET && pwpRise >= PWP_DANGER_RISE && rainPercent < RAIN_LIGHT_THRESHOLD`
10. CASE 10: `tilt >= TILT_WATCH_DEG || tiltRate >= TILT_RAPID_RISE`
11. CASE 11: otherwise.

**CASE names**
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

**Levels**
- CASE 1–7 -> DANGER (DOCX comments 5–7 describe extreme, but numeric AlertLevel maps all 1–7 to DANGER).
- CASE 8–10 -> WATCH.
- CASE 11 -> SAFE.
- Confirmation: a new case must persist `CONFIRM_SAMPLES=3` consecutive valid evaluations before changing active case.
- Preserve priority order; overlapping conditions resolve to the first matching case.

**Critical safeguards**
- If required inputs for a CASE are invalid/stale, do not silently evaluate them as zero or fall through to CASE 11. Return `UNKNOWN/INSUFFICIENT_DATA` or explicit degraded policy.
- Rule policy must be selectable as DEMO only.
- Do not mix DOCX demo rules into production `RiskManager` defaults.
- All threshold values are versioned config with `DEMO_UNVALIDATED` provenance.

**Acceptance**
- Exact priority/name/level behavior reproduced for fully valid test inputs.
- Invalid inputs cannot produce false CASE 11.
- Hysteresis/confirmation semantics deterministic.
- Derived calculations correct with irregular timestamps.
- RiskManager existing approved-config gate remains intact.

**Tests**
- One test per CASE.
- Priority overlap tests (CASE 1 before 2; 2 before 3; etc.).
- Exact boundary tests at each threshold and just below.
- 3 consecutive confirm / reset pending count.
- Invalid/stale per input.
- Window not full, gap, reboot, baseline reset.
- Time-based rain accumulation and rate calculations.
- DEMO policy disabled in production configuration.

---

## P11 — SSD1306 OLED display

**Priority:** P2  
**Dependency:** P03, P09, P10

**Files được phép sửa**
- `include/project/Drivers/SSD1306DisplayDriver.h` (new)
- `src/Drivers/SSD1306DisplayDriver.cpp` (new)
- `include/project/Services/DisplayService.h` (new)
- `src/Services/DisplayService.cpp` (new)
- `include/project/Tasks/ApplicationTasks.h`
- `src/Tasks/ApplicationTasks.cpp`
- `platformio.ini`
- `docs/OUTPUT_POLICY.md`
- `docs/DISPLAY_SERVICE.md` (new)
- display tests

**DOCX behavior**
- SSD1306 128x64, I2C address 0x3C, shared `Wire` with MPU9250, per DOCX.
- Calibration page: `HIEU CHUAN NEN`, stable model instruction, remaining baseline seconds, PWP and rain raw.
- Alert page: warning/monitor/safe, CASE number/name, soil, rain accumulation, PWP rise, tilt.
- When alert: alternate data/alert pages every 2 s; danger page invert display in DOCX.

**Triển khai**
- Driver chỉ render primitives; service tạo view-model từ snapshot.
- Không đọc globals.
- Handle display absent without blocking sensor/risk/log pipeline.
- Avoid dynamic String allocations in repeated render loop.
- Update rate bounded/configured; do not redraw faster than necessary.
- Show UNKNOWN/FAULT when data invalid, not stale numeric as valid.

**Acceptance**
- OLED present/absent handled.
- UI pages readable on 128x64.
- Invalid sensor field shown as `--`/fault marker.
- Shared I2C does not break MPU acquisition.

**Tests**
- Mock display records render calls.
- Page selection by startup/calibration/risk.
- Missing display and I2C failure.
- Hardware display + MPU simultaneous.

---

## P12 — Buzzer output service

**Priority:** P2  
**Dependency:** P03, P09, P10

**Files được phép sửa**
- `include/project/Drivers/BuzzerDriver.h` (new)
- `src/Drivers/BuzzerDriver.cpp` (new)
- `include/project/Services/AlarmService.h` (new)
- `src/Services/AlarmService.cpp` (new)
- `include/project/Managers/OutputController.h`
- `src/Managers/OutputController.cpp`
- `docs/OUTPUT_POLICY.md`
- `docs/ALARM_SERVICE.md` (new)
- tests

**DOCX behavior**
- Buzzer GPIO11 reference.
- `BUZZER_ON_LEVEL=LOW` in DOCX; configurable because active level depends on buzzer module.
- SAFE: OFF.
- WATCH: pulse pattern every 200 ms for 1200 ms (three on pulses in DOCX).
- DANGER: continuous ON.
- Output task polls every 20 ms in DOCX; do not copy timing without target scheduling review.

**Triển khai**
- Command state separate from verified feedback state.
- Safe output at boot/reset/failure.
- No direct GPIO from RiskManager.
- Task creation failure detected.
- If buzzer absent, state NOT_PRESENT, not FAILED unless expected.
- Alarm pattern state machine is non-blocking.

**Acceptance**
- Correct patterns for all states.
- No blocking delays in alarm task.
- Fail-safe OFF on init failure unless policy explicitly requires alarm-on and hardware safety review approves.

**Tests**
- Fake clock pattern test.
- active-high/active-low mapping.
- transition WATCH->DANGER->SAFE.
- output driver failure and absent device.
- hardware current/polarity test.

---

## P13 — Blynk Wi-Fi telemetry and notifications

**Priority:** P2 / optional connectivity  
**Dependency:** P00, P09, P10

**Files được phép sửa**
- `include/project/Services/BlynkService.h` (new)
- `src/Services/BlynkService.cpp` (new)
- `include/project/Tasks/TaskBlynk.h` (new)
- `src/Tasks/TaskBlynk.cpp` (new)
- `include/project/Queues/QueueRegistry.h`
- `src/Queues/QueueRegistry.cpp`
- `platformio.ini`
- `docs/BLYNK_INTEGRATION.md` (new)
- tests

**DOCX configuration to preserve as configurable defaults**
- Virtual pins:
  - V0 MPU temperature °C
  - V1 average soil relative %
  - V2 rain surface wetness relative %
  - V3 tilt degrees
  - V4 PWP kPa (ONLY if calibrated)
  - V5 status text `AN TOAN` / `THEO DOI` / `NGUY HIEM`
  - V6 rain raw
  - V7 CASE name
- Events: `theo_doi` Warning, `nguy_hiem` Critical.
- Telemetry interval 15,000 ms.
- WATCH notifications: 3 notifications, 5,000 ms gap.
- DANGER notification repeat: 10,000 ms.
- `NOTIFY_WHEN_SAFE=false` default.
- DOCX uses Wi-Fi STA auto reconnect and Blynk task; Blynk calls must be serialized in one task.

**Triển khai**
- Optional feature; no Wi-Fi required for offline core.
- One task owns all Blynk API calls and reconnect lifecycle.
- It consumes immutable latest snapshot; never reads global fields.
- Reconnect/backoff bounded; no busy loop.
- Do not send invalid PWP as 0; omit/mark unavailable according to Blynk API contract.
- Case index range checked before name lookup.
- Notification policy rate-limited; avoid notification flood after reconnect.
- No secrets in source. Missing credentials -> disabled.
- Blynk task/network failures cannot block sensor, CSV, alarm.

**Acceptance**
- V0–V7 mapping documented and implemented.
- Data interval and event transitions match configured policy.
- Wi-Fi down/up does not stop local operation.
- Credentials never logged.
- Blynk library version pinned and build verified.

**Tests**
- Fake Blynk client: connect/disconnect, send errors, reconnect.
- invalid field mapping.
- CASE index bounds.
- WATCH notification count/gap.
- DANGER repeat gap.
- SAFE transition option.
- Hardware Wi-Fi loss/recovery.

---

## P14 — Runtime storage and CSV dataset integration

**Priority:** P2  
**Dependency:** P04, P09

**Files được phép sửa**
- `include/project/Storage/StorageManager.h`
- `src/Storage/StorageManager.cpp`
- `include/project/Storage/CsvLogger.h`
- `src/Storage/CsvLogger.cpp`
- `include/project/Storage/CsvRecord.h`
- `src/Storage/CsvRecord.cpp`
- `include/project/Queues/LogRecord.h`
- `src/Queues/QueueRegistry.cpp`
- `include/project/Dataset/CsvSchemas.h`
- `src/Dataset/DsdRawSampleCsvExporter.cpp`
- `src/Dataset/CalibratedSampleCsvExporter.cpp`
- `docs/CSV_DATASET_CONTRACT.md`
- `docs/STORAGE_DESIGN.md`
- tests

**DOCX behavior**
- Serial diagnostics every sensor tick.
- Blynk telemetry every 15 s.
- Existing DOCX has no equivalent of ZIP DSD v1.2 structured dataset export; do not replace DSD with the DOCX display line.

**Triển khai**
- Logger task is the only storage writer.
- Keep DSD v1.2 raw CSV schema exactly 36 columns as currently defined; preserve field order and validity/quality.
- Keep operational telemetry CSV separate from DSD training dataset.
- Do not change legacy 6-column CSV consumer contract without version/migration.
- Raw and calibrated artifacts separate.
- Calibrated exporter implementation exists; update stale docs to `IMPLEMENTED — bounded serializer; physical calibration may remain blocked`.
- Annotation and row-level evaluation exporters remain separate tasks if not present.
- Bounded write, flush, rotation, disk full/read-only/error handling.
- Queue overflow/drop counters; critical events priority over routine samples.
- Never serialize NaN/Inf as valid numeric.

**Acceptance**
- DSD 36 columns exact.
- Existing operational CSV compatibility maintained.
- CSV parse/replay round trip.
- SD absent/full/write failure does not block sensor acquisition indefinitely.
- Every dropped record is counted.

**Tests**
- DSD header/order/36 fields.
- quote/escape/CR/LF.
- invalid numeric blank + valid flags.
- nonfinite rejection.
- queue full and storage error.
- power loss/truncated final row recovery.
- raw/calibrated artifact separation.

---

## P15 — Hardware integration, fault injection and 72-hour endurance

**Priority:** P0 release gate  
**Dependency:** P02–P14

**Files được phép sửa**
- `docs/HARDWARE_TEST_PLAN.md` (new)
- `docs/FAULT_INJECTION_REPORT.md` (new)
- `docs/ENDURANCE_TEST_REPORT.md` (new)
- test files and bug-fix source files approved by defect report
- `docs/RELEASE_CHECKLIST.md`

**Test sequence**
1. Board-only boot/serial.
2. I2C scan; MPU only; OLED only; both shared bus.
3. Soil 1, Soil 2, Soil 3 separately and together.
4. YL-83 AO and DO.
5. HX710B raw acquisition and timeout.
6. All sensors simultaneously.
7. Buzzer patterns.
8. OLED display states.
9. CSV with SD absent/full/removed if applicable.
10. Blynk online/offline/reconnect.
11. Disconnect/reconnect each sensor during runtime.
12. Queue saturation and slow storage.
13. Power cycle and boot recovery.
14. 72-hour continuous run.

**Metrics**
- reset reason, uptime, heap low-water/largest block, stack high-water per task, queue high-water/drop counters, sensor freshness/error counts, loop/WCET, storage latency, Blynk reconnect, alarm transitions.
- Task stack/priority/queue depth/watchdog values must be measured, not guessed.

**Acceptance**
- No watchdog reset during endurance.
- No unbounded memory growth.
- No silent sample loss.
- Sensor disconnection results in invalid/degraded state.
- Local sensor/risk/alarm/log pipeline continues during Wi-Fi loss.
- 72-hour run report has build hash, board revision, setup, raw evidence and defects.

---

## P16 — Edge Impulse actual model integration

**Priority:** P1 after hardware data readiness  
**Dependency:** P09, P14, approved FSS/TCS/GTIS, actual EI export

**Files được phép sửa**
- `include/project/AI/EdgeImpulseAdapter.h`
- `src/AI/EdgeImpulseAdapter.cpp`
- `include/project/AI/SchemaRegistry.h`
- `src/AI/SchemaRegistry.cpp`
- `include/project/AI/IInferenceEngine.h`
- `src/AI/IInferenceEngine.cpp`
- generated EI library folder only by copying official export unchanged
- `platformio.ini`
- `docs/EDGE_IMPULSE_ADAPTER.md`
- `docs/AI_PIPELINE_HOST_TESTING.md`
- tests

**Triển khai**
- Require actual EI deployment export and model metadata.
- Bind feature count/order/units/scaling/window/stride from actual model and approved FSS.
- Verify generated library API and model input/output dimensions.
- Regression output is not probability by default.
- Verify finite output, output dimensions, model hash, schema hash.
- No mock runtime in production.
- If artifact absent/mismatch, inference disabled and system state UNKNOWN/DEGRADED; rule policy remains independent.

**Acceptance**
- Real model inference on ESP32-S3.
- Host replay and target output agree within tolerance.
- RAM/Flash/inference latency measured.
- No AI enable with unapproved target/feature schema.

**Tests**
- Model absent/hash mismatch/dimension mismatch.
- non-finite output.
- golden replay.
- concurrent inference prevention.
- target memory and timing.

---

## P17 — Production release approval

**Priority:** final gate  
**Dependency:** P15, P16, approved calibration/FSS/TCS/GTIS/RiskConfig

**Files được phép sửa**
- `docs/RELEASE_CHECKLIST.md`
- `docs/RELEASE_MANIFEST.md` (new)
- version/config metadata only after approval

**Acceptance**
- Board/BOM and pin map verified.
- Calibration and sensor set versioned.
- FSS/model/target contract aligned.
- Ground truth provenance accepted; no GT5 misrepresented.
- Risk config and thresholds approved.
- Fault injection, replay, endurance, resource budgets pass.
- Safety review accepts limitations.
- OTA disabled unless independently validated.
- Release package contains firmware/model/config hashes and test evidence.

---

## 3. DOCX-to-ZIP porting checklist

| DOCX element | ZIP destination | Task |
|---|---|---|
| MPU9250 register read/write | MPU9250Driver | P05 |
| Gyro calibration 200 × 5 ms | CalibrationService / MPU driver | P05 |
| Roll/Pitch + alpha 0.98 | MPU processing | P05 |
| MPU temperature formula | SensorFrame/ProcessedFrame | P05 |
| 3 soil channels + mean | Soil driver/SensorManager | P06 |
| YL-83 AO/DO and wetness | Rain driver | P07 |
| Rain accumulation 120 s | Derived metrics | P07/P10 |
| HX710B 24-bit read, 25 pulses, 200 ms timeout | HX710B driver | P08 |
| PWP baseline/rise/rate | Derived metrics | P08/P10 |
| CASE 1–11 and confirmation | DemoRulePolicy | P10 |
| OLED calibration/alert pages | DisplayService | P11 |
| Buzzer SAFE/WATCH/DANGER | AlarmService | P12 |
| Blynk V0–V7/events | BlynkService | P13 |
| BOOT button zero/reset demo | Command/event service | P09/P10 |
| Serial telemetry | Diagnostics/logger | P14 |
| Secret credentials | external provisioning | P00 |

---

## 4. Known DOCX defects that must not be ported

1. **HX710B stale PWP:** failed reads leave `g_pressureKPa` unchanged and print that old value is retained. ZIP must invalidate it and exclude it from baseline/history/risk.
2. **MPU I2C short read:** `mpuReadRegisters()` does not verify transaction status or exact byte count; destination buffers may be partly uninitialized. ZIP must fail the sample.
3. **Soil validity absent:** all 3 channels are mapped and averaged regardless of disconnected/faulty sensor.
4. **Timing assumption:** DOCX treats samples as one-second intervals despite sensor reads/delays and task/network activity. Use monotonic timestamps and time-based windows.
5. **Data races:** global floats shared between loop, Blynk and buzzer tasks; `volatile` is not synchronization.
6. **Unapproved calibration:** soil ADC endpoints, rain endpoints, HX zero/full scale and 40 kPa are placeholders requiring physical calibration.
7. **GPIO0 button:** boot strapping pin; verify hardware and avoid lengthy work in ISR.
8. **Blynk credentials:** rotate if real; never copy into ZIP.
9. **CASE safe fallthrough:** if invalid sensors are treated as zero/default, the logic may report CASE 11. Invalid inputs must produce UNKNOWN/insufficient data, not SAFE.
10. **Fixed alpha/dt:** filter must validate actual dt and reset/invalidate after gaps.
11. **Blynk notification behavior:** reconnect can cause duplicate/late notification; test rate limiting and transition semantics.
12. **PWP baseline:** never include invalid or stale values.

---

## 5. Final acceptance definition

**Hardware Demo runnable** means:
- Target build + flash + serial boot succeeds.
- Actual installed sensors are detected and sampled.
- Sensor errors propagate as invalid/degraded.
- DOCX demo derived calculations and CASE policy run only with valid inputs and explicitly marked demo config.
- OLED/Buzzer work if installed.
- Blynk is optional and cannot block local operation.
- CSV output and provenance are valid.
- No claim of AI model inference unless real EI export is integrated.

**AI Production ready** additionally requires:
- Actual EI export and matching approved FSS/TCS/GTIS.
- Approved physical calibration and RiskConfig.
- Independent evaluation, fault injection, replay and 72-hour endurance.
- Release manifest and safety review.

**Không được đánh dấu toàn bộ dự án DONE chỉ vì firmware build/flash thành công.**
