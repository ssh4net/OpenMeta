// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_edit.h"

#include <gtest/gtest.h>

namespace openmeta {

static MetaStore
make_finalized_empty_store(uint32_t max_entries, uint64_t max_arena_bytes)
{
    MetaStore store;
    store.constrain_resources(max_entries, max_arena_bytes);
    store.add_block(BlockInfo {});
    store.finalize();
    return store;
}


TEST(MetaStoreTest, CumulativeResourceConstraintsCannotBeWidened)
{
    MetaStore store;
    store.constrain_resources(2U, 12U);
    store.constrain_resources(100U, 1024U);

    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry first;
    first.key = make_exif_tag_key(store.arena(), "a", 1U);
    ASSERT_NE(store.add_entry(first), kInvalidEntryId);

    Entry second;
    second.key = make_exif_tag_key(store.arena(), "b", 2U);
    ASSERT_NE(store.add_entry(second), kInvalidEntryId);

    Entry third;
    third.key = make_exif_tag_key(store.arena(), "c", 3U);
    EXPECT_EQ(store.add_entry(third), kInvalidEntryId);
    EXPECT_TRUE(store.resource_limit_exceeded());
    EXPECT_LE(store.arena().bytes().size(), 12U);
}

TEST(MetaStoreTest, SupportsDuplicateKeys)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry e1;
    e1.key          = make_exif_tag_key(store.arena(), "ifd0Id", 0x010f);
    e1.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    e1.origin.block = block;
    e1.origin.order_in_block = 0;
    e1.origin.wire_type      = WireType { WireFamily::Tiff, 2 };
    e1.origin.wire_count     = 6;
    store.add_entry(e1);

    Entry e2;
    e2.key          = make_exif_tag_key(store.arena(), "ifd0Id", 0x010f);
    e2.value        = make_text(store.arena(), "CANON", TextEncoding::Ascii);
    e2.origin.block = block;
    e2.origin.order_in_block = 1;
    e2.origin.wire_type      = WireType { WireFamily::Tiff, 2 };
    e2.origin.wire_count     = 6;
    store.add_entry(e2);

    store.finalize();

    MetaKeyView key;
    key.kind              = MetaKeyKind::ExifTag;
    key.data.exif_tag.ifd = "ifd0Id";
    key.data.exif_tag.tag = 0x010f;

    const std::span<const EntryId> ids = store.find_all(key);
    ASSERT_EQ(ids.size(), 2U);
    EXPECT_EQ(ids[0], 0U);
    EXPECT_EQ(ids[1], 1U);
}


TEST(MetaStoreTest, SupportsExrAttributeLookupByPartAndName)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry p0_owner;
    p0_owner.key   = make_exr_attribute_key(store.arena(), 0, "owner");
    p0_owner.value = make_text(store.arena(), "showA", TextEncoding::Utf8);
    p0_owner.origin.block          = block;
    p0_owner.origin.order_in_block = 0;
    store.add_entry(p0_owner);

    Entry p1_owner;
    p1_owner.key   = make_exr_attribute_key(store.arena(), 1, "owner");
    p1_owner.value = make_text(store.arena(), "showB", TextEncoding::Utf8);
    p1_owner.origin.block          = block;
    p1_owner.origin.order_in_block = 1;
    store.add_entry(p1_owner);

    Entry p0_owner_dup;
    p0_owner_dup.key   = make_exr_attribute_key(store.arena(), 0, "owner");
    p0_owner_dup.value = make_text(store.arena(), "showA-alt",
                                   TextEncoding::Utf8);
    p0_owner_dup.origin.block          = block;
    p0_owner_dup.origin.order_in_block = 2;
    store.add_entry(p0_owner_dup);

    store.finalize();

    MetaKeyView key_part0;
    key_part0.kind                           = MetaKeyKind::ExrAttribute;
    key_part0.data.exr_attribute.part_index  = 0;
    key_part0.data.exr_attribute.name        = "owner";
    const std::span<const EntryId> ids_part0 = store.find_all(key_part0);
    ASSERT_EQ(ids_part0.size(), 2U);
    EXPECT_EQ(ids_part0[0], 0U);
    EXPECT_EQ(ids_part0[1], 2U);

    MetaKeyView key_part1;
    key_part1.kind                           = MetaKeyKind::ExrAttribute;
    key_part1.data.exr_attribute.part_index  = 1;
    key_part1.data.exr_attribute.name        = "owner";
    const std::span<const EntryId> ids_part1 = store.find_all(key_part1);
    ASSERT_EQ(ids_part1.size(), 1U);
    EXPECT_EQ(ids_part1[0], 1U);
}


