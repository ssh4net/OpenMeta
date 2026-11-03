// SPDX-License-Identifier: Apache-2.0

#include "metadata_container_test_fixture.h"

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_xmp_editing.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_structure.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    static constexpr std::string_view kSchema
        = "urn:openmeta:test:xmp-provider:1";
    static constexpr std::string_view kOtherSchema
        = "urn:openmeta:test:xmp-provider:2";

    class TestDirectory final {
    public:
        TestDirectory() noexcept
        {
            std::error_code error;
            const std::filesystem::path base
                = std::filesystem::temp_directory_path(error);
            if (error)
                return;
            const uint64_t tick = static_cast<uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            for (uint32_t attempt = 0U; attempt < 128U; ++attempt) {
                const std::string name = "openmeta-xmp-provider-"
                                         + std::to_string(tick) + "-"
                                         + std::to_string(attempt);
                const std::filesystem::path candidate = base / name;
                error.clear();
                if (std::filesystem::create_directory(candidate, error)) {
                    path_ = candidate;
                    return;
                }
            }
        }

        ~TestDirectory()
        {
            if (path_.empty())
                return;
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        bool valid() const noexcept { return !path_.empty(); }
        std::filesystem::path file(std::string_view name) const
        {
            return path_ / std::string(name);
        }

    private:
        std::filesystem::path path_;
    };

    static std::string make_packet(std::string_view properties)
    {
        std::string packet;
        packet.reserve(properties.size() + 400U);
        packet.append("<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>");
        packet.append("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">");
        packet.append(
            "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">");
        packet.append("<rdf:Description rdf:about=\"\" xmlns:a=\"");
        packet.append(kSchema);
        packet.append("\" xmlns:b=\"");
        packet.append(kOtherSchema);
        packet.append("\">");
        packet.append(properties);
        packet.append("</rdf:Description></rdf:RDF></x:xmpmeta>");
        packet.append("<?xpacket end=\"w\"?>");
        return packet;
    }

    static void append_ascii(std::vector<std::byte>* output,
                             std::string_view text)
    {
        for (const char value : text) {
            output->push_back(
                static_cast<std::byte>(static_cast<uint8_t>(value)));
        }
    }

    static std::vector<std::byte> make_png_with_xmp(std::string_view packet)
    {
        std::vector<std::byte> png = container_test::make_png_fixture();
        std::vector<std::byte> itxt;
        append_ascii(&itxt, "XML:com.adobe.xmp");
        for (uint32_t i = 0U; i < 5U; ++i)
            itxt.push_back(std::byte { 0U });
        append_ascii(&itxt, packet);

        std::vector<std::byte> chunk;
        container_test::append_png_chunk(&chunk, fourcc('i', 'T', 'X', 't'),
                                         itxt);
        if (png.size() >= 12U) {
            png.insert(png.end() - 12, chunk.begin(), chunk.end());
        }
        return png;
    }

    static bool write_bytes_file(const std::filesystem::path& path,
                                 std::span<const std::byte> bytes)
    {
        const std::string path_text = path.string();
        std::FILE* file             = std::fopen(path_text.c_str(), "wb");
        if (!file) {
            return false;
        }
        const size_t written   = std::fwrite(bytes.data(), 1U, bytes.size(),
                                             file);
        const int close_status = std::fclose(file);
        return written == bytes.size() && close_status == 0;
    }

    static bool write_text_file(const std::filesystem::path& path,
                                std::string_view text)
    {
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(text.data()), text.size());
        return write_bytes_file(path, bytes);
    }

    static PrepareTransferFileOptions
    file_options(const std::filesystem::path& base_path,
                 XmpExistingSidecarPrecedence precedence)
    {
        PrepareTransferFileOptions options;
        options.prepare.target_format        = TransferTargetFormat::Jpeg;
        options.prepare.include_exif_app1    = false;
        options.prepare.include_icc_app2     = false;
        options.prepare.include_iptc_app13   = false;
        options.prepare.xmp_project_exif     = false;
        options.prepare.xmp_project_iptc     = false;
        options.prepare.xmp_include_existing = true;
        options.prepare.xmp_existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;
        options.xmp_existing_sidecar_mode
            = XmpExistingSidecarMode::MergeIfPresent;
        options.xmp_existing_sidecar_precedence = precedence;
        options.xmp_existing_sidecar_base_path  = base_path.string();
        options.preserve_custom_xmp_structures  = true;
        return options;
    }

    static bool decode_bundle_xmp(const PreparedTransferBundle& bundle,
                                  MetaStore* output)
    {
        if (!output)
            return false;
        for (const PreparedTransferBlock& block : bundle.blocks) {
            if (block.kind != TransferBlockKind::Xmp)
                continue;
            XmpDecodeOptions options;
            options.preserve_custom_structures = true;
            const XmpDecodeResult decoded      = decode_xmp_packet(
                std::span<const std::byte>(block.payload.data(),
                                                block.payload.size()),
                *output, EntryFlags::None, options);
            if (decoded.status != XmpDecodeStatus::Ok)
                return false;
            output->finalize();
            return true;
        }
        return false;
    }

    static void expect_text(const MetaStore& store, std::string_view schema,
                            std::string_view path, std::string_view expected)
    {
        const std::span<const EntryId> ids = store.find_all(
            make_xmp_property_key_view(schema, path));
        ASSERT_EQ(ids.size(), 1U) << path;
        const Entry& entry = store.entry(ids.front());
        ASSERT_EQ(entry.value.kind, MetaValueKind::Text) << path;
        const std::span<const std::byte> bytes = store.arena().span(
            entry.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                   bytes.size()),
                  expected)
            << path;
    }

    static void expect_container(const MetaStore& store,
                                 std::string_view schema, std::string_view path,
                                 XmpContainerKind expected)
    {
        const std::span<const EntryId> ids = store.find_all(
            make_xmp_property_key_view(schema, path));
        ASSERT_EQ(ids.size(), 1U) << path;
        EXPECT_EQ(xmp_container_kind(store.entry(ids.front())), expected)
            << path;
    }

    static void expect_missing(const MetaStore& store, std::string_view schema,
                               std::string_view path)
    {
        EXPECT_TRUE(
            store.find_all(make_xmp_property_key_view(schema, path)).empty())
            << path;
    }

    static MetaStore make_clean_deleted_root(std::string_view schema,
                                             std::string_view path,
                                             XmpContainerKind kind)
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        const MetadataAuthoringEntry marker
            = make_xmp_container_entry(schema, path, kind);
        Entry deleted;
        deleted.key = make_xmp_property_key(store.arena(), schema, path);
        deleted.origin.block      = block;
        deleted.origin.wire_type  = marker.wire_type;
        deleted.origin.wire_count = marker.wire_count;
        deleted.flags             = EntryFlags::Deleted;
        (void)store.add_entry(deleted);
        store.finalize();
        return store;
    }

    static MetaStore make_duplicate_active_root(std::string_view schema,
                                                std::string_view path,
                                                XmpContainerKind kind)
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});
        const MetadataAuthoringEntry marker
            = make_xmp_container_entry(schema, path, kind);
        for (uint32_t order = 0U; order < 2U; ++order) {
            Entry entry;
            entry.key = make_xmp_property_key(store.arena(), schema, path);
            entry.origin.block          = block;
            entry.origin.order_in_block = order;
            entry.origin.wire_type      = marker.wire_type;
            entry.origin.wire_count     = marker.wire_count;
            (void)store.add_entry(entry);
        }
        store.finalize();
        return store;
    }

    static bool decode_packet_to_store(std::string_view packet,
                                       MetaStore* output)
    {
        if (!output)
            return false;
        XmpDecodeOptions options;
        options.preserve_custom_structures = true;
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(packet.data()), packet.size());
        if (decode_xmp_packet(bytes, *output, EntryFlags::None, options).status
            != XmpDecodeStatus::Ok) {
            return false;
        }
        output->finalize();
        return true;
    }

    static bool
    make_dirty_deleted_tree_snapshot(std::string_view schema,
                                     std::string_view path,
                                     TransferSourceSnapshot* snapshot)
    {
        if (!snapshot)
            return false;
        const std::string packet = make_packet(
            "<a:DeletedTree rdf:parseType=\"Resource\"><a:Child>source-child</a:Child>"
            "<a:Nested rdf:parseType=\"Resource\"><a:Leaf>source-leaf</a:Leaf></a:Nested>"
            "</a:DeletedTree><a:KeepTree rdf:parseType=\"Resource\"><a:Name>keep-value</a:Name></a:KeepTree>");
        MetaStore decoded_store;
        if (!decode_packet_to_store(packet, &decoded_store))
            return false;

        MetadataXmpEditingOperation remove;
        remove.kind          = MetadataXmpEditingOperationKind::Remove;
        remove.schema_ns     = schema;
        remove.property_path = path;
        const std::array<MetadataXmpEditingOperation, 1U> operations = {
            remove,
        };
        MetaStore deleted_store;
        if (!edit_metadata_xmp(decoded_store, operations, &deleted_store).ok())
            return false;
        snapshot->store = std::move(deleted_store);
        return true;
    }

    static bool packet_store_shape(std::string_view packet, size_t* out_entries,
                                   uint64_t* out_arena_bytes)
    {
        if (!out_entries || !out_arena_bytes)
            return false;
        MetaStore store;
        if (!decode_packet_to_store(packet, &store))
            return false;
        *out_entries     = store.entries().size();
        *out_arena_bytes = store.arena().bytes().size();
        return true;
    }

    static bool
    make_dirty_deleted_scalar_snapshot(std::string_view schema,
                                       std::string_view path,
                                       TransferSourceSnapshot* snapshot)
    {
        if (!snapshot)
            return false;
        const std::string packet = make_packet(
            "<a:DeletedRoot>source-value</a:DeletedRoot>"
            "<a:KeepRoot>keep-value</a:KeepRoot>");
        MetaStore decoded_store;
        XmpDecodeOptions decode_options;
        decode_options.preserve_custom_structures = true;
        const std::span<const std::byte> packet_bytes(
            reinterpret_cast<const std::byte*>(packet.data()), packet.size());
        if (decode_xmp_packet(packet_bytes, decoded_store, EntryFlags::None,
                              decode_options)
                .status
            != XmpDecodeStatus::Ok) {
            return false;
        }
        decoded_store.finalize();

        MetadataTypedEditingOperation remove;
        remove.kind      = MetadataEditingOperationKind::Remove;
        remove.entry.key = make_xmp_property_key_view(schema, path);
        const std::array<MetadataTypedEditingOperation, 1U> operations = {
            remove,
        };
        MetaStore deleted_store;
        const MetadataTypedEditingResult edited
            = edit_metadata_typed(decoded_store, operations, &deleted_store);
        if (!edited.ok())
            return false;
        snapshot->store = std::move(deleted_store);
        return true;
    }

    static ExecutePreparedTransferSnapshotOptions
    snapshot_options(const std::filesystem::path& base_path,
                     XmpExistingSidecarPrecedence precedence)
    {
        ExecutePreparedTransferSnapshotOptions options;
        options.prepare.target_format        = TransferTargetFormat::Jpeg;
        options.prepare.include_exif_app1    = false;
        options.prepare.include_icc_app2     = false;
        options.prepare.include_iptc_app13   = false;
        options.prepare.xmp_project_exif     = false;
        options.prepare.xmp_project_iptc     = false;
        options.prepare.xmp_include_existing = true;
        options.prepare.xmp_existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;
        options.xmp_existing_sidecar_mode
            = XmpExistingSidecarMode::MergeIfPresent;
        options.xmp_existing_sidecar_precedence = precedence;
        options.xmp_existing_sidecar_base_path  = base_path.string();
        options.preserve_custom_xmp_structures  = true;
        return options;
    }

    TEST(XmpProviderPrecedence,
         SidecarWinsWholeRootsAcrossAllKindsAndKeepsOtherUri)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for structured XMP providers.";
