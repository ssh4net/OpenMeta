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
#include <span>
#include <string>
#include <string_view>

namespace openmeta {
namespace {

    static EntryId add_iptc_bytes(MetaStore* store, BlockId block,
                                  uint16_t record, uint16_t dataset,
                                  uint32_t order, std::string_view bytes,
                                  EntryFlags flags = EntryFlags::None)
    {
        Entry entry;
        entry.key = make_iptc_dataset_key(record, dataset);
        entry.value
            = make_bytes(store->arena(),
                         std::span<const std::byte>(
                             reinterpret_cast<const std::byte*>(bytes.data()),
                             bytes.size()));
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.flags                 = flags;
        return store->add_entry(entry);
    }

    static EntryId add_iptc_text(MetaStore* store, BlockId block,
                                 uint16_t record, uint16_t dataset,
                                 uint32_t order, std::string_view text,
                                 TextEncoding encoding,
                                 EntryFlags flags = EntryFlags::None)
    {
        Entry entry;
        entry.key                   = make_iptc_dataset_key(record, dataset);
        entry.value                 = make_text(store->arena(), text, encoding);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.flags                 = flags;
        return store->add_entry(entry);
    }

    static EntryId add_xmp_text(MetaStore* store, BlockId block,
                                std::string_view schema, std::string_view path,
                                std::string_view text,
                                TextEncoding encoding = TextEncoding::Utf8,
                                EntryFlags flags      = EntryFlags::None,
                                uint32_t order        = 0U)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store->arena(), schema, path);
        entry.value = make_text(store->arena(), text, encoding);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.flags                 = flags;
        return store->add_entry(entry);
    }

    static bool find_xmp_text(const MetaStore& store, std::string_view schema,
                              std::string_view path, std::string_view* out_text,
                              EntryId* out_id = nullptr) noexcept
    {
        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& entry = store.entry(id);
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            const std::span<const std::byte> schema_bytes = store.arena().span(
                entry.key.data.xmp_property.schema_ns);
            const std::span<const std::byte> path_bytes = store.arena().span(
                entry.key.data.xmp_property.property_path);
            const std::string_view entry_schema(reinterpret_cast<const char*>(
                                                    schema_bytes.data()),
                                                schema_bytes.size());
            const std::string_view entry_path(reinterpret_cast<const char*>(
                                                  path_bytes.data()),
                                              path_bytes.size());
            if (entry_schema != schema || entry_path != path
                || entry.value.kind != MetaValueKind::Text) {
                continue;
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
        return false;
    }

    static MetaStore make_sentinel_output()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_xmp_text(&store, block, "urn:openmeta:test", "Sentinel",
                           "keep");
        store.finalize();
        return store;
    }

    static MetaStore make_block_source(bool invalid_late_category,
                                       bool constrain_arena = false)
    {
        MetaStore store;
        const BlockId utf8_block  = store.add_block(BlockInfo {});
        const BlockId ascii_block = store.add_block(BlockInfo {});
        (void)add_iptc_bytes(&store, utf8_block, 1U, 90U, 0U, "\x1b%G");
        (void)add_iptc_bytes(&store, utf8_block, 2U, 5U, 1U,
                             "\xE6\x9D\xB1\xE4\xBA\xAC");
        (void)add_iptc_bytes(&store, ascii_block, 2U, 90U, 0U, "Kyoto");
        if (invalid_late_category) {
            (void)add_iptc_bytes(&store, ascii_block, 2U, 15U, 1U, "A1");
        }
        if (constrain_arena) {
            store.arena().constrain_max_size(store.arena().bytes().size());
        }
        store.finalize();
        return store;
    }

    static MetaStore make_one_iptc_source(uint16_t dataset,
                                          std::string_view bytes,
                                          EntryFlags flags = EntryFlags::None)
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        (void)add_iptc_bytes(&store, block, 2U, dataset, 0U, bytes, flags);
        store.finalize();
        return store;
    }

    TEST(IptcToXmp, BlockScopedBytesAndLateRollback)
    {
        static constexpr std::string_view kDcNs
            = "http://purl.org/dc/elements/1.1/";
        static constexpr std::string_view kPhotoshopNs
            = "http://ns.adobe.com/photoshop/1.0/";

        const MetaStore source = make_block_source(false);
        ASSERT_EQ(source.entry(1U).value.kind, MetaValueKind::Bytes);
        ASSERT_EQ(source.entry(1U).value.text_encoding, TextEncoding::Unknown);
        MetaStore output;
        const MetadataIptcToXmpResult translated
            = translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                    &output);
        ASSERT_EQ(translated.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(translated.source_properties, 2U);
        EXPECT_EQ(translated.entries_added, 2U);
        std::string_view title;
        ASSERT_TRUE(
            find_xmp_text(output, kDcNs, "title[@xml:lang=x-default]", &title));
        EXPECT_EQ(title, "\xE6\x9D\xB1\xE4\xBA\xAC");
        std::string_view city;
        ASSERT_TRUE(find_xmp_text(output, kPhotoshopNs, "City", &city));
        EXPECT_EQ(city, "Kyoto");

        MetaStore sentinel                = make_sentinel_output();
        const size_t sentinel_entry_count = sentinel.entries().size();
        const size_t sentinel_arena_size  = sentinel.arena().bytes().size();
        const MetaStore invalid_source    = make_block_source(true);
        ASSERT_EQ(invalid_source.entries().size(), 4U);
        const MetadataIptcToXmpResult rejected
            = translate_iptc_to_xmp(invalid_source, MetadataIptcToXmpOptions {},
                                    &sentinel);
        EXPECT_EQ(rejected.status, MetadataIptcToXmpStatus::InvalidSourceValue);
        EXPECT_EQ(rejected.failed_mapping,
                  MetadataDescriptiveTranslationMapping::PhotoshopCategory);
        EXPECT_EQ(sentinel.entries().size(), sentinel_entry_count);
        EXPECT_EQ(sentinel.arena().bytes().size(), sentinel_arena_size);
        std::string_view retained;
        EXPECT_TRUE(find_xmp_text(sentinel, "urn:openmeta:test", "Sentinel",
                                  &retained));
        EXPECT_EQ(retained, "keep");

        const MetaStore arena_limited_source = make_block_source(false, true);
        const size_t limited_source_arena_size
            = arena_limited_source.arena().bytes().size();
        const size_t limited_source_entries
            = arena_limited_source.entries().size();
        const MetadataIptcToXmpResult allocation_rejected
            = translate_iptc_to_xmp(arena_limited_source,
                                    MetadataIptcToXmpOptions {}, &sentinel);
        EXPECT_EQ(allocation_rejected.status,
                  MetadataIptcToXmpStatus::InternalError);
        EXPECT_EQ(allocation_rejected.source_properties, 0U);
        EXPECT_EQ(allocation_rejected.entries_added, 0U);
        EXPECT_EQ(sentinel.entries().size(), sentinel_entry_count);
        EXPECT_EQ(sentinel.arena().bytes().size(), sentinel_arena_size);
        EXPECT_EQ(arena_limited_source.entries().size(),
                  limited_source_entries);
        EXPECT_EQ(arena_limited_source.arena().bytes().size(),
                  limited_source_arena_size);
    }

    TEST(IptcToXmp, DecodedRawBytesNeedNoEncodingHint)
    {
        static constexpr std::array<std::byte, 19U> kIim = {
            std::byte { 0x1cU }, std::byte { 0x01U }, std::byte { 0x5aU },
            std::byte { 0x00U }, std::byte { 0x03U }, std::byte { 0x1bU },
            std::byte { 0x25U }, std::byte { 0x47U }, std::byte { 0x1cU },
            std::byte { 0x02U }, std::byte { 0x05U }, std::byte { 0x00U },
            std::byte { 0x06U }, std::byte { 0xe6U }, std::byte { 0x9dU },
            std::byte { 0xb1U }, std::byte { 0xe4U }, std::byte { 0xbaU },
            std::byte { 0xacU },
        };
        MetaStore source;
        const IptcIimDecodeResult decoded = decode_iptc_iim(kIim, source);
        ASSERT_EQ(decoded.status, IptcIimDecodeStatus::Ok);
        ASSERT_EQ(source.entries().size(), 2U);
        EXPECT_EQ(source.entry(1U).value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(source.entry(1U).value.text_encoding, TextEncoding::Unknown);
        source.finalize();

        MetaStore output;
        const MetadataIptcToXmpResult translated
            = translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                    &output);
        ASSERT_EQ(translated.status, MetadataIptcToXmpStatus::Ok);
        std::string_view title;
        EXPECT_TRUE(find_xmp_text(output, "http://purl.org/dc/elements/1.1/",
                                  "title[@xml:lang=x-default]", &title));
        EXPECT_EQ(title, "\xE6\x9D\xB1\xE4\xBA\xAC");
    }

    TEST(IptcToXmp, MapsAllTwentyFlatDatasets)
    {
        struct Source final {
            uint16_t dataset;
            std::string_view value;
        };
        static constexpr std::array<Source, 20U> kSources = { {
            { 5U, "Title" },       { 120U, "Description" },
            { 80U, "Creator" },    { 25U, "Keyword" },
            { 116U, "Copyright" }, { 110U, "Credit" },
            { 115U, "Source" },    { 90U, "City" },
            { 92U, "Suburb" },     { 95U, "State" },
            { 101U, "Canada" },    { 100U, "USA" },
            { 105U, "Headline" },  { 40U, "Instructions" },
            { 103U, "Ref" },       { 85U, "Editor" },
            { 122U, "Writer" },    { 15U, "ABC" },
            { 20U, "Extra" },      { 10U, "5" },
        } };
        struct Expected final {
            std::string_view schema;
            std::string_view path;
            std::string_view value;
        };
        static constexpr std::array<Expected, 20U> kExpected = { {
            { "http://purl.org/dc/elements/1.1/", "title[@xml:lang=x-default]",
              "Title" },
            { "http://purl.org/dc/elements/1.1/",
              "description[@xml:lang=x-default]", "Description" },
            { "http://purl.org/dc/elements/1.1/", "creator[1]", "Creator" },
            { "http://purl.org/dc/elements/1.1/", "subject[1]", "Keyword" },
            { "http://purl.org/dc/elements/1.1/", "rights[@xml:lang=x-default]",
              "Copyright" },
            { "http://ns.adobe.com/photoshop/1.0/", "Credit", "Credit" },
            { "http://ns.adobe.com/photoshop/1.0/", "Source", "Source" },
            { "http://ns.adobe.com/photoshop/1.0/", "City", "City" },
            { "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/", "Location",
              "Suburb" },
            { "http://ns.adobe.com/photoshop/1.0/", "State", "State" },
            { "http://ns.adobe.com/photoshop/1.0/", "Country", "Canada" },
            { "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/", "CountryCode",
              "USA" },
            { "http://ns.adobe.com/photoshop/1.0/", "Headline", "Headline" },
            { "http://ns.adobe.com/photoshop/1.0/", "Instructions",
              "Instructions" },
            { "http://ns.adobe.com/photoshop/1.0/", "TransmissionReference",
              "Ref" },
            { "http://ns.adobe.com/photoshop/1.0/", "AuthorsPosition",
              "Editor" },
            { "http://ns.adobe.com/photoshop/1.0/", "CaptionWriter", "Writer" },
            { "http://ns.adobe.com/photoshop/1.0/", "Category", "ABC" },
            { "http://ns.adobe.com/photoshop/1.0/", "SupplementalCategories[1]",
              "Extra" },
            { "http://ns.adobe.com/photoshop/1.0/", "Urgency", "5" },
        } };

        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        for (size_t i = 0U; i < kSources.size(); ++i) {
            (void)add_iptc_bytes(&source, block, 2U, kSources[i].dataset,
                                 static_cast<uint32_t>(i), kSources[i].value);
        }
        source.finalize();
        MetaStore output;
        const MetadataIptcToXmpResult result
            = translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                    &output);
        ASSERT_EQ(result.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(result.source_properties, 20U);
        EXPECT_EQ(result.groups_translated, 20U);
        EXPECT_EQ(result.entries_added, 20U);
        for (const Expected& expected : kExpected) {
            std::string_view value;
            ASSERT_TRUE(
                find_xmp_text(output, expected.schema, expected.path, &value));
            EXPECT_EQ(value, expected.value);
        }
    }

    TEST(IptcToXmp, ValidatesTextHintsAndCharsetMarkers)
    {
        MetaStore unknown_text;
        const BlockId unknown_block = unknown_text.add_block(BlockInfo {});
        (void)add_iptc_text(&unknown_text, unknown_block, 2U, 5U, 0U, "Title",
                            TextEncoding::Unknown);
        unknown_text.finalize();
        MetaStore output         = make_sentinel_output();
        const size_t output_size = output.entries().size();
        EXPECT_EQ(translate_iptc_to_xmp(unknown_text,
                                        MetadataIptcToXmpOptions {}, &output)
                      .status,
                  MetadataIptcToXmpStatus::InvalidSourceValue);
        EXPECT_EQ(output.entries().size(), output_size);

        MetaStore ascii_text_high_byte;
        const BlockId ascii_block = ascii_text_high_byte.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&ascii_text_high_byte, ascii_block, 1U, 90U, 0U,
                             "\x1b%G");
        (void)add_iptc_text(&ascii_text_high_byte, ascii_block, 2U, 5U, 1U,
                            "\xC3\xA9", TextEncoding::Ascii);
        ascii_text_high_byte.finalize();
        EXPECT_EQ(translate_iptc_to_xmp(ascii_text_high_byte,
                                        MetadataIptcToXmpOptions {}, &output)
                      .status,
                  MetadataIptcToXmpStatus::SourceEncodingConflict);
        EXPECT_EQ(output.entries().size(), output_size);

        MetaStore missing_marker;
        const BlockId missing_marker_block = missing_marker.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&missing_marker, missing_marker_block, 2U, 5U, 0U,
                             "\xC3\xA9");
        missing_marker.finalize();
        EXPECT_EQ(translate_iptc_to_xmp(missing_marker,
                                        MetadataIptcToXmpOptions {}, &output)
                      .status,
                  MetadataIptcToXmpStatus::SourceEncodingConflict);
        EXPECT_EQ(output.entries().size(), output_size);

        MetaStore unknown_text_marker;
        const BlockId marker_block = unknown_text_marker.add_block(
            BlockInfo {});
        (void)add_iptc_text(&unknown_text_marker, marker_block, 1U, 90U, 0U,
                            "\x1b%G", TextEncoding::Unknown);
        (void)add_iptc_bytes(&unknown_text_marker, marker_block, 2U, 5U, 1U,
                             "Title");
        unknown_text_marker.finalize();
        EXPECT_EQ(translate_iptc_to_xmp(unknown_text_marker,
                                        MetadataIptcToXmpOptions {}, &output)
                      .status,
                  MetadataIptcToXmpStatus::SourceEncodingConflict);
        EXPECT_EQ(output.entries().size(), output_size);

        MetaStore valid_utf8_text;
        const BlockId utf8_block = valid_utf8_text.add_block(BlockInfo {});
        (void)add_iptc_bytes(&valid_utf8_text, utf8_block, 1U, 90U, 0U,
                             "\x1b%G");
        (void)add_iptc_text(&valid_utf8_text, utf8_block, 2U, 5U, 1U,
                            "\xC3\xA9", TextEncoding::Utf8);
        valid_utf8_text.finalize();
        EXPECT_EQ(translate_iptc_to_xmp(valid_utf8_text,
                                        MetadataIptcToXmpOptions {}, &output)
                      .status,
                  MetadataIptcToXmpStatus::Ok);
    }

    TEST(IptcToXmp, RejectsDamagedSourceDestinationAndMarkerMetadata)
    {
        enum class SourceDamage : uint8_t {
            WrongKind,
            CountMismatch,
            InvalidSpan,
            Truncated,
            Unreadable,
            UnknownFlags,
            TextUnknown,
        };
        static constexpr std::array<SourceDamage, 7U> kSourceDamage = {
            SourceDamage::WrongKind,   SourceDamage::CountMismatch,
            SourceDamage::InvalidSpan, SourceDamage::Truncated,
            SourceDamage::Unreadable,  SourceDamage::UnknownFlags,
            SourceDamage::TextUnknown,
        };
        for (const SourceDamage damage : kSourceDamage) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            Entry entry;
            entry.key = make_iptc_dataset_key(2U, 5U);
            entry.value
                = damage == SourceDamage::TextUnknown
                      ? make_text(source.arena(), "Title",
                                  TextEncoding::Unknown)
                      : make_bytes(source.arena(),
                                   std::span<const std::byte>(
                                       reinterpret_cast<const std::byte*>(
                                           "Title"),
                                       5U));
            entry.origin.block = block;
            switch (damage) {
            case SourceDamage::WrongKind:
                entry.value.kind = MetaValueKind::Scalar;
                break;
            case SourceDamage::CountMismatch: ++entry.value.count; break;
            case SourceDamage::InvalidSpan:
                entry.value.data.span.offset = UINT32_MAX;
                break;
            case SourceDamage::Truncated:
                entry.flags = EntryFlags::Truncated;
                break;
            case SourceDamage::Unreadable:
                entry.flags = EntryFlags::Unreadable;
                break;
            case SourceDamage::UnknownFlags:
                entry.flags = static_cast<EntryFlags>(0x80U);
                break;
            case SourceDamage::TextUnknown: break;
            }
            (void)source.add_entry(entry);
            source.finalize();
            MetaStore output          = make_sentinel_output();
            const size_t output_count = output.entries().size();
            EXPECT_EQ(translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                            &output)
                          .status,
                      MetadataIptcToXmpStatus::InvalidSourceValue);
            EXPECT_EQ(output.entries().size(), output_count);
        }

        enum class DestinationDamage : uint8_t {
            WrongKind,
            CountMismatch,
            InvalidSpan,
            Truncated,
            Unreadable,
            UnknownFlags,
            UnknownEncoding,
            AsciiHighByte,
        };
        static constexpr std::array<DestinationDamage, 8U> kDestinationDamage = {
            DestinationDamage::WrongKind,
            DestinationDamage::CountMismatch,
            DestinationDamage::InvalidSpan,
            DestinationDamage::Truncated,
            DestinationDamage::Unreadable,
            DestinationDamage::UnknownFlags,
            DestinationDamage::UnknownEncoding,
            DestinationDamage::AsciiHighByte,
        };
        MetadataIptcToXmpOptions replace;
        replace.conflict_policy
            = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        for (const DestinationDamage damage : kDestinationDamage) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            (void)add_iptc_bytes(&source, block, 2U, 5U, 0U, "Title");
            Entry entry;
            entry.key = make_xmp_property_key(source.arena(),
                                              "http://purl.org/dc/elements/1.1/",
                                              "title[@xml:lang=x-default]");
            entry.value
                = damage == DestinationDamage::AsciiHighByte
                      ? make_text(source.arena(), "\xFF", TextEncoding::Ascii)
                      : make_text(source.arena(), "Old", TextEncoding::Utf8);
            entry.origin.block = block;
            switch (damage) {
            case DestinationDamage::WrongKind:
                entry.value.kind = MetaValueKind::Scalar;
                break;
            case DestinationDamage::CountMismatch: ++entry.value.count; break;
            case DestinationDamage::InvalidSpan:
                entry.value.data.span.offset = UINT32_MAX;
                break;
            case DestinationDamage::Truncated:
                entry.flags = EntryFlags::Truncated;
                break;
            case DestinationDamage::Unreadable:
                entry.flags = EntryFlags::Unreadable;
                break;
            case DestinationDamage::UnknownFlags:
                entry.flags = static_cast<EntryFlags>(0x80U);
                break;
            case DestinationDamage::UnknownEncoding:
                entry.value.text_encoding = TextEncoding::Unknown;
                break;
            case DestinationDamage::AsciiHighByte: break;
            }
            (void)source.add_entry(entry);
            source.finalize();
            MetaStore output          = make_sentinel_output();
            const size_t output_count = output.entries().size();
            EXPECT_EQ(translate_iptc_to_xmp(source, replace, &output).status,
                      MetadataIptcToXmpStatus::UnsupportedDestinationShape);
            EXPECT_EQ(output.entries().size(), output_count);
        }

        enum class MarkerDamage : uint8_t {
            WrongPayload,
            CountMismatch,
            Truncated,
            UnknownFlags,
            TextUnknown,
            DuplicateActive,
        };
        static constexpr std::array<MarkerDamage, 6U> kMarkerDamage = {
            MarkerDamage::WrongPayload, MarkerDamage::CountMismatch,
            MarkerDamage::Truncated,    MarkerDamage::UnknownFlags,
            MarkerDamage::TextUnknown,  MarkerDamage::DuplicateActive,
        };
        for (const MarkerDamage damage : kMarkerDamage) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            Entry marker;
            marker.key = make_iptc_dataset_key(1U, 90U);
            marker.value
                = damage == MarkerDamage::TextUnknown
                      ? make_text(source.arena(), "\x1b%G",
                                  TextEncoding::Unknown)
                      : make_bytes(source.arena(),
                                   std::span<const std::byte>(
                                       reinterpret_cast<const std::byte*>(
                                           damage == MarkerDamage::WrongPayload
                                               ? "BAD"
                                               : "\x1b%G"),
                                       3U));
            marker.origin.block = block;
            switch (damage) {
            case MarkerDamage::WrongPayload: break;
            case MarkerDamage::CountMismatch: ++marker.value.count; break;
            case MarkerDamage::Truncated:
                marker.flags = EntryFlags::Truncated;
                break;
            case MarkerDamage::UnknownFlags:
                marker.flags = static_cast<EntryFlags>(0x80U);
                break;
            case MarkerDamage::TextUnknown:
            case MarkerDamage::DuplicateActive: break;
            }
            (void)source.add_entry(marker);
            if (damage == MarkerDamage::DuplicateActive) {
                (void)add_iptc_bytes(&source, block, 1U, 90U, 1U, "\x1b%G");
            }
            (void)add_iptc_bytes(&source, block, 2U, 5U, 1U, "Title");
            source.finalize();
            MetaStore output;
            EXPECT_EQ(translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                            &output)
                          .status,
                      MetadataIptcToXmpStatus::SourceEncodingConflict);
        }
    }

    TEST(IptcToXmp, DirtySelectionAndTombstoneRemoval)
    {
        MetaStore source;
        const BlockId changed_block = source.add_block(BlockInfo {});
        const BlockId clean_block   = source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&source, changed_block, 1U, 90U, 0U, "\x1b%G",
                             EntryFlags::Dirty);
        (void)add_iptc_bytes(&source, changed_block, 2U, 5U, 1U, "Title");
        (void)add_iptc_bytes(&source, changed_block, 2U, 90U, 2U, "Osaka");
        (void)add_iptc_bytes(&source, clean_block, 2U, 105U, 0U, "Headline");
        source.finalize();
        MetadataIptcToXmpOptions dirty;
        dirty.source_mode = MetadataDescriptiveTranslationSourceMode::DirtyOnly;
        MetaStore translated;
        const MetadataIptcToXmpResult result
            = translate_iptc_to_xmp(source, dirty, &translated);
        ASSERT_EQ(result.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(result.source_properties, 2U);
        EXPECT_EQ(result.entries_added, 2U);
        std::string_view value;
        EXPECT_TRUE(find_xmp_text(translated,
                                  "http://purl.org/dc/elements/1.1/",
                                  "title[@xml:lang=x-default]", &value));
        EXPECT_TRUE(find_xmp_text(translated,
                                  "http://ns.adobe.com/photoshop/1.0/", "City",
                                  &value));
        EXPECT_FALSE(find_xmp_text(translated,
                                   "http://ns.adobe.com/photoshop/1.0/",
                                   "Headline", &value));

        MetaStore deleted_marker_source;
        const BlockId deleted_marker_block = deleted_marker_source.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&deleted_marker_source, deleted_marker_block, 1U,
                             90U, 0U, "\x1b%G",
                             EntryFlags::Deleted | EntryFlags::Dirty);
        (void)add_iptc_bytes(&deleted_marker_source, deleted_marker_block, 2U,
                             5U, 1U, "\xC3\xA9");
        deleted_marker_source.finalize();
        MetaStore deleted_marker_output;
        const MetadataIptcToXmpResult deleted_marker_result
            = translate_iptc_to_xmp(deleted_marker_source, dirty,
                                    &deleted_marker_output);
        EXPECT_EQ(deleted_marker_result.status,
                  MetadataIptcToXmpStatus::SourceEncodingConflict);

        MetaStore removal;
        const BlockId removal_block = removal.add_block(BlockInfo {});
        (void)add_iptc_bytes(&removal, removal_block, 2U, 5U, 0U, "old",
                             EntryFlags::Deleted | EntryFlags::Dirty);
        const EntryId old_title
            = add_xmp_text(&removal, removal_block,
                           "http://purl.org/dc/elements/1.1/",
                           "title[@xml:lang=x-default]", "old");
        removal.finalize();
        MetaStore rejected_removal = make_sentinel_output();
        const MetadataIptcToXmpResult conflict
            = translate_iptc_to_xmp(removal, dirty, &rejected_removal);
        EXPECT_EQ(conflict.status,
                  MetadataIptcToXmpStatus::DestinationConflict);
        EXPECT_EQ(conflict.entries_removed, 0U);
        ASSERT_TRUE(find_xmp_text(rejected_removal, "urn:openmeta:test",
                                  "Sentinel", &value));
        EXPECT_EQ(value, "keep");
        EXPECT_FALSE(any(removal.entry(old_title).flags, EntryFlags::Deleted));
        dirty.conflict_policy = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        const MetadataIptcToXmpResult removed
            = translate_iptc_to_xmp(removal, dirty, &translated);
        ASSERT_EQ(removed.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(removed.entries_removed, 1U);
        EXPECT_TRUE(
            any(translated.entry(old_title).flags, EntryFlags::Deleted));

        MetaStore clean_tombstone;
        const BlockId clean_tombstone_block = clean_tombstone.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&clean_tombstone, clean_tombstone_block, 2U, 5U,
                             0U, "old", EntryFlags::Deleted);
        const EntryId retained_title
            = add_xmp_text(&clean_tombstone, clean_tombstone_block,
                           "http://purl.org/dc/elements/1.1/",
                           "title[@xml:lang=x-default]", "retain");
        clean_tombstone.finalize();
        const MetadataIptcToXmpResult clean_ignored
            = translate_iptc_to_xmp(clean_tombstone, dirty, &translated);
        ASSERT_EQ(clean_ignored.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(clean_ignored.source_properties, 0U);
        EXPECT_EQ(clean_ignored.entries_removed, 0U);
        EXPECT_FALSE(
            any(translated.entry(retained_title).flags, EntryFlags::Deleted));
    }

    TEST(IptcToXmp, DirtyCharsetSelectionAcrossBitmapWords)
    {
        MetaStore source;
        for (uint32_t block = 0U; block < 66U; ++block) {
            ASSERT_EQ(source.add_block(BlockInfo {}), block);
        }
        // Deleted markers select clean members but do not declare a charset.
        for (uint32_t marker = 0U; marker < 128U; ++marker) {
            (void)add_iptc_bytes(&source, 63U, 1U, 90U, marker, "bad",
                                 EntryFlags::Dirty | EntryFlags::Deleted);
            (void)add_iptc_bytes(&source, 64U, 1U, 90U, marker, "bad",
                                 EntryFlags::Dirty | EntryFlags::Deleted);
        }
        (void)add_iptc_bytes(&source, 63U, 2U, 5U, 128U, "Title");
        (void)add_iptc_bytes(&source, 64U, 2U, 90U, 128U, "City");
        (void)add_iptc_bytes(&source, 62U, 2U, 105U, 0U, "Headline");
        (void)add_iptc_bytes(&source, 65U, 2U, 115U, 0U, "Source");
        source.finalize();
        MetadataIptcToXmpOptions options;
        options.source_mode
            = MetadataDescriptiveTranslationSourceMode::DirtyOnly;
        MetaStore output;
        const MetadataIptcToXmpResult result
            = translate_iptc_to_xmp(source, options, &output);
        ASSERT_EQ(result.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(result.source_properties, 2U);
        EXPECT_EQ(result.entries_added, 2U);
        std::string_view value;
        ASSERT_TRUE(find_xmp_text(output, "http://purl.org/dc/elements/1.1/",
                                  "title[@xml:lang=x-default]", &value));
        EXPECT_EQ(value, "Title");
        ASSERT_TRUE(find_xmp_text(output, "http://ns.adobe.com/photoshop/1.0/",
                                  "City", &value));
        EXPECT_EQ(value, "City");
        EXPECT_FALSE(find_xmp_text(output, "http://ns.adobe.com/photoshop/1.0/",
                                   "Headline", &value));
        EXPECT_FALSE(find_xmp_text(output, "http://ns.adobe.com/photoshop/1.0/",
                                   "Source", &value));
    }

    TEST(IptcToXmp, RepeatedSourcesKeepBlockOrderAndDuplicateValues)
    {
        MetaStore source;
        const BlockId first_block  = source.add_block(BlockInfo {});
        const BlockId second_block = source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&source, first_block, 2U, 25U, 5U, "last");
        (void)add_iptc_bytes(&source, first_block, 2U, 25U, 1U, "first");
        (void)add_iptc_bytes(&source, first_block, 2U, 25U, 1U, "tie");
        (void)add_iptc_bytes(&source, second_block, 2U, 25U, 0U, "first");
        source.finalize();
        MetaStore output;
        const MetadataIptcToXmpResult result
            = translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                    &output);
        ASSERT_EQ(result.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(result.entries_added, 4U);
        static constexpr std::array<std::string_view, 4U> kExpected
            = { "first", "tie", "last", "first" };
        static constexpr std::array<uint32_t, 4U> kExpectedOrders = { 1U, 1U,
                                                                      5U, 0U };
        for (size_t i = 0U; i < kExpected.size(); ++i) {
            const std::string path = "subject[" + std::to_string(i + 1U) + "]";
            std::string_view value;
            EntryId id = kInvalidEntryId;
            ASSERT_TRUE(find_xmp_text(output,
                                      "http://purl.org/dc/elements/1.1/", path,
                                      &value, &id));
            EXPECT_EQ(value, kExpected[i]);
            EXPECT_EQ(output.entry(id).origin.block,
                      i < 3U ? first_block : second_block);
            EXPECT_EQ(output.entry(id).origin.order_in_block,
                      kExpectedOrders[i]);
        }
    }

    TEST(IptcToXmp, PreserveExistingRetainsLanguageOnlyRoot)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&source, block, 2U, 5U, 0U, "New title");
        (void)add_xmp_text(&source, block, "http://purl.org/dc/elements/1.1/",
                           "title[@xml:lang=fr-FR]", "Bonjour");
        source.finalize();

        MetadataIptcToXmpOptions options;
        options.conflict_policy
            = MetadataIptcToXmpConflictPolicy::PreserveExisting;
        MetaStore output;
        const MetadataIptcToXmpResult result
            = translate_iptc_to_xmp(source, options, &output);
        ASSERT_EQ(result.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(result.groups_preserved, 1U);
        EXPECT_EQ(result.entries_added, 0U);
        std::string_view value;
        EXPECT_FALSE(find_xmp_text(output, "http://purl.org/dc/elements/1.1/",
                                   "title[@xml:lang=x-default]", &value));
        EXPECT_TRUE(find_xmp_text(output, "http://purl.org/dc/elements/1.1/",
                                  "title[@xml:lang=fr-FR]", &value));
        EXPECT_EQ(value, "Bonjour");
    }

    TEST(IptcToXmp, ConflictReplacementReapplyAndAliasing)
    {
        static constexpr std::string_view kDcNs
            = "http://purl.org/dc/elements/1.1/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&source, block, 2U, 5U, 3U, "New");
        const EntryId old_id
            = add_xmp_text(&source, block, kDcNs, "title[@xml:lang=x-default]",
                           "Old", TextEncoding::Utf8, EntryFlags::None, 91U);
        source.finalize();

        MetaStore failed         = make_sentinel_output();
        const size_t failed_size = failed.entries().size();
        EXPECT_EQ(translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                        &failed)
                      .status,
                  MetadataIptcToXmpStatus::DestinationConflict);
        EXPECT_EQ(failed.entries().size(), failed_size);

        MetadataIptcToXmpOptions replace;
        replace.conflict_policy
            = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        MetaStore updated;
        const MetadataIptcToXmpResult replaced
            = translate_iptc_to_xmp(source, replace, &updated);
        ASSERT_EQ(replaced.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(replaced.entries_updated, 1U);
        std::string_view value;
        EntryId updated_id = kInvalidEntryId;
        ASSERT_TRUE(find_xmp_text(updated, kDcNs, "title[@xml:lang=x-default]",
                                  &value, &updated_id));
        EXPECT_EQ(value, "New");
        EXPECT_EQ(updated_id, old_id);
        EXPECT_EQ(updated.entry(updated_id).origin.block, block);
        EXPECT_EQ(updated.entry(updated_id).origin.order_in_block, 91U);

        MetaStore reapplied;
        const MetadataIptcToXmpResult again
            = translate_iptc_to_xmp(updated, MetadataIptcToXmpOptions {},
                                    &reapplied);
        ASSERT_EQ(again.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(again.entries_added, 0U);
        EXPECT_EQ(again.entries_updated, 0U);
        EXPECT_EQ(again.groups_unchanged, 1U);

        MetaStore alias = make_one_iptc_source(5U, "Alias");
        const MetadataIptcToXmpResult aliased
            = translate_iptc_to_xmp(alias, MetadataIptcToXmpOptions {}, &alias);
        ASSERT_EQ(aliased.status, MetadataIptcToXmpStatus::Ok);
        EXPECT_EQ(aliased.entries_added, 1U);
        ASSERT_TRUE(
            find_xmp_text(alias, kDcNs, "title[@xml:lang=x-default]", &value));
        EXPECT_EQ(value, "Alias");
    }

    TEST(IptcToXmp, RepeatedDestinationIndicesAreCanonicalUniqueAndContiguous)
    {
        struct Case final {
            std::array<std::string_view, 2U> paths;
            size_t count;
        };
        static constexpr std::array<Case, 4U> kCases = { {
            { { "subject[1]", "subject[1]" }, 2U },
            { { "subject[1]", "subject[3]" }, 2U },
            { { "subject[01]", "" }, 1U },
            { { "subject[0]", "" }, 1U },
        } };
        MetadataIptcToXmpOptions replace;
        replace.conflict_policy
            = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        for (const Case& test_case : kCases) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            (void)add_iptc_bytes(&source, block, 2U, 25U, 0U, "new");
            for (size_t i = 0U; i < test_case.count; ++i) {
                (void)add_xmp_text(&source, block,
                                   "http://purl.org/dc/elements/1.1/",
                                   test_case.paths[i], "old");
            }
            source.finalize();
            MetaStore output          = make_sentinel_output();
            const size_t output_count = output.entries().size();
            const MetadataIptcToXmpResult result
                = translate_iptc_to_xmp(source, replace, &output);
            EXPECT_EQ(result.status,
                      MetadataIptcToXmpStatus::UnsupportedDestinationShape);
            EXPECT_EQ(output.entries().size(), output_count);
        }
    }

    TEST(IptcToXmp, CombinedSourceAndDestinationTextBudgetRollsBack)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&source, block, 2U, 5U, 0U, "abc");
        (void)add_xmp_text(&source, block, "http://purl.org/dc/elements/1.1/",
                           "title[@xml:lang=x-default]", "old");
        source.finalize();
        MetaStore output            = make_sentinel_output();
        const size_t output_entries = output.entries().size();
        const size_t output_arena   = output.arena().bytes().size();
        MetadataIptcToXmpOptions limited;
        limited.conflict_policy
            = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        limited.max_total_text_bytes = 5U;
        const MetadataIptcToXmpResult over_budget
            = translate_iptc_to_xmp(source, limited, &output);
        EXPECT_EQ(over_budget.status,
                  MetadataIptcToXmpStatus::SourceLimitExceeded);
        EXPECT_EQ(output.entries().size(), output_entries);
        EXPECT_EQ(output.arena().bytes().size(), output_arena);

        MetaStore invalid_utf8 = make_one_iptc_source(5U, "\xFF\xFF");
        limited.conflict_policy
            = MetadataIptcToXmpConflictPolicy::FailOnConflict;
        limited.max_total_text_bytes = 1U;
        const MetadataIptcToXmpResult budget_before_utf8
            = translate_iptc_to_xmp(invalid_utf8, limited, &output);
        EXPECT_EQ(budget_before_utf8.status,
                  MetadataIptcToXmpStatus::SourceLimitExceeded);
        EXPECT_EQ(output.entries().size(), output_entries);
        EXPECT_EQ(output.arena().bytes().size(), output_arena);

        MetaStore invalid_destination;
        const BlockId destination_block = invalid_destination.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&invalid_destination, destination_block, 2U, 5U,
                             0U, "abc");
        Entry malformed_destination;
        malformed_destination.key
            = make_xmp_property_key(invalid_destination.arena(),
                                    "http://purl.org/dc/elements/1.1/",
                                    "title[@xml:lang=x-default]");
        malformed_destination.value = make_text(invalid_destination.arena(),
                                                "\xFF\xFF", TextEncoding::Utf8);
        malformed_destination.origin.block = destination_block;
        (void)invalid_destination.add_entry(malformed_destination);
        invalid_destination.finalize();
        limited.conflict_policy
            = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        limited.max_total_text_bytes = 4U;
        const MetadataIptcToXmpResult destination_budget_first
            = translate_iptc_to_xmp(invalid_destination, limited, &output);
        EXPECT_EQ(destination_budget_first.status,
                  MetadataIptcToXmpStatus::SourceLimitExceeded);
        EXPECT_EQ(output.entries().size(), output_entries);
        EXPECT_EQ(output.arena().bytes().size(), output_arena);
    }

    TEST(IptcToXmp, InvalidOriginsDoNotShareSentinelCharsetScope)
    {
        MetaStore source;
        (void)source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&source, kInvalidBlockId, 2U, 5U, 0U, "\xC3\xA9");
        (void)add_iptc_bytes(&source, 99U, 1U, 90U, 0U, "\x1b%G");
        source.finalize();
        MetaStore output;
        EXPECT_EQ(translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                        &output)
                      .status,
                  MetadataIptcToXmpStatus::SourceEncodingConflict);

        MetaStore invalid_source;
        (void)invalid_source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&invalid_source, 99U, 2U, 5U, 0U, "Title");
        invalid_source.finalize();
        EXPECT_EQ(translate_iptc_to_xmp(invalid_source,
                                        MetadataIptcToXmpOptions {}, &output)
                      .status,
                  MetadataIptcToXmpStatus::InvalidSourceValue);

        MetaStore selected_invalid_source;
        const BlockId valid_block = selected_invalid_source.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&selected_invalid_source, valid_block, 1U, 90U, 0U,
                             "\x1b%G", EntryFlags::Dirty);
        (void)add_iptc_bytes(&selected_invalid_source, valid_block, 2U, 5U, 1U,
                             "Valid block");
        (void)add_iptc_bytes(&selected_invalid_source, 99U, 2U, 5U, 0U,
                             "Invalid block");
        selected_invalid_source.finalize();
        MetadataIptcToXmpOptions dirty;
        dirty.source_mode = MetadataDescriptiveTranslationSourceMode::DirtyOnly;
        EXPECT_EQ(translate_iptc_to_xmp(selected_invalid_source, dirty, &output)
                      .status,
                  MetadataIptcToXmpStatus::InvalidSourceValue);

        MetaStore sentinel_scope;
        (void)add_iptc_bytes(&sentinel_scope, kInvalidBlockId, 1U, 90U, 0U,
                             "\x1b%G");
        (void)add_iptc_bytes(&sentinel_scope, kInvalidBlockId, 2U, 5U, 1U,
                             "\xC3\xA9");
        sentinel_scope.finalize();
        EXPECT_EQ(translate_iptc_to_xmp(sentinel_scope,
                                        MetadataIptcToXmpOptions {}, &output)
                      .status,
                  MetadataIptcToXmpStatus::Ok);
    }

    TEST(IptcToXmp, EnforcesDatasetFieldsAndResourceLimits)
    {
        struct FieldCase final {
            uint16_t dataset;
            std::string_view value;
            MetadataIptcToXmpStatus status;
        };
        const std::string too_long_title(65U, 'T');
        const std::array<FieldCase, 5U> cases = { {
            { 15U, "A1", MetadataIptcToXmpStatus::InvalidSourceValue },
            { 15U, "ABCD", MetadataIptcToXmpStatus::ValueTooLong },
            { 100U, "us", MetadataIptcToXmpStatus::InvalidSourceValue },
            { 10U, "9", MetadataIptcToXmpStatus::InvalidSourceValue },
            { 5U, too_long_title, MetadataIptcToXmpStatus::ValueTooLong },
        } };
        for (const FieldCase& field : cases) {
            MetaStore source = make_one_iptc_source(field.dataset, field.value);
            MetaStore output;
            EXPECT_EQ(translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                            &output)
                          .status,
                      field.status);
        }

        MetaStore ambiguous;
        const BlockId ambiguous_block = ambiguous.add_block(BlockInfo {});
        (void)add_iptc_bytes(&ambiguous, ambiguous_block, 2U, 5U, 0U, "One");
        (void)add_iptc_bytes(&ambiguous, ambiguous_block, 2U, 5U, 1U, "Two");
        ambiguous.finalize();
        MetaStore ambiguous_output;
        EXPECT_EQ(translate_iptc_to_xmp(ambiguous, MetadataIptcToXmpOptions {},
                                        &ambiguous_output)
                      .status,
                  MetadataIptcToXmpStatus::AmbiguousSource);

        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&source, block, 2U, 5U, 0U, "Title");
        (void)add_iptc_bytes(&source, block, 2U, 90U, 1U, "City");
        source.finalize();
        MetadataIptcToXmpOptions limit;
        limit.max_source_properties = 1U;
        MetaStore output;
        EXPECT_EQ(translate_iptc_to_xmp(source, limit, &output).status,
                  MetadataIptcToXmpStatus::SourceLimitExceeded);
        limit                   = MetadataIptcToXmpOptions {};
        limit.max_added_entries = 1U;
        EXPECT_EQ(translate_iptc_to_xmp(source, limit, &output).status,
                  MetadataIptcToXmpStatus::EntryLimitExceeded);
        limit                = MetadataIptcToXmpOptions {};
        limit.max_operations = 1U;
        EXPECT_EQ(translate_iptc_to_xmp(source, limit, &output).status,
                  MetadataIptcToXmpStatus::OperationLimitExceeded);

        MetaStore repeated_destination;
        const BlockId repeated_block = repeated_destination.add_block(
            BlockInfo {});
        (void)add_iptc_bytes(&repeated_destination, repeated_block, 2U, 25U, 0U,
                             "new");
        (void)add_xmp_text(&repeated_destination, repeated_block,
                           "http://purl.org/dc/elements/1.1/", "subject[1]",
                           "one");
        (void)add_xmp_text(&repeated_destination, repeated_block,
                           "http://purl.org/dc/elements/1.1/", "subject[2]",
                           "two");
        repeated_destination.finalize();
        limit = MetadataIptcToXmpOptions {};
        limit.conflict_policy = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        limit.max_operations = 1U;
        EXPECT_EQ(
            translate_iptc_to_xmp(repeated_destination, limit, &output).status,
            MetadataIptcToXmpStatus::OperationLimitExceeded);
    }

    TEST(IptcToXmp, ValidatesFinalizationOptionsAndOutput)
    {
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        (void)add_iptc_bytes(&source, block, 2U, 5U, 0U, "Title");
        MetaStore output;
        EXPECT_EQ(translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                        &output)
                      .status,
                  MetadataIptcToXmpStatus::SourceNotFinalized);
        source.finalize();
        EXPECT_EQ(translate_iptc_to_xmp(source, MetadataIptcToXmpOptions {},
                                        nullptr)
                      .status,
                  MetadataIptcToXmpStatus::NullOutput);

        MetadataIptcToXmpOptions invalid;
        invalid.max_source_properties = 0U;
        EXPECT_EQ(translate_iptc_to_xmp(source, invalid, &output).status,
                  MetadataIptcToXmpStatus::InvalidOptions);
        invalid                      = MetadataIptcToXmpOptions {};
        invalid.max_total_text_bytes = kMetadataIptcToXmpMaxTotalTextBytes + 1U;
        EXPECT_EQ(translate_iptc_to_xmp(source, invalid, &output).status,
                  MetadataIptcToXmpStatus::InvalidOptions);
        invalid = MetadataIptcToXmpOptions {};
        invalid.source_mode
            = static_cast<MetadataDescriptiveTranslationSourceMode>(0xffU);
        EXPECT_EQ(translate_iptc_to_xmp(source, invalid, &output).status,
                  MetadataIptcToXmpStatus::InvalidOptions);
    }

}  // namespace
}  // namespace openmeta
