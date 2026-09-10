// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_value_names.h"
#include "openmeta/meta_edit.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_translation.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

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

    struct IptcTextField final {
        std::string_view schema_ns;
        std::string_view path;
        uint16_t dataset;
        uint16_t max_bytes;
        std::string_view value;
    };

    static constexpr std::string_view kLocationPhotoshopNs
        = "http://ns.adobe.com/photoshop/1.0/";
    static constexpr std::string_view kLocationIptcNs
        = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
    static constexpr std::array<IptcTextField, 5U> kLocationFields = {
        IptcTextField { kLocationPhotoshopNs, "City", 90U, 32U, "Kyoto" },
        IptcTextField { kLocationIptcNs, "Location", 92U, 32U, "Garden" },
        IptcTextField { kLocationPhotoshopNs, "State", 95U, 32U, "Kyoto" },
        IptcTextField { kLocationPhotoshopNs, "Country", 101U, 64U, "Japan" },
        IptcTextField { kLocationIptcNs, "CountryCode", 100U, 3U, "JPN" },
    };

    TEST(MetadataTranslation, LocationWritesFiveExactGroupsAndOwnsProvenance)
    {
        MetaStore output;
        {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (uint32_t i = 0U; i < kLocationFields.size(); ++i) {
                const IptcTextField& field = kLocationFields[i];
                ASSERT_NE(add_xmp_text(&source, block, field.schema_ns,
                                       field.path, field.value,
                                       EntryFlags::Dirty, i, "location-wire"),
                          kInvalidEntryId);
            }
            source.finalize();
            const MetadataDescriptiveTranslationResult result
                = translate_xmp_location_metadata(
                    source, MetadataLocationTranslationOptions {}, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(result.source_properties, 5U);
            EXPECT_EQ(result.groups_translated, 5U);
            EXPECT_EQ(result.entries_added, 5U);
            EXPECT_FALSE(result.utf8_charset_added);
            EXPECT_EQ(source.entries().size(), 5U);
            for (const Entry& entry : output.entries()) {
                if (entry.key.kind != MetaKeyKind::IptcDataset) {
                    continue;
                }
                EXPECT_EQ(entry.origin.block, block);
                EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty));
                const auto wire = output.arena().span(
                    entry.origin.wire_type_name);
                EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                               wire.data()),
                                           wire.size()),
                          "location-wire");
            }
        }
        for (const IptcTextField& field : kLocationFields) {
            EXPECT_TRUE(active_iptc_text(output, field.dataset, field.value));
            EXPECT_EQ(active_iptc_count(output, field.dataset), 1U);
        }
        const size_t count = output.entries().size();
        const MetadataDescriptiveTranslationResult repeated
            = translate_xmp_location_metadata(
                output, MetadataLocationTranslationOptions {}, &output);
        ASSERT_EQ(repeated.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 5U);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(output.entries().size(), count);
    }

    TEST(MetadataTranslation, LocationHonorsDirtyModeSelectionAndExactPaths)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, block, kLocationPhotoshopNs, "City",
                               "Kyoto", EntryFlags::None, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kLocationIptcNs, "CountryCode",
                               "invalid", EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kLocationIptcNs,
                               "LocationCreated[1]/Iptc4xmpCore:City",
                               "Elsewhere", EntryFlags::Dirty, 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block,
                               "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
                               "LocationShown[1]/Iptc4xmpExt:City", "Elsewhere",
                               EntryFlags::Dirty, 3U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "urn:other", "City", "Elsewhere",
                               EntryFlags::Dirty, 4U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kLocationPhotoshopNs, "City[1]",
                               "Elsewhere", EntryFlags::Dirty, 5U),
                  kInvalidEntryId);
        source.finalize();
        MetadataLocationTranslationOptions options;
        options.country_code_to_iptc = false;
        MetaStore output;
        auto result = translate_xmp_location_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 0U);
        EXPECT_EQ(output.entries().size(), source.entries().size());
        options.source_mode = MetadataDescriptiveTranslationSourceMode::All;
        result = translate_xmp_location_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.groups_translated, 1U);
        EXPECT_TRUE(active_iptc_text(output, 90U, "Kyoto"));
        EXPECT_EQ(active_iptc_count(output, 100U), 0U);
        MetaStore descriptive;
        ASSERT_EQ(translate_xmp_descriptive_metadata(
                      source, MetadataDescriptiveTranslationOptions {},
                      &descriptive)
                      .status,
                  MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(active_iptc_count(descriptive, 90U), 0U);
    }

    TEST(MetadataTranslation, LocationConflictsReplaceDuplicatesTransactionally)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        add_xmp_text(&source, block, kLocationPhotoshopNs, "City", "New",
                     EntryFlags::Dirty, 0U);
        add_iptc_bytes(&source, block, 90U, "Old", 1U);
        add_iptc_bytes(&source, block, 90U, "Duplicate", 2U);
        add_iptc_bytes(&source, block, 101U, "Keep country", 3U);
        source.finalize();
        MetadataLocationTranslationOptions options;
        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::PreserveExisting;
        MetaStore output;
        auto result = translate_xmp_location_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.groups_preserved, 1U);
        EXPECT_EQ(active_iptc_count(output, 90U), 2U);
        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict;
        result = translate_xmp_location_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataDescriptiveTranslationStatus::NativeConflict);
        EXPECT_EQ(result.failed_mapping,
                  MetadataDescriptiveTranslationMapping::PhotoshopCity);
        EXPECT_TRUE(active_iptc_text(output, 90U, "Old"));
        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
        options.max_operations = 1U;
        result = translate_xmp_location_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataDescriptiveTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(active_iptc_count(output, 90U), 2U);
        options.max_operations = kMetadataDescriptiveTranslationMaxOperations;
        result = translate_xmp_location_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(active_iptc_count(output, 90U), 1U);
        EXPECT_TRUE(active_iptc_text(output, 90U, "New"));
        EXPECT_TRUE(active_iptc_text(output, 101U, "Keep country"));
    }

    TEST(MetadataTranslation,
         LocationRemovesAllSelectedNativeGroupsOnlyWithReplace)
    {
        for (const EntryFlags flags :
             { EntryFlags::Deleted, EntryFlags::Dirty | EntryFlags::Deleted }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (const IptcTextField& field : kLocationFields) {
                add_xmp_text(&source, block, field.schema_ns, field.path, {},
                             flags, 0U);
                add_iptc_bytes(&source, block, field.dataset, field.value, 1U);
                add_iptc_bytes(&source, block, field.dataset, field.value, 2U);
            }
            source.finalize();
            MetadataLocationTranslationOptions options;
            options.source_mode = MetadataDescriptiveTranslationSourceMode::All;
            MetaStore output;
            const bool dirty = any(flags, EntryFlags::Dirty);
            auto result      = translate_xmp_location_metadata(source, options,
                                                               &output);
            EXPECT_EQ(result.status,
                      dirty
                          ? MetadataDescriptiveTranslationStatus::NativeConflict
                          : MetadataDescriptiveTranslationStatus::Ok);
            options.conflict_policy
                = MetadataDescriptiveTranslationConflictPolicy::PreserveExisting;
            result = translate_xmp_location_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(active_iptc_count(output, 90U), 2U);
            options.conflict_policy
                = MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
            result = translate_xmp_location_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(result.entries_removed, dirty ? 10U : 0U);
            for (const IptcTextField& field : kLocationFields) {
                EXPECT_EQ(active_iptc_count(output, field.dataset),
                          dirty ? 0U : 2U);
            }
        }
    }

    TEST(MetadataTranslation, LocationRejectsDuplicateSourcesAndMalformedValues)
    {
        for (const bool same : { false, true }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "City", "One",
                         EntryFlags::None, 0U);
            const EntryId duplicate
                = add_xmp_text(&source, block, kLocationPhotoshopNs, "City",
                               same ? "One" : "Two", EntryFlags::Dirty, 1U);
            source.finalize();
            MetaStore output;
            const auto result = translate_xmp_location_metadata(source, {},
                                                                &output);
            EXPECT_EQ(result.status,
                      MetadataDescriptiveTranslationStatus::AmbiguousSource);
            EXPECT_EQ(result.failed_source_entry, duplicate);
            EXPECT_TRUE(output.entries().empty());
        }
        const std::array<std::string_view, 4U> invalid = {
            std::string_view {},
            std::string_view("bad\0value", 9U),
            std::string_view("\xc0\xaf", 2U),
            std::string_view("\xed\xa0\x80", 3U),
        };
        for (const std::string_view value : invalid) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "City", "Valid",
                         EntryFlags::Dirty, 0U);
            const EntryId invalid_id
                = add_xmp_text(&source, block, kLocationPhotoshopNs, "State",
                               value, EntryFlags::Dirty, 1U);
            source.finalize();
            MetaStore output;
            const BlockId output_block = output.add_block(BlockInfo {});
            add_iptc_bytes(&output, output_block, 90U, "Sentinel", 0U);
            output.finalize();
            const auto result = translate_xmp_location_metadata(source, {},
                                                                &output);
            EXPECT_EQ(result.status,
                      MetadataDescriptiveTranslationStatus::InvalidSourceValue);
            EXPECT_EQ(result.failed_source_entry, invalid_id);
            EXPECT_EQ(output.entries().size(), 1U);
            EXPECT_TRUE(active_iptc_text(output, 90U, "Sentinel"));
        }
        MetaStore typed;
        const BlockId block = typed.add_block(BlockInfo {});
        add_xmp_value(&typed, block, kLocationPhotoshopNs, "City",
                      make_u32(42U), EntryFlags::Dirty, 0U);
        typed.finalize();
        MetaStore output;
        EXPECT_EQ(translate_xmp_location_metadata(typed, {}, &output).status,
                  MetadataDescriptiveTranslationStatus::InvalidSourceValue);
    }

    TEST(MetadataTranslation,
         LocationEnforcesWireByteLimitsAndCountryCodeSyntax)
    {
        for (const IptcTextField& field : kLocationFields) {
            for (const bool overflow : { false, true }) {
                MetaStore source;
                const BlockId block = source.add_block(BlockInfo {});
                const std::string value(field.max_bytes + (overflow ? 1U : 0U),
                                        'A');
                add_xmp_text(&source, block, field.schema_ns, field.path, value,
                             EntryFlags::Dirty, 0U);
                source.finalize();
                MetaStore output;
                const auto result = translate_xmp_location_metadata(source, {},
                                                                    &output);
                EXPECT_EQ(result.status,
                          overflow
                              ? MetadataDescriptiveTranslationStatus::ValueTooLong
                              : MetadataDescriptiveTranslationStatus::Ok);
                if (!overflow) {
                    EXPECT_TRUE(active_iptc_text(output, field.dataset, value));
                }
            }
        }
        for (const std::string_view code :
             { "JP", "JPN", "jp", "JpN", "J", "J1", " J", "J P" }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationIptcNs, "CountryCode", code,
                         EntryFlags::Dirty, 0U);
            source.finalize();
            MetaStore output;
            const auto result = translate_xmp_location_metadata(source, {},
                                                                &output);
            EXPECT_EQ(
                result.status,
                code == "JP" || code == "JPN"
                    ? MetadataDescriptiveTranslationStatus::Ok
                    : MetadataDescriptiveTranslationStatus::InvalidSourceValue)
                << code;
        }
        MetaStore multibyte;
        const BlockId block     = multibyte.add_block(BlockInfo {});
        const std::string value = std::string(31U, 'A') + "\xc3\xa9";
        add_xmp_text(&multibyte, block, kLocationPhotoshopNs, "City", value,
                     EntryFlags::Dirty, 0U);
        multibyte.finalize();
        MetaStore output;
        EXPECT_EQ(translate_xmp_location_metadata(multibyte, {}, &output).status,
                  MetadataDescriptiveTranslationStatus::ValueTooLong);
    }

    TEST(MetadataTranslation, LocationCharsetPromotionPreservesUnownedIptc)
    {
        for (const bool legacy_non_ascii : { false, true }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "City",
                         "\xe4\xba\xac\xe9\x83\xbd", EntryFlags::Dirty, 0U);
            add_iptc_bytes(&source, block, 120U,
                           legacy_non_ascii ? "\xe9" : "Caption", 1U);
            source.finalize();
            MetaStore output;
            const auto result = translate_xmp_location_metadata(source, {},
                                                                &output);
            if (legacy_non_ascii) {
                EXPECT_EQ(
                    result.status,
                    MetadataDescriptiveTranslationStatus::NativeEncodingConflict);
                EXPECT_TRUE(output.entries().empty());
            } else {
                ASSERT_EQ(result.status,
                          MetadataDescriptiveTranslationStatus::Ok);
                EXPECT_TRUE(result.utf8_charset_added);
                EXPECT_EQ(result.entries_added, 2U);
                EXPECT_TRUE(
                    active_iptc_record_text(output, 1U, 90U,
                                            std::string_view("\x1b%G", 3U)));
                EXPECT_TRUE(active_iptc_text(output, 120U, "Caption"));
            }
        }
        for (const unsigned charset_count : { 0U, 1U, 2U }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "City",
                         "\xc3\xa9", EntryFlags::Dirty, 0U);
            add_iptc_bytes(&source, block, 90U, "\xe9", 1U);
            for (unsigned i = 0U; i < charset_count; ++i) {
                Entry charset;
                charset.key          = make_iptc_dataset_key(1U, 90U);
                charset.value        = make_text(source.arena(),
                                                 std::string_view("\x1b%G", 3U),
                                                 TextEncoding::Ascii);
                charset.origin.block = block;
                source.add_entry(charset);
            }
            source.finalize();
            MetadataLocationTranslationOptions options;
            options.conflict_policy
                = MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
            MetaStore output;
            const auto result = translate_xmp_location_metadata(source, options,
                                                                &output);
            EXPECT_EQ(
                result.status,
                charset_count == 2U
                    ? MetadataDescriptiveTranslationStatus::NativeEncodingConflict
                    : MetadataDescriptiveTranslationStatus::Ok);
            if (charset_count < 2U) {
                EXPECT_TRUE(active_iptc_text(output, 90U, "\xc3\xa9"));
                EXPECT_EQ(result.utf8_charset_added, charset_count == 0U);
            }
        }
    }

    TEST(MetadataTranslation,
         LocationRejectsInvalidOptionsAndResourceExhaustion)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        add_xmp_text(&source, block, kLocationPhotoshopNs, "City", "City",
                     EntryFlags::Dirty, 0U);
        add_xmp_text(&source, block, kLocationPhotoshopNs, "State", "State",
                     EntryFlags::Dirty, 1U);
        MetaStore output;
        EXPECT_EQ(translate_xmp_location_metadata(source, {}, nullptr).status,
                  MetadataDescriptiveTranslationStatus::NullOutput);
        EXPECT_EQ(translate_xmp_location_metadata(source, {}, &output).status,
                  MetadataDescriptiveTranslationStatus::SourceNotFinalized);
        source.finalize();
        MetadataLocationTranslationOptions options;
        options.max_source_properties = 1U;
        EXPECT_EQ(
            translate_xmp_location_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::SourceLimitExceeded);
        options                   = {};
        options.max_added_entries = 1U;
        EXPECT_EQ(
            translate_xmp_location_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::EntryLimitExceeded);
        options                      = {};
        options.max_total_text_bytes = 8U;
        EXPECT_EQ(
            translate_xmp_location_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::SourceLimitExceeded);
        options                   = {};
        options.max_added_entries = 7U;
        EXPECT_EQ(
            translate_xmp_location_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::InvalidOptions);
        options = {};
        options.source_mode
            = static_cast<MetadataDescriptiveTranslationSourceMode>(255U);
        EXPECT_EQ(
            translate_xmp_location_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::InvalidOptions);
        options = {};
        options.conflict_policy
            = static_cast<MetadataDescriptiveTranslationConflictPolicy>(255U);
        EXPECT_EQ(
            translate_xmp_location_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::InvalidOptions);
        options                            = {};
        options.city_to_iptc               = options.sublocation_to_iptc
            = options.state_to_iptc        = options.country_to_iptc
            = options.country_code_to_iptc = false;
        EXPECT_EQ(
            translate_xmp_location_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::InvalidOptions);
        EXPECT_TRUE(output.entries().empty());
    }

    TEST(MetadataTranslation,
         LocationAccountsForCharsetAndRejectsIncompatibleEncoding)
    {
        for (const std::string_view charset_value :
             { "", "\x1b%G", "\x1b%@" }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (const IptcTextField& field : kLocationFields) {
                add_xmp_text(&source, block, field.schema_ns, field.path,
                             field.dataset == 90U ? "\xc3\xa9" : field.value,
                             EntryFlags::Dirty, 0U);
            }
            if (!charset_value.empty()) {
                Entry charset;
                charset.key          = make_iptc_dataset_key(1U, 90U);
                charset.value        = make_text(source.arena(), charset_value,
                                                 TextEncoding::Ascii);
                charset.origin.block = block;
                source.add_entry(charset);
            }
            source.finalize();
            MetaStore output;
            MetadataLocationTranslationOptions options;
            options.max_added_entries = 5U;
            const auto result = translate_xmp_location_metadata(source, options,
                                                                &output);
            EXPECT_EQ(
                result.status,
                charset_value.empty()
                    ? MetadataDescriptiveTranslationStatus::EntryLimitExceeded
                : charset_value == "\x1b%G"
                    ? MetadataDescriptiveTranslationStatus::Ok
                    : MetadataDescriptiveTranslationStatus::
                          NativeEncodingConflict);
            if (charset_value.empty()) {
                EXPECT_TRUE(output.entries().empty());
                options.max_added_entries = 6U;
                const auto accepted
                    = translate_xmp_location_metadata(source, options, &output);
                ASSERT_EQ(accepted.status,
                          MetadataDescriptiveTranslationStatus::Ok);
                EXPECT_EQ(accepted.entries_added, 6U);
                EXPECT_TRUE(accepted.utf8_charset_added);
            }
        }
    }

    static constexpr std::array<IptcTextField, 3U> kEditorialFields = {
        IptcTextField { kLocationPhotoshopNs, "Headline", 105U, 256U,
                        "Garden opens" },
        IptcTextField { kLocationPhotoshopNs, "Instructions", 40U, 256U,
                        "Contact the editor" },
        IptcTextField { kLocationPhotoshopNs, "TransmissionReference", 103U,
                        32U, "JOB-42" },
    };

    TEST(MetadataTranslation,
         EditorialSelectionRequiresExactPathsAndDirtySources)
    {
        for (size_t selected = 0U; selected < kEditorialFields.size();
             ++selected) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (const IptcTextField& field : kEditorialFields) {
                add_xmp_text(&source, block, field.schema_ns, field.path,
                             field.value, EntryFlags::None, 0U);
                add_xmp_text(&source, block, "urn:other", field.path,
                             "Wrong namespace", EntryFlags::Dirty, 0U);
                add_xmp_text(&source, block, field.schema_ns,
                             std::string(field.path) + "[1]", "Indexed",
                             EntryFlags::Dirty, 0U);
            }
            add_xmp_text(&source, block, kLocationPhotoshopNs, "headline",
                         "Wrong case", EntryFlags::Dirty, 0U);
            source.finalize();
            MetadataEditorialTranslationOptions options;
            options.headline_to_iptc               = selected == 0U;
            options.instructions_to_iptc           = selected == 1U;
            options.transmission_reference_to_iptc = selected == 2U;
            MetaStore output;
            auto result = translate_xmp_editorial_metadata(source, options,
                                                           &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(result.source_properties, 0U);
            EXPECT_EQ(output.entries().size(), source.entries().size());
            options.source_mode = MetadataDescriptiveTranslationSourceMode::All;
            result = translate_xmp_editorial_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(result.source_properties, 1U);
            EXPECT_EQ(result.groups_translated, 1U);
            for (size_t i = 0U; i < kEditorialFields.size(); ++i) {
                EXPECT_EQ(active_iptc_count(output, kEditorialFields[i].dataset),
                          i == selected ? 1U : 0U);
            }
            MetadataDescriptiveTranslationOptions descriptive;
            descriptive.source_mode
                = MetadataDescriptiveTranslationSourceMode::All;
            ASSERT_EQ(translate_xmp_descriptive_metadata(source, descriptive,
                                                         &output)
                          .status,
                      MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(output.entries().size(), source.entries().size());
            MetadataLocationTranslationOptions location;
            location.source_mode = MetadataDescriptiveTranslationSourceMode::All;
            ASSERT_EQ(translate_xmp_location_metadata(source, location, &output)
                          .status,
                      MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(output.entries().size(), source.entries().size());
        }
    }

    TEST(MetadataTranslation, EditorialEnforcesEncodedByteLimitsAtomically)
    {
        for (const IptcTextField& field : kEditorialFields) {
            for (const bool overflow : { false, true }) {
                for (const bool multibyte : { false, true }) {
                    SCOPED_TRACE(field.path);
                    MetaStore source;
                    const BlockId block = source.add_block(BlockInfo {});
                    std::string value(field.max_bytes - (multibyte ? 2U : 0U)
                                          + (overflow ? 1U : 0U),
                                      'A');
                    if (multibyte) {
                        value += "\xc3\xa9";
                    }
                    const EntryId id = add_xmp_text(&source, block,
                                                    field.schema_ns, field.path,
                                                    value, EntryFlags::Dirty,
                                                    0U);
                    for (const IptcTextField& other : kEditorialFields) {
                        if (other.dataset != field.dataset) {
                            add_xmp_text(&source, block, other.schema_ns,
                                         other.path, other.value,
                                         EntryFlags::Dirty, 1U);
                        }
                    }
                    source.finalize();
                    MetaStore output;
                    const BlockId sentinel = output.add_block(BlockInfo {});
                    add_iptc_bytes(&output, sentinel, 105U, "Sentinel", 0U);
                    output.finalize();
                    const auto result
                        = translate_xmp_editorial_metadata(source, {}, &output);
                    EXPECT_EQ(
                        result.status,
                        overflow
                            ? MetadataDescriptiveTranslationStatus::ValueTooLong
                            : MetadataDescriptiveTranslationStatus::Ok);
                    if (overflow) {
                        EXPECT_EQ(result.failed_source_entry, id);
                        EXPECT_EQ(output.entries().size(), 1U);
                        EXPECT_TRUE(active_iptc_text(output, 105U, "Sentinel"));
                    } else {
                        EXPECT_EQ(result.groups_translated, 3U);
                        EXPECT_TRUE(
                            active_iptc_text(output, field.dataset, value));
                        EXPECT_EQ(result.utf8_charset_added, multibyte);
                    }
                }
            }
        }
    }

    TEST(MetadataTranslation, EditorialWritesThreeExactGroupsAndOwnsProvenance)
    {
        MetaStore output;
        {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (uint32_t i = 0U; i < kEditorialFields.size(); ++i) {
                const IptcTextField& field = kEditorialFields[i];
                ASSERT_NE(add_xmp_text(&source, block, field.schema_ns,
                                       field.path, field.value,
                                       EntryFlags::Dirty, i, "editorial-wire"),
                          kInvalidEntryId);
            }
            source.finalize();
            const MetadataDescriptiveTranslationResult result
                = translate_xmp_editorial_metadata(
                    source, MetadataEditorialTranslationOptions {}, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(result.source_properties, 3U);
            EXPECT_EQ(result.groups_translated, 3U);
            EXPECT_EQ(result.entries_added, 3U);
            EXPECT_FALSE(result.utf8_charset_added);
            EXPECT_EQ(source.entries().size(), 3U);
            for (const Entry& entry : output.entries()) {
                if (entry.key.kind != MetaKeyKind::IptcDataset) {
                    continue;
                }
                EXPECT_EQ(entry.origin.block, block);
                EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty));
                const auto wire = output.arena().span(
                    entry.origin.wire_type_name);
                EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                               wire.data()),
                                           wire.size()),
                          "editorial-wire");
            }
        }
        for (const IptcTextField& field : kEditorialFields) {
            EXPECT_TRUE(active_iptc_text(output, field.dataset, field.value));
            EXPECT_EQ(active_iptc_count(output, field.dataset), 1U);
        }
        const size_t count = output.entries().size();
        const MetadataDescriptiveTranslationResult repeated
            = translate_xmp_editorial_metadata(
                output, MetadataEditorialTranslationOptions {}, &output);
        ASSERT_EQ(repeated.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 3U);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(output.entries().size(), count);
    }

    TEST(MetadataTranslation,
         EditorialConflictsReplaceDuplicatesTransactionally)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        add_xmp_text(&source, block, kLocationPhotoshopNs, "Headline", "New",
                     EntryFlags::Dirty, 0U);
        add_iptc_bytes(&source, block, 105U, "Old", 1U);
        add_iptc_bytes(&source, block, 105U, "Duplicate", 2U);
        add_iptc_bytes(&source, block, 101U, "Keep country", 3U);
        source.finalize();
        MetadataEditorialTranslationOptions options;
        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::PreserveExisting;
        MetaStore output;
        auto result = translate_xmp_editorial_metadata(source, options,
                                                       &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.groups_preserved, 1U);
        EXPECT_EQ(active_iptc_count(output, 105U), 2U);
        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict;
        result = translate_xmp_editorial_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataDescriptiveTranslationStatus::NativeConflict);
        EXPECT_EQ(result.failed_mapping,
                  MetadataDescriptiveTranslationMapping::PhotoshopHeadline);
        EXPECT_TRUE(active_iptc_text(output, 105U, "Old"));
        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
        options.max_operations = 1U;
        result = translate_xmp_editorial_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataDescriptiveTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(active_iptc_count(output, 105U), 2U);
        options.max_operations = kMetadataDescriptiveTranslationMaxOperations;
        result = translate_xmp_editorial_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(active_iptc_count(output, 105U), 1U);
        EXPECT_TRUE(active_iptc_text(output, 105U, "New"));
        EXPECT_TRUE(active_iptc_text(output, 101U, "Keep country"));
    }

    TEST(MetadataTranslation,
         EditorialRemovesAllSelectedNativeGroupsOnlyWithReplace)
    {
        for (const EntryFlags flags :
             { EntryFlags::Deleted, EntryFlags::Dirty | EntryFlags::Deleted }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (const IptcTextField& field : kEditorialFields) {
                add_xmp_text(&source, block, field.schema_ns, field.path, {},
                             flags, 0U);
                add_iptc_bytes(&source, block, field.dataset, field.value, 1U);
                add_iptc_bytes(&source, block, field.dataset, field.value, 2U);
            }
            source.finalize();
            MetadataEditorialTranslationOptions options;
            options.source_mode = MetadataDescriptiveTranslationSourceMode::All;
            MetaStore output;
            const bool dirty = any(flags, EntryFlags::Dirty);
            auto result      = translate_xmp_editorial_metadata(source, options,
                                                                &output);
            EXPECT_EQ(result.status,
                      dirty
                          ? MetadataDescriptiveTranslationStatus::NativeConflict
                          : MetadataDescriptiveTranslationStatus::Ok);
            options.conflict_policy
                = MetadataDescriptiveTranslationConflictPolicy::PreserveExisting;
            result = translate_xmp_editorial_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(active_iptc_count(output, 103U), 2U);
            options.conflict_policy
                = MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
            result = translate_xmp_editorial_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(result.entries_removed, dirty ? 6U : 0U);
            for (const IptcTextField& field : kEditorialFields) {
                EXPECT_EQ(active_iptc_count(output, field.dataset),
                          dirty ? 0U : 2U);
            }
        }
    }

    TEST(MetadataTranslation,
         EditorialRejectsDuplicateSourcesAndMalformedValues)
    {
        for (const bool same : { false, true }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "Headline",
                         "One", EntryFlags::None, 0U);
            const EntryId duplicate
                = add_xmp_text(&source, block, kLocationPhotoshopNs, "Headline",
                               same ? "One" : "Two", EntryFlags::Dirty, 1U);
            source.finalize();
            MetaStore output;
            const auto result = translate_xmp_editorial_metadata(source, {},
                                                                 &output);
            EXPECT_EQ(result.status,
                      MetadataDescriptiveTranslationStatus::AmbiguousSource);
            EXPECT_EQ(result.failed_source_entry, duplicate);
            EXPECT_TRUE(output.entries().empty());
        }
        const std::array<std::string_view, 4U> invalid = {
            std::string_view {},
            std::string_view("bad\0value", 9U),
            std::string_view("\xc0\xaf", 2U),
            std::string_view("\xed\xa0\x80", 3U),
        };
        for (const std::string_view value : invalid) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "Headline",
                         "Valid", EntryFlags::Dirty, 0U);
            const EntryId invalid_id
                = add_xmp_text(&source, block, kLocationPhotoshopNs,
                               "Instructions", value, EntryFlags::Dirty, 1U);
            source.finalize();
            MetaStore output;
            const BlockId output_block = output.add_block(BlockInfo {});
            add_iptc_bytes(&output, output_block, 105U, "Sentinel", 0U);
            output.finalize();
            const auto result = translate_xmp_editorial_metadata(source, {},
                                                                 &output);
            EXPECT_EQ(result.status,
                      MetadataDescriptiveTranslationStatus::InvalidSourceValue);
            EXPECT_EQ(result.failed_source_entry, invalid_id);
            EXPECT_EQ(output.entries().size(), 1U);
            EXPECT_TRUE(active_iptc_text(output, 105U, "Sentinel"));
        }
        MetaStore typed;
        const BlockId block = typed.add_block(BlockInfo {});
        add_xmp_value(&typed, block, kLocationPhotoshopNs, "Headline",
                      make_u32(42U), EntryFlags::Dirty, 0U);
        typed.finalize();
        MetaStore output;
        EXPECT_EQ(translate_xmp_editorial_metadata(typed, {}, &output).status,
                  MetadataDescriptiveTranslationStatus::InvalidSourceValue);
    }

    TEST(MetadataTranslation, EditorialCharsetPromotionPreservesUnownedIptc)
    {
        for (const bool legacy_non_ascii : { false, true }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "Headline",
                         "\xe4\xba\xac\xe9\x83\xbd", EntryFlags::Dirty, 0U);
            add_iptc_bytes(&source, block, 120U,
                           legacy_non_ascii ? "\xe9" : "Caption", 1U);
            source.finalize();
            MetaStore output;
            const auto result = translate_xmp_editorial_metadata(source, {},
                                                                 &output);
            if (legacy_non_ascii) {
                EXPECT_EQ(
                    result.status,
                    MetadataDescriptiveTranslationStatus::NativeEncodingConflict);
                EXPECT_TRUE(output.entries().empty());
            } else {
                ASSERT_EQ(result.status,
                          MetadataDescriptiveTranslationStatus::Ok);
                EXPECT_TRUE(result.utf8_charset_added);
                EXPECT_EQ(result.entries_added, 2U);
                EXPECT_TRUE(
                    active_iptc_record_text(output, 1U, 90U,
                                            std::string_view("\x1b%G", 3U)));
                EXPECT_TRUE(active_iptc_text(output, 120U, "Caption"));
            }
        }
        for (const unsigned charset_count : { 0U, 1U, 2U }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "Headline",
                         "\xc3\xa9", EntryFlags::Dirty, 0U);
            add_iptc_bytes(&source, block, 105U, "\xe9", 1U);
            for (unsigned i = 0U; i < charset_count; ++i) {
                Entry charset;
                charset.key          = make_iptc_dataset_key(1U, 90U);
                charset.value        = make_text(source.arena(),
                                                 std::string_view("\x1b%G", 3U),
                                                 TextEncoding::Ascii);
                charset.origin.block = block;
                source.add_entry(charset);
            }
            source.finalize();
            MetadataEditorialTranslationOptions options;
            options.conflict_policy
                = MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
            MetaStore output;
            const auto result
                = translate_xmp_editorial_metadata(source, options, &output);
            EXPECT_EQ(
                result.status,
                charset_count == 2U
                    ? MetadataDescriptiveTranslationStatus::NativeEncodingConflict
                    : MetadataDescriptiveTranslationStatus::Ok);
            if (charset_count < 2U) {
                EXPECT_TRUE(active_iptc_text(output, 105U, "\xc3\xa9"));
                EXPECT_EQ(result.utf8_charset_added, charset_count == 0U);
            }
        }
    }

    TEST(MetadataTranslation,
         EditorialRejectsInvalidOptionsAndResourceExhaustion)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        add_xmp_text(&source, block, kLocationPhotoshopNs, "Headline",
                     "Headline", EntryFlags::Dirty, 0U);
        add_xmp_text(&source, block, kLocationPhotoshopNs, "Instructions",
                     "Instructions", EntryFlags::Dirty, 1U);
        MetaStore output;
        EXPECT_EQ(translate_xmp_editorial_metadata(source, {}, nullptr).status,
                  MetadataDescriptiveTranslationStatus::NullOutput);
        EXPECT_EQ(translate_xmp_editorial_metadata(source, {}, &output).status,
                  MetadataDescriptiveTranslationStatus::SourceNotFinalized);
        source.finalize();
        MetadataEditorialTranslationOptions options;
        options.max_source_properties = 1U;
        EXPECT_EQ(
            translate_xmp_editorial_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::SourceLimitExceeded);
        options                   = {};
        options.max_added_entries = 1U;
        EXPECT_EQ(
            translate_xmp_editorial_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::EntryLimitExceeded);
        options                      = {};
        options.max_total_text_bytes = 8U;
        EXPECT_EQ(
            translate_xmp_editorial_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::SourceLimitExceeded);
        options                   = {};
        options.max_added_entries = 5U;
        EXPECT_EQ(
            translate_xmp_editorial_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::InvalidOptions);
        options = {};
        options.source_mode
            = static_cast<MetadataDescriptiveTranslationSourceMode>(255U);
        EXPECT_EQ(
            translate_xmp_editorial_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::InvalidOptions);
        options = {};
        options.conflict_policy
            = static_cast<MetadataDescriptiveTranslationConflictPolicy>(255U);
        EXPECT_EQ(
            translate_xmp_editorial_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::InvalidOptions);
        options                  = {};
        options.headline_to_iptc = options.instructions_to_iptc
            = options.transmission_reference_to_iptc = false;
        EXPECT_EQ(
            translate_xmp_editorial_metadata(source, options, &output).status,
            MetadataDescriptiveTranslationStatus::InvalidOptions);
        EXPECT_TRUE(output.entries().empty());
    }

    TEST(MetadataTranslation,
         EditorialAccountsForCharsetAndRejectsIncompatibleEncoding)
    {
        for (const std::string_view charset_value :
             { "", "\x1b%G", "\x1b%@" }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (const IptcTextField& field : kEditorialFields) {
                add_xmp_text(&source, block, field.schema_ns, field.path,
                             field.dataset == 105U ? "\xc3\xa9" : field.value,
                             EntryFlags::Dirty, 0U);
            }
            if (!charset_value.empty()) {
                Entry charset;
                charset.key          = make_iptc_dataset_key(1U, 90U);
                charset.value        = make_text(source.arena(), charset_value,
                                                 TextEncoding::Ascii);
                charset.origin.block = block;
                source.add_entry(charset);
            }
            source.finalize();
            MetaStore output;
            MetadataEditorialTranslationOptions options;
            options.max_added_entries = 3U;
            const auto result
                = translate_xmp_editorial_metadata(source, options, &output);
            EXPECT_EQ(
                result.status,
                charset_value.empty()
                    ? MetadataDescriptiveTranslationStatus::EntryLimitExceeded
                : charset_value == "\x1b%G"
                    ? MetadataDescriptiveTranslationStatus::Ok
                    : MetadataDescriptiveTranslationStatus::
                          NativeEncodingConflict);
            if (charset_value.empty()) {
                EXPECT_TRUE(output.entries().empty());
                options.max_added_entries = 4U;
                const auto accepted = translate_xmp_editorial_metadata(source,
                                                                       options,
                                                                       &output);
                ASSERT_EQ(accepted.status,
                          MetadataDescriptiveTranslationStatus::Ok);
                EXPECT_EQ(accepted.entries_added, 4U);
                EXPECT_TRUE(accepted.utf8_charset_added);
            }
        }
    }

    struct IptcBatchField final {
        IptcTextField text;
        bool MetadataIptcTranslationOptions::* enabled;
    };

    static constexpr std::array<IptcBatchField, 20U> kIptcBatchFields = {
        IptcBatchField {
            { kXmpNsDc, "title[@xml:lang=x-default]", 5U, 64U, "Title" },
            &MetadataIptcTranslationOptions::title_to_iptc_object_name },
        IptcBatchField {
            { kXmpNsDc, "description[@xml:lang=x-default]", 120U, 2000U,
              "Caption" },
            &MetadataIptcTranslationOptions::description_to_iptc_caption },
        IptcBatchField {
            { kXmpNsDc, "creator[1]", 80U, 32U, "Alice" },
            &MetadataIptcTranslationOptions::creators_to_iptc_bylines },
        IptcBatchField {
            { kXmpNsDc, "subject[1]", 25U, 64U, "Garden" },
            &MetadataIptcTranslationOptions::keywords_to_iptc_keywords },
        IptcBatchField {
            { kXmpNsDc, "rights[@xml:lang=x-default]", 116U, 128U, "Copyright" },
            &MetadataIptcTranslationOptions::copyright_to_iptc_copyright },
        IptcBatchField {
            { kLocationPhotoshopNs, "Credit", 110U, 32U, "Credit" },
            &MetadataIptcTranslationOptions::credit_to_iptc_credit },
        IptcBatchField {
            { kLocationPhotoshopNs, "Source", 115U, 32U, "Source" },
            &MetadataIptcTranslationOptions::source_to_iptc_source },
        IptcBatchField { { kLocationPhotoshopNs, "City", 90U, 32U, "Kyoto" },
                         &MetadataIptcTranslationOptions::city_to_iptc },
        IptcBatchField { { kLocationIptcNs, "Location", 92U, 32U, "Garden" },
                         &MetadataIptcTranslationOptions::sublocation_to_iptc },
        IptcBatchField { { kLocationPhotoshopNs, "State", 95U, 32U, "Kyoto" },
                         &MetadataIptcTranslationOptions::state_to_iptc },
        IptcBatchField { { kLocationPhotoshopNs, "Country", 101U, 64U, "Japan" },
                         &MetadataIptcTranslationOptions::country_to_iptc },
        IptcBatchField { { kLocationIptcNs, "CountryCode", 100U, 3U, "JP" },
                         &MetadataIptcTranslationOptions::country_code_to_iptc },
        IptcBatchField {
            { kLocationPhotoshopNs, "Headline", 105U, 256U, "Garden opens" },
            &MetadataIptcTranslationOptions::headline_to_iptc },
        IptcBatchField { { kLocationPhotoshopNs, "Instructions", 40U, 256U,
                           "Contact editor" },
                         &MetadataIptcTranslationOptions::instructions_to_iptc },
        IptcBatchField {
            { kLocationPhotoshopNs, "TransmissionReference", 103U, 32U,
              "JOB-42" },
            &MetadataIptcTranslationOptions::transmission_reference_to_iptc },
        IptcBatchField {
            { kLocationPhotoshopNs, "AuthorsPosition", 85U, 32U,
              "Photographer" },
            &MetadataIptcTranslationOptions::authors_position_to_iptc },
        IptcBatchField {
            { kLocationPhotoshopNs, "CaptionWriter", 122U, 32U, "Editor" },
            &MetadataIptcTranslationOptions::caption_writer_to_iptc },
        IptcBatchField { { kLocationPhotoshopNs, "Category", 15U, 3U, "ART" },
                         &MetadataIptcTranslationOptions::category_to_iptc },
        IptcBatchField {
            { kLocationPhotoshopNs, "SupplementalCategories[1]", 20U, 32U,
              "Painting" },
            &MetadataIptcTranslationOptions::supplemental_categories_to_iptc },
        IptcBatchField { { kLocationPhotoshopNs, "Urgency", 10U, 1U, "5" },
                         &MetadataIptcTranslationOptions::urgency_to_iptc },
    };

    static MetadataIptcTranslationOptions no_iptc_mappings()
    {
        MetadataIptcTranslationOptions options;
        for (const IptcBatchField& field : kIptcBatchFields) {
            options.*field.enabled = false;
        }
        return options;
    }

    TEST(MetadataTranslation, IptcCombinedWritesTwentyGroupsAndOwnsProvenance)
    {
        MetaStore output;
        {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (const IptcBatchField& field : kIptcBatchFields) {
                const IptcTextField& f = field.text;
                ASSERT_NE(add_xmp_text(&source, block, f.schema_ns, f.path,
                                       f.value, EntryFlags::Dirty, 0U,
                                       "batch-wire"),
                          kInvalidEntryId);
            }
            source.finalize();
            const auto result = translate_xmp_iptc_metadata(source, {},
                                                            &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(result.groups_translated, 20U);
            EXPECT_EQ(result.source_properties, 20U);
            EXPECT_EQ(result.entries_added, 20U);
            EXPECT_EQ(source.entries().size(), 20U);
            MetaStore subgroup;
            EXPECT_EQ(translate_xmp_descriptive_metadata(source, {}, &subgroup)
                          .groups_translated,
                      7U);
            EXPECT_EQ(translate_xmp_location_metadata(source, {}, &subgroup)
                          .groups_translated,
                      5U);
            EXPECT_EQ(translate_xmp_editorial_metadata(source, {}, &subgroup)
                          .groups_translated,
                      3U);
            for (const Entry& entry : output.entries()) {
                if (entry.key.kind != MetaKeyKind::IptcDataset) {
                    continue;
                }
                EXPECT_EQ(entry.origin.block, block);
                EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty));
                const auto wire = output.arena().span(
                    entry.origin.wire_type_name);
                EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                               wire.data()),
                                           wire.size()),
                          "batch-wire");
            }
        }
        for (const IptcBatchField& field : kIptcBatchFields) {
            EXPECT_TRUE(
                active_iptc_text(output, field.text.dataset, field.text.value));
            EXPECT_EQ(active_iptc_count(output, field.text.dataset), 1U);
        }
        const size_t count = output.entries().size();
        const auto result  = translate_xmp_iptc_metadata(output, {}, &output);
        EXPECT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.groups_unchanged, 20U);
        EXPECT_EQ(output.entries().size(), count);
    }

    TEST(MetadataTranslation, IptcCombinedFlagsAndSourceModesAreIndependent)
    {
        for (const IptcBatchField& selected : kIptcBatchFields) {
            SCOPED_TRACE(selected.text.path);
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (const IptcBatchField& field : kIptcBatchFields) {
                const IptcTextField& f = field.text;
                add_xmp_text(&source, block, f.schema_ns, f.path,
                             f.dataset == selected.text.dataset ? f.value : "",
                             EntryFlags::None, 0U);
            }
            add_xmp_text(&source, block, "urn:wrong", selected.text.path,
                         "Wrong", EntryFlags::Dirty, 0U);
            add_xmp_text(&source, block, selected.text.schema_ns,
                         std::string(selected.text.path) + "/qualifier",
                         "Wrong", EntryFlags::Dirty, 0U);
            source.finalize();
            auto options              = no_iptc_mappings();
            options.*selected.enabled = true;
            MetaStore output;
            auto result = translate_xmp_iptc_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(result.entries_added, 0U);
            options.source_mode = MetadataDescriptiveTranslationSourceMode::All;
            result = translate_xmp_iptc_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(result.groups_translated, 1U);
            EXPECT_EQ(result.entries_added, 1U);
            EXPECT_TRUE(active_iptc_text(output, selected.text.dataset,
                                         selected.text.value));
        }
    }

    TEST(MetadataTranslation,
         IptcCombinedConflictsAndCharsetAreAtomicAcrossGroups)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        for (const IptcBatchField& field : kIptcBatchFields) {
            const IptcTextField& f = field.text;
            add_xmp_text(&source, block, f.schema_ns, f.path,
                         f.dataset == 85U ? "Photographe"
                                            "\xc3\xa9"
                                          : f.value,
                         EntryFlags::Dirty, 0U);
            add_iptc_bytes(&source, block, f.dataset,
                           f.dataset == 120U ? "\xe9" : "OLD", 1U);
            add_iptc_bytes(&source, block, f.dataset, "Duplicate", 2U);
        }
        add_iptc_bytes(&source, block, 7U, "Keep status", 3U);
        source.finalize();
        MetaStore output;
        const BlockId sentinel = output.add_block(BlockInfo {});
        add_iptc_bytes(&output, sentinel, 5U, "Sentinel", 0U);
        output.finalize();
        MetadataIptcTranslationOptions options;
        EXPECT_EQ(translate_xmp_iptc_metadata(source, options, &output).status,
                  MetadataDescriptiveTranslationStatus::NativeConflict);
        EXPECT_TRUE(active_iptc_text(output, 5U, "Sentinel"));
        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::PreserveExisting;
        auto result = translate_xmp_iptc_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.groups_preserved, 20U);
        EXPECT_FALSE(result.utf8_charset_added);
        EXPECT_EQ(active_iptc_count(output, 85U), 2U);
        options.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
        options.max_operations = 40U;
        result = translate_xmp_iptc_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataDescriptiveTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(active_iptc_count(output, 85U), 2U);
        options.max_operations = kMetadataDescriptiveTranslationMaxOperations;
        result = translate_xmp_iptc_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.entries_updated, 20U);
        EXPECT_EQ(result.entries_removed, 20U);
        EXPECT_EQ(result.entries_added, 1U);
        EXPECT_TRUE(result.utf8_charset_added);
        EXPECT_TRUE(active_iptc_record_text(output, 1U, 90U, "\x1b%G"));
        EXPECT_TRUE(active_iptc_text(output, 120U, "Caption"));
        EXPECT_TRUE(active_iptc_text(output, 7U, "Keep status"));
        EXPECT_EQ(active_iptc_count(output, 85U), 1U);
        // A retained unrelated legacy caption blocks promotion of the whole batch.
        options.description_to_iptc_caption = false;
        const auto rejected = translate_xmp_iptc_metadata(source, options,
                                                          &output);
        EXPECT_EQ(rejected.status,
                  MetadataDescriptiveTranslationStatus::NativeEncodingConflict);
        EXPECT_TRUE(active_iptc_text(output, 120U, "Caption"));
    }

    TEST(MetadataTranslation, IptcCombinedRemovesOnlyDirtySelectedGroups)
    {
        for (const bool dirty : { false, true }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (const IptcBatchField& field : kIptcBatchFields) {
                const IptcTextField& f = field.text;
                add_xmp_text(&source, block, f.schema_ns, f.path, {},
                             dirty ? EntryFlags::Dirty | EntryFlags::Deleted
                                   : EntryFlags::Deleted,
                             0U);
                add_iptc_bytes(&source, block, f.dataset, f.value, 1U);
                add_iptc_bytes(&source, block, f.dataset, f.value, 2U);
            }
            source.finalize();
            MetadataIptcTranslationOptions options;
            options.source_mode = MetadataDescriptiveTranslationSourceMode::All;
            MetaStore output;
            EXPECT_EQ(
                translate_xmp_iptc_metadata(source, options, &output).status,
                dirty ? MetadataDescriptiveTranslationStatus::NativeConflict
                      : MetadataDescriptiveTranslationStatus::Ok);
            options.conflict_policy
                = MetadataDescriptiveTranslationConflictPolicy::PreserveExisting;
            auto result = translate_xmp_iptc_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(active_iptc_count(output, 20U), 2U);
            options.conflict_policy
                = MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
            options.urgency_to_iptc = false;
            result = translate_xmp_iptc_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
            EXPECT_EQ(result.entries_removed, dirty ? 38U : 0U);
            for (const IptcBatchField& field : kIptcBatchFields) {
                EXPECT_EQ(active_iptc_count(output, field.text.dataset),
                          dirty && field.text.dataset != 10U ? 0U : 2U);
            }
        }
    }

    TEST(MetadataTranslation, IptcWorkflowRejectsInvalidCategoryAndUrgency)
    {
        for (const std::string_view value :
             { "", "0", "9", "A", " ", "05", "+5", "5.0", " 5" }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "Headline",
                         "Valid", EntryFlags::Dirty, 0U);
            const EntryId invalid
                = add_xmp_text(&source, block, kLocationPhotoshopNs, "Urgency",
                               value, EntryFlags::Dirty, 1U);
            source.finalize();
            MetaStore output;
            const auto result = translate_xmp_iptc_metadata(source, {},
                                                            &output);
            EXPECT_EQ(
                result.status,
                value.size() > 1U
                    ? MetadataDescriptiveTranslationStatus::ValueTooLong
                    : MetadataDescriptiveTranslationStatus::InvalidSourceValue)
                << value;
            EXPECT_EQ(result.failed_source_entry, invalid);
            EXPECT_EQ(result.failed_mapping,
                      MetadataDescriptiveTranslationMapping::PhotoshopUrgency);
            EXPECT_TRUE(output.entries().empty());
        }
        for (const std::string_view value :
             { "A", "Art", "art", "", "ARTS", "A1", "A B", "\xc3\xa9" }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "Category",
                         value, EntryFlags::Dirty, 0U);
            source.finalize();
            MetaStore output;
            const auto result = translate_xmp_iptc_metadata(source, {},
                                                            &output);
            const bool valid = value == "A" || value == "Art" || value == "art";
            EXPECT_EQ(
                result.status,
                valid ? MetadataDescriptiveTranslationStatus::Ok
                : value.size() > 3U
                    ? MetadataDescriptiveTranslationStatus::ValueTooLong
                    : MetadataDescriptiveTranslationStatus::InvalidSourceValue)
                << value;
            if (valid) {
                EXPECT_TRUE(active_iptc_text(output, 15U, value));
            }
        }
    }

    TEST(MetadataTranslation, IptcUrgencyAcceptsOnlySingleIntegerScalarsInRange)
    {
        for (const int number : { -1, 0, 1, 5, 8, 9, 10, 256 }) {
            const std::array values = {
                make_i8(static_cast<int8_t>(number)),
                make_i16(static_cast<int16_t>(number)),
                make_i32(number),
                make_i64(number),
                make_u8(static_cast<uint8_t>(number)),
                make_u16(static_cast<uint16_t>(number)),
                make_u32(static_cast<uint32_t>(number)),
                make_u64(static_cast<uint64_t>(number)),
            };
            for (const MetaValue& value : values) {
                MetaStore source;
                const BlockId block = source.add_block(BlockInfo {});
                add_xmp_value(&source, block, kLocationPhotoshopNs, "Urgency",
                              value, EntryFlags::Dirty, 0U);
                source.finalize();
                MetaStore output;
                const auto result = translate_xmp_iptc_metadata(source, {},
                                                                &output);
                const bool valid  = number >= 1 && number <= 8;
                EXPECT_EQ(result.status,
                          valid ? MetadataDescriptiveTranslationStatus::Ok
                                : MetadataDescriptiveTranslationStatus::
                                      InvalidSourceValue);
                if (valid) {
                    EXPECT_TRUE(
                        active_iptc_text(output, 10U, std::to_string(number)));
                    EXPECT_EQ(translate_xmp_iptc_metadata(output, {}, &output)
                                  .groups_unchanged,
                              1U);
                }
            }
        }
        for (const unsigned kind : { 0U, 1U, 2U, 3U, 4U, 5U }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            MetaValue value     = make_u8(5U);
            switch (kind) {
            case 0U: value.count = 0U; break;
            case 1U: value.count = 2U; break;
            case 2U: value = make_f32_bits(0x40a00000U); break;
            case 3U: value = make_urational(5U, 1U); break;
            case 4U:
                value = make_bytes(source.arena(),
                                   std::as_bytes(std::span("5", 1U)));
                break;
            case 5U: value.kind = MetaValueKind::Array; break;
            }
            add_xmp_value(&source, block, kLocationPhotoshopNs, "Urgency",
                          value, EntryFlags::Dirty, 0U);
            source.finalize();
            MetaStore output;
            EXPECT_EQ(translate_xmp_iptc_metadata(source, {}, &output).status,
                      MetadataDescriptiveTranslationStatus::InvalidSourceValue);
        }
    }

    TEST(MetadataTranslation,
         IptcWorkflowEnforcesUtf8ByteLimitsWithoutTruncation)
    {
        for (const IptcBatchField& field : kIptcBatchFields) {
            const IptcTextField& f = field.text;
            if (f.dataset != 85U && f.dataset != 122U && f.dataset != 20U) {
                continue;
            }
            for (const bool overflow : { false, true }) {
                for (const bool utf8 : { false, true }) {
                    MetaStore source;
                    const BlockId block = source.add_block(BlockInfo {});
                    std::string value(32U - (utf8 ? 2U : 0U)
                                          + (overflow ? 1U : 0U),
                                      'A');
                    if (utf8) {
                        value += "\xc3\xa9";
                    }
                    add_xmp_text(&source, block, kXmpNsDc,
                                 "title[@xml:lang=x-default]", "Title",
                                 EntryFlags::Dirty, 0U);
                    const EntryId id = add_xmp_text(&source, block, f.schema_ns,
                                                    f.path, value,
                                                    EntryFlags::Dirty, 1U);
                    source.finalize();
                    MetaStore output;
                    const BlockId sentinel = output.add_block(BlockInfo {});
                    add_iptc_bytes(&output, sentinel, 5U, "Sentinel", 0U);
                    output.finalize();
                    const auto result = translate_xmp_iptc_metadata(source, {},
                                                                    &output);
                    EXPECT_EQ(
                        result.status,
                        overflow
                            ? MetadataDescriptiveTranslationStatus::ValueTooLong
                            : MetadataDescriptiveTranslationStatus::Ok);
                    if (overflow) {
                        EXPECT_EQ(result.failed_source_entry, id);
                        EXPECT_EQ(output.entries().size(), 1U);
                        EXPECT_TRUE(active_iptc_text(output, 5U, "Sentinel"));
                    } else {
                        EXPECT_TRUE(active_iptc_text(output, f.dataset, value));
                        EXPECT_EQ(result.utf8_charset_added, utf8);
                    }
                }
            }
        }
    }

    TEST(MetadataTranslation,
         IptcSupplementalCategoriesRetainIndexesAndMultiplicity)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        add_xmp_text(&source, block, kLocationPhotoshopNs,
                     "SupplementalCategories[10]", "Last", EntryFlags::None,
                     0U);
        add_xmp_text(&source, block, kLocationPhotoshopNs,
                     "SupplementalCategories[3]", "Same", EntryFlags::Dirty,
                     1U);
        add_xmp_text(&source, block, kLocationPhotoshopNs,
                     "SupplementalCategories[1]", "Same", EntryFlags::None, 2U);
        add_xmp_text(&source, block, kLocationPhotoshopNs,
                     "SupplementalCategories[2]", "Removed",
                     EntryFlags::Dirty | EntryFlags::Deleted, 3U);
        add_xmp_text(&source, block, kLocationPhotoshopNs,
                     "SupplementalCategories", "Unindexed", EntryFlags::Dirty,
                     4U);
        source.finalize();
        MetaStore output;
        const auto result = translate_xmp_iptc_metadata(source, {}, &output);
        ASSERT_EQ(result.status, MetadataDescriptiveTranslationStatus::Ok);
        EXPECT_EQ(result.source_properties, 4U);
        EXPECT_EQ(result.entries_added, 3U);
        const std::array expected = { "Same", "Same", "Last" };
        size_t index              = 0U;
        for (const Entry& entry : output.entries()) {
            if (entry.key.kind != MetaKeyKind::IptcDataset) {
                continue;
            }
            ASSERT_LT(index, expected.size());
            const auto bytes = output.arena().span(entry.value.data.span);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                           bytes.data()),
                                       bytes.size()),
                      expected[index++]);
        }
        EXPECT_EQ(index, 3U);
        EXPECT_EQ(
            translate_xmp_iptc_metadata(output, {}, &output).groups_unchanged,
            1U);
    }

    TEST(MetadataTranslation,
         IptcRepeatedGrowthRetainsNativeRanksAndAppendOrder)
    {
        struct Repeated final {
            std::string_view ns;
            std::string_view path;
            uint16_t dataset;
        };
        const std::array fields = {
            Repeated { kXmpNsDc, "creator", 80U },
            Repeated { kXmpNsDc, "subject", 25U },
            Repeated { kLocationPhotoshopNs, "SupplementalCategories", 20U },
        };
        for (const Repeated& field : fields) {
            for (const bool existing : { false, true }) {
                MetaStore source;
                const BlockId block = source.add_block(BlockInfo {});
                add_xmp_text(&source, block, field.ns,
                             std::string(field.path) + "[10]", "Last",
                             EntryFlags::None, 0U);
                add_xmp_text(&source, block, field.ns,
                             std::string(field.path) + "[3]", "Middle",
                             EntryFlags::Dirty, 1U);
                add_xmp_text(&source, block, field.ns,
                             std::string(field.path) + "[1]", "First",
                             EntryFlags::None, 2U);
                if (existing) {
                    add_iptc_bytes(&source, block, field.dataset, "OLD second",
                                   UINT32_MAX);
                    add_iptc_bytes(&source, block, field.dataset, "OLD first",
                                   UINT32_MAX - 1U);
                }
                source.finalize();
                MetaStore output;
                MetadataIptcTranslationOptions options;
                options.conflict_policy
                    = MetadataDescriptiveTranslationConflictPolicy::
                        ReplaceExisting;
                auto result = translate_xmp_iptc_metadata(source, options,
                                                          &output);
                ASSERT_EQ(result.status,
                          MetadataDescriptiveTranslationStatus::Ok);
                EXPECT_EQ(result.entries_updated, existing ? 2U : 0U);
                EXPECT_EQ(result.entries_added, existing ? 1U : 3U);
                options.conflict_policy
                    = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict;
                result = translate_xmp_iptc_metadata(output, options, &output);
                ASSERT_EQ(result.status,
                          MetadataDescriptiveTranslationStatus::Ok);
                EXPECT_EQ(result.groups_unchanged, 1U);
                if (field.dataset != 20U) {
                    const auto subgroup
                        = translate_xmp_descriptive_metadata(output, {},
                                                             &output);
                    EXPECT_EQ(subgroup.status,
                              MetadataDescriptiveTranslationStatus::Ok);
                    EXPECT_EQ(subgroup.groups_unchanged, 1U);
                }
                EXPECT_EQ(output.entries().back().origin.order_in_block,
                          existing ? UINT32_MAX : 0U);
            }
        }
    }

    TEST(MetadataTranslation,
         IptcWorkflowRejectsDuplicateSourcesAndMalformedText)
    {
        for (const IptcBatchField& field : kIptcBatchFields) {
            const IptcTextField& f = field.text;
            if (f.dataset != 85U && f.dataset != 122U && f.dataset != 15U
                && f.dataset != 20U && f.dataset != 10U) {
                continue;
            }
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, f.schema_ns, f.path, f.value,
                         EntryFlags::None, 0U);
            const EntryId duplicate
                = add_xmp_text(&source, block, f.schema_ns,
                               f.dataset == 20U ? "SupplementalCategories[01]"
                                                : f.path,
                               f.value, EntryFlags::Dirty, 1U);
            source.finalize();
            MetaStore output;
            const auto result = translate_xmp_iptc_metadata(source, {},
                                                            &output);
            EXPECT_EQ(result.status,
                      MetadataDescriptiveTranslationStatus::AmbiguousSource);
            EXPECT_EQ(result.failed_source_entry, duplicate);
            EXPECT_TRUE(output.entries().empty());
        }
        for (const std::string_view value :
             { std::string_view(""), std::string_view("bad\0text", 8U),
               std::string_view("\xc0\xaf", 2U),
               std::string_view("\xed\xa0\x80", 3U) }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            add_xmp_text(&source, block, kLocationPhotoshopNs, "CaptionWriter",
                         value, EntryFlags::Dirty, 0U);
            source.finalize();
            MetaStore output;
            EXPECT_EQ(translate_xmp_iptc_metadata(source, {}, &output).status,
                      MetadataDescriptiveTranslationStatus::InvalidSourceValue);
        }
    }

    TEST(MetadataTranslation, IptcCombinedResourceLimitsCoverEveryGroup)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        for (const IptcBatchField& field : kIptcBatchFields) {
            add_xmp_text(&source, block, field.text.schema_ns, field.text.path,
                         field.text.value, EntryFlags::Dirty, 0U);
        }
        MetaStore output;
        EXPECT_EQ(translate_xmp_iptc_metadata(source, {}, nullptr).status,
                  MetadataDescriptiveTranslationStatus::NullOutput);
        EXPECT_EQ(translate_xmp_iptc_metadata(source, {}, &output).status,
                  MetadataDescriptiveTranslationStatus::SourceNotFinalized);
        source.finalize();
        for (unsigned mode = 0U; mode < 9U; ++mode) {
            MetadataIptcTranslationOptions options;
            auto expected = MetadataDescriptiveTranslationStatus::InvalidOptions;
            switch (mode) {
            case 0U:
                options.max_source_properties = 19U;
                expected
                    = MetadataDescriptiveTranslationStatus::SourceLimitExceeded;
                break;
            case 1U:
                options.max_added_entries = 19U;
                expected
                    = MetadataDescriptiveTranslationStatus::EntryLimitExceeded;
                break;
            case 2U:
                options.max_operations = 19U;
                expected
                    = MetadataDescriptiveTranslationStatus::OperationLimitExceeded;
                break;
            case 3U:
                options.max_total_text_bytes = 4U;
                expected
                    = MetadataDescriptiveTranslationStatus::SourceLimitExceeded;
                break;
            case 4U:
                options.max_added_entries
                    = kMetadataIptcTranslationMaxAddedEntries + 1U;
                break;
            case 5U:
                options.source_mode
                    = static_cast<MetadataDescriptiveTranslationSourceMode>(
                        255U);
                break;
            case 6U:
                options.conflict_policy
                    = static_cast<MetadataDescriptiveTranslationConflictPolicy>(
                        255U);
                break;
            case 7U: options = no_iptc_mappings(); break;
            case 8U: options.max_operations = 0U; break;
            }
            EXPECT_EQ(
                translate_xmp_iptc_metadata(source, options, &output).status,
                expected)
                << mode;
            EXPECT_TRUE(output.entries().empty());
        }
    }

    TEST(MetadataTranslation, IptcSupplementalCategoryLimitIncludesCharset)
    {
        for (const unsigned count : { 1024U, 1025U }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            for (unsigned index = 1U; index <= count; ++index) {
                add_xmp_text(&source, block, kLocationPhotoshopNs,
                             "SupplementalCategories[" + std::to_string(index)
                                 + "]",
                             "\xc3\xa9", EntryFlags::Dirty, index);
            }
            source.finalize();
            MetaStore output;
            MetadataIptcTranslationOptions options;
            options.max_added_entries = 1024U;
            const auto result = translate_xmp_iptc_metadata(source, options,
                                                            &output);
            EXPECT_EQ(
                result.status,
                count == 1024U
                    ? MetadataDescriptiveTranslationStatus::EntryLimitExceeded
                    : MetadataDescriptiveTranslationStatus::SourceLimitExceeded);
            EXPECT_TRUE(output.entries().empty());
            if (count == 1024U) {
                options.max_added_entries = 1025U;
                const auto accepted
                    = translate_xmp_iptc_metadata(source, options, &output);
                ASSERT_EQ(accepted.status,
                          MetadataDescriptiveTranslationStatus::Ok);
                EXPECT_EQ(accepted.entries_added, 1025U);
                EXPECT_TRUE(accepted.utf8_charset_added);
            }
        }
    }

}  // namespace
}  // namespace openmeta

