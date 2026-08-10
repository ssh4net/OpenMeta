// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <span>
#include <vector>

/**
 * \file meta_edit.h
 * \brief Batch edit operations for MetaStore (append/set/tombstone).
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// The operation kind for a MetaEdit command stream.
enum class EditOpKind : uint8_t {
    AddEntry,
    SetValue,
    Tombstone,
};

/// A single edit operation recorded by MetaEdit.
struct EditOp final {
    EditOpKind kind = EditOpKind::AddEntry;
    EntryId target  = kInvalidEntryId;
    Entry entry;
    MetaValue value;
};

/**
 * \brief A batch of metadata edits to apply to a \ref MetaStore.
 *
 * Designed for multi-threaded production via per-thread edit buffers:
 * build edits without mutating the base store, then apply with \ref commit.
 *
 * New keys/values that require storage use this edit's \ref ByteArena.
 */
class MetaEdit final {
public:
    MetaEdit() = default;

    ByteArena& arena() noexcept;
    const ByteArena& arena() const noexcept;

    /// Reserves space for \p count operations (may allocate).
    void reserve_ops(size_t count);

    /// Appends a new entry.
    void add_entry(const Entry& entry);
    /// Updates the value of an existing entry id.
    void set_value(EntryId target, const MetaValue& value);
    /// Marks an entry as deleted (tombstone).
    void tombstone(EntryId target);

    std::span<const EditOp> ops() const noexcept;

private:
    ByteArena arena_;
    std::vector<EditOp> ops_;
};

/// Applies \p edits to \p base and returns a new \ref MetaStore snapshot.
MetaStore
commit(const MetaStore& base, std::span<const MetaEdit> edits);
/// Compacts a store by removing tombstones and rewriting indices.
MetaStore
compact(const MetaStore& base);

}  // namespace openmeta
OPENMETA_PUBLIC_END
