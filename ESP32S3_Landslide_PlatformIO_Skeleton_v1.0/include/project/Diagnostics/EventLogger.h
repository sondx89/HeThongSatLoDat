#pragma once

#include <cstddef>
#include <cstdint>

#include "Models/EventRecord.h"

namespace project::diagnostics {
using EventSeverity = project::models::EventSeverity;
using EventRecord = project::models::EventRecord;

enum class EventLoggerStatus : std::uint8_t {
    Ok,
    InvalidEvent,
    IdentifierExhausted,
    QueueUnavailable,
    QueueFull,
    QueueError,
    InvalidRecord,
    BufferTooSmall,
    SerialOutputFailed,
    CsvOutputFailed,
    OutputsFailed
};

class IEventOutput {
public:
    virtual ~IEventOutput() = default;

    // Must be bounded, accept the complete slice or return false, and route
    // through the single T-DIAG UART owner. Never write directly to Serial here.
    virtual bool writeSerialCsv(const char* bytes, std::size_t length) noexcept = 0;

    // Called by the single T-LOG owner; bounded, complete-record CSV write.
    virtual bool writeCsv(const char* bytes, std::size_t length) noexcept = 0;
};

class EventLogger {
public:
    // Multi-producer, task-context API. Copies the record into Q_EVENT.
    bool enqueue(const EventRecord& event) noexcept;
    EventLoggerStatus enqueueWithStatus(const EventRecord& event) noexcept;

    // Call only from the Q_EVENT consumer after receiving the record. The
    // caller supplies bounded storage; no allocation or shared scratch buffer.
    EventLoggerStatus writeOutputs(const EventRecord& event,
                                   IEventOutput& output,
                                   char* csvBuffer,
                                   std::size_t csvCapacity,
                                   std::size_t& bytesWritten) const noexcept;

    static EventLoggerStatus writeCsvHeader(char* output,
                                            std::size_t capacity,
                                            std::size_t& bytesWritten) noexcept;
};
}  // namespace project::diagnostics
