// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include <cstdint>

/**
 * \file orientation.h
 * \brief Helpers for interpreting EXIF/TIFF orientation values.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Result status for EXIF/TIFF orientation interpretation.
enum class ExifOrientationStatus : uint8_t {
    Ok,
    InvalidArgument,
};

/// Normalized interpretation for one EXIF/TIFF orientation value.
struct ExifOrientationInterpretation final {
    ExifOrientationStatus status       = ExifOrientationStatus::Ok;
    uint16_t orientation               = 1;
    uint16_t rotation_degrees_cw       = 0;
    uint16_t rotation_only_orientation = 1;
    bool mirrored                      = false;
    bool swaps_width_height            = false;
    const char* name                   = "Horizontal (normal)";
};

bool
exif_orientation_is_valid(uint16_t orientation) noexcept;

bool
exif_orientation_is_mirrored(uint16_t orientation) noexcept;

bool
exif_orientation_swaps_width_height(uint16_t orientation) noexcept;

/**
 * \brief Returns the clockwise rotation component for a standard EXIF/TIFF
 * orientation value.
 *
 * Mirrored values keep the same decomposition used by common metadata tools.
 * For example, orientation `5` reports mirror + 270 degrees clockwise.
 * Invalid values return `0` and set `out_valid` to false when provided.
 */
uint16_t
exif_orientation_rotation_degrees_cw(uint16_t orientation,
                                     bool* out_valid = nullptr) noexcept;

/**
 * \brief Returns the nearest rotation-only orientation.
 *
 * Valid non-mirrored values are returned unchanged. Mirrored values are mapped
 * to the closest non-mirrored rotation: `2 -> 1`, `4 -> 3`, `5 -> 8`,
 * `7 -> 6`. Invalid values return `0`.
 */
uint16_t
exif_orientation_rotation_only(uint16_t orientation) noexcept;

const char*
exif_orientation_name(uint16_t orientation) noexcept;

ExifOrientationInterpretation
interpret_exif_orientation(uint16_t orientation) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
