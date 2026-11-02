# Release Readiness Checklist

**Document version:** 1.0  
**Updated:** 2026-11-02  
**Current release disposition:** **NOT READY / BLOCKED**

Every item requires linked evidence and reviewer sign-off. A passing host
test is not evidence of target inference, hardware safety, field performance,
or durability. Do not mark an item complete from source presence alone.

## Repository and requirements

- [ ] Controlled DOP, DSD, released FSS, ICD, LLD, TCS, and GTIS versions are
      available, reviewed, and trace-linked to this build.
- [ ] All approved requirement deviations/change requests are closed or
      explicitly accepted by the authorized owner.
- [ ] `docs/REQUIREMENT_TRACEABILITY.md` is updated for the release revision.
- [ ] `PROJECT_TREE.txt` and document version/date/change history match the
      release contents.
- [ ] No TBD has been replaced with a value without a cited approved source.

## Build and tests

- [ ] Clean reproducible PlatformIO ESP32-S3 target build; record toolchain,
      framework, board ID, command, build hash, and complete output.
- [ ] Native unit and integration test suites pass from a clean build.
- [ ] Target test plan and hardware-in-loop results are complete.
- [ ] Fault injection covers the approved startup, storage, sensor, model,
      output, recovery, task, and watchdog failure cases.
- [ ] Test report includes expected/actual results, test IDs, environment,
      artifact/build hash, reviewer, and unresolved failures.

Current evidence only: on 2026-11-02, native unit tests passed 71/71 and native
integration tests passed 3/3. No target build or hardware test evidence is
recorded; therefore these checklist items remain open.

## Configuration, sensor and model gates

- [ ] Production manifest schema/version and approval provenance are frozen.
- [ ] A concrete ConfigManager persistence adapter is reviewed; read-back,
      commit marker, rollback, corruption handling, and power-loss behavior are
      tested on the selected medium.
- [ ] Board variant, schematic/BOM, pin map, electrical levels, bus settings,
      sensor identity, and wiring are approved and verified on target.
- [ ] Sensor calibration artifact, units, validity/freshness policy, and
      recalibration procedure are approved and tested with traceable references.
- [ ] Released FSS/schema is matched to the actual Edge Impulse deployment
      export; model version/hash, dimensions, order, sampling/window,
      preprocessing, and output semantics are recorded.
- [ ] Target inference build uses the approved generated SDK/export and passes
      metadata compatibility and golden-vector tests.
- [ ] Target RAM, flash, stack/heap high-water, latency, throughput, and
      thermal/power behavior are measured against approved budgets.

## Runtime, storage, outputs and recovery

- [ ] AppController has a production adapter for every phase and is used by
      firmware startup; each adapter reports actual module status.
- [ ] Task startup inference gate is enforced by runtime tasks; bounded
      stop/drain semantics exist and are tested.
- [ ] Storage backend supplies reviewed finite-operation semantics, media
      error behavior, flush/close contract, and target power-loss evidence.
- [ ] Physical safe-output state, pin/polarity/electrical design, feedback
      circuit and end-to-end output behavior are approved and verified.
- [ ] Recovery actions, retry limits/backoff, escalation, and watchdog feed/
      reset policy are approved and verified on target.
- [ ] Health thresholds, queue capacities, task periods/priorities/stacks,
      timeouts, and memory budgets are derived from reviewed measurements.

## Deployment and operations

- [ ] Release artifacts are versioned and hashed; firmware, config, model,
      FSS/schema, calibration, and dependency versions are mutually traceable.
- [ ] Installation, rollback, field validation, maintenance, operator
      instructions, and incident/recovery procedures are approved.
- [ ] Safety/FMEA review is complete and all critical hazards have verified
      mitigations and owners.
- [ ] Release authority signs the final readiness record.

## Change history

| Version | Date | Change |
|---|---|---|
| 1.0 | 2026-11-02 | Initial evidence-based release gate checklist. |
