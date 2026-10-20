// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_edit.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>

namespace openmeta {

[[noreturn]] static void
fuzz_trap() noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#else
    std::abort();
#endif
}


static uint32_t
read_u32(std::span<const uint8_t> bytes, size_t offset) noexcept
{
    if (offset + 4U > bytes.size()) {
        return 0;
    }
    uint32_t v = 0;
    v |= static_cast<uint32_t>(bytes[offset + 0U]) << 0U;
    v |= static_cast<uint32_t>(bytes[offset + 1U]) << 8U;
    v |= static_cast<uint32_t>(bytes[offset + 2U]) << 16U;
    v |= static_cast<uint32_t>(bytes[offset + 3U]) << 24U;
    return v;
}


static uint16_t
read_u16(std::span<const uint8_t> bytes, size_t offset) noexcept
{
    if (offset + 2U > bytes.size()) {
        return 0;
    }
    uint16_t v = 0;
    v |= static_cast<uint16_t>(bytes[offset + 0U]) << 0U;
    v |= static_cast<uint16_t>(bytes[offset + 1U]) << 8U;
    return v;
}


static MetaKey
make_exif_key_with_ifd(ByteSpan ifd, uint16_t tag) noexcept
{
    MetaKey key;
    key.kind              = MetaKeyKind::ExifTag;
    key.data.exif_tag.ifd = ifd;
    key.data.exif_tag.tag = tag;
    return key;
}


static void
verify_block_order(const MetaStore& store, BlockId block) noexcept
{
    const std::span<const EntryId> ids = store.entries_in_block(block);
    uint32_t last                      = 0;
    bool first                         = true;
    for (size_t i = 0; i < ids.size(); ++i) {
        const EntryId id = ids[i];
        const Entry& e   = store.entry(id);
        if (any(e.flags, EntryFlags::Deleted)) {
            fuzz_trap();
        }
        if (e.origin.block != block) {
            fuzz_trap();
        }
        if (!first && e.origin.order_in_block < last) {
            fuzz_trap();
        }
        last  = e.origin.order_in_block;
        first = false;
    }
}


static void
verify_lookup(const MetaStore& store,
              std::span<const uint16_t> sample_tags) noexcept
{
    MetaKeyView key;
    key.kind              = MetaKeyKind::ExifTag;
    key.data.exif_tag.ifd = "ifd0Id";

    for (size_t i = 0; i < sample_tags.size(); ++i) {
        key.data.exif_tag.tag              = sample_tags[i];
        const std::span<const EntryId> ids = store.find_all(key);
        for (size_t j = 0; j < ids.size(); ++j) {
            const EntryId id = ids[j];
            const Entry& e   = store.entry(id);
            if (any(e.flags, EntryFlags::Deleted)) {
                fuzz_trap();
            }
            if (e.key.kind != MetaKeyKind::ExifTag) {
                fuzz_trap();
            }
            if (e.key.data.exif_tag.tag != sample_tags[i]) {
                fuzz_trap();
            }
        }
    }
}

}  // namespace openmeta

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace openmeta;
    const std::span<const uint8_t> bytes(data, size);

    MetaStore store;
    const BlockId block      = store.add_block(BlockInfo {});
    const ByteSpan ifd_store = store.arena().append_string("ifd0Id");

    size_t cursor             = 0;
    const uint32_t base_count = (size >= 1U)
                                    ? static_cast<uint32_t>(bytes[0] & 31U)
                                    : 0U;
    cursor += (size >= 1U) ? 1U : 0U;

    uint16_t sample_tags_buf[8] = {};
    uint32_t sample_count       = 0;

    for (uint32_t i = 0; i < base_count; ++i) {
        if (cursor + 6U > bytes.size()) {
            break;
        }
        const uint16_t tag   = read_u16(bytes, cursor + 0U);
        const uint32_t value = read_u32(bytes, cursor + 2U);
        cursor += 6U;

        if (sample_count < 8U) {
            sample_tags_buf[sample_count] = tag;
            ++sample_count;
        }

        Entry e;
        e.key                   = make_exif_key_with_ifd(ifd_store, tag);
        e.value                 = make_u32(value);
        e.origin.block          = block;
        e.origin.order_in_block = static_cast<uint32_t>(i);
        store.add_entry(e);
    }

    store.finalize();

    if (size >= 3U && (bytes[0] & 0x80U) != 0U) {
        const uint32_t entries = static_cast<uint32_t>(store.entries().size());
        const uint32_t entry_limit = entries + (bytes[1] & 3U);
        store.constrain_resources(entry_limit == 0U ? 1U : entry_limit,
                                   store.arena().bytes().size() + (bytes[2] & 31U));
    }

    MetaEdit edit;
    const ByteSpan ifd_edit = edit.arena().append_string("ifd0Id");
    edit.reserve_ops(32);

    const uint32_t edit_ops = static_cast<uint32_t>(
        (bytes.size() > cursor) ? (bytes.size() - cursor) / 7U : 0U);
    const uint32_t edit_count = (edit_ops > 32U) ? 32U : edit_ops;
    for (uint32_t i = 0; i < edit_count; ++i) {
        const uint8_t op     = bytes[cursor + 0U] % 3U;
        const uint16_t tag   = read_u16(bytes, cursor + 1U);
        const uint32_t value = read_u32(bytes, cursor + 3U);
        cursor += 7U;

        if (op == 0U) {
            if (!store.entries().empty()) {
                const EntryId target = static_cast<EntryId>(
                    value % store.entries().size());
                edit.set_value(target, make_u32(value));
            }
            continue;
        }

        if (op == 1U) {
            if (!store.entries().empty()) {
                const EntryId target = static_cast<EntryId>(
                    value % store.entries().size());
                edit.tombstone(target);
            }
            continue;
        }

        Entry add;
        add.key                   = make_exif_key_with_ifd(ifd_edit, tag);
        add.value                 = make_u32(value);
        add.origin.block          = block;
        add.origin.order_in_block = static_cast<uint32_t>(base_count + i);
        edit.add_entry(add);
    }

    const std::span<const uint16_t> sample_tags(sample_tags_buf, sample_count);

    MetaStore updated = commit(store, std::span<const MetaEdit>(&edit, 1));
    if (updated.resource_limit_exceeded()) {
        if (updated.entries().size() != store.entries().size()
            || updated.arena().bytes().size() != store.arena().bytes().size()) {
            fuzz_trap();
        }
        if (!store.arena().bytes().empty()
            && std::memcmp(updated.arena().bytes().data(),
                           store.arena().bytes().data(),
                           store.arena().bytes().size()) != 0) {
            fuzz_trap();
        }
    }
    verify_block_order(updated, block);
    verify_lookup(updated, sample_tags);

    MetaStore compacted = compact(updated);
    verify_block_order(compacted, block);
    verify_lookup(compacted, sample_tags);

    return 0;
}
