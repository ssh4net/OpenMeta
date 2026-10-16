// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace {

constexpr std::array<std::byte, 14U> kPhotoshopHeader = {
    std::byte { 'P' }, std::byte { 'h' }, std::byte { 'o' }, std::byte { 't' },
    std::byte { 'o' }, std::byte { 's' }, std::byte { 'h' }, std::byte { 'o' },
    std::byte { 'p' }, std::byte { ' ' }, std::byte { '3' }, std::byte { '.' },
    std::byte { '0' }, std::byte { 0U }
};

// Each record is a literal Photoshop 8BIM fixture. Padding bytes are
// intentionally nonzero so tests can detect normalization or loss.
constexpr std::array<std::byte, 18U> kOtherPaddedNameOddData
    = { std::byte { '8' },   std::byte { 'B' },   std::byte { 'I' },
        std::byte { 'M' },   std::byte { 0x04U }, std::byte { 0x05U },
        std::byte { 2U },    std::byte { 'a' },   std::byte { 'b' },
        std::byte { 0xA1U }, std::byte { 0U },    std::byte { 0U },
        std::byte { 0U },    std::byte { 3U },    std::byte { 'X' },
        std::byte { 'Y' },   std::byte { 'Z' },   std::byte { 0xD1U } };

constexpr std::array<std::byte, 12U> kOtherDuplicateId
    = { std::byte { '8' }, std::byte { 'B' },   std::byte { 'I' },
        std::byte { 'M' }, std::byte { 0x04U }, std::byte { 0x05U },
        std::byte { 1U },  std::byte { 'q' },   std::byte { 0U },
        std::byte { 0U },  std::byte { 0U },    std::byte { 0U } };

constexpr std::array<std::byte, 14U> kIptcRecord
    = { std::byte { '8' },   std::byte { 'B' },   std::byte { 'I' },
        std::byte { 'M' },   std::byte { 0x04U }, std::byte { 0x04U },
        std::byte { 0U },    std::byte { 0xA2U }, std::byte { 0U },
        std::byte { 0U },    std::byte { 0U },    std::byte { 2U },
        std::byte { 0x1CU }, std::byte { 0x02U } };

constexpr std::array<std::byte, 14U> kIptcDuplicate
    = { std::byte { '8' },   std::byte { 'B' },   std::byte { 'I' },
        std::byte { 'M' },   std::byte { 0x04U }, std::byte { 0x04U },
        std::byte { 2U },    std::byte { 'x' },   std::byte { 'y' },
        std::byte { 0xB2U }, std::byte { 0U },    std::byte { 0U },
        std::byte { 0U },    std::byte { 0U } };

struct CountingByteWriter final : openmeta::TransferByteWriter {
    openmeta::TransferStatus
    write(std::span<const std::byte> bytes) noexcept override
    {
        calls += 1U;
        output.insert(output.end(), bytes.begin(), bytes.end());
        return openmeta::TransferStatus::Ok;
    }

    uint32_t calls = 0U;
    std::vector<std::byte> output;
};

template<size_t N>
static void
append_array(std::vector<std::byte>* output,
             const std::array<std::byte, N>& bytes)
{
    output->insert(output->end(), bytes.begin(), bytes.end());
}


static void
append_u16be(std::vector<std::byte>* output, uint16_t value)
{
    output->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    output->push_back(static_cast<std::byte>(value & 0xFFU));
}


static uint16_t
read_u16be(std::span<const std::byte> input, size_t offset)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(std::to_integer<uint8_t>(input[offset])) << 8U)
        | static_cast<uint16_t>(std::to_integer<uint8_t>(input[offset + 1U])));
}


static void
append_jpeg_segment(std::vector<std::byte>* output, uint8_t marker,
                    std::span<const std::byte> payload)
{
    output->push_back(std::byte { 0xFFU });
    output->push_back(static_cast<std::byte>(marker));
    append_u16be(output, static_cast<uint16_t>(payload.size() + 2U));
    output->insert(output->end(), payload.begin(), payload.end());
}


