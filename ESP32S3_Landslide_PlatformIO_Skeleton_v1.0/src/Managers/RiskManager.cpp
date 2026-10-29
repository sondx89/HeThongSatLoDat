#include "project/Managers/RiskManager.h"

#include <cmath>
#include <limits>

namespace project::managers {
namespace {

bool hasNonZeroHash(
    const std::array<std::uint8_t, RiskManagerConfig::kConfigHashSize>& hash)
    noexcept {
    for (const std::uint8_t byte : hash) {
        if (byte != 0U) {
            return true;
        }
    }
    return false;
}

void incrementSaturated(std::uint16_t& value) noexcept {
    if (value != std::numeric_limits<std::uint16_t>::max()) {
        ++value;
    }
}

std::uint32_t elapsedMs(std::uint64_t now,
                        std::uint64_t since) noexcept {
    if (now < since) {
        return 0U;
    }
    const std::uint64_t elapsed = now - since;
    return elapsed > std::numeric_limits<std::uint32_t>::max()
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(elapsed);
}

}  // namespace

RiskManager::RiskManager(const RiskManagerConfig& config) noexcept
    : config_(config), configValid_(validateConfig(config)) {}

bool RiskManager::validateConfig(
    const RiskManagerConfig& config) noexcept {
    if (!config.approved || !config.hashVerified || config.version == 0U ||
        !hasNonZeroHash(config.hashSha256) || !config.hysteresisConfigured ||
        config.promotionSamples == 0U || config.recoverySamples == 0U ||
        (!config.modelRuleEnabled && !config.tiltRuleEnabled)) {
        return false;
    }

    if (config.modelRuleEnabled) {
        if (config.expectedModelOutputCount == 0U ||
            config.expectedModelOutputCount >
                project::models::ModelOutput::kMaxOutputs ||
            !std::isfinite(config.scoreMinimum) ||
            !std::isfinite(config.scoreMaximum) ||
            !std::isfinite(config.scoreSafeBoundary) ||
            !std::isfinite(config.scoreMonitorBoundary) ||
            config.scoreMinimum >= config.scoreMaximum) {
            return false;
        }
        if (config.scoreDirection ==
            RiskScoreDirection::HigherIsMoreSevere) {
            if (!(config.scoreMinimum < config.scoreSafeBoundary &&
                  config.scoreSafeBoundary < config.scoreMonitorBoundary &&
                  config.scoreMonitorBoundary < config.scoreMaximum)) {
                return false;
            }
        } else if (config.scoreDirection ==
                   RiskScoreDirection::LowerIsMoreSevere) {
            if (!(config.scoreMinimum < config.scoreMonitorBoundary &&
                  config.scoreMonitorBoundary < config.scoreSafeBoundary &&
                  config.scoreSafeBoundary < config.scoreMaximum)) {
                return false;
            }
        } else {
            return false;
        }
    }

    if (config.tiltRuleEnabled &&
        (!std::isfinite(config.tiltSafeMaximumDeg) ||
         !std::isfinite(config.tiltMonitorMaximumDeg) ||
         config.tiltSafeMaximumDeg < 0.0F ||
         config.tiltSafeMaximumDeg >= config.tiltMonitorMaximumDeg)) {
        return false;
    }
    return true;
}

std::uint8_t RiskManager::severity(Candidate candidate) noexcept {
    switch (candidate) {
        case Candidate::Safe:
            return 0U;
        case Candidate::Monitor:
            return 1U;
        case Candidate::HighRiskTest:
            return 2U;
        case Candidate::Unknown:
            return 0U;
    }
    return 0U;
}

RiskManager::Candidate RiskManager::classifyModel(
    const project::models::ModelOutput& model,
    bool& scoreValid) const noexcept {
    scoreValid = false;
    if (!config_.modelRuleEnabled ||
        model.status != project::models::InferenceStatus::Ok ||
        !model.predictionValid ||
        model.outputCount != config_.expectedModelOutputCount ||
        !std::isfinite(model.predictionValue) ||
        model.predictionValue < config_.scoreMinimum ||
        model.predictionValue > config_.scoreMaximum) {
        return Candidate::Unknown;
    }
    scoreValid = true;

    if (config_.scoreDirection ==
        RiskScoreDirection::HigherIsMoreSevere) {
        if (model.predictionValue <= config_.scoreSafeBoundary) {
            return Candidate::Safe;
        }
        if (model.predictionValue <= config_.scoreMonitorBoundary) {
            return Candidate::Monitor;
        }
        return Candidate::HighRiskTest;
    }

    if (model.predictionValue >= config_.scoreSafeBoundary) {
        return Candidate::Safe;
    }
    if (model.predictionValue >= config_.scoreMonitorBoundary) {
        return Candidate::Monitor;
    }
    return Candidate::HighRiskTest;
}

RiskManager::Candidate RiskManager::classifyTilt(
    const TiltValidation& tilt) const noexcept {
    if (!config_.tiltRuleEnabled ||
        tilt.status != TiltValidationStatus::Valid ||
        !tilt.definitionApproved || !std::isfinite(tilt.deviationDeg)) {
        return Candidate::Unknown;
    }

    const float absoluteDeviation = std::fabs(tilt.deviationDeg);
    if (absoluteDeviation <= config_.tiltSafeMaximumDeg) {
        return Candidate::Safe;
    }
    if (absoluteDeviation <= config_.tiltMonitorMaximumDeg) {
        return Candidate::Monitor;
    }
    return Candidate::HighRiskTest;
}

void RiskManager::setAssessment(
    project::models::RiskAssessment& output, Candidate candidate,
    bool scoreValid, float score,
    const RiskEvaluationContext& context) noexcept {
    const bool newBoot = hasLastSequence_ && context.bootId != lastBootId_;
    const bool outOfOrder =
        hasLastSequence_ && !newBoot &&
        context.sequenceNo < lastSequenceNo_;
    const bool newSample =
        !hasLastSequence_ || newBoot ||
        context.sequenceNo != lastSequenceNo_;

    if (newBoot || outOfOrder) {
        current_ = Candidate::Unknown;
        pending_ = Candidate::Unknown;
        pendingSamples_ = 0U;
        pendingSinceMs_ = 0U;
        stateEnterMs_ = context.timestampMs;
    }

    if (candidate == Candidate::Unknown || outOfOrder) {
        const bool changedToUnknown = current_ != Candidate::Unknown;
        current_ = Candidate::Unknown;
        pending_ = Candidate::Unknown;
        pendingSamples_ = 0U;
        pendingSinceMs_ = 0U;
        if (changedToUnknown || stateEnterMs_ == 0U || outOfOrder) {
            stateEnterMs_ = context.timestampMs;
        }
    } else if (candidate == current_) {
        pending_ = Candidate::Unknown;
        pendingSamples_ = 0U;
        pendingSinceMs_ = 0U;
    } else {
        if (pending_ != candidate) {
            pending_ = candidate;
            pendingSamples_ = 0U;
            pendingSinceMs_ = context.timestampMs;
        }
        if (newSample) {
            incrementSaturated(pendingSamples_);
        }

        const bool promotion =
            current_ == Candidate::Unknown ||
            severity(candidate) > severity(current_);
        const std::uint16_t requiredSamples =
            promotion ? config_.promotionSamples : config_.recoverySamples;
        const bool recoveryTimeElapsed =
            promotion ||
            (context.timestampMs >= pendingSinceMs_ &&
             context.timestampMs - pendingSinceMs_ >=
                 config_.recoveryHoldMs);
        if (pendingSamples_ >= requiredSamples && recoveryTimeElapsed) {
            current_ = candidate;
            stateEnterMs_ = context.timestampMs;
            pending_ = Candidate::Unknown;
            pendingSamples_ = 0U;
            pendingSinceMs_ = 0U;
        }
    }

    if (!hasLastSequence_ || newBoot || newSample || outOfOrder) {
        lastBootId_ = context.bootId;
        lastSequenceNo_ = context.sequenceNo;
        hasLastSequence_ = true;
    }

    output = {};
    output.bootId = context.bootId;
    output.sequenceNo = context.sequenceNo;
    output.timestampMs = context.timestampMs;
    output.health = context.health;
    output.stateEnterMs = stateEnterMs_;
    output.persistenceMs = elapsedMs(context.timestampMs, stateEnterMs_);
    output.riskConfigVersion = configValid_ ? config_.version : 0U;
    output.activeConfigHashValid = configValid_;
    if (configValid_) {
        for (std::size_t i = 0; i < config_.hashSha256.size(); ++i) {
            output.activeConfigHashSha256[i] = config_.hashSha256[i];
        }
    }
    output.riskScoreValid = scoreValid && configValid_;
    if (output.riskScoreValid) {
        output.riskScore = score;
    }

    switch (current_) {
        case Candidate::Safe:
            output.risk = project::models::RiskState::Safe;
            break;
        case Candidate::Monitor:
            output.risk = project::models::RiskState::Monitor;
            break;
        case Candidate::HighRiskTest:
            output.risk = project::models::RiskState::HighRiskTest;
            break;
        case Candidate::Unknown:
            output.risk = project::models::RiskState::Unknown;
            break;
    }
    output.valid = true;
}

bool RiskManager::evaluate(
    const project::models::ModelOutput& model, const TiltValidation& tilt,
    project::models::DataQualityState quality,
    const RiskEvaluationContext& context,
    project::models::RiskAssessment& output) noexcept {
    Candidate candidate = Candidate::Unknown;
    bool scoreValid = false;
    float score = 0.0F;

    if (configValid_ && context.bootId != 0U &&
        context.sequenceNo != 0U) {
        Candidate modelCandidate = Candidate::Unknown;
        if (context.health != project::models::HealthState::Fault &&
            config_.modelRuleEnabled &&
            quality == project::models::DataQualityState::VALID) {
            modelCandidate = classifyModel(model, scoreValid);
            if (scoreValid) {
                score = model.predictionValue;
            }
        }
        Candidate tiltCandidate = classifyTilt(tilt);
        if (quality != project::models::DataQualityState::VALID &&
            tiltCandidate != Candidate::HighRiskTest) {
            tiltCandidate = Candidate::Unknown;
        }
        if (scoreValid && tiltCandidate != Candidate::Unknown) {
            candidate = severity(modelCandidate) >= severity(tiltCandidate)
                           ? modelCandidate
                           : tiltCandidate;
        } else if (scoreValid) {
            if (!config_.tiltRuleEnabled ||
                config_.modelOnlyFallbackApproved) {
                candidate = modelCandidate;
            }
        } else if (tiltCandidate != Candidate::Unknown) {
            if (!config_.modelRuleEnabled ||
                config_.tiltOnlyFallbackApproved ||
                tiltCandidate == Candidate::HighRiskTest) {
                candidate = tiltCandidate;
            }
        }
    }

    setAssessment(output, candidate, scoreValid, score, context);
    return true;
}

bool RiskManager::evaluate(
    const project::models::ModelOutput& model,
    const project::models::ProcessedFrame& frame,
    const project::models::HealthSnapshot& health,
    project::models::RiskAssessment& output) noexcept {
    const bool qualityValid = frame.aggregateStatus.valid;
    const RiskEvaluationContext context{frame.bootId, frame.sequence,
                                        frame.timestampMs, health.healthState};
    const TiltValidation tilt{};
    bool schemaHashPresent = false;
    for (const std::uint8_t byte : model.sourceFeatureSchemaHashSha256) {
        schemaHashPresent = schemaHashPresent || byte != 0U;
    }
    const bool provenanceMatches =
        model.bootId == frame.bootId &&
        model.relatedSequenceNo == frame.sequence &&
        model.sourceWindowSampleCount > 0U &&
        model.sourceWindowStartMs <= model.sourceWindowEndMs &&
        model.sourceWindowEndMs == frame.timestampMs &&
        model.sourceFeatureSchemaVersion != 0U &&
        model.sourceFeatureSchemaId != 0U &&
        model.sourceFeatureSchemaHashValid && schemaHashPresent;
    if (!provenanceMatches) {
        setAssessment(output, Candidate::Unknown, false, 0.0F, context);
        return true;
    }
    if (!qualityValid) {
        setAssessment(output, Candidate::Unknown, false, 0.0F, context);
        return true;
    }
    return evaluate(
        model, tilt, project::models::DataQualityState::VALID, context,
        output);
}

}  // namespace project::managers
