// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "exif_tiff_decode_internal.h"

namespace openmeta::exif_internal {

bool sony_sr2_decrypt(std::span<std::byte> bytes, uint32_t key) noexcept;

void decode_sony_sr2_subifds(SourceTiffReader* source, const TiffConfig& cfg,
                            uint32_t root_offset, uint16_t root_count,
                            MetaStore& store, const ExifDecodeOptions& options,
                            ExifDecodeResult* result) noexcept;

}  // namespace openmeta::exif_internal
