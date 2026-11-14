#include "project/Drivers/SSD1306DisplayDriver.h"

namespace project::drivers {

SSD1306DisplayDriver::SSD1306DisplayDriver(
    ISsd1306Transport& transport,
    const Ssd1306DisplayConfig& config) noexcept
    : transport_(transport), config_(config) {}

Ssd1306DisplayStatus SSD1306DisplayDriver::begin() noexcept {
    initialized_ = false;
    if (!config_.enabled) {
        return Ssd1306DisplayStatus::Disabled;
    }
    if (config_.address == 0U || config_.address > 0x7FU ||
        config_.transactionTimeoutMs == 0U) {
        return Ssd1306DisplayStatus::InvalidConfiguration;
    }
    if (!transport_.begin(config_.address, config_.transactionTimeoutMs)) {
        return Ssd1306DisplayStatus::TransportError;
    }
    initialized_ = true;
    return Ssd1306DisplayStatus::Ok;
}

bool SSD1306DisplayDriver::validFrame(
    const Ssd1306Frame& frame) noexcept {
    for (const auto& line : frame.lines) {
        bool terminated = false;
        for (const char value : line) {
            if (value == '\0') {
                terminated = true;
                break;
            }
            if (value < 0x20 || value > 0x7E) {
                return false;
            }
        }
        if (!terminated) {
            return false;
        }
    }
    return true;
}

Ssd1306DisplayStatus SSD1306DisplayDriver::present(
    const Ssd1306Frame& frame) noexcept {
    if (!config_.enabled) {
        return Ssd1306DisplayStatus::Disabled;
    }
    if (!initialized_) {
        return Ssd1306DisplayStatus::NotInitialized;
    }
    if (!validFrame(frame)) {
        return Ssd1306DisplayStatus::InvalidFrame;
    }
    return transport_.present(frame) ? Ssd1306DisplayStatus::Ok
                                     : Ssd1306DisplayStatus::TransportError;
}

}  // namespace project::drivers
