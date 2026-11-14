# Local credential provisioning

Wi-Fi and Blynk credentials are optional local build inputs. Tracked
source/configuration must not contain real credentials, and credential values
must never be logged or written to CSV, manifests, diagnostics, or source
control.

To provision a local build, copy `include/project/secrets.example.h` to
`include/project/secrets.h` and fill in the three macros on the local machine.
The real header is ignored by Git. The example contains only empty values and
must remain placeholder-only. Do not pass credentials through PlatformIO build
flags, command-line arguments, or CI logs.

Include `project/CredentialProvider.h` where an optional network service is
composed.
`project::secrets::kWifiConfigured` and
`project::secrets::kBlynkConfigured` are compile-time gates; when credentials
are absent, the network service must remain disabled/degraded while local
sensing and logging continue. The repository has a host-tested Blynk service
contract but no production Blynk transport or runtime composition. Consumers
may use the credential arrays only to configure their client and must not
expose their contents.

The local prototype reference `final3blynkiot_5259.docx` contains
credential-like literals. It is excluded from Git and must not be used to
provision the application. Treat those values as exposed and rotate/revoke
them with their respective providers; do not copy them into source, build
flags, logs, or generated reports. Keep the reference file local and do not
redistribute it until it has been sanitized by its owner.

## Checks

- Confirm the local `include/project/secrets.h` is ignored with
  `git check-ignore -v include/project/secrets.h`.
- Scan tracked source/configuration for credential assignments without
  printing matched line contents. Exclude this prompt/documentation and the
  empty example template from credential-value checks.
- Build a no-secret configuration using an isolated empty provider or a clean
  checkout without a local secrets header; do not remove or overwrite a
  developer's local file for this check. Run
  `pio run -e esp32-s3-devkitc-1`; a target build failure unrelated to
  credential provisioning must be reported separately.
- Verify the no-credential gate with a C++17 compile-time check that includes
  `project/CredentialProvider.h` and asserts both configured flags are false.
