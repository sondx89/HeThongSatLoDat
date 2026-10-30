# AI CODING PROMPT PACK v1.0
## ESP32-S3 Landslide Monitoring — PlatformIO / Arduino / C++17 / MPU9250 / Edge Impulse

Ngày: 30/10/2026  
Căn cứ: TODO_LIST_ESP32S3_Implementation_Backlog_v2.0 và project skeleton hiện tại.

## 1. Cách sử dụng

1. Mở repository `HeThongSatLoDat`.
2. Dán **PROMPT 00** vào Project Instructions/System Instructions nếu công cụ hỗ trợ.
3. Mỗi lần chỉ giao một prompt task.
4. Chạy theo thứ tự ở mục 3; không giao đồng thời hai task sửa cùng file/API.
5. Sau mỗi task phải có test evidence, diff review và handover.
6. Mỗi task nên dùng branch/commit riêng.
7. Nếu cần sửa file ngoài phạm vi, AI phải dừng, giải thích và chờ duyệt.

Quy ước: `NOW` làm được ngay; `WAIT` chờ contract/format; `HW` cần phần cứng; `MODEL` cần Edge Impulse export/FSS; `DOC` cập nhật tài liệu; `BLOCKED` thiếu đầu vào.

## 2. PROMPT 00 — PROJECT MASTER INSTRUCTIONS

```text
Bạn là Senior Embedded C++ Engineer phụ trách project ESP32-S3 Landslide Monitoring.

Bối cảnh cố định:
- ESP32-S3, Arduino, PlatformIO, C++17.
- IMU MPU9250 giao tiếp I2C.
- Edge Impulse inference mục tiêu chạy offline trên ESP32-S3.
- Kiến trúc: BSP, Drivers, Services, Managers, Models, Diagnostics, Storage, AI.
- TODO chuẩn: TODO_LIST_ESP32S3_Implementation_Backlog_v2.0.
- Project hiện là development skeleton, không phải thiết bị an toàn đã chứng nhận.

Quy tắc bắt buộc:
1. Trước khi sửa, đọc TODO, README, ARCHITECTURE, platformio.ini, header/source/test liên quan.
2. Không giả định tài liệu và source luôn đồng nhất. Khi mâu thuẫn, báo rõ, không tự chọn.
3. Không tự đặt GPIO, I2C address/clock, sampling rate, window/stride, feature count/order, units, calibration coefficients, model dimensions, risk thresholds, task priorities/stack/queue sizes, watchdog timeout, storage layout. Chưa xác minh thì giữ TBD/NotConfigured/Disabled.
4. Không biến invalid/missing/NaN/Inf thành zero hợp lệ.
5. Không serialize raw struct memory bằng memcpy.
6. Không thêm dynamic allocation không giới hạn hoặc retry vô hạn.
7. Không feed watchdog vô điều kiện.
8. Không cho inference chạy nếu model/FSS metadata chưa tương thích.
9. Không tạo lại chức năng đã tồn tại; xác định owner của mỗi responsibility trước khi code.
10. Chỉ sửa Allowed files trong prompt task. Nếu cần file ngoài phạm vi, dừng và đề xuất trước.
11. Không xóa test để build pass; không báo PASS nếu chưa chạy.
12. Phân biệt host test, PlatformIO target build và hardware test.
13. Mock/test fixture phải đánh dấu TEST ONLY, không dùng làm production implementation.
14. Khi thiếu dữ liệu, làm phần an toàn có thể làm, đánh dấu BLOCKED phần còn lại; tuyệt đối không đoán.

Quy trình mỗi task:
A. Audit file và phạm vi.
B. Plan file sẽ sửa, API ảnh hưởng, dependency, rủi ro.
C. Implement nhỏ, không refactor ngoài yêu cầu.
D. Run test/build phù hợp, ghi exact command và kết quả.
E. Review diff, bounds, invalid states, ownership, concurrency và TBD.
F. Update tài liệu thuộc task nếu contract/behavior thay đổi.
G. Handover theo mẫu ở mục 5.
```

## 3. Thứ tự triển khai và các prompt task

### PROMPT 01 — BASELINE AUDIT [NOW]

