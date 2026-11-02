#include "project/Storage/StorageManager.h"

namespace project::storage {

StorageManager::StorageManager(
    IStorage& backend, const StorageManagerConfig& config) noexcept
    : backend_(backend), config_(config) {}

project::models::ApiStatus StorageManager::begin() noexcept {
    using project::models::ApiStatus;

    if (state_ == StorageManagerState::Shutdown) {
        return ApiStatus::Cancelled;
    }
    if (state_ == StorageManagerState::Ready) {
        return ApiStatus::Ok;
    }
    if (config_.maximumRecordBytes == 0U) {
        return ApiStatus::InvalidArgument;
    }
    if (!backend_.begin()) {
        state_ = StorageManagerState::Faulted;
        return ApiStatus::IoError;
    }

    state_ = StorageManagerState::Ready;
    return ApiStatus::Ok;
}

project::models::ApiStatus StorageManager::appendRecord(
    const std::uint8_t* data, std::size_t size) noexcept {
    using project::models::ApiStatus;

    if (data == nullptr || size == 0U) {
        return ApiStatus::InvalidArgument;
    }
    if (size > config_.maximumRecordBytes) {
        return ApiStatus::ResourceExhausted;
    }
    if (state_ == StorageManagerState::Shutdown) {
        return ApiStatus::Cancelled;
    }
    if (state_ == StorageManagerState::Faulted) {
        return ApiStatus::IoError;
    }
    if (state_ != StorageManagerState::Ready) {
        return ApiStatus::NotInitialized;
    }
    if (!backend_.append(data, size)) {
        state_ = StorageManagerState::Faulted;
        return ApiStatus::IoError;
    }
    return ApiStatus::Ok;
}

project::models::ApiStatus StorageManager::flush() noexcept {
    using project::models::ApiStatus;

    if (state_ == StorageManagerState::Shutdown) {
        return ApiStatus::Cancelled;
    }
    if (state_ == StorageManagerState::Faulted) {
        return ApiStatus::IoError;
    }
    if (state_ != StorageManagerState::Ready) {
        return ApiStatus::NotInitialized;
    }
    if (!backend_.flush()) {
        state_ = StorageManagerState::Faulted;
        return ApiStatus::IoError;
    }
    return ApiStatus::Ok;
}

project::models::ApiStatus StorageManager::shutdown() noexcept {
    state_ = StorageManagerState::Shutdown;
    return project::models::ApiStatus::Ok;
}

}  // namespace project::storage
