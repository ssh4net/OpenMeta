// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/**
 * \file exif_tiff_decode.h
 * \brief Decoder for TIFF-IFD tag streams (used by EXIF and TIFF/DNG).
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// EXIF/TIFF decode result status.
enum class ExifDecodeStatus : uint8_t {
    Ok,
    OutputTruncated,
    Unsupported,
    Malformed,
    LimitExceeded,
};

/// Best-effort reason for \ref ExifDecodeStatus::LimitExceeded.
enum class ExifLimitReason : uint8_t {
    None,
    MaxIfds,
    MaxEntriesPerIfd,
    MaxTotalEntries,
    ValueCountTooLarge,
    MaxArenaBytes,
};

/// Logical IFD kinds exposed by decode_exif_tiff().
enum class ExifIfdKind : uint8_t {
    Ifd,
    ExifIfd,
    GpsIfd,
    InteropIfd,
    SubIfd,
};

/// Reference to a decoded IFD within the input TIFF byte stream.
struct ExifIfdRef final {
    ExifIfdKind kind = ExifIfdKind::Ifd;
    uint32_t index   = 0;  // For Ifd/SubIfd; otherwise 0.
    uint64_t offset  = 0;
    BlockId block    = kInvalidBlockId;
};

/// Resource limits applied during decode to bound hostile inputs.
struct ExifDecodeLimits final {
    uint32_t max_ifds            = 128;
    uint32_t max_entries_per_ifd = 4096;
    uint32_t max_total_entries   = 200000;
    uint64_t max_value_bytes     = 16ULL * 1024ULL * 1024ULL;
    /// Cumulative bytes copied into the destination store by this decode and
    /// all nested decoders (0 = unlimited).
    uint64_t max_arena_bytes = 64ULL * 1024ULL * 1024ULL;
};

/// Token strings used to label decoded IFD blocks.
struct ExifIfdTokenPolicy final {
    /// Prefix used for generic IFD chain blocks (e.g. `ifd0`, `ifd1`).
    std::string_view ifd_prefix = "ifd";
    /// Prefix used for SubIFD blocks (e.g. `subifd0`, `subifd1`).
    std::string_view subifd_prefix = "subifd";
    /// Token used for the EXIF sub-IFD pointer directory.
    std::string_view exif_ifd_token = "exififd";
    /// Token used for the GPS sub-IFD pointer directory.
    std::string_view gps_ifd_token = "gpsifd";
    /// Token used for the Interop sub-IFD pointer directory.
    std::string_view interop_ifd_token = "interopifd";
};

/// Decoder options for \ref decode_exif_tiff.
struct ExifDecodeOptions final {
    /// If true, pointer tags are preserved as entries in addition to being followed.
    bool include_pointer_tags = true;
    /// If true, decode EXIF PrintIM (0xC4A5) into \ref MetaKeyKind::PrintImField.
    bool decode_printim = true;
    /// If true, decode GeoTIFF GeoKeyDirectoryTag (0x87AF) into
    /// \ref MetaKeyKind::GeotiffKey entries (best-effort).
    bool decode_geotiff = true;
    /// If true, attempt best-effort MakerNote decoding (vendor blocks).
    bool decode_makernote = false;
    /// If true, attempt best-effort decoding of embedded containers stored as
    /// EXIF tag byte blobs (for example, Panasonic RW2 `JpgFromRaw`).
    bool decode_embedded_containers = false;
    /// IFD token naming policy (affects emitted EXIF key IFD strings).
    ExifIfdTokenPolicy tokens;
    ExifDecodeLimits limits;
};

/// Aggregated decode statistics.
struct ExifDecodeResult final {
    ExifDecodeStatus status      = ExifDecodeStatus::Ok;
    uint32_t ifds_written        = 0;
    uint32_t ifds_needed         = 0;
    uint32_t entries_decoded     = 0;
    ExifLimitReason limit_reason = ExifLimitReason::None;
    uint64_t limit_ifd_offset    = 0;
    uint16_t limit_tag           = 0;
};

/**
 * \brief Decodes a TIFF header + IFD chain and appends tags into \p store.
 *
 * The decoded entries use:
 * - \ref MetaKeyKind::ExifTag
 * - an IFD token string such as `"ifd0"`, `"exififd"`, `"gpsifd"`, `"subifd0"`
 * - the numeric TIFF tag id.
 *
 * Provenance is recorded in \ref Origin (block + order + wire type/count).
 *
 * \param tiff_bytes TIFF header + IFD stream (from an EXIF blob or a TIFF/DNG file).
 * \param store Destination \ref MetaStore (entries are appended).
 * \param out_ifds Optional output array for decoded IFD references (may be empty).
 * \param options Decode options + limits.
 */
ExifDecodeResult
decode_exif_tiff(std::span<const std::byte> tiff_bytes, MetaStore& store,
                 std::span<ExifIfdRef> out_ifds,
                 const ExifDecodeOptions& options) noexcept;

/**
 * \brief Estimates EXIF/TIFF decode counts using the same limits/options.
 *
 * This function performs a bounded decode pass into an internal scratch store
 * and returns the same status/counter model as \ref decode_exif_tiff.
 * Callers can use this to preflight entry counts before full decode/export.
 */
ExifDecodeResult
measure_exif_tiff(std::span<const std::byte> tiff_bytes,
                  const ExifDecodeOptions& options
                  = ExifDecodeOptions {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
