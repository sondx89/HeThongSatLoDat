#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/FeatureCsvExporter.h"

#include <charconv>
#include <cmath>
#include <cstring>

namespace project::dataset {
namespace {

constexpr std::array<std::string_view, 19U> kFeatureCsvBaseColumns{{
    "dataset_id",
    "window_id",
    "boot_id",
    "related_sequence_no",
    "window_start_ms",
    "window_end_ms",
    "window_sample_count",
    "feature_schema_id",
    "feature_schema_version",
    "feature_schema_sha256",
    "model_input_kind",
    "feature_contract_approved",
    "model_metadata_verified",
    "model_schema_compatible",
    "export_kind",
    "feature_count",
    "valid_feature_count",
    "model_input_eligible",
    "eligibility_reason",
}};

constexpr std::array<std::string_view, 9U> kFeatureCsvSlotSuffixes{{
    "order",
    "stable_id",
    "name",
    "unit",
    "transform_id",
    "transform_version",
    "valid",
    "quality",
    "value",
}};

constexpr std::string_view kSchemaHashDigits =
    "0123456789abcdef";

bool validText(std::string_view text) noexcept {
    return !text.empty() && text.find('\0') == std::string_view::npos;
}

bool hasNonZeroHash(
    const std::array<std::uint8_t, Sha256::kDigestSize>& hash) noexcept {
    for (const std::uint8_t byte : hash) {
        if (byte != 0U) {
            return true;
        }
    }
    return false;
}

void formatHash(
    const std::array<std::uint8_t, Sha256::kDigestSize>& hash,
    char (&output)[Sha256::kDigestSize * 2U]) noexcept {
    for (std::size_t index = 0U; index < hash.size(); ++index) {
        output[index * 2U] = kSchemaHashDigits[hash[index] >> 4U];
        output[index * 2U + 1U] =
            kSchemaHashDigits[hash[index] & 0x0FU];
    }
}

std::string_view qualityName(
    project::models::DataQualityState quality) noexcept {
    using project::models::DataQualityState;
    switch (quality) {
        case DataQualityState::MISSING: return "MISSING";
        case DataQualityState::VALID: return "VALID";
        case DataQualityState::STALE: return "STALE";
        case DataQualityState::OUT_OF_RANGE: return "OUT_OF_RANGE";
        case DataQualityState::NON_FINITE: return "NON_FINITE";
        case DataQualityState::CALIBRATION_INVALID:
            return "CALIBRATION_INVALID";
        case DataQualityState::OUT_OF_ORDER: return "OUT_OF_ORDER";
        case DataQualityState::GAP_DETECTED: return "GAP_DETECTED";
        case DataQualityState::NOT_READY: return "NOT_READY";
        case DataQualityState::SENSOR_ERROR: return "SENSOR_ERROR";
    }
    return {};
}

bool metadataKnown(const FeatureCsvSchema& schema) noexcept {
    if (!validText(schema.featureSchemaName) ||
        schema.featureSchemaName == "UNKNOWN" ||
        !validText(schema.modelInputKind) ||
        schema.modelInputKind == "UNKNOWN") {
        return false;
    }
    for (std::size_t index = 0U; index < schema.slotCount; ++index) {
        const FeatureCsvSlotDescriptor& slot = schema.slots[index];
        if (!validText(slot.stableId) || slot.stableId == "UNKNOWN" ||
            !validText(slot.name) || slot.name == "UNKNOWN" ||
            !validText(slot.unit) || slot.unit == "UNKNOWN" ||
            !validText(slot.transformId) || slot.transformId == "UNKNOWN" ||
            slot.transformVersion == 0U) {
            return false;
        }
    }
    return true;
}

bool validSchema(const FeatureCsvSchema& schema) noexcept {
    if (schema.featureSchemaId == 0U ||
        schema.featureSchemaVersion == 0U ||
        !hasNonZeroHash(schema.featureSchemaSha256) ||
        schema.slotCount == 0U ||
        schema.slotCount > kMaxFeatureCsvDimension) {
        return false;
    }
    for (std::size_t index = 0U; index < schema.slotCount; ++index) {
        const FeatureCsvSlotDescriptor& slot = schema.slots[index];
        if (slot.order != index || !validText(slot.stableId) ||
            !validText(slot.name) || !validText(slot.unit) ||
            !validText(slot.transformId)) {
            return false;
        }
    }
    return true;
}

std::string_view eligibilityReason(
    const FeatureCsvSchema& schema, const project::models::FeatureVector& vector,
    std::size_t validFeatureCount) noexcept {
    if (!schema.featureContractApproved) {
        return "FSS_NOT_APPROVED";
    }
    if (!schema.modelMetadataVerified) {
        return "MODEL_METADATA_UNVERIFIED";
    }
    if (!schema.modelSchemaCompatible) {
        return "MODEL_SCHEMA_NOT_COMPATIBLE";
    }
    if (!metadataKnown(schema)) {
        return "FEATURE_METADATA_INCOMPLETE";
    }
    if (vector.quality.eligibility !=
        project::models::WindowEligibilityState::Eligible) {
        return "WINDOW_NOT_ELIGIBLE";
    }
    if (validFeatureCount != vector.count) {
        return "INVALID_FEATURES";
    }
    return {};
}

FeatureCsvExportStatus mapArtifactStatus(
    CsvArtifactExportStatus status) noexcept {
    switch (status) {
        case CsvArtifactExportStatus::Ok:
            return FeatureCsvExportStatus::Ok;
        case CsvArtifactExportStatus::InvalidArgument:
            return FeatureCsvExportStatus::InvalidArgument;
        case CsvArtifactExportStatus::InvalidSchema:
            return FeatureCsvExportStatus::InvalidSchema;
        case CsvArtifactExportStatus::CsvWriteError:
            return FeatureCsvExportStatus::CsvWriteError;
        case CsvArtifactExportStatus::InvalidState:
            return FeatureCsvExportStatus::InvalidState;
        case CsvArtifactExportStatus::CapacityExceeded:
            return FeatureCsvExportStatus::CapacityExceeded;
        case CsvArtifactExportStatus::HashError:
            return FeatureCsvExportStatus::HashError;
        case CsvArtifactExportStatus::RowCountOverflow:
            return FeatureCsvExportStatus::RowCountOverflow;
    }
    return FeatureCsvExportStatus::CsvWriteError;
}

}  // namespace

FeatureCsvExporter::ColumnSet::ColumnSet(
    std::size_t featureCount) noexcept {
    if (featureCount == 0U || featureCount > kMaxFeatureCsvDimension) {
        return;
    }
    for (const std::string_view column : kFeatureCsvBaseColumns) {
        columns_[columnCount_++] = column;
    }

    for (std::size_t featureIndex = 0U; featureIndex < featureCount;
         ++featureIndex) {
        for (std::size_t suffixIndex = 0U;
             suffixIndex < kFeatureCsvSlotSuffixes.size(); ++suffixIndex) {
            const std::string_view suffix =
                kFeatureCsvSlotSuffixes[suffixIndex];
            std::array<char, 40U>& name =
                featureColumnNames_[featureIndex *
                                        kFeatureColumnCount +
                                    suffixIndex];
            char* cursor = name.data();
            constexpr std::string_view prefix = "feature_";
            std::memcpy(cursor, prefix.data(), prefix.size());
            cursor += prefix.size();
            const auto number = std::to_chars(
                cursor, name.data() + name.size() - 1U, featureIndex);
            if (number.ec != std::errc{}) {
                return;
            }
            cursor = number.ptr;
            *cursor++ = '_';
            const std::size_t used =
                static_cast<std::size_t>(cursor - name.data());
            if (used >= name.size() ||
                suffix.size() > name.size() - used - 1U) {
                return;
            }
            std::memcpy(cursor, suffix.data(), suffix.size());
            cursor += suffix.size();
            *cursor = '\0';
            columns_[columnCount_++] = std::string_view(
                name.data(), static_cast<std::size_t>(cursor - name.data()));
        }
    }
    valid_ = true;
}

CsvArtifactSchema FeatureCsvExporter::ColumnSet::artifactSchema()
    const noexcept {
    return {"features.csv", kFeatureCsvSchemaVersion, columns_.data(),
            columnCount_};
}

FeatureCsvExporter::FeatureCsvExporter(
    char* output, std::size_t outputCapacity, char* rowBuffer,
    std::size_t rowCapacity, const FeatureCsvSchema& schema) noexcept
    : schema_(schema),
      columns_(schema.slotCount),
      artifactWriter_(output, outputCapacity, rowBuffer, rowCapacity,
                      columns_.artifactSchema()) {}

FeatureCsvExportStatus FeatureCsvExporter::begin() noexcept {
    if (!columns_.valid() || !validSchema(schema_)) {
        return FeatureCsvExportStatus::InvalidSchema;
    }
    return mapArtifactStatus(artifactWriter_.begin());
}

FeatureCsvExportStatus FeatureCsvExporter::append(
    const FeatureCsvRecord& record) noexcept {
    if (record.vector == nullptr || record.featureQuality == nullptr ||
        !validText(record.datasetId) || !validText(record.windowId)) {
        return FeatureCsvExportStatus::InvalidArgument;
    }
    if (artifactWriter_.rowsWritten() == UINT64_MAX) {
        return FeatureCsvExportStatus::RowCountOverflow;
    }

    const project::models::FeatureVector& vector = *record.vector;
    if (!vector.valid || vector.count == 0U ||
        vector.count > kMaxFeatureCsvDimension ||
        record.featureQualityCount != vector.count ||
        vector.count != schema_.slotCount ||
        vector.windowSampleCount == 0U || vector.bootId == 0U ||
        vector.relatedSequenceNo == 0U ||
        vector.windowStartMs > vector.windowEndMs) {
        return FeatureCsvExportStatus::InvalidRecord;
    }
    if (vector.schemaId != schema_.featureSchemaId ||
        vector.schemaVersion != schema_.featureSchemaVersion ||
        !vector.schemaHashValid ||
        std::memcmp(vector.schemaHashSha256,
                    schema_.featureSchemaSha256.data(),
                    schema_.featureSchemaSha256.size()) != 0) {
        return FeatureCsvExportStatus::SchemaMismatch;
    }
    const std::uint32_t expectedMask =
        vector.count == kMaxFeatureCsvDimension
            ? UINT32_MAX
            : (UINT32_C(1) << vector.count) - 1U;
    if ((vector.featureValidMask & ~expectedMask) != 0U) {
        return FeatureCsvExportStatus::InvalidFeature;
    }

    std::size_t validFeatureCount = 0U;
    for (std::size_t index = 0U; index < vector.count; ++index) {
        const bool valid =
            (vector.featureValidMask & (UINT32_C(1) << index)) != 0U;
        const project::models::DataQualityState quality =
            record.featureQuality[index];
        if (qualityName(quality).empty() ||
            (valid && (quality != project::models::DataQualityState::VALID ||
                       !std::isfinite(vector.values[index]))) ||
            (!valid && quality == project::models::DataQualityState::VALID)) {
            return FeatureCsvExportStatus::InvalidFeature;
        }
        if (valid) {
            ++validFeatureCount;
        }
    }

    const std::string_view reason =
        eligibilityReason(schema_, vector, validFeatureCount);
    const bool eligible = reason.empty();
    char hashText[Sha256::kDigestSize * 2U]{};
    formatHash(schema_.featureSchemaSha256, hashText);
    const CsvArtifactExportStatus appendStatus =
        artifactWriter_.appendRow(
            [&record, &vector, this, eligible, reason, validFeatureCount,
             &hashText](CsvRowWriter& writer) noexcept {
                CsvWriteStatus status = writer.appendField(record.datasetId);
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendField(record.windowId);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendUnsigned(vector.bootId);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendUnsigned(vector.relatedSequenceNo);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendUnsigned(vector.windowStartMs);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendUnsigned(vector.windowEndMs);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendUnsigned(vector.windowSampleCount);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendUnsigned(schema_.featureSchemaId);
                }
                if (status == CsvWriteStatus::Ok) {
                    status =
                        writer.appendUnsigned(schema_.featureSchemaVersion);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendField(std::string_view(
                        hashText, sizeof(hashText)));
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendField(schema_.modelInputKind);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendBoolean(
                        schema_.featureContractApproved);
                }
                if (status == CsvWriteStatus::Ok) {
                    status =
                        writer.appendBoolean(schema_.modelMetadataVerified);
                }
                if (status == CsvWriteStatus::Ok) {
                    status =
                        writer.appendBoolean(schema_.modelSchemaCompatible);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendField(
                        eligible ? "PRODUCTION_MODEL_INPUT"
                                 : "CANDIDATE_ONLY");
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendUnsigned(vector.count);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendUnsigned(validFeatureCount);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendBoolean(eligible);
                }
                if (status == CsvWriteStatus::Ok) {
                    status = writer.appendField(reason);
                }
                for (std::size_t index = 0U;
                     status == CsvWriteStatus::Ok && index < vector.count;
                     ++index) {
                    const FeatureCsvSlotDescriptor& slot =
                        schema_.slots[index];
                    const bool valid =
                        (vector.featureValidMask &
                         (UINT32_C(1) << index)) != 0U;
                    status = writer.appendUnsigned(slot.order);
                    if (status == CsvWriteStatus::Ok) {
                        status = writer.appendField(slot.stableId);
                    }
                    if (status == CsvWriteStatus::Ok) {
                        status = writer.appendField(slot.name);
                    }
                    if (status == CsvWriteStatus::Ok) {
                        status = writer.appendField(slot.unit);
                    }
                    if (status == CsvWriteStatus::Ok) {
                        status = writer.appendField(slot.transformId);
                    }
                    if (status == CsvWriteStatus::Ok) {
                        status = writer.appendUnsigned(slot.transformVersion);
                    }
                    if (status == CsvWriteStatus::Ok) {
                        status = writer.appendBoolean(valid);
                    }
                    if (status == CsvWriteStatus::Ok) {
                        status = writer.appendField(
                            qualityName(record.featureQuality[index]));
                    }
                    if (status == CsvWriteStatus::Ok) {
                        status = valid
                                     ? writer.appendNumber(vector.values[index])
                                     : writer.appendEmptyField();
                    }
                }
                return status;
            });
    return mapArtifactStatus(appendStatus);
}

FeatureCsvExportStatus FeatureCsvExporter::finish(
    CsvArtifactExportReport& report) noexcept {
    return mapArtifactStatus(artifactWriter_.finish(report));
}

}  // namespace project::dataset

#endif
