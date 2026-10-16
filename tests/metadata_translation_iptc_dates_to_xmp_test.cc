// SPDX-License-Identifier: Apache-2.0

#include "openmeta/iptc_iim_decode.h"
#include "openmeta/metadata_translation.h"

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace openmeta {
namespace {

    using Status = MetadataIptcDateToXmpStatus;

    static constexpr std::string_view kPhotoshopNs
        = "http://ns.adobe.com/photoshop/1.0/";
    static constexpr std::string_view kXmpNs = "http://ns.adobe.com/xap/1.0/";

    static std::span<const std::byte> as_bytes(std::string_view text) noexcept
    {
        return { reinterpret_cast<const std::byte*>(text.data()), text.size() };
    }

    static EntryId add_iptc_value(MetaStore* store, BlockId block,
                                  uint16_t dataset, uint32_t order,
                                  MetaValue value,
                                  EntryFlags flags = EntryFlags::None) noexcept
    {
        Entry entry;
        entry.key                   = make_iptc_dataset_key(2U, dataset);
        entry.value                 = value;
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.flags                 = flags;
        return store->add_entry(entry);
    }

    static EntryId
    add_iptc_bytes(MetaStore* store, BlockId block, uint16_t dataset,
                   uint32_t order, std::string_view bytes,
                   EntryFlags flags            = EntryFlags::None,
                   TextEncoding stale_encoding = TextEncoding::Unknown) noexcept
    {
        MetaValue value     = make_bytes(store->arena(), as_bytes(bytes));
        value.text_encoding = stale_encoding;
        return add_iptc_value(store, block, dataset, order, value, flags);
    }

    static EntryId add_xmp_value(MetaStore* store, BlockId block,
                                 std::string_view schema, std::string_view path,
                                 MetaValue value, uint32_t order = 0U,
                                 EntryFlags flags = EntryFlags::Dirty)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store->arena(), schema, path);
        entry.value = value;
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.flags                 = flags;
        return store->add_entry(entry);
    }

    static EntryId add_xmp_text(MetaStore* store, BlockId block,
                                std::string_view schema, std::string_view path,
                                std::string_view text, uint32_t order = 0U,
                                EntryFlags flags = EntryFlags::Dirty)
    {
        return add_xmp_value(store, block, schema, path,
                             make_text(store->arena(), text, TextEncoding::Utf8),
                             order, flags);
    }

    static EntryId find_xmp_entry(const MetaStore& store,
                                  std::string_view schema,
                                  std::string_view path) noexcept
    {
        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& entry = store.entry(id);
            if (entry.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }
            const std::span<const std::byte> schema_bytes = store.arena().span(
                entry.key.data.xmp_property.schema_ns);
            const std::span<const std::byte> path_bytes = store.arena().span(
                entry.key.data.xmp_property.property_path);
            const std::string_view actual_schema(reinterpret_cast<const char*>(
                                                     schema_bytes.data()),
                                                 schema_bytes.size());
            const std::string_view actual_path(reinterpret_cast<const char*>(
                                                   path_bytes.data()),
                                               path_bytes.size());
            if (actual_schema == schema && actual_path == path) {
                return id;
            }
        }
        return kInvalidEntryId;
    }

    static bool find_xmp_text(const MetaStore& store, std::string_view schema,
                              std::string_view path, std::string_view* out_text,
                              EntryId* out_id = nullptr) noexcept
    {
        const EntryId id = find_xmp_entry(store, schema, path);
        if (id == kInvalidEntryId || !out_text) {
            return false;
        }
        const Entry& entry = store.entry(id);
        if (any(entry.flags, EntryFlags::Deleted)
            || entry.value.kind != MetaValueKind::Text) {
            return false;
        }
        const std::span<const std::byte> value_bytes = store.arena().span(
            entry.value.data.span);
        *out_text = value_bytes.empty()
                        ? std::string_view {}
                        : std::string_view(reinterpret_cast<const char*>(
                                               value_bytes.data()),
                                           value_bytes.size());
        if (out_id) {
            *out_id = id;
        }
        return true;
    }

    static MetaStore make_sentinel_output()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_xmp_text(&store, block, "urn:date-test", "Sentinel",
                           "preserve");
        store.finalize();
        return store;
    }

    static MetadataIptcDateToXmpResult
    translate_pair_into(std::string_view date, bool has_time,
                        std::string_view time, MetaStore* output)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&source, block, 55U, 0U, date);
        if (has_time) {
            (void)add_iptc_bytes(&source, block, 60U, 1U, time);
        }
        source.finalize();
        return translate_iptc_dates_to_xmp(source,
                                           MetadataIptcDateToXmpOptions {},
                                           output);
    }

    static MetadataIptcDateToXmpResult
    translate_pair(std::string_view date, bool has_time = false,
                   std::string_view time = {})
    {
        MetaStore output;
        return translate_pair_into(date, has_time, time, &output);
    }

    static MetadataIptcDateToXmpStatus
    translate_native_value(MetaValueKind kind, std::string_view payload,
                           TextEncoding encoding = TextEncoding::Unknown,
                           EntryFlags flags      = EntryFlags::None,
                           BlockId origin = 0U, bool bad_count = false,
                           bool bad_span = false)
    {
        MetaStore source;
        (void)source.add_block(BlockInfo {});
        MetaValue value;
        switch (kind) {
        case MetaValueKind::Empty: value = MetaValue {}; break;
        case MetaValueKind::Scalar: value = make_u8(1U); break;
        case MetaValueKind::Array:
            value = make_array(source.arena(), MetaElementType::U8,
                               as_bytes(payload), 1U);
            break;
        case MetaValueKind::Bytes:
            value               = make_bytes(source.arena(), as_bytes(payload));
            value.text_encoding = encoding;
            break;
        case MetaValueKind::Text:
            value = make_text(source.arena(), payload, encoding);
            break;
        }
        if (bad_count) {
            ++value.count;
        }
        if (bad_span) {
            value.data.span.offset = std::numeric_limits<uint32_t>::max();
            value.data.span.size   = 8U;
        }
        (void)add_iptc_value(&source, origin, 55U, 0U, value, flags);
        source.finalize();
        MetaStore output;
        return translate_iptc_dates_to_xmp(source,
                                           MetadataIptcDateToXmpOptions {},
                                           &output)
            .status;
    }

    static void
    expect_failure_preserves_output(const MetaStore& source,
                                    const MetadataIptcDateToXmpOptions& options,
                                    Status expected)
    {
        MetaStore output         = make_sentinel_output();
        const size_t entry_count = output.entries().size();
        std::string_view sentinel;
        const MetadataIptcDateToXmpResult result
            = translate_iptc_dates_to_xmp(source, options, &output);
        EXPECT_EQ(result.status, expected);
        EXPECT_EQ(result.source_properties, 0U);
        EXPECT_EQ(result.groups_translated, 0U);
        EXPECT_EQ(result.groups_preserved, 0U);
        EXPECT_EQ(result.groups_unchanged, 0U);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);
        EXPECT_EQ(output.entries().size(), entry_count);
        ASSERT_TRUE(
            find_xmp_text(output, "urn:date-test", "Sentinel", &sentinel));
        EXPECT_EQ(sentinel, "preserve");
    }

    TEST(IptcDatesToXmp, MapsBothPairsAndDatePrecisions)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&source, block, 55U, 8U, "20240229");
        (void)add_iptc_bytes(&source, block, 60U, 9U, "235960-0000");
        (void)add_iptc_bytes(&source, block, 62U, 10U, "20230500");
        (void)add_xmp_text(&source, block, "urn:date-test", "Unrelated",
                           "keep");
        source.finalize();

        MetaStore output;
        const MetadataIptcDateToXmpResult result = translate_iptc_dates_to_xmp(
            source, MetadataIptcDateToXmpOptions {}, &output);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.source_properties, 3U);
        EXPECT_EQ(result.groups_translated, 2U);
        EXPECT_EQ(result.entries_added, 2U);

        std::string_view value;
        EntryId created_id = kInvalidEntryId;
        ASSERT_TRUE(find_xmp_text(output, kPhotoshopNs, "DateCreated", &value,
                                  &created_id));
        EXPECT_EQ(value, "2024-02-29T23:59:60-00:00");
        EXPECT_EQ(output.entry(created_id).origin.block, block);
        EXPECT_EQ(output.entry(created_id).origin.order_in_block, 8U);
        EXPECT_EQ(output.entry(created_id).origin.wire_type.family,
                  WireFamily::None);
        EXPECT_EQ(output.entry(created_id).flags, EntryFlags::Dirty);
        ASSERT_TRUE(find_xmp_text(output, kXmpNs, "CreateDate", &value));
        EXPECT_EQ(value, "2023-05");
        EXPECT_EQ(source.entries().size(), 4U);
        EXPECT_EQ(output.entries().size(), 6U);
        ASSERT_TRUE(
            find_xmp_text(output, "urn:date-test", "Unrelated", &value));
        EXPECT_EQ(value, "keep");

        struct Partial final {
            std::string_view input;
            std::string_view expected;
        };
        static constexpr std::array<Partial, 2U> partials = { {
            { "20240000", "2024" },
            { "20240500", "2024-05" },
        } };
        for (const Partial& partial : partials) {
            MetaStore partial_source;
            const BlockId partial_block = partial_source.add_block(
                BlockInfo {});
            (void)add_iptc_bytes(&partial_source, partial_block, 55U, 3U,
                                 partial.input);
            partial_source.finalize();
            MetaStore partial_output;
            const MetadataIptcDateToXmpResult partial_result
                = translate_iptc_dates_to_xmp(partial_source,
                                              MetadataIptcDateToXmpOptions {},
                                              &partial_output);
            ASSERT_EQ(partial_result.status, Status::Ok);
            ASSERT_TRUE(find_xmp_text(partial_output, kPhotoshopNs,
                                      "DateCreated", &value));
            EXPECT_EQ(value, partial.expected);
        }

        struct TimeOutput final {
            bool has_time;
            std::string_view input;
            std::string_view expected;
        };
        static constexpr std::array<TimeOutput, 4U> time_outputs = { {
            { false, {}, "2024-01-01" },
            { true, "000000", "2024-01-01T00:00:00" },
            { true, "000000+2359", "2024-01-01T00:00:00+23:59" },
            { true, "000000-0130", "2024-01-01T00:00:00-01:30" },
        } };
        for (const TimeOutput& item : time_outputs) {
            MetaStore time_output;
            const MetadataIptcDateToXmpResult time_result
                = translate_pair_into("20240101", item.has_time, item.input,
                                      &time_output);
            ASSERT_EQ(time_result.status, Status::Ok);
            ASSERT_TRUE(find_xmp_text(time_output, kPhotoshopNs, "DateCreated",
                                      &value));
            EXPECT_EQ(value, item.expected);
        }
    }

    TEST(IptcDatesToXmp, DecodedRawBytesAndDirtyOnlySelection)
    {
        static constexpr std::array<std::byte, 13U> kIim = {
            std::byte { 0x1cU }, std::byte { 0x02U }, std::byte { 0x37U },
            std::byte { 0x00U }, std::byte { 0x08U }, std::byte { '2' },
            std::byte { '0' },   std::byte { '2' },   std::byte { '4' },
            std::byte { '0' },   std::byte { '2' },   std::byte { '2' },
            std::byte { '9' },
        };
        MetaStore decoded_source;
        ASSERT_EQ(decode_iptc_iim(kIim, decoded_source).status,
                  IptcIimDecodeStatus::Ok);
        ASSERT_EQ(decoded_source.entries().size(), 1U);
        EXPECT_EQ(decoded_source.entry(0U).value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(decoded_source.entry(0U).value.text_encoding,
                  TextEncoding::Unknown);
        decoded_source.finalize();
        MetaStore decoded_output;
        const MetadataIptcDateToXmpResult decoded_result
            = translate_iptc_dates_to_xmp(decoded_source,
                                          MetadataIptcDateToXmpOptions {},
                                          &decoded_output);
        ASSERT_EQ(decoded_result.status, Status::Ok);
        std::string_view value;
        ASSERT_TRUE(
            find_xmp_text(decoded_output, kPhotoshopNs, "DateCreated", &value));
        EXPECT_EQ(value, "2024-02-29");

        MetaStore dirty_only_source;
        const BlockId dirty_block = dirty_only_source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&dirty_only_source, dirty_block, 55U, 0U,
                             "20240229", EntryFlags::Dirty);
        (void)add_iptc_bytes(&dirty_only_source, dirty_block, 60U, 1U,
                             "120000");
        dirty_only_source.finalize();
        MetadataIptcDateToXmpOptions dirty_options;
        dirty_options.source_mode = MetadataDateTranslationSourceMode::DirtyOnly;
        MetaStore dirty_output;
        const MetadataIptcDateToXmpResult dirty_result
            = translate_iptc_dates_to_xmp(dirty_only_source, dirty_options,
                                          &dirty_output);
        ASSERT_EQ(dirty_result.status, Status::Ok);
        EXPECT_EQ(dirty_result.source_properties, 2U);
        ASSERT_TRUE(
            find_xmp_text(dirty_output, kPhotoshopNs, "DateCreated", &value));
        EXPECT_EQ(value, "2024-02-29T12:00:00");

        MetaStore dirty_time_source;
        const BlockId dirty_time_block = dirty_time_source.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&dirty_time_source, dirty_time_block, 55U, 0U,
                             "20240229");
        (void)add_iptc_bytes(&dirty_time_source, dirty_time_block, 60U, 1U,
                             "120000", EntryFlags::Dirty);
        dirty_time_source.finalize();
        MetaStore dirty_time_output;
        const MetadataIptcDateToXmpResult dirty_time_result
            = translate_iptc_dates_to_xmp(dirty_time_source, dirty_options,
                                          &dirty_time_output);
        ASSERT_EQ(dirty_time_result.status, Status::Ok);
        EXPECT_EQ(dirty_time_result.source_properties, 2U);
        ASSERT_TRUE(find_xmp_text(dirty_time_output, kPhotoshopNs,
                                  "DateCreated", &value));
        EXPECT_EQ(value, "2024-02-29T12:00:00");

        MetaStore clean_source;
        const BlockId clean_block = clean_source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&clean_source, clean_block, 55U, 0U, "20240229");
        (void)add_iptc_bytes(&clean_source, clean_block, 60U, 1U, "120000");
        clean_source.finalize();
        MetaStore clean_output;
        const MetadataIptcDateToXmpResult clean_result
            = translate_iptc_dates_to_xmp(clean_source, dirty_options,
                                          &clean_output);
        ASSERT_EQ(clean_result.status, Status::Ok);
        EXPECT_EQ(clean_result.source_properties, 0U);
        EXPECT_EQ(clean_result.groups_translated, 0U);
        EXPECT_EQ(clean_output.entries().size(), clean_source.entries().size());

        MetadataIptcDateToXmpOptions disabled_created;
        disabled_created.created = false;
        MetaStore disabled_output;
        const MetadataIptcDateToXmpResult disabled_result
            = translate_iptc_dates_to_xmp(clean_source, disabled_created,
                                          &disabled_output);
        ASSERT_EQ(disabled_result.status, Status::Ok);
        EXPECT_EQ(disabled_result.source_properties, 0U);
        EXPECT_EQ(disabled_result.groups_translated, 0U);

        MetaStore disabled_digital_source;
        const BlockId disabled_digital_block
            = disabled_digital_source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&disabled_digital_source, disabled_digital_block,
                             62U, 0U, "20240229");
        disabled_digital_source.finalize();
        MetadataIptcDateToXmpOptions disabled_digital;
        disabled_digital.digital_creation = false;
        MetaStore disabled_digital_output;
        const MetadataIptcDateToXmpResult disabled_digital_result
            = translate_iptc_dates_to_xmp(disabled_digital_source,
                                          disabled_digital,
                                          &disabled_digital_output);
        ASSERT_EQ(disabled_digital_result.status, Status::Ok);
        EXPECT_EQ(disabled_digital_result.source_properties, 0U);
        EXPECT_EQ(disabled_digital_result.groups_translated, 0U);

        MetaStore clean_tombstone_source;
        (void)clean_tombstone_source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&clean_tombstone_source, 99U, 55U, 0U, "ignored",
                             EntryFlags::Deleted);
        clean_tombstone_source.finalize();
        MetaStore clean_tombstone_output;
        const MetadataIptcDateToXmpResult clean_tombstone_result
            = translate_iptc_dates_to_xmp(clean_tombstone_source,
                                          MetadataIptcDateToXmpOptions {},
                                          &clean_tombstone_output);
        ASSERT_EQ(clean_tombstone_result.status, Status::Ok);
        EXPECT_EQ(clean_tombstone_result.source_properties, 0U);

        MetaStore unrelated_source;
        const BlockId unrelated_block = unrelated_source.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&unrelated_source, unrelated_block, 64U, 0U,
                             "not-a-date");
        Entry other_record;
        other_record.key          = make_iptc_dataset_key(1U, 55U);
        other_record.value        = make_bytes(unrelated_source.arena(),
                                               as_bytes("not-a-date"));
        other_record.origin.block = unrelated_block;
        (void)unrelated_source.add_entry(other_record);
        unrelated_source.finalize();
        MetaStore unrelated_output;
        const MetadataIptcDateToXmpResult unrelated_result
            = translate_iptc_dates_to_xmp(unrelated_source,
                                          MetadataIptcDateToXmpOptions {},
                                          &unrelated_output);
        ASSERT_EQ(unrelated_result.status, Status::Ok);
        EXPECT_EQ(unrelated_result.source_properties, 0U);
        EXPECT_EQ(unrelated_result.groups_translated, 0U);
    }

    TEST(IptcDatesToXmp, TombstonesSelectGroupsAndActiveValuesWin)
    {
        MetaStore date_only_source;
        const BlockId date_block = date_only_source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&date_only_source, date_block, 55U, 7U,
                             "20240229");
        (void)add_iptc_bytes(&date_only_source, 999U, 60U, 9U, "ignored",
                             EntryFlags::Deleted | EntryFlags::Dirty
                                 | EntryFlags::Unreadable);
        date_only_source.finalize();
        MetaStore date_only_output;
        const MetadataIptcDateToXmpResult date_only
            = translate_iptc_dates_to_xmp(date_only_source,
                                          MetadataIptcDateToXmpOptions {},
                                          &date_only_output);
        ASSERT_EQ(date_only.status, Status::Ok);
        EXPECT_EQ(date_only.source_properties, 2U);
        std::string_view value;
        ASSERT_TRUE(find_xmp_text(date_only_output, kPhotoshopNs, "DateCreated",
                                  &value));
        EXPECT_EQ(value, "2024-02-29");
        MetadataIptcDateToXmpOptions dirty_tombstone_mode;
        dirty_tombstone_mode.source_mode
            = MetadataDateTranslationSourceMode::DirtyOnly;
        MetaStore dirty_tombstone_output;
        const MetadataIptcDateToXmpResult dirty_tombstone_result
            = translate_iptc_dates_to_xmp(date_only_source,
                                          dirty_tombstone_mode,
                                          &dirty_tombstone_output);
        ASSERT_EQ(dirty_tombstone_result.status, Status::Ok);
        EXPECT_EQ(dirty_tombstone_result.source_properties, 2U);
        ASSERT_TRUE(find_xmp_text(dirty_tombstone_output, kPhotoshopNs,
                                  "DateCreated", &value));
        EXPECT_EQ(value, "2024-02-29");

        MetaStore active_time_with_date_tombstone;
        const BlockId active_block = active_time_with_date_tombstone.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&active_time_with_date_tombstone, 999U, 55U, 0U,
                             "ignored",
                             EntryFlags::Deleted | EntryFlags::Dirty);
        (void)add_iptc_bytes(&active_time_with_date_tombstone, active_block,
                             60U, 1U, "120000");
        active_time_with_date_tombstone.finalize();
        expect_failure_preserves_output(active_time_with_date_tombstone,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::IncompleteSource);

        MetaStore lone_time_tombstone;
        (void)lone_time_tombstone.add_block(BlockInfo {});
        (void)add_iptc_bytes(&lone_time_tombstone, 999U, 60U, 0U, "ignored",
                             EntryFlags::Deleted | EntryFlags::Dirty);
        lone_time_tombstone.finalize();
        expect_failure_preserves_output(lone_time_tombstone,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::IncompleteSource);

        MetaStore replacement_source;
        const BlockId replacement_block = replacement_source.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&replacement_source, replacement_block, 55U, 4U,
                             "20240229");
        (void)add_iptc_bytes(&replacement_source, 999U, 55U, 5U, "ignored",
                             EntryFlags::Deleted | EntryFlags::Dirty
                                 | EntryFlags::Unreadable
                                 | EntryFlags::Truncated);
        replacement_source.finalize();
        MetaStore replacement_output;
        const MetadataIptcDateToXmpResult replacement
            = translate_iptc_dates_to_xmp(replacement_source,
                                          MetadataIptcDateToXmpOptions {},
                                          &replacement_output);
        ASSERT_EQ(replacement.status, Status::Ok);
        EXPECT_EQ(replacement.source_properties, 2U);
        ASSERT_TRUE(find_xmp_text(replacement_output, kPhotoshopNs,
                                  "DateCreated", &value));
        EXPECT_EQ(value, "2024-02-29");

        MetaStore active_time_replacement;
        const BlockId time_replacement_block
            = active_time_replacement.add_block(BlockInfo {});
        (void)add_iptc_bytes(&active_time_replacement, time_replacement_block,
                             55U, 0U, "20240229");
        (void)add_iptc_bytes(&active_time_replacement, time_replacement_block,
                             60U, 1U, "120000");
        (void)add_iptc_bytes(&active_time_replacement, 999U, 60U, 2U, "240000",
                             EntryFlags::Deleted | EntryFlags::Dirty
                                 | EntryFlags::Unreadable);
        active_time_replacement.finalize();
        MetaStore active_time_output;
        const MetadataIptcDateToXmpResult active_time_result
            = translate_iptc_dates_to_xmp(active_time_replacement,
                                          MetadataIptcDateToXmpOptions {},
                                          &active_time_output);
        ASSERT_EQ(active_time_result.status, Status::Ok);
        ASSERT_TRUE(find_xmp_text(active_time_output, kPhotoshopNs,
                                  "DateCreated", &value));
        EXPECT_EQ(value, "2024-02-29T12:00:00");

        MetaStore remove_source;
        const BlockId remove_block = remove_source.add_block(BlockInfo {});
        const EntryId old_date     = add_xmp_text(&remove_source, remove_block,
                                                  kPhotoshopNs, "DateCreated",
                                                  "2020-01-01", 3U);
        MetaValue invalid_tombstone_value = make_bytes(remove_source.arena(),
                                                       as_bytes("ignored"));
        invalid_tombstone_value.count     = 8U;
        invalid_tombstone_value.data.span.offset
            = std::numeric_limits<uint32_t>::max();
        invalid_tombstone_value.data.span.size = 8U;
        const EntryId native_tombstone         = add_iptc_value(
            &remove_source, 999U, 55U, 0U, invalid_tombstone_value,
            EntryFlags::Deleted | EntryFlags::Dirty | EntryFlags::Unreadable);
        remove_source.finalize();
        expect_failure_preserves_output(remove_source,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::DestinationConflict);
        MetadataIptcDateToXmpOptions preserve_removal;
        preserve_removal.conflict_policy
            = MetadataIptcToXmpConflictPolicy::PreserveExisting;
        MetaStore preserved_removal;
        const MetadataIptcDateToXmpResult kept
            = translate_iptc_dates_to_xmp(remove_source, preserve_removal,
                                          &preserved_removal);
        ASSERT_EQ(kept.status, Status::Ok);
        EXPECT_EQ(kept.groups_preserved, 1U);
        EXPECT_EQ(kept.entries_removed, 0U);
        ASSERT_TRUE(find_xmp_text(preserved_removal, kPhotoshopNs,
                                  "DateCreated", &value));
        EXPECT_EQ(value, "2020-01-01");
        MetadataIptcDateToXmpOptions replace;
        replace.conflict_policy
            = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        MetaStore remove_output = make_sentinel_output();
        const MetadataIptcDateToXmpResult removed
            = translate_iptc_dates_to_xmp(remove_source, replace,
                                          &remove_output);
        ASSERT_EQ(removed.status, Status::Ok);
        EXPECT_EQ(removed.source_properties, 1U);
        EXPECT_EQ(removed.groups_translated, 1U);
        EXPECT_EQ(removed.entries_removed, 1U);
        EXPECT_TRUE(
            any(remove_output.entry(old_date).flags, EntryFlags::Deleted));
        EXPECT_TRUE(any(remove_output.entry(native_tombstone).flags,
                        EntryFlags::Deleted));
        EXPECT_TRUE(any(remove_output.entry(native_tombstone).flags,
                        EntryFlags::Unreadable));
        EXPECT_EQ(remove_output.entry(native_tombstone).value.count, 8U);
        EXPECT_EQ(remove_output.entry(native_tombstone).value.data.span.offset,
                  std::numeric_limits<uint32_t>::max());
        EXPECT_EQ(remove_source.entry(old_date).flags, EntryFlags::Dirty);

        MetaStore no_payload_budget_source;
        (void)no_payload_budget_source.add_block(BlockInfo {});
        const EntryId no_payload_budget_id
            = add_iptc_value(&no_payload_budget_source, 999U, 55U, 0U,
                             invalid_tombstone_value,
                             EntryFlags::Deleted | EntryFlags::Dirty);
        no_payload_budget_source.finalize();
        MetadataIptcDateToXmpOptions small_budget;
        small_budget.max_total_text_bytes = 1U;
        MetaStore no_payload_budget_output;
        const MetadataIptcDateToXmpResult no_payload_budget
            = translate_iptc_dates_to_xmp(no_payload_budget_source,
                                          small_budget,
                                          &no_payload_budget_output);
        ASSERT_EQ(no_payload_budget.status, Status::Ok);
        EXPECT_EQ(no_payload_budget.source_properties, 1U);
        EXPECT_EQ(no_payload_budget_output.entry(no_payload_budget_id)
                      .value.data.span.offset,
                  std::numeric_limits<uint32_t>::max());

        MetaStore unknown_tombstone;
        (void)unknown_tombstone.add_block(BlockInfo {});
        (void)add_iptc_bytes(&unknown_tombstone, 999U, 55U, 0U, "ignored",
                             EntryFlags::Deleted | EntryFlags::Dirty
                                 | static_cast<EntryFlags>(0x80U));
        unknown_tombstone.finalize();
        expect_failure_preserves_output(unknown_tombstone, replace,
                                        Status::InvalidSourceValue);
    }

    TEST(IptcDatesToXmp, RejectsMalformedSourcesAndAmbiguousProvenance)
    {
        EXPECT_EQ(translate_native_value(MetaValueKind::Empty, "20240229"),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Scalar, "20240229"),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Array, "20240229"),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Bytes, "20240229",
                                         TextEncoding::Utf16LE),
                  Status::Ok);
        EXPECT_EQ(translate_native_value(MetaValueKind::Bytes, "20240229",
                                         TextEncoding::Unknown,
                                         EntryFlags::Unreadable),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Bytes, "20240229",
                                         TextEncoding::Unknown,
                                         EntryFlags::Truncated),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Bytes, "20240229",
                                         TextEncoding::Unknown,
                                         static_cast<EntryFlags>(0x80U)),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Bytes, "20240229",
                                         TextEncoding::Unknown,
                                         EntryFlags::None, 999U),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Bytes, "20240229",
                                         TextEncoding::Unknown,
                                         EntryFlags::None, 0U, true),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Bytes, "20240229",
                                         TextEncoding::Unknown,
                                         EntryFlags::None, 0U, false, true),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Text, "20240229",
                                         TextEncoding::Utf8, EntryFlags::None,
                                         0U, true),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Text, "20240229",
                                         TextEncoding::Utf8, EntryFlags::None,
                                         0U, false, true),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Text, "20240229",
                                         TextEncoding::Unknown),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Text, "20240229",
                                         TextEncoding::Utf16LE),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Text,
                                         "2024\xff"
                                         "229",
                                         TextEncoding::Ascii),
                  Status::InvalidSourceValue);
        EXPECT_EQ(translate_native_value(MetaValueKind::Bytes, "2024\xff"
                                                               "229"),
                  Status::InvalidDateTime);

        MetaStore duplicate_date;
        const BlockId duplicate_block = duplicate_date.add_block(BlockInfo {});
        (void)add_iptc_bytes(&duplicate_date, duplicate_block, 55U, 0U,
                             "20240229");
        (void)add_iptc_bytes(&duplicate_date, duplicate_block, 55U, 1U,
                             "20240229");
        duplicate_date.finalize();
        expect_failure_preserves_output(duplicate_date,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::AmbiguousSource);

        MetaStore duplicate_time;
        const BlockId time_block = duplicate_time.add_block(BlockInfo {});
        (void)add_iptc_bytes(&duplicate_time, time_block, 55U, 0U, "20240229");
        (void)add_iptc_bytes(&duplicate_time, time_block, 60U, 1U, "120000");
        (void)add_iptc_bytes(&duplicate_time, time_block, 60U, 2U, "120000");
        duplicate_time.finalize();
        expect_failure_preserves_output(duplicate_time,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::AmbiguousSource);

        MetaStore mismatched_blocks;
        const BlockId first_block  = mismatched_blocks.add_block(BlockInfo {});
        const BlockId second_block = mismatched_blocks.add_block(BlockInfo {});
        (void)add_iptc_bytes(&mismatched_blocks, first_block, 55U, 0U,
                             "20240229");
        (void)add_iptc_bytes(&mismatched_blocks, second_block, 60U, 0U,
                             "120000");
        mismatched_blocks.finalize();
        expect_failure_preserves_output(mismatched_blocks,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::AmbiguousSource);

        MetaStore authored_pair;
        (void)authored_pair.add_block(BlockInfo {});
        (void)add_iptc_bytes(&authored_pair, kInvalidBlockId, 55U, 13U,
                             "20240229");
        (void)add_iptc_bytes(&authored_pair, kInvalidBlockId, 60U, 14U,
                             "120000");
        authored_pair.finalize();
        MetaStore authored_output;
        const MetadataIptcDateToXmpResult authored
            = translate_iptc_dates_to_xmp(authored_pair,
                                          MetadataIptcDateToXmpOptions {},
                                          &authored_output);
        ASSERT_EQ(authored.status, Status::Ok);
        EntryId authored_id = kInvalidEntryId;
        std::string_view authored_text;
        ASSERT_TRUE(find_xmp_text(authored_output, kPhotoshopNs, "DateCreated",
                                  &authored_text, &authored_id));
        EXPECT_EQ(authored_text, "2024-02-29T12:00:00");
        EXPECT_EQ(authored_output.entry(authored_id).origin.block,
                  kInvalidBlockId);
        EXPECT_EQ(authored_output.entry(authored_id).origin.order_in_block,
                  13U);
    }

    TEST(IptcDatesToXmp, ValidatesCalendarPrecisionAndTimeBoundaries)
    {
        struct Input final {
            std::string_view date;
            Status expected;
        };
        static constexpr std::array<Input, 10U> dates = { {
            { "20240229", Status::Ok },
            { "20230229", Status::InvalidDateTime },
            { "20000229", Status::Ok },
            { "19000229", Status::InvalidDateTime },
            { "20241301", Status::InvalidDateTime },
            { "20240230", Status::InvalidDateTime },
            { "20240001", Status::UnsupportedPrecision },
            { "00000000", Status::UnsupportedPrecision },
            { "2024022x", Status::InvalidDateTime },
            { "2024022", Status::InvalidDateTime },
        } };
        for (const Input& input : dates) {
            EXPECT_EQ(translate_pair(input.date).status, input.expected)
                << input.date;
        }

        struct TimeInput final {
            std::string_view time;
            Status expected;
        };
        static constexpr std::array<TimeInput, 10U> times = { {
            { "000000", Status::Ok },
            { "235960+2359", Status::Ok },
            { "240000", Status::InvalidDateTime },
            { "236000", Status::InvalidDateTime },
            { "235961", Status::InvalidDateTime },
            { "120000+2400", Status::InvalidDateTime },
            { "120000+2360", Status::InvalidDateTime },
            { "120000/0000", Status::InvalidDateTime },
            { "12000", Status::InvalidDateTime },
            { "120000+23590", Status::InvalidDateTime },
        } };
        for (const TimeInput& input : times) {
            EXPECT_EQ(translate_pair("20240229", true, input.time).status,
                      input.expected)
                << input.time;
        }
        EXPECT_EQ(translate_pair("20240500", true, "120000").status,
                  Status::UnsupportedPrecision);
    }

    TEST(IptcDatesToXmp, AppliesConflictPoliciesAndPreservesProvenance)
    {
        MetaStore exact_source;
        const BlockId native_block = exact_source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&exact_source, native_block, 55U, 41U, "20240229");
        const BlockId target_block = exact_source.add_block(BlockInfo {});
        const EntryId exact_target = add_xmp_text(&exact_source, target_block,
                                                  kPhotoshopNs, "DateCreated",
                                                  "2024-02-29", 27U);
        exact_source.finalize();
        MetaStore exact_output;
        const MetadataIptcDateToXmpResult exact = translate_iptc_dates_to_xmp(
            exact_source, MetadataIptcDateToXmpOptions {}, &exact_output);
        ASSERT_EQ(exact.status, Status::Ok);
        EXPECT_EQ(exact.groups_unchanged, 1U);
        EXPECT_EQ(exact.entries_added + exact.entries_updated
                      + exact.entries_removed,
                  0U);
        EXPECT_EQ(exact_output.entry(exact_target).origin.block, target_block);
        EXPECT_EQ(exact_output.entry(exact_target).origin.order_in_block, 27U);

        MetaStore conflict_source;
        const BlockId conflict_block = conflict_source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&conflict_source, conflict_block, 55U, 0U,
                             "20240229");
        const EntryId conflict_target
            = add_xmp_text(&conflict_source, conflict_block, kPhotoshopNs,
                           "DateCreated", "2020-01-01", 33U);
        conflict_source.finalize();
        expect_failure_preserves_output(conflict_source,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::DestinationConflict);

        MetadataIptcDateToXmpOptions replace;
        replace.conflict_policy
            = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        MetaStore replaced_output;
        const MetadataIptcDateToXmpResult replaced
            = translate_iptc_dates_to_xmp(conflict_source, replace,
                                          &replaced_output);
        ASSERT_EQ(replaced.status, Status::Ok);
        EXPECT_EQ(replaced.entries_updated, 1U);
        std::string_view value;
        ASSERT_TRUE(find_xmp_text(replaced_output, kPhotoshopNs, "DateCreated",
                                  &value));
        EXPECT_EQ(value, "2024-02-29");
        const Entry& updated = replaced_output.entry(conflict_target);
        EXPECT_EQ(updated.origin.block, conflict_block);
        EXPECT_EQ(updated.origin.order_in_block, 33U);
        EXPECT_EQ(updated.flags, EntryFlags::Dirty);

        MetaStore opaque_source;
        const BlockId opaque_block = opaque_source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&opaque_source, opaque_block, 55U, 0U, "20240229");
        const EntryId opaque_target
            = add_xmp_value(&opaque_source, opaque_block, kPhotoshopNs,
                            "DateCreated", make_u32(17U));
        opaque_source.finalize();
        MetadataIptcDateToXmpOptions preserve;
        preserve.conflict_policy
            = MetadataIptcToXmpConflictPolicy::PreserveExisting;
        MetaStore preserved_output;
        const MetadataIptcDateToXmpResult preserved
            = translate_iptc_dates_to_xmp(opaque_source, preserve,
                                          &preserved_output);
        ASSERT_EQ(preserved.status, Status::Ok);
        EXPECT_EQ(preserved.groups_preserved, 1U);
        EXPECT_EQ(preserved.entries_added + preserved.entries_updated
                      + preserved.entries_removed,
                  0U);
        EXPECT_EQ(preserved_output.entry(opaque_target).value.kind,
                  MetaValueKind::Scalar);

        MetaStore provenance_source;
        const BlockId provenance_block = provenance_source.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&provenance_source, provenance_block, 55U, 73U,
                             "20240229");
        provenance_source.finalize();
        MetaStore provenance_output;
        const MetadataIptcDateToXmpResult provenance
            = translate_iptc_dates_to_xmp(provenance_source,
                                          MetadataIptcDateToXmpOptions {},
                                          &provenance_output);
        ASSERT_EQ(provenance.status, Status::Ok);
        const EntryId added_id = find_xmp_entry(provenance_output, kPhotoshopNs,
                                                "DateCreated");
        ASSERT_NE(added_id, kInvalidEntryId);
        EXPECT_EQ(provenance_output.entry(added_id).origin.block,
                  provenance_block);
        EXPECT_EQ(provenance_output.entry(added_id).origin.order_in_block, 73U);

        MetaStore alias_source;
        const BlockId alias_block = alias_source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&alias_source, alias_block, 55U, 2U, "20240229");
        alias_source.finalize();
        const MetadataIptcDateToXmpResult aliased = translate_iptc_dates_to_xmp(
            alias_source, MetadataIptcDateToXmpOptions {}, &alias_source);
        ASSERT_EQ(aliased.status, Status::Ok);
        ASSERT_TRUE(
            find_xmp_text(alias_source, kPhotoshopNs, "DateCreated", &value));
        EXPECT_EQ(value, "2024-02-29");

        MetaStore failed_alias;
        const BlockId failed_alias_block = failed_alias.add_block(BlockInfo {});
        (void)add_iptc_bytes(&failed_alias, failed_alias_block, 55U, 0U,
                             "20240229");
        (void)add_iptc_bytes(&failed_alias, failed_alias_block, 62U, 1U,
                             "20240101");
        (void)add_iptc_bytes(&failed_alias, failed_alias_block, 63U, 2U,
                             "240000");
        failed_alias.finalize();
        const size_t failed_alias_entries = failed_alias.entries().size();
        const MetadataIptcDateToXmpResult failed_in_place
            = translate_iptc_dates_to_xmp(failed_alias,
                                          MetadataIptcDateToXmpOptions {},
                                          &failed_alias);
        EXPECT_EQ(failed_in_place.status, Status::InvalidDateTime);
        EXPECT_EQ(failed_alias.entries().size(), failed_alias_entries);
        EXPECT_EQ(find_xmp_entry(failed_alias, kPhotoshopNs, "DateCreated"),
                  kInvalidEntryId);
    }

    TEST(IptcDatesToXmp, RejectsUnsupportedDestinationShapesTransactionally)
    {
        MetaStore descendant;
        const BlockId descendant_block = descendant.add_block(BlockInfo {});
        (void)add_iptc_bytes(&descendant, descendant_block, 55U, 0U,
                             "20240229");
        (void)add_xmp_text(&descendant, descendant_block, kPhotoshopNs,
                           "DateCreated[1]", "2024-02-29");
        descendant.finalize();
        expect_failure_preserves_output(descendant,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::UnsupportedDestinationShape);

        MetaStore duplicate_target;
        const BlockId duplicate_block = duplicate_target.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&duplicate_target, duplicate_block, 55U, 0U,
                             "20240229");
        (void)add_xmp_text(&duplicate_target, duplicate_block, kPhotoshopNs,
                           "DateCreated", "2024-02-29");
        (void)add_xmp_text(&duplicate_target, duplicate_block, kPhotoshopNs,
                           "DateCreated", "2024-02-29");
        duplicate_target.finalize();
        expect_failure_preserves_output(duplicate_target,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::UnsupportedDestinationShape);

        MetaStore unsupported_value;
        const BlockId unsupported_block = unsupported_value.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&unsupported_value, unsupported_block, 55U, 0U,
                             "20240229");
        const EntryId unsupported_id
            = add_xmp_value(&unsupported_value, unsupported_block, kPhotoshopNs,
                            "DateCreated",
                            make_text(unsupported_value.arena(), "2024-02-29",
                                      TextEncoding::Utf16LE));
        unsupported_value.finalize();
        expect_failure_preserves_output(unsupported_value,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::UnsupportedDestinationShape);
        EXPECT_EQ(unsupported_value.entry(unsupported_id).value.kind,
                  MetaValueKind::Text);

        MetaStore scalar_destination;
        const BlockId scalar_block = scalar_destination.add_block(BlockInfo {});
        (void)add_iptc_bytes(&scalar_destination, scalar_block, 55U, 0U,
                             "20240229");
        (void)add_xmp_value(&scalar_destination, scalar_block, kPhotoshopNs,
                            "DateCreated", make_u32(2024U));
        scalar_destination.finalize();
        MetadataIptcDateToXmpOptions replace_scalar;
        replace_scalar.conflict_policy
            = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        expect_failure_preserves_output(scalar_destination, replace_scalar,
                                        Status::UnsupportedDestinationShape);

        MetaStore invalid_utf8;
        const BlockId utf8_block = invalid_utf8.add_block(BlockInfo {});
        (void)add_iptc_bytes(&invalid_utf8, utf8_block, 55U, 0U, "20240229");
        (void)add_xmp_text(&invalid_utf8, utf8_block, kPhotoshopNs,
                           "DateCreated", std::string_view("\xff", 1U));
        invalid_utf8.finalize();
        expect_failure_preserves_output(invalid_utf8,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::UnsupportedDestinationShape);

        MetaStore ascii_destination;
        const BlockId ascii_block = ascii_destination.add_block(BlockInfo {});
        (void)add_iptc_bytes(&ascii_destination, ascii_block, 55U, 0U,
                             "20240229");
        const MetaValue ascii_value = make_text(ascii_destination.arena(),
                                                std::string_view("\xff", 1U),
                                                TextEncoding::Ascii);
        (void)add_xmp_value(&ascii_destination, ascii_block, kPhotoshopNs,
                            "DateCreated", ascii_value);
        ascii_destination.finalize();
        expect_failure_preserves_output(ascii_destination,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::UnsupportedDestinationShape);

        MetaStore bad_count;
        const BlockId count_block = bad_count.add_block(BlockInfo {});
        (void)add_iptc_bytes(&bad_count, count_block, 55U, 0U, "20240229");
        MetaValue wrong_count = make_text(bad_count.arena(), "2024-02-29",
                                          TextEncoding::Utf8);
        ++wrong_count.count;
        (void)add_xmp_value(&bad_count, count_block, kPhotoshopNs,
                            "DateCreated", wrong_count);
        bad_count.finalize();
        expect_failure_preserves_output(bad_count,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::UnsupportedDestinationShape);

        MetaStore bad_span;
        const BlockId span_block = bad_span.add_block(BlockInfo {});
        (void)add_iptc_bytes(&bad_span, span_block, 55U, 0U, "20240229");
        MetaValue wrong_span        = make_text(bad_span.arena(), "2024-02-29",
                                                TextEncoding::Utf8);
        wrong_span.data.span.offset = std::numeric_limits<uint32_t>::max();
        (void)add_xmp_value(&bad_span, span_block, kPhotoshopNs, "DateCreated",
                            wrong_span);
        bad_span.finalize();
        expect_failure_preserves_output(bad_span,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::UnsupportedDestinationShape);

        MetaStore damaged_flags;
        const BlockId flags_block = damaged_flags.add_block(BlockInfo {});
        (void)add_iptc_bytes(&damaged_flags, flags_block, 55U, 0U, "20240229");
        (void)add_xmp_text(&damaged_flags, flags_block, kPhotoshopNs,
                           "DateCreated", "2024-02-29", 0U,
                           EntryFlags::Dirty | EntryFlags::Unreadable);
        damaged_flags.finalize();
        expect_failure_preserves_output(damaged_flags,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::UnsupportedDestinationShape);

        MetaStore unknown_destination_flags;
        const BlockId unknown_flags_block = unknown_destination_flags.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&unknown_destination_flags, unknown_flags_block,
                             55U, 0U, "20240229");
        (void)add_xmp_text(&unknown_destination_flags, unknown_flags_block,
                           kPhotoshopNs, "DateCreated", "2024-02-29", 0U,
                           EntryFlags::Dirty | static_cast<EntryFlags>(0x80U));
        unknown_destination_flags.finalize();
        expect_failure_preserves_output(unknown_destination_flags,
                                        MetadataIptcDateToXmpOptions {},
                                        Status::UnsupportedDestinationShape);
    }

    TEST(IptcDatesToXmp, EnforcesOptionsAndCountTextOperationLimits)
    {
        MetaStore empty;
        (void)empty.add_block(BlockInfo {});
        empty.finalize();
        EXPECT_EQ(translate_iptc_dates_to_xmp(empty,
                                              MetadataIptcDateToXmpOptions {},
                                              nullptr)
                      .status,
                  Status::NullOutput);
        MetaStore unfinalized;
        (void)unfinalized.add_block(BlockInfo {});
        MetaStore unfinalized_output = make_sentinel_output();
        EXPECT_EQ(translate_iptc_dates_to_xmp(unfinalized,
                                              MetadataIptcDateToXmpOptions {},
                                              &unfinalized_output)
                      .status,
                  Status::SourceNotFinalized);
        EXPECT_EQ(unfinalized_output.entries().size(), 1U);

        std::array<MetadataIptcDateToXmpOptions, 8U> invalid_options {};
        invalid_options[0U].max_source_properties = 0U;
        invalid_options[1U].max_source_properties
            = kMetadataIptcDateToXmpMaxSourceProperties + 1U;
        invalid_options[2U].max_added_entries = 0U;
        invalid_options[3U].max_added_entries
            = kMetadataIptcDateToXmpMaxAddedEntries + 1U;
        invalid_options[4U].max_operations = 0U;
        invalid_options[5U].max_operations = kMetadataIptcDateToXmpMaxOperations
                                             + 1U;
        invalid_options[6U].max_total_text_bytes = 0U;
        invalid_options[7U].source_mode
            = static_cast<MetadataDateTranslationSourceMode>(0xffU);
        for (const MetadataIptcDateToXmpOptions& options : invalid_options) {
            expect_failure_preserves_output(empty, options,
                                            Status::InvalidOptions);
        }
        MetadataIptcDateToXmpOptions invalid_policy;
        invalid_policy.conflict_policy
            = static_cast<MetadataIptcToXmpConflictPolicy>(0xffU);
        expect_failure_preserves_output(empty, invalid_policy,
                                        Status::InvalidOptions);
        MetadataIptcDateToXmpOptions invalid_text_limit;
        invalid_text_limit.max_total_text_bytes
            = kMetadataIptcDateToXmpMaxTotalTextBytes + 1U;
        expect_failure_preserves_output(empty, invalid_text_limit,
                                        Status::InvalidOptions);

        MetaStore two_groups;
        const BlockId two_block = two_groups.add_block(BlockInfo {});
        (void)add_iptc_bytes(&two_groups, two_block, 55U, 0U, "20240229");
        (void)add_iptc_bytes(&two_groups, two_block, 62U, 1U, "20240301");
        two_groups.finalize();
        MetaStore exact_limits_output;
        const MetadataIptcDateToXmpResult exact_limits
            = translate_iptc_dates_to_xmp(two_groups,
                                          MetadataIptcDateToXmpOptions {},
                                          &exact_limits_output);
        ASSERT_EQ(exact_limits.status, Status::Ok);
        EXPECT_EQ(exact_limits.entries_added,
                  kMetadataIptcDateToXmpMaxAddedEntries);
        EXPECT_EQ(exact_limits.entries_added,
                  kMetadataIptcDateToXmpMaxOperations);

        MetadataIptcDateToXmpOptions low_added;
        low_added.max_added_entries = 1U;
        expect_failure_preserves_output(two_groups, low_added,
                                        Status::EntryLimitExceeded);
        MetadataIptcDateToXmpOptions low_operations;
        low_operations.max_operations = 1U;
        expect_failure_preserves_output(two_groups, low_operations,
                                        Status::OperationLimitExceeded);

        MetaStore exact_source_bytes;
        const BlockId exact_bytes_block = exact_source_bytes.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&exact_source_bytes, exact_bytes_block, 55U, 0U,
                             "20240229");
        exact_source_bytes.finalize();
        MetadataIptcDateToXmpOptions exact_text_limit;
        exact_text_limit.max_total_text_bytes = 8U;
        MetaStore exact_text_output;
        EXPECT_EQ(translate_iptc_dates_to_xmp(exact_source_bytes,
                                              exact_text_limit,
                                              &exact_text_output)
                      .status,
                  Status::Ok);
        MetadataIptcDateToXmpOptions low_text_limit;
        low_text_limit.max_total_text_bytes = 7U;
        expect_failure_preserves_output(exact_source_bytes, low_text_limit,
                                        Status::SourceLimitExceeded);

        static constexpr std::array<uint32_t, 2U> source_counts = { 1024U,
                                                                    1025U };
        for (const uint32_t count : source_counts) {
            MetaStore counted;
            (void)counted.add_block(BlockInfo {});
            for (uint32_t i = 0U; i < count; ++i) {
                (void)add_iptc_bytes(&counted, 999U, 55U, i, "ignored",
                                     EntryFlags::Deleted | EntryFlags::Dirty);
            }
            counted.finalize();
            if (count == kMetadataIptcDateToXmpMaxSourceProperties) {
                MetaStore counted_output;
                const MetadataIptcDateToXmpResult exact_count
                    = translate_iptc_dates_to_xmp(
                        counted, MetadataIptcDateToXmpOptions {},
                        &counted_output);
                ASSERT_EQ(exact_count.status, Status::Ok);
                EXPECT_EQ(exact_count.source_properties, count);
            } else {
                expect_failure_preserves_output(counted,
                                                MetadataIptcDateToXmpOptions {},
                                                Status::SourceLimitExceeded);
            }
        }

        MetaStore lowered_count;
        (void)lowered_count.add_block(BlockInfo {});
        (void)add_iptc_bytes(&lowered_count, 999U, 55U, 0U, "ignored",
                             EntryFlags::Deleted | EntryFlags::Dirty);
        (void)add_iptc_bytes(&lowered_count, 999U, 55U, 1U, "ignored",
                             EntryFlags::Deleted | EntryFlags::Dirty);
        lowered_count.finalize();
        MetadataIptcDateToXmpOptions low_source_count;
        low_source_count.max_source_properties = 1U;
        expect_failure_preserves_output(lowered_count, low_source_count,
                                        Status::SourceLimitExceeded);
    }

    TEST(IptcDatesToXmp, PreflightsCombinedTextBudgetAndLateFailures)
    {
        static constexpr std::array<size_t, 2U> target_sizes = { 8184U, 8185U };
        for (const size_t target_size : target_sizes) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            (void)add_iptc_bytes(&source, block, 55U, 0U, "20240229");
            std::string target(target_size, 'x');
            if (target_size == 8185U) {
                target[0U] = static_cast<char>(0xffU);
            }
            (void)add_xmp_text(&source, block, kPhotoshopNs, "DateCreated",
                               target);
            source.finalize();
            MetaStore output      = make_sentinel_output();
            const size_t old_size = output.entries().size();
            const MetadataIptcDateToXmpResult result
                = translate_iptc_dates_to_xmp(source,
                                              MetadataIptcDateToXmpOptions {},
                                              &output);
            if (target_size == 8184U) {
                EXPECT_EQ(result.status, Status::DestinationConflict);
                MetadataIptcDateToXmpOptions replace;
                replace.conflict_policy
                    = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
                MetaStore replaced_output;
                const MetadataIptcDateToXmpResult replaced
                    = translate_iptc_dates_to_xmp(source, replace,
                                                  &replaced_output);
                ASSERT_EQ(replaced.status, Status::Ok);
                EXPECT_EQ(replaced.entries_updated, 1U);
                std::string_view value;
                ASSERT_TRUE(find_xmp_text(replaced_output, kPhotoshopNs,
                                          "DateCreated", &value));
                EXPECT_EQ(value, "2024-02-29");
            } else {
                EXPECT_EQ(result.status, Status::SourceLimitExceeded);
            }
            EXPECT_EQ(result.entries_added, 0U);
            EXPECT_EQ(output.entries().size(), old_size);
            std::string_view sentinel;
            ASSERT_TRUE(
                find_xmp_text(output, "urn:date-test", "Sentinel", &sentinel));
            EXPECT_EQ(sentinel, "preserve");
        }

        MetaStore late_failure;
        const BlockId late_block = late_failure.add_block(BlockInfo {});
        (void)add_iptc_bytes(&late_failure, late_block, 55U, 0U, "20240229");
        (void)add_iptc_bytes(&late_failure, late_block, 62U, 1U, "20240101");
        const EntryId bad_time = add_iptc_bytes(&late_failure, late_block, 63U,
                                                2U, "240000");
        late_failure.finalize();
        MetaStore output                         = make_sentinel_output();
        const MetadataIptcDateToXmpResult failed = translate_iptc_dates_to_xmp(
            late_failure, MetadataIptcDateToXmpOptions {}, &output);
        EXPECT_EQ(failed.status, Status::InvalidDateTime);
        EXPECT_EQ(failed.failed_mapping,
                  MetadataDateTranslationMapping::XmpCreateDate);
        EXPECT_EQ(failed.failed_source_entry, bad_time);
        EXPECT_EQ(failed.entries_added, 0U);
        EXPECT_EQ(failed.entries_updated, 0U);
        EXPECT_EQ(failed.entries_removed, 0U);
        EXPECT_EQ(output.entries().size(), 1U);
        std::string_view sentinel;
        ASSERT_TRUE(
            find_xmp_text(output, "urn:date-test", "Sentinel", &sentinel));
        EXPECT_EQ(sentinel, "preserve");

        MetaStore arena_limited;
        const BlockId limited_block = arena_limited.add_block(BlockInfo {});
        (void)add_iptc_bytes(&arena_limited, limited_block, 55U, 0U,
                             "20240229");
        arena_limited.arena().constrain_max_size(
            arena_limited.arena().bytes().size());
        arena_limited.finalize();
        MetaStore limited_output = make_sentinel_output();
        const MetadataIptcDateToXmpResult arena_failure
            = translate_iptc_dates_to_xmp(arena_limited,
                                          MetadataIptcDateToXmpOptions {},
                                          &limited_output);
        EXPECT_EQ(arena_failure.status, Status::InternalError);
        EXPECT_EQ(arena_failure.entries_added, 0U);
        EXPECT_EQ(limited_output.entries().size(), 1U);
    }

    TEST(IptcDatesToXmp, ReportsStableStatusNames)
    {
        struct Name final {
            Status status;
            const char* text;
        };
        static constexpr std::array<Name, 15U> names = { {
            { Status::Ok, "ok" },
            { Status::NullOutput, "null_output" },
            { Status::SourceNotFinalized, "source_not_finalized" },
            { Status::InvalidOptions, "invalid_options" },
            { Status::SourceLimitExceeded, "source_limit_exceeded" },
            { Status::AmbiguousSource, "ambiguous_source" },
            { Status::InvalidSourceValue, "invalid_source_value" },
            { Status::InvalidDateTime, "invalid_date_time" },
            { Status::UnsupportedPrecision, "unsupported_precision" },
            { Status::IncompleteSource, "incomplete_source" },
            { Status::DestinationConflict, "destination_conflict" },
            { Status::UnsupportedDestinationShape,
              "unsupported_destination_shape" },
            { Status::EntryLimitExceeded, "entry_limit_exceeded" },
            { Status::OperationLimitExceeded, "operation_limit_exceeded" },
            { Status::InternalError, "internal_error" },
        } };
        for (const Name& name : names) {
            EXPECT_STREQ(metadata_iptc_date_to_xmp_status_name(name.status),
                         name.text);
        }
        EXPECT_STREQ(metadata_iptc_date_to_xmp_status_name(
                         static_cast<Status>(0xffU)),
                     "unknown");
    }

}  // namespace
}  // namespace openmeta
