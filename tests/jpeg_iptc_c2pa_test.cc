// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_edit.h"
#include "openmeta/meta_key.h"
#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::array<std::byte, 14U> kPhotoshopHeader = {
    std::byte { 'P' }, std::byte { 'h' }, std::byte { 'o' }, std::byte { 't' },
    std::byte { 'o' }, std::byte { 's' }, std::byte { 'h' }, std::byte { 'o' },
    std::byte { 'p' }, std::byte { ' ' }, std::byte { '3' }, std::byte { '.' },
    std::byte { '0' }, std::byte { 0U }
};

// Unknown Photoshop resource with odd-sized name/data and nonzero pads.
constexpr std::array<std::byte, 18U> kOtherPhotoshopResource
    = { std::byte { '8' },   std::byte { 'B' },   std::byte { 'I' },
        std::byte { 'M' },   std::byte { 0x04U }, std::byte { 0x05U },
        std::byte { 2U },    std::byte { 'a' },   std::byte { 'b' },
        std::byte { 0xA1U }, std::byte { 0U },    std::byte { 0U },
        std::byte { 0U },    std::byte { 3U },    std::byte { 'X' },
        std::byte { 'Y' },   std::byte { 'Z' },   std::byte { 0xD1U } };

// One ordinary IIM dataset (record 2, dataset 5, three data bytes).
constexpr std::array<std::byte, 20U> kIptcPhotoshopResource
    = { std::byte { '8' },   std::byte { 'B' },   std::byte { 'I' },
        std::byte { 'M' },   std::byte { 0x04U }, std::byte { 0x04U },
        std::byte { 0U },    std::byte { 0xA2U }, std::byte { 0U },
        std::byte { 0U },    std::byte { 0U },    std::byte { 8U },
        std::byte { 0x1CU }, std::byte { 0x02U }, std::byte { 0x05U },
        std::byte { 0U },    std::byte { 3U },    std::byte { 'A' },
        std::byte { 'B' },   std::byte { 'C' } };

struct PreparedJpegFixture final {
    std::vector<std::byte> input;
    openmeta::TransferFileStatus file_status
        = openmeta::TransferFileStatus::ReadFailed;
    openmeta::TransferStatus prepare_status
        = openmeta::TransferStatus::Unsupported;
    openmeta::PreparedTransferBundle bundle;
};

static void
append_u16be(std::vector<std::byte>* out, uint16_t value)
{
    out->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    out->push_back(static_cast<std::byte>(value & 0xFFU));
}


static void
append_u32be(std::vector<std::byte>* out, uint32_t value)
{
    out->push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
    out->push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
    out->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    out->push_back(static_cast<std::byte>(value & 0xFFU));
}


static void
append_box(std::vector<std::byte>* out, std::array<char, 4U> type,
           std::span<const std::byte> payload)
{
    append_u32be(out, static_cast<uint32_t>(payload.size() + 8U));
    for (const char c : type) {
        out->push_back(static_cast<std::byte>(static_cast<uint8_t>(c)));
    }
    out->insert(out->end(), payload.begin(), payload.end());
}


static std::vector<std::byte>
make_app11_c2pa_payload()
{
    constexpr std::array<std::byte, 4U> cbor_payload
        = { std::byte { 0xA1U }, std::byte { 0x61U }, std::byte { 0x61U },
            std::byte { 0x01U } };
    constexpr std::array<char, 4U> jumd_type = { 'j', 'u', 'm', 'd' };
    constexpr std::array<char, 4U> cbor_type = { 'c', 'b', 'o', 'r' };
    constexpr std::array<char, 4U> jumb_type = { 'j', 'u', 'm', 'b' };

    std::vector<std::byte> jumd_payload = {
        std::byte { 'c' }, std::byte { '2' }, std::byte { 'p' },
        std::byte { 'a' }, std::byte { 0U },
    };
    std::vector<std::byte> jumb_payload;
    append_box(&jumb_payload, jumd_type, jumd_payload);
    append_box(&jumb_payload, cbor_type, cbor_payload);

    std::vector<std::byte> out = { std::byte { 'J' }, std::byte { 'P' },
                                   std::byte { 0U }, std::byte { 0U } };
    append_u32be(&out, 1U);
    append_box(&out, jumb_type, jumb_payload);
    return out;
}


