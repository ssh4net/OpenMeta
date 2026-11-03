// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_reconcile.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_xmp_editing.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static constexpr std::string_view kDcNamespace
        = "http://purl.org/dc/elements/1.1/";

    static EntryId add_dc_text(MetaStore* store, BlockId block,
                               std::string_view path, std::string_view text,
                               uint32_t order,
                               EntryFlags flags = EntryFlags::None)
    {
        if (store == nullptr) {
            return kInvalidEntryId;
        }

        Entry entry;
        entry.key   = make_xmp_property_key(store->arena(), kDcNamespace, path);
        entry.value = make_text(store->arena(), text, TextEncoding::Utf8);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.flags                 = flags;
        return store->add_entry(entry);
    }

    static EntryId add_iptc_byline(MetaStore* store, BlockId block,
                                   std::string_view text, uint32_t order)
    {
        if (store == nullptr) {
            return kInvalidEntryId;
        }

        Entry entry;
        entry.key = make_iptc_dataset_key(2U, 80U);
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

    static EntryId add_iptc_keyword(MetaStore* store, BlockId block,
                                    std::string_view text, uint32_t order)
    {
        if (store == nullptr) {
            return kInvalidEntryId;
        }

        Entry entry;
        entry.key = make_iptc_dataset_key(2U, 25U);
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

    static MetadataAuthoringEntry authoring_dc_text(std::string_view path,
                                                    std::string_view text)
    {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = kDcNamespace;
        key.data.xmp_property.property_path = path;
        return MetadataAuthoringEntry {
            key,
            make_value_view_text(text, TextEncoding::Utf8),
            WireType {},
            0U,
        };
    }

    static const Entry* find_dc_entry(const MetaStore& store,
                                      std::string_view path) noexcept
    {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = kDcNamespace;
        key.data.xmp_property.property_path = path;
        const std::span<const EntryId> ids  = store.find_all(key);
        return ids.size() == 1U ? &store.entry(ids[0U]) : nullptr;
    }

    static std::string_view entry_text(const MetaStore& store,
                                       const Entry& entry) noexcept
    {
        if (entry.value.kind != MetaValueKind::Text) {
            return {};
        }
        const std::span<const std::byte> bytes = store.arena().span(
            entry.value.data.span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static std::vector<std::byte> render_portable_xmp(const MetaStore& store)
    {
        XmpPortableOptions options;
        options.include_exif            = false;
        options.include_iptc            = false;
        options.include_existing_xmp    = true;
        options.limits.max_output_bytes = 1024U * 1024U;
        options.limits.max_entries      = 128U;

        const XmpDumpResult measured = dump_xmp_portable(store, {}, options);
        EXPECT_EQ(measured.status, XmpDumpStatus::OutputTruncated);
        EXPECT_GT(measured.needed, 0U);
        EXPECT_LE(measured.needed, 1024U * 1024U);
        if (measured.status != XmpDumpStatus::OutputTruncated
            || measured.needed == 0U || measured.needed > 1024U * 1024U) {
            return {};
        }

        std::vector<std::byte> output(static_cast<size_t>(measured.needed));
        const XmpDumpResult rendered = dump_xmp_portable(store, output,
                                                         options);
        EXPECT_EQ(rendered.status, XmpDumpStatus::Ok);
        EXPECT_EQ(rendered.written, measured.needed);
        if (rendered.status != XmpDumpStatus::Ok
            || rendered.written != measured.needed) {
            return {};
        }
        return output;
    }

    static bool decode_xmp(std::span<const std::byte> bytes, MetaStore* output)
    {
        if (output == nullptr || bytes.empty()
            || bytes.size() > 1024U * 1024U) {
            return false;
        }
        XmpDecodeOptions options;
        options.limits.max_input_bytes       = 1024U * 1024U;
        options.limits.max_properties        = 128U;
        options.limits.max_path_bytes        = 256U;
        options.limits.max_namespace_bytes   = 256U;
        options.limits.max_value_bytes       = 4096U;
        options.limits.max_total_value_bytes = 16U * 1024U;
        options.limits.max_arena_bytes       = 64U * 1024U;
        const XmpDecodeResult result
            = decode_xmp_packet(bytes, *output, EntryFlags::None, options);
        if (result.status != XmpDecodeStatus::Ok) {
            return false;
        }
        output->finalize();
        return true;
    }

    static void expect_dc_text(const MetaStore& store, std::string_view path,
                               std::string_view expected)
    {
        const Entry* entry = find_dc_entry(store, path);
        ASSERT_NE(entry, nullptr) << path;
        EXPECT_EQ(entry_text(store, *entry), expected) << path;
    }

    static void expect_dc_missing(const MetaStore& store, std::string_view path)
    {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = kDcNamespace;
        key.data.xmp_property.property_path = path;
        EXPECT_TRUE(store.find_all(key).empty()) << path;
    }

}  // namespace

TEST(XmpArrayPrecedence, IptcAuthorityExtendsCreatorAcrossOriginBlocks)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for XMP decode readback.";
#endif
    MetaStore source;
    const BlockId xmp_block  = source.add_block(BlockInfo {});
    const BlockId iptc_block = source.add_block(BlockInfo {});
    ASSERT_NE(xmp_block, kInvalidBlockId);
    ASSERT_NE(iptc_block, kInvalidBlockId);
    ASSERT_NE(xmp_block, iptc_block);

    ASSERT_NE(add_dc_text(&source, xmp_block, "creator[1]", "Old creator", 0U,
                          EntryFlags::Dirty),
              kInvalidEntryId);
    ASSERT_NE(add_iptc_byline(&source, iptc_block, "IPTC creator one", 0U),
              kInvalidEntryId);
    ASSERT_NE(add_iptc_byline(&source, iptc_block, "IPTC creator two", 1U),
              kInvalidEntryId);
    source.finalize();

    MetadataReconcileOptions options;
    options.title       = false;
    options.description = false;
    options.keywords    = false;
    options.copyright   = false;
    options.conflict_policy = MetadataReconcileConflictPolicy::PreferSourceOrder;
    options.family_priority = { MetadataReconcileFamily::Iptc,
                                MetadataReconcileFamily::Xmp,
                                MetadataReconcileFamily::Exif };

    MetaStore reconciled;
    const MetadataReconcileResult result
        = reconcile_descriptive_metadata(source, options, &reconciled);
    ASSERT_EQ(result.status, MetadataReconcileStatus::Ok);
    ASSERT_TRUE(reconciled.is_finalized());

    const Entry* creator_one = find_dc_entry(reconciled, "creator[1]");
    const Entry* creator_two = find_dc_entry(reconciled, "creator[2]");
    ASSERT_NE(creator_one, nullptr);
    ASSERT_NE(creator_two, nullptr);
    EXPECT_NE(creator_one->origin.block, creator_two->origin.block);
    EXPECT_EQ(entry_text(reconciled, *creator_one), "IPTC creator one");
    EXPECT_EQ(entry_text(reconciled, *creator_two), "IPTC creator two");

    const std::vector<std::byte> packet = render_portable_xmp(reconciled);
    ASSERT_FALSE(packet.empty());
    MetaStore readback;
    ASSERT_TRUE(decode_xmp(packet, &readback));
    expect_dc_text(readback, "creator[1]", "IPTC creator one");
    expect_dc_text(readback, "creator[2]", "IPTC creator two");
    expect_dc_missing(readback, "creator[3]");
}

TEST(XmpArrayPrecedence, TypedAddKeepsNewInvalidOriginCreatorAndRepeatedValues)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for XMP decode readback.";
#endif
    MetaStore base;
    const BlockId original_block = base.add_block(BlockInfo {});
    ASSERT_NE(original_block, kInvalidBlockId);
    ASSERT_NE(add_dc_text(&base, original_block, "creator[1]",
                          "Repeated creator", 0U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&base, original_block, "subject[1]",
                          "Repeated keyword", 1U),
              kInvalidEntryId);
    base.finalize();

    const MetadataAuthoringEntry creator_two
        = authoring_dc_text("creator[2]", "Repeated creator");
    const MetadataAuthoringEntry subject_two
        = authoring_dc_text("subject[2]", "Repeated keyword");
    std::array<MetadataTypedEditingOperation, 2U> operations {};
    operations[0U].kind  = MetadataEditingOperationKind::Add;
    operations[0U].entry = creator_two;
    operations[1U].kind  = MetadataEditingOperationKind::Add;
    operations[1U].entry = subject_two;

    MetaStore edited;
    const MetadataTypedEditingResult edit
        = edit_metadata_typed(base, operations, &edited);
    ASSERT_TRUE(edit.ok()) << metadata_typed_editing_status_name(edit.status);
    ASSERT_TRUE(edited.is_finalized());

    const Entry* creator_one       = find_dc_entry(edited, "creator[1]");
    const Entry* creator_two_entry = find_dc_entry(edited, "creator[2]");
    const Entry* subject_one       = find_dc_entry(edited, "subject[1]");
    const Entry* subject_two_entry = find_dc_entry(edited, "subject[2]");
    ASSERT_NE(creator_one, nullptr);
    ASSERT_NE(creator_two_entry, nullptr);
    ASSERT_NE(subject_one, nullptr);
    ASSERT_NE(subject_two_entry, nullptr);
    EXPECT_EQ(creator_one->origin.block, original_block);
    EXPECT_EQ(creator_two_entry->origin.block, kInvalidBlockId);
    EXPECT_EQ(subject_one->origin.block, original_block);
    EXPECT_EQ(subject_two_entry->origin.block, kInvalidBlockId);

    const std::vector<std::byte> packet = render_portable_xmp(edited);
    ASSERT_FALSE(packet.empty());
    MetaStore readback;
    ASSERT_TRUE(decode_xmp(packet, &readback));
    expect_dc_text(readback, "creator[1]", "Repeated creator");
    expect_dc_text(readback, "creator[2]", "Repeated creator");
    expect_dc_text(readback, "subject[1]", "Repeated keyword");
    expect_dc_text(readback, "subject[2]", "Repeated keyword");
}

TEST(XmpArrayPrecedence, DirectDumpPreservesMultiOriginCollections)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for XMP decode readback.";
#endif
    MetaStore store;
    const BlockId first_block  = store.add_block(BlockInfo {});
    const BlockId second_block = store.add_block(BlockInfo {});
    ASSERT_NE(first_block, kInvalidBlockId);
    ASSERT_NE(second_block, kInvalidBlockId);
    ASSERT_NE(first_block, second_block);

    ASSERT_NE(add_dc_text(&store, first_block, "creator[3]", "Creator first",
                          0U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, second_block, "creator[5]", "Creator second",
                          0U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, kInvalidBlockId, "creator[7]",
                          "Repeated creator", 1U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, first_block, "creator[9]", "Repeated creator",
                          1U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, first_block, "subject[2]", "Repeated keyword",
                          2U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, second_block, "subject[4]",
                          "Repeated keyword", 1U),
              kInvalidEntryId);
    store.finalize();

    const std::vector<std::byte> packet = render_portable_xmp(store);
    ASSERT_FALSE(packet.empty());
    MetaStore readback;
    ASSERT_TRUE(decode_xmp(packet, &readback));
    expect_dc_text(readback, "creator[1]", "Creator first");
    expect_dc_text(readback, "creator[2]", "Creator second");
    expect_dc_text(readback, "creator[3]", "Repeated creator");
    expect_dc_text(readback, "creator[4]", "Repeated creator");
    expect_dc_text(readback, "subject[1]", "Repeated keyword");
    expect_dc_text(readback, "subject[2]", "Repeated keyword");
    expect_dc_missing(readback, "creator[5]");
    expect_dc_missing(readback, "subject[3]");
}

