#pragma once

// Central project-level compile-time configuration.
// Keep hardware-dependent values TBD until confirmed from the actual board/schematic.
namespace project {
namespace config {
constexpr char kImuModel[] = "MPU9250";
constexpr char kImuBus[] = "I2C";
constexpr char kBoardFamily[] = "ESP32-S3";
// TODO(TBD): SDA/SCL GPIO, I2C address/clock, sensor ranges, ODR, filter, calibration.
}  // namespace config
}  // namespace project
