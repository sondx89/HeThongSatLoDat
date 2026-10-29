#pragma once
#include "Storage/IStorage.h"

namespace project::storage {
class StorageManager {
public:
    bool begin();
    bool appendRecord(const std::uint8_t* data, std::size_t size);
    bool flush();
};
}  // namespace project::storage
