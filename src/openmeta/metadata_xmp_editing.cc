// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_xmp_editing.h"

#include "metadata_logical_field_internal.h"
#include "metadata_validation_options_internal.h"
#include "metadata_xmp_editing_internal.h"

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static constexpr uint32_t kInvalidOperation
        = kInvalidMetadataEditingOperationIndex;
    static constexpr size_t kMaxRootGroups = 9U;

    struct RootGroup final {
        std::string_view schema_ns;
        std::string_view root;
        uint32_t first_operation = kInvalidOperation;
    };

    struct PreparedOperation final {
        const MetadataXmpEditingOperation* input = nullptr;
        detail::XmpStructuredEditScope scope;
        uint32_t root_group = 0U;
    };

    struct TrackedLeaf final {
        MetaKeyView key;
        detail::XmpStructuredEditLeaf leaf;
        WireType wire_type;
        uint32_t wire_count = 0U;
        uint32_t root_group = 0U;
        bool active = false;
    };

    struct PreparedLeaf final {
        const MetadataAuthoringEntry* entry = nullptr;
        detail::XmpStructuredEditLeaf leaf;
    };

    static MetadataXmpEditingResult xmp_edit_error(
        MetadataXmpEditingStatus status,
        uint32_t operation = kInvalidOperation) noexcept
    {
        MetadataXmpEditingResult result;
        result.status                 = status;
        result.failed_operation_index = operation;
        return result;
    }

    static bool options_are_valid(
        const MetadataXmpEditingOptions& options) noexcept
    {
        const MetadataTypedEditingOptions& typed = options.typed;
        return options.max_operations != 0U
               && options.max_operations <= kMetadataXmpEditingMaxOperations
               && options.max_expanded_operations != 0U
               && options.max_expanded_operations
                      <= kMetadataXmpEditingMaxExpandedOperations
               && options.max_scope_entries != 0U
               && options.max_scope_entries
                      <= kMetadataXmpEditingMaxScopeEntries
               && typed.max_operations != 0U
               && typed.max_operations <= kMetadataTypedEditingMaxOperations
               && typed.max_entries != 0U
               && typed.max_entries < kInvalidEntryId
               && typed.max_key_bytes != 0U && typed.max_arena_bytes != 0U
               && typed.max_arena_bytes <= UINT32_MAX
               && typed.max_value_bytes != 0U
               && typed.max_value_bytes <= typed.max_arena_bytes
               && typed.max_request_bytes != 0U
               && typed.max_request_bytes <= UINT32_MAX
               && static_cast<uint8_t>(typed.add_policy)
                      <= static_cast<uint8_t>(
                          MetadataTypedEditingAddPolicy::Append)
               && detail::metadata_validation_options_are_valid(
                   typed.validation);
    }

    static std::string_view arena_text(const ByteArena& arena,
                                       ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        if (bytes.size() != span.size || bytes.empty())
            return {};
        return std::string_view(
            reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    static bool entry_key_view(const MetaStore& store, const Entry& entry,
                               MetaKeyView* out) noexcept
    {
        if (!out || entry.key.kind != MetaKeyKind::XmpProperty)
            return false;
        const std::string_view schema
            = arena_text(store.arena(), entry.key.data.xmp_property.schema_ns);
        const std::string_view path
            = arena_text(store.arena(), entry.key.data.xmp_property.property_path);
        if (schema.empty() || path.empty())
            return false;
        MetaKeyView key;
        key.kind = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = schema;
        key.data.xmp_property.property_path = path;
        *out = key;
        return true;
    }

    static bool same_key(const MetaKeyView& first,
                         const MetaKeyView& second) noexcept
    {
        return first.kind == MetaKeyKind::XmpProperty
               && second.kind == MetaKeyKind::XmpProperty
               && first.data.xmp_property.schema_ns
                      == second.data.xmp_property.schema_ns
               && first.data.xmp_property.property_path
                      == second.data.xmp_property.property_path;
    }

    static bool leaf_in_scope(
        const detail::XmpStructuredEditScope& scope,
        const detail::XmpStructuredEditLeaf& leaf) noexcept
    {
        if (scope.root != leaf.root)
            return false;
        switch (scope.kind) {
        case detail::XmpStructuredEditScopeKind::CoreStructure:
            return !leaf.has_item_index;
        case detail::XmpStructuredEditScopeKind::StructuredArray:
        case detail::XmpStructuredEditScopeKind::IndexedTextArray:
            return leaf.has_item_index
                   && (!scope.has_item_index
                       || scope.item_index == leaf.item_index);
        case detail::XmpStructuredEditScopeKind::LangAlt:
            return leaf.has_language
                   && (!scope.has_language
                       || scope.language == leaf.language);
        }
        return false;
    }

    static bool same_shape_node(const detail::XmpStructuredEditLeaf& first,
                                const detail::XmpStructuredEditLeaf& second) noexcept
    {
        return first.has_item_index == second.has_item_index
               && (!first.has_item_index
                   || first.item_index == second.item_index)
               && first.shape_path == second.shape_path;
    }

    static bool same_array_item(
        const detail::XmpStructuredEditLeaf& first,
        const detail::XmpStructuredEditLeaf& second) noexcept
    {
        return first.has_item_index == second.has_item_index
               && (!first.has_item_index
                   || first.item_index == second.item_index);
    }

    static bool is_strict_path_ancestor(std::string_view ancestor,
                                        std::string_view descendant) noexcept
    {
        return descendant.size() > ancestor.size()
               && descendant.substr(0U, ancestor.size()) == ancestor
               && descendant[ancestor.size()] == '/';
    }

    static bool leaf_shape_collision(
        const detail::XmpStructuredEditLeaf& first,
        const detail::XmpStructuredEditLeaf& second) noexcept
    {
        if (!same_array_item(first, second))
            return false;
        if (same_shape_node(first, second))
            return first.shape != second.shape;
        return is_strict_path_ancestor(first.shape_path, second.shape_path)
               || is_strict_path_ancestor(second.shape_path,
                                          first.shape_path);
    }

    static bool base_text_is_valid(const MetaStore& store, const Entry& entry,
                                   uint64_t max_value_bytes) noexcept
    {
        if (any(entry.flags,
                EntryFlags::Truncated | EntryFlags::Unreadable)
            || entry.value.kind != MetaValueKind::Text
            || entry.value.elem_type != MetaElementType::U8
            || (entry.value.text_encoding != TextEncoding::Ascii
                && entry.value.text_encoding != TextEncoding::Utf8)
            || entry.value.count != entry.value.data.span.size
            || entry.value.count > max_value_bytes)
            return false;
        const std::span<const std::byte> bytes
            = store.arena().span(entry.value.data.span);
        if (bytes.size() != entry.value.data.span.size)
            return false;
        std::string_view text;
        if (!bytes.empty()) {
            text = std::string_view(
                reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        if (!detail::metadata_logical_text_is_valid(text))
            return false;
        if (entry.value.text_encoding == TextEncoding::Ascii) {
            for (const char value : text) {
                if (static_cast<unsigned char>(value) > 0x7fU)
                    return false;
            }
        }
        return true;
    }

    static bool input_text_shape_is_valid(const MetadataAuthoringEntry& entry,
                                          uint64_t max_value_bytes) noexcept
    {
        if (entry.value.kind != MetaValueKind::Text
            || entry.value.elem_type != MetaElementType::U8
            || (entry.value.text_encoding != TextEncoding::Ascii
                && entry.value.text_encoding != TextEncoding::Utf8)
            || entry.value.count != entry.value.payload.size()
            || entry.value.payload.size() > max_value_bytes)
            return false;
        return entry.wire_type.family == WireFamily::None
               && entry.wire_type.code == 0U && entry.wire_count == 0U;
    }

    static bool input_text_contents_are_valid(
        const MetadataAuthoringEntry& entry) noexcept
    {
        std::string_view text;
        if (!entry.value.payload.empty()) {
            text = std::string_view(reinterpret_cast<const char*>(
                                        entry.value.payload.data()),
                                    entry.value.payload.size());
        }
        if (!detail::metadata_logical_text_is_valid(text))
            return false;
        if (entry.value.text_encoding == TextEncoding::Ascii) {
            for (const char value : text) {
                if (static_cast<unsigned char>(value) > 0x7fU)
                    return false;
            }
        }
        return true;
    }

    static bool bytes_fit(uint64_t current, uint64_t add,
                          uint64_t limit) noexcept
    {
        return current <= limit && add <= limit - current;
    }

    static uint64_t key_size(const MetaKeyView& key) noexcept
    {
        return static_cast<uint64_t>(key.data.xmp_property.schema_ns.size())
               + static_cast<uint64_t>(
                   key.data.xmp_property.property_path.size());
    }

    static bool add_target(RootGroup* groups, size_t* group_count,
                           std::string_view schema_ns, std::string_view root,
                           uint32_t operation, uint32_t* group_index) noexcept
    {
        if (!groups || !group_count || !group_index)
            return false;
        for (size_t i = 0U; i < *group_count; ++i) {
            if (groups[i].schema_ns == schema_ns && groups[i].root == root) {
                *group_index = static_cast<uint32_t>(i);
                if (operation < groups[i].first_operation)
                    groups[i].first_operation = operation;
                return true;
            }
        }
        if (*group_count >= kMaxRootGroups)
            return false;
        RootGroup& group       = groups[*group_count];
        group.schema_ns        = schema_ns;
        group.root             = root;
        group.first_operation  = operation;
        *group_index           = static_cast<uint32_t>(*group_count);
        ++(*group_count);
        return true;
    }

    static bool append_expanded(
        std::vector<MetadataTypedEditingOperation>* expanded,
        std::vector<uint32_t>* owner_operation,
        MetadataEditingOperationKind kind, const MetadataAuthoringEntry& entry,
        uint32_t operation_index, uint32_t max_expanded,
        uint64_t* request_bytes, uint64_t max_request_bytes) noexcept
    {
        if (!expanded || !owner_operation || !request_bytes
            || expanded->size() >= max_expanded)
            return false;
        const uint64_t bytes
            = key_size(entry.key)
              + (kind == MetadataEditingOperationKind::Remove
                     ? 0U
                     : static_cast<uint64_t>(entry.value.payload.size()));
        if (!bytes_fit(*request_bytes, bytes, max_request_bytes))
            return false;
        MetadataTypedEditingOperation typed;
        typed.kind      = kind;
        typed.entry     = entry;
        typed.occurrence = kMetadataTypedEditingUniqueOccurrence;
        expanded->push_back(typed);
        owner_operation->push_back(operation_index);
        *request_bytes += bytes;
        return true;
    }

    static void compact_state(std::vector<TrackedLeaf>* state) noexcept
    {
        if (!state)
            return;
        size_t destination = 0U;
        for (size_t i = 0U; i < state->size(); ++i) {
            if (!(*state)[i].active)
                continue;
            if (destination != i)
                (*state)[destination] = (*state)[i];
            ++destination;
        }
        state->resize(destination);
    }

    static bool state_key_present(const std::vector<TrackedLeaf>& state,
                                  uint32_t root_group,
                                  const MetaKeyView& key) noexcept
    {
        for (const TrackedLeaf& current : state) {
            if (current.active && current.root_group == root_group
                && same_key(current.key, key))
                return true;
        }
        return false;
    }

    static MetadataAuthoringEntry remove_entry_for_key(
        const MetaKeyView& key) noexcept
    {
        MetadataAuthoringEntry entry;
        entry.key = key;
        return entry;
    }

    static bool scope_selection_count(const std::vector<TrackedLeaf>& state,
                                      uint32_t root_group,
                                      const detail::XmpStructuredEditScope& scope,
                                      uint32_t* count) noexcept
    {
        if (!count)
            return false;
        *count = 0U;
        for (const TrackedLeaf& current : state) {
            if (current.active && current.root_group == root_group
                && leaf_in_scope(scope, current.leaf)) {
                if (*count == UINT32_MAX)
                    return false;
                ++(*count);
            }
        }
        return true;
    }

}  // namespace

MetadataXmpEditingResult
edit_metadata_xmp(const MetaStore& base,
                  std::span<const MetadataXmpEditingOperation> operations,
                  MetaStore* output,
                  const MetadataXmpEditingOptions& options) noexcept
{
    if (!output)
        return xmp_edit_error(MetadataXmpEditingStatus::NullOutput);
    if (!base.is_finalized())
        return xmp_edit_error(MetadataXmpEditingStatus::BaseNotFinalized);
    if (!options_are_valid(options))
        return xmp_edit_error(MetadataXmpEditingStatus::InvalidOptions);
    if (base.entries().size() > options.typed.max_entries
        || base.arena().bytes().size() > options.typed.max_arena_bytes
        || base.resource_limit_exceeded())
        return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded);
    if (operations.size() > options.max_operations
        || operations.size() > kMetadataXmpEditingMaxOperations)
        return xmp_edit_error(MetadataXmpEditingStatus::TooManyOperations);

    std::vector<PreparedOperation> prepared;
    prepared.reserve(operations.size());
    std::array<RootGroup, kMaxRootGroups> groups {};
    size_t group_count = 0U;
    for (uint32_t i = 0U; i < operations.size(); ++i) {
        const MetadataXmpEditingOperation& operation = operations[i];
        if (static_cast<uint8_t>(operation.kind)
            > static_cast<uint8_t>(MetadataXmpEditingOperationKind::Remove))
            return xmp_edit_error(MetadataXmpEditingStatus::InvalidOperation,
                                  i);
        if (operation.schema_ns.size() > options.typed.max_key_bytes
            || operation.property_path.size() > options.typed.max_key_bytes)
            return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded, i);
        detail::XmpStructuredEditScope scope;
        if (!detail::parse_xmp_structured_edit_scope(
                operation.schema_ns, operation.property_path, &scope))
            return xmp_edit_error(MetadataXmpEditingStatus::InvalidScope, i);
        if ((operation.kind == MetadataXmpEditingOperationKind::Add
             || operation.kind == MetadataXmpEditingOperationKind::Replace)
            && operation.entries.empty())
            return xmp_edit_error(MetadataXmpEditingStatus::EmptyEntries, i);
        if (operation.kind == MetadataXmpEditingOperationKind::Remove
            && !operation.entries.empty())
            return xmp_edit_error(MetadataXmpEditingStatus::UnexpectedEntries,
                                  i);
        uint32_t root_group = 0U;
        if (!add_target(groups.data(), &group_count, scope.schema_ns,
                        scope.root, i, &root_group))
            return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded, i);
        PreparedOperation item;
        item.input      = &operation;
        item.scope      = scope;
        item.root_group = root_group;
        prepared.push_back(item);
    }

    std::vector<TrackedLeaf> state;
    state.reserve(options.max_scope_entries);
    const std::span<const Entry> base_entries = base.entries();
    for (EntryId id = 0U; id < base_entries.size(); ++id) {
        const Entry& entry = base_entries[id];
        if (any(entry.flags, EntryFlags::Deleted)
            || entry.key.kind != MetaKeyKind::XmpProperty)
            continue;
        const std::string_view schema
            = arena_text(base.arena(), entry.key.data.xmp_property.schema_ns);
        if (schema.empty())
            continue;
        for (uint32_t group = 0U; group < group_count; ++group) {
            if (groups[group].schema_ns != schema)
                continue;
            const std::string_view path
                = arena_text(base.arena(), entry.key.data.xmp_property.property_path);
            if (path.empty())
                return xmp_edit_error(
                    MetadataXmpEditingStatus::UnsupportedSelectedEntry,
                    groups[group].first_operation);
            if (!detail::xmp_structured_edit_path_has_root(
                    schema, path, groups[group].root))
                continue;

            MetaKeyView key;
            detail::XmpStructuredEditLeaf leaf;
            if (!entry_key_view(base, entry, &key)
                || !detail::parse_xmp_structured_edit_leaf(schema, path, &leaf)
                || leaf.root != groups[group].root
                || !base_text_is_valid(base, entry,
                                       options.typed.max_value_bytes))
                return xmp_edit_error(
                    MetadataXmpEditingStatus::UnsupportedSelectedEntry,
                    groups[group].first_operation);
            if (state.size() >= options.max_scope_entries)
                return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded,
                                      groups[group].first_operation);
            TrackedLeaf tracked;
            tracked.key        = key;
            tracked.leaf       = leaf;
            tracked.wire_type  = entry.origin.wire_type;
            tracked.wire_count = entry.origin.wire_count;
            tracked.root_group = group;
            tracked.active     = true;
            state.push_back(tracked);
        }
    }

    for (size_t i = 0U; i < state.size(); ++i) {
        for (size_t j = i + 1U; j < state.size(); ++j) {
            if (state[i].root_group != state[j].root_group)
                continue;
            if (same_key(state[i].key, state[j].key))
                return xmp_edit_error(
                    MetadataXmpEditingStatus::DuplicateEntry,
                    groups[state[i].root_group].first_operation);
            if (leaf_shape_collision(state[i].leaf, state[j].leaf))
                return xmp_edit_error(
                    MetadataXmpEditingStatus::ShapeCollision,
                    groups[state[i].root_group].first_operation);
        }
    }

    std::vector<MetadataTypedEditingOperation> expanded;
    std::vector<uint32_t> expanded_owner;
    const uint32_t max_expanded
        = std::min(options.max_expanded_operations,
                   options.typed.max_operations);
    expanded.reserve(max_expanded);
    expanded_owner.reserve(max_expanded);
    uint64_t request_bytes = 0U;
    uint64_t total_input_entries = 0U;
    uint64_t total_input_bytes = 0U;

    for (uint32_t i = 0U; i < prepared.size(); ++i) {
        const PreparedOperation& current = prepared[i];
        const MetadataXmpEditingOperation& operation = *current.input;
        if (total_input_entries > options.max_scope_entries
            || operation.entries.size()
                   > options.max_scope_entries - total_input_entries)
            return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded, i);
        total_input_entries += operation.entries.size();

        std::vector<PreparedLeaf> input_leaves;
        input_leaves.reserve(operation.entries.size());
        for (const MetadataAuthoringEntry& entry : operation.entries) {
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || entry.key.data.xmp_property.schema_ns
                       != operation.schema_ns
                || entry.key.data.xmp_property.schema_ns.empty()
                || entry.key.data.xmp_property.property_path.empty()
                || entry.key.data.xmp_property.schema_ns.size()
                       > options.typed.max_key_bytes
                || entry.key.data.xmp_property.property_path.size()
                       > options.typed.max_key_bytes
                || !input_text_shape_is_valid(
                    entry, options.typed.max_value_bytes))
                return xmp_edit_error(MetadataXmpEditingStatus::InvalidEntry,
                                      i);
            const uint64_t supplied_key_bytes = key_size(entry.key);
            const uint64_t supplied_value_bytes
                = static_cast<uint64_t>(entry.value.payload.size());
            if (!bytes_fit(total_input_bytes, supplied_key_bytes,
                           options.typed.max_request_bytes))
                return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded,
                                      i);
            total_input_bytes += supplied_key_bytes;
            if (!bytes_fit(total_input_bytes, supplied_value_bytes,
                           options.typed.max_request_bytes))
                return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded,
                                      i);
            total_input_bytes += supplied_value_bytes;

            detail::XmpStructuredEditLeaf leaf;
            if (!detail::parse_xmp_structured_edit_leaf(
                    entry.key.data.xmp_property.schema_ns,
                    entry.key.data.xmp_property.property_path, &leaf))
                return xmp_edit_error(MetadataXmpEditingStatus::UnsupportedPath,
                                      i);
            if (leaf.root != current.scope.root
                || !leaf_in_scope(current.scope, leaf))
                return xmp_edit_error(MetadataXmpEditingStatus::InvalidEntry,
                                      i);
            if (!input_text_contents_are_valid(entry))
                return xmp_edit_error(MetadataXmpEditingStatus::InvalidEntry,
                                      i);
            PreparedLeaf prepared_leaf;
            prepared_leaf.entry = &entry;
            prepared_leaf.leaf  = leaf;
            for (const PreparedLeaf& earlier : input_leaves) {
                if (same_key(earlier.entry->key, entry.key))
                    return xmp_edit_error(
                        MetadataXmpEditingStatus::DuplicateEntry, i);
                if (leaf_shape_collision(earlier.leaf, leaf))
                    return xmp_edit_error(
                        MetadataXmpEditingStatus::ShapeCollision, i);
            }
            input_leaves.push_back(prepared_leaf);
        }

        uint32_t selected_count = 0U;
        if (!scope_selection_count(state, current.root_group, current.scope,
                                   &selected_count))
            return xmp_edit_error(MetadataXmpEditingStatus::InternalError, i);
        if (operation.kind == MetadataXmpEditingOperationKind::Add
            && selected_count != 0U)
            return xmp_edit_error(MetadataXmpEditingStatus::ScopeAlreadyExists,
                                  i);
        if ((operation.kind == MetadataXmpEditingOperationKind::Replace
             || operation.kind == MetadataXmpEditingOperationKind::Remove)
            && selected_count == 0U)
            return xmp_edit_error(MetadataXmpEditingStatus::ScopeNotFound, i);

        for (const TrackedLeaf& existing : state) {
            if (!existing.active || existing.root_group != current.root_group
                || leaf_in_scope(current.scope, existing.leaf))
                continue;
            for (const PreparedLeaf& proposed : input_leaves) {
                if (same_key(existing.key, proposed.entry->key))
                    return xmp_edit_error(
                        MetadataXmpEditingStatus::DuplicateEntry, i);
                if (leaf_shape_collision(existing.leaf, proposed.leaf))
                    return xmp_edit_error(
                        MetadataXmpEditingStatus::ShapeCollision, i);
            }
        }

        if (operation.kind == MetadataXmpEditingOperationKind::Add) {
            for (const PreparedLeaf& proposed : input_leaves) {
                if (!append_expanded(
                        &expanded, &expanded_owner,
                        MetadataEditingOperationKind::Add, *proposed.entry, i,
                        max_expanded, &request_bytes,
                        options.typed.max_request_bytes))
                    return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded,
                                          i);
            }
        } else if (operation.kind == MetadataXmpEditingOperationKind::Replace) {
            for (size_t old_index = 0U; old_index < state.size(); ++old_index) {
                TrackedLeaf& old = state[old_index];
                if (!old.active || old.root_group != current.root_group
                    || !leaf_in_scope(current.scope, old.leaf))
                    continue;
                size_t replacement = input_leaves.size();
                for (size_t next = 0U; next < input_leaves.size(); ++next) {
                    if (same_key(old.key, input_leaves[next].entry->key)) {
                        replacement = next;
                        break;
                    }
                }
                if (replacement == input_leaves.size()) {
                    const MetadataAuthoringEntry removal
                        = remove_entry_for_key(old.key);
                    if (!append_expanded(
                            &expanded, &expanded_owner,
                            MetadataEditingOperationKind::Remove, removal, i,
                            max_expanded, &request_bytes,
                            options.typed.max_request_bytes))
                        return xmp_edit_error(
                            MetadataXmpEditingStatus::LimitExceeded, i);
                    old.active = false;
                    continue;
                }
                MetadataAuthoringEntry replacement_entry
                    = *input_leaves[replacement].entry;
                replacement_entry.wire_type  = old.wire_type;
                replacement_entry.wire_count = old.wire_count;
                if (!append_expanded(
                        &expanded, &expanded_owner,
                        MetadataEditingOperationKind::Set, replacement_entry,
                        i, max_expanded, &request_bytes,
                        options.typed.max_request_bytes))
                    return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded,
                                          i);
            }
            for (const PreparedLeaf& proposed : input_leaves) {
                if (state_key_present(state, current.root_group,
                                      proposed.entry->key))
                    continue;
                if (!append_expanded(
                        &expanded, &expanded_owner,
                        MetadataEditingOperationKind::Add, *proposed.entry, i,
                        max_expanded, &request_bytes,
                        options.typed.max_request_bytes))
                    return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded,
                                          i);
            }
        } else {
            for (TrackedLeaf& old : state) {
                if (!old.active || old.root_group != current.root_group
                    || !leaf_in_scope(current.scope, old.leaf))
                    continue;
                const MetadataAuthoringEntry removal
                    = remove_entry_for_key(old.key);
                if (!append_expanded(
                        &expanded, &expanded_owner,
                        MetadataEditingOperationKind::Remove, removal, i,
                        max_expanded, &request_bytes,
                        options.typed.max_request_bytes))
                    return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded,
                                          i);
                old.active = false;
            }
        }

        compact_state(&state);
        if (operation.kind != MetadataXmpEditingOperationKind::Remove) {
            for (const PreparedLeaf& proposed : input_leaves) {
                if (state_key_present(state, current.root_group,
                                      proposed.entry->key))
                    continue;
                if (state.size() >= options.max_scope_entries)
                    return xmp_edit_error(MetadataXmpEditingStatus::LimitExceeded,
                                          i);
                TrackedLeaf added;
                added.key        = proposed.entry->key;
                added.leaf       = proposed.leaf;
                added.root_group = current.root_group;
                added.active     = true;
                state.push_back(added);
            }
        }
    }

    MetadataTypedEditingOptions typed_options = options.typed;
    typed_options.max_operations = max_expanded;
    const MetadataTypedEditingResult typed = edit_metadata_typed(
        base,
        std::span<const MetadataTypedEditingOperation>(expanded.data(),
                                                       expanded.size()),
        output, typed_options);
    if (!typed.ok()) {
        const uint32_t failed
            = typed.failed_operation_index < expanded_owner.size()
                  ? expanded_owner[typed.failed_operation_index]
                  : kInvalidOperation;
        MetadataXmpEditingResult result
            = xmp_edit_error(MetadataXmpEditingStatus::TypedEditFailed, failed);
        result.typed_status = typed.status;
        return result;
    }

    MetadataXmpEditingResult result;
    result.operations_applied   = static_cast<uint32_t>(operations.size());
    result.expanded_operations  = typed.operations_applied;
    result.entries_added       = typed.entries_added;
    result.entries_updated     = typed.entries_updated;
    result.entries_removed     = typed.entries_removed;
    return result;
}

