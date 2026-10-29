#pragma once

namespace project::managers {
class ConfigManager {
public:
    bool loadLastValid();
    bool validateManifest();
    bool activateAtomically();
};
}  // namespace project::managers
