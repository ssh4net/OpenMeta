// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_edit.h"
#include "openmeta/meta_key.h"
#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::array<std::byte, 14U> kPhotoshopHeader = {
    std::byte { 'P' }, std::byte { 'h' }, std::byte { 'o' }, std::byte { 't' },
    std::byte { 'o' }, std::byte { 's' }, std::byte { 'h' }, std::byte { 'o' },
    std::byte { 'p' }, std::byte { ' ' }, std::byte { '3' }, std::byte { '.' },
    std::byte { '0' }, std::byte { 0U },
};

constexpr std::array<std::byte, 18U> kOtherPhotoshopResource = {
    std::byte { '8' },   std::byte { 'B' },   std::byte { 'I' },
    std::byte { 'M' },   std::byte { 0x04U }, std::byte { 0x05U },
    std::byte { 2U },    std::byte { 'a' },   std::byte { 'b' },
    std::byte { 0xA1U }, std::byte { 0U },    std::byte { 0U },
    std::byte { 0U },    std::byte { 3U },    std::byte { 'X' },
    std::byte { 'Y' },   std::byte { 'Z' },   std::byte { 0xD1U },
};

constexpr std::array<std::byte, 20U> kIptcPhotoshopResource = {
    std::byte { '8' },   std::byte { 'B' },   std::byte { 'I' },
    std::byte { 'M' },   std::byte { 0x04U }, std::byte { 0x04U },
    std::byte { 0U },    std::byte { 0xA2U }, std::byte { 0U },
    std::byte { 0U },    std::byte { 0U },    std::byte { 8U },
    std::byte { 0x1CU }, std::byte { 0x02U }, std::byte { 0x05U },
    std::byte { 0U },    std::byte { 3U },    std::byte { 'A' },
    std::byte { 'B' },   std::byte { 'C' },
};

class CountingWriter final : public openmeta::TransferByteWriter {
public:
    openmeta::TransferStatus
    write(std::span<const std::byte> bytes) noexcept override
    {
        ++calls;
        output.insert(output.end(), bytes.begin(), bytes.end());
        return openmeta::TransferStatus::Ok;
    }

    uint32_t calls = 0U;
    std::vector<std::byte> output;
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
           std::span<const std::byte> payload);

static void
append_u64be(std::vector<std::byte>* out, uint64_t value)
{
    for (uint32_t shift = 56U;; shift -= 8U) {
        out->push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
        if (shift == 0U) {
            break;
        }
    }
}

static void
append_bytes(std::vector<std::byte>* out, std::string_view text)
{
    for (char c : text) {
        out->push_back(static_cast<std::byte>(static_cast<uint8_t>(c)));
    }
}

static void
append_fourcc(std::vector<std::byte>* out, std::array<char, 4U> type)
{
    for (char c : type) {
        out->push_back(static_cast<std::byte>(static_cast<uint8_t>(c)));
    }
}

static void
append_cbor_major_u64(std::vector<std::byte>* out, uint8_t major,
                      uint64_t value)
{
    const uint8_t prefix = static_cast<uint8_t>(major << 5U);
    if (value < 24U) {
        out->push_back(static_cast<std::byte>(prefix | value));
    } else if (value <= 0xFFU) {
        out->push_back(static_cast<std::byte>(prefix | 24U));
        out->push_back(static_cast<std::byte>(value));
    } else if (value <= 0xFFFFU) {
        out->push_back(static_cast<std::byte>(prefix | 25U));
        append_u16be(out, static_cast<uint16_t>(value));
    } else if (value <= 0xFFFFFFFFULL) {
        out->push_back(static_cast<std::byte>(prefix | 26U));
        append_u32be(out, static_cast<uint32_t>(value));
    } else {
        out->push_back(static_cast<std::byte>(prefix | 27U));
        append_u64be(out, value);
    }
}

static void
append_cbor_text(std::vector<std::byte>* out, std::string_view text)
{
    append_cbor_major_u64(out, 3U, static_cast<uint64_t>(text.size()));
    append_bytes(out, text);
}

static void
append_cbor_bytes(std::vector<std::byte>* out, std::span<const std::byte> bytes)
{
    append_cbor_major_u64(out, 2U, static_cast<uint64_t>(bytes.size()));
    out->insert(out->end(), bytes.begin(), bytes.end());
}

static std::vector<std::byte>
make_semantic_c2pa_cbor()
{
    constexpr std::array<std::byte, 4U> signature = {
        std::byte { 0x01U },
        std::byte { 0x02U },
        std::byte { 0x03U },
        std::byte { 0x04U },
    };
    std::vector<std::byte> out;
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "manifest");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "claim_generator");
    append_cbor_text(&out, "test");
    append_cbor_text(&out, "claims");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "assertions");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "label");
    append_cbor_text(&out, "c2pa.hash.data");
    append_cbor_text(&out, "signatures");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "alg");
    append_cbor_text(&out, "ES256");
    append_cbor_text(&out, "signature");
    append_cbor_bytes(&out, signature);
    return out;
}

static std::vector<std::byte>
make_c2pa_logical_payload(std::span<const std::byte> cbor)
{
    const std::array<char, 4U> jumd_type = { 'j', 'u', 'm', 'd' };
    const std::array<char, 4U> cbor_type = { 'c', 'b', 'o', 'r' };
    const std::array<char, 4U> jumb_type = { 'j', 'u', 'm', 'b' };
    std::vector<std::byte> jumd_payload;
    append_bytes(&jumd_payload, "c2pa");
    jumd_payload.push_back(std::byte { 0U });
    std::vector<std::byte> children;
    append_box(&children, jumd_type, jumd_payload);
    append_box(&children, cbor_type, cbor);
    std::vector<std::byte> logical;
    append_box(&logical, jumb_type, children);
    return logical;
}

static std::vector<std::byte>
make_semantic_c2pa_logical_payload()
{
    const std::vector<std::byte> cbor = make_semantic_c2pa_cbor();
    return make_c2pa_logical_payload(cbor);
}

