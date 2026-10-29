#pragma once

namespace project::bsp { class BoardSupportPackage; }

namespace project::managers {
class AppController {
public:
    bool begin();
    void runHealthChecks();
    void requestShutdown();
};
}  // namespace project::managers
