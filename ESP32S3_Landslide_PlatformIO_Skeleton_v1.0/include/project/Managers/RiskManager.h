#pragma once

#include <array>
#include <cstdint>

#include "Models/HealthSnapshot.h"
#include "Models/ModelOutput.h"
#include "Models/ProcessedFrame.h"
#include "Models/RiskAssessment.h"

namespace project::managers {

enum class TiltValidationStatus : std::uint8_t {
    NotValidated,
    Valid,
    Invalid
};

struct TiltValidation {
    float deviationDeg{0.0F};
    TiltValidationStatus status{TiltValidationStatus::NotValidated};
    bool definitionApproved{false};
};

struct RiskEvaluationContext {
    std::uint64_t bootId{0};
    std::uint64_t sequenceNo{0};
    std::uint64_t timestampMs{0};
    // FAULT blocks model-derived score; only an approved severe sensor rule
    // may independently retain DANGER_TEST.
    project::models::HealthState health{project::models::HealthState::Unknown};
};

enum class RiskScoreDirection : std::uint8_t {
    HigherIsMoreSevere,
    LowerIsMoreSevere
};

struct RiskManagerConfig {
    static constexpr std::size_t kConfigHashSize = 32;

    std::uint32_t version{0};
    std::array<std::uint8_t, kConfigHashSize> hashSha256{};
    bool hashVerified{false};
    bool approved{false};

    bool modelRuleEnabled{false};
    bool modelOnlyFallbackApproved{false};
    bool tiltOnlyFallbackApproved{false};
    std::uint16_t expectedModelOutputCount{0};
    RiskScoreDirection scoreDirection{
        RiskScoreDirection::HigherIsMoreSevere};
    float scoreMinimum{0.0F};
    float scoreMaximum{0.0F};
    float scoreSafeBoundary{0.0F};
    float scoreMonitorBoundary{0.0F};

    bool tiltRuleEnabled{false};
    float tiltSafeMaximumDeg{0.0F};
    float tiltMonitorMaximumDeg{0.0F};

    std::uint16_t promotionSamples{0};
    std::uint16_t recoverySamples{0};
    std::uint32_t recoveryHoldMs{0};
    bool hysteresisConfigured{false};
};

class RiskManager final {
public:
    explicit RiskManager(
        const RiskManagerConfig& config = RiskManagerConfig{}) noexcept;

    bool configured() const noexcept { return configValid_; }

    bool evaluate(const project::models::ModelOutput& model,
                  const TiltValidation& tilt,
                  project::models::DataQualityState quality,
                  const RiskEvaluationContext& context,
                  project::models::RiskAssessment& output) noexcept;

    // Compatibility adapter for TaskRisk. ProcessedFrame currently has no
    // approved single tilt metric; this path intentionally does not validate
    // roll/pitch as a substitute.
    bool evaluate(const project::models::ModelOutput& model,
                  const project::models::ProcessedFrame& frame,
                  const project::models::HealthSnapshot& health,
                  project::models::RiskAssessment& output) noexcept;

    static bool validateConfig(const RiskManagerConfig& config) noexcept;

private:
    enum class Candidate : std::uint8_t {
        Unknown,
        Safe,
        Monitor,
        HighRiskTest
    };

    static std::uint8_t severity(Candidate candidate) noexcept;
    Candidate classifyModel(const project::models::ModelOutput& model,
                            bool& scoreValid) const noexcept;
    Candidate classifyTilt(const TiltValidation& tilt) const noexcept;
    void setAssessment(project::models::RiskAssessment& output,
                       Candidate candidate,
                       bool scoreValid,
                       float score,
                       const RiskEvaluationContext& context) noexcept;

    const RiskManagerConfig config_;
    const bool configValid_;
    Candidate current_{Candidate::Unknown};
    Candidate pending_{Candidate::Unknown};
    std::uint16_t pendingSamples_{0};
    std::uint64_t pendingSinceMs_{0};
    std::uint64_t stateEnterMs_{0};
    std::uint64_t lastBootId_{0};
    std::uint64_t lastSequenceNo_{0};
    bool hasLastSequence_{false};
};

}  // namespace project::managers
