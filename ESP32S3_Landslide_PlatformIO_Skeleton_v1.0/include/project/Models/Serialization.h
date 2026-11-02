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
    // Used only for record/format combinations without an approved codec.
    // Never serialize raw struct memory.
    return SerializationStatus::NotImplemented;
}

inline SerializationStatus notImplemented(const std::uint8_t* input,
                                          std::size_t size) noexcept {
    if (input == nullptr && size != 0) {
        return SerializationStatus::InvalidArgument;
    }
    // Used only for record/format combinations without an approved codec.
    return SerializationStatus::NotImplemented;
}
}  // namespace serialization_detail

SerializationStatus serializeSensorFrame(
    const SensorFrame&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept;
SerializationStatus deserializeSensorFrame(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    SensorFrame&) noexcept;

SerializationStatus serializeProcessedFrame(
    const ProcessedFrame&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept;
SerializationStatus deserializeProcessedFrame(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    ProcessedFrame&) noexcept;

SerializationStatus serializeFeatureVector(
    const FeatureVector&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept;
SerializationStatus deserializeFeatureVector(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    FeatureVector&) noexcept;

SerializationStatus serializeModelOutput(
    const ModelOutput&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept;
SerializationStatus deserializeModelOutput(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    ModelOutput&) noexcept;

SerializationStatus serializeRiskAssessment(
    const RiskAssessment&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept;
SerializationStatus deserializeRiskAssessment(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    RiskAssessment&) noexcept;

SerializationStatus serializeEventRecord(
    const EventRecord&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept;
SerializationStatus deserializeEventRecord(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    EventRecord&) noexcept;

SerializationStatus serializeHealthSnapshot(
    const HealthSnapshot&, SerializationFormat, std::uint8_t* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept;
SerializationStatus deserializeHealthSnapshot(
    SerializationFormat, const std::uint8_t* input, std::size_t size,
    HealthSnapshot&) noexcept;

}  // namespace project::models
