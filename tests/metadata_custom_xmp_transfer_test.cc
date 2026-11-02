// SPDX-License-Identifier: Apache-2.0

#include "custom_xmp_test_fixture.h"
#include "metadata_container_test_fixture.h"

#include "openmeta/container_payload.h"
#include "openmeta/container_scan.h"
#include "openmeta/meta_flags.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_xmp_editing.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"
#include "openmeta/xmp_structure.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    using container_test::JpegFixture;
    using container_test::kOldWebpIcc;
    using container_test::kWebpFree;
    using container_test::kWebpVp8l;
    using container_test::make_jpeg_fixture;
    using container_test::make_png_fixture;
    using container_test::make_tiff_fixture;
    using container_test::make_webp_fixture;
    using container_test::TiffFixtureVariant;
    using custom_xmp_test::kOtherSchema;
    using custom_xmp_test::kOtherSchemaLocalPath;
    using custom_xmp_test::kSchema;
    using custom_xmp_test::kXapSchema;

    static constexpr std::string_view kCameraRawSchema
        = "http://ns.adobe.com/camera-raw-settings/1.0/";
    static constexpr std::string_view kDcSchema
        = "http://purl.org/dc/elements/1.1/";

    static uint32_t read_u32_be(std::span<const std::byte> bytes,
                                size_t offset) noexcept
    {
        return (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset]))
                << 24U)
               | (static_cast<uint32_t>(
                      std::to_integer<uint8_t>(bytes[offset + 1U]))
                  << 16U)
               | (static_cast<uint32_t>(
                      std::to_integer<uint8_t>(bytes[offset + 2U]))
                  << 8U)
               | static_cast<uint32_t>(
                   std::to_integer<uint8_t>(bytes[offset + 3U]));
    }

    static uint32_t read_u32_le(std::span<const std::byte> bytes,
                                size_t offset) noexcept
    {
        return static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset]))
               | (static_cast<uint32_t>(
                      std::to_integer<uint8_t>(bytes[offset + 1U]))
                  << 8U)
               | (static_cast<uint32_t>(
                      std::to_integer<uint8_t>(bytes[offset + 2U]))
                  << 16U)
               | (static_cast<uint32_t>(
                      std::to_integer<uint8_t>(bytes[offset + 3U]))
                  << 24U);
    }

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    static bool find_png_chunk(std::span<const std::byte> png, uint32_t type,
                               std::span<const std::byte>* data) noexcept
    {
        static constexpr std::array<std::byte, 8U> signature = {
            std::byte { 0x89U }, std::byte { 'P' },   std::byte { 'N' },
            std::byte { 'G' },   std::byte { 0x0DU }, std::byte { 0x0AU },
            std::byte { 0x1AU }, std::byte { 0x0AU },
        };
        if (png.size() < signature.size()
            || !std::equal(signature.begin(), signature.end(), png.begin())) {
            return false;
        }
        size_t offset = signature.size();
        while (offset <= png.size() && png.size() - offset >= 12U) {
            const uint32_t length     = read_u32_be(png, offset);
            const uint32_t found_type = read_u32_be(png, offset + 4U);
            if (static_cast<size_t>(length) > png.size() - offset - 12U) {
                return false;
            }
            if (found_type == type) {
                if (data != nullptr) {
                    *data = png.subspan(offset + 8U, length);
                }
                return true;
            }
            offset += static_cast<size_t>(length) + 12U;
        }
        return false;
    }
