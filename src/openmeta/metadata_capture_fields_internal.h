// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openmeta/meta_value.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace openmeta::detail {

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
