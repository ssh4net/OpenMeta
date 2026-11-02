// SPDX-License-Identifier: Apache-2.0

#include "openmeta/xmp_structure.h"

#include "metadata_logical_field_internal.h"
#include "openmeta/meta_key.h"
#include "openmeta/metadata_authoring.h"
#include "xmp_structure_internal.h"

#include "metadata_structured_fields_internal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace openmeta {

XmpContainerKind
xmp_container_kind(const Entry& entry) noexcept
{
    if (entry.key.kind != MetaKeyKind::XmpProperty
        || entry.origin.wire_type.family != WireFamily::Other
        || entry.origin.wire_type.code <= detail::kXmpStructureWireBase
        || entry.origin.wire_type.code > detail::kXmpStructureWireBase + 4U) {
        return XmpContainerKind::None;
    }
    return static_cast<XmpContainerKind>(entry.origin.wire_type.code
                                         - detail::kXmpStructureWireBase);
}

MetadataAuthoringEntry
make_xmp_container_entry(std::string_view schema_ns,
                         std::string_view property_path,
                         XmpContainerKind kind) noexcept
{
    MetadataAuthoringEntry entry;
    entry.key = make_xmp_property_key_view(schema_ns, property_path);
    const uint8_t kind_value = static_cast<uint8_t>(kind);
    const uint16_t code
        = detail::kXmpStructureWireBase
          + ((kind_value >= static_cast<uint8_t>(XmpContainerKind::Struct)
              && kind_value <= static_cast<uint8_t>(XmpContainerKind::LangAlt))
                 ? kind_value
                 : 0U);
    entry.wire_type  = WireType { WireFamily::Other, code };
    entry.wire_count = 0U;
    return entry;
}

}  // namespace openmeta

namespace openmeta::detail {
namespace {

    struct RootRef final {
        std::string_view schema_ns;
        std::string_view root;
        EntryId entry        = kInvalidEntryId;
        bool has_active_root = false;
    };

    static char language_lower(char value) noexcept;
    static bool language_equal_impl(std::string_view a,
                                    std::string_view b) noexcept;

    static constexpr std::array<std::string_view, 29U> kStandardNamespaces {
        "adobe:ns:meta/",
        "http://www.w3.org/1999/02/22-rdf-syntax-ns#",
        "http://www.w3.org/XML/1998/namespace",
        "http://ns.adobe.com/xap/1.0/",
        "http://ns.adobe.com/tiff/1.0/",
        "http://ns.adobe.com/exif/1.0/",
        "http://cipa.jp/exif/1.0/",
        "http://ns.adobe.com/exif/1.0/aux/",
        "http://purl.org/dc/elements/1.1/",
        "http://ns.adobe.com/pdf/1.3/",
        "http://ns.adobe.com/xap/1.0/bj/",
        "http://ns.useplus.org/ldf/xmp/1.0/",
        "http://ns.adobe.com/camera-raw-settings/1.0/",
        "http://ns.adobe.com/dng/1.0/",
        "http://ns.adobe.com/lightroom/1.0/",
        "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "http://ns.adobe.com/xap/1.0/mm/",
        "http://ns.adobe.com/xap/1.0/t/pg/",
        "http://ns.adobe.com/xap/1.0/rights/",
        "http://ns.adobe.com/xap/1.0/sType/Dimensions#",
        "http://ns.adobe.com/xap/1.0/sType/ResourceEvent#",
        "http://ns.adobe.com/xap/1.0/sType/Font#",
        "http://ns.adobe.com/xap/1.0/sType/Job#",
        "http://ns.adobe.com/xap/1.0/sType/ManifestItem#",
        "http://ns.adobe.com/xap/1.0/sType/ResourceRef#",
        "http://ns.adobe.com/xap/1.0/sType/Version#",
        "http://ns.adobe.com/xap/1.0/g/",
        "http://ns.adobe.com/photoshop/1.0/",
        "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
    };

    static constexpr std::array<std::string_view, 2U> kOtherStandardNamespaces {
        "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "http://www.w3.org/2000/xmlns/",
    };

    static uint8_t hex_value_lower(char value) noexcept
    {
        if (value >= '0' && value <= '9')
            return static_cast<uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f')
            return static_cast<uint8_t>(value - 'a' + 10);
        return 0xffU;
    }

    static uint8_t hex_value_any(char value) noexcept
    {
        if (value >= '0' && value <= '9')
            return static_cast<uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f')
            return static_cast<uint8_t>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F')
            return static_cast<uint8_t>(value - 'A' + 10);
        return 0xffU;
    }

    static bool ascii_alpha(char value) noexcept
    {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
    }

    static bool ascii_digit(char value) noexcept
    {
        return value >= '0' && value <= '9';
    }

    static bool scheme_tail(char value) noexcept
    {
        return ascii_alpha(value) || ascii_digit(value) || value == '+'
               || value == '.' || value == '-';
    }

    static bool uri_ascii_character_valid(std::string_view uri,
                                          size_t index) noexcept
    {
        const uint8_t value = static_cast<uint8_t>(uri[index]);
        if (value <= 0x20U || value == 0x7fU || value == '<' || value == '>'
            || value == '"' || value == '\'' || value == '{' || value == '}'
            || value == '|' || value == '\\' || value == '^' || value == '`') {
            return false;
        }
        if (value == '%') {
            return index + 2U < uri.size()
                   && hex_value_any(uri[index + 1U]) != 0xffU
                   && hex_value_any(uri[index + 2U]) != 0xffU;
        }
        return true;
    }

    static bool uri_literal_is_valid(std::string_view uri) noexcept
    {
        if (uri.size() < 2U || !ascii_alpha(uri[0]))
            return false;
        size_t colon = 1U;
        while (colon < uri.size() && uri[colon] != ':') {
            if (!scheme_tail(uri[colon]))
                return false;
            ++colon;
        }
        if (colon >= uri.size())
            return false;

        size_t offset = 0U;
        while (offset < uri.size()) {
            const size_t before = offset;
            uint32_t code       = 0U;
            if (!capture_utf8_next(uri, &offset, &code)
                || !capture_xml_character(code)) {
                return false;
            }
            if (code < 0x80U && !uri_ascii_character_valid(uri, before))
                return false;
        }
        return true;
    }

    static bool hex_byte_lower(std::string_view hex, size_t offset,
                               uint8_t* out) noexcept
    {
        if (!out || offset > hex.size() || hex.size() - offset < 2U)
            return false;
        const uint8_t hi = hex_value_lower(hex[offset]);
        const uint8_t lo = hex_value_lower(hex[offset + 1U]);
        if (hi == 0xffU || lo == 0xffU)
            return false;
        *out = static_cast<uint8_t>((hi << 4U) | lo);
        return true;
    }

    static bool namespace_ref_size(const XmpStructureNamespaceRef& ref,
                                   size_t* size) noexcept
    {
        if (!size || ref.uri.empty())
            return false;
        if (ref.uri_is_hex) {
            if ((ref.uri.size() & 1U) != 0U)
                return false;
            *size = ref.uri.size() / 2U;
        } else {
            *size = ref.uri.size();
        }
        return *size <= kXmpStructureMaxUriBytes;
    }

