# Hardware configuration and approval gate

**Status:** NOT APPROVED — no board-specific pin assignment is enabled.

## Authoritative source files

- `include/project/BSP/BoardPins.h` is the only source for GPIO assignments.
  `kBoardPins` currently uses the unconfigured sentinel for every pin.
- `include/project/BSP/BoardConfig.h` holds bus, ADC, peripheral and output
  electrical settings. `kBoardConfig` retains unknown/TBD defaults and its
  explicit `hardwareReviewApproved` gate is false.
- `include/project/BSP/BoardInfo.h` and `BoardVersion.h` hold board identity and
  revision; the current identity and revision are unknown.
- `src/BSP/BoardConfig.cpp` reports validation of those active defaults.

The wiring values included in the v5 prompt are a DOCX-derived proposal, not
board approval. They have not been copied into source. No revision-specific
schematic, BOM, or board approval record was provided for this task.

## Validation and startup behavior

`GpioPin::isWellFormed()` only limits values to the Arduino pin API's
`uint8_t` range. It does not prove that a number is bonded out, ADC-capable, or
safe for a specific ESP32-S3 module.

`pinsDoNotConflict()` rejects incomplete I2C/HX710 pin pairs, out-of-range pin
numbers, and duplicate assignments across the declared `BoardPins` fields.
These structural checks do not replace review of strapping pins, flash/PSRAM
connections, ADC capability, voltage levels, shared-bus electrical loading,
or peripheral compatibility.

The default `BoardConfig` reports unknown board identity/version, missing
hardware approval, missing peripheral configuration, and an unavailable IMU
bus. `BoardSupportPackage`
rejects early setup unless board identity/version and pin-map checks pass,
rejects output setup unless configured outputs have reviewed electrical
levels, and rejects bus startup unless the complete report is clear. The
production `main.cpp` returns before runtime composition when these gates fail.
The PlatformIO `esp32-s3-devkitc-1` selection is a build target, not evidence
that a physical board has that identity.

## Evidence required before enabling hardware

| Decision | Required evidence | Current status |
|---|---|---|
| Board/module identity and revision | Exact manufacturer, module/board revision, and matching schematic/BOM | TBD |
| GPIO availability | Schematic and SoC/module pin review, including strapping, flash and PSRAM use | TBD |
| ADC channels | Verified ADC-capable pads, attenuation/range and electrical limits | TBD |
| I2C bus | Confirmed controller/pins, peripheral identities/addresses, voltage, pull-ups and bus loading | TBD |
| Other sensor interfaces | Confirmed sensor/module variants, protocol, voltage and wiring | TBD |
| Outputs | Actual polarity, safe reset state, electrical review and feedback capability | TBD |
| Approval provenance | Reviewed configuration ID/version/hash, reviewer and approval date | TBD |

Until these items are supplied and reviewed, leave all pin literals and
peripheral values unset. Do not treat successful compilation as board,
electrical, or hardware verification.

## Verification

- `pio test -e native-unit-tests`: PASS, 119/119 tests. The test suite includes
  duplicate, incomplete-pair and range checks, the default fail-closed
  configuration report, and explicit rejection of a technically complete but
  unapproved configuration.
- `pio run -d Z:\\ -e esp32-s3-devkitc-1`: PASS; `Z:` was a temporary alias for
  the project directory. This verifies compilation/image generation only, not
  that the selected PlatformIO board matches physical hardware.
- Firmware flash and physical/hardware-in-the-loop tests were not run. No
  physical board was verified.
