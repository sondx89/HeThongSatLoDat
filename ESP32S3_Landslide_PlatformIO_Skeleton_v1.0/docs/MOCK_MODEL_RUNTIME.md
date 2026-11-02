# Mock Inference Engine Contract

`project/AI/IInferenceEngine.h` defines the inference engine metadata, feature
input, and raw output contract. `IModelRuntime` remains a source-compatible
alias. Metadata carries the exact feature schema identity and dimensions; the
engine does not infer a model input shape from `FeatureVector` capacity.

`MockInferenceEngine` is available only when both
`PROJECT_HOST_DATA_TOOLS=1` and `PROJECT_TEST_ONLY=1` are explicitly defined.
It cannot be compiled into firmware, including the ESP32 mock firmware
environment. Its `InferenceEngineKind::TestOnly` metadata is not production
model metadata and cannot satisfy the Edge Impulse deployment gate.

The mock requires an explicit test configuration containing schema identity,
input dimension, expected output count, and finite raw output values. For a
valid input it deterministically copies those configured values into
`ModelOutput::rawOutputs`. It has no weights, hidden scenario mapping, target
range, output unit, probability interpretation, risk state, or automatic
`predictionValue`. Test code may inject an explicit non-success inference
status to cover failures. An unconfigured mock fails initialization.

Before returning configured outputs, the mock uses the shared input validator
to check validity, exact schema version/ID/hash, configured input dimension,
complete feature mask, finite values, and window identity. TaskInference
checks the engine metadata before startup and validates successful output
status, exact count, and finite values before publishing it. Raw outputs stay
separate from `RiskAssessment`; this mock does not produce a risk category.

The engine performs no dynamic allocation. Configuration-owned raw outputs
are fixed capacity and bounded by `ModelOutput::kMaxOutputs`, which is
storage capacity only. The caller-supplied output count is authoritative for
the test contract, not a released model dimension.

`EdgeImpulseAdapter` exposes the configured metadata but still fails
initialization without the actual generated export and verified deployment
metadata. No generated Edge Impulse code is added by this task.
