#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "AnnotationManager is available only to host data tools"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Dataset/CsvWriter.h"
#include "Dataset/DataProvenance.h"

namespace project::dataset {

inline constexpr std::uint32_t kAnnotationSchemaVersion = 1U;
inline constexpr std::size_t kAnnotationCsvColumns = 24U;

template <std::size_t Capacity>
struct BoundedText {
    std::array<char, Capacity> bytes{};
    std::uint16_t size{0U};

    bool assign(std::string_view value) noexcept {
        if (value.size() >= Capacity ||
            value.size() > UINT16_MAX ||
            value.find('\0') != std::string_view::npos) {
            return false;
        }
        bytes.fill('\0');
        for (std::size_t i = 0U; i < value.size(); ++i) {
            bytes[i] = value[i];
        }
        size = static_cast<std::uint16_t>(value.size());
        return true;
    }

    std::string_view view() const noexcept {
        return {bytes.data(), size};
    }
};

enum class AnnotationStatus : std::uint8_t {
    Unlabeled,
    PendingReview,
    Provisional,
    Accepted,
    Uncertain,
    Conflict,
    Rejected,
    NotApplicable
};

enum class AnnotationKind : std::uint8_t {
    Independent,
    SyntheticProxy
};

struct AnnotationRecord {
    BoundedText<64U> annotationId{};
    BoundedText<64U> datasetId{};
    BoundedText<64U> siteId{};
    BoundedText<64U> sessionId{};
    std::uint64_t bootId{0U};
    std::uint64_t intervalStartMs{0U};
    std::uint64_t intervalEndMs{0U};
    BoundedText<64U> targetName{};
    double targetValue{0.0};
    bool targetValueValid{false};
    BoundedText<48U> targetUnit{};
    BoundedText<64U> targetContractId{};
    std::uint32_t targetContractVersion{0U};
    AnnotationStatus status{AnnotationStatus::Unlabeled};
    DataProvenance provenance{};
    BoundedText<192U> evidenceReference{};
    BoundedText<64U> annotator{};
    BoundedText<64U> reviewer{};
    double confidence{0.0};
    bool confidenceValid{false};
    double uncertainty{0.0};
    bool uncertaintyValid{false};
    BoundedText<64U> labelMethodVersion{};
    AnnotationKind kind{AnnotationKind::Independent};
};

struct AnnotationTargetContract {
    std::string_view id{};
    std::string_view name{};
    std::string_view unit{};
    std::uint32_t version{0U};
    bool approved{false};
};

struct AnnotationWindowInterval {
    std::string_view datasetId{};
    std::string_view siteId{};
    std::string_view sessionId{};
    std::uint64_t bootId{0U};
    std::uint64_t startMs{0U};
    std::uint64_t endMs{0U};
};

enum class AnnotationManagerStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    InvalidAnnotation,
    TargetContractNotApproved,
    DuplicateAnnotation,
    CapacityExceeded,
    CsvWriteError
};

struct AnnotationManagerReport {
    std::uint64_t accepted{0U};
    std::uint64_t rejected{0U};
    std::uint64_t syntheticProxy{0U};
    std::uint64_t gt5{0U};
};

class AnnotationManager final {
public:
    AnnotationManager(AnnotationRecord* records,
                      std::size_t capacity) noexcept;

    AnnotationManagerStatus add(
        const AnnotationRecord& record,
        const AnnotationTargetContract& targetContract) noexcept;

    std::size_t size() const noexcept { return size_; }
    const AnnotationRecord* record(std::size_t index) const noexcept;
    const AnnotationManagerReport& report() const noexcept { return report_; }

    AnnotationManagerStatus writeCsvHeader(
        char* output, std::size_t capacity,
        std::size_t& bytesWritten) const noexcept;
    AnnotationManagerStatus writeCsvRow(
        std::size_t index, char* output, std::size_t capacity,
        std::size_t& bytesWritten) const noexcept;

    static bool matchesWindow(
        const AnnotationRecord& annotation,
        const AnnotationWindowInterval& window) noexcept;

private:
    AnnotationManagerStatus reject(
        AnnotationManagerStatus status) noexcept;

    AnnotationRecord* const records_;
    const std::size_t capacity_;
    std::size_t size_{0U};
    AnnotationManagerReport report_{};
};

}  // namespace project::dataset
