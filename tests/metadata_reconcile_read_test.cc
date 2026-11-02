// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_transfer.h"
#include "openmeta/xmp_structure.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#    include <process.h>
#else
#    include <unistd.h>
#endif

namespace openmeta {
namespace {
    constexpr std::string_view kNs = "urn:openmeta:reconcile:reader";
    constexpr std::string_view kPacket
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
          "xmlns:m='urn:openmeta:reconcile:reader'><rdf:RDF><rdf:Description>"
          "<m:Record rdf:parseType='Resource'><m:Name>reader text</m:Name>"
          "<m:Values><rdf:Bag><rdf:li>one</rdf:li><rdf:li>two</rdf:li></rdf:Bag></m:Values>"
          "<m:Empty><rdf:Seq/></m:Empty></m:Record>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";
    constexpr std::string_view kUnsupported
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' "
          "xmlns:m='urn:openmeta:reconcile:unsupported'><rdf:RDF><rdf:Description>"
          "<m:Other rdf:parseType='Resource'><m:Good>retained</m:Good>"
          "<m:Link rdf:resource='urn:unsupported'/></m:Other>"
          "</rdf:Description></rdf:RDF></x:xmpmeta>";

    static std::span<const std::byte> bytes(std::string_view text)
    {
        return std::as_bytes(std::span(text.data(), text.size()));
    }

    static void append_xmp_segment(std::vector<std::byte>* jpeg,
                                   std::string_view packet)
    {
        constexpr char prefix[] = "http://ns.adobe.com/xap/1.0/\0";
        const size_t length     = sizeof(prefix) - 1U + packet.size() + 2U;
        ASSERT_LT(length, 65536U);
        jpeg->push_back(std::byte { 0xff });
        jpeg->push_back(std::byte { 0xe1 });
        jpeg->push_back(static_cast<std::byte>(length >> 8U));
        jpeg->push_back(static_cast<std::byte>(length & 0xffU));
        const std::span<const std::byte> signature
            = std::as_bytes(std::span(prefix)).first(sizeof(prefix) - 1U);
        jpeg->insert(jpeg->end(), signature.begin(), signature.end());
        const std::span<const std::byte> value = bytes(packet);
        jpeg->insert(jpeg->end(), value.begin(), value.end());
    }

    static std::vector<std::byte> jpeg(std::string_view first,
                                       std::string_view second = {})
    {
        std::vector<std::byte> result { std::byte { 0xff },
                                        std::byte { 0xd8 } };
        append_xmp_segment(&result, first);
        if (!second.empty())
            append_xmp_segment(&result, second);
        result.push_back(std::byte { 0xff });
        result.push_back(std::byte { 0xd9 });
        return result;
    }

    static void expect_tree(const MetaStore& store)
    {
        const std::array paths { std::string_view("Record"),
                                 std::string_view("Record/Values"),
                                 std::string_view("Record/Empty") };
        const std::array kinds { XmpContainerKind::Struct,
                                 XmpContainerKind::Bag, XmpContainerKind::Seq };
        for (size_t i = 0U; i < paths.size(); ++i) {
            const std::span<const EntryId> found = store.find_all(
                make_xmp_property_key_view(kNs, paths[i]));
            ASSERT_EQ(found.size(), 1U) << paths[i];
            EXPECT_EQ(xmp_container_kind(store.entry(found.front())), kinds[i]);
        }
    }

    struct CallbackInput final {
        std::span<const std::byte> source;
        uint32_t calls = 0U;
    };

    static RandomAccessIoResult
    read_at(void* context, uint64_t offset,
            std::span<std::byte> destination) noexcept
    {
        CallbackInput* input = static_cast<CallbackInput*>(context);
        ++input->calls;
        if (offset > input->source.size()
            || destination.size() > input->source.size() - offset)
            return { RandomAccessIoCode::IoError, 0U };
        if (!destination.empty())
            std::memcpy(destination.data(), input->source.data() + offset,
                        destination.size());
        return { RandomAccessIoCode::Ok, destination.size() };
    }