static openmeta::PreparedTransferC2paSignerInput
make_signer_input()
{
    openmeta::PreparedTransferC2paSignerInput input;
    input.signing_time            = "2026-03-09T00:00:00Z";
    input.certificate_chain_bytes = {
        std::byte { 0x30U },
        std::byte { 0x82U },
        std::byte { 0x01U },
        std::byte { 0x00U },
    };
    input.private_key_reference       = "test-key-ref";
    input.manifest_builder_output     = make_semantic_c2pa_cbor();
    input.signed_c2pa_logical_payload = make_semantic_c2pa_logical_payload();
    return input;
}

static void
append_box(std::vector<std::byte>* out, std::array<char, 4U> type,
           std::span<const std::byte> payload)
{
    append_u32be(out, static_cast<uint32_t>(payload.size() + 8U));
    for (char c : type) {
        out->push_back(static_cast<std::byte>(static_cast<uint8_t>(c)));
    }
    out->insert(out->end(), payload.begin(), payload.end());
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
make_photoshop_payload(bool include_iptc)
{
    std::vector<std::byte> out(kPhotoshopHeader.begin(),
                               kPhotoshopHeader.end());
    out.insert(out.end(), kOtherPhotoshopResource.begin(),
               kOtherPhotoshopResource.end());
    if (include_iptc) {
        out.insert(out.end(), kIptcPhotoshopResource.begin(),
                   kIptcPhotoshopResource.end());
    }
    return out;
}

static std::vector<std::byte>
make_c2pa_jpeg_with_scan(bool include_iptc       = false,
                         bool longer_source_cbor = false)
{
    constexpr std::array<std::byte, 4U> source_cbor = {
        std::byte { 0xA1U },
        std::byte { 0x61U },
        std::byte { 'a' },
        std::byte { 0x01U },
    };
    constexpr std::array<std::byte, 5U> longer_cbor = {
        std::byte { 0xA1U }, std::byte { 0x62U }, std::byte { 'a' },
        std::byte { 'b' },   std::byte { 0x01U },
    };
    const std::span<const std::byte> cbor
        = longer_source_cbor ? std::span<const std::byte>(longer_cbor)
                             : std::span<const std::byte>(source_cbor);
    const std::vector<std::byte> jumbf = make_c2pa_logical_payload(cbor);
    std::vector<std::byte> app11       = {
        std::byte { 'J' },
        std::byte { 'P' },
        std::byte { 0U },
        std::byte { 0U },
    };
    append_u32be(&app11, 1U);
    app11.insert(app11.end(), jumbf.begin(), jumbf.end());

    std::vector<std::byte> jpeg = { std::byte { 0xFFU }, std::byte { 0xD8U } };
    constexpr std::array<std::byte, 2U> app0_payload = {
        std::byte { 0x11U },
        std::byte { 0x12U },
    };
    append_jpeg_segment(&jpeg, 0xE0U, app0_payload);
    if (include_iptc) {
        const std::vector<std::byte> photoshop = make_photoshop_payload(true);
        append_jpeg_segment(&jpeg, 0xEDU, photoshop);
    }
    append_jpeg_segment(&jpeg, 0xEBU, app11);
    jpeg.insert(jpeg.end(), { std::byte { 0xFFU }, std::byte { 0xDAU } });
    append_u16be(&jpeg, 8U);
    jpeg.insert(jpeg.end(),
                { std::byte { 1U }, std::byte { 1U }, std::byte { 0U },
                  std::byte { 0U }, std::byte { 0x3FU }, std::byte { 0U } });
    jpeg.insert(jpeg.end(),
                { std::byte { 0x31U }, std::byte { 0x32U }, std::byte { 0x33U },
                  std::byte { 0xFFU }, std::byte { 0xD9U } });
    return jpeg;
}

static std::vector<std::byte>
make_jxl_without_c2pa()
{
    std::vector<std::byte> jxl;
    append_u32be(&jxl, 12U);
    append_fourcc(&jxl, { 'J', 'X', 'L', ' ' });
    append_u32be(&jxl, 0x0D0A870AU);
    constexpr std::array<std::byte, 4U> codestream = {
        std::byte { 0x11U },
        std::byte { 0x22U },
        std::byte { 0x33U },
        std::byte { 0x44U },
    };
    append_box(&jxl, { 'j', 'x', 'l', 'c' }, codestream);
    return jxl;
}

static std::vector<std::byte>
make_jxl_with_c2pa()
{
    std::vector<std::byte> jxl           = make_jxl_without_c2pa();
    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    append_box(&jxl, { 'j', 'u', 'm', 'b' },
               std::span<const std::byte>(logical.data() + 8U,
                                          logical.size() - 8U));
    return jxl;
}

static std::vector<std::byte>
make_minimal_heif()
{
    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, { 'h', 'e', 'i', 'c' });
    append_u32be(&ftyp_payload, 0U);
    append_fourcc(&ftyp_payload, { 'm', 'i', 'f', '1' });
    append_fourcc(&ftyp_payload, { 'h', 'e', 'i', 'c' });
    std::vector<std::byte> heif;
    append_box(&heif, { 'f', 't', 'y', 'p' }, ftyp_payload);
    constexpr std::array<std::byte, 4U> media = {
        std::byte { 0x11U },
        std::byte { 0x22U },
        std::byte { 0x33U },
        std::byte { 0x44U },
    };
    append_box(&heif, { 'm', 'd', 'a', 't' }, media);
    return heif;
}

static std::vector<std::byte>
make_heif_with_c2pa()
{
    const std::vector<std::byte> input   = make_minimal_heif();
    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Heif;
    openmeta::PreparedTransferBlock block;
    block.kind    = openmeta::TransferBlockKind::C2pa;
    block.order   = 150U;
    block.route   = "bmff:item-c2pa";
    block.payload = logical;
    bundle.blocks.push_back(block);
    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);
    if (result.edit_apply.status != openmeta::TransferStatus::Ok) {
        return {};
    }
    return result.edited_output;
}

static openmeta::PrepareTransferRequest
make_request(openmeta::TransferTargetFormat target_format,
             bool include_iptc = false)
{
    openmeta::PrepareTransferRequest request;
    request.target_format      = target_format;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = include_iptc;
    request.profile.jumbf      = openmeta::TransferPolicyAction::Drop;
    request.profile.c2pa       = openmeta::TransferPolicyAction::Rewrite;
    return request;
}

