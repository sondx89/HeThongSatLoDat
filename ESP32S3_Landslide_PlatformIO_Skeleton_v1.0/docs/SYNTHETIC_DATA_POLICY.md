# Synthetic Data Policy

## Purpose and evidence limits

The host-only `SyntheticSensorGenerator` exercises data handling and pipeline
behavior without physical sensor hardware. It does not simulate validated
sensor physics and does not establish landslide behavior, field performance,
or safety evidence.

Every generated observation carries `data_origin=SYNTHETIC` and
`evidence_class=GT5`. The generator creates no labels, annotations, landslide
occurrence, risk score, or proxy target. A scenario profile is not ground
truth. Do not use the same hidden scenario rule as both a feature and an
independent label.

## Determinism and manifest

The generator is available only when `PROJECT_HOST_DATA_TOOLS=1`. Its
configuration supplies a nonzero seed, record count, monotonic timestamp
origin/step, boot/sequence origin, and scenario parameters. The default seed
and generator version are declared in `SyntheticSensorGenerator.h`; the
scenario manifest records the seed, version, parameters, count, and timestamp
step and carries `SYNTHETIC`/`GT5` provenance. Repeating the same
configuration produces the same observations and latent states.

Raw values use the explicitly synthetic `synthetic_raw_count` domain. This is
not a physical unit, ADC setting, range, sensor conversion, calibration, or
sampling-rate claim. Channel offsets and scenario parameters are synthetic
inputs. Observation values and validity/timestamp/identity fields are
separate from the generator's latent scenario state. Consumers must honor
`channelValid`; a default-initialized invalid value is not a valid zero.

The manifest sets `proxyTargetPresent=false` and
`eligibleForFieldClaim=false`. No synthetic-only result may be reported as
field accuracy or safety evidence.

## Scenario catalog

The versioned generator supports:

- stable baseline and gradual trend;
- step change and oscillation;
- impulse/noise and saturation;
- channel dropout and stale channel timestamps;
- out-of-order sequence/time, sequence gap, and reboot/boot change;
- calibration-version change and mixed channel validity.

These are deterministic software test cases, not estimates of real event
frequencies or magnitudes. Scenario parameters are validated for bounded
arithmetic and timestamp/sequence overflow. Invalid configuration and
arithmetic overflow return explicit statuses.

## Integration boundary

The generator produces raw synthetic observations only. It does not classify
quality, calibrate values, assign labels, export CSV, or apply a risk policy.
`DataQualityService` remains the sole owner of quality classification;
dataset export must preserve synthetic provenance and validity separately.
