#pragma once

#include <cstdint>

namespace project::drivers {

class IAdcReader {
public:
    virtual ~IAdcReader() = default;
    virtual bool readRaw(std::uint8_t pin,
                         std::int32_t& output) noexcept = 0;
};

class ArduinoAdcReader final : public IAdcReader {
public:
    bool readRaw(std::uint8_t pin,
                 std::int32_t& output) noexcept override;
};

}  // namespace project::drivers
