// SPDX-License-Identifier: Apache-2.0

#include "openmeta/prepared_transfer_handoff.h"

#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    struct PreparedTransferHandoffState final {
        uint32_t contract_version = kPreparedTransferHandoffContractVersion;
        PreparedTransferBundle bundle;
        PreparedTransferAdapterView adapter;
        std::vector<TransferSemanticKind> semantics;
    };

    static PreparedTransferHandoffResult
    make_handoff_error(TransferStatus status,
                       PreparedTransferHandoffCode code) noexcept
    {
        PreparedTransferHandoffResult result;
        result.status = status;
        result.code   = code;
        result.errors = 1U;
        return result;
    }

    static PreparedTransferHandoffState* handoff_state(void* state) noexcept
    {
        return static_cast<PreparedTransferHandoffState*>(state);
    }

    static const PreparedTransferHandoffState*
    const_handoff_state(const void* state) noexcept
    {
        return static_cast<const PreparedTransferHandoffState*>(state);
    }

}  // namespace

PreparedTransferHandoff::PreparedTransferHandoff() noexcept = default;

PreparedTransferHandoff::~PreparedTransferHandoff() noexcept { reset(); }

PreparedTransferHandoff::PreparedTransferHandoff(
    PreparedTransferHandoff&& other) noexcept
    : state_(std::exchange(other.state_, nullptr))
{
}

