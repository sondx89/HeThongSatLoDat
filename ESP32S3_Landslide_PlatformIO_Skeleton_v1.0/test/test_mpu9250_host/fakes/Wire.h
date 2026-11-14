#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

#include <esp_timer.h>

class TwoWire {
public:
    std::array<std::uint8_t, 256> registers{};
    std::vector<std::pair<std::uint8_t, std::uint8_t>> writes{};
    std::uint16_t timeoutMs{0};
    std::uint8_t failTransmissionAt{0};
    std::uint8_t transmissionCount{0};
    std::uint8_t transmissionError{4};
    std::uint8_t requestCountOverride{0};
    std::size_t responseByteLimit{std::numeric_limits<std::size_t>::max()};
    std::uint64_t requestAdvanceUs{0};
    std::uint32_t requestCalls{0};
    std::uint8_t shortWriteAt{0};
    std::uint8_t writeCount{0};
    bool shortWrite{false};

    void setTimeOut(std::uint16_t timeout) {
        timeoutMs = timeout;
    }

    void beginTransmission(std::uint8_t address) {
        address_ = address;
        transmit_.clear();
    }

    std::size_t write(std::uint8_t value) {
        ++writeCount;
        if (shortWrite || writeCount == shortWriteAt) {
            shortWrite = false;
            return 0;
        }
        transmit_.push_back(value);
        return 1;
    }

    std::uint8_t endTransmission(bool stop = true) {
        ++transmissionCount;
        if (transmissionCount == failTransmissionAt) {
            transmit_.clear();
            return transmissionError;
        }
        if (transmit_.size() == 1U && !stop) {
            registerPointer_ = transmit_[0];
        } else if (transmit_.size() == 2U && stop) {
            registers[transmit_[0]] = transmit_[1];
            writes.emplace_back(transmit_[0], transmit_[1]);
        } else {
            transmit_.clear();
            return 1;
        }
        transmit_.clear();
        return 0;
    }

    std::uint8_t requestFrom(std::uint8_t address,
                             std::uint8_t count,
                             bool stop = true) {
        (void)stop;
        ++requestCalls;
        receive_.clear();
        if (address != address_) {
            return 0;
        }
        for (std::size_t index = 0;
             index < count && index < responseByteLimit;
             ++index) {
            receive_.push_back(
                registers[static_cast<std::uint8_t>(registerPointer_ + index)]);
        }
        registerPointer_ =
            static_cast<std::uint8_t>(registerPointer_ + count);
        fakeEspTimerNowUs += static_cast<std::int64_t>(requestAdvanceUs);
        return requestCountOverride == 0U ? count : requestCountOverride;
    }

    int available() const {
        return static_cast<int>(receive_.size());
    }

    int read() {
        if (receive_.empty()) {
            return -1;
        }
        const int value = receive_.front();
        receive_.pop_front();
        return value;
    }

private:
    std::uint8_t address_{0};
    std::uint8_t registerPointer_{0};
    std::vector<std::uint8_t> transmit_{};
    std::deque<std::uint8_t> receive_{};
};
