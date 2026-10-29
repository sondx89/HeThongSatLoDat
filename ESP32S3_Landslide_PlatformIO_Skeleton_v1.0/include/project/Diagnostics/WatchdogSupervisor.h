#pragma once

namespace project::diagnostics {
class WatchdogSupervisor {
public:
    void begin();
    void checkTaskProgress();
};
}  // namespace project::diagnostics
