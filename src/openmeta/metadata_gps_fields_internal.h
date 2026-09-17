// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "metadata_capture_fields_internal.h"

#include <cstring>

namespace openmeta::detail {

inline constexpr bool
gps_reference_code_valid(uint16_t tag, char code) noexcept
{
    switch (tag) {
    case 0x0001U:
    case 0x0013U: return code == 'N' || code == 'S';
    case 0x0003U:
    case 0x0015U: return code == 'E' || code == 'W';
    case 0x0009U: return code == 'A' || code == 'V';
    case 0x000aU: return code == '2' || code == '3';
    case 0x000cU:
    case 0x0019U: return code == 'K' || code == 'M' || code == 'N';
    case 0x000eU:
    case 0x0010U:
    case 0x0017U: return code == 'T' || code == 'M';
    default: return false;
    }
}

inline bool
gps_prefixed_value_valid(const ByteArena& arena,
                         const MetaValue& value) noexcept
{
    if (value.kind != MetaValueKind::Bytes || value.count < 8U
        || value.count != value.data.span.size)
        return false;
    const std::span<const std::byte> bytes = arena.span(value.data.span);
    if (bytes.size() != value.count)
        return false;
    if (std::memcmp(bytes.data(), "ASCII\0\0", 8U) == 0) {
        for (size_t i = 8U; i < bytes.size(); ++i) {
            if (std::to_integer<uint8_t>(bytes[i]) > 127U)
                return false;
        }
        return true;
    }
    // JIS, Unicode and undefined character-code payloads retain their bytes.
    return std::memcmp(bytes.data(), "UNICODE", 8U) == 0
           || std::memcmp(bytes.data(), "JIS\0\0\0\0", 8U) == 0
           || std::memcmp(bytes.data(), "\0\0\0\0\0\0\0", 8U) == 0;
}

inline bool
gps_field_value_valid(const ByteArena& arena, uint16_t tag,
                      const MetaValue& value) noexcept
{
    switch (tag) {
    case 0x0001U:
    case 0x0003U:
    case 0x0009U:
    case 0x000aU:
    case 0x000cU:
    case 0x000eU:
    case 0x0010U:
    case 0x0013U:
    case 0x0015U:
    case 0x0017U:
    case 0x0019U: {
        std::string_view text;
        return native_ascii_view(arena, value, &text) && text.size() == 1U
               && gps_reference_code_valid(tag, text.front());
    }
    case 0x0008U:
    case 0x0012U: {
        std::string_view text;
        return native_ascii_view(arena, value, &text);
    }
    case 0x000bU:
    case 0x000dU:
    case 0x000fU:
    case 0x0011U:
    case 0x0018U:
    case 0x001aU:
    case 0x001fU:
        return value.kind == MetaValueKind::Scalar && value.count == 1U
               && value.elem_type == MetaElementType::URational
               && value.data.ur.denom != 0U
               && ((tag != 0x000fU && tag != 0x0011U && tag != 0x0018U)
                   || value.data.ur.numer < 360ULL * value.data.ur.denom);
    case 0x001bU:
    case 0x001cU: return gps_prefixed_value_valid(arena, value);
    case 0x001eU:
        return value.kind == MetaValueKind::Scalar && value.count == 1U
               && value.elem_type == MetaElementType::U16
               && value.data.u64 <= 1U;
    default: return true;
    }
}

}  // namespace openmeta::detail
