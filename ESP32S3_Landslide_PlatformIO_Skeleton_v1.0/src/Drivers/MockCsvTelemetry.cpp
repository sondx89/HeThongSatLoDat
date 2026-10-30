#include "project/Drivers/MockCsvTelemetry.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace project::drivers {
namespace {

constexpr char kHeader[] =
    "record_type,sequence_no,timestamp_ms,accel_x_raw,accel_y_raw,"
    "accel_z_raw,soil_raw,rain_raw,hx710b_raw,imu_valid,soil_valid,"
    "rain_valid,hx710b_valid,dataset_eligible,eligibility_reason\n";

bool sensorValid(const project::models::SensorStatus& status) noexcept {
    return status.valid &&
           status.health != project::models::SensorHealth::Fault;
}

bool appendRawValue(char* output, std::size_t capacity, std::size_t& cursor,
                    std::int32_t value, bool valid) noexcept {
    if (!valid) {
        return true;
    }
    const int written = std::snprintf(
        output + cursor, capacity - cursor, "%ld", static_cast<long>(value));
    if (written < 0 || static_cast<std::size_t>(written) >= capacity - cursor) {
        return false;
    }
    cursor += static_cast<std::size_t>(written);
    return true;
}

}  // namespace

const char* mockCsvTelemetryHeader() noexcept {
    return kHeader;
}

bool serializeMockCsvTelemetry(const project::models::SensorFrame& frame,
                               char* output, std::size_t capacity,
                               std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    if (output == nullptr || capacity == 0U) {
        return false;
    }

    const bool imuValid = sensorValid(frame.imuStatus);
    const bool soilValid = sensorValid(frame.soilStatus);
    const bool rainValid = sensorValid(frame.rainStatus);
    const bool hx710bValid = sensorValid(frame.hx710bStatus);
    const int prefixLength = std::snprintf(
        output, capacity, "MOCK_RAW_V1,%llu,%llu",
        static_cast<unsigned long long>(frame.sequence),
        static_cast<unsigned long long>(frame.timestampMs));
    if (prefixLength < 0 ||
        static_cast<std::size_t>(prefixLength) >= capacity) {
        return false;
    }
    std::size_t cursor = static_cast<std::size_t>(prefixLength);

    const std::array<std::int32_t, 6> rawValues{{
        frame.imu.accelX, frame.imu.accelY, frame.imu.accelZ,
        frame.soilMoistureRaw, frame.rainRaw, frame.hx710bRaw}};
    const std::array<bool, 6> rawValidity{{
        imuValid, imuValid, imuValid, soilValid, rainValid, hx710bValid}};
    for (std::size_t index = 0U; index < rawValues.size(); ++index) {
        if (cursor >= capacity) {
            return false;
        }
        output[cursor++] = ',';
        if (!appendRawValue(output, capacity, cursor, rawValues[index],
                            rawValidity[index])) {
            return false;
        }
    }
    const int statusLength = std::snprintf(
        output + cursor, capacity - cursor,
        ",%s,%s,%s,%s,false,SYNTHETIC_MOCK\n",
        imuValid ? "true" : "false", soilValid ? "true" : "false",
        rainValid ? "true" : "false", hx710bValid ? "true" : "false");
    if (statusLength < 0 ||
        static_cast<std::size_t>(statusLength) >= capacity - cursor) {
        return false;
    }
    bytesWritten = cursor + static_cast<std::size_t>(statusLength);
    return true;
}

}  // namespace project::drivers
