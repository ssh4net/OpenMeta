// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file icc_decode.h
 * \brief Decoder for ICC profile blobs (header + tag table).
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// ICC decode result status.
enum class IccDecodeStatus : uint8_t {
    Ok,
    /// The bytes do not look like an ICC profile.
    Unsupported,
    /// The profile is malformed or inconsistent.
    Malformed,
    /// Resource limits were exceeded.
    LimitExceeded,
};

/// Resource limits applied during ICC decode to bound hostile inputs.
struct IccDecodeLimits final {
    uint32_t max_tags            = 1U << 16;
    uint32_t max_tag_bytes       = 32U * 1024U * 1024U;
    uint64_t max_total_tag_bytes = 64ULL * 1024ULL * 1024ULL;
};

/// Decoder options for \ref decode_icc_profile.
struct IccDecodeOptions final {
    IccDecodeLimits limits;
};

struct IccDecodeResult final {
    IccDecodeStatus status   = IccDecodeStatus::Ok;
    uint32_t entries_decoded = 0;
};

/**
 * \brief Decodes an ICC profile header and tag table into \p store.
 *
 * The decoder emits:
 * - \ref MetaKeyKind::IccHeaderField entries for common header fields
 *   (typed when interpretation is stable: signature/u32/u64/s15Fixed16 arrays)
 * - \ref MetaKeyKind::IccTag entries (tag signature -> raw tag bytes)
 */
IccDecodeResult
decode_icc_profile(std::span<const std::byte> icc_bytes, MetaStore& store,
                   const IccDecodeOptions& options
                   = IccDecodeOptions {}) noexcept;

/**
 * \brief Estimates ICC decode entry count using the same limits/options.
 */
IccDecodeResult
measure_icc_profile(std::span<const std::byte> icc_bytes,
                    const IccDecodeOptions& options
                    = IccDecodeOptions {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
