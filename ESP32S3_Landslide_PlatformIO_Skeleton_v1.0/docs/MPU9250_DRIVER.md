# MPU9250 driver

## Datasheet assumptions and hardware gate

The implementation targets the TDK/InvenSense MPU-9250 product specification
(revision 1.1):

- [MPU-9250 Product Specification](https://product.tdk.com/en/system/files/dam/doc/product/sensor/mortion-inertial/imu/data_sheet/ps-mpu-9250a-01-v1.1.pdf)
- `WHO_AM_I` at `0x75` must return `0x71`.
- `PWR_MGMT_1=0x00` wakes the device. `GYRO_CONFIG=0x00` selects ±250 dps
  (131 LSB/(degree/s)); `ACCEL_CONFIG=0x00` selects ±2 g (16384 LSB/g).
- `TEMP_OUT` is signed and converted as `raw / 333.87 + 21.0` degrees C.

The driver fails configuration validation unless identity verification and
`datasheetAssumptionsApproved` are enabled, the expected identity is `0x71`,
the required register values and scale factors match those ranges, and the
filter-gap bound is nonzero. These checks do not prove that a breakout board is
genuine, correctly powered, or mounted as expected. Hardware identity,
electrical compatibility, axis orientation and mounting remain subject to
physical review. The DOCX wiring and scale assumptions are not production
board approval.

## Acquisition and calibration

- The integrator supplies the I2C address, nonzero transaction timeout, full
  register setup, approved datasheet assumptions and maximum filter gap.
  Configuration defaults remain unset.
- `initialize()` applies the I2C timeout, reads and checks `WHO_AM_I`, and
  checks every register-write byte count and `endTransmission()` result.
- Every register read checks pointer-write status, exact `requestFrom()` count,
  exact available bytes and successful consumption of the complete transfer.
  Partial reads are drained and rejected; raw output is decoded only after a
  complete read.
- `begin()` calibrates gyro offsets from 200 samples with 5 ms task delays
  between samples. The sensor must be stationary during this interval.
  Calibration is RAM-only. Accelerometer offsets are not inferred because
  the mounting/reference pose has not been measured.
- `readSample()` preserves signed raw accelerometer, temperature and gyro
  counts. Physical scales and temperature are populated only after the
  datasheet-assumption gate and identity check pass.
- Reads use fixed-size buffers and do not allocate dynamically. The intended
  owner is a sensor task; the driver has no internal lock. Do not call
  initialization or calibration from an ISR.

## Tilt calculation and validity

For the sensor's own axes, the accelerometer angles are:

```text
rollAccel  = atan2(ay, az)
pitchAccel = atan2(-ax, sqrt(ay*ay + az*az))
```

The complementary filter uses alpha `0.98`, gyroscope x/y rates, and elapsed
microseconds from the monotonic ESP timer (`dt` in seconds). The first valid
sample initializes the filter from the accelerometer. Nonfinite values,
non-increasing timestamps, or gaps exceeding the explicitly configured
`maximumFilterGapMs` invalidate/reset the filter; a subsequent valid sample
reinitializes it. `tiltAngleDeg` is `max(abs(roll), abs(pitch))`.

These are sensor-axis values, not an installed-board coordinate transform.
Axis orientation, mounting, reference pose and physical tilt interpretation
remain TBD until measured. `Mpu9250DriverConfig::tiltDefinitionApproved`
defaults to false and must be explicitly enabled only after approving this
formula/filter together with the installed sensor frame and mounting. Without
that approval, the driver continues to return the complete raw sample and
datasheet-scaled accel/gyro/temperature values, but reports
`TiltDefinitionNotApproved`; Roll, Pitch and combined tilt remain invalid and
are not calculated. An invalid tilt does not invalidate a complete raw IMU
acquisition; its validity remains separate from `imuStatus`.

## Adapter and host tests

`ImuAdapter` is a thin `ISensor` forwarding adapter around an injected driver.
Fake-`TwoWire` host tests cover identity/write failures, exact/short reads,
signed raw decoding, temperature, calibration cadence, golden poses,
real-`dt` filtering and invalid/gapped timestamps. Run the standalone host
test with a C++17 compiler:

```powershell
g++ -std=c++17 -I test\test_mpu9250_host\fakes -I include -I include\project `
  test\test_mpu9250_host\test_mpu9250_host.cpp src\Drivers\MPU9250Driver.cpp `
  -o mpu9250_host_tests.exe
.\mpu9250_host_tests.exe
```

The host fakes do not replace target build, wiring review, or physical
validation.
