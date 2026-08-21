// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/meta_store.h"

#include <cstddef>
#include <span>

namespace openmeta::x3f_internal {

bool
looks_like_x3f(std::span<const std::byte> file_bytes) noexcept;

ExifDecodeResult
decode_x3f_native(std::span<const std::byte> file_bytes, MetaStore& store,
                  const ExifDecodeLimits& limits) noexcept;

ExifRandomAccessDecodeResult
decode_x3f_native_random_access(
    const RandomAccessSourceRange& source, MetaStore& store,
    const ExifRandomAccessScratch& scratch, const ExifDecodeLimits& limits,
    const RandomAccessReadLimits& read_limits) noexcept;

}  // namespace openmeta::x3f_internal
