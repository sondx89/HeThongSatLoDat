#pragma once

#include <cstdint>

extern std::int64_t fakeRainTimerNowUs;

inline std::int64_t esp_timer_get_time() {
    return fakeRainTimerNowUs;
}
