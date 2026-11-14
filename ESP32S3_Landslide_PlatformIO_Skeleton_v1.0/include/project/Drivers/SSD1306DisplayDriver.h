#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace project::drivers {

inline constexpr std::size_t kSsd1306LineCount = 8U;
inline constexpr std::size_t kSsd1306LineCapacity = 22U;

struct Ssd1306Frame {
    std::array<std::array<char, kSsd1306LineCapacity>, kSsd1306LineCount>
        lines{};
    bool inverted{false};
};

struct Ssd1306DisplayConfig {
    bool enabled{false};
    std::uint8_t address{0U};
    std::uint32_t transactionTimeoutMs{0U};
};

enum class Ssd1306DisplayStatus : std::uint8_t {
    Ok,
    Disabled,
    InvalidConfiguration,
    NotInitialized,
    TransportError,
    InvalidFrame
};

class ISsd1306Transport {
public:
    virtual ~ISsd1306Transport() = default;
    virtual bool begin(std::uint8_t address,
                       std::uint32_t timeoutMs) noexcept = 0;
    virtual bool present(const Ssd1306Frame& frame) noexcept = 0;
};

class SSD1306DisplayDriver final {
public:
    SSD1306DisplayDriver(ISsd1306Transport& transport,
                         const Ssd1306DisplayConfig& config) noexcept;

    Ssd1306DisplayStatus begin() noexcept;
    Ssd1306DisplayStatus present(const Ssd1306Frame& frame) noexcept;
    bool initialized() const noexcept { return initialized_; }

private:
    static bool validFrame(const Ssd1306Frame& frame) noexcept;

    ISsd1306Transport& transport_;
    const Ssd1306DisplayConfig config_;
    bool initialized_{false};
};

}  // namespace project::drivers
