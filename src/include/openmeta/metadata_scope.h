// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <span>
#include <string_view>

/**
 * \file metadata_scope.h
 * \brief Borrowed directory membership supplied by a metadata query caller.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

struct MetadataDirectorySelector final {
    BlockId block = kInvalidBlockId;
    std::string_view ifd;
};

/**
 * Selects metadata directories for one query call.
 *
 * The selector span and its IFD strings are borrowed for the duration of the
 * query call. An enabled scope with no selectors selects no entries. An empty
 * selector IFD selects every directory in its exact block. An invalid block
 * selector matches only entries whose origin also has an invalid block.
 */
struct MetadataQueryScope final {
    bool enabled = false;
    std::span<const MetadataDirectorySelector> directories;
    bool inherit_across_directories = false;
};

inline bool metadata_query_scope_selects_directory(
    const MetadataQueryScope& scope, BlockId block,
    std::string_view ifd) noexcept
{
    if (!scope.enabled) {
        return true;
    }
    for (std::size_t i = 0U; i < scope.directories.size(); ++i) {
        const MetadataDirectorySelector& selector = scope.directories[i];
        if (selector.block != block) {
            continue;
        }
        if (selector.ifd.empty() || selector.ifd == ifd) {
            return true;
        }
    }
    return false;
}

}  // namespace openmeta
OPENMETA_PUBLIC_END
