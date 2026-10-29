#include "project/BSP/BoardPins.h"

namespace project::bsp {

bool validateBoardPins(const BoardPins& pins) noexcept {
    return pinsDoNotConflict(pins);
}

}  // namespace project::bsp
