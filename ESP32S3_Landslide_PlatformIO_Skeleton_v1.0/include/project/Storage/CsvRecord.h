#pragma once
#include <cstdint>

namespace project::storage {
struct CsvRecord {
    std::uint64_t timestampMicros{0};
    // TODO(TBD): finalized column list, units, schema/model/config IDs and escaping rules.
};
}  // namespace project::storage
