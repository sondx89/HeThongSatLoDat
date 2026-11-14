# Edge Impulse deployment integration status

**Status:** BLOCKED — an actual owner-supplied deployment export and approved
model contract are not present in the repository.

No generated Edge Impulse inference header/export was found during this task.
The conditional `EdgeImpulseAdapter` remains fail-closed when
`PROJECT_EDGE_IMPULSE_MODEL_HEADER` is absent, and it requires verified model
hash, input/output dimensions, schema identity and output contract before
initialization. No dimensions, scaling, target meaning, class count, output
semantics or model hash have been invented.

The mock runtime is permitted only in explicit mock/test environments. It is
excluded from the production ESP32-S3 build by the production source filter and
included only by the explicit mock environment and host integration test.
Inference remains unavailable until a real export is reviewed against the
approved FSS/TCS and schema hash.

## Required owner inputs and verification

1. Supply the actual unmodified Edge Impulse deployment export and its source
   version/license metadata.
2. Supply approved feature schema/FSS/TCS, dimensions, order, units,
   preprocessing, window/stride, target semantics and expected output count.
3. Supply model hash and expected golden input/output fixtures from the owner.
4. Verify dimensions, finiteness, output type and schema/hash at host build.
5. Run golden replay on host, then compile and measure RAM/flash and inference
   latency on the exact ESP32-S3 board.

No model-specific golden replay or target memory/latency measurement can be
claimed until those inputs are supplied. Without the export or on any mismatch,
inference must remain disabled and risk must be UNKNOWN/DEGRADED. No certified
landslide-prediction claim is made.
