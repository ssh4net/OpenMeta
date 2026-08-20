// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"
#include "openmeta/random_access_source.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file container_scan.h
 * \brief Container scanners that locate metadata blocks within file bytes.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Scanner result status.
enum class ScanStatus : uint8_t {
    Ok,
    /// Output buffer was too small; \ref ScanResult::needed reports required size.
    OutputTruncated,
    /// The bytes do not match the container format handled by the scanner.
    Unsupported,
    /// The container structure is malformed or inconsistent.
    Malformed,
};

/// Supported high-level container formats for block scanning.
enum class ContainerFormat : uint8_t {
    Unknown,
    Jpeg,
    Png,
    Webp,
    Gif,
    Tiff,
    Crw,
    Raf,
    X3f,
    Jp2,
    Jxl,
    Heif,
    Avif,
    Cr3,
};

/// Logical kind of a discovered metadata block.
enum class ContainerBlockKind : uint8_t {
    Unknown,
    Exif,
    /// Canon CRW (CIFF) directory tree (non-TIFF metadata container).
    Ciff,
    MakerNote,
    Xmp,
    XmpExtended,
    /// JPEG Universal Metadata Box Format payload (including C2PA manifests).
    Jumbf,
    Icc,
    IptcIim,
    PhotoshopIrB,
    Mpf,
    Comment,
    Text,
    CompressedMetadata,
};

/// Compression type for the block payload bytes (if any).
enum class BlockCompression : uint8_t {
    None,
    Deflate,
    Brotli,
};

/// Chunking scheme used to represent a logical stream split across blocks.
enum class BlockChunking : uint8_t {
    None,
    JpegApp2SeqTotal,
    JpegXmpExtendedGuidOffset,
    GifSubBlocks,
    BmffExifTiffOffsetU32Be,
    BrobU32BeRealTypePrefix,
    Jp2UuidPayload,
    PsIrB8Bim,
};

/**
 * \brief Reference to a metadata payload within container bytes.
 *
 * All offsets are relative to the start of the full file byte buffer passed to
 * the scanner.
 *
 * \note Scanners are intentionally shallow: they locate blocks and annotate
 * compression/chunking but do not decompress or parse the inner formats.
 */
struct ContainerBlockRef final {
    ContainerFormat format       = ContainerFormat::Unknown;
    ContainerBlockKind kind      = ContainerBlockKind::Unknown;
    BlockCompression compression = BlockCompression::None;
    BlockChunking chunking       = BlockChunking::None;

    // The outer container block (e.g. JPEG segment, PNG chunk, BMFF box).
    uint64_t outer_offset = 0;
    uint64_t outer_size   = 0;

    // The metadata bytes inside the block (after signatures/prefix fields).
    uint64_t data_offset = 0;
    uint64_t data_size   = 0;

    // Container-specific identifier:
    // - JPEG: marker (0xFFEx)
    // - PNG: chunk type (FourCC)
    // - RIFF/WebP: chunk type (FourCC)
    // - BMFF/JP2/JXL: box type (FourCC)
    // - TIFF: tag id (u16)
    uint32_t id = 0;

    // Optional logical chunking info for reassembly.
    uint32_t part_index     = 0;  // 0-based
    uint32_t part_count     = 0;  // 0 if unknown
    uint64_t logical_offset = 0;  // byte offset within the logical stream
    uint64_t logical_size   = 0;  // total logical size (0 if unknown)
    uint64_t group          = 0;  // stable group id/hash (0 if none)

    // Extra container-specific data (e.g. brob wrapped type, BMFF Exif offset).
    uint32_t aux_u32 = 0;
};

struct ScanResult final {
    ScanStatus status = ScanStatus::Ok;
    uint32_t written  = 0;
    uint32_t needed   = 0;
};

/// Caller-owned storage for callback-backed container scanning.
struct ContainerRandomAccessScratch final {
    /// Reusable read-ahead cache. JPEG scanning requires up to 512 bytes to
    /// preserve bare-XMP detection parity with the contiguous scanner. PNG,
    /// WebP, JP2, JXL, and ISO-BMFF scanning require at least 32 bytes.
    std::span<std::byte> read_window;
    RandomAccessReadWindowOptions window_options;
};

