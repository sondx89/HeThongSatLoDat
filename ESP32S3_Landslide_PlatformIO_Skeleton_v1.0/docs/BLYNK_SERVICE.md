# Optional Blynk telemetry service

**Status:** Host-tested service/owner contract; no Blynk SDK transport, Wi-Fi
runtime composition or credentials are installed.

`BlynkService` is disabled unless both its configuration and externally
provisioned credentials are available. Application composition should set
`credentialsAvailable` from `project::secrets::kBlynkConfigured`; the service
never stores, logs or exposes credential contents. The adapter must keep all
Blynk API calls inside the single `TaskBlynk` owner and enforce the configured
finite operation timeout. `TaskBlynk::runOnce()` and the service are not yet
registered in `TaskFramework` because there is no approved runtime composition
or target scheduling configuration.

The pin contract is V0 temperature °C, V1 soil relative %, V2 rain surface
wetness relative %, V3 tilt degrees, V4 calibrated PWP kPa, V5 status text,
V6 rain raw, and V7 a caller-configured CASE name. Invalid/non-finite values
are skipped; in particular, invalid PWP is never transmitted as zero. Snapshot
age is checked before any network operation. CASE indices are range-checked
before indexing the configured fixed-size name table; invalid indices are
counted and V7 is omitted. P10's DemoRulePolicy has not been implemented, so
CASE names and the lookup table must be supplied by a future approved caller.

Telemetry defaults to 15 seconds. WATCH logs `theo_doi` with Warning and sends
three notifications separated by 5 seconds. DANGER logs `nguy_hiem` with
Critical and repeats notifications every 10 seconds. Safe notifications default
off. Reconnection is one bounded attempt per due service call with capped
exponential backoff; the service never delays sensor, local output or logger
work.

No real Blynk token, Wi-Fi password, client library or network credentials were
added. Local operation and hardware/network behavior were not physically
tested. A production adapter must be supplied and validated before enabling
the feature.
