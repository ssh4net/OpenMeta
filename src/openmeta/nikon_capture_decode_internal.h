// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta::exif_internal {

// The input must remain stable while entries are appended to `store`.
void
decode_nikon_capture(std::span<const std::byte> stable_payload,
                     MetaStore& store, const ExifDecodeOptions& options,
                     ExifDecodeResult* status_out) noexcept;

// Name indices are decoder-wide identifiers. They do not encode the field
// byte offset, because offsets are meaningful only inside each record type.
std::string_view
nikon_capture_field_name(uint8_t index) noexcept;

}  // namespace openmeta::exif_internal
