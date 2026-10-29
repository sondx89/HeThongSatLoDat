#pragma once
#include <cstdint>

namespace project::diagnostics {
enum class EventSeverity : std::uint8_t { Debug, Info, Warning, Error, Critical };

struct EventRecord {
    EventSeverity severity{EventSeverity::Info};
    std::uint32_t eventCode{0};
    std::uint64_t timestampMicros{0};
};

class EventLogger {
public:
    bool enqueue(const EventRecord& event);
};
}  // namespace project::diagnostics
