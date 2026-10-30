# EdgeImpulseAdapter

`project/AI/EdgeImpulseAdapter.h` implements `IModelRuntime` (also exposed
through the existing `IInferenceEngine` alias). It adapts the approved
`FeatureVector` to the Edge Impulse Arduino `signal_t` callback and calls
`run_classifier()`. Only this adapter includes the generated inference
header.

## Generated model integration

The repository does not contain an Edge Impulse deployment export. After the
project owner supplies the approved Arduino deployment, add its generated
files and SDK to the PlatformIO build, and define the generated header macro
for the firmware environment, for example:

```ini
build_flags =
    -std=gnu++17
    -I include
    -DPROJECT_EDGE_IMPULSE_MODEL_HEADER=\"landslide_inferencing.h\"
```

Replace `landslide_inferencing.h` with the exact generated header and add
only the include paths/libraries from that export. Do not fetch an arbitrary
Edge Impulse SDK version or invent model artifacts. The generated header must
expose the standard Arduino inference API:
`EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE`, `EI_CLASSIFIER_LABEL_COUNT`,
`signal_t`, `ei_impulse_result_t`, `EI_IMPULSE_ERROR`,
`EI_IMPULSE_OK`, and `run_classifier()`.

Without `PROJECT_EDGE_IMPULSE_MODEL_HEADER`, the adapter compiles without
Edge Impulse headers but `initialize()` returns false and `run()` reports
`ModelError`. This permits the rest of the firmware to build without
mistaking a stub for working inference.

This adapter is the on-device inference integration only; it does not upload
sensor samples or datasets to Edge Impulse Studio. The current mock firmware
streams synthetic raw diagnostic CSV over serial, marked
`dataset_eligible=false`. It is not a model-input or training-data contract.
Dataset ingestion remains blocked until the DSD raw schema, FSS/model input,
labels, and eligibility rules are approved.

## Configuration and validation

`EdgeImpulseModelConfig` has no operational defaults. Populate it only from a
verified deployment manifest:

- approved FSS feature schema version, ID and SHA-256;
- model input dimension and expected output count from generated metadata;
- model artifact SHA-256 and externally verified metadata/hash flags;
- approved output kind/contract and the approval flags.

The adapter checks the compiled input dimension against
`EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE`, the configured output count against
`EI_CLASSIFIER_LABEL_COUNT` (they must match exactly), and validates each input vector's validity,
count, schema ID/version/hash, complete feature-valid mask, nonzero boot ID,
window count, and finite values. It copies inputs through Edge Impulse's
callback without heap allocation. The adapter itself performs no dynamic
allocation; confirm the generated SDK configuration also uses static result
storage (the SDK can use a `std::vector` for dynamic result storage in some
configurations).

Classification results are copied to `ModelOutput::rawOutputs`; the adapter
does not select a winning class or reinterpret confidence as landslide risk.
Regression is accepted only with an approved one-output contract and is
copied into `predictionValue`. Neither the model hash nor the approval flags
are cryptographically verified by this adapter: their provenance must be
established by the application manifest/deployment validator. Output values
must be interpreted only under the approved target contract.

The adapter is single-caller/non-reentrant, matching the inference-task
ownership contract. Do not call `initialize()` or `run()` concurrently on the
same runtime. The `ModelOutput` schema version and any score-to-risk mapping
remain owned by their approved contracts; this adapter does not invent them.

## Example

```cpp
#include "project/AI/EdgeImpulseAdapter.h"

project::ai::EdgeImpulseModelConfig modelConfig{};
// Fill from the verified model/FSS manifest; do not use guessed dimensions,
// output semantics, or hashes.
project::ai::EdgeImpulseAdapter model(modelConfig);

const bool ready = model.initialize();
if (!ready) {
    // Keep inference unavailable and report a model/configuration fault.
}

project::models::FeatureVector input{};  // Supplied by the approved pipeline.
project::models::ModelOutput output{};
if (ready && model.run(input, output) &&
    output.status == project::models::InferenceStatus::Ok) {
    // Consume only according to the approved model output contract.
}
```

Initialize once during task startup and reuse the runtime instance.
