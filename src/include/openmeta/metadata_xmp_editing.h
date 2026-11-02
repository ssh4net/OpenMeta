// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_editing.h"

#include <cstdint>
#include <span>
#include <string_view>

/**
 * \file metadata_xmp_editing.h
 * \brief Bounded transactional editing of supported structured XMP scopes.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

inline constexpr uint32_t kMetadataXmpEditingContractVersion       = 2U;
inline constexpr uint32_t kMetadataXmpEditingMaxOperations         = 1024U;
inline constexpr uint32_t kMetadataXmpEditingMaxExpandedOperations = 4096U;
inline constexpr uint32_t kMetadataXmpEditingMaxScopeEntries       = 4096U;
inline constexpr uint32_t kMetadataXmpEditingMaxPathDepth          = 3U;
inline constexpr uint32_t kMetadataXmpEditingMaxArrayIndex         = 65535U;

enum class MetadataXmpEditingOperationKind : uint8_t {
    Add,
    Replace,
    Remove,
};

/// One ordered edit to an exact XMP schema namespace and structured scope.
struct MetadataXmpEditingOperation final {
    MetadataXmpEditingOperationKind kind = MetadataXmpEditingOperationKind::Add;
    std::string_view schema_ns;
    std::string_view property_path;
    std::span<const MetadataAuthoringEntry> entries;
};

/// Resource ceilings for preparation and the single typed-edit transaction.
/// max_scope_entries bounds the aggregate active entries tracked across every
/// targeted root and the cumulative entries supplied by all operations.
struct MetadataXmpEditingOptions final {
    uint32_t max_operations          = kMetadataXmpEditingMaxOperations;
    uint32_t max_expanded_operations = kMetadataXmpEditingMaxExpandedOperations;
    uint32_t max_scope_entries       = kMetadataXmpEditingMaxScopeEntries;
    MetadataTypedEditingOptions typed;
};

enum class MetadataXmpEditingStatus : uint8_t {
    Ok,
    NullOutput,
    BaseNotFinalized,
    InvalidOptions,
    TooManyOperations,
    InvalidOperation,
    InvalidScope,
    UnsupportedPath,
    ScopeAlreadyExists,
    ScopeNotFound,
    EmptyEntries,
    UnexpectedEntries,
    InvalidEntry,
    DuplicateEntry,
    UnsupportedSelectedEntry,
    ShapeCollision,
    LimitExceeded,
    TypedEditFailed,
    InternalError,
};

struct MetadataXmpEditingResult final {
    MetadataXmpEditingStatus status = MetadataXmpEditingStatus::Ok;
    uint32_t failed_operation_index = kInvalidMetadataEditingOperationIndex;
    uint32_t operations_applied     = 0U;
    uint32_t expanded_operations    = 0U;
    uint32_t entries_added          = 0U;
    uint32_t entries_updated        = 0U;
    uint32_t entries_removed        = 0U;
    MetadataTypedEditingStatus typed_status = MetadataTypedEditingStatus::Ok;

    bool ok() const noexcept { return status == MetadataXmpEditingStatus::Ok; }
};

/**
 * Applies ordered Add/Replace/Remove operations to supported standard scopes
 * and explicitly declared custom XMP trees. Entries borrow complete exact XMP
 * keys and values. Custom containers use make_xmp_container_entry(); leaves
 * are text. Replacement retains matching entry identity and provenance,
 * tombstones omitted entries, and adds new entries as dirty. Operations observe
 * earlier operations. The complete final tree is validated before publication,
 * including when base and output alias.
 *
 * Standard scopes retain the existing leaf-only model and three-segment limit.
 * Custom scopes retain Struct/Bag/Seq/LangAlt kinds and empty containers under
 * typed.validation.structures limits (at most eight expanded levels). Stored
 * array indices remain stable and may have gaps; portable output compacts RDF
 * item positions. Indices range from 1 through 65535. Custom language selection
 * compares xml:lang case-insensitively. Empty text values are valid.
 */
MetadataXmpEditingResult
edit_metadata_xmp(const MetaStore& base,
                  std::span<const MetadataXmpEditingOperation> operations,
                  MetaStore* output,
                  const MetadataXmpEditingOptions& options
                  = MetadataXmpEditingOptions {}) noexcept;

const char*
metadata_xmp_editing_status_name(MetadataXmpEditingStatus status) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
