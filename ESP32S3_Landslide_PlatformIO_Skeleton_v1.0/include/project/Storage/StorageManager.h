#pragma once
#include "Storage/IStorage.h"

namespace project::storage {
// Current signatures, backend gap, and IStorage limitations are documented in
// docs/MODULE_API_CONTRACTS.md.
class StorageManager {
public:
    bool begin();
    bool appendRecord(const std::uint8_t* data, std::size_t size);
    bool flush();
};
}  // namespace project::storage