    static bool namespace_uri_copy(const XmpStructureNamespaceRef& ref,
                                   char* storage, size_t storage_size,
                                   std::string_view* uri) noexcept
    {
        if (!uri)
            return false;
        if (!ref.uri_is_hex) {
            if (ref.uri.size() > kXmpStructureMaxUriBytes)
                return false;
            *uri = ref.uri;
            return true;
        }
        size_t decoded_size = 0U;
        if (!storage || !namespace_ref_size(ref, &decoded_size)
            || decoded_size > storage_size) {
            return false;
        }
        for (size_t i = 0U; i < decoded_size; ++i) {
            uint8_t value = 0U;
            if (!hex_byte_lower(ref.uri, i * 2U, &value))
                return false;
            storage[i] = static_cast<char>(value);
        }
        *uri = std::string_view(storage, decoded_size);
        return true;
    }

    static bool
    namespace_ref_is_valid(const XmpStructureNamespaceRef& ref) noexcept
    {
        std::array<char, kXmpStructureMaxUriBytes> storage {};
        std::string_view uri;
        return namespace_uri_copy(ref, storage.data(), storage.size(), &uri)
               && uri_literal_is_valid(uri);
    }

    static int
    namespace_ref_compare_decoded(const XmpStructureNamespaceRef& a,
                                  const XmpStructureNamespaceRef& b) noexcept
    {
        size_t a_size = 0U;
        size_t b_size = 0U;
        if (!namespace_ref_size(a, &a_size) || !namespace_ref_size(b, &b_size))
            return 0;
        const size_t common = a_size < b_size ? a_size : b_size;
        for (size_t i = 0U; i < common; ++i) {
            uint8_t a_byte = static_cast<uint8_t>(a.uri[i]);
            uint8_t b_byte = static_cast<uint8_t>(b.uri[i]);
            if (a.uri_is_hex && !hex_byte_lower(a.uri, i * 2U, &a_byte))
                return 0;
            if (b.uri_is_hex && !hex_byte_lower(b.uri, i * 2U, &b_byte))
                return 0;
            if (a_byte < b_byte)
                return -1;
            if (a_byte > b_byte)
                return 1;
        }
        if (a_size < b_size)
            return -1;
        if (a_size > b_size)
            return 1;
        return 0;
    }

    static bool namespace_ref_equal(const XmpStructureNamespaceRef& a,
                                    const XmpStructureNamespaceRef& b) noexcept
    {
        return namespace_ref_compare_decoded(a, b) == 0;
    }

    static bool namespace_ref_less(const XmpStructureNamespaceRef& a,
                                   const XmpStructureNamespaceRef& b) noexcept
    {
        const int decoded = namespace_ref_compare_decoded(a, b);
        if (decoded != 0)
            return decoded < 0;
        if (a.uri_is_hex != b.uri_is_hex)
            return !a.uri_is_hex;
        return a.uri < b.uri;
    }

    static bool root_ref_less(const RootRef& a, const RootRef& b) noexcept
    {
        if (a.schema_ns != b.schema_ns)
            return a.schema_ns < b.schema_ns;
        return a.root < b.root;
    }

    static bool root_ref_equal(const RootRef& a, const RootRef& b) noexcept
    {
        return a.schema_ns == b.schema_ns && a.root == b.root;
    }

    static bool node_less(const XmpStructureNode& a,
                          const XmpStructureNode& b) noexcept
    {
        if (a.schema_namespace_index != b.schema_namespace_index)
            return a.schema_namespace_index < b.schema_namespace_index;
        return a.path < b.path;
    }

    static int language_compare_folded(std::string_view a,
                                       std::string_view b) noexcept
    {
        const size_t common = a.size() < b.size() ? a.size() : b.size();
        for (size_t i = 0U; i < common; ++i) {
            const char a_lower = language_lower(a[i]);
            const char b_lower = language_lower(b[i]);
            if (a_lower < b_lower)
                return -1;
            if (a_lower > b_lower)
                return 1;
        }
        if (a.size() < b.size())
            return -1;
        if (a.size() > b.size())
            return 1;
        return 0;
    }

    static bool child_ref_less(const XmpStructureChildRef& a,
                               const XmpStructureChildRef& b) noexcept
    {
        if (a.parent_node != b.parent_node)
            return a.parent_node < b.parent_node;
        if (a.selector != b.selector)
            return a.selector < b.selector;
        if (a.selector == XmpStructureSelectorKind::None) {
            if (a.field_namespace_index != b.field_namespace_index)
                return a.field_namespace_index < b.field_namespace_index;
            return a.local_name < b.local_name;
        }
        if (a.selector == XmpStructureSelectorKind::Index)
            return a.index < b.index;
        const bool a_default = language_equal_impl(a.language, "x-default");
        const bool b_default = language_equal_impl(b.language, "x-default");
        if (a_default != b_default)
            return a_default;
        const int language_order = language_compare_folded(a.language,
                                                           b.language);
        if (language_order != 0)
            return language_order < 0;
        return a.language < b.language;
    }

    static bool xmp_key_views(const MetaStore& store, const Entry& entry,
                              std::string_view* schema_ns,
                              std::string_view* property_path) noexcept
    {
        if (!schema_ns || !property_path
            || entry.key.kind != MetaKeyKind::XmpProperty) {
            return false;
        }
        const std::span<const std::byte> ns_bytes = store.arena().span(
            entry.key.data.xmp_property.schema_ns);
        const std::span<const std::byte> path_bytes = store.arena().span(
            entry.key.data.xmp_property.property_path);
        if (ns_bytes.size() != entry.key.data.xmp_property.schema_ns.size
            || path_bytes.size()
                   != entry.key.data.xmp_property.property_path.size
            || ns_bytes.empty() || path_bytes.empty()) {
            return false;
        }
        *schema_ns
            = std::string_view(reinterpret_cast<const char*>(ns_bytes.data()),
                               ns_bytes.size());
        *property_path
            = std::string_view(reinterpret_cast<const char*>(path_bytes.data()),
                               path_bytes.size());
        return true;
    }

    static bool ascii_name_start(uint32_t code) noexcept
    {
        return code == static_cast<uint32_t>('_')
               || (code >= static_cast<uint32_t>('A')
                   && code <= static_cast<uint32_t>('Z'))
               || (code >= static_cast<uint32_t>('a')
                   && code <= static_cast<uint32_t>('z'));
    }

    static bool ascii_name_continue(uint32_t code) noexcept
    {
        return ascii_name_start(code)
               || (code < 0x80U && ascii_digit(static_cast<char>(code)))
               || code == static_cast<uint32_t>('-')
               || code == static_cast<uint32_t>('.');
    }