#endif
        TestDirectory directory;
        ASSERT_TRUE(directory.valid());
        const std::filesystem::path source_path  = directory.file("source.png");
        const std::filesystem::path sidecar_path = directory.file("source.xmp");
        const std::string source_packet          = make_packet(
            "<a:StructRoot rdf:parseType=\"Resource\"><a:Name>source-struct</a:Name></a:StructRoot>"
                     "<a:BagRoot><rdf:Bag><rdf:li>source-bag</rdf:li></rdf:Bag></a:BagRoot>"
                     "<a:SeqRoot><rdf:Seq><rdf:li>source-seq</rdf:li></rdf:Seq></a:SeqRoot>"
                     "<a:AltRoot><rdf:Alt><rdf:li xml:lang=\"x-default\">source-alt</rdf:li></rdf:Alt></a:AltRoot>"
                     "<b:StructRoot rdf:parseType=\"Resource\"><b:Name>other-uri-source</b:Name></b:StructRoot>");
        const std::string sidecar_packet = make_packet(
            "<a:StructRoot><rdf:Bag><rdf:li>sidecar-struct-one</rdf:li><rdf:li>sidecar-struct-two</rdf:li></rdf:Bag></a:StructRoot>"
            "<a:BagRoot><rdf:Seq><rdf:li>sidecar-bag</rdf:li></rdf:Seq></a:BagRoot>"
            "<a:SeqRoot><rdf:Alt><rdf:li xml:lang=\"x-default\">sidecar-seq-default</rdf:li><rdf:li xml:lang=\"en-US\">sidecar-seq-en</rdf:li></rdf:Alt></a:SeqRoot>"
            "<a:AltRoot rdf:parseType=\"Resource\"><a:Name>sidecar-alt-struct</a:Name></a:AltRoot>");
        const std::vector<std::byte> png = make_png_with_xmp(source_packet);
        ASSERT_TRUE(write_bytes_file(source_path, png));
        ASSERT_TRUE(write_text_file(sidecar_path, sidecar_packet));

        const PrepareTransferFileResult result
            = prepare_metadata_for_target_file(
                source_path.string().c_str(),
                file_options(source_path,
                             XmpExistingSidecarPrecedence::SidecarWins));
        ASSERT_EQ(result.file_status, TransferFileStatus::Ok)
            << result.prepare.message;
        ASSERT_EQ(result.prepare.status, TransferStatus::Ok)
            << result.prepare.message;
        ASSERT_TRUE(result.xmp_existing_sidecar_loaded);

        MetaStore output;
        ASSERT_TRUE(decode_bundle_xmp(result.bundle, &output));
        expect_container(output, kSchema, "StructRoot", XmpContainerKind::Bag);
        expect_text(output, kSchema, "StructRoot[1]", "sidecar-struct-one");
        expect_text(output, kSchema, "StructRoot[2]", "sidecar-struct-two");
        expect_missing(output, kSchema, "StructRoot/Name");
        expect_container(output, kSchema, "BagRoot", XmpContainerKind::Seq);
        expect_text(output, kSchema, "BagRoot[1]", "sidecar-bag");
        expect_container(output, kSchema, "SeqRoot", XmpContainerKind::LangAlt);
        expect_text(output, kSchema, "SeqRoot[@xml:lang=x-default]",
                    "sidecar-seq-default");
        expect_text(output, kSchema, "SeqRoot[@xml:lang=en-US]",
                    "sidecar-seq-en");
        expect_container(output, kSchema, "AltRoot", XmpContainerKind::Struct);
        expect_text(output, kSchema, "AltRoot/Name", "sidecar-alt-struct");
        expect_container(output, kOtherSchema, "StructRoot",
                         XmpContainerKind::Struct);
        expect_text(output, kOtherSchema, "StructRoot/Name",
                    "other-uri-source");
    }

    TEST(XmpProviderPrecedence, EmptyLiveRootClaimsAndCleanDeletedRootDoesNot)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for structured XMP providers.";
