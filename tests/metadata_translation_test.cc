// SPDX-License-Identifier: Apache-2.0

#include "capture_sync_fixture.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/exif_tiff_serialize.h"
#include "openmeta/exif_value_names.h"
#include "openmeta/meta_edit.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_translation.h"
#include "openmeta/validate.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
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

    static EntryId add_exif_ifd_tombstone(MetaStore* store, BlockId block,
                                          std::string_view ifd, uint16_t tag,
                                          EntryFlags flags, uint32_t order)
    {
        Entry entry;
        entry.key          = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value        = make_text(store->arena(), {}, TextEncoding::Ascii);
        entry.origin.block = block;
        entry.origin.order_in_block = order;
        entry.flags                 = flags;
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

    static bool has_exif_dirty_tombstone(const MetaStore& store,
                                         std::string_view expected_ifd,
                                         uint16_t tag) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (!any(entry.flags, EntryFlags::Dirty)
                || !any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag) {
                continue;
            }
            const std::span<const std::byte> ifd_bytes = store.arena().span(
                entry.key.data.exif_tag.ifd);
            if (std::string_view(reinterpret_cast<const char*>(ifd_bytes.data()),
                                 ifd_bytes.size())
                == expected_ifd) {
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
        EXPECT_EQ(result.entries_added, 10U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);

        EXPECT_TRUE(
            active_exif_text(translated, 0x9004U, "2024:08:30 01:02:03"));
        EXPECT_TRUE(active_exif_text(translated, 0x9012U, "-02:30"));
        EXPECT_TRUE(
            active_exif_origin_wire_name(translated, 0x9004U, "xmp-date"));
        EXPECT_EQ(active_exif_count(translated, 0x9292U), 0U);
        EXPECT_TRUE(has_exif_dirty_tombstone(translated, "exififd", 0x9292U));
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

        MetaStore later_failure;
        const BlockId later_failure_block = later_failure.add_block(
            BlockInfo {});
        ASSERT_NE(later_failure_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&later_failure, later_failure_block,
                               "http://ns.adobe.com/xap/1.0/", "CreateDate",
                               "2024-08-30T01:02:03Z", EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&later_failure, later_failure_block,
                               "http://ns.adobe.com/exif/1.0/",
                               "DateTimeOriginal", "2023-02-29T01:02:03Z",
                               EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        later_failure.finalize();
        output = sentinel;
        result = translate_xmp_creation_dates(later_failure,
                                              MetadataDateTranslationOptions {},
                                              &output);
        EXPECT_EQ(result.status,
                  MetadataDateTranslationStatus::InvalidDateTime);
        EXPECT_EQ(result.failed_mapping,
                  MetadataDateTranslationMapping::XmpDateTimeOriginal);
        EXPECT_EQ(output.entries().size(), sentinel.entries().size());
        EXPECT_TRUE(active_iptc_text(output, 5U, "sentinel"));
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
        EXPECT_EQ(result.groups_translated, 0U);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);
        EXPECT_EQ(active_exif_count(output, 0x9004U), 1U);
        EXPECT_EQ(active_exif_count(output, 0x9012U), 2U);
        EXPECT_EQ(active_exif_count(output, 0x9292U), 1U);
        EXPECT_TRUE(active_exif_text(output, 0x9004U, "2001:02:03 04:05:06"));
        EXPECT_TRUE(active_exif_text(output, 0x9292U, "999"));
        EXPECT_FALSE(has_exif_dirty_tombstone(output, "exififd", 0x9292U));

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
        EXPECT_EQ(result.entries_added, 1U);
        EXPECT_TRUE(has_exif_dirty_tombstone(translated, "exififd", 0x9292U));
        EXPECT_EQ(active_exif_count(translated, 0x9004U), 0U);
        EXPECT_EQ(active_exif_count(translated, 0x9012U), 0U);
        EXPECT_EQ(active_iptc_count(translated, 62U), 0U);
        EXPECT_EQ(active_iptc_count(translated, 63U), 0U);
    }

    TEST(MetadataTranslation,
         MissingDateCompanionsProduceBoundedIdempotentIntents)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/exif/1.0/",
                               "DateTimeOriginal", "2026-09-30T12:34:56",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/xap/1.0/",
                               "ModifyDate", "2026-09-30T12:34:56",
                               EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        source.finalize();

        MetadataDateTranslationOptions date_options;
        date_options.create_date_to_exif_digitized        = false;
        date_options.create_date_to_iptc_digital_creation = false;
        date_options.date_created_to_iptc_created         = false;
        MetaStore date_output;
        const BlockId output_block = date_output.add_block(BlockInfo {});
        ASSERT_NE(output_block, kInvalidBlockId);
        ASSERT_NE(add_exif_ifd_text(&date_output, output_block, "ifd0", 0x0131U,
                                    "sentinel", 0U),
                  kInvalidEntryId);
        date_output.finalize();

        date_options.max_added_entries = 2U;
        MetadataDateTranslationResult date_result
            = translate_xmp_creation_dates(source, date_options, &date_output);
        EXPECT_EQ(date_result.status,
                  MetadataDateTranslationStatus::EntryLimitExceeded);
        EXPECT_EQ(date_output.entries().size(), 1U);
        EXPECT_TRUE(
            active_exif_ifd_text(date_output, "ifd0", 0x0131U, "sentinel"));

        date_options.max_added_entries = kMetadataDateTranslationMaxAddedEntries;
        date_options.max_operations = 2U;
        date_result = translate_xmp_creation_dates(source, date_options,
                                                   &date_output);
        EXPECT_EQ(date_result.status,
                  MetadataDateTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(date_output.entries().size(), 1U);
        EXPECT_TRUE(
            active_exif_ifd_text(date_output, "ifd0", 0x0131U, "sentinel"));

        date_options.max_operations = kMetadataDateTranslationMaxOperations;
        date_result = translate_xmp_creation_dates(source, date_options,
                                                   &date_output);
        ASSERT_EQ(date_result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(date_result.entries_added, 3U);
        EXPECT_EQ(date_result.groups_translated, 1U);
        EXPECT_TRUE(active_exif_ifd_text(date_output, "exififd", 0x9003U,
                                         "2026:09:30 12:34:56"));
        EXPECT_TRUE(has_exif_dirty_tombstone(date_output, "exififd", 0x9011U));
        EXPECT_TRUE(has_exif_dirty_tombstone(date_output, "exififd", 0x9291U));
        EXPECT_EQ(active_exif_count(date_output, 0x9011U), 0U);
        EXPECT_EQ(active_exif_count(date_output, 0x9291U), 0U);

        MetaStore date_repeated;
        date_result = translate_xmp_creation_dates(date_output, date_options,
                                                   &date_repeated);
        ASSERT_EQ(date_result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(date_result.entries_added, 0U);
        EXPECT_EQ(date_result.groups_unchanged, 1U);
        EXPECT_EQ(date_repeated.entries().size(), date_output.entries().size());

        MetadataTechnicalTranslationOptions technical_options;
        MetadataTechnicalTranslationResult technical_result;

        MetaStore clean_deleted_source;
        const BlockId clean_deleted_block = clean_deleted_source.add_block(
            BlockInfo {});
        ASSERT_NE(clean_deleted_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&clean_deleted_source, clean_deleted_block,
                               "http://ns.adobe.com/exif/1.0/",
                               "DateTimeOriginal", "2026-09-30T12:34:56",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&clean_deleted_source, clean_deleted_block,
                               "http://ns.adobe.com/xap/1.0/", "ModifyDate",
                               "2026-09-30T12:34:56", EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&clean_deleted_source, clean_deleted_block,
                                    "exififd", 0x9003U, "2026:09:30 12:34:56",
                                    2U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_tombstone(&clean_deleted_source,
                                         clean_deleted_block, "exififd",
                                         0x9011U, EntryFlags::Deleted, 3U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&clean_deleted_source, clean_deleted_block,
                                    "ifd0", 0x0132U, "2026:09:30 12:34:56", 4U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_tombstone(&clean_deleted_source,
                                         clean_deleted_block, "exififd",
                                         0x9010U, EntryFlags::Deleted, 5U),
                  kInvalidEntryId);
        clean_deleted_source.finalize();

        MetaStore clean_deleted_date_output;
        date_result = translate_xmp_creation_dates(clean_deleted_source,
                                                   date_options,
                                                   &clean_deleted_date_output);
        ASSERT_EQ(date_result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(date_result.entries_added, 1U);
        EXPECT_EQ(date_result.entries_updated, 2U);
        EXPECT_EQ(date_result.entries_removed, 0U);
        EXPECT_TRUE(active_exif_ifd_text(clean_deleted_date_output, "exififd",
                                         0x9003U, "2026:09:30 12:34:56"));
        EXPECT_TRUE(has_exif_dirty_tombstone(clean_deleted_date_output,
                                             "exififd", 0x9011U));
        EXPECT_TRUE(has_exif_dirty_tombstone(clean_deleted_date_output,
                                             "exififd", 0x9291U));
        EXPECT_EQ(active_exif_count(clean_deleted_date_output, 0x9003U), 1U);

        MetaStore clean_deleted_technical_output;
        technical_result
            = translate_xmp_technical_metadata(clean_deleted_source,
                                               technical_options,
                                               &clean_deleted_technical_output);
        ASSERT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(technical_result.entries_added, 1U);
        EXPECT_EQ(technical_result.entries_updated, 2U);
        EXPECT_EQ(technical_result.entries_removed, 0U);
        EXPECT_TRUE(active_exif_ifd_text(clean_deleted_technical_output, "ifd0",
                                         0x0132U, "2026:09:30 12:34:56"));
        EXPECT_TRUE(has_exif_dirty_tombstone(clean_deleted_technical_output,
                                             "exififd", 0x9010U));
        EXPECT_TRUE(has_exif_dirty_tombstone(clean_deleted_technical_output,
                                             "exififd", 0x9290U));
        EXPECT_EQ(active_exif_count(clean_deleted_technical_output, 0x0132U),
                  1U);

        MetaStore technical_output;
        const BlockId technical_output_block = technical_output.add_block(
            BlockInfo {});
        ASSERT_NE(technical_output_block, kInvalidBlockId);
        ASSERT_NE(add_exif_ifd_text(&technical_output, technical_output_block,
                                    "ifd0", 0x0131U, "sentinel", 0U),
                  kInvalidEntryId);
        technical_output.finalize();

        technical_options.max_added_entries = 2U;
        technical_result = translate_xmp_technical_metadata(source,
                                                            technical_options,
                                                            &technical_output);
        EXPECT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::EntryLimitExceeded);
        EXPECT_EQ(technical_output.entries().size(), 1U);
        EXPECT_TRUE(active_exif_ifd_text(technical_output, "ifd0", 0x0131U,
                                         "sentinel"));

        technical_options.max_added_entries
            = kMetadataTechnicalTranslationMaxAddedEntries;
        technical_options.max_operations = 2U;
        technical_result = translate_xmp_technical_metadata(source,
                                                            technical_options,
                                                            &technical_output);
        EXPECT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(technical_output.entries().size(), 1U);
        EXPECT_TRUE(active_exif_ifd_text(technical_output, "ifd0", 0x0131U,
                                         "sentinel"));

        technical_options.max_operations
            = kMetadataTechnicalTranslationMaxOperations;
        technical_result = translate_xmp_technical_metadata(source,
                                                            technical_options,
                                                            &technical_output);
        ASSERT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(technical_result.entries_added, 3U);
        EXPECT_EQ(technical_result.groups_translated, 1U);
        EXPECT_TRUE(active_exif_ifd_text(technical_output, "ifd0", 0x0132U,
                                         "2026:09:30 12:34:56"));
        EXPECT_TRUE(
            has_exif_dirty_tombstone(technical_output, "exififd", 0x9010U));
        EXPECT_TRUE(
            has_exif_dirty_tombstone(technical_output, "exififd", 0x9290U));
        EXPECT_EQ(active_exif_count(technical_output, 0x9010U), 0U);
        EXPECT_EQ(active_exif_count(technical_output, 0x9290U), 0U);

        MetaStore technical_repeated;
        technical_result = translate_xmp_technical_metadata(
            technical_output, technical_options, &technical_repeated);
        ASSERT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(technical_result.entries_added, 0U);
        EXPECT_EQ(technical_result.groups_unchanged, 1U);
        EXPECT_EQ(technical_repeated.entries().size(),
                  technical_output.entries().size());
    }

    TEST(MetadataTranslation,
         ExactFullPrecisionTimestampGroupsCarryDirtyAuthority)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/exif/1.0/",
                               "DateTimeOriginal",
                               "2026-09-30T12:34:56.125+09:00",
                               EntryFlags::None, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/xap/1.0/",
                               "ModifyDate", "2026-09-30T12:34:56.125+09:00",
                               EntryFlags::None, 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&source, block, "exififd", 0x9003U,
                                    "2026:09:30 12:34:56", 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&source, block, "exififd", 0x9011U,
                                    "+09:00", 3U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&source, block, "exififd", 0x9291U, "125",
                                    4U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&source, block, "ifd0", 0x0132U,
                                    "2026:09:30 12:34:56", 5U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&source, block, "exififd", 0x9010U,
                                    "+09:00", 6U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&source, block, "exififd", 0x9290U, "125",
                                    7U),
                  kInvalidEntryId);
        source.finalize();

        MetadataDateTranslationOptions date_options;
        date_options.source_mode = MetadataDateTranslationSourceMode::All;
        date_options.create_date_to_exif_digitized        = false;
        date_options.create_date_to_iptc_digital_creation = false;
        date_options.date_created_to_iptc_created         = false;
        date_options.max_operations                       = 2U;
        date_options.conflict_policy
            = MetadataDateTranslationConflictPolicy::PreserveExisting;
        MetaStore date_preserved;
        MetadataDateTranslationResult date_result
            = translate_xmp_creation_dates(source, date_options,
                                           &date_preserved);
        ASSERT_EQ(date_result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(date_result.groups_preserved, 1U);
        EXPECT_EQ(date_result.entries_added, 0U);
        EXPECT_EQ(date_result.entries_updated, 0U);
        EXPECT_EQ(date_result.entries_removed, 0U);
        const Entry* preserved_date_time
            = active_exif_entry(date_preserved, "exififd", 0x9003U);
        ASSERT_NE(preserved_date_time, nullptr);
        EXPECT_FALSE(any(preserved_date_time->flags, EntryFlags::Dirty));

        date_options.conflict_policy
            = MetadataDateTranslationConflictPolicy::FailOnConflict;
        MetaStore date_output;
        const BlockId date_output_block = date_output.add_block(BlockInfo {});
        ASSERT_NE(date_output_block, kInvalidBlockId);
        ASSERT_NE(add_exif_ifd_text(&date_output, date_output_block, "ifd0",
                                    0x0131U, "sentinel", 0U),
                  kInvalidEntryId);
        date_output.finalize();

        date_result = translate_xmp_creation_dates(source, date_options,
                                                   &date_output);
        EXPECT_EQ(date_result.status,
                  MetadataDateTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(date_output.entries().size(), 1U);
        EXPECT_TRUE(
            active_exif_ifd_text(date_output, "ifd0", 0x0131U, "sentinel"));

        date_options.max_operations = kMetadataDateTranslationMaxOperations;
        date_result = translate_xmp_creation_dates(source, date_options,
                                                   &date_output);
        ASSERT_EQ(date_result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(date_result.groups_translated, 1U);
        EXPECT_EQ(date_result.groups_unchanged, 0U);
        EXPECT_EQ(date_result.entries_added, 0U);
        EXPECT_EQ(date_result.entries_updated, 3U);
        EXPECT_EQ(date_result.entries_removed, 0U);
        const Entry* date_time     = active_exif_entry(date_output, "exififd",
                                                       0x9003U);
        const Entry* date_offset   = active_exif_entry(date_output, "exififd",
                                                       0x9011U);
        const Entry* date_fraction = active_exif_entry(date_output, "exififd",
                                                       0x9291U);
        ASSERT_NE(date_time, nullptr);
        ASSERT_NE(date_offset, nullptr);
        ASSERT_NE(date_fraction, nullptr);
        EXPECT_TRUE(any(date_time->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(date_offset->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(date_fraction->flags, EntryFlags::Dirty));
        EXPECT_TRUE(
            entry_matches_text(date_output, *date_time, "2026:09:30 12:34:56"));
        EXPECT_TRUE(entry_matches_text(date_output, *date_offset, "+09:00"));
        EXPECT_TRUE(entry_matches_text(date_output, *date_fraction, "125"));

        MetaStore date_repeated;
        date_result = translate_xmp_creation_dates(date_output, date_options,
                                                   &date_repeated);
        ASSERT_EQ(date_result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(date_result.groups_translated, 0U);
        EXPECT_EQ(date_result.groups_unchanged, 1U);
        EXPECT_EQ(date_result.entries_updated, 0U);

        MetaStore create_source;
        const BlockId create_block = create_source.add_block(BlockInfo {});
        ASSERT_NE(create_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&create_source, create_block,
                               "http://ns.adobe.com/xap/1.0/", "CreateDate",
                               "2026-09-30T12:34:56.125+09:00",
                               EntryFlags::None, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&create_source, create_block, "exififd",
                                    0x9004U, "2026:09:30 12:34:56", 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&create_source, create_block, "exififd",
                                    0x9012U, "+09:00", 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&create_source, create_block, "exififd",
                                    0x9292U, "125", 3U),
                  kInvalidEntryId);
        create_source.finalize();

        MetadataDateTranslationOptions create_options;
        create_options.source_mode = MetadataDateTranslationSourceMode::All;
        create_options.create_date_to_iptc_digital_creation = false;
        create_options.date_created_to_iptc_created         = false;
        create_options.date_time_original_to_exif_original  = false;
        create_options.max_operations                       = 2U;
        MetaStore create_output;
        const BlockId create_output_block = create_output.add_block(
            BlockInfo {});
        ASSERT_NE(create_output_block, kInvalidBlockId);
        ASSERT_NE(add_exif_ifd_text(&create_output, create_output_block, "ifd0",
                                    0x0131U, "sentinel", 0U),
                  kInvalidEntryId);
        create_output.finalize();
        MetadataDateTranslationResult create_result
            = translate_xmp_creation_dates(create_source, create_options,
                                           &create_output);
        EXPECT_EQ(create_result.status,
                  MetadataDateTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(create_output.entries().size(), 1U);
        EXPECT_TRUE(
            active_exif_ifd_text(create_output, "ifd0", 0x0131U, "sentinel"));

        create_options.max_operations = kMetadataDateTranslationMaxOperations;
        create_result = translate_xmp_creation_dates(create_source,
                                                     create_options,
                                                     &create_output);
        ASSERT_EQ(create_result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(create_result.groups_translated, 1U);
        EXPECT_EQ(create_result.groups_unchanged, 0U);
        EXPECT_EQ(create_result.entries_added, 0U);
        EXPECT_EQ(create_result.entries_updated, 3U);
        EXPECT_EQ(create_result.entries_removed, 0U);
        const Entry* digitized_time     = active_exif_entry(create_output,
                                                            "exififd", 0x9004U);
        const Entry* digitized_offset   = active_exif_entry(create_output,
                                                            "exififd", 0x9012U);
        const Entry* digitized_fraction = active_exif_entry(create_output,
                                                            "exififd", 0x9292U);
        ASSERT_NE(digitized_time, nullptr);
        ASSERT_NE(digitized_offset, nullptr);
        ASSERT_NE(digitized_fraction, nullptr);
        EXPECT_TRUE(any(digitized_time->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(digitized_offset->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(digitized_fraction->flags, EntryFlags::Dirty));
        EXPECT_TRUE(entry_matches_text(create_output, *digitized_time,
                                       "2026:09:30 12:34:56"));
        EXPECT_TRUE(
            entry_matches_text(create_output, *digitized_offset, "+09:00"));
        EXPECT_TRUE(
            entry_matches_text(create_output, *digitized_fraction, "125"));

        MetadataTechnicalTranslationOptions technical_options;
        technical_options.source_mode
            = MetadataTechnicalTranslationSourceMode::All;
        technical_options.make_to_exif_make             = false;
        technical_options.model_to_exif_model           = false;
        technical_options.creator_tool_to_exif_software = false;
        technical_options.max_operations                = 2U;
        technical_options.conflict_policy
            = MetadataTechnicalTranslationConflictPolicy::PreserveExisting;
        MetaStore technical_preserved;
        MetadataTechnicalTranslationResult technical_result
            = translate_xmp_technical_metadata(source, technical_options,
                                               &technical_preserved);
        ASSERT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(technical_result.groups_preserved, 1U);
        EXPECT_EQ(technical_result.entries_added, 0U);
        EXPECT_EQ(technical_result.entries_updated, 0U);
        EXPECT_EQ(technical_result.entries_removed, 0U);
        const Entry* preserved_modify_time
            = active_exif_entry(technical_preserved, "ifd0", 0x0132U);
        ASSERT_NE(preserved_modify_time, nullptr);
        EXPECT_FALSE(any(preserved_modify_time->flags, EntryFlags::Dirty));

        technical_options.conflict_policy
            = MetadataTechnicalTranslationConflictPolicy::FailOnConflict;
        MetaStore technical_output;
        const BlockId technical_output_block = technical_output.add_block(
            BlockInfo {});
        ASSERT_NE(technical_output_block, kInvalidBlockId);
        ASSERT_NE(add_exif_ifd_text(&technical_output, technical_output_block,
                                    "ifd0", 0x0131U, "sentinel", 0U),
                  kInvalidEntryId);
        technical_output.finalize();

        technical_result = translate_xmp_technical_metadata(source,
                                                            technical_options,
                                                            &technical_output);
        EXPECT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(technical_output.entries().size(), 1U);
        EXPECT_TRUE(active_exif_ifd_text(technical_output, "ifd0", 0x0131U,
                                         "sentinel"));

        technical_options.max_operations
            = kMetadataTechnicalTranslationMaxOperations;
        technical_result = translate_xmp_technical_metadata(source,
                                                            technical_options,
                                                            &technical_output);
        ASSERT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(technical_result.groups_translated, 1U);
        EXPECT_EQ(technical_result.groups_unchanged, 0U);
        EXPECT_EQ(technical_result.entries_added, 0U);
        EXPECT_EQ(technical_result.entries_updated, 3U);
        EXPECT_EQ(technical_result.entries_removed, 0U);
        const Entry* modify_time   = active_exif_entry(technical_output, "ifd0",
                                                       0x0132U);
        const Entry* modify_offset = active_exif_entry(technical_output,
                                                       "exififd", 0x9010U);
        const Entry* modify_fraction = active_exif_entry(technical_output,
                                                         "exififd", 0x9290U);
        ASSERT_NE(modify_time, nullptr);
        ASSERT_NE(modify_offset, nullptr);
        ASSERT_NE(modify_fraction, nullptr);
        EXPECT_TRUE(any(modify_time->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(modify_offset->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(modify_fraction->flags, EntryFlags::Dirty));
        EXPECT_TRUE(entry_matches_text(technical_output, *modify_time,
                                       "2026:09:30 12:34:56"));
        EXPECT_TRUE(
            entry_matches_text(technical_output, *modify_offset, "+09:00"));
        EXPECT_TRUE(
            entry_matches_text(technical_output, *modify_fraction, "125"));

        MetaStore technical_repeated;
        technical_result = translate_xmp_technical_metadata(
            technical_output, technical_options, &technical_repeated);
        ASSERT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(technical_result.groups_translated, 0U);
        EXPECT_EQ(technical_result.groups_unchanged, 1U);
        EXPECT_EQ(technical_result.entries_updated, 0U);
    }

    TEST(MetadataTranslation, ExactIptcAndTechnicalSingletonsKeepNoopCounters)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block,
                               "http://ns.adobe.com/photoshop/1.0/",
                               "DateCreated", "2026-09-30T12:34:56+09:00",
                               EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/tiff/1.0/",
                               "Make", "OpenMeta Camera", EntryFlags::Dirty,
                               1U),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_bytes(&source, block, 55U, "20260930", 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_bytes(&source, block, 60U, "123456+0900", 3U),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_ifd_text(&source, block, "ifd0", 0x010fU,
                                    "OpenMeta Camera", 4U),
                  kInvalidEntryId);
        source.finalize();

        MetadataDateTranslationOptions date_options;
        date_options.create_date_to_exif_digitized        = false;
        date_options.create_date_to_iptc_digital_creation = false;
        date_options.date_time_original_to_exif_original  = false;
        MetaStore date_output;
        const MetadataDateTranslationResult date_result
            = translate_xmp_creation_dates(source, date_options, &date_output);
        ASSERT_EQ(date_result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(date_result.source_properties, 1U);
        EXPECT_EQ(date_result.groups_translated, 0U);
        EXPECT_EQ(date_result.groups_unchanged, 1U);
        EXPECT_EQ(date_result.entries_added, 0U);
        EXPECT_EQ(date_result.entries_updated, 0U);
        EXPECT_EQ(date_result.entries_removed, 0U);
        EXPECT_TRUE(active_iptc_text(date_output, 55U, "20260930"));
        EXPECT_TRUE(active_iptc_text(date_output, 60U, "123456+0900"));

        MetadataTechnicalTranslationOptions technical_options;
        technical_options.modify_date_to_exif_datetime  = false;
        technical_options.model_to_exif_model           = false;
        technical_options.creator_tool_to_exif_software = false;
        MetaStore technical_output;
        const MetadataTechnicalTranslationResult technical_result
            = translate_xmp_technical_metadata(source, technical_options,
                                               &technical_output);
        ASSERT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(technical_result.source_properties, 1U);
        EXPECT_EQ(technical_result.groups_translated, 0U);
        EXPECT_EQ(technical_result.groups_unchanged, 1U);
        EXPECT_EQ(technical_result.entries_added, 0U);
        EXPECT_EQ(technical_result.entries_updated, 0U);
        EXPECT_EQ(technical_result.entries_removed, 0U);
        const Entry* make = active_exif_entry(technical_output, "ifd0",
                                              0x010fU);
        ASSERT_NE(make, nullptr);
        EXPECT_FALSE(any(make->flags, EntryFlags::Dirty));
        EXPECT_TRUE(
            entry_matches_text(technical_output, *make, "OpenMeta Camera"));
    }

    TEST(MetadataTranslation, DeletedTimestampOwnersEmitAllExifIntents)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/exif/1.0/",
                               "DateTimeOriginal", "removed",
                               EntryFlags::Dirty | EntryFlags::Deleted, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/xap/1.0/",
                               "CreateDate", "removed",
                               EntryFlags::Dirty | EntryFlags::Deleted, 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, "http://ns.adobe.com/xap/1.0/",
                               "ModifyDate", "removed",
                               EntryFlags::Dirty | EntryFlags::Deleted, 2U),
                  kInvalidEntryId);
        source.finalize();

        MetadataDateTranslationOptions date_options;
        date_options.create_date_to_exif_digitized        = true;
        date_options.create_date_to_iptc_digital_creation = true;
        date_options.date_created_to_iptc_created         = false;
        date_options.conflict_policy
            = MetadataDateTranslationConflictPolicy::PreserveExisting;
        MetaStore date_output;
        MetadataDateTranslationResult date_result
            = translate_xmp_creation_dates(source, date_options, &date_output);
        ASSERT_EQ(date_result.status, MetadataDateTranslationStatus::Ok);
        EXPECT_EQ(date_result.source_properties, 2U);
        EXPECT_EQ(date_result.entries_added, 6U);
        EXPECT_EQ(date_result.groups_translated, 3U);
        EXPECT_EQ(date_result.groups_unchanged, 0U);
        EXPECT_TRUE(has_exif_dirty_tombstone(date_output, "exififd", 0x9003U));
        EXPECT_TRUE(has_exif_dirty_tombstone(date_output, "exififd", 0x9011U));
        EXPECT_TRUE(has_exif_dirty_tombstone(date_output, "exififd", 0x9291U));
        EXPECT_TRUE(has_exif_dirty_tombstone(date_output, "exififd", 0x9004U));
        EXPECT_TRUE(has_exif_dirty_tombstone(date_output, "exififd", 0x9012U));
        EXPECT_TRUE(has_exif_dirty_tombstone(date_output, "exififd", 0x9292U));
        EXPECT_EQ(active_iptc_count(date_output, 62U), 0U);
        EXPECT_EQ(active_iptc_count(date_output, 63U), 0U);

        MetadataTechnicalTranslationOptions technical_options;
        technical_options.conflict_policy
            = MetadataTechnicalTranslationConflictPolicy::ReplaceExisting;
        MetaStore technical_output;
        MetadataTechnicalTranslationResult technical_result
            = translate_xmp_technical_metadata(source, technical_options,
                                               &technical_output);
        ASSERT_EQ(technical_result.status,
                  MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(technical_result.entries_added, 3U);
        EXPECT_EQ(technical_result.groups_translated, 1U);
        EXPECT_TRUE(
            has_exif_dirty_tombstone(technical_output, "ifd0", 0x0132U));
        EXPECT_TRUE(
            has_exif_dirty_tombstone(technical_output, "exififd", 0x9010U));
        EXPECT_TRUE(
            has_exif_dirty_tombstone(technical_output, "exififd", 0x9290U));
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

        MetaStore later_failure;
        const BlockId later_failure_block = later_failure.add_block(
            BlockInfo {});
        ASSERT_NE(later_failure_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&later_failure, later_failure_block,
                               "http://ns.adobe.com/xap/1.0/", "ModifyDate",
                               "2026-09-30T12:34:56Z", EntryFlags::Dirty, 0U),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&later_failure, later_failure_block,
                               "http://ns.adobe.com/tiff/1.0/", "Make",
                               "M\xc3\xa4ke", EntryFlags::Dirty, 1U),
                  kInvalidEntryId);
        later_failure.finalize();
        const size_t sentinel_entries = translated.entries().size();
        result = translate_xmp_technical_metadata(later_failure, options,
                                                  &translated);
        EXPECT_EQ(result.status,
                  MetadataTechnicalTranslationStatus::NonAsciiSource);
        EXPECT_EQ(result.failed_mapping,
                  MetadataTechnicalTranslationMapping::TiffMake);
        EXPECT_EQ(translated.entries().size(), sentinel_entries);
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

        MetaStore empty_singleton_deletion;
        const BlockId empty_singleton_block
            = empty_singleton_deletion.add_block(BlockInfo {});
        ASSERT_NE(empty_singleton_block, kInvalidBlockId);
        ASSERT_NE(add_xmp_text(&empty_singleton_deletion, empty_singleton_block,
                               "http://ns.adobe.com/tiff/1.0/", "Make",
                               "Old make",
                               EntryFlags::Dirty | EntryFlags::Deleted, 0U),
                  kInvalidEntryId);
        empty_singleton_deletion.finalize();

        MetadataTechnicalTranslationOptions singleton_options;
        singleton_options.modify_date_to_exif_datetime  = false;
        singleton_options.model_to_exif_model           = false;
        singleton_options.creator_tool_to_exif_software = false;
        singleton_options.conflict_policy
            = MetadataTechnicalTranslationConflictPolicy::PreserveExisting;
        MetaStore empty_singleton_output;
        result = translate_xmp_technical_metadata(empty_singleton_deletion,
                                                  singleton_options,
                                                  &empty_singleton_output);
        ASSERT_EQ(result.status, MetadataTechnicalTranslationStatus::Ok);
        EXPECT_EQ(result.groups_translated, 1U);
        EXPECT_EQ(result.groups_unchanged, 0U);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);
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
        EXPECT_EQ(result.status,
                  MetadataCaptureTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(active_exif_count(output, 0x829dU), 2U);
        options.max_operations = 2U;
        result = translate_xmp_capture_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_EQ(active_exif_count(output, 0x829dU), 1U);
        const Entry* exact_f_number = active_exif_entry(output, "exififd",
                                                        0x829dU);
        ASSERT_NE(exact_f_number, nullptr);
        EXPECT_EQ(exact_f_number->value.data.ur.numer, 28U);
        EXPECT_EQ(exact_f_number->value.data.ur.denom, 10U);

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

    static EntryId settings_native_entry(MetaStore& store, uint16_t tag,
                                         MetaValue value,
                                         EntryFlags flags   = EntryFlags::None,
                                         uint16_t wire_code = 0U,
                                         std::string_view wire_name = {})
    {
        Entry entry;
        entry.key   = make_exif_tag_key(store.arena(), "exififd", tag);
        entry.value = value;
        entry.flags             = flags;
        entry.origin.wire_type  = { WireFamily::Tiff, wire_code };
        entry.origin.wire_count = value.count;
        entry.origin.order_in_block = 17U;
        if (!wire_name.empty())
            entry.origin.wire_type_name = store.arena().append_string(
                wire_name);
        return store.add_entry(entry);
    }

    static void settings_native(MetaStore& store, uint16_t tag, MetaValue value)
    {
        settings_native_entry(store, tag, value);
    }

    static EntryId gps_lifecycle_native(MetaStore& store, uint16_t tag,
                                        MetaValue value, EntryFlags flags,
                                        uint16_t wire_code,
                                        std::string_view wire_name)
    {
        Entry entry;
        entry.key   = make_exif_tag_key(store.arena(), "gpsifd", tag);
        entry.value = value;
        entry.flags = flags;
        entry.origin.wire_type      = { WireFamily::Tiff, wire_code };
        entry.origin.wire_count     = value.count;
        entry.origin.order_in_block = 19U;
        entry.origin.wire_type_name = store.arena().append_string(wire_name);
        return store.add_entry(entry);
    }

    static const Entry* gps_lifecycle_find(const MetaStore& store,
                                           uint16_t tag) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag) {
                continue;
            }
            const std::span<const std::byte> ifd = store.arena().span(
                entry.key.data.exif_tag.ifd);
            const std::string_view ifd_name {
                reinterpret_cast<const char*>(ifd.data()), ifd.size()
            };
            if (!any(entry.flags, EntryFlags::Deleted)
                && ifd_name == "gpsifd") {
                return &entry;
            }
        }
        return nullptr;
    }

    static const Entry* gps_lifecycle_find_any(const MetaStore& store,
                                               uint16_t tag) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag) {
                continue;
            }
            const std::span<const std::byte> ifd = store.arena().span(
                entry.key.data.exif_tag.ifd);
            if (std::string_view(reinterpret_cast<const char*>(ifd.data()),
                                 ifd.size())
                == "gpsifd") {
                return &entry;
            }
        }
        return nullptr;
    }

    static std::string_view gps_lifecycle_text(const MetaStore& store,
                                               ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = store.arena().span(span);
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    enum class GpsLifecycleApi : uint8_t {
        Primary,
        Navigation,
        Destination,
        Quality,
        Text,
    };

    struct GpsLifecycleGroupCase final {
        GpsLifecycleApi api;
        std::array<std::string_view, 2> source_paths;
        uint8_t source_count;
        std::array<uint16_t, 2> native_tags;
        std::array<uint8_t, 2> native_source_indices;
        uint8_t native_count;
    };

    static constexpr std::array<GpsLifecycleGroupCase, 20U> kGpsLifecycleGroups {
        {
            { GpsLifecycleApi::Primary,
              { "GPSLatitude", {} },
              1U,
              { 1U, 2U },
              { 0U, 0U },
              2U },
            { GpsLifecycleApi::Primary,
              { "GPSLongitude", {} },
              1U,
              { 3U, 4U },
              { 0U, 0U },
              2U },
            { GpsLifecycleApi::Primary,
              { "GPSAltitude", "GPSAltitudeRef" },
              2U,
              { 5U, 6U },
              { 1U, 0U },
              2U },
            { GpsLifecycleApi::Navigation,
              { "GPSTimeStamp", {} },
              1U,
              { 7U, 29U },
              { 0U, 0U },
              2U },
            { GpsLifecycleApi::Navigation,
              { "GPSSpeedRef", "GPSSpeed" },
              2U,
              { 12U, 13U },
              { 0U, 1U },
              2U },
            { GpsLifecycleApi::Navigation,
              { "GPSTrackRef", "GPSTrack" },
              2U,
              { 14U, 15U },
              { 0U, 1U },
              2U },
            { GpsLifecycleApi::Navigation,
              { "GPSImgDirectionRef", "GPSImgDirection" },
              2U,
              { 16U, 17U },
              { 0U, 1U },
              2U },
            { GpsLifecycleApi::Destination,
              { "GPSDestLatitude", {} },
              1U,
              { 19U, 20U },
              { 0U, 0U },
              2U },
            { GpsLifecycleApi::Destination,
              { "GPSDestLongitude", {} },
              1U,
              { 21U, 22U },
              { 0U, 0U },
              2U },
            { GpsLifecycleApi::Destination,
              { "GPSDestBearingRef", "GPSDestBearing" },
              2U,
              { 23U, 24U },
              { 0U, 1U },
              2U },
            { GpsLifecycleApi::Destination,
              { "GPSDestDistanceRef", "GPSDestDistance" },
              2U,
              { 25U, 26U },
              { 0U, 1U },
              2U },
            { GpsLifecycleApi::Quality,
              { "GPSStatus", {} },
              1U,
              { 9U, 0U },
              { 0U, 0U },
              1U },
            { GpsLifecycleApi::Quality,
              { "GPSMeasureMode", {} },
              1U,
              { 10U, 0U },
              { 0U, 0U },
              1U },
            { GpsLifecycleApi::Quality,
              { "GPSDOP", {} },
              1U,
              { 11U, 0U },
              { 0U, 0U },
              1U },
            { GpsLifecycleApi::Quality,
              { "GPSDifferential", {} },
              1U,
              { 30U, 0U },
              { 0U, 0U },
              1U },
            { GpsLifecycleApi::Quality,
              { "GPSHPositioningError", {} },
              1U,
              { 31U, 0U },
              { 0U, 0U },
              1U },
            { GpsLifecycleApi::Text,
              { "GPSSatellites", {} },
              1U,
              { 8U, 0U },
              { 0U, 0U },
              1U },
            { GpsLifecycleApi::Text,
              { "GPSMapDatum", {} },
              1U,
              { 18U, 0U },
              { 0U, 0U },
              1U },
            { GpsLifecycleApi::Text,
              { "GPSProcessingMethod", {} },
              1U,
              { 27U, 0U },
              { 0U, 0U },
              1U },
            { GpsLifecycleApi::Text,
              { "GPSAreaInformation", {} },
              1U,
              { 28U, 0U },
              { 0U, 0U },
              1U },
        }
    };

    static MetadataGpsTranslationResult gps_lifecycle_translate(
        GpsLifecycleApi api, const MetaStore& source, MetaStore* output,
        MetadataGpsTranslationSourceMode source_mode
        = MetadataGpsTranslationSourceMode::DirtyOnly,
        MetadataGpsTranslationConflictPolicy policy
        = MetadataGpsTranslationConflictPolicy::ReplaceExisting,
        uint32_t max_added_entries = 0U, uint32_t max_operations = 0U)
    {
        switch (api) {
        case GpsLifecycleApi::Primary: {
            MetadataGpsTranslationOptions options;
            options.source_mode     = source_mode;
            options.conflict_policy = policy;
            if (max_added_entries != 0U)
                options.max_added_entries = max_added_entries;
            if (max_operations != 0U)
                options.max_operations = max_operations;
            return translate_xmp_gps_metadata(source, options, output);
        }
        case GpsLifecycleApi::Navigation: {
            MetadataGpsNavigationTranslationOptions options;
            options.source_mode     = source_mode;
            options.conflict_policy = policy;
            if (max_added_entries != 0U)
                options.max_added_entries = max_added_entries;
            if (max_operations != 0U)
                options.max_operations = max_operations;
            return translate_xmp_gps_navigation_metadata(source, options,
                                                         output);
        }
        case GpsLifecycleApi::Destination: {
            MetadataGpsDestinationTranslationOptions options;
            options.source_mode     = source_mode;
            options.conflict_policy = policy;
            if (max_added_entries != 0U)
                options.max_added_entries = max_added_entries;
            if (max_operations != 0U)
                options.max_operations = max_operations;
            return translate_xmp_gps_destination_metadata(source, options,
                                                          output);
        }
        case GpsLifecycleApi::Quality: {
            MetadataGpsQualityTranslationOptions options;
            options.source_mode     = source_mode;
            options.conflict_policy = policy;
            if (max_added_entries != 0U)
                options.max_added_entries = max_added_entries;
            if (max_operations != 0U)
                options.max_operations = max_operations;
            return translate_xmp_gps_quality_metadata(source, options, output);
        }
        case GpsLifecycleApi::Text: {
            MetadataGpsTextTranslationOptions options;
            options.source_mode     = source_mode;
            options.conflict_policy = policy;
            if (max_added_entries != 0U)
                options.max_added_entries = max_added_entries;
            if (max_operations != 0U)
                options.max_operations = max_operations;
            return translate_xmp_gps_text_metadata(source, options, output);
        }
        }
        MetadataGpsTranslationResult result;
        result.status = MetadataGpsTranslationStatus::InternalError;
        return result;
    }

    static MetaStore
    gps_lifecycle_deleted_source(const GpsLifecycleGroupCase& group,
                                 EntryFlags flags = EntryFlags::Dirty
                                                    | EntryFlags::Deleted,
                                 std::string_view schema_ns = kSettingsNs)
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        for (size_t i = 0U; i < group.source_count; ++i) {
            add_xmp_text(&store, block, schema_ns, group.source_paths[i], "",
                         flags, static_cast<uint32_t>(i),
                         i == 0U ? "gps-lifecycle-source-a"
                                 : "gps-lifecycle-source-b");
        }
        return store;
    }

    static MetaStore gps_lifecycle_exact_fixture(GpsLifecycleApi api)
    {
        MetaStore store;
        const std::array<uint8_t, 4> version { 2U, 3U, 0U, 0U };
        gps_lifecycle_native(store, 0U, make_u8_array(store.arena(), version),
                             EntryFlags::None, 1U, "gps-exact-version-wire");
        if (api == GpsLifecycleApi::Primary) {
            settings_xmp(store, "GPSLatitude",
                         make_text(store.arena(), "35,48.125N",
                                   TextEncoding::Utf8));
            gps_lifecycle_native(store, 1U,
                                 make_text(store.arena(),
                                           std::string_view("N\0", 2U),
                                           TextEncoding::Ascii),
                                 EntryFlags::None, 2U,
                                 "gps-exact-reference-wire");
            const std::array<URational, 3> coordinate { URational { 35U, 1U },
                                                        URational { 48U, 1U },
                                                        URational { 30U, 4U } };
            gps_lifecycle_native(
                store, 2U, make_urational_array(store.arena(), coordinate),
                EntryFlags::None, 5U, "gps-exact-coordinate-wire");
        } else if (api == GpsLifecycleApi::Navigation) {
            settings_xmp(store, "GPSSpeedRef",
                         make_text(store.arena(), "K", TextEncoding::Utf8));
            settings_xmp(store, "GPSSpeed",
                         make_text(store.arena(), "1.5", TextEncoding::Utf8));
            gps_lifecycle_native(store, 12U,
                                 make_text(store.arena(),
                                           std::string_view("K\0", 2U),
                                           TextEncoding::Ascii),
                                 EntryFlags::None, 2U,
                                 "gps-exact-speed-ref-wire");
            gps_lifecycle_native(store, 13U, make_urational(6U, 4U),
                                 EntryFlags::None, 5U, "gps-exact-speed-wire");
        } else if (api == GpsLifecycleApi::Destination) {
            settings_xmp(store, "GPSDestBearingRef",
                         make_text(store.arena(), "True North",
                                   TextEncoding::Utf8));
            settings_xmp(store, "GPSDestBearing",
                         make_text(store.arena(), "1.5", TextEncoding::Utf8));
            gps_lifecycle_native(store, 23U,
                                 make_text(store.arena(),
                                           std::string_view("T\0", 2U),
                                           TextEncoding::Ascii),
                                 EntryFlags::None, 2U,
                                 "gps-exact-bearing-ref-wire");
            gps_lifecycle_native(store, 24U, make_urational(3U, 2U),
                                 EntryFlags::None, 5U,
                                 "gps-exact-bearing-wire");
        } else if (api == GpsLifecycleApi::Quality) {
            settings_xmp(store, "GPSDOP",
                         make_text(store.arena(), "1.25", TextEncoding::Utf8));
            gps_lifecycle_native(store, 11U, make_urational(10U, 8U),
                                 EntryFlags::None, 5U, "gps-exact-dop-wire");
        } else {
            settings_xmp(store, "GPSProcessingMethod",
                         make_text(store.arena(), "GPS WLAN",
                                   TextEncoding::Utf8));
            const std::string_view encoded("ASCII\0\0\0GPS WLAN", 16U);
            gps_lifecycle_native(store, 27U,
                                 make_bytes(store.arena(),
                                            std::as_bytes(
                                                std::span(encoded.data(),
                                                          encoded.size()))),
                                 EntryFlags::None, 7U, "gps-exact-method-wire");
        }
        store.finalize();
        return store;
    }

    static void gps_lifecycle_add_selection_fixture(
        MetaStore& store, GpsLifecycleApi api, EntryFlags flags,
        std::string_view schema_ns = kSettingsNs)
    {
        const BlockId block = store.add_block(BlockInfo {});
        if (api == GpsLifecycleApi::Primary) {
            add_xmp_text(&store, block, schema_ns, "GPSLatitude", "35,48.125N",
                         flags, 0U, "gps-selection-source");
        } else if (api == GpsLifecycleApi::Navigation) {
            add_xmp_text(&store, block, schema_ns, "GPSSpeedRef", "K", flags,
                         0U, "gps-selection-source-a");
            add_xmp_text(&store, block, schema_ns, "GPSSpeed", "1.5", flags, 1U,
                         "gps-selection-source-b");
        } else if (api == GpsLifecycleApi::Destination) {
            add_xmp_text(&store, block, schema_ns, "GPSDestBearingRef", "T",
                         flags, 0U, "gps-selection-source-a");
            add_xmp_text(&store, block, schema_ns, "GPSDestBearing", "1.5",
                         flags, 1U, "gps-selection-source-b");
        } else if (api == GpsLifecycleApi::Quality) {
            add_xmp_text(&store, block, schema_ns, "GPSStatus", "A", flags, 0U,
                         "gps-selection-source");
        } else {
            add_xmp_text(&store, block, schema_ns, "GPSMapDatum", "WGS-84",
                         flags, 0U, "gps-selection-source");
        }
    }

    static void gps_lifecycle_check_group_flags(
        const MetaStore& store, const GpsLifecycleGroupCase& group, bool dirty)
    {
        for (size_t i = 0U; i < group.native_count; ++i) {
            const Entry* entry = gps_lifecycle_find(store,
                                                    group.native_tags[i]);
            ASSERT_NE(entry, nullptr) << group.native_tags[i];
            EXPECT_EQ(any(entry->flags, EntryFlags::Dirty), dirty)
                << group.native_tags[i];
        }
    }

    static bool gps_lifecycle_has_delete_intent(const MetaStore& store,
                                                uint16_t tag) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag
                || !any(entry.flags, EntryFlags::Dirty)
                || !any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            const std::span<const std::byte> ifd = store.arena().span(
                entry.key.data.exif_tag.ifd);
            if (std::string_view(reinterpret_cast<const char*>(ifd.data()),
                                 ifd.size())
                == "gpsifd") {
                return true;
            }
        }
        return false;
    }

    static std::vector<EntryId>
    settings_native_history_ids(const MetaStore& store, uint16_t tag)
    {
        std::vector<EntryId> ids;
        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& entry = store.entry(id);
            if (entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag)
                continue;
            const auto ifd = store.arena().span(entry.key.data.exif_tag.ifd);
            if (std::string_view(reinterpret_cast<const char*>(ifd.data()),
                                 ifd.size())
                == "exififd")
                ids.push_back(id);
        }
        return ids;
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

    static void expect_native_delete_intent(const MetaStore& store,
                                            uint16_t tag)
    {
        const auto ids = settings_native_history_ids(store, tag);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& entry = store.entry(ids.front());
        EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(entry.flags, EntryFlags::Deleted));
    }

    static void expect_native_authority(const MetaStore& store, uint16_t tag,
                                        uint16_t wire_code, uint64_t wire_count,
                                        std::string_view wire_name)
    {
        const Entry* entry = settings_find(store, tag);
        ASSERT_NE(entry, nullptr);
        EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
        EXPECT_FALSE(any(entry->flags, EntryFlags::Deleted));
        EXPECT_EQ(entry->origin.wire_type.family, WireFamily::Tiff);
        EXPECT_EQ(entry->origin.wire_type.code, wire_code);
        EXPECT_EQ(entry->origin.wire_count, wire_count);
        EXPECT_EQ(entry->origin.order_in_block, 17U);
        const auto bytes = store.arena().span(entry->origin.wire_type_name);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                   bytes.size()),
                  wire_name);
    }

    static void expect_lifecycle_repeat(uint32_t groups_unchanged,
                                        uint32_t entries_added,
                                        uint32_t entries_updated)
    {
        EXPECT_EQ(groups_unchanged, 1U);
        EXPECT_EQ(entries_added, 0U);
        EXPECT_EQ(entries_updated, 0U);
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

    TEST(MetadataCaptureLifecycle,
         SelectedScalarOwnersCreateDeleteIntentsAndPreserveExactValues)
    {
        struct Case final {
            std::string_view path;
            uint16_t tag;
            MetaValue value;
            bool MetadataCaptureTranslationOptions::* enabled;
            uint16_t wire_code;
        };
        const std::array<Case, 4U> cases {
            Case { "ExposureTime", 0x829aU, make_urational(1U, 125U),
                   &MetadataCaptureTranslationOptions::exposure_time_to_exif,
                   5U },
            Case { "FNumber", 0x829dU, make_urational(14U, 5U),
                   &MetadataCaptureTranslationOptions::f_number_to_exif, 5U },
            Case { "ISO", 0x8827U, make_u16(400U),
                   &MetadataCaptureTranslationOptions::iso_to_exif, 3U },
            Case { "FocalLength", 0x920aU, make_urational(66U, 1U),
                   &MetadataCaptureTranslationOptions::focal_length_to_exif,
                   5U },
        };
        for (const Case& item : cases) {
            SCOPED_TRACE(item.path);
            MetadataCaptureTranslationOptions options;
            options.exposure_time_to_exif = false;
            options.f_number_to_exif      = false;
            options.iso_to_exif           = false;
            options.focal_length_to_exif  = false;
            options.*item.enabled         = true;

            MetaStore deletion;
            settings_xmp(deletion, item.path, item.value,
                         EntryFlags::Dirty | EntryFlags::Deleted);
            deletion.finalize();
            const auto removed
                = translate_xmp_capture_metadata(deletion, options, &deletion);
            ASSERT_EQ(removed.status, MetadataCaptureTranslationStatus::Ok)
                << item.path;
            EXPECT_EQ(removed.entries_added, 1U) << item.path;
            EXPECT_EQ(removed.entries_updated, 0U) << item.path;
            EXPECT_EQ(removed.entries_removed, 0U) << item.path;
            ASSERT_NO_FATAL_FAILURE(
                expect_native_delete_intent(deletion, item.tag));
            const auto repeated
                = translate_xmp_capture_metadata(deletion, options, &deletion);
            expect_lifecycle_repeat(repeated.groups_unchanged,
                                    repeated.entries_added,
                                    repeated.entries_updated);

            MetaStore exact;
            settings_xmp(exact, item.path, item.value);
            settings_native_entry(exact, item.tag, item.value, EntryFlags::None,
                                  item.wire_code, "native-capture-wire");
            exact.finalize();
            const auto authority
                = translate_xmp_capture_metadata(exact, options, &exact);
            ASSERT_EQ(authority.status, MetadataCaptureTranslationStatus::Ok)
                << item.path;
            EXPECT_EQ(authority.entries_added, 0U) << item.path;
            EXPECT_EQ(authority.entries_updated, 1U) << item.path;
            const Entry* native = settings_find(exact, item.tag);
            ASSERT_NE(native, nullptr) << item.path;
            EXPECT_EQ(native->value.elem_type, item.value.elem_type)
                << item.path;
            if (item.value.elem_type == MetaElementType::URational) {
                EXPECT_EQ(native->value.data.ur.numer, item.value.data.ur.numer)
                    << item.path;
                EXPECT_EQ(native->value.data.ur.denom, item.value.data.ur.denom)
                    << item.path;
            } else {
                EXPECT_EQ(native->value.data.u64, item.value.data.u64)
                    << item.path;
            }
            ASSERT_NO_FATAL_FAILURE(
                expect_native_authority(exact, item.tag, item.wire_code,
                                        item.value.count,
                                        "native-capture-wire"));
            const auto same = translate_xmp_capture_metadata(exact, options,
                                                             &exact);
            expect_lifecycle_repeat(same.groups_unchanged, same.entries_added,
                                    same.entries_updated);

            MetaStore omitted;
            settings_native(omitted, item.tag, item.value);
            omitted.finalize();
            const auto retained
                = translate_xmp_capture_metadata(omitted, options, &omitted);
            EXPECT_EQ(retained.entries_removed, 0U) << item.path;
            EXPECT_NE(settings_find(omitted, item.tag), nullptr) << item.path;

            MetaStore conflict;
            settings_xmp(conflict, item.path, item.value,
                         EntryFlags::Dirty | EntryFlags::Deleted);
            settings_native(conflict, item.tag, item.value);
            conflict.finalize();
            EXPECT_EQ(translate_xmp_capture_metadata(conflict, options,
                                                     &conflict)
                          .status,
                      MetadataCaptureTranslationStatus::NativeConflict)
                << item.path;
            options.conflict_policy
                = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
            const auto preserved
                = translate_xmp_capture_metadata(conflict, options, &conflict);
            EXPECT_EQ(preserved.groups_preserved, 1U) << item.path;
            EXPECT_EQ(settings_active_count(conflict, item.tag), 1U)
                << item.path;
            options.conflict_policy
                = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
            options.max_operations = 1U;
            const auto replaced
                = translate_xmp_capture_metadata(conflict, options, &conflict);
            ASSERT_EQ(replaced.status, MetadataCaptureTranslationStatus::Ok)
                << item.path;
            EXPECT_EQ(replaced.entries_removed, 1U) << item.path;
            EXPECT_EQ(replaced.entries_added, 0U) << item.path;
            EXPECT_EQ(settings_active_count(conflict, item.tag), 0U)
                << item.path;
        }

        MetaStore duplicate_clean;
        settings_xmp(duplicate_clean, "ExposureTime", make_urational(1U, 125U));
        settings_native_entry(duplicate_clean, 0x829aU,
                              make_urational(1U, 125U), EntryFlags::None, 5U,
                              "clean-first");
        settings_native_entry(duplicate_clean, 0x829aU,
                              make_urational(1U, 125U), EntryFlags::None, 5U,
                              "clean-duplicate");
        duplicate_clean.finalize();
        MetadataCaptureTranslationOptions bounded;
        bounded.f_number_to_exif              = false;
        bounded.iso_to_exif                   = false;
        bounded.focal_length_to_exif          = false;
        bounded.exposure_compensation_to_exif = false;
        bounded.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        bounded.max_operations = 1U;
        MetaStore output;
        settings_native(output, 0x9209U, make_u16(95U));
        output.finalize();
        const size_t before = duplicate_clean.entries().size();
        EXPECT_EQ(translate_xmp_capture_metadata(duplicate_clean, bounded,
                                                 &output)
                      .status,
                  MetadataCaptureTranslationStatus::OperationLimitExceeded);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(settings_find(output, 0x9209U)->value.data.u64, 95U);
        EXPECT_EQ(translate_xmp_capture_metadata(duplicate_clean, bounded,
                                                 &duplicate_clean)
                      .status,
                  MetadataCaptureTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(duplicate_clean.entries().size(), before);
        for (const EntryId id :
             settings_native_history_ids(duplicate_clean, 0x829aU))
            EXPECT_FALSE(
                any(duplicate_clean.entry(id).flags, EntryFlags::Dirty));
        bounded.max_operations = 2U;
        const auto repaired    = translate_xmp_capture_metadata(duplicate_clean,
                                                                bounded,
                                                                &duplicate_clean);
        ASSERT_EQ(repaired.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(repaired.entries_updated, 1U);
        EXPECT_EQ(repaired.entries_removed, 1U);
    }

    static MetadataCaptureTranslationOptions exposure_lifecycle_options()
    {
        MetadataCaptureTranslationOptions options;
        options.f_number_to_exif              = false;
        options.iso_to_exif                   = false;
        options.focal_length_to_exif          = false;
        options.exposure_compensation_to_exif = false;
        return options;
    }

    TEST(MetadataCaptureLifecycle,
         DeletionReusesOneCleanMarkerAndLeavesOtherHistoryUntouched)
    {
        const MetaValue value = make_urational(1U, 125U);

        MetaStore already_intended;
        settings_xmp(already_intended, "ExposureTime", value,
                     EntryFlags::Dirty | EntryFlags::Deleted);
        settings_native_entry(already_intended, 0x829aU, value,
                              EntryFlags::Deleted, 5U, "clean-one");
        settings_native_entry(already_intended, 0x829aU, value,
                              EntryFlags::Deleted, 5U, "clean-two");
        settings_native_entry(already_intended, 0x829aU, value,
                              EntryFlags::Dirty | EntryFlags::Deleted, 5U,
                              "dirty-intent");
        already_intended.finalize();
        auto options           = exposure_lifecycle_options();
        options.max_operations = 1U;
        const auto reused = translate_xmp_capture_metadata(already_intended,
                                                           options,
                                                           &already_intended);
        ASSERT_EQ(reused.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(reused.groups_unchanged, 1U);
        EXPECT_EQ(reused.entries_updated, 0U);
        EXPECT_EQ(reused.entries_added, 0U);
        auto ids = settings_native_history_ids(already_intended, 0x829aU);
        ASSERT_EQ(ids.size(), 3U);
        EXPECT_FALSE(
            any(already_intended.entry(ids[0]).flags, EntryFlags::Dirty));
        EXPECT_FALSE(
            any(already_intended.entry(ids[1]).flags, EntryFlags::Dirty));
        EXPECT_TRUE(
            any(already_intended.entry(ids[2]).flags, EntryFlags::Dirty));

        MetaStore clean_history;
        settings_xmp(clean_history, "ExposureTime", value,
                     EntryFlags::Dirty | EntryFlags::Deleted);
        settings_native_entry(clean_history, 0x829aU, value,
                              EntryFlags::Deleted, 5U, "clean-one");
        settings_native_entry(clean_history, 0x829aU, value,
                              EntryFlags::Deleted, 5U, "clean-two");
        clean_history.finalize();
        const auto upgraded = translate_xmp_capture_metadata(clean_history,
                                                             options,
                                                             &clean_history);
        ASSERT_EQ(upgraded.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(upgraded.entries_updated, 1U);
        EXPECT_EQ(upgraded.entries_added, 0U);
        EXPECT_EQ(upgraded.groups_translated, 1U);
        ids = settings_native_history_ids(clean_history, 0x829aU);
        ASSERT_EQ(ids.size(), 2U);
        EXPECT_TRUE(any(clean_history.entry(ids[0]).flags, EntryFlags::Dirty));
        EXPECT_FALSE(any(clean_history.entry(ids[1]).flags, EntryFlags::Dirty));
        const auto repeated = translate_xmp_capture_metadata(clean_history,
                                                             options,
                                                             &clean_history);
        EXPECT_EQ(repeated.groups_unchanged, 1U);
        EXPECT_EQ(repeated.entries_updated, 0U);
        EXPECT_EQ(repeated.entries_added, 0U);

        MetaStore active_and_history;
        settings_xmp(active_and_history, "ExposureTime", value,
                     EntryFlags::Dirty | EntryFlags::Deleted);
        settings_native_entry(active_and_history, 0x829aU, value,
                              EntryFlags::Deleted, 5U, "clean-one");
        settings_native_entry(active_and_history, 0x829aU, value,
                              EntryFlags::Deleted, 5U, "clean-two");
        settings_native_entry(active_and_history, 0x829aU, value,
                              EntryFlags::Dirty | EntryFlags::Deleted, 5U,
                              "dirty-intent");
        settings_native_entry(active_and_history, 0x829aU, value,
                              EntryFlags::None, 5U, "active-value");
        active_and_history.finalize();
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        const auto removed
            = translate_xmp_capture_metadata(active_and_history, options,
                                             &active_and_history);
        ASSERT_EQ(removed.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(removed.entries_removed, 1U);
        EXPECT_EQ(removed.entries_updated, 0U);
        EXPECT_EQ(removed.entries_added, 0U);
        ids = settings_native_history_ids(active_and_history, 0x829aU);
        ASSERT_EQ(ids.size(), 4U);
        EXPECT_FALSE(
            any(active_and_history.entry(ids[0]).flags, EntryFlags::Dirty));
        EXPECT_FALSE(
            any(active_and_history.entry(ids[1]).flags, EntryFlags::Dirty));
        EXPECT_TRUE(
            any(active_and_history.entry(ids[2]).flags, EntryFlags::Dirty));
        EXPECT_TRUE(
            any(active_and_history.entry(ids[3]).flags, EntryFlags::Dirty));
        for (const EntryId id : ids)
            EXPECT_TRUE(
                any(active_and_history.entry(id).flags, EntryFlags::Deleted));
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
    TEST(MetadataCaptureRational,
         ExactCleanPromotionsPreserveRationalComponentsAndWireFlags)
    {
        constexpr std::array<URational, 4> native_values {
            { { 6U, 4U }, { 10U, 8U }, { 400U, 2U }, { 15U, 6U } }
        };
        constexpr std::array<std::string_view, 4> wire_names {
            "native-distance", "native-zoom", "native-index", "native-flash"
        };
        MetaStore source = rational_source();
        for (size_t i = 0U; i < native_values.size(); ++i)
            settings_native_entry(
                source, kRationalTags[i],
                make_urational(native_values[i].numer,
                               native_values[i].denom),
                EntryFlags::ValueBigEndian, 5U, wire_names[i]);
        source.finalize();

        RationalOptions limited;
        limited.max_operations = 3U;
        MetaStore separate;
        settings_native(separate, 0x829aU, make_urational(1U, 100U));
        separate.finalize();
        const size_t source_count = source.entries().size();
        const auto limited_separate = translate_xmp_capture_rational_metadata(
            source, limited, &separate);
        EXPECT_EQ(limited_separate.status,
                  SettingsStatus::OperationLimitExceeded);
        ASSERT_EQ(separate.entries().size(), 1U);
        ASSERT_NE(settings_find(separate, 0x829aU), nullptr);
        EXPECT_EQ(settings_find(separate, 0x829aU)->value.data.ur.numer, 1U);
        const auto limited_alias = translate_xmp_capture_rational_metadata(
            source, limited, &source);
        EXPECT_EQ(limited_alias.status,
                  SettingsStatus::OperationLimitExceeded);
        EXPECT_EQ(source.entries().size(), source_count);
        for (uint16_t tag : kRationalTags) {
            const Entry* entry = settings_find(source, tag);
            ASSERT_NE(entry, nullptr) << tag;
            EXPECT_FALSE(any(entry->flags, EntryFlags::Dirty)) << tag;
        }

        const auto promoted
            = translate_xmp_capture_rational_metadata(source, {}, &source);
        ASSERT_EQ(promoted.status, SettingsStatus::Ok);
        EXPECT_EQ(promoted.entries_updated, 4U);
        EXPECT_EQ(promoted.entries_added, 0U);
        EXPECT_EQ(promoted.entries_removed, 0U);
        for (size_t i = 0U; i < native_values.size(); ++i) {
            const Entry* entry = settings_find(source, kRationalTags[i]);
            ASSERT_NE(entry, nullptr) << i;
            EXPECT_EQ(entry->value.data.ur.numer, native_values[i].numer) << i;
            EXPECT_EQ(entry->value.data.ur.denom, native_values[i].denom) << i;
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty)) << i;
            EXPECT_TRUE(any(entry->flags, EntryFlags::ValueBigEndian)) << i;
            EXPECT_EQ(entry->origin.wire_type.family, WireFamily::Tiff) << i;
            EXPECT_EQ(entry->origin.wire_type.code, 5U) << i;
            EXPECT_EQ(entry->origin.wire_count, 1U) << i;
            EXPECT_EQ(entry->origin.order_in_block, 17U) << i;
            const auto name = source.arena().span(entry->origin.wire_type_name);
            EXPECT_EQ(std::string_view(
                          reinterpret_cast<const char*>(name.data()),
                          name.size()),
                      wire_names[i]);
        }
        const auto repeated
            = translate_xmp_capture_rational_metadata(source, {}, &source);
        ASSERT_EQ(repeated.status, SettingsStatus::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 4U);
        EXPECT_EQ(repeated.entries_updated, 0U);

        RationalOptions preserve;
        preserve.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
        MetaStore preserved = rational_source();
        for (size_t i = 0U; i < native_values.size(); ++i)
            settings_native_entry(
                preserved, kRationalTags[i],
                make_urational(native_values[i].numer,
                               native_values[i].denom),
                EntryFlags::None, 5U, wire_names[i]);
        preserved.finalize();
        const auto kept = translate_xmp_capture_rational_metadata(
            preserved, preserve, &preserved);
        ASSERT_EQ(kept.status, SettingsStatus::Ok);
        EXPECT_EQ(kept.groups_preserved, 4U);
        EXPECT_EQ(kept.entries_updated, 0U);
        for (size_t i = 0U; i < native_values.size(); ++i) {
            const Entry* entry = settings_find(preserved, kRationalTags[i]);
            ASSERT_NE(entry, nullptr) << i;
            EXPECT_FALSE(any(entry->flags, EntryFlags::Dirty)) << i;
            EXPECT_EQ(entry->value.data.ur.numer, native_values[i].numer) << i;
            EXPECT_EQ(entry->value.data.ur.denom, native_values[i].denom) << i;
        }

        MetaStore edge;
        settings_xmp(edge, "SubjectDistance",
                     make_text(edge.arena(), "Infinity", TextEncoding::Ascii));
        settings_xmp(edge, "DigitalZoomRatio", make_urational(0U, 1U));
        settings_native_entry(edge, 0x9206U,
                              make_urational(UINT32_MAX, 7U),
                              EntryFlags::ValueBigEndian, 5U,
                              "native-infinity");
        settings_native_entry(edge, 0xa404U, make_urational(0U, 19U),
                              EntryFlags::ValueBigEndian, 5U, "native-zero");
        edge.finalize();
        RationalOptions selected;
        selected.exposure_index_to_exif = false;
        selected.flash_energy_to_exif  = false;
        const auto edge_result = translate_xmp_capture_rational_metadata(
            edge, selected, &edge);
        ASSERT_EQ(edge_result.status, SettingsStatus::Ok);
        EXPECT_EQ(edge_result.entries_updated, 2U);
        const Entry* infinity = settings_find(edge, 0x9206U);
        const Entry* zero      = settings_find(edge, 0xa404U);
        ASSERT_NE(infinity, nullptr);
        ASSERT_NE(zero, nullptr);
        EXPECT_EQ(infinity->value.data.ur.numer, UINT32_MAX);
        EXPECT_EQ(infinity->value.data.ur.denom, 7U);
        EXPECT_EQ(zero->value.data.ur.numer, 0U);
        EXPECT_EQ(zero->value.data.ur.denom, 19U);
    }
    TEST(MetadataCaptureRational, MissingNativeDeleteIntentsAreTypedAndStable)
    {
        MetaStore source = rational_source(EntryFlags::Dirty
                                           | EntryFlags::Deleted);
        source.finalize();
        const size_t source_count = source.entries().size();
        MetaStore separate;
        settings_native(separate, 0x829aU, make_urational(1U, 100U));
        separate.finalize();
        RationalOptions limited;
        limited.max_added_entries = 3U;
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, limited,
                                                          &separate)
                      .status,
                  SettingsStatus::EntryLimitExceeded);
        ASSERT_EQ(separate.entries().size(), 1U);
        ASSERT_NE(settings_find(separate, 0x829aU), nullptr);
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, limited,
                                                          &source)
                      .status,
                  SettingsStatus::EntryLimitExceeded);
        EXPECT_EQ(source.entries().size(), source_count);
        limited                      = {};
        limited.max_operations      = 3U;
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, limited,
                                                          &separate)
                      .status,
                  SettingsStatus::OperationLimitExceeded);
        EXPECT_EQ(translate_xmp_capture_rational_metadata(source, limited,
                                                          &source)
                      .status,
                  SettingsStatus::OperationLimitExceeded);
        EXPECT_EQ(source.entries().size(), source_count);
        const auto first
            = translate_xmp_capture_rational_metadata(source, {}, &source);
        ASSERT_EQ(first.status, SettingsStatus::Ok);
        EXPECT_EQ(first.entries_added, 4U);
        for (uint16_t tag : kRationalTags) {
            const auto ids = settings_native_history_ids(source, tag);
            ASSERT_EQ(ids.size(), 1U) << tag;
            const Entry& entry = source.entry(ids.front());
            EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty)) << tag;
            EXPECT_TRUE(any(entry.flags, EntryFlags::Deleted)) << tag;
            EXPECT_EQ(entry.value.kind, MetaValueKind::Scalar) << tag;
            EXPECT_EQ(entry.value.elem_type, MetaElementType::URational) << tag;
            EXPECT_EQ(entry.value.data.ur.numer, 0U) << tag;
            EXPECT_EQ(entry.value.data.ur.denom, 1U) << tag;
        }
        const auto repeated
            = translate_xmp_capture_rational_metadata(source, {}, &source);
        ASSERT_EQ(repeated.status, SettingsStatus::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 4U);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(repeated.entries_updated, 0U);
        EXPECT_EQ(repeated.entries_removed, 0U);

        MetaStore reused = rational_source(EntryFlags::Dirty
                                           | EntryFlags::Deleted);
        settings_native_entry(reused, 0xa404U, make_urational(9U, 7U),
                              EntryFlags::Deleted, 5U, "clean-delete");
        reused.finalize();
        const auto reuse_result
            = translate_xmp_capture_rational_metadata(reused, {}, &reused);
        ASSERT_EQ(reuse_result.status, SettingsStatus::Ok);
        EXPECT_EQ(reuse_result.entries_added, 3U);
        EXPECT_EQ(reuse_result.entries_updated, 1U);
        const auto reused_ids = settings_native_history_ids(reused, 0xa404U);
        ASSERT_EQ(reused_ids.size(), 1U);
        const Entry& reused_intent = reused.entry(reused_ids.front());
        EXPECT_TRUE(any(reused_intent.flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(reused_intent.flags, EntryFlags::Deleted));
        EXPECT_EQ(reused_intent.value.data.ur.numer, 9U);
        EXPECT_EQ(reused_intent.value.data.ur.denom, 7U);
        const auto reused_again
            = translate_xmp_capture_rational_metadata(reused, {}, &reused);
        ASSERT_EQ(reused_again.status, SettingsStatus::Ok);
        EXPECT_EQ(reused_again.groups_unchanged, 4U);
        EXPECT_EQ(reused_again.entries_added, 0U);
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
            EXPECT_EQ(result.entries_updated, 1U);
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
    TEST(MetadataCaptureRational,
         OmittedAndIneligibleSourcesLeaveNativeAuthorityUnchanged)
    {
        MetaStore omitted = rational_source();
        settings_native_entry(omitted, 0xa20bU, make_urational(7U, 3U),
                              EntryFlags::None, 5U, "omitted-flash");
        omitted.finalize();
        RationalOptions omit_flash;
        omit_flash.flash_energy_to_exif = false;
        const auto omitted_result = translate_xmp_capture_rational_metadata(
            omitted, omit_flash, &omitted);
        ASSERT_EQ(omitted_result.status, SettingsStatus::Ok);
        const Entry* flash = settings_find(omitted, 0xa20bU);
        ASSERT_NE(flash, nullptr);
        EXPECT_FALSE(any(flash->flags, EntryFlags::Dirty));
        EXPECT_EQ(flash->value.data.ur.numer, 7U);
        EXPECT_EQ(flash->value.data.ur.denom, 3U);

        constexpr std::array<URational, 4> expected {
            { { 3U, 2U }, { 5U, 4U }, { 200U, 1U }, { 5U, 2U } }
        };
        MetaStore ineligible = rational_source(EntryFlags::None);
        for (size_t i = 0U; i < expected.size(); ++i)
            settings_native_entry(
                ineligible, kRationalTags[i],
                make_urational(expected[i].numer, expected[i].denom),
                EntryFlags::None, 5U, "ineligible-native");
        ineligible.finalize();
        const auto ignored = translate_xmp_capture_rational_metadata(
            ineligible, {}, &ineligible);
        ASSERT_EQ(ignored.status, SettingsStatus::Ok);
        EXPECT_EQ(ignored.source_properties, 0U);
        EXPECT_EQ(ignored.entries_updated, 0U);
        for (size_t i = 0U; i < expected.size(); ++i) {
            const Entry* entry = settings_find(ineligible, kRationalTags[i]);
            ASSERT_NE(entry, nullptr) << i;
            EXPECT_FALSE(any(entry->flags, EntryFlags::Dirty)) << i;
            EXPECT_EQ(entry->value.data.ur.numer, expected[i].numer) << i;
            EXPECT_EQ(entry->value.data.ur.denom, expected[i].denom) << i;
        }

        MetaStore clean_tombstones
            = rational_source(EntryFlags::Deleted);
        for (size_t i = 0U; i < expected.size(); ++i)
            settings_native_entry(
                clean_tombstones, kRationalTags[i],
                make_urational(expected[i].numer, expected[i].denom),
                EntryFlags::None, 5U, "clean-tombstone-source-native");
        clean_tombstones.finalize();
        RationalOptions all;
        all.source_mode = MetadataCaptureTranslationSourceMode::All;
        const auto clean_ignored = translate_xmp_capture_rational_metadata(
            clean_tombstones, all, &clean_tombstones);
        ASSERT_EQ(clean_ignored.status, SettingsStatus::Ok);
        EXPECT_EQ(clean_ignored.source_properties, 0U);
        EXPECT_EQ(clean_ignored.entries_updated, 0U);
        EXPECT_EQ(clean_ignored.entries_removed, 0U);
        for (size_t i = 0U; i < expected.size(); ++i) {
            const Entry* entry
                = settings_find(clean_tombstones, kRationalTags[i]);
            ASSERT_NE(entry, nullptr) << i;
            EXPECT_FALSE(any(entry->flags, EntryFlags::Dirty)) << i;
        }
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
        const auto preserved = translate_xmp_capture_rational_metadata(
            source, options, &output);
        ASSERT_EQ(preserved.status, SettingsStatus::Ok);
        EXPECT_EQ(preserved.groups_preserved, 1U);
        EXPECT_EQ(settings_active_count(output, 0xa404U), 2U);
        for (EntryId id : settings_native_history_ids(output, 0xa404U))
            EXPECT_FALSE(any(output.entry(id).flags, EntryFlags::Dirty));
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
    TEST(MetadataCaptureRational,
         DuplicateRepairOperationsPreflightSeparateAndAliasedOutputs)
    {
        MetaStore source;
        settings_xmp(source, "DigitalZoomRatio",
                     make_text(source.arena(), "5/4", TextEncoding::Ascii));
        settings_native_entry(source, 0xa404U, make_urational(1U, 1U),
                              EntryFlags::None, 5U, "first-duplicate");
        settings_native_entry(source, 0xa404U, make_urational(3U, 1U),
                              EntryFlags::None, 5U, "second-duplicate");
        source.finalize();
        RationalOptions options;
        options.subject_distance_to_exif = false;
        options.exposure_index_to_exif   = false;
        options.flash_energy_to_exif     = false;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        options.max_operations           = 1U;
        MetaStore separate;
        settings_native(separate, 0x829aU, make_urational(1U, 100U));
        separate.finalize();
        const size_t source_count = source.entries().size();
        const auto separate_result
            = translate_xmp_capture_rational_metadata(source, options,
                                                      &separate);
        EXPECT_EQ(separate_result.status,
                  SettingsStatus::OperationLimitExceeded);
        ASSERT_EQ(separate.entries().size(), 1U);
        ASSERT_NE(settings_find(separate, 0x829aU), nullptr);
        EXPECT_EQ(settings_find(separate, 0x829aU)->value.data.ur.numer, 1U);
        const auto aliased_result
            = translate_xmp_capture_rational_metadata(source, options, &source);
        EXPECT_EQ(aliased_result.status,
                  SettingsStatus::OperationLimitExceeded);
        EXPECT_EQ(source.entries().size(), source_count);
        EXPECT_EQ(settings_active_count(source, 0xa404U), 2U);
        for (EntryId id : settings_native_history_ids(source, 0xa404U))
            EXPECT_FALSE(any(source.entry(id).flags, EntryFlags::Dirty));

        options.max_operations = 2U;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        const auto repaired
            = translate_xmp_capture_rational_metadata(source, options, &source);
        ASSERT_EQ(repaired.status, SettingsStatus::Ok);
        EXPECT_EQ(repaired.entries_updated, 1U);
        EXPECT_EQ(repaired.entries_removed, 1U);
        ASSERT_EQ(settings_active_count(source, 0xa404U), 1U);
        EXPECT_EQ(settings_find(source, 0xa404U)->value.data.ur.numer, 5U);
        EXPECT_EQ(settings_find(source, 0xa404U)->value.data.ur.denom, 4U);
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
    TEST(MetadataFlash,
         ScalarOwnerLifecycleAddsIntentsAndKeepsExactWireProvenance)
    {
        MetaStore deleted;
        settings_xmp(deleted, "Flash", make_u16(25U),
                     EntryFlags::Dirty | EntryFlags::Deleted);
        deleted.finalize();
        auto intent = translate_xmp_flash_metadata(deleted, {}, &deleted);
        ASSERT_EQ(intent.status, SettingsStatus::Ok);
        EXPECT_EQ(intent.entries_added, 1U);
        EXPECT_EQ(intent.entries_updated, 0U);
        ASSERT_NO_FATAL_FAILURE(expect_native_delete_intent(deleted, 0x9209U));
        const auto repeated = translate_xmp_flash_metadata(deleted, {},
                                                           &deleted);
        expect_lifecycle_repeat(repeated.groups_unchanged,
                                repeated.entries_added,
                                repeated.entries_updated);

        MetaStore structured
            = flash_source(25U, false, EntryFlags::Dirty | EntryFlags::Deleted);
        structured.finalize();
        const auto structured_intent
            = translate_xmp_flash_metadata(structured, {}, &structured);
        ASSERT_EQ(structured_intent.status, SettingsStatus::Ok);
        EXPECT_EQ(structured_intent.entries_added, 1U);
        ASSERT_NO_FATAL_FAILURE(
            expect_native_delete_intent(structured, 0x9209U));

        MetaStore exact;
        settings_xmp(exact, "Flash", make_u16(25U));
        settings_native_entry(exact, 0x9209U, make_u16(25U), EntryFlags::None,
                              3U, "native-flash-wire");
        exact.finalize();
        FlashOptions options;
        options.max_operations = 1U;
        const auto accepted    = translate_xmp_flash_metadata(exact, options,
                                                              &exact);
        ASSERT_EQ(accepted.status, SettingsStatus::Ok);
        EXPECT_EQ(accepted.entries_updated, 1U);
        const Entry* native = settings_find(exact, 0x9209U);
        ASSERT_NE(native, nullptr);
        EXPECT_EQ(native->value.data.u64, 25U);
        ASSERT_NO_FATAL_FAILURE(expect_native_authority(exact, 0x9209U, 3U, 1U,
                                                        "native-flash-wire"));
        const auto same = translate_xmp_flash_metadata(exact, {}, &exact);
        expect_lifecycle_repeat(same.groups_unchanged, same.entries_added,
                                same.entries_updated);

        MetaStore omitted;
        settings_native(omitted, 0x9209U, make_u16(1U));
        omitted.finalize();
        EXPECT_EQ(
            translate_xmp_flash_metadata(omitted, {}, &omitted).entries_removed,
            0U);
        EXPECT_EQ(settings_find(omitted, 0x9209U)->value.data.u64, 1U);

        MetaStore conflict;
        settings_xmp(conflict, "Flash", make_u16(25U),
                     EntryFlags::Dirty | EntryFlags::Deleted);
        settings_native(conflict, 0x9209U, make_u16(25U));
        conflict.finalize();
        EXPECT_EQ(translate_xmp_flash_metadata(conflict, {}, &conflict).status,
                  SettingsStatus::NativeConflict);
        options                 = {};
        options.conflict_policy = SettingsPolicy::PreserveExisting;
        EXPECT_EQ(translate_xmp_flash_metadata(conflict, options, &conflict)
                      .groups_preserved,
                  1U);
        options.conflict_policy = SettingsPolicy::ReplaceExisting;
        options.max_operations  = 1U;
        const auto replaced = translate_xmp_flash_metadata(conflict, options,
                                                           &conflict);
        ASSERT_EQ(replaced.status, SettingsStatus::Ok);
        EXPECT_EQ(replaced.entries_removed, 1U);
        EXPECT_EQ(replaced.entries_added, 0U);
        EXPECT_EQ(settings_active_count(conflict, 0x9209U), 0U);
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
        for (uint16_t tag : kSensitivityTags) {
            EXPECT_EQ(settings_find(source, tag), nullptr);
            EXPECT_TRUE(settings_native_history_ids(source, tag).empty())
                << tag;
        }
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
        EXPECT_EQ(result.entries_added, 5U);
        EXPECT_EQ(result.entries_updated, 1U);
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
         BaseTombstoneCreatesAllSevenMissingNativeDeleteIntents)
    {
        MetaStore source;
        settings_xmp(source, kSensitivityNames[0], make_u16(400U),
                     EntryFlags::Dirty | EntryFlags::Deleted, kSensitivityNs);
        source.finalize();

        const auto first = translate_xmp_sensitivity_metadata(source, {},
                                                              &source);
        ASSERT_EQ(first.status, SettingsStatus::Ok);
        EXPECT_EQ(first.groups_translated, 1U);
        EXPECT_EQ(first.entries_added, 7U);
        EXPECT_EQ(first.entries_updated, 0U);
        EXPECT_EQ(first.entries_removed, 0U);
        for (uint16_t tag : kSensitivityTags) {
            const auto ids = settings_native_history_ids(source, tag);
            ASSERT_EQ(ids.size(), 1U) << tag;
            const Entry& entry = source.entry(ids.front());
            EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty)) << tag;
            EXPECT_TRUE(any(entry.flags, EntryFlags::Deleted)) << tag;
        }

        const auto repeated = translate_xmp_sensitivity_metadata(source, {},
                                                                 &source);
        ASSERT_EQ(repeated.status, SettingsStatus::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 1U);
        EXPECT_EQ(repeated.groups_translated, 0U);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(repeated.entries_updated, 0U);
        EXPECT_EQ(repeated.entries_removed, 0U);
    }
    static MetaStore sparse_sensitivity_source()
    {
        MetaStore source;
        settings_xmp(source, kSensitivityNames[0], make_u16(400U),
                     EntryFlags::Dirty, kSensitivityNs);
        settings_xmp(source, kSensitivityNames[1], make_u16(0U),
                     EntryFlags::Dirty, kSensitivityNs);
        return source;
    }

    TEST(MetadataSensitivity,
         SparseValidatedGroupDeletesMissingMembersAsOneTransaction)
    {
        MetaStore source = sparse_sensitivity_source();
        source.finalize();
        const auto translated = translate_xmp_sensitivity_metadata(source, {},
                                                                   &source);
        ASSERT_EQ(translated.status, SettingsStatus::Ok);
        EXPECT_EQ(translated.groups_translated, 1U);
        EXPECT_EQ(translated.source_properties, 2U);
        EXPECT_EQ(translated.entries_added, 7U);
        EXPECT_EQ(translated.entries_updated, 0U);
        EXPECT_EQ(translated.entries_removed, 0U);
        for (size_t i = 0U; i < kSensitivityTags.size(); ++i) {
            const auto ids = settings_native_history_ids(source,
                                                         kSensitivityTags[i]);
            ASSERT_EQ(ids.size(), 1U) << i;
            const Entry& entry = source.entry(ids.front());
            EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty)) << i;
            EXPECT_EQ(any(entry.flags, EntryFlags::Deleted), i >= 2U) << i;
            if (i < 2U)
                EXPECT_EQ(entry.value.data.u64, i == 0U ? 400U : 0U) << i;
        }
        const auto repeated = translate_xmp_sensitivity_metadata(source, {},
                                                                 &source);
        EXPECT_EQ(repeated.groups_unchanged, 1U);
        EXPECT_EQ(repeated.groups_translated, 0U);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(repeated.entries_updated, 0U);

        SensitivityOptions options;
        options.max_added_entries = 6U;
        MetaStore entry_limited   = sparse_sensitivity_source();
        sensitivity_failure(entry_limited, SettingsStatus::EntryLimitExceeded,
                            options);
        options                     = {};
        options.max_operations      = 6U;
        MetaStore operation_limited = sparse_sensitivity_source();
        sensitivity_failure(operation_limited,
                            SettingsStatus::OperationLimitExceeded, options);

        MetaStore preserved_source = sparse_sensitivity_source();
        settings_native(preserved_source, kSensitivityTags[0], make_u16(400U));
        preserved_source.finalize();
        MetaStore preserved;
        options                 = {};
        options.conflict_policy = SettingsPolicy::PreserveExisting;
        const auto kept = translate_xmp_sensitivity_metadata(preserved_source,
                                                             options,
                                                             &preserved);
        ASSERT_EQ(kept.status, SettingsStatus::Ok);
        EXPECT_EQ(kept.groups_preserved, 1U);
        EXPECT_EQ(kept.entries_added, 0U);
        EXPECT_EQ(settings_active_count(preserved, kSensitivityTags[0]), 1U);
        for (size_t i = 1U; i < kSensitivityTags.size(); ++i)
            EXPECT_EQ(preserved
                          .find_all(make_exif_tag_key_view("exififd",
                                                           kSensitivityTags[i]))
                          .size(),
                      0U)
                << i;
    }
    static MetaStore exact_sensitivity_source()
    {
        MetaStore source;
        sensitivity_source(source, 400U, 7U, 400U);
        for (size_t i = 0U; i < kSensitivityTags.size(); ++i) {
            const MetaValue value = i < 2U ? make_u16(i == 0U ? 400U : 7U)
                                           : make_u32(400U);
            settings_native_entry(source, kSensitivityTags[i], value,
                                  EntryFlags::None, i < 2U ? 3U : 4U,
                                  "clean-sensitivity-wire");
        }
        return source;
    }

    TEST(MetadataSensitivity,
         ExactCleanGroupUpdatesPreserveAllSevenNativeWireRecords)
    {
        SensitivityOptions options;
        options.max_operations = 6U;
        MetaStore limited      = exact_sensitivity_source();
        limited.finalize();
        MetaStore output;
        settings_native(output, 0x9209U, make_u16(95U));
        output.finalize();
        const size_t limited_size = limited.entries().size();
        EXPECT_EQ(translate_xmp_sensitivity_metadata(limited, options, &output)
                      .status,
                  SettingsStatus::OperationLimitExceeded);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(settings_find(output, 0x9209U)->value.data.u64, 95U);
        EXPECT_EQ(translate_xmp_sensitivity_metadata(limited, options, &limited)
                      .status,
                  SettingsStatus::OperationLimitExceeded);
        EXPECT_EQ(limited.entries().size(), limited_size);
        for (size_t i = 0U; i < kSensitivityTags.size(); ++i)
            EXPECT_FALSE(any(settings_find(limited, kSensitivityTags[i])->flags,
                             EntryFlags::Dirty))
                << i;

        options.max_operations = 7U;
        MetaStore exact        = exact_sensitivity_source();
        exact.finalize();
        const auto authority
            = translate_xmp_sensitivity_metadata(exact, options, &exact);
        ASSERT_EQ(authority.status, SettingsStatus::Ok);
        EXPECT_EQ(authority.entries_updated, 7U);
        EXPECT_EQ(authority.entries_added, 0U);
        for (size_t i = 0U; i < kSensitivityTags.size(); ++i) {
            const Entry* entry = settings_find(exact, kSensitivityTags[i]);
            ASSERT_NE(entry, nullptr) << i;
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty)) << i;
            EXPECT_EQ(entry->value.data.u64,
                      i == 0U ? 400U : (i == 1U ? 7U : 400U))
                << i;
            EXPECT_EQ(entry->origin.order_in_block, 17U) << i;
            EXPECT_EQ(entry->origin.wire_type.code, i < 2U ? 3U : 4U) << i;
            const auto wire = exact.arena().span(entry->origin.wire_type_name);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                           wire.data()),
                                       wire.size()),
                      "clean-sensitivity-wire")
                << i;
        }
        const auto repeated = translate_xmp_sensitivity_metadata(exact, {},
                                                                 &exact);
        EXPECT_EQ(repeated.groups_unchanged, 1U);
        EXPECT_EQ(repeated.entries_updated, 0U);
    }
    TEST(MetadataSensitivity, CaptureBasicIsoAndFullGroupCanRunInEitherOrder)
    {
        for (const bool sensitivity_first : { false, true }) {
            MetaStore source;
            settings_xmp(source, "ISO", make_u16(400U));
            for (size_t i = 1U; i < kSensitivityNames.size(); ++i)
                settings_xmp(source, kSensitivityNames[i],
                             make_u32(i == 1U ? 7U : 400U), EntryFlags::Dirty,
                             kSensitivityNs);
            source.finalize();
            MetadataCaptureTranslationOptions basic_iso;
            basic_iso.iso_to_exif = false;
            if (sensitivity_first) {
                ASSERT_EQ(translate_xmp_sensitivity_metadata(source, {}, &source)
                              .status,
                          SettingsStatus::Ok);
                EXPECT_EQ(translate_xmp_capture_metadata(source, basic_iso,
                                                         &source)
                              .status,
                          SettingsStatus::Ok);
            } else {
                EXPECT_EQ(translate_xmp_capture_metadata(source, basic_iso,
                                                         &source)
                              .status,
                          SettingsStatus::Ok);
                ASSERT_EQ(translate_xmp_sensitivity_metadata(source, {}, &source)
                              .status,
                          SettingsStatus::Ok);
            }
            for (const uint16_t tag : kSensitivityTags)
                EXPECT_EQ(settings_active_count(source, tag), 1U) << tag;
        }
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
            const auto accepted = translate_xmp_camera_text_metadata(source, {},
                                                                     &source);
            EXPECT_EQ(accepted.groups_unchanged, field < 3U ? 1U : 0U);
            EXPECT_EQ(accepted.entries_updated, field < 3U ? 0U : 1U);
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
    TEST(MetadataCameraText, LensOwnerLifecyclePreservesTextAndWireProvenance)
    {
        for (size_t field = 3U; field < kCameraTextNames.size(); ++field) {
            SCOPED_TRACE(kCameraTextNames[field]);
            CameraTextOptions options;
            options.spectral_sensitivity_to_exif = false;
            options.camera_owner_name_to_exif    = false;
            options.body_serial_number_to_exif   = false;
            options.lens_make_to_exif            = false;
            options.lens_model_to_exif           = false;
            options.lens_serial_number_to_exif   = false;
            options.*kCameraTextFlags[field]     = true;

            MetaStore deleted;
            settings_xmp(deleted, kCameraTextNames[field],
                         make_text(deleted.arena(), "ignored",
                                   TextEncoding::Utf8),
                         EntryFlags::Dirty | EntryFlags::Deleted,
                         kSensitivityNs);
            deleted.finalize();
            const auto removed = translate_xmp_camera_text_metadata(deleted,
                                                                    options,
                                                                    &deleted);
            ASSERT_EQ(removed.status, CameraTextStatus::Ok)
                << kCameraTextNames[field];
            EXPECT_EQ(removed.entries_added, 1U) << kCameraTextNames[field];
            EXPECT_EQ(removed.entries_updated, 0U) << kCameraTextNames[field];
            ASSERT_NO_FATAL_FAILURE(
                expect_native_delete_intent(deleted, kCameraTextTags[field]));
            const auto repeated = translate_xmp_camera_text_metadata(deleted,
                                                                     options,
                                                                     &deleted);
            expect_lifecycle_repeat(repeated.groups_unchanged,
                                    repeated.entries_added,
                                    repeated.entries_updated);

            MetaStore exact;
            settings_xmp(exact, kCameraTextNames[field],
                         make_text(exact.arena(), "Lens Value",
                                   TextEncoding::Utf8),
                         EntryFlags::Dirty, kSensitivityNs);
            settings_native_entry(exact, kCameraTextTags[field],
                                  make_text(exact.arena(),
                                            std::string_view("Lens Value\0\0",
                                                             12U),
                                            TextEncoding::Ascii),
                                  EntryFlags::None, 2U, "native-lens-wire");
            exact.finalize();
            const auto authority
                = translate_xmp_camera_text_metadata(exact, options, &exact);
            ASSERT_EQ(authority.status, CameraTextStatus::Ok)
                << kCameraTextNames[field];
            EXPECT_EQ(authority.entries_updated, 1U) << kCameraTextNames[field];
            const Entry* native = settings_find(exact, kCameraTextTags[field]);
            ASSERT_NE(native, nullptr) << kCameraTextNames[field];
            EXPECT_EQ(native->value.text_encoding, TextEncoding::Ascii)
                << kCameraTextNames[field];
            EXPECT_EQ(camera_text_value(exact, kCameraTextTags[field]),
                      std::string_view("Lens Value\0\0", 12U))
                << kCameraTextNames[field];
            ASSERT_NO_FATAL_FAILURE(
                expect_native_authority(exact, kCameraTextTags[field], 2U, 12U,
                                        "native-lens-wire"));
            const auto same = translate_xmp_camera_text_metadata(exact, options,
                                                                 &exact);
            expect_lifecycle_repeat(same.groups_unchanged, same.entries_added,
                                    same.entries_updated);

            MetaStore omitted;
            settings_native(omitted, kCameraTextTags[field],
                            make_text(omitted.arena(), "retained",
                                      TextEncoding::Ascii));
            omitted.finalize();
            EXPECT_EQ(translate_xmp_camera_text_metadata(omitted, options,
                                                         &omitted)
                          .entries_removed,
                      0U)
                << kCameraTextNames[field];
            EXPECT_EQ(camera_text_value(omitted, kCameraTextTags[field]),
                      "retained")
                << kCameraTextNames[field];

            MetaStore conflict;
            settings_xmp(conflict, kCameraTextNames[field],
                         make_text(conflict.arena(), "ignored",
                                   TextEncoding::Utf8),
                         EntryFlags::Dirty | EntryFlags::Deleted,
                         kSensitivityNs);
            settings_native(conflict, kCameraTextTags[field],
                            make_text(conflict.arena(), "old",
                                      TextEncoding::Ascii));
            conflict.finalize();
            EXPECT_EQ(translate_xmp_camera_text_metadata(conflict, options,
                                                         &conflict)
                          .status,
                      CameraTextStatus::NativeConflict)
                << kCameraTextNames[field];
            options.conflict_policy = CameraTextPolicy::PreserveExisting;
            EXPECT_EQ(translate_xmp_camera_text_metadata(conflict, options,
                                                         &conflict)
                          .groups_preserved,
                      1U)
                << kCameraTextNames[field];
            options.conflict_policy = CameraTextPolicy::ReplaceExisting;
            options.max_operations  = 1U;
            const auto replaced = translate_xmp_camera_text_metadata(conflict,
                                                                     options,
                                                                     &conflict);
            ASSERT_EQ(replaced.status, CameraTextStatus::Ok)
                << kCameraTextNames[field];
            EXPECT_EQ(replaced.entries_removed, 1U) << kCameraTextNames[field];
            EXPECT_EQ(replaced.entries_added, 0U) << kCameraTextNames[field];
            EXPECT_EQ(settings_active_count(conflict, kCameraTextTags[field]),
                      0U)
                << kCameraTextNames[field];
        }

        MetaStore deletions;
        for (size_t field = 3U; field < kCameraTextNames.size(); ++field)
            settings_xmp(deletions, kCameraTextNames[field],
                         make_text(deletions.arena(), "ignored",
                                   TextEncoding::Utf8),
                         EntryFlags::Dirty | EntryFlags::Deleted,
                         kSensitivityNs);
        CameraTextOptions options;
        options.spectral_sensitivity_to_exif = false;
        options.camera_owner_name_to_exif    = false;
        options.body_serial_number_to_exif   = false;
        options.max_added_entries            = 2U;
        camera_text_failure(deletions, CameraTextStatus::EntryLimitExceeded,
                            options);
        options                              = {};
        options.spectral_sensitivity_to_exif = false;
        options.camera_owner_name_to_exif    = false;
        options.body_serial_number_to_exif   = false;
        options.max_operations               = 2U;
        camera_text_failure(deletions, CameraTextStatus::OperationLimitExceeded,
                            options);
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

namespace openmeta {
namespace {
    using IdentityOptions = MetadataIdentityTranslationOptions;
    using IdentityStatus  = MetadataCaptureTranslationStatus;
    using IdentityPolicy  = MetadataCaptureTranslationConflictPolicy;
    constexpr std::string_view kIdentityId = "00112233445566778899aAbBcCdDeEfF";
    constexpr std::array<URational, 4> kIdentityLens
        = { URational { 50U, 3U }, { 200U, 3U }, { 14U, 5U }, { 0U, 0U } };

    static void identity_fixture_replace(MetaStore& store, EntryId id,
                                         const Entry& replacement)
    {
        MetaStore rebuilt;
        rebuilt.arena() = store.arena();
        for (BlockId block = 0U; block < store.block_count(); ++block)
            rebuilt.add_block(store.block_info(block));
        for (EntryId entry = 0U; entry < store.entries().size(); ++entry)
            rebuilt.add_entry(entry == id ? replacement : store.entry(entry));
        store = std::move(rebuilt);
    }

    static void identity_source(MetaStore& store, bool indexed = false,
                                bool legacy      = false,
                                EntryFlags flags = EntryFlags::Dirty)
    {
        const auto ns = legacy ? kSettingsNs : kSensitivityNs;
        if (indexed) {
            constexpr std::array<std::string_view, 4> values
                = { "50/3", "200/3", "2.8", "0/0" };
            for (size_t i = 0U; i < values.size(); ++i)
                settings_xmp(
                    store, "LensSpecification[" + std::to_string(i + 1U) + "]",
                    make_text(store.arena(), values[i], TextEncoding::Utf8),
                    flags, ns);
        } else {
            settings_xmp(store, "LensSpecification",
                         make_urational_array(store.arena(), kIdentityLens),
                         flags, ns);
        }
        settings_xmp(store, "ImageUniqueID",
                     make_text(store.arena(), kIdentityId, TextEncoding::Ascii),
                     flags);
    }

    static void expect_identity_lens(const MetaStore& store,
                                     const std::array<URational, 4>& expected
                                     = kIdentityLens)
    {
        const Entry* entry = settings_find(store, 0xa432U);
        ASSERT_NE(entry, nullptr);
        ASSERT_EQ(entry->value.kind, MetaValueKind::Array);
        ASSERT_EQ(entry->value.elem_type, MetaElementType::URational);
        ASSERT_EQ(entry->value.count, 4U);
        const auto bytes = store.arena().span(entry->value.data.span);
        ASSERT_EQ(bytes.size(), sizeof(expected));
        std::array<URational, 4> actual {};
        std::memcpy(actual.data(), bytes.data(), sizeof(actual));
        for (size_t i = 0U; i < actual.size(); ++i) {
            EXPECT_EQ(actual[i].numer, expected[i].numer);
            EXPECT_EQ(actual[i].denom, expected[i].denom);
        }
    }

    static void identity_failure(MetaStore& source, IdentityStatus status,
                                 const IdentityOptions& options = {})
    {
        source.finalize();
        const size_t before = source.entries().size();
        MetaStore output;
        settings_native(output, 0x9209U, make_u16(95U));
        output.finalize();
        EXPECT_EQ(
            translate_xmp_identity_metadata(source, options, &output).status,
            status);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(settings_find(output, 0x9209U)->value.data.u64, 95U);
        EXPECT_EQ(
            translate_xmp_identity_metadata(source, options, &source).status,
            status);
        EXPECT_EQ(source.entries().size(), before);
        EXPECT_EQ(settings_find(source, 0xa432U), nullptr);
        EXPECT_EQ(settings_find(source, 0xa420U), nullptr);
    }

    TEST(MetadataIdentity,
         ExactArrayAndIndexedAliasesCommitTogetherAndOwnPayloads)
    {
        for (bool indexed : { false, true }) {
            for (bool legacy : { false, true }) {
                MetaStore output;
                {
                    MetaStore source;
                    identity_source(source, indexed, legacy);
                    source.finalize();
                    const auto result
                        = translate_xmp_identity_metadata(source, {}, &output);
                    ASSERT_EQ(result.status, IdentityStatus::Ok);
                    EXPECT_EQ(result.groups_translated, 2U);
                    EXPECT_EQ(result.entries_added, 2U);
                    EXPECT_EQ(result.source_properties, indexed ? 5U : 2U);
                    EXPECT_EQ(source.entries().size(), indexed ? 5U : 2U);
                }
                expect_identity_lens(output);
                EXPECT_EQ(camera_text_value(output, 0xa420U), kIdentityId);
                EXPECT_TRUE(any(settings_find(output, 0xa432U)->flags,
                                EntryFlags::Dirty));
                EXPECT_EQ(translate_xmp_identity_metadata(output, {}, &output)
                              .groups_unchanged,
                          2U);
            }
        }
    }

    TEST(MetadataIdentity,
         ExactCleanPromotionOwnsArrayAndTextAndPreservesNativeProvenance)
    {
        MetaStore source;
        identity_source(source);
        constexpr std::array<URational, 4> native_lens {
            { { 100U, 6U }, { 200U, 3U }, { 28U, 10U }, { 0U, 0U } }
        };
        settings_native_entry(
            source, 0xa432U, make_urational_array(source.arena(), native_lens),
            EntryFlags::None, 5U, "native-lens");
        std::string native_id(kIdentityId);
        native_id.push_back('\0');
        settings_native_entry(
            source, 0xa420U,
            make_text(source.arena(),
                      std::string_view(native_id.data(), native_id.size()),
                      TextEncoding::Utf8),
            EntryFlags::None, 2U, "native-id");
        source.finalize();

        IdentityOptions limited;
        limited.max_operations = 1U;
        MetaStore separate;
        settings_native(separate, 0x9209U, make_u16(95U));
        separate.finalize();
        const size_t source_count = source.entries().size();
        const auto limited_separate
            = translate_xmp_identity_metadata(source, limited, &separate);
        EXPECT_EQ(limited_separate.status,
                  IdentityStatus::OperationLimitExceeded);
        ASSERT_EQ(separate.entries().size(), 1U);
        ASSERT_NE(settings_find(separate, 0x9209U), nullptr);
        EXPECT_EQ(settings_find(separate, 0x9209U)->value.data.u64, 95U);
        const auto limited_alias
            = translate_xmp_identity_metadata(source, limited, &source);
        EXPECT_EQ(limited_alias.status,
                  IdentityStatus::OperationLimitExceeded);
        EXPECT_EQ(source.entries().size(), source_count);
        for (uint16_t tag : { uint16_t { 0xa432U }, uint16_t { 0xa420U } }) {
            const Entry* entry = settings_find(source, tag);
            ASSERT_NE(entry, nullptr) << tag;
            EXPECT_FALSE(any(entry->flags, EntryFlags::Dirty)) << tag;
        }
        const auto promoted
            = translate_xmp_identity_metadata(source, {}, &source);
        ASSERT_EQ(promoted.status, IdentityStatus::Ok);
        EXPECT_EQ(promoted.entries_updated, 2U);
        const Entry* lens = settings_find(source, 0xa432U);
        const Entry* id   = settings_find(source, 0xa420U);
        ASSERT_NE(lens, nullptr);
        ASSERT_NE(id, nullptr);
        EXPECT_TRUE(any(lens->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(id->flags, EntryFlags::Dirty));
        EXPECT_EQ(lens->value.kind, MetaValueKind::Array);
        EXPECT_EQ(lens->value.elem_type, MetaElementType::URational);
        EXPECT_EQ(lens->value.count, 4U);
        const auto lens_bytes = source.arena().span(lens->value.data.span);
        ASSERT_EQ(lens_bytes.size(), sizeof(native_lens));
        std::array<URational, 4> actual_lens {};
        std::memcpy(actual_lens.data(), lens_bytes.data(), sizeof(actual_lens));
        for (size_t i = 0U; i < native_lens.size(); ++i) {
            EXPECT_EQ(actual_lens[i].numer, native_lens[i].numer) << i;
            EXPECT_EQ(actual_lens[i].denom, native_lens[i].denom) << i;
        }
        EXPECT_EQ(id->value.kind, MetaValueKind::Text);
        EXPECT_EQ(id->value.text_encoding, TextEncoding::Utf8);
        EXPECT_EQ(id->value.count, native_id.size());
        const auto id_bytes = source.arena().span(id->value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(id_bytes.data()),
                                   id_bytes.size()),
                  std::string_view(native_id.data(), native_id.size()));
        EXPECT_EQ(lens->origin.wire_type.family, WireFamily::Tiff);
        EXPECT_EQ(lens->origin.wire_type.code, 5U);
        EXPECT_EQ(lens->origin.wire_count, 4U);
        EXPECT_EQ(lens->origin.order_in_block, 17U);
        EXPECT_EQ(id->origin.wire_type.family, WireFamily::Tiff);
        EXPECT_EQ(id->origin.wire_type.code, 2U);
        EXPECT_EQ(id->origin.wire_count, native_id.size());
        EXPECT_EQ(id->origin.order_in_block, 17U);
        for (const Entry* entry : { lens, id }) {
            const auto wire_name = source.arena().span(
                entry->origin.wire_type_name);
            EXPECT_EQ(std::string_view(
                          reinterpret_cast<const char*>(wire_name.data()),
                          wire_name.size()),
                      entry == lens ? "native-lens" : "native-id");
        }
        ASSERT_TRUE(validate_store(source).ok());
        const auto repeated
            = translate_xmp_identity_metadata(source, {}, &source);
        ASSERT_EQ(repeated.status, IdentityStatus::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 2U);
        EXPECT_EQ(repeated.entries_updated, 0U);

        MetaStore deleted;
        identity_source(deleted, false, false,
                        EntryFlags::Dirty | EntryFlags::Deleted);
        deleted.finalize();
        const size_t deleted_source_count = deleted.entries().size();
        MetaStore deletion_output;
        settings_native(deletion_output, 0x9209U, make_u16(95U));
        deletion_output.finalize();
        IdentityOptions deletion_limit;
        deletion_limit.max_added_entries = 1U;
        EXPECT_EQ(translate_xmp_identity_metadata(deleted, deletion_limit,
                                                  &deletion_output)
                      .status,
                  IdentityStatus::EntryLimitExceeded);
        ASSERT_EQ(deletion_output.entries().size(), 1U);
        ASSERT_NE(settings_find(deletion_output, 0x9209U), nullptr);
        EXPECT_EQ(settings_find(deletion_output, 0x9209U)->value.data.u64,
                  95U);
        EXPECT_EQ(translate_xmp_identity_metadata(deleted, deletion_limit,
                                                  &deleted)
                      .status,
                  IdentityStatus::EntryLimitExceeded);
        EXPECT_EQ(deleted.entries().size(), deleted_source_count);
        deletion_limit               = {};
        deletion_limit.max_operations = 1U;
        EXPECT_EQ(translate_xmp_identity_metadata(deleted, deletion_limit,
                                                  &deletion_output)
                      .status,
                  IdentityStatus::OperationLimitExceeded);
        EXPECT_EQ(translate_xmp_identity_metadata(deleted, deletion_limit,
                                                  &deleted)
                      .status,
                  IdentityStatus::OperationLimitExceeded);
        EXPECT_EQ(deleted.entries().size(), deleted_source_count);
        const auto deletion
            = translate_xmp_identity_metadata(deleted, {}, &deleted);
        ASSERT_EQ(deletion.status, IdentityStatus::Ok);
        EXPECT_EQ(deletion.entries_added, 2U);
        EXPECT_EQ(settings_find(deleted, 0xa432U), nullptr);
        EXPECT_EQ(settings_find(deleted, 0xa420U), nullptr);
        const auto lens_ids = settings_native_history_ids(deleted, 0xa432U);
        const auto id_ids   = settings_native_history_ids(deleted, 0xa420U);
        ASSERT_EQ(lens_ids.size(), 1U);
        ASSERT_EQ(id_ids.size(), 1U);
        const Entry& deleted_lens = deleted.entry(lens_ids.front());
        const Entry& deleted_id   = deleted.entry(id_ids.front());
        EXPECT_TRUE(any(deleted_lens.flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(deleted_lens.flags, EntryFlags::Deleted));
        EXPECT_EQ(deleted_lens.value.kind, MetaValueKind::Array);
        EXPECT_EQ(deleted_lens.value.elem_type, MetaElementType::URational);
        EXPECT_EQ(deleted_lens.value.count, 0U);
        EXPECT_EQ(deleted_lens.value.data.span.size, 0U);
        EXPECT_TRUE(any(deleted_id.flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(deleted_id.flags, EntryFlags::Deleted));
        EXPECT_EQ(deleted_id.value.kind, MetaValueKind::Text);
        EXPECT_EQ(deleted_id.value.text_encoding, TextEncoding::Ascii);
        EXPECT_EQ(deleted_id.value.count, 0U);
        EXPECT_EQ(deleted_id.value.data.span.size, 0U);
        const auto repeated_deletion
            = translate_xmp_identity_metadata(deleted, {}, &deleted);
        ASSERT_EQ(repeated_deletion.status, IdentityStatus::Ok);
        EXPECT_EQ(repeated_deletion.groups_unchanged, 2U);
        EXPECT_EQ(repeated_deletion.entries_added, 0U);

        MetaStore indexed_deleted;
        identity_source(indexed_deleted, true, false,
                        EntryFlags::Dirty | EntryFlags::Deleted);
        indexed_deleted.finalize();
        const auto indexed_result = translate_xmp_identity_metadata(
            indexed_deleted, {}, &indexed_deleted);
        ASSERT_EQ(indexed_result.status, IdentityStatus::Ok);
        EXPECT_EQ(indexed_result.entries_added, 2U);
        expect_native_delete_intent(indexed_deleted, 0xa432U);
        expect_native_delete_intent(indexed_deleted, 0xa420U);
        const auto indexed_repeat = translate_xmp_identity_metadata(
            indexed_deleted, {}, &indexed_deleted);
        ASSERT_EQ(indexed_repeat.status, IdentityStatus::Ok);
        EXPECT_EQ(indexed_repeat.groups_unchanged, 2U);
        EXPECT_EQ(indexed_repeat.entries_added, 0U);

        MetaStore reused;
        identity_source(reused, false, false,
                        EntryFlags::Dirty | EntryFlags::Deleted);
        std::string reused_id(kIdentityId);
        reused_id.push_back('\0');
        settings_native_entry(
            reused, 0xa432U,
            make_urational_array(reused.arena(), kIdentityLens),
            EntryFlags::Deleted, 5U, "reused-lens-delete");
        settings_native_entry(
            reused, 0xa420U,
            make_text(reused.arena(),
                      std::string_view(reused_id.data(), reused_id.size()),
                      TextEncoding::Utf8),
            EntryFlags::Deleted, 2U, "reused-id-delete");
        reused.finalize();
        const auto reuse_result
            = translate_xmp_identity_metadata(reused, {}, &reused);
        ASSERT_EQ(reuse_result.status, IdentityStatus::Ok);
        EXPECT_EQ(reuse_result.entries_added, 0U);
        EXPECT_EQ(reuse_result.entries_updated, 2U);
        expect_native_delete_intent(reused, 0xa432U);
        expect_native_delete_intent(reused, 0xa420U);
        const auto reuse_again
            = translate_xmp_identity_metadata(reused, {}, &reused);
        ASSERT_EQ(reuse_again.status, IdentityStatus::Ok);
        EXPECT_EQ(reuse_again.groups_unchanged, 2U);
        EXPECT_EQ(reuse_again.entries_added, 0U);
    }

    TEST(MetadataIdentity,
         ExactPromotionPayloadsOutliveSeparateOutputSource)
    {
        constexpr std::array<URational, 4> native_lens {
            { { 100U, 6U }, { 200U, 3U }, { 28U, 10U }, { 0U, 0U } }
        };
        std::string native_id(kIdentityId);
        native_id.push_back('\0');
        MetaStore output;
        {
            MetaStore source;
            identity_source(source);
            settings_native_entry(
                source, 0xa432U,
                make_urational_array(source.arena(), native_lens),
                EntryFlags::None, 5U, "lifetime-lens");
            settings_native_entry(
                source, 0xa420U,
                make_text(source.arena(),
                          std::string_view(native_id.data(), native_id.size()),
                          TextEncoding::Utf8),
                EntryFlags::None, 2U, "lifetime-id");
            source.finalize();
            const auto result
                = translate_xmp_identity_metadata(source, {}, &output);
            ASSERT_EQ(result.status, IdentityStatus::Ok);
            EXPECT_EQ(result.entries_updated, 2U);
        }
        const Entry* lens = settings_find(output, 0xa432U);
        const Entry* id   = settings_find(output, 0xa420U);
        ASSERT_NE(lens, nullptr);
        ASSERT_NE(id, nullptr);
        ASSERT_EQ(lens->value.count, 4U);
        const auto lens_bytes = output.arena().span(lens->value.data.span);
        ASSERT_EQ(lens_bytes.size(), sizeof(native_lens));
        std::array<URational, 4> actual_lens {};
        std::memcpy(actual_lens.data(), lens_bytes.data(), sizeof(actual_lens));
        for (size_t i = 0U; i < native_lens.size(); ++i) {
            EXPECT_EQ(actual_lens[i].numer, native_lens[i].numer) << i;
            EXPECT_EQ(actual_lens[i].denom, native_lens[i].denom) << i;
        }
        EXPECT_EQ(id->value.text_encoding, TextEncoding::Utf8);
        EXPECT_EQ(id->value.count, native_id.size());
        const auto id_bytes = output.arena().span(id->value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(id_bytes.data()),
                                   id_bytes.size()),
                  std::string_view(native_id.data(), native_id.size()));
        ASSERT_TRUE(validate_store(output).ok());
    }

    TEST(MetadataIdentity, SelectionUsesCleanLensCompanionsAndIndependentFlags)
    {
        MetaStore source;
        identity_source(source, true, false, EntryFlags::None);
        {
            Entry replacement = source.entry(2U);
            replacement.flags = EntryFlags::Dirty;
            identity_fixture_replace(source, 2U, replacement);
        }
        source.finalize();
        const auto result = translate_xmp_identity_metadata(source, {},
                                                            &source);
        ASSERT_EQ(result.status, IdentityStatus::Ok);
        EXPECT_EQ(result.source_properties, 4U);
        EXPECT_EQ(result.entries_added, 1U);
        expect_identity_lens(source);
        EXPECT_EQ(settings_find(source, 0xa420U), nullptr);
        IdentityOptions all;
        all.source_mode = MetadataCaptureTranslationSourceMode::All;
        EXPECT_EQ(
            translate_xmp_identity_metadata(source, all, &source).entries_added,
            1U);
        for (bool lens : { false, true }) {
            MetaStore selected;
            identity_source(selected);
            selected.finalize();
            IdentityOptions options;
            options.lens_specification_to_exif = lens;
            options.image_unique_id_to_exif    = !lens;
            EXPECT_EQ(translate_xmp_identity_metadata(selected, options,
                                                      &selected)
                          .entries_added,
                      1U);
            EXPECT_EQ(settings_find(selected, lens ? 0xa420U : 0xa432U),
                      nullptr);
        }
    }

    TEST(MetadataIdentity,
         OmittedCleanAndIneligibleGroupsKeepNativeOwnership)
    {
        MetaStore omitted;
        identity_source(omitted);
        constexpr std::array<URational, 4> omitted_lens {
            { { 60U, 3U }, { 200U, 3U }, { 14U, 5U }, { 0U, 0U } }
        };
        settings_native_entry(
            omitted, 0xa432U,
            make_urational_array(omitted.arena(), omitted_lens),
            EntryFlags::None, 5U, "omitted-lens");
        omitted.finalize();
        IdentityOptions omit_lens;
        omit_lens.lens_specification_to_exif = false;
        const auto omitted_result = translate_xmp_identity_metadata(
            omitted, omit_lens, &omitted);
        ASSERT_EQ(omitted_result.status, IdentityStatus::Ok);
        EXPECT_EQ(omitted_result.entries_added, 1U);
        const Entry* untouched_lens = settings_find(omitted, 0xa432U);
        ASSERT_NE(untouched_lens, nullptr);
        EXPECT_FALSE(any(untouched_lens->flags, EntryFlags::Dirty));
        std::array<URational, 4> actual_omitted_lens {};
        const auto omitted_bytes
            = omitted.arena().span(untouched_lens->value.data.span);
        ASSERT_EQ(omitted_bytes.size(), sizeof(actual_omitted_lens));
        std::memcpy(actual_omitted_lens.data(), omitted_bytes.data(),
                    sizeof(actual_omitted_lens));
        EXPECT_EQ(actual_omitted_lens[0].numer, 60U);
        EXPECT_EQ(actual_omitted_lens[0].denom, 3U);

        MetaStore ineligible;
        identity_source(ineligible, false, false, EntryFlags::None);
        settings_native_entry(
            ineligible, 0xa432U,
            make_urational_array(ineligible.arena(), kIdentityLens),
            EntryFlags::None, 5U, "ineligible-lens");
        settings_native_entry(
            ineligible, 0xa420U,
            make_text(ineligible.arena(), kIdentityId, TextEncoding::Ascii),
            EntryFlags::None, 2U, "ineligible-id");
        ineligible.finalize();
        const auto ignored
            = translate_xmp_identity_metadata(ineligible, {}, &ineligible);
        ASSERT_EQ(ignored.status, IdentityStatus::Ok);
        EXPECT_EQ(ignored.source_properties, 0U);
        EXPECT_EQ(ignored.entries_updated, 0U);
        const Entry* ineligible_lens = settings_find(ineligible, 0xa432U);
        const Entry* ineligible_id   = settings_find(ineligible, 0xa420U);
        ASSERT_NE(ineligible_lens, nullptr);
        ASSERT_NE(ineligible_id, nullptr);
        EXPECT_FALSE(any(ineligible_lens->flags, EntryFlags::Dirty));
        EXPECT_FALSE(any(ineligible_id->flags, EntryFlags::Dirty));

        MetaStore clean_tombstones;
        identity_source(clean_tombstones, true, false,
                        EntryFlags::Deleted);
        settings_native_entry(
            clean_tombstones, 0xa432U,
            make_urational_array(clean_tombstones.arena(), kIdentityLens),
            EntryFlags::None, 5U, "clean-source-lens");
        settings_native_entry(
            clean_tombstones, 0xa420U,
            make_text(clean_tombstones.arena(), kIdentityId,
                      TextEncoding::Ascii),
            EntryFlags::None, 2U, "clean-source-id");
        clean_tombstones.finalize();
        IdentityOptions all;
        all.source_mode = MetadataCaptureTranslationSourceMode::All;
        const auto clean_ignored = translate_xmp_identity_metadata(
            clean_tombstones, all, &clean_tombstones);
        ASSERT_EQ(clean_ignored.status, IdentityStatus::Ok);
        EXPECT_EQ(clean_ignored.source_properties, 0U);
        EXPECT_EQ(clean_ignored.entries_updated, 0U);
        const Entry* clean_lens = settings_find(clean_tombstones, 0xa432U);
        const Entry* clean_id   = settings_find(clean_tombstones, 0xa420U);
        ASSERT_NE(clean_lens, nullptr);
        ASSERT_NE(clean_id, nullptr);
        EXPECT_FALSE(any(clean_lens->flags, EntryFlags::Dirty));
        EXPECT_FALSE(any(clean_id->flags, EntryFlags::Dirty));
    }

    TEST(MetadataIdentity,
         RejectsMalformedIdentityAfterValidLensWithoutMutation)
    {
        for (const std::string& text :
             { std::string(), std::string(31U, 'a'), std::string(33U, 'a'),
               std::string(31U, 'a') + 'g', std::string(kIdentityId) + '\0', std::string(kIdentityId) + ' ',
               std::string(" ") + std::string(kIdentityId),
               std::string(31U, 'a') + '\0', std::string(30U, 'a') + "\xc3\xa9",
               std::string("00112233-4455-6677-8899-aabbccddeeff") }) {
            MetaStore source;
            identity_source(source);
            {
                Entry replacement = source.entry(1U);
                replacement.value = make_text(source.arena(), text,
                                              TextEncoding::Utf8);
                identity_fixture_replace(source, 1U, replacement);
            }
            identity_failure(source, IdentityStatus::InvalidSourceValue);
        }
        for (const auto encoding :
             { TextEncoding::Unknown, TextEncoding::Utf16LE }) {
            MetaStore source;
            identity_source(source);
            {
                Entry replacement               = source.entry(1U);
                replacement.value.text_encoding = encoding;
                identity_fixture_replace(source, 1U, replacement);
            }
            identity_failure(source, IdentityStatus::InvalidSourceValue);
        }
        MetaStore scalar;
        identity_source(scalar);
        {
            Entry replacement = scalar.entry(1U);
            replacement.value = make_u32(42U);
            identity_fixture_replace(scalar, 1U, replacement);
        }
        identity_failure(scalar, IdentityStatus::InvalidSourceValue);
    }

    TEST(MetadataIdentity,
         RejectsIncompleteMixedDuplicateAndStructuredLensSources)
    {
        for (const auto path :
             { "LensSpecification[0]", "LensSpecification[01]",
               "LensSpecification[5]", "LensSpecification[1]/x",
               "LensSpecification/x", "ImageUniqueID[1]" }) {
            MetaStore source;
            identity_source(source, true);
            settings_xmp(source, path, make_u32(1U));
            identity_failure(source, IdentityStatus::UnsupportedSourceShape);
        }
        for (bool mixed : { false, true }) {
            MetaStore source;
            identity_source(source, true);
            settings_xmp(source,
                         mixed ? "LensSpecification" : "LensSpecification[1]",
                         make_u32(1U), EntryFlags::Dirty, kSensitivityNs);
            identity_failure(source,
                             mixed ? IdentityStatus::UnsupportedSourceShape
                                   : IdentityStatus::AmbiguousSource);
        }
        for (bool alias : { false, true }) {
            MetaStore source;
            identity_source(source);
            settings_xmp(source, "LensSpecification",
                         make_urational_array(source.arena(), kIdentityLens),
                         EntryFlags::Dirty,
                         alias ? kSettingsNs : kSensitivityNs);
            identity_failure(source, IdentityStatus::AmbiguousSource);
        }
        MetaStore sparse;
        settings_xmp(sparse, "LensSpecification[2]", make_u32(50U));
        identity_failure(sparse, IdentityStatus::IncompleteSource);
        MetaStore split;
        identity_source(split, true);
        {
            Entry replacement = split.entry(1U);
            replacement.key = make_xmp_property_key(split.arena(), kSettingsNs,
                                                    "LensSpecification[2]");
            identity_fixture_replace(split, 1U, replacement);
        }
        identity_failure(split, IdentityStatus::AmbiguousSource);
    }

    TEST(MetadataIdentity,
         ValidatesLensShapePositiveBoundsOrderingAndExactPrecision)
    {
        for (size_t count : { 0U, 1U, 3U, 5U }) {
            MetaStore source;
            const std::array<URational, 5> values = {
                URational { 24, 1 }, { 70, 1 }, { 28, 10 }, { 4, 1 }, { 1, 1 }
            };
            settings_xmp(source, "LensSpecification",
                         make_urational_array(source.arena(),
                                              std::span(values.data(), count)));
            identity_failure(source, IdentityStatus::InvalidSourceValue);
        }
        for (size_t index = 0U; index < 4U; ++index) {
            for (const URational bad : { URational { 1U, 0U }, { 0U, 1U } }) {
                MetaStore source;
                auto values   = kIdentityLens;
                values[index] = bad;
                settings_xmp(source, "LensSpecification",
                             make_urational_array(source.arena(), values));
                identity_failure(source, IdentityStatus::InvalidNumericValue);
            }
        }
        MetaStore reversed;
        auto values = kIdentityLens;
        values[0]   = { 100U, 1U };
        settings_xmp(reversed, "LensSpecification",
                     make_urational_array(reversed.arena(), values));
        identity_failure(reversed, IdentityStatus::InvalidNumericValue);
        for (const auto text : { "1/4294967296", "4294967296", "1e-20" }) {
            MetaStore source;
            identity_source(source, true);
            {
                Entry replacement = source.entry(0U);
                replacement.value = make_text(source.arena(), text,
                                              TextEncoding::Ascii);
                identity_fixture_replace(source, 0U, replacement);
            }
            identity_failure(source, IdentityStatus::ValueOutOfRange);
        }
        MetaStore maximum;
        values = { URational { 1U, UINT32_MAX },
                   { UINT32_MAX, 1U },
                   { UINT32_MAX, UINT32_MAX },
                   { 0U, 0U } };
        settings_xmp(maximum, "LensSpecification",
                     make_urational_array(maximum.arena(), values));
        maximum.finalize();
        ASSERT_EQ(translate_xmp_identity_metadata(maximum, {}, &maximum).status,
                  IdentityStatus::Ok);
        values[2] = { 1U, 1U };
        expect_identity_lens(maximum, values);
    }

    TEST(MetadataIdentity,
         ConflictPoliciesRepairDuplicatesAndRetainTypedEquivalence)
    {
        MetaStore source;
        identity_source(source);
        auto equivalent = kIdentityLens;
        equivalent[0]   = { 100U, 6U };
        settings_native(source, 0xa432U,
                        make_urational_array(source.arena(), equivalent));
        settings_native(source, 0xa420U,
                        make_text(source.arena(),
                                  std::string(kIdentityId) + '\0',
                                  TextEncoding::Ascii));
        source.finalize();
        const auto promoted
            = translate_xmp_identity_metadata(source, {}, &source);
        EXPECT_EQ(promoted.status, IdentityStatus::Ok);
        EXPECT_EQ(promoted.entries_updated, 2U);
        expect_identity_lens(source, equivalent);
        {
            Entry replacement = source.entry(3U);
            replacement.value = make_text(source.arena(),
                                          "ffffffffffffffffffffffffffffffff",
                                          TextEncoding::Ascii);
            identity_fixture_replace(source, 3U, replacement);
        }
        settings_native(source, 0xa420U, make_u32(1U));
        source.finalize();
        const size_t conflict_count = source.entries().size();
        MetaStore separate;
        settings_native(separate, 0x9209U, make_u16(95U));
        separate.finalize();
        IdentityOptions fail_on_conflict;
        fail_on_conflict.conflict_policy = IdentityPolicy::FailOnConflict;
        const auto failed_separate
            = translate_xmp_identity_metadata(source, fail_on_conflict,
                                              &separate);
        EXPECT_EQ(failed_separate.status, IdentityStatus::NativeConflict);
        EXPECT_EQ(failed_separate.failed_mapping,
                  MetadataCaptureTranslationMapping::XmpImageUniqueID);
        ASSERT_EQ(separate.entries().size(), 1U);
        EXPECT_EQ(settings_find(separate, 0x9209U)->value.data.u64, 95U);
        const auto failed_alias
            = translate_xmp_identity_metadata(source, fail_on_conflict,
                                              &source);
        EXPECT_EQ(failed_alias.status, IdentityStatus::NativeConflict);
        EXPECT_EQ(source.entries().size(), conflict_count);
        EXPECT_EQ(settings_active_count(source, 0xa420U), 2U);
        IdentityOptions options;
        options.conflict_policy = IdentityPolicy::PreserveExisting;
        const auto preserved
            = translate_xmp_identity_metadata(source, options, &source);
        ASSERT_EQ(preserved.status, IdentityStatus::Ok);
        EXPECT_EQ(preserved.groups_preserved, 2U);
        EXPECT_EQ(source.entries().size(), conflict_count);
        EXPECT_EQ(settings_active_count(source, 0xa432U), 1U);
        EXPECT_EQ(settings_active_count(source, 0xa420U), 2U);
        options.conflict_policy = IdentityPolicy::ReplaceExisting;
        const auto replaced = translate_xmp_identity_metadata(source, options,
                                                              &source);
        EXPECT_EQ(replaced.status, IdentityStatus::Ok);
        EXPECT_EQ(replaced.entries_updated, 1U);
        EXPECT_EQ(replaced.entries_removed, 1U);
        EXPECT_EQ(camera_text_value(source, 0xa420U), kIdentityId);
        {
            Entry replacement = source.entry(2U);
            replacement.value = make_u32(42U);
            identity_fixture_replace(source, 2U, replacement);
        }
        source.finalize();
        EXPECT_EQ(translate_xmp_identity_metadata(source, {}, &source).status,
                  IdentityStatus::NativeConflict);
        ASSERT_EQ(translate_xmp_identity_metadata(source, options, &source)
                      .entries_updated,
                  1U);
        expect_identity_lens(source);
    }

    TEST(MetadataIdentity,
         LensConflictPoliciesAndDuplicateLimitsAreAtomic)
    {
        MetaStore source;
        identity_source(source);
        constexpr std::array<URational, 4> wrong_lens {
            { { 60U, 3U }, { 200U, 3U }, { 14U, 5U }, { 0U, 0U } }
        };
        settings_native_entry(
            source, 0xa432U,
            make_urational_array(source.arena(), wrong_lens),
            EntryFlags::None, 5U, "first-lens");
        settings_native_entry(
            source, 0xa432U,
            make_urational_array(source.arena(), wrong_lens),
            EntryFlags::None, 5U, "duplicate-lens");
        source.finalize();
        const size_t source_count = source.entries().size();

        IdentityOptions lens_only;
        lens_only.image_unique_id_to_exif = false;
        lens_only.conflict_policy = IdentityPolicy::FailOnConflict;
        MetaStore separate;
        settings_native(separate, 0x9209U, make_u16(95U));
        separate.finalize();
        const auto failed_separate
            = translate_xmp_identity_metadata(source, lens_only, &separate);
        EXPECT_EQ(failed_separate.status, IdentityStatus::NativeConflict);
        ASSERT_EQ(separate.entries().size(), 1U);
        EXPECT_EQ(settings_find(separate, 0x9209U)->value.data.u64, 95U);
        const auto failed_alias
            = translate_xmp_identity_metadata(source, lens_only, &source);
        EXPECT_EQ(failed_alias.status, IdentityStatus::NativeConflict);
        EXPECT_EQ(source.entries().size(), source_count);
        EXPECT_EQ(settings_active_count(source, 0xa432U), 2U);

        lens_only.conflict_policy = IdentityPolicy::PreserveExisting;
        const auto preserved
            = translate_xmp_identity_metadata(source, lens_only, &source);
        ASSERT_EQ(preserved.status, IdentityStatus::Ok);
        EXPECT_EQ(preserved.groups_preserved, 1U);
        EXPECT_EQ(preserved.entries_updated, 0U);
        EXPECT_EQ(settings_active_count(source, 0xa432U), 2U);
        for (EntryId id : settings_native_history_ids(source, 0xa432U))
            EXPECT_FALSE(any(source.entry(id).flags, EntryFlags::Dirty));

        lens_only.conflict_policy = IdentityPolicy::ReplaceExisting;
        lens_only.max_operations  = 1U;
        const auto limited_separate = translate_xmp_identity_metadata(
            source, lens_only, &separate);
        EXPECT_EQ(limited_separate.status,
                  IdentityStatus::OperationLimitExceeded);
        ASSERT_EQ(separate.entries().size(), 1U);
        ASSERT_NE(settings_find(separate, 0x9209U), nullptr);
        EXPECT_EQ(settings_find(separate, 0x9209U)->value.data.u64, 95U);
        const auto limited_alias
            = translate_xmp_identity_metadata(source, lens_only, &source);
        EXPECT_EQ(limited_alias.status,
                  IdentityStatus::OperationLimitExceeded);
        EXPECT_EQ(source.entries().size(), source_count);
        EXPECT_EQ(settings_active_count(source, 0xa432U), 2U);

        lens_only.max_operations = 2U;
        const auto repaired
            = translate_xmp_identity_metadata(source, lens_only, &source);
        ASSERT_EQ(repaired.status, IdentityStatus::Ok);
        EXPECT_EQ(repaired.entries_updated, 1U);
        EXPECT_EQ(repaired.entries_removed, 1U);
        EXPECT_EQ(settings_active_count(source, 0xa432U), 1U);
        expect_identity_lens(source);
    }

    TEST(MetadataIdentity, DeletesCompleteGroupsAndRejectsPartialMemberDeletion)
    {
        for (bool indexed : { false, true }) {
            MetaStore source;
            identity_source(source, indexed, false,
                            EntryFlags::Dirty | EntryFlags::Deleted);
            settings_native(source, 0xa432U,
                            make_urational_array(source.arena(), kIdentityLens));
            settings_native(source, 0xa420U,
                            make_text(source.arena(), kIdentityId,
                                      TextEncoding::Ascii));
            source.finalize();
            EXPECT_EQ(
                translate_xmp_identity_metadata(source, {}, &source).status,
                IdentityStatus::NativeConflict);
            IdentityOptions options;
            options.conflict_policy = IdentityPolicy::ReplaceExisting;
            const auto result = translate_xmp_identity_metadata(source, options,
                                                                &source);
            EXPECT_EQ(result.status, IdentityStatus::Ok);
            EXPECT_EQ(result.entries_removed, 2U);
            EXPECT_EQ(settings_find(source, 0xa432U), nullptr);
            EXPECT_EQ(settings_find(source, 0xa420U), nullptr);
        }
        MetaStore partial;
        identity_source(partial, true);
        {
            Entry replacement = partial.entry(2U);
            replacement.flags |= EntryFlags::Deleted;
            identity_fixture_replace(partial, 2U, replacement);
        }
        identity_failure(partial, IdentityStatus::IncompleteSource);
        MetaStore clean;
        identity_source(clean, true, false, EntryFlags::Deleted);
        clean.finalize();
        IdentityOptions all;
        all.source_mode = MetadataCaptureTranslationSourceMode::All;
        EXPECT_EQ(translate_xmp_identity_metadata(clean, all, &clean)
                      .source_properties,
                  0U);
    }

    TEST(MetadataIdentity, ResourceLimitsRejectTheWholeBatch)
    {
        MetaStore source;
        identity_source(source, true);
        IdentityOptions options;
        options.max_added_entries = 1U;
        identity_failure(source, IdentityStatus::EntryLimitExceeded, options);
        options                = {};
        options.max_operations = 1U;
        identity_failure(source, IdentityStatus::OperationLimitExceeded,
                         options);
        options                             = {};
        options.max_text_bytes_per_property = 31U;
        identity_failure(source, IdentityStatus::ValueTooLong, options);
        options                      = {};
        options.max_total_text_bytes = 32U;
        identity_failure(source, IdentityStatus::SourceLimitExceeded, options);
        options                   = {};
        options.max_added_entries = 3U;
        identity_failure(source, IdentityStatus::InvalidOptions, options);
        options                            = {};
        options.lens_specification_to_exif = false;
        options.image_unique_id_to_exif    = false;
        identity_failure(source, IdentityStatus::InvalidOptions, options);
        MetaStore unfinalized;
        EXPECT_EQ(
            translate_xmp_identity_metadata(unfinalized, {}, &source).status,
            IdentityStatus::SourceNotFinalized);
        EXPECT_EQ(translate_xmp_identity_metadata(source, {}, nullptr).status,
                  IdentityStatus::NullOutput);
    }

    TEST(MetadataIdentity,
         PortableFractionsAndUnknownsRoundTripWithManagedArrays)
    {
        for (bool legacy : { false, true }) {
            for (bool canonical : { false, true }) {
                MetaStore source;
                identity_source(source, true, legacy);
                source.finalize();
                ASSERT_EQ(
                    translate_xmp_identity_metadata(source, {}, &source).status,
                    IdentityStatus::Ok);
                XmpPortableOptions options;
                options.include_existing_xmp = canonical;
                options.conflict_policy      = XmpConflictPolicy::ExistingWins;
                if (canonical)
                    options.existing_standard_namespace_policy
                        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
                std::array<std::byte, 8192> bytes {};
                const auto dumped = dump_xmp_portable(source, bytes, options);
                ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                const std::string_view xml(reinterpret_cast<const char*>(
                                               bytes.data()),
                                           dumped.written);
                EXPECT_NE(xml.find("<exifEX:LensSpecification>"),
                          std::string_view::npos);
                EXPECT_NE(xml.find("<rdf:li>50/3</rdf:li>"),
                          std::string_view::npos);
                EXPECT_NE(xml.find("<rdf:li>0/0</rdf:li>"),
                          std::string_view::npos);
                EXPECT_EQ(xml.find("<exif:LensSpecification>"),
                          std::string_view::npos);
                MetaStore restored;
                ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(),
                                                      dumped.written),
                                            restored)
                              .status,
                          XmpDecodeStatus::Ok);
                restored.finalize();
                IdentityOptions all;
                all.source_mode = MetadataCaptureTranslationSourceMode::All;
                ASSERT_EQ(translate_xmp_identity_metadata(restored, all,
                                                          &restored)
                              .status,
                          IdentityStatus::Ok);
                expect_identity_lens(restored);
                EXPECT_EQ(camera_text_value(restored, 0xa420U), kIdentityId);
            }
        }
    }

    TEST(MetadataIdentity,
         DecodedIdentityWhitespaceIsRejectedAndMalformedNativeIsNotProjected)
    {
        for (const auto xml :
             { "<r:RDF xmlns:r='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><r:Description xmlns:e='http://ns.adobe.com/exif/1.0/' e:ImageUniqueID=' 00112233445566778899aAbBcCdDeEfF '/></r:RDF>",
               "<r:RDF xmlns:r='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><r:Description xmlns:e='http://ns.adobe.com/exif/1.0/'><e:ImageUniqueID> 00112233445566778899aAbBcCdDeEfF </e:ImageUniqueID></r:Description></r:RDF>",
               "<r:RDF xmlns:r='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><r:Description xmlns:e='http://ns.adobe.com/exif/1.0/'><e:ImageUniqueID r:resource=' 00112233445566778899aAbBcCdDeEfF '/></r:Description></r:RDF>" }) {
            MetaStore source;
            ASSERT_EQ(decode_xmp_packet(std::as_bytes(
                                            std::span(xml, std::strlen(xml))),
                                        source)
                          .status,
                      XmpDecodeStatus::Ok);
            IdentityOptions all;
            all.source_mode = MetadataCaptureTranslationSourceMode::All;
            identity_failure(source, IdentityStatus::InvalidSourceValue, all);
        }
        MetaStore source;
        settings_native(source, 0xa432U, make_u32(42U));
        settings_native(source, 0xa420U,
                        make_text(source.arena(), "invalid",
                                  TextEncoding::Ascii));
        source.finalize();
        std::array<std::byte, 8192> bytes {};
        const auto dumped = dump_xmp_portable(source, bytes, {});
        ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
        const std::string_view xml(reinterpret_cast<const char*>(bytes.data()),
                                   dumped.written);
        EXPECT_EQ(xml.find("<exifEX:LensSpecification>"),
                  std::string_view::npos);
        EXPECT_EQ(xml.find("<exif:ImageUniqueID>"), std::string_view::npos);
    }
}  // namespace
}  // namespace openmeta

namespace openmeta {
namespace {
    using ApexOptions = MetadataApexTranslationOptions;
    using ApexStatus  = MetadataCaptureTranslationStatus;
    using ApexPolicy  = MetadataCaptureTranslationConflictPolicy;
    constexpr std::array<std::string_view, 5> kApexPaths
        = { "ShutterSpeedValue", "ApertureValue", "BrightnessValue",
            "ExposureBiasValue", "MaxApertureValue" };
    constexpr std::array<uint16_t, 5> kApexTags = { 0x9201U, 0x9202U, 0x9203U,
                                                    0x9204U, 0x9205U };
    constexpr std::array<std::string_view, 5> kApexText
        = { "-7/3", "0", "-0.5", "1/3", "4294967295/2" };
    constexpr std::array<bool ApexOptions::*, 5> kApexFlags
        = { &ApexOptions::shutter_speed_value_to_exif,
            &ApexOptions::aperture_value_to_exif,
            &ApexOptions::brightness_value_to_exif,
            &ApexOptions::exposure_bias_value_to_exif,
            &ApexOptions::max_aperture_value_to_exif };

    static MetaStore apex_source(EntryFlags flags = EntryFlags::Dirty)
    {
        MetaStore store;
        for (size_t i = 0U; i < kApexPaths.size(); ++i)
            settings_xmp(store, kApexPaths[i],
                         make_text(store.arena(), kApexText[i],
                                   TextEncoding::Ascii),
                         flags);
        return store;
    }

    static void apex_expect(const MetaStore& store, size_t index,
                            int64_t numerator, uint32_t denominator)
    {
        const Entry* entry = settings_find(store, kApexTags[index]);
        ASSERT_NE(entry, nullptr);
        const MetaValue& value = entry->value;
        ASSERT_EQ(value.kind, MetaValueKind::Scalar);
        ASSERT_EQ(value.count, 1U);
        if (index == 1U || index == 4U) {
            ASSERT_EQ(value.elem_type, MetaElementType::URational);
            EXPECT_EQ(value.data.ur.numer, numerator);
            EXPECT_EQ(value.data.ur.denom, denominator);
        } else {
            ASSERT_EQ(value.elem_type, MetaElementType::SRational);
            EXPECT_EQ(value.data.sr.numer, numerator);
            EXPECT_EQ(value.data.sr.denom, denominator);
        }
    }

    static std::vector<std::byte> apex_snapshot(const MetaStore& store)
    {
        std::vector<std::byte> bytes;
        EXPECT_EQ(serialize_transfer_source_snapshot(
                      build_transfer_source_snapshot(store), &bytes)
                      .status,
                  TransferStatus::Ok);
        return bytes;
    }

    static void apex_failure(MetaStore& source, ApexStatus status,
                             const ApexOptions& options = {})
    {
        source.finalize();
        const std::vector<std::byte> before = apex_snapshot(source);
        MetaStore output;
        settings_native(output, 0x9209U, make_u16(95U));
        output.finalize();
        const std::vector<std::byte> output_before = apex_snapshot(output);
        EXPECT_EQ(translate_xmp_apex_metadata(source, options, &output).status,
                  status);
        EXPECT_EQ(apex_snapshot(output), output_before);
        EXPECT_EQ(translate_xmp_apex_metadata(source, options, &source).status,
                  status);
        EXPECT_EQ(apex_snapshot(source), before);
    }

    enum class LifecycleTranslator : uint8_t {
        Apex,
        Settings,
        Capture,
    };

    struct LifecycleOwnerCase final {
        LifecycleTranslator translator;
        std::string_view path;
        uint16_t tag;
        MetaValue source_value;
        MetaValue native_value;
        uint16_t wire_code;
    };

    static std::array<LifecycleOwnerCase, 17U> lifecycle_owner_cases()
    {
        return { { { LifecycleTranslator::Apex, "ShutterSpeedValue", 0x9201U,
                     make_srational(-7, 3), make_srational(-14, 6), 10U },
                   { LifecycleTranslator::Apex, "ApertureValue", 0x9202U,
                     make_urational(3U, 2U), make_urational(6U, 4U), 5U },
                   { LifecycleTranslator::Apex, "BrightnessValue", 0x9203U,
                     make_srational(-2, 3), make_srational(-4, 6), 10U },
                   { LifecycleTranslator::Apex, "ExposureBiasValue", 0x9204U,
                     make_srational(-1, 2), make_srational(-2, 4), 10U },
                   { LifecycleTranslator::Apex, "MaxApertureValue", 0x9205U,
                     make_urational(5U, 2U), make_urational(10U, 4U), 5U },
                   { LifecycleTranslator::Settings, kSettingsPaths[0],
                     kSettingsTags[0], make_u16(kSettingsValues[0]),
                     make_u16(kSettingsValues[0]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[1],
                     kSettingsTags[1], make_u16(kSettingsValues[1]),
                     make_u16(kSettingsValues[1]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[2],
                     kSettingsTags[2], make_u16(kSettingsValues[2]),
                     make_u16(kSettingsValues[2]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[3],
                     kSettingsTags[3], make_u16(kSettingsValues[3]),
                     make_u16(kSettingsValues[3]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[4],
                     kSettingsTags[4], make_u16(kSettingsValues[4]),
                     make_u16(kSettingsValues[4]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[5],
                     kSettingsTags[5], make_u16(kSettingsValues[5]),
                     make_u16(kSettingsValues[5]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[6],
                     kSettingsTags[6], make_u16(kSettingsValues[6]),
                     make_u16(kSettingsValues[6]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[7],
                     kSettingsTags[7], make_u16(kSettingsValues[7]),
                     make_u16(kSettingsValues[7]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[8],
                     kSettingsTags[8], make_u16(kSettingsValues[8]),
                     make_u16(kSettingsValues[8]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[9],
                     kSettingsTags[9], make_u16(kSettingsValues[9]),
                     make_u16(kSettingsValues[9]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[10],
                     kSettingsTags[10], make_u16(kSettingsValues[10]),
                     make_u16(kSettingsValues[10]), 3U },
                   { LifecycleTranslator::Settings, kSettingsPaths[11],
                     kSettingsTags[11], make_u16(kSettingsValues[11]),
                     make_u16(kSettingsValues[11]), 3U } } };
    }

    static MetadataCaptureTranslationResult lifecycle_translate(
        const LifecycleOwnerCase& owner, const MetaStore& source,
        MetaStore* output,
        MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly,
        MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict,
        uint32_t max_added_entries = 0U, uint32_t max_operations = 0U)
    {
        if (owner.translator == LifecycleTranslator::Apex) {
            ApexOptions options;
            options.source_mode     = source_mode;
            options.conflict_policy = conflict_policy;
            if (max_added_entries != 0U)
                options.max_added_entries = max_added_entries;
            if (max_operations != 0U)
                options.max_operations = max_operations;
            return translate_xmp_apex_metadata(source, options, output);
        }
        if (owner.translator == LifecycleTranslator::Settings) {
            MetadataCaptureSettingsTranslationOptions options;
            options.source_mode     = source_mode;
            options.conflict_policy = conflict_policy;
            if (max_added_entries != 0U)
                options.max_added_entries = max_added_entries;
            if (max_operations != 0U)
                options.max_operations = max_operations;
            return translate_xmp_capture_settings_metadata(source, options,
                                                           output);
        }
        MetadataCaptureTranslationOptions options;
        options.source_mode                   = source_mode;
        options.conflict_policy               = conflict_policy;
        options.exposure_time_to_exif         = false;
        options.f_number_to_exif              = false;
        options.iso_to_exif                   = false;
        options.focal_length_to_exif          = false;
        options.exposure_compensation_to_exif = true;
        if (max_added_entries != 0U)
            options.max_added_entries = max_added_entries;
        if (max_operations != 0U)
            options.max_operations = max_operations;
        return translate_xmp_capture_metadata(source, options, output);
    }

    static void lifecycle_add_xmp(MetaStore& store,
                                  const LifecycleOwnerCase& owner,
                                  EntryFlags flags = EntryFlags::Dirty)
    {
        settings_xmp(store, owner.path, owner.source_value, flags);
    }

    static void
    lifecycle_add_native(MetaStore& store, const LifecycleOwnerCase& owner,
                         MetaValue value, EntryFlags flags,
                         std::string_view wire_name = "native-lifecycle-wire")
    {
        settings_native_entry(store, owner.tag, value, flags, owner.wire_code,
                              wire_name);
    }

    static MetaValue
    lifecycle_conflict_value(const LifecycleOwnerCase& owner) noexcept
    {
        if (owner.native_value.elem_type == MetaElementType::SRational)
            return make_srational(owner.native_value.data.sr.numer + 1,
                                  owner.native_value.data.sr.denom);
        if (owner.native_value.elem_type == MetaElementType::URational)
            return make_urational(owner.native_value.data.ur.numer + 1U,
                                  owner.native_value.data.ur.denom);
        return make_u16(
            static_cast<uint16_t>(owner.native_value.data.u64 + 1U));
    }

    static void lifecycle_expect_value(const MetaStore& store,
                                       const LifecycleOwnerCase& owner,
                                       const MetaValue& expected)
    {
        const Entry* entry = settings_find(store, owner.tag);
        ASSERT_NE(entry, nullptr);
        ASSERT_EQ(entry->value.kind, MetaValueKind::Scalar);
        ASSERT_EQ(entry->value.elem_type, expected.elem_type);
        ASSERT_EQ(entry->value.count, expected.count);
        if (expected.elem_type == MetaElementType::SRational) {
            EXPECT_EQ(entry->value.data.sr.numer, expected.data.sr.numer);
            EXPECT_EQ(entry->value.data.sr.denom, expected.data.sr.denom);
        } else if (expected.elem_type == MetaElementType::URational) {
            EXPECT_EQ(entry->value.data.ur.numer, expected.data.ur.numer);
            EXPECT_EQ(entry->value.data.ur.denom, expected.data.ur.denom);
        } else {
            EXPECT_EQ(entry->value.data.u64, expected.data.u64);
        }
    }

    static MetaStore
    lifecycle_mixed_source(const std::array<LifecycleOwnerCase, 17U>& owners,
                           LifecycleTranslator translator)
    {
        MetaStore source;
        size_t field_index = 0U;
        for (const LifecycleOwnerCase& owner : owners) {
            if (owner.translator != translator)
                continue;
            const size_t state     = field_index++ % 4U;
            const EntryFlags flags = state >= 2U ? EntryFlags::Dirty
                                                       | EntryFlags::Deleted
                                                 : EntryFlags::Dirty;
            lifecycle_add_xmp(source, owner, flags);
            if (state == 1U || state == 3U)
                lifecycle_add_native(source, owner,
                                     lifecycle_conflict_value(owner),
                                     EntryFlags::None, "native-mismatch");
        }
        return source;
    }

    static MetaStore
    lifecycle_uniform_source(const std::array<LifecycleOwnerCase, 17U>& owners,
                             LifecycleTranslator translator, bool deleted,
                             bool with_native,
                             EntryFlags native_flags = EntryFlags::None)
    {
        MetaStore source;
        for (const LifecycleOwnerCase& owner : owners) {
            if (owner.translator != translator)
                continue;
            lifecycle_add_xmp(source, owner,
                              deleted ? EntryFlags::Dirty | EntryFlags::Deleted
                                      : EntryFlags::Dirty);
            if (with_native)
                lifecycle_add_native(source, owner, owner.native_value,
                                     native_flags);
        }
        return source;
    }

    static void lifecycle_budget_failure(
        LifecycleTranslator translator, MetaStore& source,
        MetadataCaptureTranslationStatus expected,
        MetadataCaptureTranslationConflictPolicy conflict_policy,
        uint32_t max_added_entries, uint32_t max_operations)
    {
        if (translator == LifecycleTranslator::Apex) {
            ApexOptions options;
            options.conflict_policy   = conflict_policy;
            options.max_added_entries = max_added_entries;
            options.max_operations    = max_operations;
            apex_failure(source, expected, options);
            return;
        }
        MetadataCaptureSettingsTranslationOptions options;
        options.conflict_policy   = conflict_policy;
        options.max_added_entries = max_added_entries;
        options.max_operations    = max_operations;
        settings_failure(source, expected, options);
    }

    using SpatialOptions = MetadataCaptureSpatialTranslationOptions;
    using SpatialStatus  = MetadataCaptureTranslationStatus;
    using SpatialPolicy  = MetadataCaptureTranslationConflictPolicy;
    constexpr std::array<uint16_t, 5> kSpatialTags
        = { 0xa20eU, 0xa20fU, 0xa210U, 0x9214U, 0xa214U };
    constexpr std::array<std::string_view, 5> kSpatialNames
        = { "FocalPlaneXResolution", "FocalPlaneYResolution",
            "FocalPlaneResolutionUnit", "SubjectArea", "SubjectLocation" };
    constexpr std::array<uint16_t, 4> kSubjectArea = { 0U, 65535U, 12U, 34U };
    constexpr std::array<uint16_t, 2> kSubjectLocation = { 123U, 456U };

    static MetaStore spatial_source(bool indexed      = false,
                                    size_t area_count = 4U,
                                    EntryFlags flags  = EntryFlags::Dirty)
    {
        MetaStore source;
        settings_xmp(source, kSpatialNames[0], make_urational(10000U, 3U),
                     flags);
        settings_xmp(source, kSpatialNames[1],
                     make_text(source.arena(), "2.5e3", TextEncoding::Ascii),
                     flags);
        settings_xmp(source, kSpatialNames[2],
                     make_text(source.arena(), "cm", TextEncoding::Ascii),
                     flags);
        for (size_t i = 3U; i < 5U; ++i) {
            const auto values = i == 3U
                                    ? std::span(kSubjectArea.data(), area_count)
                                    : std::span(kSubjectLocation);
            if (!indexed)
                settings_xmp(source, kSpatialNames[i],
                             make_u16_array(source.arena(), values), flags);
            else
                for (size_t j = 0U; j < values.size(); ++j)
                    settings_xmp(source,
                                 std::string(kSpatialNames[i]) + "["
                                     + std::to_string(j + 1U) + "]",
                                 make_text(source.arena(),
                                           std::to_string(values[j]),
                                           TextEncoding::Ascii),
                                 flags);
        }
        return source;
    }

    static void spatial_expect(const MetaStore& store, size_t area_count = 4U)
    {
        for (uint16_t tag : kSpatialTags)
            ASSERT_NE(settings_find(store, tag), nullptr);
        const auto& x = settings_find(store, kSpatialTags[0])->value;
        EXPECT_EQ(x.elem_type, MetaElementType::URational);
        EXPECT_EQ(x.data.ur.numer, 10000U);
        EXPECT_EQ(x.data.ur.denom, 3U);
        EXPECT_EQ(settings_find(store, kSpatialTags[1])->value.data.ur.numer,
                  2500U);
        EXPECT_EQ(settings_find(store, kSpatialTags[1])->value.data.ur.denom,
                  1U);
        EXPECT_EQ(settings_find(store, kSpatialTags[2])->value.elem_type,
                  MetaElementType::U16);
        EXPECT_EQ(settings_find(store, kSpatialTags[2])->value.data.u64, 3U);
        for (size_t i = 3U; i < 5U; ++i) {
            const auto& value   = settings_find(store, kSpatialTags[i])->value;
            const auto expected = i == 3U ? std::span(kSubjectArea.data(),
                                                      area_count)
                                          : std::span(kSubjectLocation);
            EXPECT_EQ(value.kind, MetaValueKind::Array);
            EXPECT_EQ(value.elem_type, MetaElementType::U16);
            ASSERT_EQ(value.count, expected.size());
            const auto bytes = store.arena().span(value.data.span);
            ASSERT_EQ(bytes.size(), expected.size_bytes());
            EXPECT_EQ(std::memcmp(bytes.data(), expected.data(), bytes.size()),
                      0);
        }
    }

    static void spatial_failure(MetaStore& source, SpatialStatus status,
                                const SpatialOptions& options = {})
    {
        source.finalize();
        const auto before = apex_snapshot(source);
        MetaStore output;
        settings_native(output, 0x9209U, make_u16(95U));
        output.finalize();
        const auto output_before = apex_snapshot(output);
        EXPECT_EQ(translate_xmp_capture_spatial_metadata(source, options,
                                                         &output)
                      .status,
                  status);
        EXPECT_EQ(apex_snapshot(output), output_before);
        EXPECT_EQ(translate_xmp_capture_spatial_metadata(source, options,
                                                         &source)
                      .status,
                  status);
        EXPECT_EQ(apex_snapshot(source), before);
    }

    TEST(MetadataCaptureSpatial,
         CompleteTypedAndIndexedShapesOwnValuesAndProvenance)
    {
        for (bool indexed : { false, true }) {
            for (size_t count = 2U; count <= 4U; ++count) {
                MetaStore output;
                {
                    MetaStore source = spatial_source(indexed, count);
                    settings_native(source, 0x9209U, make_u16(95U));
                    source.finalize();
                    const auto result
                        = translate_xmp_capture_spatial_metadata(source, {},
                                                                 &output);
                    ASSERT_EQ(result.status, SpatialStatus::Ok);
                    EXPECT_EQ(result.entries_added, 5U);
                    EXPECT_EQ(result.groups_translated, 3U);
                    EXPECT_EQ(result.source_properties,
                              indexed ? 5U + count : 5U);
                }
                spatial_expect(output, count);
                EXPECT_EQ(settings_find(output, 0x9209U)->value.data.u64, 95U);
                for (uint16_t tag : kSpatialTags) {
                    const Entry& entry = *settings_find(output, tag);
                    EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty));
                    const auto bytes = output.arena().span(
                        entry.origin.wire_type_name);
                    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                                   bytes.data()),
                                               bytes.size()),
                              "settings-source");
                }
                const auto again
                    = translate_xmp_capture_spatial_metadata(output, {},
                                                             &output);
                EXPECT_EQ(again.status, SpatialStatus::Ok);
                EXPECT_EQ(again.groups_unchanged, 3U);
                EXPECT_EQ(again.entries_added, 0U);
            }
        }
    }

    TEST(MetadataCaptureSpatial,
         DirtyMembersSelectCleanCompanionsAndIndependentGroups)
    {
        constexpr std::array<EntryId, 3> selected = { 1U, 4U, 8U };
        for (size_t group = 0U; group < 3U; ++group) {
            MetaStore source = spatial_source(true, 4U, EntryFlags::None);
            source.finalize();
            MetaStore output;
            EXPECT_EQ(translate_xmp_capture_spatial_metadata(source, {}, &output)
                          .entries_added,
                      0U);
            Entry entry = source.entry(selected[group]);
            entry.flags = EntryFlags::Dirty;
            identity_fixture_replace(source, selected[group], entry);
            source.finalize();
            const auto result
                = translate_xmp_capture_spatial_metadata(source, {}, &output);
            ASSERT_EQ(result.status, SpatialStatus::Ok);
            EXPECT_EQ(result.groups_translated, 1U);
            EXPECT_EQ(result.entries_added, group == 0U ? 3U : 1U);
            SpatialOptions options;
            options.source_mode = MetadataCaptureTranslationSourceMode::All;
            options.focal_plane_to_exif      = group == 0U;
            options.subject_area_to_exif     = group == 1U;
            options.subject_location_to_exif = group == 2U;
            EXPECT_EQ(translate_xmp_capture_spatial_metadata(source, options,
                                                             &output)
                          .entries_added,
                      group == 0U ? 3U : 1U);
        }
    }

    TEST(MetadataCaptureSpatial,
         LifecyclePromotesExactCleanNativesAndCreatesWholeGroupMarkers)
    {
        MetaStore source = spatial_source(false, 4U, EntryFlags::None);
        for (EntryId id : { 0U, 3U }) {
            Entry entry = source.entry(id);
            entry.flags = EntryFlags::Dirty;
            identity_fixture_replace(source, id, entry);
        }
        settings_native_entry(source, 0xa20eU, make_urational(20000U, 6U),
                              EntryFlags::None, 3U, "spatial-native");
        settings_native_entry(source, 0xa20fU, make_urational(5000U, 2U),
                              EntryFlags::None, 3U, "spatial-native");
        settings_native_entry(source, 0x9214U,
                              make_u16_array(source.arena(), kSubjectArea),
                              EntryFlags::ContextualName, 7U, "spatial-native");
        settings_native_entry(source, 0xa214U,
                              make_u16_array(source.arena(), kSubjectLocation),
                              EntryFlags::None, 7U, "spatial-native");
        source.finalize();
        ASSERT_GT(settings_find(source, 0x9214U)->value.data.span.offset, 0U);

        SpatialOptions options;
        options.conflict_policy   = SpatialPolicy::ReplaceExisting;
        MetaStore separate_source = source;
        MetaStore separate_output;
        const auto separated
            = translate_xmp_capture_spatial_metadata(separate_source, options,
                                                     &separate_output);
        ASSERT_EQ(separated.status, SpatialStatus::Ok);
        EXPECT_EQ(separated.groups_translated, 2U);
        EXPECT_EQ(separated.entries_added, 1U);
        EXPECT_EQ(separated.entries_updated, 3U);
        const Entry* separate_area = settings_find(separate_output, 0x9214U);
        ASSERT_NE(separate_area, nullptr);
        EXPECT_EQ(separate_area->value.kind, MetaValueKind::Array);
        EXPECT_EQ(separate_area->value.elem_type, MetaElementType::U16);
        EXPECT_EQ(separate_area->value.count, kSubjectArea.size());
        const auto separate_area_bytes = separate_output.arena().span(
            separate_area->value.data.span);
        ASSERT_EQ(separate_area_bytes.size(), sizeof(kSubjectArea));
        EXPECT_EQ(std::memcmp(separate_area_bytes.data(), kSubjectArea.data(),
                              separate_area_bytes.size()),
                  0);

        const auto promoted
            = translate_xmp_capture_spatial_metadata(source, options, &source);
        ASSERT_EQ(promoted.status, SpatialStatus::Ok);
        EXPECT_EQ(promoted.groups_translated, 2U);
        EXPECT_EQ(promoted.entries_added, 1U);
        EXPECT_EQ(promoted.entries_updated, 3U);
        for (uint16_t tag : { 0xa20eU, 0xa20fU, 0x9214U }) {
            const Entry* entry = settings_find(source, tag);
            ASSERT_NE(entry, nullptr);
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
            EXPECT_FALSE(any(entry->flags, EntryFlags::Deleted));
            EXPECT_EQ(entry->origin.wire_type.family, WireFamily::Tiff);
            EXPECT_EQ(entry->origin.wire_type.code, tag == 0x9214U ? 7U : 3U);
            EXPECT_EQ(entry->origin.wire_count,
                      tag == 0x9214U ? kSubjectArea.size() : 1U);
            EXPECT_EQ(entry->origin.order_in_block, 17U);
            const auto name = source.arena().span(entry->origin.wire_type_name);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                           name.data()),
                                       name.size()),
                      "spatial-native");
            if (tag == 0x9214U)
                EXPECT_TRUE(any(entry->flags, EntryFlags::ContextualName));
        }
        EXPECT_EQ(settings_find(source, 0xa20eU)->value.data.ur.numer, 20000U);
        EXPECT_EQ(settings_find(source, 0xa20eU)->value.data.ur.denom, 6U);
        EXPECT_EQ(settings_find(source, 0xa20fU)->value.data.ur.numer, 5000U);
        EXPECT_EQ(settings_find(source, 0xa20fU)->value.data.ur.denom, 2U);
        EXPECT_EQ(settings_find(source, 0xa210U)->value.data.u64, 3U);
        const auto promoted_area = source.arena().span(
            settings_find(source, 0x9214U)->value.data.span);
        EXPECT_EQ(settings_find(source, 0x9214U)->value.count,
                  kSubjectArea.size());
        EXPECT_EQ(promoted_area.size(), sizeof(kSubjectArea));
        EXPECT_EQ(std::memcmp(promoted_area.data(), kSubjectArea.data(),
                              promoted_area.size()),
                  0);
        const Entry* preserved_location = settings_find(source, 0xa214U);
        ASSERT_NE(preserved_location, nullptr);
        EXPECT_FALSE(any(preserved_location->flags, EntryFlags::Dirty));

        MetaStore deleted = spatial_source();
        for (EntryId id = 0U; id < deleted.entries().size(); ++id) {
            Entry entry = deleted.entry(id);
            entry.flags = EntryFlags::Dirty | EntryFlags::Deleted;
            identity_fixture_replace(deleted, id, entry);
        }
        deleted.finalize();
        const auto removed = translate_xmp_capture_spatial_metadata(deleted,
                                                                    options,
                                                                    &deleted);
        ASSERT_EQ(removed.status, SpatialStatus::Ok);
        EXPECT_EQ(removed.groups_translated, 3U);
        EXPECT_EQ(removed.entries_added, 5U);
        EXPECT_EQ(deleted.entries().size(), 10U);
        std::array<bool, 5U> markers {};
        for (const Entry& entry : deleted.entries()) {
            if (entry.key.kind != MetaKeyKind::ExifTag)
                continue;
            for (size_t i = 0U; i < kSpatialTags.size(); ++i) {
                if (entry.key.data.exif_tag.tag != kSpatialTags[i])
                    continue;
                EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty));
                EXPECT_TRUE(any(entry.flags, EntryFlags::Deleted));
                markers[i] = true;
            }
        }
        for (bool marker : markers)
            EXPECT_TRUE(marker);
        const size_t entries_after_delete = deleted.entries().size();
        const auto repeated = translate_xmp_capture_spatial_metadata(deleted,
                                                                     options,
                                                                     &deleted);
        ASSERT_EQ(repeated.status, SpatialStatus::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 3U);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(deleted.entries().size(), entries_after_delete);
    }

    TEST(MetadataCaptureSpatial,
         LifecyclePoliciesPreserveOwnersAndPromoteExactArrays)
    {
        MetaStore exact = spatial_source(false, 4U, EntryFlags::None);
        for (EntryId id : { 3U, 4U }) {
            Entry entry = exact.entry(id);
            entry.flags = EntryFlags::Dirty;
            identity_fixture_replace(exact, id, entry);
        }
        settings_native_entry(exact, 0x9214U,
                              make_u16_array(exact.arena(), kSubjectArea),
                              EntryFlags::ContextualName, 7U, "spatial-native");
        settings_native_entry(exact, 0xa214U,
                              make_u16_array(exact.arena(), kSubjectLocation),
                              EntryFlags::ContextualName, 7U, "spatial-native");
        exact.finalize();
        MetaStore preserved = exact;

        SpatialOptions options;
        options.focal_plane_to_exif = false;
        options.conflict_policy     = SpatialPolicy::FailOnConflict;
        const auto promoted
            = translate_xmp_capture_spatial_metadata(exact, options, &exact);
        ASSERT_EQ(promoted.status, SpatialStatus::Ok);
        EXPECT_EQ(promoted.groups_translated, 2U);
        EXPECT_EQ(promoted.entries_updated, 2U);
        EXPECT_EQ(promoted.entries_added, 0U);
        for (size_t i = 3U; i < kSpatialTags.size(); ++i) {
            const Entry* entry = settings_find(exact, kSpatialTags[i]);
            ASSERT_NE(entry, nullptr);
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
            EXPECT_TRUE(any(entry->flags, EntryFlags::ContextualName));
            const std::span<const uint16_t> expected
                = i == 3U ? std::span<const uint16_t>(kSubjectArea)
                          : std::span<const uint16_t>(kSubjectLocation);
            EXPECT_EQ(entry->value.kind, MetaValueKind::Array);
            EXPECT_EQ(entry->value.elem_type, MetaElementType::U16);
            EXPECT_EQ(entry->value.count, expected.size());
            const auto bytes = exact.arena().span(entry->value.data.span);
            ASSERT_EQ(bytes.size(), expected.size_bytes());
            EXPECT_EQ(std::memcmp(bytes.data(), expected.data(), bytes.size()),
                      0);
        }

        options.conflict_policy = SpatialPolicy::PreserveExisting;
        const auto kept = translate_xmp_capture_spatial_metadata(preserved,
                                                                 options,
                                                                 &preserved);
        ASSERT_EQ(kept.status, SpatialStatus::Ok);
        EXPECT_EQ(kept.groups_preserved, 2U);
        EXPECT_EQ(kept.groups_translated, 0U);
        EXPECT_EQ(kept.entries_updated, 0U);
        EXPECT_EQ(kept.entries_added, 0U);
        for (size_t i = 3U; i < kSpatialTags.size(); ++i) {
            const Entry* entry = settings_find(preserved, kSpatialTags[i]);
            ASSERT_NE(entry, nullptr);
            EXPECT_FALSE(any(entry->flags, EntryFlags::Dirty));
            EXPECT_TRUE(any(entry->flags, EntryFlags::ContextualName));
        }

        MetaStore exact_focal = spatial_source(false, 4U);
        settings_native_entry(exact_focal, 0xa20eU, make_urational(20000U, 6U),
                              EntryFlags::ContextualName, 3U, "spatial-native");
        settings_native_entry(exact_focal, 0xa20fU, make_urational(5000U, 2U),
                              EntryFlags::None, 3U, "spatial-native");
        settings_native_entry(exact_focal, 0xa210U, make_u16(3U),
                              EntryFlags::None, 4U, "spatial-native");
        exact_focal.finalize();
        options.conflict_policy          = SpatialPolicy::FailOnConflict;
        options.focal_plane_to_exif      = true;
        options.subject_area_to_exif     = false;
        options.subject_location_to_exif = false;
        const auto exact_focal_result
            = translate_xmp_capture_spatial_metadata(exact_focal, options,
                                                     &exact_focal);
        ASSERT_EQ(exact_focal_result.status, SpatialStatus::Ok);
        EXPECT_EQ(exact_focal_result.groups_translated, 1U);
        EXPECT_EQ(exact_focal_result.entries_updated, 3U);
        EXPECT_EQ(exact_focal_result.entries_added, 0U);
        EXPECT_EQ(settings_find(exact_focal, 0xa20eU)->value.data.ur.numer,
                  20000U);
        EXPECT_EQ(settings_find(exact_focal, 0xa20eU)->value.data.ur.denom, 6U);
        EXPECT_EQ(settings_find(exact_focal, 0xa20fU)->value.data.ur.numer,
                  5000U);
        EXPECT_EQ(settings_find(exact_focal, 0xa20fU)->value.data.ur.denom, 2U);
        for (uint16_t tag : { 0xa20eU, 0xa20fU, 0xa210U })
            EXPECT_TRUE(
                any(settings_find(exact_focal, tag)->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(settings_find(exact_focal, 0xa20eU)->flags,
                        EntryFlags::ContextualName));
        EXPECT_EQ(settings_find(exact_focal, 0xa20eU)->origin.wire_type.code,
                  3U);

        options.conflict_policy = SpatialPolicy::PreserveExisting;
        MetaStore partial_owner = spatial_source(false, 4U);
        settings_native_entry(partial_owner, 0xa20eU,
                              make_urational(20000U, 6U), EntryFlags::None);
        settings_native_entry(partial_owner, 0xa20fU, make_urational(5000U, 2U),
                              EntryFlags::None);
        settings_native_entry(partial_owner, 0xa210U, make_u16(2U),
                              EntryFlags::None);
        partial_owner.finalize();
        options.focal_plane_to_exif      = true;
        options.subject_area_to_exif     = false;
        options.subject_location_to_exif = false;
        const auto preserved_owner
            = translate_xmp_capture_spatial_metadata(partial_owner, options,
                                                     &partial_owner);
        ASSERT_EQ(preserved_owner.status, SpatialStatus::Ok);
        EXPECT_EQ(preserved_owner.groups_preserved, 1U);
        EXPECT_EQ(preserved_owner.groups_translated, 0U);
        EXPECT_EQ(preserved_owner.entries_updated, 0U);
        EXPECT_EQ(preserved_owner.entries_added, 0U);
        EXPECT_FALSE(any(settings_find(partial_owner, 0xa20eU)->flags,
                         EntryFlags::Dirty));
        EXPECT_FALSE(any(settings_find(partial_owner, 0xa20fU)->flags,
                         EntryFlags::Dirty));
        EXPECT_FALSE(any(settings_find(partial_owner, 0xa210U)->flags,
                         EntryFlags::Dirty));
    }

    TEST(MetadataCaptureSpatial,
         LifecycleFailOnConflictRejectsPartialFocalOwner)
    {
        MetaStore source = spatial_source(false, 4U);
        settings_native_entry(source, 0xa20eU, make_urational(20000U, 6U),
                              EntryFlags::None);
        settings_native_entry(source, 0xa20fU, make_urational(5000U, 2U),
                              EntryFlags::None);
        settings_native_entry(source, 0xa210U, make_u16(2U), EntryFlags::None);
        SpatialOptions options;
        options.conflict_policy          = SpatialPolicy::FailOnConflict;
        options.subject_area_to_exif     = false;
        options.subject_location_to_exif = false;
        spatial_failure(source, SpatialStatus::NativeConflict, options);
    }

    TEST(MetadataCaptureSpatial,
         LifecycleReusesCleanDeleteMarkersForRootAndDenseSources)
    {
        for (bool indexed : { false, true }) {
            MetaStore source           = spatial_source(indexed);
            const EntryId source_count = indexed ? 9U : 5U;
            for (EntryId id = 0U; id < source_count; ++id) {
                Entry entry = source.entry(id);
                entry.flags = EntryFlags::Dirty | EntryFlags::Deleted;
                identity_fixture_replace(source, id, entry);
            }
            settings_native_entry(source, 0xa20eU, make_urational(0U, 1U),
                                  EntryFlags::Deleted);
            settings_native_entry(source, 0xa20fU, make_urational(0U, 1U),
                                  EntryFlags::Deleted);
            settings_native_entry(source, 0xa210U, make_u16(0U),
                                  EntryFlags::Deleted);
            settings_native_entry(source, 0x9214U,
                                  make_u16_array(source.arena(), kSubjectArea),
                                  EntryFlags::Deleted);
            settings_native_entry(source, 0xa214U,
                                  make_u16_array(source.arena(),
                                                 kSubjectLocation),
                                  EntryFlags::Deleted);
            source.finalize();

            SpatialOptions options;
            options.conflict_policy     = SpatialPolicy::ReplaceExisting;
            const size_t entries_before = source.entries().size();
            const auto result = translate_xmp_capture_spatial_metadata(source,
                                                                       options,
                                                                       &source);
            ASSERT_EQ(result.status, SpatialStatus::Ok);
            EXPECT_EQ(result.groups_translated, 3U);
            EXPECT_EQ(result.entries_updated, 5U);
            EXPECT_EQ(result.entries_added, 0U);
            EXPECT_EQ(result.entries_removed, 0U);
            EXPECT_EQ(source.entries().size(), entries_before);
            for (uint16_t tag : kSpatialTags)
                EXPECT_EQ(settings_find(source, tag), nullptr);

            std::array<bool, 5U> dirty_markers {};
            for (const Entry& entry : source.entries()) {
                if (entry.key.kind != MetaKeyKind::ExifTag)
                    continue;
                for (size_t i = 0U; i < kSpatialTags.size(); ++i) {
                    if (entry.key.data.exif_tag.tag != kSpatialTags[i])
                        continue;
                    EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty));
                    EXPECT_TRUE(any(entry.flags, EntryFlags::Deleted));
                    dirty_markers[i] = true;
                }
            }
            for (bool marker : dirty_markers)
                EXPECT_TRUE(marker);

            const auto repeated
                = translate_xmp_capture_spatial_metadata(source, options,
                                                         &source);
            ASSERT_EQ(repeated.status, SpatialStatus::Ok);
            EXPECT_EQ(repeated.groups_unchanged, 3U);
            EXPECT_EQ(repeated.entries_updated, 0U);
            EXPECT_EQ(repeated.entries_added, 0U);
            EXPECT_EQ(source.entries().size(), entries_before);
        }
    }

    TEST(MetadataCaptureSpatial, LifecycleIgnoresDisabledAndIneligibleOwners)
    {
        for (unsigned variant = 0U; variant < 2U; ++variant) {
            MetaStore source = spatial_source(false, 4U, EntryFlags::None);
            if (variant == 0U) {
                Entry location = source.entry(4U);
                location.flags = EntryFlags::Dirty;
                identity_fixture_replace(source, 4U, location);
            }
            settings_native_entry(source, 0xa214U,
                                  make_u16_array(source.arena(),
                                                 kSubjectLocation),
                                  EntryFlags::ContextualName, 7U,
                                  "spatial-native");
            source.finalize();

            SpatialOptions options;
            if (variant == 0U)
                options.subject_location_to_exif = false;
            const auto before = apex_snapshot(source);
            MetaStore output;
            const auto separate
                = translate_xmp_capture_spatial_metadata(source, options,
                                                         &output);
            ASSERT_EQ(separate.status, SpatialStatus::Ok);
            EXPECT_EQ(separate.groups_translated, 0U);
            EXPECT_EQ(separate.entries_updated, 0U);
            EXPECT_EQ(separate.entries_added, 0U);
            EXPECT_EQ(apex_snapshot(source), before);
            EXPECT_EQ(apex_snapshot(output), before);

            const auto alias = translate_xmp_capture_spatial_metadata(source,
                                                                      options,
                                                                      &source);
            ASSERT_EQ(alias.status, SpatialStatus::Ok);
            EXPECT_EQ(alias.groups_translated, 0U);
            EXPECT_EQ(alias.entries_updated, 0U);
            EXPECT_EQ(alias.entries_added, 0U);
            EXPECT_EQ(apex_snapshot(source), before);
            EXPECT_FALSE(
                any(settings_find(source, 0xa214U)->flags, EntryFlags::Dirty));
        }
    }

    TEST(MetadataCaptureSpatial,
         LifecycleOperationAndMarkerBudgetsRollBackBothOutputModes)
    {
        MetaStore promotions = spatial_source(false, 4U);
        settings_native_entry(promotions, 0xa20eU, make_urational(20000U, 6U),
                              EntryFlags::None);
        settings_native_entry(promotions, 0xa20fU, make_urational(5000U, 2U),
                              EntryFlags::None);
        settings_native_entry(promotions, 0xa210U, make_u16(3U),
                              EntryFlags::None);
        SpatialOptions promotion_limit;
        promotion_limit.conflict_policy          = SpatialPolicy::FailOnConflict;
        promotion_limit.subject_area_to_exif     = false;
        promotion_limit.subject_location_to_exif = false;
        promotion_limit.max_operations           = 2U;
        spatial_failure(promotions, SpatialStatus::OperationLimitExceeded,
                        promotion_limit);

        MetaStore deleted = spatial_source(true);
        for (EntryId id = 0U; id < deleted.entries().size(); ++id) {
            Entry entry = deleted.entry(id);
            entry.flags = EntryFlags::Dirty | EntryFlags::Deleted;
            identity_fixture_replace(deleted, id, entry);
        }
        MetaStore addition_limited = deleted;
        SpatialOptions addition_limit;
        addition_limit.conflict_policy   = SpatialPolicy::ReplaceExisting;
        addition_limit.max_added_entries = 4U;
        spatial_failure(addition_limited, SpatialStatus::EntryLimitExceeded,
                        addition_limit);
        SpatialOptions operation_limit = addition_limit;
        operation_limit.max_added_entries
            = kMetadataCaptureSpatialTranslationMaxAddedEntries;
        operation_limit.max_operations = 4U;
        spatial_failure(deleted, SpatialStatus::OperationLimitExceeded,
                        operation_limit);

        MetaStore duplicates = spatial_source(false, 4U);
        settings_native_entry(duplicates, 0xa210U, make_u16(3U),
                              EntryFlags::None);
        settings_native_entry(duplicates, 0xa210U, make_u16(3U),
                              EntryFlags::None);
        SpatialOptions duplicate_limit;
        duplicate_limit.conflict_policy          = SpatialPolicy::ReplaceExisting;
        duplicate_limit.subject_area_to_exif     = false;
        duplicate_limit.subject_location_to_exif = false;
        duplicate_limit.max_operations           = 3U;
        spatial_failure(duplicates, SpatialStatus::OperationLimitExceeded,
                        duplicate_limit);
    }

    TEST(MetadataCaptureSpatial,
         RejectsIncompleteAmbiguousAndUnsupportedShapesTransactionally)
    {
        constexpr std::array<std::string_view, 9> paths
            = { "SubjectArea[0]",           "SubjectArea[01]",
                "SubjectArea[5]",           "SubjectArea[1]/x",
                "SubjectLocation[3]",       "SubjectLocation?x",
                "FocalPlaneXResolution[1]", "FocalPlaneResolutionUnit/x",
                "FocalPlaneYResolution?x" };
        for (auto path : paths) {
            MetaStore source = spatial_source();
            settings_xmp(source, path, make_u16(1U));
            spatial_failure(source, SpatialStatus::UnsupportedSourceShape);
        }
        for (unsigned variant = 0U; variant < 5U; ++variant) {
            MetaStore source;
            if (variant == 0U)
                settings_xmp(source, kSpatialNames[0], make_u16(1U));
            else if (variant == 1U) {
                settings_xmp(source, "SubjectArea[1]", make_u16(1U));
                settings_xmp(source, "SubjectArea[3]", make_u16(3U));
            } else if (variant == 2U) {
                source = spatial_source(true);
                settings_xmp(source, "SubjectArea[2]", make_u16(1U),
                             EntryFlags::None);
            } else if (variant == 3U) {
                source = spatial_source();
                settings_xmp(source, "SubjectLocation[1]", make_u16(1U));
            } else {
                source = spatial_source();
                settings_xmp(source, kSpatialNames[2], make_u16(2U));
            }
            spatial_failure(source, variant <= 1U
                                        ? SpatialStatus::IncompleteSource
                                    : variant == 3U
                                        ? SpatialStatus::UnsupportedSourceShape
                                        : SpatialStatus::AmbiguousSource);
        }
    }

    TEST(MetadataCaptureSpatial, ExactNumericBoundsAndStandardUnits)
    {
        for (auto unit : { "2", "+3", "inches", "cm" }) {
            MetaStore source = spatial_source();
            Entry entry      = source.entry(2U);
            entry.value = make_text(source.arena(), unit, TextEncoding::Ascii);
            identity_fixture_replace(source, 2U, entry);
            entry       = source.entry(0U);
            entry.value = make_text(source.arena(), "8589934590/2",
                                    TextEncoding::Ascii);
            identity_fixture_replace(source, 0U, entry);
            source.finalize();
            ASSERT_EQ(translate_xmp_capture_spatial_metadata(source, {}, &source)
                          .status,
                      SpatialStatus::Ok);
            EXPECT_EQ(settings_find(source, 0xa20eU)->value.data.ur.numer,
                      UINT32_MAX);
            EXPECT_EQ(settings_find(source, 0xa210U)->value.data.u64,
                      std::string_view(unit) == "2"
                              || std::string_view(unit) == "inches"
                          ? 2U
                          : 3U);
        }
        struct Case {
            EntryId id;
            std::string_view text;
            SpatialStatus status;
        };
        constexpr Case cases[]
            = { { 0U, "0", SpatialStatus::ValueOutOfRange },
                { 0U, "1/0", SpatialStatus::InvalidNumericValue },
                { 1U, "4294967296", SpatialStatus::ValueOutOfRange },
                { 1U, "1/4294967296", SpatialStatus::ValueOutOfRange },
                { 2U, "1", SpatialStatus::ValueOutOfRange },
                { 2U, "4", SpatialStatus::ValueOutOfRange },
                { 2U, "5", SpatialStatus::ValueOutOfRange },
                { 2U, "mm", SpatialStatus::InvalidNumericValue },
                { 3U, "65536", SpatialStatus::ValueOutOfRange },
                { 4U, "1.5", SpatialStatus::InvalidNumericValue },
                { 7U, "1/2", SpatialStatus::InvalidNumericValue } };
        for (const auto& item : cases) {
            MetaStore source = spatial_source(true);
            Entry entry      = source.entry(item.id);
            entry.value      = make_text(source.arena(), item.text,
                                         TextEncoding::Ascii);
            identity_fixture_replace(source, item.id, entry);
            spatial_failure(source, item.status);
        }
    }

    TEST(MetadataCaptureSpatial, RejectsWrongTypesCountsAndSpans)
    {
        for (unsigned variant = 0U; variant < 6U; ++variant) {
            MetaStore source = spatial_source();
            const EntryId id = variant < 2U ? variant : 3U;
            Entry entry      = source.entry(id);
            if (variant == 0U)
                entry.value = make_srational(1, 2);
            else if (variant == 1U)
                entry.value = make_f64_bits(0x3ff0000000000000ULL);
            else if (variant == 2U)
                entry.value = make_u16(1U);
            else if (variant == 3U) {
                const std::array<uint32_t, 2> values = { 1U, 2U };
                entry.value = make_u32_array(source.arena(), values);
            } else if (variant == 4U)
                entry.value = make_u16_array(source.arena(),
                                             std::span(kSubjectArea.data(), 1U));
            else
                entry.value.data.span.size -= 1U;
            identity_fixture_replace(source, id, entry);
            source.finalize();
            // Malformed spans cannot be serialized for snapshot comparison.
            const auto count = source.entries().size();
            EXPECT_EQ(translate_xmp_capture_spatial_metadata(source, {}, &source)
                          .status,
                      SpatialStatus::InvalidSourceValue);
            EXPECT_EQ(source.entries().size(), count);
            EXPECT_EQ(settings_find(source, 0xa20eU), nullptr);
        }
    }

    TEST(MetadataCaptureSpatial,
         CompleteGroupConflictsPreserveFailReplaceAndCollapseDuplicates)
    {
        for (SpatialPolicy policy :
             { SpatialPolicy::PreserveExisting, SpatialPolicy::FailOnConflict,
               SpatialPolicy::ReplaceExisting }) {
            MetaStore source = spatial_source();
            settings_native(source, 0xa210U, make_u16(2U));
            settings_native(source, 0xa210U, make_u16(2U));
            settings_native(source, 0x9214U,
                            make_u16_array(source.arena(), kSubjectLocation));
            SpatialOptions options;
            options.conflict_policy = policy;
            if (policy == SpatialPolicy::FailOnConflict) {
                spatial_failure(source, SpatialStatus::NativeConflict, options);
                continue;
            }
            source.finalize();
            const auto result = translate_xmp_capture_spatial_metadata(source,
                                                                       options,
                                                                       &source);
            ASSERT_EQ(result.status, SpatialStatus::Ok);
            if (policy == SpatialPolicy::PreserveExisting) {
                EXPECT_EQ(result.groups_preserved, 2U);
                EXPECT_EQ(result.groups_translated, 1U);
                EXPECT_EQ(settings_find(source, 0xa20eU), nullptr);
                EXPECT_EQ(settings_active_count(source, 0xa210U), 2U);
            } else {
                EXPECT_EQ(result.groups_translated, 3U);
                EXPECT_EQ(result.entries_added, 3U);
                EXPECT_EQ(result.entries_updated, 2U);
                EXPECT_EQ(result.entries_removed, 1U);
                EXPECT_EQ(settings_active_count(source, 0xa210U), 1U);
                spatial_expect(source);
            }
        }
    }

    TEST(MetadataCaptureSpatial,
         CompleteTombstonesRemoveAndPartialDeletionRollsBack)
    {
        for (bool indexed : { false, true }) {
            MetaStore source = spatial_source(indexed);
            source.finalize();
            ASSERT_EQ(translate_xmp_capture_spatial_metadata(source, {}, &source)
                          .status,
                      SpatialStatus::Ok);
            const size_t sources = indexed ? 9U : 5U;
            for (EntryId id = 0U; id < sources; ++id) {
                Entry entry = source.entry(id);
                entry.flags = EntryFlags::Dirty | EntryFlags::Deleted;
                identity_fixture_replace(source, id, entry);
            }
            source.finalize();
            SpatialOptions options;
            options.conflict_policy = SpatialPolicy::ReplaceExisting;
            const auto result = translate_xmp_capture_spatial_metadata(source,
                                                                       options,
                                                                       &source);
            ASSERT_EQ(result.status, SpatialStatus::Ok);
            EXPECT_EQ(result.groups_translated, 3U);
            EXPECT_EQ(result.entries_removed, 5U);
            for (uint16_t tag : kSpatialTags)
                EXPECT_EQ(settings_find(source, tag), nullptr);
        }
        for (EntryId id : { 0U, 2U, 4U, 6U, 8U }) {
            MetaStore source = spatial_source(true);
            Entry entry      = source.entry(id);
            entry.flags      = EntryFlags::Dirty | EntryFlags::Deleted;
            identity_fixture_replace(source, id, entry);
            spatial_failure(source, SpatialStatus::IncompleteSource);
        }
    }

    TEST(MetadataCaptureSpatial, SharedBoundsAndInvalidOptionsAreTransactional)
    {
        for (unsigned variant = 0U; variant < 12U; ++variant) {
            MetaStore source = spatial_source(true);
            SpatialOptions options;
            SpatialStatus expected = SpatialStatus::InvalidOptions;
            switch (variant) {
            case 0U:
                options.max_added_entries = 4U;
                expected                  = SpatialStatus::EntryLimitExceeded;
                break;
            case 1U:
                options.max_operations = 4U;
                expected               = SpatialStatus::OperationLimitExceeded;
                break;
            case 2U:
                options.max_text_bytes_per_property = 1U;
                expected = SpatialStatus::ValueTooLong;
                break;
            case 3U:
                options.max_total_text_bytes = 5U;
                expected = SpatialStatus::SourceLimitExceeded;
                break;
            case 4U: options.max_added_entries = 6U; break;
            case 5U: options.max_operations = 1025U; break;
            case 6U: options.max_text_bytes_per_property = 129U; break;
            case 7U: options.max_total_text_bytes = 1153U; break;
            case 8U: options.max_operations = 0U; break;
            case 9U:
                options.source_mode
                    = static_cast<MetadataCaptureTranslationSourceMode>(255U);
                break;
            case 10U:
                options.conflict_policy = static_cast<SpatialPolicy>(255U);
                break;
            default:
                options.focal_plane_to_exif = options.subject_area_to_exif
                    = options.subject_location_to_exif = false;
                break;
            }
            spatial_failure(source, expected, options);
        }
        MetaStore source = spatial_source();
        EXPECT_EQ(
            translate_xmp_capture_spatial_metadata(source, {}, nullptr).status,
            SpatialStatus::NullOutput);
        EXPECT_EQ(
            translate_xmp_capture_spatial_metadata(source, {}, &source).status,
            SpatialStatus::SourceNotFinalized);
    }

    TEST(MetadataCaptureSpatial,
         PortableRoundTripPreservesExactFractionsAndOrderedShapes)
    {
        for (bool indexed : { false, true }) {
            for (size_t count = 2U; count <= 4U; ++count) {
                for (unsigned mode = 0U; mode < 3U; ++mode) {
                    MetaStore source = spatial_source(indexed, count);
                    source.finalize();
                    if (mode != 0U)
                        ASSERT_EQ(translate_xmp_capture_spatial_metadata(
                                      source, {}, &source)
                                      .status,
                                  SpatialStatus::Ok);
                    XmpPortableOptions portable;
                    portable.include_existing_xmp = mode != 1U;
                    portable.existing_standard_namespace_policy
                        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
                    std::array<std::byte, 8192> bytes {};
                    const auto dumped = dump_xmp_portable(source, bytes,
                                                          portable);
                    ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                    const std::string_view xml(reinterpret_cast<const char*>(
                                                   bytes.data()),
                                               dumped.written);
                    EXPECT_NE(
                        xml.find(
                            "<exif:FocalPlaneXResolution>10000/3</exif:FocalPlaneXResolution>"),
                        std::string_view::npos);
                    MetaStore restored;
                    ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(),
                                                          dumped.written),
                                                restored)
                                  .status,
                              XmpDecodeStatus::Ok);
                    restored.finalize();
                    SpatialOptions all;
                    all.source_mode = MetadataCaptureTranslationSourceMode::All;
                    ASSERT_EQ(translate_xmp_capture_spatial_metadata(restored,
                                                                     all,
                                                                     &restored)
                                  .status,
                              SpatialStatus::Ok);
                    spatial_expect(restored, count);
                }
            }
        }
    }

    TEST(MetadataCaptureSpatial,
         ManagedArraysReplaceStaleShapesAndExistingWinsRetainsThem)
    {
        for (bool indexed : { false, true }) {
            MetaStore source = spatial_source(indexed);
            source.finalize();
            ASSERT_EQ(translate_xmp_capture_spatial_metadata(source, {}, &source)
                          .status,
                      SpatialStatus::Ok);
            for (uint16_t tag : { 0x9214U, 0xa214U }) {
                const auto ids = source.find_all(
                    make_exif_tag_key_view("exififd", tag));
                ASSERT_EQ(ids.size(), 1U);
                Entry entry                            = source.entry(ids[0]);
                constexpr std::array<uint16_t, 2> zero = { 0U, 0U };
                entry.value = make_u16_array(source.arena(), zero);
                identity_fixture_replace(source, ids[0], entry);
                source.finalize();
            }
            for (bool canonical : { false, true }) {
                XmpPortableOptions options;
                options.include_existing_xmp = true;
                options.conflict_policy = XmpConflictPolicy::ExistingWins;
                if (canonical)
                    options.existing_standard_namespace_policy
                        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
                std::array<std::byte, 8192> bytes {};
                const auto dumped = dump_xmp_portable(source, bytes, options);
                ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                MetaStore restored;
                ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(),
                                                      dumped.written),
                                            restored)
                              .status,
                          XmpDecodeStatus::Ok);
                restored.finalize();
                SpatialOptions all;
                all.source_mode = MetadataCaptureTranslationSourceMode::All;
                ASSERT_EQ(translate_xmp_capture_spatial_metadata(restored, all,
                                                                 &restored)
                              .status,
                          SpatialStatus::Ok);
                if (!canonical)
                    spatial_expect(restored);
                else {
                    for (uint16_t tag : { 0x9214U, 0xa214U }) {
                        const auto& value = settings_find(restored, tag)->value;
                        EXPECT_EQ(value.count, 2U);
                        for (std::byte byte :
                             restored.arena().span(value.data.span))
                            EXPECT_EQ(byte, std::byte { 0U });
                    }
                }
            }
        }
    }

    TEST(MetadataCaptureSpatial,
         InvalidNativeShapesDoNotSuppressValidManagedXmp)
    {
        for (unsigned variant = 0U; variant < 3U; ++variant) {
            MetaStore source = spatial_source(true);
            for (size_t i = 0U; i < kSpatialTags.size(); ++i) {
                MetaValue value = make_u32(999U);
                if (variant == 1U)
                    value = i < 2U ? make_urational(1U, 0U) : make_u16(1U);
                else if (variant == 2U) {
                    value = i < 2U ? make_urational(10000U, 3U) : make_u16(1U);
                    value.count = 2U;
                }
                // Unit 1 is still preserved by the existing portable reader.
                if (variant == 1U && i == 2U)
                    value = make_u32(1U);
                settings_native(source, kSpatialTags[i], value);
            }
            source.finalize();
            XmpPortableOptions options;
            options.include_existing_xmp = true;
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
            SpatialOptions all;
            all.source_mode = MetadataCaptureTranslationSourceMode::All;
            ASSERT_EQ(translate_xmp_capture_spatial_metadata(restored, all,
                                                             &restored)
                          .status,
                      SpatialStatus::Ok);
            spatial_expect(restored);
        }
    }

    TEST(MetadataApex, FiveFieldsCommitExactValuesAndRetainUnrelatedCapture)
    {
        MetaStore output;
        {
            MetaStore source = apex_source();
            settings_native(source, 0x829aU, make_urational(1U, 125U));
            settings_native(source, 0x829dU, make_urational(28U, 10U));
            source.finalize();
            const auto result = translate_xmp_apex_metadata(source, {},
                                                            &output);
            ASSERT_EQ(result.status, ApexStatus::Ok);
            EXPECT_EQ(result.entries_added, 5U);
            EXPECT_EQ(result.groups_translated, 5U);
            EXPECT_EQ(result.source_properties, 5U);
            EXPECT_EQ(source.entries().size(), 7U);
        }
        apex_expect(output, 0U, -7, 3U);
        apex_expect(output, 1U, 0, 1U);
        apex_expect(output, 2U, -2, 4U);
        apex_expect(output, 3U, 1, 3U);
        apex_expect(output, 4U, UINT32_MAX, 2U);
        EXPECT_EQ(settings_find(output, 0x829aU)->value.data.ur.denom, 125U);
        EXPECT_EQ(settings_find(output, 0x829dU)->value.data.ur.numer, 28U);
        EXPECT_EQ(
            translate_xmp_apex_metadata(output, {}, &output).groups_unchanged,
            5U);
        for (uint16_t tag : kApexTags)
            EXPECT_TRUE(
                any(settings_find(output, tag)->flags, EntryFlags::Dirty));
    }

    TEST(MetadataApex, DirtySelectionIndependentFlagsAndBiasAlias)
    {
        for (size_t selected = 0U; selected < kApexPaths.size(); ++selected) {
            MetaStore source = apex_source(EntryFlags::None);
            source.finalize();
            MetaStore output;
            EXPECT_EQ(
                translate_xmp_apex_metadata(source, {}, &output).entries_added,
                0U);
            ApexOptions options;
            options.source_mode = MetadataCaptureTranslationSourceMode::All;
            for (size_t i = 0U; i < kApexFlags.size(); ++i)
                options.*kApexFlags[i] = i == selected;
            ASSERT_EQ(translate_xmp_apex_metadata(source, options, &output)
                          .entries_added,
                      1U);
            for (size_t i = 0U; i < kApexTags.size(); ++i)
                EXPECT_EQ(settings_find(output, kApexTags[i]) != nullptr,
                          i == selected);
        }
        MetaStore source;
        settings_xmp(source, "ExposureCompensation", make_srational(-2, 6));
        source.finalize();
        ASSERT_EQ(translate_xmp_apex_metadata(source, {}, &source).status,
                  ApexStatus::Ok);
        apex_expect(source, 3U, -1, 3U);
        EXPECT_EQ(translate_xmp_capture_metadata(source, {}, &source)
                      .groups_unchanged,
                  1U);
    }

    TEST(MetadataGpsLifecycle,
         ExactNativeGroupPromotesAndDeletedGroupCreatesMemberIntents)
    {
        MetaStore exact;
        settings_xmp(exact, "GPSLatitude",
                     make_text(exact.arena(), "35,48.125N", TextEncoding::Utf8));
        gps_lifecycle_native(exact, 1U,
                             make_text(exact.arena(),
                                       std::string_view("N\0", 2U),
                                       TextEncoding::Ascii),
                             EntryFlags::None, 2U, "gps-clean-reference-wire");
        const std::array<URational, 3> exact_coordinate {
            URational { 35U, 1U }, URational { 48U, 1U }, URational { 30U, 4U }
        };
        gps_lifecycle_native(exact, 2U,
                             make_urational_array(exact.arena(),
                                                  exact_coordinate),
                             EntryFlags::None, 5U, "gps-clean-coordinate-wire");
        const std::array<uint8_t, 4> gps_version { 2U, 3U, 0U, 0U };
        gps_lifecycle_native(exact, 0U,
                             make_u8_array(exact.arena(), gps_version),
                             EntryFlags::None, 1U, "gps-clean-version-wire");
        exact.finalize();

        MetadataGpsTranslationOptions exact_options;
        exact_options.conflict_policy
            = MetadataGpsTranslationConflictPolicy::ReplaceExisting;
        const MetadataGpsTranslationResult promoted
            = translate_xmp_gps_metadata(exact, exact_options, &exact);
        ASSERT_EQ(promoted.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(promoted.groups_translated, 1U);
        EXPECT_EQ(promoted.entries_updated, 2U);
        EXPECT_EQ(promoted.entries_added, 0U);
        const Entry* reference  = gps_lifecycle_find(exact, 1U);
        const Entry* coordinate = gps_lifecycle_find(exact, 2U);
        ASSERT_NE(reference, nullptr);
        ASSERT_NE(coordinate, nullptr);
        EXPECT_TRUE(any(reference->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(coordinate->flags, EntryFlags::Dirty));
        EXPECT_EQ(gps_lifecycle_text(exact, reference->value.data.span),
                  std::string_view("N\0", 2U));
        EXPECT_EQ(reference->origin.wire_type.family, WireFamily::Tiff);
        EXPECT_EQ(reference->origin.wire_type.code, 2U);
        EXPECT_EQ(gps_lifecycle_text(exact, reference->origin.wire_type_name),
                  "gps-clean-reference-wire");
        EXPECT_EQ(coordinate->origin.wire_type.family, WireFamily::Tiff);
        EXPECT_EQ(coordinate->origin.wire_type.code, 5U);
        EXPECT_EQ(gps_lifecycle_text(exact, coordinate->origin.wire_type_name),
                  "gps-clean-coordinate-wire");
        const Entry* version = gps_lifecycle_find(exact, 0U);
        ASSERT_NE(version, nullptr);
        EXPECT_FALSE(any(version->flags, EntryFlags::Dirty));
        EXPECT_EQ(gps_lifecycle_text(exact, version->origin.wire_type_name),
                  "gps-clean-version-wire");
        const std::span<const std::byte> coordinate_bytes = exact.arena().span(
            coordinate->value.data.span);
        ASSERT_EQ(coordinate_bytes.size(), sizeof(exact_coordinate));
        EXPECT_EQ(std::memcmp(coordinate_bytes.data(), exact_coordinate.data(),
                              sizeof(exact_coordinate)),
                  0);
        const MetadataGpsTranslationResult promoted_repeat
            = translate_xmp_gps_metadata(exact, exact_options, &exact);
        ASSERT_EQ(promoted_repeat.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(promoted_repeat.groups_unchanged, 1U);
        EXPECT_EQ(promoted_repeat.entries_updated, 0U);

        MetaStore deleted;
        const EntryFlags deleted_flags = EntryFlags::Dirty
                                         | EntryFlags::Deleted;
        add_xmp_text(&deleted, 0U, kSettingsNs, "GPSAltitude", "",
                     deleted_flags, 3U, "gps-altitude-source-wire");
        add_xmp_text(&deleted, 0U, kSettingsNs, "GPSAltitudeRef", "",
                     deleted_flags, 4U, "gps-reference-source-wire");
        deleted.finalize();
        const MetadataGpsTranslationResult synthesized
            = translate_xmp_gps_metadata(deleted, {}, &deleted);
        ASSERT_EQ(synthesized.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(synthesized.groups_translated, 1U);
        EXPECT_EQ(synthesized.entries_added, 2U);
        for (uint16_t tag : { 5U, 6U }) {
            bool found = false;
            for (const Entry& entry : deleted.entries()) {
                if (entry.key.kind != MetaKeyKind::ExifTag
                    || entry.key.data.exif_tag.tag != tag) {
                    continue;
                }
                const std::span<const std::byte> ifd = deleted.arena().span(
                    entry.key.data.exif_tag.ifd);
                const std::string_view ifd_name {
                    reinterpret_cast<const char*>(ifd.data()), ifd.size()
                };
                if (ifd_name != "gpsifd") {
                    continue;
                }
                ASSERT_TRUE(any(entry.flags, EntryFlags::Deleted));
                ASSERT_TRUE(any(entry.flags, EntryFlags::Dirty));
                const std::string_view expected_wire_name
                    = tag == 5U ? "gps-reference-source-wire"
                                : "gps-altitude-source-wire";
                EXPECT_EQ(gps_lifecycle_text(deleted,
                                             entry.origin.wire_type_name),
                          expected_wire_name);
                found = true;
            }
            EXPECT_TRUE(found) << tag;
        }
        const size_t deleted_size = deleted.entries().size();
        const MetadataGpsTranslationResult synthesized_repeat
            = translate_xmp_gps_metadata(deleted, {}, &deleted);
        ASSERT_EQ(synthesized_repeat.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(synthesized_repeat.groups_unchanged, 1U);
        EXPECT_EQ(synthesized_repeat.entries_added, 0U);
        EXPECT_EQ(deleted.entries().size(), deleted_size);
    }

    TEST(MetadataGpsLifecycle,
         DeletedGroupsCoverAllValueTagsAndReuseCleanMemberHistory)
    {
        for (const GpsLifecycleGroupCase& group : kGpsLifecycleGroups) {
            SCOPED_TRACE(group.source_paths[0]);
            MetaStore missing = gps_lifecycle_deleted_source(group);
            missing.finalize();
            const MetadataGpsTranslationResult synthesized
                = gps_lifecycle_translate(group.api, missing, &missing);
            ASSERT_EQ(synthesized.status, MetadataGpsTranslationStatus::Ok);
            EXPECT_EQ(synthesized.source_properties, group.source_count);
            EXPECT_EQ(synthesized.groups_translated, 1U);
            EXPECT_EQ(synthesized.entries_added, group.native_count);
            for (size_t i = 0U; i < group.native_count; ++i) {
                const Entry* intent
                    = gps_lifecycle_find_any(missing, group.native_tags[i]);
                ASSERT_NE(intent, nullptr) << group.native_tags[i];
                EXPECT_TRUE(any(intent->flags, EntryFlags::Dirty));
                EXPECT_TRUE(any(intent->flags, EntryFlags::Deleted));
                EXPECT_TRUE(
                    gps_lifecycle_has_delete_intent(missing,
                                                    group.native_tags[i]));
                const size_t source_index = group.native_source_indices[i];
                const std::string_view expected_source_wire
                    = source_index == 0U ? "gps-lifecycle-source-a"
                                         : "gps-lifecycle-source-b";
                EXPECT_EQ(gps_lifecycle_text(missing,
                                             intent->origin.wire_type_name),
                          expected_source_wire);
            }
            const size_t missing_size = missing.entries().size();
            const MetadataGpsTranslationResult missing_repeat
                = gps_lifecycle_translate(group.api, missing, &missing);
            ASSERT_EQ(missing_repeat.status, MetadataGpsTranslationStatus::Ok);
            EXPECT_EQ(missing_repeat.groups_unchanged, 1U);
            EXPECT_EQ(missing_repeat.entries_added, 0U);
            EXPECT_EQ(missing.entries().size(), missing_size);

            MetaStore clean_history = gps_lifecycle_deleted_source(group);
            gps_lifecycle_native(clean_history, group.native_tags[0],
                                 make_u8(0U), EntryFlags::Deleted, 7U,
                                 "gps-clean-delete-history-wire");
            clean_history.finalize();
            const MetadataGpsTranslationResult reused
                = gps_lifecycle_translate(group.api, clean_history,
                                          &clean_history);
            ASSERT_EQ(reused.status, MetadataGpsTranslationStatus::Ok);
            EXPECT_EQ(reused.groups_translated, 1U);
            EXPECT_EQ(reused.entries_updated, 1U);
            EXPECT_EQ(reused.entries_added, group.native_count - 1U);
            const Entry* reused_marker
                = gps_lifecycle_find_any(clean_history, group.native_tags[0]);
            ASSERT_NE(reused_marker, nullptr);
            EXPECT_TRUE(any(reused_marker->flags, EntryFlags::Dirty));
            EXPECT_TRUE(any(reused_marker->flags, EntryFlags::Deleted));
            EXPECT_EQ(gps_lifecycle_text(clean_history,
                                         reused_marker->origin.wire_type_name),
                      "gps-clean-delete-history-wire");
            for (size_t i = 1U; i < group.native_count; ++i) {
                const Entry* intent
                    = gps_lifecycle_find_any(clean_history,
                                             group.native_tags[i]);
                ASSERT_NE(intent, nullptr) << group.native_tags[i];
                EXPECT_TRUE(any(intent->flags, EntryFlags::Dirty));
                EXPECT_TRUE(any(intent->flags, EntryFlags::Deleted));
                EXPECT_TRUE(
                    gps_lifecycle_has_delete_intent(clean_history,
                                                    group.native_tags[i]));
            }
            const size_t history_size = clean_history.entries().size();
            const MetadataGpsTranslationResult history_repeat
                = gps_lifecycle_translate(group.api, clean_history,
                                          &clean_history);
            ASSERT_EQ(history_repeat.status, MetadataGpsTranslationStatus::Ok);
            EXPECT_EQ(history_repeat.groups_unchanged, 1U);
            EXPECT_EQ(history_repeat.entries_added, 0U);
            EXPECT_EQ(history_repeat.entries_updated, 0U);
            EXPECT_EQ(clean_history.entries().size(), history_size);
        }
    }

    TEST(MetadataGpsLifecycle, VersionCleanupRetainsOtherLiveGpsValues)
    {
        const std::array<uint8_t, 4> version { 2U, 3U, 0U, 0U };
        MetaStore last_group = gps_lifecycle_deleted_source(
            kGpsLifecycleGroups[0]);
        gps_lifecycle_native(last_group, 0U,
                             make_u8_array(last_group.arena(), version),
                             EntryFlags::None, 1U, "gps-clean-version-wire");
        last_group.finalize();
        const MetadataGpsTranslationResult cleaned
            = gps_lifecycle_translate(GpsLifecycleApi::Primary, last_group,
                                      &last_group);
        ASSERT_EQ(cleaned.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(cleaned.entries_added, 2U);
        EXPECT_EQ(cleaned.entries_removed, 1U);
        EXPECT_EQ(gps_lifecycle_find(last_group, 0U), nullptr);
        EXPECT_TRUE(gps_lifecycle_has_delete_intent(last_group, 1U));
        EXPECT_TRUE(gps_lifecycle_has_delete_intent(last_group, 2U));

        MetaStore unrelated_live = gps_lifecycle_deleted_source(
            kGpsLifecycleGroups[0]);
        gps_lifecycle_native(unrelated_live, 0U,
                             make_u8_array(unrelated_live.arena(), version),
                             EntryFlags::None, 1U, "gps-retained-version-wire");
        gps_lifecycle_native(unrelated_live, 18U,
                             make_text(unrelated_live.arena(), "WGS-84",
                                       TextEncoding::Ascii),
                             EntryFlags::None, 2U, "gps-unselected-datum-wire");
        unrelated_live.finalize();
        const MetadataGpsTranslationResult retained
            = gps_lifecycle_translate(GpsLifecycleApi::Primary, unrelated_live,
                                      &unrelated_live);
        ASSERT_EQ(retained.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(retained.entries_removed, 0U);
        const Entry* retained_version = gps_lifecycle_find(unrelated_live, 0U);
        ASSERT_NE(retained_version, nullptr);
        EXPECT_FALSE(any(retained_version->flags, EntryFlags::Dirty));
        EXPECT_NE(gps_lifecycle_find(unrelated_live, 18U), nullptr);
    }

    TEST(MetadataGpsLifecycle,
         ExactCleanValuesPromoteAcrossApisAndPreserveExistingSkipsGroups)
    {
        const std::array<GpsLifecycleApi, 5U> apis {
            GpsLifecycleApi::Primary, GpsLifecycleApi::Navigation,
            GpsLifecycleApi::Destination, GpsLifecycleApi::Quality,
            GpsLifecycleApi::Text
        };
        const std::array<size_t, 5U> group_indices { 0U, 4U, 9U, 13U, 18U };
        const std::array<std::array<std::string_view, 2U>, 5U> wire_names { {
            { "gps-exact-reference-wire", "gps-exact-coordinate-wire" },
            { "gps-exact-speed-ref-wire", "gps-exact-speed-wire" },
            { "gps-exact-bearing-ref-wire", "gps-exact-bearing-wire" },
            { "gps-exact-dop-wire", {} },
            { "gps-exact-method-wire", {} },
        } };
        const std::array<std::array<uint16_t, 2U>, 5U> wire_codes {
            { { 2U, 5U }, { 2U, 5U }, { 2U, 5U }, { 5U, 0U }, { 7U, 0U } }
        };
        for (size_t i = 0U; i < apis.size(); ++i) {
            const GpsLifecycleGroupCase& group
                = kGpsLifecycleGroups[group_indices[i]];
            SCOPED_TRACE(group.source_paths[0]);
            MetaStore exact = gps_lifecycle_exact_fixture(apis[i]);
            const MetadataGpsTranslationResult promoted
                = gps_lifecycle_translate(
                    apis[i], exact, &exact,
                    MetadataGpsTranslationSourceMode::DirtyOnly,
                    MetadataGpsTranslationConflictPolicy::FailOnConflict);
            ASSERT_EQ(promoted.status, MetadataGpsTranslationStatus::Ok);
            EXPECT_EQ(promoted.groups_translated, 1U);
            EXPECT_EQ(promoted.entries_updated, group.native_count);
            EXPECT_EQ(promoted.entries_added, 0U);
            gps_lifecycle_check_group_flags(exact, group, true);
            for (size_t member = 0U; member < group.native_count; ++member) {
                const Entry* entry
                    = gps_lifecycle_find(exact, group.native_tags[member]);
                ASSERT_NE(entry, nullptr);
                EXPECT_EQ(entry->origin.wire_type.family, WireFamily::Tiff);
                EXPECT_EQ(entry->origin.wire_type.code, wire_codes[i][member]);
                EXPECT_EQ(entry->origin.order_in_block, 19U);
                EXPECT_EQ(gps_lifecycle_text(exact,
                                             entry->origin.wire_type_name),
                          wire_names[i][member]);
            }
            if (apis[i] == GpsLifecycleApi::Navigation) {
                const Entry* speed = gps_lifecycle_find(exact, 13U);
                ASSERT_NE(speed, nullptr);
                EXPECT_EQ(speed->value.data.ur.numer, 6U);
                EXPECT_EQ(speed->value.data.ur.denom, 4U);
            } else if (apis[i] == GpsLifecycleApi::Destination) {
                const Entry* bearing = gps_lifecycle_find(exact, 24U);
                ASSERT_NE(bearing, nullptr);
                EXPECT_EQ(bearing->value.data.ur.numer, 3U);
                EXPECT_EQ(bearing->value.data.ur.denom, 2U);
            } else if (apis[i] == GpsLifecycleApi::Quality) {
                const Entry* dop = gps_lifecycle_find(exact, 11U);
                ASSERT_NE(dop, nullptr);
                EXPECT_EQ(dop->value.data.ur.numer, 10U);
                EXPECT_EQ(dop->value.data.ur.denom, 8U);
            }
            const MetadataGpsTranslationResult repeat = gps_lifecycle_translate(
                apis[i], exact, &exact,
                MetadataGpsTranslationSourceMode::DirtyOnly,
                MetadataGpsTranslationConflictPolicy::FailOnConflict);
            ASSERT_EQ(repeat.status, MetadataGpsTranslationStatus::Ok);
            EXPECT_EQ(repeat.groups_unchanged, 1U);
            EXPECT_EQ(repeat.entries_updated, 0U);

            MetaStore preserved = gps_lifecycle_exact_fixture(apis[i]);
            const MetadataGpsTranslationResult keep = gps_lifecycle_translate(
                apis[i], preserved, &preserved,
                MetadataGpsTranslationSourceMode::DirtyOnly,
                MetadataGpsTranslationConflictPolicy::PreserveExisting);
            ASSERT_EQ(keep.status, MetadataGpsTranslationStatus::Ok);
            EXPECT_EQ(keep.groups_preserved, 1U);
            EXPECT_EQ(keep.entries_updated, 0U);
            EXPECT_EQ(keep.entries_added, 0U);
            gps_lifecycle_check_group_flags(preserved, group, false);
        }

        MetaStore partial;
        settings_xmp(partial, "GPSLatitude",
                     make_text(partial.arena(), "35,48.125N",
                               TextEncoding::Utf8));
        gps_lifecycle_native(
            partial, 1U, make_text(partial.arena(), "N", TextEncoding::Ascii),
            EntryFlags::None, 2U, "gps-partial-reference-wire");
        partial.finalize();
        const MetadataGpsTranslationResult partial_kept
            = gps_lifecycle_translate(
                GpsLifecycleApi::Primary, partial, &partial,
                MetadataGpsTranslationSourceMode::DirtyOnly,
                MetadataGpsTranslationConflictPolicy::PreserveExisting);
        ASSERT_EQ(partial_kept.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(partial_kept.groups_preserved, 1U);
        EXPECT_EQ(partial_kept.entries_updated, 0U);
        EXPECT_EQ(partial_kept.entries_added, 0U);
        const Entry* partial_reference = gps_lifecycle_find(partial, 1U);
        ASSERT_NE(partial_reference, nullptr);
        EXPECT_FALSE(any(partial_reference->flags, EntryFlags::Dirty));
        EXPECT_EQ(gps_lifecycle_find(partial, 2U), nullptr);
        EXPECT_EQ(gps_lifecycle_find(partial, 0U), nullptr);
    }

    TEST(MetadataGpsLifecycle, DirtyOnlyAllAndNamespaceSelectionAcrossApis)
    {
        const std::array<GpsLifecycleApi, 5U> apis {
            GpsLifecycleApi::Primary, GpsLifecycleApi::Navigation,
            GpsLifecycleApi::Destination, GpsLifecycleApi::Quality,
            GpsLifecycleApi::Text
        };
        for (const GpsLifecycleApi api : apis) {
            MetaStore clean;
            gps_lifecycle_add_selection_fixture(clean, api, EntryFlags::None);
            clean.finalize();
            const MetadataGpsTranslationResult omitted
                = gps_lifecycle_translate(api, clean, &clean);
            ASSERT_EQ(omitted.status, MetadataGpsTranslationStatus::Ok);
            EXPECT_EQ(omitted.source_properties, 0U);
            EXPECT_EQ(omitted.entries_added, 0U);
            EXPECT_EQ(omitted.groups_translated, 0U);

            const MetadataGpsTranslationResult all = gps_lifecycle_translate(
                api, clean, &clean, MetadataGpsTranslationSourceMode::All);
            ASSERT_EQ(all.status, MetadataGpsTranslationStatus::Ok);
            EXPECT_EQ(all.groups_translated, 1U);
            EXPECT_GT(all.entries_added, 0U);

            MetaStore foreign;
            gps_lifecycle_add_selection_fixture(
                foreign, api, EntryFlags::Dirty,
                "http://example.test/foreign-gps/");
            foreign.finalize();
            const MetadataGpsTranslationResult wrong_namespace
                = gps_lifecycle_translate(api, foreign, &foreign);
            ASSERT_EQ(wrong_namespace.status, MetadataGpsTranslationStatus::Ok);
            EXPECT_EQ(wrong_namespace.source_properties, 0U);
            EXPECT_EQ(wrong_namespace.entries_added, 0U);
            EXPECT_EQ(wrong_namespace.groups_translated, 0U);
        }

        MetaStore clean_delete
            = gps_lifecycle_deleted_source(kGpsLifecycleGroups[0],
                                           EntryFlags::Deleted);
        clean_delete.finalize();
        const MetadataGpsTranslationResult clean_tombstone
            = gps_lifecycle_translate(GpsLifecycleApi::Primary, clean_delete,
                                      &clean_delete,
                                      MetadataGpsTranslationSourceMode::All);
        ASSERT_EQ(clean_tombstone.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(clean_tombstone.source_properties, 0U);
        EXPECT_EQ(clean_tombstone.entries_added, 0U);
        EXPECT_EQ(clean_tombstone.groups_translated, 0U);
    }

    TEST(MetadataGpsLifecycle, SameValuePromotionKeepsPlainAndEncodedTextBytes)
    {
        MetaStore source;
        settings_xmp(source, "GPSMapDatum",
                     make_text(source.arena(), "WGS-84", TextEncoding::Utf8));
        settings_xmp(source, "GPSProcessingMethod",
                     make_text(source.arena(), "GPS WLAN", TextEncoding::Utf8));
        gps_lifecycle_native(source, 18U,
                             make_text(source.arena(),
                                       std::string_view("WGS-84\0", 7U),
                                       TextEncoding::Ascii),
                             EntryFlags::None, 2U, "gps-plain-text-wire");
        const std::string_view encoded("ASCII\0\0\0GPS WLAN", 16U);
        gps_lifecycle_native(source, 27U,
                             make_bytes(source.arena(),
                                        std::as_bytes(std::span(
                                            encoded.data(), encoded.size()))),
                             EntryFlags::None, 7U, "gps-encoded-text-wire");
        const std::array<uint8_t, 4> version { 2U, 3U, 0U, 0U };
        gps_lifecycle_native(source, 0U, make_u8_array(source.arena(), version),
                             EntryFlags::None, 1U, "gps-text-version-wire");
        source.finalize();

        const MetadataGpsTranslationResult promoted = gps_lifecycle_translate(
            GpsLifecycleApi::Text, source, &source,
            MetadataGpsTranslationSourceMode::DirtyOnly,
            MetadataGpsTranslationConflictPolicy::FailOnConflict);
        ASSERT_EQ(promoted.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(promoted.groups_translated, 2U);
        EXPECT_EQ(promoted.entries_updated, 2U);
        EXPECT_EQ(promoted.entries_added, 0U);
        const Entry* datum  = gps_lifecycle_find(source, 18U);
        const Entry* method = gps_lifecycle_find(source, 27U);
        ASSERT_NE(datum, nullptr);
        ASSERT_NE(method, nullptr);
        EXPECT_TRUE(any(datum->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(method->flags, EntryFlags::Dirty));
        EXPECT_EQ(gps_lifecycle_text(source, datum->value.data.span),
                  std::string_view("WGS-84\0", 7U));
        EXPECT_EQ(gps_lifecycle_text(source, method->value.data.span), encoded);
        EXPECT_EQ(gps_lifecycle_text(source, datum->origin.wire_type_name),
                  "gps-plain-text-wire");
        EXPECT_EQ(gps_lifecycle_text(source, method->origin.wire_type_name),
                  "gps-encoded-text-wire");
        const MetadataGpsTranslationResult repeated = gps_lifecycle_translate(
            GpsLifecycleApi::Text, source, &source,
            MetadataGpsTranslationSourceMode::DirtyOnly,
            MetadataGpsTranslationConflictPolicy::FailOnConflict);
        ASSERT_EQ(repeated.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(repeated.groups_unchanged, 2U);
        EXPECT_EQ(repeated.entries_updated, 0U);
    }

    TEST(MetadataGpsLifecycle, ReplaceRepairsNativeDuplicatesAsOneGroup)
    {
        MetaStore source;
        settings_xmp(source, "GPSLatitude",
                     make_text(source.arena(), "1,2N", TextEncoding::Utf8));
        gps_lifecycle_native(
            source, 1U, make_text(source.arena(), "N", TextEncoding::Ascii),
            EntryFlags::None, 2U, "gps-duplicate-reference-wire");
        const std::array<URational, 3> exact { URational { 1U, 1U },
                                               URational { 2U, 1U },
                                               URational { 0U, 1U } };
        gps_lifecycle_native(source, 2U,
                             make_urational_array(source.arena(), exact),
                             EntryFlags::None, 5U,
                             "gps-duplicate-exact-coordinate-wire");
        const std::array<URational, 3> duplicate { URational { 1U, 1U },
                                                   URational { 3U, 1U },
                                                   URational { 0U, 1U } };
        const EntryId duplicate_id = gps_lifecycle_native(
            source, 2U, make_urational_array(source.arena(), duplicate),
            EntryFlags::None, 5U, "gps-duplicate-extra-coordinate-wire");
        source.finalize();

        const std::vector<std::byte> source_before = apex_snapshot(source);
        const MetadataGpsTranslationResult limited = gps_lifecycle_translate(
            GpsLifecycleApi::Primary, source, &source,
            MetadataGpsTranslationSourceMode::DirtyOnly,
            MetadataGpsTranslationConflictPolicy::ReplaceExisting, 0U, 3U);
        EXPECT_EQ(limited.status,
                  MetadataGpsTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(apex_snapshot(source), source_before);

        const MetadataGpsTranslationResult repaired
            = gps_lifecycle_translate(GpsLifecycleApi::Primary, source,
                                      &source);
        ASSERT_EQ(repaired.status, MetadataGpsTranslationStatus::Ok);
        EXPECT_EQ(repaired.groups_translated, 1U);
        EXPECT_EQ(repaired.entries_updated, 2U);
        EXPECT_EQ(repaired.entries_removed, 1U);
        EXPECT_EQ(repaired.entries_added, 1U);
        ASSERT_LT(duplicate_id, source.entries().size());
        EXPECT_TRUE(any(source.entry(duplicate_id).flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(source.entry(duplicate_id).flags, EntryFlags::Deleted));
        gps_lifecycle_check_group_flags(source, kGpsLifecycleGroups[0], true);
        ASSERT_NE(gps_lifecycle_find(source, 0U), nullptr);
    }

    TEST(MetadataGpsLifecycle, ExactPromotionsCountTowardOperationLimit)
    {
        MetaStore source = gps_lifecycle_exact_fixture(
            GpsLifecycleApi::Primary);
        const std::vector<std::byte> source_before = apex_snapshot(source);
        const MetadataGpsTranslationResult alias_failure
            = gps_lifecycle_translate(
                GpsLifecycleApi::Primary, source, &source,
                MetadataGpsTranslationSourceMode::DirtyOnly,
                MetadataGpsTranslationConflictPolicy::FailOnConflict, 0U, 1U);
        EXPECT_EQ(alias_failure.status,
                  MetadataGpsTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(apex_snapshot(source), source_before);

        MetaStore output;
        settings_native(output, 0x9201U, make_srational(7, 3));
        output.finalize();
        const std::vector<std::byte> output_before = apex_snapshot(output);
        const MetadataGpsTranslationResult separate_failure
            = gps_lifecycle_translate(
                GpsLifecycleApi::Primary, source, &output,
                MetadataGpsTranslationSourceMode::DirtyOnly,
                MetadataGpsTranslationConflictPolicy::FailOnConflict, 0U, 1U);
        EXPECT_EQ(separate_failure.status,
                  MetadataGpsTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(apex_snapshot(source), source_before);
        EXPECT_EQ(apex_snapshot(output), output_before);
    }

    TEST(MetadataGpsLifecycle,
         LimitsRollbackAliasedAndSeparateOutputsForAllApis)
    {
        const std::array<GpsLifecycleApi, 5U> apis {
            GpsLifecycleApi::Primary, GpsLifecycleApi::Navigation,
            GpsLifecycleApi::Destination, GpsLifecycleApi::Quality,
            GpsLifecycleApi::Text
        };
        const std::array<size_t, 3U> deleted_group_indices { 0U, 3U, 7U };
        for (size_t i = 0U; i < apis.size(); ++i) {
            SCOPED_TRACE(static_cast<uint32_t>(i));
            MetaStore source;
            if (i < deleted_group_indices.size()) {
                source = gps_lifecycle_deleted_source(
                    kGpsLifecycleGroups[deleted_group_indices[i]]);
            } else {
                gps_lifecycle_add_selection_fixture(source, apis[i],
                                                    EntryFlags::Dirty);
            }
            source.finalize();
            const std::vector<std::byte> source_before = apex_snapshot(source);
            const MetadataGpsTranslationResult entry_limit_alias
                = gps_lifecycle_translate(
                    apis[i], source, &source,
                    MetadataGpsTranslationSourceMode::DirtyOnly,
                    MetadataGpsTranslationConflictPolicy::ReplaceExisting, 1U);
            EXPECT_EQ(entry_limit_alias.status,
                      MetadataGpsTranslationStatus::EntryLimitExceeded);
            EXPECT_EQ(apex_snapshot(source), source_before);

            MetaStore entry_limit_output;
            settings_native(entry_limit_output, 0x9201U, make_srational(7, 3));
            entry_limit_output.finalize();
            const std::vector<std::byte> entry_output_before = apex_snapshot(
                entry_limit_output);
            const MetadataGpsTranslationResult entry_limit_separate
                = gps_lifecycle_translate(
                    apis[i], source, &entry_limit_output,
                    MetadataGpsTranslationSourceMode::DirtyOnly,
                    MetadataGpsTranslationConflictPolicy::ReplaceExisting, 1U);
            EXPECT_EQ(entry_limit_separate.status,
                      MetadataGpsTranslationStatus::EntryLimitExceeded);
            EXPECT_EQ(apex_snapshot(source), source_before);
            EXPECT_EQ(apex_snapshot(entry_limit_output), entry_output_before);

            const MetadataGpsTranslationResult operation_limit_alias
                = gps_lifecycle_translate(
                    apis[i], source, &source,
                    MetadataGpsTranslationSourceMode::DirtyOnly,
                    MetadataGpsTranslationConflictPolicy::ReplaceExisting, 0U,
                    1U);
            EXPECT_EQ(operation_limit_alias.status,
                      MetadataGpsTranslationStatus::OperationLimitExceeded);
            EXPECT_EQ(apex_snapshot(source), source_before);

            MetaStore operation_limit_output;
            settings_native(operation_limit_output, 0x9201U,
                            make_srational(7, 3));
            operation_limit_output.finalize();
            const std::vector<std::byte> operation_output_before
                = apex_snapshot(operation_limit_output);
            const MetadataGpsTranslationResult operation_limit_separate
                = gps_lifecycle_translate(
                    apis[i], source, &operation_limit_output,
                    MetadataGpsTranslationSourceMode::DirtyOnly,
                    MetadataGpsTranslationConflictPolicy::ReplaceExisting, 0U,
                    1U);
            EXPECT_EQ(operation_limit_separate.status,
                      MetadataGpsTranslationStatus::OperationLimitExceeded);
            EXPECT_EQ(apex_snapshot(source), source_before);
            EXPECT_EQ(apex_snapshot(operation_limit_output),
                      operation_output_before);
        }
    }

    TEST(MetadataCaptureLifecycle,
         ApexSettingsAndExposureBiasPromoteNativeLifecycleAuthority)
    {
        const std::array<LifecycleOwnerCase, 17U> owners
            = lifecycle_owner_cases();
        for (const LifecycleOwnerCase& owner : owners) {
            SCOPED_TRACE(owner.path);

            MetaStore deleted;
            lifecycle_add_xmp(deleted, owner,
                              EntryFlags::Dirty | EntryFlags::Deleted);
            deleted.finalize();
            auto removed = lifecycle_translate(
                owner, deleted, &deleted,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U,
                1U);
            ASSERT_EQ(removed.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(removed.entries_added, 1U);
            EXPECT_EQ(removed.entries_updated, 0U);
            EXPECT_EQ(removed.entries_removed, 0U);
            ASSERT_NO_FATAL_FAILURE(
                expect_native_delete_intent(deleted, owner.tag));
            const auto deleted_ids = settings_native_history_ids(deleted,
                                                                 owner.tag);
            ASSERT_EQ(deleted_ids.size(), 1U);
            const Entry& delete_intent = deleted.entry(deleted_ids.front());
            EXPECT_EQ(delete_intent.value.kind, MetaValueKind::Scalar);
            EXPECT_EQ(delete_intent.value.elem_type,
                      owner.source_value.elem_type);
            EXPECT_EQ(delete_intent.value.count, 1U);
            removed = lifecycle_translate(
                owner, deleted, &deleted,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U,
                1U);
            expect_lifecycle_repeat(removed.groups_unchanged,
                                    removed.entries_added,
                                    removed.entries_updated);

            MetaStore exact;
            lifecycle_add_xmp(exact, owner);
            lifecycle_add_native(exact, owner, owner.native_value,
                                 EntryFlags::None);
            exact.finalize();
            const auto authority = lifecycle_translate(
                owner, exact, &exact,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U,
                1U);
            ASSERT_EQ(authority.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(authority.entries_added, 0U);
            EXPECT_EQ(authority.entries_updated, 1U);
            ASSERT_NO_FATAL_FAILURE(
                expect_native_authority(exact, owner.tag, owner.wire_code, 1U,
                                        "native-lifecycle-wire"));
            lifecycle_expect_value(exact, owner, owner.native_value);
            const auto exact_repeat = lifecycle_translate(owner, exact, &exact);
            expect_lifecycle_repeat(exact_repeat.groups_unchanged,
                                    exact_repeat.entries_added,
                                    exact_repeat.entries_updated);

            MetaStore clean_history;
            lifecycle_add_xmp(clean_history, owner,
                              EntryFlags::Dirty | EntryFlags::Deleted);
            lifecycle_add_native(clean_history, owner, owner.native_value,
                                 EntryFlags::Deleted);
            lifecycle_add_native(clean_history, owner, owner.native_value,
                                 EntryFlags::Deleted,
                                 "unrelated-clean-history");
            clean_history.finalize();
            const auto reused = lifecycle_translate(
                owner, clean_history, &clean_history,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U,
                1U);
            ASSERT_EQ(reused.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(reused.entries_updated, 1U);
            EXPECT_EQ(reused.entries_added, 0U);
            auto history_ids = settings_native_history_ids(clean_history,
                                                           owner.tag);
            ASSERT_EQ(history_ids.size(), 2U);
            EXPECT_TRUE(any(clean_history.entry(history_ids[0]).flags,
                            EntryFlags::Dirty));
            EXPECT_TRUE(any(clean_history.entry(history_ids[0]).flags,
                            EntryFlags::Deleted));
            const Entry& reused_marker = clean_history.entry(history_ids[0]);
            EXPECT_EQ(reused_marker.value.elem_type,
                      owner.native_value.elem_type);
            EXPECT_EQ(reused_marker.value.count, owner.native_value.count);
            EXPECT_EQ(reused_marker.origin.wire_type.family, WireFamily::Tiff);
            EXPECT_EQ(reused_marker.origin.wire_type.code, owner.wire_code);
            EXPECT_EQ(reused_marker.origin.wire_count, 1U);
            EXPECT_EQ(reused_marker.origin.order_in_block, 17U);
            const auto reused_marker_wire = clean_history.arena().span(
                reused_marker.origin.wire_type_name);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                           reused_marker_wire.data()),
                                       reused_marker_wire.size()),
                      "native-lifecycle-wire");
            EXPECT_FALSE(any(clean_history.entry(history_ids[1]).flags,
                             EntryFlags::Dirty));
            const auto retained_history_wire = clean_history.arena().span(
                clean_history.entry(history_ids[1]).origin.wire_type_name);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                           retained_history_wire.data()),
                                       retained_history_wire.size()),
                      "unrelated-clean-history");
            const auto history_repeat
                = lifecycle_translate(owner, clean_history, &clean_history);
            expect_lifecycle_repeat(history_repeat.groups_unchanged,
                                    history_repeat.entries_added,
                                    history_repeat.entries_updated);

            MetaStore existing_intent;
            lifecycle_add_xmp(existing_intent, owner,
                              EntryFlags::Dirty | EntryFlags::Deleted);
            lifecycle_add_native(existing_intent, owner, owner.native_value,
                                 EntryFlags::Dirty | EntryFlags::Deleted);
            lifecycle_add_native(existing_intent, owner, owner.native_value,
                                 EntryFlags::Deleted,
                                 "preserved-clean-history");
            existing_intent.finalize();
            const auto intent_repeat = lifecycle_translate(
                owner, existing_intent, &existing_intent,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U,
                1U);
            expect_lifecycle_repeat(intent_repeat.groups_unchanged,
                                    intent_repeat.entries_added,
                                    intent_repeat.entries_updated);
            history_ids = settings_native_history_ids(existing_intent,
                                                      owner.tag);
            ASSERT_EQ(history_ids.size(), 2U);
            EXPECT_TRUE(any(existing_intent.entry(history_ids[0]).flags,
                            EntryFlags::Dirty));
            EXPECT_FALSE(any(existing_intent.entry(history_ids[1]).flags,
                             EntryFlags::Dirty));
            const auto preserved_history_wire = existing_intent.arena().span(
                existing_intent.entry(history_ids[1]).origin.wire_type_name);
            EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                           preserved_history_wire.data()),
                                       preserved_history_wire.size()),
                      "preserved-clean-history");

            MetaStore omitted;
            lifecycle_add_native(omitted, owner, owner.native_value,
                                 EntryFlags::None);
            omitted.finalize();
            const auto omitted_result = lifecycle_translate(owner, omitted,
                                                            &omitted);
            EXPECT_EQ(omitted_result.status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(omitted_result.entries_added, 0U);
            EXPECT_EQ(omitted_result.entries_updated, 0U);
            EXPECT_EQ(omitted_result.entries_removed, 0U);
            EXPECT_EQ(settings_active_count(omitted, owner.tag), 1U);
            lifecycle_expect_value(omitted, owner, owner.native_value);

            MetaStore clean_source;
            lifecycle_add_xmp(clean_source, owner, EntryFlags::None);
            clean_source.finalize();
            const auto dirty_only = lifecycle_translate(owner, clean_source,
                                                        &clean_source);
            EXPECT_EQ(dirty_only.source_properties, 0U);
            EXPECT_EQ(dirty_only.entries_added, 0U);
            const auto all_source
                = lifecycle_translate(owner, clean_source, &clean_source,
                                      MetadataCaptureTranslationSourceMode::All);
            EXPECT_EQ(all_source.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(all_source.entries_added, 1U);
            EXPECT_NE(settings_find(clean_source, owner.tag), nullptr);
        }

        LifecycleOwnerCase capture_bias = owners[3U];
        capture_bias.translator         = LifecycleTranslator::Capture;
        capture_bias.path               = "ExposureCompensation";
        MetaStore bias_deleted;
        lifecycle_add_xmp(bias_deleted, capture_bias,
                          EntryFlags::Dirty | EntryFlags::Deleted);
        bias_deleted.finalize();
        auto bias_removed = lifecycle_translate(
            capture_bias, bias_deleted, &bias_deleted,
            MetadataCaptureTranslationSourceMode::DirtyOnly,
            MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U, 1U);
        ASSERT_EQ(bias_removed.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(bias_removed.entries_added, 1U);
        ASSERT_NO_FATAL_FAILURE(
            expect_native_delete_intent(bias_deleted, capture_bias.tag));

        MetaStore bias_exact;
        lifecycle_add_xmp(bias_exact, capture_bias);
        lifecycle_add_native(bias_exact, capture_bias,
                             capture_bias.native_value, EntryFlags::None,
                             "native-capture-bias-wire");
        bias_exact.finalize();
        const auto bias_authority = lifecycle_translate(
            capture_bias, bias_exact, &bias_exact,
            MetadataCaptureTranslationSourceMode::DirtyOnly,
            MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U, 1U);
        ASSERT_EQ(bias_authority.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(bias_authority.entries_updated, 1U);
        lifecycle_expect_value(bias_exact, capture_bias, make_srational(-2, 4));
        ASSERT_NO_FATAL_FAILURE(
            expect_native_authority(bias_exact, capture_bias.tag,
                                    capture_bias.wire_code, 1U,
                                    "native-capture-bias-wire"));
        const auto bias_repeat = lifecycle_translate(capture_bias, bias_exact,
                                                     &bias_exact);
        expect_lifecycle_repeat(bias_repeat.groups_unchanged,
                                bias_repeat.entries_added,
                                bias_repeat.entries_updated);

        MetaStore duplicate_bias;
        lifecycle_add_xmp(duplicate_bias, capture_bias);
        settings_xmp(duplicate_bias, "ExposureBiasValue",
                     capture_bias.source_value);
        duplicate_bias.finalize();
        const std::vector<std::byte> duplicate_before = apex_snapshot(
            duplicate_bias);
        const auto ambiguous = lifecycle_translate(
            capture_bias, duplicate_bias, &duplicate_bias,
            MetadataCaptureTranslationSourceMode::DirtyOnly,
            MetadataCaptureTranslationConflictPolicy::FailOnConflict);
        EXPECT_EQ(ambiguous.status,
                  MetadataCaptureTranslationStatus::AmbiguousSource);
        EXPECT_EQ(apex_snapshot(duplicate_bias), duplicate_before);
    }

    TEST(MetadataCaptureLifecycle,
         AllOwnersHonorPoliciesAndMixedPreflightBudgets)
    {
        const std::array<LifecycleOwnerCase, 17U> owners
            = lifecycle_owner_cases();
        for (const LifecycleOwnerCase& owner : owners) {
            SCOPED_TRACE(owner.path);
            const MetaValue mismatch = lifecycle_conflict_value(owner);

            MetaStore conflict;
            lifecycle_add_xmp(conflict, owner);
            lifecycle_add_native(conflict, owner, mismatch, EntryFlags::None);
            conflict.finalize();
            const auto preserved = lifecycle_translate(
                owner, conflict, &conflict,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::PreserveExisting);
            ASSERT_EQ(preserved.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(preserved.groups_preserved, 1U);
            EXPECT_EQ(preserved.entries_updated, 0U);
            lifecycle_expect_value(conflict, owner, mismatch);
            const Entry* retained = settings_find(conflict, owner.tag);
            ASSERT_NE(retained, nullptr);
            EXPECT_FALSE(any(retained->flags, EntryFlags::Dirty));

            const std::vector<std::byte> before_conflict = apex_snapshot(
                conflict);
            const auto failed = lifecycle_translate(
                owner, conflict, &conflict,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict);
            EXPECT_EQ(failed.status,
                      MetadataCaptureTranslationStatus::NativeConflict);
            EXPECT_EQ(apex_snapshot(conflict), before_conflict);

            const auto replaced = lifecycle_translate(
                owner, conflict, &conflict,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::ReplaceExisting);
            ASSERT_EQ(replaced.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(replaced.entries_updated, 1U);
            lifecycle_expect_value(conflict, owner, owner.source_value);
            retained = settings_find(conflict, owner.tag);
            ASSERT_NE(retained, nullptr);
            EXPECT_TRUE(any(retained->flags, EntryFlags::Dirty));

            MetaStore exact_preserved;
            lifecycle_add_xmp(exact_preserved, owner);
            lifecycle_add_native(exact_preserved, owner, owner.native_value,
                                 EntryFlags::None);
            exact_preserved.finalize();
            const auto exact_kept = lifecycle_translate(
                owner, exact_preserved, &exact_preserved,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::PreserveExisting);
            ASSERT_EQ(exact_kept.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(exact_kept.groups_preserved, 1U);
            EXPECT_EQ(exact_kept.entries_updated, 0U);
            lifecycle_expect_value(exact_preserved, owner, owner.native_value);
            retained = settings_find(exact_preserved, owner.tag);
            ASSERT_NE(retained, nullptr);
            EXPECT_FALSE(any(retained->flags, EntryFlags::Dirty));
        }

        for (const LifecycleTranslator translator :
             { LifecycleTranslator::Apex, LifecycleTranslator::Settings }) {
            SCOPED_TRACE(translator == LifecycleTranslator::Apex ? "APEX"
                                                                 : "settings");
            LifecycleOwnerCase selector {};
            selector.translator        = translator;
            const uint32_t owner_count = translator == LifecycleTranslator::Apex
                                             ? 5U
                                             : 12U;

            MetaStore partial_dirty;
            size_t field_index = 0U;
            for (const LifecycleOwnerCase& owner : owners) {
                if (owner.translator != translator)
                    continue;
                const EntryFlags flags = field_index % 2U == 0U
                                             ? EntryFlags::Dirty
                                             : EntryFlags::None;
                lifecycle_add_xmp(partial_dirty, owner, flags);
                ++field_index;
            }
            partial_dirty.finalize();
            const auto dirty_selection = lifecycle_translate(
                selector, partial_dirty, &partial_dirty,
                MetadataCaptureTranslationSourceMode::DirtyOnly);
            ASSERT_EQ(dirty_selection.status,
                      MetadataCaptureTranslationStatus::Ok);
            const uint32_t expected_dirty_count = (owner_count + 1U) / 2U;
            EXPECT_EQ(dirty_selection.entries_added, expected_dirty_count);
            const auto all_selection
                = lifecycle_translate(selector, partial_dirty, &partial_dirty,
                                      MetadataCaptureTranslationSourceMode::All);
            ASSERT_EQ(all_selection.status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(all_selection.entries_added,
                      owner_count - expected_dirty_count);
            EXPECT_EQ(all_selection.groups_unchanged, expected_dirty_count);

            MetaStore added = lifecycle_uniform_source(owners, translator,
                                                       false, false);
            added.finalize();
            const auto added_result = lifecycle_translate(
                selector, added, &added,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                owner_count, owner_count);
            ASSERT_EQ(added_result.status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(added_result.entries_added, owner_count);
            EXPECT_EQ(added_result.groups_translated, owner_count);

            MetaStore add_entry_short
                = lifecycle_uniform_source(owners, translator, false, false);
            lifecycle_budget_failure(
                translator, add_entry_short,
                MetadataCaptureTranslationStatus::EntryLimitExceeded,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                owner_count - 1U, owner_count);
            MetaStore add_operation_short
                = lifecycle_uniform_source(owners, translator, false, false);
            lifecycle_budget_failure(
                translator, add_operation_short,
                MetadataCaptureTranslationStatus::OperationLimitExceeded,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                owner_count, owner_count - 1U);

            MetaStore deleted = lifecycle_uniform_source(owners, translator,
                                                         true, false);
            deleted.finalize();
            const auto deleted_result = lifecycle_translate(
                selector, deleted, &deleted,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                owner_count, owner_count);
            ASSERT_EQ(deleted_result.status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(deleted_result.entries_added, owner_count);
            EXPECT_EQ(deleted_result.entries_updated, 0U);
            MetaStore deleted_entry_short
                = lifecycle_uniform_source(owners, translator, true, false);
            lifecycle_budget_failure(
                translator, deleted_entry_short,
                MetadataCaptureTranslationStatus::EntryLimitExceeded,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                owner_count - 1U, owner_count);
            MetaStore deleted_operation_short
                = lifecycle_uniform_source(owners, translator, true, false);
            lifecycle_budget_failure(
                translator, deleted_operation_short,
                MetadataCaptureTranslationStatus::OperationLimitExceeded,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                owner_count, owner_count - 1U);

            MetaStore exact_values
                = lifecycle_uniform_source(owners, translator, false, true,
                                           EntryFlags::None);
            exact_values.finalize();
            const auto exact_result = lifecycle_translate(
                selector, exact_values, &exact_values,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U,
                owner_count);
            ASSERT_EQ(exact_result.status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(exact_result.entries_updated, owner_count);
            EXPECT_EQ(exact_result.entries_added, 0U);
            MetaStore exact_operation_short
                = lifecycle_uniform_source(owners, translator, false, true,
                                           EntryFlags::None);
            lifecycle_budget_failure(
                translator, exact_operation_short,
                MetadataCaptureTranslationStatus::OperationLimitExceeded,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U,
                owner_count - 1U);

            MetaStore clean_markers
                = lifecycle_uniform_source(owners, translator, true, true,
                                           EntryFlags::Deleted);
            clean_markers.finalize();
            const auto markers_promoted = lifecycle_translate(
                selector, clean_markers, &clean_markers,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U,
                owner_count);
            ASSERT_EQ(markers_promoted.status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(markers_promoted.entries_updated, owner_count);
            EXPECT_EQ(markers_promoted.entries_added, 0U);
            MetaStore marker_operation_short
                = lifecycle_uniform_source(owners, translator, true, true,
                                           EntryFlags::Deleted);
            lifecycle_budget_failure(
                translator, marker_operation_short,
                MetadataCaptureTranslationStatus::OperationLimitExceeded,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict, 1U,
                owner_count - 1U);

            MetaStore mixed = lifecycle_mixed_source(owners, translator);
            mixed.finalize();
            const uint32_t mixed_added = translator == LifecycleTranslator::Apex
                                             ? 3U
                                             : 6U;
            const uint32_t mixed_updated
                = translator == LifecycleTranslator::Apex ? 1U : 3U;
            const uint32_t mixed_removed
                = translator == LifecycleTranslator::Apex ? 1U : 3U;
            const auto mixed_result = lifecycle_translate(
                selector, mixed, &mixed,
                MetadataCaptureTranslationSourceMode::DirtyOnly,
                MetadataCaptureTranslationConflictPolicy::ReplaceExisting,
                mixed_added, owner_count);
            ASSERT_EQ(mixed_result.status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(mixed_result.entries_added, mixed_added);
            EXPECT_EQ(mixed_result.entries_updated, mixed_updated);
            EXPECT_EQ(mixed_result.entries_removed, mixed_removed);
            EXPECT_EQ(mixed_result.groups_translated, owner_count);

            MetaStore mixed_entry_short = lifecycle_mixed_source(owners,
                                                                 translator);
            lifecycle_budget_failure(
                translator, mixed_entry_short,
                MetadataCaptureTranslationStatus::EntryLimitExceeded,
                MetadataCaptureTranslationConflictPolicy::ReplaceExisting,
                mixed_added - 1U, owner_count);
            MetaStore mixed_operation_short
                = lifecycle_mixed_source(owners, translator);
            lifecycle_budget_failure(
                translator, mixed_operation_short,
                MetadataCaptureTranslationStatus::OperationLimitExceeded,
                MetadataCaptureTranslationConflictPolicy::ReplaceExisting,
                mixed_added, owner_count - 1U);
        }
    }

    TEST(MetadataApex, ExactTypedDecimalScientificAndBoundaryValues)
    {
        struct Case {
            size_t index;
            std::string_view text;
            int64_t numer;
            uint32_t denom;
        };
        constexpr Case cases[] = {
            { 0U, "-2147483648", INT32_MIN, 1U },
            { 0U, "2147483647/2147483646", INT32_MAX, 2147483646U },
            { 0U, "4294967294/2", INT32_MAX, 1U },
            { 1U, "+0.0", 0, 1U },
            { 1U, "3.125e1", 125, 4U },
            { 2U, "-99.99", -9999, 100U },
            { 2U, "1000", 1000, 1U },
            { 3U, "-2.5e-1", -1, 4U },
            { 4U, "8589934590/2", UINT32_MAX, 1U },
            { 4U, "1/4294967295", 1, UINT32_MAX },
        };
        for (const Case& item : cases) {
            MetaStore source;
            settings_xmp(source, kApexPaths[item.index],
                         make_text(source.arena(), item.text,
                                   TextEncoding::Utf8));
            source.finalize();
            ASSERT_EQ(translate_xmp_apex_metadata(source, {}, &source).status,
                      ApexStatus::Ok)
                << item.text;
            apex_expect(source, item.index, item.numer, item.denom);
        }
        MetaStore typed;
        settings_xmp(typed, kApexPaths[0], make_srational(INT32_MIN, 2));
        settings_xmp(typed, kApexPaths[1], make_i32(0));
        settings_xmp(typed, kApexPaths[2], make_i32(-1));
        settings_xmp(typed, kApexPaths[3], make_u32(3U));
        settings_xmp(typed, kApexPaths[4], make_urational(8U, 4U));
        typed.finalize();
        ASSERT_EQ(translate_xmp_apex_metadata(typed, {}, &typed).status,
                  ApexStatus::Ok);
        apex_expect(typed, 0U, -1073741824, 1U);
        apex_expect(typed, 1U, 0, 1U);
        apex_expect(typed, 2U, -2, 2U);
        apex_expect(typed, 3U, 3, 1U);
        apex_expect(typed, 4U, 2, 1U);
    }

    TEST(MetadataApex,
         BrightnessUnknownPrecedesReductionAndFiniteValuesStayFinite)
    {
        struct Case {
            std::string_view text;
            int32_t numer;
            int32_t denom;
        };
        constexpr Case cases[] = {
            { "Unknown", -1, 1 },        { "-1/7", -1, 1 },
            { "-01/2147483647", -1, 1 }, { "-1", -2, 2 },
            { "-1.0", -2, 2 },           { "-1e-1", -2, 20 },
            { "-2/4", -2, 4 },           { "-2/2147483646", -2, 2147483646 },
        };
        for (const Case& item : cases) {
            MetaStore source;
            settings_xmp(source, kApexPaths[2],
                         make_text(source.arena(), item.text,
                                   TextEncoding::Ascii));
            source.finalize();
            ASSERT_EQ(translate_xmp_apex_metadata(source, {}, &source).status,
                      ApexStatus::Ok)
                << item.text;
            apex_expect(source, 2U, item.numer, item.denom);
        }
        for (int32_t numerator : { -1, -2 }) {
            MetaStore source;
            settings_xmp(source, kApexPaths[2], make_srational(numerator, 4));
            source.finalize();
            ASSERT_EQ(translate_xmp_apex_metadata(source, {}, &source).status,
                      ApexStatus::Ok);
            apex_expect(source, 2U, numerator, numerator == -1 ? 1U : 4U);
        }
        for (std::string_view text : { "-2/2147483648", "-1/2147483648" }) {
            MetaStore source  = apex_source();
            Entry replacement = source.entry(2U);
            replacement.value = make_text(source.arena(), text,
                                          TextEncoding::Ascii);
            identity_fixture_replace(source, 2U, replacement);
            apex_failure(source, ApexStatus::ValueOutOfRange);
        }
    }

    TEST(MetadataApex, InvalidNumbersTypesAndLateErrorsRollbackTheBatch)
    {
        constexpr std::string_view invalid[]
            = { "1/0", "1/-2", "NaN", "inf",  "4 EV", "f/4",  "1 s",
                "",    " 2",   "2 ",  "0x10", ".5",   "1/2/3" };
        for (std::string_view text : invalid) {
            MetaStore source = apex_source();
            Entry entry      = source.entry(4U);
            entry.value = make_text(source.arena(), text, TextEncoding::Ascii);
            identity_fixture_replace(source, 4U, entry);
            apex_failure(source, ApexStatus::InvalidNumericValue);
        }
        for (const MetaValue value :
             { make_f64_bits(0x3ff0000000000000ULL), make_srational(2, 1),
               make_urational(1U, 0U), make_i32(-1) }) {
            MetaStore source;
            settings_xmp(source, kApexPaths[4], value);
            const ApexStatus expected
                = value.elem_type == MetaElementType::URational
                      ? ApexStatus::InvalidNumericValue
                  : value.elem_type == MetaElementType::I32
                      ? ApexStatus::ValueOutOfRange
                      : ApexStatus::InvalidSourceValue;
            apex_failure(source, expected);
        }
        for (const MetaValue value :
             { make_srational(1, -1), make_srational(1, 0),
               make_urational(1U, 2U) }) {
            MetaStore source;
            settings_xmp(source, kApexPaths[0], value);
            apex_failure(source, value.elem_type == MetaElementType::URational
                                     ? ApexStatus::InvalidSourceValue
                                     : ApexStatus::InvalidNumericValue);
        }
        for (size_t index : { 0U, 4U }) {
            for (std::string_view text :
                 { "4294967296", "1/4294967296", "1e999",
                   "18446744073709551616/2" }) {
                MetaStore source;
                settings_xmp(source, kApexPaths[index],
                             make_text(source.arena(), text,
                                       TextEncoding::Utf8));
                apex_failure(source, ApexStatus::ValueOutOfRange);
            }
        }
        MetaStore malformed;
        MetaValue count = make_srational(2, 3);
        count.count     = 2U;
        settings_xmp(malformed, kApexPaths[0], count);
        malformed.finalize();
        EXPECT_EQ(translate_xmp_apex_metadata(malformed, {}, &malformed).status,
                  ApexStatus::InvalidSourceValue);
        ASSERT_EQ(malformed.entries().size(), 1U);
        EXPECT_EQ(malformed.entry(0U).value.count, 2U);
        EXPECT_EQ(malformed.entry(0U).value.data.sr.numer, 2);
        EXPECT_EQ(malformed.entry(0U).value.data.sr.denom, 3);
    }

    TEST(MetadataApex, DuplicateAliasesAndStructuredShapesAreRejected)
    {
        for (std::string_view path :
             { "ShutterSpeedValue", "ExposureCompensation", "ApertureValue[1]",
               "BrightnessValue/exif:Value", "MaxApertureValue?xml:lang" }) {
            MetaStore source = apex_source();
            settings_xmp(source, path, make_u32(1U));
            apex_failure(source, path == "ShutterSpeedValue"
                                         || path == "ExposureCompensation"
                                     ? ApexStatus::AmbiguousSource
                                     : ApexStatus::UnsupportedSourceShape);
        }
        MetaStore source;
        settings_xmp(source, "ApertureValue", make_u32(4U), EntryFlags::Dirty,
                     "http://example.test/exif/");
        settings_xmp(source, "ApertureValueExtra", make_u32(4U));
        settings_xmp(source, "ApertureValue[1]", make_u32(4U),
                     EntryFlags::None);
        source.finalize();
        EXPECT_EQ(translate_xmp_apex_metadata(source, {}, &source).entries_added,
                  0U);
    }

    TEST(MetadataApex,
         NativeTypesDuplicatesAndBrightnessSentinelsHaveExplicitConflicts)
    {
        for (ApexPolicy policy :
             { ApexPolicy::PreserveExisting, ApexPolicy::FailOnConflict,
               ApexPolicy::ReplaceExisting }) {
            MetaStore source = apex_source();
            settings_native(source, kApexTags[2], make_srational(-1, 2));
            settings_native(source, kApexTags[4], make_u32(2U));
            settings_native(source, kApexTags[4], make_urational(3U, 1U));
            ApexOptions options;
            options.conflict_policy = policy;
            source.finalize();
            if (policy == ApexPolicy::FailOnConflict) {
                apex_failure(source, ApexStatus::NativeConflict, options);
                continue;
            }
            const auto result = translate_xmp_apex_metadata(source, options,
                                                            &source);
            ASSERT_EQ(result.status, ApexStatus::Ok);
            if (policy == ApexPolicy::PreserveExisting) {
                EXPECT_EQ(result.groups_preserved, 2U);
                apex_expect(source, 2U, -1, 2U);
                EXPECT_EQ(settings_active_count(source, kApexTags[4]), 2U);
            } else {
                EXPECT_EQ(result.entries_removed, 1U);
                EXPECT_EQ(result.entries_updated, 2U);
                apex_expect(source, 2U, -2, 4U);
                apex_expect(source, 4U, UINT32_MAX, 2U);
            }
        }
        MetaStore source;
        settings_xmp(source, kApexPaths[2],
                     make_text(source.arena(), "Unknown", TextEncoding::Ascii));
        settings_native(source, kApexTags[2], make_srational(-1, INT32_MAX));
        source.finalize();
        const auto unknown_authority = translate_xmp_apex_metadata(source, {},
                                                                   &source);
        ASSERT_EQ(unknown_authority.status, ApexStatus::Ok);
        EXPECT_EQ(unknown_authority.groups_unchanged, 0U);
        EXPECT_EQ(unknown_authority.entries_updated, 1U);
        const Entry* unknown_native = settings_find(source, kApexTags[2]);
        ASSERT_NE(unknown_native, nullptr);
        EXPECT_TRUE(any(unknown_native->flags, EntryFlags::Dirty));
        EXPECT_EQ(unknown_native->value.elem_type, MetaElementType::SRational);
        EXPECT_EQ(unknown_native->value.count, 1U);
        EXPECT_EQ(unknown_native->value.data.sr.numer, -1);
        EXPECT_EQ(unknown_native->value.data.sr.denom, INT32_MAX);
        const auto unknown_repeat = translate_xmp_apex_metadata(source, {},
                                                                &source);
        ASSERT_EQ(unknown_repeat.status, ApexStatus::Ok);
        EXPECT_EQ(unknown_repeat.groups_unchanged, 1U);
        EXPECT_EQ(unknown_repeat.entries_updated, 0U);
    }

    TEST(MetadataApex,
         DirtyTombstonesRemoveFiveFieldsAndMissingSourcesRetainThem)
    {
        for (bool dirty : { false, true }) {
            MetaStore source;
            for (size_t i = 0U; i < kApexTags.size(); ++i) {
                settings_xmp(source, kApexPaths[i], {},
                             dirty ? EntryFlags::Dirty | EntryFlags::Deleted
                                   : EntryFlags::Deleted);
                settings_native(source, kApexTags[i],
                                i == 1U || i == 4U ? make_urational(2U, 1U)
                                                   : make_srational(2, 1));
            }
            settings_native(source, 0x829aU, make_urational(1U, 125U));
            source.finalize();
            ApexOptions options;
            options.source_mode     = MetadataCaptureTranslationSourceMode::All;
            options.conflict_policy = ApexPolicy::ReplaceExisting;
            const auto result = translate_xmp_apex_metadata(source, options,
                                                            &source);
            ASSERT_EQ(result.status, ApexStatus::Ok);
            EXPECT_EQ(result.entries_removed, dirty ? 5U : 0U);
            for (uint16_t tag : kApexTags)
                EXPECT_EQ(settings_find(source, tag) == nullptr, dirty);
            EXPECT_NE(settings_find(source, 0x829aU), nullptr);
        }
    }

    TEST(MetadataApex, ResourceAndOptionLimitsAreTransactional)
    {
        for (unsigned variant = 0U; variant < 4U; ++variant) {
            MetaStore source = apex_source();
            ApexOptions options;
            ApexStatus status;
            switch (variant) {
            case 0U:
                options.max_added_entries = 4U;
                status                    = ApexStatus::EntryLimitExceeded;
                break;
            case 1U:
                options.max_operations = 4U;
                status                 = ApexStatus::OperationLimitExceeded;
                break;
            case 2U:
                options.max_text_bytes_per_property = 4U;
                status                              = ApexStatus::ValueTooLong;
                break;
            default:
                options.max_total_text_bytes = 10U;
                status                       = ApexStatus::SourceLimitExceeded;
                break;
            }
            apex_failure(source, status, options);
        }
        for (unsigned variant = 0U; variant < 7U; ++variant) {
            MetaStore source = apex_source();
            ApexOptions options;
            switch (variant) {
            case 0U: options.max_added_entries = 6U; break;
            case 1U: options.max_operations = 0U; break;
            case 2U: options.max_text_bytes_per_property = 129U; break;
            case 3U: options.max_total_text_bytes = 641U; break;
            case 4U:
                options.source_mode
                    = static_cast<MetadataCaptureTranslationSourceMode>(255U);
                break;
            case 5U:
                options.conflict_policy = static_cast<ApexPolicy>(255U);
                break;
            default:
                for (bool ApexOptions::* flag : kApexFlags)
                    options.*flag = false;
                break;
            }
            apex_failure(source, ApexStatus::InvalidOptions, options);
        }
        MetaStore source = apex_source();
        EXPECT_EQ(translate_xmp_apex_metadata(source, {}, nullptr).status,
                  ApexStatus::NullOutput);
        MetaStore output;
        EXPECT_EQ(translate_xmp_apex_metadata(source, {}, &output).status,
                  ApexStatus::SourceNotFinalized);
    }

    TEST(MetadataApex, PortableRoundTripRetainsExactUnitsAndManagedNativeValues)
    {
        for (bool unknown : { false, true }) {
            MetaStore source = apex_source();
            if (unknown) {
                Entry entry = source.entry(2U);
                entry.value = make_srational(-1, 7);
                identity_fixture_replace(source, 2U, entry);
            }
            source.finalize();
            ASSERT_EQ(translate_xmp_apex_metadata(source, {}, &source).status,
                      ApexStatus::Ok);
            for (bool canonical : { false, true }) {
                XmpPortableOptions options;
                options.include_existing_xmp = canonical;
                options.existing_standard_namespace_policy
                    = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
                std::array<std::byte, 8192> bytes {};
                const auto dumped = dump_xmp_portable(source, bytes, options);
                ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                const std::string_view xml(reinterpret_cast<const char*>(
                                               bytes.data()),
                                           dumped.written);
                EXPECT_NE(
                    xml.find(
                        "<exif:ShutterSpeedValue>-7/3</exif:ShutterSpeedValue>"),
                    std::string_view::npos);
                EXPECT_NE(xml.find(
                              "<exif:ApertureValue>0/1</exif:ApertureValue>"),
                          std::string_view::npos);
                EXPECT_NE(
                    xml.find(
                        "<exif:ExposureCompensation>1/3</exif:ExposureCompensation>"),
                    std::string_view::npos);
                MetaStore restored;
                ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(),
                                                      dumped.written),
                                            restored)
                              .status,
                          XmpDecodeStatus::Ok);
                restored.finalize();
                ApexOptions all;
                all.source_mode = MetadataCaptureTranslationSourceMode::All;
                ASSERT_EQ(translate_xmp_apex_metadata(restored, all, &restored)
                              .status,
                          ApexStatus::Ok);
                apex_expect(restored, 0U, -7, 3U);
                apex_expect(restored, 1U, 0, 1U);
                apex_expect(restored, 2U, unknown ? -1 : -2, unknown ? 1U : 4U);
                apex_expect(restored, 3U, 1, 3U);
                apex_expect(restored, 4U, UINT32_MAX, 2U);
            }
        }
    }
}  // namespace
}  // namespace openmeta

namespace openmeta {
namespace {
    TEST(MetadataApex,
         ExistingTypedXmpRetainsExactFractionsAndBrightnessMeaning)
    {
        for (bool unknown : { false, true }) {
            MetaStore source;
            settings_xmp(source, kApexPaths[0],
                         make_srational(INT32_MAX, INT32_MAX - 1));
            settings_xmp(source, kApexPaths[1], make_urational(1U, UINT32_MAX));
            settings_xmp(source, kApexPaths[2],
                         make_srational(unknown ? -1 : -2, 6));
            settings_xmp(source, kApexPaths[3], make_srational(-7, 3));
            settings_xmp(source, kApexPaths[4], make_urational(UINT32_MAX, 2U));
            source.finalize();
            std::array<std::byte, 8192> bytes {};
            XmpPortableOptions portable;
            portable.include_existing_xmp = true;
            const auto dumped = dump_xmp_portable(source, bytes, portable);
            ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
            const std::string_view xml(reinterpret_cast<const char*>(
                                           bytes.data()),
                                       dumped.written);
            EXPECT_NE(
                xml.find(
                    unknown
                        ? "<exif:BrightnessValue>-1/6</exif:BrightnessValue>"
                        : "<exif:BrightnessValue>-2/6</exif:BrightnessValue>"),
                std::string_view::npos);
            MetaStore restored;
            ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(), dumped.written),
                                        restored)
                          .status,
                      XmpDecodeStatus::Ok);
            restored.finalize();
            ApexOptions options;
            options.source_mode = MetadataCaptureTranslationSourceMode::All;
            ASSERT_EQ(translate_xmp_apex_metadata(restored, options, &restored)
                          .status,
                      ApexStatus::Ok);
            apex_expect(restored, 0U, INT32_MAX, INT32_MAX - 1U);
            apex_expect(restored, 1U, 1, UINT32_MAX);
            apex_expect(restored, 2U, unknown ? -1 : -2, unknown ? 1U : 6U);
            apex_expect(restored, 3U, -7, 3U);
            apex_expect(restored, 4U, UINT32_MAX, 2U);
        }
    }

    constexpr std::array<uint16_t, 9> kAdditionalTags
        = { 0xa405U, 0xa300U, 0xa301U, 0x9400U, 0x9401U,
            0x9402U, 0x9403U, 0x9404U, 0x9405U };
    constexpr std::array<std::string_view, 9> kAdditionalNames
        = { "FocalLengthIn35mmFilm",
            "FileSource",
            "SceneType",
            "Temperature",
            "Humidity",
            "Pressure",
            "WaterDepth",
            "Acceleration",
            "CameraElevationAngle" };

    static MetadataCaptureTranslationResult additional_translate_to(
        const MetaStore& source, bool environment,
        MetadataCaptureTranslationConflictPolicy policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict,
        uint32_t max_ops = kMetadataCaptureTranslationMaxOperations,
        MetaStore* output = nullptr)
    {
        if (!output)
            return { .status = MetadataCaptureTranslationStatus::NullOutput };
        if (environment)
            return translate_xmp_environment_metadata(
                source,
                { .conflict_policy = policy, .max_operations = max_ops },
                output);
        return translate_xmp_capture_additional_metadata(
            source, { .conflict_policy = policy, .max_operations = max_ops },
            output);
    }

    static MetadataCaptureTranslationResult additional_translate(
        MetaStore& source, bool environment,
        MetadataCaptureTranslationConflictPolicy policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict,
        uint32_t max_ops = kMetadataCaptureTranslationMaxOperations)
    {
        return additional_translate_to(source, environment, policy, max_ops,
                                       &source);
    }

    static void additional_xmp_group(MetaStore& store, size_t index,
                                     EntryFlags flags = EntryFlags::Dirty,
                                     std::string_view schema_ns = {},
                                     bool unsigned_reduction_witness = false)
    {
        if (index >= kAdditionalNames.size())
            return;
        const std::string_view ns
            = schema_ns.empty()
                  ? (index < 3U ? kSettingsNs : kSensitivityNs)
                  : schema_ns;
        switch (index) {
        case 0U:
            settings_xmp(store, kAdditionalNames[index], make_u16(35U), flags,
                         ns);
            break;
        case 1U:
            settings_xmp(store, kAdditionalNames[index],
                         make_text(store.arena(), "3", TextEncoding::Ascii),
                         flags, ns);
            break;
        case 2U:
            settings_xmp(store, kAdditionalNames[index],
                         make_text(store.arena(), "1", TextEncoding::Utf8),
                         flags, ns);
            break;
        case 3U:
        case 6U:
        case 8U:
            settings_xmp(store, kAdditionalNames[index],
                         make_text(store.arena(), "-7/-1", TextEncoding::Utf8),
                         flags, ns);
            break;
        case 4U:
        case 5U:
        case 7U:
            settings_xmp(store, kAdditionalNames[index],
                         make_text(store.arena(),
                                   unsigned_reduction_witness
                                       ? "3/4294967295"
                                       : "7/4294967295",
                                   TextEncoding::Utf8),
                         flags, ns);
            break;
        default: break;
        }
    }

    static void additional_exact_native(MetaStore& store, size_t index,
                                        EntryFlags flags = EntryFlags::None)
    {
        static constexpr std::array<uint16_t, 9> wire_codes {
            3U, 7U, 7U, 10U, 5U, 5U, 10U, 5U, 10U
        };
        static constexpr std::array<std::string_view, 9> wire_names {
            "native-35mm", "native-file-source", "native-scene-type",
            "native-temperature", "native-humidity", "native-pressure",
            "native-water-depth", "native-acceleration",
            "native-elevation"
        };
        if (index >= kAdditionalTags.size())
            return;
        switch (index) {
        case 0U:
            settings_native_entry(store, kAdditionalTags[index],
                                  make_u16(35U), flags, wire_codes[index],
                                  wire_names[index]);
            break;
        case 1U: {
            const std::array<std::byte, 1> value { std::byte { 3U } };
            settings_native_entry(store, kAdditionalTags[index],
                                  make_bytes(store.arena(), value), flags,
                                  wire_codes[index], wire_names[index]);
            break;
        }
        case 2U: {
            const std::array<std::byte, 1> value { std::byte { 1U } };
            settings_native_entry(store, kAdditionalTags[index],
                                  make_bytes(store.arena(), value), flags,
                                  wire_codes[index], wire_names[index]);
            break;
        }
        case 3U:
        case 6U:
        case 8U:
            settings_native_entry(store, kAdditionalTags[index],
                                  make_srational(-7, -1), flags,
                                  wire_codes[index], wire_names[index]);
            break;
        case 4U:
        case 5U:
        case 7U:
            settings_native_entry(store, kAdditionalTags[index],
                                  make_urational(7U, UINT32_MAX), flags,
                                  wire_codes[index], wire_names[index]);
            break;
        default: break;
        }
    }

    static void additional_finite_counterpart(MetaStore& store, size_t index,
                                              bool different_sentinel = false)
    {
        if (index < 3U || index >= kAdditionalTags.size())
            return;
        switch (index) {
        case 3U:
        case 6U:
        case 8U:
            settings_native_entry(
                store, kAdditionalTags[index],
                different_sentinel ? make_srational(-8, -1)
                                   : make_srational(7, 1));
            break;
        case 4U:
        case 5U:
        case 7U:
            settings_native_entry(
                store, kAdditionalTags[index],
                different_sentinel
                    ? make_urational(4U, UINT32_MAX)
                    : make_urational(1U, UINT32_MAX / 3U));
            break;
        default: break;
        }
    }

    TEST(MetadataAdditionalCapture, CodesUseUndefinedBytesAndExplicitAliases)
    {
        for (uint16_t code = 0U; code < 4U; ++code) {
            MetaStore source;
            settings_xmp(source,
                         code % 2U ? "FocalLengthIn35mmFormat"
                                   : "FocalLengthIn35mmFilm",
                         make_u16(code == 0U ? 0U : UINT16_MAX));
            settings_xmp(source, "FileSource", make_u16(code));
            settings_xmp(source, "SceneType",
                         make_text(source.arena(), "1", TextEncoding::Utf8));
            source.finalize();
            const auto result = additional_translate(source, false);
            ASSERT_EQ(result.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(result.entries_added, 3U);
            EXPECT_EQ(settings_find(source, 0xa405U)->value.data.u64,
                      code == 0U ? 0U : UINT16_MAX);
            for (uint16_t tag : { 0xa300U, 0xa301U }) {
                const auto& value = settings_find(source, tag)->value;
                ASSERT_EQ(value.kind, MetaValueKind::Bytes);
                ASSERT_EQ(value.count, 1U);
                EXPECT_EQ(std::to_integer<uint8_t>(
                              source.arena().span(value.data.span)[0]),
                          tag == 0xa300U ? code : 1U);
            }
            EXPECT_EQ(additional_translate(source, false).groups_unchanged, 3U);
        }
    }

    TEST(MetadataAdditionalCapture, InvalidCodesAndAliasesRollBack)
    {
        for (size_t i = 0U; i < 3U; ++i) {
            for (unsigned variant = 0U; variant < 5U; ++variant) {
                MetaStore source;
                settings_xmp(source, "Temperature", make_srational(20, 1));
                MetaValue value = make_u32(i == 0U   ? 65536U
                                           : i == 1U ? 4U
                                                     : 0U);
                if (variant == 1U)
                    value = make_i32(-1);
                if (variant == 2U)
                    value = make_text(source.arena(), "3 mm",
                                      TextEncoding::Utf8);
                if (variant == 3U) {
                    value       = make_u16(1);
                    value.count = 2U;
                }
                if (variant == 4U)
                    value = make_urational(1U, 1U);
                settings_xmp(source, kAdditionalNames[i], value);
                source.finalize();
                const auto before = source.entries().size();
                const auto result = additional_translate(source, false);
                EXPECT_NE(result.status, MetadataCaptureTranslationStatus::Ok);
                EXPECT_EQ(result.entries_added, 0U);
                EXPECT_EQ(source.entries().size(), before);
                EXPECT_EQ(settings_find(source, kAdditionalTags[i]), nullptr);
            }
        }
        MetaStore source;
        settings_xmp(source, "FocalLengthIn35mmFilm", make_u16(35));
        settings_xmp(source, "FocalLengthIn35mmFormat", make_u16(35));
        source.finalize();
        EXPECT_EQ(additional_translate(source, false).status,
                  MetadataCaptureTranslationStatus::AmbiguousSource);
    }

    TEST(MetadataEnvironment, ExactFiniteValuesAndUnknownDenominators)
    {
        struct Case {
            size_t field;
            std::string_view input;
            int64_t numer;
            int64_t denom;
        };
        constexpr std::array cases = {
            Case { 3U, "-20.5", -41, 2 },
            Case { 4U, "301/3", 301, 3 },
            Case { 5U, "1.01325e3", 4053, 4 },
            Case { 6U, "-1/3", -1, 3 },
            Case { 7U, "9.80665e5", 980665, 1 },
            Case { 8U, "-180", -180, 1 },
            Case { 8U, "179999/1000", 179999, 1000 },
            Case { 3U, "-7/-1", -7, -1 },
            Case { 6U, "-2147483648/4294967295", INT32_MIN, -1 },
            Case { 8U, "Unknown", 0, -1 },
            Case { 4U, "7/4294967295", 7, UINT32_MAX },
            Case { 5U, "4294967295/4294967295", UINT32_MAX, UINT32_MAX },
            Case { 7U, "Unknown", 0, UINT32_MAX },
            Case { 3U, "2/4", 1, 2 },
            Case { 4U, "4294967295/4294967294", UINT32_MAX, UINT32_MAX - 1U },
        };
        for (const auto& item : cases) {
            SCOPED_TRACE(item.input);
            for (bool legacy : { false, true }) {
                MetaStore source;
                settings_xmp(source, kAdditionalNames[item.field],
                             make_text(source.arena(), item.input,
                                       TextEncoding::Utf8),
                             EntryFlags::Dirty,
                             legacy ? kSettingsNs : kSensitivityNs);
                source.finalize();
                const auto result = additional_translate(source, true);
                ASSERT_EQ(result.status, MetadataCaptureTranslationStatus::Ok);
                const auto& value
                    = settings_find(source, kAdditionalTags[item.field])->value;
                const bool sign = item.field == 3U || item.field == 6U
                                  || item.field == 8U;
                if (sign) {
                    ASSERT_EQ(value.elem_type, MetaElementType::SRational);
                    EXPECT_EQ(value.data.sr.numer, item.numer);
                    EXPECT_EQ(value.data.sr.denom, item.denom);
                } else {
                    ASSERT_EQ(value.elem_type, MetaElementType::URational);
                    EXPECT_EQ(value.data.ur.numer, item.numer);
                    EXPECT_EQ(value.data.ur.denom, item.denom);
                }
                EXPECT_EQ(additional_translate(source, true).groups_unchanged,
                          1U);
                XmpPortableOptions options;
                options.include_existing_xmp = true;
                options.existing_standard_namespace_policy
                    = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
                std::array<std::byte, 4096> bytes {};
                const auto dumped = dump_xmp_portable(source, bytes, options);
                ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                const std::string xml(reinterpret_cast<const char*>(
                                          bytes.data()),
                                      dumped.written);
                const std::string name(kAdditionalNames[item.field]);
                EXPECT_NE(xml.find("<exifEX:" + name + ">"
                                   + std::to_string(item.numer) + "/"
                                   + std::to_string(item.denom)
                                   + "</exifEX:" + name + ">"),
                          std::string::npos);
                EXPECT_EQ(xml.find("<exif:" + name + ">"), std::string::npos);
                MetaStore restored;
                ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(),
                                                      dumped.written),
                                            restored)
                              .status,
                          XmpDecodeStatus::Ok);
                restored.finalize();
                ASSERT_EQ(translate_xmp_environment_metadata(
                              restored,
                              { .source_mode
                                = MetadataCaptureTranslationSourceMode::All },
                              &restored)
                              .status,
                          MetadataCaptureTranslationStatus::Ok);
                const auto& r = settings_find(restored,
                                              kAdditionalTags[item.field])
                                    ->value;
                if (sign) {
                    EXPECT_EQ(r.data.sr.numer, item.numer);
                    EXPECT_EQ(r.data.sr.denom, item.denom);
                } else {
                    EXPECT_EQ(r.data.ur.numer, item.numer);
                    EXPECT_EQ(r.data.ur.denom, item.denom);
                }
            }
        }
    }

    TEST(MetadataEnvironment,
         InvalidValuesShapesAndNamespaceDuplicatesAreTransactional)
    {
        struct Case {
            size_t field;
            std::string_view text;
        };
        constexpr std::array cases = {
            Case { 3U, "1/0" },          Case { 3U, "1/-2" },
            Case { 3U, "1.5/-1" },       Case { 3U, "2147483648/-1" },
            Case { 3U, "1/2147483648" }, Case { 4U, "-1" },
            Case { 4U, "1/-1" },         Case { 4U, "4294967296/4294967295" },
            Case { 4U, "2/8589934590" }, Case { 5U, "1013 hPa" },
            Case { 8U, "180" },          Case { 8U, "-180.001" },
            Case { 8U, "nan" },
        };
        for (const auto& item : cases) {
            SCOPED_TRACE(item.text);
            MetaStore source;
            settings_xmp(source, "Acceleration", make_u32(100));
            settings_xmp(source, kAdditionalNames[item.field],
                         make_text(source.arena(), item.text,
                                   TextEncoding::Utf8),
                         EntryFlags::Dirty, kSensitivityNs);
            source.finalize();
            const auto before = source.entries().size();
            EXPECT_NE(additional_translate(source, true).status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(source.entries().size(), before);
            EXPECT_EQ(settings_find(source, 0x9404U), nullptr);
        }
        for (size_t i = 0U; i < 9U; ++i) {
            for (std::string_view suffix : { "[1]", "/value", "?xml:lang" }) {
                MetaStore source;
                settings_xmp(source,
                             std::string(kAdditionalNames[i])
                                 + std::string(suffix),
                             make_u16(1));
                source.finalize();
                EXPECT_EQ(
                    additional_translate(source, i >= 3U).status,
                    MetadataCaptureTranslationStatus::UnsupportedSourceShape);
            }
        }
        MetaStore duplicate;
        settings_xmp(duplicate, "Temperature", make_srational(1, 1));
        settings_xmp(duplicate, "Temperature", make_srational(1, 1),
                     EntryFlags::Dirty, kSensitivityNs);
        duplicate.finalize();
        EXPECT_EQ(additional_translate(duplicate, true).status,
                  MetadataCaptureTranslationStatus::AmbiguousSource);
    }

    TEST(MetadataAdditionalCapture, ConflictsDeletionAndOperationBudgets)
    {
        for (size_t i = 0U; i < 9U; ++i) {
            SCOPED_TRACE(i);
            for (const auto policy :
                 { MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                   MetadataCaptureTranslationConflictPolicy::PreserveExisting,
                   MetadataCaptureTranslationConflictPolicy::ReplaceExisting }) {
                MetaStore source;
                settings_xmp(source, kAdditionalNames[i], make_u16(1));
                settings_native(source, kAdditionalTags[i], make_u32(2));
                settings_native(source, kAdditionalTags[i], make_u32(3));
                source.finalize();
                const auto result = additional_translate(source, i >= 3U,
                                                         policy);
                if (policy
                    == MetadataCaptureTranslationConflictPolicy::FailOnConflict) {
                    EXPECT_EQ(result.status,
                              MetadataCaptureTranslationStatus::NativeConflict);
                    EXPECT_EQ(settings_active_count(source, kAdditionalTags[i]),
                              2U);
                } else if (policy
                           == MetadataCaptureTranslationConflictPolicy::
                               PreserveExisting) {
                    EXPECT_EQ(result.status,
                              MetadataCaptureTranslationStatus::Ok);
                    EXPECT_EQ(result.groups_preserved, 1U);
                    EXPECT_EQ(settings_active_count(source, kAdditionalTags[i]),
                              2U);
                } else {
                    ASSERT_EQ(result.status,
                              MetadataCaptureTranslationStatus::Ok);
                    EXPECT_EQ(result.entries_updated, 1U);
                    EXPECT_EQ(result.entries_removed, 1U);
                    EXPECT_EQ(settings_active_count(source, kAdditionalTags[i]),
                              1U);
                }
            }
            MetaStore source;
            settings_xmp(source, kAdditionalNames[i], {},
                         EntryFlags::Dirty | EntryFlags::Deleted);
            settings_native(source, kAdditionalTags[i], make_u32(2));
            settings_native(source, kAdditionalTags[i], make_u32(3));
            source.finalize();
            constexpr auto replace
                = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
            EXPECT_EQ(additional_translate(source, i >= 3U, replace, 1U).status,
                      MetadataCaptureTranslationStatus::OperationLimitExceeded);
            EXPECT_EQ(settings_active_count(source, kAdditionalTags[i]), 2U);
            EXPECT_EQ(
                additional_translate(source, i >= 3U, replace).entries_removed,
                2U);
            EXPECT_EQ(settings_active_count(source, kAdditionalTags[i]), 0U);
        }
        for (bool environment : { false, true }) {
            for (bool entry_limit : { false, true }) {
                MetaStore source;
                settings_xmp(source, environment ? "Temperature" : "FileSource",
                             make_u16(1U));
                source.finalize();
                source.constrain_resources(entry_limit ? 1U : 0U,
                                           entry_limit
                                               ? 0U
                                               : source.arena().bytes().size());
                const auto before = source.arena().bytes().size();
                const auto result = additional_translate(source, environment);
                EXPECT_EQ(result.status,
                          MetadataCaptureTranslationStatus::EntryLimitExceeded);
                EXPECT_EQ(result.entries_added, 0U);
                EXPECT_EQ(source.entries().size(), 1U);
                EXPECT_EQ(source.arena().bytes().size(), before);
                EXPECT_FALSE(source.resource_limit_exceeded());
            }
        }
    }

    TEST(MetadataAdditionalCapture, InvalidNativeKeepsExistingPortableSource)
    {
        for (unsigned variant = 0U; variant < 4U; ++variant) {
            MetaStore source;
            for (size_t i = 0U; i < 9U; ++i) {
                settings_xmp(source, kAdditionalNames[i], make_u16(1),
                             EntryFlags::Dirty,
                             i < 3U ? kSettingsNs : kSensitivityNs);
                MetaValue value = make_u64(1);
                if (variant == 1U)
                    value = make_srational(1, -2);
                if (variant == 2U)
                    value = make_urational(1U, 0U);
                if (variant == 3U) {
                    value       = make_u16(1);
                    value.count = 2U;
                }
                settings_native(source, kAdditionalTags[i], value);
            }
            source.finalize();
            XmpPortableOptions options;
            options.include_existing_xmp = true;
            options.conflict_policy      = XmpConflictPolicy::GeneratedWins;
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
            EXPECT_EQ(translate_xmp_capture_additional_metadata(
                          restored,
                          { .source_mode
                            = MetadataCaptureTranslationSourceMode::All },
                          &restored)
                          .entries_added,
                      3U);
            EXPECT_EQ(translate_xmp_environment_metadata(
                          restored,
                          { .source_mode
                            = MetadataCaptureTranslationSourceMode::All },
                          &restored)
                          .entries_added,
                      6U);
        }
    }

    TEST(MetadataAdditionalCapture,
         ExactCleanPromotionsRetainRawComponentsAndOwnedCodeBytes)
    {
        constexpr std::array<uint16_t, 9> wire_codes {
            3U, 7U, 7U, 10U, 5U, 5U, 10U, 5U, 10U
        };
        constexpr std::array<std::string_view, 9> wire_names {
            "native-35mm", "native-file-source", "native-scene-type",
            "native-temperature", "native-humidity", "native-pressure",
            "native-water-depth", "native-acceleration",
            "native-elevation"
        };
        const std::array<std::byte, 1> file_source_byte { std::byte { 3U } };
        const std::array<std::byte, 1> scene_type_byte { std::byte { 1U } };
        MetaStore output;
        {
            MetaStore source;
            settings_xmp(source, "FocalLengthIn35mmFilm", make_u16(35U));
            settings_xmp(source, "FileSource",
                         make_text(source.arena(), "3", TextEncoding::Ascii));
            settings_xmp(source, "SceneType",
                         make_text(source.arena(), "1", TextEncoding::Utf8));
            settings_xmp(source, "Temperature",
                         make_text(source.arena(), "-2/3", TextEncoding::Utf8));
            settings_xmp(source, "Humidity",
                         make_text(source.arena(), "2/3", TextEncoding::Utf8));
            settings_xmp(source, "Pressure",
                         make_text(source.arena(), "7/4294967295",
                                   TextEncoding::Utf8),
                         EntryFlags::Dirty, kSensitivityNs);
            settings_xmp(source, "WaterDepth",
                         make_text(source.arena(), "-7/-1", TextEncoding::Utf8),
                         EntryFlags::Dirty, kSensitivityNs);
            settings_xmp(source, "Acceleration",
                         make_text(source.arena(), "9/2", TextEncoding::Utf8),
                         EntryFlags::Dirty, kSensitivityNs);
            settings_xmp(source, "CameraElevationAngle",
                         make_text(source.arena(), "-45", TextEncoding::Utf8),
                         EntryFlags::Dirty, kSensitivityNs);
            settings_native_entry(source, 0xa405U, make_u16(35U),
                                  EntryFlags::Derived, wire_codes[0],
                                  wire_names[0]);
            settings_native_entry(
                source, 0xa300U, make_bytes(source.arena(), file_source_byte),
                EntryFlags::Derived, wire_codes[1], wire_names[1]);
            settings_native_entry(
                source, 0xa301U, make_bytes(source.arena(), scene_type_byte),
                EntryFlags::Derived, wire_codes[2], wire_names[2]);
            settings_native_entry(source, 0x9400U, make_srational(-4, 6),
                                  EntryFlags::Derived, wire_codes[3],
                                  wire_names[3]);
            settings_native_entry(source, 0x9401U, make_urational(4U, 6U),
                                  EntryFlags::Derived, wire_codes[4],
                                  wire_names[4]);
            settings_native_entry(
                source, 0x9402U, make_urational(7U, UINT32_MAX),
                EntryFlags::Derived, wire_codes[5], wire_names[5]);
            settings_native_entry(source, 0x9403U, make_srational(-7, -1),
                                  EntryFlags::Derived, wire_codes[6],
                                  wire_names[6]);
            settings_native_entry(source, 0x9404U, make_urational(18U, 4U),
                                  EntryFlags::Derived, wire_codes[7],
                                  wire_names[7]);
            settings_native_entry(source, 0x9405U, make_srational(-90, 2),
                                  EntryFlags::Derived, wire_codes[8],
                                  wire_names[8]);
            source.finalize();

            const auto additional
                = translate_xmp_capture_additional_metadata(source, {},
                                                            &output);
            ASSERT_EQ(additional.status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(additional.entries_updated, 3U);
            EXPECT_EQ(additional.entries_added, 0U);
            const auto environment
                = translate_xmp_environment_metadata(output, {}, &output);
            ASSERT_EQ(environment.status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(environment.entries_updated, 6U);
            EXPECT_EQ(environment.entries_added, 0U);
        }

        const Entry* focal = settings_find(output, 0xa405U);
        ASSERT_NE(focal, nullptr);
        EXPECT_TRUE(any(focal->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(focal->flags, EntryFlags::Derived));
        EXPECT_EQ(focal->value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(focal->value.elem_type, MetaElementType::U16);
        EXPECT_EQ(focal->value.data.u64, 35U);
        const Entry* file_source = settings_find(output, 0xa300U);
        const Entry* scene_type  = settings_find(output, 0xa301U);
        ASSERT_NE(file_source, nullptr);
        ASSERT_NE(scene_type, nullptr);
        for (const auto [entry, expected] :
             { std::pair<const Entry*, uint8_t> { file_source, 3U },
               std::pair<const Entry*, uint8_t> { scene_type, 1U } }) {
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
            EXPECT_TRUE(any(entry->flags, EntryFlags::Derived));
            EXPECT_EQ(entry->value.kind, MetaValueKind::Bytes);
            EXPECT_EQ(entry->value.elem_type, MetaElementType::U8);
            EXPECT_EQ(entry->value.count, 1U);
            const auto bytes = output.arena().span(entry->value.data.span);
            ASSERT_EQ(bytes.size(), 1U);
            EXPECT_EQ(std::to_integer<uint8_t>(bytes[0]), expected);
        }

        const Entry* temperature = settings_find(output, 0x9400U);
        const Entry* humidity    = settings_find(output, 0x9401U);
        const Entry* pressure    = settings_find(output, 0x9402U);
        const Entry* water_depth = settings_find(output, 0x9403U);
        const Entry* acceleration = settings_find(output, 0x9404U);
        const Entry* elevation = settings_find(output, 0x9405U);
        ASSERT_NE(temperature, nullptr);
        ASSERT_NE(humidity, nullptr);
        ASSERT_NE(pressure, nullptr);
        ASSERT_NE(water_depth, nullptr);
        ASSERT_NE(acceleration, nullptr);
        ASSERT_NE(elevation, nullptr);
        EXPECT_EQ(temperature->value.elem_type, MetaElementType::SRational);
        EXPECT_EQ(temperature->value.data.sr.numer, -4);
        EXPECT_EQ(temperature->value.data.sr.denom, 6);
        EXPECT_EQ(humidity->value.elem_type, MetaElementType::URational);
        EXPECT_EQ(humidity->value.data.ur.numer, 4U);
        EXPECT_EQ(humidity->value.data.ur.denom, 6U);
        EXPECT_EQ(pressure->value.elem_type, MetaElementType::URational);
        EXPECT_EQ(pressure->value.data.ur.numer, 7U);
        EXPECT_EQ(pressure->value.data.ur.denom, UINT32_MAX);
        EXPECT_EQ(water_depth->value.elem_type, MetaElementType::SRational);
        EXPECT_EQ(water_depth->value.data.sr.numer, -7);
        EXPECT_EQ(water_depth->value.data.sr.denom, -1);
        EXPECT_EQ(acceleration->value.elem_type, MetaElementType::URational);
        EXPECT_EQ(acceleration->value.data.ur.numer, 18U);
        EXPECT_EQ(acceleration->value.data.ur.denom, 4U);
        EXPECT_EQ(elevation->value.elem_type, MetaElementType::SRational);
        EXPECT_EQ(elevation->value.data.sr.numer, -90);
        EXPECT_EQ(elevation->value.data.sr.denom, 2);

        for (size_t i = 0U; i < kAdditionalTags.size(); ++i) {
            const Entry* entry = settings_find(output, kAdditionalTags[i]);
            ASSERT_NE(entry, nullptr) << i;
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty)) << i;
            EXPECT_TRUE(any(entry->flags, EntryFlags::Derived)) << i;
            EXPECT_EQ(entry->origin.wire_type.family, WireFamily::Tiff) << i;
            EXPECT_EQ(entry->origin.wire_type.code, wire_codes[i]) << i;
            EXPECT_EQ(entry->origin.wire_count, 1U) << i;
            EXPECT_EQ(entry->origin.order_in_block, 17U) << i;
            const auto wire_name = output.arena().span(
                entry->origin.wire_type_name);
            EXPECT_EQ(std::string_view(
                          reinterpret_cast<const char*>(wire_name.data()),
                          wire_name.size()),
                      wire_names[i]);
        }
        EXPECT_EQ(additional_translate(output, false).groups_unchanged, 3U);
        EXPECT_EQ(additional_translate(output, true).groups_unchanged, 6U);
    }

    TEST(MetadataAdditionalCapture,
         MissingNativeDeleteIntentsAreTypedAndBudgeted)
    {
        constexpr EntryFlags deleted
            = EntryFlags::Dirty | EntryFlags::Deleted;
        MetaStore source;
        settings_xmp(source, "FocalLengthIn35mmFilm", {}, deleted);
        settings_xmp(source, "FileSource", {}, deleted);
        settings_xmp(source, "SceneType", {}, deleted);
        source.finalize();
        MetaStore separate;
        settings_native(separate, 0x829aU, make_urational(1U, 100U));
        separate.finalize();

        MetadataCaptureAdditionalTranslationOptions options;
        options.max_added_entries = 2U;
        EXPECT_EQ(translate_xmp_capture_additional_metadata(source, options,
                                                            &separate)
                      .status,
                  MetadataCaptureTranslationStatus::EntryLimitExceeded);
        ASSERT_EQ(separate.entries().size(), 1U);
        EXPECT_EQ(translate_xmp_capture_additional_metadata(source, options,
                                                            &source)
                      .status,
                  MetadataCaptureTranslationStatus::EntryLimitExceeded);
        ASSERT_EQ(source.entries().size(), 3U);
        for (size_t i = 0U; i < 3U; ++i)
            EXPECT_TRUE(settings_native_history_ids(source,
                                                    kAdditionalTags[i])
                            .empty())
                << i;

        options.max_added_entries = 3U;
        options.max_operations    = 2U;
        EXPECT_EQ(translate_xmp_capture_additional_metadata(source, options,
                                                            &separate)
                      .status,
                  MetadataCaptureTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(translate_xmp_capture_additional_metadata(source, options,
                                                            &source)
                      .status,
                  MetadataCaptureTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(source.entries().size(), 3U);
        options.max_operations = 3U;
        const auto translated
            = translate_xmp_capture_additional_metadata(source, options,
                                                        &source);
        ASSERT_EQ(translated.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(translated.entries_added, 3U);
        const std::array<MetaElementType, 3> types {
            MetaElementType::U16, MetaElementType::U8, MetaElementType::U8
        };
        for (size_t i = 0U; i < 3U; ++i) {
            const auto ids = settings_native_history_ids(source,
                                                         kAdditionalTags[i]);
            ASSERT_EQ(ids.size(), 1U) << i;
            const Entry& intent = source.entry(ids.front());
            EXPECT_TRUE(any(intent.flags, EntryFlags::Dirty)) << i;
            EXPECT_TRUE(any(intent.flags, EntryFlags::Deleted)) << i;
            EXPECT_EQ(intent.value.kind,
                      i == 0U ? MetaValueKind::Scalar : MetaValueKind::Bytes)
                << i;
            EXPECT_EQ(intent.value.elem_type, types[i]) << i;
            EXPECT_EQ(intent.value.count, i == 0U ? 1U : 0U) << i;
            if (i > 0U)
                EXPECT_EQ(intent.value.data.span.size, 0U) << i;
        }
        const auto repeated
            = translate_xmp_capture_additional_metadata(source, options,
                                                        &source);
        ASSERT_EQ(repeated.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(repeated.entries_updated, 0U);
    }

    TEST(MetadataEnvironment, MissingNativeDeleteIntentsAreTypedAndBudgeted)
    {
        constexpr EntryFlags deleted
            = EntryFlags::Dirty | EntryFlags::Deleted;
        constexpr std::array<std::string_view, 6> names {
            "Temperature", "Humidity", "Pressure", "WaterDepth",
            "Acceleration", "CameraElevationAngle"
        };
        constexpr std::array<uint16_t, 6> tags {
            0x9400U, 0x9401U, 0x9402U, 0x9403U, 0x9404U, 0x9405U
        };
        constexpr std::array<MetaElementType, 6> types {
            MetaElementType::SRational, MetaElementType::URational,
            MetaElementType::URational, MetaElementType::SRational,
            MetaElementType::URational, MetaElementType::SRational
        };
        MetaStore source;
        for (const std::string_view name : names)
            settings_xmp(source, name, {}, deleted, kSensitivityNs);
        source.finalize();
        MetaStore separate;
        settings_native(separate, 0x829aU, make_urational(1U, 100U));
        separate.finalize();

        MetadataEnvironmentTranslationOptions options;
        options.max_added_entries = 5U;
        EXPECT_EQ(translate_xmp_environment_metadata(source, options,
                                                    &separate)
                      .status,
                  MetadataCaptureTranslationStatus::EntryLimitExceeded);
        ASSERT_EQ(separate.entries().size(), 1U);
        EXPECT_EQ(translate_xmp_environment_metadata(source, options, &source)
                      .status,
                  MetadataCaptureTranslationStatus::EntryLimitExceeded);
        ASSERT_EQ(source.entries().size(), names.size());
        for (const uint16_t tag : tags)
            EXPECT_TRUE(settings_native_history_ids(source, tag).empty());

        options.max_added_entries = 6U;
        options.max_operations    = 5U;
        EXPECT_EQ(translate_xmp_environment_metadata(source, options,
                                                    &separate)
                      .status,
                  MetadataCaptureTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(translate_xmp_environment_metadata(source, options, &source)
                      .status,
                  MetadataCaptureTranslationStatus::OperationLimitExceeded);
        EXPECT_EQ(source.entries().size(), names.size());
        options.max_operations = 6U;
        const auto translated
            = translate_xmp_environment_metadata(source, options, &source);
        ASSERT_EQ(translated.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(translated.entries_added, 6U);
        for (size_t i = 0U; i < tags.size(); ++i) {
            const auto ids = settings_native_history_ids(source, tags[i]);
            ASSERT_EQ(ids.size(), 1U) << i;
            const Entry& intent = source.entry(ids.front());
            EXPECT_TRUE(any(intent.flags, EntryFlags::Dirty)) << i;
            EXPECT_TRUE(any(intent.flags, EntryFlags::Deleted)) << i;
            EXPECT_EQ(intent.value.kind, MetaValueKind::Scalar) << i;
            EXPECT_EQ(intent.value.elem_type, types[i]) << i;
            if (i == 0U || i == 3U || i == 5U) {
                EXPECT_EQ(intent.value.data.sr.numer, 0) << i;
                EXPECT_EQ(intent.value.data.sr.denom, 1) << i;
            } else {
                EXPECT_EQ(intent.value.data.ur.numer, 0U) << i;
                EXPECT_EQ(intent.value.data.ur.denom, 1U) << i;
            }
        }
        const auto repeated
            = translate_xmp_environment_metadata(source, options, &source);
        ASSERT_EQ(repeated.status, MetadataCaptureTranslationStatus::Ok);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(repeated.entries_updated, 0U);
    }

    TEST(MetadataEnvironment,
         UnknownSentinelsRequireExactNumeratorAndReservedDenominator)
    {
        for (size_t index = 3U; index < kAdditionalTags.size(); ++index) {
            for (bool different_sentinel : { false, true }) {
                SCOPED_TRACE(index);
                SCOPED_TRACE(different_sentinel);
                MetaStore source;
                additional_xmp_group(source, index, EntryFlags::Dirty, {},
                                     true);
                additional_finite_counterpart(source, index,
                                              different_sentinel);
                source.finalize();
                const size_t source_count = source.entries().size();

                MetaStore separate;
                settings_native(separate, 0x829aU,
                                make_urational(1U, 100U));
                separate.finalize();
                const auto failed_separate = additional_translate_to(
                    source, true,
                    MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                    kMetadataCaptureTranslationMaxOperations, &separate);
                EXPECT_EQ(failed_separate.status,
                          MetadataCaptureTranslationStatus::NativeConflict);
                ASSERT_EQ(separate.entries().size(), 1U);
                const Entry* baseline = settings_find(separate, 0x829aU);
                ASSERT_NE(baseline, nullptr);
                EXPECT_EQ(baseline->value.data.ur.numer, 1U);
                EXPECT_EQ(baseline->value.data.ur.denom, 100U);
                const auto failed_alias = additional_translate(
                    source, true,
                    MetadataCaptureTranslationConflictPolicy::FailOnConflict);
                EXPECT_EQ(failed_alias.status,
                          MetadataCaptureTranslationStatus::NativeConflict);
                EXPECT_EQ(source.entries().size(), source_count);
                EXPECT_EQ(settings_active_count(source,
                                                kAdditionalTags[index]),
                          1U);
                const auto kept = additional_translate(
                    source, true,
                    MetadataCaptureTranslationConflictPolicy::PreserveExisting);
                ASSERT_EQ(kept.status, MetadataCaptureTranslationStatus::Ok);
                EXPECT_EQ(kept.groups_preserved, 1U);
                const Entry* native = settings_find(source,
                                                    kAdditionalTags[index]);
                ASSERT_NE(native, nullptr);
                EXPECT_FALSE(any(native->flags, EntryFlags::Dirty));
                if (index == 3U || index == 6U || index == 8U) {
                    EXPECT_EQ(native->value.elem_type,
                              MetaElementType::SRational);
                    EXPECT_EQ(native->value.data.sr.numer,
                              different_sentinel ? -8 : 7);
                    EXPECT_EQ(native->value.data.sr.denom,
                              different_sentinel ? -1 : 1);
                } else {
                    EXPECT_EQ(native->value.elem_type,
                              MetaElementType::URational);
                    EXPECT_EQ(native->value.data.ur.numer,
                              different_sentinel ? 4U : 1U);
                    EXPECT_EQ(native->value.data.ur.denom,
                              different_sentinel ? UINT32_MAX
                                                 : UINT32_MAX / 3U);
                }

                const auto replaced = additional_translate(
                    source, true,
                    MetadataCaptureTranslationConflictPolicy::ReplaceExisting);
                ASSERT_EQ(replaced.status, MetadataCaptureTranslationStatus::Ok);
                EXPECT_EQ(replaced.entries_updated, 1U);
                EXPECT_EQ(replaced.entries_removed, 0U);
                EXPECT_EQ(settings_active_count(source,
                                                kAdditionalTags[index]),
                          1U);
                native = settings_find(source, kAdditionalTags[index]);
                ASSERT_NE(native, nullptr);
                EXPECT_TRUE(any(native->flags, EntryFlags::Dirty));
                if (index == 3U || index == 6U || index == 8U) {
                    EXPECT_EQ(native->value.data.sr.numer, -7);
                    EXPECT_EQ(native->value.data.sr.denom, -1);
                } else {
                    EXPECT_EQ(native->value.data.ur.numer, 3U);
                    EXPECT_EQ(native->value.data.ur.denom, UINT32_MAX);
                }
            }
        }
    }

    TEST(MetadataAdditionalCapture,
         AlternateSourceAliasesFailWithoutChangingEitherOutput)
    {
        MetaStore source;
        settings_xmp(source, "FocalLengthIn35mmFilm", make_u16(35U));
        settings_xmp(source, "FocalLengthIn35mmFormat", make_u16(35U));
        additional_exact_native(source, 0U);
        source.finalize();
        MetaStore separate;
        settings_native(separate, 0x829aU, make_urational(1U, 100U));
        separate.finalize();
        const size_t source_count = source.entries().size();
        const auto separate_result = additional_translate_to(
            source, false,
            MetadataCaptureTranslationConflictPolicy::ReplaceExisting,
            kMetadataCaptureTranslationMaxOperations, &separate);
        EXPECT_EQ(separate_result.status,
                  MetadataCaptureTranslationStatus::AmbiguousSource);
        ASSERT_EQ(separate.entries().size(), 1U);
        ASSERT_NE(settings_find(separate, 0x829aU), nullptr);
        EXPECT_EQ(settings_find(separate, 0x829aU)->value.data.ur.denom, 100U);
        const auto alias_result = additional_translate(
            source, false,
            MetadataCaptureTranslationConflictPolicy::ReplaceExisting);
        EXPECT_EQ(alias_result.status,
                  MetadataCaptureTranslationStatus::AmbiguousSource);
        EXPECT_EQ(source.entries().size(), source_count);
        ASSERT_NE(settings_find(source, 0xa405U), nullptr);
        EXPECT_FALSE(any(settings_find(source, 0xa405U)->flags,
                         EntryFlags::Dirty));

        for (size_t index = 3U; index < kAdditionalTags.size(); ++index) {
            MetaStore environment_source;
            additional_xmp_group(environment_source, index, EntryFlags::Dirty,
                                 kSettingsNs);
            additional_xmp_group(environment_source, index, EntryFlags::Dirty,
                                 kSensitivityNs);
            additional_exact_native(environment_source, index);
            environment_source.finalize();
            MetaStore environment_output;
            settings_native(environment_output, 0x829aU,
                            make_urational(1U, 100U));
            environment_output.finalize();
            const size_t count = environment_source.entries().size();
            const auto failed_separate = additional_translate_to(
                environment_source, true,
                MetadataCaptureTranslationConflictPolicy::ReplaceExisting,
                kMetadataCaptureTranslationMaxOperations, &environment_output);
            EXPECT_EQ(failed_separate.status,
                      MetadataCaptureTranslationStatus::AmbiguousSource)
                << index;
            ASSERT_EQ(environment_output.entries().size(), 1U) << index;
            ASSERT_NE(settings_find(environment_output, 0x829aU), nullptr)
                << index;
            EXPECT_EQ(settings_find(environment_output, 0x829aU)
                          ->value.data.ur.denom,
                      100U)
                << index;
            const auto failed_alias = additional_translate(
                environment_source, true,
                MetadataCaptureTranslationConflictPolicy::ReplaceExisting);
            EXPECT_EQ(failed_alias.status,
                      MetadataCaptureTranslationStatus::AmbiguousSource)
                << index;
            EXPECT_EQ(environment_source.entries().size(), count) << index;
            const Entry* native = settings_find(environment_source,
                                                kAdditionalTags[index]);
            ASSERT_NE(native, nullptr) << index;
            EXPECT_FALSE(any(native->flags, EntryFlags::Dirty)) << index;
        }
    }

    TEST(MetadataAdditionalCapture,
         PreserveOmissionAndIneligibleSourcesKeepNativeOwners)
    {
        for (bool environment : { false, true }) {
            const size_t first = environment ? 3U : 0U;
            const size_t end   = environment ? 9U : 3U;
            MetaStore preserved;
            for (size_t index = first; index < end; ++index) {
                additional_xmp_group(preserved, index);
                additional_exact_native(preserved, index,
                                        EntryFlags::Derived);
            }
            preserved.finalize();
            MetadataCaptureTranslationResult preserve_result;
            if (environment) {
                MetadataEnvironmentTranslationOptions options;
                options.conflict_policy
                    = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
                preserve_result = translate_xmp_environment_metadata(
                    preserved, options, &preserved);
            } else {
                MetadataCaptureAdditionalTranslationOptions options;
                options.conflict_policy
                    = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
                preserve_result = translate_xmp_capture_additional_metadata(
                    preserved, options, &preserved);
            }
            ASSERT_EQ(preserve_result.status,
                      MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(preserve_result.groups_preserved, end - first);
            EXPECT_EQ(preserve_result.entries_updated, 0U);
            for (size_t index = first; index < end; ++index) {
                const Entry* native = settings_find(preserved,
                                                    kAdditionalTags[index]);
                ASSERT_NE(native, nullptr) << index;
                EXPECT_FALSE(any(native->flags, EntryFlags::Dirty)) << index;
                EXPECT_TRUE(any(native->flags, EntryFlags::Derived)) << index;
            }

            for (size_t omitted = first; omitted < end; ++omitted) {
                MetaStore source;
                for (size_t index = first; index < end; ++index) {
                    additional_xmp_group(source, index);
                    additional_exact_native(source, index);
                }
                source.finalize();
                MetadataCaptureTranslationResult omission_result;
                if (environment) {
                    MetadataEnvironmentTranslationOptions options;
                    switch (omitted) {
                    case 3U: options.temperature_to_exif = false; break;
                    case 4U: options.humidity_to_exif = false; break;
                    case 5U: options.pressure_to_exif = false; break;
                    case 6U: options.water_depth_to_exif = false; break;
                    case 7U: options.acceleration_to_exif = false; break;
                    case 8U:
                        options.camera_elevation_angle_to_exif = false;
                        break;
                    default: break;
                    }
                    omission_result = translate_xmp_environment_metadata(
                        source, options, &source);
                } else {
                    MetadataCaptureAdditionalTranslationOptions options;
                    switch (omitted) {
                    case 0U:
                        options.focal_length_in_35mm_film_to_exif = false;
                        break;
                    case 1U: options.file_source_to_exif = false; break;
                    case 2U: options.scene_type_to_exif = false; break;
                    default: break;
                    }
                    omission_result
                        = translate_xmp_capture_additional_metadata(
                            source, options, &source);
                }
                ASSERT_EQ(omission_result.status,
                          MetadataCaptureTranslationStatus::Ok)
                    << omitted;
                const Entry* untouched
                    = settings_find(source, kAdditionalTags[omitted]);
                ASSERT_NE(untouched, nullptr) << omitted;
                EXPECT_FALSE(any(untouched->flags, EntryFlags::Dirty))
                    << omitted;
            }

            MetaStore ineligible;
            for (size_t index = first; index < end; ++index) {
                additional_xmp_group(ineligible, index, EntryFlags::None);
                additional_exact_native(ineligible, index);
            }
            ineligible.finalize();
            MetadataCaptureTranslationResult ignored;
            if (environment)
                ignored = translate_xmp_environment_metadata(ineligible, {},
                                                            &ineligible);
            else
                ignored = translate_xmp_capture_additional_metadata(
                    ineligible, {}, &ineligible);
            ASSERT_EQ(ignored.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(ignored.source_properties, 0U);
            EXPECT_EQ(ignored.entries_updated, 0U);
            for (size_t index = first; index < end; ++index) {
                const Entry* native = settings_find(ineligible,
                                                    kAdditionalTags[index]);
                ASSERT_NE(native, nullptr) << index;
                EXPECT_FALSE(any(native->flags, EntryFlags::Dirty)) << index;
            }

            MetaStore clean_tombstones;
            for (size_t index = first; index < end; ++index) {
                additional_xmp_group(clean_tombstones, index,
                                     EntryFlags::Deleted);
                additional_exact_native(clean_tombstones, index);
            }
            clean_tombstones.finalize();
            if (environment) {
                MetadataEnvironmentTranslationOptions options;
                options.source_mode
                    = MetadataCaptureTranslationSourceMode::All;
                ignored = translate_xmp_environment_metadata(
                    clean_tombstones, options, &clean_tombstones);
            } else {
                MetadataCaptureAdditionalTranslationOptions options;
                options.source_mode = MetadataCaptureTranslationSourceMode::All;
                ignored = translate_xmp_capture_additional_metadata(
                    clean_tombstones, options, &clean_tombstones);
            }
            ASSERT_EQ(ignored.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(ignored.source_properties, 0U);
            EXPECT_EQ(ignored.entries_updated, 0U);
            for (size_t index = first; index < end; ++index) {
                const Entry* native = settings_find(clean_tombstones,
                                                    kAdditionalTags[index]);
                ASSERT_NE(native, nullptr) << index;
                EXPECT_FALSE(any(native->flags, EntryFlags::Dirty)) << index;
            }
        }
    }

    TEST(MetadataAdditionalCapture,
         CleanNativeTombstonesAreReusedForCompleteSourceDeletion)
    {
        constexpr EntryFlags deleted
            = EntryFlags::Dirty | EntryFlags::Deleted;
        for (bool environment : { false, true }) {
            const size_t first = environment ? 3U : 0U;
            const size_t end   = environment ? 9U : 3U;
            MetaStore source;
            for (size_t index = first; index < end; ++index) {
                additional_xmp_group(source, index, deleted);
                additional_exact_native(source, index, EntryFlags::Deleted);
            }
            source.finalize();
            MetadataCaptureTranslationResult translated;
            if (environment)
                translated = translate_xmp_environment_metadata(source, {},
                                                                &source);
            else
                translated = translate_xmp_capture_additional_metadata(
                    source, {}, &source);
            ASSERT_EQ(translated.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(translated.entries_added, 0U);
            EXPECT_EQ(translated.entries_updated, end - first);
            for (size_t index = first; index < end; ++index) {
                const auto ids = settings_native_history_ids(
                    source, kAdditionalTags[index]);
                ASSERT_EQ(ids.size(), 1U) << index;
                const Entry& entry = source.entry(ids.front());
                EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty)) << index;
                EXPECT_TRUE(any(entry.flags, EntryFlags::Deleted)) << index;
                EXPECT_EQ(entry.origin.wire_count, 1U) << index;
                if (index == 0U) {
                    EXPECT_EQ(entry.value.elem_type, MetaElementType::U16);
                    EXPECT_EQ(entry.value.data.u64, 35U);
                } else if (index == 1U || index == 2U) {
                    EXPECT_EQ(entry.value.kind, MetaValueKind::Bytes) << index;
                    EXPECT_EQ(entry.value.count, 1U) << index;
                    const auto bytes = source.arena().span(
                        entry.value.data.span);
                    ASSERT_EQ(bytes.size(), 1U) << index;
                    EXPECT_EQ(std::to_integer<uint8_t>(bytes[0]),
                              index == 1U ? 3U : 1U);
                } else if (index == 3U || index == 6U || index == 8U) {
                    EXPECT_EQ(entry.value.data.sr.numer, -7) << index;
                    EXPECT_EQ(entry.value.data.sr.denom, -1) << index;
                } else {
                    EXPECT_EQ(entry.value.data.ur.numer, 7U) << index;
                    EXPECT_EQ(entry.value.data.ur.denom, UINT32_MAX) << index;
                }
            }
            const auto repeated = environment
                                     ? translate_xmp_environment_metadata(
                                           source, {}, &source)
                                     : translate_xmp_capture_additional_metadata(
                                           source, {}, &source);
            ASSERT_EQ(repeated.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(repeated.entries_added, 0U);
            EXPECT_EQ(repeated.entries_updated, 0U);
        }
    }

    TEST(MetadataAdditionalCapture,
         PromotionAndDuplicateRemovalLimitsRollbackTransactionally)
    {
        for (bool environment : { false, true }) {
            const size_t first = environment ? 3U : 0U;
            const size_t end   = environment ? 9U : 3U;
            const uint32_t promotion_budget
                = static_cast<uint32_t>(end - first - 1U);
            MetaStore promotion;
            for (size_t index = first; index < end; ++index) {
                additional_xmp_group(promotion, index);
                additional_exact_native(promotion, index);
            }
            promotion.finalize();
            MetaStore separate;
            settings_native(separate, 0x829aU, make_urational(1U, 100U));
            separate.finalize();
            const size_t promotion_count = promotion.entries().size();
            const auto promotion_options = additional_translate_to(
                promotion, environment,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                promotion_budget, &separate);
            EXPECT_EQ(promotion_options.status,
                      MetadataCaptureTranslationStatus::OperationLimitExceeded)
                << environment;
            ASSERT_EQ(separate.entries().size(), 1U);
            ASSERT_NE(settings_find(separate, 0x829aU), nullptr);
            EXPECT_EQ(settings_find(separate, 0x829aU)->value.data.ur.denom,
                      100U);
            const auto promotion_alias = additional_translate(
                promotion, environment,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                promotion_budget);
            EXPECT_EQ(promotion_alias.status,
                      MetadataCaptureTranslationStatus::OperationLimitExceeded)
                << environment;
            EXPECT_EQ(promotion.entries().size(), promotion_count);
            for (size_t index = first; index < end; ++index) {
                const Entry* native = settings_find(promotion,
                                                    kAdditionalTags[index]);
                ASSERT_NE(native, nullptr) << index;
                EXPECT_FALSE(any(native->flags, EntryFlags::Dirty)) << index;
            }
            const uint32_t full_promotion_budget
                = static_cast<uint32_t>(end - first);
            const auto promoted = additional_translate(
                promotion, environment,
                MetadataCaptureTranslationConflictPolicy::FailOnConflict,
                full_promotion_budget);
            ASSERT_EQ(promoted.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(promoted.entries_updated, end - first);

            MetaStore duplicates;
            for (size_t index = first; index < end; ++index) {
                additional_xmp_group(duplicates, index);
                additional_exact_native(duplicates, index);
                additional_exact_native(duplicates, index);
            }
            duplicates.finalize();
            const size_t duplicate_count = duplicates.entries().size();
            const uint32_t duplicate_budget
                = static_cast<uint32_t>(2U * (end - first) - 1U);
            MetaStore duplicate_output;
            settings_native(duplicate_output, 0x829aU,
                            make_urational(1U, 100U));
            duplicate_output.finalize();
            const auto duplicate_separate = additional_translate_to(
                duplicates, environment,
                MetadataCaptureTranslationConflictPolicy::ReplaceExisting,
                duplicate_budget, &duplicate_output);
            EXPECT_EQ(duplicate_separate.status,
                      MetadataCaptureTranslationStatus::OperationLimitExceeded)
                << environment;
            ASSERT_EQ(duplicate_output.entries().size(), 1U);
            ASSERT_NE(settings_find(duplicate_output, 0x829aU), nullptr);
            EXPECT_EQ(settings_find(duplicate_output, 0x829aU)
                          ->value.data.ur.denom,
                      100U);
            const auto duplicate_alias = additional_translate(
                duplicates, environment,
                MetadataCaptureTranslationConflictPolicy::ReplaceExisting,
                duplicate_budget);
            EXPECT_EQ(duplicate_alias.status,
                      MetadataCaptureTranslationStatus::OperationLimitExceeded)
                << environment;
            EXPECT_EQ(duplicates.entries().size(), duplicate_count);
            for (size_t index = first; index < end; ++index) {
                EXPECT_EQ(settings_active_count(duplicates,
                                               kAdditionalTags[index]),
                          2U)
                    << index;
                for (EntryId id : settings_native_history_ids(
                         duplicates, kAdditionalTags[index]))
                    EXPECT_FALSE(any(duplicates.entry(id).flags,
                                     EntryFlags::Dirty))
                        << index;
            }
            const uint32_t full_duplicate_budget
                = static_cast<uint32_t>(2U * (end - first));
            const auto repaired = additional_translate(
                duplicates, environment,
                MetadataCaptureTranslationConflictPolicy::ReplaceExisting,
                full_duplicate_budget);
            ASSERT_EQ(repaired.status, MetadataCaptureTranslationStatus::Ok);
            EXPECT_EQ(repaired.entries_updated, end - first);
            EXPECT_EQ(repaired.entries_removed, end - first);
            for (size_t index = first; index < end; ++index)
                EXPECT_EQ(settings_active_count(duplicates,
                                                kAdditionalTags[index]),
                          1U)
                    << index;
        }
    }

    TEST(MetadataCaptureSync,
         SixteenApisRoundTripTogetherAndKeepCallTransactions)
    {
        const auto xml = test::kCaptureSyncXml;
        MetaStore source;
        ASSERT_EQ(decode_xmp_packet(
                      std::as_bytes(std::span(xml.data(), xml.size())), source)
                      .status,
                  XmpDecodeStatus::Ok);
        source.finalize();
        const size_t source_count = source.entries().size();
        MetaStore translated;
        ASSERT_EQ(translate_xmp_capture_metadata(
                      source,
                      { .source_mode
                        = MetadataCaptureTranslationSourceMode::All },
                      &translated)
                      .status,
                  MetadataCaptureTranslationStatus::Ok);
        const size_t partial_count = translated.entries().size();
        EXPECT_EQ(translate_xmp_sensitivity_metadata(
                      translated,
                      { .source_mode
                        = MetadataCaptureTranslationSourceMode::All },
                      &translated)
                      .status,
                  MetadataCaptureTranslationStatus::NativeConflict);
        EXPECT_EQ(translated.entries().size(), partial_count);
        ASSERT_TRUE(test::capture_sync_translate(translated));
        EXPECT_EQ(translated.entries().size(), source_count + 77U);
        EXPECT_EQ(source.entries().size(), source_count);
        ASSERT_TRUE(test::capture_sync_translate(translated, true));
        EXPECT_EQ(translated.entries().size(), source_count + 77U);
        for (bool existing : { false, true }) {
            for (const auto policy : { XmpConflictPolicy::CurrentBehavior,
                                       XmpConflictPolicy::ExistingWins,
                                       XmpConflictPolicy::GeneratedWins }) {
                XmpPortableOptions options;
                options.include_existing_xmp = existing;
                options.conflict_policy      = policy;
                options.existing_standard_namespace_policy
                    = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
                std::array<std::byte, 16384> bytes {};
                const auto dumped = dump_xmp_portable(translated, bytes,
                                                      options);
                ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                MetaStore restored;
                ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(),
                                                      dumped.written),
                                            restored)
                              .status,
                          XmpDecodeStatus::Ok);
                restored.finalize();
                ASSERT_TRUE(test::capture_sync_translate(restored, true));
                test::capture_sync_expect_native(restored, translated);
            }
        }
    }

    TEST(MetadataCaptureSync, NativeAndTypedXmpFractionsKeepExactBoundaries)
    {
        constexpr std::array<uint16_t, 3> tags = { 0x829dU, 0x920aU, 0xa404U };
        constexpr std::array<std::string_view, 3> names
            = { "FNumber", "FocalLength", "DigitalZoomRatio" };
        constexpr std::array<std::array<URational, 3>, 3> cases
            = { { { { { 17U, 6U }, { 50U, 3U }, { 1U, 3U } } },
                  { { { UINT32_MAX, UINT32_MAX - 1U },
                      { 1U, UINT32_MAX },
                      { 0U, 1U } } },
                  { { { 139U, 50U },
                      { UINT32_MAX, 1U },
                      { UINT32_MAX, UINT32_MAX - 1U } } } } };
        for (const auto& values : cases) {
            for (bool existing : { false, true }) {
                MetaStore source;
                for (size_t i = 0; i < tags.size(); ++i) {
                    const auto value = make_urational(values[i].numer,
                                                      values[i].denom);
                    if (existing)
                        settings_xmp(source, names[i], value);
                    else
                        settings_native(source, tags[i], value);
                }
                source.finalize();
                XmpPortableOptions options;
                options.include_existing_xmp = existing;
                std::array<std::byte, 4096> bytes {};
                const auto dumped = dump_xmp_portable(source, bytes, options);
                ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                const std::string_view packet(reinterpret_cast<const char*>(
                                                  bytes.data()),
                                              dumped.written);
                for (size_t i = 0; i < tags.size(); ++i)
                    EXPECT_NE(packet.find(
                                  "<exif:" + std::string(names[i]) + ">"
                                  + std::to_string(values[i].numer) + "/"
                                  + std::to_string(values[i].denom)
                                  + "</exif:" + std::string(names[i]) + ">"),
                              std::string_view::npos);
                MetaStore restored;
                ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(),
                                                      dumped.written),
                                            restored)
                              .status,
                          XmpDecodeStatus::Ok);
                restored.finalize();
                ASSERT_TRUE(test::capture_sync_translate_step(restored, 0U));
                ASSERT_TRUE(test::capture_sync_translate_step(restored, 2U));
                for (size_t i = 0; i < tags.size(); ++i) {
                    const Entry* entry = settings_find(restored, tags[i]);
                    ASSERT_NE(entry, nullptr);
                    EXPECT_EQ(entry->value.data.ur.numer, values[i].numer);
                    EXPECT_EQ(entry->value.data.ur.denom, values[i].denom);
                }
            }
        }
    }

    TEST(MetadataCaptureSync, InvalidNativeScalarsCannotClaimExistingFractions)
    {
        constexpr std::array<uint16_t, 3> tags = { 0x829dU, 0x920aU, 0xa404U };
        constexpr std::array<std::string_view, 3> names
            = { "FNumber", "FocalLength", "DigitalZoomRatio" };
        for (unsigned variant = 0U; variant < 8U; ++variant) {
            SCOPED_TRACE(variant);
            MetaStore source;
            for (size_t i = 0; i < tags.size(); ++i) {
                settings_xmp(source, names[i],
                             make_text(source.arena(), "7/3",
                                       TextEncoding::Ascii));
                MetaValue value = make_urational(2U, 1U);
                switch (variant) {
                case 0: value = make_u16(3U); break;
                case 1: value = make_urational(1U, 0U); break;
                case 2: value.count = 2U; break;
                case 3: {
                    const std::array<URational, 1> array
                        = { URational { 2U, 1U } };
                    value = make_urational_array(source.arena(), array);
                    break;
                }
                case 4: value = make_urational(0U, i == 2U ? 0U : 1U); break;
                case 5: value = make_srational(2, 1); break;
                case 6:
                    value = make_text(source.arena(), "2", TextEncoding::Ascii);
                    break;
                default: value.count = 0U; break;
                }
                settings_native(source, tags[i], value);
            }
            source.finalize();
            for (const auto policy : { XmpConflictPolicy::CurrentBehavior,
                                       XmpConflictPolicy::ExistingWins,
                                       XmpConflictPolicy::GeneratedWins }) {
                for (const auto standard :
                     { XmpExistingStandardNamespacePolicy::PreserveAll,
                       XmpExistingStandardNamespacePolicy::CanonicalizeManaged }) {
                    XmpPortableOptions options;
                    options.include_existing_xmp               = true;
                    options.conflict_policy                    = policy;
                    options.existing_standard_namespace_policy = standard;
                    std::array<std::byte, 4096> bytes {};
                    const auto dumped = dump_xmp_portable(source, bytes,
                                                          options);
                    ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                    const std::string_view packet(reinterpret_cast<const char*>(
                                                      bytes.data()),
                                                  dumped.written);
                    for (const auto name : names)
                        EXPECT_NE(packet.find("<exif:" + std::string(name)
                                              + ">7/3</exif:"
                                              + std::string(name) + ">"),
                                  std::string_view::npos);
                }
            }
        }
    }

    TEST(MetadataCaptureSync,
         ManagedSensitivityReconcilesLegacyBasesAndCompanions)
    {
        for (const std::string_view base :
             { "ISO", "ISOSpeedRatings", "ISO[1]", "ISOSpeedRatings[1]" }) {
            for (const auto policy : { XmpConflictPolicy::CurrentBehavior,
                                       XmpConflictPolicy::ExistingWins,
                                       XmpConflictPolicy::GeneratedWins }) {
                MetaStore source;
                for (size_t i = 0U; i < kSensitivityTags.size(); ++i) {
                    settings_xmp(source, i == 0U ? base : kSensitivityNames[i],
                                 make_u32(i == 1U ? 7U : 100U));
                    settings_native(source, kSensitivityTags[i],
                                    i < 2U ? make_u16(i == 1U ? 7U : 400U)
                                           : make_u32(400U));
                }
                source.finalize();
                for (bool canonical : { false, true }) {
                    XmpPortableOptions options;
                    options.include_existing_xmp = true;
                    options.conflict_policy      = policy;
                    if (canonical)
                        options.existing_standard_namespace_policy
                            = XmpExistingStandardNamespacePolicy::
                                CanonicalizeManaged;
                    std::array<std::byte, 8192> bytes {};
                    const auto dumped = dump_xmp_portable(source, bytes,
                                                          options);
                    ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                    MetaStore restored;
                    ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(),
                                                          dumped.written),
                                                restored)
                                  .status,
                              XmpDecodeStatus::Ok);
                    restored.finalize();
                    const auto result = translate_xmp_sensitivity_metadata(
                        restored,
                        { .source_mode
                          = MetadataCaptureTranslationSourceMode::All },
                        &restored);
                    ASSERT_EQ(
                        result.status,
                        canonical
                            ? MetadataCaptureTranslationStatus::Ok
                            : MetadataCaptureTranslationStatus::AmbiguousSource);
                    if (canonical) {
                        for (size_t i = 0; i < kSensitivityTags.size(); ++i) {
                            const Entry* entry
                                = settings_find(restored, kSensitivityTags[i]);
                            ASSERT_NE(entry, nullptr);
                            EXPECT_EQ(entry->value.data.u64,
                                      i == 1U ? 7U : 400U);
                        }
                    }
                }
            }
        }
    }

    TEST(MetadataCaptureSync,
         InvalidOrAbsentSensitivityReplacementRetainsSource)
    {
        for (unsigned variant = 0U; variant < 8U; ++variant) {
            SCOPED_TRACE(variant);
            MetaStore source;
            for (size_t i = 0; i < kSensitivityTags.size(); ++i) {
                settings_xmp(source,
                             i == 0U ? "ISOSpeedRatings[1]"
                                     : kSensitivityNames[i],
                             make_u32(i == 1U ? 7U : 100U));
                if (variant == 0U)
                    continue;
                MetaValue value = i < 2U ? make_u16(i == 1U ? 7U : 400U)
                                         : make_u32(400U);
                switch (variant) {
                case 1: value = make_u64(400U); break;
                case 2: value.count = 2U; break;
                case 3: {
                    const std::array<uint16_t, 1> array = { 400U };
                    value = make_u16_array(source.arena(), array);
                    break;
                }
                case 4: value.data.u64 = i == 1U ? 8U : 0U; break;
                case 5: value.count = 0U; break;
                case 6: value.data.u64 = i < 2U ? 65536U : UINT64_MAX; break;
                default: break;
                }
                settings_native(source, kSensitivityTags[i], value);
            }
            source.finalize();
            for (const auto policy : { XmpConflictPolicy::CurrentBehavior,
                                       XmpConflictPolicy::ExistingWins,
                                       XmpConflictPolicy::GeneratedWins }) {
                XmpPortableOptions options;
                options.include_exif         = variant != 7U;
                options.include_existing_xmp = true;
                options.conflict_policy      = policy;
                options.existing_standard_namespace_policy
                    = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
                std::array<std::byte, 8192> bytes {};
                const auto dumped = dump_xmp_portable(source, bytes, options);
                ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                MetaStore restored;
                ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(),
                                                      dumped.written),
                                            restored)
                              .status,
                          XmpDecodeStatus::Ok);
                restored.finalize();
                const auto result = translate_xmp_sensitivity_metadata(
                    restored,
                    { .source_mode = MetadataCaptureTranslationSourceMode::All },
                    &restored);
                ASSERT_EQ(result.status, MetadataCaptureTranslationStatus::Ok);
                for (size_t i = 0; i < kSensitivityTags.size(); ++i) {
                    const Entry* entry = settings_find(restored,
                                                       kSensitivityTags[i]);
                    ASSERT_NE(entry, nullptr);
                    EXPECT_EQ(entry->value.data.u64, i == 1U ? 7U : 100U);
                }
            }
        }
    }

    TEST(MetadataCaptureSync, ManagedBaseReplacementWorksWithoutSensitivityType)
    {
        MetaStore source;
        settings_xmp(source, "PhotographicSensitivity", make_u32(100U),
                     EntryFlags::None, kSensitivityNs);
        settings_native(source, 0x8827U, make_u16(400U));
        source.finalize();
        XmpPortableOptions options;
        options.include_existing_xmp = true;
        options.conflict_policy      = XmpConflictPolicy::ExistingWins;
        options.existing_standard_namespace_policy
            = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
        std::array<std::byte, 4096> bytes {};
        const auto dumped = dump_xmp_portable(source, bytes, options);
        ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
        const std::string_view packet(reinterpret_cast<const char*>(
                                          bytes.data()),
                                      dumped.written);
        EXPECT_EQ(packet.find("<exifEX:PhotographicSensitivity>"),
                  std::string_view::npos);
        EXPECT_NE(packet.find("<exif:ISO>400</exif:ISO>"),
                  std::string_view::npos);
        MetaStore restored;
        ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(), dumped.written),
                                    restored)
                      .status,
                  XmpDecodeStatus::Ok);
        restored.finalize();
        ASSERT_TRUE(test::capture_sync_translate_step(restored, 0U));
        ASSERT_NE(settings_find(restored, 0x8827U), nullptr);
        EXPECT_EQ(settings_find(restored, 0x8827U)->value.data.u64, 400U);
    }

    TEST(MetadataApex, MalformedNativeValuesDoNotReplaceExistingManagedXmp)
    {
        for (unsigned variant = 0U; variant < 4U; ++variant) {
            MetaStore source = apex_source();
            for (size_t i = 0U; i < kApexTags.size(); ++i) {
                MetaValue value;
                if (variant == 0U)
                    value = make_u32(3U);
                else if (variant == 1U)
                    value = i == 1U || i == 4U ? make_urational(2U, 0U)
                                               : make_srational(2, -1);
                else if (variant == 2U) {
                    value       = i == 1U || i == 4U ? make_urational(2U, 1U)
                                                     : make_srational(2, 1);
                    value.count = 2U;
                } else {
                    const std::array<URational, 1> ur = { URational { 2U, 1U } };
                    const std::array<SRational, 1> sr = { SRational { 2, 1 } };
                    value                             = i == 1U || i == 4U
                                                            ? make_urational_array(source.arena(), ur)
                                                            : make_srational_array(source.arena(), sr);
                }
                settings_native(source, kApexTags[i], value);
            }
            source.finalize();
            for (bool existing : { false, true }) {
                XmpPortableOptions options;
                options.include_existing_xmp = existing;
                options.existing_standard_namespace_policy
                    = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
                std::array<std::byte, 8192> bytes {};
                const auto dumped = dump_xmp_portable(source, bytes, options);
                ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                const std::string_view xml(reinterpret_cast<const char*>(
                                               bytes.data()),
                                           dumped.written);
                for (size_t i = 0U; i < kApexPaths.size(); ++i) {
                    const std::string name(i == 3U ? "ExposureCompensation"
                                                   : kApexPaths[i]);
                    if (existing)
                        EXPECT_NE(xml.find("<exif:" + name + ">"
                                           + std::string(kApexText[i])
                                           + "</exif:" + name + ">"),
                                  std::string_view::npos);
                    else
                        EXPECT_EQ(xml.find("<exif:" + name + ">"),
                                  std::string_view::npos);
                }
            }
        }
    }
}  // namespace
}  // namespace openmeta

namespace openmeta {
namespace {
    constexpr std::string_view kEncodingExif = "http://ns.adobe.com/exif/1.0/";
    constexpr std::string_view kEncodingCipa = "http://cipa.jp/exif/1.0/";
    constexpr auto kEncodingAll = MetadataCaptureTranslationSourceMode::All;
    constexpr auto kEncodingReplace
        = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
    constexpr auto kEncodingOk = MetadataCaptureTranslationStatus::Ok;

    static MetaStore encoding_source()
    {
        MetaStore store;
        const auto xml = test::kCaptureSyncXml;
        EXPECT_EQ(decode_xmp_packet(
                      std::as_bytes(std::span(xml.data(), xml.size())), store)
                      .status,
                  XmpDecodeStatus::Ok);
        store.finalize();
        return store;
    }

    static void encoding_change(MetaStore& store, std::string_view path,
                                std::string_view text, bool remove = false,
                                std::string_view ns = kEncodingCipa)
    {
        const auto ids = store.find_all(make_xmp_property_key_view(ns, path));
        ASSERT_EQ(ids.size(), 1U) << path;
        MetaEdit edit;
        if (remove)
            edit.tombstone(ids[0]);
        else
            edit.set_value(ids[0],
                           make_text(edit.arena(), text, TextEncoding::Utf8));
        store = commit(store, std::span(&edit, 1U));
    }

    static std::string encoding_packet(const MetaStore& store,
                                       bool native = true)
    {
        XmpPortableOptions options;
        options.include_exif         = native;
        options.include_existing_xmp = !native;
        options.include_iptc         = false;
        std::array<std::byte, 32768> buffer {};
        const auto result = dump_xmp_portable(store, buffer, options);
        EXPECT_EQ(result.status, XmpDumpStatus::Ok);
        return std::string(reinterpret_cast<const char*>(buffer.data()),
                           result.written);
    }

    static void
    encoding_expect_rollback(MetaStore& source, bool composite,
                             const MetadataCompositeTranslationOptions& options
                             = { .source_mode = kEncodingAll })
    {
        const Entry* entries = source.entries().data();
        const auto raw       = source.arena().bytes();
        const std::vector<std::byte> before(raw.begin(), raw.end());
        const auto result
            = composite
                  ? translate_xmp_composite_metadata(source, options, &source)
                  : translate_xmp_image_encoding_metadata(
                        source, { .source_mode = kEncodingAll }, &source);
        EXPECT_NE(result.status, kEncodingOk);
        EXPECT_EQ(source.entries().data(), entries);
        ASSERT_EQ(source.arena().bytes().size(), before.size());
        EXPECT_EQ(std::memcmp(source.arena().bytes().data(), before.data(),
                              before.size()),
                  0);
    }

    TEST(MetadataImageEncoding,
         ExactRationalsComponentsAndNativeOnlyPortableRoundTrip)
    {
        MetaStore source = encoding_source();
        MetaStore output;
        EXPECT_EQ(translate_xmp_image_encoding_metadata(source, {}, &output)
                      .source_properties,
                  0U);
        const auto result = translate_xmp_image_encoding_metadata(
            source, { .source_mode = kEncodingAll }, &output);
        ASSERT_EQ(result.status, kEncodingOk);
        EXPECT_EQ(result.entries_added, 3U);
        const Entry* gamma      = active_exif_entry(output, "exififd", 0xa500U);
        const Entry* bits       = active_exif_entry(output, "exififd", 0x9102U);
        const Entry* components = active_exif_entry(output, "exififd", 0x9101U);
        ASSERT_NE(gamma, nullptr);
        ASSERT_NE(bits, nullptr);
        ASSERT_NE(components, nullptr);
        EXPECT_EQ(gamma->value.data.ur.numer, 11U);
        EXPECT_EQ(gamma->value.data.ur.denom, 5U);
        EXPECT_EQ(bits->value.data.ur.numer, 7U);
        EXPECT_EQ(bits->value.data.ur.denom, 3U);
        EXPECT_EQ(components->value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(components->value.count, 4U);
        const auto bytes = output.arena().span(components->value.data.span);
        const std::array<std::byte, 4> expected = {
            std::byte { 1 }, std::byte { 2 }, std::byte { 3 }, std::byte { 0 }
        };
        EXPECT_EQ(std::memcmp(bytes.data(), expected.data(), 4U), 0);
        const std::string xml = encoding_packet(output);
        EXPECT_NE(xml.find("<exifEX:Gamma>11/5</exifEX:Gamma>"),
                  std::string::npos);
        EXPECT_NE(
            xml.find(
                "<exif:CompressedBitsPerPixel>7/3</exif:CompressedBitsPerPixel>"),
            std::string::npos);
        EXPECT_NE(xml.find("xmlns:exifEX=\"http://cipa.jp/exif/1.0/\""),
                  std::string::npos);
        MetaStore restored;
        ASSERT_EQ(decode_xmp_packet(std::as_bytes(
                                        std::span(xml.data(), xml.size())),
                                    restored)
                      .status,
                  XmpDecodeStatus::Ok);
        restored.finalize();
        ASSERT_EQ(translate_xmp_image_encoding_metadata(
                      restored, { .source_mode = kEncodingAll }, &restored)
                      .status,
                  kEncodingOk);
        EXPECT_EQ(translate_xmp_image_encoding_metadata(
                      restored, { .source_mode = kEncodingAll }, &restored)
                      .entries_added,
                  0U);
        EXPECT_TRUE(validate_store(restored).ok());
    }

    TEST(MetadataImageEncoding, InvalidBatchRollsBackAndPreservesOriginalOutput)
    {
        for (std::string_view value :
             { "-1", "1/0", "nan", "2.2 gamma", "2 mm", "1/4294967296" }) {
            MetaStore source = encoding_source();
            encoding_change(source, "Gamma", value);
            encoding_expect_rollback(source, false);
        }
        for (std::string_view value : { "7", "-1", "1/2", "65536" }) {
            MetaStore source = encoding_source();
            encoding_change(source, "ComponentsConfiguration[4]", value, false,
                            kEncodingExif);
            encoding_expect_rollback(source, false);
        }
        MetaStore source = encoding_source();
        encoding_change(source, "ComponentsConfiguration[4]", "", true,
                        kEncodingExif);
        encoding_expect_rollback(source, false);
    }

    TEST(MetadataImageEncoding, TypedArraysZeroValuesAliasesAndLimits)
    {
        MetaStore fresh;
        constexpr std::array<uint16_t, 4> codes = { 4U, 5U, 6U, 0U };
        const std::array<MetadataAuthoringEntry, 3> authored = {
            { { make_xmp_property_key_view(kEncodingCipa, "Gamma"),
                make_value_view_urational(0U, 7U) },
              { make_xmp_property_key_view(kEncodingExif,
                                           "CompressedBitsPerPixel"),
                make_value_view_urational(UINT32_MAX, UINT32_MAX) },
              { make_xmp_property_key_view(kEncodingExif,
                                           "ComponentsConfiguration"),
                make_value_view_array(MetaElementType::U16,
                                      std::as_bytes(std::span(codes)), 4U) } }
        };
        ASSERT_TRUE(create_metadata_store(authored, &fresh).ok());
        ASSERT_EQ(
            translate_xmp_image_encoding_metadata(fresh, {}, &fresh).status,
            kEncodingOk);
        EXPECT_TRUE(validate_store(fresh).ok());
        const auto gamma = active_exif_entry(fresh, "exififd", 0xa500U);
        ASSERT_NE(gamma, nullptr);
        EXPECT_EQ(gamma->value.data.ur.numer, 0U);
        MetaStore bounded = encoding_source();
        MetaStore output;
        auto result = translate_xmp_image_encoding_metadata(
            bounded, { .source_mode = kEncodingAll, .max_added_entries = 2U },
            &output);
        EXPECT_EQ(result.status,
                  MetadataCaptureTranslationStatus::EntryLimitExceeded);
        EXPECT_TRUE(output.entries().empty());
        result = translate_xmp_image_encoding_metadata(
            bounded, { .source_mode = kEncodingAll, .max_operations = 2U },
            &output);
        EXPECT_EQ(result.status,
                  MetadataCaptureTranslationStatus::OperationLimitExceeded);
        result = translate_xmp_image_encoding_metadata(
            bounded,
            { .source_mode = kEncodingAll, .max_total_text_bytes = 1U },
            &output);
        EXPECT_EQ(result.status,
                  MetadataCaptureTranslationStatus::SourceLimitExceeded);
        MetaEdit duplicate;
        Entry alias;
        alias.key   = make_xmp_property_key(duplicate.arena(), kEncodingExif,
                                            "Gamma");
        alias.value = make_u16(2U);
        alias.flags = EntryFlags::Dirty;
        duplicate.add_entry(alias);
        bounded = commit(bounded, std::span(&duplicate, 1U));
        encoding_expect_rollback(bounded, false);
    }

    TEST(MetadataComposite, StructuredSequencesUnknownSummariesAndRoundTrip)
    {
        MetaStore source = encoding_source();
        MetaStore output;
        EXPECT_EQ(translate_xmp_composite_metadata(source, {}, &output)
                      .source_properties,
                  0U);
        const auto result = translate_xmp_composite_metadata(
            source, { .source_mode = kEncodingAll }, &output);
        ASSERT_EQ(result.status, kEncodingOk);
        EXPECT_EQ(result.entries_added, 3U);
        EXPECT_EQ(result.groups_translated, 1U);
        EXPECT_TRUE(validate_store(output).ok());
        const Entry* entry = active_exif_entry(output, "exififd", 0xa462U);
        ASSERT_NE(entry, nullptr);
        ASSERT_EQ(entry->value.kind, MetaValueKind::Bytes);
        const auto raw = output.arena().span(entry->value.data.span);
        ASSERT_EQ(raw.size(), 92U);
        EXPECT_EQ(raw[56], std::byte { 2 });
        EXPECT_EQ(raw[58], std::byte { 2 });
        for (size_t i = 16U; i < 24U; ++i)
            EXPECT_EQ(raw[i], std::byte { 0 });
        const std::string xml = encoding_packet(output);
        EXPECT_NE(
            xml.find(
                "<exifEX:SourceExposureTimesOfCompositeImage rdf:parseType=\"Resource\">"),
            std::string::npos);
        EXPECT_NE(
            xml.find(
                "<exifEX:SumOfExposureTimesOfUsed>0/0</exifEX:SumOfExposureTimesOfUsed>"),
            std::string::npos);
        MetaStore restored;
        ASSERT_EQ(decode_xmp_packet(std::as_bytes(
                                        std::span(xml.data(), xml.size())),
                                    restored)
                      .status,
                  XmpDecodeStatus::Ok);
        restored.finalize();
        ASSERT_EQ(translate_xmp_composite_metadata(
                      restored, { .source_mode = kEncodingAll }, &restored)
                      .status,
                  kEncodingOk);
        const Entry* reread = active_exif_entry(restored, "exififd", 0xa462U);
        ASSERT_NE(reread, nullptr);
        EXPECT_EQ(
            std::memcmp(restored.arena().span(reread->value.data.span).data(),
                        raw.data(), raw.size()),
            0);
        EXPECT_EQ(translate_xmp_composite_metadata(
                      output, { .source_mode = kEncodingAll }, &output)
                      .groups_unchanged,
                  1U);
    }

    TEST(MetadataComposite, UnavailableSequenceListAndUsedCountRemainExplicit)
    {
        MetaStore source = encoding_source();
        encoding_change(source, "SourceImageNumberOfCompositeImage[2]", "0");
        encoding_change(source,
                        "SourceExposureTimesOfCompositeImage/NumberOfSequences",
                        "0");
        encoding_change(
            source,
            "SourceExposureTimesOfCompositeImage/NumberOfImagesInSequences", "",
            true);
        for (unsigned i = 1U; i <= 4U; ++i)
            encoding_change(source,
                            "SourceExposureTimesOfCompositeImage/Values["
                                + std::to_string(i) + "]",
                            "", true);
        ASSERT_EQ(translate_xmp_composite_metadata(
                      source, { .source_mode = kEncodingAll }, &source)
                      .status,
                  kEncodingOk);
        EXPECT_TRUE(validate_store(source).ok());
        const Entry* entry = active_exif_entry(source, "exififd", 0xa462U);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->value.count, 58U);
        const std::string portable = encoding_packet(source);
        EXPECT_NE(portable.find(
                      "<exifEX:NumberOfSequences>0</exifEX:NumberOfSequences>"),
                  std::string::npos);
        EXPECT_EQ(portable.find("<exifEX:Values>"), std::string::npos);
    }

    TEST(MetadataComposite, IncompleteInvalidAndAliasedGroupsRollback)
    {
        const std::array<std::pair<std::string_view, std::string_view>, 10> bad = {
            { { "CompositeImage", "4" },
              { "CompositeImage", "1" },
              { "SourceImageNumberOfCompositeImage[1]", "1" },
              { "SourceImageNumberOfCompositeImage[1]", "3" },
              { "SourceImageNumberOfCompositeImage[2]", "1" },
              { "SourceImageNumberOfCompositeImage[2]", "5" },
              { "SourceExposureTimesOfCompositeImage/NumberOfImagesInSequences",
                "1" },
              { "SourceExposureTimesOfCompositeImage/TotalExposurePeriod",
                "1/0" },
              { "SourceExposureTimesOfCompositeImage/Values[4]", "0/0" },
              { "SourceExposureTimesOfCompositeImage/Values[4]", "-1/3" } }
        };
        for (const auto& item : bad) {
            SCOPED_TRACE(item.first);
            MetaStore source = encoding_source();
            encoding_change(source, item.first, item.second);
            encoding_expect_rollback(source, true);
        }
        for (std::string_view path :
             { "CompositeImage", "SourceImageNumberOfCompositeImage[1]",
               "SourceExposureTimesOfCompositeImage/TotalExposurePeriod",
               "SourceExposureTimesOfCompositeImage/Values[4]" }) {
            MetaStore source = encoding_source();
            encoding_change(source, path, "", true);
            encoding_expect_rollback(source, true);
        }
        MetaStore source = encoding_source();
        MetaEdit edit;
        Entry alias;
        alias.key   = make_xmp_property_key(edit.arena(), kEncodingCipa,
                                            "CompositeImageCount[1]");
        alias.value = make_u16(4U);
        alias.flags = EntryFlags::Dirty;
        edit.add_entry(alias);
        source = commit(source, std::span(&edit, 1U));
        encoding_expect_rollback(source, true);
    }

    TEST(MetadataComposite, GroupConflictPolicyLimitsDirtySelectionAndDeletion)
    {
        MetaStore source = encoding_source();
        ASSERT_EQ(translate_xmp_composite_metadata(
                      source, { .source_mode = kEncodingAll }, &source)
                      .status,
                  kEncodingOk);
        encoding_change(
            source, "SourceExposureTimesOfCompositeImage/TotalExposurePeriod",
            "2");
        MetaStore output;
        EXPECT_EQ(translate_xmp_composite_metadata(source, {}, &output).status,
                  MetadataCaptureTranslationStatus::NativeConflict);
        EXPECT_TRUE(output.entries().empty());
        EXPECT_EQ(
            translate_xmp_composite_metadata(
                source,
                { .conflict_policy
                  = MetadataCaptureTranslationConflictPolicy::PreserveExisting },
                &output)
                .groups_preserved,
            1U);
        EXPECT_EQ(translate_xmp_composite_metadata(
                      source, { .conflict_policy = kEncodingReplace }, &source)
                      .groups_translated,
                  1U);
        MetaStore bounded = encoding_source();
        encoding_expect_rollback(bounded, true,
                                 { .source_mode       = kEncodingAll,
                                   .max_added_entries = 2U });
        encoding_expect_rollback(bounded, true,
                                 { .source_mode    = kEncodingAll,
                                   .max_operations = 2U });
        encoding_expect_rollback(bounded, true,
                                 { .source_mode          = kEncodingAll,
                                   .max_total_text_bytes = 1U });
        encoding_expect_rollback(bounded, true,
                                 { .source_mode         = kEncodingAll,
                                   .max_exposure_values = 3U });
        MetaEdit edit;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind != MetaKeyKind::XmpProperty)
                continue;
            const auto bytes = source.arena().span(
                entry.key.data.xmp_property.property_path);
            const std::string_view path(reinterpret_cast<const char*>(
                                            bytes.data()),
                                        bytes.size());
            if (path == "CompositeImage"
                || path.starts_with("SourceImageNumberOfCompositeImage")
                || path.starts_with("SourceExposureTimesOfCompositeImage"))
                edit.tombstone(id);
        }
        source = commit(source, std::span(&edit, 1U));
        ASSERT_EQ(translate_xmp_composite_metadata(
                      source, { .conflict_policy = kEncodingReplace }, &source)
                      .status,
                  kEncodingOk);
        EXPECT_EQ(active_exif_entry(source, "exififd", 0xa460U), nullptr);
        EXPECT_EQ(active_exif_entry(source, "exififd", 0xa461U), nullptr);
        EXPECT_EQ(active_exif_entry(source, "exififd", 0xa462U), nullptr);
    }

    TEST(MetadataComposite, TypedNativeGroupEditingRejectsBrokenRelationships)
    {
        MetaStore source = encoding_source();
        ASSERT_EQ(translate_xmp_composite_metadata(
                      source, { .source_mode = kEncodingAll }, &source)
                      .status,
                  kEncodingOk);
        MetadataTypedEditingOperation operation;
        operation.kind        = MetadataEditingOperationKind::Set;
        operation.entry.key   = make_exif_tag_key_view("exififd", 0xa460U);
        operation.entry.value = make_value_view_u16(1U);
        const Entry* before   = source.entries().data();
        EXPECT_FALSE(
            edit_metadata_typed(source, std::span(&operation, 1U), &source)
                .ok());
        EXPECT_EQ(source.entries().data(), before);
        std::array<MetadataTypedEditingOperation, 3> operations {};
        operations[0] = operation;
        for (size_t i = 1U; i < 3U; ++i) {
            operations[i].kind = MetadataEditingOperationKind::Remove;
            operations[i].entry.key
                = make_exif_tag_key_view("exififd",
                                         static_cast<uint16_t>(0xa460U + i));
        }
        ASSERT_TRUE(edit_metadata_typed(source, operations, &source).ok());
        EXPECT_TRUE(validate_store(source).ok());
        for (const auto policy : { XmpConflictPolicy::CurrentBehavior,
                                   XmpConflictPolicy::ExistingWins,
                                   XmpConflictPolicy::GeneratedWins }) {
            XmpPortableOptions options;
            options.include_existing_xmp = true;
            options.conflict_policy      = policy;
            options.existing_standard_namespace_policy
                = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
            std::array<std::byte, 32768> bytes {};
            const auto dumped = dump_xmp_portable(source, bytes, options);
            ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
            const std::string_view xml(reinterpret_cast<const char*>(
                                           bytes.data()),
                                       dumped.written);
            EXPECT_NE(xml.find(
                          "<exifEX:CompositeImage>1</exifEX:CompositeImage>"),
                      std::string_view::npos);
            EXPECT_EQ(xml.find("SourceImageNumberOfCompositeImage"),
                      std::string_view::npos);
            EXPECT_EQ(xml.find("SourceExposureTimesOfCompositeImage"),
                      std::string_view::npos);
        }
    }

    TEST(MetadataComposite, InvalidNativeCannotSuppressValidSourceStructure)
    {
        MetaStore source = encoding_source();
        MetaEdit edit;
        Entry entry;
        entry.key   = make_exif_tag_key(edit.arena(), "exififd", 0xa460U);
        entry.value = make_u16(3U);
        edit.add_entry(entry);
        source = commit(source, std::span(&edit, 1U));
        EXPECT_FALSE(validate_store(source).ok());
        XmpPortableOptions options;
        options.include_existing_xmp = true;
        options.existing_standard_namespace_policy
            = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
        std::array<std::byte, 32768> buffer {};
        const auto result = dump_xmp_portable(source, buffer, options);
        ASSERT_EQ(result.status, XmpDumpStatus::Ok);
        MetaStore restored;
        ASSERT_EQ(decode_xmp_packet(std::span(buffer.data(), result.written),
                                    restored)
                      .status,
                  XmpDecodeStatus::Ok);
        restored.finalize();
        ASSERT_EQ(translate_xmp_composite_metadata(
                      restored, { .source_mode = kEncodingAll }, &restored)
                      .status,
                  kEncodingOk);
        EXPECT_TRUE(validate_store(restored).ok());
    }
    TEST(MetadataComposite, BinaryBoundsByteOrderAndTypedReplacement)
    {
        MetaStore source = encoding_source();
        ASSERT_TRUE(test::capture_sync_translate(source));
        const Entry* exposure = active_exif_entry(source, "exififd", 0xa462U);
        ASSERT_NE(exposure, nullptr);
        const auto payload = source.arena().span(exposure->value.data.span);
        const std::vector<std::byte> little(payload.begin(), payload.end());
        const std::array<size_t, 8> sizes = { 0U,  55U, 57U, 58U,
                                              59U, 60U, 91U, 93U };
        for (size_t size : sizes) {
            SCOPED_TRACE(size);
            auto bad = little;
            bad.resize(size);
            MetadataTypedEditingOperation operation;
            operation.kind        = MetadataEditingOperationKind::Set;
            operation.entry.key   = make_exif_tag_key_view("exififd", 0xa462U);
            operation.entry.value = make_value_view_bytes(bad);
            const auto* before    = source.entries().data();
            EXPECT_FALSE(
                edit_metadata_typed(source, std::span(&operation, 1U), &source)
                    .ok());
            EXPECT_EQ(source.entries().data(), before);
        }
        for (size_t offset : { 4U, 56U, 58U, 64U }) {
            auto bad    = little;
            bad[offset] = std::byte { 0 };
            MetadataTypedEditingOperation operation;
            operation.kind        = MetadataEditingOperationKind::Set;
            operation.entry.key   = make_exif_tag_key_view("exififd", 0xa462U);
            operation.entry.value = make_value_view_bytes(bad);
            EXPECT_FALSE(
                edit_metadata_typed(source, std::span(&operation, 1U), &source)
                    .ok());
        }
        auto big = little;
        for (size_t off = 0U; off < big.size();) {
            const size_t width = off == 56U || off == 58U ? 2U : 4U;
            std::reverse(big.begin() + off, big.begin() + off + width);
            off += width;
        }
        MetaEdit edit;
        const auto ids = source.find_all(
            make_exif_tag_key_view("exififd", 0xa462U));
        ASSERT_EQ(ids.size(), 1U);
        edit.tombstone(ids[0]);
        Entry entry;
        entry.key   = make_exif_tag_key(edit.arena(), "exififd", 0xa462U);
        entry.value = make_bytes(edit.arena(), big);
        entry.flags = EntryFlags::ValueBigEndian;
        edit.add_entry(entry);
        source = commit(source, std::span(&edit, 1U));
        ASSERT_TRUE(validate_store(source).ok());
        // Portable and canonical EXIF serialization read byte order without
        // changing the raw source value exposed to the host.
        const std::string xml = encoding_packet(source);
        EXPECT_NE(
            xml.find(
                "<exifEX:TotalExposurePeriod>5/3</exifEX:TotalExposurePeriod>"),
            std::string::npos);
        const auto measured = serialize_exif_tiff(source, {});
        ASSERT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> tiff(measured.needed);
        ASSERT_TRUE(serialize_exif_tiff(source, tiff).ok());
        std::array<ExifIfdRef, 16> ifds {};
        MetaStore decoded;
        ASSERT_EQ(decode_exif_tiff(tiff, decoded, ifds, {}).status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        test::capture_sync_expect_native(decoded, source);
        const auto original = active_exif_entry(source, "exififd", 0xa462U);
        ASSERT_NE(original, nullptr);
        const auto unchanged = source.arena().span(original->value.data.span);
        EXPECT_EQ(std::vector<std::byte>(unchanged.begin(), unchanged.end()),
                  big);
        MetadataTypedEditingOperation replacement;
        replacement.kind        = MetadataEditingOperationKind::Set;
        replacement.entry.key   = make_exif_tag_key_view("exififd", 0xa462U);
        replacement.entry.value = make_value_view_bytes(little);
        ASSERT_TRUE(
            edit_metadata_typed(source, std::span(&replacement, 1U), &source)
                .ok());
        EXPECT_FALSE(any(active_exif_entry(source, "exififd", 0xa462U)->flags,
                         EntryFlags::ValueBigEndian));
        EXPECT_TRUE(validate_store(source).ok());
    }

    TEST(MetadataComposite, ExplicitAliasesAndBoundedTypedExposureArrays)
    {
        constexpr std::array<std::string_view, 7> summaries
            = { "TotalExposurePeriod",      "SumOfExposureTimesOfAll",
                "SumOfExposureTimesOfUsed", "MaxExposureTimesOfAll",
                "MaxExposureTimesOfUsed",   "MinExposureTimesOfAll",
                "MinExposureTimesOfUsed" };
        for (const auto count : { 2U, 4096U, 4097U }) {
            SCOPED_TRACE(count);
            MetaStore source;
            add_xmp_value(&source, kInvalidBlockId, kEncodingCipa,
                          "CompositeImage", make_u16(3U), EntryFlags::Dirty,
                          0U);
            const std::array<uint16_t, 2> counts
                = { static_cast<uint16_t>(count), 0U };
            add_xmp_value(&source, kInvalidBlockId, kEncodingCipa,
                          "CompositeImageCount",
                          make_u16_array(source.arena(), counts),
                          EntryFlags::Dirty, 0U);
            for (const auto name : summaries)
                add_xmp_value(&source, kInvalidBlockId, kEncodingCipa,
                              "CompositeImageExposureTimes/"
                                  + std::string(name),
                              make_urational(0U, 0U), EntryFlags::Dirty, 0U);
            add_xmp_value(&source, kInvalidBlockId, kEncodingCipa,
                          "CompositeImageExposureTimes/NumberOfSequences",
                          make_u16(1U), EntryFlags::Dirty, 0U);
            add_xmp_value(
                &source, kInvalidBlockId, kEncodingCipa,
                "CompositeImageExposureTimes/NumberOfImagesInSequences",
                make_u16(static_cast<uint16_t>(count)), EntryFlags::Dirty, 0U);
            const std::vector<URational> values(count, { 0U, 7U });
            add_xmp_value(&source, kInvalidBlockId, kEncodingCipa,
                          "CompositeImageExposureTimes/Values",
                          make_urational_array(source.arena(), values),
                          EntryFlags::Dirty, 0U);
            source.finalize();
            MetaStore output;
            const auto result = translate_xmp_composite_metadata(source, {},
                                                                 &output);
            if (count > 4096U) {
                EXPECT_EQ(result.status,
                          MetadataCaptureTranslationStatus::SourceLimitExceeded);
                EXPECT_TRUE(output.entries().empty());
            } else {
                ASSERT_EQ(result.status, kEncodingOk);
                EXPECT_TRUE(validate_store(output).ok());
                const auto exposure = active_exif_entry(output, "exififd",
                                                        0xa462U);
                ASSERT_NE(exposure, nullptr);
                EXPECT_EQ(exposure->value.count, 60U + count * 8U);
            }
        }
    }

    static constexpr std::array<uint16_t, 4> kStructuredTags
        = { 0x8828U, 0xa20cU, 0xa302U, 0xa40bU };
    static constexpr std::array<std::string_view, 4> kStructuredNames
        = { "OECF", "SpatialFrequencyResponse", "CFAPattern",
            "DeviceSettingDescription" };

    static std::vector<std::byte>
    structured_native_bytes(const MetaStore& store, uint16_t tag)
    {
        const Entry* entry = active_exif_entry(store, "exififd", tag);
        if (!entry) {
            ADD_FAILURE() << tag;
            return {};
        }
        const auto bytes = store.arena().span(entry->value.data.span);
        return { bytes.begin(), bytes.end() };
    }

    static void
    structured_rollback(MetaStore& source,
                        MetadataStructuredCaptureTranslationOptions options = {
                            .source_mode = kEncodingAll })
    {
        const Entry* before = source.entries().data();
        const auto bytes    = source.arena().bytes();
        const std::vector<std::byte> original(bytes.begin(), bytes.end());
        EXPECT_NE(translate_xmp_structured_capture_metadata(source, options,
                                                            &source)
                      .status,
                  kEncodingOk);
        EXPECT_EQ(source.entries().data(), before);
        EXPECT_EQ(std::vector<std::byte>(source.arena().bytes().begin(),
                                         source.arena().bytes().end()),
                  original);
    }

    TEST(MetadataStructuredCapture, ExactTablesUnicodeAndPortableRoundTrip)
    {
        MetaStore source = encoding_source();
        MetaStore translated;
        EXPECT_EQ(translate_xmp_structured_capture_metadata(source, {},
                                                            &translated)
                      .source_properties,
                  0U);
        const auto result = translate_xmp_structured_capture_metadata(
            source, { .source_mode = kEncodingAll }, &translated);
        ASSERT_EQ(result.status, kEncodingOk)
            << metadata_capture_translation_status_name(result.status);
        EXPECT_EQ(result.entries_added, 4U);
        EXPECT_EQ(result.groups_translated, 4U);
        EXPECT_TRUE(validate_store(translated).ok());
        const std::string xml = encoding_packet(translated);
        EXPECT_NE(xml.find("<rdf:li> log input </rdf:li>"), std::string::npos);
        EXPECT_NE(xml.find("<rdf:li>-2147483648/2147483647</rdf:li>"),
                  std::string::npos);
        EXPECT_NE(xml.find("<rdf:li>-1/-2</rdf:li>"), std::string::npos);
        EXPECT_NE(xml.find("<rdf:li>124/10</rdf:li>"), std::string::npos);
        EXPECT_NE(xml.find("&#13;&#10;&#9;"), std::string::npos);
        EXPECT_NE(xml.find("<rdf:li></rdf:li>"), std::string::npos);
        MetaStore restored;
        ASSERT_EQ(decode_xmp_packet(std::as_bytes(
                                        std::span(xml.data(), xml.size())),
                                    restored)
                      .status,
                  XmpDecodeStatus::Ok);
        restored.finalize();
        ASSERT_EQ(translate_xmp_structured_capture_metadata(
                      restored, { .source_mode = kEncodingAll }, &restored)
                      .status,
                  kEncodingOk);
        for (uint16_t tag : kStructuredTags)
            EXPECT_EQ(structured_native_bytes(restored, tag),
                      structured_native_bytes(translated, tag));
        const std::array<std::byte, 10> cfa = {
            std::byte { 3 }, std::byte { 0 }, std::byte { 2 }, std::byte { 0 },
            std::byte { 0 }, std::byte { 1 }, std::byte { 2 }, std::byte { 3 },
            std::byte { 4 }, std::byte { 6 }
        };
        EXPECT_EQ(structured_native_bytes(translated, 0xa302U),
                  std::vector<std::byte>(cfa.begin(), cfa.end()));
        EXPECT_EQ(translate_xmp_structured_capture_metadata(
                      translated, { .source_mode = kEncodingAll }, &translated)
                      .groups_unchanged,
                  4U);

        for (TextEncoding text_encoding :
             { TextEncoding::Ascii, TextEncoding::Utf8 }) {
            MetaStore controls;
            add_xmp_text(&controls, kInvalidBlockId, kEncodingExif,
                         "DeviceSettingDescription/Columns", "1",
                         EntryFlags::Dirty, 0U);
            add_xmp_text(&controls, kInvalidBlockId, kEncodingExif,
                         "DeviceSettingDescription/Rows", "1", EntryFlags::None,
                         0U);
            add_xmp_value(&controls, kInvalidBlockId, kEncodingExif,
                          "DeviceSettingDescription/Values[1]",
                          make_text(controls.arena(), " \r\n\t\x7f ",
                                    text_encoding),
                          EntryFlags::None, 0U);
            controls.finalize();
            ASSERT_EQ(translate_xmp_structured_capture_metadata(controls, {},
                                                                &controls)
                          .status,
                      kEncodingOk);
            XmpPortableOptions options;
            options.include_existing_xmp = true;
            options.include_exif         = false;
            std::array<std::byte, 4096> packet {};
            const auto dumped = dump_xmp_portable(controls, packet, options);
            ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
            MetaStore reread;
            ASSERT_EQ(decode_xmp_packet(std::span(packet.data(), dumped.written),
                                        reread)
                          .status,
                      XmpDecodeStatus::Ok);
            reread.finalize();
            ASSERT_EQ(translate_xmp_structured_capture_metadata(
                          reread, { .source_mode = kEncodingAll }, &reread)
                          .status,
                      kEncodingOk);
            EXPECT_EQ(structured_native_bytes(reread, 0xa40bU),
                      structured_native_bytes(controls, 0xa40bU));
        }
    }

    TEST(MetadataStructuredCapture, DirtyMemberConflictsDeletionAndLateRollback)
    {
        MetaStore source = encoding_source();
        ASSERT_EQ(translate_xmp_structured_capture_metadata(
                      source, { .source_mode = kEncodingAll }, &source)
                      .status,
                  kEncodingOk);
        const auto original = structured_native_bytes(source, 0x8828U);
        encoding_change(source, "OECF/Values[1]", "-7/3", false, kEncodingExif);
        structured_rollback(source, {});
        auto result = translate_xmp_structured_capture_metadata(
            source,
            { .conflict_policy
              = MetadataCaptureTranslationConflictPolicy::PreserveExisting },
            &source);
        ASSERT_EQ(result.status, kEncodingOk);
        EXPECT_EQ(result.groups_preserved, 1U);
        EXPECT_EQ(structured_native_bytes(source, 0x8828U), original);
        result = translate_xmp_structured_capture_metadata(
            source,
            { .conflict_policy
              = MetadataCaptureTranslationConflictPolicy::ReplaceExisting },
            &source);
        ASSERT_EQ(result.status, kEncodingOk);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_NE(structured_native_bytes(source, 0x8828U), original);
        encoding_change(source, "DeviceSettingDescription/Values[2]",
                        std::string_view("bad\0setting", 11U), false,
                        kEncodingExif);
        structured_rollback(
            source,
            { .source_mode = kEncodingAll,
              .conflict_policy
              = MetadataCaptureTranslationConflictPolicy::ReplaceExisting });
        encoding_change(source, "DeviceSettingDescription/Values[2]", "valid",
                        false, kEncodingExif);
        MetaEdit remove;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind != MetaKeyKind::XmpProperty)
                continue;
            const auto bytes = source.arena().span(
                entry.key.data.xmp_property.property_path);
            const std::string_view path(reinterpret_cast<const char*>(
                                            bytes.data()),
                                        bytes.size());
            if (path.starts_with("OECF/"))
                remove.tombstone(id);
        }
        source = commit(source, std::span(&remove, 1U));
        result = translate_xmp_structured_capture_metadata(
            source,
            { .conflict_policy
              = MetadataCaptureTranslationConflictPolicy::ReplaceExisting },
            &source);
        ASSERT_EQ(result.status, kEncodingOk);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(active_exif_entry(source, "exififd", 0x8828U), nullptr);
        EXPECT_NE(active_exif_entry(source, "exififd", 0xa20cU), nullptr);
    }

    TEST(MetadataStructuredCapture, SourceShapesAliasesAndResourceBudgets)
    {
        for (const auto change :
             { std::pair { "OECF/Columns", "0" },
               std::pair { "OECF/Rows", "65536" },
               std::pair { "OECF/Values[1]", "2147483648/1" },
               std::pair { "OECF/Values[1]", "1/0" },
               std::pair { "SpatialFrequencyResponse/Values[1]", "-1/2" },
               std::pair { "SpatialFrequencyResponse/Values[1]",
                           "1/4294967296" },
               std::pair { "CFAPattern/Values[6]", "7" } }) {
            SCOPED_TRACE(change.first);
            SCOPED_TRACE(change.second);
            MetaStore source = encoding_source();
            encoding_change(source, change.first, change.second, false,
                            kEncodingExif);
            structured_rollback(source);
        }
        for (const std::string_view path :
             { "OECF/Names[3]", "CFAPattern/Values[01]", "CFAPattern/Values[7]",
               "OECF/Values[1]?xml:lang", "OECF/other:Rows",
               "DeviceSettingDescription/Names[1]" }) {
            MetaStore source = encoding_source();
            MetaEdit edit;
            Entry entry;
            entry.key   = make_xmp_property_key(edit.arena(), kEncodingExif,
                                                path);
            entry.value = make_text(edit.arena(), "1", TextEncoding::Ascii);
            entry.flags = EntryFlags::Dirty;
            edit.add_entry(entry);
            source = commit(source, std::span(&edit, 1U));
            structured_rollback(source);
        }
        MetaStore source  = encoding_source();
        const auto column = source.find_all(
            make_xmp_property_key_view(kEncodingExif, "OECF/Columns"));
        ASSERT_EQ(column.size(), 1U);
        MetaEdit alias;
        Entry alternative;
        alternative.key   = make_xmp_property_key(alias.arena(), kEncodingExif,
                                                  "OECF/Columus");
        alternative.value = make_u16(2U);
        alternative.flags = EntryFlags::Dirty;
        alias.add_entry(alternative);
        MetaStore ambiguous = commit(source, std::span(&alias, 1U));
        structured_rollback(ambiguous);
        MetaStore only_alias;
        add_xmp_value(&only_alias, kInvalidBlockId, kEncodingExif,
                      "OECF/Columus", make_u16(1U), EntryFlags::Dirty, 0U);
        add_xmp_value(&only_alias, kInvalidBlockId, kEncodingExif, "OECF/Rows",
                      make_u16(1U), EntryFlags::None, 0U);
        add_xmp_text(&only_alias, kInvalidBlockId, kEncodingExif,
                     "OECF/Names[1]", "", EntryFlags::None, 0U);
        add_xmp_text(&only_alias, kInvalidBlockId, kEncodingExif,
                     "OECF/Values[1]", "-1/-2", EntryFlags::None, 0U);
        only_alias.finalize();
        ASSERT_EQ(translate_xmp_structured_capture_metadata(only_alias, {},
                                                            &only_alias)
                      .status,
                  kEncodingOk);
        EXPECT_NE(
            encoding_packet(only_alias).find("<exif:Columns>1</exif:Columns>"),
            std::string::npos);
        for (unsigned budget = 0U; budget < 6U; ++budget) {
            auto options = MetadataStructuredCaptureTranslationOptions {
                .source_mode = kEncodingAll
            };
            if (budget == 0U)
                options.max_added_entries = 3U;
            if (budget == 1U)
                options.max_operations = 3U;
            if (budget == 2U)
                options.max_columns = 1U;
            if (budget == 3U)
                options.max_values = 3U;
            if (budget == 4U)
                options.max_text_bytes_per_property = 2U;
            if (budget == 5U)
                options.max_total_text_bytes = 8U;
            structured_rollback(source, options);
        }
    }

    TEST(MetadataStructuredCapture, TypedArraysAndExactPayloadLimit)
    {
        for (const uint32_t count : { 4096U, 4097U }) {
            MetaStore source;
            add_xmp_value(&source, kInvalidBlockId, kEncodingExif,
                          "CFAPattern/Columns",
                          make_u16(count == 4096U ? 256U : 17U),
                          EntryFlags::Dirty, 0U);
            add_xmp_value(&source, kInvalidBlockId, kEncodingExif,
                          "CFAPattern/Rows",
                          make_u16(count == 4096U ? 16U : 241U),
                          EntryFlags::None, 0U);
            const std::vector<uint8_t> codes(count, 1U);
            add_xmp_value(&source, kInvalidBlockId, kEncodingExif,
                          "CFAPattern/Values",
                          make_u8_array(source.arena(), codes),
                          EntryFlags::None, 0U);
            source.finalize();
            if (count == 4097U) {
                structured_rollback(source);
                continue;
            }
            ASSERT_EQ(translate_xmp_structured_capture_metadata(source, {},
                                                                &source)
                          .status,
                      kEncodingOk);
            EXPECT_EQ(structured_native_bytes(source, 0xa302U).size(), 4100U);
        }
        for (bool signed_values : { false, true }) {
            MetaStore source;
            const std::string name = signed_values ? "OECF"
                                                   : "SpatialFrequencyResponse";
            add_xmp_value(&source, kInvalidBlockId, kEncodingExif,
                          name + "/Columns", make_u16(1U), EntryFlags::Dirty,
                          0U);
            add_xmp_value(&source, kInvalidBlockId, kEncodingExif,
                          name + "/Rows", make_u16(2U), EntryFlags::None, 0U);
            add_xmp_text(&source, kInvalidBlockId, kEncodingExif,
                         name + "/Names[1]", "n", EntryFlags::None, 0U);
            const std::array<SRational, 2> signed_pairs = {
                SRational { INT32_MIN, -1 }, SRational { INT32_MAX, INT32_MIN }
            };
            const std::array<URational, 2> unsigned_pairs = {
                URational { UINT32_MAX, 1U }, URational { 0U, UINT32_MAX }
            };
            const MetaValue values
                = signed_values
                      ? make_srational_array(source.arena(), signed_pairs)
                      : make_urational_array(source.arena(), unsigned_pairs);
            add_xmp_value(&source, kInvalidBlockId, kEncodingExif,
                          name + "/Values", values, EntryFlags::None, 0U);
            source.finalize();
            ASSERT_EQ(translate_xmp_structured_capture_metadata(source, {},
                                                                &source)
                          .status,
                      kEncodingOk);
            EXPECT_TRUE(validate_store(source).ok());
        }
        MetaStore source;
        add_xmp_value(&source, kInvalidBlockId, kEncodingExif,
                      "DeviceSettingDescription/Columns", make_u16(1U),
                      EntryFlags::Dirty, 0U);
        add_xmp_value(&source, kInvalidBlockId, kEncodingExif,
                      "DeviceSettingDescription/Rows", make_u16(1U),
                      EntryFlags::None, 0U);
        add_xmp_text(&source, kInvalidBlockId, kEncodingExif,
                     "DeviceSettingDescription/Values[1]", "\xf0\x9f\x98\x80",
                     EntryFlags::None, 0U);
        source.finalize();
        structured_rollback(source, { .source_mode       = kEncodingAll,
                                      .max_payload_bytes = 11U });
        ASSERT_EQ(translate_xmp_structured_capture_metadata(
                      source, { .max_payload_bytes = 12U }, &source)
                      .status,
                  kEncodingOk);
        EXPECT_EQ(structured_native_bytes(source, 0xa40bU).size(), 12U);
    }

    TEST(MetadataStructuredCapture,
         BinaryBoundsTypedValidationAndSourceFallback)
    {
        MetaStore valid = encoding_source();
        ASSERT_EQ(translate_xmp_structured_capture_metadata(
                      valid, { .source_mode = kEncodingAll }, &valid)
                      .status,
                  kEncodingOk);
        for (size_t i = 0U; i < kStructuredTags.size(); ++i) {
            const uint16_t tag = kStructuredTags[i];
            const auto good    = structured_native_bytes(valid, tag);
            for (size_t size = 0U; size < good.size(); ++size) {
                // Device settings may end after any complete string.
                if (tag == 0xa40bU && size >= 8U
                    && good[size - 1U] == std::byte { 0 }
                    && good[size - 2U] == std::byte { 0 })
                    continue;
                SCOPED_TRACE(tag);
                SCOPED_TRACE(size);
                MetaStore raw_source;
                add_xmp_value(&raw_source, kInvalidBlockId, kEncodingExif,
                              kStructuredNames[i],
                              make_bytes(raw_source.arena(),
                                         std::span(good.data(), size)),
                              EntryFlags::Dirty, 0U);
                raw_source.finalize();
                structured_rollback(raw_source);
                MetadataTypedEditingOperation set;
                set.kind        = MetadataEditingOperationKind::Set;
                set.entry.key   = make_exif_tag_key_view("exififd", tag);
                set.entry.value = make_value_view_bytes(
                    std::span(good.data(), size));
                const Entry* before = valid.entries().data();
                EXPECT_FALSE(
                    edit_metadata_typed(valid, std::span(&set, 1U), &valid)
                        .ok());
                EXPECT_EQ(valid.entries().data(), before);
            }
            MetaStore raw_source;
            add_xmp_value(&raw_source, kInvalidBlockId, kEncodingExif,
                          kStructuredNames[i],
                          make_bytes(raw_source.arena(), good),
                          EntryFlags::Dirty, 0U);
            raw_source.finalize();
            ASSERT_EQ(translate_xmp_structured_capture_metadata(raw_source, {},
                                                                &raw_source)
                          .status,
                      kEncodingOk);
            EXPECT_EQ(structured_native_bytes(raw_source, tag), good);
            auto bad         = good;
            bad[0]           = std::byte { 0 };
            bad[1]           = std::byte { 0 };
            MetaStore source = encoding_source();
            Entry malformed;
            malformed.key   = make_exif_tag_key(source.arena(), "exififd", tag);
            malformed.value = make_bytes(source.arena(), bad);
            source.add_entry(malformed);
            source.finalize();
            for (auto policy : { XmpConflictPolicy::CurrentBehavior,
                                 XmpConflictPolicy::ExistingWins,
                                 XmpConflictPolicy::GeneratedWins }) {
                XmpPortableOptions options;
                options.conflict_policy      = policy;
                options.include_existing_xmp = true;
                options.existing_standard_namespace_policy
                    = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
                std::array<std::byte, 32768> bytes {};
                const auto dumped = dump_xmp_portable(source, bytes, options);
                ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
                MetaStore restored;
                ASSERT_EQ(decode_xmp_packet(std::span(bytes.data(),
                                                      dumped.written),
                                            restored)
                              .status,
                          XmpDecodeStatus::Ok);
                restored.finalize();
                ASSERT_EQ(translate_xmp_structured_capture_metadata(
                              restored, { .source_mode = kEncodingAll },
                              &restored)
                              .status,
                          kEncodingOk);
                EXPECT_EQ(structured_native_bytes(restored, tag), good);
            }
        }
    }

    TEST(MetadataStructuredCapture, MalformedUnicodeAsciiAndRationalBytes)
    {
        for (std::string_view invalid :
             { std::string_view("\xc0\xaf", 2U),
               std::string_view("\xed\xa0\x80", 3U),
               std::string_view("\xf4\x90\x80\x80", 4U),
               std::string_view("\xe2\x82", 2U),
               std::string_view("\x01", 1U) }) {
            MetaStore source = encoding_source();
            encoding_change(source, "DeviceSettingDescription/Values[1]",
                            invalid, false, kEncodingExif);
            structured_rollback(source);
            source = encoding_source();
            encoding_change(source, "OECF/Names[1]", invalid, false,
                            kEncodingExif);
            structured_rollback(source);
        }
        for (const std::vector<std::byte>& raw :
             { std::vector<std::byte> { std::byte { 1 }, std::byte { 0 },
                                        std::byte { 1 }, std::byte { 0 },
                                        std::byte { 'A' }, std::byte { 0 },
                                        std::byte { 0 }, std::byte { 0 } },
               std::vector<std::byte> { std::byte { 1 }, std::byte { 0 },
                                        std::byte { 1 }, std::byte { 0 },
                                        std::byte { 0xff }, std::byte { 0xfe },
                                        std::byte { 0 }, std::byte { 0xd8 },
                                        std::byte { 0 }, std::byte { 0 } },
               std::vector<std::byte> { std::byte { 1 }, std::byte { 0 },
                                        std::byte { 1 }, std::byte { 0 },
                                        std::byte { 0xff }, std::byte { 0xfe },
                                        std::byte { 0 }, std::byte { 0xdc },
                                        std::byte { 0 }, std::byte { 0 } } }) {
            MetaStore source;
            add_xmp_value(&source, kInvalidBlockId, kEncodingExif,
                          "DeviceSettingDescription",
                          make_bytes(source.arena(), raw), EntryFlags::Dirty,
                          0U);
            source.finalize();
            structured_rollback(source);
        }

        MetaStore valid = encoding_source();
        ASSERT_EQ(translate_xmp_structured_capture_metadata(
                      valid, { .source_mode = kEncodingAll }, &valid)
                      .status,
                  kEncodingOk);
        for (uint16_t tag : { 0x8828U, 0xa20cU }) {
            auto raw = structured_native_bytes(valid, tag);
            std::fill(raw.end() - 4, raw.end(), std::byte { 0 });
            MetaStore source;
            add_xmp_value(&source, kInvalidBlockId, kEncodingExif,
                          tag == 0x8828U ? "OECF" : "SpatialFrequencyResponse",
                          make_bytes(source.arena(), raw), EntryFlags::Dirty,
                          0U);
            source.finalize();
            structured_rollback(source);
            MetadataTypedEditingOperation set;
            set.kind        = MetadataEditingOperationKind::Set;
            set.entry.key   = make_exif_tag_key_view("exififd", tag);
            set.entry.value = make_value_view_bytes(raw);
            EXPECT_FALSE(
                edit_metadata_typed(valid, std::span(&set, 1U), &valid).ok());
        }
    }

    TEST(MetadataStructuredCapture, BigEndianMixedStringBomsAndTypedReplacement)
    {
        // Big-endian dimensions, then independent BE and LE UTF-16 strings.
        const std::array<std::byte, 16> big
            = { std::byte { 0 },    std::byte { 4 },    std::byte { 0 },
                std::byte { 1 },    std::byte { 0xfe }, std::byte { 0xff },
                std::byte { 0x65 }, std::byte { 0xe5 }, std::byte { 0 },
                std::byte { 0 },    std::byte { 0xff }, std::byte { 0xfe },
                std::byte { 'A' },  std::byte { 0 },    std::byte { 0 },
                std::byte { 0 } };
        MetaStore source;
        Entry entry;
        entry.key   = make_exif_tag_key(source.arena(), "exififd", 0xa40bU);
        entry.value = make_bytes(source.arena(), big);
        entry.flags = EntryFlags::ValueBigEndian;
        source.add_entry(entry);
        source.finalize();
        ASSERT_TRUE(validate_store(source).ok());
        const auto measured = serialize_exif_tiff(source, {});
        ASSERT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> tiff(measured.needed);
        ASSERT_TRUE(serialize_exif_tiff(source, tiff).ok());
        MetaStore decoded;
        std::array<ExifIfdRef, 16> ifds {};
        ASSERT_EQ(decode_exif_tiff(tiff, decoded, ifds, {}).status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        auto little = big;
        std::swap(little[0], little[1]);
        std::swap(little[2], little[3]);
        EXPECT_EQ(structured_native_bytes(decoded, 0xa40bU),
                  std::vector<std::byte>(little.begin(), little.end()));
        EXPECT_EQ(structured_native_bytes(source, 0xa40bU),
                  std::vector<std::byte>(big.begin(), big.end()));
        const auto xml = encoding_packet(source);
        MetaStore restored;
        ASSERT_EQ(decode_xmp_packet(std::as_bytes(
                                        std::span(xml.data(), xml.size())),
                                    restored)
                      .status,
                  XmpDecodeStatus::Ok);
        restored.finalize();
        ASSERT_EQ(translate_xmp_structured_capture_metadata(
                      restored, { .source_mode = kEncodingAll }, &restored)
                      .status,
                  kEncodingOk);
        MetaEdit native;
        const Entry* translated = active_exif_entry(restored, "exififd",
                                                    0xa40bU);
        ASSERT_NE(translated, nullptr);
        Entry copied;
        copied.key   = make_exif_tag_key(native.arena(), "exififd", 0xa40bU);
        copied.value = make_bytes(native.arena(), big);
        copied.flags = EntryFlags::ValueBigEndian;
        native.add_entry(copied);
        const auto ids = restored.find_all(
            make_exif_tag_key_view("exififd", 0xa40bU));
        native.tombstone(ids[0]);
        restored = commit(restored, std::span(&native, 1U));
        EXPECT_EQ(translate_xmp_structured_capture_metadata(
                      restored, { .source_mode = kEncodingAll }, &restored)
                      .groups_unchanged,
                  1U);
        MetadataTypedEditingOperation set;
        set.kind        = MetadataEditingOperationKind::Set;
        set.entry.key   = make_exif_tag_key_view("exififd", 0xa40bU);
        set.entry.value = make_value_view_bytes(little);
        ASSERT_TRUE(
            edit_metadata_typed(source, std::span(&set, 1U), &source).ok());
        EXPECT_FALSE(any(active_exif_entry(source, "exififd", 0xa40bU)->flags,
                         EntryFlags::ValueBigEndian));
    }

}  // namespace
}  // namespace openmeta
