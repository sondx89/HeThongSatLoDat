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
    participant TD as TaskDiagnostics
    participant HP as Health source/publisher

    App->>TF: startAll() after QueueRegistry initialization
    TF->>TF: validate all task configurations
    TF->>TF: create six tasks at entry gates
    TF-->>TS: release after all creates succeed
    TF-->>TP: release
    TF-->>TI: release
    TF-->>TR: release
    TF-->>TL: release
    TF-->>TD: release

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
    end
    loop configured diagnostics interval
        TD->>TD: snapshot progress heartbeat and stack high-water
        TD->>TD: collect six queue depth/high-water/drop statistics
        TD->>HP: collect system metrics and publish HealthSnapshot
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

## Scheduling configuration by task

| Task | Scheduling input | Queue behavior | Stack / priority |
|---|---|---|---|
| `TaskSensor` | `cycleTicks` | Nonblocking `Q_SENSOR` send; overflow counted | Required, TBD |
| `TaskFeature` | Bounded `queueWaitTicks` (or idle wait) | `Q_SENSOR` receive, `Q_FEATURE` send | Required, TBD |
| `TaskInference` | Bounded `queueWaitTicks` (or idle wait) | `Q_FEATURE` receive, `Q_MODEL` send | Required, TBD |
| `TaskRisk` | Bounded `queueWaitTicks` (or idle wait) | `Q_MODEL` receive, `Q_RISK` send | Required, TBD |
| `TaskLogger` | `idleWaitTicks`; queue wait must be zero | Polls `Q_EVENT` and `Q_LOG`, event queue first/alternating | Required, TBD |
| `TaskDiagnostics` | `cycleTicks` | Reads queue/heartbeat snapshots; publishes health | Required, TBD |

`heartbeatTimeoutTicks` is required for every task. The proposal's 1 Hz sensor
aggregate cadence and 1–10 second diagnostics cadence are not compiled defaults;
convert only approved, measured periods to FreeRTOS ticks during application
composition.
