// SPDX-License-Identifier: Apache-2.0

#include "openmeta/prepared_transfer_handoff.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

static openmeta::TransferSourceSnapshot
make_source_snapshot()
{
    openmeta::TransferSourceSnapshot snapshot;
    const openmeta::BlockId block = snapshot.store.add_block(
        openmeta::BlockInfo {});

    openmeta::Entry make;
    make.key   = openmeta::make_exif_tag_key(snapshot.store.arena(), "ifd0",
                                             0x010FU);
    make.value = openmeta::make_text(snapshot.store.arena(), "Vendor",
                                     openmeta::TextEncoding::Ascii);
    make.origin.block          = block;
    make.origin.order_in_block = 0U;
    snapshot.store.add_entry(make);
    snapshot.store.finalize();
    return snapshot;
}

static openmeta::PrepareTransferRequest
make_request(openmeta::TransferTargetFormat target)
{
    openmeta::PrepareTransferRequest request;
    request.target_format      = target;
    request.include_exif_app1  = true;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    return request;
}

struct ReplayProbe final {
    uint32_t calls = 0U;
    openmeta::TransferAdapterOpKind first_kind
        = openmeta::TransferAdapterOpKind::JpegMarker;
    uint8_t first_marker                  = 0U;
    const std::byte* first_payload        = nullptr;
    uint64_t first_payload_size           = 0U;
    openmeta::TransferStatus return_value = openmeta::TransferStatus::Ok;
};

static openmeta::TransferStatus
replay_probe(
    void* user,
    const openmeta::PreparedTransferHandoffOperationView* operation) noexcept
{
    ReplayProbe* probe = static_cast<ReplayProbe*>(user);
    if (!probe || !operation) {
        return openmeta::TransferStatus::InvalidArgument;
    }
    if (probe->calls == 0U) {
        probe->first_kind         = operation->operation.kind;
        probe->first_marker       = operation->operation.jpeg_marker_code;
        probe->first_payload      = operation->payload.data();
        probe->first_payload_size = operation->payload.size();
    }
    probe->calls += 1U;
    return probe->return_value;
}

}  // namespace

TEST(PreparedTransferHandoff, ReportsStableOpaqueContract)
{
    static_assert(
        !std::is_copy_constructible_v<openmeta::PreparedTransferHandoff>);
    static_assert(
        std::is_nothrow_move_constructible_v<openmeta::PreparedTransferHandoff>);
    static_assert(
        std::is_trivially_copyable_v<openmeta::PreparedTransferHandoffResult>);
    static_assert(sizeof(openmeta::PreparedTransferHandoff) == sizeof(void*));

    EXPECT_EQ(openmeta::prepared_transfer_handoff_contract_version(),
              openmeta::kPreparedTransferHandoffContractVersion);
    EXPECT_EQ(openmeta::prepared_transfer_handoff_code_name(
                  openmeta::PreparedTransferHandoffCode::NullOutput),
              "null_output");
    EXPECT_FALSE(openmeta::prepared_transfer_handoff_code_message(
                     openmeta::PreparedTransferHandoffCode::NullOutput)
                     .empty());

    openmeta::PreparedTransferHandoff handoff;
    EXPECT_FALSE(handoff.valid());
    EXPECT_EQ(handoff.operation_count(), 0U);
}

