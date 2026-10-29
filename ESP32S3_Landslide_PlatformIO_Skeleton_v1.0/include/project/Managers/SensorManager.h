#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Drivers/ISensor.h"
#include "Models/SensorFrame.h"
#include "Models/SensorStatus.h"

namespace project::managers {

enum class SensorSlot : std::uint8_t {
    Imu,
    Soil,
    Rain,
    Hx710b,
    Count
};

struct SensorReadPolicy {
    std::uint32_t beginTimeoutMicros{0};
    std::uint32_t timeoutMicros{0};
    std::uint8_t maxRetries{0};
    std::uint32_t retryBackoffMicros{0};
};

struct SensorManagerConfig {
    std::uint64_t bootId{0};
    std::uint32_t samplePeriodMicros{0};
    std::array<SensorReadPolicy,
               static_cast<std::size_t>(SensorSlot::Count)> readPolicies{};
};

struct SensorManagerDiagnostics {
    std::uint32_t beginAttempts{0};
    std::uint32_t beginFailures{0};
    std::uint32_t readAttempts{0};
    std::uint32_t retries{0};
    std::uint32_t timeouts{0};
    std::uint32_t readFailures{0};
    std::uint32_t clockErrors{0};
    std::uint64_t lastElapsedMicros{0};
};

class ISensorManagerClock {
public:
    virtual ~ISensorManagerClock() = default;
    virtual std::uint64_t nowMicros() noexcept = 0;
    virtual void delayMicros(std::uint32_t durationMicros) noexcept = 0;
};

class PlatformSensorManagerClock final : public ISensorManagerClock {
public:
    std::uint64_t nowMicros() noexcept override;
    void delayMicros(std::uint32_t durationMicros) noexcept override;
};

class SensorManager final {
public:
    SensorManager(project::drivers::ISensor& imu,
                  project::drivers::ISensor& soil,
                  project::drivers::ISensor& rain,
                  project::drivers::ISensor& hx710b,
                  const SensorManagerConfig& config,
                  ISensorManagerClock& clock) noexcept;

    bool beginAll() noexcept;
    bool pollDue(std::uint64_t nowMicros) noexcept;
    bool readSnapshot(project::models::SensorFrame& output) noexcept;
    SensorManagerDiagnostics diagnostics(SensorSlot sensor) const noexcept;

private:
    bool validateConfig() const noexcept;
    project::models::SensorStatus beginSensor(
        project::drivers::ISensor& sensor,
        SensorSlot slot) noexcept;
    project::models::SensorStatus readSensor(
        project::drivers::ISensor& sensor,
        SensorSlot slot,
        project::models::SensorFrame& sensorFrame) noexcept;
    SensorManagerDiagnostics& mutableDiagnostics(SensorSlot sensor) noexcept;

    project::drivers::ISensor& imu_;
    project::drivers::ISensor& soil_;
    project::drivers::ISensor& rain_;
    project::drivers::ISensor& hx710b_;
    const SensorManagerConfig config_;
    ISensorManagerClock& clock_;
    std::array<SensorManagerDiagnostics,
               static_cast<std::size_t>(SensorSlot::Count)> diagnostics_{};
    std::uint64_t nextSequence_{0};
    std::uint64_t dueTimestampMicros_{0};
    bool ready_{false};
    bool firstPoll_{true};
    bool sampleDue_{false};
};
}  // namespace project::managers