namespace openmeta {
namespace {
    using SettingsOptions = MetadataCaptureSettingsTranslationOptions;
    using SettingsStatus  = MetadataCaptureTranslationStatus;
    using SettingsPolicy  = MetadataCaptureTranslationConflictPolicy;
    constexpr std::string_view kSettingsNs = "http://ns.adobe.com/exif/1.0/";
    constexpr std::array<std::string_view, 12> kSettingsPaths {
        "ExposureProgram",  "MeteringMode", "SensingMethod",
        "CustomRendered",   "ExposureMode", "WhiteBalance",
        "SceneCaptureType", "GainControl",  "Contrast",
        "Saturation",       "Sharpness",    "SubjectDistanceRange"
    };
    constexpr std::array<uint16_t, 12> kSettingsTags { 0x8822, 0x9207, 0xa217,
                                                       0xa401, 0xa402, 0xa403,
                                                       0xa406, 0xa407, 0xa408,
                                                       0xa409, 0xa40a, 0xa40c };
    constexpr std::array<uint16_t, 12> kSettingsValues { 3, 5, 2, 1, 2, 1,
                                                         3, 4, 2, 1, 2, 3 };
    constexpr std::array<std::string_view, 12> kSettingsLabels {
        "Aperture-priority AE",
        "Multi-segment",
        "One-chip color area",
        "Custom",
        "Auto bracket",
        "Manual",
        "Night scene",
        "High gain down",
        "High",
        "Low",
        "Hard",
        "Distant"
    };