#endif

    static bool find_webp_chunk(std::span<const std::byte> webp, uint32_t type,
                                std::span<const std::byte>* data) noexcept
    {
        static constexpr std::array<std::byte, 12U> signature = {
            std::byte { 'R' }, std::byte { 'I' }, std::byte { 'F' },
            std::byte { 'F' }, std::byte { 0U },  std::byte { 0U },
            std::byte { 0U },  std::byte { 0U },  std::byte { 'W' },
            std::byte { 'E' }, std::byte { 'B' }, std::byte { 'P' },
        };
        if (webp.size() < signature.size() || webp[0U] != signature[0U]
            || webp[1U] != signature[1U] || webp[2U] != signature[2U]
            || webp[3U] != signature[3U] || webp[8U] != signature[8U]
            || webp[9U] != signature[9U] || webp[10U] != signature[10U]
            || webp[11U] != signature[11U]) {
            return false;
        }
        size_t offset = signature.size();
        while (offset <= webp.size() && webp.size() - offset >= 8U) {
            const uint32_t found_type = read_u32_be(webp, offset);
            const uint32_t length     = read_u32_le(webp, offset + 4U);
            if (static_cast<size_t>(length) > webp.size() - offset - 8U) {
                return false;
            }
            if (found_type == type) {
                if (data != nullptr) {
                    *data = webp.subspan(offset + 8U, length);
                }
                return true;
            }
            const size_t padded_length = static_cast<size_t>(length)
                                         + (length & 1U);
            if (padded_length > webp.size() - offset - 8U) {
                return false;
            }
            offset += 8U + padded_length;
        }
        return false;
    }

    struct XmpContainerReadback final {
        ScanResult scan;
        PayloadResult payload;
        uint32_t xmp_block_count = 0U;
        std::vector<std::byte> packet;
    };

    static XmpContainerReadback
    extract_single_xmp_packet(std::span<const std::byte> container)
    {
        XmpContainerReadback result;
        std::array<ContainerBlockRef, 128U> scan_blocks {};
        result.scan = scan_auto(container, scan_blocks);
        if (result.scan.status != ScanStatus::Ok
            || result.scan.written > scan_blocks.size()) {
            return result;
        }

        uint32_t xmp_index = UINT32_MAX;
        for (uint32_t index = 0U; index < result.scan.written; ++index) {
            if (scan_blocks[index].kind != ContainerBlockKind::Xmp) {
                continue;
            }
            ++result.xmp_block_count;
            xmp_index = index;
        }
        if (result.xmp_block_count != 1U) {
            return result;
        }

        std::array<std::byte, 32768U> payload_bytes {};
        std::array<uint32_t, 128U> scratch_indices {};
        const std::span<const ContainerBlockRef> blocks(
            scan_blocks.data(), static_cast<size_t>(result.scan.written));
        result.payload = extract_payload(container, blocks, xmp_index,
                                         payload_bytes, scratch_indices,
                                         PayloadOptions {});
        if (result.payload.status == PayloadStatus::Ok
            && result.payload.written <= payload_bytes.size()) {
            result.packet.assign(payload_bytes.begin(),
                                 payload_bytes.begin()
                                     + static_cast<size_t>(
                                         result.payload.written));
        }
        return result;
    }

    static const PreparedTransferBlock*
    find_xmp_transfer_block(const PreparedTransferBundle& bundle) noexcept
    {
        for (const PreparedTransferBlock& block : bundle.blocks) {
            if (block.kind == TransferBlockKind::Xmp) {
                return &block;
            }
        }
        return nullptr;
    }

    static const PreparedTransferBlock*
    find_exif_transfer_block(const PreparedTransferBundle& bundle) noexcept
    {
        for (const PreparedTransferBlock& block : bundle.blocks) {
            if (block.kind == TransferBlockKind::Exif) {
                return &block;
            }
        }
        return nullptr;
    }

    static void expect_text(const MetaStore& store, std::string_view schema,
                            std::string_view path, std::string_view expected)
    {
        const std::span<const EntryId> ids = store.find_all(
            make_xmp_property_key_view(schema, path));
        ASSERT_EQ(ids.size(), 1U) << path;
        const Entry& entry = store.entry(ids.front());
        ASSERT_EQ(entry.value.kind, MetaValueKind::Text) << path;
        const std::span<const std::byte> raw = store.arena().span(
            entry.value.data.span);
        ASSERT_FALSE(raw.empty()) << path;
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size()),
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

    static void expect_custom_content(const MetaStore& store, bool edited)
    {
        expect_container(store, kSchema, "Record", XmpContainerKind::Struct);
        expect_text(store, kSchema, "Record/Title",
                    edited ? "new title" : "old");

        expect_container(store, kSchema, "Record/Values",
                         XmpContainerKind::Bag);
        expect_text(store, kSchema, "Record/Values[1]",
                    edited ? "one-updated" : "one");
        expect_text(store, kSchema, "Record/Values[2]",
                    edited ? "three-updated" : "three");
        expect_text(store, kSchema, "Record/Values[3]",
                    edited ? "ten-updated" : "ten");
        EXPECT_TRUE(
            store
                .find_all(
                    make_xmp_property_key_view(kSchema, "Record/Values[10]"))
                .empty());

        expect_container(store, kSchema, "Record/Steps", XmpContainerKind::Seq);
        expect_text(store, kSchema, "Record/Steps[1]", "1");
        expect_text(store, kSchema, "Record/Steps[2]", "2");

        expect_container(store, kSchema, "Record/Labels",
                         XmpContainerKind::LangAlt);
        expect_text(store, kSchema, "Record/Labels[@xml:lang=x-default]",
                    "Default label");
        expect_text(store, kSchema, "Record/Labels[@xml:lang=en-US]",
                    "English label");

        expect_container(store, kSchema, "Record/Frames",
                         XmpContainerKind::Seq);
        if (edited) {
            expect_container(store, kSchema, "Record/Frames[1]",
                             XmpContainerKind::Struct);
            expect_text(store, kSchema, "Record/Frames[1]/Name", "ten");
            EXPECT_TRUE(store
                            .find_all(make_xmp_property_key_view(
                                kSchema, "Record/Frames[2]/Name"))
                            .empty());
        } else {
            expect_container(store, kSchema, "Record/Frames[1]",
                             XmpContainerKind::Struct);
            expect_container(store, kSchema, "Record/Frames[2]",
                             XmpContainerKind::Struct);
            expect_text(store, kSchema, "Record/Frames[1]/Name", "one");
            expect_text(store, kSchema, "Record/Frames[2]/Name", "ten");
        }
        EXPECT_TRUE(
            store
                .find_all(
                    make_xmp_property_key_view(kSchema, "Record/Frames[10]"))
                .empty());

        expect_container(store, kSchema, "Record/EmptyStruct",
                         XmpContainerKind::Struct);
        expect_container(store, kSchema, "Record/EmptyBag",
                         XmpContainerKind::Bag);

        expect_container(store, kSchema, "Record/Matrix",
                         XmpContainerKind::Seq);
        expect_container(store, kSchema, "Record/Matrix[1]",
                         XmpContainerKind::Seq);
        expect_container(store, kSchema, "Record/Matrix[2]",
                         XmpContainerKind::Seq);
        expect_text(store, kSchema, "Record/Matrix[1][1]", "m11");
        expect_text(store, kSchema, "Record/Matrix[1][2]", "m12");
        expect_text(store, kSchema, "Record/Matrix[2][1]", "m31");
        EXPECT_TRUE(
            store
                .find_all(
                    make_xmp_property_key_view(kSchema, "Record/Matrix[3]"))
                .empty());

        expect_text(store, kSchema, "Record/Local", "same-local-name");
        expect_text(store, kSchema, kOtherSchemaLocalPath,
                    "other-child-namespace");
        expect_text(store, kSchema, "Record/BlackLevel", "0.125");
        expect_container(store, kOtherSchema, "Record",
                         XmpContainerKind::Struct);
        expect_text(store, kOtherSchema, "Record/Local",
                    "other-root-namespace");
        expect_text(store, kXapSchema, "CreatorTool", "old tool");
        if (edited) {
            expect_text(store, kDcSchema, "title[@xml:lang=x-default]",
                        "Added title");
        } else {
            EXPECT_TRUE(store
                            .find_all(make_xmp_property_key_view(
                                kDcSchema, "title[@xml:lang=x-default]"))
                            .empty());
        }

        if (edited) {
            expect_container(store, kSchema, "Record/Notes",
                             XmpContainerKind::Bag);
            expect_text(store, kSchema, "Record/Notes[1]", "note-one");
            expect_text(store, kSchema, "Record/Notes[2]", "note-three");
            EXPECT_TRUE(
                store
                    .find_all(
                        make_xmp_property_key_view(kSchema, "Record/Notes[3]"))
                    .empty());
        } else {
            EXPECT_TRUE(
                store
                    .find_all(
                        make_xmp_property_key_view(kSchema, "Record/Notes"))
                    .empty());
        }
    }

    static void expect_portable_packet(std::span<const std::byte> packet,
                                       bool edited)
    {
        XmpDecodeOptions decode_options;
        decode_options.preserve_custom_structures = true;
        MetaStore decoded;
        const XmpDecodeResult result = decode_xmp_packet(packet, decoded,
                                                         EntryFlags::None,
                                                         decode_options);
        ASSERT_EQ(result.status, XmpDecodeStatus::Ok);
        decoded.finalize();
        expect_custom_content(decoded, edited);
    }

    static void
    expect_embedded_packet(std::span<const std::byte> container,
                           std::span<const std::byte> expected_packet,
                           bool edited)
    {
        const XmpContainerReadback extracted = extract_single_xmp_packet(
            container);
        ASSERT_EQ(extracted.scan.status, ScanStatus::Ok);
        ASSERT_EQ(extracted.xmp_block_count, 1U);
        ASSERT_EQ(extracted.payload.status, PayloadStatus::Ok);
        ASSERT_FALSE(extracted.packet.empty());
        ASSERT_EQ(extracted.packet.size(), expected_packet.size());
        EXPECT_TRUE(std::equal(extracted.packet.begin(), extracted.packet.end(),
                               expected_packet.begin()));
        expect_portable_packet(extracted.packet, edited);
    }

    static MetadataAuthoringEntry text_entry(std::string_view schema,
                                             std::string_view path,
                                             std::string_view text)
    {
        MetadataAuthoringEntry entry;
        entry.key   = make_xmp_property_key_view(schema, path);
        entry.value = make_value_view_text(text, TextEncoding::Utf8);
        return entry;
    }

    static MetaStore make_invalid_custom_store()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});

        const MetadataAuthoringEntry declaration
            = make_xmp_container_entry(kSchema, "Record/Orphan",
                                       XmpContainerKind::Struct);
        Entry marker;
        marker.key = make_xmp_property_key(
            store.arena(), declaration.key.data.xmp_property.schema_ns,
            declaration.key.data.xmp_property.property_path);
        marker.value.kind          = declaration.value.kind;
        marker.value.elem_type     = declaration.value.elem_type;
        marker.value.text_encoding = declaration.value.text_encoding;
        marker.value.count         = declaration.value.count;
        marker.origin.block        = block;
        marker.origin.wire_type    = declaration.wire_type;
        marker.origin.wire_count   = declaration.wire_count;
        (void)store.add_entry(marker);

        Entry orphan;
        orphan.key          = make_xmp_property_key(store.arena(), kSchema,
                                                    "Record/Orphan/Title");
        orphan.value        = make_text(store.arena(), "unselected",
                                        TextEncoding::Utf8);
        orphan.origin.block = block;
        orphan.origin.order_in_block = 1U;
        (void)store.add_entry(orphan);

        Entry camera_make;
        camera_make.key   = make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
        camera_make.value = make_text(store.arena(), "Fixture Camera",
                                      TextEncoding::Ascii);
        camera_make.origin.block          = block;
        camera_make.origin.order_in_block = 2U;
        (void)store.add_entry(camera_make);
        store.finalize();
        return store;
    }

    static void disable_competing_metadata(PrepareTransferRequest* request)
    {
        request->include_exif_app1    = false;
        request->include_icc_app2     = false;
        request->include_iptc_app13   = false;
        request->xmp_project_exif     = false;
        request->xmp_project_iptc     = false;
        request->xmp_include_existing = true;
        request->xmp_existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;
    }

    TEST(MetadataCustomXmpTransfer,
         CreateEditPrepareApplyAndReadBackAllImageCarriers)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for structured decode readback.";