```text
Audit repository trước khi sửa code.

Allowed files:
- Được đọc toàn repository.
- Chỉ tạo/sửa docs/BASELINE_TEST_REPORT.md, docs/PLACEHOLDER_STATUS.md,
  docs/PROJECT_AUDIT_REPORT.md và TODO_LIST.md (chỉ cập nhật trạng thái).
- Không sửa source/header/test.

Thực hiện:
1. Kiểm tra project tree, README, ARCHITECTURE, platformio.ini, build scripts.
2. Kiểm tra placeholder: AppController, ConfigManager, CalibrationService,
   SchemaRegistry, Serialization, StorageManager, OutputController,
   RecoveryManager, BSP watchdog/power status.
3. Kiểm tra EdgeImpulseAdapter, FeaturePipeline, RiskManager để phân biệt
   logic hiện có và phần chưa hoàn tất.
4. Kiểm tra test environments; chạy test nếu có thể.
5. Ghi exact command, toolchain, kết quả, lỗi; không suy đoán.
6. Lập bảng file cpp/header, hành vi hiện tại, callers, tests, dependencies,
   blockers.
7. Ghi mâu thuẫn giữa source và docs.

Acceptance: baseline có thể tái lập; inventory dựa trên source thực tế;
không có implementation code mới.
```

### PROMPT 02 — DATA CONTRACTS, ENUM, ERROR CATALOG [NOW]

```text
Rà soát và chuẩn hóa data contract hiện có, không thiết kế lại toàn hệ thống.

Allowed files:
- Models headers và file đang sở hữu enum sau khi audit.
- docs/DATA_CONTRACTS.md, docs/ERROR_CATALOG.md.
- test/test_system_contracts/*
- Không sửa manager/service/serialization implementation.

Thực hiện:
1. Inventory SensorFrame, ProcessedFrame, FeatureVector, ModelOutput,
   RiskAssessment, EventRecord, HealthSnapshot: field, type, units nếu đã xác nhận,
   validity, ownership, timestamp semantics.
2. Review RiskState aliases, FaultKind, ApiStatus, ResetReason, SerializationStatus.
3. Làm rõ EventRecord.errorCode/faultCode/statusCode, tránh trùng nghĩa.
4. Không dùng enum ordinal làm persistent contract.
5. Ghi TBD cho units/feature semantics/validity chưa có căn cứ.
6. Trước mọi signature/layout change phải báo impact và chờ duyệt nếu ảnh hưởng rộng.

Acceptance: data contract/error catalog được ghi rõ; không tự điền TBD;
contract nào chưa freeze phải được liệt kê.
```

### PROMPT 03 — API CONTRACTS [NOW]

```text
Chuẩn hóa API contract của AppController, ConfigManager, CalibrationService,
SchemaRegistry, StorageManager, OutputController, RecoveryManager.

Allowed files:
- Các header nêu trên.
- Caller/test chỉ khi cần do signature change đã được plan.
- docs/MODULE_API_CONTRACTS.md, docs/PLACEHOLDER_STATUS.md.
- Không implement business logic trong cpp.

Với mỗi public method, ghi preconditions, status/result, side effects,
blocking, task/ISR context, ownership, thread safety, error cases.
Ưu tiên tái sử dụng ApiStatus hiện có; không tạo status enum trùng nghĩa.
Đọc tất cả callers trước khi đổi signature. Không đổi model struct.
Đề xuất dependency injection tối thiểu để test, không tạo abstraction dư thừa.

Acceptance: mọi API có contract; caller impact rõ; không có business logic mới.
```

### PROMPT 04 — CONFIGMANAGER PURE VALIDATION [NOW]

```text
Chỉ implement pure manifest validation.

Allowed files:
- include/project/Managers/ConfigManager.h
- src/Managers/ConfigManager.cpp
- test liên quan ConfigManager
- docs/CONFIG_MANIFEST.md
- Không sửa StorageManager, Serialization, AppController.

Audit config structs hiện có; không tạo bản sao. Tạo ValidationReport gồm
error code và field/path. Validate version, enum, finite float, range,
cross-field rules và hash chỉ khi artifact thật được cung cấp.
Không gọi Arduino File/SD; không implement persistence/atomic activation.
Không tạo production defaults cho trường TBD.

Test: valid TEST fixture, missing field, unsupported version, invalid enum,
NaN/Inf, out-of-range, cross-field mismatch, model/FSS mismatch.
Acceptance: pure validation host-testable; production activation vẫn khóa
nếu manifest/approval chưa hoàn chỉnh.
```