    static bool unicode_name_start(uint32_t code) noexcept
    {
        return (code >= 0x00c0U && code <= 0x00d6U)
               || (code >= 0x00d8U && code <= 0x00f6U)
               || (code >= 0x00f8U && code <= 0x02ffU)
               || (code >= 0x0370U && code <= 0x037dU)
               || (code >= 0x037fU && code <= 0x1fffU)
               || (code >= 0x200cU && code <= 0x200dU)
               || (code >= 0x2070U && code <= 0x218fU)
               || (code >= 0x2c00U && code <= 0x2fefU)
               || (code >= 0x3001U && code <= 0xd7ffU)
               || (code >= 0xf900U && code <= 0xfdcfU)
               || (code >= 0xfdf0U && code <= 0xfffdU)
               || (code >= 0x10000U && code <= 0xeffffU);
    }

    static bool unicode_name_continue(uint32_t code) noexcept
    {
        return unicode_name_start(code) || code == 0x00b7U
               || (code >= 0x0300U && code <= 0x036fU)
               || (code >= 0x203fU && code <= 0x2040U);
    }

    static bool ncname_valid(std::string_view name) noexcept
    {
        if (name.empty())
            return false;
        size_t offset = 0U;
        bool first    = true;
        while (offset < name.size()) {
            uint32_t code = 0U;
            if (!capture_utf8_next(name, &offset, &code)
                || !capture_xml_character(code)) {
                return false;
            }
            if (code < 0x80U) {
                if (first ? !ascii_name_start(code)
                          : !ascii_name_continue(code)) {
                    return false;
                }
            } else if (first ? !unicode_name_start(code)
                             : !unicode_name_continue(code)) {
                return false;
            }
            first = false;
        }
        return !first;
    }

    static char language_lower(char value) noexcept
    {
        if (value >= 'A' && value <= 'Z')
            return static_cast<char>(value + ('a' - 'A'));
        return value;
    }

