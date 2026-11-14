#pragma once

#include <cstdint>

using BaseType_t = std::int32_t;
using UBaseType_t = std::uint32_t;
using TickType_t = std::uint32_t;

#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define configTICK_RATE_HZ 1000U