    static ReadTransferSourceSnapshotRandomAccessResult
    positional(CallbackInput* input,
               const ReadTransferSourceSnapshotRandomAccessOptions& options)
    {
        std::array<ContainerBlockRef, 16U> blocks;
        std::array<ExifIfdRef, 16U> ifds;
        std::array<uint32_t, 32U> indices;
        std::array<std::byte, 1024U> window;
        std::array<std::byte, 8192U> payload;
        std::array<std::byte, 8192U> compressed;
        std::array<std::byte, 8192U> value;
        ReadTransferSourceSnapshotRandomAccessScratch scratch;
        scratch.blocks             = blocks;
        scratch.ifds               = ifds;
        scratch.payload_indices    = indices;
        scratch.read_window        = window;
        scratch.payload            = payload;
        scratch.compressed_payload = compressed;
        scratch.value              = value;
        const RandomAccessSource source
            = make_callback_random_access_source(input->source.size(), input,
                                                 read_at);
        return read_transfer_source_snapshot_random_access(
            make_random_access_source_range(source), ContainerFormat::Jpeg,
            scratch, options);
    }

    struct TemporaryInput final {
        std::filesystem::path directory;
        std::string path;
        bool valid = false;
        explicit TemporaryInput(std::span<const std::byte> content)
        {
            std::error_code error;
            const std::filesystem::path temp
                = std::filesystem::temp_directory_path(error);
            if (error)
                return;
#if defined(_WIN32)
            const int process = _getpid();
#else
            const int process = static_cast<int>(getpid());
#endif
            for (uint32_t attempt = 0U; attempt < 128U; ++attempt) {
                const std::filesystem::path candidate
                    = temp
                      / ("openmeta-b37-read-" + std::to_string(process) + "-"
                         + std::to_string(attempt));
                if (!std::filesystem::create_directory(candidate, error)) {
                    if (error)
                        return;
                    continue;
                }
                directory    = candidate;
                path         = (directory / "input.jpg").string();
                FILE* output = std::fopen(path.c_str(), "wb");
                if (!output)
                    return;
                const size_t written = std::fwrite(content.data(), 1U,
                                                   content.size(), output);
                const int closed     = std::fclose(output);
                valid                = written == content.size() && closed == 0;
                return;
            }
        }
        ~TemporaryInput()
        {
            if (!directory.empty()) {
                std::error_code error;
                std::filesystem::remove_all(directory, error);
            }
        }
    };

    TEST(MetadataReconcileRead, BytesAndCallbackRetainTreesOnlyWhenRequested)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for XMP retention.";
#endif
        const std::vector<std::byte> input = jpeg(kPacket);
        ReadTransferSourceSnapshotOptions options;
        const ReadTransferSourceSnapshotBytesResult flat
            = read_transfer_source_snapshot_bytes(input, options);
        ASSERT_EQ(flat.status, TransferStatus::Ok);
        for (const Entry& entry : flat.snapshot.store.entries())
            EXPECT_EQ(xmp_container_kind(entry), XmpContainerKind::None);
        options.preserve_custom_xmp_structures = true;
        const ReadTransferSourceSnapshotBytesResult retained
            = read_transfer_source_snapshot_bytes(input, options);
        ASSERT_EQ(retained.status, TransferStatus::Ok);
        expect_tree(retained.snapshot.store);
        ReadTransferSourceSnapshotRandomAccessOptions random_options;
        random_options.preserve_custom_xmp_structures = true;
        CallbackInput callback { input };
        const ReadTransferSourceSnapshotRandomAccessResult positioned
            = positional(&callback, random_options);
        ASSERT_EQ(positioned.status, TransferStatus::Ok);
        EXPECT_GT(callback.calls, 0U);
        expect_tree(positioned.snapshot.store);
    }

