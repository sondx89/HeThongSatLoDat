#pragma once

#include <cstdint>

inline std::int64_t fakeSoilTimerNowUs = 0;

inline std::int64_t esp_timer_get_time() {
    return fakeSoilTimerNowUs;
}
