#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace project::dataset {

class Sha256 final {
public:
    static constexpr std::size_t kDigestSize = 32U;

    Sha256() noexcept;
    void reset() noexcept;
    bool update(const std::uint8_t* data, std::size_t length) noexcept;
    bool finish(std::array<std::uint8_t, kDigestSize>& digest) noexcept;

private:
    void transform(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8U> state_{};
    std::array<std::uint8_t, 64U> block_{};
    std::uint64_t totalBytes_{0U};
    std::size_t blockLength_{0U};
    bool finished_{false};
};

}  // namespace project::dataset
