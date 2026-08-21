// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/meta_store.h"

#include <cstddef>
#include <span>

namespace openmeta::ciff_internal {

// Best-effort decoder for Canon CRW (CIFF) directory trees.
//
// This is intentionally internal-only for now: CIFF is a vendor-specific
// container and OpenMeta currently exposes its fields as `MetaKeyKind::ExifTag`
// entries under `ifd=ciff_*` tokens.
bool
decode_crw_ciff(std::span<const std::byte> file_bytes, MetaStore& store,
                const ExifDecodeLimits& limits,
                ExifDecodeResult* status_out) noexcept;

ExifRandomAccessDecodeResult
decode_crw_ciff_random_access(
    const RandomAccessSourceRange& source, MetaStore& store,
    const ExifRandomAccessScratch& scratch, const ExifDecodeLimits& limits,
    const RandomAccessReadLimits& read_limits) noexcept;

}  // namespace openmeta::ciff_internal
