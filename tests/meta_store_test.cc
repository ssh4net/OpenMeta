// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_edit.h"

#include <gtest/gtest.h>

namespace openmeta {

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
