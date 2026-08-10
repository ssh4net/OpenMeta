// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file exr_decode.h
 * \brief Decoder for OpenEXR header attributes.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable EXR canonical value encoding contract version.
inline constexpr uint32_t kExrCanonicalEncodingVersion = 1U;

/// OpenEXR decode result status.
enum class ExrDecodeStatus : uint8_t {
    Ok,
    /// The bytes do not look like an OpenEXR file.
    Unsupported,
    /// The EXR header is malformed or inconsistent.
    Malformed,
    /// Resource limits were exceeded.
    LimitExceeded,
};

/// Resource limits applied during EXR header decode.
struct ExrDecodeLimits final {
    uint32_t max_parts                 = 64;
    uint32_t max_attributes_per_part   = 1U << 16;
    uint32_t max_attributes            = 200000;
    uint32_t max_name_bytes            = 1024;
    uint32_t max_type_name_bytes       = 1024;
    uint32_t max_attribute_bytes       = 8U * 1024U * 1024U;
    uint64_t max_total_attribute_bytes = 64ULL * 1024ULL * 1024ULL;
};

/// Decoder options for \ref decode_exr_header.
struct ExrDecodeOptions final {
    /// If true, decodes known scalar/vector EXR attribute types into typed values.
    /// Unknown and complex attribute types are always preserved as raw bytes.
    bool decode_known_types = true;
    /// If true, preserves original EXR type name for unknown/custom attrs in
    /// \ref Origin::wire_type_name.
    bool preserve_unknown_type_name = true;
    ExrDecodeLimits limits;
};

struct ExrDecodeResult final {
    ExrDecodeStatus status   = ExrDecodeStatus::Ok;
    uint32_t parts_decoded   = 0;
    uint32_t entries_decoded = 0;
};

/**
 * \brief Decodes OpenEXR header attributes and appends entries into \p store.
 *
 * Each decoded header attribute becomes one \ref Entry with:
 * - \ref MetaKeyKind::ExrAttribute (`part_index` + attribute name)
 * - typed \ref MetaValue for common scalar/vector/matrix EXR types
 * - raw \ref MetaValueKind::Bytes for unknown/complex EXR types
 *
 * Duplicate attribute names are preserved.
 */
ExrDecodeResult
decode_exr_header(std::span<const std::byte> exr_bytes, MetaStore& store,
                  EntryFlags flags = EntryFlags::None,
                  const ExrDecodeOptions& options
                  = ExrDecodeOptions {}) noexcept;

/**
 * \brief Estimates EXR header decode counts using the same limits/options.
 */
ExrDecodeResult
measure_exr_header(std::span<const std::byte> exr_bytes,
                   const ExrDecodeOptions& options
                   = ExrDecodeOptions {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
