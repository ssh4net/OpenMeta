// SPDX-License-Identifier: Apache-2.0

#include "openmeta/prepared_transfer_handoff.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

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

    openmeta::Entry date_time;
    date_time.key = openmeta::make_exif_tag_key(snapshot.store.arena(), "ifd0",
                                                0x0132U);
    date_time.value        = openmeta::make_text(snapshot.store.arena(),
                                                 "2024:01:02 03:04:05",
                                                 openmeta::TextEncoding::Ascii);
    date_time.origin.block = block;
    date_time.origin.order_in_block = 1U;
    snapshot.store.add_entry(date_time);
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

static std::span<const std::byte>
text_bytes(std::string_view text) noexcept
{
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                          text.data()),
                                      text.size());
}

static std::array<std::byte, 20>
exif_date_time_bytes(std::string_view text) noexcept
{
    std::array<std::byte, 20> bytes {};
    if (text.size() == 19U) {
        std::memcpy(bytes.data(), text.data(), text.size());
    }
    return bytes;
}

static std::string
payload_string(const openmeta::PreparedTransferHandoffOperationView& operation)
{
    return std::string(reinterpret_cast<const char*>(operation.payload.data()),
                       operation.payload.size());
}

static const std::byte*
find_payload_text(
    const openmeta::PreparedTransferHandoffOperationView& operation,
    std::string_view needle) noexcept
{
    if (needle.empty() || needle.size() > operation.payload.size()) {
        return nullptr;
    }
    for (size_t i = 0U; i <= operation.payload.size() - needle.size(); ++i) {
        if (std::memcmp(operation.payload.data() + i, needle.data(),
                        needle.size())
            == 0) {
            return operation.payload.data() + i;
        }
    }
    return nullptr;
}

struct InstanceWorker final {
    openmeta::PreparedTransferHandoffInstance* instance = nullptr;
    std::array<std::byte, 20> value;
    std::string_view visible_value;
    openmeta::PreparedTransferHandoffPatchResult patch;
    ReplayProbe replay;
};

