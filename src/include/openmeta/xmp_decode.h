// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file xmp_decode.h
 * \brief Decoder for XMP packets (RDF/XML).
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// XMP decode result status.
enum class XmpDecodeStatus : uint8_t {
    Ok,
    OutputTruncated,
    Unsupported,
    Malformed,
    LimitExceeded,
};

/// Controls how malformed XMP packets are reported to callers.
///
/// Some workflows prefer treating malformed XML as "best-effort partial output"
/// rather than a hard failure.
enum class XmpDecodeMalformedMode : uint8_t {
    /// Report malformed XML as \ref XmpDecodeStatus::Malformed.
    Malformed,
    /// Report malformed XML as \ref XmpDecodeStatus::OutputTruncated.
    OutputTruncated,
};

/// Resource limits applied during XMP decode to bound hostile inputs.
struct XmpDecodeLimits final {
    uint32_t max_depth      = 128;
    uint32_t max_properties = 200000;

    /// Caps the input XMP packet size (0 = unlimited).
    uint64_t max_input_bytes = 64ULL * 1024ULL * 1024ULL;

    /// Max bytes per decoded property path string.
    uint32_t max_path_bytes = 1024;

    /// Max bytes in a schema namespace URI copied into a property key.
    uint32_t max_namespace_bytes = 4096;

    /// Max text bytes per decoded value (element/attribute).
    uint32_t max_value_bytes = 8U * 1024U * 1024U;

    /// Max total text bytes accumulated across values (0 = unlimited).
    uint64_t max_total_value_bytes = 64ULL * 1024ULL * 1024ULL;

    /// Cumulative bytes copied into the destination store (0 = unlimited).
    uint64_t max_arena_bytes = 64ULL * 1024ULL * 1024ULL;
};

/// Decoder options for \ref decode_xmp_packet.
struct XmpDecodeOptions final {
    /// If true, decodes attributes on `rdf:Description` as XMP properties.
    bool decode_description_attributes = true;
    /// Controls whether malformed XML should be reported as Malformed or as
    /// best-effort OutputTruncated.
    XmpDecodeMalformedMode malformed_mode = XmpDecodeMalformedMode::Malformed;
    XmpDecodeLimits limits;
};

struct XmpDecodeResult final {
    XmpDecodeStatus status   = XmpDecodeStatus::Ok;
    uint32_t entries_decoded = 0;
};

/**
 * \brief Decodes an XMP packet and appends properties into \p store.
 *
 * The decoder emits one \ref Entry per decoded property value with:
 * - \ref MetaKeyKind::XmpProperty (`schema_ns` URI + `property_path`)
 * - \ref MetaValueKind::Text (UTF-8)
 *
 * Duplicate properties are preserved.
 */
XmpDecodeResult
decode_xmp_packet(std::span<const std::byte> xmp_bytes, MetaStore& store,
                  EntryFlags flags = EntryFlags::None,
                  const XmpDecodeOptions& options
                  = XmpDecodeOptions {}) noexcept;

/**
 * \brief Estimates XMP decode counts using the same limits/options.
 *
 * This function performs a bounded decode pass into an internal scratch store
 * and returns the same status/counter model as \ref decode_xmp_packet.
 */
XmpDecodeResult
measure_xmp_packet(std::span<const std::byte> xmp_bytes,
                   const XmpDecodeOptions& options
                   = XmpDecodeOptions {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
