#pragma once

#include <cstdint>

namespace project::dataset {

enum class DataOrigin : std::uint8_t {
    Unspecified,
    Synthetic,
    Bench,
    Field,
    Reference,
    Replay
};

enum class EvidenceClass : std::uint8_t {
    NotApplicable,
    GT1,
    GT2,
    GT3,
    GT4,
    GT5
};

struct DataProvenance {
    DataOrigin origin{DataOrigin::Unspecified};
    EvidenceClass evidenceClass{EvidenceClass::NotApplicable};

    constexpr bool isValid() const noexcept {
        const bool validOrigin =
            origin == DataOrigin::Synthetic || origin == DataOrigin::Bench ||
            origin == DataOrigin::Field || origin == DataOrigin::Reference ||
            origin == DataOrigin::Replay;
        const bool validEvidence =
            evidenceClass == EvidenceClass::NotApplicable ||
            evidenceClass == EvidenceClass::GT1 ||
            evidenceClass == EvidenceClass::GT2 ||
            evidenceClass == EvidenceClass::GT3 ||
            evidenceClass == EvidenceClass::GT4 ||
            evidenceClass == EvidenceClass::GT5;
        return validOrigin && validEvidence &&
               (origin != DataOrigin::Synthetic ||
                evidenceClass == EvidenceClass::GT5);
    }
};

static_assert(!DataProvenance{}.isValid(),
              "Unspecified provenance must fail closed");

}  // namespace project::dataset
