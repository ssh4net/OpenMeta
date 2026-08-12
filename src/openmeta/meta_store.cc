// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_store.h"

#include <algorithm>

namespace openmeta {
namespace {

    static const BlockInfo kEmptyBlockInfo {};
    static const Entry kEmptyEntry {};

    static std::span<const EntryId>
    make_entry_id_span(const std::vector<EntryId>& ids, uint32_t start,
                       uint32_t count) noexcept
    {
        const size_t ids_size = ids.size();
        const size_t first    = static_cast<size_t>(start);
        const size_t n        = static_cast<size_t>(count);
        if (first > ids_size || n > (ids_size - first)) {
            return std::span<const EntryId>();
        }
        return std::span<const EntryId>(ids.data() + first, n);
    }

}  // namespace

ByteArena&
MetaStore::arena() noexcept
{
    return arena_;
}


const ByteArena&
MetaStore::arena() const noexcept
{
    return arena_;
}


BlockId
MetaStore::add_block(const BlockInfo& info)
{
    if (finalized_) {
        return kInvalidBlockId;
    }
    if (blocks_.size() >= max_entries_ || arena_.limit_exceeded()) {
        entry_limit_exceeded_ = true;
        return kInvalidBlockId;
    }
    const BlockId id = static_cast<BlockId>(blocks_.size());
    blocks_.push_back(info);
    return id;
}


EntryId
MetaStore::add_entry(const Entry& entry)
{
    if (finalized_) {
        return kInvalidEntryId;
    }
    if (entries_.size() >= max_entries_ || arena_.limit_exceeded()) {
        entry_limit_exceeded_ = true;
        return kInvalidEntryId;
    }
    const EntryId id = static_cast<EntryId>(entries_.size());
    entries_.push_back(entry);
    return id;
}


void
MetaStore::constrain_resources(uint32_t max_entries,
                               uint64_t max_arena_bytes) noexcept
{
    if (max_entries != 0U && max_entries < max_entries_) {
        max_entries_ = max_entries;
    }
    arena_.constrain_max_size(max_arena_bytes);
    if (entries_.size() > max_entries_ || blocks_.size() > max_entries_) {
        entry_limit_exceeded_ = true;
    }
}


bool
MetaStore::resource_limit_exceeded() const noexcept
{
    return entry_limit_exceeded_ || arena_.limit_exceeded();
}


void
MetaStore::clear_indices() noexcept
{
    entries_by_block_.clear();
    block_spans_.clear();
    entries_by_key_.clear();
    key_spans_.clear();
}


void
MetaStore::finalize()
{
    clear_indices();
    rebuild_block_index();
    rebuild_key_index();
    finalized_ = true;
}


void
MetaStore::rehash()
{
    if (!finalized_) {
        finalize();
        return;
    }
    clear_indices();
    rebuild_block_index();
    rebuild_key_index();
}


bool
MetaStore::is_finalized() const noexcept
{
    return finalized_;
}


uint32_t
MetaStore::block_count() const noexcept
{
    return static_cast<uint32_t>(blocks_.size());
}


const BlockInfo&
MetaStore::block_info(BlockId id) const noexcept
{
    if (static_cast<size_t>(id) >= blocks_.size()) {
        return kEmptyBlockInfo;
    }
    return blocks_[id];
}


std::span<const Entry>
MetaStore::entries() const noexcept
{
    return std::span<const Entry>(entries_.data(), entries_.size());
}


const Entry&
MetaStore::entry(EntryId id) const noexcept
{
    if (static_cast<size_t>(id) >= entries_.size()) {
        return kEmptyEntry;
    }
    return entries_[id];
}


std::span<const EntryId>
MetaStore::entries_in_block(BlockId block) const noexcept
{
    if (block >= block_spans_.size()) {
        return std::span<const EntryId>();
    }
    const BlockSpan span = block_spans_[block];
    return make_entry_id_span(entries_by_block_, span.start, span.count);
}


std::span<const EntryId>
MetaStore::find_all(const MetaKeyView& key) const noexcept
{
    if (!finalized_ || key_spans_.empty()) {
        return std::span<const EntryId>();
    }

    size_t lo = 0;
    size_t hi = key_spans_.size();
    while (lo < hi) {
        const size_t mid      = lo + ((hi - lo) / 2U);
        const KeySpan span    = key_spans_[mid];
        const EntryId repr_id = span.repr;
        if (static_cast<size_t>(repr_id) >= entries_.size()) {
            return std::span<const EntryId>();
        }
        const int cmp = compare_key_view(arena_, key, entries_[repr_id].key);
        if (cmp == 0) {
            return make_entry_id_span(entries_by_key_, span.start, span.count);
        }
        if (cmp < 0) {
            hi = mid;
        } else {
            lo = mid + 1U;
        }
    }

    return std::span<const EntryId>();
}


struct EntryIdLessByBlock final {
    const MetaStore* store = nullptr;