#endif
        TestDirectory directory;
        ASSERT_TRUE(directory.valid());
        const std::filesystem::path source_path  = directory.file("empty.png");
        const std::filesystem::path sidecar_path = directory.file("empty.xmp");
        const std::string source_packet          = make_packet(
            "<a:EmptyRoot><rdf:Bag/></a:EmptyRoot>");
        const std::string sidecar_packet = make_packet(
            "<a:EmptyRoot><rdf:Seq><rdf:li>sidecar-item</rdf:li></rdf:Seq></a:EmptyRoot>");
        const std::vector<std::byte> png = make_png_with_xmp(source_packet);
        ASSERT_TRUE(write_bytes_file(source_path, png));
        ASSERT_TRUE(write_text_file(sidecar_path, sidecar_packet));

        const PrepareTransferFileResult file_result
            = prepare_metadata_for_target_file(
                source_path.string().c_str(),
                file_options(source_path,
                             XmpExistingSidecarPrecedence::SourceWins));
        ASSERT_EQ(file_result.file_status, TransferFileStatus::Ok)
            << file_result.prepare.message;
        ASSERT_EQ(file_result.prepare.status, TransferStatus::Ok)
            << file_result.prepare.message;
        MetaStore empty_output;
        ASSERT_TRUE(decode_bundle_xmp(file_result.bundle, &empty_output));
        expect_container(empty_output, kSchema, "EmptyRoot",
                         XmpContainerKind::Bag);
        expect_missing(empty_output, kSchema, "EmptyRoot[1]");

        const std::filesystem::path snapshot_base = directory.file(
            "snapshot-source.jpg");
        const std::filesystem::path snapshot_sidecar = directory.file(
            "snapshot-source.xmp");
        ASSERT_TRUE(write_text_file(snapshot_sidecar, sidecar_packet));
        TransferSourceSnapshot snapshot;
        snapshot.store = make_clean_deleted_root(kSchema, "EmptyRoot",
                                                 XmpContainerKind::Bag);
        const ExecutePreparedTransferFileResult snapshot_result
            = execute_prepared_transfer_snapshot(
                snapshot,
                snapshot_options(snapshot_base,
                                 XmpExistingSidecarPrecedence::SourceWins));
        ASSERT_EQ(snapshot_result.prepared.file_status, TransferFileStatus::Ok)
            << snapshot_result.prepared.prepare.message;
        ASSERT_EQ(snapshot_result.prepared.prepare.status, TransferStatus::Ok)
            << snapshot_result.prepared.prepare.message;
        MetaStore snapshot_output;
        ASSERT_TRUE(decode_bundle_xmp(snapshot_result.prepared.bundle,
                                      &snapshot_output));
        expect_container(snapshot_output, kSchema, "EmptyRoot",
                         XmpContainerKind::Seq);
        expect_text(snapshot_output, kSchema, "EmptyRoot[1]", "sidecar-item");
    }

    TEST(XmpProviderPrecedence,
         RootScalarAndDirtyDeletionTakeWholeProviderPrecedence)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for structured XMP providers.";