TEST(MetaStoreTest, TombstonesHideEntriesFromLookup)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry e;
    e.key          = make_exif_tag_key(store.arena(), "ifd0Id", 0x010f);
    e.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    e.origin.block = block;
    store.add_entry(e);
    store.finalize();

    MetaEdit edit;
    edit.tombstone(0);

    MetaStore updated = commit(store, std::span<const MetaEdit>(&edit, 1));
    EXPECT_TRUE(any(updated.entry(0).flags, EntryFlags::Deleted));
    EXPECT_TRUE(any(updated.entry(0).flags, EntryFlags::Dirty));

    MetaKeyView key;
    key.kind              = MetaKeyKind::ExifTag;
    key.data.exif_tag.ifd = "ifd0Id";
    key.data.exif_tag.tag = 0x010f;

    EXPECT_TRUE(updated.find_all(key).empty());
}


TEST(MetaStoreTest, CommitAppendsNewEntry)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry e;
    e.key          = make_exif_tag_key(store.arena(), "ifd0Id", 0x010f);
    e.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    e.origin.block = block;
    e.origin.order_in_block = 10;
    store.add_entry(e);
    store.finalize();

    MetaEdit edit;
    Entry added;
    added.key          = make_exif_tag_key(edit.arena(), "ifd0Id", 0x0110);
    added.value        = make_text(edit.arena(), "EOS", TextEncoding::Ascii);
    added.origin.block = block;
    added.origin.order_in_block = 5;
    edit.add_entry(added);

    MetaStore updated = commit(store, std::span<const MetaEdit>(&edit, 1));
    ASSERT_EQ(updated.entries().size(), 2U);

    MetaKeyView key_model;
    key_model.kind              = MetaKeyKind::ExifTag;
    key_model.data.exif_tag.ifd = "ifd0Id";
    key_model.data.exif_tag.tag = 0x0110;
    ASSERT_EQ(updated.find_all(key_model).size(), 1U);

    const std::span<const EntryId> ids = updated.entries_in_block(block);
    ASSERT_EQ(ids.size(), 2U);
    EXPECT_EQ(ids[0], 1U);
    EXPECT_EQ(ids[1], 0U);
}


TEST(MetaStoreTest, CommitRollsBackSequentialEditsAtInheritedEntryLimit)
{
    MetaStore store;
    store.constrain_resources(1U, 1024U);
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry original;
    original.key   = make_exif_tag_key(store.arena(), "ifd0Id", 0x010fU);
    original.value = make_text(store.arena(), "base", TextEncoding::Ascii);
    ASSERT_NE(store.add_entry(original), kInvalidEntryId);
    store.finalize();

    MetaEdit set_edit;
    const MetaValue replacement
        = make_text(set_edit.arena(), "changed", TextEncoding::Ascii);
    set_edit.set_value(0U, replacement);

    MetaEdit add_edit;
    Entry added;
    added.key = make_exif_tag_key(add_edit.arena(), "ifd0Id", 0x0110U);
    added.value = make_text(add_edit.arena(), "new", TextEncoding::Ascii);
    add_edit.add_entry(added);

    const MetaEdit edits[] = { set_edit, add_edit };
    const MetaStore updated
        = commit(store, std::span<const MetaEdit>(edits, 2U));

    ASSERT_EQ(updated.entries().size(), 1U);
    EXPECT_TRUE(updated.is_finalized());
    EXPECT_TRUE(updated.resource_limit_exceeded());
    EXPECT_FALSE(store.resource_limit_exceeded());
    EXPECT_EQ(updated.entry(0U).value.data.span.offset,
              store.entry(0U).value.data.span.offset);
    EXPECT_EQ(updated.entry(0U).value.data.span.size,
              store.entry(0U).value.data.span.size);

    MetaKeyView key;
    key.kind              = MetaKeyKind::ExifTag;
    key.data.exif_tag.ifd = "ifd0Id";
    key.data.exif_tag.tag = 0x010fU;
    EXPECT_EQ(updated.find_all(key).size(), 1U);
}