TEST(PreparedTransferHandoff, PreparesAndReplaysWithoutRecompilation)
{
    const openmeta::TransferSourceSnapshot snapshot = make_source_snapshot();
    const openmeta::PrepareTransferRequest request  = make_request(
        openmeta::TransferTargetFormat::Jpeg);

    const openmeta::PreparedTransferHandoffResult null_output
        = openmeta::prepare_transfer_handoff(snapshot, request,
                                             openmeta::EmitTransferOptions {},
                                             nullptr);
    EXPECT_EQ(null_output.code,
              openmeta::PreparedTransferHandoffCode::NullOutput);

    openmeta::PreparedTransferHandoff handoff;
    const openmeta::PreparedTransferHandoffResult prepared
        = openmeta::prepare_transfer_handoff(snapshot, request,
                                             openmeta::EmitTransferOptions {},
                                             &handoff);
    ASSERT_TRUE(prepared.ok());
    ASSERT_TRUE(handoff.valid());
    ASSERT_EQ(prepared.operation_count, handoff.operation_count());
    ASSERT_GT(handoff.operation_count(), 0U);
    EXPECT_EQ(handoff.target_format(), openmeta::TransferTargetFormat::Jpeg);

    openmeta::PreparedTransferHandoffOperationView operation;
    const openmeta::PreparedTransferHandoffResult resolved
        = openmeta::prepared_transfer_handoff_operation(handoff, 0U,
                                                        &operation);
    ASSERT_TRUE(resolved.ok());
    EXPECT_EQ(operation.semantic_kind, openmeta::TransferSemanticKind::Exif);
    EXPECT_EQ(operation.operation.kind,
              openmeta::TransferAdapterOpKind::JpegMarker);
    EXPECT_EQ(operation.operation.jpeg_marker_code, 0xE1U);
    EXPECT_EQ(operation.payload.size(), operation.operation.payload_size);
    ASSERT_FALSE(operation.payload.empty());

    ReplayProbe first;
    const openmeta::PreparedTransferHandoffResult replayed_first
        = openmeta::replay_prepared_transfer_handoff(handoff, replay_probe,
                                                     &first);
    ASSERT_TRUE(replayed_first.ok());
    EXPECT_EQ(replayed_first.operation_count, handoff.operation_count());
    EXPECT_EQ(first.calls, handoff.operation_count());

    ReplayProbe second;
    const openmeta::PreparedTransferHandoffResult replayed_second
        = openmeta::replay_prepared_transfer_handoff(handoff, replay_probe,
                                                     &second);
    ASSERT_TRUE(replayed_second.ok());
    EXPECT_EQ(second.calls, first.calls);
    EXPECT_EQ(second.first_kind, first.first_kind);
    EXPECT_EQ(second.first_marker, first.first_marker);
    EXPECT_EQ(second.first_payload, first.first_payload);
    EXPECT_EQ(second.first_payload_size, first.first_payload_size);
}

TEST(PreparedTransferHandoff, ResolvesTypedExrAttributeWithoutRoutes)
{
    const openmeta::TransferSourceSnapshot snapshot = make_source_snapshot();
    const openmeta::PrepareTransferRequest request  = make_request(
        openmeta::TransferTargetFormat::Exr);

    openmeta::PreparedTransferHandoff handoff;
    const openmeta::PreparedTransferHandoffResult prepared
        = openmeta::prepare_transfer_handoff(snapshot, request,
                                             openmeta::EmitTransferOptions {},
                                             &handoff);
    ASSERT_TRUE(prepared.ok());
    ASSERT_GT(handoff.operation_count(), 0U);

    openmeta::PreparedTransferHandoffOperationView operation;
    const openmeta::PreparedTransferHandoffResult resolved
        = openmeta::prepared_transfer_handoff_operation(handoff, 0U,
                                                        &operation);
    ASSERT_TRUE(resolved.ok());
    EXPECT_EQ(operation.operation.kind,
              openmeta::TransferAdapterOpKind::ExrAttribute);
    EXPECT_EQ(operation.exr_name, "Make");
    EXPECT_EQ(operation.exr_type_name, "string");
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                   operation.exr_value.data()),
                               operation.exr_value.size()),
              "Vendor");
    EXPECT_FALSE(operation.exr_is_opaque);
}

TEST(PreparedTransferHandoff, CoversEveryTypedTargetFamily)
{
    struct TargetCase final {
        openmeta::TransferTargetFormat target;
        openmeta::TransferAdapterOpKind expected_kind;
    };
    constexpr std::array<TargetCase, 10> cases = { {
        { openmeta::TransferTargetFormat::Jpeg,
          openmeta::TransferAdapterOpKind::JpegMarker },
        { openmeta::TransferTargetFormat::Tiff,
          openmeta::TransferAdapterOpKind::TiffTagBytes },
        { openmeta::TransferTargetFormat::Dng,
          openmeta::TransferAdapterOpKind::TiffTagBytes },
        { openmeta::TransferTargetFormat::Jxl,
          openmeta::TransferAdapterOpKind::JxlBox },
        { openmeta::TransferTargetFormat::Webp,
          openmeta::TransferAdapterOpKind::WebpChunk },
        { openmeta::TransferTargetFormat::Png,
          openmeta::TransferAdapterOpKind::PngChunk },
        { openmeta::TransferTargetFormat::Jp2,
          openmeta::TransferAdapterOpKind::Jp2Box },
        { openmeta::TransferTargetFormat::Heif,
          openmeta::TransferAdapterOpKind::BmffItem },
        { openmeta::TransferTargetFormat::Avif,
          openmeta::TransferAdapterOpKind::BmffItem },
        { openmeta::TransferTargetFormat::Cr3,
          openmeta::TransferAdapterOpKind::BmffItem },
    } };

    const openmeta::TransferSourceSnapshot snapshot = make_source_snapshot();
    for (const TargetCase& target_case : cases) {
        const openmeta::PrepareTransferRequest request = make_request(
            target_case.target);
        openmeta::PreparedTransferHandoff handoff;
        const openmeta::PreparedTransferHandoffResult prepared
            = openmeta::prepare_transfer_handoff(
                snapshot, request, openmeta::EmitTransferOptions {}, &handoff);
        ASSERT_TRUE(prepared.ok())
            << static_cast<uint32_t>(target_case.target) << " "
            << openmeta::prepared_transfer_handoff_code_name(prepared.code);
        ASSERT_GT(handoff.operation_count(), 0U);

        openmeta::PreparedTransferHandoffOperationView operation;
        ASSERT_TRUE(openmeta::prepared_transfer_handoff_operation(handoff, 0U,
                                                                  &operation)
                        .ok());
        EXPECT_EQ(operation.operation.kind, target_case.expected_kind);
        EXPECT_EQ(operation.semantic_kind,
                  openmeta::TransferSemanticKind::Exif);
    }
}