    bool operator()(EntryId a, EntryId b) const noexcept
    {
        const Entry& ea = store->entry(a);
        const Entry& eb = store->entry(b);
        if (ea.origin.block != eb.origin.block) {
            return ea.origin.block < eb.origin.block;
        }
        if (ea.origin.order_in_block != eb.origin.order_in_block) {
            return ea.origin.order_in_block < eb.origin.order_in_block;
        }
        return a < b;
    }
};

struct EntryIdLessByKey final {
    const MetaStore* store = nullptr;

    bool operator()(EntryId a, EntryId b) const noexcept
    {
        const int cmp = compare_key(store->arena(), store->entry(a).key,
                                    store->entry(b).key);
        if (cmp != 0) {
            return cmp < 0;
        }
        return a < b;
    }
};

void
MetaStore::rebuild_block_index()
{
    const BlockId block_count = static_cast<BlockId>(blocks_.size());
    block_spans_.assign(block_count, BlockSpan { 0, 0 });

    entries_by_block_.reserve(entries_.size());
    for (EntryId id = 0; id < static_cast<EntryId>(entries_.size()); ++id) {
        if (any(entries_[id].flags, EntryFlags::Deleted)) {
            continue;
        }
        entries_by_block_.push_back(id);
    }

    EntryIdLessByBlock less;
    less.store = this;
    std::stable_sort(entries_by_block_.begin(), entries_by_block_.end(), less);

    for (uint32_t i = 0; i < static_cast<uint32_t>(entries_by_block_.size());
         ++i) {
        const EntryId id    = entries_by_block_[i];
        const BlockId block = entries_[id].origin.block;
        if (block >= block_count) {
            continue;
        }
        BlockSpan& span = block_spans_[block];
        if (span.count == 0) {
            span.start = i;
        }
        ++span.count;
    }

    uint32_t next_start = static_cast<uint32_t>(entries_by_block_.size());
    for (size_t b = block_spans_.size(); b-- > 0;) {
        BlockSpan& span = block_spans_[b];
        if (span.count == 0) {
            span.start = next_start;
            continue;
        }
        next_start = span.start;
    }
}


void
MetaStore::rebuild_key_index()
{
    entries_by_key_.reserve(entries_.size());
    for (EntryId id = 0; id < static_cast<EntryId>(entries_.size()); ++id) {
        if (any(entries_[id].flags, EntryFlags::Deleted)) {
            continue;
        }
        entries_by_key_.push_back(id);
    }

    EntryIdLessByKey less;
    less.store = this;
    std::stable_sort(entries_by_key_.begin(), entries_by_key_.end(), less);

    key_spans_.clear();
    if (entries_by_key_.empty()) {
        return;
    }

    uint32_t run_start = 0;
    EntryId run_repr   = entries_by_key_[0];
    for (uint32_t i = 1; i < static_cast<uint32_t>(entries_by_key_.size());
         ++i) {
        const EntryId current = entries_by_key_[i];
        const int cmp         = compare_key(arena_, entries_[run_repr].key,
                                            entries_[current].key);
        if (cmp != 0) {
            key_spans_.push_back(
                KeySpan { run_start, i - run_start, run_repr });
            run_start = i;
            run_repr  = current;
        }
    }
    const uint32_t end = static_cast<uint32_t>(entries_by_key_.size());
    key_spans_.push_back(KeySpan { run_start, end - run_start, run_repr });
}

}  // namespace openmeta