TEST(MetaStoreTest, CommitAllowsAddAtInheritedEntryLimit)
{
    const MetaStore base = make_finalized_empty_store(1U, 1U);
    MetaEdit edit;

    Entry added;
    added.key.kind = MetaKeyKind::Comment;
    added.value   = make_u32(7U);
    edit.add_entry(added);

    const MetaStore updated = commit(base, std::span<const MetaEdit>(&edit, 1U));
    ASSERT_EQ(updated.entries().size(), 1U);
    EXPECT_FALSE(updated.resource_limit_exceeded());
    EXPECT_TRUE(updated.is_finalized());
}


TEST(MetaStoreTest, CommitRollsBackAddKeyValueAndOriginCopyExhaustion)
{
    {
        const MetaStore base = make_finalized_empty_store(2U, 1U);
        MetaEdit edit;
        Entry added;
        added.key   = make_exif_tag_key(edit.arena(), "ab", 1U);
        added.value = make_u32(1U);
        edit.add_entry(added);

        const MetaStore updated
            = commit(base, std::span<const MetaEdit>(&edit, 1U));
        EXPECT_TRUE(updated.resource_limit_exceeded());
        EXPECT_TRUE(updated.entries().empty());
        EXPECT_FALSE(base.resource_limit_exceeded());
    }
    {
        const MetaStore base = make_finalized_empty_store(2U, 1U);
        MetaEdit edit;
        Entry added;
        added.key   = MetaKey {};
        added.value = make_text(edit.arena(), "ab", TextEncoding::Utf8);
        edit.add_entry(added);

        const MetaStore updated
            = commit(base, std::span<const MetaEdit>(&edit, 1U));
        EXPECT_TRUE(updated.resource_limit_exceeded());
        EXPECT_TRUE(updated.entries().empty());
        EXPECT_FALSE(base.resource_limit_exceeded());
    }
    {
        const MetaStore base = make_finalized_empty_store(2U, 1U);
        MetaEdit edit;
        Entry added;
        added.key.kind           = MetaKeyKind::Comment;
        added.value              = make_u32(1U);
        added.origin.wire_type_name = edit.arena().append_string("ab");
        edit.add_entry(added);

        const MetaStore updated
            = commit(base, std::span<const MetaEdit>(&edit, 1U));
        EXPECT_TRUE(updated.resource_limit_exceeded());
        EXPECT_TRUE(updated.entries().empty());
        EXPECT_FALSE(base.resource_limit_exceeded());
    }
}


TEST(MetaStoreTest, CommitAcceptsValidEmptyValueSpans)
{
    const MetaStore base = make_finalized_empty_store(1U, 1U);
    MetaEdit edit;

    Entry added;
    added.key.kind = MetaKeyKind::Comment;
    added.value   = make_text(edit.arena(), "", TextEncoding::Utf8);
    edit.add_entry(added);

    const MetaStore updated = commit(base, std::span<const MetaEdit>(&edit, 1U));
    ASSERT_EQ(updated.entries().size(), 1U);
    EXPECT_EQ(updated.entry(0U).value.kind, MetaValueKind::Text);
    EXPECT_EQ(updated.entry(0U).value.data.span.size, 0U);
    EXPECT_FALSE(updated.resource_limit_exceeded());
}


TEST(MetaStoreTest, CommitCanonicalizesEmptyOriginNamesAcrossEdits)
{
    MetaStore base;
    base.finalize();

    MetaEdit first_edit;
    Entry first;
    first.key   = make_exif_tag_key(first_edit.arena(), "ifd0", 1U);
    first.value = make_text(first_edit.arena(), "value", TextEncoding::Utf8);
    first_edit.add_entry(first);

    const MetaStore first_result
        = commit(base, std::span<const MetaEdit>(&first_edit, 1U));
    ASSERT_EQ(first_result.entries().size(), 1U);
    EXPECT_EQ(first_result.entry(0U).origin.wire_type_name.offset, 0U);
    EXPECT_EQ(first_result.entry(0U).origin.wire_type_name.size, 0U);

    MetaEdit second_edit;
    Entry second;
    second.key.kind              = MetaKeyKind::Comment;
    second.value                 = make_u32(2U);
    second.origin                = first_result.entry(0U).origin;
    second_edit.add_entry(second);
    ASSERT_TRUE(second_edit.arena().bytes().empty());

    const MetaStore second_result
        = commit(first_result, std::span<const MetaEdit>(&second_edit, 1U));
    ASSERT_EQ(second_result.entries().size(), 2U);
    EXPECT_FALSE(second_result.resource_limit_exceeded());
    EXPECT_EQ(second_result.entry(0U).origin.wire_type_name.offset, 0U);
    EXPECT_EQ(second_result.entry(0U).origin.wire_type_name.size, 0U);
    EXPECT_EQ(second_result.entry(1U).origin.wire_type_name.offset, 0U);
    EXPECT_EQ(second_result.entry(1U).origin.wire_type_name.size, 0U);
}


