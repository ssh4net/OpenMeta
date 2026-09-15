// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_creation.h"

#include <cstdint>
#include <span>

/**
 * \file metadata_editing.h
 * \brief Bounded transactional editing of canonical logical metadata fields.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable high-level metadata editing contract version.
inline constexpr uint32_t kMetadataEditingContractVersion = 1U;

inline constexpr uint32_t kMetadataEditingMaxOperations            = 1024U;
inline constexpr uint32_t kMetadataEditingMaxTextBytesPerOperation = 1024U
                                                                     * 1024U;
inline constexpr uint64_t kMetadataEditingMaxTotalTextBytes = 8ULL * 1024ULL
                                                              * 1024ULL;
inline constexpr uint32_t kMetadataEditingAllOccurrences        = 0xffffffffU;
inline constexpr uint32_t kInvalidMetadataEditingOperationIndex = 0xffffffffU;

enum class MetadataEditingOperationKind : uint8_t {
    Add,
    Set,
    Remove,
};

/**
 * \brief One logical edit applied in request order.
 *
 * Add ignores occurrence and appends a repeated field or creates an absent
 * singleton. Set replaces one zero-based active occurrence. Remove deletes one
 * occurrence, or all occurrences when occurrence is
 * \ref kMetadataEditingAllOccurrences.
 */
struct MetadataEditingOperation final {
    MetadataEditingOperationKind kind = MetadataEditingOperationKind::Add;
    MetadataCreationField field;
    uint32_t occurrence = 0U;
};

MetadataEditingOperation
make_metadata_edit_add(const MetadataCreationField& field) noexcept;

MetadataEditingOperation
make_metadata_edit_set(const MetadataCreationField& field,
                       uint32_t occurrence = 0U) noexcept;

MetadataEditingOperation
make_metadata_edit_remove(MetadataCreationFieldKind kind,
                          uint32_t occurrence = 0U) noexcept;

MetadataEditingOperation
make_metadata_edit_remove_all(MetadataCreationFieldKind kind) noexcept;

struct MetadataEditingLimits final {
    uint32_t max_operations = kMetadataEditingMaxOperations;
    uint32_t max_text_bytes_per_operation
        = kMetadataEditingMaxTextBytesPerOperation;
    uint64_t max_total_text_bytes = kMetadataEditingMaxTotalTextBytes;
};

struct MetadataEditingRequest final {
    std::span<const MetadataEditingOperation> operations;
    MetadataEditingLimits limits;
};

enum class MetadataEditingStatus : uint8_t {
    Ok,
    NullOutput,
    BaseNotFinalized,
    InvalidLimits,
    TooManyOperations,
    InvalidOperationKind,
    InvalidOccurrence,
    WrongValueKind,
    EmptyText,
    TextTooLong,
    TotalTextTooLong,
    InvalidText,
    InvalidValue,
    SingletonAlreadyExists,
    TargetNotFound,
    AmbiguousTarget,
    EntryLimitExceeded,
    InternalError,
};

struct MetadataEditingResult final {
    MetadataEditingStatus status    = MetadataEditingStatus::Ok;
    uint32_t failed_operation_index = kInvalidMetadataEditingOperationIndex;
    uint32_t operation_count        = 0U;
    uint32_t operations_applied     = 0U;
    uint32_t entries_added          = 0U;
    uint32_t entries_updated        = 0U;
    uint32_t entries_removed        = 0U;
};

/**
 * \brief Applies a validated logical edit transaction to a finalized store.
 *
 * The output is replaced only after every operation succeeds. Set preserves
 * the existing key, origin, block, wire provenance, and flags while replacing
 * the value and adding \ref EntryFlags::Dirty. Remove preserves the entry as a
 * dirty tombstone. Add emits a new dirty canonical portable-XMP entry.
 *
 * Operations observe earlier operations in the same request. This function
 * keeps no global state and is safe for concurrent calls that use distinct
 * output stores.
 */
