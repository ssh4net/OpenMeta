// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "metadata_structured_fields_internal.h"

namespace openmeta::detail {

inline bool
exif3_text_tag(uint16_t tag) noexcept
{
    return tag >= 0xa436U && tag <= 0xa43cU;
}

inline bool
exif_utf8_tag(std::string_view ifd, uint16_t tag) noexcept
{
    return (ifd == "exififd"
            && (exif3_text_tag(tag) || tag == 0xa430U || tag == 0xa433U
                || tag == 0xa434U))
           || (ifd == "ifd0"
               && (tag == 0x010eU || tag == 0x010fU || tag == 0x0110U
                   || tag == 0x0131U || tag == 0x013bU));
}

inline bool
exif_text_valid(std::string_view text, bool* ascii = nullptr) noexcept
{
    bool all_ascii = true;
    size_t offset  = 0U;
    while (offset < text.size()) {
        uint32_t code = 0U;
        if (!capture_utf8_next(text, &offset, &code)
            || !capture_xml_character(code))
            return false;
        all_ascii = all_ascii && code < 128U;
    }
    if (ascii)
        *ascii = all_ascii;
    return true;
}

inline bool
exif_text_view(const ByteArena& arena, const MetaValue& value, bool native,
               std::string_view* out) noexcept
{
    if (value.kind != MetaValueKind::Text
        || (value.text_encoding != TextEncoding::Ascii
            && value.text_encoding != TextEncoding::Utf8)
        || value.count != value.data.span.size)
        return false;
    const auto raw = arena.span(value.data.span);
    if (raw.size() != value.count)
        return false;
    std::string_view text(reinterpret_cast<const char*>(raw.data()),
                          raw.size());
    if (native && !text.empty() && text.back() == '\0')
        text.remove_suffix(1U);
    bool ascii = false;
    if (!exif_text_valid(text, &ascii)
        || (value.text_encoding == TextEncoding::Ascii && !ascii)
        || text.starts_with("\xef\xbb\xbf"))
        return false;
    *out = text;
    return true;
}

inline uint16_t
exif_text_wire_type(const ByteArena& arena, const Entry& e) noexcept
{
    const auto ifd_raw = arena.span(e.key.data.exif_tag.ifd);
    const std::string_view ifd(reinterpret_cast<const char*>(ifd_raw.data()),
                               ifd_raw.size());
    if (!exif_utf8_tag(ifd, e.key.data.exif_tag.tag))
        return 2U;
    if (e.origin.wire_type.family == WireFamily::Tiff
        && e.origin.wire_type.code == 129U)
        return 129U;
    std::string_view text;
    bool ascii = true;
    if (exif_text_view(arena, e.value, true, &text)
        && exif_text_valid(text, &ascii) && !ascii)
        return 129U;
    return 2U;
}

inline bool
exif_version(std::span<const std::byte> raw, uint32_t* out) noexcept
{
    if (raw.size() != 4U)
        return false;
    uint32_t version = 0U;
    for (std::byte b : raw) {
        const uint32_t c = std::to_integer<uint8_t>(b);
        if (c < '0' || c > '9')
            return false;
        version = version * 10U + c - '0';
    }
    *out = version;
    return true;
}

inline bool
exif_version_value(const ByteArena& arena, const MetaValue& value, uint16_t tag,
                   uint32_t* out) noexcept
{
    return value.kind == MetaValueKind::Bytes && value.count == 4U
           && value.data.span.size == 4U
           && exif_version(arena.span(value.data.span), out)
           && (tag != 0xa000U || *out == 100U);
}

// A missing version selects the legacy interpretation. Duplicates are ambiguous.
inline bool
exif_store_version(const ByteArena& arena, std::span<const Entry> entries,
                   uint32_t* out) noexcept
{
    *out       = 0U;
    bool found = false;
    for (const Entry& e : entries) {
        if (!primary_exif_entry(arena, e, 0x9000U))
            continue;
        if (found || !exif_version_value(arena, e.value, 0x9000U, out))
            return false;
        found = true;
    }
    return true;
}

struct UserCommentView final {
    std::span<const std::byte> text;
    TextEncoding encoding = TextEncoding::Ascii;
    bool container_endian = false;
};

inline bool
user_comment_next(const UserCommentView& view, size_t* offset,
                  uint32_t* code) noexcept
{
    if (view.encoding == TextEncoding::Utf16LE
        || view.encoding == TextEncoding::Utf16BE)
        return capture_utf16_next(view.text, offset,
                                  view.encoding == TextEncoding::Utf16LE, code);
    const std::string_view text(reinterpret_cast<const char*>(view.text.data()),
                                view.text.size());
    return capture_utf8_next(text, offset, code)
           && (view.encoding != TextEncoding::Ascii || *code < 128U);
}

inline bool
user_comment_view(std::span<const std::byte> raw, uint32_t version, bool little,
                  UserCommentView* out) noexcept
{
    if (raw.size() < 8U)
        return false;
    UserCommentView view;
    view.text = raw.subspan(8U);
    if (std::memcmp(raw.data(), "ASCII\0\0\0", 8U) == 0) {
        view.encoding = TextEncoding::Ascii;
    } else if (std::memcmp(raw.data(), "UTF8\0\0\0\0", 8U) == 0) {
        // Read compatibility only. Writers use the standard UNICODE marker.
        view.encoding = TextEncoding::Utf8;
    } else if (std::memcmp(raw.data(), "UNICODE\0", 8U) == 0) {
        if (view.text.size() >= 2U
            && ((view.text[0] == std::byte { 0xff }
                 && view.text[1] == std::byte { 0xfe })
                || (view.text[0] == std::byte { 0xfe }
                    && view.text[1] == std::byte { 0xff }))) {
            little        = view.text[0] == std::byte { 0xff };
            view.text     = view.text.subspan(2U);
            view.encoding = little ? TextEncoding::Utf16LE
                                   : TextEncoding::Utf16BE;
        } else if (version >= 300U) {
            view.encoding = TextEncoding::Utf8;
        } else {
            view.encoding         = little ? TextEncoding::Utf16LE
                                           : TextEncoding::Utf16BE;
            view.container_endian = true;
        }
    } else {
        return false;  // JIS and undefined character sets stay opaque.
    }
    const bool utf16 = view.encoding == TextEncoding::Utf16LE
                       || view.encoding == TextEncoding::Utf16BE;
    if (utf16 && view.text.size() % 2U != 0U)
        return false;
    const size_t unit = utf16 ? 2U : 1U;
    while (view.text.size() >= unit && view.text.back() == std::byte { 0 }
           && (!utf16 || view.text[view.text.size() - 2U] == std::byte { 0 }))
        view.text = view.text.first(view.text.size() - unit);
    size_t offset = 0U;
    while (offset < view.text.size()) {
        uint32_t code = 0U;
        if (!user_comment_next(view, &offset, &code)
            || !capture_xml_character(code))
            return false;
    }
    if (out)
        *out = view;
    return true;
}

inline bool
user_comment_value(const ByteArena& arena, const MetaValue& value,
                   uint32_t version, EntryFlags flags,
                   UserCommentView* out) noexcept
{
    return value.kind == MetaValueKind::Bytes
           && value.count == value.data.span.size
           && arena.span(value.data.span).size() == value.count
           && user_comment_view(arena.span(value.data.span), version,
                                !any(flags, EntryFlags::ValueBigEndian), out);
}

inline bool
user_comment_swap(std::span<std::byte> raw, uint32_t version,
                  bool little) noexcept
{
    UserCommentView view;
    if (!user_comment_view(raw, version, little, &view))
        return false;
    if (view.container_endian) {
        for (size_t i = 8U; i < raw.size(); i += 2U) {
            const std::byte b = raw[i];
            raw[i]            = raw[i + 1U];
            raw[i + 1U]       = b;
        }
    }
    return true;
}

}  // namespace openmeta::detail