static size_t
find_bytes(std::span<const std::byte> bytes,
           std::span<const std::byte> needle) noexcept
{
    if (needle.empty() || needle.size() > bytes.size()) {
        return SIZE_MAX;
    }
    for (size_t offset = 0U; offset <= bytes.size() - needle.size(); ++offset) {
        size_t matched = 0U;
        while (matched < needle.size()
               && bytes[offset + matched] == needle[matched]) {
            ++matched;
        }
        if (matched == needle.size()) {
            return offset;
        }
    }
    return SIZE_MAX;
}

static std::vector<std::byte>
expected_iptc_removal_binding(const std::vector<std::byte>& input)
{
    std::vector<std::byte> expected          = { std::byte { 0xFFU },
                                                 std::byte { 0xD8U } };
    constexpr std::array<std::byte, 2U> app0 = {
        std::byte { 0x11U },
        std::byte { 0x12U },
    };
    append_jpeg_segment(&expected, 0xE0U, app0);
    std::vector<std::byte> residual(kPhotoshopHeader.begin(),
                                    kPhotoshopHeader.end());
    residual.insert(residual.end(), kOtherPhotoshopResource.begin(),
                    kOtherPhotoshopResource.end());
    append_jpeg_segment(&expected, 0xEDU, residual);
    const size_t scan = input.size() - 15U;
    expected.insert(expected.end(),
                    input.begin() + static_cast<std::ptrdiff_t>(scan),
                    input.end());
    return expected;
}

static std::vector<std::byte>
remove_jpeg_jumbf_app11(std::span<const std::byte> input)
{
    if (input.size() < 2U || input[0] != std::byte { 0xFFU }
        || input[1] != std::byte { 0xD8U }) {
        return {};
    }

    std::vector<std::byte> output = { input[0], input[1] };
    size_t offset                 = 2U;
    while (offset < input.size()) {
        if (input.size() - offset < 2U
            || input[offset] != std::byte { 0xFFU }) {
            return {};
        }
        const uint8_t marker = std::to_integer<uint8_t>(input[offset + 1U]);
        if (marker == 0xDAU || marker == 0xD9U) {
            output.insert(output.end(),
                          input.begin() + static_cast<std::ptrdiff_t>(offset),
                          input.end());
            return output;
        }
        if (input.size() - offset < 4U) {
            return {};
        }
        const uint16_t segment_length = static_cast<uint16_t>(
            (std::to_integer<uint16_t>(input[offset + 2U]) << 8U)
            | std::to_integer<uint16_t>(input[offset + 3U]));
        if (segment_length < 2U) {
            return {};
        }
        const size_t segment_size = 2U + static_cast<size_t>(segment_length);
        if (segment_size > input.size() - offset) {
            return {};
        }

        const size_t payload_offset = offset + 4U;
        const size_t payload_size   = static_cast<size_t>(segment_length) - 2U;
        const bool is_jumbf_app11
            = marker == 0xEBU && payload_size >= 4U
              && input[payload_offset] == std::byte { 'J' }
              && input[payload_offset + 1U] == std::byte { 'P' }
              && input[payload_offset + 2U] == std::byte { 0U }
              && input[payload_offset + 3U] == std::byte { 0U };
        if (!is_jumbf_app11) {
            output.insert(output.end(),
                          input.begin() + static_cast<std::ptrdiff_t>(offset),
                          input.begin()
                              + static_cast<std::ptrdiff_t>(offset
                                                            + segment_size));
        }
        offset += segment_size;
    }
    return {};
}

