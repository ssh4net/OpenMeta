// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"
#include "openmeta/metadata_transfer.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/**
 * \file prepared_transfer_handoff.h
 * \brief Stable target preparation and typed codec handoff contract.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable prepared-transfer handoff contract version.
inline constexpr uint32_t kPreparedTransferHandoffContractVersion = 1U;

/// Stable mutable prepared-transfer instance contract version.
inline constexpr uint32_t kPreparedTransferHandoffInstanceContractVersion = 1U;

/// Stable result code for prepared-transfer handoff operations.
enum class PreparedTransferHandoffCode : uint16_t {
    None = 0,
    NullOutput,
    RawCarrierPassthroughUnsupported,
    AllocationFailed,
    PrepareFailed,
    OperationCompilationFailed,
    InvalidState,
    NullOperationView,
    OperationIndexOutOfRange,
    NullReplayCallback,
    ReplayCallbackFailed,
};

/// Stable result for preparation, operation access, and replay.
struct PreparedTransferHandoffResult final {
    TransferStatus status            = TransferStatus::Ok;
    PreparedTransferHandoffCode code = PreparedTransferHandoffCode::None;
    PrepareTransferCode prepare_code = PrepareTransferCode::None;
    EmitTransferCode adapter_code    = EmitTransferCode::None;
    uint32_t operation_count         = 0U;
    uint32_t warnings                = 0U;
    uint32_t errors                  = 0U;
    uint32_t failed_operation_index  = 0xFFFFFFFFU;

    bool ok() const noexcept { return status == TransferStatus::Ok; }
};

/// Stable result code for fixed-width instance patches.
enum class PreparedTransferHandoffPatchCode : uint16_t {
    None = 0,
    NullInstance,
    NullFieldView,
    InvalidState,
    EmptyUpdates,
    InvalidField,
    DuplicateField,
    SlotNotFound,
    InvalidSlot,
    WidthMismatch,
    ValueAliasesInstance,
    LimitExceeded,
};

/// Allocation-free result for one transactional instance patch batch.
struct PreparedTransferHandoffPatchResult final {
    TransferStatus status = TransferStatus::Ok;
    PreparedTransferHandoffPatchCode code
        = PreparedTransferHandoffPatchCode::None;
    uint32_t patched_slots       = 0U;
    uint32_t failed_update_index = 0xFFFFFFFFU;
    uint32_t failed_slot_index   = 0xFFFFFFFFU;
    uint32_t errors              = 0U;

    bool ok() const noexcept { return status == TransferStatus::Ok; }
};

/// Stable description of one patchable field in a worker instance.
struct PreparedTransferHandoffTimePatchFieldView final {
    TimePatchField field = TimePatchField::DateTime;
    uint16_t width       = 0U;
    uint32_t slot_count  = 0U;
};

/// Stable token for one prepared-transfer handoff result code.
std::string_view
prepared_transfer_handoff_code_name(PreparedTransferHandoffCode code) noexcept;

/// Stable default message for one prepared-transfer handoff result code.
std::string_view
prepared_transfer_handoff_code_message(
    PreparedTransferHandoffCode code) noexcept;

/// Stable token for one prepared-transfer instance patch result code.
std::string_view
prepared_transfer_handoff_patch_code_name(
    PreparedTransferHandoffPatchCode code) noexcept;

/// Stable default message for one instance patch result code.
std::string_view
prepared_transfer_handoff_patch_code_message(
    PreparedTransferHandoffPatchCode code) noexcept;

/**
 * \brief One borrowed typed operation from a prepared handoff.
 *
 * `payload` is the prepared carrier payload for all non-EXR operations. For
 * `ExrAttribute`, use `exr_name`, `exr_type_name`, and `exr_value`; `payload`
 * remains available only for diagnostics. Views borrow either handoff or
 * instance storage. They remain valid until that owner is reset, moved from,
 * destroyed, or prepared again; mutable-instance payload contents may change
 * after a successful patch.
 */