/// Combined container-scan and source-I/O result.
struct ContainerRandomAccessScanResult final {
    ScanResult scan;
    RandomAccessReadState input;

    /// True when the positional source was read without an I/O or limit error.
    /// The container can still be malformed or unsupported; inspect `scan`.
    bool complete() const noexcept { return input.ok(); }
};

/// Packs four ASCII characters into a big-endian FourCC integer.
static constexpr uint32_t
fourcc(char a, char b, char c, char d) noexcept
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24)
           | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16)
           | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8)
           | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 0);
}


ScanResult
scan_auto(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept;

/**
 * \brief Measures discovered metadata block count for \ref scan_auto.
 *
 * Performs the same bounded scan logic and reports \ref ScanResult::needed
 * without requiring output block storage.
 */
ScanResult
measure_scan_auto(std::span<const std::byte> bytes) noexcept;

/// Scans a JPEG byte stream and returns all metadata segments found.
ScanResult
scan_jpeg(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept;
ScanResult
measure_scan_jpeg(std::span<const std::byte> bytes) noexcept;

/**
 * \brief Scans JPEG metadata segments through a bounded positional source.
 *
 * Discovered offsets are relative to \p jpeg, not the backing source. Callback
 * input uses caller-owned scratch and never reads entropy-coded image data.
 * A 512-byte read window preserves all contiguous scanner classifications;
 * smaller windows report \ref RandomAccessReadCode::ScratchTooSmall when a
 * larger segment probe is required.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ContainerRandomAccessScanResult
scan_jpeg_random_access(const RandomAccessSourceRange& jpeg,
                        std::span<ContainerBlockRef> out,
                        const ContainerRandomAccessScratch& scratch,
                        const RandomAccessReadLimits& read_limits
                        = RandomAccessReadLimits {}) noexcept;

/// Measures JPEG metadata block count through the same positional scan path.
ContainerRandomAccessScanResult
measure_scan_jpeg_random_access(const RandomAccessSourceRange& jpeg,
                                const ContainerRandomAccessScratch& scratch,
                                const RandomAccessReadLimits& read_limits
                                = RandomAccessReadLimits {}) noexcept;
/// Scans a PNG byte stream and returns all metadata chunks found.
ScanResult
scan_png(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept;
ScanResult
measure_scan_png(std::span<const std::byte> bytes) noexcept;
/**
 * \brief Scans PNG metadata chunks through a bounded positional source.
 *
 * Offsets are relative to \p png. Callback input requires at least 32 bytes of
 * caller-owned read-window storage and skips pixel payloads.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ContainerRandomAccessScanResult
scan_png_random_access(const RandomAccessSourceRange& png,
                       std::span<ContainerBlockRef> out,
                       const ContainerRandomAccessScratch& scratch,
                       const RandomAccessReadLimits& read_limits
                       = RandomAccessReadLimits {}) noexcept;
ContainerRandomAccessScanResult
measure_scan_png_random_access(const RandomAccessSourceRange& png,
                               const ContainerRandomAccessScratch& scratch,
                               const RandomAccessReadLimits& read_limits
                               = RandomAccessReadLimits {}) noexcept;
/// Scans a RIFF/WebP byte stream and returns all metadata chunks found.
ScanResult
scan_webp(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept;
ScanResult
measure_scan_webp(std::span<const std::byte> bytes) noexcept;
/**
 * \brief Scans WebP metadata chunks through a bounded positional source.
 *
 * Offsets are relative to \p webp. Callback input requires at least 32 bytes of
 * caller-owned read-window storage and skips image payloads.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ContainerRandomAccessScanResult
scan_webp_random_access(const RandomAccessSourceRange& webp,
                        std::span<ContainerBlockRef> out,
                        const ContainerRandomAccessScratch& scratch,
                        const RandomAccessReadLimits& read_limits
                        = RandomAccessReadLimits {}) noexcept;
ContainerRandomAccessScanResult
measure_scan_webp_random_access(const RandomAccessSourceRange& webp,
                                const ContainerRandomAccessScratch& scratch,
                                const RandomAccessReadLimits& read_limits
                                = RandomAccessReadLimits {}) noexcept;
/// Scans a GIF byte stream and returns all metadata extension blocks found.
ScanResult
scan_gif(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept;
ScanResult
measure_scan_gif(std::span<const std::byte> bytes) noexcept;
/// Scans a TIFF/DNG byte stream; the whole file is exposed as an EXIF/TIFF-IFD block.
ScanResult
scan_tiff(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept;
ScanResult
measure_scan_tiff(std::span<const std::byte> bytes) noexcept;
/// Scans a JPEG 2000 (JP2) byte stream and returns metadata boxes found.
ScanResult
scan_jp2(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept;
ScanResult
measure_scan_jp2(std::span<const std::byte> bytes) noexcept;
/**
 * \brief Scans JP2 metadata boxes through a bounded positional source.
 *
 * Offsets are relative to \p jp2. Callback input requires at least 32 bytes of
 * caller-owned read-window storage and skips codestream payloads.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ContainerRandomAccessScanResult
scan_jp2_random_access(const RandomAccessSourceRange& jp2,
                       std::span<ContainerBlockRef> out,
                       const ContainerRandomAccessScratch& scratch,
                       const RandomAccessReadLimits& read_limits
                       = RandomAccessReadLimits {}) noexcept;
ContainerRandomAccessScanResult
measure_scan_jp2_random_access(const RandomAccessSourceRange& jp2,
                               const ContainerRandomAccessScratch& scratch,
                               const RandomAccessReadLimits& read_limits
                               = RandomAccessReadLimits {}) noexcept;
/// Scans a JPEG XL container byte stream and returns metadata boxes found.
ScanResult
scan_jxl(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept;
ScanResult
measure_scan_jxl(std::span<const std::byte> bytes) noexcept;
/**
 * \brief Scans JXL metadata boxes through a bounded positional source.
 *
 * Offsets are relative to \p jxl. Callback input requires at least 32 bytes of
 * caller-owned read-window storage and skips codestream payloads.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ContainerRandomAccessScanResult
scan_jxl_random_access(const RandomAccessSourceRange& jxl,
                       std::span<ContainerBlockRef> out,
                       const ContainerRandomAccessScratch& scratch,
                       const RandomAccessReadLimits& read_limits
                       = RandomAccessReadLimits {}) noexcept;
ContainerRandomAccessScanResult
measure_scan_jxl_random_access(const RandomAccessSourceRange& jxl,
                               const ContainerRandomAccessScratch& scratch,
                               const RandomAccessReadLimits& read_limits
                               = RandomAccessReadLimits {}) noexcept;
/// Scans an ISO-BMFF (`ftyp`) container (e.g. HEIF/AVIF/CR3) and returns
/// metadata items found within `meta` boxes.
ScanResult
scan_bmff(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept;
ScanResult
measure_scan_bmff(std::span<const std::byte> bytes) noexcept;
/**
 * \brief Scans ISO-BMFF metadata through a bounded positional source.
 *
 * Offsets are relative to \p bmff. Callback input requires at least 32 bytes of
 * caller-owned read-window storage. Structural boxes and metadata item tables
 * are traversed without reading `mdat` payloads.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
ContainerRandomAccessScanResult
scan_bmff_random_access(const RandomAccessSourceRange& bmff,
                        std::span<ContainerBlockRef> out,
                        const ContainerRandomAccessScratch& scratch,
                        const RandomAccessReadLimits& read_limits
                        = RandomAccessReadLimits {}) noexcept;
ContainerRandomAccessScanResult
measure_scan_bmff_random_access(const RandomAccessSourceRange& bmff,
                                const ContainerRandomAccessScratch& scratch,
                                const RandomAccessReadLimits& read_limits
                                = RandomAccessReadLimits {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