static void
expect_target_identity_check(openmeta::TransferTargetFormat target_format,
                             const std::vector<std::byte>& target,
                             std::span<const std::byte> changed_bytes)
{
    const openmeta::ReadTransferSourceSnapshotBytesResult snapshot
        = openmeta::read_transfer_source_snapshot_bytes(
            std::span<const std::byte>(target.data(), target.size()));
    ASSERT_EQ(snapshot.status, openmeta::TransferStatus::Ok);

    const openmeta::PrepareTransferRequest request = make_request(
        target_format);
    openmeta::PreparedTransferC2paSigningContext context;
    const openmeta::PrepareTransferC2paSigningContextResult prepared
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(target.data(), target.size()), request,
            &context);
    ASSERT_EQ(prepared.binding.status, openmeta::TransferStatus::Ok)
        << prepared.binding.message;
    EXPECT_EQ(prepared.prepare.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(prepared.prepare.code,
              openmeta::PrepareTransferCode::RequestedMetadataNotSerializable);
    EXPECT_EQ(prepared.prepare.errors, 0U);
    EXPECT_EQ(context.handoff.request.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(context.handoff.binding_bytes.size(),
              context.handoff.binding.written);

    const openmeta::BuildPreparedC2paBindingResult valid
        = openmeta::validate_prepared_c2pa_signing_context(
            context, std::span<const std::byte>(target.data(), target.size()));
    EXPECT_EQ(valid.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(valid.code, openmeta::EmitTransferCode::None);

    ASSERT_EQ(changed_bytes.size(), target.size());
    const openmeta::BuildPreparedC2paBindingResult rejected
        = openmeta::validate_prepared_c2pa_signing_context(context,
                                                           changed_bytes);
    EXPECT_EQ(rejected.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(rejected.code, openmeta::EmitTransferCode::PlanMismatch);
}

TEST(C2paSnapshotContext, BindsAndReplansAllSupportedTargetFamilies)
{
    const std::vector<std::byte> jpeg   = make_c2pa_jpeg_with_scan();
    std::vector<std::byte> changed_jpeg = jpeg;
    changed_jpeg[changed_jpeg.size() - 3U] ^= std::byte { 0x01U };
    expect_target_identity_check(
        openmeta::TransferTargetFormat::Jpeg, jpeg,
        std::span<const std::byte>(changed_jpeg.data(), changed_jpeg.size()));

    const std::vector<std::byte> jxl               = make_jxl_with_c2pa();
    constexpr std::array<std::byte, 4U> codestream = {
        std::byte { 0x11U },
        std::byte { 0x22U },
        std::byte { 0x33U },
        std::byte { 0x44U },
    };
    const size_t jxl_media_offset = find_bytes(jxl, codestream);
    ASSERT_NE(jxl_media_offset, SIZE_MAX);
    std::vector<std::byte> changed_jxl = jxl;
    changed_jxl[jxl_media_offset + 1U] ^= std::byte { 0x01U };
    expect_target_identity_check(openmeta::TransferTargetFormat::Jxl, jxl,
                                 std::span<const std::byte>(changed_jxl.data(),
                                                            changed_jxl.size()));

    const std::vector<std::byte> heif = make_heif_with_c2pa();
    ASSERT_FALSE(heif.empty());
    const size_t heif_media_offset = find_bytes(heif, codestream);
    ASSERT_NE(heif_media_offset, SIZE_MAX);
    std::vector<std::byte> changed_heif = heif;
    changed_heif[heif_media_offset + 1U] ^= std::byte { 0x01U };
    expect_target_identity_check(
        openmeta::TransferTargetFormat::Heif, heif,
        std::span<const std::byte>(changed_heif.data(), changed_heif.size()));
}

TEST(C2paSnapshotContext, AllowsUnchangedJxlBindingButRejectsOrdinaryNoOpEdit)
{
    const std::vector<std::byte> source = make_c2pa_jpeg_with_scan();
    const openmeta::ReadTransferSourceSnapshotBytesResult snapshot
        = openmeta::read_transfer_source_snapshot_bytes(
            std::span<const std::byte>(source.data(), source.size()));
    ASSERT_EQ(snapshot.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> target            = make_jxl_without_c2pa();
    const openmeta::PrepareTransferRequest request = make_request(
        openmeta::TransferTargetFormat::Jxl);
    openmeta::PreparedTransferC2paSigningContext context;
    const openmeta::PrepareTransferC2paSigningContextResult prepared
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(target.data(), target.size()), request,
            &context);
    ASSERT_EQ(prepared.binding.status, openmeta::TransferStatus::Ok)
        << prepared.binding.message;
    EXPECT_EQ(context.handoff.binding_bytes, target);

    openmeta::PreparedTransferBundle empty_bundle;
    empty_bundle.target_format = openmeta::TransferTargetFormat::Jxl;
    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult ordinary_edit
        = openmeta::execute_prepared_transfer(
            &empty_bundle,
            std::span<const std::byte>(target.data(), target.size()), options);
    EXPECT_EQ(ordinary_edit.edit_plan_status,
              openmeta::TransferStatus::InvalidArgument);
    EXPECT_NE(ordinary_edit.edit_plan_message.find(
                  "jxl edit requires at least one metadata box update"),
              std::string::npos);
}

TEST(C2paSnapshotContext, AcceptsSourceDifferenceRemovedByRewrite)
{
    const std::vector<std::byte> jpeg = make_c2pa_jpeg_with_scan();
    const openmeta::ReadTransferSourceSnapshotBytesResult snapshot
        = openmeta::read_transfer_source_snapshot_bytes(
            std::span<const std::byte>(jpeg.data(), jpeg.size()));
    ASSERT_EQ(snapshot.status, openmeta::TransferStatus::Ok);

    const openmeta::PrepareTransferRequest request = make_request(
        openmeta::TransferTargetFormat::Jpeg);
    openmeta::PreparedTransferC2paSigningContext context;
    const openmeta::PrepareTransferC2paSigningContextResult prepared
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
            &context);
    ASSERT_EQ(prepared.binding.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> changed                     = jpeg;
    constexpr std::array<std::byte, 4U> old_cbor_value = {
        std::byte { 0xA1U },
        std::byte { 0x61U },
        std::byte { 'a' },
        std::byte { 0x01U },
    };
    const size_t cbor_offset = find_bytes(changed, old_cbor_value);
    ASSERT_NE(cbor_offset, SIZE_MAX);
    changed[cbor_offset + 2U] = std::byte { 'b' };
    ASSERT_EQ(changed.size(), jpeg.size());
    const openmeta::BuildPreparedC2paBindingResult equivalent
        = openmeta::validate_prepared_c2pa_signing_context(
            context,
            std::span<const std::byte>(changed.data(), changed.size()));
    EXPECT_EQ(equivalent.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(equivalent.code, openmeta::EmitTransferCode::None);
}

TEST(C2paSnapshotContext, RejectsTamperedIssuedAndPreparedChunkDescriptors)
{
    const std::vector<std::byte> jpeg = make_c2pa_jpeg_with_scan();
    const openmeta::ReadTransferSourceSnapshotBytesResult snapshot
        = openmeta::read_transfer_source_snapshot_bytes(
            std::span<const std::byte>(jpeg.data(), jpeg.size()));
    ASSERT_EQ(snapshot.status, openmeta::TransferStatus::Ok);
    const openmeta::PrepareTransferRequest request = make_request(
        openmeta::TransferTargetFormat::Jpeg);
    openmeta::PreparedTransferC2paSigningContext context;
    ASSERT_EQ(openmeta::prepare_c2pa_signing_context(
                  snapshot.snapshot,
                  std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
                  &context)
                  .binding.status,
              openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferC2paSigningContext changed_count = context;
    ++changed_count.handoff.request.source_range_chunks;
    const openmeta::BuildPreparedC2paBindingResult count_rejected
        = openmeta::validate_prepared_c2pa_signing_context(
            changed_count,
            std::span<const std::byte>(jpeg.data(), jpeg.size()));
    EXPECT_EQ(count_rejected.code, openmeta::EmitTransferCode::PlanMismatch);

    openmeta::PreparedTransferC2paSigningContext changed_request = context;
    ASSERT_FALSE(
        changed_request.handoff.request.content_binding_chunks.empty());
    ++changed_request.handoff.request.content_binding_chunks[0].source_offset;
    const openmeta::BuildPreparedC2paBindingResult request_rejected
        = openmeta::validate_prepared_c2pa_signing_context(
            changed_request,
            std::span<const std::byte>(jpeg.data(), jpeg.size()));
    EXPECT_EQ(request_rejected.code, openmeta::EmitTransferCode::PlanMismatch);

    openmeta::PreparedTransferC2paSigningContext changed_bundle = context;
    ASSERT_FALSE(
        changed_bundle.bundle.c2pa_rewrite.content_binding_chunks.empty());
    ++changed_bundle.bundle.c2pa_rewrite.content_binding_chunks[0].source_offset;
    const openmeta::BuildPreparedC2paBindingResult bundle_rejected
        = openmeta::validate_prepared_c2pa_signing_context(
            changed_bundle,
            std::span<const std::byte>(jpeg.data(), jpeg.size()));
    EXPECT_EQ(bundle_rejected.code, openmeta::EmitTransferCode::PlanMismatch);
}

TEST(C2paSnapshotContext, EnforcesExplicitSnapshotAndBindingBudgets)
{
    const std::vector<std::byte> jpeg = make_c2pa_jpeg_with_scan();
    const openmeta::ReadTransferSourceSnapshotBytesResult snapshot
        = openmeta::read_transfer_source_snapshot_bytes(
            std::span<const std::byte>(jpeg.data(), jpeg.size()));
    ASSERT_EQ(snapshot.status, openmeta::TransferStatus::Ok);
    ASSERT_GT(snapshot.snapshot.store.entries().size(), 1U);
    ASSERT_GT(snapshot.snapshot.store.arena().bytes().size(), 1U);
    const openmeta::PrepareTransferRequest request = make_request(
        openmeta::TransferTargetFormat::Jpeg);

    openmeta::PreparedTransferC2paSigningContext sentinel;
    sentinel.handoff.binding_bytes = { std::byte { 0xA5U } };

    openmeta::C2paSigningContextLimits limits;
    limits.max_snapshot_entries = 1U;
    const openmeta::PrepareTransferC2paSigningContextResult entry_limited
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
            &sentinel, openmeta::OpenMetaResourcePolicy {}, limits);
    EXPECT_EQ(entry_limited.binding.status,
              openmeta::TransferStatus::LimitExceeded);
    EXPECT_EQ(sentinel.handoff.binding_bytes.size(), 1U);

    limits                          = openmeta::C2paSigningContextLimits {};
    limits.max_snapshot_arena_bytes = 1U;
    const openmeta::PrepareTransferC2paSigningContextResult arena_limited
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
            &sentinel, openmeta::OpenMetaResourcePolicy {}, limits);
    EXPECT_EQ(arena_limited.binding.status,
              openmeta::TransferStatus::LimitExceeded);

    limits                   = openmeta::C2paSigningContextLimits {};
    limits.max_binding_bytes = 1U;
    const openmeta::PrepareTransferC2paSigningContextResult binding_limited
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
            &sentinel, openmeta::OpenMetaResourcePolicy {}, limits);
    EXPECT_EQ(binding_limited.binding.status,
              openmeta::TransferStatus::LimitExceeded);
    EXPECT_EQ(sentinel.handoff.binding_bytes.size(), 1U);

    limits.max_snapshot_entries = 0U;
    const openmeta::PrepareTransferC2paSigningContextResult zero_entries
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
            &sentinel, openmeta::OpenMetaResourcePolicy {}, limits);
    EXPECT_EQ(zero_entries.binding.status,
              openmeta::TransferStatus::InvalidArgument);

    limits                          = openmeta::C2paSigningContextLimits {};
    limits.max_snapshot_arena_bytes = 0U;
    const openmeta::PrepareTransferC2paSigningContextResult zero_arena
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
            &sentinel, openmeta::OpenMetaResourcePolicy {}, limits);
    EXPECT_EQ(zero_arena.binding.status,
              openmeta::TransferStatus::InvalidArgument);

    openmeta::OpenMetaResourcePolicy policy;
    policy.max_file_bytes = static_cast<uint64_t>(jpeg.size() - 1U);
    const openmeta::PrepareTransferC2paSigningContextResult file_limited
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
            &sentinel, policy);
    EXPECT_EQ(file_limited.binding.status,
              openmeta::TransferStatus::LimitExceeded);

    openmeta::PreparedTransferC2paSigningContext context;
    ASSERT_EQ(openmeta::prepare_c2pa_signing_context(
                  snapshot.snapshot,
                  std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
                  &context)
                  .binding.status,
              openmeta::TransferStatus::Ok);
    context.limits.max_binding_bytes = 1U;
    const openmeta::BuildPreparedC2paBindingResult deferred_binding_limited
        = openmeta::validate_prepared_c2pa_signing_context(
            context, std::span<const std::byte>(jpeg.data(), jpeg.size()));
    EXPECT_EQ(deferred_binding_limited.status,
              openmeta::TransferStatus::LimitExceeded);

    context.limits                      = openmeta::C2paSigningContextLimits {};
    context.limits.max_snapshot_entries = 1U;
    const openmeta::BuildPreparedC2paBindingResult deferred_entries_limited
        = openmeta::validate_prepared_c2pa_signing_context(
            context, std::span<const std::byte>(jpeg.data(), jpeg.size()));
    EXPECT_EQ(deferred_entries_limited.status,
              openmeta::TransferStatus::LimitExceeded);
}

