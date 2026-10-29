#pragma once

#include <cstdint>

#include "Models/EventRecord.h"

namespace project::diagnostics {
using EventSeverity = project::models::EventSeverity;
using EventRecord = project::models::EventRecord;

class EventLogger {
public:
    bool enqueue(const EventRecord& event) noexcept;
};
}  // namespace project::diagnostics