    TEST(MetadataReconcileRead,
         UnsupportedPacketsCannotHideBehindSuccessfulPackets)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for XMP retention.";
#endif
        const std::vector<std::byte> input = jpeg(kPacket, kUnsupported);
        ReadTransferSourceSnapshotOptions options;
        EXPECT_EQ(read_transfer_source_snapshot_bytes(input, options).status,
                  TransferStatus::Ok);
        options.preserve_custom_xmp_structures = true;
        const ReadTransferSourceSnapshotBytesResult retained
            = read_transfer_source_snapshot_bytes(input, options);
        EXPECT_EQ(retained.status, TransferStatus::Malformed);
        EXPECT_EQ(retained.code,
                  ReadTransferSourceSnapshotBytesCode::DecodeFailed);
        ReadTransferSourceSnapshotRandomAccessOptions random_options;
        random_options.preserve_custom_xmp_structures = true;
        CallbackInput callback { input };
        const ReadTransferSourceSnapshotRandomAccessResult positioned
            = positional(&callback, random_options);
        EXPECT_EQ(positioned.status, TransferStatus::Malformed);
        EXPECT_EQ(positioned.code,
                  ReadTransferSourceSnapshotRandomAccessCode::DecodeFailed);
    }

    TEST(MetadataReconcileRead, IncompleteValuesFailStrictReadsAndPreparation)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for XMP retention.";
#endif
        const std::vector<std::byte> input = jpeg(kPacket);
        ReadTransferSourceSnapshotOptions options;
        options.policy.xmp_limits.max_value_bytes = 2U;
        const ReadTransferSourceSnapshotBytesResult flat
            = read_transfer_source_snapshot_bytes(input, options);
        ASSERT_EQ(flat.status, TransferStatus::Ok);
        EXPECT_EQ(flat.read.xmp.status, XmpDecodeStatus::OutputTruncated);
        options.preserve_custom_xmp_structures = true;
        EXPECT_EQ(read_transfer_source_snapshot_bytes(input, options).status,
                  TransferStatus::LimitExceeded);
        TemporaryInput file(input);
        ASSERT_TRUE(file.valid);
        PrepareTransferFileOptions prepare;
        prepare.policy.xmp_limits.max_value_bytes = 2U;
        prepare.preserve_custom_xmp_structures    = true;
        prepare.prepare.target_format             = TransferTargetFormat::Jpeg;
        const PrepareTransferFileResult result
            = prepare_metadata_for_target_file(file.path.c_str(), prepare);
        EXPECT_EQ(result.file_status, TransferFileStatus::ReadFailed);
        EXPECT_EQ(result.prepare.status, TransferStatus::LimitExceeded);
    }

    TEST(MetadataReconcileRead, FileSnapshotAndPreparationRetainContainerKinds)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for XMP retention.";
