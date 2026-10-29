#include "project/AI/EdgeImpulseAdapter.h"

#include <cmath>
#include <cstring>

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#else
#include <chrono>
#endif

#if defined(PROJECT_EDGE_IMPULSE_MODEL_HEADER)
#include PROJECT_EDGE_IMPULSE_MODEL_HEADER
#define PROJECT_HAS_EDGE_IMPULSE_MODEL 1
#else
#define PROJECT_HAS_EDGE_IMPULSE_MODEL 0
#endif

namespace project::ai {
namespace {

#if PROJECT_HAS_EDGE_IMPULSE_MODEL
const project::models::FeatureVector* gActiveInput = nullptr;

class ActiveInputGuard final {
public:
    explicit ActiveInputGuard(
        const project::models::FeatureVector& input) noexcept {
        gActiveInput = &input;
    }

    ~ActiveInputGuard() { gActiveInput = nullptr; }

    ActiveInputGuard(const ActiveInputGuard&) = delete;
    ActiveInputGuard& operator=(const ActiveInputGuard&) = delete;
};
#endif

}  // namespace

EdgeImpulseAdapter::EdgeImpulseAdapter(
    const EdgeImpulseModelConfig& config) noexcept
    : config_(config) {}

bool EdgeImpulseAdapter::hasNonZeroHash(
    const std::array<std::uint8_t, EdgeImpulseModelConfig::kHashSize>&
        hash) noexcept {
    for (const std::uint8_t byte : hash) {
        if (byte != 0U) {
            return true;
        }
    }
    return false;
}

bool EdgeImpulseAdapter::isValidInput(
    const project::models::FeatureVector& input,
    const EdgeImpulseModelConfig& config) noexcept {
    if (!input.valid || input.count == 0U ||
        input.count > project::models::FeatureVector::kMaxFeatures ||
        input.count != config.inputDimension ||
        input.schemaVersion != config.featureSchemaVersion ||
        input.schemaId != config.featureSchemaId ||
        !input.schemaHashValid ||
        input.bootId == 0U ||
        input.relatedSequenceNo == 0U ||
        input.windowStartMs > input.windowEndMs ||
        input.windowSampleCount == 0U) {
        return false;
    }

    const std::uint32_t expectedMask =
        input.count == project::models::FeatureVector::kMaxFeatures
            ? UINT32_MAX
            : (UINT32_C(1) << input.count) - 1U;
    if (input.featureValidMask != expectedMask) {
        return false;
    }
    if (std::memcmp(input.schemaHashSha256, config.featureSchemaHash.data(),
                    config.featureSchemaHash.size()) != 0) {
        return false;
    }
    for (std::size_t i = 0U; i < input.count; ++i) {
        if (!std::isfinite(input.values[i])) {
            return false;
        }
    }
    return true;
}

std::uint64_t EdgeImpulseAdapter::monotonicMillis() noexcept {
#if defined(ARDUINO_ARCH_ESP32)
    return static_cast<std::uint64_t>(::millis());
#else
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
#endif
}

std::uint32_t EdgeImpulseAdapter::monotonicMicros() noexcept {
#if defined(ARDUINO_ARCH_ESP32)
    return static_cast<std::uint32_t>(::micros());
#else
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now().time_since_epoch();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
#endif
}

bool EdgeImpulseAdapter::initialize() {
    initialized_ = false;
#if PROJECT_HAS_EDGE_IMPULSE_MODEL
    if (!config_.featureSchemaApproved || !config_.modelMetadataVerified ||
        !config_.modelHashVerified || !config_.outputContractApproved ||
        config_.featureSchemaVersion == 0U || config_.featureSchemaId == 0U ||
        !hasNonZeroHash(config_.featureSchemaHash) ||
        !hasNonZeroHash(config_.modelHash) || config_.inputDimension == 0U ||
        config_.inputDimension >
            project::models::FeatureVector::kMaxFeatures ||
        config_.outputCount == 0U ||
        config_.outputCount > project::models::ModelOutput::kMaxOutputs) {
        return false;
    }
    if (config_.inputDimension != EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        return false;
    }
    if (config_.outputCount != EI_CLASSIFIER_LABEL_COUNT) {
        return false;
    }
    switch (config_.outputKind) {
        case EdgeImpulseOutputKind::Classification:
            break;
        case EdgeImpulseOutputKind::Regression:
            if (config_.outputCount != 1U) {
                return false;
            }
            break;
        default:
            return false;
    }
    initialized_ = true;
    return true;
#else
    return false;
#endif
}

bool EdgeImpulseAdapter::run(
    const project::models::FeatureVector& input,
    project::models::ModelOutput& output) {
    output = {};
    output.bootId = input.bootId;
    output.relatedSequenceNo = input.relatedSequenceNo;
    output.sourceWindowStartMs = input.windowStartMs;
    output.sourceWindowEndMs = input.windowEndMs;
    output.sourceWindowSampleCount = input.windowSampleCount;
    output.sourceFeatureSchemaVersion = input.schemaVersion;
    output.sourceFeatureSchemaId = input.schemaId;
    for (std::size_t i = 0U; i < sizeof(input.schemaHashSha256); ++i) {
        output.sourceFeatureSchemaHashSha256[i] =
            input.schemaHashSha256[i];
    }
    output.sourceFeatureSchemaHashValid = input.schemaHashValid;
    output.timestampMs = monotonicMillis();

    if (!initialized_) {
        output.status = project::models::InferenceStatus::ModelError;
        return false;
    }
    if (!isValidInput(input, config_)) {
        output.status = project::models::InferenceStatus::InvalidInput;
        return true;
    }

#if PROJECT_HAS_EDGE_IMPULSE_MODEL
    signal_t signal{};
    signal.total_length = input.count;
    signal.get_data = &EdgeImpulseAdapter::getSignalData;
    ei_impulse_result_t result{};

    output.inferenceStartMs = output.timestampMs;
    const std::uint32_t inferenceStartUs = monotonicMicros();
    ActiveInputGuard activeInput(input);
    const EI_IMPULSE_ERROR inferenceStatus =
        run_classifier(&signal, &result, false);
    const std::uint32_t elapsedUs =
        static_cast<std::uint32_t>(monotonicMicros() - inferenceStartUs);
    output.inferenceLatencyUs = elapsedUs;
    output.inferenceDurationMs = elapsedUs / 1000U;

    if (inferenceStatus != EI_IMPULSE_OK) {
        output.status = project::models::InferenceStatus::ModelError;
        return true;
    }

    if (config_.outputCount > project::models::ModelOutput::kMaxOutputs) {
        output.status = project::models::InferenceStatus::ModelError;
        return true;
    }
    for (std::size_t i = 0U; i < config_.outputCount; ++i) {
        const float value =
            static_cast<float>(result.classification[i].value);
        if (!std::isfinite(value)) {
            output.outputCount = 0U;
            output.status =
                project::models::InferenceStatus::NonFiniteOutput;
            return true;
        }
        output.rawOutputs[i] = value;
    }
    output.outputCount = config_.outputCount;
    output.status = project::models::InferenceStatus::Ok;

    if (config_.outputKind == EdgeImpulseOutputKind::Regression) {
        output.predictionValue = output.rawOutputs[0];
        output.predictionValid = std::isfinite(output.predictionValue);
    }
    for (std::size_t i = 0U; i < config_.modelHash.size(); ++i) {
        output.modelHashSha256[i] = config_.modelHash[i];
    }
    output.modelHashValid = config_.modelHashVerified;
    return true;
#else
    output.status = project::models::InferenceStatus::ModelError;
    return false;
#endif
}

bool EdgeImpulseAdapter::getModelMetadata() const noexcept {
    return initialized_ && config_.modelMetadataVerified &&
           config_.modelHashVerified && config_.outputContractApproved;
}

std::size_t EdgeImpulseAdapter::getInputDimension() const noexcept {
    return initialized_ ? config_.inputDimension : 0U;
}

#if PROJECT_HAS_EDGE_IMPULSE_MODEL
int EdgeImpulseAdapter::getSignalData(std::size_t offset,
                                      std::size_t length,
                                      float* output) noexcept {
    if (gActiveInput == nullptr || output == nullptr ||
        offset > gActiveInput->count ||
        length > static_cast<std::size_t>(gActiveInput->count) - offset) {
        return -1;
    }
    for (std::size_t i = 0U; i < length; ++i) {
        output[i] = gActiveInput->values[offset + i];
    }
    return 0;
}
#endif

}  // namespace project::ai

#undef PROJECT_HAS_EDGE_IMPULSE_MODEL
