// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openmeta/meta_value.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace openmeta::detail {

inline constexpr bool
capture_enum_tag(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x8822U:
    case 0x8830U:
    case 0x9207U:
    case 0x9208U:
    case 0x9209U:
    case 0xa217U:
    case 0xa401U:
    case 0xa402U:
    case 0xa403U:
    case 0xa406U:
    case 0xa407U:
    case 0xa408U:
    case 0xa409U:
    case 0xa40aU:
    case 0xa40cU: return true;
    default: return false;
    }
}

inline constexpr bool
capture_enum_code_valid(uint16_t tag, uint64_t code) noexcept
{
    switch (tag) {
    case 0x8822U: return code <= 8U;
    case 0x8830U: return code <= 7U;
    case 0x9207U: return code <= 6U || code == 255U;
    case 0x9208U:
        return code <= 4U || (code >= 9U && code <= 34U) || code == 255U;
    case 0x9209U: return code <= 127U && ((code >> 1U) & 3U) != 1U;
    case 0xa217U: return code >= 1U && code <= 8U && code != 6U;
    case 0xa401U:
    case 0xa403U: return code <= 1U;
    case 0xa402U:
    case 0xa408U:
    case 0xa409U:
    case 0xa40aU: return code <= 2U;
    case 0xa406U:
    case 0xa40cU: return code <= 3U;
    case 0xa407U: return code <= 4U;
    default: return false;
    }
}

inline bool
native_ascii_view(const ByteArena& arena, const MetaValue& value,
                  std::string_view* text) noexcept
{
    if (value.kind != MetaValueKind::Text || value.count != value.data.span.size
        || (value.text_encoding != TextEncoding::Ascii
            && value.text_encoding != TextEncoding::Utf8
            && value.text_encoding != TextEncoding::Unknown))
        return false;
    const std::span<const std::byte> raw = arena.span(value.data.span);
    if (raw.size() != value.count)
        return false;
    *text = std::string_view(reinterpret_cast<const char*>(raw.data()),
                             raw.size());
    if (!text->empty() && text->back() == '\0')
        text->remove_suffix(1U);
    for (char c : *text) {
        if (c == '\0' || static_cast<unsigned char>(c) > 127U)
            return false;
    }
    return true;
}

inline bool
lens_specification_value_valid(const ByteArena& arena,
                               const MetaValue& value) noexcept
{
    if (value.kind != MetaValueKind::Array
        || value.elem_type != MetaElementType::URational || value.count != 4U
        || value.data.span.size != 4U * sizeof(URational))
        return false;
    const std::span<const std::byte> raw = arena.span(value.data.span);
    if (raw.size() != 4U * sizeof(URational))
        return false;
    URational values[4] {};
    std::memcpy(values, raw.data(), sizeof(values));
    for (size_t i = 0U; i < 4U; ++i) {
        if (i >= 2U && values[i].numer == 0U && values[i].denom == 0U)
            continue;
        if (values[i].numer == 0U || values[i].denom == 0U)
            return false;
    }
    return static_cast<uint64_t>(values[0].numer) * values[1].denom
           <= static_cast<uint64_t>(values[1].numer) * values[0].denom;
}

