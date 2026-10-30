#pragma once

namespace project::bsp { class BoardSupportPackage; }

namespace project::managers {
// Current signatures and lifecycle limitations are documented in
// docs/MODULE_API_CONTRACTS.md.
class AppController {
public:
    bool begin();
    void runHealthChecks();
    void requestShutdown();
};
}  // namespace project::managers
