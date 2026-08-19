// SPDX-License-Identifier: Apache-2.0

#include "openmeta/interop_import.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static MetaStore make_import_source()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});

        Entry iso;
        iso.key          = make_exif_tag_key(store.arena(), "exififd", 0x8827U);
        iso.value        = make_u16(100U);
        iso.origin.block = block;
        iso.origin.order_in_block = 0U;
        (void)store.add_entry(iso);

        Entry rating_one;
        rating_one.key                   = make_xmp_property_key(store.arena(),
                                                                 "http://ns.adobe.com/xap/1.0/",
                                                                 "Rating");
        rating_one.value                 = make_u16(1U);
        rating_one.origin.block          = block;
        rating_one.origin.order_in_block = 1U;
        (void)store.add_entry(rating_one);

        Entry rating_two;
        rating_two.key                   = make_xmp_property_key(store.arena(),
                                                                 "http://ns.adobe.com/xap/1.0/",
                                                                 "Rating");
        rating_two.value                 = make_u16(2U);
        rating_two.origin.block          = block;
        rating_two.origin.order_in_block = 2U;
        (void)store.add_entry(rating_two);

        store.finalize();
        return store;
    }

    static FlatHostImportValue scalar_u16(uint16_t value)
    {
        FlatHostImportValue out;
        out.kind       = MetaValueKind::Scalar;
        out.elem_type  = MetaElementType::U16;
        out.count      = 1U;
        out.scalar.u64 = value;
        return out;
    }

}  // namespace

TEST(InteropImport, UpdatesByUniqueNameAndIdentityAndAddsExplicitKey)
{
    const MetaStore source = make_import_source();
    const std::array<std::byte, 5> custom_text
        = { std::byte { 'h' }, std::byte { 'e' }, std::byte { 'l' },
            std::byte { 'l' }, std::byte { 'o' } };

    std::array<FlatHostImportItem, 3> items;
    items[0].name   = "Exif:ISOSpeedRatings";
    items[0].target = FlatHostImportTarget::UniqueName;
    items[0].value  = scalar_u16(200U);

    items[1].name         = "XMP:Rating";
    items[1].target       = FlatHostImportTarget::SourceEntry;
    items[1].source_entry = 2U;
    items[1].value        = scalar_u16(5U);

    items[2].name              = "XMP:CustomThing";
    items[2].target            = FlatHostImportTarget::ExplicitKey;
    items[2].explicit_key.kind = MetaKeyKind::XmpProperty;
    items[2].explicit_key.data.xmp_property.schema_ns
        = "http://example.com/ns/1.0/";
    items[2].explicit_key.data.xmp_property.property_path = "CustomThing";
    items[2].value.kind                                   = MetaValueKind::Text;
    items[2].value.elem_type                              = MetaElementType::U8;
    items[2].value.text_encoding                          = TextEncoding::Utf8;
    items[2].value.count                                  = custom_text.size();
    items[2].value.payload                                = custom_text;

    FlatHostImportOptions options;
    options.name_policy         = ExportNamePolicy::Spec;
    FlatHostImportResult result = import_flat_host_metadata(source, items,
                                                            options);

    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(result.imported, 3U);
    EXPECT_EQ(result.updated, 2U);
    EXPECT_EQ(result.added, 1U);
    ASSERT_TRUE(result.store.is_finalized());
    ASSERT_EQ(result.store.entries().size(), 4U);
    EXPECT_EQ(source.entry(0U).value.data.u64, 100U);
    EXPECT_EQ(result.store.entry(0U).value.data.u64, 200U);
    EXPECT_TRUE(any(result.store.entry(0U).flags, EntryFlags::Dirty));
    EXPECT_EQ(result.store.entry(1U).value.data.u64, 1U);
    EXPECT_EQ(result.store.entry(2U).value.data.u64, 5U);
    EXPECT_TRUE(any(result.store.entry(2U).flags, EntryFlags::Dirty));

    const Entry& added = result.store.entry(3U);
    ASSERT_EQ(added.key.kind, MetaKeyKind::XmpProperty);
    ASSERT_EQ(added.value.kind, MetaValueKind::Text);
    const std::span<const std::byte> added_text = result.store.arena().span(
        added.value.data.span);
    ASSERT_EQ(added_text.size(), custom_text.size());
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(added_text.data()),
                               added_text.size()),
              "hello");
    EXPECT_EQ(added.origin.block, kInvalidBlockId);
    EXPECT_TRUE(any(added.flags, EntryFlags::Dirty));
}

TEST(InteropImport, RejectsAmbiguousNameWithoutChangingSource)
{
    const MetaStore source = make_import_source();
    FlatHostImportItem item;
    item.name   = "XMP:Rating";
    item.target = FlatHostImportTarget::UniqueName;
    item.value  = scalar_u16(4U);

    const FlatHostImportResult result = import_flat_host_metadata(
        source, std::span<const FlatHostImportItem>(&item, 1U));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, FlatHostImportCode::AmbiguousName);
    EXPECT_EQ(result.failed_item, 0U);
    EXPECT_FALSE(result.store.is_finalized());
    EXPECT_TRUE(result.store.entries().empty());
    EXPECT_EQ(source.entry(1U).value.data.u64, 1U);
    EXPECT_EQ(source.entry(2U).value.data.u64, 2U);
}

TEST(InteropImport, RejectsIdentityNameMismatchAndInvalidArrayShape)
{
    const MetaStore source = make_import_source();
    FlatHostImportItem mismatch;
    mismatch.name         = "Exif:ExposureTime";
    mismatch.target       = FlatHostImportTarget::SourceEntry;
    mismatch.source_entry = 0U;
    mismatch.value        = scalar_u16(400U);
    EXPECT_EQ(import_flat_host_metadata(
                  source, std::span<const FlatHostImportItem>(&mismatch, 1U))
                  .code,
              FlatHostImportCode::NameMismatch);

    const std::array<std::byte, 3> malformed
        = { std::byte { 1 }, std::byte { 2 }, std::byte { 3 } };
    FlatHostImportItem bad_value;
    bad_value.name            = "Exif:ISOSpeedRatings";
    bad_value.target          = FlatHostImportTarget::UniqueName;
    bad_value.value.kind      = MetaValueKind::Array;
    bad_value.value.elem_type = MetaElementType::U16;
    bad_value.value.count     = 2U;
    bad_value.value.payload   = malformed;
    EXPECT_EQ(import_flat_host_metadata(
                  source, std::span<const FlatHostImportItem>(&bad_value, 1U))
                  .code,
              FlatHostImportCode::InvalidValue);
}

}  // namespace openmeta
