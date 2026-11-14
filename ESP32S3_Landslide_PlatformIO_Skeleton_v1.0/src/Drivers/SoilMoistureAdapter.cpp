#include "project/Drivers/SoilMoistureAdapter.h"

namespace project::drivers {

SoilMoistureAdapter::SoilMoistureAdapter(
    SoilMoistureDriver& driver) noexcept
    : driver_(driver) {}

project::models::SensorStatus SoilMoistureAdapter::begin() {
    return driver_.begin();
}

project::models::SensorStatus SoilMoistureAdapter::read(
    project::models::SensorFrame& output) {
    return driver_.read(output);
}

project::models::SensorStatus SoilMoistureAdapter::selfTest() {
    return driver_.selfTest();
}

}  // namespace project::drivers
