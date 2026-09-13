// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/xmp_dump.h"

#include <string_view>

namespace openmeta::detail {

struct XmpScalarPatchSlot final {
    std::string_view ns_uri;
    std::string_view property_path;
    uint64_t byte_offset = 0U;
    uint64_t byte_width  = 0U;
    uint32_t matches     = 0U;
};

XmpDumpResult
dump_xmp_portable_for_patch(const MetaStore& store, std::span<std::byte> output,
                            const XmpPortableOptions& options,
                            std::span<XmpScalarPatchSlot> slots) noexcept;

}  // namespace openmeta::detail
