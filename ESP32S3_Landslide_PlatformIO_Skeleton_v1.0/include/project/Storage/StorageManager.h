#pragma once

#include <cstddef>
#include <cstdint>

#include "Models/DataContractTypes.h"
#include "Storage/IStorage.h"

namespace project::storage {

struct StorageManagerConfig {
    // Must be selected from the integration's approved memory/storage budget.
    std::size_t maximumRecordBytes{0U};
};

enum class StorageManagerState : std::uint8_t {
    Uninitialized,
    Ready,
    Faulted,
    Shutdown
};

class StorageManager final {
public:
    explicit StorageManager(IStorage& backend,
                            const StorageManagerConfig& config) noexcept;
    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;
    StorageManager(StorageManager&&) = delete;
    StorageManager& operator=(StorageManager&&) = delete;

    project::models::ApiStatus begin() noexcept;
    // Synchronous; data is borrowed only for this call and is never retained.
    project::models::ApiStatus appendRecord(const std::uint8_t* data,
                                            std::size_t size) noexcept;
    project::models::ApiStatus flush() noexcept;
    project::models::ApiStatus shutdown() noexcept;

    StorageManagerState state() const noexcept { return state_; }

private:
    IStorage& backend_;
    const StorageManagerConfig config_;
    StorageManagerState state_{StorageManagerState::Uninitialized};
};

}  // namespace project::storage