    static void settings_xmp(MetaStore& store, std::string_view path,
                             MetaValue value,
                             EntryFlags flags    = EntryFlags::Dirty,
                             std::string_view ns = kSettingsNs)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store.arena(), ns, path);
        entry.value = value;
        entry.flags = flags;
        entry.origin.wire_type_name = store.arena().append_string(
            "settings-source");
        store.add_entry(entry);
    }

    static void settings_native(MetaStore& store, uint16_t tag, MetaValue value)
    {
        Entry entry;
        entry.key   = make_exif_tag_key(store.arena(), "exififd", tag);
        entry.value = value;
        store.add_entry(entry);
    }

    static const Entry* settings_find(const MetaStore& store, uint16_t tag)
    {
        const auto ids = store.find_all(make_exif_tag_key_view("exififd", tag));
        for (const EntryId id : ids) {
            if (!any(store.entry(id).flags, EntryFlags::Deleted))
                return &store.entry(id);
        }
        return nullptr;
    }

    static size_t settings_active_count(const MetaStore& store, uint16_t tag)
    {
        size_t count = 0U;
        for (const EntryId id :
             store.find_all(make_exif_tag_key_view("exififd", tag)))
            if (!any(store.entry(id).flags, EntryFlags::Deleted))
                ++count;
        return count;
    }

    static MetaStore all_settings(bool labels      = false,
                                  EntryFlags flags = EntryFlags::Dirty)
    {
        MetaStore source;
        for (size_t i = 0U; i < kSettingsPaths.size(); ++i) {
            settings_xmp(source, kSettingsPaths[i],
                         labels ? make_text(source.arena(), kSettingsLabels[i],
                                            TextEncoding::Utf8)
                                : make_u32(kSettingsValues[i]),
                         flags);
        }
        return source;
    }

    static void settings_failure(MetaStore& source, SettingsStatus expected,
                                 const SettingsOptions& options = {})
    {
        source.finalize();
        const size_t before = source.entries().size();
        MetaStore output;
        settings_native(output, 0x8822U, make_u16(77U));
        output.finalize();
        EXPECT_EQ(translate_xmp_capture_settings_metadata(source, options,
                                                          &output)
                      .status,
                  expected);
        ASSERT_EQ(output.entries().size(), 1U);
        ASSERT_NE(settings_find(output, 0x8822U), nullptr);
        EXPECT_EQ(settings_find(output, 0x8822U)->value.data.u64, 77U);
        EXPECT_EQ(translate_xmp_capture_settings_metadata(source, options,
                                                          &source)
                      .status,
                  expected);
        EXPECT_EQ(source.entries().size(), before);
    }

    TEST(MetadataCaptureSettings, CreatesTwelveShortFieldsWithOwnedProvenance)
    {
        MetaStore output;
        {
            MetaStore source = all_settings(true);
            source.finalize();
            const auto result
                = translate_xmp_capture_settings_metadata(source, {}, &output);
            ASSERT_EQ(result.status, SettingsStatus::Ok);
            EXPECT_EQ(result.entries_added, 12U);
            EXPECT_EQ(result.source_properties, 12U);
            EXPECT_EQ(result.groups_translated, 12U);
        }
        for (size_t i = 0U; i < kSettingsTags.size(); ++i) {
            const Entry* entry = settings_find(output, kSettingsTags[i]);
            ASSERT_NE(entry, nullptr);
            EXPECT_EQ(entry->value.kind, MetaValueKind::Scalar);
            EXPECT_EQ(entry->value.elem_type, MetaElementType::U16);
            EXPECT_EQ(entry->value.count, 1U);
            EXPECT_EQ(entry->value.data.u64, kSettingsValues[i]);
            const auto bytes = output.arena().span(
                entry->origin.wire_type_name);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                           bytes.data()),
                                       bytes.size()),
                      "settings-source");
        }
        const auto same = translate_xmp_capture_settings_metadata(output, {},
                                                                  &output);
        EXPECT_EQ(same.status, SettingsStatus::Ok);
        EXPECT_EQ(same.groups_unchanged, 12U);
    }

    TEST(MetadataCaptureSettings,
         ClosedCodesIncludeUnknownAndOtherButRejectReservedValues)
    {
        const std::array<std::vector<uint16_t>, 12> accepted {
            { { 0, 1, 2, 3, 4, 5, 6, 7, 8 },
              { 0, 1, 2, 3, 4, 5, 6, 255 },
              { 1, 2, 3, 4, 5, 7, 8 },
              { 0, 1 },
              { 0, 1, 2 },
              { 0, 1 },
              { 0, 1, 2, 3 },
              { 0, 1, 2, 3, 4 },
              { 0, 1, 2 },
              { 0, 1, 2 },
              { 0, 1, 2 },
              { 0, 1, 2, 3 } }
        };
        for (size_t i = 0U; i < accepted.size(); ++i) {
            for (const uint16_t code : accepted[i]) {
                for (const bool as_text : { false, true }) {
                    MetaStore source;
                    settings_xmp(source, kSettingsPaths[i],
                                 as_text ? make_text(source.arena(),
                                                     std::to_string(code),
                                                     TextEncoding::Ascii)
                                         : make_i32(code));
                    source.finalize();
                    MetaStore output;
                    ASSERT_EQ(translate_xmp_capture_settings_metadata(source,
                                                                      {},
                                                                      &output)
                                  .status,
                              SettingsStatus::Ok);
                    ASSERT_NE(settings_find(output, kSettingsTags[i]), nullptr);
                    EXPECT_EQ(
                        settings_find(output, kSettingsTags[i])->value.data.u64,
                        code);
                }
            }
            MetaStore source;
            settings_xmp(source, kSettingsPaths[i], make_u32(65536U));
            settings_failure(source, SettingsStatus::ValueOutOfRange);
        }
        for (const auto item : { std::pair<size_t, uint16_t> { 0, 9 },
                                 { 1, 7 },
                                 { 2, 0 },
                                 { 2, 6 },
                                 { 5, 2 } }) {
            MetaStore source;
            settings_xmp(source, kSettingsPaths[item.first],
                         make_u16(item.second));
            settings_failure(source, SettingsStatus::ValueOutOfRange);
        }
    }

    TEST(MetadataCaptureSettings,
         RejectsCoercionUnsafeTextAndReadOnlyExtensions)
    {
        for (const std::string_view text :
             { "1.0", "1/1", "1e0", "+1", "-1", " 1", "1 ", "manual", "Bulb",
               "" }) {
            MetaStore source;
            settings_xmp(source, "ExposureProgram",
                         make_text(source.arena(), text, TextEncoding::Utf8));
            settings_failure(source, SettingsStatus::InvalidNumericValue);
        }
        for (const MetaValue value :
             { make_f64_bits(0x3ff0000000000000ULL), make_urational(1, 1),
               make_srational(1, 1) }) {
            MetaStore source;
            settings_xmp(source, "WhiteBalance", value);
            settings_failure(source, SettingsStatus::InvalidSourceValue);
        }
        MetaStore source;
        settings_xmp(source, "WhiteBalance", make_i32(-1));
        settings_failure(source, SettingsStatus::ValueOutOfRange);
        source = MetaStore {};
        settings_xmp(source, "WhiteBalance",
                     make_text(source.arena(), "1", TextEncoding::Utf16LE));
        settings_failure(source, SettingsStatus::InvalidSourceValue);
        source = MetaStore {};
        settings_xmp(source, "WhiteBalance",
                     make_text(source.arena(), std::string_view("1\0", 2),
                               TextEncoding::Ascii));
        settings_failure(source, SettingsStatus::InvalidNumericValue);
    }

    TEST(MetadataCaptureSettings, DirtySelectionFlagsNamespacesAndDuplicates)
    {
        MetaStore source = all_settings(false, EntryFlags::None);
        source.finalize();
        MetaStore output;
        EXPECT_EQ(translate_xmp_capture_settings_metadata(source, {}, &output)
                      .entries_added,
                  0U);
        SettingsOptions options;
        options.source_mode = MetadataCaptureTranslationSourceMode::All;
        EXPECT_EQ(translate_xmp_capture_settings_metadata(source, options,
                                                          &output)
                      .entries_added,
                  12U);
        source = MetaStore {};
        settings_xmp(source, "WhiteBalance", make_u16(1), EntryFlags::Dirty,
                     "foreign");
        settings_xmp(source, "WhiteBalance[1]", make_u16(1));
        source.finalize();
        EXPECT_EQ(translate_xmp_capture_settings_metadata(source, {}, &output)
                      .entries_added,
                  0U);
        source = MetaStore {};
        settings_xmp(source, "WhiteBalance", make_u16(1));
        settings_xmp(source, "WhiteBalance", make_u16(1));
        settings_failure(source, SettingsStatus::AmbiguousSource);
        options                       = {};
        options.white_balance_to_exif = false;
        EXPECT_EQ(translate_xmp_capture_settings_metadata(source, options,
                                                          &output)
                      .status,
                  SettingsStatus::Ok);
    }

    TEST(MetadataCaptureSettings, TypedEquivalenceDuplicateRepairAndRemoval)
    {
        MetaStore source = all_settings();
        settings_native(source, 0xa403U, make_u32(1));
        settings_native(source, 0xa403U, make_u16(0));
        settings_failure(source, SettingsStatus::NativeConflict);
        SettingsOptions options;
        options.conflict_policy = SettingsPolicy::PreserveExisting;
        MetaStore output;
        const auto kept
            = translate_xmp_capture_settings_metadata(source, options, &output);
        EXPECT_EQ(kept.status, SettingsStatus::Ok);
        EXPECT_EQ(kept.groups_preserved, 1U);
        EXPECT_EQ(settings_active_count(output, 0xa403U), 2U);
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        const auto changed
            = translate_xmp_capture_settings_metadata(source, options, &output);
        EXPECT_EQ(changed.status, SettingsStatus::Ok);
        EXPECT_EQ(changed.entries_updated, 1U);
        EXPECT_EQ(changed.entries_removed, 1U);
        EXPECT_EQ(settings_active_count(output, 0xa403U), 1U);
        EXPECT_EQ(settings_find(output, 0xa403U)->value.elem_type,
                  MetaElementType::U16);
        source = all_settings(false, EntryFlags::Dirty | EntryFlags::Deleted);
        for (const uint16_t tag : kSettingsTags)
            settings_native(source, tag, make_u16(99));
        settings_native(source, 0x829aU, make_urational(1, 100));
        source.finalize();
        const auto removed
            = translate_xmp_capture_settings_metadata(source, options, &source);
        EXPECT_EQ(removed.status, SettingsStatus::Ok);
        EXPECT_EQ(removed.entries_removed, 12U);
        for (const uint16_t tag : kSettingsTags)
            EXPECT_EQ(settings_find(source, tag), nullptr);
        EXPECT_NE(settings_find(source, 0x829aU), nullptr);
    }

    TEST(MetadataCaptureSettings,
         BudgetsAndLateFailuresLeaveAliasedOutputUnchanged)
    {
        MetaStore source = all_settings();
        SettingsOptions options;
        options.max_added_entries = 11U;
        settings_failure(source, SettingsStatus::EntryLimitExceeded, options);
        options                = {};
        options.max_operations = 11U;
        settings_failure(source, SettingsStatus::OperationLimitExceeded,
                         options);
        source                       = all_settings(true);
        options                      = {};
        options.max_total_text_bytes = 5U;
        settings_failure(source, SettingsStatus::SourceLimitExceeded, options);
        source = MetaStore {};
        settings_xmp(source, "ExposureProgram", make_u16(1));
        settings_xmp(source, "SubjectDistanceRange", make_u16(9));
        settings_failure(source, SettingsStatus::ValueOutOfRange);
        EXPECT_EQ(settings_find(source, 0x8822U), nullptr);
        source = MetaStore {};
        settings_xmp(source, "WhiteBalance",
                     make_text(source.arena(), std::string(129U, '1'),
                               TextEncoding::Ascii));
        settings_failure(source, SettingsStatus::ValueTooLong);
        options                   = {};
        options.max_added_entries = 13U;
        settings_failure(source, SettingsStatus::InvalidOptions, options);
        MetaStore unfinalized;
        MetaStore output;
        EXPECT_EQ(translate_xmp_capture_settings_metadata(unfinalized, {},
                                                          nullptr)
                      .status,
                  SettingsStatus::NullOutput);
        EXPECT_EQ(translate_xmp_capture_settings_metadata(unfinalized, {},
                                                          &output)
                      .status,
                  SettingsStatus::SourceNotFinalized);
    }
}  // namespace
}  // namespace openmeta

