#pragma once

#include <cstdint>

namespace project::storage {
struct CsvRecord {
    std::uint32_t schemaVersion{0};
    std::uint64_t bootId{0};
    std::uint64_t sequenceNo{0};
    std::uint64_t timestampMs{0};  // Monotonic milliseconds within boot.
    // TODO(TBD): finalized columns, manifest/version metadata, units and CSV escaping.
};
}  // namespace project::storage
