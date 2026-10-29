# Serial diagnostics CLI

`project/Diagnostics/SerialDiagnostics.h` provides a bounded command parser
and response handler for the read-only commands:

```text
diag status
diag health
diag sensor
diag memory
diag risk
```

`diag sensors` is accepted as an alias for `diag sensor`. Commands are
case-sensitive, one per LF/CRLF-terminated line, with a 128-byte maximum
excluding the line ending. Extra arguments, malformed input, unknown commands,
and overlong lines return an error response. An overlong line is discarded
through its newline so the next command starts at a clean boundary.

## Wire format

Responses are bounded JSON Lines with CRLF endings. Every response includes
protocol version, request ID, number of rate-limited responses suppressed
since the previous response, status, and numeric error code. Missing values
are emitted as JSON `null`, never as a made-up zero. Example:

```json
{"protocol_version":1,"request_id":1,"suppressed_before":0,"status":"ok","error_code":0,"data":{"firmware_version_code":null,"boot_id":42,"uptime_ms":1200,"mode":"diagnostic","risk_state":"unknown","health_state":"unknown"}}
```

Error codes are stable within protocol version 1: `1` malformed command,
`2` unknown command, `3` command too long, `4` snapshot unavailable,
`5` invalid snapshot, and `6` response too large. Rate-limited requests are
counted rather than answered individually.

The five commands expose:

- `status`: firmware version code, boot ID, uptime, runtime mode, risk and
  health states. Unavailable firmware version codes are `null`.
- `health`: health state, fault flags, watchdog/inference/sensor/queue/deadline
  counters, and queue depth/capacity/drop counts.
- `sensor`: per-sensor health/validity/error/quality/timestamp/age and raw
  readings only when the corresponding sensor status is valid, plus read and
  out-of-range counts when available. IMU raw values are returned for all six
  axes. Sensor age is `null` until a health uptime snapshot is available.
- `memory`: free/minimum heap, largest block, internal heap, and task stack
  high-water marks. These are `available:false` unless the provider marks the
  memory snapshot available.
- `risk`: assessment and risk/health states, model status, score only when
  valid and finite, reason code, active fault flags, and risk config version.

The response contains bounded diagnostics only; it does not dump memory,
credentials, or arbitrary model buffers.

## Integration and ownership

The CLI has no direct Arduino/FreeRTOS dependency. The UART owner implements
`IDiagnosticsSerialPort` around `Serial` and implements
`IDiagnosticsSnapshotProvider` to copy a consistent set of latest published
snapshots. Only the UART-owner task should call `poll()` or write to that port;
other tasks publish snapshots and never print directly. `readSnapshot()` must
be bounded and must not perform sensor I/O or block on a queue.
The port's `write()` must also be bounded and accept the complete response or
return failure; use a bounded TX buffer if the underlying serial write can
block or partially enqueue.

Configure a nonzero, reviewed `minimumResponseIntervalMs`; there is no
unreviewed default rate. Each `poll()` consumes at most 64 available bytes and
does not wait for input. This bounds parsing work, while the configured
response interval limits output during command floods. A suppressed request
is counted and reported in `suppressed_before` on the next transmitted reply.

Example composition (the adapters are application-specific):

```cpp
project::diagnostics::SerialDiagnosticsConfig cliConfig{};
cliConfig.minimumResponseIntervalMs = reviewedCliResponseIntervalMs;
cliConfig.responseRateLimitConfigured = true;

ArduinoSerialPort serialPort(Serial);  // Implements IDiagnosticsSerialPort.
ApplicationSnapshotProvider snapshotProvider;  // Implements snapshot copy.
project::diagnostics::SerialDiagnostics cli(
    serialPort, snapshotProvider, cliConfig);

void setup() {
    Serial.begin(115200);  // Must match the board's approved configuration.
    const auto result = cli.begin();
    if (result != project::diagnostics::SerialDiagnosticsStatus::Ok) {
        // Report/handle invalid CLI configuration.
    }
}

void loop() {
    (void)cli.poll();  // Call only from the UART-owner context.
    serviceOtherBoundedWork();
}
```

The exact baud rate remains a board configuration decision. This interface is
read-only; reset, configuration changes, arbitrary commands, and model
management are intentionally not implemented.
