// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file exif_tiff_serialize.h
 * \brief Deterministic target-neutral TIFF/EXIF serialization.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable canonical TIFF/EXIF serialization contract version.
inline constexpr uint32_t kExifTiffSerializeContractVersion = 1U;

/// Opaque MakerNote handling for canonical TIFF serialization.
enum class ExifTiffMakerNotePolicy : uint8_t {
    Drop,
    PreserveOpaque,
};

/// Stable result status for `serialize_exif_tiff()`.
enum class ExifTiffSerializeStatus : uint8_t {
    Ok,
    OutputTruncated,
    InvalidOptions,
    StoreNotFinalized,
    InvalidMetadata,
    NoExifData,
    LimitExceeded,
    SerializationFailed,
};

/// Policy and resource limits for canonical TIFF/EXIF serialization.
struct ExifTiffSerializeOptions final {
    bool include_subifds                     = false;
    bool inject_minimal_dng_version          = false;
    bool validate                            = true;
    bool honor_wire_type_hints               = true;
    ExifTiffMakerNotePolicy makernote_policy = ExifTiffMakerNotePolicy::Drop;
    uint64_t max_output_bytes                = 64ULL * 1024ULL * 1024ULL;
};

/// Size and coverage details from canonical TIFF/EXIF serialization.
struct ExifTiffSerializeResult final {
    ExifTiffSerializeStatus status = ExifTiffSerializeStatus::Ok;
    uint64_t written               = 0U;
    uint64_t needed                = 0U;
    uint32_t entries_serialized    = 0U;
    uint32_t entries_skipped       = 0U;

    bool ok() const noexcept { return status == ExifTiffSerializeStatus::Ok; }
};

/**
 * \brief Serialize an unwrapped little-endian TIFF/EXIF byte stream.
 *
 * Output begins with the TIFF `II` header. It never includes an `Exif\0\0`
 * preamble, JPEG APP1 framing, or a boxed-container EXIF offset prefix. Call
 * once with an empty span to measure, then provide a span of `needed` bytes.
 * Repeated calls for an immutable finalized store are deterministic and safe
 * to run concurrently.
 */
ExifTiffSerializeResult
serialize_exif_tiff(const MetaStore& store, std::span<std::byte> output,
                    const ExifTiffSerializeOptions& options
                    = ExifTiffSerializeOptions {}) noexcept;

const char*
exif_tiff_serialize_status_name(ExifTiffSerializeStatus status) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
