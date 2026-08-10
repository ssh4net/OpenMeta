// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/mapped_file.h"
#include "openmeta/meta_store.h"
#include "openmeta/simple_meta.h"

#include <cstdint>

/**
 * \file libraw_adapter.h
 * \brief Explicit orientation bridge for LibRaw-facing host integrations.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Result status for EXIF/XMP orientation to LibRaw flip mapping.
enum class LibRawOrientationStatus : uint8_t {
    Ok,
    InvalidArgument,
    Unsupported,
};

/// Detailed mapping code for \ref LibRawOrientationResult.
enum class LibRawOrientationCode : uint8_t {
    None,
    PreviewPassThrough,
    MissingExifOrientationAssumedDefault,
    InvalidExifOrientation,
    UnsupportedMirroredOrientation,
    MirroredOrientationDropped,
};

/// Canonical metadata source used for \ref LibRawOrientationResult.
enum class LibRawOrientationSource : uint8_t {
    ExplicitInput,
    AssumedDefault,
    ExifIfd0,
    XmpTiffOrientation,
};

/// Output target for LibRaw orientation mapping.
enum class LibRawOrientationTarget : uint8_t {
    RawImage,
    EmbeddedPreview,
};

/// Policy for mirrored EXIF orientations (2/4/5/7).
enum class LibRawMirrorPolicy : uint8_t {
    Reject,
    DropMirror,
};

/// Detailed mapping code for \ref LibRawFlipToExifResult.
enum class LibRawFlipToExifCode : uint8_t {
    None,
    PreviewPassThrough,
    InvalidLibRawFlip,
};

/// Options for \ref map_exif_orientation_to_libraw_flip.
struct LibRawOrientationOptions final {
    /// Raw-image orientation should normally map into `imgdata.sizes.flip`.
    LibRawOrientationTarget target = LibRawOrientationTarget::RawImage;
    /// Embedded previews are usually delivered as-is or carry their own EXIF.
    bool preserve_embedded_preview_orientation = true;
    /// Mirrored TIFF/EXIF orientations are not representable in LibRaw flip.
    LibRawMirrorPolicy mirror_policy = LibRawMirrorPolicy::Reject;
};

/// Result for \ref map_exif_orientation_to_libraw_flip.
struct LibRawOrientationResult final {
    LibRawOrientationStatus status = LibRawOrientationStatus::Ok;
    LibRawOrientationCode code     = LibRawOrientationCode::None;
    LibRawOrientationSource source = LibRawOrientationSource::ExplicitInput;
    uint16_t exif_orientation      = 1;
    uint32_t libraw_flip           = 0;
    bool apply_flip                = false;
    bool mirrored                  = false;
    bool preview_passthrough       = false;
    bool has_exif_ifd0_orientation = false;
    bool has_xmp_tiff_orientation  = false;
    uint16_t exif_ifd0_orientation = 0;
    uint16_t xmp_tiff_orientation  = 0;
    bool orientation_conflict      = false;
};

/// Options for \ref map_libraw_flip_to_exif_orientation.
struct LibRawFlipToExifOptions final {
    /// Raw-image flips should normally map back into EXIF orientation.
    LibRawOrientationTarget target = LibRawOrientationTarget::RawImage;
    /// Embedded previews are usually delivered as-is or carry their own EXIF.
    bool preserve_embedded_preview_orientation = true;
};

/// Result for \ref map_libraw_flip_to_exif_orientation.
struct LibRawFlipToExifResult final {
    LibRawOrientationStatus status = LibRawOrientationStatus::Ok;
    LibRawFlipToExifCode code      = LibRawFlipToExifCode::None;
    uint32_t libraw_flip           = 0;
    uint16_t exif_orientation      = 1;
    bool preview_passthrough       = false;
};

/// Status for \ref map_meta_orientation_to_libraw_flip_from_file.
enum class LibRawOrientationFileStatus : uint8_t {
    Ok,
    InvalidArgument,
    OpenFailed,
    StatFailed,
    TooLarge,
    MapFailed,
    DecodeFailed,
};

/// Options for \ref map_meta_orientation_to_libraw_flip_from_file.
struct LibRawOrientationFileOptions final {
    uint64_t max_file_bytes = 0;
    SimpleMetaDecodeOptions decode;
    LibRawOrientationOptions orientation;
};

/// Result for \ref map_meta_orientation_to_libraw_flip_from_file.
struct LibRawOrientationFileResult final {
    LibRawOrientationFileStatus file_status = LibRawOrientationFileStatus::Ok;
    MappedFileStatus mapped_file_status     = MappedFileStatus::Ok;
    uint64_t file_size                      = 0;
    SimpleMetaResult read;
    LibRawOrientationResult orientation;
};

/**
 * \brief Maps one EXIF/TIFF/XMP orientation value into the common LibRaw
 * `imgdata.sizes.flip` convention.
 *
 * Supported exact mappings are:
 * - EXIF `1` -> LibRaw `0`
 * - EXIF `3` -> LibRaw `3`
 * - EXIF `6` -> LibRaw `6`
 * - EXIF `8` -> LibRaw `5`
 *
 * Mirrored EXIF orientations (`2`, `4`, `5`, `7`) are not directly
 * representable in LibRaw's common flip model. With
 * \ref LibRawMirrorPolicy::Reject they fail explicitly. With
 * \ref LibRawMirrorPolicy::DropMirror they are collapsed to the nearest
 * rotation-only convention:
 * - `2` -> `1`
 * - `4` -> `3`
 * - `5` -> `8`
 * - `7` -> `6`
 *
 * For \ref LibRawOrientationTarget::EmbeddedPreview, the default policy is to
 * pass previews through unchanged because preview orientation is often already
 * baked into the extracted bytes or carried by the preview's own metadata.
 */
