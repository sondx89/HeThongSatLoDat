#pragma once

#include <cstddef>
#include <cstdint>

#include "Models/DataContractTypes.h"

namespace project::models {

namespace event_capacity {
// Temporary fixed storage only. These are not approved wire-format limits.
// TODO(TBD): set/test capacities and account for queue RAM from the released
// Event Catalog, FreeRTOS task set, and RAM budget.
constexpr std::size_t kEventNameBytes = 32;
constexpr std::size_t kSourceBytes = 24;
constexpr std::size_t kCodeBytes = 32;
constexpr std::size_t kIdentityBytes = 64;
constexpr std::size_t kVersionBytes = 65;
constexpr std::size_t kStateNameBytes = 16;
constexpr std::size_t kUnitBytes = 16;
constexpr std::size_t kMessageBytes = 96;
}  // namespace event_capacity

struct EventRecord {
    std::uint16_t schemaVersion{1};  // ICD EventRecord v1 logical schema.
    std::uint64_t eventId{0};        // Unique with bootId; not an event catalog code.
    std::uint32_t eventSequenceNo{0};
    std::uint64_t bootId{0};
    std::uint64_t timestampMs{0};

    char eventName[event_capacity::kEventNameBytes]{};
    char eventCatalogId[event_capacity::kCodeBytes]{};
    EventSeverity severity{EventSeverity::Info};
    char source[event_capacity::kSourceBytes]{};
    char statusCode[event_capacity::kCodeBytes]{};
    char errorCode[event_capacity::kCodeBytes]{};  // TODO(TBD): reconcile with statusCode/faultCode.
    bool errorCodeValid{false};
    char faultCode[event_capacity::kCodeBytes]{};
    bool faultCodeValid{false};

    RiskState riskState{RiskState::Unknown};
    HealthState healthState{HealthState::Unknown};
    char manifestId[event_capacity::kIdentityBytes]{};
    bool manifestIdValid{false};
    char firmwareVersion[event_capacity::kVersionBytes]{};
    char modelVersion[event_capacity::kVersionBytes]{};
    bool modelVersionValid{false};
    std::uint8_t featureSchemaHashSha256[32]{};
    bool featureSchemaHashValid{false};
    char configVersion[event_capacity::kVersionBytes]{};
    bool configVersionValid{false};
    char calibrationVersion[event_capacity::kVersionBytes]{};
    bool calibrationVersionValid{false};

    std::uint64_t relatedSequenceNo{0};
    bool relatedSequenceNoValid{false};
    char message[event_capacity::kMessageBytes]{};
    enum class ValueType : std::uint8_t { None, Numeric, Text };
    ValueType valueType{ValueType::None};
    float value{0.0F};
    char valueText[event_capacity::kMessageBytes]{};
    char unit[event_capacity::kUnitBytes]{};
    bool valueValid{false};
    std::uint32_t droppedEventCount{0};
    std::uint32_t crc32{0};
    bool crc32Valid{false};
    bool valid{false};

    // Additional ICD logical fields; reconcile with the frozen EventRecord v1.
    SensorId sensorId{SensorId::Unknown};
    bool sensorIdValid{false};
    char stateBefore[event_capacity::kStateNameBytes]{};
    bool stateBeforeValid{false};
    char stateAfter[event_capacity::kStateNameBytes]{};
    bool stateAfterValid{false};
};

}  // namespace project::models
