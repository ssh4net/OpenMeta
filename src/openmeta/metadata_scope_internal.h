// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/metadata_scope.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace openmeta {

inline std::string_view metadata_scope_entry_ifd(const MetaStore& store,
                                                 const Entry& entry) noexcept
{
    if (entry.key.kind != MetaKeyKind::ExifTag) {
        return {};
    }
    const std::span<const std::byte> bytes
        = store.arena().span(entry.key.data.exif_tag.ifd);
    if (bytes.empty()) {
        return {};
    }
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

inline bool metadata_scope_contains(const MetaStore& store,
                                    const MetadataQueryScope& scope,
                                    const Entry& entry) noexcept
{
    return metadata_query_scope_selects_directory(
        scope, entry.origin.block, metadata_scope_entry_ifd(store, entry));
}

inline bool metadata_scope_same_directory(const MetaStore& store,
                                          const Entry& a,
                                          const Entry& b) noexcept
{
    return a.origin.block == b.origin.block
           && metadata_scope_entry_ifd(store, a)
                  == metadata_scope_entry_ifd(store, b);
}

inline bool metadata_scope_can_inherit(const MetaStore& store,
                                       const MetadataQueryScope& scope,
                                       const Entry& a,
                                       const Entry& b) noexcept
{
    if (metadata_scope_same_directory(store, a, b)) {
        return true;
    }
    return scope.enabled && scope.inherit_across_directories
           && metadata_scope_contains(store, scope, a)
           && metadata_scope_contains(store, scope, b);
}

}  // namespace openmeta
