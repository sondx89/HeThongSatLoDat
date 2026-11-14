#include "project/Tasks/TaskBlynk.h"

namespace project::tasks {

TaskBlynk::TaskBlynk(project::services::BlynkService& service,
                     IBlynkSnapshotSource& source) noexcept
    : service_(service), source_(source) {}

project::services::BlynkServiceStatus TaskBlynk::runOnce(
    std::uint64_t nowMs) noexcept {
    project::services::BlynkSnapshot snapshot{};
    if (!source_.capture(snapshot)) {
        return project::services::BlynkServiceStatus::SnapshotUnavailable;
    }
    return service_.update(snapshot, nowMs);
}

}  // namespace project::tasks
