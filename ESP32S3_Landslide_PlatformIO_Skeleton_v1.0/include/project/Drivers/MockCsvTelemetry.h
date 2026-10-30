#pragma once

#include <cstddef>

#include "Models/SensorFrame.h"

namespace project::drivers {

const char* mockCsvTelemetryHeader() noexcept;

bool serializeMockCsvTelemetry(const project::models::SensorFrame& frame,
                               char* output, std::size_t capacity,
                               std::size_t& bytesWritten) noexcept;

}  // namespace project::drivers