namespace openmeta {
namespace {
    using RationalOptions = MetadataCaptureRationalTranslationOptions;
    constexpr std::array<std::string_view, 4> kRationalPaths {
        "SubjectDistance", "DigitalZoomRatio", "ExposureIndex", "FlashEnergy"
    };
    constexpr std::array<uint16_t, 4> kRationalTags { 0x9206U, 0xa404U, 0xa215U,
                                                      0xa20bU };
    static MetaStore rational_source(EntryFlags flags = EntryFlags::Dirty)
    {
        MetaStore source;
        constexpr std::array<std::string_view, 4> values { "3/2", "1.25", "2e2",
                                                           "+5/2" };
        for (size_t i = 0U; i < values.size(); ++i)
            settings_xmp(source, kRationalPaths[i],
                         make_text(source.arena(), values[i],
                                   TextEncoding::Utf8),
                         flags);
        return source;
    }
    static void rational_failure(MetaStore& source, SettingsStatus expected,
                                 const RationalOptions& options = {})
    {
        source.finalize();
        MetaStore output;
        settings_native(output, 0x9206U, make_urational(7U, 2U));
        output.finalize();
        const size_t count = source.entries().size();
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, options,
                                                          &output)
                      .status,
                  expected);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(output.entry(0U).value.data.ur.numer, 7U);
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, options,
                                                          &source)
                      .status,
                  expected);
        EXPECT_EQ(source.entries().size(), count);
    }
    TEST(MetadataCaptureRational,
         WritesFourExactCanonicalFieldsWithOwnedProvenance)
    {
        MetaStore output;
        {
            MetaStore source = rational_source();
            source.finalize();
            const auto result
                = translate_xmp_capture_rational_metadata(source, {}, &output);
            ASSERT_EQ(result.status, SettingsStatus::Ok);
            EXPECT_EQ(result.source_properties, 4U);
            EXPECT_EQ(result.entries_added, 4U);
        }
        constexpr std::array<URational, 4> expected {
            { { 3U, 2U }, { 5U, 4U }, { 200U, 1U }, { 5U, 2U } }
        };
        for (size_t i = 0U; i < expected.size(); ++i) {
            const Entry* entry = settings_find(output, kRationalTags[i]);
            ASSERT_NE(entry, nullptr);
            EXPECT_EQ(entry->value.elem_type, MetaElementType::URational);
            EXPECT_EQ(entry->value.count, 1U);
            EXPECT_EQ(entry->value.data.ur.numer, expected[i].numer);
            EXPECT_EQ(entry->value.data.ur.denom, expected[i].denom);
            const auto bytes = output.arena().span(
                entry->origin.wire_type_name);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                           bytes.data()),
                                       bytes.size()),
                      "settings-source");
        }
        EXPECT_EQ(translate_xmp_capture_rational_metadata(output, {}, &output)
                      .groups_unchanged,
                  4U);
        EXPECT_EQ(settings_find(output, 0x9215U), nullptr);
        EXPECT_EQ(settings_find(output, 0x920bU), nullptr);
    }
    TEST(MetadataCaptureRational, SubjectDistanceSentinelsPrecedeReduction)
    {
        for (const MetaValue value :
             { make_urational(UINT32_MAX, 3U),
               make_urational(UINT32_MAX, UINT32_MAX), make_u32(UINT32_MAX) }) {
            MetaStore source;
            settings_xmp(source, "SubjectDistance", value);
            source.finalize();
            MetaStore output;
            ASSERT_EQ(translate_xmp_capture_rational_metadata(source, {},
                                                              &output)
                          .status,
                      SettingsStatus::Ok);
            ASSERT_NE(settings_find(output, 0x9206U), nullptr);
            EXPECT_EQ(settings_find(output, 0x9206U)->value.data.ur.numer,
                      UINT32_MAX);
            EXPECT_EQ(settings_find(output, 0x9206U)->value.data.ur.denom, 1U);
        }
        for (const std::string_view text :
             { "Infinity", "4294967295", "4294967295/3" }) {
            MetaStore source;
            settings_xmp(source, "SubjectDistance",
                         make_text(source.arena(), text, TextEncoding::Ascii));
            settings_native(source, 0x9206U, make_urational(UINT32_MAX, 3U));
            source.finalize();
            MetaStore output;
            const auto result
                = translate_xmp_capture_rational_metadata(source, {}, &output);
            EXPECT_EQ(result.status, SettingsStatus::Ok);
            EXPECT_EQ(result.groups_unchanged, 1U);
            EXPECT_EQ(settings_find(output, 0x9206U)->value.data.ur.denom, 3U);
        }
        for (const std::string_view text : { "Unknown", "0", "0/17" }) {
            MetaStore source;
            settings_xmp(source, "SubjectDistance",
                         make_text(source.arena(), text, TextEncoding::Ascii));
            source.finalize();
            MetaStore output;
            ASSERT_EQ(translate_xmp_capture_rational_metadata(source, {},
                                                              &output)
                          .status,
                      SettingsStatus::Ok);
            EXPECT_EQ(settings_find(output, 0x9206U)->value.data.ur.numer, 0U);
            EXPECT_EQ(settings_find(output, 0x9206U)->value.data.ur.denom, 1U);
        }
    }
    TEST(MetadataCaptureRational, FiniteDistanceCannotBecomeAnInfinitySentinel)
    {
        for (const std::string_view text : { "4294967295.0", "4.294967295e9",
                                             "8589934590/2", "2147483647.5" }) {
            MetaStore source;
            settings_xmp(source, "SubjectDistance",
                         make_text(source.arena(), text, TextEncoding::Ascii));
            rational_failure(source, SettingsStatus::ValueOutOfRange);
        }
        MetaStore source;
        settings_xmp(source, "SubjectDistance",
                     make_text(source.arena(), "429496729.5",
                               TextEncoding::Ascii));
        source.finalize();
        MetaStore output;
        ASSERT_EQ(
            translate_xmp_capture_rational_metadata(source, {}, &output).status,
            SettingsStatus::Ok);
        EXPECT_EQ(settings_find(output, 0x9206U)->value.data.ur.numer,
                  858993459U);
        EXPECT_EQ(settings_find(output, 0x9206U)->value.data.ur.denom, 2U);
        source = MetaStore {};
        settings_xmp(source, "SubjectDistance", make_u32(1431655765U));
        settings_native(source, 0x9206U, make_urational(UINT32_MAX, 3U));
        rational_failure(source, SettingsStatus::NativeConflict);
    }
    TEST(MetadataCaptureRational, ZeroRulesAndExactReductionAreFieldSpecific)
    {
        for (const size_t index : { 0U, 1U, 3U }) {
            MetaStore source;
            settings_xmp(source, kRationalPaths[index],
                         make_urational(0U, 17U));
            source.finalize();
            MetaStore output;
            ASSERT_EQ(translate_xmp_capture_rational_metadata(source, {},
                                                              &output)
                          .status,
                      SettingsStatus::Ok);
            EXPECT_EQ(settings_find(output, kRationalTags[index])
                          ->value.data.ur.numer,
                      0U);
            EXPECT_EQ(settings_find(output, kRationalTags[index])
                          ->value.data.ur.denom,
                      1U);
        }
        MetaStore source;
        settings_xmp(source, "ExposureIndex", make_u32(0U));
        rational_failure(source, SettingsStatus::ValueOutOfRange);
        source = MetaStore {};
        settings_xmp(source, "ExposureIndex",
                     make_text(source.arena(), "8589934590/2",
                               TextEncoding::Ascii));
        source.finalize();
        MetaStore output;
        ASSERT_EQ(
            translate_xmp_capture_rational_metadata(source, {}, &output).status,
            SettingsStatus::Ok);
        EXPECT_EQ(settings_find(output, 0xa215U)->value.data.ur.numer,
                  UINT32_MAX);
    }
    TEST(MetadataCaptureRational,
         RejectsUnsupportedTypesEncodingsAndMalformedNumbers)
    {
        for (const MetaValue value :
             { make_urational(0U, 0U), make_urational(UINT32_MAX, 0U) }) {
            MetaStore source;
            settings_xmp(source, "SubjectDistance", value);
            rational_failure(source, SettingsStatus::InvalidNumericValue);
        }
        for (const MetaValue value :
             { make_f64_bits(0x3ff0000000000000ULL), make_srational(1, 2) }) {
            MetaStore source;
            settings_xmp(source, "SubjectDistance", value);
            rational_failure(source, SettingsStatus::InvalidSourceValue);
        }
        for (const std::string_view text : { " 1", "1 ", "1 m", "-1", "inf",
                                             "unknown", "1/0", "NaN", ".5" }) {
            MetaStore source;
            settings_xmp(source, "SubjectDistance",
                         make_text(source.arena(), text, TextEncoding::Ascii));
            rational_failure(source, SettingsStatus::InvalidNumericValue);
        }
        for (const std::string_view text :
             { "18446744073709551616", "1/4294967296", "1e20" }) {
            MetaStore source;
            settings_xmp(source, "FlashEnergy",
                         make_text(source.arena(), text, TextEncoding::Ascii));
            rational_failure(source, SettingsStatus::ValueOutOfRange);
        }
        MetaStore source;
        settings_xmp(source, "DigitalZoomRatio",
                     make_text(source.arena(), "1", TextEncoding::Utf16LE));
        rational_failure(source, SettingsStatus::InvalidSourceValue);
        source          = MetaStore {};
        MetaValue count = make_u32(1U);
        count.count     = 2U;
        settings_xmp(source, "DigitalZoomRatio", count);
        rational_failure(source, SettingsStatus::InvalidSourceValue);
        source = MetaStore {};
        settings_xmp(source, "DigitalZoomRatio", make_i32(-1));
        rational_failure(source, SettingsStatus::ValueOutOfRange);
    }
    TEST(MetadataCaptureRational,
         DirtyFlagsExactNamespacesAndLegacyTagsAreIndependent)
    {
        MetaStore source = rational_source(EntryFlags::None);
        source.finalize();
        MetaStore output;
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, {}, &output)
                      .entries_added,
                  0U);
        RationalOptions options;
        options.source_mode = MetadataCaptureTranslationSourceMode::All;
        options.flash_energy_to_exif = false;
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, options,
                                                          &output)
                      .entries_added,
                  3U);
        source = MetaStore {};
        settings_xmp(source, "SubjectDistance[1]", make_u32(1U));
        settings_xmp(source, "SubjectDistance", make_u32(1U), EntryFlags::Dirty,
                     "foreign");
        settings_xmp(source, "ExposureIndex", make_u32(200U));
        settings_native(source, 0x9215U, make_urational(99U, 1U));
        source.finalize();
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, {}, &output)
                      .entries_added,
                  1U);
        EXPECT_EQ(settings_find(output, 0x9215U)->value.data.ur.numer, 99U);
        source = MetaStore {};
        settings_xmp(source, "ExposureIndex", make_u32(100U));
        settings_xmp(source, "ExposureIndex", make_u32(100U));
        rational_failure(source, SettingsStatus::AmbiguousSource);
    }
    TEST(MetadataCaptureRational, ConflictsRepairDuplicatesAndRemoveAtomically)
    {
        MetaStore source = rational_source();
        settings_native(source, 0xa404U, make_u32(1U));
        settings_native(source, 0xa404U, make_urational(3U, 1U));
        rational_failure(source, SettingsStatus::NativeConflict);
        RationalOptions options;
        options.conflict_policy = SettingsPolicy::PreserveExisting;
        MetaStore output;
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, options,
                                                          &output)
                      .groups_preserved,
                  1U);
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        const auto repaired
            = translate_xmp_capture_rational_metadata(source, options, &output);
        ASSERT_EQ(repaired.status, SettingsStatus::Ok);
        EXPECT_EQ(repaired.entries_removed, 1U);
        EXPECT_EQ(repaired.entries_updated, 1U);
        EXPECT_EQ(settings_active_count(output, 0xa404U), 1U);
        source = rational_source(EntryFlags::Dirty | EntryFlags::Deleted);
        for (uint16_t tag : kRationalTags)
            settings_native(source, tag, make_urational(1U, 1U));
        settings_native(source, 0x829aU, make_urational(1U, 100U));
        source.finalize();
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, options,
                                                          &source)
                      .entries_removed,
                  4U);
        for (uint16_t tag : kRationalTags)
            EXPECT_EQ(settings_find(source, tag), nullptr);
        EXPECT_NE(settings_find(source, 0x829aU), nullptr);
    }
    TEST(MetadataCaptureRational, BudgetsAndLateFailuresPreserveAliasedOutput)
    {
        MetaStore source = rational_source();
        RationalOptions options;
        options.max_added_entries = 3U;
        rational_failure(source, SettingsStatus::EntryLimitExceeded, options);
        options                = {};
        options.max_operations = 3U;
        rational_failure(source, SettingsStatus::OperationLimitExceeded,
                         options);
        options                      = {};
        options.max_total_text_bytes = 2U;
        rational_failure(source, SettingsStatus::SourceLimitExceeded, options);
        options                             = {};
        options.max_text_bytes_per_property = 2U;
        rational_failure(source, SettingsStatus::ValueTooLong, options);
        options                   = {};
        options.max_added_entries = 5U;
        rational_failure(source, SettingsStatus::InvalidOptions, options);
        source = MetaStore {};
        settings_xmp(source, "SubjectDistance", make_u32(1U));
        settings_xmp(source, "FlashEnergy",
                     make_text(source.arena(), "bad", TextEncoding::Ascii));
        rational_failure(source, SettingsStatus::InvalidNumericValue);
        EXPECT_EQ(settings_find(source, 0x9206U), nullptr);
        MetaStore unfinalized;
        MetaStore output;
        EXPECT_EQ(translate_xmp_capture_rational_metadata(unfinalized, {},
                                                          nullptr)
                      .status,
                  SettingsStatus::NullOutput);
        EXPECT_EQ(translate_xmp_capture_rational_metadata(unfinalized, {},
                                                          &output)
                      .status,
                  SettingsStatus::SourceNotFinalized);
    }
}  // namespace
}  // namespace openmeta

