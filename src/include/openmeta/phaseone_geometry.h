// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstdint>

/**
 * \file phaseone_geometry.h
 * \brief Normalized helpers for Phase One/Leaf RAW sensor geometry and
 * processing tags.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

enum class PhaseOneRawGeometryStatus : uint8_t {
    Ok,
    MissingField,
    InvalidValue,
    OutOfBounds,
};

struct PhaseOneRawGeometry final {
    uint32_t sensor_width       = 0;
    uint32_t sensor_height      = 0;
    uint32_t sensor_left_margin = 0;
    uint32_t sensor_top_margin  = 0;
    uint32_t image_width        = 0;
    uint32_t image_height       = 0;
    uint32_t active_x           = 0;
    uint32_t active_y           = 0;
    uint32_t active_width       = 0;
    uint32_t active_height      = 0;
    uint32_t right_margin       = 0;
    uint32_t bottom_margin      = 0;
};

struct PhaseOneRawGeometryResult final {
    PhaseOneRawGeometryStatus status = PhaseOneRawGeometryStatus::MissingField;
    PhaseOneRawGeometry geometry;
};

enum class PhaseOneRawProcessingStatus : uint8_t {
    Ok,
    MissingField,
    InvalidValue,
    Partial,
};

struct PhaseOneRawProcessingInfo final {
    bool has_color_matrix1 = false;
    double color_matrix1[9] {};

    bool has_color_matrix2 = false;
    double color_matrix2[9] {};

    bool has_wb_rgb_levels = false;
    double wb_rgb_levels[3] {};

    bool has_black_level = false;
    uint32_t black_level = 0;

    bool has_sensor_temperature_c = false;
    double sensor_temperature_c = 0.0;

    bool has_sensor_temperature2_c = false;
    double sensor_temperature2_c = 0.0;

    bool has_raw_format = false;
    uint32_t raw_format = 0;

    bool has_raw_data = false;
    uint64_t raw_data_bytes = 0;

    bool has_strip_offsets = false;
    uint64_t strip_offsets_bytes = 0;

    bool has_black_level_data = false;
    uint64_t black_level_data_bytes = 0;

    bool has_sensor_calibration = false;
    uint32_t sensor_calibration_entry_count = 0;
    uint64_t sensor_calibration_payload_bytes = 0;

    bool has_sensor_defects = false;
    uint64_t sensor_defects_bytes = 0;

    bool has_flat_field = false;
    uint64_t flat_field_bytes = 0;

    bool has_linearization_coefficients = false;
    uint32_t linearization_coefficients_count = 0;
};

struct PhaseOneRawProcessingResult final {
    PhaseOneRawProcessingStatus status =
        PhaseOneRawProcessingStatus::MissingField;
    uint32_t fields_seen = 0;
    uint32_t fields_decoded = 0;
    uint32_t invalid_fields = 0;
    PhaseOneRawProcessingInfo info;
};

PhaseOneRawGeometryResult
phaseone_raw_geometry_from_values(uint32_t sensor_width,
                                  uint32_t sensor_height,
                                  uint32_t sensor_left_margin,
                                  uint32_t sensor_top_margin,
                                  uint32_t image_width,
                                  uint32_t image_height) noexcept;

PhaseOneRawGeometryResult
phaseone_raw_geometry_from_store(const MetaStore& store) noexcept;

PhaseOneRawProcessingResult
phaseone_raw_processing_from_store(const MetaStore& store) noexcept;

const char*
phaseone_raw_geometry_status_name(PhaseOneRawGeometryStatus status) noexcept;

const char*
phaseone_raw_processing_status_name(PhaseOneRawProcessingStatus status)
    noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