    static bool language_equal_impl(std::string_view a,
                                    std::string_view b) noexcept
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0U; i < a.size(); ++i) {
            if (language_lower(a[i]) != language_lower(b[i]))
                return false;
        }
        return true;
    }

    static bool canonical_language(std::string_view language) noexcept
    {
        if (language.empty())
            return false;
        if (language_equal_impl(language, "x-default"))
            return true;
        size_t position = 0U;
        size_t part     = 0U;
        while (position < language.size()) {
            const size_t begin = position;
            while (position < language.size() && language[position] != '-') {
                const char value = language[position];
                if (!ascii_alpha(value) && !ascii_digit(value))
                    return false;
                ++position;
            }
            const size_t length = position - begin;
            if (length == 0U || length > 8U)
                return false;
            if (part == 0U) {
                for (size_t i = begin; i < begin + length; ++i)
                    if (!ascii_alpha(language[i]))
                        return false;
            }
            ++part;
            if (position == language.size())
                break;
            ++position;
            if (position == language.size())
                return false;
        }
        return part > 0U;
    }

    static bool
    resolve_standard_prefix(std::string_view prefix,
                            XmpStructureNamespaceRef* name_space) noexcept
    {
        if (!name_space)
            return false;
        if (prefix == "xmp")
            name_space->uri = "http://ns.adobe.com/xap/1.0/";
        else if (prefix == "tiff")
            name_space->uri = "http://ns.adobe.com/tiff/1.0/";
        else if (prefix == "exif")
            name_space->uri = "http://ns.adobe.com/exif/1.0/";
        else if (prefix == "exifEX")
            name_space->uri = "http://cipa.jp/exif/1.0/";
        else if (prefix == "xml")
            name_space->uri = "http://www.w3.org/XML/1998/namespace";
        else if (prefix == "aux")
            name_space->uri = "http://ns.adobe.com/exif/1.0/aux/";
        else if (prefix == "dc")
            name_space->uri = "http://purl.org/dc/elements/1.1/";
        else if (prefix == "pdf")
            name_space->uri = "http://ns.adobe.com/pdf/1.3/";
        else if (prefix == "xmpBJ")
            name_space->uri = "http://ns.adobe.com/xap/1.0/bj/";
        else if (prefix == "plus")
            name_space->uri = "http://ns.useplus.org/ldf/xmp/1.0/";
        else if (prefix == "crs")
            name_space->uri = "http://ns.adobe.com/camera-raw-settings/1.0/";
        else if (prefix == "dng")
            name_space->uri = "http://ns.adobe.com/dng/1.0/";
        else if (prefix == "lr")
            name_space->uri = "http://ns.adobe.com/lightroom/1.0/";
        else if (prefix == "xmpDM")
            name_space->uri = "http://ns.adobe.com/xmp/1.0/DynamicMedia/";
        else if (prefix == "xmpMM")
            name_space->uri = "http://ns.adobe.com/xap/1.0/mm/";
        else if (prefix == "xmpTPg")
            name_space->uri = "http://ns.adobe.com/xap/1.0/t/pg/";
        else if (prefix == "xmpRights")
            name_space->uri = "http://ns.adobe.com/xap/1.0/rights/";
        else if (prefix == "stDim")
            name_space->uri = "http://ns.adobe.com/xap/1.0/sType/Dimensions#";
        else if (prefix == "stEvt")
            name_space->uri = "http://ns.adobe.com/xap/1.0/sType/ResourceEvent#";
        else if (prefix == "stFnt")
            name_space->uri = "http://ns.adobe.com/xap/1.0/sType/Font#";
        else if (prefix == "stJob")
            name_space->uri = "http://ns.adobe.com/xap/1.0/sType/Job#";
        else if (prefix == "stMfs")
            name_space->uri = "http://ns.adobe.com/xap/1.0/sType/ManifestItem#";
        else if (prefix == "stRef")
            name_space->uri = "http://ns.adobe.com/xap/1.0/sType/ResourceRef#";
        else if (prefix == "stVer")
            name_space->uri = "http://ns.adobe.com/xap/1.0/sType/Version#";
        else if (prefix == "xmpG")
            name_space->uri = "http://ns.adobe.com/xap/1.0/g/";
        else if (prefix == "photoshop")
            name_space->uri = "http://ns.adobe.com/photoshop/1.0/";
        else if (prefix == "Iptc4xmpCore")
            name_space->uri = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
        else if (prefix == "Iptc4xmpExt")
            name_space->uri = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
        else
            return false;
        name_space->uri_is_hex = false;
        return true;
    }

    static bool parse_segment(std::string_view path, uint32_t begin,
                              uint32_t end, bool is_root,
                              uint32_t max_array_index,
                              XmpStructurePathSegment* out) noexcept
    {
        if (!out || end <= begin || end > path.size())
            return false;
        XmpStructurePathSegment segment;
        segment.start              = begin;
        segment.size               = end - begin;
        segment.token              = path.substr(begin, end - begin);
        const size_t first_bracket = segment.token.find('[');
        const std::string_view qualified_name
            = first_bracket == std::string_view::npos
                  ? segment.token
                  : segment.token.substr(0U, first_bracket);
        if (first_bracket != std::string_view::npos) {
            size_t position = first_bracket;
            while (position < segment.token.size()) {
                if (segment.selector_count >= kXmpStructureMaxDepth
                    || segment.token[position] != '[') {
                    return false;
                }
                const size_t close = segment.token.find(']', position + 1U);
                if (close == std::string_view::npos
                    || segment.token.find('[', position + 1U) < close) {
                    return false;
                }
                const std::string_view selector
                    = segment.token.substr(position + 1U,
                                           close - position - 1U);
                XmpStructureSelector item;
                item.start = begin + static_cast<uint32_t>(position);
                if (selector.starts_with("@xml:lang=")) {
                    if (close + 1U != segment.token.size()) {
                        return false;
                    }
                    item.language = selector.substr(10U);
                    if (!canonical_language(item.language))
                        return false;
                    item.kind = XmpStructureSelectorKind::Language;
                } else {
                    if (selector.empty() || selector[0] == '0')
                        return false;
                    uint32_t index = 0U;
                    for (const char value : selector) {
                        if (!ascii_digit(value))
                            return false;
                        const uint32_t digit = static_cast<uint32_t>(value
                                                                     - '0');
                        if (index > (UINT32_MAX - digit) / 10U)
                            return false;
                        index = index * 10U + digit;
                    }
                    if (index == 0U || index > max_array_index)
                        return false;
                    item.index = index;
                    item.kind  = XmpStructureSelectorKind::Index;
                }
                segment.selectors[segment.selector_count++] = item;
                position                                    = close + 1U;
                if (position < segment.token.size()
                    && segment.token[position] != '[') {
                    return false;
                }
            }
        } else if (segment.token.find(']') != std::string_view::npos) {
            return false;
        }

        const size_t colon = qualified_name.find(':');
        if (colon == std::string_view::npos) {
            segment.local_name = qualified_name;
            if (!ncname_valid(segment.local_name))
                return false;
        } else {
            if (is_root || colon == 0U || colon + 1U >= qualified_name.size()
                || qualified_name.find(':', colon + 1U)
                       != std::string_view::npos) {
                return false;
            }
            segment.prefix     = qualified_name.substr(0U, colon);
            segment.local_name = qualified_name.substr(colon + 1U);
            if (!ncname_valid(segment.prefix)
                || !ncname_valid(segment.local_name)) {
                return false;
            }
            if (segment.prefix.starts_with("nsu_")) {
                const std::string_view encoded = segment.prefix.substr(4U);
                if (encoded.empty() || (encoded.size() & 1U) != 0U
                    || encoded.size() / 2U > kXmpStructureMaxUriBytes) {
                    return false;
                }
                for (const char value : encoded)
                    if (hex_value_lower(value) == 0xffU)
                        return false;
                segment.name_space.uri        = encoded;
                segment.name_space.uri_is_hex = true;
            } else if (!resolve_standard_prefix(segment.prefix,
                                                &segment.name_space)) {
                return false;
            }
        }
        *out = segment;
        return true;
    }

    static bool root_candidate(std::string_view path, size_t max_root_bytes,
                               std::string_view* root) noexcept
    {
        if (!root || path.empty())
            return false;
        size_t end = 0U;
        while (end < path.size() && end < max_root_bytes && path[end] != '/'
               && path[end] != '[') {
            ++end;
        }
        if (end == 0U
            || (end == max_root_bytes && end < path.size() && path[end] != '/'
                && path[end] != '[')) {
            return false;
        }
        const std::string_view name = path.substr(0U, end);
        if (name.find(':') != std::string_view::npos || !ncname_valid(name))
            return false;
        *root = name;
        return true;
    }

    static uint32_t find_root(const std::vector<RootRef>& roots,
                              std::string_view schema_ns,
                              std::string_view name) noexcept
    {
        size_t low  = 0U;
        size_t high = roots.size();
        while (low < high) {
            const size_t middle    = low + (high - low) / 2U;
            const RootRef& current = roots[middle];
            if (current.schema_ns < schema_ns
                || (current.schema_ns == schema_ns && current.root < name)) {
                low = middle + 1U;
            } else {
                high = middle;
            }
        }
        if (low >= roots.size() || roots[low].schema_ns != schema_ns
            || roots[low].root != name) {
            return kInvalidXmpStructureNode;
        }
        return static_cast<uint32_t>(low);
    }

    static XmpStructureStatus add_namespace_ref(
        const XmpStructureNamespaceRef& ref, uint32_t max_namespace_bytes,
        uint32_t max_namespaces,
        std::array<XmpStructureNamespaceRef, kXmpStructureMaxNamespaces>* refs,
        size_t* ref_count) noexcept
    {
        if (!refs || !ref_count)
            return XmpStructureStatus::InvalidMetadata;
        size_t namespace_size = 0U;
        if (!namespace_ref_size(ref, &namespace_size))
            return XmpStructureStatus::InvalidMetadata;
        if (namespace_size > max_namespace_bytes)
            return XmpStructureStatus::LimitExceeded;
        if (!namespace_ref_is_valid(ref))
            return XmpStructureStatus::InvalidMetadata;
        for (size_t i = 0U; i < *ref_count; ++i) {
            if (namespace_ref_equal((*refs)[i], ref))
                return XmpStructureStatus::Ok;
        }
        if (*ref_count >= max_namespaces
            || *ref_count >= kXmpStructureMaxNamespaces) {
            return XmpStructureStatus::LimitExceeded;
        }
        (*refs)[(*ref_count)++] = ref;
        return XmpStructureStatus::Ok;
    }

    static XmpStructureNamespaceRef
    segment_namespace_ref(const XmpStructurePathSegment& segment,
                          std::string_view root_uri) noexcept
    {
        if (segment.name_space.uri.empty())
            return XmpStructureNamespaceRef { root_uri, false };
        return segment.name_space;
    }

    static bool xmp_text_leaf_shape_is_valid(const MetaStore& store,
                                             const Entry& entry) noexcept
    {
        if (entry.value.kind != MetaValueKind::Text
            || entry.value.elem_type != MetaElementType::U8
            || (entry.value.text_encoding != TextEncoding::Ascii
                && entry.value.text_encoding != TextEncoding::Utf8)
            || entry.value.count != entry.value.data.span.size) {
            return false;
        }
        const std::span<const std::byte> raw = store.arena().span(
            entry.value.data.span);
        return raw.size() == entry.value.count;
    }

    static bool xmp_text_leaf_contents_are_valid(const MetaStore& store,
                                                 const Entry& entry) noexcept
    {
        const std::span<const std::byte> raw = store.arena().span(
            entry.value.data.span);
        const std::string_view text(reinterpret_cast<const char*>(raw.data()),
                                    raw.size());
        if (entry.value.text_encoding == TextEncoding::Ascii) {
            for (const char value : text)
                if (static_cast<uint8_t>(value) > 0x7fU)
                    return false;
        }
        return metadata_logical_text_is_valid(text);
    }

    static bool bytes_fit(uint64_t current, uint64_t add,
                          uint64_t limit) noexcept
    {
        return current <= limit && add <= limit - current;
    }

    static uint32_t find_namespace_index(
        const std::array<XmpStructureNamespaceRef, kXmpStructureMaxNamespaces>&
            refs,
        size_t ref_count, const XmpStructureNamespaceRef& ref) noexcept
    {
        size_t low  = 0U;
        size_t high = ref_count;
        while (low < high) {
            const size_t middle = low + (high - low) / 2U;
            if (namespace_ref_compare_decoded(refs[middle], ref) < 0)
                low = middle + 1U;
            else
                high = middle;
        }
        if (low >= ref_count || !namespace_ref_equal(refs[low], ref))
            return kInvalidXmpStructureNode;
        return static_cast<uint32_t>(low);
    }

    static uint32_t find_node(const XmpStructureTree& tree,
                              uint32_t schema_namespace_index,
                              std::string_view path) noexcept
    {
        size_t low  = 0U;
        size_t high = tree.nodes.size();
        while (low < high) {
            const size_t middle          = low + (high - low) / 2U;
            const XmpStructureNode& node = tree.nodes[middle];
            if (node.schema_namespace_index < schema_namespace_index
                || (node.schema_namespace_index == schema_namespace_index
                    && node.path < path)) {
                low = middle + 1U;
            } else {
                high = middle;
            }
        }
        if (low >= tree.nodes.size()
            || tree.nodes[low].schema_namespace_index != schema_namespace_index
            || tree.nodes[low].path != path) {
            return kInvalidXmpStructureNode;
        }
        return static_cast<uint32_t>(low);
    }

    static bool node_is_array_container(XmpContainerKind kind) noexcept
    {
        return kind == XmpContainerKind::Bag || kind == XmpContainerKind::Seq;
    }

    static bool assign_node_parents(XmpStructureTree* tree,
                                    EntryId* failed_entry) noexcept
    {
        if (!tree || !failed_entry)
            return false;
        for (size_t i = 0U; i < tree->nodes.size(); ++i) {
            XmpStructureNode& node              = tree->nodes[i];
            node.parent_node                    = kInvalidXmpStructureNode;
            const XmpStructurePathSegment& last = node.last_segment;
            if (last.selector_count != 0U) {
                const XmpStructureSelector& selector
                    = last.selectors[last.selector_count - 1U];
                const uint32_t parent
                    = find_node(*tree, node.schema_namespace_index,
                                node.path.substr(0U, selector.start));
                if (parent == kInvalidXmpStructureNode) {
                    *failed_entry = node.entry;
                    return false;
                }
                const XmpContainerKind parent_kind
                    = tree->nodes[parent].container_kind;
                const bool matching_kind
                    = selector.kind == XmpStructureSelectorKind::Index
                          ? node_is_array_container(parent_kind)
                          : parent_kind == XmpContainerKind::LangAlt;
                if (!matching_kind) {
                    *failed_entry = node.entry;
                    return false;
                }
                node.parent_node = parent;
            } else if (last.start != 0U) {
                const size_t parent_end = static_cast<size_t>(last.start) - 1U;
                const uint32_t parent
                    = find_node(*tree, node.schema_namespace_index,
                                node.path.substr(0U, parent_end));
                if (parent == kInvalidXmpStructureNode
                    || tree->nodes[parent].container_kind
                           != XmpContainerKind::Struct) {
                    *failed_entry = node.entry;
                    return false;
                }
                node.parent_node = parent;
            } else if (node.container_kind == XmpContainerKind::None) {
                *failed_entry = node.entry;
                return false;
            }
        }
        return true;
    }

    static bool build_ordered_children(XmpStructureTree* tree,
                                       EntryId* failed_entry) noexcept
    {
        if (!tree || !failed_entry)
            return false;
        tree->ordered_children.clear();
        tree->ordered_children.reserve(tree->nodes.size());
        for (size_t i = 0U; i < tree->nodes.size(); ++i) {
            const XmpStructureNode& node = tree->nodes[i];
            if (node.parent_node == kInvalidXmpStructureNode)
                continue;
            XmpStructureChildRef child;
            child.parent_node           = node.parent_node;
            child.node_index            = static_cast<uint32_t>(i);
            child.field_namespace_index = node.field_namespace_index;
            child.local_name            = node.last_segment.local_name;
            if (node.last_segment.selector_count != 0U) {
                const XmpStructureSelector& selector
                    = node.last_segment
                          .selectors[node.last_segment.selector_count - 1U];
                child.selector = selector.kind;
                child.index    = selector.index;
                child.language = selector.language;
            }
            tree->ordered_children.push_back(child);
        }
        std::sort(tree->ordered_children.begin(), tree->ordered_children.end(),
                  child_ref_less);

        size_t begin = 0U;
        while (begin < tree->ordered_children.size()) {
            size_t end = begin + 1U;
            const uint32_t parent_index
                = tree->ordered_children[begin].parent_node;
            while (end < tree->ordered_children.size()
                   && tree->ordered_children[end].parent_node == parent_index) {
                ++end;
            }
            const XmpStructureNode& parent = tree->nodes[parent_index];
            XmpStructureChildRef previous  = tree->ordered_children[begin];
            if (parent.container_kind == XmpContainerKind::LangAlt) {
                if (previous.selector != XmpStructureSelectorKind::Language
                    || tree->nodes[previous.node_index].container_kind
                           != XmpContainerKind::None) {
                    *failed_entry = tree->nodes[previous.node_index].entry;
                    return false;
                }
            } else if (node_is_array_container(parent.container_kind)) {
                if (previous.selector != XmpStructureSelectorKind::Index) {
                    *failed_entry = tree->nodes[previous.node_index].entry;
                    return false;
                }
            } else if (parent.container_kind != XmpContainerKind::Struct
                       || previous.selector != XmpStructureSelectorKind::None) {
                *failed_entry = tree->nodes[previous.node_index].entry;
                return false;
            }

            XmpContainerKind array_item_kind
                = tree->nodes[previous.node_index].container_kind;
            for (size_t i = begin + 1U; i < end; ++i) {
                const XmpStructureChildRef& current = tree->ordered_children[i];
                const XmpStructureNode& current_node
                    = tree->nodes[current.node_index];
                if (parent.container_kind == XmpContainerKind::Struct) {
                    if (current.selector != XmpStructureSelectorKind::None
                        || (current.field_namespace_index
                                == previous.field_namespace_index
                            && current.local_name == previous.local_name)) {
                        *failed_entry = current_node.entry;
                        return false;
                    }
                } else if (parent.container_kind == XmpContainerKind::LangAlt) {
                    if (current.selector != XmpStructureSelectorKind::Language
                        || current_node.container_kind != XmpContainerKind::None
                        || language_equal_impl(current.language,
                                               previous.language)) {
                        *failed_entry = current_node.entry;
                        return false;
                    }
                } else {
                    if (current.selector != XmpStructureSelectorKind::Index
                        || current.index == previous.index
                        || current_node.container_kind != array_item_kind) {
                        *failed_entry = current_node.entry;
                        return false;
                    }
                }
                if (parent.container_kind == XmpContainerKind::Bag
                    || parent.container_kind == XmpContainerKind::Seq) {
                    array_item_kind = current_node.container_kind;
                }
                previous = current;
            }
            begin = end;
        }
        return true;
    }

    static bool path_within_limits(const XmpStructurePath& path,
                                   const XmpStructureLimits& limits) noexcept
    {
        if (path.expanded_depth > limits.max_depth)
            return false;
        for (uint8_t segment_index = 0U; segment_index < path.depth;
             ++segment_index) {
            const XmpStructurePathSegment& segment
                = path.segments[segment_index];
            for (uint8_t selector_index = 0U;
                 selector_index < segment.selector_count; ++selector_index) {
                const XmpStructureSelector& selector
                    = segment.selectors[selector_index];
                if (selector.kind == XmpStructureSelectorKind::Index
                    && selector.index > limits.max_array_index) {
                    return false;
                }
            }
        }
        return true;
    }

    static XmpStructureBuildResult build_error(XmpStructureStatus status,
                                               EntryId failed_entry) noexcept
    {
        XmpStructureBuildResult result;
        result.status       = status;
        result.failed_entry = failed_entry;
        return result;
    }

    static bool
    data_namespace_is_syntax(const XmpStructureNamespaceRef& ref) noexcept
    {
        constexpr std::array<std::string_view, 4U> syntax_namespaces = {
            "http://www.w3.org/XML/1998/namespace",
            "http://www.w3.org/2000/xmlns/",
            "http://www.w3.org/1999/02/22-rdf-syntax-ns#", "adobe:ns:meta/"
        };
        for (const std::string_view uri : syntax_namespaces)
            if (namespace_ref_equal(ref,
                                    XmpStructureNamespaceRef { uri, false }))
                return true;
        return false;
    }

    static XmpStructureStatus add_entry_namespaces(
        const XmpStructurePath& parsed, std::string_view schema_ns,
        uint32_t max_namespace_bytes, uint32_t max_namespaces,
        std::array<XmpStructureNamespaceRef, kXmpStructureMaxNamespaces>* refs,
        size_t* ref_count) noexcept
    {
        XmpStructureStatus status
            = add_namespace_ref(XmpStructureNamespaceRef { schema_ns, false },
                                max_namespace_bytes, max_namespaces, refs,
                                ref_count);
        if (status != XmpStructureStatus::Ok)
            return status;
        for (uint8_t i = 0U; i < parsed.depth; ++i) {
            const XmpStructureNamespaceRef field
                = segment_namespace_ref(parsed.segments[i], schema_ns);
            if (data_namespace_is_syntax(field))
                return XmpStructureStatus::InvalidMetadata;
            status = add_namespace_ref(field, max_namespace_bytes,
                                       max_namespaces, refs, ref_count);
            if (status != XmpStructureStatus::Ok)
                return status;
        }
        return XmpStructureStatus::Ok;
    }

    static bool active_value_unusable(const Entry& entry) noexcept
    {
        return any(entry.flags, EntryFlags::Truncated | EntryFlags::Unreadable);
    }

    static bool append_node_id(std::vector<EntryId>* nodes, EntryId entry,
                               uint32_t max_nodes) noexcept
    {
        if (nodes->size() >= max_nodes)
            return false;
        nodes->push_back(entry);
        return true;
    }

}  // namespace