namespace openmeta {
namespace {
    using FlashOptions = MetadataFlashTranslationOptions;
    constexpr std::array<std::string_view, 5> kFlashChildren {
        "Fired", "Function", "Mode", "RedEyeMode", "Return"
    };
    static MetaStore flash_source(uint16_t code, bool text = false,
                                  EntryFlags flags = EntryFlags::Dirty,
                                  bool qualified   = false)
    {
        MetaStore source;
        const std::array<uint16_t, 5> values {
            static_cast<uint16_t>(code & 1U),
            static_cast<uint16_t>((code >> 5U) & 1U),
            static_cast<uint16_t>((code >> 3U) & 3U),
            static_cast<uint16_t>((code >> 6U) & 1U),
            static_cast<uint16_t>((code >> 1U) & 3U)
        };
        for (size_t i = 0U; i < values.size(); ++i) {
            std::string path = qualified ? "Flash/exif:" : "Flash/";
            path += kFlashChildren[i];
            const bool boolean_field = i == 0U || i == 1U || i == 3U;
            const std::string value  = boolean_field
                                           ? (values[i] ? "True" : "False")
                                           : std::to_string(values[i]);
            settings_xmp(source, path,
                         text ? make_text(source.arena(), value,
                                          TextEncoding::Utf8)
                              : make_u16(values[i]),
                         flags);
        }
        return source;
    }
    static void flash_failure(MetaStore& source, SettingsStatus expected,
                              const FlashOptions& options = {})
    {
        source.finalize();
        MetaStore output;
        settings_native(output, 0x9209U, make_u16(25U));
        output.finalize();
        const size_t count = source.entries().size();
        EXPECT_EQ(translate_xmp_flash_metadata(source, options, &output).status,
                  expected);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(output.entry(0U).value.data.u64, 25U);
        EXPECT_EQ(translate_xmp_flash_metadata(source, options, &source).status,
                  expected);
        EXPECT_EQ(source.entries().size(), count);
    }
    TEST(MetadataFlash, AllDefinedBitPatternsMatchScalarAndStructuredSources)
    {
        uint32_t accepted = 0U;
        for (uint16_t code = 0U; code <= 127U; ++code) {
            for (const bool structured : { false, true }) {
                MetaStore source = structured ? flash_source(code)
                                              : MetaStore {};
                if (!structured)
                    settings_xmp(source, "Flash", make_u16(code));
                if (((code >> 1U) & 3U) == 1U) {
                    flash_failure(source, SettingsStatus::ValueOutOfRange);
                    continue;
                }
                source.finalize();
                MetaStore output;
                const auto result = translate_xmp_flash_metadata(source, {},
                                                                 &output);
                ASSERT_EQ(result.status, SettingsStatus::Ok);
                EXPECT_EQ(result.source_properties, structured ? 5U : 1U);
                EXPECT_EQ(result.entries_added, 1U);
                const Entry* native = settings_find(output, 0x9209U);
                ASSERT_NE(native, nullptr);
                EXPECT_EQ(native->value.elem_type, MetaElementType::U16);
                EXPECT_EQ(native->value.count, 1U);
                EXPECT_EQ(native->value.data.u64, code);
                EXPECT_EQ(translate_xmp_flash_metadata(output, {}, &output)
                              .groups_unchanged,
                          1U);
            }
            if (((code >> 1U) & 3U) != 1U)
                ++accepted;
        }
        EXPECT_EQ(accepted, 96U);
    }
    TEST(MetadataFlash, CompleteTextAndFunctionAbsenceAreExplicit)
    {
        MetaStore output;
        {
            MetaStore source = flash_source(48U, true, EntryFlags::Dirty, true);
            source.finalize();
            ASSERT_EQ(translate_xmp_flash_metadata(source, {}, &output).status,
                      SettingsStatus::Ok);
        }
        ASSERT_NE(settings_find(output, 0x9209U), nullptr);
        EXPECT_EQ(settings_find(output, 0x9209U)->value.data.u64, 48U);
        const auto bytes = output.arena().span(
            settings_find(output, 0x9209U)->origin.wire_type_name);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                   bytes.size()),
                  "settings-source");
        for (const std::string_view value : { "25", "Auto, fired" }) {
            MetaStore source;
            settings_xmp(source, "Flash",
                         make_text(source.arena(), value, TextEncoding::Ascii));
            source.finalize();
            ASSERT_EQ(translate_xmp_flash_metadata(source, {}, &output).status,
                      SettingsStatus::Ok);
            EXPECT_EQ(settings_find(output, 0x9209U)->value.data.u64, 25U);
        }
    }
    TEST(MetadataFlash,
         DirtyChildSelectsCompleteCleanCompanionsWithoutNativeInference)
    {
        MetaStore source = flash_source(89U, true, EntryFlags::None);
        source.finalize();
        MetaStore output;
        EXPECT_EQ(
            translate_xmp_flash_metadata(source, {}, &output).entries_added,
            0U);
        MetaEdit edit;
        edit.set_value(0U,
                       make_text(edit.arena(), "True", TextEncoding::Ascii));
        source            = commit(source, std::span(&edit, 1U));
        const auto result = translate_xmp_flash_metadata(source, {}, &output);
        ASSERT_EQ(result.status, SettingsStatus::Ok);
        EXPECT_EQ(result.source_properties, 5U);
        EXPECT_EQ(settings_find(output, 0x9209U)->value.data.u64, 89U);
        source = MetaStore {};
        settings_xmp(source, "Flash/Fired", make_u16(1U));
        settings_native(source, 0x9209U, make_u16(25U));
        flash_failure(source, SettingsStatus::IncompleteSource);
        source = flash_source(25U, true, EntryFlags::None);
        source.finalize();
        FlashOptions options;
        options.source_mode = MetadataCaptureTranslationSourceMode::All;
        EXPECT_EQ(translate_xmp_flash_metadata(source, options, &output)
                      .entries_added,
                  1U);
    }
    TEST(MetadataFlash, RejectsAliasesDuplicatesAndCompetingStructures)
    {
        for (const std::string_view path :
             { "Flash/Fired", "Flash/exif:Fired", "Flash" }) {
            MetaStore source = flash_source(25U);
            settings_xmp(source, path, make_u16(1U));
            flash_failure(source, SettingsStatus::AmbiguousSource);
        }
        for (const std::string_view path :
             { "Flash[1]/Fired", "Flash/Fired/Nested",
               "Flash/Fired[@xml:lang=en]", "Flash/foreign:Fired",
               "Flash/Unknown" }) {
            MetaStore source = flash_source(25U);
            settings_xmp(source, path, make_u16(1U));
            flash_failure(source, SettingsStatus::UnsupportedSourceShape);
        }
        MetaStore source;
        settings_xmp(source, "Flash", make_u16(25U), EntryFlags::Dirty,
                     "foreign");
        settings_xmp(source, "FlashEnergy", make_u16(1U));
        source.finalize();
        MetaStore output;
        EXPECT_EQ(
            translate_xmp_flash_metadata(source, {}, &output).source_properties,
            0U);
    }
    TEST(MetadataFlash, RejectsReservedBitsInvalidCodesAndCoercion)
    {
        for (uint16_t code : { 128U, 255U, 65535U }) {
            MetaStore source;
            settings_xmp(source, "Flash", make_u16(code));
            flash_failure(source, SettingsStatus::ValueOutOfRange);
        }
        for (const std::string_view value :
             { "true", "1.0", "1/1", "+1", "-1", "Auto, Fired", " 25", "" }) {
            MetaStore source;
            settings_xmp(source, "Flash",
                         make_text(source.arena(), value, TextEncoding::Ascii));
            flash_failure(source, SettingsStatus::InvalidNumericValue);
        }
        for (const MetaValue value :
             { make_f64_bits(0x3ff0000000000000ULL), make_urational(1U, 1U) }) {
            MetaStore source;
            settings_xmp(source, "Flash", value);
            flash_failure(source, SettingsStatus::InvalidSourceValue);
        }
        MetaStore source;
        settings_xmp(source, "Flash", make_i32(-1));
        flash_failure(source, SettingsStatus::ValueOutOfRange);
        source = MetaStore {};
        settings_xmp(source, "Flash",
                     make_text(source.arena(), "25", TextEncoding::Utf16LE));
        flash_failure(source, SettingsStatus::InvalidSourceValue);
        for (const size_t field : { 0U, 1U, 2U, 3U, 4U }) {
            source = flash_source(25U);
            source.finalize();
            MetaEdit edit;
            edit.set_value(static_cast<EntryId>(field), make_u16(4U));
            source = commit(source, std::span(&edit, 1U));
            flash_failure(source, SettingsStatus::ValueOutOfRange);
        }
    }
    TEST(MetadataFlash, NativeConflictsRepairTypesAndDuplicates)
    {
        MetaStore source = flash_source(25U);
        settings_native(source, 0x9209U, make_u32(25U));
        settings_native(source, 0x9209U, make_u16(0U));
        flash_failure(source, SettingsStatus::NativeConflict);
        FlashOptions options;
        options.conflict_policy = SettingsPolicy::PreserveExisting;
        MetaStore output;
        EXPECT_EQ(translate_xmp_flash_metadata(source, options, &output)
                      .groups_preserved,
                  1U);
        EXPECT_EQ(settings_active_count(output, 0x9209U), 2U);
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        const auto result       = translate_xmp_flash_metadata(source, options,
                                                               &output);
        ASSERT_EQ(result.status, SettingsStatus::Ok);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(settings_active_count(output, 0x9209U), 1U);
        EXPECT_EQ(settings_find(output, 0x9209U)->value.elem_type,
                  MetaElementType::U16);
    }
    TEST(MetadataFlash, WholeDeletionIsAtomicAndPartialDeletionFails)
    {
        FlashOptions options;
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        for (const bool structured : { false, true }) {
            MetaStore source = structured
                                   ? flash_source(25U, false,
                                                  EntryFlags::Dirty
                                                      | EntryFlags::Deleted)
                                   : MetaStore {};
            if (!structured)
                settings_xmp(source, "Flash", make_u16(25U),
                             EntryFlags::Dirty | EntryFlags::Deleted);
            settings_native(source, 0x9209U, make_u16(25U));
            settings_native(source, 0xa20bU, make_urational(7U, 3U));
            source.finalize();
            ASSERT_EQ(
                translate_xmp_flash_metadata(source, options, &source).status,
                SettingsStatus::Ok);
            EXPECT_EQ(settings_find(source, 0x9209U), nullptr);
            EXPECT_NE(settings_find(source, 0xa20bU), nullptr);
        }
        MetaStore source = flash_source(25U);
        source.finalize();
        MetaEdit edit;
        edit.tombstone(0U);
        source = commit(source, std::span(&edit, 1U));
        flash_failure(source, SettingsStatus::IncompleteSource, options);
    }
    TEST(MetadataFlash, LimitsAndFailureDiagnosticsPreserveOutput)
    {
        MetaStore source = flash_source(25U, true);
        FlashOptions options;
        options.max_source_properties = 4U;
        flash_failure(source, SettingsStatus::SourceLimitExceeded, options);
        options                      = {};
        options.max_total_text_bytes = 4U;
        flash_failure(source, SettingsStatus::SourceLimitExceeded, options);
        options                             = {};
        options.max_text_bytes_per_property = 2U;
        flash_failure(source, SettingsStatus::ValueTooLong, options);
        options                   = {};
        options.max_added_entries = 2U;
        flash_failure(source, SettingsStatus::InvalidOptions, options);
        source = flash_source(25U);
        settings_native(source, 0x9209U, make_u16(0U));
        settings_native(source, 0x9209U, make_u16(0U));
        options                 = {};
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        options.max_operations  = 1U;
        flash_failure(source, SettingsStatus::OperationLimitExceeded, options);
        MetaStore unfinalized;
        MetaStore output;
        EXPECT_EQ(translate_xmp_flash_metadata(unfinalized, {}, nullptr).status,
                  SettingsStatus::NullOutput);
        EXPECT_EQ(translate_xmp_flash_metadata(unfinalized, {}, &output).status,
                  SettingsStatus::SourceNotFinalized);
        EXPECT_STREQ(metadata_capture_translation_status_name(
                         SettingsStatus::IncompleteSource),
                     "incomplete_source");
        EXPECT_STREQ(metadata_capture_translation_status_name(
                         SettingsStatus::UnsupportedSourceShape),
                     "unsupported_source_shape");
        EXPECT_STREQ(metadata_capture_translation_mapping_name(
                         MetadataCaptureTranslationMapping::XmpFlash),
                     "xmp_flash");
    }
}  // namespace
}  // namespace openmeta

