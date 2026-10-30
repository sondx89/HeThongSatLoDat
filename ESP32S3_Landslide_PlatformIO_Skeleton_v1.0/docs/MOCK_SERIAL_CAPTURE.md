# Mock firmware build, upload, and serial capture

From Windows, double-click `build_project.bat` or run it from a terminal. The
default `all` action:

1. Runs native Unity unit and integration tests.
2. Builds and uploads the synthetic `esp32-s3-mock` firmware over USB.
3. Opens the 115200-baud serial monitor.

Stop the monitor with Ctrl+C. PlatformIO auto-detects a single serial port. If
multiple ports are present, set `PIO_UPLOAD_PORT=COMx` and/or
`PIO_MONITOR_PORT=COMx` in the environment before launching the batch file.

Supported actions are `tests`, `build`, `upload`, and `production-build`; run
`build_project.bat help` for descriptions. `production-build` compiles the
non-mock, fail-closed target and does not upload it. The production application
is not deployable until its approved runtime configuration and task composition
are supplied.

## Synthetic serial CSV

The mock firmware streams a distinct, diagnostic-only raw CSV:

```csv
record_type,sequence_no,timestamp_ms,accel_x_raw,accel_y_raw,accel_z_raw,soil_raw,rain_raw,hx710b_raw,imu_valid,soil_valid,rain_valid,hx710b_valid,dataset_eligible,eligibility_reason
MOCK_RAW_V1,1,1000,120,-120,15920,1600,0,4988000,true,true,true,true,false,SYNTHETIC_MOCK
```

Unavailable sensor values are empty, with the corresponding validity field
false; they are not replaced with zero. `dataset_eligible` is always `false`
and `eligibility_reason` is `SYNTHETIC_MOCK`.
This stream is synthetic diagnostics, not the DSD raw dataset schema and not a
valid Edge Impulse training upload. Do not feed it to a model or use it as
field data.

The native integration test checks the mock CSV header, row, and missing-value
behavior. It separately exercises the sensor-to-feature-to-test-model-to-risk
path and the existing six-column CSV logger using in-memory fakes.

## Edge Impulse and CSV boundaries

`EdgeImpulseAdapter` is the on-device inference boundary for an approved
generated Edge Impulse deployment. It does not upload data to Edge Impulse
Studio. No deployment artifact or approved FSS input contract is present, so
the adapter remains fail-closed.

The runtime logger retains its existing
`timestamp,tilt,soil,rain,stress,risk` contract. It is not the DSD raw dataset
format and lacks dataset provenance and per-channel quality fields. The mock
serial CSV is a third, separate diagnostic format. Keep these schemas separate
until the dataset owner approves the raw schema, labels, eligibility policy,
and mapping to a concrete Edge Impulse project.