#endif
        const std::vector<std::byte> input = jpeg(kPacket);
        TemporaryInput file(input);
        ASSERT_TRUE(file.valid);
        ReadTransferSourceSnapshotFileOptions options;
        options.preserve_custom_xmp_structures = true;
        const ReadTransferSourceSnapshotFileResult read
            = read_transfer_source_snapshot_file(file.path.c_str(), options);
        ASSERT_EQ(read.file_status, TransferFileStatus::Ok);
        expect_tree(read.snapshot.store);
        PrepareTransferFileOptions prepare;
        prepare.preserve_custom_xmp_structures = true;
        prepare.prepare.target_format          = TransferTargetFormat::Jpeg;
        prepare.prepare.include_exif_app1      = false;
        prepare.prepare.include_iptc_app13     = false;
        prepare.prepare.include_icc_app2       = false;
        prepare.prepare.xmp_include_existing   = true;
        prepare.prepare.xmp_existing_namespace_policy
            = XmpExistingNamespacePolicy::PreserveCustom;
        const PrepareTransferFileResult result
            = prepare_metadata_for_target_file(file.path.c_str(), prepare);
        ASSERT_EQ(result.file_status, TransferFileStatus::Ok);
        ASSERT_EQ(result.prepare.status, TransferStatus::Ok);
        const ReadTransferSourceSnapshotBytesResult decoded
            = read_transfer_source_snapshot_bytes(
                result.bundle.generated_xmp_sidecar, options);
        ASSERT_EQ(decoded.status, TransferStatus::Ok);
        expect_tree(decoded.snapshot.store);
    }
    TEST(MetadataReconcileRead, DestinationMergesRetainTreesOrReportFailure)
    {
#if !defined(OPENMETA_HAS_EXPAT) || !OPENMETA_HAS_EXPAT
        GTEST_SKIP() << "Expat is required for XMP retention.";
#endif
        MetaStore empty;
        empty.finalize();
        const TransferSourceSnapshot source = build_transfer_source_snapshot(
            empty);
        for (bool sidecar : { false, true }) {
            for (uint32_t scenario = 0U; scenario < 3U; ++scenario) {
                SCOPED_TRACE(sidecar);
                SCOPED_TRACE(scenario);
                const std::string_view packet = scenario == 1U ? kUnsupported
                                                               : kPacket;
                const std::vector<std::byte> target = jpeg(packet);
                TemporaryInput file(target);
                ASSERT_TRUE(file.valid);
                if (sidecar) {
                    const std::string path
                        = (file.directory / "input.xmp").string();
                    FILE* output = std::fopen(path.c_str(), "wb");
                    ASSERT_NE(output, nullptr);
                    const size_t written = std::fwrite(packet.data(), 1U,
                                                       packet.size(), output);
                    const int closed     = std::fclose(output);
                    ASSERT_EQ(written, packet.size());
                    ASSERT_EQ(closed, 0);
                }
                ExecutePreparedTransferSnapshotOptions options;
                options.preserve_custom_xmp_structures = true;
                options.prepare.target_format      = TransferTargetFormat::Jpeg;
                options.prepare.include_exif_app1  = false;
                options.prepare.include_iptc_app13 = false;
                options.prepare.include_icc_app2   = false;
                options.prepare.xmp_project_exif   = false;
                options.prepare.xmp_project_iptc   = false;
                options.prepare.xmp_include_existing = true;
                options.prepare.xmp_existing_namespace_policy
                    = XmpExistingNamespacePolicy::PreserveCustom;
                options.edit_target_path = file.path;
                if (sidecar)
                    options.xmp_existing_sidecar_mode
                        = XmpExistingSidecarMode::MergeIfPresent;
                else
                    options.xmp_existing_destination_embedded_mode
                        = XmpExistingDestinationEmbeddedMode::MergeIfPresent;
                if (scenario == 2U)
                    options.policy.xmp_limits.max_value_bytes = 2U;
                const auto result = execute_prepared_transfer_snapshot(source,
                                                                       target,
                                                                       options);
                const bool loaded
                    = sidecar ? result.prepared.xmp_existing_sidecar_loaded
                              : result.xmp_existing_destination_embedded_loaded;
                const TransferStatus status
                    = sidecar ? result.prepared.xmp_existing_sidecar_status
                              : result.xmp_existing_destination_embedded_status;
                EXPECT_EQ(loaded, scenario == 0U);
                EXPECT_EQ(status, scenario == 0U ? TransferStatus::Ok
                                  : scenario == 1U
                                      ? TransferStatus::Malformed
                                      : TransferStatus::LimitExceeded);
                const auto& generated
                    = result.prepared.bundle.generated_xmp_sidecar;
                if (scenario == 0U) {
                    ReadTransferSourceSnapshotOptions read;
                    read.preserve_custom_xmp_structures = true;
                    const auto output
                        = read_transfer_source_snapshot_bytes(generated, read);
                    ASSERT_EQ(output.status, TransferStatus::Ok);
                    expect_tree(output.snapshot.store);
                } else if (!generated.empty()) {
                    ReadTransferSourceSnapshotOptions read;
                    read.preserve_custom_xmp_structures = true;
                    const auto output
                        = read_transfer_source_snapshot_bytes(generated, read);
                    ASSERT_EQ(output.status, TransferStatus::Ok);
                    for (const Entry& entry : output.snapshot.store.entries()) {
                        ASSERT_EQ(entry.key.kind, MetaKeyKind::XmpProperty);
                        const auto schema = output.snapshot.store.arena().span(
                            entry.key.data.xmp_property.schema_ns);
                        const std::string_view uri(
                            reinterpret_cast<const char*>(schema.data()),
                            schema.size());
                        EXPECT_NE(uri, kNs);
                        EXPECT_NE(uri, "urn:openmeta:reconcile:unsupported");
                    }
                }
                EXPECT_TRUE(source.store.entries().empty());
            }
        }
    }
}  // namespace
}  // namespace openmeta
