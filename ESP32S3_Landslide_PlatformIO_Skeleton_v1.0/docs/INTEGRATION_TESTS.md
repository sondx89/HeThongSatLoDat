# Integration tests: Host data replay and sensor/model wiring

Run the native integration automation from this project directory (on Windows,
make the installed MSYS2 UCRT64 compiler available on `PATH` if needed):

```text
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
pio test -e native-integration-tests
```

The Unity suite runs in-process with host fakes. It includes a DSD raw-core
export/validation/replay path and a separate legacy
sensor→feature→test-model→risk→operational-logger wiring test. No ESP32 board,
SD card, or deployed Edge Impulse model is needed.

## Test cases and expected results

| ID | Trace / setup | Expected result |
|---|---|---|
| IT-01 | Healthy fake sensor implementations → quality validation → one-sample approved test feature window → deterministic test-only model output `0.8` → approved test risk configuration → in-memory CSV filesystem | A valid two-value feature vector arrives at the model (`tilt_x_deg=0`, `accel_magnitude=9.81`); inference status is `Ok`; risk reaches `HIGH_RISK_TEST` under the fixture thresholds; logger writes the header and exactly `1000,0,55,25,0.8,HIGH_RISK_TEST\n`, with successful header/record flushes. |
| IT-02 | Same sensor/feature trace → explicitly configured test-only mock with injected `ModelError` → risk → logger | Valid input is handled with `ModelError`, no prediction is marked valid, risk is `UNKNOWN`, and CSV records `1000,0,55,25,,UNKNOWN\n` with model stress blank. No fallback score or SAFE result is synthesized. |
| IT-03 | Configured `MockInferenceEngine` raw output and invalid schema/output configurations | Exact schema mismatch is rejected; configured finite raw values are returned with a matching count and no implicit regression target or risk state. Nonfinite raw output and count mismatch fail validation. |
| IT-04 | `EdgeImpulseAdapter` without generated export | Metadata remains explicit, but initialization and inference fail closed without generated model artifacts. |
| IT-05 | Deterministic `SyntheticSensorGenerator` → data quality → `RAW_ONLY` calibration → bounded DSD v1.2 raw-core export → manifest/hash/count validation → parser replay | Exported identity/raw values and replay quality outcomes match the generated records; synthetic manifest provenance stays `SYNTHETIC`/`GT5`; missing/stale/sequence-gap/reboot/config-mismatch/malformed-row paths reject or report invalid input; feature windows remain ineligible without approved calibration, and production schema/inference/risk gates remain closed. Annotation interval identity is checked before evaluation summary export; target metrics remain disabled. |

`0.8`, `9.81`, the schema/config hashes, window size, thresholds, soil/rain
test sensor values, selected CSV mapping, and risk-token mapping are test
fixtures to exercise wiring only.
They are not production approvals, a deployed model result, a target/risk
threshold, or a released FSS mapping. The integration currently logs the
pipeline's first test feature as `tilt`, soil, and relative rain wetness; these mappings only exercise the CSV
path and are not an approved production logging contract.
The configured mock raw output is a test fixture, not a model result. Replace
these fixtures only with versioned, reviewed manifests and integration mapping
once their source documents are finalized.

The A14 path does not run mock inference on the invalid feature windows and is
not end-to-end model validation. The raw-core CSV does not store per-channel
acquisition timestamps; replay cannot recompute stale age without a separately
approved sidecar. The generated Edge Impulse adapter is tested only for
fail-closed behavior because no generated model export/manifest is present.
Add hardware/model qualification tests only after the exact deployment,
calibration, feature schema, and output contract are approved; do not infer
success from test-only fixtures.

Latest recorded result: `pio test -e native-integration-tests` — exit 0,
7 test cases passed. This is native test evidence only, not ESP32-S3 build,
HIL, model, field-accuracy, or safety evidence.
