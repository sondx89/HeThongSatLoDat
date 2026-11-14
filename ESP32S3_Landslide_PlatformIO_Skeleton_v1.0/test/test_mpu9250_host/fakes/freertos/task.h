#pragma once

#include <cstdint>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

inline std::uint32_t fakeDelayCount = 0;
inline std::uint64_t fakeDelayedMs = 0;

inline void vTaskDelay(TickType_t ticks) {
    ++fakeDelayCount;
    fakeDelayedMs += ticks;
    fakeEspTimerNowUs += static_cast<std::int64_t>(ticks) * 1000;
}
