// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/metadata_patch.h"

namespace openmeta::detail {

struct CompiledMetadataPatchSlot final {
    uint32_t byte_offset = 0U;
    uint32_t byte_width  = 0U;
    MetadataPatchValueSpec value;
    MetadataPatchPayload family = MetadataPatchPayload::ExifTiff;
};

MetadataPatchResult
metadata_patch_error(MetadataPatchCode code,
                     uint32_t failed_index = UINT32_MAX) noexcept;
bool
valid_exif_patch_spec(const MetadataPatchValueSpec& spec) noexcept;
bool
exif_patch_specs_equal(const MetadataPatchValueSpec& first,
                       const MetadataPatchValueSpec& second) noexcept;
bool
exif_patch_width(const MetadataPatchValueSpec& spec, uint32_t* width) noexcept;

MetadataPatchResult
compile_exif_patch_payload(const MetaStore& store,
                           std::span<const MetadataPatchRequest> requests,
                           const MetadataPatchExifOptions& options,
                           std::span<CompiledMetadataPatchSlot> slots,
                           std::vector<std::byte>* payload) noexcept;

}  // namespace openmeta::detail