static std::vector<std::byte>
make_source_photoshop_payload()
{
    std::vector<std::byte> out(kPhotoshopHeader.begin(),
                               kPhotoshopHeader.end());
    out.insert(out.end(), kOtherPhotoshopResource.begin(),
               kOtherPhotoshopResource.end());
    out.insert(out.end(), kIptcPhotoshopResource.begin(),
               kIptcPhotoshopResource.end());
    return out;
}


static void
append_jpeg_segment(std::vector<std::byte>* out, uint8_t marker,
                    std::span<const std::byte> payload)
{
    out->push_back(std::byte { 0xFFU });
    out->push_back(static_cast<std::byte>(marker));
    append_u16be(out, static_cast<uint16_t>(payload.size() + 2U));
    out->insert(out->end(), payload.begin(), payload.end());
}


static std::vector<std::byte>
make_source_jpeg()
{
    std::vector<std::byte> out = { std::byte { 0xFFU }, std::byte { 0xD8U } };
    constexpr std::array<std::byte, 2U> app0_payload = { std::byte { 0x11U },
                                                         std::byte { 0x12U } };
    constexpr std::array<std::byte, 3U> comment_payload
        = { std::byte { 'C' }, std::byte { 'M' }, std::byte { 'T' } };
    constexpr std::array<std::byte, 8U> scan_tail
        = { std::byte { 0xFFU }, std::byte { 0xDAU }, std::byte { 0U },
            std::byte { 2U },    std::byte { 0x31U }, std::byte { 0x32U },
            std::byte { 0xFFU }, std::byte { 0xD9U } };
    const std::vector<std::byte> photoshop = make_source_photoshop_payload();
    const std::vector<std::byte> c2pa      = make_app11_c2pa_payload();
    append_jpeg_segment(&out, 0xE0U, app0_payload);
    append_jpeg_segment(&out, 0xEDU, photoshop);
    append_jpeg_segment(&out, 0xFEU, comment_payload);
    append_jpeg_segment(&out, 0xEBU, c2pa);
    out.insert(out.end(), scan_tail.begin(), scan_tail.end());
    return out;
}


static std::string
make_temp_path()
{
    const char* root = std::getenv("TMPDIR");
    if (!root || !*root) {
        root = std::getenv("TEMP");
    }
#if defined(_WIN32)
    if (!root || !*root) {
        root = ".";
    }
#else
    if (!root || !*root) {
        root = "/tmp";
    }
#endif

    const uint64_t ticks = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::string path(root);
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
#if defined(_WIN32)
        path.push_back('\\');
#else
        path.push_back('/');
#endif
    }
    path += "openmeta_b25_c2pa_" + std::to_string(ticks) + ".jpg";
    return path;
}


static bool
write_file(std::string_view path, std::span<const std::byte> bytes)
{
    const std::string owned_path(path);
    std::FILE* file = std::fopen(owned_path.c_str(), "wb");
    if (!file) {
        return false;
    }
    const size_t written   = bytes.empty() ? 0U
                                           : std::fwrite(bytes.data(), 1U,
                                                         bytes.size(), file);
    const int close_status = std::fclose(file);
    return written == bytes.size() && close_status == 0;
}


static bool
prepare_source_fixture(PreparedJpegFixture* out)
{
    if (!out) {
        return false;
    }
    out->input             = make_source_jpeg();
    const std::string path = make_temp_path();
    if (!write_file(path, out->input)) {
        return false;
    }

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = true;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult prepared
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());
    out->file_status    = prepared.file_status;
    out->prepare_status = prepared.prepare.status;
    out->bundle         = prepared.bundle;
    return true;
}


static bool
append_jpeg_segment_for_expected(std::vector<std::byte>* out, uint8_t marker,
                                 std::span<const std::byte> payload)
{
    if (!out || payload.size() > 65533U) {
        return false;
    }
    append_jpeg_segment(out, marker, payload);
    return true;
}


static uint32_t
read_u32le(std::span<const std::byte> bytes, size_t offset)
{
    return static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset]))
           | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 1U]))
              << 8U)
           | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 2U]))
              << 16U)
           | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 3U]))
              << 24U);
}


