# Queue and Snapshot Contract

**Status:** Implemented host contract; production queue sizing and task composition remain unapproved.

`SensorFrame` is the complete by-value sensor snapshot. `SensorManager` is its
single producer: it assigns the configured boot ID, increasing sequence number,
monotonic timestamp and per-channel status before returning a frame. A zero raw
or physical value is never evidence of validity; consumers must honor channel
validity, status and freshness metadata.

`QueueRegistry` owns typed FreeRTOS queues for application lifetime. Its send
methods copy trivially-copyable payloads into bounded queues, and receive
methods copy a whole payload out. Tasks must not publish sensor fields through
shared mutable globals or retain queue handles. Queue depths and finite waits
are runtime configuration, not defaults; a caller must not use
`portMAX_DELAY`. A failed send is reported and counted, and queue depth,
high-water mark and drop count are available through `getStats()`. Drop counts
saturate instead of wrapping.

A queue is a work-distribution channel, not a broadcast mechanism. If multiple
consumers need each update, the producer/composition layer must send an
independent immutable copy to each bounded consumer queue, or provide an
explicitly synchronized snapshot service. A consumer that reads a queue item
must finish using that copy without assuming another task will preserve it.
Network, display and storage calls happen after the queue operation; no queue
lock is held across external I/O.

Feature windows are invalidated across boot changes, forward sequence gaps,
timestamp discontinuities, calibration changes and schema mismatches.
Out-of-order sensor frames are rejected and cannot seed a fresh feature window.
Sensor/channel freshness remains explicit through `SensorStatus`; stale or
invalid samples must not be reinterpreted as valid zero measurements.

There are no button ISRs or display/Blynk runtime tasks in the current
firmware. Any future button ISR may only signal a task or set an atomic flag.
Debouncing, reset policy and other work belong in task context; an ISR must not
perform I/O, block, mutate the snapshot, or call network/storage code.

The native queue stress test uses a synchronized host FreeRTOS queue fake to
exercise the same `QueueRegistry` API, whole-frame copying, bounded overflow
accounting and concurrent producer/consumer behavior. It does not replace
target scheduling, heap/stack sizing, hardware timing, or on-device stress
validation.
