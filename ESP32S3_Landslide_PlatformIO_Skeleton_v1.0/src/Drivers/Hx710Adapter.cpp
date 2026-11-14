#include "project/Drivers/Hx710Adapter.h"

namespace project::drivers {

Hx710Adapter::Hx710Adapter(HX710BDriver& driver) noexcept
    : driver_(driver) {}

project::models::SensorStatus Hx710Adapter::begin() {
    return driver_.begin();
}

project::models::SensorStatus Hx710Adapter::read(
    project::models::SensorFrame& output) {
    return driver_.read(output);
}

project::models::SensorStatus Hx710Adapter::selfTest() {
    return driver_.selfTest();
}

}  // namespace project::drivers