TEST(C2paSnapshotContext, RejectsMalformedTruncatedAndUnsupportedTargets)
{
    const std::vector<std::byte> jpeg = make_c2pa_jpeg_with_scan();
    const openmeta::ReadTransferSourceSnapshotBytesResult snapshot
        = openmeta::read_transfer_source_snapshot_bytes(
            std::span<const std::byte>(jpeg.data(), jpeg.size()));
    ASSERT_EQ(snapshot.status, openmeta::TransferStatus::Ok);
    const openmeta::PrepareTransferRequest request = make_request(
        openmeta::TransferTargetFormat::Jpeg);

    openmeta::PreparedTransferC2paSigningContext sentinel;
    sentinel.handoff.binding_bytes = { std::byte { 0xA5U } };
    std::vector<std::byte> truncated(jpeg.begin(), jpeg.begin() + 12);
    const openmeta::PrepareTransferC2paSigningContextResult truncated_result
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(truncated.data(), truncated.size()),
            request, &sentinel);
    EXPECT_EQ(truncated_result.binding.status,
              openmeta::TransferStatus::Malformed);
    EXPECT_EQ(sentinel.handoff.binding_bytes.size(), 1U);

    std::vector<std::byte> malformed = jpeg;
    malformed[0]                     = std::byte { 0U };
    const openmeta::PrepareTransferC2paSigningContextResult malformed_result
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(malformed.data(), malformed.size()),
            request, &sentinel);
    EXPECT_EQ(malformed_result.binding.status,
              openmeta::TransferStatus::Malformed);

    openmeta::PrepareTransferRequest unsupported = make_request(
        openmeta::TransferTargetFormat::Webp);
    const openmeta::PrepareTransferC2paSigningContextResult unsupported_result
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(jpeg.data(), jpeg.size()), unsupported,
            &sentinel);
    EXPECT_EQ(unsupported_result.binding.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(sentinel.handoff.binding_bytes.size(), 1U);
}