#endif
        TestDirectory directory;
        ASSERT_TRUE(directory.valid());
        const std::filesystem::path source_path  = directory.file("scalar.png");
        const std::filesystem::path sidecar_path = directory.file("scalar.xmp");
        const std::string source_packet          = make_packet(
            "<a:ScalarRoot>source-scalar</a:ScalarRoot>");
        const std::string sidecar_packet = make_packet(
            "<a:ScalarRoot rdf:parseType=\"Resource\"><a:Child>sidecar-child</a:Child></a:ScalarRoot>");
        const std::vector<std::byte> png = make_png_with_xmp(source_packet);
        ASSERT_TRUE(write_bytes_file(source_path, png));
        ASSERT_TRUE(write_text_file(sidecar_path, sidecar_packet));

        const PrepareTransferFileResult file_result
            = prepare_metadata_for_target_file(
                source_path.string().c_str(),
                file_options(source_path,
                             XmpExistingSidecarPrecedence::SourceWins));
        ASSERT_EQ(file_result.file_status, TransferFileStatus::Ok)
            << file_result.prepare.message;
        ASSERT_EQ(file_result.prepare.status, TransferStatus::Ok)
            << file_result.prepare.message;
        MetaStore scalar_output;
        ASSERT_TRUE(decode_bundle_xmp(file_result.bundle, &scalar_output));
        expect_text(scalar_output, kSchema, "ScalarRoot", "source-scalar");
        expect_missing(scalar_output, kSchema, "ScalarRoot/Child");

        const std::filesystem::path snapshot_base = directory.file(
            "deleted-source.jpg");
        const std::filesystem::path snapshot_sidecar = directory.file(
            "deleted-source.xmp");
        TransferSourceSnapshot snapshot;
        ASSERT_TRUE(make_dirty_deleted_scalar_snapshot(kSchema, "DeletedRoot",
                                                       &snapshot));
        const std::string deleted_sidecar_packet = make_packet(
            "<a:DeletedRoot rdf:parseType=\"Resource\"><a:Child>sidecar-child</a:Child></a:DeletedRoot>");
        ASSERT_TRUE(write_text_file(snapshot_sidecar, deleted_sidecar_packet));
        const ExecutePreparedTransferFileResult snapshot_result
            = execute_prepared_transfer_snapshot(
                snapshot,
                snapshot_options(snapshot_base,
                                 XmpExistingSidecarPrecedence::SourceWins));
        ASSERT_EQ(snapshot_result.prepared.file_status, TransferFileStatus::Ok)
            << snapshot_result.prepared.prepare.message;
        ASSERT_EQ(snapshot_result.prepared.prepare.status, TransferStatus::Ok)
            << snapshot_result.prepared.prepare.message;
        MetaStore deleted_output;
        ASSERT_TRUE(decode_bundle_xmp(snapshot_result.prepared.bundle,
                                      &deleted_output));
        expect_missing(deleted_output, kSchema, "DeletedRoot");
        expect_missing(deleted_output, kSchema, "DeletedRoot/Child");
        expect_text(deleted_output, kSchema, "KeepRoot", "keep-value");

        const std::filesystem::path tree_snapshot_base = directory.file(
            "deleted-tree-source.jpg");
        const std::filesystem::path tree_snapshot_sidecar = directory.file(
            "deleted-tree-source.xmp");
        TransferSourceSnapshot tree_snapshot;
        ASSERT_TRUE(make_dirty_deleted_tree_snapshot(kSchema, "DeletedTree",
                                                     &tree_snapshot));
        ASSERT_TRUE(write_text_file(
            tree_snapshot_sidecar,
            make_packet("<a:DeletedTree><rdf:Bag><rdf:li>sidecar-child</rdf:li>"
                        "</rdf:Bag></a:DeletedTree>")));
        const ExecutePreparedTransferFileResult tree_result
            = execute_prepared_transfer_snapshot(
                tree_snapshot,
                snapshot_options(tree_snapshot_base,
                                 XmpExistingSidecarPrecedence::SourceWins));
        ASSERT_EQ(tree_result.prepared.file_status, TransferFileStatus::Ok)
            << tree_result.prepared.prepare.message;
        ASSERT_EQ(tree_result.prepared.prepare.status, TransferStatus::Ok)
            << tree_result.prepared.prepare.message;
        MetaStore tree_output;
        ASSERT_TRUE(
            decode_bundle_xmp(tree_result.prepared.bundle, &tree_output));
        expect_missing(tree_output, kSchema, "DeletedTree");
        expect_missing(tree_output, kSchema, "DeletedTree[1]");
        expect_missing(tree_output, kSchema, "DeletedTree/Child");
        expect_missing(tree_output, kSchema, "DeletedTree/Nested/Leaf");
        expect_text(tree_output, kSchema, "KeepTree/Name", "keep-value");

        const std::filesystem::path retained_snapshot_base = directory.file(
            "declared-snapshot-source.jpg");
        const std::filesystem::path retained_snapshot_sidecar = directory.file(
            "declared-snapshot-source.xmp");
        TransferSourceSnapshot retained_snapshot;
        ASSERT_TRUE(decode_packet_to_store(
            make_packet("<a:DeclaredRoot><rdf:Bag><rdf:li>snapshot-item</rdf:li>"
                        "</rdf:Bag></a:DeclaredRoot>"),
            &retained_snapshot.store));
        ASSERT_TRUE(write_text_file(
            retained_snapshot_sidecar,
            make_packet("<a:DeclaredRoot>legacy-scalar</a:DeclaredRoot>")));
        ExecutePreparedTransferSnapshotOptions legacy_read_options
            = snapshot_options(retained_snapshot_base,
                               XmpExistingSidecarPrecedence::SourceWins);
        legacy_read_options.preserve_custom_xmp_structures = false;
        const ExecutePreparedTransferFileResult retained_result
            = execute_prepared_transfer_snapshot(retained_snapshot,
                                                 legacy_read_options);
        ASSERT_EQ(retained_result.prepared.prepare.status, TransferStatus::Ok)
            << retained_result.prepared.prepare.message;
        MetaStore retained_output;
        ASSERT_TRUE(decode_bundle_xmp(retained_result.prepared.bundle,
                                      &retained_output));
        expect_container(retained_output, kSchema, "DeclaredRoot",
                         XmpContainerKind::Bag);
        expect_text(retained_output, kSchema, "DeclaredRoot[1]",
                    "snapshot-item");
    }

    TEST(XmpProviderPrecedence, MalformedLosingSourceProviderIsStillRejected)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for structured XMP providers.";
