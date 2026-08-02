// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_editing.h"

#include "openmeta/meta_edit.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include "metadata_logical_field_internal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    using LogicalFieldEntries
        = std::array<std::vector<EntryId>,
                     detail::kMetadataLogicalFieldKindCount>;

    static MetadataEditingResult
    editing_error(MetadataEditingStatus status, uint32_t operation_count,
                  uint32_t failed_operation_index) noexcept
    {
        MetadataEditingResult result;
        result.status                 = status;
        result.failed_operation_index = failed_operation_index;
        result.operation_count        = operation_count;
        return result;
    }

    static bool limits_are_valid(const MetadataEditingLimits& limits) noexcept
    {
        return limits.max_operations != 0U
               && limits.max_operations <= kMetadataEditingMaxOperations
               && limits.max_text_bytes_per_operation != 0U
               && limits.max_text_bytes_per_operation
                      <= kMetadataEditingMaxTextBytesPerOperation
               && limits.max_total_text_bytes != 0U
               && limits.max_total_text_bytes
                      <= kMetadataEditingMaxTotalTextBytes;
    }

    static std::string_view arena_text(const ByteArena& arena,
                                       ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool repeated_property_path_matches(std::string_view path,
                                               std::string_view base,
                                               uint32_t* parsed_index) noexcept
    {
        if (!parsed_index) {
            return false;
        }
        if (path == base) {
            *parsed_index = 0U;
            return true;
        }
        if (path.size() <= base.size() + 2U
            || path.substr(0U, base.size()) != base || path[base.size()] != '['
            || path.back() != ']') {
            return false;
        }

        uint32_t index = 0U;
        for (size_t i = base.size() + 1U; i + 1U < path.size(); ++i) {
            const char c = path[i];
            if (c < '0' || c > '9') {
                return false;
            }
            const uint32_t digit = static_cast<uint32_t>(c - '0');
            if (index > (std::numeric_limits<uint32_t>::max() - digit) / 10U) {
                return false;
            }
            index = (index * 10U) + digit;
        }
        if (index == 0U) {
            return false;
        }
        *parsed_index = index;
        return true;
    }

    static bool entry_matches_descriptor(
        const MetaStore& store, const Entry& entry,
        const detail::MetadataLogicalFieldDescriptor& descriptor,
        uint32_t* repeated_index) noexcept
    {
        if (entry.key.kind != MetaKeyKind::XmpProperty) {
            return false;
        }
        const std::string_view schema
            = arena_text(store.arena(), entry.key.data.xmp_property.schema_ns);
        if (schema != descriptor.schema_ns) {
            return false;
        }
        const std::string_view path
            = arena_text(store.arena(),
                         entry.key.data.xmp_property.property_path);
        if (!descriptor.repeated) {
            return path == descriptor.property_path;
        }
        return repeated_property_path_matches(path, descriptor.property_path,
                                              repeated_index);
    }

    static void collect_logical_entries(
        const MetaStore& base, LogicalFieldEntries* active,
        std::array<uint32_t, detail::kMetadataLogicalFieldKindCount>*
            repeated_max) noexcept
    {
        if (!active || !repeated_max) {
            return;
        }
        const std::span<const Entry> entries = base.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (entry.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }
            for (size_t k = 0U; k < active->size(); ++k) {
                const MetadataCreationFieldKind kind
                    = static_cast<MetadataCreationFieldKind>(k);
                detail::MetadataLogicalFieldDescriptor descriptor;
                if (!detail::metadata_logical_field_descriptor(kind,
                                                               &descriptor)) {
                    continue;
                }
                uint32_t repeated_index = 0U;
                if (!entry_matches_descriptor(base, entry, descriptor,
                                              &repeated_index)) {
                    continue;
                }
                if (descriptor.repeated
                    && repeated_index > (*repeated_max)[k]) {
                    (*repeated_max)[k] = repeated_index;
                }
                if (!any(entry.flags, EntryFlags::Deleted)) {
                    (*active)[k].push_back(id);
                }
                break;
            }
        }
    }

    static void select_addition_origin(const MetaStore& base, BlockId* block,
                                       uint32_t* next_order) noexcept
    {
        if (!block || !next_order) {
            return;
        }
        *block      = kInvalidBlockId;
        *next_order = 0U;

        bool have_order                      = false;
        const std::span<const Entry> entries = base.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::XmpProperty
                || entry.origin.block >= base.block_count()) {
                continue;
            }
            if (*block == kInvalidBlockId) {
                *block = entry.origin.block;
            }
            if (entry.origin.block != *block) {
                continue;
            }
            if (!have_order || entry.origin.order_in_block >= *next_order) {
                have_order  = true;
                *next_order = entry.origin.order_in_block
                                      == std::numeric_limits<uint32_t>::max()
                                  ? std::numeric_limits<uint32_t>::max()
                                  : entry.origin.order_in_block + 1U;
            }
        }
    }

    static bool make_edit_value(MetaEdit* edit,
                                const MetadataCreationField& field,
                                MetaValue* out) noexcept
    {
        if (!edit || !out) {
            return false;
        }
        switch (field.value_kind) {
        case MetadataCreationValueKind::Text:
            *out = make_text(edit->arena(), field.text, TextEncoding::Utf8);
            return out->data.span.size == field.text.size();
        case MetadataCreationValueKind::UnsignedInteger:
            *out = make_u32(field.unsigned_value);
            return true;
        case MetadataCreationValueKind::SignedInteger:
            *out = make_i32(field.signed_value);
            return true;
        case MetadataCreationValueKind::UnsignedRational:
            *out = make_urational(field.rational.numer, field.rational.denom);
            return true;
        }
        return false;
    }

    static MetadataEditingStatus validate_value_operation(
        const MetadataEditingOperation& operation,
        const detail::MetadataLogicalFieldDescriptor& descriptor,
        const MetadataEditingLimits& limits,
        uint64_t* total_text_bytes) noexcept
    {
        const MetadataCreationField& field = operation.field;
        if (field.value_kind != descriptor.value_kind) {
            return MetadataEditingStatus::WrongValueKind;
        }
        if (field.value_kind != MetadataCreationValueKind::Text) {
            return detail::metadata_logical_field_value_is_valid(field)
                       ? MetadataEditingStatus::Ok
                       : MetadataEditingStatus::InvalidValue;
        }
        if (field.text.empty()) {
            return MetadataEditingStatus::EmptyText;
        }
        if (field.text.size() > limits.max_text_bytes_per_operation
            || field.text.size() > kMetadataEditingMaxTextBytesPerOperation) {
            return MetadataEditingStatus::TextTooLong;
        }
        if (!total_text_bytes) {
            return MetadataEditingStatus::InternalError;
        }
        *total_text_bytes += field.text.size();
        if (*total_text_bytes > limits.max_total_text_bytes
            || *total_text_bytes > kMetadataEditingMaxTotalTextBytes) {
            return MetadataEditingStatus::TotalTextTooLong;
        }
        return detail::metadata_logical_text_is_valid(field.text)
                   ? MetadataEditingStatus::Ok
                   : MetadataEditingStatus::InvalidText;
    }

    static bool
    append_edit_entry(MetaEdit* edit, const MetadataCreationField& field,
                      const detail::MetadataLogicalFieldDescriptor& descriptor,
                      uint32_t repeated_index, BlockId block,
                      uint32_t order) noexcept
    {
        if (!edit) {
            return false;
        }
        std::array<char, 48U> indexed_path {};
        std::string_view path = descriptor.property_path;
        if (descriptor.repeated) {
            path = detail::metadata_logical_indexed_property_path(
                descriptor.property_path, repeated_index, &indexed_path);
            if (path.empty()) {
                return false;
            }
        }

        Entry entry;
        entry.key = make_xmp_property_key(edit->arena(), descriptor.schema_ns,
                                          path);
        if (entry.key.data.xmp_property.schema_ns.size
                != descriptor.schema_ns.size()
            || entry.key.data.xmp_property.property_path.size != path.size()
            || !make_edit_value(edit, field, &entry.value)) {
            return false;
        }
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.flags                 = EntryFlags::Dirty;
        edit->add_entry(entry);
        return true;
    }

}  // namespace