TEST(XmpArrayPrecedence, SameOriginIndexedShapeSuppressesScalar)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    ASSERT_NE(add_dc_text(&store, block, "creator", "Legacy scalar", 0U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, block, "creator[1]", "Explicit item", 1U),
              kInvalidEntryId);
    store.finalize();

    const std::vector<std::byte> packet = render_portable_xmp(store);
    ASSERT_FALSE(packet.empty());
    const std::string_view xml(reinterpret_cast<const char*>(packet.data()),
                               packet.size());
    EXPECT_EQ(xml.find("Legacy scalar"), std::string_view::npos);
    EXPECT_NE(xml.find("Explicit item"), std::string_view::npos);
}

TEST(XmpArrayPrecedence, DeletedIndexedEntriesDoNotSuppressLiveScalars)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for XMP decode readback.";
#endif
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    ASSERT_NE(add_dc_text(&store, block, "creator", "Live creator", 0U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, block, "creator[1]", "Deleted creator", 1U,
                          EntryFlags::Dirty | EntryFlags::Deleted),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, block, "subject", "Live keyword", 2U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, block, "subject[1]", "Deleted keyword", 3U,
                          EntryFlags::Dirty | EntryFlags::Deleted),
              kInvalidEntryId);
    store.finalize();

    const std::vector<std::byte> packet = render_portable_xmp(store);
    ASSERT_FALSE(packet.empty());
    MetaStore readback;
    ASSERT_TRUE(decode_xmp(packet, &readback));
    expect_dc_text(readback, "creator[1]", "Live creator");
    expect_dc_text(readback, "subject[1]", "Live keyword");
    expect_dc_missing(readback, "creator[2]");
    expect_dc_missing(readback, "subject[2]");
}