TEST(MetaStoreTest, CommitRollsBackSetCopyFailureAndRejectsMalformedSpans)
{
    {
        MetaStore base;
        base.constrain_resources(1U, 2U);
        base.add_block(BlockInfo {});
        Entry original;
        original.key.kind = MetaKeyKind::Comment;
        original.value   = make_text(base.arena(), "a", TextEncoding::Utf8);
        ASSERT_NE(base.add_entry(original), kInvalidEntryId);
        base.finalize();

        MetaEdit edit;
        const MetaValue replacement
            = make_text(edit.arena(), "bc", TextEncoding::Utf8);
        edit.set_value(0U, replacement);

        const MetaStore updated
            = commit(base, std::span<const MetaEdit>(&edit, 1U));
        ASSERT_EQ(updated.entries().size(), 1U);
        EXPECT_TRUE(updated.resource_limit_exceeded());
        EXPECT_FALSE(base.resource_limit_exceeded());
        EXPECT_EQ(updated.entry(0U).value.data.span.offset,
                  base.entry(0U).value.data.span.offset);
        EXPECT_EQ(updated.entry(0U).value.data.span.size,
                  base.entry(0U).value.data.span.size);
    }
    {
        const MetaStore base = make_finalized_empty_store(2U, 8U);
        MetaEdit edit;
        Entry malformed;
        malformed.key.kind         = MetaKeyKind::Comment;
        malformed.value.kind       = MetaValueKind::Text;
        malformed.value.data.span  = ByteSpan { UINT32_MAX, 1U };
        edit.add_entry(malformed);

        const MetaStore updated
            = commit(base, std::span<const MetaEdit>(&edit, 1U));
        EXPECT_TRUE(updated.resource_limit_exceeded());
        EXPECT_TRUE(updated.entries().empty());
    }
}


TEST(MetaStoreTest, CommitRejectsFailedBaseAndEditArenas)
{
    const MetaStore healthy_base = make_finalized_empty_store(1U, 4U);
    MetaEdit failed_edit;
    failed_edit.arena().constrain_max_size(1U);
    failed_edit.arena().append_string("ab");
    failed_edit.tombstone(kInvalidEntryId);

    const MetaStore failed_edit_result
        = commit(healthy_base, std::span<const MetaEdit>(&failed_edit, 1U));
    EXPECT_TRUE(failed_edit_result.resource_limit_exceeded());
    EXPECT_TRUE(failed_edit_result.entries().empty());

    MetaStore failed_base = make_finalized_empty_store(1U, 1U);
    failed_base.arena().append_string("ab");
    ASSERT_TRUE(failed_base.resource_limit_exceeded());
    const MetaStore failed_base_result
        = commit(failed_base, std::span<const MetaEdit>());
    EXPECT_TRUE(failed_base_result.resource_limit_exceeded());
    EXPECT_TRUE(failed_base_result.entries().empty());
    EXPECT_TRUE(failed_base_result.is_finalized());
}