#endif
        TestDirectory directory;
        ASSERT_TRUE(directory.valid());
        const std::filesystem::path source_path  = directory.file("bad.png");
        const std::filesystem::path sidecar_path = directory.file("bad.xmp");
        const std::string source_packet          = make_packet(
            "<a:Root rdf:parseType=\"Resource\"><a:Name>first</a:Name><a:Name>duplicate</a:Name></a:Root>");
        const std::string sidecar_packet = make_packet(
            "<a:Root rdf:parseType=\"Resource\"><a:Name>sidecar</a:Name></a:Root>");
        const std::vector<std::byte> png = make_png_with_xmp(source_packet);
        ASSERT_TRUE(write_bytes_file(source_path, png));
        ASSERT_TRUE(write_text_file(sidecar_path, sidecar_packet));

        const PrepareTransferFileResult result
            = prepare_metadata_for_target_file(
                source_path.string().c_str(),
                file_options(source_path,
                             XmpExistingSidecarPrecedence::SidecarWins));
        EXPECT_EQ(result.file_status, TransferFileStatus::ReadFailed);
        EXPECT_EQ(result.prepare.status, TransferStatus::Malformed);
        EXPECT_EQ(result.read.xmp.status, XmpDecodeStatus::Malformed);

        const std::filesystem::path snapshot_base = directory.file(
            "bad-snapshot-source.jpg");
        const std::filesystem::path snapshot_sidecar = directory.file(
            "bad-snapshot-source.xmp");
        ASSERT_TRUE(write_text_file(
            snapshot_sidecar,
            make_packet(
                "<a:Root rdf:parseType=\"Resource\"><a:Name>sidecar</a:Name>"
                "</a:Root>")));
        TransferSourceSnapshot malformed_snapshot;
        malformed_snapshot.store
            = make_duplicate_active_root(kSchema, "Root",
                                         XmpContainerKind::Struct);
        const size_t original_entries
            = malformed_snapshot.store.entries().size();
        const ExecutePreparedTransferFileResult malformed_snapshot_result
            = execute_prepared_transfer_snapshot(
                malformed_snapshot,
                snapshot_options(snapshot_base,
                                 XmpExistingSidecarPrecedence::SidecarWins));
        EXPECT_EQ(malformed_snapshot_result.prepared.prepare.status,
                  TransferStatus::InvalidArgument);
        EXPECT_EQ(malformed_snapshot_result.prepared.prepare.code,
                  PrepareTransferCode::XmpPackFailed);
        EXPECT_TRUE(malformed_snapshot_result.prepared.bundle.blocks.empty());
        EXPECT_EQ(malformed_snapshot.store.entries().size(), original_entries);
        EXPECT_EQ(malformed_snapshot.store
                      .find_all(make_xmp_property_key_view(kSchema, "Root"))
                      .size(),
                  2U);

        ExecutePreparedTransferSnapshotOptions ignored_options
            = snapshot_options(snapshot_base,
                               XmpExistingSidecarPrecedence::SidecarWins);
        ignored_options.xmp_existing_sidecar_mode
            = XmpExistingSidecarMode::Ignore;
        ignored_options.prepare.xmp_include_existing = false;
        const ExecutePreparedTransferFileResult ignored_result
            = execute_prepared_transfer_snapshot(malformed_snapshot,
                                                 ignored_options);
        EXPECT_EQ(ignored_result.prepared.prepare.status, TransferStatus::Ok)
            << ignored_result.prepared.prepare.message;
    }

    TEST(XmpProviderPrecedence, CallerStructureBoundsApplyToEachProvider)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for structured XMP providers.";
