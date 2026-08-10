// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include <cstdint>

/**
 * \file meta_flags.h
 * \brief Flags attached to metadata entries.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Per-entry flags used during edits and provenance tracking.
enum class EntryFlags : uint8_t {
    None = 0,
    /// Entry is logically removed (kept for stable ids / provenance).
    Deleted = 1U << 0U,
    /// Entry was modified or added relative to an origin snapshot.
    Dirty = 1U << 1U,
    /// Entry was derived from other data (e.g. EXIF->XMP mapping).
    Derived = 1U << 2U,
    /// Value payload was truncated or omitted due to configured limits.
    Truncated = 1U << 3U,
    /// Value payload could not be read from the underlying container.
    Unreadable = 1U << 4U,
    /// Entry name has a decode-time contextual display variant.
    ContextualName = 1U << 5U,
};

constexpr EntryFlags
operator|(EntryFlags a, EntryFlags b) noexcept
{
    return static_cast<EntryFlags>(static_cast<uint8_t>(a)
                                   | static_cast<uint8_t>(b));
}


constexpr EntryFlags
operator&(EntryFlags a, EntryFlags b) noexcept
{
    return static_cast<EntryFlags>(static_cast<uint8_t>(a)
                                   & static_cast<uint8_t>(b));
}


constexpr EntryFlags&
operator|=(EntryFlags& a, EntryFlags b) noexcept
{
    a = a | b;
    return a;
}


/// Returns true if any bits in \p test are present in \p flags.
constexpr bool
any(EntryFlags flags, EntryFlags test) noexcept
{
    return static_cast<uint8_t>(flags & test) != 0;
}

}  // namespace openmeta
OPENMETA_PUBLIC_END
