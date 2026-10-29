#pragma once
#include <cstddef>
#include <cstdint>

namespace project::storage {
class IStorage {
public:
    virtual ~IStorage() = default;
    virtual bool begin() = 0;
    virtual bool append(const std::uint8_t* data, std::size_t size) = 0;
    virtual bool flush() = 0;
};
}  // namespace project::storage