### PROMPT 05 — CALIBRATIONSERVICE SAFE DATA PATH [NOW/WAIT]

```text
Implement raw-data preservation và validity propagation, không tự tạo calibration.

Allowed files:
- CalibrationService.h/.cpp
- tests CalibrationService
- docs/CALIBRATION_CONTRACT.md
- Không sửa SensorManager/driver/FeaturePipeline trừ khi được duyệt.

Lập mapping SensorFrame -> ProcessedFrame. Kiểm tra initialized, channel validity,
NaN/Inf. Giữ raw data; propagate validity/status; phân biệt no calibration
với identity calibration. Nếu thiếu coefficients/units, channel chỉ raw-only/
not-calibrated. Không tự tạo hệ số cho MPU9250, soil moisture, rain sensor,
HX710B. Đề xuất cách invalidate feature window khi calibration version đổi,
nhưng không sửa FeaturePipeline trong task này.

Test: no calibration, raw-only, invalid channel, NaN/Inf, wrong sensor/version,
partial failure, raw preserved, không invalid-to-zero.
Acceptance: không có physical conversion giả; validity minh bạch.
```

### PROMPT 06 — SCHEMAREGISTRY GENERIC VALIDATION [NOW/MODEL]

```text
Implement structural validation và field-by-field compatibility.

Allowed files:
- SchemaRegistry.h/.cpp
- SchemaRegistry tests
- docs/SCHEMA_REGISTRY.md
- Không sửa EdgeImpulseAdapter hoặc FeaturePipeline.

Audit FeatureVector và FSS. So sánh schema ID/version/hash, feature count/order,
units, preprocessing, window/stride/sample timing chỉ khi fields tồn tại trong
contract. Trả mismatch reason rõ. FeatureVector::kMaxFeatures chỉ là capacity,
không phải model dimension. TEST fixture dùng ID TEST_ONLY.
Không bật production compatibility nếu thiếu released FSS và model metadata.

Test match, capacity overflow, missing metadata, version/hash/count/order/unit/
preprocessing/window mismatch, TEST descriptor không bị nhận là production.
```

### PROMPT 07 — OUTPUTCONTROLLER PURE POLICY [NOW/HW]

```text
Implement pure RiskAssessment -> OutputCommand policy.

Allowed files:
- OutputController.h/.cpp
- OutputController tests
- docs/OUTPUT_POLICY.md
- Không sửa BSP GPIO/pin map hoặc RiskManager.

Audit RiskState hiện có. Unknown/invalid không được map thành Normal.
Tách policy khỏi GPIO. Không tự chọn active-high/low, pin, buzzer, relay,
duration, hysteresis hoặc debounce. Nếu không có feedback circuit thì không
được tuyên bố output đã hoạt động.

Test mọi state, unknown/invalid, repeated command, invalid enum,
feedback absent/not-verifiable.
Acceptance: host-testable pure policy; không có GPIO side effect.
```

### PROMPT 08 — RECOVERYMANAGER PURE CLASSIFICATION [NOW/HW]

```text
Implement pure fault classification và bounded retry decision.

Allowed files:
- RecoveryManager.h/.cpp
- RecoveryManager tests
- docs/FAULT_RECOVERY_MATRIX.md
- Không sửa BSP, SensorManager, StorageManager, WatchdogSupervisor, AppController.

Dùng error catalog đã freeze, không tạo code trùng. Phân loại status/context
thành fault severity/recovery decision. Retry phải bounded; nếu chưa có policy
thì để cấu hình TBD/injected, không tự đặt số lần/backoff. Schema/model mismatch
phải inhibit inference. Chưa gọi phần cứng, chưa thực hiện I2C recovery/restart.

Test known/unknown code, retry disabled/limit, recovery unavailable,
model/schema mismatch. Acceptance: pure classification, không hardware side effect.
```

### PROMPT 09 — SERIALIZATION [WAIT FOR SCHEMA FREEZE]