MetadataEditingResult
edit_metadata(const MetaStore& base, const MetadataEditingRequest& request,
              MetaStore* out_store);

const char*
metadata_editing_operation_kind_name(MetadataEditingOperationKind kind) noexcept;

const char*
metadata_editing_status_name(MetadataEditingStatus status) noexcept;

inline constexpr uint32_t kMetadataTypedEditingContractVersion  = 1U;
inline constexpr uint32_t kMetadataTypedEditingUniqueOccurrence = 0xfffffffeU;
inline constexpr uint32_t kMetadataTypedEditingMaxOperations    = 4096U;

enum class MetadataTypedEditingAddPolicy : uint8_t {
    FailIfPresent,
    Append,
};

/// Exact keys are matched without namespace aliasing or native translation.
/// Set/Remove default to one unambiguous active entry. Remove also accepts
/// kMetadataEditingAllOccurrences. Numeric occurrences follow request order.
struct MetadataTypedEditingOperation final {
    MetadataEditingOperationKind kind = MetadataEditingOperationKind::Add;
    MetadataAuthoringEntry entry;
    uint32_t occurrence = kMetadataTypedEditingUniqueOccurrence;
};

struct MetadataTypedEditingOptions final {
    MetadataTypedEditingAddPolicy add_policy
        = MetadataTypedEditingAddPolicy::FailIfPresent;
    uint32_t max_operations    = kMetadataTypedEditingMaxOperations;
    uint32_t max_entries       = 200000U;
    uint32_t max_key_bytes     = 4096U;
    uint64_t max_arena_bytes   = 64ULL * 1024ULL * 1024ULL;
    uint64_t max_value_bytes   = 64ULL * 1024ULL * 1024ULL;
    uint64_t max_request_bytes = 8ULL * 1024ULL * 1024ULL;
    MetadataValidationOptions validation;
};

enum class MetadataTypedEditingStatus : uint8_t {
    Ok,
    NullOutput,
    BaseNotFinalized,
    InvalidOptions,
    TooManyOperations,
    UnsupportedKeyKind,
    InvalidKey,
    InvalidValue,
    InvalidOperation,
    InvalidOccurrence,
    TargetNotFound,
    AmbiguousTarget,
    TargetAlreadyExists,
    LimitExceeded,
    ValidationFailed,
};

struct MetadataTypedEditingResult final {
    MetadataTypedEditingStatus status = MetadataTypedEditingStatus::Ok;
    uint32_t failed_operation_index   = kInvalidMetadataEditingOperationIndex;
    EntryId failed_entry              = kInvalidEntryId;
    MetadataValidationIssueCode validation_issue
        = MetadataValidationIssueCode::None;
    uint32_t operations_applied = 0U;
    uint32_t entries_added      = 0U;
    uint32_t entries_updated    = 0U;
    uint32_t entries_removed    = 0U;

    bool ok() const noexcept
    {
        return status == MetadataTypedEditingStatus::Ok;
    }
};

/**
 * Applies ordered EXIF/TIFF, IPTC and XMP edits to a detached candidate.
 * Every supplied value and the complete result are validated. Set retains
 * entry identity, source block/order and flags, but replaces wire hints with
 * those supplied for the new value; default hints request inferred encoding.
 * Add creates an entry without source-block provenance. Remove ignores value
 * and wire hints, retains a dirty tombstone, and fails for a missing target.
 * Output, including an aliased base/output, is replaced only on success.
 * This is a bounded preparation operation that allocates, not patch replay.
 */
MetadataTypedEditingResult
edit_metadata_typed(const MetaStore& base,
                    std::span<const MetadataTypedEditingOperation> operations,
                    MetaStore* output,
                    const MetadataTypedEditingOptions& options
                    = MetadataTypedEditingOptions {}) noexcept;

const char*
metadata_typed_editing_status_name(MetadataTypedEditingStatus status) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
