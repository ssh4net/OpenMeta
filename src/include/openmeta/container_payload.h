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

}  // namespace openmeta
OPENMETA_PUBLIC_END
