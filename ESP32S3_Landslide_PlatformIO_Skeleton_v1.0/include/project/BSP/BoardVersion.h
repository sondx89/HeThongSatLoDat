#pragma once

#include <cstddef>
#include <cstdint>

namespace project::bsp {

struct BoardVersion {
    std::uint16_t major{0};
    std::uint16_t minor{0};
    std::uint16_t patch{0};
    bool known{false};
};

inline constexpr BoardVersion kBoardVersion{};

// Writes "major.minor.patch" into caller-owned storage. Returns false when
// the revision is TBD or the destination cannot hold the complete string.
bool formatBoardVersion(const BoardVersion& version,
                        char* output,
                        std::size_t capacity) noexcept;

}  // namespace project::bsp