#endif
        TestDirectory directory;
        ASSERT_TRUE(directory.valid());
        const std::filesystem::path source_path = directory.file("limited.png");
        const std::string source_packet         = make_packet(
            "<a:Root rdf:parseType=\"Resource\"><a:Child>value</a:Child></a:Root>");
        const std::vector<std::byte> png = make_png_with_xmp(source_packet);
        ASSERT_TRUE(write_bytes_file(source_path, png));

        PrepareTransferFileOptions options
            = file_options(source_path,
                           XmpExistingSidecarPrecedence::SourceWins);
        options.prepare.xmp_structures.max_nodes = 1U;
        const PrepareTransferFileResult result
            = prepare_metadata_for_target_file(source_path.string().c_str(),
                                               options);
        EXPECT_EQ(result.file_status, TransferFileStatus::Ok);
        EXPECT_EQ(result.prepare.status, TransferStatus::LimitExceeded);
        EXPECT_EQ(result.read.xmp.status, XmpDecodeStatus::Ok);

        const std::filesystem::path losing_source_path = directory.file(
            "losing-provider.png");
        const std::filesystem::path winning_sidecar_path = directory.file(
            "losing-provider.xmp");
        const std::vector<std::byte> losing_png = make_png_with_xmp(make_packet(
            "<a:Root rdf:parseType=\"Resource\"><a:Child>losing-value</a:Child>"
            "</a:Root>"));
        ASSERT_TRUE(write_bytes_file(losing_source_path, losing_png));
        ASSERT_TRUE(
            write_text_file(winning_sidecar_path,
                            make_packet("<a:Root><rdf:Bag/></a:Root>")));
        PrepareTransferFileOptions losing_options
            = file_options(losing_source_path,
                           XmpExistingSidecarPrecedence::SidecarWins);
        losing_options.prepare.xmp_structures.max_nodes = 1U;
        const PrepareTransferFileResult losing_result
            = prepare_metadata_for_target_file(
                losing_source_path.string().c_str(), losing_options);
        EXPECT_EQ(losing_result.file_status, TransferFileStatus::Ok);
        EXPECT_EQ(losing_result.prepare.status, TransferStatus::LimitExceeded);

        const std::filesystem::path union_source_path = directory.file(
            "union-provider.png");
        const std::filesystem::path union_sidecar_path = directory.file(
            "union-provider.xmp");
        const std::vector<std::byte> union_png = make_png_with_xmp(
            make_packet("<a:SourceEmpty><rdf:Bag/></a:SourceEmpty>"));
        ASSERT_TRUE(write_bytes_file(union_source_path, union_png));
        ASSERT_TRUE(write_text_file(
            union_sidecar_path,
            make_packet("<a:SidecarEmpty><rdf:Seq/></a:SidecarEmpty>")));
        PrepareTransferFileOptions union_options
            = file_options(union_source_path,
                           XmpExistingSidecarPrecedence::SidecarWins);
        union_options.prepare.xmp_structures.max_nodes = 1U;
        const PrepareTransferFileResult union_result
            = prepare_metadata_for_target_file(
                union_source_path.string().c_str(), union_options);
        EXPECT_EQ(union_result.file_status, TransferFileStatus::Ok);
        EXPECT_EQ(union_result.read.xmp.status, XmpDecodeStatus::Ok);
        EXPECT_EQ(union_result.prepare.status, TransferStatus::LimitExceeded);
    }

    TEST(XmpProviderPrecedence, ProviderStoreResourceCapsRemainCumulative)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for structured XMP providers.";