#endif
        std::vector<MetadataAuthoringEntry> authoring_entries
            = custom_xmp_test::initial_entries();
        ASSERT_LT(authoring_entries.size(), 64U);
        MetaStore original;
        const MetadataAuthoringResult creation
            = create_metadata_store(authoring_entries, &original);
        ASSERT_TRUE(creation.ok())
            << metadata_authoring_status_name(creation.status);

        const std::array<MetadataAuthoringEntry, 1U> title_replacement = {
            text_entry(kSchema, "Record/Title", "new title"),
        };
        const std::array<MetadataAuthoringEntry, 4U> values_replacement = {
            make_xmp_container_entry(kSchema, "Record/Values",
                                     XmpContainerKind::Bag),
            text_entry(kSchema, "Record/Values[1]", "one-updated"),
            text_entry(kSchema, "Record/Values[3]", "three-updated"),
            text_entry(kSchema, "Record/Values[10]", "ten-updated"),
        };
        const std::array<MetadataAuthoringEntry, 3U> notes = {
            make_xmp_container_entry(kSchema, "Record/Notes",
                                     XmpContainerKind::Bag),
            text_entry(kSchema, "Record/Notes[1]", "note-one"),
            text_entry(kSchema, "Record/Notes[3]", "note-three"),
        };
        const std::array<MetadataAuthoringEntry, 1U> dc_title = {
            text_entry(kDcSchema, "title[@xml:lang=x-default]", "Added title"),
        };
        const std::array<MetadataXmpEditingOperation, 5U> operations = { {
            { MetadataXmpEditingOperationKind::Replace, kSchema, "Record/Title",
              title_replacement },
            { MetadataXmpEditingOperationKind::Replace, kSchema,
              "Record/Values", values_replacement },
            { MetadataXmpEditingOperationKind::Add, kSchema, "Record/Notes",
              notes },
            { MetadataXmpEditingOperationKind::Remove,
              kSchema,
              "Record/Frames[1]",
              {} },
            { MetadataXmpEditingOperationKind::Add, kDcSchema, "title",
              dc_title },
        } };
        MetaStore edited;
        const MetadataXmpEditingResult editing
            = edit_metadata_xmp(original, operations, &edited);
        ASSERT_TRUE(editing.ok())
            << metadata_xmp_editing_status_name(editing.status)
            << " failed_operation=" << editing.failed_operation_index;

        expect_text(original, kSchema, "Record/Title", "old");
        expect_text(original, kSchema, "Record/Frames[1]/Name", "one");
        expect_text(original, kXapSchema, "CreatorTool", "old tool");
        EXPECT_TRUE(
            original
                .find_all(make_xmp_property_key_view(kSchema, "Record/Notes"))
                .empty());
        expect_text(edited, kSchema, "Record/Title", "new title");
        expect_text(edited, kSchema, "Record/Frames[10]/Name", "ten");
        EXPECT_TRUE(
            edited
                .find_all(make_xmp_property_key_view(kSchema,
                                                     "Record/Frames[1]/Name"))
                .empty());
        expect_text(edited, kXapSchema, "CreatorTool", "old tool");
        expect_text(edited, kDcSchema, "title[@xml:lang=x-default]",
                    "Added title");

        XmpPortableOptions portable_options;
        portable_options.include_exif         = false;
        portable_options.include_iptc         = false;
        portable_options.include_existing_xmp = true;
        portable_options.existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;
        std::vector<std::byte> portable_packet(32768U);
        const XmpDumpResult dumped = dump_xmp_portable(edited, portable_packet,
                                                       portable_options);
        ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
        ASSERT_LE(dumped.written, portable_packet.size());
        portable_packet.resize(static_cast<size_t>(dumped.written));
        expect_portable_packet(portable_packet, true);

        ExecutePreparedTransferOptions execute_options;
        execute_options.edit_requested = true;
        execute_options.edit_apply     = true;

        PrepareTransferRequest jpeg_request;
        jpeg_request.target_format = TransferTargetFormat::Jpeg;
        disable_competing_metadata(&jpeg_request);
        PreparedTransferBundle jpeg_bundle;
        const PrepareTransferResult jpeg_prepared
            = prepare_metadata_for_target(edited, jpeg_request, &jpeg_bundle);
        ASSERT_EQ(jpeg_prepared.status, TransferStatus::Ok);
        ASSERT_FALSE(jpeg_bundle.generated_xmp_sidecar.empty());
        EXPECT_EQ(jpeg_bundle.generated_xmp_sidecar, portable_packet);
        const PreparedTransferBlock* jpeg_xmp = find_xmp_transfer_block(
            jpeg_bundle);
        ASSERT_NE(jpeg_xmp, nullptr);
        EXPECT_EQ(jpeg_xmp->route, "jpeg:app1-xmp");

        const JpegFixture jpeg_input               = make_jpeg_fixture();
        const std::vector<std::byte> jpeg_original = jpeg_input.bytes;
        const ExecutePreparedTransferResult jpeg_applied
            = execute_prepared_transfer(&jpeg_bundle, jpeg_input.bytes,
                                        execute_options);
        ASSERT_EQ(jpeg_applied.compile.status, TransferStatus::Ok);
        ASSERT_EQ(jpeg_applied.emit.status, TransferStatus::Ok);
        ASSERT_EQ(jpeg_applied.edit_plan_status, TransferStatus::Ok);
        ASSERT_EQ(jpeg_applied.edit_apply.status, TransferStatus::Ok);
        EXPECT_EQ(jpeg_input.bytes, jpeg_original);
        ASSERT_GE(jpeg_input.scan_offset, 2U);
        ASSERT_LE(jpeg_input.scan_offset, jpeg_input.bytes.size());
        const std::span<const std::byte> jpeg_comment(
            jpeg_input.bytes.data() + 2U, jpeg_input.scan_offset - 2U);
        const auto comment = std::search(jpeg_applied.edited_output.begin(),
                                         jpeg_applied.edited_output.end(),
                                         jpeg_comment.begin(),
                                         jpeg_comment.end());
        EXPECT_NE(comment, jpeg_applied.edited_output.end());
        const size_t jpeg_scan_size = jpeg_input.bytes.size()
                                      - jpeg_input.scan_offset;
        ASSERT_GE(jpeg_applied.edited_output.size(), jpeg_scan_size);
        EXPECT_TRUE(
            std::equal(jpeg_input.bytes.begin()
                           + static_cast<std::ptrdiff_t>(jpeg_input.scan_offset),
                       jpeg_input.bytes.end(),
                       jpeg_applied.edited_output.end()
                           - static_cast<std::ptrdiff_t>(jpeg_scan_size)));
        expect_embedded_packet(jpeg_applied.edited_output, portable_packet,
                               true);

        PrepareTransferRequest tiff_request = jpeg_request;
        tiff_request.target_format          = TransferTargetFormat::Tiff;
        PreparedTransferBundle tiff_bundle;
        const PrepareTransferResult tiff_prepared
            = prepare_metadata_for_target(edited, tiff_request, &tiff_bundle);
        ASSERT_EQ(tiff_prepared.status, TransferStatus::Ok);
        ASSERT_EQ(tiff_bundle.generated_xmp_sidecar, portable_packet);
        const PreparedTransferBlock* tiff_xmp = find_xmp_transfer_block(
            tiff_bundle);
        ASSERT_NE(tiff_xmp, nullptr);
        EXPECT_EQ(tiff_xmp->route, "tiff:tag-700-xmp");

        static constexpr TiffFixtureVariant tiff_variant { true, true, false };
        const std::vector<std::byte> tiff_input = make_tiff_fixture(
            tiff_variant);
        const std::vector<std::byte> tiff_original = tiff_input;
        const ExecutePreparedTransferResult tiff_applied
            = execute_prepared_transfer(&tiff_bundle, tiff_input,
                                        execute_options);
        ASSERT_EQ(tiff_applied.compile.status, TransferStatus::Ok);
        ASSERT_EQ(tiff_applied.emit.status, TransferStatus::Ok);
        ASSERT_EQ(tiff_applied.edit_plan_status, TransferStatus::Ok);
        ASSERT_EQ(tiff_applied.edit_apply.status, TransferStatus::Ok);
        EXPECT_EQ(tiff_input, tiff_original);

        static constexpr std::array<uint16_t, 2U> dimension_tags = {
            0x0100U,
            0x0101U,
        };
        for (const uint16_t tag : dimension_tags) {
            container_test::TiffEntrySpan source_field;
            container_test::TiffEntrySpan output_field;
            ASSERT_TRUE(
                container_test::find_big_endian_bigtiff_entry(tiff_input, tag,
                                                              &source_field));
            ASSERT_TRUE(container_test::find_big_endian_bigtiff_entry(
                tiff_applied.edited_output, tag, &output_field));
            ASSERT_EQ(source_field.type, 4U);
            ASSERT_EQ(output_field.type, source_field.type);
            ASSERT_EQ(source_field.count, 1U);
            ASSERT_EQ(output_field.count, source_field.count);
            ASSERT_EQ(output_field.value.size(), source_field.value.size());
            EXPECT_TRUE(std::equal(source_field.value.begin(),
                                   source_field.value.end(),
                                   output_field.value.begin()));
            uint64_t source_dimension = 0U;
            uint64_t output_dimension = 0U;
            ASSERT_TRUE(container_test::decode_tiff_scalar(source_field,
                                                           &source_dimension));
            ASSERT_TRUE(container_test::decode_tiff_scalar(output_field,
                                                           &output_dimension));
            EXPECT_EQ(source_dimension, 2U);
            EXPECT_EQ(output_dimension, source_dimension);
        }

        container_test::TiffEntrySpan source_strip_offset_entry;
        container_test::TiffEntrySpan output_strip_offset_entry;
        container_test::TiffEntrySpan source_strip_bytes_entry;
        container_test::TiffEntrySpan output_strip_bytes_entry;
        ASSERT_TRUE(container_test::find_big_endian_bigtiff_entry(
            tiff_input, 0x0111U, &source_strip_offset_entry));
        ASSERT_TRUE(container_test::find_big_endian_bigtiff_entry(
            tiff_applied.edited_output, 0x0111U, &output_strip_offset_entry));
        ASSERT_TRUE(container_test::find_big_endian_bigtiff_entry(
            tiff_input, 0x0117U, &source_strip_bytes_entry));
        ASSERT_TRUE(container_test::find_big_endian_bigtiff_entry(
            tiff_applied.edited_output, 0x0117U, &output_strip_bytes_entry));
        ASSERT_EQ(source_strip_offset_entry.type, 4U);
        ASSERT_EQ(output_strip_offset_entry.type, 4U);
        ASSERT_EQ(source_strip_offset_entry.count, 1U);
        ASSERT_EQ(output_strip_offset_entry.count, 1U);
        ASSERT_EQ(source_strip_bytes_entry.type, 4U);
        ASSERT_EQ(output_strip_bytes_entry.type, 4U);
        ASSERT_EQ(source_strip_bytes_entry.count, 1U);
        ASSERT_EQ(output_strip_bytes_entry.count, 1U);
        uint64_t source_strip_offset = 0U;
        uint64_t output_strip_offset = 0U;
        uint64_t source_strip_size   = 0U;
        uint64_t output_strip_size   = 0U;
        ASSERT_TRUE(
            container_test::decode_tiff_scalar(source_strip_offset_entry,
                                               &source_strip_offset));
        ASSERT_TRUE(
            container_test::decode_tiff_scalar(output_strip_offset_entry,
                                               &output_strip_offset));
        ASSERT_TRUE(container_test::decode_tiff_scalar(source_strip_bytes_entry,
                                                       &source_strip_size));
        ASSERT_TRUE(container_test::decode_tiff_scalar(output_strip_bytes_entry,
                                                       &output_strip_size));
        ASSERT_EQ(source_strip_size, 4U);
        ASSERT_EQ(output_strip_size, source_strip_size);
        ASSERT_LE(source_strip_offset,
                  static_cast<uint64_t>(tiff_input.size()));
        ASSERT_LE(output_strip_offset,
                  static_cast<uint64_t>(tiff_applied.edited_output.size()));
        const size_t source_strip_offset_size = static_cast<size_t>(
            source_strip_offset);
        const size_t output_strip_offset_size = static_cast<size_t>(
            output_strip_offset);
        ASSERT_LE(source_strip_size,
                  static_cast<uint64_t>(tiff_input.size()
                                        - source_strip_offset_size));
        ASSERT_LE(output_strip_size,
                  static_cast<uint64_t>(tiff_applied.edited_output.size()
                                        - output_strip_offset_size));
        const std::span<const std::byte> source_strip
            = std::span<const std::byte>(tiff_input)
                  .subspan(source_strip_offset_size,
                           static_cast<size_t>(source_strip_size));
        const std::span<const std::byte> output_strip
            = std::span<const std::byte>(tiff_applied.edited_output)
                  .subspan(output_strip_offset_size,
                           static_cast<size_t>(output_strip_size));
        ASSERT_EQ(output_strip.size(), source_strip.size());
        EXPECT_TRUE(std::equal(source_strip.begin(), source_strip.end(),
                               output_strip.begin()));
        expect_embedded_packet(tiff_applied.edited_output, portable_packet,
                               true);

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
        PrepareTransferRequest png_request = jpeg_request;
        png_request.target_format          = TransferTargetFormat::Png;
        PreparedTransferBundle png_bundle;
        const PrepareTransferResult png_prepared
            = prepare_metadata_for_target(edited, png_request, &png_bundle);
        ASSERT_EQ(png_prepared.status, TransferStatus::Ok);
        ASSERT_EQ(png_bundle.generated_xmp_sidecar, portable_packet);
        const PreparedTransferBlock* png_xmp = find_xmp_transfer_block(
            png_bundle);
        ASSERT_NE(png_xmp, nullptr);
        EXPECT_EQ(png_xmp->route, "png:chunk-xmp");

        const std::vector<std::byte> png_input    = make_png_fixture();
        const std::vector<std::byte> png_original = png_input;
        std::span<const std::byte> input_ihdr;
        std::span<const std::byte> input_idat;
        std::span<const std::byte> input_text;
        ASSERT_TRUE(
            find_png_chunk(png_input, fourcc('I', 'H', 'D', 'R'), &input_ihdr));
        ASSERT_TRUE(
            find_png_chunk(png_input, fourcc('I', 'D', 'A', 'T'), &input_idat));
        ASSERT_TRUE(
            find_png_chunk(png_input, fourcc('t', 'E', 'X', 't'), &input_text));
        const ExecutePreparedTransferResult png_applied
            = execute_prepared_transfer(&png_bundle, png_input,
                                        execute_options);
        ASSERT_EQ(png_applied.compile.status, TransferStatus::Ok);
        ASSERT_EQ(png_applied.emit.status, TransferStatus::Ok);
        ASSERT_EQ(png_applied.edit_plan_status, TransferStatus::Ok);
        ASSERT_EQ(png_applied.edit_apply.status, TransferStatus::Ok);
        EXPECT_EQ(png_input, png_original);
        std::span<const std::byte> output_ihdr;
        std::span<const std::byte> output_idat;
        std::span<const std::byte> output_text;
        ASSERT_TRUE(find_png_chunk(png_applied.edited_output,
                                   fourcc('I', 'H', 'D', 'R'), &output_ihdr));
        ASSERT_TRUE(find_png_chunk(png_applied.edited_output,
                                   fourcc('I', 'D', 'A', 'T'), &output_idat));
        ASSERT_TRUE(find_png_chunk(png_applied.edited_output,
                                   fourcc('t', 'E', 'X', 't'), &output_text));
        ASSERT_EQ(output_ihdr.size(), input_ihdr.size());
        ASSERT_EQ(output_idat.size(), input_idat.size());
        ASSERT_EQ(output_text.size(), input_text.size());
        EXPECT_TRUE(std::equal(input_ihdr.begin(), input_ihdr.end(),
                               output_ihdr.begin()));
        EXPECT_TRUE(std::equal(input_idat.begin(), input_idat.end(),
                               output_idat.begin()));
        EXPECT_TRUE(std::equal(input_text.begin(), input_text.end(),
                               output_text.begin()));
        expect_embedded_packet(png_applied.edited_output, portable_packet,
                               true);
