// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_reconcile.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <algorithm>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static EntryId add_xmp_text(MetaStore* store, BlockId block,
                                std::string_view schema, std::string_view path,
                                std::string_view text,
                                EntryFlags flags = EntryFlags::Dirty,
                                uint32_t order   = 0U)
    {
        Entry entry;
        entry.key   = make_xmp_property_key(store->arena(), schema, path);
        entry.value = make_text(store->arena(), text, TextEncoding::Utf8);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.flags                 = flags;
        return store->add_entry(entry);
    }

    static EntryId add_iptc_text(MetaStore* store, BlockId block,
                                 uint8_t dataset, std::string_view text,
                                 uint32_t order = 0U)
    {
        Entry entry;
        entry.key = make_iptc_dataset_key(2U, dataset);
        entry.value
            = make_bytes(store->arena(),
                         std::span<const std::byte>(
                             reinterpret_cast<const std::byte*>(text.data()),
                             text.size()));
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.flags                 = EntryFlags::Dirty;
        return store->add_entry(entry);
    }

    static EntryId add_exif_text(MetaStore* store, BlockId block, uint16_t tag,
                                 std::string_view text, uint16_t wire_type = 2U,
                                 TextEncoding encoding = TextEncoding::Ascii,
                                 EntryFlags flags      = EntryFlags::Dirty)
    {
        Entry entry;
        entry.key              = make_exif_tag_key(store->arena(), "ifd0", tag);
        entry.value            = make_text(store->arena(), text, encoding);
        entry.origin.block     = block;
        entry.origin.wire_type = { WireFamily::Tiff, wire_type };
        entry.origin.wire_count = entry.value.count + 1U;
        entry.flags             = flags;
        return store->add_entry(entry);
    }

    static std::string_view entry_text(const MetaStore& store,
                                       const Entry& entry)
    {
        const std::span<const std::byte> bytes = store.arena().span(
            entry.value.data.span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static std::string_view entry_path(const MetaStore& store,
                                       const Entry& entry)
    {
        const std::span<const std::byte> bytes = store.arena().span(
            entry.key.data.xmp_property.property_path);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static std::string_view entry_schema(const MetaStore& store,
                                         const Entry& entry)
    {
        const std::span<const std::byte> bytes = store.arena().span(
            entry.key.data.xmp_property.schema_ns);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static std::string_view entry_wire_type_name(const MetaStore& store,
                                                 const Entry& entry)
    {
        const std::span<const std::byte> bytes = store.arena().span(
            entry.origin.wire_type_name);
        return bytes.empty() ? std::string_view {}
                             : std::string_view(reinterpret_cast<const char*>(
                                                    bytes.data()),
                                                bytes.size());
    }

    static EntryId find_xmp(const MetaStore& store, std::string_view schema,
                            std::string_view path, bool include_deleted = false)
    {
        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& entry = store.entry(id);
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || (!include_deleted && any(entry.flags, EntryFlags::Deleted)))
                continue;
            if (entry_schema(store, entry) == schema
                && entry_path(store, entry) == path)
                return id;
        }
        return kInvalidEntryId;
    }

    static EntryId find_iptc(const MetaStore& store, uint8_t dataset,
                             bool include_deleted = false)
    {
        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& entry = store.entry(id);
            if (entry.key.kind == MetaKeyKind::IptcDataset
                && entry.key.data.iptc_dataset.record == 2U
                && entry.key.data.iptc_dataset.dataset == dataset
                && (include_deleted || !any(entry.flags, EntryFlags::Deleted)))
                return id;
        }
        return kInvalidEntryId;
    }

    static EntryId find_exif(const MetaStore& store, uint16_t tag,
                             bool include_deleted = false)
    {
        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& entry = store.entry(id);
            if (entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag
                || (!include_deleted && any(entry.flags, EntryFlags::Deleted)))
                continue;
            const std::span<const std::byte> ifd = store.arena().span(
                entry.key.data.exif_tag.ifd);
            if (std::string_view(reinterpret_cast<const char*>(ifd.data()),
                                 ifd.size())
                == "ifd0")
                return id;
        }
        return kInvalidEntryId;
    }

    TEST(MetadataReconcile, MixedFamilyConflictLeavesOutputUnchanged)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore source;
        const BlockId source_block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, source_block, kDc,
                               "title[@xml:lang=x-default]", "XMP title"),
                  kInvalidEntryId);
        Entry native_title;
        native_title.key = make_iptc_dataset_key(2U, 5U);
        static constexpr std::string_view kIptcTitle = "IPTC title";
        native_title.value                           = make_bytes(
            source.arena(),
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                           kIptcTitle.data()),
                                                                 kIptcTitle.size()));
        native_title.origin.block = source_block;
        native_title.flags        = EntryFlags::Dirty;
        ASSERT_NE(source.add_entry(native_title), kInvalidEntryId);
        source.finalize();

        MetaStore output;
        const BlockId output_block = output.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&output, output_block,
                               "https://example.test/custom/", "Note",
                               "sentinel"),
                  kInvalidEntryId);
        output.finalize();

        const MetadataReconcileResult result = reconcile_descriptive_metadata(
            source, MetadataReconcileOptions {}, &output);
        EXPECT_EQ(result.status, MetadataReconcileStatus::Conflict);
        EXPECT_EQ(result.failed_field, MetadataReconcileField::Title);
        EXPECT_EQ(result.failed_family, MetadataReconcileFamily::Iptc);
        EXPECT_EQ(result.failed_stage,
                  MetadataReconcileStage::ResolveAuthority);
        EXPECT_NE(result.failed_entry, kInvalidEntryId);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);
        EXPECT_EQ(result.groups_reconciled, 0U);
        ASSERT_EQ(output.entries().size(), 1U);
        const Entry& retained = output.entry(0U);
        ASSERT_EQ(retained.key.kind, MetaKeyKind::XmpProperty);
        const std::span<const std::byte> schema_bytes = output.arena().span(
            retained.key.data.xmp_property.schema_ns);
        const std::span<const std::byte> path_bytes = output.arena().span(
            retained.key.data.xmp_property.property_path);
        const std::string_view output_schema(reinterpret_cast<const char*>(
                                                 schema_bytes.data()),
                                             schema_bytes.size());
        const std::string_view output_path(reinterpret_cast<const char*>(
                                               path_bytes.data()),
                                           path_bytes.size());
        EXPECT_EQ(output_schema, "https://example.test/custom/");
        EXPECT_EQ(output_path, "Note");
        const std::span<const std::byte> output_text = output.arena().span(
            retained.value.data.span);
        ASSERT_EQ(output_text.size(), 8U);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                       output_text.data()),
                                   output_text.size()),
                  "sentinel");
        EXPECT_EQ(source.entries().size(), 2U);
    }

    TEST(MetadataReconcile, IptcAuthorityPreservesSparseCreatorEntryIds)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        const EntryId first = add_xmp_text(&store, block, kDc, "creator[1]",
                                           "Old first");
        const EntryId tenth = add_xmp_text(&store, block, kDc, "creator[10]",
                                           "Old tenth");
        ASSERT_NE(first, kInvalidEntryId);
        ASSERT_NE(tenth, kInvalidEntryId);
        ASSERT_LT(first, tenth);

        Entry native_first;
        native_first.key = make_iptc_dataset_key(2U, 80U);
        static constexpr std::string_view kNewFirst = "New first";
        native_first.value                          = make_bytes(
            store.arena(),
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                           kNewFirst.data()),
                                                                kNewFirst.size()));
        native_first.origin.block          = block;
        native_first.origin.order_in_block = 1U;
        native_first.flags                 = EntryFlags::Dirty;
        ASSERT_NE(store.add_entry(native_first), kInvalidEntryId);

        Entry native_second;
        native_second.key = make_iptc_dataset_key(2U, 80U);
        static constexpr std::string_view kNewSecond = "New second";
        native_second.value                          = make_bytes(
            store.arena(),
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                           kNewSecond.data()),
                                                                kNewSecond.size()));
        native_second.origin.block          = block;
        native_second.origin.order_in_block = 2U;
        native_second.flags                 = EntryFlags::Dirty;
        ASSERT_NE(store.add_entry(native_second), kInvalidEntryId);
        store.finalize();

        MetadataReconcileOptions options;
        options.conflict_policy
            = MetadataReconcileConflictPolicy::PreferSourceOrder;
        options.family_priority = { MetadataReconcileFamily::Iptc,
                                    MetadataReconcileFamily::Xmp,
                                    MetadataReconcileFamily::Exif };
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(store, options, &store);

        ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
        EXPECT_EQ(result.entries_updated, 2U);
        EXPECT_EQ(result.entries_removed, 0U);
        EXPECT_EQ(result.entries_added, 0U);
        ASSERT_LT(first, store.entries().size());
        ASSERT_LT(tenth, store.entries().size());
        EXPECT_EQ(entry_text(store, store.entry(first)), kNewFirst);
        EXPECT_EQ(entry_text(store, store.entry(tenth)), kNewSecond);
        EXPECT_EQ(entry_path(store, store.entry(first)), "creator[1]");
        EXPECT_EQ(entry_path(store, store.entry(tenth)), "creator[10]");
    }

    TEST(MetadataReconcile, InvalidOriginDoesNotShareCharsetMarkerScope)
    {
        MetaStore source;
        const BlockId unrelated_block = source.add_block(BlockInfo {});
        Entry native_title;
        native_title.key = make_iptc_dataset_key(2U, 5U);
        static constexpr std::string_view kNonAscii = "caf\xc3\xa9";
        native_title.value                          = make_bytes(
            source.arena(),
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                           kNonAscii.data()),
                                                                kNonAscii.size()));
        native_title.origin.block = kInvalidBlockId;
        native_title.flags        = EntryFlags::Dirty;
        ASSERT_NE(source.add_entry(native_title), kInvalidEntryId);

        Entry marker;
        marker.key = make_iptc_dataset_key(1U, 90U);
        static constexpr std::array<std::byte, 3U> kUtf8Marker
            = { std::byte { 0x1bU }, std::byte { 0x25U }, std::byte { 0x47U } };
        marker.value        = make_bytes(source.arena(), kUtf8Marker);
        marker.origin.block = unrelated_block;
        marker.flags        = EntryFlags::Dirty;
        ASSERT_NE(source.add_entry(marker), kInvalidEntryId);
        source.finalize();

        MetaStore output;
        const BlockId output_block = output.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&output, output_block,
                               "https://example.test/custom/", "Note",
                               "sentinel"),
                  kInvalidEntryId);
        output.finalize();

        const MetadataReconcileResult result = reconcile_descriptive_metadata(
            source, MetadataReconcileOptions {}, &output);
        EXPECT_EQ(result.status,
                  MetadataReconcileStatus::SourceEncodingConflict);
        EXPECT_EQ(result.failed_field, MetadataReconcileField::Title);
        EXPECT_EQ(result.failed_family, MetadataReconcileFamily::Iptc);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(entry_text(output, output.entry(0U)), "sentinel");
    }

    TEST(MetadataReconcile, SixPriorityPermutationsSelectXmpIptcOrExif)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        const std::array<std::array<MetadataReconcileFamily, 3U>, 6U> orders
            = { {
                { MetadataReconcileFamily::Xmp, MetadataReconcileFamily::Iptc,
                  MetadataReconcileFamily::Exif },
                { MetadataReconcileFamily::Xmp, MetadataReconcileFamily::Exif,
                  MetadataReconcileFamily::Iptc },
                { MetadataReconcileFamily::Iptc, MetadataReconcileFamily::Xmp,
                  MetadataReconcileFamily::Exif },
                { MetadataReconcileFamily::Iptc, MetadataReconcileFamily::Exif,
                  MetadataReconcileFamily::Xmp },
                { MetadataReconcileFamily::Exif, MetadataReconcileFamily::Xmp,
                  MetadataReconcileFamily::Iptc },
                { MetadataReconcileFamily::Exif, MetadataReconcileFamily::Iptc,
                  MetadataReconcileFamily::Xmp },
            } };
        for (const auto& order : orders) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            ASSERT_NE(add_xmp_text(&source, block, kDc,
                                   "description[@xml:lang=x-default]",
                                   "XMP description"),
                      kInvalidEntryId);
            ASSERT_NE(add_xmp_text(&source, block, kTiff, "ImageDescription",
                                   "XMP description"),
                      kInvalidEntryId);
            ASSERT_NE(add_iptc_text(&source, block, 120U, "IPTC caption"),
                      kInvalidEntryId);
            ASSERT_NE(add_exif_text(&source, block, 0x010eU, "EXIF caption"),
                      kInvalidEntryId);
            source.finalize();

            MetadataReconcileOptions options;
            options.enable_exif_bridge = true;
            options.conflict_policy
                = MetadataReconcileConflictPolicy::PreferSourceOrder;
            options.family_priority = order;
            MetaStore output;
            const MetadataReconcileResult result
                = reconcile_descriptive_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
            const std::string_view expected
                = order[0U] == MetadataReconcileFamily::Xmp
                      ? "XMP description"
                      : (order[0U] == MetadataReconcileFamily::Iptc
                             ? "IPTC caption"
                             : "EXIF caption");
            const EntryId dc    = find_xmp(output, kDc,
                                           "description[@xml:lang=x-default]");
            const EntryId alias = find_xmp(output, kTiff, "ImageDescription");
            const EntryId iptc  = find_iptc(output, 120U);
            const EntryId exif  = find_exif(output, 0x010eU);
            ASSERT_NE(dc, kInvalidEntryId);
            ASSERT_NE(alias, kInvalidEntryId);
            ASSERT_NE(iptc, kInvalidEntryId);
            ASSERT_NE(exif, kInvalidEntryId);
            EXPECT_EQ(entry_text(output, output.entry(dc)), expected);
            EXPECT_EQ(entry_text(output, output.entry(alias)), expected);
            EXPECT_EQ(entry_text(output, output.entry(iptc)), expected);
            EXPECT_EQ(entry_text(output, output.entry(exif)), expected);
        }
    }

    TEST(MetadataReconcile, CanonicalDcWinsAliasAndPreservesAliasNativeIds)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        static constexpr std::string_view kCustom
            = "https://example.test/custom/";
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        static constexpr std::string_view kDescription = "Caf\xc3\xa9";
        const EntryId canonical
            = add_xmp_text(&store, block, kDc,
                           "description[@xml:lang=x-default]", kDescription);
        const EntryId alias   = add_xmp_text(&store, block, kTiff,
                                             "ImageDescription", "stale alias");
        const EntryId sibling = add_xmp_text(&store, block, kDc,
                                             "description[@xml:lang=fr-FR]",
                                             "Bonjour");
        const EntryId custom  = add_xmp_text(&store, block, kCustom, "Note",
                                             "keep custom tree");
        Entry native_entry;
        native_entry.key   = make_exif_tag_key(store.arena(), "ifd0", 0x010eU);
        native_entry.value = make_text(store.arena(), "stale native",
                                       TextEncoding::Ascii);
        native_entry.origin.block          = block;
        native_entry.origin.order_in_block = 41U;
        native_entry.origin.wire_type      = { WireFamily::Tiff, 2U };
        native_entry.origin.wire_count     = native_entry.value.count + 1U;
        native_entry.origin.wire_type_name = store.arena().append_string(
            "original-ascii-hint");
        native_entry.origin.name_context_kind
            = EntryNameContextKind::SonyMainCompat;
        native_entry.origin.name_context_variant = 7U;
        native_entry.flags                       = EntryFlags::Dirty;
        const EntryId native = store.add_entry(native_entry);
        const EntryId native_tombstone
            = add_exif_text(&store, block, 0x010eU, "", 2U, TextEncoding::Ascii,
                            EntryFlags::Dirty | EntryFlags::Deleted);
        ASSERT_NE(canonical, kInvalidEntryId);
        ASSERT_NE(alias, kInvalidEntryId);
        ASSERT_NE(sibling, kInvalidEntryId);
        ASSERT_NE(custom, kInvalidEntryId);
        ASSERT_NE(native, kInvalidEntryId);
        ASSERT_NE(native_tombstone, kInvalidEntryId);
        store.finalize();

        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        options.conflict_policy
            = MetadataReconcileConflictPolicy::PreferSourceOrder;
        for (const MetadataReconcileDeletionPolicy deletion_policy :
             { MetadataReconcileDeletionPolicy::Ignore,
               MetadataReconcileDeletionPolicy::PropagateDirty }) {
            options.deletion_policy = deletion_policy;
            MetaStore output        = store;
            const MetadataReconcileResult result
                = reconcile_descriptive_metadata(output, options, &output);
            ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
            ASSERT_LT(canonical, output.entries().size());
            ASSERT_LT(alias, output.entries().size());
            ASSERT_LT(sibling, output.entries().size());
            ASSERT_LT(custom, output.entries().size());
            ASSERT_LT(native, output.entries().size());
            ASSERT_LT(native_tombstone, output.entries().size());
            EXPECT_EQ(entry_text(output, output.entry(canonical)),
                      kDescription);
            EXPECT_EQ(entry_text(output, output.entry(alias)), kDescription);
            EXPECT_EQ(entry_text(output, output.entry(native)), kDescription);
            EXPECT_EQ(entry_text(output, output.entry(sibling)), "Bonjour");
            EXPECT_EQ(entry_text(output, output.entry(custom)),
                      "keep custom tree");

            const Entry& projected_native = output.entry(native);
            EXPECT_EQ(projected_native.key.data.exif_tag.tag, 0x010eU);
            EXPECT_EQ(projected_native.value.kind, MetaValueKind::Text);
            EXPECT_EQ(projected_native.value.elem_type, MetaElementType::U8);
            EXPECT_EQ(projected_native.value.text_encoding, TextEncoding::Utf8);
            EXPECT_EQ(projected_native.value.count, kDescription.size());
            EXPECT_EQ(projected_native.origin.wire_type.family,
                      WireFamily::Tiff);
            EXPECT_EQ(projected_native.origin.wire_type.code, 129U);
            EXPECT_EQ(projected_native.origin.wire_count,
                      kDescription.size() + 1U);
            EXPECT_EQ(projected_native.origin.block, block);
            EXPECT_EQ(projected_native.origin.order_in_block, 41U);
            EXPECT_EQ(projected_native.origin.name_context_kind,
                      EntryNameContextKind::SonyMainCompat);
            EXPECT_EQ(projected_native.origin.name_context_variant, 7U);
            EXPECT_TRUE(entry_wire_type_name(output, projected_native).empty());
            EXPECT_TRUE(
                any(output.entry(native_tombstone).flags, EntryFlags::Deleted));
            EXPECT_EQ(find_exif(output, 0x010eU), native);
        }
    }

    TEST(MetadataReconcile, ExifPriorityUsesAliasFallbackAndRetainsNativeId)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        MetaStore store;
        const BlockId block  = store.add_block(BlockInfo {});
        const EntryId native = add_exif_text(&store, block, 0x010eU,
                                             "Caf\xc3\xa9", 129U,
                                             TextEncoding::Utf8);
        ASSERT_NE(native, kInvalidEntryId);
        store.finalize();

        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        options.family_priority    = { MetadataReconcileFamily::Exif,
                                       MetadataReconcileFamily::Xmp,
                                       MetadataReconcileFamily::Iptc };
        MetaStore output;
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(store, options, &output);
        ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
        const EntryId dc    = find_xmp(output, kDc,
                                       "description[@xml:lang=x-default]");
        const EntryId alias = find_xmp(output, kTiff, "ImageDescription");
        ASSERT_NE(dc, kInvalidEntryId);
        ASSERT_NE(alias, kInvalidEntryId);
        EXPECT_EQ(entry_text(output, output.entry(dc)), "Caf\xc3\xa9");
        EXPECT_EQ(entry_text(output, output.entry(alias)), "Caf\xc3\xa9");
        EXPECT_EQ(find_exif(output, 0x010eU), native);
        EXPECT_EQ(output.entry(native).origin.wire_type.code, 129U);
    }

    TEST(MetadataReconcile, InvalidExifOriginCannotScopeProjectedUtf8)
    {
        MetaStore source;
        const EntryId native = add_exif_text(&source, kInvalidBlockId, 0x010eU,
                                             "Caf\xc3\xa9", 129U,
                                             TextEncoding::Utf8);
        ASSERT_NE(native, kInvalidEntryId);
        source.finalize();
        MetaStore output;
        const BlockId output_block = output.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&output, output_block,
                               "https://example.test/custom/", "Note",
                               "sentinel"),
                  kInvalidEntryId);
        output.finalize();
        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.family_priority    = { MetadataReconcileFamily::Exif,
                                       MetadataReconcileFamily::Xmp,
                                       MetadataReconcileFamily::Iptc };
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataReconcileStatus::SourceEncodingConflict);
        EXPECT_EQ(result.failed_field, MetadataReconcileField::Description);
        EXPECT_EQ(result.failed_family, MetadataReconcileFamily::Xmp);
        EXPECT_EQ(result.entries_added, 0U);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(entry_text(output, output.entry(0U)), "sentinel");
    }

    TEST(MetadataReconcile, TiffAliasFallbackCreatesCanonicalDcAndNativeExif)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        const EntryId alias = add_xmp_text(&source, block, kTiff,
                                           "ImageDescription", "Alias source");
        ASSERT_NE(alias, kInvalidEntryId);
        source.finalize();
        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        MetaStore output;
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
        const EntryId canonical = find_xmp(output, kDc,
                                           "description[@xml:lang=x-default]");
        const EntryId exif      = find_exif(output, 0x010eU);
        ASSERT_NE(canonical, kInvalidEntryId);
        ASSERT_NE(exif, kInvalidEntryId);
        EXPECT_EQ(find_xmp(output, kTiff, "ImageDescription"), alias);
        EXPECT_EQ(entry_text(output, output.entry(canonical)), "Alias source");
        EXPECT_EQ(entry_text(output, output.entry(alias)), "Alias source");
        EXPECT_EQ(entry_text(output, output.entry(exif)), "Alias source");
    }

    TEST(MetadataReconcile, SingleCreatorAndScalarRightsSynchronize)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        MetaStore source;
        const BlockId block   = source.add_block(BlockInfo {});
        const EntryId creator = add_xmp_text(&source, block, kDc, "creator[7]",
                                             "Alice");
        const EntryId artist_alias    = add_xmp_text(&source, block, kTiff,
                                                     "Artist", "Old artist");
        const EntryId artist          = add_exif_text(&source, block, 0x013bU,
                                                      "Old artist");
        const EntryId rights          = add_xmp_text(&source, block, kDc,
                                                     "rights[@xml:lang=x-default]",
                                                     "Owner");
        const EntryId copyright_alias = add_xmp_text(&source, block, kTiff,
                                                     "Copyright", "Old rights");
        const EntryId copyright       = add_exif_text(&source, block, 0x8298U,
                                                      "Old rights");
        ASSERT_NE(creator, kInvalidEntryId);
        ASSERT_NE(artist_alias, kInvalidEntryId);
        ASSERT_NE(artist, kInvalidEntryId);
        ASSERT_NE(rights, kInvalidEntryId);
        ASSERT_NE(copyright_alias, kInvalidEntryId);
        ASSERT_NE(copyright, kInvalidEntryId);
        source.finalize();
        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        options.conflict_policy
            = MetadataReconcileConflictPolicy::PreferSourceOrder;
        MetaStore output;
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
        EXPECT_EQ(find_xmp(output, kTiff, "Artist"), artist_alias);
        EXPECT_EQ(find_exif(output, 0x013bU), artist);
        EXPECT_EQ(entry_text(output, output.entry(artist_alias)), "Alice");
        EXPECT_EQ(entry_text(output, output.entry(artist)), "Alice");
        EXPECT_EQ(find_xmp(output, kTiff, "Copyright"), copyright_alias);
        EXPECT_EQ(find_exif(output, 0x8298U), copyright);
        EXPECT_EQ(entry_text(output, output.entry(copyright_alias)), "Owner");
        EXPECT_EQ(entry_text(output, output.entry(copyright)), "Owner");
        EXPECT_EQ(entry_path(output, output.entry(creator)), "creator[7]");
        EXPECT_EQ(entry_text(output, output.entry(rights)), "Owner");
    }

    TEST(MetadataReconcile, AliasConflictRollsBackAndDuplicateAliasIsAmbiguous)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, block, kDc,
                               "description[@xml:lang=x-default]", "DC"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kTiff, "ImageDescription",
                               "alias"),
                  kInvalidEntryId);
        source.finalize();
        MetaStore output;
        const BlockId out_block = output.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&output, out_block,
                               "https://example.test/custom/", "Note",
                               "sentinel"),
                  kInvalidEntryId);
        output.finalize();

        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        const MetadataReconcileResult conflict
            = reconcile_descriptive_metadata(source, options, &output);
        EXPECT_EQ(conflict.status, MetadataReconcileStatus::Conflict);
        EXPECT_EQ(conflict.failed_field, MetadataReconcileField::Description);
        EXPECT_EQ(conflict.failed_family, MetadataReconcileFamily::Xmp);
        EXPECT_EQ(conflict.entries_added, 0U);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(entry_text(output, output.entry(0U)), "sentinel");

        MetaStore duplicate;
        const BlockId duplicate_block = duplicate.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&duplicate, duplicate_block, kTiff,
                               "ImageDescription", "one"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&duplicate, duplicate_block, kTiff,
                               "ImageDescription", "two"),
                  kInvalidEntryId);
        duplicate.finalize();
        const MetadataReconcileResult ambiguous
            = reconcile_descriptive_metadata(duplicate, options, &output);
        EXPECT_EQ(ambiguous.status, MetadataReconcileStatus::AmbiguousSource);
        EXPECT_EQ(ambiguous.failed_family, MetadataReconcileFamily::Xmp);
        EXPECT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(entry_text(output, output.entry(0U)), "sentinel");
    }

    TEST(MetadataReconcile, DirtyAliasDeletionCreatesMissingNativeMarker)
    {
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        const EntryId alias
            = add_xmp_text(&source, block, kTiff, "ImageDescription", "",
                           EntryFlags::Dirty | EntryFlags::Deleted, 37U);
        ASSERT_NE(alias, kInvalidEntryId);
        source.finalize();

        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        options.deletion_policy
            = MetadataReconcileDeletionPolicy::PropagateDirty;
        MetaStore output;
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
        const EntryId native = find_exif(output, 0x010eU, true);
        ASSERT_NE(native, kInvalidEntryId);
        EXPECT_TRUE(any(output.entry(native).flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(output.entry(native).flags, EntryFlags::Deleted));
        EXPECT_EQ(output.entry(native).origin.block, block);
        EXPECT_EQ(output.entry(native).origin.order_in_block, 37U);
        EXPECT_EQ(output.entry(native).value.kind, MetaValueKind::Text);
        EXPECT_EQ(output.entry(native).value.elem_type, MetaElementType::U8);
        EXPECT_EQ(output.entry(native).value.text_encoding,
                  TextEncoding::Ascii);
        EXPECT_EQ(output.entry(native).value.count, 0U);
        EXPECT_EQ(output.entry(native).origin.wire_type.family,
                  WireFamily::Tiff);
        EXPECT_EQ(output.entry(native).origin.wire_type.code, 2U);
        EXPECT_EQ(output.entry(native).origin.wire_count, 1U);
        const MetadataReconcileResult reapplied
            = reconcile_descriptive_metadata(output, options, &output);
        ASSERT_EQ(reapplied.status, MetadataReconcileStatus::Ok);
        EXPECT_EQ(reapplied.entries_added, 0U);
        EXPECT_EQ(reapplied.entries_updated, 0U);
        EXPECT_EQ(reapplied.entries_removed, 0U);
        EXPECT_EQ(find_exif(output, 0x010eU, true), native);
    }

    TEST(MetadataReconcile, DirtyAliasTombstoneConflictPreservesExactOutput)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        MetaStore source;
        const BlockId source_block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, source_block, kDc,
                               "description[@xml:lang=x-default]", "DC"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, source_block, kTiff, "ImageDescription",
                               "", EntryFlags::Dirty | EntryFlags::Deleted),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_text(&source, source_block, 0x010eU, "native"),
                  kInvalidEntryId);
        source.finalize();

        MetaStore output;
        const BlockId output_block = output.add_block(BlockInfo {});
        const EntryId note         = add_xmp_text(&output, output_block,
                                                  "https://example.test/custom/",
                                                  "Note", "retain exact output");
        ASSERT_NE(note, kInvalidEntryId);
        output.finalize();
        const std::vector<std::byte> arena_before(output.arena().bytes().begin(),
                                                  output.arena().bytes().end());
        const Entry expected = output.entry(note);

        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        options.deletion_policy
            = MetadataReconcileDeletionPolicy::PropagateDirty;
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(source, options, &output);
        EXPECT_EQ(result.status, MetadataReconcileStatus::Conflict);
        EXPECT_EQ(result.failed_family, MetadataReconcileFamily::Exif);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);
        ASSERT_EQ(output.entries().size(), 1U);
        ASSERT_EQ(output.block_count(), 1U);
        EXPECT_EQ(output.entry(note).key.kind, expected.key.kind);
        EXPECT_EQ(output.entry(note).key.data.xmp_property.schema_ns.offset,
                  expected.key.data.xmp_property.schema_ns.offset);
        EXPECT_EQ(output.entry(note).key.data.xmp_property.property_path.offset,
                  expected.key.data.xmp_property.property_path.offset);
        EXPECT_EQ(output.entry(note).value.data.span.offset,
                  expected.value.data.span.offset);
        EXPECT_EQ(output.entry(note).value.data.span.size,
                  expected.value.data.span.size);
        EXPECT_EQ(output.entry(note).flags, expected.flags);
        EXPECT_EQ(output.arena().bytes().size(), arena_before.size());
        EXPECT_TRUE(std::equal(output.arena().bytes().begin(),
                               output.arena().bytes().end(),
                               arena_before.begin(), arena_before.end()));
    }

    TEST(MetadataReconcile, ExifProjectionIsIncludedInOperationBudget)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore source;
        const BlockId source_block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, source_block, kDc,
                               "description[@xml:lang=x-default]", "caption"),
                  kInvalidEntryId);
        source.finalize();

        MetaStore output;
        const BlockId output_block = output.add_block(BlockInfo {});
        const EntryId note         = add_xmp_text(&output, output_block,
                                                  "https://example.test/custom/",
                                                  "Note", "unchanged");
        ASSERT_NE(note, kInvalidEntryId);
        output.finalize();
        const std::vector<std::byte> arena_before(output.arena().bytes().begin(),
                                                  output.arena().bytes().end());

        MetadataReconcileOptions options;
        options.enable_exif_bridge    = true;
        options.participate_iptc      = false;
        options.title                 = false;
        options.creators              = false;
        options.keywords              = false;
        options.copyright             = false;
        options.max_source_properties = 1U;
        options.max_operations        = 1U;
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(source, options, &output);
        EXPECT_EQ(result.status,
                  MetadataReconcileStatus::OperationLimitExceeded);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(entry_text(output, output.entry(note)), "unchanged");
        ASSERT_EQ(output.arena().bytes().size(), arena_before.size());
        EXPECT_TRUE(std::equal(output.arena().bytes().begin(),
                               output.arena().bytes().end(),
                               arena_before.begin(), arena_before.end()));

        MetaStore oversized;
        const BlockId oversized_block = oversized.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&oversized, oversized_block,
                               "https://example.test/custom/", "Unrelated",
                               "keep"),
                  kInvalidEntryId);
        const std::string too_long(65537U, 'a');
        const EntryId oversized_caption
            = add_xmp_text(&oversized, oversized_block, kDc,
                           "description[@xml:lang=x-default]", too_long);
        ASSERT_NE(oversized_caption, kInvalidEntryId);
        oversized.finalize();
        options.max_operations = kMetadataReconcileMaxOperations;
        const auto rejected = reconcile_descriptive_metadata(oversized, options,
                                                             &output);
        EXPECT_EQ(rejected.status, MetadataReconcileStatus::ValueTooLong);
        EXPECT_EQ(rejected.failed_field, MetadataReconcileField::Description);
        EXPECT_EQ(rejected.failed_family, MetadataReconcileFamily::Xmp);
        EXPECT_EQ(rejected.failed_stage,
                  MetadataReconcileStage::ResolveAuthority);
        EXPECT_EQ(rejected.failed_entry, oversized_caption);
        EXPECT_EQ(rejected.entries_added + rejected.entries_updated
                      + rejected.entries_removed,
                  0U);
        ASSERT_EQ(output.entries().size(), 1U);
        EXPECT_EQ(entry_text(output, output.entry(note)), "unchanged");
        EXPECT_TRUE(std::equal(output.arena().bytes().begin(),
                               output.arena().bytes().end(),
                               arena_before.begin(), arena_before.end()));
    }

    TEST(MetadataReconcile, CreatorMultiValueAndLegacyCopyrightAreUnsupported)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore creators;
        const BlockId creator_block = creators.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&creators, creator_block, kDc, "creator[1]",
                               "Alice"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&creators, creator_block, kDc, "creator[2]",
                               "Bob"),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_text(&creators, creator_block, 0x013bU, "Alice"),
                  kInvalidEntryId);
        creators.finalize();
        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        MetaStore output;
        const MetadataReconcileResult creator_result
            = reconcile_descriptive_metadata(creators, options, &output);
        EXPECT_EQ(creator_result.status,
                  MetadataReconcileStatus::UnsupportedSourceShape);
        EXPECT_EQ(creator_result.failed_field, MetadataReconcileField::Creator);

        MetaStore copyright;
        const BlockId copyright_block = copyright.add_block(BlockInfo {});
        static constexpr char kLegacyCopyright[] = "Author\0Owner";
        ASSERT_NE(add_exif_text(&copyright, copyright_block, 0x8298U,
                                std::string_view(kLegacyCopyright,
                                                 sizeof(kLegacyCopyright) - 1U)),
                  kInvalidEntryId);
        copyright.finalize();
        const MetadataReconcileResult copyright_result
            = reconcile_descriptive_metadata(copyright, options, &output);
        EXPECT_EQ(copyright_result.status,
                  MetadataReconcileStatus::UnsupportedSourceShape);
        EXPECT_EQ(copyright_result.failed_field,
                  MetadataReconcileField::Rights);
    }

    TEST(MetadataReconcile, DirtyDeletionPropagatesAcrossAllThreeFamilies)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, block, kDc,
                               "description[@xml:lang=x-default]", "",
                               EntryFlags::Dirty | EntryFlags::Deleted),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_text(&source, block, 120U, "remove me"),
                  kInvalidEntryId);
        ASSERT_NE(add_exif_text(&source, block, 0x010eU, "remove me"),
                  kInvalidEntryId);
        source.finalize();

        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.conflict_policy
            = MetadataReconcileConflictPolicy::PreferSourceOrder;
        options.deletion_policy
            = MetadataReconcileDeletionPolicy::PropagateDirty;
        MetaStore output;
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
        const EntryId iptc = find_iptc(output, 120U, true);
        const EntryId exif = find_exif(output, 0x010eU, true);
        ASSERT_NE(iptc, kInvalidEntryId);
        ASSERT_NE(exif, kInvalidEntryId);
        EXPECT_TRUE(any(output.entry(iptc).flags, EntryFlags::Deleted));
        EXPECT_TRUE(any(output.entry(exif).flags, EntryFlags::Deleted));
    }

    TEST(MetadataReconcile, IgnoreDeletionKeepsActiveIptcAndExifValues)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, block, kDc,
                               "description[@xml:lang=x-default]", "",
                               EntryFlags::Dirty | EntryFlags::Deleted),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_text(&source, block, 120U, "keep"), kInvalidEntryId);
        ASSERT_NE(add_exif_text(&source, block, 0x010eU, "keep"),
                  kInvalidEntryId);
        source.finalize();
        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.deletion_policy    = MetadataReconcileDeletionPolicy::Ignore;
        MetaStore output;
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
        const EntryId iptc = find_iptc(output, 120U);
        const EntryId exif = find_exif(output, 0x010eU);
        ASSERT_NE(iptc, kInvalidEntryId);
        ASSERT_NE(exif, kInvalidEntryId);
        EXPECT_FALSE(any(output.entry(iptc).flags, EntryFlags::Deleted));
        EXPECT_FALSE(any(output.entry(exif).flags, EntryFlags::Deleted));
        EXPECT_EQ(entry_text(output, output.entry(iptc)), "keep");
        EXPECT_EQ(entry_text(output, output.entry(exif)), "keep");
    }

    TEST(MetadataReconcile,
         AliasDirtyTombstonesRespectActivePrecedenceAndIgnore)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        for (const MetadataReconcileDeletionPolicy policy :
             { MetadataReconcileDeletionPolicy::Ignore,
               MetadataReconcileDeletionPolicy::PropagateDirty }) {
            MetaStore source;
            const BlockId block = source.add_block(BlockInfo {});
            const EntryId dc    = add_xmp_text(&source, block, kDc,
                                               "description[@xml:lang=x-default]",
                                               "keep");
            const EntryId alias_tombstone
                = add_xmp_text(&source, block, kTiff, "ImageDescription", "",
                               EntryFlags::Dirty | EntryFlags::Deleted);
            const EntryId alias_active
                = policy == MetadataReconcileDeletionPolicy::PropagateDirty
                      ? add_xmp_text(&source, block, kTiff, "ImageDescription",
                                     "keep")
                      : kInvalidEntryId;
            const EntryId native = add_exif_text(&source, block, 0x010eU,
                                                 "old native");
            ASSERT_NE(dc, kInvalidEntryId);
            ASSERT_NE(alias_tombstone, kInvalidEntryId);
            ASSERT_NE(native, kInvalidEntryId);
            if (policy == MetadataReconcileDeletionPolicy::PropagateDirty)
                ASSERT_NE(alias_active, kInvalidEntryId);
            source.finalize();

            MetadataReconcileOptions options;
            options.enable_exif_bridge = true;
            options.participate_iptc   = false;
            options.deletion_policy    = policy;
            options.conflict_policy
                = MetadataReconcileConflictPolicy::PreferSourceOrder;
            MetaStore output;
            const MetadataReconcileResult result
                = reconcile_descriptive_metadata(source, options, &output);
            ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
            const EntryId active_alias  = find_xmp(output, kTiff,
                                                   "ImageDescription");
            const EntryId active_native = find_exif(output, 0x010eU);
            ASSERT_NE(active_alias, kInvalidEntryId);
            ASSERT_NE(active_native, kInvalidEntryId);
            EXPECT_EQ(entry_text(output, output.entry(active_alias)), "keep");
            EXPECT_EQ(entry_text(output, output.entry(active_native)), "keep");
            EXPECT_TRUE(
                any(output.entry(alias_tombstone).flags, EntryFlags::Deleted));
            if (alias_active != kInvalidEntryId)
                EXPECT_EQ(find_xmp(output, kTiff, "ImageDescription"),
                          alias_active);
        }
    }

    TEST(MetadataReconcile, AliasOnlyDirtyDeletionReachesExif)
    {
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        const EntryId alias
            = add_xmp_text(&source, block, kTiff, "ImageDescription", "",
                           EntryFlags::Dirty | EntryFlags::Deleted);
        Entry native_entry;
        native_entry.key   = make_exif_tag_key(source.arena(), "ifd0", 0x010eU);
        native_entry.value = make_text(source.arena(), "inactive clean value",
                                       TextEncoding::Ascii);
        native_entry.origin.block          = block;
        native_entry.origin.order_in_block = 51U;
        native_entry.origin.wire_type      = { WireFamily::Tiff, 2U };
        native_entry.origin.wire_count     = native_entry.value.count + 1U;
        native_entry.origin.wire_type_name = source.arena().append_string(
            "clean-native-hint");
        native_entry.origin.name_context_kind
            = EntryNameContextKind::SonyMainCompat;
        native_entry.origin.name_context_variant = 9U;
        native_entry.flags                       = EntryFlags::Deleted;
        const EntryId native = source.add_entry(native_entry);
        ASSERT_NE(alias, kInvalidEntryId);
        ASSERT_NE(native, kInvalidEntryId);
        source.finalize();
        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        options.deletion_policy
            = MetadataReconcileDeletionPolicy::PropagateDirty;
        options.conflict_policy
            = MetadataReconcileConflictPolicy::PreferSourceOrder;
        MetaStore output;
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
        const EntryId output_native = find_exif(output, 0x010eU, true);
        ASSERT_NE(output_native, kInvalidEntryId);
        EXPECT_EQ(output_native, native);
        EXPECT_EQ(output.entries().size(), source.entries().size() + 1U);
        EXPECT_TRUE(
            any(output.entry(output_native).flags, EntryFlags::Deleted));
        EXPECT_TRUE(any(output.entry(output_native).flags, EntryFlags::Dirty));
        EXPECT_EQ(output.entry(output_native).origin.block, block);
        EXPECT_EQ(output.entry(output_native).origin.order_in_block, 51U);
        EXPECT_EQ(output.entry(output_native).origin.wire_type.code, 2U);
        EXPECT_EQ(output.entry(output_native).origin.wire_count,
                  native_entry.origin.wire_count);
        EXPECT_EQ(output.entry(output_native).origin.name_context_kind,
                  EntryNameContextKind::SonyMainCompat);
        EXPECT_EQ(output.entry(output_native).origin.name_context_variant, 9U);
        EXPECT_EQ(entry_wire_type_name(output, output.entry(output_native)),
                  "clean-native-hint");
    }

    TEST(MetadataReconcile,
         OnePropertyOneOperationSucceedsAndLowerLimitsRollback)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, block, kDc,
                               "title[@xml:lang=x-default]", "Title"),
                  kInvalidEntryId);
        source.finalize();

        MetadataReconcileOptions options;
        options.description           = false;
        options.creators              = false;
        options.keywords              = false;
        options.copyright             = false;
        options.max_source_properties = 1U;
        options.max_operations        = 1U;
        MetaStore output;
        const MetadataReconcileResult one_op
            = reconcile_descriptive_metadata(source, options, &output);
        ASSERT_EQ(one_op.status, MetadataReconcileStatus::Ok);
        EXPECT_EQ(one_op.entries_added, 1U);
        EXPECT_EQ(find_iptc(output, 5U), 1U);

        MetadataReconcileOptions none_selected;
        none_selected.title       = false;
        none_selected.description = false;
        none_selected.creators    = false;
        none_selected.keywords    = false;
        none_selected.copyright   = false;
        const MetadataReconcileResult no_fields
            = reconcile_descriptive_metadata(source, none_selected, &output);
        EXPECT_EQ(no_fields.status, MetadataReconcileStatus::InvalidOptions);

        MetaStore two_values;
        const BlockId two_block = two_values.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&two_values, two_block, kDc,
                               "title[@xml:lang=x-default]", "Title"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&two_values, two_block, kDc,
                               "description[@xml:lang=x-default]",
                               "Description"),
                  kInvalidEntryId);
        two_values.finalize();
        MetadataReconcileOptions lower_ops;
        lower_ops.max_operations = 1U;
        MetaStore sentinel;
        const BlockId sentinel_block = sentinel.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&sentinel, sentinel_block,
                               "https://example.test/custom/", "Note",
                               "retain"),
                  kInvalidEntryId);
        sentinel.finalize();
        const MetadataReconcileResult rejected
            = reconcile_descriptive_metadata(two_values, lower_ops, &sentinel);
        EXPECT_EQ(rejected.status,
                  MetadataReconcileStatus::OperationLimitExceeded);
        EXPECT_EQ(rejected.entries_added, 0U);
        ASSERT_EQ(sentinel.entries().size(), 1U);
        EXPECT_EQ(entry_text(sentinel, sentinel.entry(0U)), "retain");

        lower_ops                       = MetadataReconcileOptions {};
        lower_ops.max_source_properties = 1U;
        const MetadataReconcileResult property_rejected
            = reconcile_descriptive_metadata(two_values, lower_ops, &sentinel);
        EXPECT_EQ(property_rejected.status,
                  MetadataReconcileStatus::SourceLimitExceeded);
        ASSERT_EQ(sentinel.entries().size(), 1U);
        EXPECT_EQ(entry_text(sentinel, sentinel.entry(0U)), "retain");
    }

    TEST(MetadataReconcile, BlockOnlySourceLimitAndIdempotentExifNoop)
    {
        MetaStore block_only;
        block_only.add_block(BlockInfo {});
        block_only.add_block(BlockInfo {});
        block_only.finalize();
        MetadataReconcileOptions limited;
        limited.max_source_entries = 1U;
        MetaStore output;
        const MetadataReconcileResult block_limit
            = reconcile_descriptive_metadata(block_only, limited, &output);
        EXPECT_EQ(block_limit.status,
                  MetadataReconcileStatus::SourceLimitExceeded);

        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        static constexpr std::string_view kTiff
            = "http://ns.adobe.com/tiff/1.0/";
        MetaStore synchronized;
        const BlockId block         = synchronized.add_block(BlockInfo {});
        const std::string_view text = "same value";
        const EntryId dc            = add_xmp_text(&synchronized, block, kDc,
                                                   "description[@xml:lang=x-default]",
                                                   text);
        const EntryId alias         = add_xmp_text(&synchronized, block, kTiff,
                                                   "ImageDescription", text);
        const EntryId exif = add_exif_text(&synchronized, block, 0x010eU, text);
        ASSERT_NE(dc, kInvalidEntryId);
        ASSERT_NE(alias, kInvalidEntryId);
        ASSERT_NE(exif, kInvalidEntryId);
        synchronized.finalize();
        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        const MetadataReconcileResult first
            = reconcile_descriptive_metadata(synchronized, options,
                                             &synchronized);
        ASSERT_EQ(first.status, MetadataReconcileStatus::Ok);
        EXPECT_EQ(first.entries_added + first.entries_updated
                      + first.entries_removed,
                  0U);
        const MetadataReconcileResult second
            = reconcile_descriptive_metadata(synchronized, options,
                                             &synchronized);
        EXPECT_EQ(second.status, MetadataReconcileStatus::Ok);
        EXPECT_EQ(second.entries_added + second.entries_updated
                      + second.entries_removed,
                  0U);
        EXPECT_EQ(find_xmp(synchronized, kDc,
                           "description[@xml:lang=x-default]"),
                  dc);
        EXPECT_EQ(find_xmp(synchronized, kTiff, "ImageDescription"), alias);
        EXPECT_EQ(find_exif(synchronized, 0x010eU), exif);
    }

    TEST(MetadataReconcile, KeywordEqualityUsesMultisetAndPreservesDuplicates)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, block, kDc, "subject[1]", "red"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kDc, "subject[2]", "red"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, block, kDc, "subject[3]", "blue"),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_text(&source, block, 25U, "blue", 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_text(&source, block, 25U, "red", 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_text(&source, block, 25U, "red", 3U),
                  kInvalidEntryId);
        source.finalize();
        MetadataReconcileOptions options;
        options.title       = false;
        options.description = false;
        options.creators    = false;
        options.copyright   = false;
        MetaStore output;
        const MetadataReconcileResult equal
            = reconcile_descriptive_metadata(source, options, &output);
        ASSERT_EQ(equal.status, MetadataReconcileStatus::Ok);
        EXPECT_EQ(equal.entries_added + equal.entries_updated
                      + equal.entries_removed,
                  0U);

        MetaStore mismatch;
        const BlockId mismatch_block = mismatch.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&mismatch, mismatch_block, kDc, "subject[1]",
                               "red"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&mismatch, mismatch_block, kDc, "subject[2]",
                               "red"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&mismatch, mismatch_block, kDc, "subject[3]",
                               "blue"),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_text(&mismatch, mismatch_block, 25U, "blue", 1U),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_text(&mismatch, mismatch_block, 25U, "red", 2U),
                  kInvalidEntryId);
        ASSERT_NE(add_iptc_text(&mismatch, mismatch_block, 25U, "blue", 3U),
                  kInvalidEntryId);
        mismatch.finalize();
        const MetadataReconcileResult unequal
            = reconcile_descriptive_metadata(mismatch, options, &output);
        EXPECT_EQ(unequal.status, MetadataReconcileStatus::Conflict);
        EXPECT_EQ(unequal.failed_field, MetadataReconcileField::Keywords);
        EXPECT_EQ(unequal.entries_added, 0U);
    }

    TEST(MetadataReconcile, Utf8ProjectionKeepsMarkerInSourceBlockAndReapplies)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore source;
        const BlockId block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, block, kDc,
                               "title[@xml:lang=x-default]", "Caf\xc3\xa9"),
                  kInvalidEntryId);
        source.finalize();
        MetadataReconcileOptions options;
        options.description = false;
        options.creators    = false;
        options.keywords    = false;
        options.copyright   = false;
        MetaStore projected;
        const MetadataReconcileResult first
            = reconcile_descriptive_metadata(source, options, &projected);
        ASSERT_EQ(first.status, MetadataReconcileStatus::Ok);
        const EntryId title = find_iptc(projected, 5U);
        ASSERT_NE(title, kInvalidEntryId);
        EXPECT_EQ(projected.entry(title).origin.block, block);
        EntryId marker = kInvalidEntryId;
        for (EntryId id = 0U; id < projected.entries().size(); ++id) {
            const Entry& entry = projected.entry(id);
            if (entry.key.kind == MetaKeyKind::IptcDataset
                && entry.key.data.iptc_dataset.record == 1U
                && entry.key.data.iptc_dataset.dataset == 90U
                && !any(entry.flags, EntryFlags::Deleted))
                marker = id;
        }
        ASSERT_NE(marker, kInvalidEntryId);
        EXPECT_EQ(projected.entry(marker).origin.block, block);
        const MetadataReconcileResult second
            = reconcile_descriptive_metadata(projected, options, &projected);
        EXPECT_EQ(second.status, MetadataReconcileStatus::Ok);
        EXPECT_EQ(second.entries_added + second.entries_updated
                      + second.entries_removed,
                  0U);
        EXPECT_EQ(find_iptc(projected, 5U), title);
        EXPECT_EQ(projected.entry(marker).origin.block, block);
    }

    TEST(MetadataReconcile, FinalValidationFailureRollsBackExactOutputStore)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore source;
        const BlockId source_block = source.add_block(BlockInfo {});
        ASSERT_NE(add_xmp_text(&source, source_block, kDc,
                               "title[@xml:lang=x-default]", "will stage"),
                  kInvalidEntryId);
        ASSERT_NE(add_xmp_text(&source, source_block,
                               "https://example.test/custom/", "Bad[",
                               "invalid custom path"),
                  kInvalidEntryId);
        source.finalize();

        MetaStore output;
        const BlockId output_block = output.add_block(BlockInfo {});
        const EntryId sentinel     = add_xmp_text(&output, output_block,
                                                  "https://example.test/custom/",
                                                  "Note", "exact output");
        ASSERT_NE(sentinel, kInvalidEntryId);
        output.finalize();
        const uint32_t old_entry_count = static_cast<uint32_t>(
            output.entries().size());

        const MetadataReconcileResult result = reconcile_descriptive_metadata(
            source, MetadataReconcileOptions {}, &output);
        EXPECT_EQ(result.status,
                  MetadataReconcileStatus::UnsupportedSourceShape);
        EXPECT_EQ(result.failed_stage, MetadataReconcileStage::Publish);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);
        ASSERT_EQ(output.entries().size(), old_entry_count);
        ASSERT_LT(sentinel, output.entries().size());
        EXPECT_EQ(entry_text(output, output.entry(sentinel)), "exact output");
        EXPECT_EQ(find_iptc(output, 5U), kInvalidEntryId);
    }

    TEST(MetadataReconcile, TitleAndKeywordsHaveNoExifProjection)
    {
        static constexpr std::string_view kDc
            = "http://purl.org/dc/elements/1.1/";
        MetaStore source;
        const BlockId block   = source.add_block(BlockInfo {});
        const EntryId title   = add_xmp_text(&source, block, kDc,
                                             "title[@xml:lang=x-default]",
                                             "Title");
        const EntryId keyword = add_xmp_text(&source, block, kDc, "subject[1]",
                                             "keyword");
        ASSERT_NE(title, kInvalidEntryId);
        ASSERT_NE(keyword, kInvalidEntryId);
        source.finalize();
        MetadataReconcileOptions options;
        options.enable_exif_bridge = true;
        options.participate_iptc   = false;
        options.description        = false;
        options.creators           = false;
        options.copyright          = false;
        MetaStore output;
        const MetadataReconcileResult result
            = reconcile_descriptive_metadata(source, options, &output);
        ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
        for (const Entry& entry : output.entries())
            EXPECT_NE(entry.key.kind, MetaKeyKind::ExifTag);
        EXPECT_EQ(find_xmp(output, kDc, "title[@xml:lang=x-default]"), title);
        EXPECT_EQ(find_xmp(output, kDc, "subject[1]"), keyword);
    }

}  // namespace
}  // namespace openmeta