bool
xmp_structure_limits_valid(const XmpStructureLimits& limits) noexcept
{
    return limits.max_nodes > 0U && limits.max_nodes <= 4096U
           && limits.max_depth > 0U && limits.max_depth <= 8U
           && limits.max_namespaces > 0U && limits.max_namespaces <= 64U
           && limits.max_path_bytes > 0U && limits.max_path_bytes <= 4096U
           && limits.max_namespace_bytes > 0U
           && limits.max_namespace_bytes <= 4096U
           && limits.max_request_bytes > 0U
           && limits.max_request_bytes <= 16ULL * 1024ULL * 1024ULL
           && limits.max_array_index > 0U && limits.max_array_index <= 65535U;
}

bool
xmp_structure_wire_code_reserved(uint16_t code) noexcept
{
    return code >= kXmpStructureWireBase && code <= kXmpStructureWireLast;
}

bool
xmp_structure_is_declaration(const Entry& entry) noexcept
{
    return entry.key.kind == MetaKeyKind::XmpProperty
           && entry.origin.wire_type.family == WireFamily::Other
           && xmp_structure_wire_code_reserved(entry.origin.wire_type.code);
}

bool
xmp_container_value_shape_is_valid(MetaValueKind value_kind,
                                   MetaElementType element_type,
                                   TextEncoding text_encoding,
                                   uint32_t value_count, uint64_t payload_bytes,
                                   WireType wire_type, uint32_t wire_count,
                                   uint64_t wire_type_name_bytes) noexcept
{
    return value_kind == MetaValueKind::Empty
           && element_type == MetaElementType::U8
           && text_encoding == TextEncoding::Unknown && value_count == 0U
           && payload_bytes == 0U && wire_type.family == WireFamily::Other
           && wire_type.code > kXmpStructureWireBase
           && wire_type.code <= kXmpStructureWireBase + 4U && wire_count == 0U
           && wire_type_name_bytes == 0U;
}

