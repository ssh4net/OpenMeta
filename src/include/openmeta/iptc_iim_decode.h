// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file iptc_iim_decode.h
 * \brief Decoder for IPTC-IIM dataset streams.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// IPTC-IIM decode result status.
enum class IptcIimDecodeStatus : uint8_t {
    Ok,
    /// The bytes do not look like an IPTC-IIM dataset stream.
    Unsupported,
    /// The stream is malformed or inconsistent.
    Malformed,
    /// Resource limits were exceeded.
    LimitExceeded,
};

/// Resource limits applied during IPTC-IIM decode to bound hostile inputs.
struct IptcIimDecodeLimits final {
    uint32_t max_datasets      = 200000;
    uint32_t max_dataset_bytes = 8U * 1024U * 1024U;
    uint64_t max_total_bytes   = 64ULL * 1024ULL * 1024ULL;
};

/// Decoder options for \ref decode_iptc_iim.
struct IptcIimDecodeOptions final {
    IptcIimDecodeLimits limits;
};

struct IptcIimDecodeResult final {
    IptcIimDecodeStatus status = IptcIimDecodeStatus::Ok;
    uint32_t entries_decoded   = 0;
};

/**
 * \brief Decodes an IPTC-IIM dataset stream and appends datasets into \p store.
 *
 * Each dataset becomes one \ref Entry with:
 * - \ref MetaKeyKind::IptcDataset (record + dataset id)
 * - \ref MetaValueKind::Bytes (raw dataset payload)
 *
 * Duplicate datasets are preserved.
 */
IptcIimDecodeResult
decode_iptc_iim(std::span<const std::byte> iptc_bytes, MetaStore& store,
                EntryFlags flags = EntryFlags::None,
                const IptcIimDecodeOptions& options
                = IptcIimDecodeOptions {}) noexcept;

/**
 * \brief Estimates IPTC-IIM dataset count using the same limits/options.
 */
IptcIimDecodeResult
measure_iptc_iim(std::span<const std::byte> iptc_bytes,
                 const IptcIimDecodeOptions& options
                 = IptcIimDecodeOptions {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
