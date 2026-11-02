// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstdint>
#include <string_view>

/**
 * \file xmp_structure.h
 * \brief Explicit bounded RDF container declarations for custom XMP.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Contract version for explicit structured XMP declarations.
inline constexpr uint32_t kXmpStructureContractVersion = 1U;

/// Persistent XMP container kind encoded in the reserved Other wire-code range
/// 0x5840..0x584f. Values 1..4 identify supported containers; all other values
/// in that range are reserved and invalid when used on an XMP property.
enum class XmpContainerKind : uint8_t {
    /// No recognized container wire form is present.
    None = 0U,
    /// A property whose children are uniquely named fields.
    Struct = 1U,
    /// An unordered array with positive indexed item paths.
    Bag = 2U,
    /// An ordered array with positive indexed item paths.
    Seq = 3U,
    /// Language alternatives selected by terminal `@xml:lang` selectors.
    LangAlt = 4U,
};

/// Shared resource ceilings for custom structured XMP validation and output.
/// Every field must be nonzero and no greater than its default hard maximum.
/// Callers can lower limits for a request but cannot raise the hard maxima.
/// Node and request accounting includes retained declarations (also deleted
/// declarations) and active text leaves selected into declared root scopes.
struct XmpStructureLimits final {
    uint32_t max_nodes = 4096U;  ///< Retained declarations plus selected leaves.
    uint8_t max_depth       = 8U;     ///< Expanded named and selector levels.
    uint32_t max_namespaces = 64U;    ///< Distinct decoded URIs in that input.
    uint32_t max_path_bytes = 4096U;  ///< Bytes in one property path.
    uint32_t max_namespace_bytes = 4096U;  ///< Decoded bytes in one URI.
    uint64_t max_request_bytes
        = 16ULL * 1024ULL * 1024ULL;    ///< URI, path, and selected text bytes.
    uint32_t max_array_index = 65535U;  ///< Largest canonical positive index.
};

/// Returns whether an entry carries a recognized XMP container wire form.
/// Value-shape validation is provided by the shared private structure core.
XmpContainerKind
xmp_container_kind(const Entry& entry) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
