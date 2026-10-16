// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace openmeta::detail {

struct JpegPhotoshopIptcInfo final {
    uint32_t iptc_resources        = 0U;
    uint32_t other_resources       = 0U;
    uint32_t retained_payload_size = 0U;
};

bool
inspect_jpeg_photoshop_iptc(std::span<const std::byte> payload,
                            JpegPhotoshopIptcInfo* out) noexcept;

// Requires a successful inspection of the same immutable payload and a
// disjoint output span whose size equals retained_payload_size.
void
copy_jpeg_photoshop_without_iptc(std::span<const std::byte> payload,
                                 std::span<std::byte> output) noexcept;

}  // namespace openmeta::detail