TEST(XmpArrayPrecedence, ExistingWinsAndUnrelatedIndexedFieldsStayUnchanged)
{
    MetaStore store;
    const BlockId first_block = store.add_block(BlockInfo {});
    const BlockId later_block = store.add_block(BlockInfo {});
    const BlockId iptc_block  = store.add_block(BlockInfo {});
    ASSERT_NE(first_block, kInvalidBlockId);
    ASSERT_NE(later_block, kInvalidBlockId);
    ASSERT_NE(iptc_block, kInvalidBlockId);

    ASSERT_NE(add_dc_text(&store, first_block, "subject[1]",
                          "Existing XMP keyword", 0U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, first_block, "publisher[1]",
                          "First publisher", 1U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&store, later_block, "publisher[2]",
                          "Second publisher", 0U),
              kInvalidEntryId);
    ASSERT_NE(add_iptc_keyword(&store, iptc_block, "Native IPTC keyword", 0U),
              kInvalidEntryId);
    store.finalize();

    XmpPortableOptions options;
    options.include_exif         = false;
    options.include_iptc         = true;
    options.include_existing_xmp = true;
    options.conflict_policy      = XmpConflictPolicy::ExistingWins;
    const XmpDumpResult measured = dump_xmp_portable(store, {}, options);
    ASSERT_EQ(measured.status, XmpDumpStatus::OutputTruncated);
    ASSERT_GT(measured.needed, 0U);
    std::vector<std::byte> packet(static_cast<size_t>(measured.needed));
    const XmpDumpResult rendered = dump_xmp_portable(store, packet, options);
    ASSERT_EQ(rendered.status, XmpDumpStatus::Ok);

    const std::string_view xml(reinterpret_cast<const char*>(packet.data()),
                               static_cast<size_t>(rendered.written));
    EXPECT_NE(xml.find("Existing XMP keyword"), std::string_view::npos);
    EXPECT_EQ(xml.find("Native IPTC keyword"), std::string_view::npos);
    EXPECT_NE(xml.find("First publisher"), std::string_view::npos);
    EXPECT_NE(xml.find("Second publisher"), std::string_view::npos);
}

