// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/byte_arena.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/**
 * \file meta_store.h
 * \brief In-memory representation of decoded metadata (keys/values + provenance).
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

class MetaEdit;

using BlockId = uint32_t;
using EntryId = uint32_t;

static constexpr BlockId kInvalidBlockId = 0xffffffffU;
static constexpr EntryId kInvalidEntryId = 0xffffffffU;

/// The wire-format family a value came from (used for round-trip encoding).
enum class WireFamily : uint8_t {
    None,
    Tiff,
    Other,
};

/// Decode-time contextual-name selector for compatibility/display surfaces.
enum class EntryNameContextKind : uint8_t {
    None,
    CasioType2Legacy,
    FujifilmMain1304,
    OlympusFocusInfo1600,
    KodakMain0028,
    MinoltaMainCompat,
    MotorolaMain6420,
    SonyMainCompat,
    SonyTag94060005,
    SigmaMainCompat,
    RicohMainCompat,
    NikonSettingsMain,
    NikonMainZ,
    NikonMainCompactType2,
    NikonFlashInfoGroups,
    NikonFlashInfoLegacy,
    NikonShotInfoD800,
    NikonShotInfoD850,
    NikonShotInfoZ8,
    PentaxMain0062,
    CanonMain0038,
    CanonShotInfo000E,
    CanonCameraSettings0021,
    CanonColorData4PSInfo,
    CanonColorData7PSInfo2,
    CanonColorData400EA,
    CanonColorData400EE,
    CanonColorData402CF,
    CanonColorCalib0038,
    CanonCameraInfo1D0048,
    CanonCameraInfo600D00EA,
    CanonCustomFunctions20103,
    CanonCustomFunctions2010C,
    CanonCustomFunctions20510,
    CanonCustomFunctions20701,
    CanonMain0000,
};

/// Wire-format element type + family (e.g. TIFF type code).
struct WireType final {
    WireFamily family = WireFamily::None;
    uint16_t code     = 0;
};

/// Where an \ref Entry came from inside the original container.
struct Origin final {
    BlockId block           = kInvalidBlockId;
    uint32_t order_in_block = 0;
    WireType wire_type;
    uint32_t wire_count = 0;
    /// Optional wire type name (for formats with named types, e.g. OpenEXR custom attrs).
    ByteSpan wire_type_name;
    EntryNameContextKind name_context_kind = EntryNameContextKind::None;
    uint8_t name_context_variant           = 0;
};

/**
 * \brief A single metadata entry (key/value) with provenance.
 *
 * \note Duplicate keys are allowed and preserved.
 */
struct Entry final {
    MetaKey key;
    MetaValue value;
    Origin origin;
    EntryFlags flags = EntryFlags::None;
};

/// Container-block identity used to associate \ref Entry::origin with a source block.
struct BlockInfo final {
    uint32_t format    = 0;
    uint32_t container = 0;
    uint32_t id        = 0;
};

struct KeySpan final {
    uint32_t start = 0;
    uint32_t count = 0;
    EntryId repr   = kInvalidEntryId;
};

struct BlockSpan final {
    uint32_t start = 0;
    uint32_t count = 0;
};

/**
 * \brief Stores decoded metadata entries grouped into blocks.
 *
 * Lifecycle:
 * - Build phase: call \ref add_block and \ref add_entry (not thread-safe).
 * - Finalize: call \ref finalize to build lookup indices; treat as read-only.
 *
 * Indices:
 * - \ref entries_in_block returns entries sorted by \ref Origin::order_in_block.
 * - \ref find_all returns all matching entries (duplicates preserved).
 */
class MetaStore final {
public:
    MetaStore() = default;

    // Build phase (not thread-safe, not allowed after finalize).
    /// Adds a new block and returns its id.
    BlockId add_block(const BlockInfo& info);
    /// Appends an entry and returns its id.
    EntryId add_entry(const Entry& entry);

    /**
     * Reserves build, lookup-index, and arena capacity during initialization.
     * The call is ignored after finalization and does not change logical size.
     */
    void reserve(uint32_t block_capacity, uint32_t entry_capacity,
                 size_t arena_bytes);

    /// Applies cumulative decode ceilings. Repeated calls retain the lowest
    /// non-zero limits, including across nested decoder calls.
    void constrain_resources(uint32_t max_entries,
                             uint64_t max_arena_bytes) noexcept;
    /// Returns true after a block, entry, or arena allocation was refused.
    bool resource_limit_exceeded() const noexcept;

    ByteArena& arena() noexcept;
    const ByteArena& arena() const noexcept;

    /// Builds lookup indices and marks the store as finalized.
    void finalize();
    /// Rebuilds indices after an edit pipeline (invalidates previous spans).
    void rehash();

    /// Returns whether lookup indices have been finalized.
    bool is_finalized() const noexcept;

    uint32_t block_count() const noexcept;
    const BlockInfo& block_info(BlockId id) const noexcept;

    std::span<const Entry> entries() const noexcept;
    const Entry& entry(EntryId id) const noexcept;

    /// Returns all entries in \p block, ordered by \ref Origin::order_in_block.
    std::span<const EntryId> entries_in_block(BlockId block) const noexcept;
    /// Returns all entry ids matching \p key (excluding tombstones).
    std::span<const EntryId> find_all(const MetaKeyView& key) const noexcept;

private:
    friend MetaStore commit(const MetaStore& base,
                            std::span<const MetaEdit> edits);
    friend MetaStore compact(const MetaStore& base);

    void rebuild_block_index();
    void rebuild_key_index();
    void clear_indices() noexcept;

    ByteArena arena_;
    std::vector<Entry> entries_;
    std::vector<BlockInfo> blocks_;

    std::vector<EntryId> entries_by_block_;
    std::vector<BlockSpan> block_spans_;

    std::vector<EntryId> entries_by_key_;
    std::vector<KeySpan> key_spans_;

    bool finalized_            = false;
    uint64_t max_entries_      = UINT32_MAX;
    bool entry_limit_exceeded_ = false;
};

}  // namespace openmeta
OPENMETA_PUBLIC_END