namespace openmeta {
namespace {
    using LightOptions = MetadataLightSourceTranslationOptions;
    constexpr std::array<uint16_t, 32> kLightCodes {
        0U,  1U,  2U,  3U,  4U,  9U,  10U, 11U, 12U, 13U, 14U,
        15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U,
        26U, 27U, 28U, 29U, 30U, 31U, 32U, 33U, 34U, 255U
    };

    static void light_failure(MetaStore& source, SettingsStatus expected,
                              const LightOptions& options = {})
    {
        source.finalize();
        MetaStore output;
        settings_native(output, 0x9208U, make_u16(21U));
        output.finalize();
        const size_t count = source.entries().size();
        const auto result = translate_xmp_light_source_metadata(source, options,
                                                                &output);
        EXPECT_EQ(result.status, expected);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(output.entry(0U).value.data.u64, 21U);
        EXPECT_EQ(translate_xmp_light_source_metadata(source, options, &source)
                      .status,
                  expected);
        EXPECT_EQ(source.entries().size(), count);
    }

    TEST(MetadataLightSource, ClosedCodeSetPreservesEveryDefinedInteger)
    {
        uint32_t accepted = 0U;
        for (uint16_t code = 0U; code <= 256U; ++code) {
            bool known = false;
            for (const uint16_t candidate : kLightCodes)
                known = known || code == candidate;
            for (const bool text : { false, true }) {
                MetaStore source;
                settings_xmp(source, "LightSource",
                             text ? make_text(source.arena(),
                                              std::to_string(code),
                                              TextEncoding::Ascii)
                                  : make_u32(code));
                if (!known) {
                    light_failure(source, SettingsStatus::ValueOutOfRange);
                    continue;
                }
                source.finalize();
                MetaStore output;
                const auto result
                    = translate_xmp_light_source_metadata(source, {}, &output);
                ASSERT_EQ(result.status, SettingsStatus::Ok) << code;
                EXPECT_EQ(result.source_properties, 1U);
                EXPECT_EQ(result.entries_added, 1U);
                EXPECT_EQ(result.failed_mapping,
                          MetadataCaptureTranslationMapping::None);
                const Entry* native = settings_find(output, 0x9208U);
                ASSERT_NE(native, nullptr);
                EXPECT_EQ(native->value.kind, MetaValueKind::Scalar);
                EXPECT_EQ(native->value.elem_type, MetaElementType::U16);
                EXPECT_EQ(native->value.count, 1U);
                EXPECT_EQ(native->value.data.u64, code);
                EXPECT_EQ(translate_xmp_light_source_metadata(output, {},
                                                              &output)
                              .groups_unchanged,
                          1U);
            }
            accepted += known ? 1U : 0U;
        }
        EXPECT_EQ(accepted, 32U);
    }

    TEST(MetadataLightSource, UniqueLabelsAndExplicitAliasesOwnTheirProvenance)
    {
        for (const uint16_t code : kLightCodes) {
            if (code == 1U || code == 25U)
                continue;
            MetaStore output;
            {
                MetaStore source;
                settings_xmp(source, "LightSource",
                             make_text(source.arena(),
                                       exif_light_source_name(code),
                                       TextEncoding::Utf8));
                source.finalize();
                ASSERT_EQ(translate_xmp_light_source_metadata(source, {},
                                                              &output)
                              .status,
                          SettingsStatus::Ok);
            }
            const Entry* native = settings_find(output, 0x9208U);
            ASSERT_NE(native, nullptr);
            EXPECT_EQ(native->value.data.u64, code);
            const auto provenance = output.arena().span(
                native->origin.wire_type_name);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                           provenance.data()),
                                       provenance.size()),
                      "settings-source");
        }
        for (const bool cloudy : { false, true }) {
            MetaStore source;
            settings_xmp(source, "LightSource",
                         make_text(source.arena(),
                                   cloudy ? "Cloudy weather" : "Tungsten",
                                   TextEncoding::Unknown));
            source.finalize();
            ASSERT_EQ(
                translate_xmp_light_source_metadata(source, {}, &source).status,
                SettingsStatus::Ok);
            EXPECT_EQ(settings_find(source, 0x9208U)->value.data.u64,
                      cloudy ? 10U : 3U);
        }
    }

    TEST(MetadataLightSource, AmbiguousDaylightNeverUsesNativeOrCaptureHints)
    {
        for (const SettingsPolicy policy :
             { SettingsPolicy::FailOnConflict, SettingsPolicy::PreserveExisting,
               SettingsPolicy::ReplaceExisting }) {
            for (const uint16_t native_code : { 1U, 25U, 255U }) {
                MetaStore source;
                settings_xmp(source, "LightSource",
                             make_text(source.arena(), "Daylight",
                                       TextEncoding::Ascii));
                if (native_code != 255U)
                    settings_native(source, 0x9208U, make_u16(native_code));
                settings_native(source, 0xa403U, make_u16(0U));
                settings_native(source, 0x9209U, make_u16(0U));
                LightOptions options;
                options.conflict_policy = policy;
                light_failure(source, SettingsStatus::AmbiguousSource, options);
                MetaStore output;
                const auto result
                    = translate_xmp_light_source_metadata(source, options,
                                                          &output);
                EXPECT_EQ(result.failed_mapping,
                          MetadataCaptureTranslationMapping::XmpLightSource);
                EXPECT_EQ(result.failed_source_entry, 0U);
            }
        }
    }

    TEST(MetadataLightSource, DirtySelectionNamespaceAndShapesAreExplicit)
    {
        MetaStore source;
        settings_xmp(source, "LightSource", make_u16(25U), EntryFlags::None);
        settings_xmp(source, "LightSource", make_u16(3U), EntryFlags::Dirty,
                     "urn:foreign");
        settings_xmp(source, "LightSourceExtra", make_u16(3U));
        settings_native(source, 0x9209U, make_u16(95U));
        settings_native(source, 0xa403U, make_u16(1U));
        source.finalize();
        MetaStore output;
        ASSERT_EQ(
            translate_xmp_light_source_metadata(source, {}, &output).status,
            SettingsStatus::Ok);
        EXPECT_EQ(settings_find(output, 0x9208U), nullptr);
        LightOptions all;
        all.source_mode = MetadataCaptureTranslationSourceMode::All;
        ASSERT_EQ(
            translate_xmp_light_source_metadata(source, all, &output).status,
            SettingsStatus::Ok);
        EXPECT_EQ(settings_find(output, 0x9208U)->value.data.u64, 25U);
        EXPECT_EQ(settings_find(output, 0x9209U)->value.data.u64, 95U);
        EXPECT_EQ(settings_find(output, 0xa403U)->value.data.u64, 1U);
        for (const std::string_view path :
             { "LightSource[1]", "LightSource/?xml:lang",
               "LightSource/Value" }) {
            MetaStore bad;
            settings_xmp(bad, path, make_u16(1U));
            light_failure(bad, SettingsStatus::UnsupportedSourceShape);
        }
        MetaStore duplicate;
        settings_xmp(duplicate, "LightSource", make_u16(1U));
        settings_xmp(duplicate, "LightSource", make_u16(1U));
        light_failure(duplicate, SettingsStatus::AmbiguousSource);
    }

    TEST(MetadataLightSource, RejectsCoercionMalformedTextAndOverflow)
    {
        for (const std::string_view text :
             { "", "daylight", "D65 ", " 21", "+21", "-1", "21.0", "2.1e1",
               "21/1", "Tungsten (Incandescent)", "6500K" }) {
            SCOPED_TRACE(text);
            MetaStore source;
            settings_xmp(source, "LightSource",
                         make_text(source.arena(), text, TextEncoding::Ascii));
            light_failure(source, SettingsStatus::InvalidNumericValue);
        }
        for (const std::string_view text :
             { "65535", "18446744073709551616" }) {
            MetaStore source;
            settings_xmp(source, "LightSource",
                         make_text(source.arena(), text, TextEncoding::Ascii));
            light_failure(source, SettingsStatus::ValueOutOfRange);
        }
        for (const MetaValue value :
             { make_i32(-1), make_u64(UINT64_MAX),
               make_f64_bits(0x3ff0000000000000ULL), make_urational(1U, 1U),
               make_srational(1, 1) }) {
            MetaStore source;
            settings_xmp(source, "LightSource", value);
            light_failure(source,
                          value.elem_type == MetaElementType::I32
                                  || value.elem_type == MetaElementType::U64
                              ? SettingsStatus::ValueOutOfRange
                              : SettingsStatus::InvalidSourceValue);
        }
        MetaStore array;
        MetaValue value = make_u16(1U);
        value.count     = 2U;
        settings_xmp(array, "LightSource", value);
        light_failure(array, SettingsStatus::InvalidSourceValue);
        MetaStore encoding;
        settings_xmp(encoding, "LightSource",
                     make_text(encoding.arena(), "D65", TextEncoding::Utf16LE));
        light_failure(encoding, SettingsStatus::InvalidSourceValue);
        MetaStore signed_value;
        settings_xmp(signed_value, "LightSource", make_i32(25));
        signed_value.finalize();
        ASSERT_EQ(translate_xmp_light_source_metadata(signed_value, {},
                                                      &signed_value)
                      .status,
                  SettingsStatus::Ok);
        EXPECT_EQ(settings_find(signed_value, 0x9208U)->value.data.u64, 25U);
    }

    TEST(MetadataLightSource, NativeTypeConflictsAndDuplicateRepair)
    {
        MetaStore source;
        settings_xmp(source, "LightSource", make_u16(25U));
        settings_native(source, 0x9208U, make_u32(25U));
        settings_native(source, 0x9208U, make_u16(1U));
        light_failure(source, SettingsStatus::NativeConflict);
        LightOptions options;
        options.conflict_policy = SettingsPolicy::PreserveExisting;
        MetaStore output;
        EXPECT_EQ(translate_xmp_light_source_metadata(source, options, &output)
                      .groups_preserved,
                  1U);
        EXPECT_EQ(settings_active_count(output, 0x9208U), 2U);
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        const auto result = translate_xmp_light_source_metadata(source, options,
                                                                &output);
        ASSERT_EQ(result.status, SettingsStatus::Ok);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_EQ(result.entries_removed, 1U);
        ASSERT_EQ(settings_active_count(output, 0x9208U), 1U);
        EXPECT_EQ(settings_find(output, 0x9208U)->value.elem_type,
                  MetaElementType::U16);
        EXPECT_EQ(settings_find(output, 0x9208U)->value.data.u64, 25U);
    }

    TEST(MetadataLightSource,
         ExplicitDeletionRetainsUnrelatedCaptureAndOmission)
    {
        MetaStore source;
        settings_xmp(source, "LightSource",
                     make_text(source.arena(), "Daylight", TextEncoding::Ascii),
                     EntryFlags::Dirty | EntryFlags::Deleted);
        settings_native(source, 0x9208U, make_u16(25U));
        settings_native(source, 0x9208U, make_u16(1U));
        settings_native(source, 0x9209U, make_u16(95U));
        settings_native(source, 0xa403U, make_u16(1U));
        settings_native(source, 0xa20bU, make_urational(7U, 3U));
        light_failure(source, SettingsStatus::NativeConflict);
        LightOptions options;
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        const auto result = translate_xmp_light_source_metadata(source, options,
                                                                &source);
        ASSERT_EQ(result.status, SettingsStatus::Ok);
        EXPECT_EQ(result.entries_removed, 2U);
        EXPECT_EQ(settings_find(source, 0x9208U), nullptr);
        EXPECT_EQ(settings_find(source, 0x9209U)->value.data.u64, 95U);
        EXPECT_EQ(settings_find(source, 0xa403U)->value.data.u64, 1U);
        EXPECT_NE(settings_find(source, 0xa20bU), nullptr);
        MetaStore omitted;
        settings_native(omitted, 0x9208U, make_u16(25U));
        settings_xmp(omitted, "LightSource", make_u16(1U), EntryFlags::Deleted);
        omitted.finalize();
        ASSERT_EQ(translate_xmp_light_source_metadata(omitted, options, &omitted)
                      .status,
                  SettingsStatus::Ok);
        EXPECT_EQ(settings_find(omitted, 0x9208U)->value.data.u64, 25U);
    }

    TEST(MetadataLightSource, ResourceFailuresPreserveAliasedAndSeparateOutput)
    {
        MetaStore source;
        settings_xmp(source, "LightSource",
                     make_text(source.arena(), "D65", TextEncoding::Ascii));
        LightOptions options;
        options.max_text_bytes_per_property = 2U;
        light_failure(source, SettingsStatus::ValueTooLong, options);
        options                      = {};
        options.max_total_text_bytes = 2U;
        light_failure(source, SettingsStatus::SourceLimitExceeded, options);
        options                   = {};
        options.max_added_entries = 2U;
        light_failure(source, SettingsStatus::InvalidOptions, options);
        options             = {};
        options.source_mode = static_cast<MetadataCaptureTranslationSourceMode>(
            255U);
        light_failure(source, SettingsStatus::InvalidOptions, options);
        MetaStore duplicate;
        settings_xmp(duplicate, "LightSource", make_u16(25U));
        settings_native(duplicate, 0x9208U, make_u16(1U));
        settings_native(duplicate, 0x9208U, make_u16(1U));
        options                 = {};
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        options.max_operations  = 1U;
        light_failure(duplicate, SettingsStatus::OperationLimitExceeded,
                      options);
        MetaStore unfinalized;
        MetaStore output;
        EXPECT_EQ(translate_xmp_light_source_metadata(unfinalized, {}, nullptr)
                      .status,
                  SettingsStatus::NullOutput);
        EXPECT_EQ(translate_xmp_light_source_metadata(unfinalized, {}, &output)
                      .status,
                  SettingsStatus::SourceNotFinalized);
        EXPECT_STREQ(metadata_capture_translation_mapping_name(
                         MetadataCaptureTranslationMapping::XmpLightSource),
                     "xmp_light_source");
    }

    TEST(MetadataLightSource, NativePortableRoundTripRetainsEveryDefinedCode)
    {
        for (const uint16_t code : kLightCodes) {
            MetaStore native;
            settings_native(native, 0x9208U, make_u16(code));
            native.finalize();
            XmpPortableOptions options;
            options.include_existing_xmp = false;
            std::array<std::byte, 4096> bytes {};
            const auto dumped = dump_xmp_portable(native, bytes, options);
            ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
            const std::string_view xml(reinterpret_cast<const char*>(
                                           bytes.data()),
                                       dumped.written);
            if (code == 1U || code == 25U) {
                const std::string expected = "<exif:LightSource>"
                                             + std::to_string(code)
                                             + "</exif:LightSource>";
                EXPECT_NE(xml.find(expected), std::string_view::npos);
            }
            MetaStore restored;
            ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(), dumped.written),
                                        restored)
                          .status,
                      XmpDecodeStatus::Ok);
            restored.finalize();
            LightOptions all;
            all.source_mode = MetadataCaptureTranslationSourceMode::All;
            ASSERT_EQ(translate_xmp_light_source_metadata(restored, all,
                                                          &restored)
                          .status,
                      SettingsStatus::Ok)
                << code;
            ASSERT_NE(settings_find(restored, 0x9208U), nullptr);
            EXPECT_EQ(settings_find(restored, 0x9208U)->value.data.u64, code);
        }
    }
}  // namespace
}  // namespace openmeta

