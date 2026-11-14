# Pilot Configuration Register

**Status:** NOT APPROVED — all active board and physical configuration values
remain unset.

`include/project/BSP/BoardPins.h` is the sole GPIO source and currently uses
unconfigured pins. `include/project/BSP/BoardConfig.h` keeps peripheral,
electrical and review settings unknown. The values below are captured only as
an unapproved proposal from the supplied prompt/prototype; they are not
compiled defaults or wiring instructions.

## Reference-only proposal

| Function | Proposed value | Active value/status |
|---|---:|---|
| Soil moisture channels 1/2/3 | GPIO 1 / 2 / 3 | TBD — confirm board pinout, ADC unit/channel, range and input protection |
| HX710B SCK / DOUT | GPIO 5 / 6 | TBD — confirm module variant, protocol/timing, electrical levels |
| Surface wetness AO / optional DO | GPIO 7 / 10 | TBD — confirm sensor/module, ADC range and digital polarity |
| I2C SDA / SCL | GPIO 8 / 9 | TBD — confirm controller, board availability, voltage and pull-ups |
| Buzzer | GPIO 11 | TBD — confirm polarity, driver circuitry and reset-safe electrical state |
| BOOT button | GPIO 0 | TBD — confirm board implementation and strapping behavior; do not attach an external load without review |
| MPU9250 address | `0x68` | TBD — verify WHO_AM_I and actual address strap |
| SSD1306 address | `0x3C` | TBD — discover on the approved shared bus |
| MPU mounting/frame | Not specified | TBD — Roll/Pitch calculation remains disabled until approved |
| Soil/HX710B/rain calibration | Not approved | TBD — no prototype endpoint or coefficient is installed |
| Risk limits/model output meaning | Not approved | TBD — no geotechnical limit or probability interpretation is installed |
| Runtime task/queue/storage settings | Not approved | TBD — choose from measured target budgets and backend bounds |

The prototype describes an ESP32-S3 N16R8, whereas the current PlatformIO
environment selects `esp32-s3-devkitc-1` as an N8 build target. Neither value
identifies the physical Pilot board. Resolve this mismatch before selecting
any pin or module-specific configuration.

## Explicit non-defaults

- Do not enable ADC calibration from prototype AIR/WATER endpoints.
- Do not configure PWP conversion from prototype raw-zero/full-scale values.
- Do not convert surface wetness to rainfall or `mm/h`.
- Do not enable MPU Roll/Pitch until the installed coordinate frame, mounting,
  reference pose, filter and sign convention are approved.
- Do not use prototype risk thresholds or CASE labels as runtime truth.
- Do not use prototype buzzer polarity or network credentials.
- Do not clear calibration or stored data as part of a BOOT/demo reset.

## Approval record required

Before changing `BoardPins` or `BoardConfig`, attach the board manufacturer,
exact product/revision, schematic/BOM revision, reviewer, approval date,
configuration version and content hash. Separately record ADC settings,
sensor identities, I2C wiring/address, output electrical levels, calibration
artifact identities and model/schema identities. Until then the production
startup gate must remain closed.
