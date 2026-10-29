#pragma once

#include <cstdint>

#include "BSP/BoardVersion.h"

namespace project::bsp {

enum class BoardFamily : std::uint8_t {
    Esp32S3
};

struct BoardInfo {
    BoardFamily family{BoardFamily::Esp32S3};
    const char* boardId{"TBD-BOARD"};
    const char* variant{"TBD"};
    const char* manufacturer{"TBD"};
    BoardVersion hardwareVersion{kBoardVersion};
    bool boardIdKnown{false};
    bool variantKnown{false};
};

inline constexpr BoardInfo kBoardInfo{};

const BoardInfo& boardInfo() noexcept;
bool boardIdentityIsKnown(const BoardInfo& info) noexcept;
const char* boardFamilyName(BoardFamily family) noexcept;

}  // namespace project::bsp
