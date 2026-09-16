// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "metadata_encoding_fields_internal.h"

#include <bit>

namespace openmeta::detail {

inline constexpr bool
structured_capture_tag(uint16_t tag) noexcept
{
    return tag == 0x8828U || tag == 0xa20cU || tag == 0xa302U || tag == 0xa40bU;
}

struct StructuredCaptureView final {
    uint16_t columns      = 0U;
    uint16_t rows         = 0U;
    size_t values_offset  = 4U;
    uint32_t values_count = 0U;
};

inline bool
capture_xml_character(uint32_t c) noexcept
{
    return c == 9U || c == 10U || c == 13U || (c >= 32U && c <= 0xd7ffU)
           || (c >= 0xe000U && c <= 0xfffdU)
           || (c >= 0x10000U && c <= 0x10ffffU);
}

inline bool
capture_utf8_next(std::string_view text, size_t* offset,
                  uint32_t* code) noexcept
{
    if (*offset >= text.size())
        return false;
    const uint8_t first = static_cast<uint8_t>(text[(*offset)++]);
    unsigned following  = 0U;
    uint32_t minimum    = 0U;
    uint32_t value      = first;
    if (first >= 0xc2U && first <= 0xdfU) {
        following = 1U;
        minimum   = 0x80U;
        value &= 0x1fU;
    } else if (first >= 0xe0U && first <= 0xefU) {
        following = 2U;
        minimum   = 0x800U;
        value &= 0x0fU;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        following = 3U;
        minimum   = 0x10000U;
        value &= 7U;
    } else if (first >= 0x80U)
        return false;
    if (text.size() - *offset < following)
        return false;
    for (unsigned i = 0U; i < following; ++i) {
        const uint8_t next = static_cast<uint8_t>(text[(*offset)++]);
        if ((next & 0xc0U) != 0x80U)
            return false;
        value = (value << 6U) | (next & 0x3fU);
    }
    if (value < minimum || !capture_xml_character(value))
        return false;
    *code = value;
    return true;
}

inline bool
capture_utf16_next(std::span<const std::byte> raw, size_t* offset, bool little,
                   uint32_t* code) noexcept
{
    if (*offset > raw.size() || raw.size() - *offset < 2U)
        return false;
    uint32_t value = composite_uint(raw, *offset, 2U, little);
    *offset += 2U;
    if (value >= 0xd800U && value <= 0xdbffU) {
        if (raw.size() - *offset < 2U)
            return false;
        const uint32_t low = composite_uint(raw, *offset, 2U, little);
        *offset += 2U;
        if (low < 0xdc00U || low > 0xdfffU)
            return false;
        value = 0x10000U + ((value - 0xd800U) << 10U) + low - 0xdc00U;
    }
    if (value != 0U && !capture_xml_character(value))
        return false;
    *code = value;
    return true;
}

// Each setting has its own BOM; its byte order is independent of the TIFF header.
inline bool
capture_setting(std::span<const std::byte> raw, size_t* offset,
                std::span<const std::byte>* text, bool* little) noexcept
{
    if (*offset > raw.size() || raw.size() - *offset < 4U)
        return false;
    const uint32_t bom = composite_uint(raw, *offset, 2U, true);
    if (bom != 0xfeffU && bom != 0xfffeU)
        return false;
    *little = bom == 0xfeffU;
    *offset += 2U;
    const size_t begin = *offset;
    uint32_t code      = 0U;
    do {
        if (!capture_utf16_next(raw, offset, *little, &code))
            return false;
    } while (code != 0U);
    *text = raw.subspan(begin, *offset - begin - 2U);
    return true;
}

inline bool
structured_capture_view(std::span<const std::byte> raw, uint16_t tag,
                        bool little, StructuredCaptureView* out) noexcept
{
    if (!structured_capture_tag(tag) || raw.size() < 4U)
        return false;
    StructuredCaptureView view;
    view.columns = static_cast<uint16_t>(composite_uint(raw, 0U, 2U, little));
    view.rows    = static_cast<uint16_t>(composite_uint(raw, 2U, 2U, little));
    if (view.columns == 0U || view.rows == 0U)
        return false;
    if (tag == 0xa40bU) {
        size_t offset = 4U;
        while (offset < raw.size()) {
            std::span<const std::byte> text;
            bool text_little = true;
            if (!capture_setting(raw, &offset, &text, &text_little)
                || view.values_count == UINT32_MAX)
                return false;
            ++view.values_count;
        }
        if (view.values_count == 0U)
            return false;
    } else {
        view.values_count = static_cast<uint32_t>(view.columns) * view.rows;
        if (tag == 0x8828U || tag == 0xa20cU) {
            size_t offset = 4U;
            for (uint32_t i = 0U; i < view.columns; ++i) {
                while (offset < raw.size() && raw[offset] != std::byte { 0 }) {
                    const uint32_t c = std::to_integer<uint8_t>(raw[offset++]);
                    if (c >= 128U || !capture_xml_character(c))
                        return false;
                }
                if (offset == raw.size())
                    return false;
                ++offset;
            }
            view.values_offset = offset;
            if (raw.size() - offset
                != static_cast<uint64_t>(view.values_count) * 8U)
                return false;
            for (; offset < raw.size(); offset += 8U)
                if (composite_uint(raw, offset + 4U, 4U, little) == 0U)
                    return false;
        } else {
            if (raw.size() - 4U != view.values_count)
                return false;
            for (size_t offset = 4U; offset < raw.size(); ++offset)
                if (std::to_integer<uint8_t>(raw[offset]) > 6U)
                    return false;
        }
    }
    if (out)
        *out = view;
    return true;
}

inline bool
structured_capture_value_valid(const ByteArena& arena, uint16_t tag,
                               const MetaValue& value,
                               EntryFlags flags) noexcept
{
    if (value.kind != MetaValueKind::Bytes
        || value.count != value.data.span.size)
        return false;
    const std::span<const std::byte> raw = arena.span(value.data.span);
    return raw.size() == value.count
           && structured_capture_view(raw, tag,
                                      !any(flags, EntryFlags::ValueBigEndian),
                                      nullptr);
}

inline bool
structured_capture_swap(std::span<std::byte> raw, uint16_t tag,
                        bool little) noexcept
{
    StructuredCaptureView view;
    if (!structured_capture_view(raw, tag, little, &view))
        return false;
    for (size_t offset = 0U; offset < 4U; offset += 2U) {
        const std::byte first = raw[offset];
        raw[offset]           = raw[offset + 1U];
        raw[offset + 1U]      = first;
    }
    if (tag == 0x8828U || tag == 0xa20cU) {
        for (size_t offset = view.values_offset; offset < raw.size();
             offset += 4U) {
            const std::byte first  = raw[offset];
            const std::byte second = raw[offset + 1U];
            raw[offset]            = raw[offset + 3U];
            raw[offset + 1U]       = raw[offset + 2U];
            raw[offset + 2U]       = second;
            raw[offset + 3U]       = first;
        }
    }
    return true;
}

inline bool
structured_capture_equal(std::span<const std::byte> actual, bool little,
                         std::span<const std::byte> expected,
                         uint16_t tag) noexcept
{
    StructuredCaptureView a, b;
    if (!structured_capture_view(actual, tag, little, &a)
        || !structured_capture_view(expected, tag, true, &b)
        || a.columns != b.columns || a.rows != b.rows
        || a.values_count != b.values_count)
        return false;
    if (tag == 0xa40bU) {
        size_t a_offset = 4U, b_offset = 4U;
        for (uint32_t i = 0U; i < a.values_count; ++i) {
            std::span<const std::byte> a_text, b_text;
            bool a_little = true, b_little = true;
            if (!capture_setting(actual, &a_offset, &a_text, &a_little)
                || !capture_setting(expected, &b_offset, &b_text, &b_little))
                return false;
            size_t ai = 0U, bi = 0U;
            while (ai < a_text.size() && bi < b_text.size()) {
                uint32_t ac = 0U, bc = 0U;
                if (!capture_utf16_next(a_text, &ai, a_little, &ac)
                    || !capture_utf16_next(b_text, &bi, b_little, &bc)
                    || ac != bc)
                    return false;
            }
            if (ai != a_text.size() || bi != b_text.size())
                return false;
        }
        return true;
    }
    if (a.values_offset != b.values_offset || actual.size() != expected.size()
        || std::memcmp(actual.data() + 4U, expected.data() + 4U,
                       a.values_offset - 4U)
               != 0)
        return false;
    const unsigned width = tag == 0xa302U ? 1U : 4U;
    for (size_t offset = a.values_offset; offset < actual.size();
         offset += width)
        if (composite_uint(actual, offset, width, little)
            != composite_uint(expected, offset, width, true))
            return false;
    return true;
}

}  // namespace openmeta::detail