struct PreparedTransferHandoffOperationView final {
    TransferSemanticKind semantic_kind = TransferSemanticKind::Unknown;
    PreparedTransferAdapterOp operation;
    std::span<const std::byte> payload;
    std::string_view exr_name;
    std::string_view exr_type_name;
    std::span<const std::byte> exr_value;
    bool exr_is_opaque = false;
};

/// Allocation-free replay callback invoked once per prepared operation.
using PreparedTransferHandoffReplayCallback = TransferStatus (*)(
    void* user, const PreparedTransferHandoffOperationView* operation) noexcept;

class PreparedTransferHandoffInstance;

/**
 * \brief Move-only owner of immutable target-specific metadata and operations.
 *
 * The public object has a fixed opaque-pointer layout. Preparation performs
 * all allocation and route compilation. Indexed access and replay borrow
 * immutable storage and perform no allocation. Concurrent const access is
 * supported when each replay uses independent caller state; reset, move,
 * destruction, or preparation require exclusive ownership.
 */
class PreparedTransferHandoff final {
public:
    PreparedTransferHandoff() noexcept;
    ~PreparedTransferHandoff() noexcept;

    PreparedTransferHandoff(PreparedTransferHandoff&& other) noexcept;
    PreparedTransferHandoff& operator=(PreparedTransferHandoff&& other) noexcept;

    PreparedTransferHandoff(const PreparedTransferHandoff&)            = delete;
    PreparedTransferHandoff& operator=(const PreparedTransferHandoff&) = delete;

    bool valid() const noexcept;
    TransferTargetFormat target_format() const noexcept;
    uint32_t operation_count() const noexcept;
    void reset() noexcept;

private:
    void* state_ = nullptr;

    friend PreparedTransferHandoffResult prepare_transfer_handoff(
        const TransferSourceSnapshot&, const PrepareTransferRequest&,
        const EmitTransferOptions&, PreparedTransferHandoff*) noexcept;
    friend PreparedTransferHandoffResult prepared_transfer_handoff_operation(
        const PreparedTransferHandoff&, uint32_t,
        PreparedTransferHandoffOperationView*) noexcept;
    friend PreparedTransferHandoffResult
    create_prepared_transfer_handoff_instance(
        const PreparedTransferHandoff&,
        PreparedTransferHandoffInstance*) noexcept;
};

/**
 * \brief Move-only mutable worker instance cloned from an immutable handoff.
 *
 * Creation allocates and copies compact payload/operation state. Fixed-width
 * patching, indexed access, and replay perform no allocation. One instance
 * requires exclusive ownership while patching; independent instances may be
 * patched and replayed concurrently and do not borrow their source template.
 */
class PreparedTransferHandoffInstance final {
public:
    PreparedTransferHandoffInstance() noexcept;
    ~PreparedTransferHandoffInstance() noexcept;

    PreparedTransferHandoffInstance(
        PreparedTransferHandoffInstance&& other) noexcept;
    PreparedTransferHandoffInstance&
    operator=(PreparedTransferHandoffInstance&& other) noexcept;

    PreparedTransferHandoffInstance(const PreparedTransferHandoffInstance&)
        = delete;
    PreparedTransferHandoffInstance&
    operator=(const PreparedTransferHandoffInstance&)
        = delete;

    bool valid() const noexcept;
    TransferTargetFormat target_format() const noexcept;
    uint32_t operation_count() const noexcept;
    uint32_t time_patch_slot_count(TimePatchField field) const noexcept;
    void reset() noexcept;

private:
    void* state_ = nullptr;

