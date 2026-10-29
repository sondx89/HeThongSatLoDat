#include "project/Diagnostics/EventLogger.h"

#include <atomic>

#include "project/Queues/QueueRegistry.h"

namespace project::diagnostics {
namespace {

std::atomic<std::uint32_t> gEventSequence{0};
std::atomic<std::uint64_t> gEventId{0};

}  // namespace

bool EventLogger::enqueue(const EventRecord& event) noexcept {
    if (!event.valid) {
        return false;
    }

    EventRecord queued = event;
    queued.eventSequenceNo =
        gEventSequence.fetch_add(1, std::memory_order_relaxed) + 1U;
    queued.eventId = gEventId.fetch_add(1, std::memory_order_relaxed) + 1U;

    return project::queues::QueueRegistry::instance().sendEvent(queued, 0) ==
           project::queues::QueueRegistryStatus::Ok;
}

}  // namespace project::diagnostics
