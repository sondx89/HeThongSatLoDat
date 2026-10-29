#pragma once

#include <cstddef>
#include <cstdint>

#include "Models/EventRecord.h"
#include "Models/FeatureVector.h"
#include "Models/HealthSnapshot.h"
#include "Models/ModelOutput.h"
#include "Models/ProcessedFrame.h"
#include "Models/RiskAssessment.h"
#include "Models/SensorFrame.h"

namespace project::models {

// Caller-owned bounded buffer. This helper deliberately does not define byte
// order, field order, floating-point encoding, or a persistent file format.
class BoundedByteWriter {
public:
    BoundedByteWriter(std::uint8_t* buffer, std::size_t capacity) noexcept
        : buffer_(buffer), capacity_(capacity), size_(0), status_(SerializationStatus::Ok) {
        if (buffer_ == nullptr && capacity_ != 0) {
            status_ = SerializationStatus::InvalidArgument;
        }
    }

    SerializationStatus appendBytes(const std::uint8_t* bytes, std::size_t length) noexcept {
        if (status_ != SerializationStatus::Ok) {
            return status_;
        }
        if (bytes == nullptr && length != 0) {
            status_ = SerializationStatus::InvalidArgument;
            return status_;
        }
        if (length > capacity_ - size_) {
            status_ = SerializationStatus::BufferTooSmall;
            return status_;
        }
        for (std::size_t i = 0; i < length; ++i) {
            buffer_[size_ + i] = bytes[i];
        }
        size_ += length;
        return status_;
    }

    std::size_t size() const noexcept { return size_; }
    SerializationStatus status() const noexcept { return status_; }

private:
    std::uint8_t* buffer_;
    std::size_t capacity_;
    std::size_t size_;
    SerializationStatus status_;
};

namespace serialization_detail {
inline SerializationStatus notImplemented(std::uint8_t* output,
                                          std::size_t capacity,
                                          std::size_t& bytesWritten) noexcept {
    bytesWritten = 0;
    if (output == nullptr && capacity != 0) {
        return SerializationStatus::InvalidArgument;
    }
    // TODO(TBD): implement only after canonical formats, bounds, and versions
    // are approved. Never serialize raw struct memory.
    return SerializationStatus::NotImplemented;
}

inline SerializationStatus notImplemented(const std::uint8_t* input,
                                          std::size_t size) noexcept {
    if (input == nullptr && size != 0) {
        return SerializationStatus::InvalidArgument;
    }
    // TODO(TBD): reject unknown schema versions and invalid/non-finite fields.
    return SerializationStatus::NotImplemented;
}
}  // namespace serialization_detail

#define PROJECT_DECLARE_SERIALIZATION(Type, prefix)                                         \
    inline SerializationStatus serialize##prefix(                                         \
        const Type&, SerializationFormat, std::uint8_t* output, std::size_t capacity,     \
        std::size_t& bytesWritten) noexcept {                                             \
        return serialization_detail::notImplemented(output, capacity, bytesWritten);     \
    }                                                                                     \
    inline SerializationStatus deserialize##prefix(                                      \
        SerializationFormat, const std::uint8_t* input, std::size_t size, Type&) noexcept { \
        return serialization_detail::notImplemented(input, size);                         \
    }

PROJECT_DECLARE_SERIALIZATION(SensorFrame, SensorFrame)
PROJECT_DECLARE_SERIALIZATION(ProcessedFrame, ProcessedFrame)
PROJECT_DECLARE_SERIALIZATION(FeatureVector, FeatureVector)
PROJECT_DECLARE_SERIALIZATION(ModelOutput, ModelOutput)
PROJECT_DECLARE_SERIALIZATION(RiskAssessment, RiskAssessment)
PROJECT_DECLARE_SERIALIZATION(EventRecord, EventRecord)
PROJECT_DECLARE_SERIALIZATION(HealthSnapshot, HealthSnapshot)

#undef PROJECT_DECLARE_SERIALIZATION

}  // namespace project::models
