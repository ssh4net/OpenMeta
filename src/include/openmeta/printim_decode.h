// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file printim_decode.h
 * \brief Decoder for the EXIF PrintIM (0xC4A5) embedded block.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// PrintIM decode result status.
enum class PrintImDecodeStatus : uint8_t {
    Ok,
    /// Input does not look like a PrintIM block.
    Unsupported,
    /// Input is truncated or structurally invalid.
    Malformed,
    /// Refused due to size/entry-count limits.
    LimitExceeded,
};

/// Resource limits for decoding a PrintIM block.
struct PrintImDecodeLimits final {
    /// Maximum number of PrintIM entries to decode.
    uint32_t max_entries = 4096;
    /// Maximum input bytes to accept (0 = unlimited).
    uint64_t max_bytes = 256ULL * 1024ULL;
};

/// PrintIM decode result summary.
struct PrintImDecodeResult final {
    PrintImDecodeStatus status = PrintImDecodeStatus::Unsupported;
    uint32_t entries_decoded   = 0;  // Includes the version field when present.
};

/**
 * \brief Decodes a PrintIM block and appends fields into \p store.
 *
 * On success, this creates a new block in \p store containing
 * \ref MetaKeyKind::PrintImField entries. The original EXIF tag should still
 * be preserved separately by the EXIF/TIFF decoder.
 *
 * Field naming:
 * - `version` for the header version (ASCII, 4 bytes)
 * - `0xNNNN` for each 16-bit entry id
 */
PrintImDecodeResult
decode_printim(std::span<const std::byte> bytes, MetaStore& store,
               const PrintImDecodeLimits& limits) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
