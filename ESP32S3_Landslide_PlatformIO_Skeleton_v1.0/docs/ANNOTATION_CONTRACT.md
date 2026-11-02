# Annotation Contract v1

**Status:** Host-only annotation validation and CSV serialization. This
contract does not define or approve a landslide target, label meaning, unit, or
ground-truth method.

## Separation and target approval

Annotations are independent records; they are not inserted into
`raw_samples.csv` and are never derived from `RiskManager` output or thresholds.
Each record identifies its dataset/site/session/boot and a monotonic interval.
The interval is start-inclusive/end-exclusive and must have positive duration.
`timestamp_ms` is not UTC.

A target value can be stored only when the caller provides an approved target
contract whose ID, version, name, and unit exactly match the annotation. The
repository has no approved target contract. Without one, the manager accepts
only target-free statuses such as `UNLABELED`, `PENDING_REVIEW`, and
`NOT_APPLICABLE`; it does not invent a target or value.

`AnnotationManager` uses fixed-size text fields and caller-owned bounded
record storage. It rejects duplicate annotation IDs, invalid interval/enum/
provenance, nonfinite supplied values, unapproved/mismatched target contracts,
and storage exhaustion. Optional confidence and uncertainty fields are kept
nullable; no range or physical meaning is presumed.

## Evidence and synthetic proxies

`evidence_class` is independent of origin. Synthetic annotations are required
to be `SYNTHETIC`/`GT5` and explicitly use `SYNTHETIC_PROXY`; non-synthetic
annotations cannot claim that kind. The manager reports accepted annotation,
GT5, and synthetic-proxy counts separately. GT5 labels do not establish
independent ground truth and cannot support field accuracy or safety claims.

For GT1-GT4, retain the evidence reference, annotator, review identity where
available, uncertainty/confidence when supplied, and label-method version.
Status values are local working values, not a released controlled vocabulary.

## CSV artifact

The versioned `annotations.csv` artifact has its own 24-column schema and is
written through the shared bounded `CsvRowWriter`. It is separate from all raw
and operational CSV formats. Invalid optional numeric values are emitted
empty with a corresponding validity field; malformed UTF-8, numeric overflow,
or buffer exhaustion returns an explicit writer error. Schema version `1` is
project-local and does not imply DSD approval.

## Window joins

`AnnotationManager::matchesWindow` requires exact dataset/site/session/boot
identity and positive, overlapping monotonic intervals. Intervals use
start-inclusive/end-exclusive overlap semantics. A timestamp match without
identity is never sufficient.