MetadataEditingOperation
make_metadata_edit_add(const MetadataCreationField& field) noexcept
{
    MetadataEditingOperation operation;
    operation.kind  = MetadataEditingOperationKind::Add;
    operation.field = field;
    return operation;
}


MetadataEditingOperation
make_metadata_edit_set(const MetadataCreationField& field,
                       uint32_t occurrence) noexcept
{
    MetadataEditingOperation operation;
    operation.kind       = MetadataEditingOperationKind::Set;
    operation.field      = field;
    operation.occurrence = occurrence;
    return operation;
}


MetadataEditingOperation
make_metadata_edit_remove(MetadataCreationFieldKind kind,
                          uint32_t occurrence) noexcept
{
    MetadataEditingOperation operation;
    operation.kind       = MetadataEditingOperationKind::Remove;
    operation.field.kind = kind;
    operation.occurrence = occurrence;
    return operation;
}


MetadataEditingOperation
make_metadata_edit_remove_all(MetadataCreationFieldKind kind) noexcept
{
    return make_metadata_edit_remove(kind, kMetadataEditingAllOccurrences);
}


MetadataEditingResult
edit_metadata(const MetaStore& base, const MetadataEditingRequest& request,
              MetaStore* out_store)
{
    const uint32_t operation_count
        = request.operations.size() > std::numeric_limits<uint32_t>::max()
              ? std::numeric_limits<uint32_t>::max()
              : static_cast<uint32_t>(request.operations.size());
    if (!out_store) {
        return editing_error(MetadataEditingStatus::NullOutput, operation_count,
                             kInvalidMetadataEditingOperationIndex);
    }
    if (!base.is_finalized()) {
        return editing_error(MetadataEditingStatus::BaseNotFinalized,
                             operation_count,
                             kInvalidMetadataEditingOperationIndex);
    }
    if (!limits_are_valid(request.limits)) {
        return editing_error(MetadataEditingStatus::InvalidLimits,
                             operation_count,
                             kInvalidMetadataEditingOperationIndex);
    }
    if (request.operations.size() > request.limits.max_operations
        || request.operations.size() > kMetadataEditingMaxOperations) {
        return editing_error(MetadataEditingStatus::TooManyOperations,
                             operation_count,
                             kInvalidMetadataEditingOperationIndex);
    }
    if (request.operations.empty()) {
        *out_store = base;
        MetadataEditingResult result;
        return result;
    }

    LogicalFieldEntries active;
    std::array<uint32_t, detail::kMetadataLogicalFieldKindCount> repeated_max {};
    collect_logical_entries(base, &active, &repeated_max);

    BlockId addition_block  = kInvalidBlockId;
    uint32_t addition_order = 0U;
    select_addition_origin(base, &addition_block, &addition_order);

    MetaEdit edit;
    edit.reserve_ops(request.operations.size());
    uint64_t total_text_bytes = 0U;
    uint32_t added            = 0U;
    uint32_t updated          = 0U;
    uint32_t removed          = 0U;

    for (uint32_t i = 0U; i < operation_count; ++i) {
        const MetadataEditingOperation& operation = request.operations[i];
        detail::MetadataLogicalFieldDescriptor descriptor;
        if (!detail::metadata_logical_field_descriptor(operation.field.kind,
                                                       &descriptor)) {
            return editing_error(MetadataEditingStatus::WrongValueKind,
                                 operation_count, i);
        }
        const size_t kind_index = static_cast<size_t>(operation.field.kind);
        if (kind_index >= active.size()) {
            return editing_error(MetadataEditingStatus::WrongValueKind,
                                 operation_count, i);
        }
        std::vector<EntryId>& matches = active[kind_index];

        if (operation.kind == MetadataEditingOperationKind::Add) {
            if (operation.occurrence != 0U) {
                return editing_error(MetadataEditingStatus::InvalidOccurrence,
                                     operation_count, i);
            }
            const MetadataEditingStatus value_status
                = validate_value_operation(operation, descriptor,
                                           request.limits, &total_text_bytes);
            if (value_status != MetadataEditingStatus::Ok) {
                return editing_error(value_status, operation_count, i);
            }
            if (!descriptor.repeated && !matches.empty()) {
                return editing_error(
                    MetadataEditingStatus::SingletonAlreadyExists,
                    operation_count, i);
            }
            const uint64_t predicted_id
                = static_cast<uint64_t>(base.entries().size()) + added;
            if (predicted_id >= kInvalidEntryId) {
                return editing_error(MetadataEditingStatus::EntryLimitExceeded,
                                     operation_count, i);
            }
            uint32_t repeated_index = 0U;
            if (descriptor.repeated) {
                if (repeated_max[kind_index]
                    == std::numeric_limits<uint32_t>::max()) {
                    return editing_error(
                        MetadataEditingStatus::EntryLimitExceeded,
                        operation_count, i);
                }
                repeated_index = ++repeated_max[kind_index];
                if (repeated_index == 0U) {
                    repeated_index = ++repeated_max[kind_index];
                }
            }
            if (!append_edit_entry(&edit, operation.field, descriptor,
                                   repeated_index, addition_block,
                                   addition_order)) {
                return editing_error(MetadataEditingStatus::InternalError,
                                     operation_count, i);
            }
            matches.push_back(static_cast<EntryId>(predicted_id));
            if (addition_order != std::numeric_limits<uint32_t>::max()) {
                ++addition_order;
            }
            ++added;
            continue;
        }

        if (operation.kind == MetadataEditingOperationKind::Set) {
            if (operation.occurrence == kMetadataEditingAllOccurrences
                || (!descriptor.repeated && operation.occurrence != 0U)) {
                return editing_error(MetadataEditingStatus::InvalidOccurrence,
                                     operation_count, i);
            }
            const MetadataEditingStatus value_status
                = validate_value_operation(operation, descriptor,
                                           request.limits, &total_text_bytes);
            if (value_status != MetadataEditingStatus::Ok) {
                return editing_error(value_status, operation_count, i);
            }
            if (!descriptor.repeated && matches.size() > 1U) {
                return editing_error(MetadataEditingStatus::AmbiguousTarget,
                                     operation_count, i);
            }
            if (operation.occurrence >= matches.size()) {
                return editing_error(MetadataEditingStatus::TargetNotFound,
                                     operation_count, i);
            }
            MetaValue value;
            if (!make_edit_value(&edit, operation.field, &value)) {
                return editing_error(MetadataEditingStatus::InternalError,
                                     operation_count, i);
            }
            edit.set_value(matches[operation.occurrence], value);
            ++updated;
            continue;
        }

        if (operation.kind == MetadataEditingOperationKind::Remove) {
            if (!descriptor.repeated && operation.occurrence != 0U
                && operation.occurrence != kMetadataEditingAllOccurrences) {
                return editing_error(MetadataEditingStatus::InvalidOccurrence,
                                     operation_count, i);
            }
            if (matches.empty()) {
                return editing_error(MetadataEditingStatus::TargetNotFound,
                                     operation_count, i);
            }
            if (operation.occurrence == kMetadataEditingAllOccurrences) {
                for (size_t m = 0U; m < matches.size(); ++m) {
                    edit.tombstone(matches[m]);
                    ++removed;
                }
                matches.clear();
                continue;
            }
            if (!descriptor.repeated && matches.size() > 1U) {
                return editing_error(MetadataEditingStatus::AmbiguousTarget,
                                     operation_count, i);
            }
            if (operation.occurrence >= matches.size()) {
                return editing_error(MetadataEditingStatus::TargetNotFound,
                                     operation_count, i);
            }
            edit.tombstone(matches[operation.occurrence]);
            matches.erase(matches.begin() + operation.occurrence);
            ++removed;
            continue;
        }

        return editing_error(MetadataEditingStatus::InvalidOperationKind,
                             operation_count, i);
    }

    MetaStore edited = commit(base, std::span<const MetaEdit>(&edit, 1U));
    *out_store       = std::move(edited);

    MetadataEditingResult result;
    result.operation_count    = operation_count;
    result.operations_applied = operation_count;
    result.entries_added      = added;
    result.entries_updated    = updated;
    result.entries_removed    = removed;
    return result;
}


