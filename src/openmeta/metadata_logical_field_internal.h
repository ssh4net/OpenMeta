// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/metadata_creation.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace openmeta::detail {

inline constexpr size_t kMetadataLogicalFieldKindCount = 24U;

struct MetadataLogicalFieldDescriptor final {
    std::string_view schema_ns;
    std::string_view property_path;
    MetadataCreationValueKind value_kind = MetadataCreationValueKind::Text;
    bool repeated                        = false;
};

bool
metadata_logical_field_descriptor(MetadataCreationFieldKind kind,
                                  MetadataLogicalFieldDescriptor* out) noexcept;

bool
metadata_logical_text_is_valid(std::string_view text) noexcept;

bool
metadata_logical_field_value_is_valid(
    const MetadataCreationField& field) noexcept;

std::string_view
metadata_logical_indexed_property_path(std::string_view base, uint32_t index,
                                       std::array<char, 48U>* storage) noexcept;

}  // namespace openmeta::detail
