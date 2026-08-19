// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/interop_export.h"
#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

/**
 * \file interop_import.h
 * \brief Bounded typed import from flat host metadata models.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// How one flat host record identifies its destination metadata key.
enum class FlatHostImportTarget : uint8_t {
    /// Update one exact source entry exported earlier by the host.
    SourceEntry,
    /// Update an existing source entry only when the exported name is unique.
    UniqueName,
    /// Append a new entry using the explicit typed key in the import item.
    ExplicitKey,
};

/// Stable result code for \ref import_flat_host_metadata.
enum class FlatHostImportCode : uint16_t {
    None = 0,
    InvalidArgument,
    SourceNotFinalized,
    LimitExceeded,
    EntryNotFound,
    EntryNotExported,
    NameMismatch,
    AmbiguousName,
    DuplicateTarget,
    InvalidKey,
    InvalidValue,
};

/**
 * \brief Borrowed typed value supplied by a flat host.
 *
 * Scalar values use \ref scalar. Array, byte, and text values use \ref payload;
 * their element type, count, and text encoding remain explicit.
 */
struct FlatHostImportValue final {
    MetaValueKind kind         = MetaValueKind::Empty;
    MetaElementType elem_type  = MetaElementType::U8;
    TextEncoding text_encoding = TextEncoding::Unknown;
    uint32_t count             = 0U;
    MetaValue::Data scalar;
    std::span<const std::byte> payload;
};

/// One ordered typed record imported from a flat host attribute model.
struct FlatHostImportItem final {
    std::string_view name;
    FlatHostImportTarget target = FlatHostImportTarget::UniqueName;
    EntryId source_entry        = kInvalidEntryId;
    MetaKeyView explicit_key;
    FlatHostImportValue value;
};

/// Resource and naming policy for bounded flat host import.
struct FlatHostImportOptions final {
    ExportNamePolicy name_policy = ExportNamePolicy::Spec;
    bool include_makernotes      = false;
    uint32_t max_items           = 200000U;
    uint32_t max_entries         = 200000U;
    uint64_t max_arena_bytes     = 64ULL * 1024ULL * 1024ULL;
    uint64_t max_value_bytes     = 64ULL * 1024ULL * 1024ULL;
    uint32_t max_name_bytes      = 4096U;
};

/// Transactional result for bounded flat host import.
struct FlatHostImportResult final {
    FlatHostImportCode code = FlatHostImportCode::None;
    uint32_t imported       = 0U;
    uint32_t updated        = 0U;
    uint32_t added          = 0U;
    uint32_t failed_item    = 0xFFFFFFFFU;
    uint32_t errors         = 0U;
    std::string message;
    MetaStore store;

    bool ok() const noexcept { return code == FlatHostImportCode::None; }
};

/**
 * \brief Import ordered typed flat-host records into a detached store.
 *
 * The finalized source is never modified. Existing entries may be selected by
 * exact entry id or by a unique FlatHost name under \p options. New entries
 * require an explicit typed key, because FlatHost names are intentionally
 * lossy and cannot safely reconstruct arbitrary metadata namespaces.
 *
 * Calls use only local mutable state and are safe to run concurrently against
 * an immutable finalized source store.
 *
 * \par API Stability
 * Experimental host-facing API.
 */
FlatHostImportResult
import_flat_host_metadata(const MetaStore& source,
                          std::span<const FlatHostImportItem> items,
                          const FlatHostImportOptions& options
                          = FlatHostImportOptions {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