    friend PreparedTransferHandoffResult
    create_prepared_transfer_handoff_instance(
        const PreparedTransferHandoff&,
        PreparedTransferHandoffInstance*) noexcept;
    friend PreparedTransferHandoffPatchResult
    patch_prepared_transfer_handoff_instance(
        PreparedTransferHandoffInstance*,
        std::span<const TimePatchView>) noexcept;
    friend PreparedTransferHandoffPatchResult
    prepared_transfer_handoff_instance_time_patch_field(
        const PreparedTransferHandoffInstance&, TimePatchField,
        PreparedTransferHandoffTimePatchFieldView*) noexcept;
    friend PreparedTransferHandoffResult
    prepared_transfer_handoff_instance_operation(
        const PreparedTransferHandoffInstance&, uint32_t,
        PreparedTransferHandoffOperationView*) noexcept;
};

/// Returns the prepared-transfer handoff version compiled into the library.
uint32_t
prepared_transfer_handoff_contract_version() noexcept;

/// Returns the mutable instance contract version compiled into the library.
uint32_t
prepared_transfer_handoff_instance_contract_version() noexcept;

/**
 * \brief Prepare immutable target payloads and typed operations once.
 *
 * On failure, `out_handoff` is unchanged. Stable v1 deliberately rejects
 * opt-in raw-carrier passthrough because that policy remains experimental.
 */
PreparedTransferHandoffResult
prepare_transfer_handoff(const TransferSourceSnapshot& snapshot,
                         const PrepareTransferRequest& request,
                         const EmitTransferOptions& emit_options,
                         PreparedTransferHandoff* out_handoff) noexcept;

/// Resolve one operation without allocation or route parsing by the host.
PreparedTransferHandoffResult
prepared_transfer_handoff_operation(
    const PreparedTransferHandoff& handoff, uint32_t operation_index,
    PreparedTransferHandoffOperationView* out_operation) noexcept;

/// Replay immutable typed operation views without recompilation or allocation.
PreparedTransferHandoffResult
replay_prepared_transfer_handoff(const PreparedTransferHandoff& handoff,
                                 PreparedTransferHandoffReplayCallback callback,
                                 void* user) noexcept;

/**
 * \brief Clone one independently owned mutable worker instance.
 *
 * Creation may allocate. On failure, `out_instance` is unchanged. The new
 * instance does not borrow the source handoff and remains valid after the
 * source is reset, moved, prepared again, or destroyed.
 */
PreparedTransferHandoffResult
create_prepared_transfer_handoff_instance(
    const PreparedTransferHandoff& handoff,
    PreparedTransferHandoffInstance* out_instance) noexcept;

/**
 * \brief Query the exact serialized width and slot count for one field.
 *
 * The query performs no allocation and exposes no payload offsets. It fails if
 * the field is absent or its prepared slots do not have one uniform nonzero
 * width, because one strict patch value could not update such a layout.
 */
PreparedTransferHandoffPatchResult
prepared_transfer_handoff_instance_time_patch_field(
    const PreparedTransferHandoffInstance& instance, TimePatchField field,
    PreparedTransferHandoffTimePatchFieldView* out_field) noexcept;

/**
 * \brief Apply one strict fixed-width patch batch transactionally.
 *
 * Every field must be valid, unique, and have at least one prepared slot.
 * Every value must exactly match every corresponding slot width and must not
 * alias instance payload storage. Validation completes before any payload byte
 * changes. Successful and failed calls perform no allocation.
 */
PreparedTransferHandoffPatchResult
patch_prepared_transfer_handoff_instance(
    PreparedTransferHandoffInstance* instance,
    std::span<const TimePatchView> updates) noexcept;

/// Resolve one mutable-instance operation without allocation.
PreparedTransferHandoffResult
prepared_transfer_handoff_instance_operation(
    const PreparedTransferHandoffInstance& instance, uint32_t operation_index,
    PreparedTransferHandoffOperationView* out_operation) noexcept;

/// Replay mutable-instance operations without recompilation or allocation.
PreparedTransferHandoffResult
replay_prepared_transfer_handoff_instance(
    const PreparedTransferHandoffInstance& instance,
    PreparedTransferHandoffReplayCallback callback, void* user) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
