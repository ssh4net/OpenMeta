// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openmeta/xmp_structure.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace openmeta {
struct MetadataAuthoringEntry;
}

namespace openmeta::detail {

inline constexpr uint16_t kXmpStructureWireBase    = 0x5840U;
inline constexpr uint16_t kXmpStructureWireLast    = 0x584fU;
inline constexpr uint32_t kInvalidXmpStructureNode = 0xffffffffU;
inline constexpr size_t kXmpStructureMaxDepth      = 8U;
inline constexpr size_t kXmpStructureMaxNamespaces = 64U;
inline constexpr size_t kXmpStructureMaxUriBytes   = 4096U;

enum class XmpStructureStatus : uint8_t {
    Ok,
    InvalidOptions,
    InvalidMetadata,
    LimitExceeded,
};

enum class XmpStructureSelectorKind : uint8_t {
    None,
    Index,
    Language,
};

struct XmpStructureNamespaceRef final {
    /// Literal URI, or lowercase hexadecimal URI bytes from an nsu_ token.
    std::string_view uri;
    bool uri_is_hex = false;
};

struct XmpStructureSelector final {
    XmpStructureSelectorKind kind = XmpStructureSelectorKind::None;
    uint32_t start                = 0U;
    uint32_t index                = 0U;
    std::string_view language;
};

struct XmpStructurePathSegment final {
    std::string_view token;
    std::string_view local_name;
    std::string_view prefix;
    XmpStructureNamespaceRef name_space;
    std::array<XmpStructureSelector, kXmpStructureMaxDepth> selectors {};
    uint32_t start         = 0U;
    uint32_t size          = 0U;
    uint8_t selector_count = 0U;
};

/// Bounded parsed view. Every string view borrows the input property path.
/// `depth` counts slash-separated names; `expanded_depth` also counts each
/// numeric or language selector as its own expanded node level.
struct XmpStructurePath final {
    std::string_view root;
    std::array<XmpStructurePathSegment, kXmpStructureMaxDepth> segments {};
    uint8_t depth          = 0U;
    uint8_t expanded_depth = 0U;
};

struct XmpStructureNamespace final {
    XmpStructureNamespaceRef ref;
};

/// One active declaration or selected leaf in a declared custom tree.
/// `schema_namespace_index` identifies the root schema URI. The final field
/// URI is stored separately because nested qualified fields can use another
/// namespace. All views borrow the unchanged source store arena.
struct XmpStructureNode final {
    EntryId entry                   = kInvalidEntryId;
    uint32_t schema_namespace_index = kInvalidXmpStructureNode;
    uint32_t field_namespace_index  = kInvalidXmpStructureNode;
    uint32_t parent_node            = kInvalidXmpStructureNode;
    XmpContainerKind container_kind = XmpContainerKind::None;
    std::string_view path;
    uint8_t depth = 0U;
    XmpStructurePathSegment last_segment;
};

/// Parent-grouped canonical child order, shared with the XMP writer.
struct XmpStructureChildRef final {
    uint32_t parent_node              = kInvalidXmpStructureNode;
    uint32_t node_index               = kInvalidXmpStructureNode;
    uint32_t field_namespace_index    = kInvalidXmpStructureNode;
    XmpStructureSelectorKind selector = XmpStructureSelectorKind::None;
    uint32_t index                    = 0U;
    std::string_view local_name;
    std::string_view language;
};

struct XmpStructureTree final {
    std::vector<XmpStructureNamespace> namespaces;
    std::vector<XmpStructureNode> nodes;
    std::vector<XmpStructureChildRef> ordered_children;

    void clear() noexcept
    {
        ordered_children.clear();
        nodes.clear();
        namespaces.clear();
    }
};

struct XmpStructureBuildResult final {
    XmpStructureStatus status = XmpStructureStatus::Ok;
    EntryId failed_entry      = kInvalidEntryId;

    bool ok() const noexcept { return status == XmpStructureStatus::Ok; }
};

bool
xmp_structure_limits_valid(const XmpStructureLimits& limits) noexcept;
bool
xmp_structure_wire_code_reserved(uint16_t code) noexcept;
/// Exact persistent marker predicate: XMP property + Other wire family +
/// reserved 0x5840..0x584f numeric range. Shape/kind validation is separate.
bool
xmp_structure_is_declaration(const Entry& entry) noexcept;
bool
xmp_container_value_shape_is_valid(MetaValueKind value_kind,
                                   MetaElementType element_type,
                                   TextEncoding text_encoding,
                                   uint32_t value_count, uint64_t payload_bytes,
                                   WireType wire_type, uint32_t wire_count,
                                   uint64_t wire_type_name_bytes) noexcept;
bool
xmp_container_entry_shape_is_valid(const Entry& entry) noexcept;
bool
xmp_container_view_shape_is_valid(const MetadataAuthoringEntry& entry) noexcept;

bool
xmp_structure_is_standard_namespace(std::string_view uri) noexcept;
bool
xmp_structure_namespace_is_custom(std::string_view uri) noexcept;
bool
xmp_structure_namespace_uri(const XmpStructureNamespaceRef& ref, char* storage,
                            size_t storage_size,
                            std::string_view* uri) noexcept;
bool
xmp_structure_parse_path(std::string_view path, uint8_t max_depth,
                         uint32_t max_array_index,
                         XmpStructurePath* out) noexcept;
bool
xmp_structure_extract_root(std::string_view path, uint32_t max_array_index,
                           std::string_view* root) noexcept;
bool
xmp_structure_namespace_equal(const XmpStructureNamespaceRef& a,
                              const XmpStructureNamespaceRef& b) noexcept;
bool
xmp_structure_language_equal(std::string_view a, std::string_view b) noexcept;
bool
xmp_structure_language_is_default(std::string_view language) noexcept;

/// Builds a bounded active tree from retained declarations. `output` changes
/// only on success; an empty successful result replaces any previous tree.
XmpStructureBuildResult
prepare_xmp_structure_tree(const MetaStore& store,
                           const XmpStructureLimits& limits,
                           XmpStructureTree* output) noexcept;

}  // namespace openmeta::detail
