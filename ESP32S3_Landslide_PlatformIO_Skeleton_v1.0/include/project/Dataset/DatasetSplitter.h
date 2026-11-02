#pragma once

#if !defined(PROJECT_HOST_DATA_TOOLS) || PROJECT_HOST_DATA_TOOLS != 1
#error "DatasetSplitter is available only to host data tools"
#endif

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Dataset/CsvWriter.h"
#include "Dataset/DataProvenance.h"

namespace project::dataset {

inline constexpr std::uint32_t kDatasetSplitSchemaVersion = 1U;
inline constexpr std::size_t kDatasetSplitCsvColumns = 8U;
inline constexpr std::size_t kMaximumSplitWindows = 1024U;
inline constexpr std::uint16_t kSplitBasisPointTotal = 10000U;

enum class DatasetSplit : std::uint8_t {
    Train,
    Validation,
    Test
};

struct DatasetSplitPolicy {
    std::uint32_t version{0U};
    std::uint64_t seed{0U};
    std::uint16_t trainBasisPoints{0U};
    std::uint16_t validationBasisPoints{0U};
    std::uint16_t testBasisPoints{0U};
};

struct DatasetWindowDescriptor {
    std::string_view windowId{};
    std::string_view datasetId{};
    std::string_view siteId{};
    std::string_view sessionId{};
    std::string_view splitGroupId{};
    std::uint64_t bootId{0U};
    std::uint64_t startSequence{0U};
    std::uint64_t endSequence{0U};
    DataProvenance provenance{};
};

struct DatasetSplitGroupEntry {
    std::string_view splitGroupId{};
    DataProvenance provenance{};
    DatasetSplit split{DatasetSplit::Train};
};

struct DatasetSplitWorkspace {
    DatasetSplitGroupEntry* groups{nullptr};
    std::size_t groupCapacity{0U};
    std::size_t groupCount{0U};
};

struct DatasetWindowSplitAssignment {
    std::string_view windowId{};
    std::string_view splitGroupId{};
    std::uint32_t policyVersion{0U};
    std::uint64_t seed{0U};
    DataProvenance provenance{};
    DatasetSplit split{DatasetSplit::Train};
};

enum class DatasetSplitStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    InvalidPolicy,
    InvalidWindow,
    DuplicateWindow,
    InconsistentGroup,
    GroupCapacityExceeded,
    AssignmentCapacityExceeded,
    OverlappingWindowLeakage,
    CsvWriteError
};

inline constexpr std::size_t kDataOriginCount = 6U;

struct DatasetSplitReport {
    std::uint64_t windowCount{0U};
    std::uint64_t groupCount{0U};
    std::uint64_t windowsBySplit[3U]{};
    std::uint64_t groupsBySplit[3U]{};
    std::uint64_t windowsBySplitAndOrigin[3U][kDataOriginCount]{};
    std::uint64_t groupsBySplitAndOrigin[3U][kDataOriginCount]{};
    bool hasTestGroups{false};
    bool testGroupsSyntheticGt5Only{false};
    bool testContainsFieldGt1ToGt4{false};
};

class DatasetSplitter final {
public:
    DatasetSplitStatus assign(
        const DatasetSplitPolicy& policy,
        const DatasetWindowDescriptor* windows, std::size_t windowCount,
        DatasetSplitWorkspace& workspace,
        DatasetWindowSplitAssignment* assignments,
        std::size_t assignmentCapacity,
        DatasetSplitReport& report) const noexcept;

    static DatasetSplitStatus writeCsvHeader(
        char* output, std::size_t capacity,
        std::size_t& bytesWritten) noexcept;
    static DatasetSplitStatus writeCsvRow(
        const DatasetWindowSplitAssignment& assignment, char* output,
        std::size_t capacity, std::size_t& bytesWritten) noexcept;
};

}  // namespace project::dataset
