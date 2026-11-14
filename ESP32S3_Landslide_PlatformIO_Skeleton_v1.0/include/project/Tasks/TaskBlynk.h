#pragma once

#include "Services/BlynkService.h"

namespace project::tasks {

class IBlynkSnapshotSource {
public:
    virtual ~IBlynkSnapshotSource() = default;
    // Must copy all sensor/risk fields from one coherent snapshot.
    virtual bool capture(
        project::services::BlynkSnapshot& output) noexcept = 0;
};

class TaskBlynk final {
public:
    TaskBlynk(project::services::BlynkService& service,
              IBlynkSnapshotSource& source) noexcept;

    project::services::BlynkServiceStatus runOnce(
        std::uint64_t nowMs) noexcept;

private:
    project::services::BlynkService& service_;
    IBlynkSnapshotSource& source_;
};

}  // namespace project::tasks
