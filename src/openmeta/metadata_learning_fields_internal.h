// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openmeta/meta_flags.h"
#include "openmeta/meta_value.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace openmeta::detail {

inline constexpr uint16_t kLearningOptOutInTag = 0x9287U;
inline constexpr uint32_t kLearningOptOutInHardMaxSets = 64U;

inline uint16_t learning_opt_out_in_u16(std::span<const std::byte> bytes,
                                        size_t offset,
                                        bool little) noexcept
{
    const uint16_t first = std::to_integer<uint8_t>(bytes[offset]);
    const uint16_t second = std::to_integer<uint8_t>(bytes[offset + 1U]);
    return little ? static_cast<uint16_t>(first | (second << 8U))
                  : static_cast<uint16_t>((first << 8U) | second);
}

inline bool
learning_opt_out_in_value_valid(const ByteArena& arena, const MetaValue& value,
                                EntryFlags flags,
                                uint32_t max_sets
                                = kLearningOptOutInHardMaxSets) noexcept
{
    if (max_sets == 0U || max_sets > kLearningOptOutInHardMaxSets
        || value.kind != MetaValueKind::Bytes
        || value.count != value.data.span.size) {
        return false;
    }
    const std::span<const std::byte> bytes = arena.span(value.data.span);
    if (bytes.size() != value.count || bytes.size() < 6U
        || (bytes.size() & 1U) != 0U) {
        return false;
    }
    const bool little = !any(flags, EntryFlags::ValueBigEndian);
    const uint32_t sets = learning_opt_out_in_u16(bytes, 0U, little);
    if (sets == 0U || sets > max_sets
        || bytes.size() != static_cast<size_t>(sets) * 4U + 2U) {
        return false;
    }

    // The first usage value describes all usage types not listed separately.
    if (learning_opt_out_in_u16(bytes, 2U, little) != 0U) {
        return false;
    }
    uint32_t usage_mask = 0U;
    for (uint32_t i = 0U; i < sets; ++i) {
        const size_t offset = 2U + static_cast<size_t>(i) * 4U;
        const uint16_t usage = learning_opt_out_in_u16(bytes, offset, little);
        const uint16_t intention
            = learning_opt_out_in_u16(bytes, offset + 2U, little);
        if (usage > 4U || intention > 2U
            || (usage_mask & (1U << usage)) != 0U) {
            return false;
        }
        usage_mask |= 1U << usage;
    }
    return true;
}

}  // namespace openmeta::detail
