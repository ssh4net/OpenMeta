// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include <cstdint>
#include <string_view>

/**
 * \file exif_tag_names.h
 * \brief Human-readable names for common EXIF/TIFF tags.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

class MetaStore;
struct Entry;

enum class ExifTagNamePolicy : uint8_t {
    Canonical,
    ExifToolCompat,
};

/**
 * \brief Returns a human-readable EXIF/TIFF tag name for a given IFD token and tag id.
 *
 * The IFD token matches the decoder output (e.g. `"ifd0"`, `"exififd"`, `"gpsifd"`).
 *
 * The returned view does not own its characters. Registry names have static
 * storage duration. Synthesized vendor placeholder names use thread-local
 * scratch storage; treat those views as valid only until the next call to
 * either name lookup function on the same thread. Such a view does not outlive
 * that thread.
 *
 * \return An empty view for unknown tags without a synthesized vendor name.
 */
std::string_view
exif_tag_name(std::string_view ifd, uint16_t tag) noexcept;

/**
 * \brief Returns a human-readable name for an EXIF-tag entry.
 *
 * Canonical names come from the static tag registry or a validated private-table
 * field context. Compatibility policy may also use decode-time contextual
 * variants for ambiguous MakerNote tags.
 *
 * The returned view follows the same non-owning storage and synthesized
 * placeholder lifetime rules documented for `exif_tag_name`.
 */
std::string_view
exif_entry_name(const MetaStore& store, const Entry& entry,
                ExifTagNamePolicy policy) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
