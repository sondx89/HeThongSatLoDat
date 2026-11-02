#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "Dataset/CsvWriter.h"

namespace project::dataset {

inline constexpr std::size_t kDsdRawChannelCount = 6U;

inline constexpr std::array<std::string_view, 36U>
    kDsdV1_2RawSampleCoreFields{{
        "dataset_id",
        "site_id",
        "device_id",
        "sensor_set_id",
        "session_id",
        "boot_id",
        "sequence_no",
        "timestamp_ms",
        "utc_time",
        "time_status",
        "firmware_version",
        "config_version",
        "calibration_id",
        "sensor_status_bits",
        "accel_x_raw",
        "accel_y_raw",
        "accel_z_raw",
        "soil_raw",
        "yl83_raw",
        "hx710b_raw",
        "accel_x_valid",
        "accel_y_valid",
        "accel_z_valid",
        "soil_valid",
        "yl83_valid",
        "hx710b_valid",
        "accel_x_quality",
        "accel_y_quality",
        "accel_z_quality",
        "soil_quality",
        "yl83_quality",
        "hx710b_quality",
        "valid_mask",
        "quality_flags",
        "event_id",
        "annotation_id",
    }};

inline CsvWriteStatus writeDsdV1_2RawSampleCoreHeader(
    char* output, std::size_t capacity,
    std::size_t& bytesWritten) noexcept {
    bytesWritten = 0U;
    CsvRowWriter writer(output, capacity, kDsdV1_2RawSampleCoreFields.size());
    for (const std::string_view field : kDsdV1_2RawSampleCoreFields) {
        const CsvWriteStatus status = writer.appendField(field);
        if (status != CsvWriteStatus::Ok) {
            return status;
        }
    }
    const CsvWriteStatus status = writer.finish();
    if (status == CsvWriteStatus::Ok) {
        bytesWritten = writer.size();
    }
    return status;
}

}  // namespace project::dataset
