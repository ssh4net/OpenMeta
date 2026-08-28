// SPDX-License-Identifier: Apache-2.0

#include "openmeta/prepared_transfer_handoff.h"

#include <cstring>
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

    struct PreparedTransferHandoffInstanceBlock final {
        uint64_t payload_offset = 0U;
        uint64_t payload_size   = 0U;
    };

    struct PreparedTransferHandoffInstanceExrView final {
        uint64_t name_offset  = 0U;
        uint64_t name_size    = 0U;
        uint64_t value_offset = 0U;
        uint64_t value_size   = 0U;
        bool valid            = false;
        bool is_opaque        = false;
    };

    struct PreparedTransferHandoffInstanceState final {
        uint32_t contract_version
            = kPreparedTransferHandoffInstanceContractVersion;
        TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
        uint32_t block_count               = 0U;
        uint32_t operation_count           = 0U;
        uint32_t time_patch_slot_count     = 0U;
        uint64_t payload_size              = 0U;
        std::byte* payloads                = nullptr;
        PreparedTransferHandoffInstanceBlock* blocks      = nullptr;
        PreparedTransferAdapterOp* operations             = nullptr;
        TransferSemanticKind* semantics                   = nullptr;
        TimePatchSlot* time_patch_slots                   = nullptr;
        PreparedTransferHandoffInstanceExrView* exr_views = nullptr;

        ~PreparedTransferHandoffInstanceState() noexcept
        {
            delete[] exr_views;
            delete[] time_patch_slots;
            delete[] semantics;
            delete[] operations;
            delete[] blocks;
            delete[] payloads;
        }
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

    static PreparedTransferHandoffInstanceState*
    handoff_instance_state(void* state) noexcept
    {
        return static_cast<PreparedTransferHandoffInstanceState*>(state);
    }

    static const PreparedTransferHandoffInstanceState*
    const_handoff_instance_state(const void* state) noexcept
    {
        return static_cast<const PreparedTransferHandoffInstanceState*>(state);
    }

    static bool valid_time_patch_field(TimePatchField field) noexcept
    {
        return static_cast<uint32_t>(field)
               <= static_cast<uint32_t>(TimePatchField::GpsTimeStamp);
    }

    static bool pointer_range_contains(const void* parent_data,
                                       uint64_t parent_size,
                                       const void* child_data,
                                       uint64_t child_size,
                                       uint64_t* out_offset) noexcept
    {
        if (!out_offset || !parent_data || !child_data) {
            return false;
        }
        const uintptr_t parent_begin = reinterpret_cast<uintptr_t>(parent_data);
        const uintptr_t child_begin  = reinterpret_cast<uintptr_t>(child_data);
        const uintptr_t max_address  = std::numeric_limits<uintptr_t>::max();
        if (parent_size > max_address - parent_begin
            || child_size > max_address - child_begin) {
            return false;
        }
        const uintptr_t parent_end = parent_begin
                                     + static_cast<uintptr_t>(parent_size);
        const uintptr_t child_end = child_begin
                                    + static_cast<uintptr_t>(child_size);
        if (child_begin < parent_begin || child_end > parent_end) {
            return false;
        }
        *out_offset = static_cast<uint64_t>(child_begin - parent_begin);
        return true;
    }

    static bool pointer_ranges_overlap(const void* first_data,
                                       uint64_t first_size,
                                       const void* second_data,
                                       uint64_t second_size) noexcept
    {
        if (first_size == 0U || second_size == 0U || !first_data
            || !second_data) {
            return false;
        }
        const uintptr_t first_begin  = reinterpret_cast<uintptr_t>(first_data);
        const uintptr_t second_begin = reinterpret_cast<uintptr_t>(second_data);
        const uintptr_t max_address  = std::numeric_limits<uintptr_t>::max();
        if (first_size > max_address - first_begin
            || second_size > max_address - second_begin) {
            return true;
        }
        const uintptr_t first_end = first_begin
                                    + static_cast<uintptr_t>(first_size);
        const uintptr_t second_end = second_begin
                                     + static_cast<uintptr_t>(second_size);
        return first_begin < second_end && second_begin < first_end;
    }

    static PreparedTransferHandoffPatchResult
    make_patch_error(TransferStatus status,
                     PreparedTransferHandoffPatchCode code,
                     uint32_t failed_update_index = 0xFFFFFFFFU,
                     uint32_t failed_slot_index   = 0xFFFFFFFFU) noexcept
    {
        PreparedTransferHandoffPatchResult result;
        result.status              = status;
        result.code                = code;
        result.failed_update_index = failed_update_index;
        result.failed_slot_index   = failed_slot_index;
        result.errors              = 1U;
        return result;
    }

    static bool
    instance_block_payload(const PreparedTransferHandoffInstanceState& state,
                           uint32_t block_index,
                           std::span<const std::byte>* out_payload) noexcept
    {
        if (!out_payload || block_index >= state.block_count) {
            return false;
        }
        const PreparedTransferHandoffInstanceBlock& block
            = state.blocks[block_index];
        if (block.payload_offset > state.payload_size
            || block.payload_size > state.payload_size - block.payload_offset) {
            return false;
        }
        const std::byte* data
            = block.payload_size == 0U
                  ? nullptr
                  : state.payloads + static_cast<size_t>(block.payload_offset);
        *out_payload = std::span<const std::byte>(data,
                                                  static_cast<size_t>(
                                                      block.payload_size));
        return true;
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

PreparedTransferHandoffInstance::PreparedTransferHandoffInstance() noexcept
    = default;

PreparedTransferHandoffInstance::~PreparedTransferHandoffInstance() noexcept
{
    reset();
}

PreparedTransferHandoffInstance::PreparedTransferHandoffInstance(
    PreparedTransferHandoffInstance&& other) noexcept
    : state_(std::exchange(other.state_, nullptr))
{
}

PreparedTransferHandoffInstance&
PreparedTransferHandoffInstance::operator=(
    PreparedTransferHandoffInstance&& other) noexcept
{
    if (this != &other) {
        reset();
        state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
}

bool
PreparedTransferHandoffInstance::valid() const noexcept
{
    const PreparedTransferHandoffInstanceState* state
        = const_handoff_instance_state(state_);
    return state
           && state->contract_version
                  == kPreparedTransferHandoffInstanceContractVersion;
}

TransferTargetFormat
PreparedTransferHandoffInstance::target_format() const noexcept
{
    const PreparedTransferHandoffInstanceState* state
        = const_handoff_instance_state(state_);
    return state ? state->target_format : TransferTargetFormat::Jpeg;
}

uint32_t
PreparedTransferHandoffInstance::operation_count() const noexcept
{
    const PreparedTransferHandoffInstanceState* state
        = const_handoff_instance_state(state_);
    return state ? state->operation_count : 0U;
}

uint32_t
PreparedTransferHandoffInstance::time_patch_slot_count(
    TimePatchField field) const noexcept
{
    const PreparedTransferHandoffInstanceState* state
        = const_handoff_instance_state(state_);
    if (!state
        || state->contract_version
               != kPreparedTransferHandoffInstanceContractVersion
        || !valid_time_patch_field(field)) {
        return 0U;
    }

    uint32_t count = 0U;
    for (uint32_t i = 0U; i < state->time_patch_slot_count; ++i) {
        count += static_cast<uint32_t>(state->time_patch_slots[i].field
                                       == field);
    }
    return count;
}

void
PreparedTransferHandoffInstance::reset() noexcept
{
    delete handoff_instance_state(state_);
    state_ = nullptr;
}

uint32_t
prepared_transfer_handoff_contract_version() noexcept
{
    return kPreparedTransferHandoffContractVersion;
}

uint32_t
prepared_transfer_handoff_instance_contract_version() noexcept
{
    return kPreparedTransferHandoffInstanceContractVersion;
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
        return "output handoff or instance is null";
    case PreparedTransferHandoffCode::RawCarrierPassthroughUnsupported:
        return "stable handoff does not support raw-carrier passthrough";
    case PreparedTransferHandoffCode::AllocationFailed:
        return "failed to allocate prepared handoff or instance state";
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

std::string_view
prepared_transfer_handoff_patch_code_name(
    PreparedTransferHandoffPatchCode code) noexcept
{
    switch (code) {
    case PreparedTransferHandoffPatchCode::None: return "none";
    case PreparedTransferHandoffPatchCode::NullInstance: return "null_instance";
    case PreparedTransferHandoffPatchCode::NullFieldView:
        return "null_field_view";
    case PreparedTransferHandoffPatchCode::InvalidState: return "invalid_state";
    case PreparedTransferHandoffPatchCode::EmptyUpdates: return "empty_updates";
    case PreparedTransferHandoffPatchCode::InvalidField: return "invalid_field";
    case PreparedTransferHandoffPatchCode::DuplicateField:
        return "duplicate_field";
    case PreparedTransferHandoffPatchCode::SlotNotFound:
        return "slot_not_found";
    case PreparedTransferHandoffPatchCode::InvalidSlot: return "invalid_slot";
    case PreparedTransferHandoffPatchCode::WidthMismatch:
        return "width_mismatch";
    case PreparedTransferHandoffPatchCode::ValueAliasesInstance:
        return "value_aliases_instance";
    case PreparedTransferHandoffPatchCode::LimitExceeded:
        return "limit_exceeded";
    }
    return "unknown";
}

std::string_view
prepared_transfer_handoff_patch_code_message(
    PreparedTransferHandoffPatchCode code) noexcept
{
    switch (code) {
    case PreparedTransferHandoffPatchCode::None: return "success";
    case PreparedTransferHandoffPatchCode::NullInstance:
        return "prepared handoff instance is null";
    case PreparedTransferHandoffPatchCode::NullFieldView:
        return "output time patch field view is null";
    case PreparedTransferHandoffPatchCode::InvalidState:
        return "prepared handoff instance is not valid";
    case PreparedTransferHandoffPatchCode::EmptyUpdates:
        return "time patch updates are empty";
    case PreparedTransferHandoffPatchCode::InvalidField:
        return "time patch field is invalid";
    case PreparedTransferHandoffPatchCode::DuplicateField:
        return "time patch field occurs more than once";
    case PreparedTransferHandoffPatchCode::SlotNotFound:
        return "time patch field has no prepared slot";
    case PreparedTransferHandoffPatchCode::InvalidSlot:
        return "prepared time patch slot is invalid";
    case PreparedTransferHandoffPatchCode::WidthMismatch:
        return "time patch value does not match the prepared slot width";
    case PreparedTransferHandoffPatchCode::ValueAliasesInstance:
        return "time patch value aliases mutable instance payload storage";
    case PreparedTransferHandoffPatchCode::LimitExceeded:
        return "time patch count exceeds the stable result limit";
    }
    return "unknown prepared handoff patch result";
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
            return make_handoff_error(TransferStatus::InternalError,
                                      PreparedTransferHandoffCode::InvalidState);
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

PreparedTransferHandoffResult
create_prepared_transfer_handoff_instance(
    const PreparedTransferHandoff& handoff,
    PreparedTransferHandoffInstance* out_instance) noexcept
{
    if (!out_instance) {
        return make_handoff_error(TransferStatus::InvalidArgument,
                                  PreparedTransferHandoffCode::NullOutput);
    }
    const PreparedTransferHandoffState* source = const_handoff_state(
        handoff.state_);
    if (!source
        || source->contract_version
               != kPreparedTransferHandoffContractVersion) {
        return make_handoff_error(TransferStatus::InvalidArgument,
                                  PreparedTransferHandoffCode::InvalidState);
    }
    if (source->bundle.blocks.size()
            > static_cast<size_t>(std::numeric_limits<uint32_t>::max())
        || source->adapter.ops.size()
               > static_cast<size_t>(std::numeric_limits<uint32_t>::max())
        || source->bundle.time_patch_map.size()
               > static_cast<size_t>(std::numeric_limits<uint32_t>::max())
        || source->semantics.size() != source->adapter.ops.size()) {
        return make_handoff_error(TransferStatus::LimitExceeded,
                                  PreparedTransferHandoffCode::InvalidState);
    }

    uint64_t payload_size = 0U;
    for (size_t i = 0U; i < source->bundle.blocks.size(); ++i) {
        const uint64_t block_size = static_cast<uint64_t>(
            source->bundle.blocks[i].payload.size());
        if (block_size > std::numeric_limits<uint64_t>::max() - payload_size) {
            return make_handoff_error(
                TransferStatus::LimitExceeded,
                PreparedTransferHandoffCode::AllocationFailed);
        }
        payload_size += block_size;
    }
    if (payload_size
        > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return make_handoff_error(TransferStatus::LimitExceeded,
                                  PreparedTransferHandoffCode::AllocationFailed);
    }

    PreparedTransferHandoffInstanceState* state = new (std::nothrow)
        PreparedTransferHandoffInstanceState;
    if (!state) {
        return make_handoff_error(TransferStatus::InternalError,
                                  PreparedTransferHandoffCode::AllocationFailed);
    }

    state->target_format = source->adapter.target_format;
    state->block_count   = static_cast<uint32_t>(source->bundle.blocks.size());
    state->operation_count = static_cast<uint32_t>(source->adapter.ops.size());
    state->time_patch_slot_count = static_cast<uint32_t>(
        source->bundle.time_patch_map.size());
    state->payload_size = payload_size;

    if (state->payload_size != 0U) {
        state->payloads = new (std::nothrow)
            std::byte[static_cast<size_t>(state->payload_size)];
    }
    if (state->block_count != 0U) {
        state->blocks = new (std::nothrow)
            PreparedTransferHandoffInstanceBlock[state->block_count];
    }
    if (state->operation_count != 0U) {
        state->operations = new (std::nothrow)
            PreparedTransferAdapterOp[state->operation_count];
        state->semantics = new (std::nothrow)
            TransferSemanticKind[state->operation_count];
        state->exr_views = new (std::nothrow)
            PreparedTransferHandoffInstanceExrView[state->operation_count];
    }
    if (state->time_patch_slot_count != 0U) {
        state->time_patch_slots = new (std::nothrow)
            TimePatchSlot[state->time_patch_slot_count];
    }
    if ((state->payload_size != 0U && !state->payloads)
        || (state->block_count != 0U && !state->blocks)
        || (state->operation_count != 0U
            && (!state->operations || !state->semantics || !state->exr_views))
        || (state->time_patch_slot_count != 0U && !state->time_patch_slots)) {
        delete state;
        return make_handoff_error(TransferStatus::InternalError,
                                  PreparedTransferHandoffCode::AllocationFailed);
    }

    uint64_t payload_offset = 0U;
    for (uint32_t i = 0U; i < state->block_count; ++i) {
        const PreparedTransferBlock& source_block = source->bundle.blocks[i];
        const uint64_t block_size                 = static_cast<uint64_t>(
            source_block.payload.size());
        state->blocks[i].payload_offset = payload_offset;
        state->blocks[i].payload_size   = block_size;
        if (block_size != 0U) {
            std::memcpy(state->payloads + static_cast<size_t>(payload_offset),
                        source_block.payload.data(),
                        static_cast<size_t>(block_size));
        }
        payload_offset += block_size;
    }
    for (uint32_t i = 0U; i < state->time_patch_slot_count; ++i) {
        state->time_patch_slots[i] = source->bundle.time_patch_map[i];
    }

    for (uint32_t i = 0U; i < state->operation_count; ++i) {
        PreparedTransferHandoffOperationView operation;
        const PreparedTransferHandoffResult resolved
            = prepared_transfer_handoff_operation(handoff, i, &operation);
        if (!resolved.ok()) {
            delete state;
            return resolved;
        }
        state->operations[i] = operation.operation;
        state->semantics[i]  = operation.semantic_kind;
        if (operation.operation.kind != TransferAdapterOpKind::ExrAttribute) {
            continue;
        }

        PreparedTransferHandoffInstanceExrView& exr = state->exr_views[i];
        const bool name_valid                       = pointer_range_contains(
            operation.payload.data(), operation.payload.size(),
            operation.exr_name.data(), operation.exr_name.size(),
            &exr.name_offset);
        const bool value_valid = pointer_range_contains(
            operation.payload.data(), operation.payload.size(),
            operation.exr_value.data(), operation.exr_value.size(),
            &exr.value_offset);
        if (!name_valid || !value_valid
            || operation.exr_type_name != "string") {
            delete state;
            return make_handoff_error(TransferStatus::InternalError,
                                      PreparedTransferHandoffCode::InvalidState);
        }
        exr.name_size  = static_cast<uint64_t>(operation.exr_name.size());
        exr.value_size = static_cast<uint64_t>(operation.exr_value.size());
        exr.valid      = true;
        exr.is_opaque  = operation.exr_is_opaque;
    }

    out_instance->reset();
    out_instance->state_ = state;
    PreparedTransferHandoffResult result;
    result.operation_count = state->operation_count;
    return result;
}

PreparedTransferHandoffPatchResult
prepared_transfer_handoff_instance_time_patch_field(
    const PreparedTransferHandoffInstance& instance, TimePatchField field,
    PreparedTransferHandoffTimePatchFieldView* out_field) noexcept
{
    if (!out_field) {
        return make_patch_error(TransferStatus::InvalidArgument,
                                PreparedTransferHandoffPatchCode::NullFieldView);
    }
    const PreparedTransferHandoffInstanceState* state
        = const_handoff_instance_state(instance.state_);
    if (!state
        || state->contract_version
               != kPreparedTransferHandoffInstanceContractVersion) {
        return make_patch_error(TransferStatus::InvalidArgument,
                                PreparedTransferHandoffPatchCode::InvalidState);
    }
    if (!valid_time_patch_field(field)) {
        return make_patch_error(TransferStatus::InvalidArgument,
                                PreparedTransferHandoffPatchCode::InvalidField);
    }

    PreparedTransferHandoffTimePatchFieldView view;
    view.field = field;
    for (uint32_t si = 0U; si < state->time_patch_slot_count; ++si) {
        const TimePatchSlot& slot = state->time_patch_slots[si];
        if (slot.field != field) {
            continue;
        }
        if (slot.block_index >= state->block_count || slot.width == 0U) {
            return make_patch_error(
                TransferStatus::InternalError,
                PreparedTransferHandoffPatchCode::InvalidSlot, 0xFFFFFFFFU, si);
        }
        const PreparedTransferHandoffInstanceBlock& block
            = state->blocks[slot.block_index];
        const uint64_t slot_offset = static_cast<uint64_t>(slot.byte_offset);
        const uint64_t slot_width  = static_cast<uint64_t>(slot.width);
        if (block.payload_offset > state->payload_size
            || block.payload_size > state->payload_size - block.payload_offset
            || slot_offset > block.payload_size
            || slot_width > block.payload_size - slot_offset
            || (view.slot_count != 0U && view.width != slot.width)) {
            return make_patch_error(
                TransferStatus::InternalError,
                PreparedTransferHandoffPatchCode::InvalidSlot, 0xFFFFFFFFU, si);
        }
        view.width = slot.width;
        view.slot_count += 1U;
    }
    if (view.slot_count == 0U) {
        return make_patch_error(TransferStatus::InvalidArgument,
                                PreparedTransferHandoffPatchCode::SlotNotFound);
    }

    *out_field = view;
    return PreparedTransferHandoffPatchResult {};
}

PreparedTransferHandoffPatchResult
patch_prepared_transfer_handoff_instance(
    PreparedTransferHandoffInstance* instance,
    std::span<const TimePatchView> updates) noexcept
{
    if (!instance) {
        return make_patch_error(TransferStatus::InvalidArgument,
                                PreparedTransferHandoffPatchCode::NullInstance);
    }
    PreparedTransferHandoffInstanceState* state = handoff_instance_state(
        instance->state_);
    if (!state
        || state->contract_version
               != kPreparedTransferHandoffInstanceContractVersion) {
        return make_patch_error(TransferStatus::InvalidArgument,
                                PreparedTransferHandoffPatchCode::InvalidState);
    }
    if (updates.empty()) {
        return make_patch_error(TransferStatus::InvalidArgument,
                                PreparedTransferHandoffPatchCode::EmptyUpdates);
    }
    if (updates.size()
        > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return make_patch_error(TransferStatus::LimitExceeded,
                                PreparedTransferHandoffPatchCode::LimitExceeded);
    }

    uint64_t patch_count = 0U;
    for (size_t ui = 0U; ui < updates.size(); ++ui) {
        const uint32_t update_index = static_cast<uint32_t>(ui);
        const TimePatchView& update = updates[ui];
        if (!valid_time_patch_field(update.field)) {
            return make_patch_error(
                TransferStatus::InvalidArgument,
                PreparedTransferHandoffPatchCode::InvalidField, update_index);
        }
        for (size_t previous = 0U; previous < ui; ++previous) {
            if (updates[previous].field == update.field) {
                return make_patch_error(
                    TransferStatus::InvalidArgument,
                    PreparedTransferHandoffPatchCode::DuplicateField,
                    update_index);
            }
        }
        PreparedTransferHandoffTimePatchFieldView field_view;
        PreparedTransferHandoffPatchResult field_result
            = prepared_transfer_handoff_instance_time_patch_field(*instance,
                                                                  update.field,
                                                                  &field_view);
        if (!field_result.ok()) {
            field_result.failed_update_index = update_index;
            return field_result;
        }
        if (update.value.size() != field_view.width) {
            return make_patch_error(
                TransferStatus::InvalidArgument,
                PreparedTransferHandoffPatchCode::WidthMismatch, update_index);
        }
        if (pointer_ranges_overlap(update.value.data(), update.value.size(),
                                   state->payloads, state->payload_size)) {
            return make_patch_error(
                TransferStatus::InvalidArgument,
                PreparedTransferHandoffPatchCode::ValueAliasesInstance,
                update_index);
        }
        patch_count += field_view.slot_count;
        if (patch_count
            > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            return make_patch_error(
                TransferStatus::LimitExceeded,
                PreparedTransferHandoffPatchCode::LimitExceeded, update_index);
        }
    }

    for (size_t ui = 0U; ui < updates.size(); ++ui) {
        const TimePatchView& update = updates[ui];
        for (uint32_t si = 0U; si < state->time_patch_slot_count; ++si) {
            const TimePatchSlot& slot = state->time_patch_slots[si];
            if (slot.field != update.field) {
                continue;
            }
            const PreparedTransferHandoffInstanceBlock& block
                = state->blocks[slot.block_index];
            std::memcpy(state->payloads
                            + static_cast<size_t>(block.payload_offset)
                            + static_cast<size_t>(slot.byte_offset),
                        update.value.data(), update.value.size());
        }
    }

    PreparedTransferHandoffPatchResult result;
    result.patched_slots = static_cast<uint32_t>(patch_count);
    return result;
}

PreparedTransferHandoffResult
prepared_transfer_handoff_instance_operation(
    const PreparedTransferHandoffInstance& instance, uint32_t operation_index,
    PreparedTransferHandoffOperationView* out_operation) noexcept
{
    if (!out_operation) {
        return make_handoff_error(
            TransferStatus::InvalidArgument,
            PreparedTransferHandoffCode::NullOperationView);
    }
    const PreparedTransferHandoffInstanceState* state
        = const_handoff_instance_state(instance.state_);
    if (!state
        || state->contract_version
               != kPreparedTransferHandoffInstanceContractVersion) {
        return make_handoff_error(TransferStatus::InvalidArgument,
                                  PreparedTransferHandoffCode::InvalidState);
    }
    if (operation_index >= state->operation_count) {
        PreparedTransferHandoffResult result = make_handoff_error(
            TransferStatus::InvalidArgument,
            PreparedTransferHandoffCode::OperationIndexOutOfRange);
        result.failed_operation_index = operation_index;
        return result;
    }

    const PreparedTransferAdapterOp& operation
        = state->operations[operation_index];
    std::span<const std::byte> payload;
    if (!instance_block_payload(*state, operation.block_index, &payload)
        || operation.payload_size != payload.size()) {
        PreparedTransferHandoffResult result
            = make_handoff_error(TransferStatus::InternalError,
                                 PreparedTransferHandoffCode::InvalidState);
        result.failed_operation_index = operation_index;
        return result;
    }

    PreparedTransferHandoffOperationView view;
    view.semantic_kind = state->semantics[operation_index];
    view.operation     = operation;
    view.payload       = payload;
    if (operation.kind == TransferAdapterOpKind::ExrAttribute) {
        const PreparedTransferHandoffInstanceExrView& exr
            = state->exr_views[operation_index];
        if (!exr.valid || exr.name_offset > payload.size()
            || exr.name_size > payload.size() - exr.name_offset
            || exr.value_offset > payload.size()
            || exr.value_size > payload.size() - exr.value_offset) {
            PreparedTransferHandoffResult result
                = make_handoff_error(TransferStatus::InternalError,
                                     PreparedTransferHandoffCode::InvalidState);
            result.failed_operation_index = operation_index;
            return result;
        }
        view.exr_name = std::string_view(
            reinterpret_cast<const char*>(
                payload.data() + static_cast<size_t>(exr.name_offset)),
            static_cast<size_t>(exr.name_size));
        view.exr_type_name = "string";
        view.exr_value     = std::span<const std::byte>(
            payload.data() + static_cast<size_t>(exr.value_offset),
            static_cast<size_t>(exr.value_size));
        view.exr_is_opaque = exr.is_opaque;
    }

    *out_operation = view;
    PreparedTransferHandoffResult result;
    result.operation_count = 1U;
    return result;
}

PreparedTransferHandoffResult
replay_prepared_transfer_handoff_instance(
    const PreparedTransferHandoffInstance& instance,
    PreparedTransferHandoffReplayCallback callback, void* user) noexcept
{
    if (!callback) {
        return make_handoff_error(
            TransferStatus::InvalidArgument,
            PreparedTransferHandoffCode::NullReplayCallback);
    }
    if (!instance.valid()) {
        return make_handoff_error(TransferStatus::InvalidArgument,
                                  PreparedTransferHandoffCode::InvalidState);
    }

    PreparedTransferHandoffResult result;
    const uint32_t operation_count = instance.operation_count();
    for (uint32_t i = 0U; i < operation_count; ++i) {
        PreparedTransferHandoffOperationView operation;
        PreparedTransferHandoffResult resolved
            = prepared_transfer_handoff_instance_operation(instance, i,
                                                           &operation);
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
