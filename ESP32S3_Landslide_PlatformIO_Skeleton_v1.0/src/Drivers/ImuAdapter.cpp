#include "project/Drivers/ImuAdapter.h"

namespace project::drivers {

ImuAdapter::ImuAdapter(MPU9250Driver& driver) noexcept : driver_(driver) {}

project::models::SensorStatus ImuAdapter::begin() {
    return driver_.begin();
}

project::models::SensorStatus ImuAdapter::read(
    project::models::SensorFrame& output) {
    return driver_.read(output);
}

project::models::SensorStatus ImuAdapter::selfTest() {
    return driver_.selfTest();
}

}  // namespace project::drivers
