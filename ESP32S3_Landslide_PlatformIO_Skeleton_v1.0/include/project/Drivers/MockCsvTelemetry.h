#pragma once

#if (!defined(PROJECT_TEST_ONLY) || PROJECT_TEST_ONLY != 1) && \
    (!defined(PROJECT_MOCK_ENVIRONMENT) || PROJECT_MOCK_ENVIRONMENT != 1)
#error "MockCsvTelemetry requires an explicit test or mock firmware build"
#endif

#include <cstddef>

#include "Models/SensorFrame.h"

namespace project::drivers {

const char* mockCsvTelemetryHeader() noexcept;

bool serializeMockCsvTelemetry(const project::models::SensorFrame& frame,
                               char* output, std::size_t capacity,
                               std::size_t& bytesWritten) noexcept;

}  // namespace project::drivers