namespace openmeta {
namespace {
    using SensitivityOptions = MetadataSensitivityTranslationOptions;
    static constexpr std::string_view kSensitivityNs
        = "http://cipa.jp/exif/1.0/";
    static constexpr std::array<std::string_view, 7> kSensitivityNames
        = { "PhotographicSensitivity",
            "SensitivityType",
            "StandardOutputSensitivity",
            "RecommendedExposureIndex",
            "ISOSpeed",
            "ISOSpeedLatitudeyyy",
            "ISOSpeedLatitudezzz" };
    static constexpr std::array<uint16_t, 7> kSensitivityTags
        = { 0x8827U, 0x8830U, 0x8831U, 0x8832U, 0x8833U, 0x8834U, 0x8835U };
    static void sensitivity_source(MetaStore& source, uint16_t base = 400U,
                                   uint16_t type = 7U, uint32_t extended = 400U,
                                   EntryFlags flags = EntryFlags::Dirty)
    {
        for (size_t i = 0U; i < kSensitivityNames.size(); ++i)
            settings_xmp(source, kSensitivityNames[i],
                         make_u32(i == 0U ? base : (i == 1U ? type : extended)),
                         flags, kSensitivityNs);
    }
    static void sensitivity_failure(MetaStore& source, SettingsStatus status,
                                    const SensitivityOptions& options = {})
    {
        source.finalize();
        const size_t size = source.entries().size();
        MetaStore output;
        settings_native(output, 0x9209U, make_u16(95U));
        output.finalize();
        EXPECT_EQ(
            translate_xmp_sensitivity_metadata(source, options, &output).status,
            status);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(settings_find(output, 0x9209U)->value.data.u64, 95U);
        EXPECT_EQ(
            translate_xmp_sensitivity_metadata(source, options, &source).status,
            status);
        EXPECT_EQ(source.entries().size(), size);
        for (uint16_t tag : kSensitivityTags)
            EXPECT_EQ(settings_find(source, tag), nullptr);
    }
    TEST(MetadataSensitivity, AllTypesAndLimitsRetainExactTypedValues)
    {
        constexpr std::array<uint32_t, 4> values = { 1U, 65534U, 65535U,
                                                     UINT32_MAX };
        for (uint16_t type = 0U; type <= 7U; ++type) {
            for (uint32_t value : values) {
                MetaStore source;
                sensitivity_source(source,
                                   static_cast<uint16_t>(
                                       value >= 65535U ? 65535U : value),
                                   type, value);
                source.finalize();
                const auto result
                    = translate_xmp_sensitivity_metadata(source, {}, &source);
                ASSERT_EQ(result.status, SettingsStatus::Ok)
                    << type << ' ' << value;
                EXPECT_EQ(result.groups_translated, 1U);
                EXPECT_EQ(result.source_properties, 7U);
                EXPECT_EQ(result.entries_added, 7U);
                for (size_t i = 0U; i < kSensitivityTags.size(); ++i) {
                    const auto* entry = settings_find(source,
                                                      kSensitivityTags[i]);
                    ASSERT_NE(entry, nullptr);
                    EXPECT_EQ(entry->value.elem_type,
                              i < 2U ? MetaElementType::U16
                                     : MetaElementType::U32);
                    EXPECT_EQ(entry->value.data.u64,
                              i == 0U ? (value >= 65535U ? 65535U : value)
                                      : (i == 1U ? type : value));
                }
                const auto repeated
                    = translate_xmp_sensitivity_metadata(source, {}, &source);
                EXPECT_EQ(repeated.status, SettingsStatus::Ok);
                EXPECT_EQ(repeated.groups_unchanged, 1U);
                EXPECT_EQ(repeated.groups_translated, 0U);
            }
        }
    }
    TEST(MetadataSensitivity,
         TypeSelectsRelationshipsWithoutInferringMissingValues)
    {
        for (uint16_t type = 0U; type <= 7U; ++type) {
            MetaStore minimal;
            settings_xmp(minimal, kSensitivityNames[0], make_u16(65535U),
                         EntryFlags::Dirty, kSensitivityNs);
            settings_xmp(minimal, kSensitivityNames[1], make_u16(type),
                         EntryFlags::Dirty, kSensitivityNs);
            minimal.finalize();
            ASSERT_EQ(translate_xmp_sensitivity_metadata(minimal, {}, &minimal)
                          .status,
                      SettingsStatus::Ok);
            for (size_t i = 2U; i < kSensitivityTags.size(); ++i)
                EXPECT_EQ(settings_find(minimal, kSensitivityTags[i]), nullptr);
        }
        MetaStore source;
        settings_xmp(source, kSensitivityNames[0], make_u16(400U),
                     EntryFlags::Dirty, kSensitivityNs);
        settings_xmp(source, kSensitivityNames[1], make_u16(1U),
                     EntryFlags::Dirty, kSensitivityNs);
        settings_xmp(source, kSensitivityNames[2], make_u32(400U),
                     EntryFlags::Dirty, kSensitivityNs);
        settings_xmp(source, kSensitivityNames[3], make_u32(800U),
                     EntryFlags::Dirty, kSensitivityNs);
        source.finalize();
        EXPECT_EQ(translate_xmp_sensitivity_metadata(source, {}, &source).status,
                  SettingsStatus::Ok);
        EXPECT_EQ(settings_find(source, 0x8832U)->value.data.u64, 800U);
    }
    TEST(MetadataSensitivity, RejectsIncompleteAndContradictoryGroupsAtomically)
    {
        for (size_t missing : { 0U, 1U, 4U, 5U, 6U }) {
            MetaStore source;
            for (size_t i = 0U; i < kSensitivityNames.size(); ++i) {
                if (i != missing)
                    settings_xmp(source, kSensitivityNames[i],
                                 make_u32(i == 1U ? 7U : 400U),
                                 EntryFlags::Dirty, kSensitivityNs);
            }
            sensitivity_failure(source, SettingsStatus::IncompleteSource);
        }
        for (uint32_t wrong : { 401U, 65536U }) {
            MetaStore source;
            for (size_t i = 0U; i < 4U; ++i)
                settings_xmp(source, kSensitivityNames[i],
                             make_u32(i == 0U ? (wrong > 65535U ? 65535U : 400U)
                                              : (i == 1U ? 4U
                                                         : (i == 2U ? wrong
                                                                    : 400U))),
                             EntryFlags::Dirty, kSensitivityNs);
            sensitivity_failure(source, SettingsStatus::InvalidNumericValue);
        }
        MetaStore saturated;
        settings_xmp(saturated, kSensitivityNames[0], make_u16(65535U),
                     EntryFlags::Dirty, kSensitivityNs);
        settings_xmp(saturated, kSensitivityNames[1], make_u16(4U),
                     EntryFlags::Dirty, kSensitivityNs);
        settings_xmp(saturated, kSensitivityNames[2], make_u32(100000U),
                     EntryFlags::Dirty, kSensitivityNs);
        settings_xmp(saturated, kSensitivityNames[3], make_u32(200000U),
                     EntryFlags::Dirty, kSensitivityNs);
        sensitivity_failure(saturated, SettingsStatus::InvalidNumericValue);
    }
    TEST(MetadataSensitivity,
         DirtySelectionReadsCleanCompanionsAndIgnoresWrongNamespace)
    {
        MetaStore clean;
        sensitivity_source(clean, 400U, 7U, 400U, EntryFlags::None);
        settings_xmp(clean, "ISOSpeed", make_u32(800U), EntryFlags::Dirty,
                     "urn:unrelated");
        clean.finalize();
        EXPECT_EQ(translate_xmp_sensitivity_metadata(clean, {}, &clean)
                      .groups_translated,
                  0U);
        MetaEdit edit;
        edit.set_value(0U, make_u16(400U));
        MetaStore dirty = commit(clean, std::span<const MetaEdit>(&edit, 1U));
        EXPECT_EQ(
            translate_xmp_sensitivity_metadata(dirty, {}, &dirty).entries_added,
            7U);
        SensitivityOptions all;
        all.source_mode = MetadataCaptureTranslationSourceMode::All;
        EXPECT_EQ(translate_xmp_sensitivity_metadata(clean, all, &clean)
                      .entries_added,
                  7U);
    }
    TEST(MetadataSensitivity, StrictNumericShapesAliasesAndLimits)
    {
        for (size_t i = 0U; i < kSensitivityNames.size(); ++i) {
            for (const std::string text :
                 { std::string("-1"), std::string("1.5"),
                   std::string("4294967296"), std::string("0") }) {
                if (i == 1U && text == "0")
                    continue;
                MetaStore source;
                for (size_t j = 0U; j < kSensitivityNames.size(); ++j)
                    settings_xmp(source, kSensitivityNames[j],
                                 j == i ? make_text(source.arena(), text,
                                                    TextEncoding::Ascii)
                                        : make_u32(j == 1U ? 7U : 400U),
                                 EntryFlags::Dirty, kSensitivityNs);
                sensitivity_failure(source,
                                    text == "-1" || text == "1.5"
                                        ? SettingsStatus::InvalidNumericValue
                                        : SettingsStatus::ValueOutOfRange);
            }
        }
        for (std::string_view path :
             { "SensitivityType[1]", "ISOSpeed/a", "ISOSpeedRatings[2]",
               "ISOSpeedRatings[01]" }) {
            MetaStore source;
            sensitivity_source(source);
            settings_xmp(source, path, make_u16(7U), EntryFlags::Dirty,
                         path.starts_with("ISOSpeedRatings") ? kSettingsNs
                                                             : kSensitivityNs);
            sensitivity_failure(source, SettingsStatus::UnsupportedSourceShape);
        }
        for (std::string_view alias :
             { "ISO", "ISOSpeedRatings", "ISOSpeedRatings[1]" }) {
            MetaStore source;
            sensitivity_source(source);
            settings_xmp(source, alias, make_u16(400U));
            sensitivity_failure(source, SettingsStatus::AmbiguousSource);
        }
        MetaStore signed_value;
        settings_xmp(signed_value, kSensitivityNames[0], make_i32(400),
                     EntryFlags::Dirty, kSensitivityNs);
        sensitivity_failure(signed_value, SettingsStatus::InvalidSourceValue);
    }
    TEST(MetadataSensitivity,
         ConflictPolicyAppliesToWholeGroupAndRepairsStaleValues)
    {
        MetaStore source;
        settings_xmp(source, kSensitivityNames[0], make_u16(400U),
                     EntryFlags::Dirty, kSensitivityNs);
        settings_xmp(source, kSensitivityNames[1], make_u16(1U),
                     EntryFlags::Dirty, kSensitivityNs);
        settings_native(source, 0x8827U, make_u16(400U));
        settings_native(source, 0x8833U, make_u32(800U));
        settings_native(source, 0x8833U, make_u16(800U));
        settings_native(source, 0x9209U, make_u16(95U));
        source.finalize();
        MetaStore output;
        EXPECT_EQ(translate_xmp_sensitivity_metadata(source, {}, &output).status,
                  SettingsStatus::NativeConflict);
        EXPECT_TRUE(output.entries().empty());
        SensitivityOptions options;
        options.conflict_policy = SettingsPolicy::PreserveExisting;
        EXPECT_EQ(translate_xmp_sensitivity_metadata(source, options, &output)
                      .groups_preserved,
                  1U);
        EXPECT_EQ(settings_find(output, 0x8830U), nullptr);
        EXPECT_EQ(settings_active_count(output, 0x8833U), 2U);
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        const auto result = translate_xmp_sensitivity_metadata(source, options,
                                                               &source);
        EXPECT_EQ(result.status, SettingsStatus::Ok);
        EXPECT_EQ(result.entries_added, 1U);
        EXPECT_EQ(result.entries_removed, 2U);
        EXPECT_EQ(result.groups_translated, 1U);
        EXPECT_EQ(settings_find(source, 0x8833U), nullptr);
        EXPECT_EQ(settings_find(source, 0x9209U)->value.data.u64, 95U);
    }
    TEST(MetadataSensitivity,
         BaseTombstoneRemovesGroupAndRejectsActiveCompanions)
    {
        MetaStore source;
        settings_xmp(source, kSensitivityNames[0], make_u16(400U),
                     EntryFlags::Dirty | EntryFlags::Deleted, kSensitivityNs);
        for (size_t i = 0U; i < kSensitivityTags.size(); ++i)
            settings_native(source, kSensitivityTags[i],
                            i < 2U ? make_u16(1U) : make_u32(1U));
        source.finalize();
        SensitivityOptions options;
        EXPECT_EQ(
            translate_xmp_sensitivity_metadata(source, options, &source).status,
            SettingsStatus::NativeConflict);
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        const auto result = translate_xmp_sensitivity_metadata(source, options,
                                                               &source);
        EXPECT_EQ(result.status, SettingsStatus::Ok);
        EXPECT_EQ(result.entries_removed, 7U);
        EXPECT_EQ(result.groups_translated, 1U);
        MetaStore active;
        settings_xmp(active, kSensitivityNames[0], make_u16(400U),
                     EntryFlags::Dirty | EntryFlags::Deleted, kSensitivityNs);
        settings_xmp(active, kSensitivityNames[1], make_u16(1U),
                     EntryFlags::None, kSensitivityNs);
        sensitivity_failure(active, SettingsStatus::IncompleteSource, options);
        MetaStore orphan;
        settings_xmp(orphan, kSensitivityNames[4], make_u32(400U),
                     EntryFlags::Dirty | EntryFlags::Deleted, kSensitivityNs);
        sensitivity_failure(orphan, SettingsStatus::IncompleteSource, options);
    }
    TEST(MetadataSensitivity,
         ResourceFailuresAndInvalidOptionsLeaveOutputUntouched)
    {
        MetaStore source;
        for (size_t i = 0U; i < kSensitivityNames.size(); ++i)
            settings_xmp(source, kSensitivityNames[i],
                         make_text(source.arena(), i == 1U ? "7" : "400",
                                   TextEncoding::Ascii),
                         EntryFlags::Dirty, kSensitivityNs);
        SensitivityOptions options;
        options.max_added_entries = 6U;
        sensitivity_failure(source, SettingsStatus::EntryLimitExceeded,
                            options);
        options                = {};
        options.max_operations = 6U;
        sensitivity_failure(source, SettingsStatus::OperationLimitExceeded,
                            options);
        options                      = {};
        options.max_total_text_bytes = 5U;
        sensitivity_failure(source, SettingsStatus::SourceLimitExceeded,
                            options);
        options                             = {};
        options.max_text_bytes_per_property = 2U;
        sensitivity_failure(source, SettingsStatus::ValueTooLong, options);
        options                   = {};
        options.max_added_entries = 8U;
        sensitivity_failure(source, SettingsStatus::InvalidOptions, options);
        options                 = {};
        options.conflict_policy = static_cast<SettingsPolicy>(255U);
        sensitivity_failure(source, SettingsStatus::InvalidOptions, options);
        MetaStore unfinalized;
        EXPECT_EQ(
            translate_xmp_sensitivity_metadata(unfinalized, {}, &source).status,
            SettingsStatus::SourceNotFinalized);
        EXPECT_EQ(translate_xmp_sensitivity_metadata(source, {}, nullptr).status,
                  SettingsStatus::NullOutput);
    }
    TEST(MetadataSensitivity,
         PortableExistingStandardGroupDoesNotCreateDuplicateBase)
    {
        MetaStore source;
        sensitivity_source(source, 65535U, 7U, 102400U);
        source.finalize();
        ASSERT_EQ(translate_xmp_sensitivity_metadata(source, {}, &source).status,
                  SettingsStatus::Ok);
        XmpPortableOptions options;
        options.include_existing_xmp = true;
        options.conflict_policy      = XmpConflictPolicy::ExistingWins;
        std::array<std::byte, 8192> bytes {};
        const auto dumped = dump_xmp_portable(source, bytes, options);
        ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
        MetaStore restored;
        ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(), dumped.written),
                                    restored)
                      .status,
                  XmpDecodeStatus::Ok);
        restored.finalize();
        SensitivityOptions all;
        all.source_mode   = MetadataCaptureTranslationSourceMode::All;
        const auto result = translate_xmp_sensitivity_metadata(restored, all,
                                                               &restored);
        EXPECT_EQ(result.status, SettingsStatus::Ok);
        EXPECT_EQ(result.source_properties, 7U);
        EXPECT_EQ(result.entries_added, 7U);
    }

    TEST(MetadataSensitivity,
         PortableCanonicalizationReconcilesManagedCompanions)
    {
        MetaStore source;
        for (size_t i = 0U; i < kSensitivityNames.size(); ++i) {
            settings_xmp(source, kSensitivityNames[i],
                         make_u32(i == 1U ? 7U : 100U), EntryFlags::None,
                         kSensitivityNs);
            settings_native(source, kSensitivityTags[i],
                            i < 2U ? make_u16(i == 1U ? 7U : 200U)
                                   : make_u32(200U));
        }
        source.finalize();
        for (const bool canonical : { false, true }) {
            XmpPortableOptions options;
            options.include_existing_xmp = true;
            options.conflict_policy      = XmpConflictPolicy::ExistingWins;
            if (canonical)
                options.existing_standard_namespace_policy
                    = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
            std::array<std::byte, 8192> bytes {};
            const auto dumped = dump_xmp_portable(source, bytes, options);
            ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
            MetaStore restored;
            ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(), dumped.written),
                                        restored)
                          .status,
                      XmpDecodeStatus::Ok);
            restored.finalize();
            SensitivityOptions all;
            all.source_mode = MetadataCaptureTranslationSourceMode::All;
            ASSERT_EQ(translate_xmp_sensitivity_metadata(restored, all,
                                                         &restored)
                          .status,
                      SettingsStatus::Ok);
            for (size_t i = 0U; i < kSensitivityTags.size(); ++i)
                EXPECT_EQ(settings_find(restored, kSensitivityTags[i])
                              ->value.data.u64,
                          i == 1U ? 7U : (canonical ? 200U : 100U));
        }
    }

    TEST(MetadataSensitivity,
         NativePortableRoundTripPreservesHighValuesAndNamespaces)
    {
        MetaStore source;
        sensitivity_source(source, 65535U, 7U, UINT32_MAX);
        source.finalize();
        ASSERT_EQ(translate_xmp_sensitivity_metadata(source, {}, &source).status,
                  SettingsStatus::Ok);
        XmpPortableOptions options;
        options.include_existing_xmp = false;
        std::array<std::byte, 8192> bytes {};
        const auto dumped = dump_xmp_portable(source, bytes, options);
        ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
        const std::string_view xml(reinterpret_cast<const char*>(bytes.data()),
                                   dumped.written);
        EXPECT_NE(xml.find("<exifEX:ISOSpeed>4294967295</exifEX:ISOSpeed>"),
                  std::string_view::npos);
        MetaStore restored;
        ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(), dumped.written),
                                    restored)
                      .status,
                  XmpDecodeStatus::Ok);
        restored.finalize();
        SensitivityOptions all;
        all.source_mode = MetadataCaptureTranslationSourceMode::All;
        ASSERT_EQ(
            translate_xmp_sensitivity_metadata(restored, all, &restored).status,
            SettingsStatus::Ok);
        for (size_t i = 0U; i < kSensitivityTags.size(); ++i)
            EXPECT_EQ(
                settings_find(restored, kSensitivityTags[i])->value.data.u64,
                settings_find(source, kSensitivityTags[i])->value.data.u64);
    }
}  // namespace
}  // namespace openmeta

