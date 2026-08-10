// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/container_scan.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file preview_extract.h
 * \brief Read-only preview/thumbnail candidate discovery and extraction.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Candidate preview source kind.
enum class PreviewKind : uint8_t {
    /// EXIF/TIFF pair `JPEGInterchangeFormat` (0x0201) + length (0x0202).
    ExifJpegInterchange,
    /// EXIF/TIFF blob tag `JpgFromRaw` (0x002E).
    ExifJpgFromRaw,
    /// EXIF/TIFF blob tag `JpgFromRaw2` (0x0127).
    ExifJpgFromRaw2,
    /// Canon CR3 preview JPEG stored in a PRVW stream inside a UUID box.
    Cr3PrvwJpeg,
};

/// Preview candidate discovered in a container.
struct PreviewCandidate final {
    PreviewKind kind            = PreviewKind::ExifJpegInterchange;
    ContainerFormat format      = ContainerFormat::Unknown;
    uint32_t block_index        = 0;
    uint16_t offset_tag         = 0;
    uint16_t length_tag         = 0;
    uint64_t file_offset        = 0;
    uint64_t size               = 0;
    bool has_jpeg_soi_signature = false;
};

/// Status for preview candidate discovery.
enum class PreviewScanStatus : uint8_t {
    Ok,
    OutputTruncated,
    Unsupported,
    Malformed,
    LimitExceeded,
};

/// Limits for preview candidate discovery.
struct PreviewScanLimits final {
    uint32_t max_ifds          = 256;
    uint32_t max_total_entries = 8192;
    uint64_t max_preview_bytes = 512ULL * 1024ULL * 1024ULL;
};

/// Options for preview candidate discovery.
struct PreviewScanOptions final {
    bool include_exif_jpeg_interchange = true;
    bool include_jpg_from_raw          = true;
    bool include_cr3_prvw_jpeg         = true;
    bool require_jpeg_soi              = false;
    PreviewScanLimits limits;
};

/// Result for preview candidate discovery.
struct PreviewScanResult final {
    PreviewScanStatus status = PreviewScanStatus::Ok;
    uint32_t written         = 0;
    uint32_t needed          = 0;
};

/**
 * \brief Finds preview candidates from already scanned blocks.
 *
 * This function currently analyzes EXIF/TIFF blocks and discovers:
 * - `JPEGInterchangeFormat`/`JPEGInterchangeFormatLength` pairs
 * - `JpgFromRaw` and `JpgFromRaw2` byte blobs
 * - Canon CR3 PRVW UUID stream preview JPEGs
 *
 * Candidates are file-relative (`file_offset` + `size`) and can be copied with
 * \ref extract_preview_candidate.
 */
PreviewScanResult
find_preview_candidates(std::span<const std::byte> file_bytes,
                        std::span<const ContainerBlockRef> blocks,
                        std::span<PreviewCandidate> out,
                        const PreviewScanOptions& options) noexcept;

/**
 * \brief Convenience wrapper that runs \ref scan_auto first, then
 * \ref find_preview_candidates.
 */
PreviewScanResult
scan_preview_candidates(std::span<const std::byte> file_bytes,
                        std::span<ContainerBlockRef> blocks_scratch,
                        std::span<PreviewCandidate> out,
                        const PreviewScanOptions& options) noexcept;

/// Status for preview extraction.
enum class PreviewExtractStatus : uint8_t {
    Ok,
    OutputTruncated,
    Malformed,
    LimitExceeded,
};

/// Options for preview extraction.
struct PreviewExtractOptions final {
    uint64_t max_output_bytes = 128ULL * 1024ULL * 1024ULL;
    bool require_jpeg_soi     = false;
};

/// Result for preview extraction.
struct PreviewExtractResult final {
    PreviewExtractStatus status = PreviewExtractStatus::Ok;
    uint64_t written            = 0;
    uint64_t needed             = 0;
};

/**
 * \brief Extracts bytes for one preview candidate into \p out.
 */
PreviewExtractResult
extract_preview_candidate(std::span<const std::byte> file_bytes,
                          const PreviewCandidate& candidate,
                          std::span<std::byte> out,
                          const PreviewExtractOptions& options) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