static void
run_instance_worker(InstanceWorker* worker) noexcept
{
    if (!worker || !worker->instance) {
        return;
    }
    const openmeta::TimePatchView update {
        openmeta::TimePatchField::DateTime,
        std::span<const std::byte>(worker->value.data(), worker->value.size())
    };
    const std::array<openmeta::TimePatchView, 1> updates = { update };
    for (uint32_t i = 0U; i < 1000U; ++i) {
        worker->patch = openmeta::patch_prepared_transfer_handoff_instance(
            worker->instance, updates);
        if (!worker->patch.ok()) {
            return;
        }
    }
    openmeta::replay_prepared_transfer_handoff_instance(*worker->instance,
                                                        replay_probe,
                                                        &worker->replay);
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
    static_assert(
        !std::is_copy_constructible_v<openmeta::PreparedTransferHandoffInstance>);
    static_assert(std::is_nothrow_move_constructible_v<
                  openmeta::PreparedTransferHandoffInstance>);
    static_assert(std::is_trivially_copyable_v<
                  openmeta::PreparedTransferHandoffPatchResult>);
    static_assert(std::is_trivially_copyable_v<
                  openmeta::PreparedTransferHandoffTimePatchFieldView>);
    static_assert(sizeof(openmeta::PreparedTransferHandoff) == sizeof(void*));
    static_assert(sizeof(openmeta::PreparedTransferHandoffInstance)
                  == sizeof(void*));

    EXPECT_EQ(openmeta::prepared_transfer_handoff_contract_version(),
              openmeta::kPreparedTransferHandoffContractVersion);
    EXPECT_EQ(openmeta::prepared_transfer_handoff_code_name(
                  openmeta::PreparedTransferHandoffCode::NullOutput),
              "null_output");
    EXPECT_FALSE(openmeta::prepared_transfer_handoff_code_message(
                     openmeta::PreparedTransferHandoffCode::NullOutput)
                     .empty());
    EXPECT_EQ(openmeta::prepared_transfer_handoff_instance_contract_version(),
              openmeta::kPreparedTransferHandoffInstanceContractVersion);
    EXPECT_EQ(openmeta::prepared_transfer_handoff_patch_code_name(
                  openmeta::PreparedTransferHandoffPatchCode::WidthMismatch),
              "width_mismatch");
    EXPECT_FALSE(openmeta::prepared_transfer_handoff_patch_code_message(
                     openmeta::PreparedTransferHandoffPatchCode::WidthMismatch)
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

    openmeta::PreparedTransferHandoffInstance instance;
    ASSERT_TRUE(
        openmeta::create_prepared_transfer_handoff_instance(handoff, &instance)
            .ok());
    openmeta::PreparedTransferHandoffOperationView instance_operation;
    ASSERT_TRUE(openmeta::prepared_transfer_handoff_instance_operation(
                    instance, 0U, &instance_operation)
                    .ok());
    EXPECT_EQ(instance_operation.exr_name, "Make");
    EXPECT_EQ(instance_operation.exr_type_name, "string");
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                   instance_operation.exr_value.data()),
                               instance_operation.exr_value.size()),
              "Vendor");
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

        openmeta::PreparedTransferHandoffInstance instance;
        const openmeta::PreparedTransferHandoffResult cloned
            = openmeta::create_prepared_transfer_handoff_instance(handoff,
                                                                  &instance);
        ASSERT_TRUE(cloned.ok());
        EXPECT_EQ(instance.target_format(), target_case.target);
        EXPECT_EQ(instance.operation_count(), handoff.operation_count());

        openmeta::PreparedTransferHandoffOperationView instance_operation;
        ASSERT_TRUE(openmeta::prepared_transfer_handoff_instance_operation(
                        instance, 0U, &instance_operation)
                        .ok());
        EXPECT_EQ(instance_operation.operation.kind, target_case.expected_kind);
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