namespace openmeta {
namespace {
    using CameraTextOptions = MetadataCameraTextTranslationOptions;
    using CameraTextStatus  = MetadataTechnicalTranslationStatus;
    using CameraTextPolicy  = MetadataTechnicalTranslationConflictPolicy;
    static constexpr std::array<std::string_view, 6> kCameraTextNames
        = { "SpectralSensitivity", "CameraOwnerName",
            "BodySerialNumber",    "LensMake",
            "LensModel",           "LensSerialNumber" };
    static constexpr std::array<uint16_t, 6> kCameraTextTags
        = { 0x8824U, 0xa430U, 0xa431U, 0xa433U, 0xa434U, 0xa435U };
    static constexpr std::array<bool CameraTextOptions::*, 6> kCameraTextFlags
        = { &CameraTextOptions::spectral_sensitivity_to_exif,
            &CameraTextOptions::camera_owner_name_to_exif,
            &CameraTextOptions::body_serial_number_to_exif,
            &CameraTextOptions::lens_make_to_exif,
            &CameraTextOptions::lens_model_to_exif,
            &CameraTextOptions::lens_serial_number_to_exif };
    static void camera_text_source(
        MetaStore& source, std::string_view text = "  A & <B> \"C\" 'D'  ",
        EntryFlags flags = EntryFlags::Dirty, bool legacy = false)
    {
        for (size_t i = 0U; i < kCameraTextNames.size(); ++i)
            settings_xmp(source, kCameraTextNames[i],
                         make_text(source.arena(), text, TextEncoding::Utf8),
                         flags,
                         i == 0U || legacy ? kSettingsNs : kSensitivityNs);
    }
    static std::string_view camera_text_value(const MetaStore& store,
                                              uint16_t tag)
    {
        const Entry* entry = settings_find(store, tag);
        if (!entry)
            return {};
        const auto bytes = store.arena().span(entry->value.data.span);
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }
    static void camera_text_failure(MetaStore& source, CameraTextStatus status,
                                    const CameraTextOptions& options = {})
    {
        source.finalize();
        const size_t size = source.entries().size();
        MetaStore output;
        settings_native(output, 0x9209U, make_u16(95U));
        output.finalize();
        EXPECT_EQ(
            translate_xmp_camera_text_metadata(source, options, &output).status,
            status);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(settings_find(output, 0x9209U)->value.data.u64, 95U);
        EXPECT_EQ(
            translate_xmp_camera_text_metadata(source, options, &source).status,
            status);
        EXPECT_EQ(source.entries().size(), size);
        for (uint16_t tag : kCameraTextTags)
            EXPECT_EQ(settings_find(source, tag), nullptr);
    }
    TEST(MetadataCameraText,
         BatchWritesSixIndependentAsciiFieldsWithOwnedProvenance)
    {
        for (const bool legacy : { false, true }) {
            MetaStore source;
            camera_text_source(source, "  A & <B> \"C\" 'D'  ",
                               EntryFlags::Dirty, legacy);
            settings_native(source, 0x829aU, make_urational(1U, 125U));
            source.finalize();
            MetaStore output;
            const auto result = translate_xmp_camera_text_metadata(source, {},
                                                                   &output);
            ASSERT_EQ(result.status, CameraTextStatus::Ok);
            EXPECT_EQ(result.source_properties, 6U);
            EXPECT_EQ(result.groups_translated, 6U);
            EXPECT_EQ(result.entries_added, 6U);
            source = MetaStore {};
            for (uint16_t tag : kCameraTextTags) {
                ASSERT_NE(settings_find(output, tag), nullptr);
                EXPECT_EQ(settings_find(output, tag)->value.kind,
                          MetaValueKind::Text);
                EXPECT_EQ(settings_find(output, tag)->value.text_encoding,
                          TextEncoding::Ascii);
                EXPECT_EQ(camera_text_value(output, tag),
                          "  A & <B> \"C\" 'D'  ");
                const auto wire = output.arena().span(
                    settings_find(output, tag)->origin.wire_type_name);
                EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                               wire.data()),
                                           wire.size()),
                          "settings-source");
            }
            EXPECT_NE(settings_find(output, 0x829aU), nullptr);
            EXPECT_EQ(translate_xmp_camera_text_metadata(output, {}, &output)
                          .groups_unchanged,
                      6U);
        }
    }
    TEST(MetadataCameraText,
         SelectionModesAndIndividualSwitchesRetainOmittedFields)
    {
        for (size_t disabled = 0U; disabled < kCameraTextNames.size();
             ++disabled) {
            MetaStore source;
            camera_text_source(source);
            settings_native(source, kCameraTextTags[disabled],
                            make_text(source.arena(), "retained",
                                      TextEncoding::Ascii));
            source.finalize();
            CameraTextOptions options;
            options.*kCameraTextFlags[disabled] = false;
            const auto result
                = translate_xmp_camera_text_metadata(source, options, &source);
            EXPECT_EQ(result.status, CameraTextStatus::Ok);
            EXPECT_EQ(result.entries_added, 5U);
            EXPECT_EQ(camera_text_value(source, kCameraTextTags[disabled]),
                      "retained");
        }
        MetaStore clean;
        camera_text_source(clean, "clean", EntryFlags::None);
        settings_xmp(clean, "LensMake[1]", make_u16(1U), EntryFlags::None,
                     "urn:unrelated");
        settings_xmp(clean, "SpectralSensitivity", make_u16(1U),
                     EntryFlags::Dirty, kSensitivityNs);
        settings_xmp(clean, "Lens", make_u16(1U), EntryFlags::Dirty,
                     "http://ns.adobe.com/exif/1.0/aux/");
        clean.finalize();
        EXPECT_EQ(translate_xmp_camera_text_metadata(clean, {}, &clean)
                      .groups_translated,
                  0U);
        CameraTextOptions all;
        all.source_mode = MetadataTechnicalTranslationSourceMode::All;
        EXPECT_EQ(translate_xmp_camera_text_metadata(clean, all, &clean)
                      .entries_added,
                  6U);
    }
    TEST(MetadataCameraText,
         RejectsNonPrintableEmptyAndWrongTypedSourcesAtomically)
    {
        const std::array<std::string, 7> invalid
            = { "",     std::string("a\0b", 3), "a\nb",       "a\rb",
                "a\tb", std::string(1, '\x7f'), "caf\xc3\xa9" };
        for (size_t field = 0U; field < kCameraTextNames.size(); ++field) {
            for (const auto& value : invalid) {
                MetaStore source;
                settings_xmp(source, kCameraTextNames[field],
                             make_text(source.arena(), value,
                                       TextEncoding::Utf8),
                             EntryFlags::Dirty,
                             field == 0U ? kSettingsNs : kSensitivityNs);
                camera_text_failure(source,
                                    value.empty()
                                        ? CameraTextStatus::InvalidSourceValue
                                        : CameraTextStatus::NonAsciiSource);
            }
            for (const auto encoding :
                 { TextEncoding::Unknown, TextEncoding::Utf16LE,
                   TextEncoding::Utf16BE }) {
                MetaStore source;
                settings_xmp(source, kCameraTextNames[field],
                             make_text(source.arena(), "plain", encoding),
                             EntryFlags::Dirty,
                             field == 0U ? kSettingsNs : kSensitivityNs);
                camera_text_failure(source,
                                    CameraTextStatus::InvalidSourceValue);
            }
            MetaStore scalar;
            settings_xmp(scalar, kCameraTextNames[field], make_u32(100U),
                         EntryFlags::Dirty,
                         field == 0U ? kSettingsNs : kSensitivityNs);
            camera_text_failure(scalar, CameraTextStatus::InvalidSourceValue);
        }
    }
    TEST(MetadataCameraText,
         RejectsDuplicateAliasesAndMalformedShapesBeforePolicies)
    {
        for (size_t field = 0U; field < kCameraTextNames.size(); ++field) {
            MetaStore duplicate;
            camera_text_source(duplicate, "first");
            settings_xmp(duplicate, kCameraTextNames[field],
                         make_text(duplicate.arena(), "first",
                                   TextEncoding::Ascii));
            camera_text_failure(duplicate, CameraTextStatus::AmbiguousSource);
            for (std::string_view suffix :
                 { "[1]", "[@xml:lang=x-default]", "/nested" }) {
                MetaStore source;
                camera_text_source(source, "valid");
                const std::string path = std::string(kCameraTextNames[field])
                                         + std::string(suffix);
                settings_xmp(source, path, make_u16(1U), EntryFlags::Dirty,
                             field == 0U ? kSettingsNs : kSensitivityNs);
                for (auto policy : { CameraTextPolicy::PreserveExisting,
                                     CameraTextPolicy::FailOnConflict,
                                     CameraTextPolicy::ReplaceExisting }) {
                    CameraTextOptions options;
                    options.conflict_policy = policy;
                    camera_text_failure(source,
                                        CameraTextStatus::UnsupportedSourceShape,
                                        options);
                }
            }
        }
    }
    TEST(MetadataCameraText,
         PerFieldConflictsAndTypedDuplicateRepairAreOneTransaction)
    {
        MetaStore source;
        camera_text_source(source, "new");
        settings_native(source, kCameraTextTags[5],
                        make_text(source.arena(), "old", TextEncoding::Ascii));
        settings_native(source, kCameraTextTags[5], make_u16(42U));
        source.finalize();
        MetaStore output;
        EXPECT_EQ(translate_xmp_camera_text_metadata(source, {}, &output).status,
                  CameraTextStatus::NativeConflict);
        EXPECT_TRUE(output.entries().empty());
        CameraTextOptions options;
        options.conflict_policy = CameraTextPolicy::PreserveExisting;
        const auto preserved
            = translate_xmp_camera_text_metadata(source, options, &output);
        EXPECT_EQ(preserved.entries_added, 5U);
        EXPECT_EQ(preserved.groups_preserved, 1U);
        EXPECT_EQ(settings_active_count(output, kCameraTextTags[5]), 2U);
        options.conflict_policy = CameraTextPolicy::ReplaceExisting;
        const auto replaced
            = translate_xmp_camera_text_metadata(source, options, &source);
        ASSERT_EQ(replaced.status, CameraTextStatus::Ok);
        EXPECT_EQ(replaced.entries_added, 5U);
        EXPECT_EQ(replaced.entries_updated, 1U);
        EXPECT_EQ(replaced.entries_removed, 1U);
        for (auto tag : kCameraTextTags)
            EXPECT_EQ(camera_text_value(source, tag), "new");
    }
    TEST(MetadataCameraText,
         NativeEncodingAndTerminalNulsHaveExplicitEquivalence)
    {
        for (size_t field = 0U; field < kCameraTextNames.size(); ++field) {
            MetaStore source;
            settings_xmp(source, kCameraTextNames[field],
                         make_text(source.arena(), "exact", TextEncoding::Utf8),
                         EntryFlags::Dirty,
                         field == 0U ? kSettingsNs : kSensitivityNs);
            settings_native(source, kCameraTextTags[field],
                            make_text(source.arena(),
                                      std::string_view("exact\0\0", 7U),
                                      TextEncoding::Ascii));
            source.finalize();
            EXPECT_EQ(translate_xmp_camera_text_metadata(source, {}, &source)
                          .groups_unchanged,
                      1U);
            MetaEdit edit;
            edit.set_value(1U, make_u32(42U));
            source = commit(source, std::span<const MetaEdit>(&edit, 1U));
            EXPECT_EQ(
                translate_xmp_camera_text_metadata(source, {}, &source).status,
                CameraTextStatus::NativeConflict);
            CameraTextOptions options;
            options.conflict_policy = CameraTextPolicy::ReplaceExisting;
            EXPECT_EQ(translate_xmp_camera_text_metadata(source, options,
                                                         &source)
                          .entries_updated,
                      1U);
            EXPECT_EQ(settings_find(source, kCameraTextTags[field])
                          ->value.text_encoding,
                      TextEncoding::Ascii);
        }
    }
    TEST(MetadataCameraText,
         DirtyTombstonesRemoveSelectedFieldsAndIgnorePayloads)
    {
        MetaStore source;
        camera_text_source(source, "\n",
                           EntryFlags::Dirty | EntryFlags::Deleted);
        for (auto tag : kCameraTextTags)
            settings_native(source, tag,
                            make_text(source.arena(), "old",
                                      TextEncoding::Ascii));
        settings_native(source, 0x829aU, make_urational(1U, 125U));
        source.finalize();
        EXPECT_EQ(translate_xmp_camera_text_metadata(source, {}, &source).status,
                  CameraTextStatus::NativeConflict);
        CameraTextOptions options;
        options.conflict_policy    = CameraTextPolicy::ReplaceExisting;
        options.lens_model_to_exif = false;
        const auto result = translate_xmp_camera_text_metadata(source, options,
                                                               &source);
        ASSERT_EQ(result.status, CameraTextStatus::Ok);
        EXPECT_EQ(result.entries_removed, 5U);
        EXPECT_EQ(camera_text_value(source, 0xa434U), "old");
        EXPECT_NE(settings_find(source, 0x829aU), nullptr);
        MetaStore clean;
        camera_text_source(clean, "ignored", EntryFlags::Deleted);
        for (auto tag : kCameraTextTags)
            settings_native(clean, tag,
                            make_text(clean.arena(), "old",
                                      TextEncoding::Ascii));
        clean.finalize();
        options.source_mode = MetadataTechnicalTranslationSourceMode::All;
        EXPECT_EQ(translate_xmp_camera_text_metadata(clean, options, &clean)
                      .source_properties,
                  0U);
        for (auto tag : kCameraTextTags)
            EXPECT_EQ(camera_text_value(clean, tag), "old");
    }
    TEST(MetadataCameraText,
         BatchBudgetsAndApiPreconditionsFailWithoutPartialWrites)
    {
        MetaStore source;
        camera_text_source(source, "123456");
        CameraTextOptions options;
        options.max_added_entries = 5U;
        camera_text_failure(source, CameraTextStatus::EntryLimitExceeded,
                            options);
        options                = {};
        options.max_operations = 5U;
        camera_text_failure(source, CameraTextStatus::OperationLimitExceeded,
                            options);
        options                             = {};
        options.max_text_bytes_per_property = 5U;
        camera_text_failure(source, CameraTextStatus::ValueTooLong, options);
        options                      = {};
        options.max_total_text_bytes = 35U;
        camera_text_failure(source, CameraTextStatus::SourceLimitExceeded,
                            options);
        options                   = {};
        options.max_added_entries = 7U;
        camera_text_failure(source, CameraTextStatus::InvalidOptions, options);
        options = {};
        for (auto flag : kCameraTextFlags)
            options.*flag = false;
        camera_text_failure(source, CameraTextStatus::InvalidOptions, options);
        options = {};
        options.source_mode
            = static_cast<MetadataTechnicalTranslationSourceMode>(255U);
        camera_text_failure(source, CameraTextStatus::InvalidOptions, options);
        MetaStore unfinalized;
        EXPECT_EQ(
            translate_xmp_camera_text_metadata(unfinalized, {}, &source).status,
            CameraTextStatus::SourceNotFinalized);
        EXPECT_EQ(translate_xmp_camera_text_metadata(source, {}, nullptr).status,
                  CameraTextStatus::NullOutput);
        MetaStore maximum;
        camera_text_source(maximum, std::string(4096, 'A'));
        maximum.finalize();
        EXPECT_EQ(translate_xmp_camera_text_metadata(maximum, {}, &maximum)
                      .entries_added,
                  6U);
    }
    TEST(MetadataCameraText,
         PortableRoundTripRetainsPrintableAsciiAndManagedPolicy)
    {
        std::string printable;
        for (unsigned c = 0x20; c <= 0x7e; ++c)
            printable.push_back(static_cast<char>(c));
        for (const bool existing : { false, true }) {
            MetaStore source;
            camera_text_source(source, printable);
            source.finalize();
            ASSERT_EQ(
                translate_xmp_camera_text_metadata(source, {}, &source).status,
                CameraTextStatus::Ok);
            XmpPortableOptions options;
            options.include_existing_xmp = existing;
            options.conflict_policy      = XmpConflictPolicy::ExistingWins;
            std::array<std::byte, 16384> bytes {};
            const auto dumped = dump_xmp_portable(source, bytes, options);
            ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
            const std::string_view xml(reinterpret_cast<const char*>(
                                           bytes.data()),
                                       dumped.written);
            for (size_t i = 0U; i < kCameraTextNames.size(); ++i) {
                const std::string element
                    = std::string(i == 0U ? "<exif:" : "<exifEX:")
                      + std::string(kCameraTextNames[i]) + ">";
                EXPECT_NE(xml.find(element), std::string_view::npos);
            }
            MetaStore restored;
            ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(), dumped.written),
                                        restored)
                          .status,
                      XmpDecodeStatus::Ok);
            restored.finalize();
            CameraTextOptions all;
            all.source_mode = MetadataTechnicalTranslationSourceMode::All;
            ASSERT_EQ(translate_xmp_camera_text_metadata(restored, all,
                                                         &restored)
                          .status,
                      CameraTextStatus::Ok);
            for (auto tag : kCameraTextTags)
                EXPECT_EQ(camera_text_value(restored, tag), printable);
        }
        for (const bool canonical : { false, true }) {
            MetaStore source;
            camera_text_source(source, "source");
            for (auto tag : kCameraTextTags)
                settings_native(source, tag,
                                make_text(source.arena(), "native",
                                          TextEncoding::Ascii));
            source.finalize();
            XmpPortableOptions options;
            options.include_existing_xmp = true;
            options.conflict_policy      = XmpConflictPolicy::ExistingWins;
            if (canonical)
                options.existing_standard_namespace_policy
                    = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
            std::array<std::byte, 8192> bytes {};
            const auto dumped = dump_xmp_portable(source, bytes, options);
            ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
            MetaStore restored;
            ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(), dumped.written),
                                        restored)
                          .status,
                      XmpDecodeStatus::Ok);
            restored.finalize();
            CameraTextOptions all;
            all.source_mode = MetadataTechnicalTranslationSourceMode::All;
            ASSERT_EQ(translate_xmp_camera_text_metadata(restored, all,
                                                         &restored)
                          .status,
                      CameraTextStatus::Ok);
            for (auto tag : kCameraTextTags)
                EXPECT_EQ(camera_text_value(restored, tag),
                          canonical ? "native" : "source");
        }
    }
}  // namespace
}  // namespace openmeta
