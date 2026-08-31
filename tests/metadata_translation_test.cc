// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_translation.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    static constexpr std::string_view kXmpNsDc
        = "http://purl.org/dc/elements/1.1/";

    static EntryId add_xmp_text(MetaStore* store, BlockId block,
                                std::string_view schema_ns,
                                std::string_view property_path,
                                std::string_view value, EntryFlags flags,
                                uint32_t order,
                                std::string_view wire_type_name = {})
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store->arena(), schema_ns,
                                            property_path);
        entry.value = make_text(store->arena(), value, TextEncoding::Utf8);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        if (!wire_type_name.empty()) {
            entry.origin.wire_type_name = store->arena().append_string(
                wire_type_name);
        }
        entry.flags = flags;
        return store->add_entry(entry);
    }

    static EntryId add_xmp_value(MetaStore* store, BlockId block,
                                 std::string_view schema_ns,
                                 std::string_view property_path,
                                 const MetaValue& value, EntryFlags flags,
                                 uint32_t order)
    {
        Entry entry;
        entry.key          = make_xmp_property_key(store->arena(), schema_ns,
                                                   property_path);
        entry.value        = value;
        entry.origin.block = block;
        entry.origin.order_in_block = order;
        entry.flags                 = flags;
        return store->add_entry(entry);
    }

    static EntryId add_exif_text(MetaStore* store, BlockId block, uint16_t tag,
                                 std::string_view value, uint32_t order)
    {
        Entry entry;
        entry.key   = make_exif_tag_key(store->arena(), "exififd", tag);
        entry.value = make_text(store->arena(), value, TextEncoding::Ascii);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        return store->add_entry(entry);
    }

    static EntryId add_exif_ifd_text(MetaStore* store, BlockId block,
                                     std::string_view ifd, uint16_t tag,
                                     std::string_view value, uint32_t order)
    {
        Entry entry;
        entry.key   = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value = make_text(store->arena(), value, TextEncoding::Ascii);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        return store->add_entry(entry);
    }

    static EntryId add_iptc_bytes(MetaStore* store, BlockId block,
                                  uint16_t dataset, std::string_view value,
                                  uint32_t order)
    {
        Entry entry;
        entry.key = make_iptc_dataset_key(2U, dataset);
        entry.value
            = make_bytes(store->arena(),
                         std::span<const std::byte>(
                             reinterpret_cast<const std::byte*>(value.data()),
                             value.size()));
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        return store->add_entry(entry);
    }

    static bool entry_matches_text(const MetaStore& store, const Entry& entry,
                                   std::string_view expected) noexcept
    {
        if (entry.value.kind != MetaValueKind::Text
            && entry.value.kind != MetaValueKind::Bytes) {
            return false;
        }
        std::span<const std::byte> bytes = store.arena().span(
            entry.value.data.span);
        while (!bytes.empty() && bytes.back() == std::byte { 0U }) {
            bytes = bytes.first(bytes.size() - 1U);
        }
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size())
               == expected;
    }

    static bool active_exif_text(const MetaStore& store, uint16_t tag,
                                 std::string_view expected) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag
                || std::string_view(
                       reinterpret_cast<const char*>(
                           store.arena()
                               .span(entry.key.data.exif_tag.ifd)
                               .data()),
                       store.arena().span(entry.key.data.exif_tag.ifd).size())
                       != "exififd") {
                continue;
            }
            if (entry_matches_text(store, entry, expected)) {
                return true;
            }
        }
        return false;
    }

    static bool active_exif_ifd_text(const MetaStore& store,
                                     std::string_view expected_ifd,
                                     uint16_t tag,
                                     std::string_view expected) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag) {
                continue;
            }
            const std::span<const std::byte> ifd_bytes = store.arena().span(
                entry.key.data.exif_tag.ifd);
            if (std::string_view(reinterpret_cast<const char*>(ifd_bytes.data()),
                                 ifd_bytes.size())
                    == expected_ifd
                && entry_matches_text(store, entry, expected)) {
                return true;
            }
        }
        return false;
    }

    static bool active_iptc_text(const MetaStore& store, uint16_t dataset,
                                 std::string_view expected) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::IptcDataset
                || entry.key.data.iptc_dataset.record != 2U
                || entry.key.data.iptc_dataset.dataset != dataset) {
                continue;
            }
            if (entry_matches_text(store, entry, expected)) {
                return true;
            }
        }
        return false;
    }

    static bool active_iptc_record_text(const MetaStore& store, uint16_t record,
                                        uint16_t dataset,
                                        std::string_view expected) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (!any(entry.flags, EntryFlags::Deleted)
                && entry.key.kind == MetaKeyKind::IptcDataset
                && entry.key.data.iptc_dataset.record == record
                && entry.key.data.iptc_dataset.dataset == dataset
                && entry_matches_text(store, entry, expected)) {
                return true;
            }
        }
        return false;
    }

    static std::vector<std::string_view>
    active_iptc_values(const MetaStore& store, uint16_t dataset)
    {
        std::vector<std::string_view> values;
        for (const Entry& entry : store.entries()) {
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::IptcDataset
                || entry.key.data.iptc_dataset.record != 2U
                || entry.key.data.iptc_dataset.dataset != dataset
                || (entry.value.kind != MetaValueKind::Text
                    && entry.value.kind != MetaValueKind::Bytes)) {
                continue;
            }
            const std::span<const std::byte> bytes = store.arena().span(
                entry.value.data.span);
            values.emplace_back(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
        }
        return values;
    }

    static uint32_t active_exif_count(const MetaStore& store,
                                      uint16_t tag) noexcept
    {
        uint32_t count = 0U;
        for (const Entry& entry : store.entries()) {
            if (!any(entry.flags, EntryFlags::Deleted)
                && entry.key.kind == MetaKeyKind::ExifTag
                && entry.key.data.exif_tag.tag == tag) {
                ++count;
            }
        }
        return count;
    }

    static const Entry* active_exif_entry(const MetaStore& store,
                                          std::string_view ifd,
                                          uint16_t tag) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag) {
                continue;
            }
            const std::span<const std::byte> ifd_bytes = store.arena().span(
                entry.key.data.exif_tag.ifd);
            if (std::string_view(reinterpret_cast<const char*>(ifd_bytes.data()),
                                 ifd_bytes.size())
                == ifd) {
                return &entry;
            }
        }
        return nullptr;
    }

    static bool active_exif_origin_wire_name(const MetaStore& store,
                                             uint16_t tag,
                                             std::string_view expected) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag) {
                continue;
            }
            const std::span<const std::byte> bytes = store.arena().span(
                entry.origin.wire_type_name);
            if (std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                 bytes.size())
                == expected) {
                return true;
            }
        }
        return false;
    }

    static uint32_t active_iptc_count(const MetaStore& store,
                                      uint16_t dataset) noexcept
    {
        uint32_t count = 0U;
        for (const Entry& entry : store.entries()) {
            if (!any(entry.flags, EntryFlags::Deleted)
                && entry.key.kind == MetaKeyKind::IptcDataset
                && entry.key.data.iptc_dataset.record == 2U
                && entry.key.data.iptc_dataset.dataset == dataset) {
                ++count;
            }
        }
        return count;
    }

    TEST(MetadataTranslation, TranslatesStrictDirtyDatesWithoutPrecisionLoss)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/xap/1.0/",
                               "CreateDate", "2024-08-30T01:02:03-02:30",
                               EntryFlags::Dirty, 0U, "xmp-date"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block,
                               "http://ns.adobe.com/photoshop/1.0/",
                               "DateCreated", "2024-08-29T12:35:01+09:00",
                               EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/exif/1.0/",
                               "DateTimeOriginal", "2024-08-28T10:11:12.500Z",
                               EntryFlags::Dirty, 2U),
                  kInvalidEntryId);
        source.finalize();

        MetaStore translated;
        const MetadataDateTranslationResult result
            = translate_xmp_creation_dates(source,
                                           MetadataDateTranslationOptions {},
                                           &translated);
        ASSERT_EQ(result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 3U);
        EXPECT_EQ(result.groups_translated, 4U);
        EXPECT_EQ(result.entries_added, 9U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);

        EXPECT_TRUE(
            active_exif_text(translated, 0x9004U, "2024:08:30 01:02:03"));
        EXPECT_TRUE(active_exif_text(translated, 0x9012U, "-02:30"));
        EXPECT_TRUE(
            active_exif_origin_wire_name(translated, 0x9004U, "xmp-date"));
        EXPECT_EQ(active_exif_count(translated, 0x9292U), 0U);
        EXPECT_TRUE(active_iptc_text(translated, 62U, "20240830"));
        EXPECT_TRUE(active_iptc_text(translated, 63U, "010203-0230"));

        EXPECT_TRUE(active_iptc_text(translated, 55U, "20240829"));
        EXPECT_TRUE(active_iptc_text(translated, 60U, "123501+0900"));

        EXPECT_TRUE(
            active_exif_text(translated, 0x9003U, "2024:08:28 10:11:12"));
        EXPECT_TRUE(active_exif_text(translated, 0x9011U, "+00:00"));
        EXPECT_TRUE(active_exif_text(translated, 0x9291U, "500"));
    }

    TEST(MetadataTranslation, PreservesNegativeZeroTimezoneLexically)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/xap/1.0/",
                               "CreateDate", "2024-08-30T01:02:03-00:00",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        source.finalize();

        MetaStore translated;
        const MetadataDateTranslationResult result
            = translate_xmp_creation_dates(source,
                                           MetadataDateTranslationOptions {},
                                           &translated);
        ASSERT_EQ(result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_TRUE(active_exif_text(translated, 0x9012U, "-00:00"));
        EXPECT_TRUE(active_iptc_text(translated, 63U, "010203-0000"));
    }

    TEST(MetadataTranslation, RejectsLossyOrMalformedMappingsTransactionally)
    {
        MetaStore fractional;
        const BlockId block = fractional.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&fractional, block,
                               "http://ns.adobe.com/xap/1.0/", "CreateDate",
                               "2024-08-30T01:02:03.125Z", EntryFlags::Dirty,
                               0U),
                  kInvalidEntryId);
        fractional.finalize();

        MetaStore sentinel;
        const BlockId sentinel_block = sentinel.add_block(BlockInfo {});
        ASSERT_NE(sentinel_block, kInvalidBlockId);
        ASSERT_NE(add_iptc_bytes(&sentinel, sentinel_block, 5U, "sentinel", 0U),
                  kInvalidEntryId);
        sentinel.finalize();
        MetaStore output = sentinel;

        MetadataDateTranslationResult result = translate_xmp_creation_dates(
            fractional, MetadataDateTranslationOptions {}, &output);
        EXPECT_EQ(result.status,
                  MetadataDateTranslationStatus::UnsupportedPrecision);
        EXPECT_EQ(result.failed_mapping,
                  MetadataDateTranslationMapping::XmpCreateDate);
        EXPECT_TRUE(active_iptc_text(output, 5U, "sentinel"));
        EXPECT_EQ(output.entries().size(), sentinel.entries().size());

        MetadataDateTranslationOptions exif_only;
        exif_only.create_date_to_iptc_digital_creation = false;
        exif_only.date_created_to_iptc_created         = false;
        exif_only.date_time_original_to_exif_original  = false;
        result = translate_xmp_creation_dates(fractional, exif_only, &output);
        ASSERT_EQ(result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_TRUE(active_exif_text(output, 0x9292U, "125"));

        MetaStore date_only;
        const BlockId date_block = date_only.add_block(BlockInfo {});
        ASSERT_NE(date_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&date_only, date_block,
                               "http://ns.adobe.com/xap/1.0/", "CreateDate",
                               "2024-02-29", EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        date_only.finalize();
        result = translate_xmp_creation_dates(date_only,
                                              MetadataDateTranslationOptions {},
                                              &output);
        EXPECT_EQ(result.status,
                  MetadataDateTranslationStatus::UnsupportedPrecision);

        MetadataDateTranslationOptions iptc_only;
        iptc_only.create_date_to_exif_digitized       = false;
        iptc_only.date_created_to_iptc_created        = false;
        iptc_only.date_time_original_to_exif_original = false;
        result = translate_xmp_creation_dates(date_only, iptc_only, &output);
        ASSERT_EQ(result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_TRUE(active_iptc_text(output, 62U, "20240229"));
        EXPECT_EQ(active_iptc_count(output, 63U), 0U);

        MetaStore malformed;
        const BlockId malformed_block = malformed.add_block(BlockInfo {});
        ASSERT_NE(malformed_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&malformed, malformed_block,
                               "http://ns.adobe.com/xap/1.0/", "CreateDate",
                               "2023-02-29T01:02:03Ztrailing",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        malformed.finalize();
        result = translate_xmp_creation_dates(malformed, iptc_only, &output);
        EXPECT_EQ(result.status,
                  MetadataDateTranslationStatus::InvalidDateTime);
    }

    TEST(MetadataTranslation, HonorsSourceSelectionAndRejectsDuplicates)
    {
        MetaStore clean;
        const BlockId block = clean.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&clean, block, "http://ns.adobe.com/xap/1.0/",
                               "CreateDate", "2024-08-30T01:02:03Z",
                               EntryFlags::None, 0U),
                  kInvalidEntryId);
        clean.finalize();

        MetaStore output;
        MetadataDateTranslationOptions options;
        options.date_created_to_iptc_created        = false;
        options.date_time_original_to_exif_original = false;
        MetadataDateTranslationResult result
            = translate_xmp_creation_dates(clean, options, &output);
        ASSERT_EQ(result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 0U);
        EXPECT_EQ(output.entries().size(), clean.entries().size());

        options.source_mode = MetadataDateTranslationSourceMode::All;
        result = translate_xmp_creation_dates(clean, options, &output);
        ASSERT_EQ(result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 1U);
        EXPECT_TRUE(active_exif_text(output, 0x9004U, "2024:08:30 01:02:03"));

        MetaStore duplicate;
        const BlockId duplicate_block = duplicate.add_block(BlockInfo {});
        ASSERT_NE(duplicate_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&duplicate, duplicate_block,
                               "http://ns.adobe.com/xap/1.0/", "CreateDate",
                               "2024-08-30T01:02:03Z", EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&duplicate, duplicate_block,
                               "http://ns.adobe.com/xap/1.0/", "CreateDate",
                               "2024-08-31T01:02:03Z", EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        duplicate.finalize();
        result = translate_xmp_creation_dates(duplicate, options, &output);
        EXPECT_EQ(result.status,
                  MetadataDateTranslationStatus::AmbiguousSource);
    }

    TEST(MetadataTranslation, ReconcilesNativeGroupsByExplicitPolicy)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/xap/1.0/",
                               "CreateDate", "2024-08-30T01:02:03+09:00",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_text(&source, block, 0x9004U, "2001:02:03 04:05:06",
                                1U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_text(&source, block, 0x9012U, "-01:00", 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_text(&source, block, 0x9012U, "+02:00", 3U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_text(&source, block, 0x9292U, "999", 4U),
                  kInvalidEntryId);
        source.finalize();

        MetadataDateTranslationOptions options;
        options.create_date_to_iptc_digital_creation = false;
        options.date_created_to_iptc_created         = false;
        options.date_time_original_to_exif_original  = false;

        MetaStore output;
        options.conflict_policy
            = MetadataDateTranslationConflictPolicy::PreserveExisting;
        MetadataDateTranslationResult result
            = translate_xmp_creation_dates(source, options, &output);
        ASSERT_EQ(result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(result.groups_preserved, 1U);
        EXPECT_TRUE(active_exif_text(output, 0x9004U, "2001:02:03 04:05:06"));

        options.conflict_policy
            = MetadataDateTranslationConflictPolicy::FailOnConflict;
        result = translate_xmp_creation_dates(source, options, &output);
        EXPECT_EQ(result.status, MetadataDateTranslationStatus::NativeConflict);

        options.conflict_policy
            = MetadataDateTranslationConflictPolicy::ReplaceExisting;
        options.max_operations = 1U;
        result = translate_xmp_creation_dates(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataDateTranslationStatus::OperationLimitExceeded);

        options.max_operations = kMetadataDateTranslationMaxOperations;
        result = translate_xmp_creation_dates(source, options, &output);
        ASSERT_EQ(result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(result.entries_updated, 2U);
        EXPECT_EQ(result.entries_removed, 2U);
        EXPECT_EQ(active_exif_count(output, 0x9004U), 1U);
        EXPECT_EQ(active_exif_count(output, 0x9012U), 1U);
        EXPECT_EQ(active_exif_count(output, 0x9292U), 0U);
        EXPECT_TRUE(active_exif_text(output, 0x9004U, "2024:08:30 01:02:03"));
        EXPECT_TRUE(active_exif_text(output, 0x9012U, "+09:00"));
    }

    TEST(MetadataTranslation, DirtyDateRemovalTombstonesNativeGroups)
    {
        MetaStore base;
        const BlockId block = base.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&base, block, "http://ns.adobe.com/xap/1.0/",
                               "CreateDate", "2024-08-30T01:02:03Z",
                               EntryFlags::None, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_text(&base, block, 0x9004U, "2024:08:30 01:02:03",
                                1U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_text(&base, block, 0x9012U, "+00:00", 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_bytes(&base, block, 62U, "20240830", 3U),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_bytes(&base, block, 63U, "010203+0000", 4U),
                  kInvalidEntryId);
        base.finalize();

        const MetadataEditingOperation operation = make_metadata_edit_remove(
            MetadataCreationFieldKind::CreateDate);
        MetadataEditingRequest request;
        request.operations
            = std::span<const MetadataEditingOperation>(&operation, 1U);
        MetaStore edited;
        ASSERT_EQ(edit_metadata(base, request, &edited).status,
                  MetadataEditingStatus::Ok);

        MetadataDateTranslationOptions options;
        options.conflict_policy
            = MetadataDateTranslationConflictPolicy::ReplaceExisting;
        options.date_created_to_iptc_created        = false;
        options.date_time_original_to_exif_original = false;
        MetaStore translated;
        const MetadataDateTranslationResult result
            = translate_xmp_creation_dates(edited, options, &translated);
        ASSERT_EQ(result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 1U);
        EXPECT_EQ(result.groups_translated, 2U);
        EXPECT_EQ(result.entries_removed, 4U);
        EXPECT_EQ(active_exif_count(translated, 0x9004U), 0U);
        EXPECT_EQ(active_exif_count(translated, 0x9012U), 0U);
        EXPECT_EQ(active_iptc_count(translated, 62U), 0U);
        EXPECT_EQ(active_iptc_count(translated, 63U), 0U);
    }

    TEST(MetadataTranslation, TranslatesTechnicalXmpToExactExifGroups)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/xap/1.0/",
                               "ModifyDate", "2026-08-31T12:34:56.125+09:00",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/tiff/1.0/",
                               "Make", "OpenMeta Camera", EntryFlags::Dirty,
                               1U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/tiff/1.0/",
                               "Model", "OM-1", EntryFlags::Dirty, 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/xap/1.0/",
                               "CreatorTool", "OpenMeta 0.4", EntryFlags::Dirty,
                               3U),
                  kInvalidEntryId);
        source.finalize();

        MetaStore translated;
        const MetadataTechnicalTranslationResult result
            = translate_xmp_technical_metadata(
                source, MetadataTechnicalTranslationOptions {}, &translated);
        ASSERT_EQ(result.status, MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 4U);
        EXPECT_EQ(result.groups_translated, 4U);
        EXPECT_EQ(result.entries_added, 6U);
        EXPECT_TRUE(active_exif_ifd_text(translated, "ifd0", 0x0132U,
                                         "2026:08:31 12:34:56"));
        EXPECT_TRUE(
            active_exif_ifd_text(translated, "exififd", 0x9010U, "+09:00"));
        EXPECT_TRUE(
            active_exif_ifd_text(translated, "exififd", 0x9290U, "125"));
        EXPECT_TRUE(active_exif_ifd_text(translated, "ifd0", 0x010fU,
                                         "OpenMeta Camera"));
        EXPECT_TRUE(active_exif_ifd_text(translated, "ifd0", 0x0110U, "OM-1"));
        EXPECT_TRUE(
            active_exif_ifd_text(translated, "ifd0", 0x0131U, "OpenMeta 0.4"));
    }

    TEST(MetadataTranslation, TechnicalSourcesAreExactAsciiAndBounded)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/tiff/1.0/",
                               "Make", "Clean ignored", EntryFlags::None, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/tiff/1.0",
                               "Model", "Wrong namespace", EntryFlags::Dirty,
                               1U),
                  kInvalidEntryId);
        source.finalize();

        MetaStore translated;
        MetadataTechnicalTranslationOptions options;
        MetadataTechnicalTranslationResult result
            = translate_xmp_technical_metadata(source, options, &translated);
        ASSERT_EQ(result.status, MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 0U);

        options.source_mode = MetadataTechnicalTranslationSourceMode::All;
        result = translate_xmp_technical_metadata(source, options, &translated);
        ASSERT_EQ(result.status, MetadataTechnicalTranslationStatus::Ok);
        EXPECT_TRUE(
            active_exif_ifd_text(translated, "ifd0", 0x010fU, "Clean ignored"));
        EXPECT_EQ(active_exif_count(translated, 0x0110U), 0U);

        MetaStore ambiguous;
        const BlockId ambiguous_block = ambiguous.add_block(BlockInfo {});
        ASSERT_NE(ambiguous_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&ambiguous, ambiguous_block,
                               "http://ns.adobe.com/tiff/1.0/", "Model", "A",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&ambiguous, ambiguous_block,
                               "http://ns.adobe.com/tiff/1.0/", "Model", "B",
                               EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        ambiguous.finalize();
        options.source_mode = MetadataTechnicalTranslationSourceMode::DirtyOnly;
        result = translate_xmp_technical_metadata(ambiguous, options,
                                                  &translated);
        EXPECT_EQ(result.status,
                  MetadataTechnicalTranslationStatus::AmbiguousSource);
        EXPECT_EQ(result.failed_mapping,
                  MetadataTechnicalTranslationMapping::TiffModel);
        EXPECT_TRUE(
            active_exif_ifd_text(translated, "ifd0", 0x010fU, "Clean ignored"));

        MetaStore invalid;
        const BlockId invalid_block = invalid.add_block(BlockInfo {});
        ASSERT_NE(invalid_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&invalid, invalid_block,
                               "http://ns.adobe.com/tiff/1.0/", "Make",
                               "M\xc3\xa4ke", EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        invalid.finalize();
        result = translate_xmp_technical_metadata(invalid, options,
                                                  &translated);
        EXPECT_EQ(result.status,
                  MetadataTechnicalTranslationStatus::NonAsciiSource);
        EXPECT_EQ(result.failed_mapping,
                  MetadataTechnicalTranslationMapping::TiffMake);
        EXPECT_TRUE(
            active_exif_ifd_text(translated, "ifd0", 0x010fU, "Clean ignored"));

        MetaStore embedded_nul;
        const BlockId embedded_nul_block = embedded_nul.add_block(BlockInfo {});
        ASSERT_NE(embedded_nul_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&embedded_nul, embedded_nul_block,
                               "http://ns.adobe.com/tiff/1.0/", "Model",
                               std::string_view("A\0B", 3U), EntryFlags::Dirty,
                               0U),
                  kInvalidEntryId);
        embedded_nul.finalize();
        result = translate_xmp_technical_metadata(embedded_nul, options,
                                                  &translated);
        EXPECT_EQ(result.status,
                  MetadataTechnicalTranslationStatus::NonAsciiSource);
        EXPECT_EQ(result.failed_mapping,
                  MetadataTechnicalTranslationMapping::TiffModel);

        MetaStore oversized;
        const BlockId oversized_block = oversized.add_block(BlockInfo {});
        ASSERT_NE(oversized_block, kInvalidBlockId);
        const std::string long_model(33U, 'x');
        ASSERT_NE(add_xmp_text(&oversized, oversized_block,
                               "http://ns.adobe.com/tiff/1.0/", "Model",
                               long_model, EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        oversized.finalize();
        options.max_text_bytes_per_property = 32U;
        result = translate_xmp_technical_metadata(oversized, options,
                                                  &translated);
        EXPECT_EQ(result.status,
                  MetadataTechnicalTranslationStatus::ValueTooLong);
        EXPECT_EQ(result.failed_mapping,
                  MetadataTechnicalTranslationMapping::TiffModel);

        options.max_text_bytes_per_property
            = kMetadataTechnicalTranslationMaxTextBytesPerProperty;
        options.max_total_text_bytes = 32U;
        result = translate_xmp_technical_metadata(oversized, options,
                                                  &translated);
        EXPECT_EQ(result.status,
                  MetadataTechnicalTranslationStatus::SourceLimitExceeded);

        MetaStore malformed_date;
        const BlockId malformed_date_block = malformed_date.add_block(
            BlockInfo {});
        ASSERT_NE(malformed_date_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&malformed_date, malformed_date_block,
                               "http://ns.adobe.com/xap/1.0/", "ModifyDate",
                               "2026-02-29T01:02:03Z", EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        malformed_date.finalize();
        options.max_total_text_bytes
            = kMetadataTechnicalTranslationMaxTotalTextBytes;
        result = translate_xmp_technical_metadata(malformed_date, options,
                                                  &translated);
        EXPECT_EQ(result.status,
                  MetadataTechnicalTranslationStatus::InvalidDateTime);
        EXPECT_EQ(result.failed_mapping,
                  MetadataTechnicalTranslationMapping::XmpModifyDate);

        MetaStore date_only;
        const BlockId date_only_block = date_only.add_block(BlockInfo {});
        ASSERT_NE(date_only_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&date_only, date_only_block,
                               "http://ns.adobe.com/xap/1.0/", "ModifyDate",
                               "2026-08-31", EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        date_only.finalize();
        result = translate_xmp_technical_metadata(date_only, options,
                                                  &translated);
        EXPECT_EQ(result.status,
                  MetadataTechnicalTranslationStatus::UnsupportedPrecision);
    }

    TEST(MetadataTranslation,
         TechnicalConflictReplacementAndRemovalAreTransactional)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/tiff/1.0/",
                               "Make", "Replacement", EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&source, block, "ifd0", 0x010fU, "Old A",
                                    1U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&source, block, "ifd0", 0x010fU, "Old B",
                                    2U),
                  kInvalidEntryId);
        source.finalize();

        MetadataTechnicalTranslationOptions options;
        options.modify_date_to_exif_datetime  = false;
        options.model_to_exif_model           = false;
        options.creator_tool_to_exif_software = false;
        options.conflict_policy
            = MetadataTechnicalTranslationConflictPolicy::PreserveExisting;
        MetaStore output;
        MetadataTechnicalTranslationResult result
            = translate_xmp_technical_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(result.groups_preserved, 1U);
        EXPECT_TRUE(active_exif_ifd_text(output, "ifd0", 0x010fU, "Old A"));

        options.conflict_policy
            = MetadataTechnicalTranslationConflictPolicy::FailOnConflict;
        result = translate_xmp_technical_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataTechnicalTranslationStatus::NativeConflict);

        options.conflict_policy
            = MetadataTechnicalTranslationConflictPolicy::ReplaceExisting;
        options.max_operations = 1U;
        result = translate_xmp_technical_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataTechnicalTranslationStatus::OperationLimitExceeded);

        options.max_operations = kMetadataTechnicalTranslationMaxOperations;
        result = translate_xmp_technical_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(active_exif_count(output, 0x010fU), 1U);
        EXPECT_TRUE(
            active_exif_ifd_text(output, "ifd0", 0x010fU, "Replacement"));

        MetaStore removal;
        const BlockId removal_block = removal.add_block(BlockInfo {});
        ASSERT_NE(removal_block, kInvalidBlockId);
        Entry removed_xmp;
        removed_xmp.key          = make_xmp_property_key(removal.arena(),
                                                         "http://ns.adobe.com/tiff/1.0/",
                                                         "Model");
        removed_xmp.value        = make_text(removal.arena(), "Old model",
                                             TextEncoding::Utf8);
        removed_xmp.origin.block = removal_block;
        removed_xmp.flags        = EntryFlags::Dirty | EntryFlags::Deleted;
        ASSERT_NE(removal.add_entry(removed_xmp), kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&removal, removal_block, "ifd0", 0x0110U,
                                    "Old model", 1U),
                  kInvalidEntryId);
        removal.finalize();
        options.make_to_exif_make   = false;
        options.model_to_exif_model = true;
        options.conflict_policy
            = MetadataTechnicalTranslationConflictPolicy::ReplaceExisting;
        result = translate_xmp_technical_metadata(removal, options, &output);
        ASSERT_EQ(result.status, MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(active_exif_count(output, 0x0110U), 0U);
    }

    TEST(MetadataTranslation, TranslatesCaptureXmpToTypedExifScalars)
    {
        static constexpr std::string_view kExifNs
            = "http://ns.adobe.com/exif/1.0/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_value(&source, block, kExifNs, "ExposureTime",
                                make_urational(1U, 125U), EntryFlags::Dirty,
                                0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kExifNs, "FNumber", "2.8",
                               EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kExifNs, "ISOSpeedRatings[1]",
                               "400", EntryFlags::Dirty, 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kExifNs, "FocalLength",
                               "66.0 mm", EntryFlags::Dirty, 3U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kExifNs, "ExposureCompensation",
                               "-1/3", EntryFlags::Dirty, 4U),
                  kInvalidEntryId);
        source.finalize();

        MetaStore translated;
        const MetadataCaptureTranslationResult result
            = translate_xmp_capture_metadata(
                source, MetadataCaptureTranslationOptions {}, &translated);
        ASSERT_EQ(result.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 5U);
        EXPECT_EQ(result.groups_translated, 5U);
        EXPECT_EQ(result.entries_added, 5U);
        EXPECT_EQ(source.entries().size(), 5U);

        const Entry* exposure = active_exif_entry(translated, "exififd",
                                                  0x829aU);
        ASSERT_NE(exposure, nullptr);
        ASSERT_EQ(exposure->value.elem_type, MetaElementType::URational);
        EXPECT_EQ(exposure->value.data.ur.numer, 1U);
        EXPECT_EQ(exposure->value.data.ur.denom, 125U);

        const Entry* f_number = active_exif_entry(translated, "exififd",
                                                  0x829dU);
        ASSERT_NE(f_number, nullptr);
        ASSERT_EQ(f_number->value.elem_type, MetaElementType::URational);
        EXPECT_EQ(f_number->value.data.ur.numer, 14U);
        EXPECT_EQ(f_number->value.data.ur.denom, 5U);

        const Entry* iso = active_exif_entry(translated, "exififd", 0x8827U);
        ASSERT_NE(iso, nullptr);
        ASSERT_EQ(iso->value.elem_type, MetaElementType::U16);
        EXPECT_EQ(iso->value.data.u64, 400U);

        const Entry* focal = active_exif_entry(translated, "exififd", 0x920aU);
        ASSERT_NE(focal, nullptr);
        ASSERT_EQ(focal->value.elem_type, MetaElementType::URational);
        EXPECT_EQ(focal->value.data.ur.numer, 66U);
        EXPECT_EQ(focal->value.data.ur.denom, 1U);

        const Entry* bias = active_exif_entry(translated, "exififd", 0x9204U);
        ASSERT_NE(bias, nullptr);
        ASSERT_EQ(bias->value.elem_type, MetaElementType::SRational);
        EXPECT_EQ(bias->value.data.sr.numer, -1);
        EXPECT_EQ(bias->value.data.sr.denom, 3);
    }

    TEST(MetadataTranslation, CaptureSourcesAreExactBoundedAndTransactional)
    {
        static constexpr std::string_view kExifNs
            = "http://ns.adobe.com/exif/1.0/";
        MetadataCaptureTranslationOptions options;
        MetaStore output;

        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, kExifNs, "ExposureTime", "8e-3",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kExifNs, "ISO", "200",
                               EntryFlags::None, 1U),
                  kInvalidEntryId);
        source.finalize();
        MetadataCaptureTranslationResult result
            = translate_xmp_capture_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 1U);
        const Entry* exposure = active_exif_entry(output, "exififd", 0x829aU);
        ASSERT_NE(exposure, nullptr);
        EXPECT_EQ(exposure->value.data.ur.numer, 1U);
        EXPECT_EQ(exposure->value.data.ur.denom, 125U);
        EXPECT_EQ(active_exif_count(output, 0x8827U), 0U);

        options.source_mode = MetadataCaptureTranslationSourceMode::All;
        result = translate_xmp_capture_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_NE(active_exif_entry(output, "exififd", 0x8827U), nullptr);

        MetaStore ambiguous;
        const BlockId ambiguous_block = ambiguous.add_block(BlockInfo {});
        ASSERT_NE(ambiguous_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&ambiguous, ambiguous_block, kExifNs, "ISO",
                               "100", EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&ambiguous, ambiguous_block, kExifNs,
                               "ISOSpeedRatings[1]", "100", EntryFlags::Dirty,
                               1U),
                  kInvalidEntryId);
        ambiguous.finalize();
        options.source_mode = MetadataCaptureTranslationSourceMode::DirtyOnly;
        result = translate_xmp_capture_metadata(ambiguous, options, &output);
        EXPECT_EQ(result.status,
                  MetadataCaptureTranslationStatus::AmbiguousSource);
        EXPECT_EQ(result.failed_mapping,
                  MetadataCaptureTranslationMapping::XmpIso);
        EXPECT_NE(active_exif_entry(output, "exififd", 0x8827U), nullptr);

        const auto expect_failure =
            [&](std::string_view path, std::string_view value,
                MetadataCaptureTranslationStatus status,
                MetadataCaptureTranslationMapping mapping) {
                MetaStore invalid;
                const BlockId invalid_block = invalid.add_block(BlockInfo {});
                EXPECT_NE(invalid_block, kInvalidBlockId);
                EXPECT_NE(add_xmp_text(&invalid, invalid_block, kExifNs, path,
                                       value, EntryFlags::Dirty, 0U),
                          kInvalidEntryId);
                invalid.finalize();
                const MetadataCaptureTranslationResult failed
                    = translate_xmp_capture_metadata(invalid, options, &output);
                EXPECT_EQ(failed.status, status);
                EXPECT_EQ(failed.failed_mapping, mapping);
                EXPECT_NE(active_exif_entry(output, "exififd", 0x8827U),
                          nullptr);
            };
        expect_failure("ExposureTime", "1/0",
                       MetadataCaptureTranslationStatus::InvalidNumericValue,
                       MetadataCaptureTranslationMapping::XmpExposureTime);
        expect_failure("ISO", "65536",
                       MetadataCaptureTranslationStatus::ValueOutOfRange,
                       MetadataCaptureTranslationMapping::XmpIso);
        expect_failure(
            "ExposureCompensation", "0.333333333333333",
            MetadataCaptureTranslationStatus::ValueOutOfRange,
            MetadataCaptureTranslationMapping::XmpExposureCompensation);
        expect_failure("FocalLength", "50 pixels",
                       MetadataCaptureTranslationStatus::InvalidNumericValue,
                       MetadataCaptureTranslationMapping::XmpFocalLength);

        MetaStore multi_iso;
        const BlockId multi_iso_block = multi_iso.add_block(BlockInfo {});
        ASSERT_NE(multi_iso_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&multi_iso, multi_iso_block, kExifNs,
                               "ISOSpeedRatings[1]", "100", EntryFlags::Dirty,
                               0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&multi_iso, multi_iso_block, kExifNs,
                               "ISOSpeedRatings[2]", "200", EntryFlags::Dirty,
                               1U),
                  kInvalidEntryId);
        multi_iso.finalize();
        result = translate_xmp_capture_metadata(multi_iso, options, &output);
        EXPECT_EQ(result.status,
                  MetadataCaptureTranslationStatus::InvalidSourceValue);
        EXPECT_EQ(result.failed_mapping,
                  MetadataCaptureTranslationMapping::XmpIso);

        MetaStore oversized;
        const BlockId oversized_block = oversized.add_block(BlockInfo {});
        ASSERT_NE(oversized_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&oversized, oversized_block, kExifNs, "FNumber",
                               std::string(33U, '1'), EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        oversized.finalize();
        options.max_text_bytes_per_property = 32U;
        result = translate_xmp_capture_metadata(oversized, options, &output);
        EXPECT_EQ(result.status,
                  MetadataCaptureTranslationStatus::ValueTooLong);
    }

    TEST(MetadataTranslation,
         CaptureConflictReplacementAndRemovalAreTransactional)
    {
        static constexpr std::string_view kExifNs
            = "http://ns.adobe.com/exif/1.0/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, kExifNs, "FNumber", "2.8",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        Entry first;
        first.key   = make_exif_tag_key(source.arena(), "exififd", 0x829dU);
        first.value = make_urational(28U, 10U);
        first.origin.block          = block;
        first.origin.order_in_block = 1U;
        ASSERT_NE(source.add_entry(first), kInvalidEntryId);
        Entry duplicate                 = first;
        duplicate.value                 = make_urational(14U, 5U);
        duplicate.origin.order_in_block = 2U;
        ASSERT_NE(source.add_entry(duplicate), kInvalidEntryId);
        source.finalize();

        MetadataCaptureTranslationOptions options;
        options.exposure_time_to_exif         = false;
        options.iso_to_exif                   = false;
        options.focal_length_to_exif          = false;
        options.exposure_compensation_to_exif = false;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
        MetaStore output;
        MetadataCaptureTranslationResult result
            = translate_xmp_capture_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(result.groups_preserved, 1U);
        EXPECT_EQ(active_exif_count(output, 0x829dU), 2U);

        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
        result = translate_xmp_capture_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataCaptureTranslationStatus::NativeConflict);

        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        options.max_operations = 1U;
        result = translate_xmp_capture_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(active_exif_count(output, 0x829dU), 1U);

        MetaStore removal;
        const BlockId removal_block = removal.add_block(BlockInfo {});
        ASSERT_NE(removal_block, kInvalidBlockId);
        Entry deleted;
        deleted.key   = make_xmp_property_key(removal.arena(), kExifNs, "ISO");
        deleted.value = make_u16(400U);
        deleted.origin.block = removal_block;
        deleted.flags        = EntryFlags::Dirty | EntryFlags::Deleted;
        ASSERT_NE(removal.add_entry(deleted), kInvalidEntryId);
        Entry native;
        native.key   = make_exif_tag_key(removal.arena(), "exififd", 0x8827U);
        native.value = make_u16(400U);
        native.origin.block          = removal_block;
        native.origin.order_in_block = 1U;
        ASSERT_NE(removal.add_entry(native), kInvalidEntryId);
        removal.finalize();

        options.f_number_to_exif = false;
        options.iso_to_exif      = true;
        result = translate_xmp_capture_metadata(removal, options, &output);
        ASSERT_EQ(result.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(active_exif_count(output, 0x8827U), 0U);
    }

    TEST(MetadataTranslation,
         TranslatesTargetBoundXmpGeometryToCanonicalExifGroups)
    {
        static constexpr std::string_view kExifNs
            = "http://ns.adobe.com/exif/1.0/";
        static constexpr std::string_view kTiffNs
            = "http://ns.adobe.com/tiff/1.0/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_value(&source, block, kTiffNs, "Orientation",
                                make_u16(6U), EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kTiffNs, "ImageWidth", "6000",
                               EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_value(&source, block, kExifNs, "ExifImageWidth",
                                make_u32(6000U), EntryFlags::None, 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kExifNs, "PixelXDimension",
                               "6000", EntryFlags::None, 3U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_value(&source, block, kTiffNs, "ImageHeight",
                                make_u32(4000U), EntryFlags::None, 4U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_value(&source, block, kExifNs, "ExifImageHeight",
                                make_u32(4000U), EntryFlags::Dirty, 5U),
                  kInvalidEntryId);
        source.finalize();

        TransferTargetImageSpec target;
        target.has_dimensions  = true;
        target.width           = 6000U;
        target.height          = 4000U;
        target.has_orientation = true;
        target.orientation     = 6U;

        MetaStore translated;
        const MetadataGeometryTranslationResult result
            = translate_xmp_image_geometry(source, target,
                                           MetadataGeometryTranslationOptions {},
                                           &translated);
        ASSERT_EQ(result.status, MetadataGeometryTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 6U);
        EXPECT_EQ(result.groups_translated, 2U);
        EXPECT_EQ(result.entries_added, 5U);

        const auto expect_native = [&](std::string_view ifd, uint16_t tag,
                                       MetaElementType type, uint64_t value) {
            const Entry* entry = active_exif_entry(translated, ifd, tag);
            ASSERT_NE(entry, nullptr);
            EXPECT_EQ(entry->value.kind, MetaValueKind::Scalar);
            EXPECT_EQ(entry->value.elem_type, type);
            EXPECT_EQ(entry->value.data.u64, value);
        };
        expect_native("ifd0", 0x0100U, MetaElementType::U32, 6000U);
        expect_native("ifd0", 0x0101U, MetaElementType::U32, 4000U);
        expect_native("exififd", 0xA002U, MetaElementType::U32, 6000U);
        expect_native("exififd", 0xA003U, MetaElementType::U32, 4000U);
        expect_native("ifd0", 0x0112U, MetaElementType::U16, 6U);
    }

    TEST(MetadataTranslation,
         GeometryRejectsMissingMismatchedAmbiguousAndIncompleteSources)
    {
        static constexpr std::string_view kExifNs
            = "http://ns.adobe.com/exif/1.0/";
        static constexpr std::string_view kTiffNs
            = "http://ns.adobe.com/tiff/1.0/";
        MetadataGeometryTranslationOptions options;
        options.orientation_to_exif = false;

        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, kExifNs, "ExifImageWidth", "640",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kExifNs, "ExifImageHeight",
                               "480", EntryFlags::None, 1U),
                  kInvalidEntryId);
        source.finalize();

        MetaStore output;
        TransferTargetImageSpec target;
        MetadataGeometryTranslationResult result
            = translate_xmp_image_geometry(source, target, options, &output);
        EXPECT_EQ(result.status,
                  MetadataGeometryTranslationStatus::TargetImageSpecRequired);
        EXPECT_EQ(result.failed_mapping,
                  MetadataGeometryTranslationMapping::XmpDimensions);

        target.has_dimensions = true;
        target.width          = 640U;
        target.height         = 480U;
        result = translate_xmp_image_geometry(source, target, options, &output);
        ASSERT_EQ(result.status, MetadataGeometryTranslationStatus::Ok);
        const Entry* committed_width = active_exif_entry(output, "ifd0",
                                                         0x0100U);
        ASSERT_NE(committed_width, nullptr);
        EXPECT_EQ(committed_width->value.data.u64, 640U);

        target.width  = 480U;
        target.height = 640U;
        result = translate_xmp_image_geometry(source, target, options, &output);
        EXPECT_EQ(result.status,
                  MetadataGeometryTranslationStatus::TargetImageSpecMismatch);
        committed_width = active_exif_entry(output, "ifd0", 0x0100U);
        ASSERT_NE(committed_width, nullptr);
        EXPECT_EQ(committed_width->value.data.u64, 640U);

        MetaStore incomplete;
        const BlockId incomplete_block = incomplete.add_block(BlockInfo {});
        ASSERT_NE(incomplete_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_value(&incomplete, incomplete_block, kTiffNs,
                                "ImageWidth", make_u32(640U), EntryFlags::Dirty,
                                0U),
                  kInvalidEntryId);
        incomplete.finalize();
        target.width  = 640U;
        target.height = 480U;
        result = translate_xmp_image_geometry(incomplete, target, options,
                                              &output);
        EXPECT_EQ(result.status,
                  MetadataGeometryTranslationStatus::IncompleteSourceGroup);

        MetaStore duplicate;
        const BlockId duplicate_block = duplicate.add_block(BlockInfo {});
        ASSERT_NE(duplicate_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_value(&duplicate, duplicate_block, kExifNs,
                                "ExifImageWidth", make_u32(640U),
                                EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_value(&duplicate, duplicate_block, kExifNs,
                                "ExifImageWidth", make_u32(640U),
                                EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_value(&duplicate, duplicate_block, kExifNs,
                                "ExifImageHeight", make_u32(480U),
                                EntryFlags::Dirty, 2U),
                  kInvalidEntryId);
        duplicate.finalize();
        result = translate_xmp_image_geometry(duplicate, target, options,
                                              &output);
        EXPECT_EQ(result.status,
                  MetadataGeometryTranslationStatus::AmbiguousSource);

        MetaStore oversized;
        const BlockId oversized_block = oversized.add_block(BlockInfo {});
        ASSERT_NE(oversized_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&oversized, oversized_block, kExifNs,
                               "ExifImageWidth", std::string(33U, '1'),
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&oversized, oversized_block, kExifNs,
                               "ExifImageHeight", "480", EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        oversized.finalize();
        result = translate_xmp_image_geometry(oversized, target, options,
                                              &output);
        EXPECT_EQ(result.status,
                  MetadataGeometryTranslationStatus::ValueTooLong);
    }

    TEST(MetadataTranslation, GeometryOrientationCoversAllExifIndexes)
    {
        static constexpr std::string_view kTiffNs
            = "http://ns.adobe.com/tiff/1.0/";
        MetadataGeometryTranslationOptions options;
        options.dimensions_to_exif = false;
        for (uint16_t orientation = 1U; orientation <= 8U; ++orientation) {
            SCOPED_TRACE(orientation);
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            ASSERT_NE(block, kInvalidBlockId);
            ASSERT_NE(add_xmp_value(&source, block, kTiffNs, "Orientation",
                                    make_u16(orientation), EntryFlags::Dirty,
                                    0U),
                      kInvalidEntryId);
            source.finalize();

            TransferTargetImageSpec target;
            target.has_orientation = true;
            target.orientation     = orientation;
            MetaStore output;
            const MetadataGeometryTranslationResult result
                = translate_xmp_image_geometry(source, target, options,
                                               &output);
            ASSERT_EQ(result.status, MetadataGeometryTranslationStatus::Ok);
            const Entry* native = active_exif_entry(output, "ifd0", 0x0112U);
            ASSERT_NE(native, nullptr);
            EXPECT_EQ(native->value.elem_type, MetaElementType::U16);
            EXPECT_EQ(native->value.data.u64, orientation);
        }

        MetaStore invalid_source;
        const BlockId invalid_block = invalid_source.add_block(BlockInfo {});
        ASSERT_NE(invalid_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_value(&invalid_source, invalid_block, kTiffNs,
                                "Orientation", make_u16(9U), EntryFlags::Dirty,
                                0U),
                  kInvalidEntryId);
        invalid_source.finalize();
        TransferTargetImageSpec target;
        target.has_orientation = true;
        target.orientation     = 1U;
        MetaStore output;
        MetadataGeometryTranslationResult result
            = translate_xmp_image_geometry(invalid_source, target, options,
                                           &output);
        EXPECT_EQ(result.status,
                  MetadataGeometryTranslationStatus::ValueOutOfRange);

        target.orientation = 9U;
        result = translate_xmp_image_geometry(invalid_source, target, options,
                                              &output);
        EXPECT_EQ(result.status,
                  MetadataGeometryTranslationStatus::InvalidTargetImageSpec);
    }

    TEST(MetadataTranslation,
         GeometryConflictReplacementAndRemovalAreTransactional)
    {
        static constexpr std::string_view kExifNs
            = "http://ns.adobe.com/exif/1.0/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_value(&source, block, kExifNs, "ExifImageWidth",
                                make_u32(640U), EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_value(&source, block, kExifNs, "ExifImageHeight",
                                make_u32(480U), EntryFlags::None, 1U),
                  kInvalidEntryId);
        Entry old_width;
        old_width.key   = make_exif_tag_key(source.arena(), "ifd0", 0x0100U);
        old_width.value = make_u16(320U);
        old_width.origin.block          = block;
        old_width.origin.order_in_block = 2U;
        ASSERT_NE(source.add_entry(old_width), kInvalidEntryId);
        source.finalize();

        TransferTargetImageSpec target;
        target.has_dimensions = true;
        target.width          = 640U;
        target.height         = 480U;
        MetadataGeometryTranslationOptions options;
        options.orientation_to_exif = false;
        options.conflict_policy
            = MetadataGeometryTranslationConflictPolicy::PreserveExisting;
        MetaStore output;
        MetadataGeometryTranslationResult result
            = translate_xmp_image_geometry(source, target, options, &output);
        ASSERT_EQ(result.status, MetadataGeometryTranslationStatus::Ok);
        EXPECT_EQ(result.groups_preserved, 1U);
        EXPECT_EQ(active_exif_count(output, 0x0100U), 1U);
        EXPECT_EQ(active_exif_count(output, 0xA002U), 0U);

        options.conflict_policy
            = MetadataGeometryTranslationConflictPolicy::FailOnConflict;
        result = translate_xmp_image_geometry(source, target, options, &output);
        EXPECT_EQ(result.status,
                  MetadataGeometryTranslationStatus::NativeConflict);

        options.conflict_policy
            = MetadataGeometryTranslationConflictPolicy::ReplaceExisting;
        options.max_operations = 3U;
        result = translate_xmp_image_geometry(source, target, options, &output);
        EXPECT_EQ(result.status,
                  MetadataGeometryTranslationStatus::OperationLimitExceeded);

        options.max_operations = kMetadataGeometryTranslationMaxOperations;
        result = translate_xmp_image_geometry(source, target, options, &output);
        ASSERT_EQ(result.status, MetadataGeometryTranslationStatus::Ok);
        EXPECT_EQ(result.entries_added, 3U);
        EXPECT_EQ(result.entries_updated, 1U);
        const Entry* width = active_exif_entry(output, "ifd0", 0x0100U);
        ASSERT_NE(width, nullptr);
        EXPECT_EQ(width->value.elem_type, MetaElementType::U32);
        EXPECT_EQ(width->value.data.u64, 640U);

        MetaStore removal;
        const BlockId removal_block = removal.add_block(BlockInfo {});
        ASSERT_NE(removal_block, kInvalidBlockId);
        for (uint32_t i = 0U; i < 2U; ++i) {
            Entry deleted;
            deleted.key   = make_xmp_property_key(removal.arena(), kExifNs,
                                                i == 0U ? "ExifImageWidth"
                                                          : "ExifImageHeight");
            deleted.value = make_u32(i == 0U ? 640U : 480U);
            deleted.origin.block          = removal_block;
            deleted.origin.order_in_block = i;
            deleted.flags = EntryFlags::Dirty | EntryFlags::Deleted;
            ASSERT_NE(removal.add_entry(deleted), kInvalidEntryId);
        }
        static constexpr std::array<std::pair<std::string_view, uint16_t>, 4U>
            kNative = { std::pair { "ifd0", uint16_t { 0x0100U } },
                        std::pair { "ifd0", uint16_t { 0x0101U } },
                        std::pair { "exififd", uint16_t { 0xA002U } },
                        std::pair { "exififd", uint16_t { 0xA003U } } };
        for (uint32_t i = 0U; i < kNative.size(); ++i) {
            Entry native;
            native.key   = make_exif_tag_key(removal.arena(), kNative[i].first,
                                             kNative[i].second);
            native.value = make_u32((i & 1U) == 0U ? 640U : 480U);
            native.origin.block          = removal_block;
            native.origin.order_in_block = i + 2U;
            ASSERT_NE(removal.add_entry(native), kInvalidEntryId);
        }
        removal.finalize();
        target.has_dimensions = false;
        result = translate_xmp_image_geometry(removal, target, options,
                                              &output);
        ASSERT_EQ(result.status, MetadataGeometryTranslationStatus::Ok);
        EXPECT_EQ(result.entries_removed, 4U);
        EXPECT_EQ(active_exif_count(output, 0x0100U), 0U);
        EXPECT_EQ(active_exif_count(output, 0x0101U), 0U);
        EXPECT_EQ(active_exif_count(output, 0xA002U), 0U);
        EXPECT_EQ(active_exif_count(output, 0xA003U), 0U);
    }

    TEST(MetadataTranslation, TranslatesDescriptiveXmpToBoundedIptcGroups)
    {
        const std::array fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        "Evening frame"),
            make_metadata_creation_text(MetadataCreationFieldKind::Description,
                                        "City lights"),
            make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                        "Alice"),
            make_metadata_creation_text(MetadataCreationFieldKind::Creator,
                                        "Bob"),
            make_metadata_creation_text(MetadataCreationFieldKind::Keyword,
                                        "night"),
            make_metadata_creation_text(MetadataCreationFieldKind::Keyword,
                                        "street"),
            make_metadata_creation_text(MetadataCreationFieldKind::Copyright,
                                        "Copyright 2026"),
            make_metadata_creation_text(MetadataCreationFieldKind::Credit,
                                        "OpenMeta News"),
            make_metadata_creation_text(MetadataCreationFieldKind::Source,
                                        "Agency"),
        };
        MetadataCreationRequest request;
        request.fields = fields;
        MetaStore source;
        ASSERT_EQ(create_metadata(request, &source).status,
                  MetadataCreationStatus::Ok);

        MetaStore translated;
        const MetadataDescriptiveTranslationResult result
            = translate_xmp_descriptive_metadata(
                source, MetadataDescriptiveTranslationOptions {}, &translated);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, fields.size());
        EXPECT_EQ(result.groups_translated, 7U);
        EXPECT_EQ(result.entries_added, fields.size());
        EXPECT_FALSE(result.utf8_charset_added);
        EXPECT_TRUE(active_iptc_text(translated, 5U, "Evening frame"));
        EXPECT_TRUE(active_iptc_text(translated, 120U, "City lights"));
        EXPECT_TRUE(active_iptc_text(translated, 116U, "Copyright 2026"));
        EXPECT_TRUE(active_iptc_text(translated, 110U, "OpenMeta News"));
        EXPECT_TRUE(active_iptc_text(translated, 115U, "Agency"));
        EXPECT_EQ(active_iptc_values(translated, 80U),
                  (std::vector<std::string_view> { "Alice", "Bob" }));
        EXPECT_EQ(active_iptc_values(translated, 25U),
                  (std::vector<std::string_view> { "night", "street" }));
    }

    TEST(MetadataTranslation, DeclaresUtf8OnlyWhenExistingIptcIsSafe)
    {
        const std::array fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        "Night \xe6\x99\xaf"),
        };
        MetadataCreationRequest request;
        request.fields = fields;
        MetaStore source;
        ASSERT_EQ(create_metadata(request, &source).status,
                  MetadataCreationStatus::Ok);

        MetaStore translated;
        MetadataDescriptiveTranslationResult result
            = translate_xmp_descriptive_metadata(
                source, MetadataDescriptiveTranslationOptions {}, &translated);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_TRUE(result.utf8_charset_added);
        EXPECT_EQ(result.entries_added, 2U);
        EXPECT_TRUE(active_iptc_record_text(translated, 1U, 90U,
                                            std::string_view("\x1b%G", 3U)));
        EXPECT_TRUE(active_iptc_text(translated, 5U, "Night \xe6\x99\xaf"));

        MetaStore conflict;
        const BlockId block = conflict.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&conflict, block, kXmpNsDc,
                               "title[@xml:lang=x-default]",
                               "Night \xe6\x99\xaf", EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        const std::array<std::byte, 1U> legacy = { std::byte { 0xe9U } };
        Entry native;
        native.key                   = make_iptc_dataset_key(2U, 115U);
        native.value                 = make_bytes(conflict.arena(), legacy);
        native.origin.block          = block;
        native.origin.order_in_block = 1U;
        ASSERT_NE(conflict.add_entry(native), kInvalidEntryId);
        conflict.finalize();

        MetaStore unchanged            = std::move(translated);
        const size_t unchanged_entries = unchanged.entries().size();
        result                         = translate_xmp_descriptive_metadata(
            conflict, MetadataDescriptiveTranslationOptions {}, &unchanged);
        EXPECT_EQ(result.status,
                  MetadataDescriptiveTranslationStatus::NativeEncodingConflict);
        EXPECT_EQ(unchanged.entries().size(), unchanged_entries);
        EXPECT_TRUE(active_iptc_text(unchanged, 5U, "Night \xe6\x99\xaf"));
    }

    TEST(MetadataTranslation,
         DescriptiveConflictLimitsAndRemovalAreTransactional)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, kXmpNsDc,
                               "title[@xml:lang=x-default]", "Replacement",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_bytes(&source, block, 5U, "Old one", 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_bytes(&source, block, 5U, "Old two", 2U),
                  kInvalidEntryId);
        source.finalize();

        MetadataDescriptiveTranslationOptions options;
        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::PreserveExisting;
        MetaStore output;
        MetadataDescriptiveTranslationResult result
            = translate_xmp_descriptive_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.groups_preserved, 1U);
        EXPECT_TRUE(active_iptc_text(output, 5U, "Old one"));

        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict;
        result = translate_xmp_descriptive_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataDescriptiveTranslationStatus::NativeConflict);

        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
        options.max_operations = 1U;
        result = translate_xmp_descriptive_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataDescriptiveTranslationStatus::OperationLimitExceeded);

        options.max_operations = kMetadataDescriptiveTranslationMaxOperations;
        result = translate_xmp_descriptive_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(active_iptc_count(output, 5U), 1U);
        EXPECT_TRUE(active_iptc_text(output, 5U, "Replacement"));

        const MetadataEditingOperation remove = make_metadata_edit_remove(
            MetadataCreationFieldKind::Title);
        MetadataEditingRequest edit_request;
        edit_request.operations
            = std::span<const MetadataEditingOperation>(&remove, 1U);
        MetaStore edited;
        ASSERT_EQ(edit_metadata(output, edit_request, &edited).status,
                  MetadataEditingStatus::Ok);
        result = translate_xmp_descriptive_metadata(edited, options, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(active_iptc_count(output, 5U), 0U);
    }

    TEST(MetadataTranslation, RejectsAmbiguousOrOversizedDescriptiveSources)
    {
        MetaStore ambiguous;
        const BlockId block = ambiguous.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&ambiguous, block, kXmpNsDc, "subject[1]", "one",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&ambiguous, block, kXmpNsDc, "subject[1]", "two",
                               EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        ambiguous.finalize();
        MetaStore output;
        MetadataDescriptiveTranslationResult result
            = translate_xmp_descriptive_metadata(
                ambiguous, MetadataDescriptiveTranslationOptions {}, &output);
        EXPECT_EQ(result.status,
                  MetadataDescriptiveTranslationStatus::AmbiguousSource);
        EXPECT_EQ(result.failed_mapping,
                  MetadataDescriptiveTranslationMapping::DcSubject);

        const std::string long_title(65U, 'x');
        const std::array fields = {
            make_metadata_creation_text(MetadataCreationFieldKind::Title,
                                        long_title),
        };
        MetadataCreationRequest request;
        request.fields = fields;
        MetaStore oversized;
        ASSERT_EQ(create_metadata(request, &oversized).status,
                  MetadataCreationStatus::Ok);
        result = translate_xmp_descriptive_metadata(
            oversized, MetadataDescriptiveTranslationOptions {}, &output);
        EXPECT_EQ(result.status,
                  MetadataDescriptiveTranslationStatus::ValueTooLong);
        EXPECT_EQ(result.failed_mapping,
                  MetadataDescriptiveTranslationMapping::DcTitle);
    }

}  // namespace
}  // namespace openmeta
