# Requirement-to-Evidence Traceability

**Document version:** 1.0  
**Updated:** 2026-11-02  
**Status:** Repository-level traceability snapshot; not a controlled system
requirements baseline.

## Authority and scope

The repository does not contain controlled DOP, DSD, released FSS, ICD, LLD,
TCS, or GTIS documents. The task prompts and existing implementation guides
are not substitutes for those approved requirements. Therefore this matrix
traces implementation intent and verifiable repository evidence only; it
cannot assert system-level compliance.

Status meanings: **Implemented** means source behavior exists; **Host-tested**
means the named tests exercise that behavior in the native environment;
**Partial** means only a subset is implemented; **Mock/test-only** means the
evidence is fixture-based; **Blocked** means a required artifact/adapter/
approval is absent; **Not configured** means no verified operational value is
provided. None of these labels implies target or safety acceptance.

## Traceability matrix

| Requirement / intent | Module(s) | Test(s) / evidence | Status and remaining gate |
|---|---|---|---|
| Deterministic startup order; inference held until all gates pass | `Managers/AppController` | `testAppControllerStartsPhasesInOrderAndGatesInference`; `testAppControllerFailsClosedWhenInferenceGateCannotOpen` in `test/test_system_contracts/test_system_contracts.cpp` | **Host-tested protocol only.** No concrete production lifecycle adapter; task-start gate enforcement not wired. |
| Startup failure rolls back in reverse order | `Managers/AppController` | `testAppControllerCleansUpEveryStartupFailureInReverse` | **Host-tested protocol only.** Partial module initialization behavior depends on adapter contracts. |
| Shutdown stops work, drains tasks, flushes, then requests safe output | `Managers/AppController` | `testAppControllerShutdownOrderAndRepeatedCalls`; `testAppControllerFlushFailureStillMakesOutputsSafe` | **Partial.** Interface order is tested; `TaskFramework` has no stop/drain API, `StorageManager::flush()` does not accept a deadline, and physical safe outputs are not verified. |
| Health checks are bounded and failure closes runtime | `Managers/AppController` | `testAppControllerHealthChecksAreBoundedAndFailClosed`; `testAppControllerRejectsUnconfiguredTimeouts` | **Host-tested port contract.** The adapter must enforce deadlines; synchronous calls are not preempted by AppController. |
| Manifest validation and two-slot candidate commit/fallback | `Managers/ConfigManager` | `testConfigManagerAcceptsValidTestOnlyManifestWithoutArtifacts`; `testConfigManagerRejectsModelFssMismatch`; `testConfigManagerCandidateFaultsPreservePreviousCommittedRecord`; `testConfigManagerRejectsInvalidCandidateWithoutWriting`; `testConfigManagerCommitsOnlyAfterValidReadBack`; `testConfigManagerFallsBackWhenNewestRecordIsCorrupt` | **Host-tested protocol.** Adapter owns explicit persistence encoding and backing bytes. No production adapter is supplied. |
| Production configuration requires approval and model/FSS compatibility | `Managers/ConfigManager`, `AI/SchemaRegistry` | `testConfigManagerKeepsProductionActivationFailClosed`; SchemaRegistry test group in `test/test_system_contracts/test_system_contracts.cpp` | **Partial / blocked.** Synthetic descriptors test validation. Production approval, released schema, persistent artifact ownership, and startup wiring are absent. |
| Calibration preserves validity and rejects unsupported values without fabricating valid zero | `Services/CalibrationService` | `testCalibrationServicePreservesRawInputAndRemainsUncalibrated`; `testCalibrationServicePropagatesInvalidAndPartialChannels`; `testCalibrationServiceRejectsNonFiniteAndDoesNotMakeValidZero`; `testCalibrationServiceRejectsUnmatchedCalibrationVersion`; `testCalibrationServiceRejectsMissingFrameSchemaVersion` | **Partial, host-tested.** No calibration source/approved coefficients; output is not calibrated production data. |
| Data quality separates valid/stale/missing/range/nonfinite states | `Services/DataQualityService` | DataQuality test group in `test/test_system_contracts/test_system_contracts.cpp` | **Implemented/host-tested with explicit test configuration.** Production ranges, required channels, and freshness limits are not configured. |
| Feature extraction validates metadata and produces fixed-capacity windows | `Services/FeaturePipeline` | `testFeaturePipelineBuildsApprovedWindow`; `testFeaturePipelineFailsClosedForTbdRawFeature` | **Partial, host-tested.** Test fixture only; released FSS and production input contract absent. |
| Model/schema mismatch inhibits inference | `AI/SchemaRegistry`, `Managers/RecoveryManager`, `AI/EdgeImpulseAdapter` | `testSchemaRegistryReportsVersionHashAndCountMismatches`; `testRecoveryManagerInhibitsInferenceForSchemaAndModelMismatch` | **Validation logic host-tested.** Actual model export and production schema absent; EdgeImpulseAdapter is not integrated. |
| Pure risk classification honors provenance, quality, health, and configured thresholds | `Managers/RiskManager` | RiskManager test group in `test/test_system_contracts/test_system_contracts.cpp` | **Implemented/host-tested using synthetic approved-looking fixtures.** This does not approve thresholds or risk semantics. |
| Risk-to-output policy has no GPIO semantics and unknown is not normal | `Managers/OutputController` | `testOutputPolicyMapsEveryRiskStateWithoutHardwareSemantics`; `testOutputPolicyRejectsInvalidAssessmentsAndRiskEnums`; `testOutputPolicyPreservesAbsentAndUnverifiableFeedback`; `testOutputPolicyRepeatedInputProducesIdenticalCommand` | **Pure policy host-tested.** No physical output driver, pin/polarity approval, or feedback circuit evidence. |
| Fault classification is pure; retries require injected bounded policy | `Managers/RecoveryManager` | `testRecoveryManagerClassifiesKnownAndUnknownCatalogValues`; `testRecoveryManagerBoundsRetryByInjectedPolicy`; `testRecoveryManagerInhibitsInferenceForSchemaAndModelMismatch` | **Host-tested decision logic.** No I2C recovery, restart, or other hardware recovery is implemented. |
| Serialization has explicit versioned field-level encoding and strict bounds | `Models/Serialization` | `testEventRecordJsonLinesRoundTripsEveryMember`; rejection/boundary tests in `test/test_system_contracts/test_system_contracts.cpp` | **Partial, host-tested.** EventRecord JSONL v2 only. Other record codecs and CSV/binary serializer operations remain unsupported. |
| Storage lifecycle propagates fake backend failures without claiming durability | `Storage/StorageManager` | StorageManager test group in `test/test_system_contracts/test_system_contracts.cpp` | **Host-tested lifecycle.** `IStorage` is bool-only and lacks finite timeout/close/byte-count contract; no physical backend or durability evidence. |
| Sensor acquisition ordering and failure state are explicit | `Managers/SensorManager` | SensorManager test group in unit suite; sensor-to-feature integration test | **Implemented/host-tested with fakes.** No target sensor/electrical validation. |
| Inference metadata/input/output validation; mock stays test-only and raw output stays separate from risk | `AI/IInferenceEngine`, `AI/MockInferenceEngine`, `AI/EdgeImpulseAdapter`, `Tasks` | `testMockInferenceEngineReturnsConfiguredRawOutputOnly`; `testMockInferenceEngineRejectsSchemaAndInvalidOutputConfiguration`; `testEdgeImpulseAdapterExposesMetadataAndFailsClosedWithoutExport`; `testMockRuntimeFailurePropagatesUnknownToLogger` in `test/test_system_flow/test_system_flow.cpp` | **Mock/test-only.** Configured values are not model evidence; no generated export or hardware behavior is proven. |
| Evaluation reports quality, feature distributions, provenance, eligible labels and gated metrics | `Dataset/EvaluationRunner`, `Dataset/AnnotationManager` | `testEvaluationRunnerReportsFeatureAndApprovedRegressionMetrics`; `testEvaluationRunnerDisablesUndefinedTargetsAndSeparatesGt5Proxy`; `testEvaluationRunnerCountsMissingRejectedAndJoinFailures` in `test/test_system_contracts/test_system_contracts.cpp` | **Host-only.** Regression metrics require explicit target/output approval; synthetic proxy metrics are separate; row-level residual export remains deferred. |
| Actual offline Edge Impulse inference matches approved FSS/model metadata | `AI/EdgeImpulseAdapter` | No actual-export integration or target evidence | **Blocked.** Deployment export and released FSS absent. |
| Target memory/latency/timing and hardware startup/shutdown are acceptable | BSP, Tasks, Storage, Drivers, AppController | No current target evidence | **Blocked / not measured.** Native tests are not target measurements. |