TEST(XmpArrayPrecedence, DirtyDeletedCollectionWinsWhileCleanDeletedFallsBack)
{
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
    GTEST_SKIP() << "Expat is required for XMP decode readback.";
#endif
    MetaStore destination;
    const BlockId block = destination.add_block(BlockInfo {});
    ASSERT_NE(add_dc_text(&destination, block, "creator[1]",
                          "Destination author", 0U),
              kInvalidEntryId);
    ASSERT_NE(add_dc_text(&destination, block, "subject[1]",
                          "Destination keyword", 1U),
              kInvalidEntryId);
    destination.finalize();
    const std::vector<std::byte> packet = render_portable_xmp(destination);
    constexpr char signature[]          = "http://ns.adobe.com/xap/1.0/";
    const size_t length = 2U + sizeof(signature) + packet.size();
    ASSERT_LE(length, 65535U);
    std::vector<std::byte> target { std::byte { 0xff },
                                    std::byte { 0xd8 },
                                    std::byte { 0xff },
                                    std::byte { 0xe1 },
                                    static_cast<std::byte>(length >> 8U),
                                    static_cast<std::byte>(length & 255U) };
    const std::span<const std::byte> signature_bytes = std::as_bytes(
        std::span(signature));
    target.insert(target.end(), signature_bytes.begin(), signature_bytes.end());
    target.insert(target.end(), packet.begin(), packet.end());
    constexpr std::array<std::byte, 8U> media {
        std::byte { 0xff }, std::byte { 0xda }, std::byte { 0 },
        std::byte { 2 },    std::byte { 0x21 }, std::byte { 0x22 },
        std::byte { 0xff }, std::byte { 0xd9 }
    };
    target.insert(target.end(), media.begin(), media.end());
    const std::vector<std::byte> before = target;
    for (const bool dirty : { false, true }) {
        SCOPED_TRACE(dirty);
        MetaStore source;
        const BlockId source_block = source.add_block(BlockInfo {});
        const EntryFlags flags = dirty ? EntryFlags::Deleted | EntryFlags::Dirty
                                       : EntryFlags::Deleted;
        ASSERT_NE(add_dc_text(&source, source_block, "creator[1]",
                              "Deleted source author", 0U, flags),
                  kInvalidEntryId);
        ASSERT_NE(add_dc_text(&source, source_block, "subject[1]",
                              "Deleted source keyword", 1U, flags),
                  kInvalidEntryId);
        source.finalize();
        const TransferSourceSnapshot snapshot = build_transfer_source_snapshot(
            source);
        ExecutePreparedTransferSnapshotOptions options;
        options.prepare.target_format      = TransferTargetFormat::Jpeg;
        options.prepare.include_exif_app1  = false;
        options.prepare.include_iptc_app13 = false;
        options.prepare.xmp_project_exif   = false;
        options.prepare.xmp_project_iptc   = false;
        options.xmp_existing_destination_embedded_mode
            = XmpExistingDestinationEmbeddedMode::MergeIfPresent;
        options.xmp_existing_destination_embedded_precedence
            = XmpExistingDestinationEmbeddedPrecedence::SourceWins;
        options.execute.edit_apply = true;
        const ExecutePreparedTransferFileResult result
            = execute_prepared_transfer_snapshot(snapshot, target, options);
        ASSERT_EQ(result.prepared.prepare.status, TransferStatus::Ok);
        ASSERT_EQ(result.execute.edit_apply.status, TransferStatus::Ok);
        const ReadTransferSourceSnapshotBytesResult read
            = read_transfer_source_snapshot_bytes(result.execute.edited_output);
        ASSERT_EQ(read.status, TransferStatus::Ok);
        if (dirty) {
            expect_dc_missing(read.snapshot.store, "creator[1]");
            expect_dc_missing(read.snapshot.store, "subject[1]");
        } else {
            expect_dc_text(read.snapshot.store, "creator[1]",
                           "Destination author");
            expect_dc_text(read.snapshot.store, "subject[1]",
                           "Destination keyword");
        }
        EXPECT_EQ(target, before);
        ASSERT_EQ(source.entries().size(), 2U);
        EXPECT_EQ(source.entry(0U).flags, flags);
        EXPECT_EQ(source.entry(1U).flags, flags);
    }
}

}  // namespace openmeta