inline bool
standard_capture_value_valid(const ByteArena& arena, uint16_t tag,
                             const MetaValue& value) noexcept
{
    if (capture_enum_tag(tag))
        return value.kind == MetaValueKind::Scalar && value.count == 1U
               && value.elem_type == MetaElementType::U16
               && capture_enum_code_valid(tag, value.data.u64);
    if (tag >= 0x8831U && tag <= 0x8835U)
        return value.kind == MetaValueKind::Scalar && value.count == 1U
               && value.elem_type == MetaElementType::U32 && value.data.u64 > 0U
               && value.data.u64 <= UINT32_MAX;
    switch (tag) {
    case 0x9201U:
    case 0x9203U:
        return value.kind == MetaValueKind::Scalar && value.count == 1U
               && value.elem_type == MetaElementType::SRational
               && value.data.sr.denom != 0;
    case 0x9202U:
    case 0x9205U:
    case 0x9206U:
    case 0xa20bU:
    case 0xa404U:
    case 0xa20eU:
    case 0xa20fU:
    case 0xa215U:
        return value.kind == MetaValueKind::Scalar && value.count == 1U
               && value.elem_type == MetaElementType::URational
               && value.data.ur.denom != 0U
               && ((tag != 0xa20eU && tag != 0xa20fU && tag != 0xa215U)
                   || value.data.ur.numer != 0U);
    case 0xa210U:
        return value.kind == MetaValueKind::Scalar && value.count == 1U
               && value.elem_type == MetaElementType::U16
               && value.data.u64 >= 1U && value.data.u64 <= 5U;
    case 0x9214U:
    case 0xa214U:
        return value.kind == MetaValueKind::Array
               && value.elem_type == MetaElementType::U16 && value.count >= 2U
               && value.count <= (tag == 0x9214U ? 4U : 2U)
               && value.data.span.size == value.count * sizeof(uint16_t)
               && arena.span(value.data.span).size() == value.data.span.size;
    case 0xa432U: return lens_specification_value_valid(arena, value);
    case 0x8824U:
    case 0xa431U:
    case 0xa435U:
    case 0xa420U:
    case 0x9290U:
    case 0x9291U:
    case 0x9292U: {
        std::string_view text;
        if (!native_ascii_view(arena, value, &text))
            return false;
        if (tag == 0xa420U) {
            if (text.size() != 32U)
                return false;
            for (char c : text) {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                      || (c >= 'A' && c <= 'F')))
                    return false;
            }
        } else if (tag >= 0x9290U && tag <= 0x9292U) {
            for (char c : text) {
                if ((c < '0' || c > '9') && c != ' ')
                    return false;
            }
        }
        return true;
    }
    default: return true;
    }
}

inline constexpr bool
environment_tag(uint16_t tag) noexcept
{
    return tag >= 0x9400U && tag <= 0x9405U;
}

inline constexpr bool
additional_capture_tag(uint16_t tag) noexcept
{
    return environment_tag(tag) || tag == 0xa405U || tag == 0xa300U
           || tag == 0xa301U;
}

inline constexpr bool
environment_property(std::string_view name) noexcept
{
    return name == "Temperature" || name == "Humidity" || name == "Pressure"
           || name == "WaterDepth" || name == "Acceleration"
           || name == "CameraElevationAngle";
}

// Native shapes and defined values shared by validation and portable emission.
inline bool
additional_capture_value_valid(const ByteArena& arena, uint16_t tag,
                               const MetaValue& value) noexcept
{
    if (tag == 0xa300U || tag == 0xa301U) {
        if (value.kind != MetaValueKind::Bytes || value.count != 1U
            || value.data.span.size != 1U)
            return false;
        const auto bytes = arena.span(value.data.span);
        if (bytes.size() != 1U)
            return false;
        const auto code = std::to_integer<uint8_t>(bytes[0]);
        return tag == 0xa300U ? code <= 3U : code == 1U;
    }
    if (value.kind != MetaValueKind::Scalar || value.count != 1U)
        return false;
    if (tag == 0xa405U)
        return value.elem_type == MetaElementType::U16
               && value.data.u64 <= UINT16_MAX;
    if (tag == 0x9400U || tag == 0x9403U || tag == 0x9405U) {
        if (value.elem_type != MetaElementType::SRational)
            return false;
        const auto r = value.data.sr;
        if (r.denom == -1)
            return true;  // Unknown: preserve raw numerator and denominator.
        if (r.denom <= 0)
            return false;
        return tag != 0x9405U
               || (int64_t(r.numer) >= -180 * int64_t(r.denom)
                   && int64_t(r.numer) < 180 * int64_t(r.denom));
    }
    return environment_tag(tag) && value.elem_type == MetaElementType::URational
           && value.data.ur.denom != 0U;
}

}  // namespace openmeta::detail
