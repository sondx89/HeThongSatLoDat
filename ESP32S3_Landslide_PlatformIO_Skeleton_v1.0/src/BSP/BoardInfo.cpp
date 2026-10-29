#include "project/BSP/BoardInfo.h"

namespace project::bsp {

const BoardInfo& boardInfo() noexcept {
    return kBoardInfo;
}

bool boardIdentityIsKnown(const BoardInfo& info) noexcept {
    return info.boardIdKnown && info.variantKnown &&
           info.boardId != nullptr && info.variant != nullptr &&
           info.boardId[0] != '\0' && info.variant[0] != '\0';
}

const char* boardFamilyName(BoardFamily family) noexcept {
    switch (family) {
    case BoardFamily::Esp32S3:
        return "ESP32-S3";
    }
    return "UNKNOWN";
}

}  // namespace project::bsp