TEST(PreparedTransferHandoff, WorkerInstancesAreIndependentAndOwnTheirState)
{
    const openmeta::TransferSourceSnapshot snapshot = make_source_snapshot();
    const openmeta::PrepareTransferRequest request  = make_request(
        openmeta::TransferTargetFormat::Jpeg);

    openmeta::PreparedTransferHandoff handoff;
    ASSERT_TRUE(
        openmeta::prepare_transfer_handoff(snapshot, request,
                                           openmeta::EmitTransferOptions {},
                                           &handoff)
            .ok());

    openmeta::PreparedTransferHandoffOperationView template_operation;
    ASSERT_TRUE(
        openmeta::prepared_transfer_handoff_operation(handoff, 0U,
                                                      &template_operation)
            .ok());
    ASSERT_NE(find_payload_text(template_operation, "2024:01:02 03:04:05"),
              nullptr);

    openmeta::PreparedTransferHandoffInstance first;
    openmeta::PreparedTransferHandoffInstance second;
    ASSERT_TRUE(
        openmeta::create_prepared_transfer_handoff_instance(handoff, &first)
            .ok());
    ASSERT_TRUE(
        openmeta::create_prepared_transfer_handoff_instance(handoff, &second)
            .ok());
    EXPECT_EQ(openmeta::create_prepared_transfer_handoff_instance(handoff,
                                                                  nullptr)
                  .code,
              openmeta::PreparedTransferHandoffCode::NullOutput);
    EXPECT_EQ(first.time_patch_slot_count(openmeta::TimePatchField::DateTime),
              1U);
    EXPECT_EQ(first.time_patch_slot_count(
                  static_cast<openmeta::TimePatchField>(0xFFU)),
              0U);

    openmeta::PreparedTransferHandoffTimePatchFieldView field_view;
    ASSERT_TRUE(openmeta::prepared_transfer_handoff_instance_time_patch_field(
                    first, openmeta::TimePatchField::DateTime, &field_view)
                    .ok());
    EXPECT_EQ(field_view.field, openmeta::TimePatchField::DateTime);
    EXPECT_EQ(field_view.width, 20U);
    EXPECT_EQ(field_view.slot_count, 1U);
    EXPECT_EQ(openmeta::prepared_transfer_handoff_instance_time_patch_field(
                  first, openmeta::TimePatchField::DateTime, nullptr)
                  .code,
              openmeta::PreparedTransferHandoffPatchCode::NullFieldView);
    EXPECT_EQ(openmeta::prepared_transfer_handoff_instance_time_patch_field(
                  first, openmeta::TimePatchField::GpsDateStamp, &field_view)
                  .code,
              openmeta::PreparedTransferHandoffPatchCode::SlotNotFound);

    openmeta::PreparedTransferHandoff invalid_template;
    const uint32_t first_operation_count = first.operation_count();
    EXPECT_EQ(openmeta::create_prepared_transfer_handoff_instance(
                  invalid_template, &first)
                  .code,
              openmeta::PreparedTransferHandoffCode::InvalidState);
    EXPECT_TRUE(first.valid());
    EXPECT_EQ(first.operation_count(), first_operation_count);

    handoff.reset();
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    constexpr std::string_view first_value      = "2025:06:07 08:09:10";
    const std::array<std::byte, 20> first_bytes = exif_date_time_bytes(
        first_value);
    const openmeta::TimePatchView update {
        openmeta::TimePatchField::DateTime,
        std::span<const std::byte>(first_bytes.data(), first_bytes.size())
    };
    const std::array<openmeta::TimePatchView, 1> updates = { update };
    const openmeta::PreparedTransferHandoffPatchResult patched
        = openmeta::patch_prepared_transfer_handoff_instance(&first, updates);
    ASSERT_TRUE(patched.ok());
    EXPECT_EQ(patched.patched_slots, 1U);

    openmeta::PreparedTransferHandoffOperationView first_operation;
    openmeta::PreparedTransferHandoffOperationView second_operation;
    ASSERT_TRUE(
        openmeta::prepared_transfer_handoff_instance_operation(first, 0U,
                                                               &first_operation)
            .ok());
    ASSERT_TRUE(openmeta::prepared_transfer_handoff_instance_operation(
                    second, 0U, &second_operation)
                    .ok());
    EXPECT_NE(first_operation.payload.data(), second_operation.payload.data());
    EXPECT_NE(find_payload_text(first_operation, first_value), nullptr);
    EXPECT_NE(find_payload_text(second_operation, "2024:01:02 03:04:05"),
              nullptr);

    ReplayProbe probe;
    ASSERT_TRUE(
        openmeta::replay_prepared_transfer_handoff_instance(first, replay_probe,
                                                            &probe)
            .ok());
    EXPECT_EQ(probe.calls, first.operation_count());
    EXPECT_EQ(openmeta::replay_prepared_transfer_handoff_instance(first,
                                                                  nullptr,
                                                                  nullptr)
                  .code,
              openmeta::PreparedTransferHandoffCode::NullReplayCallback);
    EXPECT_EQ(openmeta::prepared_transfer_handoff_instance_operation(
                  first, first.operation_count(), &first_operation)
                  .code,
              openmeta::PreparedTransferHandoffCode::OperationIndexOutOfRange);

    probe.return_value = openmeta::TransferStatus::InternalError;
    EXPECT_EQ(openmeta::replay_prepared_transfer_handoff_instance(first,
                                                                  replay_probe,
                                                                  &probe)
                  .code,
              openmeta::PreparedTransferHandoffCode::ReplayCallbackFailed);

    openmeta::PreparedTransferHandoffInstance moved(std::move(first));
    EXPECT_FALSE(first.valid());
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.operation_count(), first_operation_count);
}