#endif

        PrepareTransferRequest webp_request = jpeg_request;
        webp_request.target_format          = TransferTargetFormat::Webp;
        PreparedTransferBundle webp_bundle;
        const PrepareTransferResult webp_prepared
            = prepare_metadata_for_target(edited, webp_request, &webp_bundle);
        ASSERT_EQ(webp_prepared.status, TransferStatus::Ok);
        ASSERT_EQ(webp_bundle.generated_xmp_sidecar, portable_packet);
        const PreparedTransferBlock* webp_xmp = find_xmp_transfer_block(
            webp_bundle);
        ASSERT_NE(webp_xmp, nullptr);
        EXPECT_EQ(webp_xmp->route, "webp:chunk-xmp");

        const std::vector<std::byte> webp_input    = make_webp_fixture();
        const std::vector<std::byte> webp_original = webp_input;
        const ExecutePreparedTransferResult webp_applied
            = execute_prepared_transfer(&webp_bundle, webp_input,
                                        execute_options);
        ASSERT_EQ(webp_applied.compile.status, TransferStatus::Ok);
        ASSERT_EQ(webp_applied.emit.status, TransferStatus::Ok);
        ASSERT_EQ(webp_applied.edit_plan_status, TransferStatus::Ok);
        ASSERT_EQ(webp_applied.edit_apply.status, TransferStatus::Ok);
        EXPECT_EQ(webp_input, webp_original);
        std::span<const std::byte> output_vp8x;
        std::span<const std::byte> output_vp8l;
        std::span<const std::byte> output_free;
        std::span<const std::byte> output_iccp;
        ASSERT_TRUE(find_webp_chunk(webp_applied.edited_output,
                                    fourcc('V', 'P', '8', 'X'), &output_vp8x));
        ASSERT_TRUE(find_webp_chunk(webp_applied.edited_output,
                                    fourcc('V', 'P', '8', 'L'), &output_vp8l));
        ASSERT_TRUE(find_webp_chunk(webp_applied.edited_output,
                                    fourcc('F', 'R', 'E', 'E'), &output_free));
        ASSERT_TRUE(find_webp_chunk(webp_applied.edited_output,
                                    fourcc('I', 'C', 'C', 'P'), &output_iccp));
        ASSERT_EQ(output_vp8x.size(), 10U);
        EXPECT_EQ(std::to_integer<uint8_t>(output_vp8x[0U]), 0x24U);
        EXPECT_EQ(std::to_integer<uint8_t>(output_vp8x[1U]), 0U);
        EXPECT_EQ(std::to_integer<uint8_t>(output_vp8x[2U]), 0U);
        EXPECT_EQ(std::to_integer<uint8_t>(output_vp8x[3U]), 0U);
        EXPECT_EQ(std::to_integer<uint8_t>(output_vp8x[4U]), 1U);
        EXPECT_EQ(std::to_integer<uint8_t>(output_vp8x[5U]), 0U);
        EXPECT_EQ(std::to_integer<uint8_t>(output_vp8x[6U]), 0U);
        EXPECT_EQ(std::to_integer<uint8_t>(output_vp8x[7U]), 1U);
        EXPECT_EQ(std::to_integer<uint8_t>(output_vp8x[8U]), 0U);
        EXPECT_EQ(std::to_integer<uint8_t>(output_vp8x[9U]), 0U);
        ASSERT_EQ(output_vp8l.size(), kWebpVp8l.size());
        ASSERT_EQ(output_free.size(), kWebpFree.size());
        ASSERT_EQ(output_iccp.size(), kOldWebpIcc.size());
        EXPECT_TRUE(std::equal(kWebpVp8l.begin(), kWebpVp8l.end(),
                               output_vp8l.begin()));
        EXPECT_TRUE(std::equal(kWebpFree.begin(), kWebpFree.end(),
                               output_free.begin()));
        EXPECT_TRUE(std::equal(kOldWebpIcc.begin(), kOldWebpIcc.end(),
                               output_iccp.begin()));
        expect_embedded_packet(webp_applied.edited_output, portable_packet,
                               true);
    }

    TEST(MetadataCustomXmpTransfer,
         RenderedRawDescriptorKeepsCustomBlackLevelAndDropsRawControl)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for structured decode readback.";