PreparedTransferHandoff&
PreparedTransferHandoff::operator=(PreparedTransferHandoff&& other) noexcept
{
    if (this != &other) {
        reset();
        state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
}

bool
PreparedTransferHandoff::valid() const noexcept
{
    const PreparedTransferHandoffState* state = const_handoff_state(state_);
    return state
           && state->contract_version
                  == kPreparedTransferHandoffContractVersion;
}

TransferTargetFormat
PreparedTransferHandoff::target_format() const noexcept
{
    const PreparedTransferHandoffState* state = const_handoff_state(state_);
    return state ? state->adapter.target_format : TransferTargetFormat::Jpeg;
}

uint32_t
PreparedTransferHandoff::operation_count() const noexcept
{
    const PreparedTransferHandoffState* state = const_handoff_state(state_);
    return state ? static_cast<uint32_t>(state->adapter.ops.size()) : 0U;
}

void
PreparedTransferHandoff::reset() noexcept
{
    delete handoff_state(state_);
    state_ = nullptr;
}

uint32_t
prepared_transfer_handoff_contract_version() noexcept
{
    return kPreparedTransferHandoffContractVersion;
}

std::string_view
prepared_transfer_handoff_code_name(PreparedTransferHandoffCode code) noexcept
{
    switch (code) {
    case PreparedTransferHandoffCode::None: return "none";
    case PreparedTransferHandoffCode::NullOutput: return "null_output";
    case PreparedTransferHandoffCode::RawCarrierPassthroughUnsupported:
        return "raw_carrier_passthrough_unsupported";
    case PreparedTransferHandoffCode::AllocationFailed:
        return "allocation_failed";
    case PreparedTransferHandoffCode::PrepareFailed: return "prepare_failed";
    case PreparedTransferHandoffCode::OperationCompilationFailed:
        return "operation_compilation_failed";
    case PreparedTransferHandoffCode::InvalidState: return "invalid_state";
    case PreparedTransferHandoffCode::NullOperationView:
        return "null_operation_view";
    case PreparedTransferHandoffCode::OperationIndexOutOfRange:
        return "operation_index_out_of_range";
    case PreparedTransferHandoffCode::NullReplayCallback:
        return "null_replay_callback";
    case PreparedTransferHandoffCode::ReplayCallbackFailed:
        return "replay_callback_failed";
    }
    return "unknown";
}

std::string_view
prepared_transfer_handoff_code_message(PreparedTransferHandoffCode code) noexcept
{
    switch (code) {
    case PreparedTransferHandoffCode::None: return "success";
    case PreparedTransferHandoffCode::NullOutput:
        return "output handoff is null";
    case PreparedTransferHandoffCode::RawCarrierPassthroughUnsupported:
        return "stable handoff does not support raw-carrier passthrough";
    case PreparedTransferHandoffCode::AllocationFailed:
        return "failed to allocate prepared handoff state";
    case PreparedTransferHandoffCode::PrepareFailed:
        return "target metadata preparation failed";
    case PreparedTransferHandoffCode::OperationCompilationFailed:
        return "typed operation compilation failed";
    case PreparedTransferHandoffCode::InvalidState:
        return "prepared handoff is not valid";
    case PreparedTransferHandoffCode::NullOperationView:
        return "output operation view is null";
    case PreparedTransferHandoffCode::OperationIndexOutOfRange:
        return "prepared operation index is out of range";
    case PreparedTransferHandoffCode::NullReplayCallback:
        return "replay callback is null";
    case PreparedTransferHandoffCode::ReplayCallbackFailed:
        return "replay callback failed";
    }
    return "unknown prepared handoff result";
}

PreparedTransferHandoffResult
prepare_transfer_handoff(const TransferSourceSnapshot& snapshot,
                         const PrepareTransferRequest& request,
                         const EmitTransferOptions& emit_options,
                         PreparedTransferHandoff* out_handoff) noexcept
{
    if (!out_handoff) {
        return make_handoff_error(TransferStatus::InvalidArgument,
                                  PreparedTransferHandoffCode::NullOutput);
    }
    if (request.raw_carrier_passthrough_mode
        != TransferRawCarrierPassthroughMode::Disabled) {
        return make_handoff_error(
            TransferStatus::InvalidArgument,
            PreparedTransferHandoffCode::RawCarrierPassthroughUnsupported);
    }

    PreparedTransferHandoffState* state = new (std::nothrow)
        PreparedTransferHandoffState;
    if (!state) {
        return make_handoff_error(TransferStatus::InternalError,
                                  PreparedTransferHandoffCode::AllocationFailed);
    }

    PreparedTransferHandoffResult result;
    const PrepareTransferResult prepared
        = prepare_metadata_for_target_snapshot(snapshot, request,
                                               &state->bundle);
    result.status       = prepared.status;
    result.prepare_code = prepared.code;
    result.warnings     = prepared.warnings;
    result.errors       = prepared.errors;
    if (prepared.status != TransferStatus::Ok) {
        result.code = PreparedTransferHandoffCode::PrepareFailed;
        delete state;
        return result;
    }

    const EmitTransferResult compiled
        = build_prepared_transfer_adapter_view(state->bundle, &state->adapter,
                                               emit_options);
    result.status       = compiled.status;
    result.adapter_code = compiled.code;
    result.errors += compiled.errors;
    if (compiled.status != TransferStatus::Ok) {
        result.code = PreparedTransferHandoffCode::OperationCompilationFailed;
        delete state;
        return result;
    }
    if (state->adapter.ops.size()
        > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        delete state;
        return make_handoff_error(
            TransferStatus::LimitExceeded,
            PreparedTransferHandoffCode::OperationCompilationFailed);
    }

    state->semantics.reserve(state->adapter.ops.size());
    for (size_t i = 0U; i < state->adapter.ops.size(); ++i) {
        const PreparedTransferAdapterOp& operation = state->adapter.ops[i];
        if (operation.block_index >= state->bundle.blocks.size()) {
            delete state;
            return make_handoff_error(
                TransferStatus::InternalError,
                PreparedTransferHandoffCode::OperationCompilationFailed);
        }
        state->semantics.push_back(classify_transfer_route_semantic_kind(
            state->bundle.blocks[operation.block_index].route));
    }

    out_handoff->reset();
    out_handoff->state_    = state;
    result.status          = TransferStatus::Ok;
    result.code            = PreparedTransferHandoffCode::None;
    result.adapter_code    = EmitTransferCode::None;
    result.operation_count = static_cast<uint32_t>(state->adapter.ops.size());
    result.errors          = 0U;
    result.failed_operation_index = 0xFFFFFFFFU;
    return result;
}

PreparedTransferHandoffResult
prepared_transfer_handoff_operation(
    const PreparedTransferHandoff& handoff, uint32_t operation_index,
    PreparedTransferHandoffOperationView* out_operation) noexcept
{
    if (!out_operation) {
        return make_handoff_error(
            TransferStatus::InvalidArgument,
            PreparedTransferHandoffCode::NullOperationView);
    }

    const PreparedTransferHandoffState* state = const_handoff_state(
        handoff.state_);
    if (!state
        || state->contract_version != kPreparedTransferHandoffContractVersion) {
        return make_handoff_error(TransferStatus::InvalidArgument,
                                  PreparedTransferHandoffCode::InvalidState);
    }
    if (operation_index >= state->adapter.ops.size()
        || operation_index >= state->semantics.size()) {
        PreparedTransferHandoffResult result = make_handoff_error(
            TransferStatus::InvalidArgument,
            PreparedTransferHandoffCode::OperationIndexOutOfRange);
        result.failed_operation_index = operation_index;
        return result;
    }

    const PreparedTransferAdapterOp& operation
        = state->adapter.ops[operation_index];
    if (operation.block_index >= state->bundle.blocks.size()) {
        PreparedTransferHandoffResult result
            = make_handoff_error(TransferStatus::InternalError,
                                 PreparedTransferHandoffCode::InvalidState);
        result.failed_operation_index = operation_index;
        return result;
    }
    const PreparedTransferBlock& block
        = state->bundle.blocks[operation.block_index];
    if (operation.payload_size != block.payload.size()) {
        PreparedTransferHandoffResult result
            = make_handoff_error(TransferStatus::InternalError,
                                 PreparedTransferHandoffCode::InvalidState);
        result.failed_operation_index = operation_index;
        return result;
    }

    PreparedTransferHandoffOperationView view;
    view.semantic_kind = state->semantics[operation_index];
    view.operation     = operation;
    view.payload       = std::span<const std::byte>(block.payload.data(),
                                                    block.payload.size());
    if (operation.kind == TransferAdapterOpKind::ExrAttribute) {
        ExrPreparedAttributeView attribute;
        const EmitTransferResult resolved
            = get_prepared_transfer_adapter_exr_attribute_view(state->bundle,
                                                               operation,
                                                               &attribute);
        if (resolved.status != TransferStatus::Ok) {
            PreparedTransferHandoffResult result
                = make_handoff_error(resolved.status,
                                     PreparedTransferHandoffCode::InvalidState);
            result.adapter_code           = resolved.code;
            result.failed_operation_index = operation_index;
            return result;
        }
        view.exr_name      = attribute.name;
        view.exr_type_name = attribute.type_name;
        view.exr_value     = attribute.value;
        view.exr_is_opaque = attribute.is_opaque;
    }

    *out_operation = view;
    PreparedTransferHandoffResult result;
    result.operation_count = 1U;
    return result;
}

PreparedTransferHandoffResult
replay_prepared_transfer_handoff(const PreparedTransferHandoff& handoff,
                                 PreparedTransferHandoffReplayCallback callback,
                                 void* user) noexcept
{
    if (!callback) {
        return make_handoff_error(
            TransferStatus::InvalidArgument,
            PreparedTransferHandoffCode::NullReplayCallback);
    }
    if (!handoff.valid()) {
        return make_handoff_error(TransferStatus::InvalidArgument,
                                  PreparedTransferHandoffCode::InvalidState);
    }

    PreparedTransferHandoffResult result;
    const uint32_t operation_count = handoff.operation_count();
    for (uint32_t i = 0U; i < operation_count; ++i) {
        PreparedTransferHandoffOperationView operation;
        PreparedTransferHandoffResult resolved
            = prepared_transfer_handoff_operation(handoff, i, &operation);
        if (!resolved.ok()) {
            return resolved;
        }
        const TransferStatus status = callback(user, &operation);
        if (status != TransferStatus::Ok) {
            result.status = status;
            result.code   = PreparedTransferHandoffCode::ReplayCallbackFailed;
            result.adapter_code    = EmitTransferCode::BackendWriteFailed;
            result.operation_count = i;
            result.errors          = 1U;
            result.failed_operation_index = i;
            return result;
        }
    }

    result.operation_count = operation_count;
    return result;
}

}  // namespace openmeta