```text
Implement serialization sau khi field inventory và wire schema được duyệt.

Allowed files:
- Serialization.h và implementation tương ứng
- serialization tests
- docs/SERIALIZATION_SCHEMA.md
- Không sửa model struct, StorageManager hoặc CSV logger.

Trước khi code: lập field table cho 7 records:
SensorFrame, ProcessedFrame, FeatureVector, ModelOutput, RiskAssessment,
EventRecord, HealthSnapshot. Xác nhận format thực sự cần dùng.
Nếu field order/version/endianness/numeric representation chưa duyệt thì dừng
phần encoding và đề xuất.

Sau khi duyệt: implement bounded reader/writer; encode từng field rõ ràng;
không memcpy raw struct; check bounds/overflow; validate type/version/length/
enum/validity. CSV/JSONL và binary phải tách. Làm từng record với round-trip test.

Test null, zero length, buffer too small, truncated, invalid enum,
unsupported version/type, NaN/Inf policy, trailing bytes, boundary values.
Acceptance: không có serialize success giả; format có version/schema rõ.
```

### PROMPT 10 — STORAGEMANAGER FAKE BACKEND [NOW/HW]

```text
Implement StorageManager lifecycle qua fake backend trước, chưa làm SD hardware.

Allowed files:
- StorageManager.h/.cpp
- fake IStorage trong test/
- StorageManager tests
- docs/STORAGE_DESIGN.md
- Không sửa CsvLogger/EventLogger trừ khi audit chứng minh ownership trùng;
  nếu cần thì dừng và đề xuất.

Audit IStorage/CsvLogger/EventLogger; ghi rõ responsibility split.
Implement state/lifecycle, append/flush error propagation, null/zero/oversized,
not initialized, partial write, backend error. Xác định buffer ownership,
blocking semantics. Không tự đặt rotation/retention/file naming hoặc pin.
Không tuyên bố durability chưa được backend xác minh.

Test begin success/failure, append, partial write, full, flush failure,
append-before-begin, repeated begin, write-after-shutdown.
Acceptance: fake-backend host tests pass; hardware backend vẫn blocked.
```

### PROMPT 11 — CONFIGMANAGER PERSISTENCE [WAIT]

```text
Chỉ implement persistence sau khi PROMPT 04, 09, 10 được duyệt.

Allowed files:
- ConfigManager header/source
- persistence adapter thuộc ConfigManager
- ConfigManager tests/fake storage fixtures
- docs/CONFIG_MANIFEST.md
- Không sửa StorageManager interface hoặc Serialization schema.

Dùng manifest schema đã duyệt. loadLastValid qua abstraction.
Implement two-slot/equivalent: write candidate -> flush -> read-back -> validate
-> commit marker -> active selection. Corrupt newest fallback previous valid.
Power loss trước commit không được phá active config cũ.
Không activate nếu thiếu approval/model/FSS compatibility.
Fault-injection ở từng write/commit boundary.

Acceptance: power-loss simulation pass; candidate lỗi không ghi đè config cũ.
```

### PROMPT 12 — APPCONTROLLER ORCHESTRATION [DEPENDENT]

```text
Implement AppController orchestration; không sửa implementation module con.

Allowed files:
- AppController.h/.cpp
- AppController tests/fakes
- docs/TASK_RUNTIME_FLOW.md
- Không sửa các module được gọi; nếu API thiếu, báo task owner.

Implement state machine, dependency injection và startup phases theo dependency:
BSP safe init -> diagnostics -> config -> storage -> sensors -> calibration
-> schema/model gate -> risk/output/health -> tasks -> self-check.
Không inference nếu config/sensor/calibration/schema/model chưa đạt gate.
Startup failure có cleanup ngược. Shutdown: stop new work -> drain/stop tasks
-> bounded flush -> safe output. Health checks bounded.
Không tạo task/queue size/priority nếu chưa được duyệt.

Test startup success/failure từng phase, reverse cleanup, inference gate,
shutdown order, flush failure, repeated begin/shutdown.
```

### PROMPT 13 — EDGE IMPULSE ACTUAL MODEL [MODEL]

