# Dataset Split Policy v1

**Status:** Host-only deterministic group splitter. The policy requires
caller-supplied proportions, seed, policy version, and independent source-group
IDs. It does not choose a project split ratio or establish evaluation
eligibility.

## Grouping and deterministic assignment

The splitter assigns a stable split to each caller-provided
`split_group_id`, using the configured nonzero seed and an explicit
train/validation/test basis-point allocation summing to 10,000. Repeated
windows with one group ID always receive the same split, independent of their
order in the input. All assignments include `split_group_id`, `split_name`,
policy version, and seed. The assignment artifact is separate from feature and
annotation artifacts.

Choose the independent group before deriving overlapping windows, or preserve
the same group ID on every overlapping window. If two windows from different
groups overlap in sequence range for the same dataset/site/session/boot stream,
the splitter fails with `OverlappingWindowLeakage`; it does not silently
reassign or discard them. Sequence ranges are inclusive at both ends, so a
shared endpoint is conservatively considered overlap. Duplicate
`(dataset_id, window_id)` identities and inconsistent origin/evidence for one
source group are rejected.

The splitter is bounded to 1,024 window descriptors per call and uses
caller-owned fixed-capacity group workspace and assignment storage. Null,
undersized, or exhausted storage returns explicit status. No random-window
split or silent truncation is performed.

## Provenance reporting

Reports include window and unique-group counts by split and origin.
`SYNTHETIC` groups require GT5 by `DataProvenance`; test-set reporting marks
whether test groups are synthetic-GT5-only and separately records the
presence of field-origin GT1-GT4 evidence. A synthetic-only test is not a
field test. Split membership alone does not make labels eligible for model
evaluation or safety analysis.

The deterministic group hash is an assignment mechanism, not a leakage proof
by itself. The implementation additionally rejects cross-group overlapping
windows on the same source stream. Callers must provide semantically correct,
stable group IDs; the splitter cannot infer independent experiments or events
from timestamps or sensor values.

## Scope limit

This component persists per-window split assignments and reports. It does not
implement a model-training loader, annotation-to-label join, metrics, or
integration of split artifacts into the raw dataset manifest. The bounded
raw-manifest JSON contract is documented in `CSV_DATASET_CONTRACT.md`. Local
schema versions are project-specific and do not modify controlled
DSD/DOP/FSS/GTIS inputs.
