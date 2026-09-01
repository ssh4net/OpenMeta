// SPDX-License-Identifier: Apache-2.0

#include "openmeta/validate.h"

#include "metadata_logical_field_internal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    enum class SchemaIfd : uint8_t {
        Ifd0,
        ImageIfd,
        ExifIfd,
        GpsIfd,
        InteropIfd,
        RawIfd,
    };

    struct TagSchema final {
        SchemaIfd ifd      = SchemaIfd::Ifd0;
        uint16_t tag       = 0U;
        uint16_t type_mask = 0U;
        uint32_t min_count = 0U;
        uint32_t max_count = 0U;
        bool singleton     = true;
    };

    static constexpr uint16_t type_bit(uint16_t type) noexcept
    {
        return type <= 15U ? static_cast<uint16_t>(1U << type) : 0U;
    }

    static constexpr uint16_t kByte      = type_bit(1U);
    static constexpr uint16_t kAscii     = type_bit(2U);
    static constexpr uint16_t kShort     = type_bit(3U);
    static constexpr uint16_t kLong      = type_bit(4U);
    static constexpr uint16_t kRational  = type_bit(5U);
    static constexpr uint16_t kUndefined = type_bit(7U);
    static constexpr uint16_t kSRational = type_bit(10U);

    static constexpr TagSchema kTagSchemas[] = {
        { SchemaIfd::ImageIfd, 0x0100U, kShort | kLong, 1U, 1U, true },
        { SchemaIfd::ImageIfd, 0x0101U, kShort | kLong, 1U, 1U, true },
        { SchemaIfd::ImageIfd, 0x0102U, kShort, 1U, 0U, true },
        { SchemaIfd::ImageIfd, 0x0103U, kShort, 1U, 1U, true },
        { SchemaIfd::ImageIfd, 0x0106U, kShort, 1U, 1U, true },
        { SchemaIfd::Ifd0, 0x010FU, kAscii, 1U, 0U, true },
        { SchemaIfd::Ifd0, 0x0110U, kAscii, 1U, 0U, true },
        { SchemaIfd::ImageIfd, 0x0112U, kShort, 1U, 1U, true },
        { SchemaIfd::ImageIfd, 0x0115U, kShort, 1U, 1U, true },
        { SchemaIfd::ImageIfd, 0x011AU, kRational, 1U, 1U, true },
        { SchemaIfd::ImageIfd, 0x011BU, kRational, 1U, 1U, true },
        { SchemaIfd::ImageIfd, 0x011CU, kShort, 1U, 1U, true },
        { SchemaIfd::ImageIfd, 0x0128U, kShort, 1U, 1U, true },
        { SchemaIfd::Ifd0, 0x0131U, kAscii, 1U, 0U, true },
        { SchemaIfd::Ifd0, 0x0132U, kAscii, 20U, 20U, true },
        { SchemaIfd::ExifIfd, 0x829AU, kRational, 1U, 1U, true },
        { SchemaIfd::ExifIfd, 0x829DU, kRational, 1U, 1U, true },
        { SchemaIfd::ExifIfd, 0x8827U, kShort | kLong, 1U, 0U, true },
        { SchemaIfd::ExifIfd, 0x9000U, kUndefined, 4U, 4U, true },
        { SchemaIfd::ExifIfd, 0x9003U, kAscii, 20U, 20U, true },
        { SchemaIfd::ExifIfd, 0x9004U, kAscii, 20U, 20U, true },
        { SchemaIfd::ExifIfd, 0x9010U, kAscii, 7U, 7U, true },
        { SchemaIfd::ExifIfd, 0x9011U, kAscii, 7U, 7U, true },
        { SchemaIfd::ExifIfd, 0x9012U, kAscii, 7U, 7U, true },
        { SchemaIfd::ExifIfd, 0x9204U, kSRational, 1U, 1U, true },
        { SchemaIfd::ExifIfd, 0x920AU, kRational, 1U, 1U, true },
        { SchemaIfd::ExifIfd, 0xA001U, kShort, 1U, 1U, true },
        { SchemaIfd::ExifIfd, 0xA002U, kShort | kLong, 1U, 1U, true },
        { SchemaIfd::ExifIfd, 0xA003U, kShort | kLong, 1U, 1U, true },
        { SchemaIfd::GpsIfd, 0x0000U, kByte, 4U, 4U, true },
        { SchemaIfd::GpsIfd, 0x0001U, kAscii, 2U, 2U, true },
        { SchemaIfd::GpsIfd, 0x0002U, kRational, 3U, 3U, true },
        { SchemaIfd::GpsIfd, 0x0003U, kAscii, 2U, 2U, true },
        { SchemaIfd::GpsIfd, 0x0004U, kRational, 3U, 3U, true },
        { SchemaIfd::GpsIfd, 0x0005U, kByte, 1U, 1U, true },
        { SchemaIfd::GpsIfd, 0x0006U, kRational, 1U, 1U, true },
        { SchemaIfd::GpsIfd, 0x0007U, kRational, 3U, 3U, true },
        { SchemaIfd::GpsIfd, 0x001DU, kAscii, 11U, 11U, true },
        { SchemaIfd::RawIfd, 0x828DU, kShort, 2U, 2U, true },
        { SchemaIfd::RawIfd, 0x828EU, kByte, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC612U, kByte, 4U, 4U, true },
        { SchemaIfd::RawIfd, 0xC613U, kByte, 4U, 4U, true },
        { SchemaIfd::RawIfd, 0xC616U, kByte, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC617U, kShort, 1U, 1U, true },
        { SchemaIfd::RawIfd, 0xC618U, kShort, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC619U, kShort, 2U, 2U, true },
        { SchemaIfd::RawIfd, 0xC61AU, kShort | kLong | kRational, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC61DU, kShort | kLong, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC621U, kSRational, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC622U, kSRational, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC623U, kSRational, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC624U, kSRational, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC627U, kRational, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC628U, kRational, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC65AU, kShort, 1U, 1U, true },
        { SchemaIfd::RawIfd, 0xC65BU, kShort, 1U, 1U, true },
        { SchemaIfd::RawIfd, 0xC714U, kSRational, 1U, 0U, true },
        { SchemaIfd::RawIfd, 0xC715U, kSRational, 1U, 0U, true },
    };

    static uint32_t element_width(MetaElementType type) noexcept
    {
        switch (type) {
        case MetaElementType::U8:
        case MetaElementType::I8: return 1U;
        case MetaElementType::U16:
        case MetaElementType::I16: return 2U;
        case MetaElementType::U32:
        case MetaElementType::I32:
        case MetaElementType::F32: return 4U;
        case MetaElementType::U64:
        case MetaElementType::I64:
        case MetaElementType::F64:
        case MetaElementType::URational:
        case MetaElementType::SRational: return 8U;
        }
        return 0U;
    }

    static uint16_t inferred_tiff_type(const MetaValue& value) noexcept
    {
        if (value.kind == MetaValueKind::Text) {
            return 2U;
        }
        if (value.kind == MetaValueKind::Bytes) {
            return 7U;
        }
        if (value.kind != MetaValueKind::Scalar
            && value.kind != MetaValueKind::Array) {
            return 0U;
        }
        switch (value.elem_type) {
        case MetaElementType::U8: return 1U;
        case MetaElementType::I8: return 6U;
        case MetaElementType::U16: return 3U;
        case MetaElementType::I16: return 8U;
        case MetaElementType::U32: return 4U;
        case MetaElementType::I32: return 9U;
        case MetaElementType::URational: return 5U;
        case MetaElementType::SRational: return 10U;
        case MetaElementType::F32: return 11U;
        case MetaElementType::F64: return 12U;
        case MetaElementType::U64:
        case MetaElementType::I64: return 0U;
        }
        return 0U;
    }

    static uint32_t inferred_tiff_count(const MetaValue& value) noexcept
    {
        if (value.kind == MetaValueKind::Text) {
            return value.count == UINT32_MAX ? UINT32_MAX : value.count + 1U;
        }
        return value.count;
    }

    static bool span_is_valid(ByteSpan span, size_t arena_size) noexcept
    {
        const size_t offset = static_cast<size_t>(span.offset);
        const size_t size   = static_cast<size_t>(span.size);
        return offset <= arena_size && size <= arena_size - offset;
    }

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool ascii_is_valid(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0U; i < bytes.size(); ++i) {
            if (std::to_integer<uint8_t>(bytes[i]) > 0x7FU) {
                return false;
            }
        }
        return true;
    }

    static bool xmp_name_start(char c) noexcept
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }

    static bool xmp_name_char(char c) noexcept
    {
        return xmp_name_start(c) || (c >= '0' && c <= '9') || c == '-'
               || c == '.';
    }

    static bool xmp_qname_is_valid(std::string_view name) noexcept
    {
        if (name.empty()) {
            return false;
        }
        size_t part_start = 0U;
        bool saw_colon    = false;
        for (size_t i = 0U; i <= name.size(); ++i) {
            if (i == name.size() || name[i] == ':') {
                if (i == part_start || !xmp_name_start(name[part_start])) {
                    return false;
                }
                for (size_t j = part_start + 1U; j < i; ++j) {
                    if (!xmp_name_char(name[j])) {
                        return false;
                    }
                }
                if (i == name.size()) {
                    return true;
                }
                if (saw_colon) {
                    return false;
                }
                saw_colon  = true;
                part_start = i + 1U;
            }
        }
        return false;
    }

    static bool xmp_selector_is_valid(std::string_view selector) noexcept
    {
        if (selector.empty()) {
            return true;
        }
        if (selector.size() < 3U || selector.front() != '['
            || selector.back() != ']') {
            return false;
        }
        const std::string_view body = selector.substr(1U, selector.size() - 2U);
        if (body.starts_with("@xml:lang=")) {
            const std::string_view lang = body.substr(10U);
            if (lang.empty()) {
                return false;
            }
            for (char c : lang) {
                if (!xmp_name_char(c)) {
                    return false;
                }
            }
            return true;
        }
        if (body.empty() || body.front() == '0') {
            return false;
        }
        for (char c : body) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        return true;
    }

    static bool xmp_property_path_is_valid(std::string_view path) noexcept
    {
        if (path.empty()) {
            return false;
        }
        size_t start = 0U;
        while (start < path.size()) {
            const size_t slash = path.find('/', start);
            const size_t end   = slash == std::string_view::npos ? path.size()
                                                                 : slash;
            const std::string_view component = path.substr(start, end - start);
            const size_t bracket             = component.find('[');
            const std::string_view name      = component.substr(0U, bracket);
            const std::string_view selector  = bracket == std::string_view::npos
                                                   ? std::string_view {}
                                                   : component.substr(bracket);
            if (!xmp_qname_is_valid(name) || !xmp_selector_is_valid(selector)) {
                return false;
            }
            if (slash == std::string_view::npos) {
                return true;
            }
            start = slash + 1U;
        }
        return false;
    }

    static bool xmp_namespace_is_valid(std::string_view uri) noexcept
    {
        if (uri.empty()) {
            return false;
        }
        for (char raw : uri) {
            const uint8_t c = static_cast<uint8_t>(raw);
            if (c < 0x20U || c > 0x7EU || c == '"' || c == '&' || c == '<'
                || c == '>') {
                return false;
            }
        }
        return true;
    }

    static bool indexed_ifd_name(std::string_view ifd,
                                 std::string_view prefix) noexcept
    {
        if (!ifd.starts_with(prefix) || ifd.size() <= prefix.size()) {
            return false;
        }
        for (size_t i = prefix.size(); i < ifd.size(); ++i) {
            if (ifd[i] < '0' || ifd[i] > '9') {
                return false;
            }
        }
        return true;
    }

    static bool ifd_name_is_raw(std::string_view ifd) noexcept
    {
        if (ifd == "ifd0") {
            return true;
        }
        return indexed_ifd_name(ifd, "subifd");
    }

    static bool ifd_name_is_image(std::string_view ifd) noexcept
    {
        return indexed_ifd_name(ifd, "ifd") || indexed_ifd_name(ifd, "subifd");
    }

    static bool schema_ifd_matches(SchemaIfd expected,
                                   std::string_view actual) noexcept
    {
        switch (expected) {
        case SchemaIfd::Ifd0: return actual == "ifd0";
        case SchemaIfd::ImageIfd: return ifd_name_is_image(actual);
        case SchemaIfd::ExifIfd: return actual == "exififd";
        case SchemaIfd::GpsIfd: return actual == "gpsifd";
        case SchemaIfd::InteropIfd: return actual == "interopifd";
        case SchemaIfd::RawIfd: return ifd_name_is_raw(actual);
        }
        return false;
    }

    static const TagSchema* find_schema(std::string_view ifd,
                                        uint16_t tag) noexcept
    {
        for (const TagSchema& schema : kTagSchemas) {
            if (schema.tag == tag && schema_ifd_matches(schema.ifd, ifd)) {
                return &schema;
            }
        }
        return nullptr;
    }

    static bool tag_known_in_other_ifd(std::string_view ifd,
                                       uint16_t tag) noexcept
    {
        const bool standard_ifd = ifd_name_is_image(ifd) || ifd == "exififd"
                                  || ifd == "gpsifd" || ifd == "interopifd";
        if (!standard_ifd) {
            return false;
        }
        for (const TagSchema& schema : kTagSchemas) {
            if (schema.tag == tag && !schema_ifd_matches(schema.ifd, ifd)) {
                return true;
            }
        }
        return false;
    }

    static void append_issue(MetadataValidationResult* out,
                             const MetadataValidationOptions& options,
                             ValidateIssueSeverity severity,
                             MetadataValidationIssueCode code, EntryId entry,
                             EntryId related, MetaKeyKind key_kind,
                             uint16_t tag) noexcept
    {
        if (!out || out->status == MetadataValidationStatus::LimitExceeded) {
            return;
        }
        if (out->issues.size() >= options.max_issues) {
            out->status = MetadataValidationStatus::LimitExceeded;
            out->error_count += 1U;
            return;
        }
        MetadataValidationIssue issue;
        issue.severity      = severity;
        issue.code          = code;
        issue.entry         = entry;
        issue.related_entry = related;
        issue.key_kind      = key_kind;
        issue.tag           = tag;
        out->issues.push_back(issue);
        if (severity == ValidateIssueSeverity::Error) {
            out->error_count += 1U;
        } else {
            out->warning_count += 1U;
        }
    }

    static void validate_rationals(const MetaStore& store, const Entry& entry,
                                   EntryId id,
                                   const MetadataValidationOptions& options,
                                   MetadataValidationResult* out) noexcept
    {
        const MetaValue& value = entry.value;
        if (value.elem_type != MetaElementType::URational
            && value.elem_type != MetaElementType::SRational) {
            return;
        }
        if (value.kind == MetaValueKind::Scalar) {
            const bool zero = value.elem_type == MetaElementType::URational
                                  ? value.data.ur.denom == 0U
                                  : value.data.sr.denom == 0;
            if (zero) {
                append_issue(
                    out, options, ValidateIssueSeverity::Error,
                    MetadataValidationIssueCode::RationalDenominatorZero, id,
                    kInvalidEntryId, entry.key.kind,
                    entry.key.kind == MetaKeyKind::ExifTag
                        ? entry.key.data.exif_tag.tag
                        : 0U);
            }
            return;
        }
        if (value.kind != MetaValueKind::Array) {
            return;
        }
        const std::span<const std::byte> bytes = store.arena().span(
            value.data.span);
        for (uint32_t i = 0U; i < value.count; ++i) {
            const size_t offset = static_cast<size_t>(i) * 8U + 4U;
            if (offset + 4U > bytes.size()) {
                break;
            }
            uint32_t denom = 0U;
            std::memcpy(&denom, bytes.data() + offset, sizeof(denom));
            if (denom == 0U) {
                append_issue(
                    out, options, ValidateIssueSeverity::Error,
                    MetadataValidationIssueCode::RationalDenominatorZero, id,
                    kInvalidEntryId, entry.key.kind,
                    entry.key.kind == MetaKeyKind::ExifTag
                        ? entry.key.data.exif_tag.tag
                        : 0U);
                return;
            }
        }
    }

    static bool key_span_is_valid(const MetaStore& store, ByteSpan span,
                                  uint32_t max_bytes,
                                  bool require_nonempty) noexcept
    {
        return span_is_valid(span, store.arena().bytes().size())
               && (!require_nonempty || span.size != 0U)
               && span.size <= max_bytes;
    }

    static bool validate_key(const MetaStore& store, const Entry& entry,
                             EntryId id,
                             const MetadataValidationOptions& options,
                             MetadataValidationResult* out) noexcept
    {
        if (static_cast<uint8_t>(entry.key.kind)
            > static_cast<uint8_t>(MetaKeyKind::PngText)) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::InvalidKey, id,
                         kInvalidEntryId, MetaKeyKind::ExifTag, 0U);
            return false;
        }
        bool valid = true;
        switch (entry.key.kind) {
        case MetaKeyKind::ExifTag:
            valid = key_span_is_valid(store, entry.key.data.exif_tag.ifd,
                                      options.max_key_bytes, true);
            break;
        case MetaKeyKind::ExrAttribute:
            valid = key_span_is_valid(store, entry.key.data.exr_attribute.name,
                                      options.max_key_bytes, true);
            break;
        case MetaKeyKind::XmpProperty:
            valid = key_span_is_valid(store,
                                      entry.key.data.xmp_property.schema_ns,
                                      options.max_key_bytes, true)
                    && key_span_is_valid(
                        store, entry.key.data.xmp_property.property_path,
                        options.max_key_bytes, true);
            break;
        case MetaKeyKind::PhotoshopIrbField:
            valid = key_span_is_valid(store,
                                      entry.key.data.photoshop_irb_field.field,
                                      options.max_key_bytes, false);
            break;
        case MetaKeyKind::PrintImField:
            valid = key_span_is_valid(store, entry.key.data.printim_field.field,
                                      options.max_key_bytes, false);
            break;
        case MetaKeyKind::BmffField:
            valid = key_span_is_valid(store, entry.key.data.bmff_field.field,
                                      options.max_key_bytes, false);
            break;
        case MetaKeyKind::JumbfField:
            valid = key_span_is_valid(store, entry.key.data.jumbf_field.field,
                                      options.max_key_bytes, false);
            break;
        case MetaKeyKind::JumbfCborKey:
            valid = key_span_is_valid(store, entry.key.data.jumbf_cbor_key.key,
                                      options.max_key_bytes, false);
            break;
        case MetaKeyKind::PngText:
            valid = key_span_is_valid(store, entry.key.data.png_text.keyword,
                                      options.max_key_bytes, true)
                    && key_span_is_valid(store, entry.key.data.png_text.field,
                                         options.max_key_bytes, false);
            break;
        case MetaKeyKind::Comment:
        case MetaKeyKind::IptcDataset:
        case MetaKeyKind::IccHeaderField:
        case MetaKeyKind::IccTag:
        case MetaKeyKind::PhotoshopIrb:
        case MetaKeyKind::GeotiffKey: break;
        }
        if (!valid) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::InvalidKey, id,
                         kInvalidEntryId, entry.key.kind,
                         entry.key.kind == MetaKeyKind::ExifTag
                             ? entry.key.data.exif_tag.tag
                             : 0U);
        }
        return valid;
    }

    static void validate_origin(const MetaStore& store, const Entry& entry,
                                EntryId id,
                                const MetadataValidationOptions& options,
                                MetadataValidationResult* out) noexcept
    {
        if ((entry.origin.block != kInvalidBlockId
             && entry.origin.block >= store.block_count())
            || static_cast<uint8_t>(entry.origin.wire_type.family)
                   > static_cast<uint8_t>(WireFamily::Other)
            || (entry.origin.wire_type_name.size != 0U
                && !key_span_is_valid(store, entry.origin.wire_type_name,
                                      options.max_key_bytes, false))) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::InvalidOrigin, id,
                         kInvalidEntryId, entry.key.kind,
                         entry.key.kind == MetaKeyKind::ExifTag
                             ? entry.key.data.exif_tag.tag
                             : 0U);
        }
    }

    static void validate_value(const MetaStore& store, const Entry& entry,
                               EntryId id,
                               const MetadataValidationOptions& options,
                               MetadataValidationResult* out) noexcept
    {
        const MetaValue& value = entry.value;
        if (static_cast<uint8_t>(value.kind)
                > static_cast<uint8_t>(MetaValueKind::Text)
            || static_cast<uint8_t>(value.elem_type)
                   > static_cast<uint8_t>(MetaElementType::SRational)
            || static_cast<uint8_t>(value.text_encoding)
                   > static_cast<uint8_t>(TextEncoding::Utf16BE)) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::InvalidValueShape, id,
                         kInvalidEntryId, entry.key.kind, 0U);
            return;
        }
        bool shape_valid = true;
        switch (value.kind) {
        case MetaValueKind::Empty: shape_valid = value.count == 0U; break;
        case MetaValueKind::Scalar: shape_valid = value.count == 1U; break;
        case MetaValueKind::Array: {
            const uint32_t width = element_width(value.elem_type);
            shape_valid          = width != 0U
                          && span_is_valid(value.data.span,
                                           store.arena().bytes().size())
                          && static_cast<uint64_t>(value.count) * width
                                 == value.data.span.size;
            break;
        }
        case MetaValueKind::Bytes:
        case MetaValueKind::Text:
            shape_valid = span_is_valid(value.data.span,
                                        store.arena().bytes().size())
                          && value.count == value.data.span.size;
            break;
        }
        if (!shape_valid) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::InvalidValueShape, id,
                         kInvalidEntryId, entry.key.kind, 0U);
            return;
        }
        if ((value.kind == MetaValueKind::Array
             || value.kind == MetaValueKind::Bytes
             || value.kind == MetaValueKind::Text)
            && value.data.span.size > options.max_value_bytes) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::ValueLimitExceeded, id,
                         kInvalidEntryId, entry.key.kind, 0U);
            out->status = MetadataValidationStatus::LimitExceeded;
            return;
        }

        if (value.kind == MetaValueKind::Scalar) {
            bool in_range = true;
            switch (value.elem_type) {
            case MetaElementType::U8:
                in_range = value.data.u64
                           <= std::numeric_limits<uint8_t>::max();
                break;
            case MetaElementType::I8:
                in_range = value.data.i64 >= std::numeric_limits<int8_t>::min()
                           && value.data.i64
                                  <= std::numeric_limits<int8_t>::max();
                break;
            case MetaElementType::U16:
                in_range = value.data.u64
                           <= std::numeric_limits<uint16_t>::max();
                break;
            case MetaElementType::I16:
                in_range = value.data.i64 >= std::numeric_limits<int16_t>::min()
                           && value.data.i64
                                  <= std::numeric_limits<int16_t>::max();
                break;
            case MetaElementType::U32:
                in_range = value.data.u64
                           <= std::numeric_limits<uint32_t>::max();
                break;
            case MetaElementType::I32:
                in_range = value.data.i64 >= std::numeric_limits<int32_t>::min()
                           && value.data.i64
                                  <= std::numeric_limits<int32_t>::max();
                break;
            default: break;
            }
            if (!in_range) {
                append_issue(out, options, ValidateIssueSeverity::Error,
                             MetadataValidationIssueCode::ScalarOutOfRange, id,
                             kInvalidEntryId, entry.key.kind, 0U);
            }
        }
        validate_rationals(store, entry, id, options, out);
    }

    static void validate_exif_entry(const MetaStore& store, const Entry& entry,
                                    EntryId id,
                                    const MetadataValidationOptions& options,
                                    MetadataValidationResult* out) noexcept
    {
        const ByteSpan ifd_span = entry.key.data.exif_tag.ifd;
        if (!span_is_valid(ifd_span, store.arena().bytes().size())
            || ifd_span.size == 0U || ifd_span.size > options.max_key_bytes) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::InvalidKey, id,
                         kInvalidEntryId, entry.key.kind,
                         entry.key.data.exif_tag.tag);
            return;
        }
        const std::string_view ifd = arena_string(store.arena(), ifd_span);
        const uint16_t tag         = entry.key.data.exif_tag.tag;
        const uint16_t inferred    = inferred_tiff_type(entry.value);
        uint16_t type              = inferred;
        uint32_t count             = inferred_tiff_count(entry.value);

        if (options.validate_wire_hints) {
            if (entry.origin.wire_type.family == WireFamily::Tiff) {
                const uint16_t hinted = entry.origin.wire_type.code;
                if (hinted == 0U || hinted > 12U
                    || (hinted != inferred
                        && !(entry.value.kind == MetaValueKind::Bytes
                             && (hinted == 1U || hinted == 6U
                                 || hinted == 7U)))) {
                    append_issue(out, options, ValidateIssueSeverity::Error,
                                 MetadataValidationIssueCode::InvalidWireType,
                                 id, kInvalidEntryId, entry.key.kind, tag);
                } else {
                    type = hinted;
                }
                if (entry.origin.wire_count != 0U
                    && entry.origin.wire_count != count) {
                    append_issue(out, options, ValidateIssueSeverity::Error,
                                 MetadataValidationIssueCode::InvalidWireCount,
                                 id, kInvalidEntryId, entry.key.kind, tag);
                }
            } else if (entry.origin.wire_type.family == WireFamily::None
                       && (entry.origin.wire_type.code != 0U
                           || entry.origin.wire_count != 0U)) {
                append_issue(out, options, ValidateIssueSeverity::Error,
                             MetadataValidationIssueCode::InvalidWireType, id,
                             kInvalidEntryId, entry.key.kind, tag);
            }
        }

        if (!options.validate_schema) {
            return;
        }
        const TagSchema* schema = find_schema(ifd, tag);
        if (!schema) {
            if (tag_known_in_other_ifd(ifd, tag)) {
                append_issue(out, options, ValidateIssueSeverity::Error,
                             MetadataValidationIssueCode::WrongIfd, id,
                             kInvalidEntryId, entry.key.kind, tag);
            } else if (options.unknown_exif_tags
                       != MetadataUnknownTagPolicy::Allow) {
                const ValidateIssueSeverity severity
                    = options.unknown_exif_tags
                              == MetadataUnknownTagPolicy::Error
                          ? ValidateIssueSeverity::Error
                          : ValidateIssueSeverity::Warning;
                append_issue(out, options, severity,
                             MetadataValidationIssueCode::UnknownExifTag, id,
                             kInvalidEntryId, entry.key.kind, tag);
            }
            return;
        }
        if (type == 0U || (schema->type_mask & type_bit(type)) == 0U) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::WrongType, id,
                         kInvalidEntryId, entry.key.kind, tag);
        }
        if (count < schema->min_count
            || (schema->max_count != 0U && count > schema->max_count)) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::WrongCount, id,
                         kInvalidEntryId, entry.key.kind, tag);
        }
        if (type == 2U && entry.value.kind == MetaValueKind::Text) {
            const std::span<const std::byte> text = store.arena().span(
                entry.value.data.span);
            if (!ascii_is_valid(text)) {
                append_issue(out, options, ValidateIssueSeverity::Error,
                             MetadataValidationIssueCode::InvalidText, id,
                             kInvalidEntryId, entry.key.kind, tag);
            }
        }
    }

    static void validate_xmp_entry(const MetaStore& store, const Entry& entry,
                                   EntryId id,
                                   const MetadataValidationOptions& options,
                                   MetadataValidationResult* out) noexcept
    {
        const ByteSpan ns_span   = entry.key.data.xmp_property.schema_ns;
        const ByteSpan path_span = entry.key.data.xmp_property.property_path;
        if (!span_is_valid(ns_span, store.arena().bytes().size())
            || ns_span.size == 0U || ns_span.size > options.max_key_bytes) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::InvalidXmpNamespace, id,
                         kInvalidEntryId, entry.key.kind, 0U);
        } else if (!xmp_namespace_is_valid(
                       arena_string(store.arena(), ns_span))) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::InvalidXmpNamespace, id,
                         kInvalidEntryId, entry.key.kind, 0U);
        }
        if (!span_is_valid(path_span, store.arena().bytes().size())
            || path_span.size == 0U || path_span.size > options.max_key_bytes
            || !xmp_property_path_is_valid(
                arena_string(store.arena(), path_span))) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::InvalidXmpPropertyPath,
                         id, kInvalidEntryId, entry.key.kind, 0U);
        }
        if (entry.value.kind == MetaValueKind::Text) {
            const std::span<const std::byte> bytes = store.arena().span(
                entry.value.data.span);
            const std::string_view text(reinterpret_cast<const char*>(
                                            bytes.data()),
                                        bytes.size());
            if ((entry.value.text_encoding != TextEncoding::Utf8
                 && entry.value.text_encoding != TextEncoding::Ascii)
                || !detail::metadata_logical_text_is_valid(text)) {
                append_issue(out, options, ValidateIssueSeverity::Error,
                             MetadataValidationIssueCode::InvalidText, id,
                             kInvalidEntryId, entry.key.kind, 0U);
            }
        }
    }

    static void validate_iptc_entry(const Entry& entry, EntryId id,
                                    const MetadataValidationOptions& options,
                                    MetadataValidationResult* out) noexcept
    {
        if (entry.key.data.iptc_dataset.record > 255U
            || entry.key.data.iptc_dataset.dataset > 255U) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::InvalidKey, id,
                         kInvalidEntryId, entry.key.kind, 0U);
        }
    }

    static void validate_one(const MetaStore& store, EntryId id,
                             const MetadataValidationOptions& options,
                             MetadataValidationResult* out) noexcept
    {
        const Entry& entry = store.entry(id);
        out->entries_checked += 1U;
        if (!validate_key(store, entry, id, options, out)) {
            return;
        }
        validate_origin(store, entry, id, options, out);
        if (any(entry.flags, EntryFlags::Deleted)) {
            return;
        }
        validate_value(store, entry, id, options, out);
        switch (entry.key.kind) {
        case MetaKeyKind::ExifTag:
            validate_exif_entry(store, entry, id, options, out);
            break;
        case MetaKeyKind::XmpProperty:
            validate_xmp_entry(store, entry, id, options, out);
            break;
        case MetaKeyKind::IptcDataset:
            validate_iptc_entry(entry, id, options, out);
            break;
        default: break;
        }
    }

    static bool entry_u64(const Entry& entry, uint64_t* out) noexcept
    {
        if (!out || entry.value.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (entry.value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: *out = entry.value.data.u64; return true;
        default: return false;
        }
    }

    static EntryId first_exif(const MetaStore& store, std::string_view ifd,
                              uint16_t tag) noexcept
    {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd              = ifd;
        key.data.exif_tag.tag              = tag;
        const std::span<const EntryId> ids = store.find_all(key);
        return ids.empty() ? kInvalidEntryId : ids.front();
    }

    static void
    validate_context_dimension(const MetaStore& store, EntryId id,
                               uint32_t expected,
                               const MetadataValidationOptions& options,
                               MetadataValidationResult* out) noexcept
    {
        if (id == kInvalidEntryId) {
            return;
        }
        uint64_t actual = 0U;
        if (entry_u64(store.entry(id), &actual) && actual != expected) {
            append_issue(out, options, ValidateIssueSeverity::Error,
                         MetadataValidationIssueCode::ImageContextMismatch, id,
                         kInvalidEntryId, MetaKeyKind::ExifTag,
                         store.entry(id).key.data.exif_tag.tag);
        }
    }

    static void validate_image_context(const MetaStore& store,
                                       const MetadataValidationOptions& options,
                                       MetadataValidationResult* out) noexcept
    {
        if (options.context.has_dimensions) {
            validate_context_dimension(store,
                                       first_exif(store, "ifd0", 0x0100U),
                                       options.context.width, options, out);
            validate_context_dimension(store,
                                       first_exif(store, "ifd0", 0x0101U),
                                       options.context.height, options, out);
            validate_context_dimension(store,
                                       first_exif(store, "exififd", 0xA002U),
                                       options.context.width, options, out);
            validate_context_dimension(store,
                                       first_exif(store, "exififd", 0xA003U),
                                       options.context.height, options, out);
        }
        if (options.context.has_samples_per_pixel) {
            validate_context_dimension(store,
                                       first_exif(store, "ifd0", 0x0115U),
                                       options.context.samples_per_pixel,
                                       options, out);
        }

        const EntryId cfa_dim = first_exif(store, "ifd0", 0x828DU);
        const EntryId cfa     = first_exif(store, "ifd0", 0x828EU);
        if (cfa_dim != kInvalidEntryId && cfa != kInvalidEntryId) {
            const Entry& dim_entry                     = store.entry(cfa_dim);
            const Entry& cfa_entry                     = store.entry(cfa);
            const std::span<const std::byte> dim_bytes = store.arena().span(
                dim_entry.value.data.span);
            if (dim_entry.value.kind == MetaValueKind::Array
                && dim_entry.value.elem_type == MetaElementType::U16
                && dim_entry.value.count == 2U && dim_bytes.size() == 4U) {
                uint16_t rows = 0U;
                uint16_t cols = 0U;
                std::memcpy(&rows, dim_bytes.data(), sizeof(rows));
                std::memcpy(&cols, dim_bytes.data() + 2U, sizeof(cols));
                const uint64_t expected = static_cast<uint64_t>(rows) * cols;
                if (rows == 0U || cols == 0U
                    || cfa_entry.value.count != expected) {
                    append_issue(
                        out, options, ValidateIssueSeverity::Error,
                        MetadataValidationIssueCode::InconsistentRelatedEntries,
                        cfa, cfa_dim, MetaKeyKind::ExifTag, 0x828EU);
                }
            }
        }

        if (options.context.has_color_planes) {
            const uint32_t color_planes = options.context.color_planes;
            const uint32_t matrix_count
                = static_cast<uint32_t>(options.context.color_planes) * 3U;
            static constexpr uint16_t kMatrixTags[] = {
                0xC621U,
                0xC622U,
                0xC714U,
                0xC715U,
            };
            for (uint16_t tag : kMatrixTags) {
                const EntryId id = first_exif(store, "ifd0", tag);
                if (id != kInvalidEntryId
                    && store.entry(id).value.count != matrix_count) {
                    append_issue(
                        out, options, ValidateIssueSeverity::Error,
                        MetadataValidationIssueCode::ImageContextMismatch, id,
                        kInvalidEntryId, MetaKeyKind::ExifTag, tag);
                }
            }
            const uint32_t calibration_count = color_planes * color_planes;
            static constexpr uint16_t kCalibrationTags[] = {
                0xC623U,
                0xC624U,
            };
            for (uint16_t tag : kCalibrationTags) {
                const EntryId id = first_exif(store, "ifd0", tag);
                if (id != kInvalidEntryId
                    && store.entry(id).value.count != calibration_count) {
                    append_issue(
                        out, options, ValidateIssueSeverity::Error,
                        MetadataValidationIssueCode::ImageContextMismatch, id,
                        kInvalidEntryId, MetaKeyKind::ExifTag, tag);
                }
            }
            static constexpr uint16_t kColorVectorTags[] = {
                0xC616U,
                0xC627U,
                0xC628U,
            };
            for (uint16_t tag : kColorVectorTags) {
                const EntryId id = first_exif(store, "ifd0", tag);
                if (id != kInvalidEntryId
                    && store.entry(id).value.count != color_planes) {
                    append_issue(
                        out, options, ValidateIssueSeverity::Error,
                        MetadataValidationIssueCode::ImageContextMismatch, id,
                        kInvalidEntryId, MetaKeyKind::ExifTag, tag);
                }
            }
        }
    }

    static void
    validate_duplicate_singletons(const MetaStore& store,
                                  const MetadataValidationOptions& options,
                                  MetadataValidationResult* out) noexcept
    {
        if (!options.validate_schema) {
            return;
        }
        std::vector<EntryId> ids;
        ids.reserve(store.entries().size());
        for (EntryId id = 0U; id < static_cast<EntryId>(store.entries().size());
             ++id) {
            const Entry& entry = store.entry(id);
            if (entry.key.kind != MetaKeyKind::ExifTag
                || any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            const std::string_view ifd
                = arena_string(store.arena(), entry.key.data.exif_tag.ifd);
            const TagSchema* schema = find_schema(ifd,
                                                  entry.key.data.exif_tag.tag);
            if (!schema || !schema->singleton) {
                continue;
            }
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end(), [&store](EntryId a, EntryId b) {
            const int compared = compare_key(store.arena(), store.entry(a).key,
                                             store.entry(b).key);
            return compared != 0 ? compared < 0 : a < b;
        });
        size_t first = 0U;
        while (first < ids.size()) {
            size_t end = first + 1U;
            while (end < ids.size()
                   && compare_key(store.arena(), store.entry(ids[first]).key,
                                  store.entry(ids[end]).key)
                          == 0) {
                ++end;
            }
            for (size_t i = first + 1U; i < end; ++i) {
                const EntryId other = ids[i];
                append_issue(out, options, ValidateIssueSeverity::Error,
                             MetadataValidationIssueCode::DuplicateSingleton,
                             other, ids[first], MetaKeyKind::ExifTag,
                             store.entry(ids[first]).key.data.exif_tag.tag);
            }
            first = end;
        }
    }

    static void finalize_result(const MetadataValidationOptions& options,
                                MetadataValidationResult* out) noexcept
    {
        if (!out || out->status == MetadataValidationStatus::InvalidArgument
            || out->status == MetadataValidationStatus::LimitExceeded) {
            return;
        }
        if (out->error_count != 0U
            || (options.warnings_as_errors && out->warning_count != 0U)) {
            out->status = MetadataValidationStatus::InvalidMetadata;
        }
    }

    static bool validation_options_are_valid(
        const MetadataValidationOptions& options) noexcept
    {
        return options.max_issues != 0U && options.max_key_bytes != 0U
               && options.max_entries != 0U && options.max_arena_bytes != 0U
               && options.max_value_bytes != 0U
               && options.max_value_bytes <= options.max_arena_bytes
               && static_cast<uint8_t>(options.unknown_exif_tags)
                      <= static_cast<uint8_t>(MetadataUnknownTagPolicy::Error)
               && (!options.context.has_dimensions
                   || (options.context.width != 0U
                       && options.context.height != 0U))
               && (!options.context.has_samples_per_pixel
                   || options.context.samples_per_pixel != 0U)
               && (!options.context.has_color_planes
                   || options.context.color_planes != 0U);
    }

}  // namespace


MetadataValidationResult
validate_entry(const MetaStore& store, EntryId entry,
               const MetadataValidationOptions& options) noexcept
{
    MetadataValidationResult out;
    if (!validation_options_are_valid(options)) {
        out.status = MetadataValidationStatus::InvalidArgument;
        return out;
    }
    if (store.entries().size() > options.max_entries
        || store.arena().bytes().size() > options.max_arena_bytes) {
        append_issue(&out, options, ValidateIssueSeverity::Error,
                     MetadataValidationIssueCode::StoreLimitExceeded,
                     kInvalidEntryId, kInvalidEntryId, MetaKeyKind::ExifTag,
                     0U);
        out.status = MetadataValidationStatus::LimitExceeded;
        return out;
    }
    if (static_cast<size_t>(entry) >= store.entries().size()) {
        out.status = MetadataValidationStatus::InvalidArgument;
        append_issue(&out, options, ValidateIssueSeverity::Error,
                     MetadataValidationIssueCode::InvalidEntryId, entry,
                     kInvalidEntryId, MetaKeyKind::ExifTag, 0U);
        return out;
    }
    if (!store.is_finalized()) {
        append_issue(&out, options,
                     options.require_finalized ? ValidateIssueSeverity::Error
                                               : ValidateIssueSeverity::Warning,
                     MetadataValidationIssueCode::StoreNotFinalized,
                     kInvalidEntryId, kInvalidEntryId, MetaKeyKind::ExifTag,
                     0U);
    }
    validate_one(store, entry, options, &out);
    finalize_result(options, &out);
    return out;
}


MetadataValidationResult
validate_store(const MetaStore& store,
               const MetadataValidationOptions& options) noexcept
{
    MetadataValidationResult out;
    if (!validation_options_are_valid(options)) {
        out.status = MetadataValidationStatus::InvalidArgument;
        return out;
    }
    if (store.entries().size() > options.max_entries
        || store.arena().bytes().size() > options.max_arena_bytes) {
        append_issue(&out, options, ValidateIssueSeverity::Error,
                     MetadataValidationIssueCode::StoreLimitExceeded,
                     kInvalidEntryId, kInvalidEntryId, MetaKeyKind::ExifTag,
                     0U);
        out.status = MetadataValidationStatus::LimitExceeded;
        return out;
    }
    if (!store.is_finalized()) {
        append_issue(&out, options,
                     options.require_finalized ? ValidateIssueSeverity::Error
                                               : ValidateIssueSeverity::Warning,
                     MetadataValidationIssueCode::StoreNotFinalized,
                     kInvalidEntryId, kInvalidEntryId, MetaKeyKind::ExifTag,
                     0U);
    }
    for (EntryId id = 0U; id < static_cast<EntryId>(store.entries().size());
         ++id) {
        validate_one(store, id, options, &out);
        if (out.status == MetadataValidationStatus::LimitExceeded) {
            return out;
        }
    }
    validate_duplicate_singletons(store, options, &out);
    if (store.is_finalized()) {
        validate_image_context(store, options, &out);
    }
    finalize_result(options, &out);
    return out;
}


const char*
metadata_validation_status_name(MetadataValidationStatus status) noexcept
{
    switch (status) {
    case MetadataValidationStatus::Ok: return "ok";
    case MetadataValidationStatus::InvalidArgument: return "invalid_argument";
    case MetadataValidationStatus::InvalidMetadata: return "invalid_metadata";
    case MetadataValidationStatus::LimitExceeded: return "limit_exceeded";
    }
    return "unknown";
}


const char*
metadata_validation_issue_code_name(MetadataValidationIssueCode code) noexcept
{
    switch (code) {
    case MetadataValidationIssueCode::None: return "none";
    case MetadataValidationIssueCode::StoreNotFinalized:
        return "store_not_finalized";
    case MetadataValidationIssueCode::InvalidEntryId: return "invalid_entry_id";
    case MetadataValidationIssueCode::StoreLimitExceeded:
        return "store_limit_exceeded";
    case MetadataValidationIssueCode::ValueLimitExceeded:
        return "value_limit_exceeded";
    case MetadataValidationIssueCode::InvalidKey: return "invalid_key";
    case MetadataValidationIssueCode::InvalidOrigin: return "invalid_origin";
    case MetadataValidationIssueCode::InvalidValueShape:
        return "invalid_value_shape";
    case MetadataValidationIssueCode::ScalarOutOfRange:
        return "scalar_out_of_range";
    case MetadataValidationIssueCode::RationalDenominatorZero:
        return "rational_denominator_zero";
    case MetadataValidationIssueCode::InvalidText: return "invalid_text";
    case MetadataValidationIssueCode::InvalidWireType:
        return "invalid_wire_type";
    case MetadataValidationIssueCode::InvalidWireCount:
        return "invalid_wire_count";
    case MetadataValidationIssueCode::WrongIfd: return "wrong_ifd";
    case MetadataValidationIssueCode::WrongType: return "wrong_type";
    case MetadataValidationIssueCode::WrongCount: return "wrong_count";
    case MetadataValidationIssueCode::DuplicateSingleton:
        return "duplicate_singleton";
    case MetadataValidationIssueCode::InvalidXmpNamespace:
        return "invalid_xmp_namespace";
    case MetadataValidationIssueCode::InvalidXmpPropertyPath:
        return "invalid_xmp_property_path";
    case MetadataValidationIssueCode::UnknownExifTag: return "unknown_exif_tag";
    case MetadataValidationIssueCode::ImageContextMismatch:
        return "image_context_mismatch";
    case MetadataValidationIssueCode::InconsistentRelatedEntries:
        return "inconsistent_related_entries";
    case MetadataValidationIssueCode::IssueLimitExceeded:
        return "issue_limit_exceeded";
    }
    return "unknown";
}

}  // namespace openmeta