TEST(PreparedTransferHandoff, WorkerPatchBatchIsStrictAndTransactional)
{
    const openmeta::TransferSourceSnapshot snapshot = make_source_snapshot();
    const openmeta::PrepareTransferRequest request  = make_request(
        openmeta::TransferTargetFormat::Jpeg);
    openmeta::PreparedTransferHandoff handoff;
    ASSERT_TRUE(
        openmeta::prepare_transfer_handoff(snapshot, request,
                                           openmeta::EmitTransferOptions {},
                                           &handoff)
            .ok());
    openmeta::PreparedTransferHandoffInstance instance;
    ASSERT_TRUE(
        openmeta::create_prepared_transfer_handoff_instance(handoff, &instance)
            .ok());

    openmeta::PreparedTransferHandoffOperationView before_operation;
    ASSERT_TRUE(openmeta::prepared_transfer_handoff_instance_operation(
                    instance, 0U, &before_operation)
                    .ok());
    const std::string before = payload_string(before_operation);

    const std::array<std::byte, 20> valid_bytes = exif_date_time_bytes(
        "2025:01:02 03:04:05");
    const openmeta::TimePatchView valid_update {
        openmeta::TimePatchField::DateTime,
        std::span<const std::byte>(valid_bytes.data(), valid_bytes.size())
    };
    const openmeta::TimePatchView missing_update {
        openmeta::TimePatchField::GpsDateStamp, text_bytes("2025:01:02")
    };
    const std::array<openmeta::TimePatchView, 2> partial_batch
        = { valid_update, missing_update };
    const openmeta::PreparedTransferHandoffPatchResult missing
        = openmeta::patch_prepared_transfer_handoff_instance(&instance,
                                                             partial_batch);
    EXPECT_EQ(missing.code,
              openmeta::PreparedTransferHandoffPatchCode::SlotNotFound);
    EXPECT_EQ(missing.failed_update_index, 1U);
    EXPECT_EQ(missing.patched_slots, 0U);

    openmeta::PreparedTransferHandoffOperationView after_missing;
    ASSERT_TRUE(
        openmeta::prepared_transfer_handoff_instance_operation(instance, 0U,
                                                               &after_missing)
            .ok());
    EXPECT_EQ(payload_string(after_missing), before);

    const openmeta::TimePatchView short_update {
        openmeta::TimePatchField::DateTime, text_bytes("short")
    };
    const std::array<openmeta::TimePatchView, 1> short_batch = { short_update };
    EXPECT_EQ(openmeta::patch_prepared_transfer_handoff_instance(&instance,
                                                                 short_batch)
                  .code,
              openmeta::PreparedTransferHandoffPatchCode::WidthMismatch);

    const std::array<openmeta::TimePatchView, 2> duplicate_batch
        = { valid_update, valid_update };
    EXPECT_EQ(openmeta::patch_prepared_transfer_handoff_instance(&instance,
                                                                 duplicate_batch)
                  .code,
              openmeta::PreparedTransferHandoffPatchCode::DuplicateField);

    const openmeta::TimePatchView invalid_update {
        static_cast<openmeta::TimePatchField>(0xFFU),
        text_bytes("2025:01:02 03:04:05")
    };
    const std::array<openmeta::TimePatchView, 1> invalid_batch
        = { invalid_update };
    EXPECT_EQ(openmeta::patch_prepared_transfer_handoff_instance(&instance,
                                                                 invalid_batch)
                  .code,
              openmeta::PreparedTransferHandoffPatchCode::InvalidField);
    EXPECT_EQ(openmeta::patch_prepared_transfer_handoff_instance(
                  &instance, std::span<const openmeta::TimePatchView> {})
                  .code,
              openmeta::PreparedTransferHandoffPatchCode::EmptyUpdates);
    EXPECT_EQ(openmeta::patch_prepared_transfer_handoff_instance(nullptr,
                                                                 short_batch)
                  .code,
              openmeta::PreparedTransferHandoffPatchCode::NullInstance);

    openmeta::PreparedTransferHandoffOperationView alias_operation;
    ASSERT_TRUE(
        openmeta::prepared_transfer_handoff_instance_operation(instance, 0U,
                                                               &alias_operation)
            .ok());
    const std::byte* alias_value = find_payload_text(alias_operation,
                                                     "2024:01:02 03:04:05");
    ASSERT_NE(alias_value, nullptr);
    const openmeta::TimePatchView alias_update {
        openmeta::TimePatchField::DateTime,
        std::span<const std::byte>(alias_value, 20U)
    };
    const std::array<openmeta::TimePatchView, 1> alias_batch = { alias_update };
    EXPECT_EQ(openmeta::patch_prepared_transfer_handoff_instance(&instance,
                                                                 alias_batch)
                  .code,
              openmeta::PreparedTransferHandoffPatchCode::ValueAliasesInstance);

    const std::array<openmeta::TimePatchView, 1> valid_batch = { valid_update };
    ASSERT_TRUE(openmeta::patch_prepared_transfer_handoff_instance(&instance,
                                                                   valid_batch)
                    .ok());
    openmeta::PreparedTransferHandoffOperationView after_valid;
    ASSERT_TRUE(
        openmeta::prepared_transfer_handoff_instance_operation(instance, 0U,
                                                               &after_valid)
            .ok());
    EXPECT_NE(find_payload_text(after_valid, "2025:01:02 03:04:05"), nullptr);
}