static size_t
serialized_chunk_kind_offset(
    const openmeta::PreparedTransferC2paSignRequest& request,
    size_t chunk_index)
{
    size_t offset = 12U + 4U;
    offset += 8U + request.carrier_route.size();
    offset += 8U + request.manifest_label.size();
    offset += 4U + 4U + 4U + 8U + 5U;
    offset += 8U + request.message.size();
    offset += 4U;
    offset += chunk_index * (1U + 8U + 8U + 4U + 1U);
    return offset;
}


static size_t
find_preserved_chunk(
    std::span<const openmeta::PreparedTransferC2paRewriteChunk> chunks)
{
    for (size_t i = 0U; i < chunks.size(); ++i) {
        if (chunks[i].kind
            == openmeta::TransferC2paRewriteChunkKind::
                PreservedJpegPhotoshopResources) {
            return i;
        }
    }
    return chunks.size();
}

TEST(JpegIptcC2pa, BindsPreservedResidualAndRoundTripsVersionTwoPackages)
{
    PreparedJpegFixture fixture;
    ASSERT_TRUE(prepare_source_fixture(&fixture));
    ASSERT_EQ(fixture.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(fixture.prepare_status, openmeta::TransferStatus::Ok);

    uint32_t iptc_block_index = UINT32_MAX;
    uint32_t iptc_blocks      = 0U;
    for (size_t i = 0U; i < fixture.bundle.blocks.size(); ++i) {
        if (fixture.bundle.blocks[i].route == "jpeg:app13-iptc") {
            iptc_block_index = static_cast<uint32_t>(i);
            ++iptc_blocks;
        }
    }
    ASSERT_EQ(iptc_blocks, 1U);
    ASSERT_NE(iptc_block_index, UINT32_MAX);
    EXPECT_EQ(fixture.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::SigningMaterialRequired);

    openmeta::PreparedTransferC2paSignRequest request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(fixture.bundle,
                                                         &request),
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(request.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(request.prepared_segment_chunks, 2U);

    const size_t residual_index = find_preserved_chunk(
        request.content_binding_chunks);
    ASSERT_LT(residual_index, request.content_binding_chunks.size());
    const openmeta::PreparedTransferC2paRewriteChunk& residual
        = request.content_binding_chunks[residual_index];
    EXPECT_EQ(residual.source_offset, 8U);
    EXPECT_EQ(residual.size, 4U + 14U + kOtherPhotoshopResource.size());
    EXPECT_EQ(residual.jpeg_marker_code, 0xEDU);
    EXPECT_EQ(residual.block_index, UINT32_MAX);

    std::vector<std::byte> binding;
    const openmeta::BuildPreparedC2paBindingResult built_binding
        = openmeta::build_prepared_c2pa_sign_request_binding(fixture.bundle,
                                                             fixture.input,
                                                             request, &binding);
    ASSERT_EQ(built_binding.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(built_binding.written, request.content_binding_bytes);

    const openmeta::JpegEditPlan edit_plan
        = openmeta::plan_prepared_bundle_jpeg_edit(fixture.input,
                                                   fixture.bundle);
    ASSERT_EQ(edit_plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(edit_plan.selected_mode, openmeta::JpegEditMode::MetadataRewrite);
    std::vector<std::byte> edited;
    ASSERT_EQ(openmeta::apply_prepared_bundle_jpeg_edit(fixture.input,
                                                        fixture.bundle,
                                                        edit_plan, &edited)
                  .status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(binding, edited);

    std::vector<std::byte> residual_payload(kPhotoshopHeader.begin(),
                                            kPhotoshopHeader.end());
    residual_payload.insert(residual_payload.end(),
                            kOtherPhotoshopResource.begin(),
                            kOtherPhotoshopResource.end());
    std::vector<std::byte> expected                  = { std::byte { 0xFFU },
                                                         std::byte { 0xD8U } };
    constexpr std::array<std::byte, 2U> app0_payload = { std::byte { 0x11U },
                                                         std::byte { 0x12U } };
    constexpr std::array<std::byte, 3U> comment_payload
        = { std::byte { 'C' }, std::byte { 'M' }, std::byte { 'T' } };
    ASSERT_TRUE(
        append_jpeg_segment_for_expected(&expected, 0xE0U, app0_payload));
    ASSERT_TRUE(append_jpeg_segment_for_expected(
        &expected, 0xEDU, fixture.bundle.blocks[iptc_block_index].payload));
    ASSERT_TRUE(
        append_jpeg_segment_for_expected(&expected, 0xEDU, residual_payload));
    ASSERT_TRUE(
        append_jpeg_segment_for_expected(&expected, 0xFEU, comment_payload));
    constexpr std::array<std::byte, 8U> scan_tail
        = { std::byte { 0xFFU }, std::byte { 0xDAU }, std::byte { 0U },
            std::byte { 2U },    std::byte { 0x31U }, std::byte { 0x32U },
            std::byte { 0xFFU }, std::byte { 0xD9U } };
    expected.insert(expected.end(), scan_tail.begin(), scan_tail.end());
    EXPECT_EQ(edited, expected);

    openmeta::PreparedTransferC2paHandoffPackage handoff;
    ASSERT_EQ(openmeta::build_prepared_c2pa_handoff_package(fixture.bundle,
                                                            fixture.input,
                                                            &handoff),
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(handoff.binding_bytes, expected);
    std::vector<std::byte> handoff_bytes;
    ASSERT_EQ(openmeta::serialize_prepared_c2pa_handoff_package(handoff,
                                                                &handoff_bytes)
                  .status,
              openmeta::TransferStatus::Ok);
    ASSERT_GE(handoff_bytes.size(), 12U);
    EXPECT_EQ(read_u32le(handoff_bytes, 8U), 2U);

    openmeta::PreparedTransferC2paHandoffPackage decoded_handoff;
    ASSERT_EQ(openmeta::deserialize_prepared_c2pa_handoff_package(
                  handoff_bytes, &decoded_handoff)
                  .status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(decoded_handoff.binding_bytes, handoff.binding_bytes);
    ASSERT_EQ(decoded_handoff.request.content_binding_chunks.size(),
              handoff.request.content_binding_chunks.size());
    const size_t decoded_residual_index = find_preserved_chunk(
        decoded_handoff.request.content_binding_chunks);
    ASSERT_LT(decoded_residual_index,
              decoded_handoff.request.content_binding_chunks.size());
    EXPECT_EQ(decoded_handoff.request
                  .content_binding_chunks[decoded_residual_index]
                  .source_offset,
              residual.source_offset);
    EXPECT_EQ(decoded_handoff.request
                  .content_binding_chunks[decoded_residual_index]
                  .size,
              residual.size);

    openmeta::PreparedTransferC2paSignerInput signer_input;
    signer_input.signing_time                = "2026-10-16T00:00:00Z";
    signer_input.certificate_chain_bytes     = { std::byte { 0x30U },
                                                 std::byte { 0x01U } };
    signer_input.private_key_reference       = "test-only-key";
    signer_input.manifest_builder_output     = { std::byte { 0xA1U },
                                                 std::byte { 0x01U } };
    signer_input.signed_c2pa_logical_payload = { std::byte { 0xCAU },
                                                 std::byte { 0xFEU } };
    openmeta::PreparedTransferC2paSignedPackage signed_package;
    ASSERT_EQ(openmeta::build_prepared_c2pa_signed_package(fixture.bundle,
                                                           signer_input,
                                                           &signed_package),
              openmeta::TransferStatus::Ok);
    std::vector<std::byte> signed_bytes;
    ASSERT_EQ(openmeta::serialize_prepared_c2pa_signed_package(signed_package,
                                                               &signed_bytes)
                  .status,
              openmeta::TransferStatus::Ok);
    ASSERT_GE(signed_bytes.size(), 12U);
    EXPECT_EQ(read_u32le(signed_bytes, 8U), 2U);

    openmeta::PreparedTransferC2paSignedPackage decoded_signed;
    ASSERT_EQ(openmeta::deserialize_prepared_c2pa_signed_package(signed_bytes,
                                                                 &decoded_signed)
                  .status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(decoded_signed.signer_input.signing_time,
              signer_input.signing_time);
    EXPECT_EQ(decoded_signed.signer_input.certificate_chain_bytes,
              signer_input.certificate_chain_bytes);
    EXPECT_EQ(decoded_signed.signer_input.private_key_reference,
              signer_input.private_key_reference);
    EXPECT_EQ(decoded_signed.signer_input.manifest_builder_output,
              signer_input.manifest_builder_output);
    EXPECT_EQ(decoded_signed.signer_input.signed_c2pa_logical_payload,
              signer_input.signed_c2pa_logical_payload);
    EXPECT_EQ(find_preserved_chunk(
                  decoded_signed.request.content_binding_chunks),
              residual_index);
}


TEST(JpegIptcC2pa, SupportsLegacyVersionOneAndRejectsDowngradedNewChunks)
{
    openmeta::PreparedTransferC2paHandoffPackage legacy;
    legacy.request.status = openmeta::TransferStatus::Ok;
    legacy.request.rewrite_state
        = openmeta::TransferC2paRewriteState::SigningMaterialRequired;
    legacy.request.target_format = openmeta::TransferTargetFormat::Jpeg;
    legacy.request.source_kind = openmeta::TransferC2paSourceKind::ContentBound;
    legacy.request.carrier_route         = "jpeg:app11-c2pa";
    legacy.request.manifest_label        = "c2pa";
    legacy.request.source_range_chunks   = 1U;
    legacy.request.content_binding_bytes = 2U;
    openmeta::PreparedTransferC2paRewriteChunk source;
    source.kind          = openmeta::TransferC2paRewriteChunkKind::SourceRange;
    source.source_offset = 0U;
    source.size          = 2U;
    legacy.request.content_binding_chunks.push_back(source);
    legacy.binding.status  = openmeta::TransferStatus::Ok;
    legacy.binding.written = 2U;
    legacy.binding_bytes   = { std::byte { 0xFFU }, std::byte { 0xD8U } };

    std::vector<std::byte> legacy_bytes;
    ASSERT_EQ(openmeta::serialize_prepared_c2pa_handoff_package(legacy,
                                                                &legacy_bytes)
                  .status,
              openmeta::TransferStatus::Ok);
    ASSERT_GE(legacy_bytes.size(), 12U);
    EXPECT_EQ(read_u32le(legacy_bytes, 8U), 1U);
    openmeta::PreparedTransferC2paHandoffPackage decoded_legacy;
    ASSERT_EQ(openmeta::deserialize_prepared_c2pa_handoff_package(
                  legacy_bytes, &decoded_legacy)
                  .status,
              openmeta::TransferStatus::Ok);
    ASSERT_EQ(decoded_legacy.request.content_binding_chunks.size(), 1U);
    EXPECT_EQ(decoded_legacy.request.content_binding_chunks[0].kind,
              openmeta::TransferC2paRewriteChunkKind::SourceRange);
    EXPECT_EQ(decoded_legacy.binding_bytes, legacy.binding_bytes);

    PreparedJpegFixture fixture;
    ASSERT_TRUE(prepare_source_fixture(&fixture));
    ASSERT_EQ(fixture.file_status, openmeta::TransferFileStatus::Ok);
    openmeta::PreparedTransferC2paHandoffPackage current;
    ASSERT_EQ(openmeta::build_prepared_c2pa_handoff_package(fixture.bundle,
                                                            fixture.input,
                                                            &current),
              openmeta::TransferStatus::Ok);
    std::vector<std::byte> current_bytes;
    ASSERT_EQ(openmeta::serialize_prepared_c2pa_handoff_package(current,
                                                                &current_bytes)
                  .status,
              openmeta::TransferStatus::Ok);
    const size_t residual_index = find_preserved_chunk(
        current.request.content_binding_chunks);
    ASSERT_LT(residual_index, current.request.content_binding_chunks.size());
    const size_t residual_kind_offset
        = serialized_chunk_kind_offset(current.request, residual_index);
    ASSERT_LT(residual_kind_offset, current_bytes.size());

    std::vector<std::byte> downgraded = current_bytes;
    downgraded[8U]                    = std::byte { 1U };
    openmeta::PreparedTransferC2paHandoffPackage rejected_downgrade;
    EXPECT_EQ(openmeta::deserialize_prepared_c2pa_handoff_package(
                  downgraded, &rejected_downgrade)
                  .status,
              openmeta::TransferStatus::Malformed);

    std::vector<std::byte> unknown_kind = current_bytes;
    unknown_kind[residual_kind_offset]  = std::byte { 0xFFU };
    openmeta::PreparedTransferC2paHandoffPackage rejected_unknown;
    EXPECT_EQ(openmeta::deserialize_prepared_c2pa_handoff_package(
                  unknown_kind, &rejected_unknown)
                  .status,
              openmeta::TransferStatus::Malformed);
}


TEST(JpegIptcC2pa,
     RevalidatesTamperedResidualLayoutAfterConsistentRequestUpdate)
{
    PreparedJpegFixture fixture;
    ASSERT_TRUE(prepare_source_fixture(&fixture));
    ASSERT_EQ(fixture.file_status, openmeta::TransferFileStatus::Ok);

    openmeta::PreparedTransferC2paSignRequest request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(fixture.bundle,
                                                         &request),
              openmeta::TransferStatus::Ok);
    const size_t residual_index = find_preserved_chunk(
        request.content_binding_chunks);
    ASSERT_LT(residual_index, request.content_binding_chunks.size());

    openmeta::PreparedTransferBundle tampered_bundle           = fixture.bundle;
    openmeta::PreparedTransferC2paSignRequest tampered_request = request;
    tampered_bundle.c2pa_rewrite.content_binding_chunks[residual_index].size
        += 1U;
    tampered_request.content_binding_chunks[residual_index].size += 1U;
    tampered_bundle.c2pa_rewrite.content_binding_bytes += 1U;
    tampered_request.content_binding_bytes += 1U;

    std::vector<std::byte> binding = { std::byte { 0xAAU } };
    const openmeta::BuildPreparedC2paBindingResult rejected
        = openmeta::build_prepared_c2pa_sign_request_binding(tampered_bundle,
                                                             fixture.input,
                                                             tampered_request,
                                                             &binding);
    EXPECT_EQ(rejected.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(rejected.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_TRUE(binding.empty());
}

}  // namespace


TEST(JpegIptcC2pa, RemovalOnlyBindsExactResidualAndRejectsChangedIntent)
{
    using namespace openmeta;
    const auto input = make_source_jpeg();
    ReadTransferSourceSnapshotOptions read_options;
    read_options.preserve_raw_carriers = true;
    auto read = read_transfer_source_snapshot_bytes(input, read_options);
    ASSERT_EQ(read.status, TransferStatus::Ok);
    MetaEdit edit;
    uint32_t tombstones = 0U;
    for (EntryId id = 0U; id < read.snapshot.store.entries().size(); ++id) {
        if (read.snapshot.store.entry(id).key.kind
            == MetaKeyKind::IptcDataset) {
            edit.tombstone(id);
            ++tombstones;
        }
    }
    ASSERT_GT(tombstones, 0U);
    read.snapshot.store = commit(read.snapshot.store,
                                 std::span<const MetaEdit>(&edit, 1U));
    PrepareTransferRequest snapshot_request;
    snapshot_request.target_format     = TransferTargetFormat::Jpeg;
    snapshot_request.include_exif_app1 = false;
    snapshot_request.include_xmp_app1  = false;
    snapshot_request.include_icc_app2  = false;
    snapshot_request.profile.jumbf     = TransferPolicyAction::Drop;
    snapshot_request.profile.c2pa      = TransferPolicyAction::Rewrite;
    PreparedTransferBundle unsupported_snapshot;
    // Store/snapshot preparation has no target binding context. Removal must
    // not hide this independently unavailable signing operation.
    EXPECT_EQ(prepare_metadata_for_target_snapshot(read.snapshot,
                                                   snapshot_request,
                                                   &unsupported_snapshot)
                  .status,
              TransferStatus::Unsupported);
    EXPECT_TRUE(unsupported_snapshot.remove_existing_iptc);
    EXPECT_TRUE(unsupported_snapshot.blocks.empty());

    PreparedJpegFixture fixture;
    ASSERT_TRUE(prepare_source_fixture(&fixture));
    ASSERT_EQ(fixture.prepare_status, TransferStatus::Ok);
    auto bundle = fixture.bundle;
    ASSERT_EQ(bundle.blocks.size(), 1U);
    ASSERT_EQ(bundle.blocks.front().route, "jpeg:app13-iptc");
    // This manually authored complete binding contract removes the one old
    // prepared IPTC segment. The public binder independently recomputes the
    // canonical output from target bytes and must agree with this contract.
    // It does not claim automatic snapshot-to-signing-context preparation.
    bundle.blocks.clear();
    bundle.remove_existing_iptc = true;
    auto& chunks                = bundle.c2pa_rewrite.content_binding_chunks;
    uint32_t removed_prepared_chunks = 0U;
    for (size_t i = 0U; i < chunks.size();) {
        if (chunks[i].kind
            == TransferC2paRewriteChunkKind::PreparedJpegSegment) {
            ASSERT_EQ(chunks[i].block_index, 0U);
            ASSERT_LE(chunks[i].size,
                      bundle.c2pa_rewrite.content_binding_bytes);
            bundle.c2pa_rewrite.content_binding_bytes -= chunks[i].size;
            chunks.erase(chunks.begin() + static_cast<std::ptrdiff_t>(i));
            ++removed_prepared_chunks;
        } else {
            ++i;
        }
    }
    ASSERT_EQ(removed_prepared_chunks, 1U);
    ExecutePreparedTransferOptions execute;
    execute.edit_requested = true;
    execute.edit_apply     = true;
    const auto result      = execute_prepared_transfer(&bundle, input, execute);
    ASSERT_EQ(result.edit_apply.status, TransferStatus::Ok)
        << result.edit_apply.message;
    PreparedTransferC2paSignRequest request;
    ASSERT_EQ(build_prepared_c2pa_sign_request(bundle, &request),
              TransferStatus::Ok);
    const size_t residual_index = find_preserved_chunk(
        request.content_binding_chunks);
    ASSERT_LT(residual_index, request.content_binding_chunks.size());
    EXPECT_EQ(request.content_binding_chunks[residual_index].source_offset, 8U);
    EXPECT_EQ(request.content_binding_chunks[residual_index].size,
              4U + kPhotoshopHeader.size() + kOtherPhotoshopResource.size());
    std::vector<std::byte> binding;
    ASSERT_EQ(build_prepared_c2pa_sign_request_binding(bundle, input, request,
                                                       &binding)
                  .status,
              TransferStatus::Ok);
    std::vector<std::byte> expected          = { std::byte { 0xFFU },
                                                 std::byte { 0xD8U } };
    constexpr std::array<std::byte, 2U> app0 = { std::byte { 0x11U },
                                                 std::byte { 0x12U } };
    append_jpeg_segment(&expected, 0xE0U, app0);
    std::vector<std::byte> residual(kPhotoshopHeader.begin(),
                                    kPhotoshopHeader.end());
    residual.insert(residual.end(), kOtherPhotoshopResource.begin(),
                    kOtherPhotoshopResource.end());
    append_jpeg_segment(&expected, 0xEDU, residual);
    constexpr std::array<std::byte, 3U> comment
        = { std::byte { 'C' }, std::byte { 'M' }, std::byte { 'T' } };
    append_jpeg_segment(&expected, 0xFEU, comment);
    constexpr std::array<std::byte, 8U> tail
        = { std::byte { 0xFFU }, std::byte { 0xDAU }, std::byte { 0U },
            std::byte { 2U },    std::byte { 0x31U }, std::byte { 0x32U },
            std::byte { 0xFFU }, std::byte { 0xD9U } };
    expected.insert(expected.end(), tail.begin(), tail.end());
    EXPECT_EQ(binding, expected);
    EXPECT_EQ(result.edited_output, expected);
    PreparedTransferC2paHandoffPackage handoff;
    ASSERT_EQ(build_prepared_c2pa_handoff_package(bundle, input, &handoff),
              TransferStatus::Ok);
    std::vector<std::byte> wire;
    ASSERT_EQ(serialize_prepared_c2pa_handoff_package(handoff, &wire).status,
              TransferStatus::Ok);
    ASSERT_GE(wire.size(), 12U);
    EXPECT_EQ(read_u32le(wire, 8U), 2U);
    PreparedTransferC2paHandoffPackage restored;
    ASSERT_EQ(deserialize_prepared_c2pa_handoff_package(wire, &restored).status,
              TransferStatus::Ok);
    EXPECT_EQ(restored.binding_bytes, expected);
    auto changed                     = bundle;
    changed.remove_existing_iptc     = false;
    std::vector<std::byte> untouched = { std::byte { 0xA5U } };
    EXPECT_NE(build_prepared_c2pa_sign_request_binding(changed, input, request,
                                                       &untouched)
                  .status,
              TransferStatus::Ok);
    // Binding materialization retains its existing clear-on-error contract.
    EXPECT_TRUE(untouched.empty());
}