TEST(PreparedTransferHandoff, FailedPreparationIsTransactional)
{
    const openmeta::TransferSourceSnapshot snapshot = make_source_snapshot();
    openmeta::PrepareTransferRequest request        = make_request(
        openmeta::TransferTargetFormat::Jpeg);

    openmeta::PreparedTransferHandoff handoff;
    ASSERT_TRUE(
        openmeta::prepare_transfer_handoff(snapshot, request,
                                           openmeta::EmitTransferOptions {},
                                           &handoff)
            .ok());
    const uint32_t original_count = handoff.operation_count();

    request.target_format = openmeta::TransferTargetFormat::Webp;
    request.raw_carrier_passthrough_mode
        = openmeta::TransferRawCarrierPassthroughMode::WhenSafe;
    const openmeta::PreparedTransferHandoffResult rejected
        = openmeta::prepare_transfer_handoff(snapshot, request,
                                             openmeta::EmitTransferOptions {},
                                             &handoff);
    EXPECT_EQ(rejected.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(
        rejected.code,
        openmeta::PreparedTransferHandoffCode::RawCarrierPassthroughUnsupported);
    EXPECT_TRUE(handoff.valid());
    EXPECT_EQ(handoff.target_format(), openmeta::TransferTargetFormat::Jpeg);
    EXPECT_EQ(handoff.operation_count(), original_count);
}

TEST(PreparedTransferHandoff, MoveAndErrorsPreserveOwnership)
{
    const openmeta::TransferSourceSnapshot snapshot = make_source_snapshot();
    const openmeta::PrepareTransferRequest request  = make_request(
        openmeta::TransferTargetFormat::Jpeg);

    openmeta::PreparedTransferHandoff source;
    ASSERT_TRUE(
        openmeta::prepare_transfer_handoff(snapshot, request,
                                           openmeta::EmitTransferOptions {},
                                           &source)
            .ok());
    const uint32_t operation_count = source.operation_count();

    openmeta::PreparedTransferHandoff moved(std::move(source));
    EXPECT_FALSE(source.valid());
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.operation_count(), operation_count);

    EXPECT_EQ(
        openmeta::prepared_transfer_handoff_operation(moved, 0U, nullptr).code,
        openmeta::PreparedTransferHandoffCode::NullOperationView);
    EXPECT_EQ(
        openmeta::replay_prepared_transfer_handoff(moved, nullptr, nullptr).code,
        openmeta::PreparedTransferHandoffCode::NullReplayCallback);

    openmeta::PreparedTransferHandoffOperationView operation;
    const openmeta::PreparedTransferHandoffResult out_of_range
        = openmeta::prepared_transfer_handoff_operation(moved, operation_count,
                                                        &operation);
    EXPECT_EQ(out_of_range.code,
              openmeta::PreparedTransferHandoffCode::OperationIndexOutOfRange);

    ReplayProbe probe;
    probe.return_value = openmeta::TransferStatus::InternalError;
    const openmeta::PreparedTransferHandoffResult callback_failed
        = openmeta::replay_prepared_transfer_handoff(moved, replay_probe,
                                                     &probe);
    EXPECT_EQ(callback_failed.code,
              openmeta::PreparedTransferHandoffCode::ReplayCallbackFailed);
    EXPECT_EQ(callback_failed.failed_operation_index, 0U);

    moved.reset();
    EXPECT_FALSE(moved.valid());
}
