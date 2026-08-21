// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/meta_store.h"

#include <cstddef>
#include <span>

namespace openmeta::raf_internal {

bool
looks_like_raf(std::span<const std::byte> file_bytes) noexcept;

ExifDecodeResult
decode_raf_native(std::span<const std::byte> file_bytes, MetaStore& store,
                  const ExifDecodeLimits& limits) noexcept;

ExifRandomAccessDecodeResult
decode_raf_native_random_access(
    const RandomAccessSourceRange& source, MetaStore& store,
    const ExifRandomAccessScratch& scratch, const ExifDecodeLimits& limits,
    const RandomAccessReadLimits& read_limits) noexcept;

}  // namespace openmeta::raf_internal