TEST(C2paSnapshotContext, PreparesIptcRemovalAndPreservesSnapshot)
{
    const std::vector<std::byte> jpeg          = make_c2pa_jpeg_with_scan(true);
    const std::vector<std::byte> original_jpeg = jpeg;
    openmeta::ReadTransferSourceSnapshotOptions read_options;
    read_options.preserve_raw_carriers = true;
    openmeta::ReadTransferSourceSnapshotBytesResult snapshot
        = openmeta::read_transfer_source_snapshot_bytes(
            std::span<const std::byte>(jpeg.data(), jpeg.size()), read_options);
    ASSERT_EQ(snapshot.status, openmeta::TransferStatus::Ok);
    openmeta::MetaEdit edit;
    uint32_t tombstones = 0U;
    for (openmeta::EntryId id = 0U;
         id < snapshot.snapshot.store.entries().size(); ++id) {
        if (snapshot.snapshot.store.entry(id).key.kind
            == openmeta::MetaKeyKind::IptcDataset) {
            edit.tombstone(id);
            ++tombstones;
        }
    }
    ASSERT_GT(tombstones, 0U);
    snapshot.snapshot.store
        = openmeta::commit(snapshot.snapshot.store,
                           std::span<const openmeta::MetaEdit>(&edit, 1U));
    const size_t original_entry_count = snapshot.snapshot.store.entries().size();
    const std::vector<std::byte> original_arena(
        snapshot.snapshot.store.arena().bytes().begin(),
        snapshot.snapshot.store.arena().bytes().end());

    const openmeta::PrepareTransferRequest request
        = make_request(openmeta::TransferTargetFormat::Jpeg, true);
    openmeta::PreparedTransferC2paSigningContext context;
    const openmeta::PrepareTransferC2paSigningContextResult prepared
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
            &context);
    ASSERT_EQ(prepared.binding.status, openmeta::TransferStatus::Ok)
        << prepared.binding.message;
    EXPECT_TRUE(context.bundle.remove_existing_iptc);
    EXPECT_TRUE(context.bundle.blocks.empty());
    EXPECT_EQ(context.handoff.binding_bytes,
              expected_iptc_removal_binding(jpeg));
    constexpr std::array<std::byte, 20U> iptc_resource = kIptcPhotoshopResource;
    EXPECT_EQ(find_bytes(context.handoff.binding_bytes, iptc_resource),
              SIZE_MAX);
    EXPECT_EQ(snapshot.snapshot.store.entries().size(), original_entry_count);
    EXPECT_TRUE(std::equal(snapshot.snapshot.store.arena().bytes().begin(),
                           snapshot.snapshot.store.arena().bytes().end(),
                           original_arena.begin(), original_arena.end()));
    EXPECT_EQ(jpeg, original_jpeg);
}