TEST(PreparedTransferHandoff, IndependentWorkersPatchAndReplayConcurrently)
{
    const openmeta::TransferSourceSnapshot snapshot = make_source_snapshot();
    const openmeta::PrepareTransferRequest request  = make_request(
        openmeta::TransferTargetFormat::Jpeg);
    openmeta::PreparedTransferHandoff handoff;
    ASSERT_TRUE(
        openmeta::prepare_transfer_handoff(snapshot, request,
                                           openmeta::EmitTransferOptions {},
                                           &handoff)
            .ok());

    openmeta::PreparedTransferHandoffInstance first;
    openmeta::PreparedTransferHandoffInstance second;
    ASSERT_TRUE(
        openmeta::create_prepared_transfer_handoff_instance(handoff, &first)
            .ok());
    ASSERT_TRUE(
        openmeta::create_prepared_transfer_handoff_instance(handoff, &second)
            .ok());

    InstanceWorker first_worker { &first,
                                  exif_date_time_bytes("2026:01:02 03:04:05"),
                                  "2026:01:02 03:04:05" };
    InstanceWorker second_worker { &second,
                                   exif_date_time_bytes("2027:06:07 08:09:10"),
                                   "2027:06:07 08:09:10" };
    std::thread first_thread(run_instance_worker, &first_worker);
    std::thread second_thread(run_instance_worker, &second_worker);
    first_thread.join();
    second_thread.join();

    ASSERT_TRUE(first_worker.patch.ok());
    ASSERT_TRUE(second_worker.patch.ok());
    EXPECT_EQ(first_worker.replay.calls, first.operation_count());
    EXPECT_EQ(second_worker.replay.calls, second.operation_count());

    openmeta::PreparedTransferHandoffOperationView first_operation;
    openmeta::PreparedTransferHandoffOperationView second_operation;
    ASSERT_TRUE(
        openmeta::prepared_transfer_handoff_instance_operation(first, 0U,
                                                               &first_operation)
            .ok());
    ASSERT_TRUE(openmeta::prepared_transfer_handoff_instance_operation(
                    second, 0U, &second_operation)
                    .ok());
    EXPECT_NE(find_payload_text(first_operation, first_worker.visible_value),
              nullptr);
    EXPECT_NE(find_payload_text(second_operation, second_worker.visible_value),
              nullptr);
}
