// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/container_scan.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file container_payload.h
 * \brief Reassembles and optionally decompresses logical metadata payloads.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Payload extraction result status.
enum class PayloadStatus : uint8_t {
    Ok,
    /// Output buffer was too small; \ref PayloadResult::needed reports required size.
    OutputTruncated,
    /// The payload encoding requires an optional dependency that is not available.
    Unsupported,
    /// The container data is malformed or inconsistent.
    Malformed,
    /// Resource limits were exceeded (e.g. too many parts or too large output).
    LimitExceeded,
};

/// Resource limits applied during payload extraction to bound hostile inputs.
struct PayloadLimits final {
    uint32_t max_parts        = 1U << 14;
    uint64_t max_output_bytes = 64ULL * 1024ULL * 1024ULL;
};

/// Options for payload extraction.
struct PayloadOptions final {
    /// If true, attempt to decompress payloads marked with \ref BlockCompression.
    bool decompress = true;
    PayloadLimits limits;
};

struct PayloadResult final {
    PayloadStatus status = PayloadStatus::Ok;
    uint64_t written     = 0;
    uint64_t needed      = 0;
};

/// Caller-owned storage for callback-backed payload extraction.
struct PayloadRandomAccessScratch final {
    /// Reusable cache for GIF sub-block framing and small positional reads.
    std::span<std::byte> read_window;
    /// Storage for a compressed logical stream before decompression.
    std::span<std::byte> compressed;
    RandomAccessReadWindowOptions window_options;
};

/// Combined logical-payload and source-I/O result.
struct PayloadRandomAccessResult final {
    PayloadResult payload;
    RandomAccessReadState input;
    /// Required compressed-stream storage when `scratch.compressed` was too
    /// small. Zero means no stream was skipped for this reason.
    uint64_t compressed_scratch_needed = 0U;

    bool complete() const noexcept
    {
        return input.ok() && compressed_scratch_needed == 0U;
    }
};

/**
 * \brief Extracts the logical payload for a discovered block.
 *
 * The function uses \p seed_index to identify the logical stream to extract and,
 * when applicable, gathers additional parts from \p blocks to reassemble it.
 *
 * Supported reassembly:
 * - \ref BlockChunking::GifSubBlocks
 * - \ref BlockChunking::JpegApp2SeqTotal (ICC)
 * - \ref BlockChunking::JpegXmpExtendedGuidOffset
 * - Multi-part logical streams with \ref ContainerBlockRef::part_count > 1
 *
 * Supported decompression (optional):
 * - \ref BlockCompression::Deflate (zlib)
 * - \ref BlockCompression::Brotli
 *
 * Callers provide buffers to keep data flow explicit and allocation-free.
 */
PayloadResult
extract_payload(std::span<const std::byte> file_bytes,
                std::span<const ContainerBlockRef> blocks, uint32_t seed_index,
                std::span<std::byte> out_payload,
                std::span<uint32_t> scratch_indices,
                const PayloadOptions& options) noexcept;

/**
 * \brief Extracts one logical metadata payload through positional reads.
 *
 * Offsets in \p blocks are relative to \p source. The function reads only the
 * selected logical stream. Direct uncompressed reads stop at the accepted
 * output prefix; framing read-ahead for GIF sub-blocks may overlap adjacent
 * payload bytes. Compressed streams use caller-owned compressed scratch before
 * bounded decompression into \p out_payload.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
PayloadRandomAccessResult
extract_payload_random_access(const RandomAccessSourceRange& source,
                              std::span<const ContainerBlockRef> blocks,
                              uint32_t seed_index,
                              std::span<std::byte> out_payload,
                              std::span<uint32_t> scratch_indices,
                              const PayloadRandomAccessScratch& scratch,
                              const PayloadOptions& options,
                              const RandomAccessReadLimits& read_limits
                              = RandomAccessReadLimits {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
