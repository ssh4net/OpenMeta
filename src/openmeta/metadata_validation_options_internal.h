// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openmeta/validate.h"

namespace openmeta::detail {

inline bool
metadata_validation_options_are_valid(
    const MetadataValidationOptions& options) noexcept
{
    const MetadataTargetProfileContext& target = options.target_profile;
    const uint8_t profile = static_cast<uint8_t>(target.profile);
    const uint8_t interop = static_cast<uint8_t>(target.interoperability);
    const bool target_profile_valid
        = profile <= static_cast<uint8_t>(MetadataTargetProfile::ExifTiffYCbCr)
          && (target.profile == MetadataTargetProfile::None
              || (interop
                      <= static_cast<uint8_t>(MetadataTargetInteropIndex::THM)
                  && (target.exif_version == 300U || target.exif_version == 310U)
                  && options.context.has_dimensions
                  && options.context.has_samples_per_pixel
                  && options.context.samples_per_pixel == 3U
                  && (!target.has_color_space || target.color_space == 1U
                      || target.color_space == 0xffffU)
                  && (target.profile == MetadataTargetProfile::ExifJpeg
                      || target.interoperability
                             == MetadataTargetInteropIndex::Unspecified)
                  && !(target.profile == MetadataTargetProfile::ExifJpeg
                       && target.require_storage_tags)));

    return options.max_issues != 0U && options.max_key_bytes != 0U
           && options.max_entries != 0U && options.max_arena_bytes != 0U
           && options.max_value_bytes != 0U
           && options.max_value_bytes <= options.max_arena_bytes
           && static_cast<uint8_t>(options.unknown_exif_tags)
                  <= static_cast<uint8_t>(MetadataUnknownTagPolicy::Error)
           && target_profile_valid
           && (!options.context.has_dimensions
               || (options.context.width != 0U && options.context.height != 0U))
           && (!options.context.has_samples_per_pixel
               || options.context.samples_per_pixel != 0U)
           && (!options.context.has_color_planes
               || options.context.color_planes != 0U);
}

}  // namespace openmeta::detail
