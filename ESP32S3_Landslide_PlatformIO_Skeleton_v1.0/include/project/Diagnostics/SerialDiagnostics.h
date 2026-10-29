#pragma once

namespace project::diagnostics {
class SerialDiagnostics {
public:
    void begin();
    void poll();
};
}  // namespace project::diagnostics