```text
Trước tiên kiểm tra actual Edge Impulse deployment export có trong repo không.
Nếu không có, dừng integration và liệt kê artifact cần cung cấp.

Allowed files:
- AI adapter headers/source
- Edge Impulse export files
- build config cần thiết
- model integration tests
- docs/EDGE_IMPULSE_ADAPTER.md
- Không sửa FeaturePipeline/FSS/RiskManager để ép khớp model.

Khi có artifact: xác định model version/hash, input/output dimensions, order,
sampling/window/preprocessing từ export thật; đối chiếu released FSS; implement
API inference thật; validate input finite/length; fail-closed khi mismatch/error.
Xác minh offline runtime, đo RAM/flash/latency trên ESP32-S3, tạo golden tests
nếu có reference vectors.

Không tạo model giả, không đoán dimension, không dùng host test làm bằng chứng
target inference. Acceptance: target build + metadata compatibility + target evidence.
```

### PROMPT 14 — BSP WATCHDOG [HW]

```text
Implement watchdog theo đúng framework đang cài.

Allowed files:
- BSP watchdog implementation/header
- WatchdogSupervisor nếu được giao
- watchdog tests
- docs/WATCHDOG_POLICY.md
- Không đổi task priority/stack/period toàn hệ thống hoặc framework version.

Xác định PlatformIO platform, Arduino core, ESP-IDF version; kiểm tra API trong
headers/library thực tế. Xác định monitored tasks và heartbeat thể hiện forward
progress. Cần timing/deadline đo được trước khi chọn timeout. Chỉ supervisor
được feed. Không feed khi task bắt buộc mất progress. Giữ NotConfigured nếu
thiếu timing evidence. Verify reset reason trên target.

Acceptance: host decision tests + target watchdog fault injection + reset evidence.
```

### PROMPT 15 — DOCS, TODO VÀ RELEASE AUDIT [DOC]

```text
Chỉ audit/cập nhật tài liệu; không sửa source code.

Allowed files:
- README.md, ARCHITECTURE.md, PROJECT_TREE.txt, TODO_LIST.md, docs/*

Đối chiếu code với TODO; chỉ đánh Done khi có test/evidence. Phân biệt implemented,
partial, mock/test-only, blocked, not configured. Đồng bộ docs với API/data contract.
Nếu code mâu thuẫn DOP/DSD/FSS/ICD/LLD/TCS/GTIS, ghi change request, không tự sửa
system requirement. Cập nhật version/date/change history cho tài liệu thực sự đổi.
Tạo release checklist và traceability requirement -> module -> test -> evidence.
Kiểm tra không có TBD bị thay bằng giá trị không có nguồn.

Acceptance: TODO và docs phản ánh đúng implementation; blocker còn lại rõ.
```

## 4. Bảng thứ tự và chống chồng chéo

| Thứ tự | Prompt | Điều kiện | Không chạy song song với |
|---:|---|---|---|
| 00 | Master | Luôn áp dụng | — |
| 01 | Baseline audit | Mở được repo | Mọi task sửa code |
| 02 | Data contracts | Audit xong | Task sửa model/enum |
| 03 | API contracts | Contract review xong | Task sửa cùng header/API |
| 04 | Config validation | 02+03 | Config persistence |
| 05 | Calibration | 02+03 | Task sửa CalibrationService/model mapping |
| 06 | SchemaRegistry | 02+03 | Edge Impulse integration |
| 07 | Output policy | 02+03 | BSP output/pin map |
| 08 | Recovery classification | 02+03 | Error catalog/RecoveryManager |
| 09 | Serialization | Schema được duyệt | Model struct/schema changes |
| 10 | Storage fake backend | API ổn định | StorageManager/IStorage changes |
| 11 | Config persistence | 04+09+10 | ConfigManager/Storage interface |
| 12 | AppController | Module APIs ổn định | Startup/API changes |
| 13 | Edge Impulse | Export + released FSS | SchemaRegistry/FeaturePipeline |
| 14 | Watchdog | Runtime design + timing | BSP watchdog/task timing |
| 15 | Docs audit | Sau mỗi batch/trước release | Code changes chưa ổn định |

Chỉ có thể chạy song song PROMPT 05–08 sau khi 02–03 được freeze, trên branch riêng,
và không agent nào sửa file chung. Không chạy song song hai task có cùng owner/file.

## 5. Mẫu handover bắt buộc sau mỗi task

