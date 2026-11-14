# Hardware verification and endurance plan

**Status:** NOT RUN — no revision-specific board/BOM, approved pin map or
physical sensors were supplied. This document is a test plan, not hardware
evidence. Do not copy planned outcomes into a PASS report without attached
measurements.

## Run identity and evidence record

Create one immutable record per run. Every field below is required before
marking a hardware stage PASS or FAIL:

| Field | Value |
|---|---|
| Run ID / defect IDs | TBD |
| Operator / reviewer / date and timezone | TBD |
| ESP32-S3 board manufacturer, model, revision, module and BOM | TBD |
| Schematic/BOM revision and approved pin-map ID/hash | TBD |
| Firmware version, git commit and firmware SHA-256 | TBD |
| Active manifest/configuration ID and SHA-256 | TBD |
| Sensor module IDs/serials/revisions | TBD |
| Calibration identities, versions and artifact hashes | TBD |
| Power supply, voltage/current limit and wiring photos | TBD |
| Reset reason before/after each stage | TBD |
| Raw log, scope/logic-analyzer files and measurement references | TBD |

Use only `PASS`, `FAIL`, or `NOT RUN`. Record the operator, timestamp, measured
value, limit/source, raw evidence path and defect ID for every FAIL. A missing
measurement is NOT RUN, never PASS. Host tests and firmware compilation are
software evidence and do not change these hardware statuses.

## Staged test matrix

All stages start as **NOT RUN**. Run in order; stop and file a defect on a
failed safety-critical stage.

| Stage | Test | Required observation/evidence | Status | Defect ID |
|---:|---|---|---|---|
| 0 | Board-only boot | Exact board identity, reset reason, safe outputs, boot log | NOT RUN | TBD |
| 1 | I2C scan | Bus/pins, pull-ups, voltage, addresses and repeated-scan stability | NOT RUN | TBD |
| 2 | MPU9250 | Identity, raw/physical plausibility, freshness and transaction latency | NOT RUN | TBD |
| 3 | SSD1306 | Address, readable pages, invalid markers, inversion and update latency | NOT RUN | TBD |
| 4 | Shared I2C bus | MPU9250 and OLED simultaneous traffic, timeout and bus recovery | NOT RUN | TBD |
| 5 | Soil channel 1 | Independent raw range, calibration identity, stale/unplug behavior | NOT RUN | TBD |
| 6 | Soil channel 2 | Independent raw range, calibration identity, stale/unplug behavior | NOT RUN | TBD |
| 7 | Soil channel 3 | Independent raw range, calibration identity, stale/unplug behavior | NOT RUN | TBD |
| 8 | YL-83/FC-37 AO and DO | Exact module, AO endpoints, verified DO polarity, independent faults | NOT RUN | TBD |
| 9 | HX710B raw/timeout | Variant/timing, signed raw samples, ready timeout and unplug recovery | NOT RUN | TBD |
| 10 | All sensors together | Cross-sensor freshness, task latency, bus contention and queue high-water | NOT RUN | TBD |
| 11 | Buzzer/alarm | Verified polarity, startup/reset safe state, pattern and feedback semantics | NOT RUN | TBD |
| 12 | CSV/storage | Schema/header, rotation, bounded latency, full-card/write/flush failure | NOT RUN | TBD |
| 13 | Blynk offline/reconnect | No local blocking, reconnect/backoff, notification and telemetry cadence | NOT RUN | TBD |
| 14 | Sensor unplug/replug | Each sensor independently invalidates and recovers without stale reuse | NOT RUN | TBD |
| 15 | Queue saturation | Induced overload, bounded producers, exact drop/high-water counters | NOT RUN | TBD |
| 16 | Storage failure | Absent/full/write/flush failures, recovery, no false success | NOT RUN | TBD |
| 17 | Power cycle | Repeated cold/warm reset, boot identity/sequence behavior, safe outputs | NOT RUN | TBD |
| 18 | 72-hour endurance | Continuous trace and periodic resource/latency/freshness measurements | NOT RUN | TBD |

## Required measurements

Record timestamped samples at start, each configured interval, each fault and
recovery, and end of run:

- heap free, minimum-ever free and largest free block;
- per-task stack high-water words and task progress/failure/deadline counts;
- per-queue capacity, high-water mark and drop count;
- sensor last-sample age, validity, error counts and recovery counts;
- sensor-loop, task, I2C, inference, storage-write/flush and reconnect latency;
- reset reason, watchdog state, storage bytes/rotation and Blynk reconnect count;
- supply voltage/current and any brownout or thermal observation.

Priorities, stack sizes, queue depths, watchdog bounds, storage limits and
acceptance thresholds must be approved and measured for the exact target. They
remain TBD; do not derive them from this plan or from successful host tests.

## Current evidence

Native tests and ESP32-S3 compilation have been run in software-only contexts.
No physical board, peripheral wiring, firmware flash, I2C scan, sensor reading,
storage card, network session or 72-hour run was available. Therefore every
hardware stage above remains NOT RUN and no hardware defect/pass result is
asserted.