TEST(C2paSnapshotContext, RejectsChangedPreparedPayload)
{
    const std::vector<std::byte> jpeg = make_c2pa_jpeg_with_scan(true);
    const openmeta::ReadTransferSourceSnapshotBytesResult snapshot
        = openmeta::read_transfer_source_snapshot_bytes(
            std::span<const std::byte>(jpeg.data(), jpeg.size()));
    ASSERT_EQ(snapshot.status, openmeta::TransferStatus::Ok);
    const openmeta::PrepareTransferRequest request
        = make_request(openmeta::TransferTargetFormat::Jpeg, true);
    openmeta::PreparedTransferC2paSigningContext context;
    const openmeta::PrepareTransferC2paSigningContextResult prepared
        = openmeta::prepare_c2pa_signing_context(
            snapshot.snapshot,
            std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
            &context);
    ASSERT_EQ(prepared.binding.status, openmeta::TransferStatus::Ok)
        << prepared.binding.message;
    ASSERT_FALSE(context.bundle.blocks.empty());
    ASSERT_EQ(context.bundle.blocks[0].kind,
              openmeta::TransferBlockKind::IptcIim);
    constexpr std::array<std::byte, 3U> iptc_value = {
        std::byte { 'A' },
        std::byte { 'B' },
        std::byte { 'C' },
    };
    const size_t value_offset = find_bytes(context.bundle.blocks[0].payload,
                                           iptc_value);
    ASSERT_NE(value_offset, SIZE_MAX);
    context.bundle.blocks[0].payload[value_offset] = std::byte { 'D' };
    const openmeta::BuildPreparedC2paBindingResult rejected
        = openmeta::validate_prepared_c2pa_signing_context(
            context, std::span<const std::byte>(jpeg.data(), jpeg.size()));
    EXPECT_EQ(rejected.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(rejected.code, openmeta::EmitTransferCode::PlanMismatch);
}

TEST(C2paSnapshotContext, PreflightsSignerBeforeCallingWriter)
{
    const std::vector<std::byte> jpeg = make_c2pa_jpeg_with_scan();
    const openmeta::ReadTransferSourceSnapshotBytesResult snapshot
        = openmeta::read_transfer_source_snapshot_bytes(
            std::span<const std::byte>(jpeg.data(), jpeg.size()));
    ASSERT_EQ(snapshot.status, openmeta::TransferStatus::Ok);
    const openmeta::PrepareTransferRequest request = make_request(
        openmeta::TransferTargetFormat::Jpeg);
    openmeta::PreparedTransferC2paSigningContext context;
    ASSERT_EQ(openmeta::prepare_c2pa_signing_context(
                  snapshot.snapshot,
                  std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
                  &context)
                  .binding.status,
              openmeta::TransferStatus::Ok);

    CountingWriter no_apply_writer;
    const openmeta::PreparedTransferC2paSignerInput signer = make_signer_input();
    const openmeta::ExecutePreparedTransferFileResult writer_without_apply
        = openmeta::execute_prepared_c2pa_signing_context(
            context, std::span<const std::byte>(jpeg.data(), jpeg.size()),
            signer, false, &no_apply_writer);
    EXPECT_EQ(writer_without_apply.execute.c2pa_stage_requested, true);
    EXPECT_EQ(writer_without_apply.execute.c2pa_stage.status,
              openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(no_apply_writer.calls, 0U);
    EXPECT_TRUE(writer_without_apply.execute.edited_output.empty());
    EXPECT_TRUE(writer_without_apply.xmp_sidecar_output.empty());

    CountingWriter missing_signer_writer;
    const openmeta::PreparedTransferC2paSignerInput missing_signer;
    const openmeta::ExecutePreparedTransferFileResult missing_signer_result
        = openmeta::execute_prepared_c2pa_signing_context(
            context, std::span<const std::byte>(jpeg.data(), jpeg.size()),
            missing_signer, true, &missing_signer_writer);
    EXPECT_EQ(missing_signer_result.execute.c2pa_stage_requested, true);
    EXPECT_EQ(missing_signer_result.execute.c2pa_stage_validation.status,
              openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(missing_signer_writer.calls, 0U);
    EXPECT_TRUE(missing_signer_result.execute.edited_output.empty());
    EXPECT_TRUE(missing_signer_result.xmp_sidecar_output.empty());

    CountingWriter invalid_signer_writer;
    openmeta::PreparedTransferC2paSignerInput invalid_signer = signer;
    invalid_signer.signed_c2pa_logical_payload = { std::byte { 0U } };
    const openmeta::ExecutePreparedTransferFileResult invalid_signer_result
        = openmeta::execute_prepared_c2pa_signing_context(
            context, std::span<const std::byte>(jpeg.data(), jpeg.size()),
            invalid_signer, true, &invalid_signer_writer);
    EXPECT_NE(invalid_signer_result.execute.c2pa_stage_validation.status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(invalid_signer_writer.calls, 0U);
    EXPECT_TRUE(invalid_signer_result.execute.edited_output.empty());
    EXPECT_TRUE(invalid_signer_result.xmp_sidecar_output.empty());

    CountingWriter mismatch_writer;
    std::vector<std::byte> changed = jpeg;
    changed[changed.size() - 3U] ^= std::byte { 0x01U };
    const openmeta::ExecutePreparedTransferFileResult mismatch_result
        = openmeta::execute_prepared_c2pa_signing_context(
            context, std::span<const std::byte>(changed.data(), changed.size()),
            signer, true, &mismatch_writer);
    EXPECT_EQ(mismatch_result.execute.c2pa_stage.code,
              openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(mismatch_writer.calls, 0U);
    EXPECT_TRUE(mismatch_result.execute.edited_output.empty());
}

TEST(C2paSnapshotContext,
     UsesValidatedReplanForExecutionAndWritesOnlyWhenApplied)
{
    const std::vector<std::byte> jpeg = make_c2pa_jpeg_with_scan();
    const std::vector<std::byte> shifted_c2pa_jpeg
        = make_c2pa_jpeg_with_scan(false, true);
    ASSERT_EQ(shifted_c2pa_jpeg.size(), jpeg.size() + 1U);
    constexpr std::array<std::byte, 9U> scan_header = {
        std::byte { 0xFFU }, std::byte { 0xDAU }, std::byte { 0U },
        std::byte { 8U },    std::byte { 1U },    std::byte { 1U },
        std::byte { 0U },    std::byte { 0U },    std::byte { 0x3FU },
    };
    const size_t original_scan_offset = find_bytes(jpeg, scan_header);
    const size_t shifted_scan_offset  = find_bytes(shifted_c2pa_jpeg,
                                                   scan_header);
    ASSERT_NE(original_scan_offset, SIZE_MAX);
    ASSERT_NE(shifted_scan_offset, SIZE_MAX);
    EXPECT_EQ(shifted_scan_offset, original_scan_offset + 1U);

    const openmeta::ReadTransferSourceSnapshotBytesResult snapshot
        = openmeta::read_transfer_source_snapshot_bytes(
            std::span<const std::byte>(jpeg.data(), jpeg.size()));
    ASSERT_EQ(snapshot.status, openmeta::TransferStatus::Ok);
    const openmeta::PrepareTransferRequest request = make_request(
        openmeta::TransferTargetFormat::Jpeg);
    openmeta::PreparedTransferC2paSigningContext context;
    ASSERT_EQ(openmeta::prepare_c2pa_signing_context(
                  snapshot.snapshot,
                  std::span<const std::byte>(jpeg.data(), jpeg.size()), request,
                  &context)
                  .binding.status,
              openmeta::TransferStatus::Ok);

    const openmeta::PreparedTransferC2paSignerInput signer = make_signer_input();
    const openmeta::ExecutePreparedTransferFileResult staged_only
        = openmeta::execute_prepared_c2pa_signing_context(
            context, std::span<const std::byte>(jpeg.data(), jpeg.size()),
            signer);
    EXPECT_EQ(staged_only.execute.c2pa_stage_validation.status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(staged_only.execute.c2pa_stage.status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(staged_only.prepared.prepare.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(staged_only.execute.edit_requested);
    EXPECT_TRUE(staged_only.execute.edited_output.empty());

    const openmeta::BuildPreparedC2paBindingResult shifted_validation
        = openmeta::validate_prepared_c2pa_signing_context(
            context, std::span<const std::byte>(shifted_c2pa_jpeg.data(),
                                                shifted_c2pa_jpeg.size()));
    EXPECT_EQ(shifted_validation.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(shifted_validation.written, context.handoff.binding_bytes.size());

    CountingWriter writer;
    const openmeta::ExecutePreparedTransferFileResult applied
        = openmeta::execute_prepared_c2pa_signing_context(
            context,
            std::span<const std::byte>(shifted_c2pa_jpeg.data(),
                                       shifted_c2pa_jpeg.size()),
            signer, true, &writer);
    EXPECT_EQ(applied.execute.c2pa_stage_validation.status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(applied.execute.c2pa_stage.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(applied.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_GT(writer.calls, 0U);
    EXPECT_FALSE(writer.output.empty());
    EXPECT_TRUE(applied.execute.edited_output.empty());

    const std::vector<std::byte> non_c2pa_output = remove_jpeg_jumbf_app11(
        writer.output);
    EXPECT_EQ(non_c2pa_output, context.handoff.binding_bytes);
    const std::span<const std::byte> preserved_scan(shifted_c2pa_jpeg.data()
                                                        + shifted_scan_offset,
                                                    shifted_c2pa_jpeg.size()
                                                        - shifted_scan_offset);
    ASSERT_GE(writer.output.size(), preserved_scan.size());
    EXPECT_TRUE(
        std::equal(preserved_scan.begin(), preserved_scan.end(),
                   writer.output.end()
                       - static_cast<std::ptrdiff_t>(preserved_scan.size())));
}

TEST(C2paSnapshotContext, RequiresCompleteJpegFramingBeforePublication)
{
    const std::vector<std::byte> jpeg = make_c2pa_jpeg_with_scan();
    const openmeta::ReadTransferSourceSnapshotBytesResult source
        = openmeta::read_transfer_source_snapshot_bytes(jpeg);
    ASSERT_EQ(source.status, openmeta::TransferStatus::Ok);
    const openmeta::PrepareTransferRequest request = make_request(
        openmeta::TransferTargetFormat::Jpeg);
    openmeta::PreparedTransferC2paSigningContext context;
    ASSERT_EQ(openmeta::prepare_c2pa_signing_context(source.snapshot, jpeg,
                                                     request, &context)
                  .binding.status,
              openmeta::TransferStatus::Ok);
    const openmeta::PreparedTransferC2paSignerInput signer = make_signer_input();
    const std::vector<std::byte> missing_eoi(jpeg.begin(), jpeg.end() - 2);
    const std::array<std::vector<std::byte>, 7U> malformed = {
        std::vector<std::byte> { std::byte { 0xFFU }, std::byte { 0xD8U } },
        std::vector<std::byte> { std::byte { 0xFFU }, std::byte { 0xD8U },
                                 std::byte { 0xFFU } },
        std::vector<std::byte> { std::byte { 0xFFU }, std::byte { 0xD8U },
                                 std::byte { 0xFFU }, std::byte { 0xE1U },
                                 std::byte { 0U }, std::byte { 10U },
                                 std::byte { 0xFFU }, std::byte { 0xD9U } },
        std::vector<std::byte> {
            std::byte { 0xFFU }, std::byte { 0xD8U }, std::byte { 0xFFU },
            std::byte { 0xDAU }, std::byte { 0U }, std::byte { 8U },
            std::byte { 1U }, std::byte { 0xFFU }, std::byte { 0xD9U } },
        std::vector<std::byte> {
            std::byte { 0xFFU }, std::byte { 0xD8U }, std::byte { 0xFFU },
            std::byte { 0xDAU }, std::byte { 0U }, std::byte { 6U },
            std::byte { 1U }, std::byte { 0U }, std::byte { 0U },
            std::byte { 0U }, std::byte { 0xFFU }, std::byte { 0xD9U } },
        std::vector<std::byte> { std::byte { 0xFFU }, std::byte { 0xD8U },
                                 std::byte { 0xFFU }, std::byte { 0U },
                                 std::byte { 0xFFU }, std::byte { 0xD9U } },
        missing_eoi,
    };
    for (const std::vector<std::byte>& target : malformed) {
        openmeta::PreparedTransferC2paSigningContext sentinel;
        sentinel.handoff.binding_bytes = { std::byte { 0xA5U } };
        const openmeta::PrepareTransferC2paSigningContextResult prepared
            = openmeta::prepare_c2pa_signing_context(source.snapshot, target,
                                                     request, &sentinel);
        EXPECT_EQ(prepared.binding.status, openmeta::TransferStatus::Malformed);
        EXPECT_EQ(sentinel.handoff.binding_bytes,
                  std::vector<std::byte> { std::byte { 0xA5U } });
        EXPECT_EQ(openmeta::validate_prepared_c2pa_signing_context(context,
                                                                   target)
                      .status,
                  openmeta::TransferStatus::Malformed);
        CountingWriter writer;
        const openmeta::ExecutePreparedTransferFileResult executed
            = openmeta::execute_prepared_c2pa_signing_context(context, target,
                                                              signer, true,
                                                              &writer);
        EXPECT_EQ(executed.execute.c2pa_stage_validation.status,
                  openmeta::TransferStatus::Malformed);
        EXPECT_EQ(writer.calls, 0U);
        EXPECT_TRUE(executed.execute.edited_output.empty());
    }

    const std::array<std::byte, 4U> marker_only = {
        std::byte { 0xFFU },
        std::byte { 0xD8U },
        std::byte { 0xFFU },
        std::byte { 0xD9U },
    };
    EXPECT_EQ(openmeta::prepare_c2pa_signing_context(source.snapshot,
                                                     marker_only, request,
                                                     &context)
                  .binding.status,
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> multiple_scans = missing_eoi;
    multiple_scans.insert(
        multiple_scans.end(),
        {
            std::byte { 0xFFU }, std::byte { 0U },    std::byte { 0xFFU },
            std::byte { 0xD0U }, std::byte { 0xFFU }, std::byte { 0xDCU },
            std::byte { 0U },    std::byte { 4U },    std::byte { 0U },
            std::byte { 1U },    std::byte { 0x35U }, std::byte { 0xFFU },
            std::byte { 0xFEU }, std::byte { 0U },    std::byte { 4U },
            std::byte { 'a' },   std::byte { 'b' },   std::byte { 0xFFU },
            std::byte { 0xDAU }, std::byte { 0U },    std::byte { 8U },
            std::byte { 1U },    std::byte { 1U },    std::byte { 0U },
            std::byte { 0U },    std::byte { 0x3FU }, std::byte { 0U },
            std::byte { 0x21U }, std::byte { 0xFFU }, std::byte { 0U },
            std::byte { 0xFFU }, std::byte { 0xD9U }, std::byte { 0x31U },
        });
    EXPECT_EQ(openmeta::prepare_c2pa_signing_context(source.snapshot,
                                                     multiple_scans, request,
                                                     &context)
                  .binding.status,
              openmeta::TransferStatus::Ok);
}

}  // namespace
