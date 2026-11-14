#pragma once

#include <cstddef>
#include <cstdint>

constexpr std::uint8_t INPUT = 0U;
constexpr std::uint8_t OUTPUT = 1U;
constexpr std::uint8_t LOW = 0U;
constexpr std::uint8_t HIGH = 1U;

namespace ArduinoFake {
extern std::uint32_t nowUs;
extern std::uint8_t dataPin;
extern std::uint8_t clockPin;
extern bool defaultDataHigh;
extern bool clockHigh;
extern std::uint32_t risingClockEdges;
extern bool dataLevels[64];
extern std::size_t dataLevelCount;
extern std::size_t nextDataLevel;

void reset() noexcept;
void queueDataLevel(bool high) noexcept;
}  // namespace ArduinoFake

void pinMode(std::uint8_t pin, std::uint8_t mode);
void digitalWrite(std::uint8_t pin, std::uint8_t level);
int digitalRead(std::uint8_t pin);
std::uint32_t micros();
void delayMicroseconds(std::uint32_t duration);
void yield();
