// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "metadata_structured_fields_internal.h"

#include <cstddef>
#include <cstdint>

namespace openmeta::detail {

inline constexpr bool
development_correction_tag(uint16_t tag) noexcept
{
    return tag >= 0xa40dU && tag <= 0xa412U;
}

inline constexpr bool
development_correction_scalar_tag(uint16_t tag) noexcept
{
    return tag == 0xa40dU || (tag >= 0xa40fU && tag <= 0xa412U);
}

inline constexpr bool
development_type_choice(uint64_t value) noexcept
{
    return value == 1U || value == 2U || value == 4U;
}

inline constexpr bool
development_type_value_valid(uint64_t value) noexcept
{
    return development_type_choice((value >> 8U) & 0xffU)
           && development_type_choice(value & 0xffU);
}

inline bool
development_description_value_valid(const ByteArena& arena,
                                    const MetaValue& value) noexcept
{
    if (value.kind != MetaValueKind::Text || value.count != value.data.span.size
        || value.text_encoding != TextEncoding::Utf8) {
        return false;
    }
    const std::span<const std::byte> raw = arena.span(value.data.span);
    if (raw.size() != value.count || (raw.size() > 0U && raw.back() == std::byte { 0 })) {
        // The in-memory text may carry a native terminal NUL. It is not part
        // of the logical description and is removed by the EXIF text view.
        if (raw.empty() || raw.back() != std::byte { 0 }) {
            return false;
        }
    }
    size_t size = raw.size();
    if (size > 0U && raw[size - 1U] == std::byte { 0 }) {
        --size;
    }
    const std::string_view text(reinterpret_cast<const char*>(raw.data()), size);
    size_t offset = 0U;
    while (offset < text.size()) {
        uint32_t code = 0U;
        if (!capture_utf8_next(text, &offset, &code)
            || !capture_xml_character(code)) {
            return false;
        }
    }
    return true;
}

inline bool
development_correction_value_valid(const ByteArena& arena, uint16_t tag,
                                   const MetaValue& value) noexcept
{
    if (tag == 0xa40eU) {
        return development_description_value_valid(arena, value);
    }
    if (!development_correction_scalar_tag(tag)
        || value.kind != MetaValueKind::Scalar || value.count != 1U
        || value.elem_type != MetaElementType::U16) {
        return false;
    }
    if (tag == 0xa40dU) {
        return development_type_value_valid(value.data.u64);
    }
    return value.data.u64 <= (tag == 0xa412U ? 3U : 1U);
}

}  // namespace openmeta::detail