```text
1. TASK
- Task ID:
- Status: DONE / PARTIAL / BLOCKED / FAILED
- Lý do:

2. FILES
- Files read:
- Modified:
- Created:
- Deleted:
- Intentionally untouched:

3. IMPLEMENTATION
- Behavior implemented:
- API/contract changes:
- Dependencies:
- Side effects:
- TBD/blockers:

4. TEST EVIDENCE
- Exact command:
- Toolchain/environment:
- Passed/failed/not run:
- Target build result:
- Hardware test result:

5. SCOPE CONTROL
- Có sửa file ngoài Allowed files không?
- Có logic trùng responsibility không?
- Có ảnh hưởng module khác không?

6. DOCUMENTATION
- Docs updated:
- TODO updated:
- Evidence reference:

7. HANDOFF
- Task tiếp theo:
- Prerequisites:
- Cần người dùng cung cấp gì:
- Branch/commit/checkpoint:
```

Không được báo DONE nếu chưa có evidence phù hợp.

## 6. PROMPT REVIEW CHỐNG TRÙNG VÀ CHỒNG CHÉO

```text
Bạn là reviewer độc lập. Không thêm feature, không refactor, không tự sửa code.

Review toàn bộ diff:
1. Có responsibility nào do hai module cùng sở hữu?
2. Có logic/hàm bị implement hai lần?
3. Enum/status/error code có trùng nghĩa/không nhất quán?
4. Header/API có bị nhiều task sửa theo hướng khác nhau?
5. Có include cycle hoặc vi phạm layer dependency?
6. Module có gọi trực tiếp hardware thay vì BSP/Driver?
7. Có default/threshold/calibration/model metadata không có nguồn?
8. Mock/test fixture có lọt vào production?
9. Serialization format có lệch giữa logger/storage/dataset?
10. Có phá backward compatibility persisted config/data?
11. Có retry/blocking/allocation không giới hạn?
12. Có báo success dù thao tác partial/failure?
13. Docs mô tả chức năng chưa implement?
14. TODO đánh Done thiếu evidence?

Trả về Critical/Major/Minor, file/line, owner duy nhất cho mỗi responsibility,
và đề xuất patch. Không tự sửa trong lần review này.
```

## 7. PROMPT KHI TASK BỊ BLOCKED

```text
Task bị BLOCKED một phần. Không đoán dữ liệu còn thiếu.
Chia thành:
A. Có thể implement an toàn ngay.
B. Chỉ có thể tạo interface/test scaffold.
C. Bắt buộc chờ hardware/model/approval/data.

Chỉ implement A và scaffold tối thiểu B. Không tạo production defaults,
không đổi API module khác, không dùng mock làm production. Viết test chứng minh
fail-closed. Cập nhật TODO và liệt kê chính xác đầu vào để mở khóa C.
```

## 8. Đầu vào cần cung cấp để mở khóa các task

| Task | Đầu vào |
|---|---|
| Calibration physical conversion | Datasheet, sensor identity, calibration procedure, coefficients, units, valid range |
| SchemaRegistry production | Released FSS + actual Edge Impulse export metadata |
| Binary serialization | Approved wire schema, endianness, numeric representation, version/CRC decision |
| Storage hardware | Media, interface, wiring, filesystem, capacity/retention |
| Output hardware | Alarm BOM, schematic, pin map, polarity, safe electrical state, feedback |
| AppController full startup | Stable module APIs + approved runtime/task config |
| Edge Impulse | Actual deployment export, model hash/version, FSS, golden vectors |
| Watchdog | Exact framework version, task list, measured timing, deadline policy |
| Risk policy | Regression target meaning, validation data, approved thresholds/escalation |

## 9. Nguyên tắc cuối cùng

- Mỗi prompt là một work item với owner và phạm vi rõ ràng.
- Một file chỉ nên do một agent sửa tại một thời điểm.
- Contract phải được review trước khi module phụ thuộc implement.
- Không giao “hoàn thiện toàn bộ project” trong một prompt.
- Không merge khi chưa review diff/test evidence.
- Host tests không chứng minh hardware validation.
- Model build pass không chứng minh risk model đã được kiểm chứng.
- Không triển khai cảnh báo thực địa khi chưa hoàn tất validation, FMEA và safety review.