## Evidence runs (2026-11-02)

From the project directory, with `C:\msys64\ucrt64\bin` added to `PATH`:

| Command | Result |
|---|---|
| `pio test -e native-unit-tests` | **PASS — 71 test cases, 0 failed.** |
| `pio test -e native-integration-tests` | **PASS — 3 test cases, 0 failed.** |
| Strict standalone C++17 compile of `AppController.cpp` | **PASS** with `-Wall -Wextra -Wpedantic -Werror`. |
| ESP32-S3 target build, hardware-in-loop, deployment model inference | **Not run / no evidence.** |

The first-party test source is itself evidence of what is exercised; the pass
counts above are the current recorded command results. No target build hash,
hardware serial capture, or model golden-vector evidence is available.

## Change requests / requirement-source gaps

1. Provide controlled DOP/DSD/FSS/ICD/LLD/TCS/GTIS versions and approval status
   so each implementation row can be traced to an authoritative requirement.
2. Resolve and assign the `TaskFramework` owner a bounded stop/drain contract.
3. Resolve and assign the storage owner a flush deadline/timeout and backend
   completion contract; do not infer bounded durability from `bool flush()`.
4. Provide a ConfigManager persistence adapter with explicit field encoding
   and persistent artifact/config ownership for production startup.
5. Provide released Edge Impulse deployment + FSS metadata and target
   validation requirements before enabling model inference.
6. Approve physical safe-output definition and feedback evidence before
   claiming output actuation or shutdown safety.

These are requests for requirements/integration decisions; no system
requirement has been changed by this audit.

## Change history

| Version | Date | Change |
|---|---|---|
| 1.0 | 2026-11-02 | Initial repository-level requirement-to-module-to-test/evidence matrix. |
