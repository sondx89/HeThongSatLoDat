#pragma once

#include <array>
#include <cstdint>

#include "Drivers/SSD1306DisplayDriver.h"
#include "Models/DataContractTypes.h"

namespace project::services {

struct DisplaySnapshot {
    std::uint64_t timestampMs{0U};
    project::models::RiskState risk{project::models::RiskState::Unknown};
    std::uint8_t caseNumber{0U};
    std::array<char, 32U> caseName{};
    float baselineRemainingSeconds{0.0F};
    float pwpKpa{0.0F};
    float soilMoisturePercent{0.0F};
    float rainAccumulationWetSeconds{0.0F};
    float pwpRiseKpa{0.0F};
    float tiltDegrees{0.0F};
    std::int32_t rainRaw{0};
    bool riskValid{false};
    bool caseValid{false};
    bool baselineRemainingValid{false};
    bool pwpValid{false};
    bool soilMoistureValid{false};
    bool rainAccumulationValid{false};
    bool pwpRiseValid{false};
    bool tiltValid{false};
    bool rainRawValid{false};
};

struct DisplayServiceConfig {
    bool enabled{false};
    std::uint32_t pageIntervalMs{2000U};
    std::uint32_t maximumSnapshotAgeMs{0U};
};

enum class DisplayPage : std::uint8_t {
    Calibration,
    Alert
};

enum class DisplayServiceStatus : std::uint8_t {
    Ok,
    Disabled,
    InvalidConfiguration,
    NotInitialized,
    TimeWentBackwards,
    DriverError
};

class DisplayService final {
public:
    DisplayService(project::drivers::SSD1306DisplayDriver& driver,
                   const DisplayServiceConfig& config) noexcept;

    DisplayServiceStatus begin(std::uint64_t nowMs) noexcept;
    DisplayServiceStatus update(const DisplaySnapshot& snapshot,
                                std::uint64_t nowMs) noexcept;
    DisplayPage currentPage() const noexcept { return page_; }

private:
    static void setLine(project::drivers::Ssd1306Frame& frame,
                        std::size_t index, const char* format, ...) noexcept;
    static bool validCaseName(
        const std::array<char, 32U>& name) noexcept;
    void makeFrame(const DisplaySnapshot& snapshot, bool fresh,
                   project::drivers::Ssd1306Frame& output) const noexcept;

    project::drivers::SSD1306DisplayDriver& driver_;
    const DisplayServiceConfig config_;
    DisplayPage page_{DisplayPage::Calibration};
    std::uint64_t pageEpochMs_{0U};
    std::uint64_t lastUpdateMs_{0U};
    bool initialized_{false};
};

}  // namespace project::services
