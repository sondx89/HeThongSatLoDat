# Integration tests: Sensor → Feature → Model → Risk → Logger

Run the native integration automation from this project directory:

```text
pio test -e native-integration-tests
```

The test binary runs the production `SensorManager`, `DataQualityService`,
`FeaturePipeline`, `RiskManager`, and `CsvLogger` implementations in-process.
It substitutes only hardware clock/sensors, filesystem storage, and model
runtime. No ESP32 board, SD card, or deployed Edge Impulse model is needed.

## Test cases and expected results

| ID | Trace / setup | Expected result |
|---|---|---|
| IT-01 | Healthy fake sensor implementations → quality validation → one-sample approved test feature window → deterministic test-only model output `0.8` → approved test risk configuration → in-memory CSV filesystem | A valid two-value feature vector arrives at the model (`tilt_x_deg=0`, `accel_magnitude=9.81`); inference status is `Ok`; risk reaches `HIGH_RISK_TEST` under the fixture thresholds; logger writes the header and exactly `1000,0,55,25,0.8,HIGH_RISK_TEST\n`, with successful header/record flushes. |
| IT-02 | Same sensor/feature trace → real `MockModelRuntime` (no model export) → risk → logger | Valid input is handled with `ModelError`, no prediction is marked valid, risk is `UNKNOWN`, and CSV records `1000,0,55,25,,UNKNOWN\n` with model stress blank. No fallback score or SAFE result is synthesized. |

`0.8`, `9.81`, the schema/config hashes, window size, thresholds, soil/rain
test sensor values, selected CSV mapping, and risk-token mapping are test
fixtures to exercise wiring only.
They are not production approvals, a deployed model result, a target/risk
threshold, or a released FSS mapping. The integration currently logs the
pipeline's first test feature as `tilt`, soil, and relative rain wetness; these mappings only exercise the CSV
path and are not an approved production logging contract.
Replace these fixtures only with the versioned, reviewed manifests and
integration mapping once their source documents are finalized.

The generated Edge Impulse adapter is not used by these tests because no
generated model export/manifest is present. Add a separate hardware/model
qualification test after that exact deployment and output contract are
provided; do not infer success from the test-only scripted runtime.
