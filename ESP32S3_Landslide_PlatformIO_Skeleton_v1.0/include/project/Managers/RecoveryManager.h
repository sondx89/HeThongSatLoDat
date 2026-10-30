#pragma once

namespace project::managers {
// Current signatures and fault-to-action blockers are documented in
// docs/MODULE_API_CONTRACTS.md.
class RecoveryManager {
public:
    void classifyError();
    void executeRecovery();
};
}  // namespace project::managers