const char*
metadata_xmp_editing_status_name(MetadataXmpEditingStatus status) noexcept
{
    switch (status) {
    case MetadataXmpEditingStatus::Ok: return "ok";
    case MetadataXmpEditingStatus::NullOutput: return "null_output";
    case MetadataXmpEditingStatus::BaseNotFinalized:
        return "base_not_finalized";
    case MetadataXmpEditingStatus::InvalidOptions: return "invalid_options";
    case MetadataXmpEditingStatus::TooManyOperations:
        return "too_many_operations";
    case MetadataXmpEditingStatus::InvalidOperation:
        return "invalid_operation";
    case MetadataXmpEditingStatus::InvalidScope: return "invalid_scope";
    case MetadataXmpEditingStatus::UnsupportedPath:
        return "unsupported_path";
    case MetadataXmpEditingStatus::ScopeAlreadyExists:
        return "scope_already_exists";
    case MetadataXmpEditingStatus::ScopeNotFound: return "scope_not_found";
    case MetadataXmpEditingStatus::EmptyEntries: return "empty_entries";
    case MetadataXmpEditingStatus::UnexpectedEntries:
        return "unexpected_entries";
    case MetadataXmpEditingStatus::InvalidEntry: return "invalid_entry";
    case MetadataXmpEditingStatus::DuplicateEntry: return "duplicate_entry";
    case MetadataXmpEditingStatus::UnsupportedSelectedEntry:
        return "unsupported_selected_entry";
    case MetadataXmpEditingStatus::ShapeCollision:
        return "shape_collision";
    case MetadataXmpEditingStatus::LimitExceeded: return "limit_exceeded";
    case MetadataXmpEditingStatus::TypedEditFailed:
        return "typed_edit_failed";
    case MetadataXmpEditingStatus::InternalError: return "internal_error";
    }
    return "unknown";
}

}  // namespace openmeta
