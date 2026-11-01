// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openmeta::detail {

struct IccProfileTagView final {
    uint32_t signature = 0U;
    std::span<const std::byte> bytes;
};

enum class IccProfileFinalTagPadding : uint8_t {
    /// Canonical profile output pads the final tag data to four bytes.
    FourByte,
    /// Clean legacy transfer preserves its historical unpadded final length.
    PreserveLegacyLength,
};

bool
icc_authoring_key_valid(const MetaKeyView& key) noexcept;
bool
icc_authoring_value_valid(const MetaKeyView& key,
                          const MetaValueView& value) noexcept;

bool
icc_srational_triplet_to_s15fixed16(std::span<const std::byte> raw,
                                    uint32_t count,
                                    std::span<std::byte, 12U> output) noexcept;

bool
icc_profile_measure(std::span<const IccProfileTagView> tags,
                    IccProfileFinalTagPadding final_padding,
                    uint32_t* profile_size) noexcept;

// Internal callers provide strictly ascending unique signatures, stable
// borrowed spans, and a fully prepared 128-byte header. The emitter checks span
// pointers and layout before it publishes its local output buffer.
bool
build_icc_profile_bytes_from_items(std::span<const std::byte, 128U> header,
                                   std::span<const IccProfileTagView> tags,
                                   IccProfileFinalTagPadding final_padding,
                                   std::vector<std::byte>* out_profile) noexcept;

}  // namespace openmeta::detail