bool
xmp_container_entry_shape_is_valid(const Entry& entry) noexcept
{
    return xmp_structure_is_declaration(entry)
           && entry.origin.wire_type.code > kXmpStructureWireBase
           && entry.origin.wire_type.code <= kXmpStructureWireBase + 4U
           && xmp_container_value_shape_is_valid(
               entry.value.kind, entry.value.elem_type,
               entry.value.text_encoding, entry.value.count, 0U,
               entry.origin.wire_type, entry.origin.wire_count,
               entry.origin.wire_type_name.size);
}

bool
xmp_container_view_shape_is_valid(const MetadataAuthoringEntry& entry) noexcept
{
    return entry.key.kind == MetaKeyKind::XmpProperty
           && entry.wire_type.family == WireFamily::Other
           && xmp_structure_wire_code_reserved(entry.wire_type.code)
           && entry.wire_type.code > kXmpStructureWireBase
           && entry.wire_type.code <= kXmpStructureWireBase + 4U
           && xmp_container_value_shape_is_valid(
               entry.value.kind, entry.value.elem_type,
               entry.value.text_encoding, entry.value.count,
               static_cast<uint64_t>(entry.value.payload.size()),
               entry.wire_type, entry.wire_count, 0U);
}

bool
xmp_structure_is_standard_namespace(std::string_view uri) noexcept
{
    for (const std::string_view standard : kStandardNamespaces)
        if (uri == standard)
            return true;
    for (const std::string_view standard : kOtherStandardNamespaces)
        if (uri == standard)
            return true;
    return false;
}

bool
xmp_structure_namespace_is_custom(std::string_view uri) noexcept
{
    return uri.size() <= kXmpStructureMaxUriBytes && uri_literal_is_valid(uri)
           && !xmp_structure_is_standard_namespace(uri);
}

bool
xmp_structure_namespace_uri(const XmpStructureNamespaceRef& ref, char* storage,
                            size_t storage_size, std::string_view* uri) noexcept
{
    return namespace_uri_copy(ref, storage, storage_size, uri);
}

