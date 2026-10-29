#include "project/BSP/BoardConfig.h"

namespace project::bsp {

BoardConfigReport activeBoardConfigReport() noexcept {
    return validateBoardConfig(kBoardConfig, kBoardPins);
}

}  // namespace project::bsp
