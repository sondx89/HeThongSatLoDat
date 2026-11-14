#pragma once

#include <cstdint>

#include "Models/DataContractTypes.h"

namespace project::drivers {

enum class BuzzerActiveLevel : std::uint8_t {
    Unconfigured,
    High,
    Low
};

struct BuzzerDriverConfig {
    bool enabled{false};
    bool pinConfigured{false};
    bool polarityVerified{false};
    BuzzerActiveLevel activeLevel{BuzzerActiveLevel::Unconfigured};
};

enum class BuzzerDriverStatus : std::uint8_t {
    Ok,
    NotPresent,
    InvalidConfiguration,
    NotInitialized,
    OutputFailure
};

struct BuzzerOutputReport {
    bool present{false};
    bool commandActive{false};
    project::models::AlarmOutputState feedback{
        project::models::AlarmOutputState::NotPresent};
    BuzzerDriverStatus status{BuzzerDriverStatus::NotPresent};
};

class IBuzzerPin {
public:
    virtual ~IBuzzerPin() = default;
    virtual bool configureOutput(bool inactiveHigh) noexcept = 0;
    virtual bool writeLevel(bool high) noexcept = 0;
    virtual bool hasFeedback() const noexcept = 0;
    virtual bool readFeedback(bool& active) noexcept = 0;
};

class BuzzerDriver final {
public:
    BuzzerDriver(IBuzzerPin& pin,
                 const BuzzerDriverConfig& config) noexcept;

    BuzzerDriverStatus begin() noexcept;
    BuzzerDriverStatus setActive(bool active) noexcept;
    BuzzerOutputReport report() const noexcept { return report_; }

private:
    project::models::AlarmOutputState readFeedback() noexcept;
    bool electricalLevel(bool active) const noexcept;

    IBuzzerPin& pin_;
    const BuzzerDriverConfig config_;
    BuzzerOutputReport report_{};
    bool initialized_{false};
};

}  // namespace project::drivers
