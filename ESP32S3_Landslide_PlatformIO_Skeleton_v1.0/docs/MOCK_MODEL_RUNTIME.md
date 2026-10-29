# MockModelRuntime

`project/AI/MockModelRuntime.h` is a fixed-storage implementation of
`IInferenceEngine` for running and exercising the inference task before an
Edge Impulse export is available. It has no Edge Impulse dependency, heap
allocation, or fabricated model parameters.

The target contract is still DRAFT and the model output dimension, output
meaning, range, and interpretation are not approved. Accordingly, a valid
feature vector produces an explicit `ModelOutput` with
`InferenceStatus::ModelError`, `predictionValid == false`, `outputCount == 0`,
and no raw outputs. It does not report a neutral/normal prediction or a
landslide probability. Returning `true` from `run()` means the mock handled the
call and populated the output; it does not mean inference succeeded.

`initialize()` succeeds so `TaskInference` can exercise its normal queue and
output path. Malformed feature vectors are returned as
`InferenceStatus::InvalidInput`. The runtime requires `valid`, a count within
`FeatureVector::kMaxFeatures`, an exact validity mask for those features, and
finite feature values. It does not replace `SchemaRegistry` or approve a
feature schema; the normal task remains responsible for schema validation.

## Example

```cpp
#include "project/AI/MockModelRuntime.h"

project::ai::MockModelRuntime runtime;
if (!runtime.initialize()) {
    // Handle initialization failure.
}

project::models::FeatureVector features{};
// In production, use the FeatureVector provided by the approved pipeline.
features.values[0] = 0.25F;
features.count = 1U;
features.featureValidMask = 1U;
features.valid = true;

project::models::ModelOutput output{};
if (!runtime.run(features, output)) {
    // Runtime was not initialized or could not process the call.
} else if (output.status ==
           project::models::InferenceStatus::ModelError) {
    // Expected for a valid input: no actual model is installed.
    // Keep the output explicitly unavailable; do not treat it as a prediction.
}
```

Replace the example feature with a valid vector from the feature pipeline;
the literal is only a small standalone interface example, not a sensor value,
feature definition, or model recommendation. `TaskInference` can be wired to
this implementation through its existing `IInferenceEngine&` dependency. Swap
in `EdgeImpulseAdapter` after the exported model and approved schema/output
contract are available.
