// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace openmeta {
namespace internal {

    struct IccProfileLayout final {
        uint32_t tag_count   = 0U;
        uint32_t data_offset = 0U;
    };

    struct IccProfileTagLayout final {
        uint32_t offset     = 0U;
        uint32_t size       = 0U;
        uint64_t end_offset = 0U;
    };

    static inline bool icc_profile_layout_begin(uint64_t tag_count,
                                                IccProfileLayout* out) noexcept
    {
        if (!out || tag_count == 0U) {
            return false;
        }

        const uint64_t max_u32 = static_cast<uint64_t>(
            std::numeric_limits<uint32_t>::max());
        if (tag_count > max_u32) {
            return false;
        }

        constexpr uint64_t kTagTableStart     = 132U;
        constexpr uint64_t kTagTableEntrySize = 12U;
        if (tag_count > (max_u32 - kTagTableStart) / kTagTableEntrySize) {
            return false;
        }

        const uint64_t table_end = kTagTableStart
                                   + tag_count * kTagTableEntrySize;
        const uint64_t data_offset = (table_end + 3U) & ~uint64_t { 3U };
        if (data_offset > max_u32) {
            return false;
        }

        out->tag_count   = static_cast<uint32_t>(tag_count);
        out->data_offset = static_cast<uint32_t>(data_offset);
        return true;
    }

    static inline bool icc_profile_layout_tag(uint64_t cursor,
                                              uint64_t tag_size,
                                              IccProfileTagLayout* out) noexcept
    {
        if (!out) {
            return false;
        }

        const uint64_t max_u32 = static_cast<uint64_t>(
            std::numeric_limits<uint32_t>::max());
        if (cursor > max_u32 - 3U || tag_size > max_u32) {
            return false;
        }

        const uint64_t offset = (cursor + 3U) & ~uint64_t { 3U };
        if (offset > max_u32 || tag_size > max_u32 - offset) {
            return false;
        }

        out->offset     = static_cast<uint32_t>(offset);
        out->size       = static_cast<uint32_t>(tag_size);
        out->end_offset = offset + tag_size;
        return true;
    }

    static inline bool icc_profile_layout_finish(uint64_t profile_size,
                                                 uint32_t* out_size) noexcept
    {
        if (!out_size
            || profile_size
                   > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
            || profile_size > static_cast<uint64_t>(
                   std::numeric_limits<size_t>::max())) {
            return false;
        }

        *out_size = static_cast<uint32_t>(profile_size);
        return true;
    }

}  // namespace internal
}  // namespace openmeta