LibRawOrientationResult
map_exif_orientation_to_libraw_flip(uint16_t exif_orientation,
                                    const LibRawOrientationOptions& options
                                    = LibRawOrientationOptions {}) noexcept;

/**
 * \brief Maps one common LibRaw `imgdata.sizes.flip` value back into EXIF/TIFF
 * orientation space.
 *
 * Supported exact mappings are:
 * - LibRaw `0` -> EXIF `1`
 * - LibRaw `3` -> EXIF `3`
 * - LibRaw `5` -> EXIF `8`
 * - LibRaw `6` -> EXIF `6`
 *
 * This recovers only the non-mirrored EXIF orientation states because the
 * common LibRaw flip model does not preserve the full TIFF 8-state orientation
 * space.
 *
 * For \ref LibRawOrientationTarget::EmbeddedPreview, the default policy is to
 * pass previews through unchanged and assume EXIF orientation `1`.
 */
LibRawFlipToExifResult
map_libraw_flip_to_exif_orientation(uint32_t libraw_flip,
                                    const LibRawFlipToExifOptions& options
                                    = LibRawFlipToExifOptions {}) noexcept;

/**
 * \brief Extracts canonical orientation metadata from a \ref MetaStore and
 * maps it into the common LibRaw flip convention.
 *
 * Source precedence is:
 * - EXIF `ifd0:0x0112`
 * - XMP `tiff:Orientation`
 * - assumed default EXIF orientation `1` if neither is present
 */
LibRawOrientationResult
map_meta_orientation_to_libraw_flip(const MetaStore& store,
                                    const LibRawOrientationOptions& options
                                    = LibRawOrientationOptions {}) noexcept;

/**
 * \brief Reads one file, decodes supported metadata, then maps canonical
 * orientation metadata into the LibRaw flip convention.
 *
 * This is the direct file-helper for thin host integrations that want the same
 * explicit orientation result without manually calling \ref simple_meta_read
 * and extracting `ifd0:0x0112`.
 */
LibRawOrientationFileResult
map_meta_orientation_to_libraw_flip_from_file(
    const char* path, const LibRawOrientationFileOptions& options
                      = LibRawOrientationFileOptions {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
