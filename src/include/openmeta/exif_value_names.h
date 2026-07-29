// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/**
 * \file exif_value_names.h
 * \brief Human-readable names for common EXIF/TIFF/DNG numeric values and
 * selected bounded MakerNote contexts.
 */

namespace openmeta {

const char*
tiff_compression_name(uint64_t value) noexcept;

const char*
tiff_photometric_interpretation_name(uint64_t value) noexcept;

const char*
tiff_planar_configuration_name(uint64_t value) noexcept;

const char*
tiff_resolution_unit_name(uint64_t value) noexcept;

const char*
tiff_ycbcr_positioning_name(uint64_t value) noexcept;

const char*
exif_exposure_program_name(uint64_t value) noexcept;

const char*
exif_exposure_mode_name(uint64_t value) noexcept;

const char*
exif_metering_mode_name(uint64_t value) noexcept;

const char*
exif_light_source_name(uint64_t value) noexcept;

const char*
exif_flash_name(uint64_t value) noexcept;

const char*
exif_color_space_name(uint64_t value) noexcept;

const char*
exif_white_balance_name(uint64_t value) noexcept;

const char*
exif_scene_capture_type_name(uint64_t value) noexcept;

const char*
exif_gain_control_name(uint64_t value) noexcept;

const char*
exif_sensitivity_type_name(uint64_t value) noexcept;

const char*
exif_focal_plane_resolution_unit_name(uint64_t value) noexcept;

const char*
exif_sensing_method_name(uint64_t value) noexcept;

const char*
exif_file_source_name(uint64_t value) noexcept;

const char*
exif_scene_type_name(uint64_t value) noexcept;

const char*
exif_custom_rendered_name(uint64_t value) noexcept;

const char*
exif_contrast_name(uint64_t value) noexcept;

const char*
exif_saturation_name(uint64_t value) noexcept;

const char*
exif_sharpness_name(uint64_t value) noexcept;

const char*
exif_subject_distance_range_name(uint64_t value) noexcept;

const char*
dng_cfa_layout_name(uint64_t value) noexcept;

const char*
dng_calibration_illuminant_name(uint64_t value) noexcept;

/**
 * \brief Interprets common numeric enum-like values by EXIF/TIFF/DNG tag id
 * and selected bounded MakerNote contexts.
 *
 * Returns an empty string when OpenMeta has no stable public interpretation for
 * the value. Unknown values remain numeric and lossless in the underlying
 * MetaStore entry.
 */
const char*
exif_tag_numeric_value_name(std::string_view ifd, uint16_t tag,
                            uint64_t value) noexcept;

/**
 * \brief Formats selected version/firmware-style numeric values.
 *
 * This is separate from \ref exif_tag_numeric_value_name because firmware and
 * version fields are formatted values, not enum labels. Returns false when the
 * context is unsupported or the output buffer is too small; in both cases the
 * output buffer is cleared when possible.
 */
bool
exif_tag_numeric_value_format(std::string_view ifd, uint16_t tag,
                              uint64_t value, char* out,
                              std::size_t out_size) noexcept;

/**
 * \brief Formats selected one-byte enums and version/firmware-style payloads.
 *
 * Stable standard enum labels are emitted for one-byte payloads. Printable
 * ASCII payloads are copied after NUL/space trimming. Other payloads are
 * formatted as bounded dotted decimal bytes only for version-like contexts.
 * Returns false when the context is unsupported or the output buffer is too
 * small; in both cases the output buffer is cleared when possible.
 */
bool
exif_tag_byte_value_format(std::string_view ifd, uint16_t tag,
                           std::span<const std::byte> value, char* out,
                           std::size_t out_size) noexcept;

}  // namespace openmeta