#endif
        TestDirectory directory;
        ASSERT_TRUE(directory.valid());
        const std::filesystem::path source_path = directory.file(
            "resource-source.png");
        const std::filesystem::path sidecar_path = directory.file(
            "resource-source.xmp");
        const std::string source_packet = make_packet(
            "<a:SourceRoot><rdf:Bag><rdf:li>source-item</rdf:li>"
            "</rdf:Bag></a:SourceRoot>");
        const std::string sidecar_packet = make_packet(
            "<a:SidecarRoot><rdf:Seq><rdf:li>sidecar-itm</rdf:li>"
            "</rdf:Seq></a:SidecarRoot>");
        const std::vector<std::byte> png = make_png_with_xmp(source_packet);
        ASSERT_TRUE(write_bytes_file(source_path, png));
        ASSERT_TRUE(write_text_file(sidecar_path, sidecar_packet));

        size_t source_entries        = 0U;
        size_t sidecar_entries       = 0U;
        uint64_t source_arena_bytes  = 0U;
        uint64_t sidecar_arena_bytes = 0U;
        ReadTransferSourceSnapshotOptions source_read_options;
        source_read_options.preserve_custom_xmp_structures = true;
        const ReadTransferSourceSnapshotBytesResult source_shape
            = read_transfer_source_snapshot_bytes(png, source_read_options);
        ASSERT_EQ(source_shape.status, TransferStatus::Ok);
        source_entries     = source_shape.snapshot.store.entries().size();
        source_arena_bytes = source_shape.snapshot.store.arena().bytes().size();
        ASSERT_TRUE(packet_store_shape(sidecar_packet, &sidecar_entries,
                                       &sidecar_arena_bytes));
        const size_t per_provider_entry_cap = (source_entries > sidecar_entries
                                                   ? source_entries
                                                   : sidecar_entries);
        ASSERT_GT(source_entries + sidecar_entries, per_provider_entry_cap);
        ASSERT_LE(per_provider_entry_cap, static_cast<size_t>(UINT32_MAX));
        const uint64_t per_provider_arena_cap = source_arena_bytes
                                                        > sidecar_arena_bytes
                                                    ? source_arena_bytes
                                                    : sidecar_arena_bytes;
        ASSERT_GT(per_provider_arena_cap, 0U);
        ASSERT_GT(source_arena_bytes + sidecar_arena_bytes,
                  per_provider_arena_cap);

        PrepareTransferFileOptions entry_limited_options
            = file_options(source_path,
                           XmpExistingSidecarPrecedence::SourceWins);
        entry_limited_options.policy.xmp_limits.max_properties
            = static_cast<uint32_t>(per_provider_entry_cap);
        const PrepareTransferFileResult entry_limited_result
            = prepare_metadata_for_target_file(source_path.string().c_str(),
                                               entry_limited_options);
        EXPECT_EQ(entry_limited_result.file_status, TransferFileStatus::Ok);
        EXPECT_EQ(entry_limited_result.xmp_existing_sidecar_status,
                  TransferStatus::LimitExceeded);
        EXPECT_FALSE(entry_limited_result.xmp_existing_sidecar_loaded);
        EXPECT_EQ(entry_limited_result.prepare.status, TransferStatus::Ok)
            << entry_limited_result.prepare.message;
        MetaStore entry_limited_output;
        ASSERT_TRUE(decode_bundle_xmp(entry_limited_result.bundle,
                                      &entry_limited_output));
        expect_text(entry_limited_output, kSchema, "SourceRoot[1]",
                    "source-item");
        expect_missing(entry_limited_output, kSchema, "SidecarRoot");

        PrepareTransferFileOptions arena_limited_options
            = file_options(source_path,
                           XmpExistingSidecarPrecedence::SourceWins);
        arena_limited_options.policy.xmp_limits.max_arena_bytes
            = per_provider_arena_cap;
        const PrepareTransferFileResult arena_limited_result
            = prepare_metadata_for_target_file(source_path.string().c_str(),
                                               arena_limited_options);
        EXPECT_EQ(arena_limited_result.file_status, TransferFileStatus::Ok);
        EXPECT_EQ(arena_limited_result.xmp_existing_sidecar_status,
                  TransferStatus::LimitExceeded);
        EXPECT_FALSE(arena_limited_result.xmp_existing_sidecar_loaded);
        EXPECT_EQ(arena_limited_result.prepare.status, TransferStatus::Ok)
            << arena_limited_result.prepare.message;
        MetaStore arena_limited_output;
        ASSERT_TRUE(decode_bundle_xmp(arena_limited_result.bundle,
                                      &arena_limited_output));
        expect_text(arena_limited_output, kSchema, "SourceRoot[1]",
                    "source-item");
        expect_missing(arena_limited_output, kSchema, "SidecarRoot");
    }

}  // namespace
}  // namespace openmeta
