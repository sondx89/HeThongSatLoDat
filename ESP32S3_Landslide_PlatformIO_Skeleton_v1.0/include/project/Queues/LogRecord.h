#pragma once

#include <cstdint>
#include <type_traits>

#include "Models/EventRecord.h"

namespace project::queues {

enum class LogRecordKind : std::uint8_t {
    Invalid = 0,
    Event
    // TODO(TBD): add the released log record alternatives from the log catalog.
};

struct LogRecord {
    union Payload {
        project::models::EventRecord event;

        Payload() noexcept : event{} {}
        ~Payload() = default;
    };

    LogRecordKind kind{LogRecordKind::Invalid};
    Payload payload{};
    bool valid{false};

    static LogRecord fromEvent(
        const project::models::EventRecord& eventRecord) noexcept {
        LogRecord record{};
        record.kind = LogRecordKind::Event;
        record.payload.event = eventRecord;
        record.valid = true;
        return record;
    }
};

static_assert(std::is_trivially_copyable<LogRecord>::value,
              "LogRecord must be safe to copy by value into a FreeRTOS queue");

}  // namespace project::queues
