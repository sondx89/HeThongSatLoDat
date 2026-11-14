#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "FeatureCsvExporter is available only to host data tools"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Dataset/CsvArtifactExporter.h"
#include "Models/FeatureVector.h"

namespace project::dataset {

inline constexpr std::uint32_t kFeatureCsvSchemaVersion = 1U;
inline constexpr std::size_t kMaxFeatureCsvDimension =
    project::models::FeatureVector::kMaxFeatures;

struct FeatureCsvSlotDescriptor {
    std::uint16_t order{0U};
    std::string_view stableId{};
    std::string_view name{};
    std::string_view unit{};
    std::string_view transformId{};
    std::uint32_t transformVersion{0U};
};

struct FeatureCsvSchema {
    std::uint32_t featureSchemaId{0U};
    std::uint32_t featureSchemaVersion{0U};
    std::array<std::uint8_t, Sha256::kDigestSize> featureSchemaSha256{};
    std::string_view featureSchemaName{};
    std::string_view modelInputKind{};
    std::array<FeatureCsvSlotDescriptor, kMaxFeatureCsvDimension> slots{};
    std::size_t slotCount{0U};
    bool featureContractApproved{false};
    bool modelMetadataVerified{false};
    bool modelSchemaCompatible{false};
};

struct FeatureCsvRecord {
    std::string_view datasetId{};
    std::string_view windowId{};
    const project::models::FeatureVector* vector{nullptr};
    const project::models::DataQualityState* featureQuality{nullptr};
    std::size_t featureQualityCount{0U};
};

enum class FeatureCsvExportStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    InvalidSchema,
    InvalidRecord,
    InvalidState,
    SchemaMismatch,
    InvalidFeature,
    CsvWriteError,
    CapacityExceeded,
    HashError,
    RowCountOverflow
};

class FeatureCsvExporter final {
public:
    FeatureCsvExporter(char* output, std::size_t outputCapacity,
                       char* rowBuffer, std::size_t rowCapacity,
                       const FeatureCsvSchema& schema) noexcept;

    FeatureCsvExportStatus begin() noexcept;
    FeatureCsvExportStatus append(const FeatureCsvRecord& record) noexcept;
    FeatureCsvExportStatus finish(CsvArtifactExportReport& report) noexcept;

    std::size_t bytesWritten() const noexcept {
        return artifactWriter_.bytesWritten();
    }
    std::uint64_t rowsWritten() const noexcept {
        return artifactWriter_.rowsWritten();
    }

private:
    class ColumnSet final {
    public:
        explicit ColumnSet(std::size_t featureCount) noexcept;
        bool valid() const noexcept { return valid_; }
        CsvArtifactSchema artifactSchema() const noexcept;

    private:
        static constexpr std::size_t kBaseColumnCount = 19U;
        static constexpr std::size_t kFeatureColumnCount = 9U;
        static constexpr std::size_t kMaxColumnCount =
            kBaseColumnCount +
            kMaxFeatureCsvDimension * kFeatureColumnCount;

        std::array<std::array<char, 40U>,
                   kMaxFeatureCsvDimension * kFeatureColumnCount>
            featureColumnNames_{};
        std::array<std::string_view, kMaxColumnCount> columns_{};
        std::size_t columnCount_{0U};
        bool valid_{false};
    };

    const FeatureCsvSchema schema_;
    ColumnSet columns_;
    CsvArtifactWriter artifactWriter_;
};

}  // namespace project::dataset