static std::vector<std::byte>
make_photoshop_payload(std::span<const std::byte> first_record)
{
    std::vector<std::byte> payload(kPhotoshopHeader.begin(),
                                   kPhotoshopHeader.end());
    payload.insert(payload.end(), first_record.begin(), first_record.end());
    return payload;
}


static std::vector<std::byte>
make_mixed_source_one()
{
    std::vector<std::byte> payload(kPhotoshopHeader.begin(),
                                   kPhotoshopHeader.end());
    append_array(&payload, kOtherPaddedNameOddData);
    append_array(&payload, kIptcRecord);
    append_array(&payload, kOtherDuplicateId);
    return payload;
}


static std::vector<std::byte>
make_mixed_source_two()
{
    std::vector<std::byte> payload(kPhotoshopHeader.begin(),
                                   kPhotoshopHeader.end());
    append_array(&payload, kIptcDuplicate);
    append_array(&payload, kOtherPaddedNameOddData);
    append_array(&payload, kIptcRecord);
    return payload;
}


static std::vector<std::byte>
make_jpeg_prefix(std::span<const std::byte> first_payload,
                 std::span<const std::byte> second_payload)
{
    std::vector<std::byte> input = { std::byte { 0xFFU }, std::byte { 0xD8U } };
    constexpr std::array<std::byte, 2U> app0_payload = { std::byte { 0x11U },
                                                         std::byte { 0x12U } };
    constexpr std::array<std::byte, 3U> comment_payload
        = { std::byte { 'C' }, std::byte { 'O' }, std::byte { 'M' } };
    constexpr std::array<std::byte, 2U> app2_payload = { std::byte { 0x21U },
                                                         std::byte { 0x22U } };
    append_jpeg_segment(&input, 0xE0U, app0_payload);
    append_jpeg_segment(&input, 0xEDU, first_payload);
    append_jpeg_segment(&input, 0xFEU, comment_payload);
    append_jpeg_segment(&input, 0xEDU, second_payload);
    append_jpeg_segment(&input, 0xE2U, app2_payload);
    return input;
}


static void
append_jpeg_tail(std::vector<std::byte>* output)
{
    constexpr std::array<std::byte, 8U> tail
        = { std::byte { 0xFFU }, std::byte { 0xDAU }, std::byte { 0U },
            std::byte { 2U },    std::byte { 0x31U }, std::byte { 0x32U },
            std::byte { 0xFFU }, std::byte { 0xD9U } };
    append_array(output, tail);
}


static std::vector<std::byte>
make_simple_jpeg()
{
    std::vector<std::byte> input = { std::byte { 0xFFU }, std::byte { 0xD8U } };
    append_jpeg_tail(&input);
    return input;
}


static openmeta::PreparedTransferBundle
make_iptc_bundle(std::span<const std::byte> payload)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;
    openmeta::PreparedTransferBlock iptc;
    iptc.route = "jpeg:app13-iptc";
    iptc.payload.assign(payload.begin(), payload.end());
    bundle.blocks.push_back(std::move(iptc));
    return bundle;
}


static std::vector<std::byte>
make_large_resource_payload(uint16_t resource_id, uint32_t data_size,
                            std::byte data_byte)
{
    std::vector<std::byte> payload(kPhotoshopHeader.begin(),
                                   kPhotoshopHeader.end());
    constexpr std::array<std::byte, 8U> resource_prefix
        = { std::byte { '8' }, std::byte { 'B' }, std::byte { 'I' },
            std::byte { 'M' }, std::byte { 0U },  std::byte { 0U },
            std::byte { 0U },  std::byte { 0U } };
    append_array(&payload, resource_prefix);
    payload[18U] = static_cast<std::byte>((resource_id >> 8U) & 0xFFU);
    payload[19U] = static_cast<std::byte>(resource_id & 0xFFU);
    payload.push_back(std::byte { static_cast<uint8_t>(data_size >> 24U) });
    payload.push_back(std::byte { static_cast<uint8_t>(data_size >> 16U) });
    payload.push_back(std::byte { static_cast<uint8_t>(data_size >> 8U) });
    payload.push_back(std::byte { static_cast<uint8_t>(data_size) });
    payload.insert(payload.end(), data_size, data_byte);
    return payload;
}

}  // namespace

