# SensorManager

## Read order

`readSnapshot()` performs synchronous calls in a fixed order:

1. MPU9250 / IMU
2. Soil moisture
3. Rain sensor
4. HX710B

Each sensor is retried independently. A failed/excessively slow sensor does not
prevent later sensors from being read. The resulting `SensorFrame` contains
each sensor's status, copies values only from valid reads, and is still
published if one or more channels fault. `readSnapshot()` returns `true` when
it produced a frame, not when every sensor succeeded. The frame's timestamp
denotes the scheduled cycle start; per-sensor status timestamps are filled with
that same cycle timestamp.

## Timeout and retry policy

`SensorManagerConfig::readPolicies` has one `SensorReadPolicy` per sensor slot.
Each policy requires a nonzero `beginTimeoutMicros` and `timeoutMicros`, sets
`maxRetries` as retries after the first attempt, and optionally selects a
`retryBackoffMicros`. The period, per-device WCET/timeouts, retry limits and
backoff are integration inputs and must be measured/approved; `beginAll()`
rejects unset timeout values. Startup and reads use the same bounded retry
count/backoff, but startup and read have separate timeout budgets because
driver initialization (for example calibration capture) may take longer.

The manager measures every synchronous call with `ISensorManagerClock`. A call
whose elapsed time exceeds its budget is rejected, its output discarded, and
retried within the configured retry count. A failed final attempt leaves that
sensor invalid and sets its health to `Fault`; other sensors still proceed.
Saturating per-slot diagnostics expose begin/read attempts, retries, timeouts,
failures, clock anomalies and last measured duration.

**Timeout enforcement boundary:** C++ cannot safely abort an arbitrary
synchronous virtual call from the caller. The manager detects a late return and
rejects its result; it cannot preempt a driver that never returns. Therefore
every concrete driver's I/O must itself have a finite timeout/WCET (I2C timeout,
HX710B ready/conversion timeout, etc.) shorter than the manager budget. This is
necessary to bound the complete cycle; do not treat the manager's elapsed-time
check as a substitute for driver-level timeouts.

`PlatformSensorManagerClock` uses ESP monotonic microseconds on the ESP32
Arduino target and `steady_clock` in the host mock build. Tests may inject an
`ISensorManagerClock` to control elapsed time and retry backoff deterministically.
`SensorManager` is single-owner task state and must only be called by
`TaskSensor`; it contains no internal mutex.

## Composition example

Production configuration must provide reviewed values rather than using the
mock demo's illustrative settings:

```cpp
project::managers::SensorManagerConfig config{};
config.bootId = bootId;
config.samplePeriodMicros = approvedSamplePeriodMicros;
for (auto& policy : config.readPolicies) {
    policy.beginTimeoutMicros = approvedBeginTimeoutMicros;
    policy.timeoutMicros = approvedReadTimeoutMicros;
    policy.maxRetries = approvedRetryCount;
    policy.retryBackoffMicros = approvedRetryBackoffMicros;
}

project::managers::PlatformSensorManagerClock clock;
project::managers::SensorManager manager(
    imu, soil, rain, hx710b, config, clock);
```