#endif
        std::vector<MetadataAuthoringEntry> entries
            = custom_xmp_test::initial_entries();
        entries.push_back(custom_xmp_test::text_entry(kCameraRawSchema,
                                                      "Exposure2012", "+0.35"));
        MetaStore store;
        const MetadataAuthoringResult creation = create_metadata_store(entries,
                                                                       &store);
        ASSERT_TRUE(creation.ok())
            << metadata_authoring_status_name(creation.status);

        PrepareTransferRequest request;
        request.target_format        = TransferTargetFormat::Jpeg;
        request.include_icc_app2     = false;
        request.include_iptc_app13   = false;
        request.xmp_project_exif     = false;
        request.xmp_project_iptc     = false;
        request.xmp_include_existing = true;
        request.xmp_existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;
        XmpDecodeOptions decode_options;
        decode_options.preserve_custom_structures = true;

        PreparedTransferBundle compatible_bundle;
        const PrepareTransferResult compatible_prepared
            = prepare_metadata_for_target(store, request, &compatible_bundle);
        ASSERT_EQ(compatible_prepared.status, TransferStatus::Ok);
        ASSERT_FALSE(compatible_bundle.generated_xmp_sidecar.empty());
        ASSERT_NE(find_xmp_transfer_block(compatible_bundle), nullptr);

        MetaStore compatible_decoded;
        const XmpDecodeResult compatible_result
            = decode_xmp_packet(compatible_bundle.generated_xmp_sidecar,
                                compatible_decoded, EntryFlags::None,
                                decode_options);
        ASSERT_EQ(compatible_result.status, XmpDecodeStatus::Ok);
        compatible_decoded.finalize();
        expect_custom_content(compatible_decoded, false);
        expect_text(compatible_decoded, kCameraRawSchema, "Exposure2012",
                    "+0.35");

        request.has_source_raw_data_descriptor = true;
        request.source_raw_data_descriptor.encoding
            = MetadataRawDataEncoding::Rendered;
        PreparedTransferBundle rendered_bundle;
        const PrepareTransferResult rendered_prepared
            = prepare_metadata_for_target(store, request, &rendered_bundle);
        ASSERT_EQ(rendered_prepared.status, TransferStatus::Ok);
        ASSERT_FALSE(rendered_bundle.generated_xmp_sidecar.empty());
        ASSERT_NE(find_xmp_transfer_block(rendered_bundle), nullptr);

        MetaStore rendered_decoded;
        const XmpDecodeResult rendered_result
            = decode_xmp_packet(rendered_bundle.generated_xmp_sidecar,
                                rendered_decoded, EntryFlags::None,
                                decode_options);
        ASSERT_EQ(rendered_result.status, XmpDecodeStatus::Ok);
        rendered_decoded.finalize();
        expect_custom_content(rendered_decoded, false);
        EXPECT_TRUE(rendered_decoded
                        .find_all(make_xmp_property_key_view(kCameraRawSchema,
                                                             "Exposure2012"))
                        .empty());
    }

    TEST(MetadataCustomXmpTransfer,
         InvalidCustomGraphFailsOnlyWhenSelectedForPortableOutput)
    {
        const MetaStore invalid = make_invalid_custom_store();

        PrepareTransferRequest request;
        request.target_format        = TransferTargetFormat::Jpeg;
        request.include_exif_app1    = true;
        request.include_icc_app2     = false;
        request.include_iptc_app13   = false;
        request.xmp_project_exif     = false;
        request.xmp_project_iptc     = false;
        request.xmp_include_existing = true;
        request.xmp_existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;

        PreparedTransferBundle selected_bundle;
        const PrepareTransferResult selected
            = prepare_metadata_for_target(invalid, request, &selected_bundle);
        EXPECT_EQ(selected.status, TransferStatus::InvalidArgument);
        EXPECT_EQ(selected.code, PrepareTransferCode::XmpPackFailed);
        EXPECT_EQ(find_xmp_transfer_block(selected_bundle), nullptr);

        request.xmp_include_existing = false;
        PreparedTransferBundle excluded_bundle;
        const PrepareTransferResult excluded
            = prepare_metadata_for_target(invalid, request, &excluded_bundle);
        EXPECT_EQ(excluded.status, TransferStatus::Ok);
        const std::string_view excluded_xmp(
            reinterpret_cast<const char*>(
                excluded_bundle.generated_xmp_sidecar.data()),
            excluded_bundle.generated_xmp_sidecar.size());
        EXPECT_EQ(excluded_xmp.find(kSchema), std::string_view::npos);
        EXPECT_EQ(excluded_xmp.find("Orphan"), std::string_view::npos);
        const PreparedTransferBlock* excluded_exif = find_exif_transfer_block(
            excluded_bundle);
        ASSERT_NE(excluded_exif, nullptr);
        EXPECT_EQ(excluded_exif->route, "jpeg:app1-exif");

        request.xmp_include_existing = true;
        request.xmp_existing_namespace_policy
            = XmpExistingNamespacePolicy::KnownPortableOnly;
        PreparedTransferBundle dropped_bundle;
        const PrepareTransferResult dropped
            = prepare_metadata_for_target(invalid, request, &dropped_bundle);
        EXPECT_EQ(dropped.status, TransferStatus::Ok);
        const std::string_view dropped_xmp(
            reinterpret_cast<const char*>(
                dropped_bundle.generated_xmp_sidecar.data()),
            dropped_bundle.generated_xmp_sidecar.size());
        EXPECT_EQ(dropped_xmp.find(kSchema), std::string_view::npos);
        EXPECT_EQ(dropped_xmp.find("Orphan"), std::string_view::npos);
        const PreparedTransferBlock* dropped_exif = find_exif_transfer_block(
            dropped_bundle);
        ASSERT_NE(dropped_exif, nullptr);
        EXPECT_EQ(dropped_exif->route, "jpeg:app1-exif");
    }

}  // namespace
}  // namespace openmeta
