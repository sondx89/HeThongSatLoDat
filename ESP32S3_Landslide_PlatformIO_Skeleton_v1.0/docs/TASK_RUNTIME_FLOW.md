# Task Framework Runtime Flow

Priority, stack words, cycle periods, queue wait times and heartbeat timeouts are
runtime configuration inputs. They default to unconfigured values and must be
selected from target measurements before `TaskFramework::startAll()` can succeed.
The framework creates tasks behind a startup gate, so no task runs until every
task creation succeeds. `startAll()` is a task-context operation and suspends the
scheduler briefly while broadcasting the release notifications.

```mermaid
sequenceDiagram
    autonumber
    participant App as Application startup
    participant TF as TaskFramework
    participant TS as TaskSensor
    participant QS as Q_SENSOR
    participant TP as TaskFeature
    participant QF as Q_FEATURE
    participant TI as TaskInference
    participant QM as Q_MODEL
    participant TR as TaskRisk
    participant RC as Risk context provider
    participant QR as Q_RISK
    participant TL as TaskLogger
    participant QE as Q_EVENT
    participant QL as Q_LOG
    participant EO as Event output sinks
    participant TD as TaskDiagnostics
    participant TW as TaskWatchdog
    participant HM as HealthMonitor
    participant HP as Health source/publisher
    participant W as WatchdogSupervisor

    App->>TF: startAll() after QueueRegistry initialization
    TF->>TF: validate all task configurations
    TF->>TF: create all configured tasks at entry gates
    TF-->>TS: release after all creates succeed
    TF-->>TP: release
    TF-->>TI: release
    TF-->>TR: release
    TF-->>TL: release
    TF-->>TD: release
    TF-->>TW: release

    loop configured sensor cycle
        TS->>TS: poll sensors and read snapshot
        TS->>QS: bounded send SensorFrame
        Note over TS,QS: acquisition progress heartbeat; queue drops counted
    end
    loop sensor messages
        TP->>QS: bounded receive SensorFrame
        TP->>TP: validate, calibrate, update feature window
        opt rejected sensor/calibration data
            TP->>QE: event reporter enqueues approved validation event
        end
        opt valid complete window
            TP->>QF: bounded send FeatureVector
        end
    end
    loop feature messages
        TI->>QF: bounded receive FeatureVector
        TI->>TI: validate schema and model readiness
        TI->>QM: bounded send ModelOutput, including explicit failures
    end
    loop model results
        TR->>QM: bounded receive ModelOutput
        TR->>RC: obtain current frame/health context
        TR->>TR: evaluate AI output and sensor rules
        opt risk state transition
            TR->>QE: event reporter enqueues approved transition event
        end
        TR->>QR: bounded send RiskAssessment
    end
    loop logger service
        TL->>QE: poll events first
        opt no event available
            TL->>QL: poll tagged LogRecord
        end
        TL->>TL: write/flush using single-writer sink
        opt event received
            TL->>EO: format fixed-buffer CSV; persist with T-LOG
            TL->>TD: forward CSV diagnostic line to UART owner
        end
    end
    loop configured diagnostics interval
        TD->>TD: snapshot progress heartbeat and stack high-water
        TD->>TD: collect six queue depth/high-water/drop statistics
        TD->>HM: collect ESP32-S3 free/minimum heap and reset/uptime
        HM-->>TD: HealthSnapshot system metrics + watchdog count
        TD->>HP: publish complete HealthSnapshot
    end
    loop configured watchdog check interval
        TW->>W: begin(bootId) after five required tasks register
        W->>W: inspect heartbeat age / exited state
        alt all required tasks progressing
            W-->>TW: Healthy
        else timeout or task exit
            W->>W: latch fault; invoke system fault sink
            W->>QE: enqueue EVT-SYS-0005 TASK_HEARTBEAT_MISSED
            W->>QE: enqueue EVT-SYS-0004 WATCHDOG_TIMEOUT
            W->>W: BSP restart (ESP32-S3 esp_restart)
        end
    end
```

`Q_RISK` is published for the output/telemetry consumer, which is outside the six
requested tasks. Risk context acquisition, event catalog/deduplication, log
encoding/storage policy and system health collection are injectable ports because
their complete contracts remain unresolved in the system documents. Heartbeat
advances only after meaningful task work; queue timeout/idle does not count as
progress. A stale or exited
essential task degrades the emitted health snapshot. Watchdog feeding is outside
this framework and must remain conditional on the approved supervisor policy.

EventLogger producers copy records to `Q_EVENT`; only TaskLogger consumes them.
The CSV serializer writes field-by-field into caller-owned bounded storage, and its
same escaped row is offered to the CSV sink and the T-DIAG-owned Serial route.
Neither producer tasks nor TaskLogger may write directly to the UART. Concrete
storage and UART adapters remain integration points until the board/storage
contracts are finalized.

HealthMonitor implements `ITaskHealthSource` and `IWatchdogFaultSink`. Reuse the
same instance for TaskDiagnostics' system-metric source and WatchdogSupervisor's
fault sink so each distinct watchdog task fault contributes to
`watchdogCount`. Stack watermark values are FreeRTOS stack words (not bytes).

## Scheduling configuration by task

| Task | Scheduling input | Queue behavior | Stack / priority |
|---|---|---|---|
| `TaskSensor` | `cycleTicks` | Nonblocking `Q_SENSOR` send; overflow counted | Required, TBD |
| `TaskFeature` | Bounded `queueWaitTicks` (or idle wait) | `Q_SENSOR` receive, `Q_FEATURE` send | Required, TBD |
| `TaskInference` | Bounded `queueWaitTicks` (or idle wait) | `Q_FEATURE` receive, `Q_MODEL` send | Required, TBD |
| `TaskRisk` | Bounded `queueWaitTicks` (or idle wait) | `Q_MODEL` receive, `Q_RISK` send | Required, TBD |
| `TaskLogger` | `idleWaitTicks`; queue wait must be zero | Polls `Q_EVENT` and `Q_LOG`, event queue first/alternating | Required, TBD |
| `TaskDiagnostics` | `cycleTicks` | Reads queue/heartbeat snapshots; publishes health | Required, TBD |
| `TaskWatchdog` | `cycleTicks` | Checks progress for Sensor, Feature, Inference, Risk, Logger | Required, TBD |

`heartbeatTimeoutTicks` is required for every task. The proposal's 1 Hz sensor
aggregate cadence and 1–10 second diagnostics cadence are not compiled defaults;
convert only approved, measured periods to FreeRTOS ticks during application
composition.

On timeout, the supervisor latches the affected task bit and invokes the
application fault sink, then enqueues the catalogued heartbeat-missed and
watchdog-timeout events before requesting BSP restart. Event enqueue is
nonblocking and may fail if Q_EVENT is full; fault/reset still proceed, and the
failure is exposed through `failureFlags()`. A queued event is not guaranteed to
reach persistent storage before an immediate reset. Hardware watchdog
initialization/feed remains a BSP TODO pending framework/timeout approval; this
application supervisor directly calls `esp_restart()` on the confirmed ESP32-S3
target when it detects a monitored task timeout.
