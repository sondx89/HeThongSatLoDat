#if defined(PROJECT_HOST_DATA_TOOLS) && PROJECT_HOST_DATA_TOOLS == 1

#include "project/Dataset/DatasetSplitter.h"

#include <limits>

namespace project::dataset {
namespace {

constexpr std::string_view kSplitHeader[] = {
    "split_schema_version", "window_id", "split_group_id", "split_name",
    "split_policy_version", "split_seed", "data_origin", "evidence_class"};

constexpr std::size_t splitIndex(DatasetSplit split) noexcept {
    return static_cast<std::size_t>(split);
}

constexpr std::size_t originIndex(DataOrigin origin) noexcept {
    return static_cast<std::size_t>(origin);
}

bool validSplit(DatasetSplit split) noexcept {
    return split == DatasetSplit::Train ||
           split == DatasetSplit::Validation || split == DatasetSplit::Test;
}

std::string_view splitName(DatasetSplit split) noexcept {
    switch (split) {
        case DatasetSplit::Train:
            return "TRAIN";
        case DatasetSplit::Validation:
            return "VALIDATION";
        case DatasetSplit::Test:
            return "TEST";
    }
    return {};
}

std::string_view splitOriginName(DataOrigin origin) noexcept {
    switch (origin) {
        case DataOrigin::Synthetic:
            return "SYNTHETIC";
        case DataOrigin::Bench:
            return "BENCH";
        case DataOrigin::Field:
            return "FIELD";
        case DataOrigin::Reference:
            return "REFERENCE";
        case DataOrigin::Replay:
            return "REPLAY";
        case DataOrigin::Unspecified:
            return {};
    }
    return {};
}

std::string_view splitEvidenceName(EvidenceClass evidence) noexcept {
    switch (evidence) {
        case EvidenceClass::GT1:
            return "GT1";
        case EvidenceClass::GT2:
            return "GT2";
        case EvidenceClass::GT3:
            return "GT3";
        case EvidenceClass::GT4:
            return "GT4";
        case EvidenceClass::GT5:
            return "GT5";
        case EvidenceClass::NotApplicable:
            return "NOT_APPLICABLE";
    }
    return {};
}

bool validPolicy(const DatasetSplitPolicy& policy) noexcept {
    const std::uint32_t sum =
        static_cast<std::uint32_t>(policy.trainBasisPoints) +
        static_cast<std::uint32_t>(policy.validationBasisPoints) +
        static_cast<std::uint32_t>(policy.testBasisPoints);
    return policy.version != 0U && policy.seed != 0U &&
           policy.trainBasisPoints != 0U &&
           policy.validationBasisPoints != 0U &&
           policy.testBasisPoints != 0U &&
           sum == kSplitBasisPointTotal;
}

std::uint64_t groupHash(std::string_view groupId,
                       std::uint64_t seed) noexcept {
    std::uint64_t hash = 14695981039346656037ULL ^ seed;
    for (const char byte : groupId) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    hash += 0x9E3779B97F4A7C15ULL;
    hash = (hash ^ (hash >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    hash = (hash ^ (hash >> 27U)) * 0x94D049BB133111EBULL;
    return hash ^ (hash >> 31U);
}

DatasetSplit splitForGroup(std::string_view groupId,
                           const DatasetSplitPolicy& policy) noexcept {
    const std::uint32_t bucket =
        static_cast<std::uint32_t>(groupHash(groupId, policy.seed) %
                                   kSplitBasisPointTotal);
    if (bucket < policy.trainBasisPoints) {
        return DatasetSplit::Train;
    }
    if (bucket < static_cast<std::uint32_t>(policy.trainBasisPoints) +
                     policy.validationBasisPoints) {
        return DatasetSplit::Validation;
    }
    return DatasetSplit::Test;
}

bool validWindow(const DatasetWindowDescriptor& window) noexcept {
    return !window.windowId.empty() && !window.datasetId.empty() &&
           !window.siteId.empty() && !window.sessionId.empty() &&
           !window.splitGroupId.empty() &&
           window.startSequence <= window.endSequence &&
           window.provenance.isValid();
}

bool sameSourceStream(const DatasetWindowDescriptor& left,
                      const DatasetWindowDescriptor& right) noexcept {
    return left.datasetId == right.datasetId &&
           left.siteId == right.siteId &&
           left.sessionId == right.sessionId && left.bootId == right.bootId;
}

bool sequenceIntervalsOverlap(const DatasetWindowDescriptor& left,
                              const DatasetWindowDescriptor& right) noexcept {
    return left.startSequence <= right.endSequence &&
           right.startSequence <= left.endSequence;
}

std::size_t findGroup(const DatasetSplitWorkspace& workspace,
                      std::string_view groupId) noexcept {
    for (std::size_t i = 0U; i < workspace.groupCount; ++i) {
        if (workspace.groups[i].splitGroupId == groupId) {
            return i;
        }
    }
    return workspace.groupCount;
}

bool gt1ToGt4(EvidenceClass evidence) noexcept {
    return evidence == EvidenceClass::GT1 || evidence == EvidenceClass::GT2 ||
           evidence == EvidenceClass::GT3 || evidence == EvidenceClass::GT4;
}

void increment(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

}  // namespace

DatasetSplitStatus DatasetSplitter::assign(
    const DatasetSplitPolicy& policy,
    const DatasetWindowDescriptor* windows, std::size_t windowCount,
    DatasetSplitWorkspace& workspace,
    DatasetWindowSplitAssignment* assignments,
    std::size_t assignmentCapacity, DatasetSplitReport& report) const noexcept {
    report = {};
    workspace.groupCount = 0U;
    if (windowCount > kMaximumSplitWindows ||
        (windowCount != 0U &&
         (windows == nullptr || assignments == nullptr)) ||
        (workspace.groups == nullptr && workspace.groupCapacity != 0U) ||
        (workspace.groups != nullptr && workspace.groupCapacity == 0U)) {
        return DatasetSplitStatus::InvalidArgument;
    }
    if (!validPolicy(policy)) {
        return DatasetSplitStatus::InvalidPolicy;
    }
    if (assignmentCapacity < windowCount) {
        return DatasetSplitStatus::AssignmentCapacityExceeded;
    }
    if (windowCount != 0U && workspace.groupCapacity == 0U) {
        return DatasetSplitStatus::GroupCapacityExceeded;
    }
    for (std::size_t i = 0U; i < windowCount; ++i) {
        assignments[i] = {};
    }

    for (std::size_t i = 0U; i < windowCount; ++i) {
        const DatasetWindowDescriptor& current = windows[i];
        if (!validWindow(current)) {
            workspace.groupCount = 0U;
            return DatasetSplitStatus::InvalidWindow;
        }
        for (std::size_t previous = 0U; previous < i; ++previous) {
            const DatasetWindowDescriptor& earlier = windows[previous];
            if (current.datasetId == earlier.datasetId &&
                current.windowId == earlier.windowId) {
                workspace.groupCount = 0U;
                return DatasetSplitStatus::DuplicateWindow;
            }
            if (current.splitGroupId != earlier.splitGroupId &&
                sameSourceStream(current, earlier) &&
                sequenceIntervalsOverlap(current, earlier)) {
                workspace.groupCount = 0U;
                return DatasetSplitStatus::OverlappingWindowLeakage;
            }
        }

        const std::size_t groupIndex =
            findGroup(workspace, current.splitGroupId);
        if (groupIndex == workspace.groupCount) {
            if (workspace.groupCount == workspace.groupCapacity) {
                workspace.groupCount = 0U;
                return DatasetSplitStatus::GroupCapacityExceeded;
            }
            workspace.groups[workspace.groupCount++] = {
                current.splitGroupId, current.provenance,
                splitForGroup(current.splitGroupId, policy)};
        } else if (workspace.groups[groupIndex].provenance.origin !=
                       current.provenance.origin ||
                   workspace.groups[groupIndex].provenance.evidenceClass !=
                       current.provenance.evidenceClass) {
            workspace.groupCount = 0U;
            return DatasetSplitStatus::InconsistentGroup;
        }
    }

    report.windowCount = windowCount;
    report.groupCount = workspace.groupCount;
    report.hasTestGroups = false;
    report.testGroupsSyntheticGt5Only = true;
    for (std::size_t group = 0U; group < workspace.groupCount; ++group) {
        const DatasetSplitGroupEntry& entry = workspace.groups[group];
        if (!validSplit(entry.split) ||
            originIndex(entry.provenance.origin) >= kDataOriginCount) {
            workspace.groupCount = 0U;
            report = {};
            return DatasetSplitStatus::InconsistentGroup;
        }
        increment(report.groupsBySplit[splitIndex(entry.split)]);
        increment(report.groupsBySplitAndOrigin[splitIndex(entry.split)]
                                                     [originIndex(entry.provenance.origin)]);
        if (entry.split == DatasetSplit::Test) {
            report.hasTestGroups = true;
            const bool syntheticGt5 =
                entry.provenance.origin == DataOrigin::Synthetic &&
                entry.provenance.evidenceClass == EvidenceClass::GT5;
            report.testGroupsSyntheticGt5Only =
                report.testGroupsSyntheticGt5Only && syntheticGt5;
            report.testContainsFieldGt1ToGt4 =
                report.testContainsFieldGt1ToGt4 ||
                (entry.provenance.origin == DataOrigin::Field &&
                 gt1ToGt4(entry.provenance.evidenceClass));
        }
    }
    report.testGroupsSyntheticGt5Only =
        report.hasTestGroups && report.testGroupsSyntheticGt5Only;

    for (std::size_t i = 0U; i < windowCount; ++i) {
        const DatasetWindowDescriptor& window = windows[i];
        const std::size_t group =
            findGroup(workspace, window.splitGroupId);
        if (group == workspace.groupCount) {
            workspace.groupCount = 0U;
            report = {};
            return DatasetSplitStatus::InconsistentGroup;
        }
        const DatasetSplit split = workspace.groups[group].split;
        assignments[i] = {window.windowId, window.splitGroupId,
                           policy.version, policy.seed, window.provenance,
                           split};
        increment(report.windowsBySplit[splitIndex(split)]);
        increment(report.windowsBySplitAndOrigin[splitIndex(split)]
                                                      [originIndex(window.provenance.origin)]);
    }
    return DatasetSplitStatus::Ok;
}

DatasetSplitStatus DatasetSplitter::writeCsvHeader(
    char* output, std::size_t capacity,
    std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    if (output == nullptr || capacity == 0U) {
        return DatasetSplitStatus::InvalidArgument;
    }
    CsvRowWriter writer(output, capacity, kDatasetSplitCsvColumns);
    for (const std::string_view field : kSplitHeader) {
        if (writer.appendField(field) != CsvWriteStatus::Ok) {
            return DatasetSplitStatus::CsvWriteError;
        }
    }
    if (writer.finish() != CsvWriteStatus::Ok) {
        return DatasetSplitStatus::CsvWriteError;
    }
    bytesWritten = writer.size();
    return DatasetSplitStatus::Ok;
}

DatasetSplitStatus DatasetSplitter::writeCsvRow(
    const DatasetWindowSplitAssignment& assignment, char* output,
    std::size_t capacity, std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    if (output == nullptr || capacity == 0U || assignment.windowId.empty() ||
        assignment.splitGroupId.empty() ||
        assignment.policyVersion == 0U || assignment.seed == 0U ||
        !assignment.provenance.isValid() || !validSplit(assignment.split)) {
        return DatasetSplitStatus::InvalidArgument;
    }
    CsvRowWriter writer(output, capacity, kDatasetSplitCsvColumns);
    if (writer.appendUnsigned(kDatasetSplitSchemaVersion) !=
            CsvWriteStatus::Ok ||
        writer.appendField(assignment.windowId) != CsvWriteStatus::Ok ||
        writer.appendField(assignment.splitGroupId) != CsvWriteStatus::Ok ||
        writer.appendField(splitName(assignment.split)) != CsvWriteStatus::Ok ||
        writer.appendUnsigned(assignment.policyVersion) !=
            CsvWriteStatus::Ok ||
        writer.appendUnsigned(assignment.seed) != CsvWriteStatus::Ok ||
        writer.appendField(splitOriginName(assignment.provenance.origin)) !=
            CsvWriteStatus::Ok ||
        writer.appendField(
            splitEvidenceName(assignment.provenance.evidenceClass)) !=
            CsvWriteStatus::Ok ||
        writer.finish() != CsvWriteStatus::Ok) {
        return DatasetSplitStatus::CsvWriteError;
    }
    bytesWritten = writer.size();
    return DatasetSplitStatus::Ok;
}

}  // namespace project::dataset

#endif