TEST(JpegIptcResourceEdit, RewritesMixedApp13PreservingResidualsAndParity)
{
    const std::vector<std::byte> source_one = make_mixed_source_one();
    const std::vector<std::byte> source_two = make_mixed_source_two();
    const std::vector<std::byte> desired = make_photoshop_payload(kIptcRecord);
    const openmeta::PreparedTransferBundle bundle = make_iptc_bundle(desired);
    std::vector<std::byte> input = make_jpeg_prefix(source_one, source_two);
    append_jpeg_tail(&input);

    std::vector<std::byte> residual_one(kPhotoshopHeader.begin(),
                                        kPhotoshopHeader.end());
    append_array(&residual_one, kOtherPaddedNameOddData);
    append_array(&residual_one, kOtherDuplicateId);
    std::vector<std::byte> residual_two(kPhotoshopHeader.begin(),
                                        kPhotoshopHeader.end());
    append_array(&residual_two, kOtherPaddedNameOddData);

    std::vector<std::byte> expected                  = { std::byte { 0xFFU },
                                                         std::byte { 0xD8U } };
    constexpr std::array<std::byte, 2U> app0_payload = { std::byte { 0x11U },
                                                         std::byte { 0x12U } };
    constexpr std::array<std::byte, 3U> comment_payload
        = { std::byte { 'C' }, std::byte { 'O' }, std::byte { 'M' } };
    constexpr std::array<std::byte, 2U> app2_payload = { std::byte { 0x21U },
                                                         std::byte { 0x22U } };
    append_jpeg_segment(&expected, 0xE0U, app0_payload);
    append_jpeg_segment(&expected, 0xEDU, desired);
    append_jpeg_segment(&expected, 0xEDU, residual_one);
    append_jpeg_segment(&expected, 0xFEU, comment_payload);
    append_jpeg_segment(&expected, 0xEDU, residual_two);
    append_jpeg_segment(&expected, 0xE2U, app2_payload);
    append_jpeg_tail(&expected);

    const openmeta::JpegEditPlan plan
        = openmeta::plan_prepared_bundle_jpeg_edit(input, bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(plan.selected_mode, openmeta::JpegEditMode::MetadataRewrite);
    EXPECT_EQ(plan.output_size, expected.size());
    EXPECT_EQ(plan.replaced_segments, 2U);
    EXPECT_EQ(plan.emitted_segments, 3U);
    EXPECT_EQ(plan.appended_segments, 3U);

    openmeta::PlanJpegEditOptions require_in_place;
    require_in_place.require_in_place = true;
    EXPECT_EQ(openmeta::plan_prepared_bundle_jpeg_edit(input, bundle,
                                                       require_in_place)
                  .status,
              openmeta::TransferStatus::Unsupported);
    require_in_place.mode = openmeta::JpegEditMode::InPlace;
    EXPECT_EQ(openmeta::plan_prepared_bundle_jpeg_edit(input, bundle,
                                                       require_in_place)
                  .status,
              openmeta::TransferStatus::Unsupported);

    std::vector<std::byte> applied;
    const openmeta::EmitTransferResult apply
        = openmeta::apply_prepared_bundle_jpeg_edit(input, bundle, plan,
                                                    &applied);
    ASSERT_EQ(apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(apply.emitted, 3U);
    EXPECT_EQ(apply.skipped, 2U);
    EXPECT_EQ(applied, expected);

    CountingByteWriter direct_writer;
    const openmeta::EmitTransferResult written
        = openmeta::write_prepared_bundle_jpeg_edit(input, bundle, plan,
                                                    direct_writer);
    ASSERT_EQ(written.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(written.emitted, 3U);
    EXPECT_EQ(written.skipped, 2U);
    EXPECT_EQ(direct_writer.output, expected);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_bundle_jpeg_package(input, bundle, plan,
                                                       &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(package.output_size, expected.size());
    CountingByteWriter package_writer;
    ASSERT_EQ(openmeta::write_prepared_transfer_package(input, bundle, package,
                                                        package_writer)
                  .status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(package_writer.output, expected);

    openmeta::PreparedTransferPackageBatch batch;
    ASSERT_EQ(openmeta::build_prepared_transfer_package_batch(input, bundle,
                                                              package, &batch)
                  .status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(batch.output_size, expected.size());
    CountingByteWriter batch_writer;
    ASSERT_EQ(openmeta::write_prepared_transfer_package_batch(batch,
                                                              batch_writer)
                  .status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(batch_writer.output, expected);
}


TEST(JpegIptcResourceEdit,
     PreservesPurePhotoshopAndRewritesDuplicateOldIptcSegments)
{
    const std::vector<std::byte> desired = make_photoshop_payload(kIptcRecord);
    openmeta::PreparedTransferBundle bundle = make_iptc_bundle(desired);
    bundle.blocks[0].payload.back()         = std::byte { 0x19U };
    std::vector<std::byte> pure_photoshop(kPhotoshopHeader.begin(),
                                          kPhotoshopHeader.end());
    append_array(&pure_photoshop, kOtherPaddedNameOddData);
    append_array(&pure_photoshop, kOtherDuplicateId);

    std::vector<std::byte> input = { std::byte { 0xFFU }, std::byte { 0xD8U } };
    append_jpeg_segment(&input, 0xEDU, pure_photoshop);
    append_jpeg_segment(&input, 0xEDU, desired);
    constexpr std::array<std::byte, 2U> tail = { std::byte { 0xFFU },
                                                 std::byte { 0xD9U } };
    append_array(&input, tail);

    const openmeta::JpegEditPlan exact_plan
        = openmeta::plan_prepared_bundle_jpeg_edit(input, bundle);
    ASSERT_EQ(exact_plan.status, openmeta::TransferStatus::Ok);
    EXPECT_TRUE(exact_plan.in_place_possible);
    EXPECT_EQ(exact_plan.selected_mode, openmeta::JpegEditMode::InPlace);
    EXPECT_EQ(exact_plan.output_size, input.size());
    std::vector<std::byte> exact_output;
    ASSERT_EQ(openmeta::apply_prepared_bundle_jpeg_edit(input, bundle,
                                                        exact_plan,
                                                        &exact_output)
                  .status,
              openmeta::TransferStatus::Ok);
    std::vector<std::byte> expected_exact = input;
    expected_exact[2U + 4U + pure_photoshop.size() + 4U + desired.size() - 1U]
        = std::byte { 0x19U };
    EXPECT_EQ(exact_output, expected_exact);

    std::vector<std::byte> duplicates = { std::byte { 0xFFU },
                                          std::byte { 0xD8U } };
    append_jpeg_segment(&duplicates, 0xEDU, pure_photoshop);
    append_jpeg_segment(&duplicates, 0xEDU, desired);
    append_jpeg_segment(&duplicates, 0xEDU, desired);
    append_array(&duplicates, tail);

    const openmeta::JpegEditPlan duplicate_plan
        = openmeta::plan_prepared_bundle_jpeg_edit(duplicates, bundle);
    ASSERT_EQ(duplicate_plan.status, openmeta::TransferStatus::Ok);
    EXPECT_FALSE(duplicate_plan.in_place_possible);
    EXPECT_EQ(duplicate_plan.selected_mode,
              openmeta::JpegEditMode::MetadataRewrite);
    EXPECT_EQ(duplicate_plan.removed_existing_segments, 2U);
    EXPECT_EQ(duplicate_plan.emitted_segments, 1U);

    std::vector<std::byte> expected = { std::byte { 0xFFU },
                                        std::byte { 0xD8U } };
    append_jpeg_segment(&expected, 0xEDU, pure_photoshop);
    append_jpeg_segment(&expected, 0xEDU, bundle.blocks[0].payload);
    append_array(&expected, tail);
    std::vector<std::byte> duplicate_output;
    ASSERT_EQ(openmeta::apply_prepared_bundle_jpeg_edit(duplicates, bundle,
                                                        duplicate_plan,
                                                        &duplicate_output)
                  .status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(duplicate_output, expected);
}


TEST(JpegIptcResourceEdit, RejectsMalformedResourcesAndLeavesOutputsUntouched)
{
    const std::vector<std::byte> desired = make_photoshop_payload(kIptcRecord);
    const openmeta::PreparedTransferBundle bundle = make_iptc_bundle(desired);
    const std::vector<std::byte> source_one       = make_mixed_source_one();
    const std::vector<std::byte> source_two       = make_mixed_source_two();
    std::vector<std::byte> valid_input            = make_jpeg_prefix(source_one,
                                                                     source_two);
    append_jpeg_tail(&valid_input);

    std::vector<std::byte> malformed_second = source_two;
    malformed_second[14U]                   = std::byte { 'X' };
    std::vector<std::byte> malformed_input  = make_jpeg_prefix(source_one,
                                                               malformed_second);
    append_jpeg_tail(&malformed_input);

    const openmeta::JpegEditPlan good_plan
        = openmeta::plan_prepared_bundle_jpeg_edit(valid_input, bundle);
    ASSERT_EQ(good_plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(openmeta::plan_prepared_bundle_jpeg_edit(malformed_input, bundle)
                  .status,
              openmeta::TransferStatus::Malformed);

    const std::array<std::byte, 1U> malformed_desired_payload
        = { std::byte { 'X' } };
    const openmeta::PreparedTransferBundle malformed_bundle = make_iptc_bundle(
        malformed_desired_payload);
    EXPECT_EQ(openmeta::plan_prepared_bundle_jpeg_edit(valid_input,
                                                       malformed_bundle)
                  .status,
              openmeta::TransferStatus::Malformed);

    std::vector<std::byte> mixed_desired_payload = make_photoshop_payload(
        kIptcRecord);
    append_array(&mixed_desired_payload, kOtherDuplicateId);
    const openmeta::PreparedTransferBundle mixed_bundle = make_iptc_bundle(
        mixed_desired_payload);
    EXPECT_EQ(openmeta::plan_prepared_bundle_jpeg_edit(valid_input, mixed_bundle)
                  .status,
              openmeta::TransferStatus::Unsupported);

    std::vector<std::byte> output_sentinel
        = { std::byte { 's' }, std::byte { 'e' }, std::byte { 'n' } };
    const std::vector<std::byte> original_sentinel = output_sentinel;
    const openmeta::EmitTransferResult apply
        = openmeta::apply_prepared_bundle_jpeg_edit(malformed_input, bundle,
                                                    good_plan,
                                                    &output_sentinel);
    EXPECT_EQ(apply.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(output_sentinel, original_sentinel);

    openmeta::PreparedTransferPackagePlan package_sentinel;
    package_sentinel.input_size  = 17U;
    package_sentinel.output_size = 19U;
    openmeta::PreparedTransferPackageChunk sentinel_chunk;
    sentinel_chunk.output_offset = 23U;
    sentinel_chunk.inline_bytes  = { std::byte { 'p' } };
    package_sentinel.chunks.push_back(sentinel_chunk);
    const std::vector<std::byte> original_inline
        = package_sentinel.chunks[0].inline_bytes;
    const openmeta::EmitTransferResult package_result
        = openmeta::build_prepared_bundle_jpeg_package(malformed_input, bundle,
                                                       good_plan,
                                                       &package_sentinel);
    EXPECT_EQ(package_result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(package_sentinel.input_size, 17U);
    EXPECT_EQ(package_sentinel.output_size, 19U);
    ASSERT_EQ(package_sentinel.chunks.size(), 1U);
    EXPECT_EQ(package_sentinel.chunks[0].output_offset, 23U);
    EXPECT_EQ(package_sentinel.chunks[0].inline_bytes, original_inline);

    CountingByteWriter writer;
    const openmeta::EmitTransferResult write_result
        = openmeta::write_prepared_bundle_jpeg_edit(malformed_input, bundle,
                                                    good_plan, writer);
    EXPECT_EQ(write_result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(writer.calls, 0U);

    std::vector<std::byte> opaque_malformed_payload(kPhotoshopHeader.begin(),
                                                    kPhotoshopHeader.end());
    opaque_malformed_payload.push_back(std::byte { 'b' });
    std::vector<std::byte> no_update_input = { std::byte { 0xFFU },
                                               std::byte { 0xD8U } };
    append_jpeg_segment(&no_update_input, 0xEDU, opaque_malformed_payload);
    constexpr std::array<std::byte, 2U> eoi = { std::byte { 0xFFU },
                                                std::byte { 0xD9U } };
    append_array(&no_update_input, eoi);
    openmeta::PreparedTransferBundle no_update_bundle;
    no_update_bundle.target_format = openmeta::TransferTargetFormat::Jpeg;
    const openmeta::JpegEditPlan no_update_plan
        = openmeta::plan_prepared_bundle_jpeg_edit(no_update_input,
                                                   no_update_bundle);
    ASSERT_EQ(no_update_plan.status, openmeta::TransferStatus::Ok);
    std::vector<std::byte> no_update_output;
    ASSERT_EQ(openmeta::apply_prepared_bundle_jpeg_edit(no_update_input,
                                                        no_update_bundle,
                                                        no_update_plan,
                                                        &no_update_output)
                  .status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(no_update_output, no_update_input);
}


TEST(JpegIptcResourceEdit, RejectsTamperedPlanAndSupportsInputOutputAlias)
{
    const std::vector<std::byte> desired = make_photoshop_payload(kIptcRecord);
    const openmeta::PreparedTransferBundle bundle = make_iptc_bundle(desired);
    const std::vector<std::byte> input            = make_simple_jpeg();
    const openmeta::JpegEditPlan plan
        = openmeta::plan_prepared_bundle_jpeg_edit(input, bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    openmeta::JpegEditPlan tampered = plan;
    tampered.output_size            = std::numeric_limits<uint64_t>::max();
    std::vector<std::byte> output   = { std::byte { 'x' }, std::byte { 'y' } };
    const std::vector<std::byte> sentinel = output;
    const openmeta::EmitTransferResult rejected
        = openmeta::apply_prepared_bundle_jpeg_edit(input, bundle, tampered,
                                                    &output);
    EXPECT_EQ(rejected.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(output, sentinel);

    std::vector<std::byte> expected;
    ASSERT_EQ(openmeta::apply_prepared_bundle_jpeg_edit(input, bundle, plan,
                                                        &expected)
                  .status,
              openmeta::TransferStatus::Ok);
    std::vector<std::byte> aliased = input;
    const std::span<const std::byte> input_alias(aliased.data(),
                                                 aliased.size());
    const openmeta::EmitTransferResult alias_result
        = openmeta::apply_prepared_bundle_jpeg_edit(input_alias, bundle, plan,
                                                    &aliased);
    EXPECT_EQ(alias_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(aliased, expected);
}


TEST(JpegIptcResourceEdit, KeepsNearCeilingIptcAndPhotoshopResidualSeparate)
{
    std::vector<std::byte> source(kPhotoshopHeader.begin(),
                                  kPhotoshopHeader.end());
    constexpr uint32_t kSourceOtherDataSize = 65494U;
    constexpr std::array<std::byte, 8U> other_prefix
        = { std::byte { '8' }, std::byte { 'B' },   std::byte { 'I' },
            std::byte { 'M' }, std::byte { 0x77U }, std::byte { 0x77U },
            std::byte { 0U },  std::byte { 0U } };
    append_array(&source, other_prefix);
    source.push_back(std::byte { 0U });
    source.push_back(std::byte { 0U });
    source.push_back(
        std::byte { static_cast<uint8_t>(kSourceOtherDataSize >> 8U) });
    source.push_back(std::byte { static_cast<uint8_t>(kSourceOtherDataSize) });
    source.insert(source.end(), kSourceOtherDataSize, std::byte { 0x61U });
    append_array(&source,
                 std::array<std::byte, 12U> {
                     std::byte { '8' }, std::byte { 'B' }, std::byte { 'I' },
                     std::byte { 'M' }, std::byte { 0x04U },
                     std::byte { 0x04U }, std::byte { 0U }, std::byte { 0U },
                     std::byte { 0U }, std::byte { 0U }, std::byte { 0U },
                     std::byte { 0U } });
    ASSERT_EQ(source.size(), 65532U);

    const std::vector<std::byte> desired
        = make_large_resource_payload(0x0404U, 65506U, std::byte { 0x62U });
    ASSERT_EQ(desired.size(), 65532U);
    const openmeta::PreparedTransferBundle bundle = make_iptc_bundle(desired);

    std::vector<std::byte> expected_residual(kPhotoshopHeader.begin(),
                                             kPhotoshopHeader.end());
    expected_residual.insert(expected_residual.end(), source.begin() + 14,
                             source.begin() + 14 + 12U + kSourceOtherDataSize);

    std::vector<std::byte> input = { std::byte { 0xFFU }, std::byte { 0xD8U } };
    append_jpeg_segment(&input, 0xEDU, source);
    constexpr std::array<std::byte, 2U> eoi = { std::byte { 0xFFU },
                                                std::byte { 0xD9U } };
    append_array(&input, eoi);

    std::vector<std::byte> expected = { std::byte { 0xFFU },
                                        std::byte { 0xD8U } };
    append_jpeg_segment(&expected, 0xEDU, desired);
    append_jpeg_segment(&expected, 0xEDU, expected_residual);
    append_array(&expected, eoi);

    const openmeta::JpegEditPlan plan
        = openmeta::plan_prepared_bundle_jpeg_edit(input, bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(plan.selected_mode, openmeta::JpegEditMode::MetadataRewrite);
    EXPECT_EQ(plan.emitted_segments, 2U);
    EXPECT_EQ(plan.output_size, expected.size());

    std::vector<std::byte> output;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_jpeg_edit(input, bundle, plan,
                                                    &output);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(applied.emitted, 2U);
    EXPECT_EQ(applied.skipped, 1U);
    EXPECT_EQ(output, expected);

    const size_t first_payload_offset = 2U + 4U;
    ASSERT_EQ(output[2U], std::byte { 0xFFU });
    ASSERT_EQ(output[3U], std::byte { 0xEDU });
    const size_t first_payload_size
        = static_cast<size_t>(read_u16be(output, 4U)) - 2U;
    EXPECT_EQ(first_payload_size, desired.size());
    EXPECT_LE(first_payload_size, 65533U);

    const size_t second_marker_offset = first_payload_offset
                                        + first_payload_size;
    ASSERT_EQ(output[second_marker_offset], std::byte { 0xFFU });
    ASSERT_EQ(output[second_marker_offset + 1U], std::byte { 0xEDU });
    const size_t second_payload_size
        = static_cast<size_t>(read_u16be(output, second_marker_offset + 2U))
          - 2U;
    EXPECT_EQ(second_payload_size, expected_residual.size());
    EXPECT_LE(second_payload_size, 65533U);
}
