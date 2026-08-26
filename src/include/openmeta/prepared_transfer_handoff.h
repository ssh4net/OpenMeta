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

/// Stable token for one prepared-transfer handoff result code.
std::string_view
prepared_transfer_handoff_code_name(PreparedTransferHandoffCode code) noexcept;

/// Stable default message for one prepared-transfer handoff result code.
std::string_view
prepared_transfer_handoff_code_message(
    PreparedTransferHandoffCode code) noexcept;

/**
 * \brief One borrowed typed operation from a prepared handoff.
 *
 * `payload` is the prepared carrier payload for all non-EXR operations. For
 * `ExrAttribute`, use `exr_name`, `exr_type_name`, and `exr_value`; `payload`
 * remains available only for diagnostics. All views remain valid until the
 * source handoff is reset, moved from, destroyed, or prepared again.
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
};

/// Returns the prepared-transfer handoff version compiled into the library.
uint32_t
prepared_transfer_handoff_contract_version() noexcept;

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

}  // namespace openmeta
OPENMETA_PUBLIC_END
