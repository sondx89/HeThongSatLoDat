#include "project/BSP/BoardVersion.h"

#include <cstdio>

namespace project::bsp {

bool formatBoardVersion(const BoardVersion& version,
                        char* output,
                        std::size_t capacity) noexcept {
    if (output == nullptr || capacity == 0) {
        return false;
    }

    output[0] = '\0';
    if (!version.known) {
        return false;
    }

    const int written = std::snprintf(output,
                                     capacity,
                                     "%u.%u.%u",
                                     static_cast<unsigned int>(version.major),
                                     static_cast<unsigned int>(version.minor),
                                     static_cast<unsigned int>(version.patch));
    if (written < 0 || static_cast<std::size_t>(written) >= capacity) {
        output[0] = '\0';
        return false;
    }
    return true;
}

}  // namespace project::bsp
