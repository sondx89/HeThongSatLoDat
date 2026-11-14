# Production release gate

**Release decision: REJECTED / NOT RELEASE-READY.** This checklist records
requirements and evidence status only. It is not a signed approval.

| Gate | Required evidence | Status |
|---|---|---|
| Board/BOM/revision and pin map | Approved exact board identity, schematic/BOM and pin-map hash | TBD |
| Sensor set and electrical review | Exact sensor variants, wiring, voltage and independent presence policy | TBD |
| Calibration | Approved calibration IDs, versions, ranges and artifact hashes | TBD |
| FSS/schema/model | Actual model export and compatible schema/FSS/TCS hashes | BLOCKED |
| Target semantics | TCS target definition, units, validity and decision use approval | TBD |
| GTIS provenance | Dataset provenance, split/leakage audit and owner approval | TBD |
| Risk configuration | Approved RiskConfig/version/hash and separate demo-policy provenance | TBD |
| Replay and fault injection | Golden replay plus sensor, queue, storage, output and network fault evidence | TBD |
| RAM/Flash/timing | Target measurements with reviewed margins and exact firmware hash | NOT RUN |
| Watchdog and task policy | Measured priorities, stacks, queue capacities and timeout approval | TBD |
| Storage durability | Exact card/filesystem, bounded writes, flush/rotation/recovery evidence | NOT RUN |
| Display/alarm/network | Hardware checks and local-operation independence evidence | NOT RUN |
| 72-hour endurance | Traceable run report and all resource/freshness/drop metrics | NOT RUN |
| Firmware/model/config hashes | Release manifest with hashes and reproducible build provenance | TBD |
| OTA | Remains disabled unless partition, rollback and recovery are separately validated | DISABLED |
| Limitations and use restrictions | Explicit non-certification and non-life-safety statement | REQUIRED |
| Sign-off | Named reviewers, dates and approval record | MISSING |

Any critical TBD, NOT RUN or BLOCKED item rejects release. Do not sign this
checklist or label the system production-ready until every critical row has
traceable evidence and approval. The repository does not claim certified
landslide prediction or life-safety use.