bool
xmp_structure_parse_path(std::string_view path, uint8_t max_depth,
                         uint32_t max_array_index,
                         XmpStructurePath* out) noexcept
{
    if (!out || path.empty() || max_depth == 0U
        || max_depth > kXmpStructureMaxDepth || max_array_index == 0U
        || max_array_index > 65535U
        || path.size() > static_cast<size_t>(UINT32_MAX)) {
        return false;
    }
    XmpStructurePath parsed;
    size_t begin       = 0U;
    bool language_seen = false;
    while (begin < path.size()) {
        if (parsed.depth >= kXmpStructureMaxDepth)
            return false;
        const size_t slash = path.find('/', begin);
        const size_t end   = slash == std::string_view::npos ? path.size()
                                                             : slash;
        if (end <= begin)
            return false;
        XmpStructurePathSegment segment;
        if (!parse_segment(path, static_cast<uint32_t>(begin),
                           static_cast<uint32_t>(end), parsed.depth == 0U,
                           max_array_index, &segment)) {
            return false;
        }
        if (language_seen)
            return false;
        for (uint8_t i = 0U; i < segment.selector_count; ++i) {
            if (segment.selectors[i].kind == XmpStructureSelectorKind::Language)
                language_seen = true;
        }
        parsed.segments[parsed.depth] = segment;
        if (parsed.depth == 0U)
            parsed.root = segment.local_name;
        ++parsed.depth;
        const uint16_t expanded_add = static_cast<uint16_t>(
            1U + segment.selector_count);
        if (expanded_add > max_depth
            || parsed.expanded_depth > max_depth - expanded_add) {
            return false;
        }
        parsed.expanded_depth = static_cast<uint8_t>(parsed.expanded_depth
                                                     + expanded_add);
        if (slash == std::string_view::npos)
            break;
        if (language_seen)
            return false;
        begin = slash + 1U;
        if (begin >= path.size())
            return false;
    }
    *out = parsed;
    return parsed.depth > 0U;
}

bool
xmp_structure_extract_root(std::string_view path, uint32_t max_array_index,
                           std::string_view* root) noexcept
{
    if (!root)
        return false;
    XmpStructurePath parsed;
    if (!xmp_structure_parse_path(path, 8U, max_array_index, &parsed))
        return false;
    *root = parsed.root;
    return !root->empty();
}

bool
xmp_structure_namespace_equal(const XmpStructureNamespaceRef& a,
                              const XmpStructureNamespaceRef& b) noexcept
{
    return namespace_ref_equal(a, b);
}

bool
xmp_structure_language_equal(std::string_view a, std::string_view b) noexcept
{
    return language_equal_impl(a, b);
}

bool
xmp_structure_language_is_default(std::string_view language) noexcept
{
    return language_equal_impl(language, "x-default");
}