const char*
metadata_editing_operation_kind_name(MetadataEditingOperationKind kind) noexcept
{
    switch (kind) {
    case MetadataEditingOperationKind::Add: return "add";
    case MetadataEditingOperationKind::Set: return "set";
    case MetadataEditingOperationKind::Remove: return "remove";
    }
    return "unknown";
}


const char*
metadata_editing_status_name(MetadataEditingStatus status) noexcept
{
    switch (status) {
    case MetadataEditingStatus::Ok: return "ok";
    case MetadataEditingStatus::NullOutput: return "null_output";
    case MetadataEditingStatus::BaseNotFinalized: return "base_not_finalized";
    case MetadataEditingStatus::InvalidLimits: return "invalid_limits";
    case MetadataEditingStatus::TooManyOperations: return "too_many_operations";
    case MetadataEditingStatus::InvalidOperationKind:
        return "invalid_operation_kind";
    case MetadataEditingStatus::InvalidOccurrence: return "invalid_occurrence";
    case MetadataEditingStatus::WrongValueKind: return "wrong_value_kind";
    case MetadataEditingStatus::EmptyText: return "empty_text";
    case MetadataEditingStatus::TextTooLong: return "text_too_long";
    case MetadataEditingStatus::TotalTextTooLong: return "total_text_too_long";
    case MetadataEditingStatus::InvalidText: return "invalid_text";
    case MetadataEditingStatus::InvalidValue: return "invalid_value";
    case MetadataEditingStatus::SingletonAlreadyExists:
        return "singleton_already_exists";
    case MetadataEditingStatus::TargetNotFound: return "target_not_found";
    case MetadataEditingStatus::AmbiguousTarget: return "ambiguous_target";
    case MetadataEditingStatus::EntryLimitExceeded:
        return "entry_limit_exceeded";
    case MetadataEditingStatus::InternalError: return "internal_error";
    }
    return "unknown";
}

}  // namespace openmeta