TEST(MetaStoreTest, CompactPreservesBudgetsAndRollsBackFailedCopies)
{
    const MetaStore empty_base = make_finalized_empty_store(1U, 1U);
    const MetaStore compacted_empty = compact(empty_base);
    EXPECT_TRUE(compacted_empty.entries().empty());
    EXPECT_TRUE(compacted_empty.is_finalized());
    EXPECT_FALSE(compacted_empty.resource_limit_exceeded());

    MetaStore shared_span_base;
    shared_span_base.constrain_resources(2U, 1U);
    shared_span_base.add_block(BlockInfo {});
    Entry first;
    first.key.kind = MetaKeyKind::Comment;
    first.value   = make_text(shared_span_base.arena(), "x", TextEncoding::Utf8);
    ASSERT_NE(shared_span_base.add_entry(first), kInvalidEntryId);
    Entry second = first;
    ASSERT_NE(shared_span_base.add_entry(second), kInvalidEntryId);
    shared_span_base.finalize();

    const MetaStore failed_compaction = compact(shared_span_base);
    ASSERT_EQ(failed_compaction.entries().size(), 2U);
    EXPECT_TRUE(failed_compaction.resource_limit_exceeded());
    EXPECT_FALSE(shared_span_base.resource_limit_exceeded());
    EXPECT_EQ(failed_compaction.entry(0U).value.data.span.offset,
              shared_span_base.entry(0U).value.data.span.offset);

    MetaStore deleted_base;
    deleted_base.constrain_resources(1U, 1U);
    deleted_base.add_block(BlockInfo {});
    Entry deleted;
    deleted.key.kind = MetaKeyKind::Comment;
    deleted.value   = make_text(deleted_base.arena(), "x", TextEncoding::Utf8);
    deleted.flags   = EntryFlags::Deleted | EntryFlags::Dirty;
    ASSERT_NE(deleted_base.add_entry(deleted), kInvalidEntryId);
    deleted_base.finalize();

    const MetaStore compacted = compact(deleted_base);
    EXPECT_TRUE(compacted.entries().empty());
    EXPECT_EQ(compacted.block_count(), 1U);
    EXPECT_TRUE(compacted.is_finalized());
    EXPECT_FALSE(compacted.resource_limit_exceeded());
}


TEST(MetaStoreTest, BlockEntriesAreOrderedByOrigin)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry e0;
    e0.key          = make_exif_tag_key(store.arena(), "ifd0Id", 0x010f);
    e0.value        = make_text(store.arena(), "A", TextEncoding::Ascii);
    e0.origin.block = block;
    e0.origin.order_in_block = 10;
    store.add_entry(e0);

    Entry e1;
    e1.key          = make_exif_tag_key(store.arena(), "ifd0Id", 0x0110);
    e1.value        = make_text(store.arena(), "B", TextEncoding::Ascii);
    e1.origin.block = block;
    e1.origin.order_in_block = 0;
    store.add_entry(e1);

    Entry e2;
    e2.key          = make_exif_tag_key(store.arena(), "ifd0Id", 0x0111);
    e2.value        = make_text(store.arena(), "C", TextEncoding::Ascii);
    e2.origin.block = block;
    e2.origin.order_in_block = 5;
    store.add_entry(e2);

    store.finalize();

    const std::span<const EntryId> ids = store.entries_in_block(block);
    ASSERT_EQ(ids.size(), 3U);
    EXPECT_EQ(ids[0], 1U);
    EXPECT_EQ(ids[1], 2U);
    EXPECT_EQ(ids[2], 0U);
}


TEST(MetaStoreTest, InvalidAccessorsReturnEmptySentinels)
{
    MetaStore store;

    const BlockInfo& block = store.block_info(kInvalidBlockId);
    EXPECT_EQ(block.format, 0U);
    EXPECT_EQ(block.container, 0U);
    EXPECT_EQ(block.id, 0U);

    const Entry& entry = store.entry(kInvalidEntryId);
    EXPECT_EQ(entry.origin.block, kInvalidBlockId);
    EXPECT_EQ(entry.origin.order_in_block, 0U);
    EXPECT_EQ(entry.origin.wire_type.family, WireFamily::None);
    EXPECT_EQ(entry.origin.wire_type.code, 0U);
    EXPECT_EQ(entry.origin.wire_count, 0U);

    EXPECT_TRUE(store.entries_in_block(kInvalidBlockId).empty());
}


TEST(MetaStoreTest, PreservesWireTypeUtf8_129)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry e;
    e.key = make_exif_tag_key(store.arena(), "ifd0Id", 0x010e);
    static constexpr const char* kUtf8Hello
        = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    e.value        = make_text(store.arena(), kUtf8Hello, TextEncoding::Utf8);
    e.origin.block = block;
    e.origin.order_in_block = 0;
    e.origin.wire_type      = WireType { WireFamily::Tiff, 129 };
    e.origin.wire_count     = static_cast<uint32_t>(
        std::string_view(kUtf8Hello).size());
    store.add_entry(e);
    store.finalize();

    EXPECT_EQ(store.entry(0).origin.wire_type.code, 129U);
    EXPECT_EQ(store.entry(0).value.text_encoding, TextEncoding::Utf8);
}

}  // namespace openmeta
