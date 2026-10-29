#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "AI/IInferenceEngine.h"

namespace project::ai {

enum class EdgeImpulseOutputKind : std::uint8_t {
    Classification,
    Regression
};

struct EdgeImpulseModelConfig {
    static constexpr std::size_t kHashSize = 32U;

    std::uint32_t featureSchemaVersion{0U};
    std::uint32_t featureSchemaId{0U};
    std::array<std::uint8_t, kHashSize> featureSchemaHash{};
    std::array<std::uint8_t, kHashSize> modelHash{};

    std::uint16_t inputDimension{0U};
    std::uint16_t outputCount{0U};
    EdgeImpulseOutputKind outputKind{
        EdgeImpulseOutputKind::Classification};

    bool featureSchemaApproved{false};
    bool modelMetadataVerified{false};
    bool modelHashVerified{false};
    bool outputContractApproved{false};
};

class EdgeImpulseAdapter final : public IModelRuntime {
public:
    explicit EdgeImpulseAdapter(
        const EdgeImpulseModelConfig& config =
            EdgeImpulseModelConfig{}) noexcept;

    bool initialize() override;
    bool run(const project::models::FeatureVector& input,
             project::models::ModelOutput& output) override;

    bool getModelMetadata() const noexcept;
    std::size_t getInputDimension() const noexcept;

private:
    static bool isValidInput(
        const project::models::FeatureVector& input,
        const EdgeImpulseModelConfig& config) noexcept;
    static bool hasNonZeroHash(
        const std::array<std::uint8_t, EdgeImpulseModelConfig::kHashSize>&
            hash) noexcept;
    static std::uint64_t monotonicMillis() noexcept;
    static std::uint32_t monotonicMicros() noexcept;

#if defined(PROJECT_EDGE_IMPULSE_MODEL_HEADER)
    static int getSignalData(std::size_t offset, std::size_t length,
                             float* output) noexcept;
#endif

    const EdgeImpulseModelConfig config_;
    bool initialized_{false};
};

}  // namespace project::ai
