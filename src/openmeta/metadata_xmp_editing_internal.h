// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string_view>

namespace openmeta::detail {

enum class XmpStructuredEditScopeKind : uint8_t {
    CoreStructure,
    StructuredArray,
    LangAlt,
    IndexedTextArray,
};

enum class XmpStructuredEditLeafShape : uint8_t {
    Scalar,
    Indexed,
    LangAlt,
};

struct XmpStructuredEditScope final {
    std::string_view schema_ns;
    std::string_view property_path;
    std::string_view root;
    std::string_view language;
    uint32_t item_index = 0U;
    XmpStructuredEditScopeKind kind
        = XmpStructuredEditScopeKind::CoreStructure;
    bool has_item_index = false;
    bool has_language = false;
};

struct XmpStructuredEditLeaf final {
    std::string_view root;
    std::string_view shape_path;
    std::string_view language;
    uint32_t item_index = 0U;
    XmpStructuredEditLeafShape shape = XmpStructuredEditLeafShape::Scalar;
    uint8_t path_depth = 0U;
    bool has_item_index = false;
    bool has_language = false;
};

bool parse_xmp_structured_edit_scope(std::string_view schema_ns,
                                     std::string_view property_path,
                                     XmpStructuredEditScope* out) noexcept;

bool parse_xmp_structured_edit_leaf(std::string_view schema_ns,
                                    std::string_view property_path,
                                    XmpStructuredEditLeaf* out) noexcept;

bool xmp_structured_edit_path_has_root(std::string_view schema_ns,
                                       std::string_view property_path,
                                       std::string_view root) noexcept;

}  // namespace openmeta::detail