XmpStructureBuildResult
prepare_xmp_structure_tree(const MetaStore& store,
                           const XmpStructureLimits& limits,
                           XmpStructureTree* output) noexcept
{
    if (!output || !xmp_structure_limits_valid(limits))
        return build_error(XmpStructureStatus::InvalidOptions, kInvalidEntryId);

    const std::span<const Entry> entries = store.entries();
    size_t declaration_count             = 0U;
    uint64_t request_bytes               = 0U;

    // Count and charge all retained declarations before reading any key bytes.
    for (size_t i = 0U; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (!xmp_structure_is_declaration(entry))
            continue;
        const EntryId id = static_cast<EntryId>(i);
        if (declaration_count >= limits.max_nodes)
            return build_error(XmpStructureStatus::LimitExceeded, id);
        ++declaration_count;
        const uint64_t namespace_bytes
            = entry.key.data.xmp_property.schema_ns.size;
        const uint64_t path_bytes
            = entry.key.data.xmp_property.property_path.size;
        if (namespace_bytes > UINT64_MAX - path_bytes
            || !bytes_fit(request_bytes, namespace_bytes + path_bytes,
                          limits.max_request_bytes)) {
            return build_error(XmpStructureStatus::LimitExceeded, id);
        }
        request_bytes += namespace_bytes + path_bytes;
    }

    if (declaration_count == 0U) {
        XmpStructureTree empty;
        *output = std::move(empty);
        return XmpStructureBuildResult {};
    }

    std::vector<RootRef> roots;
    roots.reserve(declaration_count);
    std::array<XmpStructureNamespaceRef, kXmpStructureMaxNamespaces>
        input_namespace_refs {};
    size_t input_namespace_count = 0U;
    std::array<XmpStructureNamespaceRef, kXmpStructureMaxNamespaces>
        output_namespace_refs {};
    size_t output_namespace_count = 0U;
    std::vector<EntryId> raw_nodes;
    raw_nodes.reserve(limits.max_nodes);

    for (size_t i = 0U; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (!xmp_structure_is_declaration(entry))
            continue;
        const EntryId id   = static_cast<EntryId>(i);
        const bool deleted = any(entry.flags, EntryFlags::Deleted);
        if (!deleted && active_value_unusable(entry))
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        if (!xmp_container_entry_shape_is_valid(entry))
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        std::string_view schema_ns;
        std::string_view path;
        if (!xmp_key_views(store, entry, &schema_ns, &path))
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        if (schema_ns.size() > limits.max_namespace_bytes
            || path.size() > limits.max_path_bytes) {
            return build_error(XmpStructureStatus::LimitExceeded, id);
        }
        if (!xmp_structure_namespace_is_custom(schema_ns))
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        XmpStructurePath parsed;
        if (!xmp_structure_parse_path(path, kXmpStructureMaxDepth, 65535U,
                                      &parsed)) {
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        }
        if (!path_within_limits(parsed, limits))
            return build_error(XmpStructureStatus::LimitExceeded, id);
        XmpStructureStatus namespace_status
            = add_entry_namespaces(parsed, schema_ns,
                                   limits.max_namespace_bytes,
                                   limits.max_namespaces, &input_namespace_refs,
                                   &input_namespace_count);
        if (namespace_status != XmpStructureStatus::Ok)
            return build_error(namespace_status, id);
        const bool root_marker = parsed.depth == 1U
                                 && parsed.segments[0].selector_count == 0U;
        RootRef root;
        root.schema_ns       = schema_ns;
        root.root            = parsed.root;
        root.entry           = id;
        root.has_active_root = root_marker && !deleted;
        roots.push_back(root);
        if (!deleted) {
            namespace_status = add_entry_namespaces(parsed, schema_ns,
                                                    limits.max_namespace_bytes,
                                                    limits.max_namespaces,
                                                    &output_namespace_refs,
                                                    &output_namespace_count);
            if (namespace_status != XmpStructureStatus::Ok)
                return build_error(namespace_status, id);
            if (!append_node_id(&raw_nodes, id, limits.max_nodes)) {
                return build_error(XmpStructureStatus::LimitExceeded, id);
            }
        }
    }

    std::sort(roots.begin(), roots.end(), root_ref_less);
    size_t root_count = 0U;
    for (size_t i = 0U; i < roots.size(); ++i) {
        if (root_count != 0U
            && root_ref_equal(roots[root_count - 1U], roots[i])) {
            roots[root_count - 1U].has_active_root
                = roots[root_count - 1U].has_active_root
                  || roots[i].has_active_root;
            continue;
        }
        roots[root_count++] = roots[i];
    }
    roots.resize(root_count);

    uint64_t selected_leaf_count = 0U;
    for (size_t i = 0U; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (any(entry.flags, EntryFlags::Deleted)
            || entry.key.kind != MetaKeyKind::XmpProperty
            || xmp_structure_is_declaration(entry)) {
            continue;
        }
        std::string_view schema_ns;
        std::string_view path;
        if (!xmp_key_views(store, entry, &schema_ns, &path))
            continue;
        if (schema_ns.size() > limits.max_namespace_bytes)
            continue;
        std::string_view candidate_root;
        if (!root_candidate(path, limits.max_path_bytes, &candidate_root))
            continue;
        const uint32_t root_index = find_root(roots, schema_ns, candidate_root);
        if (root_index == kInvalidXmpStructureNode)
            continue;
        const EntryId id = static_cast<EntryId>(i);
        if (path.size() > limits.max_path_bytes)
            return build_error(XmpStructureStatus::LimitExceeded, id);
        const uint64_t key_bytes = static_cast<uint64_t>(schema_ns.size())
                                   + static_cast<uint64_t>(path.size());
        if (entry.value.kind != MetaValueKind::Text)
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        const uint64_t text_bytes = static_cast<uint64_t>(entry.value.count);
        if (key_bytes > UINT64_MAX - text_bytes
            || !bytes_fit(request_bytes, key_bytes + text_bytes,
                          limits.max_request_bytes)) {
            return build_error(XmpStructureStatus::LimitExceeded, id);
        }
        request_bytes += key_bytes + text_bytes;
        ++selected_leaf_count;
        if (declaration_count + selected_leaf_count > limits.max_nodes)
            return build_error(XmpStructureStatus::LimitExceeded, id);
        if (active_value_unusable(entry))
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        XmpStructurePath parsed;
        if (!xmp_structure_parse_path(path, kXmpStructureMaxDepth, 65535U,
                                      &parsed)
            || parsed.root != candidate_root) {
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        }
        if (!path_within_limits(parsed, limits))
            return build_error(XmpStructureStatus::LimitExceeded, id);
        if (!xmp_text_leaf_shape_is_valid(store, entry)
            || !xmp_text_leaf_contents_are_valid(store, entry)) {
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        }
        XmpStructureStatus namespace_status
            = add_entry_namespaces(parsed, schema_ns,
                                   limits.max_namespace_bytes,
                                   limits.max_namespaces, &input_namespace_refs,
                                   &input_namespace_count);
        if (namespace_status != XmpStructureStatus::Ok)
            return build_error(namespace_status, id);
        namespace_status = add_entry_namespaces(parsed, schema_ns,
                                                limits.max_namespace_bytes,
                                                limits.max_namespaces,
                                                &output_namespace_refs,
                                                &output_namespace_count);
        if (namespace_status != XmpStructureStatus::Ok)
            return build_error(namespace_status, id);
        if (!append_node_id(&raw_nodes, id, limits.max_nodes)) {
            return build_error(XmpStructureStatus::LimitExceeded, id);
        }
    }

    // Declarations can establish intent for a root even when the root marker
    // was deleted. Any active node in that scope still requires a live root.
    for (const EntryId id : raw_nodes) {
        std::string_view schema_ns;
        std::string_view path;
        std::string_view root;
        if (!xmp_key_views(store, store.entry(id), &schema_ns, &path)
            || !root_candidate(path, limits.max_path_bytes, &root))
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        const uint32_t root_index = find_root(roots, schema_ns, root);
        if (root_index == kInvalidXmpStructureNode
            || !roots[root_index].has_active_root) {
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        }
    }

    if (raw_nodes.empty()) {
        XmpStructureTree empty;
        *output = std::move(empty);
        return XmpStructureBuildResult {};
    }

    std::sort(output_namespace_refs.begin(),
              output_namespace_refs.begin() + output_namespace_count,
              namespace_ref_less);
    size_t unique_namespace_count = 0U;
    for (size_t i = 0U; i < output_namespace_count; ++i) {
        if (unique_namespace_count != 0U
            && namespace_ref_equal(
                output_namespace_refs[unique_namespace_count - 1U],
                output_namespace_refs[i])) {
            continue;
        }
        output_namespace_refs[unique_namespace_count++]
            = output_namespace_refs[i];
    }
    output_namespace_count = unique_namespace_count;

    XmpStructureTree candidate;
    candidate.namespaces.reserve(output_namespace_count);
    for (size_t i = 0U; i < output_namespace_count; ++i)
        candidate.namespaces.push_back(
            XmpStructureNamespace { output_namespace_refs[i] });
    candidate.nodes.reserve(raw_nodes.size());
    for (const EntryId id : raw_nodes) {
        const Entry& entry = store.entry(id);
        std::string_view schema_ns;
        std::string_view path;
        XmpStructurePath parsed;
        if (!xmp_key_views(store, entry, &schema_ns, &path)
            || !xmp_structure_parse_path(path, kXmpStructureMaxDepth, 65535U,
                                         &parsed))
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        const uint32_t schema_namespace_index = find_namespace_index(
            output_namespace_refs, output_namespace_count,
            XmpStructureNamespaceRef { schema_ns, false });
        if (schema_namespace_index == kInvalidXmpStructureNode)
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        XmpStructureNode node;
        node.entry                  = id;
        node.schema_namespace_index = schema_namespace_index;
        node.container_kind         = xmp_container_kind(entry);
        node.path                   = path;
        node.depth                  = parsed.expanded_depth;
        node.last_segment           = parsed.segments[parsed.depth - 1U];
        const XmpStructureNamespaceRef field_ref
            = segment_namespace_ref(node.last_segment, schema_ns);
        node.field_namespace_index
            = find_namespace_index(output_namespace_refs,
                                   output_namespace_count, field_ref);
        if (node.field_namespace_index == kInvalidXmpStructureNode)
            return build_error(XmpStructureStatus::InvalidMetadata, id);
        candidate.nodes.push_back(node);
    }
    std::sort(candidate.nodes.begin(), candidate.nodes.end(), node_less);
    for (size_t i = 1U; i < candidate.nodes.size(); ++i) {
        if (candidate.nodes[i - 1U].schema_namespace_index
                == candidate.nodes[i].schema_namespace_index
            && candidate.nodes[i - 1U].path == candidate.nodes[i].path) {
            return build_error(XmpStructureStatus::InvalidMetadata,
                               candidate.nodes[i].entry);
        }
    }

    EntryId failed_entry = kInvalidEntryId;
    if (!assign_node_parents(&candidate, &failed_entry)
        || !build_ordered_children(&candidate, &failed_entry)) {
        return build_error(XmpStructureStatus::InvalidMetadata, failed_entry);
    }

    *output = std::move(candidate);
    return XmpStructureBuildResult {};
}

}  // namespace openmeta::detail
