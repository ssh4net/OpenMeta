// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_reconcile.h"

#include "metadata_logical_field_internal.h"
#include "metadata_text_fields_internal.h"
#include "metadata_translation_iptc_xmp_mapping_internal.h"

#include "openmeta/meta_edit.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_translation.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    using Descriptor         = detail::IptcXmpMappingDescriptor;
    using DescriptiveMapping = MetadataDescriptiveTranslationMapping;

    struct ExifAliasDescriptor final {
        MetadataReconcileField field = MetadataReconcileField::None;
        std::string_view schema_ns;
        std::string_view path;
        uint16_t tag = 0U;
        MetadataCaptureTranslationMapping mapping
            = MetadataCaptureTranslationMapping::None;
    };

    static constexpr std::string_view kTiffXmpNamespace
        = "http://ns.adobe.com/tiff/1.0/";
    static constexpr std::array<ExifAliasDescriptor, 3U> kExifAliases = { {
        { MetadataReconcileField::Description, kTiffXmpNamespace,
          "ImageDescription", 0x010eU,
          MetadataCaptureTranslationMapping::XmpTiffImageDescription },
        { MetadataReconcileField::Creator, kTiffXmpNamespace, "Artist", 0x013bU,
          MetadataCaptureTranslationMapping::XmpTiffArtist },
        { MetadataReconcileField::Rights, kTiffXmpNamespace, "Copyright",
          0x8298U, MetadataCaptureTranslationMapping::XmpTiffCopyright },
    } };

    enum class ValueState : uint8_t { Absent, Active, Deleted };
    enum class XmpPathKind : uint8_t {
        Other,
        Target,
        LanguageSibling,
        Invalid
    };

    struct PendingValue final {
        EntryId entry        = kInvalidEntryId;
        uint32_t index       = 0U;
        BlockId block        = kInvalidBlockId;
        uint32_t order       = 0U;
        uint32_t key_bytes   = 0U;
        uint32_t value_bytes = 0U;
    };

    struct DeleteRef final {
        EntryId entry  = kInvalidEntryId;
        uint32_t index = 0U;
    };

    struct ValueRef final {
        EntryId entry  = kInvalidEntryId;
        uint32_t index = 0U;
        BlockId block  = kInvalidBlockId;
        uint32_t order = 0U;
        std::string_view text;
    };

    struct FamilyState final {
        ValueState state = ValueState::Absent;
        std::vector<PendingValue> pending;
        std::vector<DeleteRef> deletions;
        std::vector<ValueRef> values;
        EntryId first_entry             = kInvalidEntryId;
        EntryId dirty_delete            = kInvalidEntryId;
        EntryId clean_delete            = kInvalidEntryId;
        EntryId projection_dirty_delete = kInvalidEntryId;
        uint32_t inspected_properties   = 0U;
        uint32_t max_index              = 0U;
    };

    struct FieldPlan final {
        MetadataReconcileField field = MetadataReconcileField::None;
        const Descriptor* descriptor = nullptr;
        bool enabled                 = false;
        FamilyState xmp;
        FamilyState alias;
        FamilyState iptc;
        FamilyState exif;
        const FamilyState* xmp_family    = nullptr;
        const FamilyState* authoritative = nullptr;
        MetadataReconcileFamily winner   = MetadataReconcileFamily::None;
        bool project_xmp_to_iptc         = false;
        bool project_iptc_to_xmp         = false;
        bool project_xmp_to_exif         = false;
        bool seed_canonical_xmp          = false;
        std::vector<ValueRef> materialized;
    };

    struct CharsetBlock final {
        BlockId block           = kInvalidBlockId;
        EntryId marker          = kInvalidEntryId;
        uint32_t marker_count   = 0U;
        uint32_t marker_bytes   = 0U;
        bool marker_shape_valid = true;
        bool marker_value_valid = true;
        bool marker_required    = false;
    };

    struct IptcOrder final {
        bool operator()(const ValueRef& left,
                        const ValueRef& right) const noexcept
        {
            if (left.block != right.block)
                return left.block < right.block;
            if (left.order != right.order)
                return left.order < right.order;
            return left.entry < right.entry;
        }
    };

    struct XmpIndexOrder final {
        bool operator()(const ValueRef& left,
                        const ValueRef& right) const noexcept
        {
            return left.index == right.index ? left.entry < right.entry
                                             : left.index < right.index;
        }
    };

    struct BlockOrder final {
        bool operator()(BlockId left, BlockId right) const noexcept
        {
            return left < right;
        }
    };

    static constexpr std::array<std::byte, 3U> kUtf8Escape
        = { std::byte { 0x1bU }, std::byte { 0x25U }, std::byte { 0x47U } };

    static constexpr std::array<MetadataReconcileField, 5U> kFields = {
        MetadataReconcileField::Title,   MetadataReconcileField::Description,
        MetadataReconcileField::Creator, MetadataReconcileField::Keywords,
        MetadataReconcileField::Rights,
    };

    struct Budget final {
        uint32_t source_properties = 0U;
        std::vector<EntryId> charged_input_entries;
        uint64_t input_text_bytes        = 0U;
        uint64_t output_text_bytes       = 0U;
        uint64_t input_key_value_bytes   = 0U;
        uint64_t output_key_value_bytes  = 0U;
        uint64_t output_arena_bytes      = 0U;
        uint64_t stage_arena_bytes       = 0U;
        uint64_t scratch_arena_bytes     = 0U;
        uint64_t profile_scratch_entries = 0U;
        uint64_t operations              = 0U;
        uint64_t additions               = 0U;
        uint64_t stage_additions         = 0U;
        uint64_t scratch_additions       = 0U;
    };

    static MetadataReconcileResult
    failure(MetadataReconcileStatus status,
            MetadataReconcileField field   = MetadataReconcileField::None,
            MetadataReconcileFamily family = MetadataReconcileFamily::None,
            MetadataReconcileStage stage   = MetadataReconcileStage::None,
            EntryId entry                  = kInvalidEntryId) noexcept
    {
        MetadataReconcileResult result;
        result.status        = status;
        result.failed_field  = field;
        result.failed_family = family;
        result.failed_stage  = stage;
        result.failed_entry  = entry;
        return result;
    }

    static bool add_u64(uint64_t* total, uint64_t amount) noexcept
    {
        if (!total || amount > UINT64_MAX - *total)
            return false;
        *total += amount;
        return true;
    }

    static bool span_valid(const ByteArena& arena, ByteSpan span) noexcept
    {
        const size_t size   = arena.bytes().size();
        const size_t offset = static_cast<size_t>(span.offset);
        const size_t length = static_cast<size_t>(span.size);
        return offset <= size && length <= size - offset;
    }

    static bool arena_string(const ByteArena& arena, ByteSpan span,
                             std::string_view* out) noexcept
    {
        if (!out || !span_valid(arena, span))
            return false;
        const std::span<const std::byte> bytes = arena.span(span);
        *out = bytes.empty() ? std::string_view {}
                             : std::string_view(reinterpret_cast<const char*>(
                                                    bytes.data()),
                                                bytes.size());
        return true;
    }

    static bool flags_valid(const Entry& entry) noexcept
    {
        constexpr uint8_t known   = 0x7fU;
        constexpr uint8_t damaged = static_cast<uint8_t>(EntryFlags::Truncated)
                                    | static_cast<uint8_t>(
                                        EntryFlags::Unreadable);
        const uint8_t flags = static_cast<uint8_t>(entry.flags);
        return (flags & static_cast<uint8_t>(~known)) == 0U
               && (flags & damaged) == 0U;
    }

    static MetadataReconcileField
    field_for_mapping(DescriptiveMapping mapping) noexcept
    {
        switch (mapping) {
        case DescriptiveMapping::DcTitle: return MetadataReconcileField::Title;
        case DescriptiveMapping::DcDescription:
            return MetadataReconcileField::Description;
        case DescriptiveMapping::DcCreator:
            return MetadataReconcileField::Creator;
        case DescriptiveMapping::DcSubject:
            return MetadataReconcileField::Keywords;
        case DescriptiveMapping::DcRights:
            return MetadataReconcileField::Rights;
        default: return MetadataReconcileField::None;
        }
    }

    static const Descriptor*
    descriptor_for(MetadataReconcileField field) noexcept
    {
        for (const Descriptor& descriptor : detail::kIptcXmpPrimaryMappings) {
            if (field_for_mapping(descriptor.mapping) == field)
                return &descriptor;
        }
        return nullptr;
    }

    static bool field_enabled(MetadataReconcileField field,
                              const MetadataReconcileOptions& options) noexcept
    {
        switch (field) {
        case MetadataReconcileField::Title: return options.title;
        case MetadataReconcileField::Description: return options.description;
        case MetadataReconcileField::Creator: return options.creators;
        case MetadataReconcileField::Keywords: return options.keywords;
        case MetadataReconcileField::Rights: return options.copyright;
        case MetadataReconcileField::None: return false;
        }
        return false;
    }

    static const ExifAliasDescriptor*
    exif_alias_for_field(MetadataReconcileField field) noexcept
    {
        for (const ExifAliasDescriptor& alias : kExifAliases) {
            if (alias.field == field)
                return &alias;
        }
        return nullptr;
    }

    static bool
    exif_field_selected(MetadataReconcileField field,
                        const MetadataReconcileOptions& options) noexcept
    {
        return options.enable_exif_bridge && field_enabled(field, options)
               && exif_alias_for_field(field) != nullptr;
    }

    static XmpPathKind alias_path_kind(std::string_view path,
                                       const ExifAliasDescriptor& alias) noexcept
    {
        if (path == alias.path)
            return XmpPathKind::Target;
        if (path.size() > alias.path.size()
            && path.substr(0U, alias.path.size()) == alias.path
            && (path[alias.path.size()] == '['
                || path[alias.path.size()] == '/'))
            return XmpPathKind::Invalid;
        return XmpPathKind::Other;
    }

    static bool field_selected(const FieldPlan& plan,
                               const MetadataReconcileOptions& options) noexcept
    {
        return plan.enabled && field_enabled(plan.field, options);
    }

    static const FamilyState& effective_xmp(const FieldPlan& plan) noexcept
    {
        return plan.xmp_family ? *plan.xmp_family : plan.xmp;
    }

    static const FamilyState& final_family(const FieldPlan& plan) noexcept
    {
        if (plan.winner == MetadataReconcileFamily::Iptc)
            return plan.iptc;
        if (plan.winner == MetadataReconcileFamily::Exif)
            return plan.exif;
        return effective_xmp(plan);
    }

    static const FamilyState& projection_xmp(const FieldPlan& plan) noexcept
    {
        return plan.winner == MetadataReconcileFamily::Exif
                   ? plan.exif
                   : effective_xmp(plan);
    }

    static bool xmp_namespace_text(const MetaStore& source, const Entry& entry,
                                   std::string_view* schema) noexcept
    {
        if (!schema || entry.key.kind != MetaKeyKind::XmpProperty)
            return false;
        const ByteSpan ns = entry.key.data.xmp_property.schema_ns;
        return ns.size > 0U && ns.size <= kMetadataReconcileMaxKeyBytes
               && arena_string(source.arena(), ns, schema);
    }

    static bool xmp_key_text(const MetaStore& source, const Entry& entry,
                             std::string_view* schema, std::string_view* path,
                             uint32_t* key_bytes) noexcept
    {
        if (!schema || !path || !key_bytes
            || !xmp_namespace_text(source, entry, schema))
            return false;
        const ByteSpan property = entry.key.data.xmp_property.property_path;
        if (property.size == 0U || property.size > kMetadataReconcileMaxKeyBytes
            || !arena_string(source.arena(), property, path))
            return false;
        const ByteSpan ns = entry.key.data.xmp_property.schema_ns;
        *key_bytes        = ns.size + property.size;
        return true;
    }

    static bool parse_index(std::string_view path, std::string_view root,
                            uint32_t* out) noexcept
    {
        if (!out || path.size() <= root.size() + 2U
            || path.substr(0U, root.size()) != root || path[root.size()] != '['
            || path.back() != ']')
            return false;
        const std::string_view digits
            = path.substr(root.size() + 1U, path.size() - root.size() - 2U);
        if (digits.empty() || digits.front() == '0')
            return false;
        uint32_t index = 0U;
        for (const char c : digits) {
            if (c < '0' || c > '9')
                return false;
            const uint32_t digit = static_cast<uint32_t>(c - '0');
            if (index > (UINT32_MAX - digit) / 10U)
                return false;
            index = index * 10U + digit;
        }
        if (index == 0U)
            return false;
        *out = index;
        return true;
    }

    static XmpPathKind xmp_path_kind(std::string_view path,
                                     const Descriptor& descriptor,
                                     uint32_t* index) noexcept
    {
        if (descriptor.repeated) {
            if (path == descriptor.property_root)
                return XmpPathKind::Invalid;
            if (parse_index(path, descriptor.property_root, index))
                return XmpPathKind::Target;
            if (path.size() > descriptor.property_root.size()
                && path.substr(0U, descriptor.property_root.size())
                       == descriptor.property_root
                && (path[descriptor.property_root.size()] == '['
                    || path[descriptor.property_root.size()] == '/'))
                return XmpPathKind::Invalid;
            return XmpPathKind::Other;
        }
        if (path == descriptor.property_path) {
            if (index)
                *index = 0U;
            return XmpPathKind::Target;
        }
        if (descriptor.shape == detail::IptcXmpPropertyShape::LangAlt) {
            static constexpr std::string_view prefix = "[@xml:lang=";
            const std::string_view root              = descriptor.property_root;
            if (path.size() > root.size() + prefix.size()
                && path.substr(0U, root.size()) == root
                && path.substr(root.size(), prefix.size()) == prefix
                && path.back() == ']') {
                const std::string_view language
                    = path.substr(root.size() + prefix.size(),
                                  path.size() - root.size() - prefix.size()
                                      - 1U);
                if (!language.empty()
                    && language.find_first_of("[]") == std::string_view::npos) {
                    return language == "x-default"
                               ? XmpPathKind::Invalid
                               : XmpPathKind::LanguageSibling;
                }
            }
        }
        const std::string_view root = descriptor.property_root;
        if (path.size() > root.size() && path.substr(0U, root.size()) == root
            && (path[root.size()] == '[' || path[root.size()] == '/'))
            return XmpPathKind::Invalid;
        return XmpPathKind::Other;
    }

    static bool pending_value_shape(const Entry& entry, const ByteArena& arena,
                                    bool allow_bytes,
                                    uint32_t* out_size) noexcept
    {
        if (!out_size
            || (entry.value.kind != MetaValueKind::Text
                && (!allow_bytes || entry.value.kind != MetaValueKind::Bytes))
            || entry.value.elem_type != MetaElementType::U8
            || entry.value.count != entry.value.data.span.size
            || !span_valid(arena, entry.value.data.span))
            return false;
        if (entry.value.kind == MetaValueKind::Text
            && entry.value.text_encoding != TextEncoding::Ascii
            && entry.value.text_encoding != TextEncoding::Utf8)
            return false;
        *out_size = entry.value.data.span.size;
        return true;
    }

    static MetadataReconcileResult
    budget_failure(MetadataReconcileField field, MetadataReconcileFamily family,
                   EntryId entry) noexcept
    {
        return failure(MetadataReconcileStatus::SourceLimitExceeded, field,
                       family, MetadataReconcileStage::Preflight, entry);
    }

    static bool charge_property(Budget* budget, uint32_t key_bytes,
                                uint32_t value_bytes,
                                const MetadataReconcileOptions& options,
                                MetadataReconcileField field,
                                MetadataReconcileFamily family, EntryId id,
                                MetadataReconcileResult* error) noexcept
    {
        if (!budget || !error)
            return false;
        for (const EntryId charged : budget->charged_input_entries) {
            if (charged == id)
                return true;
        }
        if (budget->source_properties >= options.max_source_properties
            || budget->source_properties
                   >= kMetadataReconcileMaxSourceProperties
            || budget->input_text_bytes
                   > UINT64_MAX - static_cast<uint64_t>(value_bytes)
            || budget->input_key_value_bytes
                   > UINT64_MAX - static_cast<uint64_t>(key_bytes)
                         - value_bytes) {
            *error = budget_failure(field, family, id);
            return false;
        }
        ++budget->source_properties;
        budget->charged_input_entries.push_back(id);
        budget->input_text_bytes += value_bytes;
        budget->input_key_value_bytes += static_cast<uint64_t>(key_bytes)
                                         + value_bytes;
        if (budget->input_text_bytes > options.max_total_text_bytes
            || budget->input_text_bytes > kMetadataReconcileMaxTotalTextBytes
            || budget->input_key_value_bytes
                   > options.max_total_participating_bytes
            || budget->input_key_value_bytes
                   > kMetadataReconcileMaxTotalParticipatingBytes) {
            *error = budget_failure(field, family, id);
            return false;
        }
        return true;
    }

    static bool source_candidate(const Entry& entry,
                                 const MetadataReconcileOptions& options,
                                 bool* deleted) noexcept
    {
        if (!deleted)
            return false;
        const bool is_deleted = any(entry.flags, EntryFlags::Deleted);
        const bool dirty      = any(entry.flags, EntryFlags::Dirty);
        if (is_deleted) {
            if (!dirty
                || options.deletion_policy
                       == MetadataReconcileDeletionPolicy::Ignore)
                return false;
            *deleted = true;
            return true;
        }
        *deleted = false;
        return true;
    }

    static MetadataReconcileStatus
    collect_xmp_candidates(const MetaStore& source, FieldPlan* plans,
                           const MetadataReconcileOptions& options,
                           Budget* budget, MetadataReconcileResult* error)
    {
        if (!plans || !budget || !error)
            return MetadataReconcileStatus::InternalError;
        for (FieldPlan& plan : std::span<FieldPlan, 5U>(plans, 5U)) {
            if (!field_selected(plan, options))
                continue;
            for (EntryId id = 0U; id < source.entries().size(); ++id) {
                const Entry& entry = source.entry(id);
                if (entry.key.kind != MetaKeyKind::XmpProperty)
                    continue;
                bool deleted = false;
                if (!source_candidate(entry, options, &deleted))
                    continue;
                std::string_view schema;
                if (!xmp_namespace_text(source, entry, &schema))
                    continue;
                if (schema != plan.descriptor->schema_ns)
                    continue;
                std::string_view path;
                uint32_t key_bytes = 0U;
                if (!xmp_key_text(source, entry, &schema, &path, &key_bytes)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
                uint32_t index         = 0U;
                const XmpPathKind kind = xmp_path_kind(path, *plan.descriptor,
                                                       &index);
                if (kind == XmpPathKind::Other
                    || kind == XmpPathKind::LanguageSibling)
                    continue;
                if (kind == XmpPathKind::Invalid) {
                    *error = failure(
                        MetadataReconcileStatus::UnsupportedSourceShape,
                        plan.field, MetadataReconcileFamily::Xmp,
                        MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
                if (!deleted && !flags_valid(entry)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
                uint32_t value_bytes = 0U;
                if (!deleted
                    && !pending_value_shape(entry, source.arena(), false,
                                            &value_bytes)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
                if (!charge_property(budget, key_bytes, value_bytes, options,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     id, error))
                    return error->status;
                FamilyState& state = plan.xmp;
                state.max_index    = std::max(state.max_index, index);
                ++state.inspected_properties;
                if (deleted) {
                    state.deletions.push_back(DeleteRef { id, index });
                    if (state.dirty_delete == kInvalidEntryId)
                        state.dirty_delete = id;
                } else {
                    state.pending.push_back(PendingValue {
                        id, index, entry.origin.block,
                        entry.origin.order_in_block, key_bytes, value_bytes });
                }
            }
        }
        return MetadataReconcileStatus::Ok;
    }

    static MetadataReconcileStatus
    collect_iptc_candidates(const MetaStore& source, FieldPlan* plans,
                            const MetadataReconcileOptions& options,
                            Budget* budget, MetadataReconcileResult* error)
    {
        if (!plans || !budget || !error)
            return MetadataReconcileStatus::InternalError;
        for (FieldPlan& plan : std::span<FieldPlan, 5U>(plans, 5U)) {
            if (!field_selected(plan, options) || !options.participate_iptc)
                continue;
            for (EntryId id = 0U; id < source.entries().size(); ++id) {
                const Entry& entry = source.entry(id);
                if (entry.key.kind != MetaKeyKind::IptcDataset
                    || entry.key.data.iptc_dataset.record != 2U
                    || entry.key.data.iptc_dataset.dataset
                           != plan.descriptor->iptc_dataset)
                    continue;
                bool deleted = false;
                if (!source_candidate(entry, options, &deleted))
                    continue;
                if (!deleted && !flags_valid(entry)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Iptc,
                                     MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
                uint32_t value_bytes = 0U;
                if (!deleted) {
                    if (entry.origin.block != kInvalidBlockId
                        && entry.origin.block >= source.block_count()) {
                        *error = failure(
                            MetadataReconcileStatus::InvalidSourceValue,
                            plan.field, MetadataReconcileFamily::Iptc,
                            MetadataReconcileStage::Preflight, id);
                        return error->status;
                    }
                    if (entry.origin.wire_type_name.size > 0U
                        && !span_valid(source.arena(),
                                       entry.origin.wire_type_name)) {
                        *error = failure(
                            MetadataReconcileStatus::InvalidSourceValue,
                            plan.field, MetadataReconcileFamily::Iptc,
                            MetadataReconcileStage::Preflight, id);
                        return error->status;
                    }
                    if (!pending_value_shape(entry, source.arena(), true,
                                             &value_bytes)) {
                        *error = failure(
                            MetadataReconcileStatus::InvalidSourceValue,
                            plan.field, MetadataReconcileFamily::Iptc,
                            MetadataReconcileStage::Preflight, id);
                        return error->status;
                    }
                }
                if (!charge_property(budget, 4U, value_bytes, options,
                                     plan.field, MetadataReconcileFamily::Iptc,
                                     id, error))
                    return error->status;
                FamilyState& state = plan.iptc;
                ++state.inspected_properties;
                if (deleted) {
                    state.deletions.push_back(DeleteRef { id, 0U });
                    if (state.dirty_delete == kInvalidEntryId)
                        state.dirty_delete = id;
                } else {
                    state.pending.push_back(PendingValue {
                        id, 0U, entry.origin.block, entry.origin.order_in_block,
                        4U, value_bytes });
                }
            }
        }
        return MetadataReconcileStatus::Ok;
    }

    static MetadataReconcileStatus
    collect_exif_alias_candidates(const MetaStore& source, FieldPlan* plans,
                                  const MetadataReconcileOptions& options,
                                  Budget* budget,
                                  MetadataReconcileResult* error)
    {
        if (!plans || !budget || !error)
            return MetadataReconcileStatus::InternalError;
        if (!options.enable_exif_bridge)
            return MetadataReconcileStatus::Ok;
        for (FieldPlan& plan : std::span<FieldPlan, 5U>(plans, 5U)) {
            const ExifAliasDescriptor* alias = exif_alias_for_field(plan.field);
            if (!plan.enabled || !alias)
                continue;
            for (EntryId id = 0U; id < source.entries().size(); ++id) {
                const Entry& entry = source.entry(id);
                if (entry.key.kind != MetaKeyKind::XmpProperty)
                    continue;
                bool deleted = false;
                if (!source_candidate(entry, options, &deleted))
                    continue;
                std::string_view schema;
                if (!xmp_namespace_text(source, entry, &schema)
                    || schema != alias->schema_ns)
                    continue;
                std::string_view path;
                uint32_t key_bytes = 0U;
                if (!xmp_key_text(source, entry, &schema, &path, &key_bytes)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
                const XmpPathKind kind = alias_path_kind(path, *alias);
                if (kind == XmpPathKind::Other)
                    continue;
                if (kind == XmpPathKind::Invalid) {
                    *error = failure(
                        MetadataReconcileStatus::UnsupportedSourceShape,
                        plan.field, MetadataReconcileFamily::Xmp,
                        MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
                if (!deleted && !flags_valid(entry)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
                uint32_t value_bytes = 0U;
                if (!deleted
                    && !pending_value_shape(entry, source.arena(), false,
                                            &value_bytes)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
                if (!charge_property(budget, key_bytes, value_bytes, options,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     id, error))
                    return error->status;
                FamilyState& state = plan.alias;
                if (deleted) {
                    state.deletions.push_back(DeleteRef { id, 0U });
                    if (state.dirty_delete == kInvalidEntryId)
                        state.dirty_delete = id;
                } else {
                    if (state.first_entry == kInvalidEntryId)
                        state.first_entry = id;
                    state.pending.push_back(PendingValue {
                        id, 0U, entry.origin.block, entry.origin.order_in_block,
                        key_bytes, value_bytes });
                }
            }
        }
        return MetadataReconcileStatus::Ok;
    }

    static MetadataReconcileStatus
    collect_exif_candidates(const MetaStore& source, FieldPlan* plans,
                            const MetadataReconcileOptions& options,
                            Budget* budget, MetadataReconcileResult* error)
    {
        if (!plans || !budget || !error)
            return MetadataReconcileStatus::InternalError;
        if (!options.enable_exif_bridge)
            return MetadataReconcileStatus::Ok;
        for (FieldPlan& plan : std::span<FieldPlan, 5U>(plans, 5U)) {
            const ExifAliasDescriptor* alias = exif_alias_for_field(plan.field);
            if (!plan.enabled || !alias)
                continue;
            for (EntryId id = 0U; id < source.entries().size(); ++id) {
                const Entry& entry = source.entry(id);
                if (entry.key.kind != MetaKeyKind::ExifTag
                    || entry.key.data.exif_tag.tag != alias->tag)
                    continue;
                std::string_view ifd;
                if (!arena_string(source.arena(), entry.key.data.exif_tag.ifd,
                                  &ifd)
                    || ifd != "ifd0")
                    continue;
                FamilyState& state = plan.exif;
                if (any(entry.flags, EntryFlags::Deleted)) {
                    if (!charge_property(budget, 6U, 0U, options, plan.field,
                                         MetadataReconcileFamily::Exif, id,
                                         error))
                        return error->status;
                    if (any(entry.flags, EntryFlags::Dirty)) {
                        if (state.projection_dirty_delete == kInvalidEntryId)
                            state.projection_dirty_delete = id;
                    } else if (state.clean_delete == kInvalidEntryId) {
                        state.clean_delete = id;
                    }
                }
                bool deleted = false;
                if (!source_candidate(entry, options, &deleted))
                    continue;
                uint32_t value_bytes = 0U;
                if (!deleted) {
                    if (!flags_valid(entry)
                        || !pending_value_shape(entry, source.arena(), false,
                                                &value_bytes)) {
                        *error = failure(
                            MetadataReconcileStatus::InvalidSourceValue,
                            plan.field, MetadataReconcileFamily::Exif,
                            MetadataReconcileStage::Preflight, id);
                        return error->status;
                    }
                    if (!charge_property(budget, 6U, value_bytes, options,
                                         plan.field,
                                         MetadataReconcileFamily::Exif, id,
                                         error))
                        return error->status;
                    std::string_view raw;
                    if (!arena_string(source.arena(), entry.value.data.span,
                                      &raw)) {
                        *error = failure(
                            MetadataReconcileStatus::InvalidSourceValue,
                            plan.field, MetadataReconcileFamily::Exif,
                            MetadataReconcileStage::Preflight, id);
                        return error->status;
                    }
                    const size_t logical_size = !raw.empty()
                                                        && raw.back() == '\0'
                                                    ? raw.size() - 1U
                                                    : raw.size();
                    if (plan.field == MetadataReconcileField::Rights
                        && std::string_view(raw.data(), logical_size).find('\0')
                               != std::string_view::npos) {
                        *error = failure(
                            MetadataReconcileStatus::UnsupportedSourceShape,
                            plan.field, MetadataReconcileFamily::Exif,
                            MetadataReconcileStage::Preflight, id);
                        return error->status;
                    }
                    const uint16_t wire_type
                        = detail::exif_text_wire_type(source.arena(), entry);
                    if ((entry.origin.wire_type.family == WireFamily::Tiff
                         && entry.origin.wire_type.code != wire_type)
                        || (entry.origin.wire_type.family != WireFamily::None
                            && entry.origin.wire_type.family != WireFamily::Tiff)
                        || (wire_type != 2U && wire_type != 129U)
                        || (wire_type == 129U
                            && entry.value.text_encoding
                                   != TextEncoding::Utf8)) {
                        *error = failure(
                            MetadataReconcileStatus::InvalidSourceValue,
                            plan.field, MetadataReconcileFamily::Exif,
                            MetadataReconcileStage::Preflight, id);
                        return error->status;
                    }
                    std::string_view logical_text;
                    if (!detail::exif_text_view(source.arena(), entry.value,
                                                true, &logical_text)
                        || logical_text.size()
                               > kMetadataProfileTranslationMaxTextBytesPerProperty) {
                        *error = failure(
                            logical_text.size()
                                    > kMetadataProfileTranslationMaxTextBytesPerProperty
                                ? MetadataReconcileStatus::ValueTooLong
                                : MetadataReconcileStatus::InvalidSourceValue,
                            plan.field, MetadataReconcileFamily::Exif,
                            MetadataReconcileStage::Preflight, id);
                        return error->status;
                    }
                    value_bytes = entry.value.data.span.size;
                }
                if (!charge_property(budget, 6U, value_bytes, options,
                                     plan.field, MetadataReconcileFamily::Exif,
                                     id, error))
                    return error->status;
                if (deleted) {
                    state.deletions.push_back(DeleteRef { id, 0U });
                    if (state.dirty_delete == kInvalidEntryId)
                        state.dirty_delete = id;
                } else {
                    if (state.first_entry == kInvalidEntryId)
                        state.first_entry = id;
                    state.pending.push_back(PendingValue {
                        id, 0U, entry.origin.block, entry.origin.order_in_block,
                        6U, value_bytes });
                }
            }
        }
        return MetadataReconcileStatus::Ok;
    }

    static size_t charset_info_index(std::span<const CharsetBlock> blocks,
                                     BlockId block) noexcept
    {
        size_t low  = 0U;
        size_t high = blocks.size();
        while (low < high) {
            const size_t middle = low + (high - low) / 2U;
            if (blocks[middle].block < block)
                low = middle + 1U;
            else
                high = middle;
        }
        return low < blocks.size() && blocks[low].block == block
                   ? low
                   : blocks.size();
    }

    static MetadataReconcileStatus collect_charset_candidates(
        const MetaStore& source, FieldPlan* plans,
        const MetadataReconcileOptions& options, Budget* budget,
        std::vector<CharsetBlock>* out_blocks, MetadataReconcileResult* error)
    {
        if (!plans || !budget || !out_blocks || !error)
            return MetadataReconcileStatus::InternalError;
        out_blocks->clear();
        if (!options.participate_iptc)
            return MetadataReconcileStatus::Ok;
        std::vector<BlockId> relevant;
        for (FieldPlan& plan : std::span<FieldPlan, 5U>(plans, 5U)) {
            if (!field_selected(plan, options))
                continue;
            for (const PendingValue& pending : plan.iptc.pending) {
                if (pending.block == kInvalidBlockId)
                    continue;
                relevant.push_back(pending.block);
            }
        }
        if (relevant.empty())
            return MetadataReconcileStatus::Ok;
        std::sort(relevant.begin(), relevant.end(), BlockOrder {});
        relevant.erase(std::unique(relevant.begin(), relevant.end()),
                       relevant.end());
        out_blocks->reserve(relevant.size());
        for (const BlockId block : relevant)
            out_blocks->push_back(CharsetBlock { block });

        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind != MetaKeyKind::IptcDataset
                || entry.key.data.iptc_dataset.record != 1U
                || entry.key.data.iptc_dataset.dataset != 90U
                || any(entry.flags, EntryFlags::Deleted)
                || entry.origin.block == kInvalidBlockId)
                continue;
            const size_t index = charset_info_index(*out_blocks,
                                                    entry.origin.block);
            if (index == out_blocks->size())
                continue;
            CharsetBlock& info = (*out_blocks)[index];
            ++info.marker_count;
            if (info.marker == kInvalidEntryId)
                info.marker = id;
            uint32_t value_bytes  = 0U;
            const bool value_kind = entry.value.kind == MetaValueKind::Text
                                    || entry.value.kind == MetaValueKind::Bytes;
            if (value_kind && entry.value.count == entry.value.data.span.size
                && span_valid(source.arena(), entry.value.data.span))
                value_bytes = entry.value.data.span.size;
            if (!charge_property(budget, 4U, value_bytes, options,
                                 MetadataReconcileField::None,
                                 MetadataReconcileFamily::Iptc, id, error))
                return error->status;
            if (!flags_valid(entry)
                || !pending_value_shape(entry, source.arena(), true,
                                        &value_bytes)
                || value_bytes != kUtf8Escape.size()) {
                info.marker_shape_valid = false;
                continue;
            }
            info.marker_bytes = value_bytes;
        }
        return MetadataReconcileStatus::Ok;
    }

    static MetadataReconcileStatus
    validate_charset_blocks(const MetaStore& source,
                            std::span<const CharsetBlock> blocks,
                            MetadataReconcileResult* error) noexcept
    {
        for (const CharsetBlock& info : blocks) {
            if (info.marker_count > 1U || !info.marker_shape_valid)
                return error->status
                       = MetadataReconcileStatus::SourceEncodingConflict,
                       error->failed_family = MetadataReconcileFamily::Iptc,
                       error->failed_stage  = MetadataReconcileStage::Preflight,
                       error->failed_entry  = info.marker, error->status;
            if (info.marker_count == 0U)
                continue;
            const Entry& marker                    = source.entry(info.marker);
            const std::span<const std::byte> bytes = source.arena().span(
                marker.value.data.span);
            if (bytes.size() != kUtf8Escape.size()
                || !std::equal(bytes.begin(), bytes.end(),
                               kUtf8Escape.begin())) {
                error->status = MetadataReconcileStatus::SourceEncodingConflict;
                error->failed_family = MetadataReconcileFamily::Iptc;
                error->failed_stage  = MetadataReconcileStage::Preflight;
                error->failed_entry  = info.marker;
                return error->status;
            }
        }
        return MetadataReconcileStatus::Ok;
    }

    static bool read_text_value(const MetaStore& source, const Entry& entry,
                                bool allow_bytes, std::string_view* text,
                                bool* non_ascii) noexcept
    {
        if (!text || !non_ascii
            || (entry.value.kind != MetaValueKind::Text
                && (!allow_bytes || entry.value.kind != MetaValueKind::Bytes))
            || entry.value.elem_type != MetaElementType::U8
            || entry.value.count != entry.value.data.span.size
            || !span_valid(source.arena(), entry.value.data.span)
            || (entry.value.kind == MetaValueKind::Text
                && entry.value.text_encoding != TextEncoding::Ascii
                && entry.value.text_encoding != TextEncoding::Utf8))
            return false;
        const std::span<const std::byte> bytes = source.arena().span(
            entry.value.data.span);
        if (bytes.empty() || bytes.size() != entry.value.count)
            return false;
        bool high = false;
        for (const std::byte byte : bytes)
            high = high || std::to_integer<uint8_t>(byte) >= 0x80U;
        if (entry.value.kind == MetaValueKind::Text
            && entry.value.text_encoding == TextEncoding::Ascii && high)
            return false;
        *text = std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                 bytes.size());
        *non_ascii = high;
        return detail::metadata_logical_text_is_valid(*text);
    }

    static MetadataReconcileStatus
    validate_pending_values(const MetaStore& source, FieldPlan* plans,
                            const MetadataReconcileOptions& options,
                            std::span<const CharsetBlock> charsets,
                            MetadataReconcileResult* error)
    {
        for (FieldPlan& plan : std::span<FieldPlan, 5U>(plans, 5U)) {
            if (!field_selected(plan, options))
                continue;
            for (const PendingValue& pending : plan.xmp.pending) {
                const Entry& entry = source.entry(pending.entry);
                std::string_view text;
                bool non_ascii = false;
                if (!read_text_value(source, entry, false, &text, &non_ascii)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::Preflight,
                                     pending.entry);
                    return error->status;
                }
                if (plan.xmp.values.empty())
                    plan.xmp.first_entry = pending.entry;
                plan.xmp.values.push_back(
                    ValueRef { pending.entry, pending.index, pending.block,
                               pending.order, text });
            }
            for (const PendingValue& pending : plan.iptc.pending) {
                const Entry& entry = source.entry(pending.entry);
                std::string_view text;
                bool non_ascii = false;
                if (!read_text_value(source, entry, true, &text, &non_ascii)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Iptc,
                                     MetadataReconcileStage::Preflight,
                                     pending.entry);
                    return error->status;
                }
                if (text.size() > plan.descriptor->max_bytes) {
                    *error = failure(MetadataReconcileStatus::ValueTooLong,
                                     plan.field, MetadataReconcileFamily::Iptc,
                                     MetadataReconcileStage::Preflight,
                                     pending.entry);
                    return error->status;
                }
                if (pending.block == kInvalidBlockId) {
                    if (non_ascii) {
                        *error = failure(
                            MetadataReconcileStatus::SourceEncodingConflict,
                            plan.field, MetadataReconcileFamily::Iptc,
                            MetadataReconcileStage::Preflight, pending.entry);
                        return error->status;
                    }
                } else {
                    const size_t index = charset_info_index(charsets,
                                                            pending.block);
                    if (index == charsets.size()) {
                        *error = failure(MetadataReconcileStatus::InternalError,
                                         plan.field,
                                         MetadataReconcileFamily::Iptc,
                                         MetadataReconcileStage::Preflight,
                                         pending.entry);
                        return error->status;
                    }
                    const CharsetBlock& charset = charsets[index];
                    if (charset.marker_count > 1U || !charset.marker_shape_valid
                        || (non_ascii && charset.marker_count != 1U)) {
                        *error = failure(
                            MetadataReconcileStatus::SourceEncodingConflict,
                            plan.field, MetadataReconcileFamily::Iptc,
                            MetadataReconcileStage::Preflight, pending.entry);
                        return error->status;
                    }
                }
                if (plan.iptc.values.empty())
                    plan.iptc.first_entry = pending.entry;
                plan.iptc.values.push_back(ValueRef {
                    pending.entry, 0U, pending.block, pending.order, text });
            }
            if (plan.descriptor->repeated) {
                std::sort(plan.xmp.values.begin(), plan.xmp.values.end(),
                          XmpIndexOrder {});
                std::sort(plan.iptc.values.begin(), plan.iptc.values.end(),
                          IptcOrder {});
                for (size_t i = 1U; i < plan.xmp.values.size(); ++i) {
                    if (plan.xmp.values[i - 1U].index
                        == plan.xmp.values[i].index) {
                        *error
                            = failure(MetadataReconcileStatus::AmbiguousSource,
                                      plan.field, MetadataReconcileFamily::Xmp,
                                      MetadataReconcileStage::Preflight,
                                      plan.xmp.values[i].entry);
                        return error->status;
                    }
                }
            } else if (plan.xmp.values.size() > 1U
                       || plan.iptc.values.size() > 1U) {
                *error = failure(MetadataReconcileStatus::AmbiguousSource,
                                 plan.field,
                                 plan.xmp.values.size() > 1U
                                     ? MetadataReconcileFamily::Xmp
                                     : MetadataReconcileFamily::Iptc,
                                 MetadataReconcileStage::Preflight,
                                 plan.xmp.values.size() > 1U
                                     ? plan.xmp.values[1U].entry
                                     : plan.iptc.values[1U].entry);
                return error->status;
            }
            if (plan.xmp.values.empty()
                && options.deletion_policy
                       == MetadataReconcileDeletionPolicy::PropagateDirty) {
                for (size_t i = 0U; i < plan.xmp.deletions.size(); ++i) {
                    for (size_t j = i + 1U; j < plan.xmp.deletions.size();
                         ++j) {
                        if (!plan.descriptor->repeated
                            || plan.xmp.deletions[i].index
                                   == plan.xmp.deletions[j].index) {
                            *error = failure(
                                MetadataReconcileStatus::AmbiguousSource,
                                plan.field, MetadataReconcileFamily::Xmp,
                                MetadataReconcileStage::Preflight,
                                plan.xmp.deletions[j].entry);
                            return error->status;
                        }
                    }
                }
            }
            if (plan.iptc.values.empty()
                && options.deletion_policy
                       == MetadataReconcileDeletionPolicy::PropagateDirty
                && !plan.descriptor->repeated
                && plan.iptc.deletions.size() > 1U) {
                *error = failure(MetadataReconcileStatus::AmbiguousSource,
                                 plan.field, MetadataReconcileFamily::Iptc,
                                 MetadataReconcileStage::Preflight,
                                 plan.iptc.deletions[1U].entry);
                return error->status;
            }
            plan.xmp.state
                = !plan.xmp.values.empty()
                      ? ValueState::Active
                      : (!plan.xmp.deletions.empty()
                                 && options.deletion_policy
                                        == MetadataReconcileDeletionPolicy::
                                            PropagateDirty
                             ? ValueState::Deleted
                             : ValueState::Absent);
            plan.iptc.state
                = !plan.iptc.values.empty()
                      ? ValueState::Active
                      : (!plan.iptc.deletions.empty()
                                 && options.deletion_policy
                                        == MetadataReconcileDeletionPolicy::
                                            PropagateDirty
                             ? ValueState::Deleted
                             : ValueState::Absent);

            if (!exif_field_selected(plan.field, options))
                continue;
            for (const PendingValue& pending : plan.alias.pending) {
                const Entry& entry = source.entry(pending.entry);
                std::string_view text;
                bool non_ascii = false;
                if (!read_text_value(source, entry, false, &text, &non_ascii)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::Preflight,
                                     pending.entry);
                    return error->status;
                }
                plan.alias.values.push_back(ValueRef {
                    pending.entry, 0U, pending.block, pending.order, text });
            }
            for (const PendingValue& pending : plan.exif.pending) {
                const Entry& entry = source.entry(pending.entry);
                std::string_view text;
                if (!detail::exif_text_view(source.arena(), entry.value, true,
                                            &text)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Exif,
                                     MetadataReconcileStage::Preflight,
                                     pending.entry);
                    return error->status;
                }
                plan.exif.values.push_back(ValueRef {
                    pending.entry, 0U, pending.block, pending.order, text });
            }
            if (plan.alias.values.size() > 1U) {
                *error = failure(MetadataReconcileStatus::AmbiguousSource,
                                 plan.field, MetadataReconcileFamily::Xmp,
                                 MetadataReconcileStage::Preflight,
                                 plan.alias.values[1U].entry);
                return error->status;
            }
            if (plan.exif.values.size() > 1U) {
                *error = failure(MetadataReconcileStatus::AmbiguousSource,
                                 plan.field, MetadataReconcileFamily::Exif,
                                 MetadataReconcileStage::Preflight,
                                 plan.exif.values[1U].entry);
                return error->status;
            }
            if (plan.alias.values.empty()
                && options.deletion_policy
                       == MetadataReconcileDeletionPolicy::PropagateDirty
                && plan.alias.deletions.size() > 1U) {
                *error = failure(MetadataReconcileStatus::AmbiguousSource,
                                 plan.field, MetadataReconcileFamily::Xmp,
                                 MetadataReconcileStage::Preflight,
                                 plan.alias.deletions[1U].entry);
                return error->status;
            }
            if (plan.exif.values.empty()
                && options.deletion_policy
                       == MetadataReconcileDeletionPolicy::PropagateDirty
                && plan.exif.deletions.size() > 1U) {
                *error = failure(MetadataReconcileStatus::AmbiguousSource,
                                 plan.field, MetadataReconcileFamily::Exif,
                                 MetadataReconcileStage::Preflight,
                                 plan.exif.deletions[1U].entry);
                return error->status;
            }
            plan.alias.state
                = !plan.alias.values.empty()
                      ? ValueState::Active
                      : (!plan.alias.deletions.empty()
                                 && options.deletion_policy
                                        == MetadataReconcileDeletionPolicy::
                                            PropagateDirty
                             ? ValueState::Deleted
                             : ValueState::Absent);
            plan.exif.state
                = !plan.exif.values.empty()
                      ? ValueState::Active
                      : (!plan.exif.deletions.empty()
                                 && options.deletion_policy
                                        == MetadataReconcileDeletionPolicy::
                                            PropagateDirty
                             ? ValueState::Deleted
                             : ValueState::Absent);
        }
        return MetadataReconcileStatus::Ok;
    }

    static bool values_equal(const FieldPlan& plan, const FamilyState& left,
                             const FamilyState& right) noexcept
    {
        if (left.state != right.state)
            return false;
        if (left.state != ValueState::Active)
            return true;
        if (left.values.size() != right.values.size())
            return false;
        if (plan.field != MetadataReconcileField::Keywords) {
            for (size_t i = 0U; i < left.values.size(); ++i) {
                if (left.values[i].text != right.values[i].text)
                    return false;
            }
            return true;
        }
        if (right.values.size() > kMetadataReconcileMaxSourceProperties)
            return false;
        std::array<uint8_t, kMetadataReconcileMaxSourceProperties> matched {};
        for (const ValueRef& value : left.values) {
            bool found = false;
            for (size_t i = 0U; i < right.values.size(); ++i) {
                if (matched[i] == 0U && value.text == right.values[i].text) {
                    matched[i] = 1U;
                    found      = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        return true;
    }

    static bool options_valid(const MetadataReconcileOptions& options) noexcept
    {
        if ((!options.title && !options.description && !options.creators
             && !options.keywords && !options.copyright)
            || (options.conflict_policy
                    != MetadataReconcileConflictPolicy::FailOnConflict
                && options.conflict_policy
                       != MetadataReconcileConflictPolicy::PreferSourceOrder)
            || (options.deletion_policy
                    != MetadataReconcileDeletionPolicy::Ignore
                && options.deletion_policy
                       != MetadataReconcileDeletionPolicy::PropagateDirty)
            || options.max_source_entries == 0U
            || options.max_source_entries > kMetadataReconcileMaxSourceEntries
            || options.max_source_arena_bytes == 0U
            || options.max_source_arena_bytes
                   > kMetadataReconcileMaxSourceArenaBytes
            || options.max_source_properties == 0U
            || options.max_source_properties
                   > kMetadataReconcileMaxSourceProperties
            || options.max_total_text_bytes == 0U
            || options.max_total_text_bytes
                   > kMetadataReconcileMaxTotalTextBytes
            || options.max_total_participating_bytes == 0U
            || options.max_total_participating_bytes
                   > kMetadataReconcileMaxTotalParticipatingBytes
            || options.max_operations == 0U
            || options.max_operations > kMetadataReconcileMaxOperations
            || options.validation.max_entries == 0U
            || options.validation.max_entries
                   > kMetadataReconcileMaxSourceEntries
            || options.validation.max_key_bytes == 0U
            || options.validation.max_key_bytes > kMetadataReconcileMaxKeyBytes
            || options.validation.max_arena_bytes == 0U
            || options.validation.max_arena_bytes
                   > kMetadataReconcileMaxSourceArenaBytes
            || options.validation.max_value_bytes == 0U
            || options.validation.max_value_bytes
                   > kMetadataReconcileMaxSourceArenaBytes
            || options.validation.max_issues == 0U
            || options.validation.max_issues > kMetadataReconcileMaxOperations)
            return false;
        for (size_t i = 0U; i < options.family_priority.size(); ++i) {
            const MetadataReconcileFamily family = options.family_priority[i];
            if (family != MetadataReconcileFamily::Xmp
                && family != MetadataReconcileFamily::Iptc
                && family != MetadataReconcileFamily::Exif)
                return false;
            for (size_t j = i + 1U; j < options.family_priority.size(); ++j) {
                if (family == options.family_priority[j])
                    return false;
            }
        }
        return true;
    }

    static bool family_enabled(MetadataReconcileFamily family,
                               const MetadataReconcileOptions& options) noexcept
    {
        if (family == MetadataReconcileFamily::Xmp)
            return true;
        if (family == MetadataReconcileFamily::Iptc)
            return options.participate_iptc;
        return family == MetadataReconcileFamily::Exif
               && options.enable_exif_bridge;
    }

    static const FamilyState* state_for(const FieldPlan& plan,
                                        MetadataReconcileFamily family) noexcept
    {
        if (family == MetadataReconcileFamily::Xmp)
            return plan.xmp_family ? plan.xmp_family : &plan.xmp;
        if (family == MetadataReconcileFamily::Iptc)
            return &plan.iptc;
        if (family == MetadataReconcileFamily::Exif)
            return &plan.exif;
        return nullptr;
    }

    static uint32_t positional_changes(const FamilyState& source,
                                       const FamilyState& target) noexcept
    {
        const size_t source_count = source.state == ValueState::Active
                                        ? source.values.size()
                                        : 0U;
        const size_t target_count = target.state == ValueState::Active
                                        ? target.values.size()
                                        : 0U;
        const size_t overlap      = std::min(source_count, target_count);
        uint32_t operations       = static_cast<uint32_t>(
            source_count > target_count ? source_count - target_count
                                        : target_count - source_count);
        for (size_t i = 0U; i < overlap; ++i) {
            if (source.values[i].text != target.values[i].text)
                ++operations;
        }
        return operations;
    }

    static uint64_t indexed_path_length(const Descriptor& descriptor,
                                        uint32_t index) noexcept
    {
        if (!descriptor.repeated)
            return descriptor.property_path.size();
        uint32_t digits = 1U;
        while (index >= 10U) {
            index /= 10U;
            ++digits;
        }
        return descriptor.property_root.size() + digits + 2U;
    }

    static uint64_t value_wire_name_bytes(const MetaStore& source,
                                          const ValueRef& value) noexcept
    {
        return source.entry(value.entry).origin.wire_type_name.size;
    }

    static bool reserve_operation(Budget* budget, uint64_t count) noexcept
    {
        return budget && add_u64(&budget->operations, count);
    }

    static bool source_has_active_marker(const MetaStore& source) noexcept
    {
        for (const Entry& entry : source.entries()) {
            if (entry.key.kind == MetaKeyKind::IptcDataset
                && entry.key.data.iptc_dataset.record == 1U
                && entry.key.data.iptc_dataset.dataset == 90U
                && !any(entry.flags, EntryFlags::Deleted))
                return true;
        }
        return false;
    }

    static bool value_has_non_ascii(const ValueRef& value) noexcept
    {
        for (const char c : value.text) {
            if (static_cast<unsigned char>(c) >= 0x80U)
                return true;
        }
        return false;
    }

    static bool project_xmp_needed(FieldPlan& plan,
                                   const MetadataReconcileOptions& options)
    {
        if (!options.participate_iptc
            || (plan.winner != MetadataReconcileFamily::Xmp
                && plan.winner != MetadataReconcileFamily::Exif))
            return true;
        const FamilyState& xmp = projection_xmp(plan);
        if (values_equal(plan, xmp, plan.iptc))
            return true;
        if (xmp.state == ValueState::Deleted
            && plan.iptc.state != ValueState::Active)
            return true;
        plan.project_xmp_to_iptc = true;
        return true;
    }

    static bool project_iptc_needed(FieldPlan& plan,
                                    const MetadataReconcileOptions& options)
    {
        if (!options.participate_iptc
            || plan.winner != MetadataReconcileFamily::Iptc)
            return true;
        const FamilyState& xmp = effective_xmp(plan);
        if (values_equal(plan, xmp, plan.iptc)
            && plan.xmp.state != ValueState::Absent)
            return true;
        if (plan.iptc.state == ValueState::Deleted
            && xmp.state != ValueState::Active
            && plan.xmp.state != ValueState::Absent)
            return true;
        plan.project_iptc_to_xmp = true;
        return true;
    }

    static bool field_writes_iptc(const FieldPlan& plan) noexcept
    {
        return plan.project_xmp_to_iptc;
    }

    static bool field_writes_xmp(const FieldPlan& plan) noexcept
    {
        return plan.project_iptc_to_xmp;
    }

    static bool field_dataset_selected(const Entry& entry,
                                       const FieldPlan* plans,
                                       bool xmp_to_iptc) noexcept
    {
        if (!plans || entry.key.kind != MetaKeyKind::IptcDataset)
            return false;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.enabled
                || (xmp_to_iptc ? !field_writes_iptc(plan)
                                : !field_writes_xmp(plan)))
                continue;
            if (entry.key.data.iptc_dataset.record == 2U
                && entry.key.data.iptc_dataset.dataset
                       == plan.descriptor->iptc_dataset)
                return true;
        }
        return false;
    }

    static MetadataReconcileStatus preflight_global_xmp_to_iptc_charset(
        const MetaStore& source, const FieldPlan* plans,
        const MetadataReconcileOptions& options, Budget* budget,
        MetadataReconcileResult* error)
    {
        uint32_t active_markers = 0U;
        EntryId marker_id       = kInvalidEntryId;
        uint32_t marker_bytes   = 0U;
        bool marker_shape_valid = true;
        bool has_marker         = false;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind != MetaKeyKind::IptcDataset
                || entry.key.data.iptc_dataset.record != 1U
                || entry.key.data.iptc_dataset.dataset != 90U
                || any(entry.flags, EntryFlags::Deleted))
                continue;
            has_marker = true;
            ++active_markers;
            if (marker_id == kInvalidEntryId)
                marker_id = id;
            uint32_t bytes = 0U;
            if ((entry.value.kind == MetaValueKind::Bytes
                 || entry.value.kind == MetaValueKind::Text)
                && entry.value.count == entry.value.data.span.size
                && span_valid(source.arena(), entry.value.data.span))
                bytes = entry.value.data.span.size;
            if (!charge_property(budget, 4U, bytes, options,
                                 MetadataReconcileField::None,
                                 MetadataReconcileFamily::Iptc, id, error))
                return error->status;
            if (!flags_valid(entry)
                || !pending_value_shape(entry, source.arena(), true, &bytes)
                || bytes != kUtf8Escape.size()) {
                marker_shape_valid = false;
                continue;
            }
            marker_bytes = bytes;
        }
        if (has_marker) {
            if (active_markers != 1U || !marker_shape_valid) {
                *error = failure(MetadataReconcileStatus::SourceEncodingConflict,
                                 MetadataReconcileField::None,
                                 MetadataReconcileFamily::Iptc,
                                 MetadataReconcileStage::Preflight, marker_id);
                return error->status;
            }
            const Entry& marker = source.entry(marker_id);
            const std::span<const std::byte> marker_value = source.arena().span(
                marker.value.data.span);
            if (marker_value.size() != kUtf8Escape.size()
                || !std::equal(marker_value.begin(), marker_value.end(),
                               kUtf8Escape.begin())) {
                *error = failure(MetadataReconcileStatus::SourceEncodingConflict,
                                 MetadataReconcileField::None,
                                 MetadataReconcileFamily::Iptc,
                                 MetadataReconcileStage::Preflight, marker_id);
                return error->status;
            }
            (void)marker_bytes;
            return MetadataReconcileStatus::Ok;
        }

        struct IptcScanRef final {
            EntryId entry  = kInvalidEntryId;
            uint32_t bytes = 0U;
            bool supported = true;
            bool owned     = false;
        };
        std::vector<IptcScanRef> refs;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind != MetaKeyKind::IptcDataset
                || any(entry.flags, EntryFlags::Deleted))
                continue;
            uint32_t bytes       = 0U;
            const bool supported = entry.value.kind == MetaValueKind::Bytes
                                   || entry.value.kind == MetaValueKind::Text;
            if (supported) {
                if (entry.value.count != entry.value.data.span.size
                    || !span_valid(source.arena(), entry.value.data.span)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     MetadataReconcileField::None,
                                     MetadataReconcileFamily::Iptc,
                                     MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
                bytes = entry.value.data.span.size;
            }
            if (!charge_property(budget, 4U, bytes, options,
                                 MetadataReconcileField::None,
                                 MetadataReconcileFamily::Iptc, id, error))
                return error->status;
            refs.push_back(
                IptcScanRef { id, bytes, supported,
                              field_dataset_selected(entry, plans, true) });
        }
        for (const IptcScanRef& ref : refs) {
            const Entry& entry = source.entry(ref.entry);
            if (!ref.supported) {
                if (!ref.owned) {
                    *error = failure(
                        MetadataReconcileStatus::SourceEncodingConflict,
                        MetadataReconcileField::None,
                        MetadataReconcileFamily::Iptc,
                        MetadataReconcileStage::Preflight, ref.entry);
                    return error->status;
                }
                continue;
            }
            const std::span<const std::byte> bytes = source.arena().span(
                entry.value.data.span);
            bool non_ascii = false;
            for (const std::byte byte : bytes)
                non_ascii = non_ascii
                            || std::to_integer<uint8_t>(byte) >= 0x80U;
            if (non_ascii && !ref.owned) {
                *error = failure(MetadataReconcileStatus::SourceEncodingConflict,
                                 MetadataReconcileField::None,
                                 MetadataReconcileFamily::Iptc,
                                 MetadataReconcileStage::Preflight, ref.entry);
                return error->status;
            }
        }
        return MetadataReconcileStatus::Ok;
    }

    static MetadataReconcileStatus preflight_projected_iptc_charsets(
        const MetaStore& source, const FieldPlan* plans,
        const MetadataReconcileOptions& options,
        std::span<const CharsetBlock> validated_source_blocks, Budget* budget,
        MetadataReconcileResult* error)
    {
        std::vector<BlockId> required_blocks;
        std::vector<BlockId> non_ascii_blocks;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.project_xmp_to_iptc)
                continue;
            for (const ValueRef& value : projection_xmp(plan).values) {
                if (!value_has_non_ascii(value))
                    continue;
                if (value.block == kInvalidBlockId
                    || value.block >= source.block_count()) {
                    *error = failure(
                        MetadataReconcileStatus::SourceEncodingConflict,
                        plan.field, MetadataReconcileFamily::Xmp,
                        MetadataReconcileStage::Preflight, value.entry);
                    return error->status;
                }
                non_ascii_blocks.push_back(value.block);
            }
            for (const ValueRef& value : projection_xmp(plan).values) {
                if (value.block != kInvalidBlockId
                    && value.block < source.block_count())
                    required_blocks.push_back(value.block);
            }
        }
        if (required_blocks.empty())
            return MetadataReconcileStatus::Ok;
        std::sort(required_blocks.begin(), required_blocks.end(),
                  BlockOrder {});
        required_blocks.erase(std::unique(required_blocks.begin(),
                                          required_blocks.end()),
                              required_blocks.end());
        std::sort(non_ascii_blocks.begin(), non_ascii_blocks.end(),
                  BlockOrder {});
        non_ascii_blocks.erase(std::unique(non_ascii_blocks.begin(),
                                           non_ascii_blocks.end()),
                               non_ascii_blocks.end());

        std::vector<CharsetBlock> output_blocks;
        output_blocks.reserve(required_blocks.size());
        for (const BlockId block : required_blocks) {
            CharsetBlock info { block };
            const size_t old_index = charset_info_index(validated_source_blocks,
                                                        block);
            if (old_index < validated_source_blocks.size()) {
                info = validated_source_blocks[old_index];
            } else {
                for (EntryId id = 0U; id < source.entries().size(); ++id) {
                    const Entry& entry = source.entry(id);
                    if (entry.key.kind != MetaKeyKind::IptcDataset
                        || entry.key.data.iptc_dataset.record != 1U
                        || entry.key.data.iptc_dataset.dataset != 90U
                        || entry.origin.block != block
                        || any(entry.flags, EntryFlags::Deleted))
                        continue;
                    ++info.marker_count;
                    if (info.marker == kInvalidEntryId)
                        info.marker = id;
                    uint32_t value_bytes = 0U;
                    if ((entry.value.kind == MetaValueKind::Bytes
                         || entry.value.kind == MetaValueKind::Text)
                        && entry.value.count == entry.value.data.span.size
                        && span_valid(source.arena(), entry.value.data.span))
                        value_bytes = entry.value.data.span.size;
                    if (!charge_property(budget, 4U, value_bytes, options,
                                         MetadataReconcileField::None,
                                         MetadataReconcileFamily::Iptc, id,
                                         error))
                        return error->status;
                    if (!flags_valid(entry)
                        || !pending_value_shape(entry, source.arena(), true,
                                                &value_bytes)
                        || value_bytes != kUtf8Escape.size()) {
                        info.marker_shape_valid = false;
                        continue;
                    }
                    const std::span<const std::byte> bytes
                        = source.arena().span(entry.value.data.span);
                    info.marker_value_valid = std::equal(bytes.begin(),
                                                         bytes.end(),
                                                         kUtf8Escape.begin());
                }
            }
            if (info.marker_count > 1U || !info.marker_shape_valid
                || (info.marker_count == 1U && !info.marker_value_valid)) {
                *error
                    = failure(MetadataReconcileStatus::SourceEncodingConflict,
                              MetadataReconcileField::None,
                              MetadataReconcileFamily::Iptc,
                              MetadataReconcileStage::Preflight, info.marker);
                return error->status;
            }
            output_blocks.push_back(info);
        }

        if (!non_ascii_blocks.empty()) {
            const bool has_global_marker = source_has_active_marker(source);
            if (!has_global_marker && non_ascii_blocks.size() > 1U) {
                MetadataReconcileField failed_field
                    = MetadataReconcileField::None;
                EntryId failed_entry = kInvalidEntryId;
                for (const FieldPlan& plan :
                     std::span<const FieldPlan, 5U>(plans, 5U)) {
                    if (plan.project_xmp_to_iptc
                        && failed_entry == kInvalidEntryId) {
                        for (const ValueRef& value :
                             projection_xmp(plan).values) {
                            if (!value_has_non_ascii(value))
                                continue;
                            failed_field = plan.field;
                            failed_entry = value.entry;
                            break;
                        }
                    }
                }
                *error
                    = failure(MetadataReconcileStatus::SourceEncodingConflict,
                              failed_field, MetadataReconcileFamily::Xmp,
                              MetadataReconcileStage::Preflight, failed_entry);
                return error->status;
            }
            for (const BlockId block : non_ascii_blocks) {
                const size_t index = charset_info_index(output_blocks, block);
                if (index == output_blocks.size())
                    return MetadataReconcileStatus::InternalError;
                const bool marker_exists = output_blocks[index].marker_count
                                           == 1U;
                if (has_global_marker && !marker_exists) {
                    MetadataReconcileField failed_field
                        = MetadataReconcileField::None;
                    EntryId failed_entry = kInvalidEntryId;
                    for (const FieldPlan& plan :
                         std::span<const FieldPlan, 5U>(plans, 5U)) {
                        if (!plan.project_xmp_to_iptc)
                            continue;
                        for (const ValueRef& value :
                             projection_xmp(plan).values) {
                            if (value.block == block
                                && value_has_non_ascii(value)) {
                                failed_field = plan.field;
                                failed_entry = value.entry;
                                break;
                            }
                        }
                        if (failed_entry != kInvalidEntryId)
                            break;
                    }
                    *error = failure(
                        MetadataReconcileStatus::SourceEncodingConflict,
                        failed_field, MetadataReconcileFamily::Xmp,
                        MetadataReconcileStage::Preflight, failed_entry);
                    return error->status;
                }
                if (!has_global_marker && marker_exists) {
                    *error = failure(
                        MetadataReconcileStatus::SourceEncodingConflict,
                        MetadataReconcileField::None,
                        MetadataReconcileFamily::Iptc,
                        MetadataReconcileStage::Preflight,
                        output_blocks[index].marker);
                    return error->status;
                }
            }
        }
        return MetadataReconcileStatus::Ok;
    }

    static MetadataReconcileResult
    resolve_authority(FieldPlan* plans, const MetadataReconcileOptions& options,
                      uint32_t* groups) noexcept
    {
        if (!plans || !groups)
            return failure(MetadataReconcileStatus::InternalError,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::None,
                           MetadataReconcileStage::ResolveAuthority);
        *groups = 0U;
        for (FieldPlan& plan : std::span<FieldPlan, 5U>(plans, 5U)) {
            if (!field_selected(plan, options))
                continue;

            const bool canonical_xmp_present = plan.xmp.state
                                               != ValueState::Absent;
            const bool alias_xmp_present = options.enable_exif_bridge
                                           && plan.alias.state
                                                  != ValueState::Absent;
            const bool canonical_xmp_active = plan.xmp.state
                                              == ValueState::Active;
            const bool alias_xmp_active = plan.alias.state
                                          == ValueState::Active;
            plan.xmp_family
                = canonical_xmp_active
                      ? &plan.xmp
                      : (alias_xmp_active
                             ? &plan.alias
                             : (canonical_xmp_present
                                    ? &plan.xmp
                                    : (alias_xmp_present ? &plan.alias
                                                         : &plan.xmp)));
            if (canonical_xmp_active && alias_xmp_active
                && !values_equal(plan, plan.xmp, plan.alias)
                && options.conflict_policy
                       == MetadataReconcileConflictPolicy::FailOnConflict) {
                return failure(MetadataReconcileStatus::Conflict, plan.field,
                               MetadataReconcileFamily::Xmp,
                               MetadataReconcileStage::ResolveAuthority,
                               plan.alias.first_entry != kInvalidEntryId
                                   ? plan.alias.first_entry
                                   : plan.alias.dirty_delete);
            }

            if (exif_field_selected(plan.field, options)
                && plan.field == MetadataReconcileField::Creator
                && (plan.xmp.values.size() > 1U
                    || plan.iptc.values.size() > 1U)) {
                return failure(MetadataReconcileStatus::UnsupportedSourceShape,
                               plan.field,
                               plan.iptc.values.size() > 1U
                                   ? MetadataReconcileFamily::Iptc
                                   : MetadataReconcileFamily::Xmp,
                               MetadataReconcileStage::ResolveAuthority,
                               plan.iptc.values.size() > 1U
                                   ? plan.iptc.values[1U].entry
                                   : plan.xmp.values[1U].entry);
            }

            const bool xmp_present = effective_xmp(plan).state
                                     != ValueState::Absent;
            const bool iptc_present = options.participate_iptc
                                      && plan.iptc.state != ValueState::Absent;
            const bool exif_present = options.enable_exif_bridge
                                      && plan.exif.state != ValueState::Absent
                                      && exif_field_selected(plan.field,
                                                             options);
            if (!xmp_present && !iptc_present && !exif_present)
                continue;
            if (options.conflict_policy
                == MetadataReconcileConflictPolicy::FailOnConflict) {
                const FamilyState* families[3U] = { nullptr, nullptr, nullptr };
                const MetadataReconcileFamily names[3U] = {
                    MetadataReconcileFamily::Xmp,
                    MetadataReconcileFamily::Iptc,
                    MetadataReconcileFamily::Exif,
                };
                for (size_t i = 0U; i < 3U; ++i) {
                    if (!family_enabled(names[i], options))
                        continue;
                    families[i] = state_for(plan, names[i]);
                    if (!families[i]
                        || families[i]->state == ValueState::Absent)
                        families[i] = nullptr;
                }
                for (size_t i = 0U; i < 3U; ++i) {
                    for (size_t j = i + 1U; j < 3U; ++j) {
                        if (families[i] && families[j]
                            && !values_equal(plan, *families[i], *families[j])) {
                            const FamilyState* failed = families[j];
                            return failure(
                                MetadataReconcileStatus::Conflict, plan.field,
                                names[j],
                                MetadataReconcileStage::ResolveAuthority,
                                failed->first_entry != kInvalidEntryId
                                    ? failed->first_entry
                                    : failed->dirty_delete);
                        }
                    }
                }
            }
            for (const MetadataReconcileFamily family :
                 options.family_priority) {
                if (!family_enabled(family, options))
                    continue;
                const FamilyState* state = state_for(plan, family);
                if (state && state->state != ValueState::Absent) {
                    plan.winner        = family;
                    plan.authoritative = state;
                    break;
                }
            }
            if (plan.winner == MetadataReconcileFamily::None)
                continue;
            if (exif_field_selected(plan.field, options)) {
                for (const ValueRef& value : plan.authoritative->values) {
                    if (value.text.size()
                        > kMetadataProfileTranslationMaxTextBytesPerProperty)
                        return failure(MetadataReconcileStatus::ValueTooLong,
                                       plan.field, plan.winner,
                                       MetadataReconcileStage::ResolveAuthority,
                                       value.entry);
                }
            }
            ++(*groups);
            if (!project_xmp_needed(plan, options)
                || !project_iptc_needed(plan, options))
                return failure(MetadataReconcileStatus::InternalError,
                               plan.field, plan.winner,
                               MetadataReconcileStage::ResolveAuthority);
            plan.seed_canonical_xmp
                = plan.winner == MetadataReconcileFamily::Exif
                  || (plan.winner == MetadataReconcileFamily::Xmp
                      && plan.xmp_family != &plan.xmp);
        }
        return MetadataReconcileResult {};
    }

    static bool field_plan_has_non_ascii_xmp(const FieldPlan& plan) noexcept
    {
        if (!plan.project_xmp_to_iptc)
            return false;
        for (const ValueRef& value : projection_xmp(plan).values) {
            if (value_has_non_ascii(value))
                return true;
        }
        return false;
    }

    static bool cost_xmp_to_iptc(const MetaStore& source, FieldPlan& plan,
                                 Budget* budget) noexcept
    {
        if (!plan.project_xmp_to_iptc || !budget)
            return true;
        const FamilyState& xmp    = projection_xmp(plan);
        const size_t source_count = xmp.state == ValueState::Active
                                        ? xmp.values.size()
                                        : 0U;
        const size_t target_count = plan.iptc.state == ValueState::Active
                                        ? plan.iptc.values.size()
                                        : 0U;
        const size_t overlap      = std::min(source_count, target_count);
        uint64_t ops              = 0U;
        uint64_t output_text      = 0U;
        uint64_t output_key_value = 0U;
        uint64_t output_arena     = 0U;
        for (size_t i = 0U; i < overlap; ++i) {
            if (xmp.values[i].text == plan.iptc.values[i].text)
                continue;
            ++ops;
            const uint64_t size = xmp.values[i].text.size();
            output_text += size;
            output_key_value += size;
            output_arena += size;
        }
        for (size_t i = overlap; i < source_count; ++i) {
            const ValueRef& value    = xmp.values[i];
            const uint64_t size      = value.text.size();
            const uint64_t type_name = value_wire_name_bytes(source, value);
            ++ops;
            ++budget->additions;
            ++budget->stage_additions;
            output_text += size;
            output_key_value += 4U + size + type_name;
            output_arena += size + type_name;
        }
        ops += target_count - overlap;
        if (!add_u64(&budget->operations, ops)
            || !add_u64(&budget->output_text_bytes, output_text)
            || !add_u64(&budget->output_key_value_bytes, output_key_value)
            || !add_u64(&budget->output_arena_bytes, output_arena)
            || !add_u64(&budget->stage_arena_bytes, output_arena))
            return false;
        return true;
    }

    static uint64_t origin_name_size(const MetaStore& source,
                                     const ValueRef& value) noexcept
    {
        return source.entry(value.entry).origin.wire_type_name.size;
    }

    static uint64_t merge_new_index_key_bytes(const Descriptor& descriptor,
                                              uint32_t first_index,
                                              size_t count) noexcept
    {
        uint64_t total = 0U;
        for (size_t i = 0U; i < count; ++i)
            total += descriptor.schema_ns.size()
                     + indexed_path_length(descriptor,
                                           first_index
                                               + static_cast<uint32_t>(i));
        return total;
    }

    static bool cost_iptc_to_xmp(const MetaStore& source, FieldPlan& plan,
                                 Budget* budget) noexcept
    {
        if (!plan.project_iptc_to_xmp || !budget)
            return true;
        const size_t source_count = plan.iptc.state == ValueState::Active
                                        ? plan.iptc.values.size()
                                        : 0U;
        const size_t target_count = plan.xmp.state == ValueState::Active
                                        ? plan.xmp.values.size()
                                        : 0U;
        const size_t overlap      = std::min(source_count, target_count);
        const uint32_t merge_ops  = positional_changes(plan.iptc, plan.xmp);
        const uint64_t scratch_clear_ops   = target_count;
        const uint64_t scratch_project_ops = source_count;
        if (!add_u64(&budget->operations, scratch_clear_ops)
            || !add_u64(&budget->operations, scratch_project_ops)
            || !add_u64(&budget->operations, merge_ops))
            return false;
        if (!add_u64(&budget->scratch_additions, source_count))
            return false;

        uint64_t scratch_text      = 0U;
        uint64_t scratch_key_value = 0U;
        uint64_t scratch_arena     = 0U;
        for (size_t i = 0U; i < source_count; ++i) {
            const ValueRef& value    = plan.iptc.values[i];
            const uint64_t text_size = value.text.size();
            const uint64_t key_size
                = plan.descriptor->schema_ns.size()
                  + indexed_path_length(*plan.descriptor,
                                        static_cast<uint32_t>(i + 1U));
            const uint64_t type_name = origin_name_size(source, value);
            scratch_text += text_size;
            scratch_key_value += key_size + text_size + type_name;
            scratch_arena += key_size + text_size + type_name;
        }
        if (!add_u64(&budget->output_text_bytes, scratch_text)
            || !add_u64(&budget->output_key_value_bytes, scratch_key_value)
            || !add_u64(&budget->scratch_arena_bytes, scratch_arena))
            return false;

        uint64_t final_text      = 0U;
        uint64_t final_key_value = 0U;
        uint64_t final_arena     = 0U;
        for (size_t i = 0U; i < overlap; ++i) {
            if (plan.iptc.values[i].text == plan.xmp.values[i].text)
                continue;
            const uint64_t size = plan.iptc.values[i].text.size();
            final_text += size;
            final_key_value += size;
            final_arena += size;
        }
        if (source_count > target_count) {
            const size_t missing     = source_count - target_count;
            const uint32_t max_index = plan.xmp.max_index;
            if (max_index > UINT32_MAX - missing)
                return false;
            const uint32_t first_new = max_index + 1U;
            const uint64_t key_size
                = merge_new_index_key_bytes(*plan.descriptor, first_new,
                                            missing);
            for (size_t i = target_count; i < source_count; ++i) {
                const uint64_t text_size = plan.iptc.values[i].text.size();
                const uint64_t type_name
                    = origin_name_size(source, plan.iptc.values[i]);
                final_text += text_size;
                final_key_value += text_size + type_name;
                final_arena += text_size + type_name;
            }
            final_key_value += key_size;
            final_arena += key_size;
            budget->additions += missing;
        }
        if (!add_u64(&budget->output_text_bytes, final_text)
            || !add_u64(&budget->output_key_value_bytes, final_key_value)
            || !add_u64(&budget->output_arena_bytes, final_arena))
            return false;
        return true;
    }

    static uint32_t xmp_next_index(const FieldPlan& plan) noexcept;

    static bool account_projected_operation(Budget* budget, uint64_t key_bytes,
                                            uint64_t value_bytes,
                                            bool add_entry) noexcept
    {
        if (!budget || !add_u64(&budget->operations, 1U)
            || !add_u64(&budget->output_text_bytes, value_bytes)
            || !add_u64(&budget->output_key_value_bytes, key_bytes + value_bytes)
            || !add_u64(&budget->output_arena_bytes, key_bytes + value_bytes)
            || !add_u64(&budget->stage_arena_bytes, key_bytes + value_bytes))
            return false;
        if (add_entry) {
            ++budget->additions;
            ++budget->stage_additions;
        }
        return true;
    }

    static bool profile_native_matches(const MetaStore& source,
                                       const FieldPlan& plan,
                                       std::string_view expected) noexcept
    {
        if (plan.exif.values.size() != 1U)
            return false;
        const Entry& entry       = source.entry(plan.exif.values[0U].entry);
        const uint16_t wire_type = detail::exif_text_wire_type(source.arena(),
                                                               entry);
        if (entry.origin.wire_type.family != WireFamily::Tiff
            || (wire_type != 2U && wire_type != 129U)
            || entry.origin.wire_type.code != wire_type
            || (wire_type == 129U
                && entry.value.text_encoding != TextEncoding::Utf8))
            return false;
        std::string_view native;
        if (!detail::exif_text_view(source.arena(), entry.value, true, &native)
            || native != expected)
            return false;
        const uint64_t expected_wire_count = wire_type == 129U
                                                 ? native.size() + 1U
                                                 : entry.value.count + 1U;
        return entry.origin.wire_count == expected_wire_count;
    }

    static bool cost_exif_bridge(const MetaStore& source, FieldPlan* plans,
                                 Budget* budget)
    {
        if (!plans || !budget)
            return false;
        for (FieldPlan& plan : std::span<FieldPlan, 5U>(plans, 5U)) {
            const ExifAliasDescriptor* alias = exif_alias_for_field(plan.field);
            if (!alias || plan.winner == MetadataReconcileFamily::None)
                continue;
            plan.project_xmp_to_exif   = false;
            const FamilyState& desired = final_family(plan);
            const EntryId native_source
                = !plan.exif.values.empty()
                      ? plan.exif.values.front().entry
                      : (plan.exif.projection_dirty_delete != kInvalidEntryId
                             ? plan.exif.projection_dirty_delete
                             : plan.exif.clean_delete);
            const uint64_t alias_key_bytes = alias->schema_ns.size()
                                             + alias->path.size();
            const uint64_t canonical_key_bytes
                = plan.descriptor->schema_ns.size()
                  + (plan.descriptor->repeated
                         ? indexed_path_length(*plan.descriptor,
                                               xmp_next_index(plan))
                         : plan.descriptor->property_path.size());

            if (plan.seed_canonical_xmp) {
                if (desired.state == ValueState::Active) {
                    if (desired.values.size() != 1U)
                        return false;
                    const std::string_view text = desired.values[0U].text;
                    if (plan.xmp.values.empty()) {
                        if (!account_projected_operation(budget,
                                                         canonical_key_bytes,
                                                         text.size(), true))
                            return false;
                    } else if (plan.xmp.values.size() == 1U
                               && plan.xmp.values[0U].text != text) {
                        if (!account_projected_operation(budget, 0U,
                                                         text.size(), false))
                            return false;
                    } else if (plan.xmp.values.size() > 1U) {
                        return false;
                    }
                } else if (desired.state == ValueState::Deleted) {
                    if (!plan.xmp.values.empty()) {
                        for (size_t i = 0U; i < plan.xmp.values.size(); ++i) {
                            if (!add_u64(&budget->operations, 1U))
                                return false;
                        }
                    } else if (plan.xmp.deletions.empty()) {
                        if (!account_projected_operation(budget,
                                                         canonical_key_bytes,
                                                         0U, true))
                            return false;
                    }
                }
            }

            if (desired.state == ValueState::Active) {
                if (desired.values.size() != 1U)
                    return false;
                const std::string_view text = desired.values[0U].text;
                if (plan.alias.values.empty()) {
                    if (!account_projected_operation(budget, alias_key_bytes,
                                                     text.size(), true))
                        return false;
                } else if (plan.alias.values[0U].text != text) {
                    if (!account_projected_operation(budget, 0U, text.size(),
                                                     false))
                        return false;
                }
            } else if (desired.state == ValueState::Deleted) {
                if (!plan.alias.values.empty()) {
                    if (!add_u64(&budget->operations, 1U))
                        return false;
                } else if (plan.alias.deletions.empty()) {
                    if (!account_projected_operation(budget, alias_key_bytes,
                                                     0U, true))
                        return false;
                }
            }

            bool profile_work                      = false;
            uint64_t profile_translator_operations = 0U;
            uint64_t profile_translator_additions  = 0U;
            uint64_t profile_edit_key              = 0U;
            uint64_t profile_edit_value            = 0U;
            if (desired.state == ValueState::Active) {
                if (desired.values.size() != 1U)
                    return false;
                const std::string_view text = desired.values[0U].text;
                if (plan.exif.values.empty()) {
                    profile_work                  = true;
                    profile_translator_operations = 1U;
                    profile_translator_additions  = 1U;
                    profile_edit_key              = 4U;
                    profile_edit_value            = text.size();
                    if (!account_projected_operation(budget, 4U, text.size(),
                                                     true))
                        return false;
                } else if (profile_native_matches(source, plan, text)) {
                    const Entry& native = source.entry(
                        plan.exif.values[0U].entry);
                    if (!any(native.flags, EntryFlags::Dirty)) {
                        profile_work                  = true;
                        profile_translator_operations = 1U;
                        profile_edit_value = native.value.data.span.size;
                        if (!account_projected_operation(budget, 0U,
                                                         profile_edit_value,
                                                         false))
                            return false;
                    }
                } else {
                    profile_work                  = true;
                    profile_translator_operations = 1U;
                    profile_edit_value            = text.size();
                    if (!account_projected_operation(budget, 0U, text.size(),
                                                     false))
                        return false;
                }
            } else if (desired.state == ValueState::Deleted) {
                if (!plan.exif.values.empty()) {
                    profile_work                  = true;
                    profile_translator_operations = plan.exif.values.size();
                    if (!add_u64(&budget->operations, plan.exif.values.size()))
                        return false;
                } else if (plan.exif.projection_dirty_delete
                           != kInvalidEntryId) {
                    profile_work = false;
                } else if (plan.exif.clean_delete != kInvalidEntryId) {
                    profile_work                  = true;
                    profile_translator_operations = 1U;
                    if (!add_u64(&budget->operations, 1U))
                        return false;
                } else if (!account_projected_operation(budget, 4U, 0U, true)) {
                    return false;
                } else {
                    profile_work                  = true;
                    profile_translator_operations = 1U;
                    profile_translator_additions  = 1U;
                    profile_edit_key              = 4U;
                }
            }
            plan.project_xmp_to_exif = profile_work;
            if (!profile_work)
                continue;
            const uint64_t profile_source_entries
                = 1U + (native_source != kInvalidEntryId ? 1U : 0U);
            const uint64_t profile_scratch_entries
                = profile_source_entries * 2U + profile_translator_additions;
            if (!add_u64(&budget->profile_scratch_entries,
                         profile_scratch_entries)
                || !reserve_operation(budget,
                                      profile_source_entries
                                          + profile_translator_operations))
                return false;

            uint64_t profile_base_arena = alias_key_bytes;
            uint64_t profile_base_text  = desired.state == ValueState::Active
                                              ? desired.values[0U].text.size()
                                              : 0U;
            // The normalized alias stores its selected text as well as its key.
            profile_base_arena += profile_base_text;
            uint64_t profile_native_bytes = 0U;
            if (native_source != kInvalidEntryId) {
                profile_base_arena += 4U;
                if (!any(source.entry(native_source).flags,
                         EntryFlags::Deleted)) {
                    profile_native_bytes
                        = source.entry(native_source).value.data.span.size;
                    profile_base_arena += profile_native_bytes;
                    profile_base_text += profile_native_bytes;
                }
            }
            if (!add_u64(&budget->scratch_arena_bytes, profile_base_arena * 2U)
                || !add_u64(&budget->output_text_bytes, profile_base_text * 2U)
                || !add_u64(&budget->output_key_value_bytes,
                            profile_base_arena * 2U)
                || !add_u64(&budget->scratch_arena_bytes,
                            profile_edit_key + profile_edit_value)
                || !add_u64(&budget->output_text_bytes, profile_edit_value)
                || !add_u64(&budget->output_key_value_bytes,
                            profile_edit_key + profile_edit_value))
                return false;
        }
        return true;
    }

    static bool source_marker_present_for_projection(
        const MetaStore& source, const FieldPlan* plans,
        EntryId* first_non_ascii_source) noexcept
    {
        bool non_ascii = false;
        if (first_non_ascii_source)
            *first_non_ascii_source = kInvalidEntryId;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.project_xmp_to_iptc)
                continue;
            for (const ValueRef& value : projection_xmp(plan).values) {
                if (!value_has_non_ascii(value))
                    continue;
                non_ascii = true;
                if (first_non_ascii_source
                    && *first_non_ascii_source == kInvalidEntryId)
                    *first_non_ascii_source = value.entry;
            }
        }
        return non_ascii && source_has_active_marker(source);
    }

    static bool cost_utf8_marker(const MetaStore& source,
                                 const FieldPlan* plans, Budget* budget)
    {
        EntryId source_entry = kInvalidEntryId;
        const bool has_existing_marker
            = source_marker_present_for_projection(source, plans,
                                                   &source_entry);
        bool needs_marker = source_entry != kInvalidEntryId;
        if (!needs_marker || has_existing_marker)
            return true;
        if (source_entry >= source.entries().size())
            return false;
        const Entry& entry       = source.entry(source_entry);
        const uint64_t type_name = entry.origin.wire_type_name.size;
        ++budget->operations;
        ++budget->additions;
        ++budget->stage_additions;
        return add_u64(&budget->output_key_value_bytes, 7U + type_name)
               && add_u64(&budget->output_arena_bytes, 3U + type_name)
               && add_u64(&budget->stage_arena_bytes, 3U + type_name);
    }

    static bool project_xmp_to_iptc_non_ascii(const FieldPlan* plans) noexcept
    {
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (field_plan_has_non_ascii_xmp(plan))
                return true;
        }
        return false;
    }

    static MetadataReconcileStatus
    preflight_costs(const MetaStore& source, FieldPlan* plans,
                    const MetadataReconcileOptions& options, Budget* budget,
                    std::span<const CharsetBlock> source_charsets,
                    MetadataReconcileResult* error)
    {
        if (!plans || !budget || !error)
            return MetadataReconcileStatus::InternalError;
        for (FieldPlan& plan : std::span<FieldPlan, 5U>(plans, 5U)) {
            if (!cost_xmp_to_iptc(source, plan, budget)
                || !cost_iptc_to_xmp(source, plan, budget)) {
                *error = failure(MetadataReconcileStatus::SourceLimitExceeded,
                                 plan.field, plan.winner,
                                 MetadataReconcileStage::Preflight);
                return error->status;
            }
        }
        if (options.enable_exif_bridge
            && !cost_exif_bridge(source, plans, budget)) {
            *error = failure(MetadataReconcileStatus::SourceLimitExceeded,
                             MetadataReconcileField::None,
                             MetadataReconcileFamily::Exif,
                             MetadataReconcileStage::Preflight);
            return error->status;
        }
        bool needs_iptc_scratch = false;
        for (const FieldPlan& plan : std::span<const FieldPlan, 5U>(plans, 5U))
            needs_iptc_scratch = needs_iptc_scratch || plan.project_iptc_to_xmp;
        if (needs_iptc_scratch) {
            for (EntryId id = 0U; id < source.entries().size(); ++id) {
                const Entry& entry = source.entry(id);
                if (entry.key.kind != MetaKeyKind::IptcDataset
                    || entry.key.data.iptc_dataset.record != 1U
                    || entry.key.data.iptc_dataset.dataset != 90U
                    || entry.origin.block != kInvalidBlockId
                    || any(entry.flags, EntryFlags::Deleted))
                    continue;
                uint32_t value_bytes = 0U;
                if ((entry.value.kind == MetaValueKind::Bytes
                     || entry.value.kind == MetaValueKind::Text)
                    && entry.value.count == entry.value.data.span.size
                    && span_valid(source.arena(), entry.value.data.span))
                    value_bytes = entry.value.data.span.size;
                if (!charge_property(budget, 4U, value_bytes, options,
                                     MetadataReconcileField::None,
                                     MetadataReconcileFamily::Iptc, id, error))
                    return error->status;
                if (!reserve_operation(budget, 1U)) {
                    *error = failure(
                        MetadataReconcileStatus::OperationLimitExceeded,
                        MetadataReconcileField::None,
                        MetadataReconcileFamily::Iptc,
                        MetadataReconcileStage::Preflight, id);
                    return error->status;
                }
            }
        }
        if (!cost_utf8_marker(source, plans, budget)) {
            *error = failure(MetadataReconcileStatus::SourceLimitExceeded,
                             MetadataReconcileField::None,
                             MetadataReconcileFamily::Xmp,
                             MetadataReconcileStage::Preflight);
            return error->status;
        }
        if (project_xmp_to_iptc_non_ascii(plans)) {
            const MetadataReconcileStatus charset_status
                = preflight_global_xmp_to_iptc_charset(source, plans, options,
                                                       budget, error);
            if (charset_status != MetadataReconcileStatus::Ok)
                return charset_status;
        }
        const MetadataReconcileStatus projected_charset_status
            = preflight_projected_iptc_charsets(source, plans, options,
                                                source_charsets, budget, error);
        if (projected_charset_status != MetadataReconcileStatus::Ok)
            return projected_charset_status;
        const uint64_t total_text = budget->input_text_bytes
                                    + budget->output_text_bytes;
        const uint64_t total_participating = budget->input_key_value_bytes
                                             + budget->output_key_value_bytes;
        const uint64_t arena_limit = std::min(
            { options.max_source_arena_bytes,
              options.validation.max_arena_bytes,
              kMetadataReconcileMaxSourceArenaBytes });
        const uint64_t entry_limit = std::min<uint64_t>(
            { options.max_source_entries, options.validation.max_entries,
              kMetadataReconcileMaxSourceEntries });
        if (total_text > options.max_total_text_bytes
            || total_text > kMetadataReconcileMaxTotalTextBytes
            || total_participating > options.max_total_participating_bytes
            || total_participating
                   > kMetadataReconcileMaxTotalParticipatingBytes) {
            *error = failure(MetadataReconcileStatus::SourceLimitExceeded,
                             MetadataReconcileField::None,
                             MetadataReconcileFamily::None,
                             MetadataReconcileStage::Preflight);
            return error->status;
        }
        if (budget->operations > options.max_operations
            || budget->operations > kMetadataReconcileMaxOperations) {
            *error = failure(MetadataReconcileStatus::OperationLimitExceeded,
                             MetadataReconcileField::None,
                             MetadataReconcileFamily::None,
                             MetadataReconcileStage::Preflight);
            return error->status;
        }
        uint64_t final_entries   = 0U;
        uint64_t stage_entries   = 0U;
        uint64_t scratch_entries = 0U;
        uint64_t final_arena     = 0U;
        uint64_t scratch_arena   = 0U;
        const bool counts_valid
            = add_u64(&final_entries, source.entries().size())
              && add_u64(&final_entries, budget->additions)
              && add_u64(&stage_entries, source.entries().size())
              && add_u64(&stage_entries, budget->stage_additions)
              && add_u64(&scratch_entries, stage_entries)
              && add_u64(&scratch_entries, budget->scratch_additions)
              && add_u64(&final_arena, source.arena().bytes().size())
              && add_u64(&final_arena, budget->output_arena_bytes)
              && add_u64(&scratch_arena, source.arena().bytes().size())
              && add_u64(&scratch_arena, budget->stage_arena_bytes)
              && add_u64(&scratch_arena, budget->scratch_arena_bytes);
        if (!counts_valid || final_entries > entry_limit
            || scratch_entries > entry_limit
            || budget->profile_scratch_entries > entry_limit
            || final_arena > arena_limit || scratch_arena > arena_limit) {
            *error = failure(MetadataReconcileStatus::SourceLimitExceeded,
                             MetadataReconcileField::None,
                             MetadataReconcileFamily::None,
                             MetadataReconcileStage::Preflight);
            return error->status;
        }
        return MetadataReconcileStatus::Ok;
    }

    static MetadataReconcileStatus
    map_descriptive_status(MetadataDescriptiveTranslationStatus status) noexcept
    {
        switch (status) {
        case MetadataDescriptiveTranslationStatus::Ok:
            return MetadataReconcileStatus::Ok;
        case MetadataDescriptiveTranslationStatus::InvalidOptions:
            return MetadataReconcileStatus::InvalidOptions;
        case MetadataDescriptiveTranslationStatus::SourceLimitExceeded:
            return MetadataReconcileStatus::SourceLimitExceeded;
        case MetadataDescriptiveTranslationStatus::AmbiguousSource:
            return MetadataReconcileStatus::AmbiguousSource;
        case MetadataDescriptiveTranslationStatus::InvalidSourceValue:
            return MetadataReconcileStatus::InvalidSourceValue;
        case MetadataDescriptiveTranslationStatus::ValueTooLong:
            return MetadataReconcileStatus::ValueTooLong;
        case MetadataDescriptiveTranslationStatus::NativeConflict:
            return MetadataReconcileStatus::Conflict;
        case MetadataDescriptiveTranslationStatus::NativeEncodingConflict:
            return MetadataReconcileStatus::SourceEncodingConflict;
        case MetadataDescriptiveTranslationStatus::EntryLimitExceeded:
            return MetadataReconcileStatus::EntryLimitExceeded;
        case MetadataDescriptiveTranslationStatus::OperationLimitExceeded:
            return MetadataReconcileStatus::OperationLimitExceeded;
        case MetadataDescriptiveTranslationStatus::UnsupportedSourceShape:
            return MetadataReconcileStatus::UnsupportedSourceShape;
        case MetadataDescriptiveTranslationStatus::InternalError:
            return MetadataReconcileStatus::InternalError;
        case MetadataDescriptiveTranslationStatus::NullOutput:
        case MetadataDescriptiveTranslationStatus::SourceNotFinalized:
        case MetadataDescriptiveTranslationStatus::AmbiguousLocation:
        case MetadataDescriptiveTranslationStatus::LocationNotFound:
            return MetadataReconcileStatus::ProjectionFailure;
        }
        return MetadataReconcileStatus::ProjectionFailure;
    }

    static MetadataReconcileStatus
    map_iptc_status(MetadataIptcToXmpStatus status) noexcept
    {
        switch (status) {
        case MetadataIptcToXmpStatus::Ok: return MetadataReconcileStatus::Ok;
        case MetadataIptcToXmpStatus::InvalidOptions:
            return MetadataReconcileStatus::InvalidOptions;
        case MetadataIptcToXmpStatus::SourceLimitExceeded:
            return MetadataReconcileStatus::SourceLimitExceeded;
        case MetadataIptcToXmpStatus::AmbiguousSource:
            return MetadataReconcileStatus::AmbiguousSource;
        case MetadataIptcToXmpStatus::InvalidSourceValue:
            return MetadataReconcileStatus::InvalidSourceValue;
        case MetadataIptcToXmpStatus::ValueTooLong:
            return MetadataReconcileStatus::ValueTooLong;
        case MetadataIptcToXmpStatus::SourceEncodingConflict:
            return MetadataReconcileStatus::SourceEncodingConflict;
        case MetadataIptcToXmpStatus::DestinationConflict:
            return MetadataReconcileStatus::Conflict;
        case MetadataIptcToXmpStatus::UnsupportedDestinationShape:
            return MetadataReconcileStatus::UnsupportedSourceShape;
        case MetadataIptcToXmpStatus::EntryLimitExceeded:
            return MetadataReconcileStatus::EntryLimitExceeded;
        case MetadataIptcToXmpStatus::OperationLimitExceeded:
            return MetadataReconcileStatus::OperationLimitExceeded;
        case MetadataIptcToXmpStatus::InternalError:
        case MetadataIptcToXmpStatus::NullOutput:
        case MetadataIptcToXmpStatus::SourceNotFinalized:
            return MetadataReconcileStatus::InternalError;
        }
        return MetadataReconcileStatus::ProjectionFailure;
    }

    static MetadataReconcileResult
    translation_failure(MetadataReconcileStatus status,
                        MetadataDescriptiveTranslationMapping mapping,
                        EntryId entry, MetadataReconcileFamily family,
                        MetadataReconcileStage stage) noexcept
    {
        return failure(status, field_for_mapping(mapping), family, stage,
                       entry);
    }

    static MetadataReconcileResult
    translate_batch_xmp_to_iptc(const MetaStore& source, const FieldPlan* plans,
                                const MetadataReconcileOptions& options,
                                MetaStore* candidate)
    {
        MetadataDescriptiveTranslationOptions translation;
        translation.source_mode = MetadataDescriptiveTranslationSourceMode::All;
        translation.conflict_policy
            = MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
        translation.title_to_iptc_object_name   = false;
        translation.description_to_iptc_caption = false;
        translation.creators_to_iptc_bylines    = false;
        translation.keywords_to_iptc_keywords   = false;
        translation.copyright_to_iptc_copyright = false;
        translation.credit_to_iptc_credit       = false;
        translation.source_to_iptc_source       = false;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.project_xmp_to_iptc)
                continue;
            switch (plan.field) {
            case MetadataReconcileField::Title:
                translation.title_to_iptc_object_name = true;
                break;
            case MetadataReconcileField::Description:
                translation.description_to_iptc_caption = true;
                break;
            case MetadataReconcileField::Creator:
                translation.creators_to_iptc_bylines = true;
                break;
            case MetadataReconcileField::Keywords:
                translation.keywords_to_iptc_keywords = true;
                break;
            case MetadataReconcileField::Rights:
                translation.copyright_to_iptc_copyright = true;
                break;
            case MetadataReconcileField::None: break;
            }
        }
        translation.max_source_properties = options.max_source_properties;
        translation.max_added_entries
            = kMetadataDescriptiveTranslationMaxAddedEntries;
        translation.max_operations       = options.max_operations;
        translation.max_total_text_bytes = options.max_total_text_bytes;
        const MetadataDescriptiveTranslationResult translated
            = translate_xmp_descriptive_metadata(source, translation,
                                                 candidate);
        const MetadataReconcileStatus status = map_descriptive_status(
            translated.status);
        if (status != MetadataReconcileStatus::Ok) {
            return translation_failure(status, translated.failed_mapping,
                                       translated.failed_source_entry,
                                       MetadataReconcileFamily::Xmp,
                                       MetadataReconcileStage::ProjectIptc);
        }
        MetadataReconcileResult result;
        result.translator_passes = 1U;
        result.entries_added     = translated.entries_added;
        result.entries_updated   = translated.entries_updated;
        result.entries_removed   = translated.entries_removed;
        return result;
    }

    static bool clear_projection_scratch(const MetaStore& source,
                                         const FieldPlan* plans,
                                         MetaStore* scratch,
                                         uint32_t* operations)
    {
        if (!plans || !scratch || !operations)
            return false;
        uint32_t op_count = 0U;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (plan.project_iptc_to_xmp)
                op_count += static_cast<uint32_t>(plan.xmp.values.size());
        }
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind == MetaKeyKind::IptcDataset
                && entry.key.data.iptc_dataset.record == 1U
                && entry.key.data.iptc_dataset.dataset == 90U
                && entry.origin.block == kInvalidBlockId
                && !any(entry.flags, EntryFlags::Deleted))
                ++op_count;
        }
        if (op_count == 0U) {
            *scratch    = source;
            *operations = 0U;
            return !scratch->resource_limit_exceeded();
        }
        MetaEdit edit;
        edit.reserve_ops(op_count);
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.project_iptc_to_xmp)
                continue;
            for (const ValueRef& value : plan.xmp.values)
                edit.tombstone(value.entry);
        }
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind == MetaKeyKind::IptcDataset
                && entry.key.data.iptc_dataset.record == 1U
                && entry.key.data.iptc_dataset.dataset == 90U
                && entry.origin.block == kInvalidBlockId
                && !any(entry.flags, EntryFlags::Deleted))
                edit.tombstone(id);
        }
        if (edit.ops().size() != op_count)
            return false;
        *scratch    = commit(source, std::span<const MetaEdit>(&edit, 1U));
        *operations = op_count;
        return !scratch->resource_limit_exceeded();
    }

    static MetadataReconcileResult
    translate_batch_iptc_to_xmp(const MetaStore& source, const FieldPlan* plans,
                                const MetadataReconcileOptions& options,
                                MetaStore* candidate,
                                uint32_t* scratch_operations)
    {
        MetaStore scratch;
        if (!clear_projection_scratch(source, plans, &scratch,
                                      scratch_operations))
            return failure(MetadataReconcileStatus::InternalError,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Xmp,
                           MetadataReconcileStage::ProjectXmp);
        MetadataIptcToXmpOptions translation;
        translation.source_mode = MetadataDescriptiveTranslationSourceMode::All;
        translation.conflict_policy
            = MetadataIptcToXmpConflictPolicy::ReplaceExisting;
        translation.title                   = false;
        translation.description             = false;
        translation.creators                = false;
        translation.keywords                = false;
        translation.copyright               = false;
        translation.credit                  = false;
        translation.source                  = false;
        translation.city                    = false;
        translation.sublocation             = false;
        translation.state                   = false;
        translation.country                 = false;
        translation.country_code            = false;
        translation.headline                = false;
        translation.instructions            = false;
        translation.transmission_reference  = false;
        translation.authors_position        = false;
        translation.caption_writer          = false;
        translation.category                = false;
        translation.supplemental_categories = false;
        translation.urgency                 = false;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.project_iptc_to_xmp)
                continue;
            switch (plan.field) {
            case MetadataReconcileField::Title: translation.title = true; break;
            case MetadataReconcileField::Description:
                translation.description = true;
                break;
            case MetadataReconcileField::Creator:
                translation.creators = true;
                break;
            case MetadataReconcileField::Keywords:
                translation.keywords = true;
                break;
            case MetadataReconcileField::Rights:
                translation.copyright = true;
                break;
            case MetadataReconcileField::None: break;
            }
        }
        translation.max_source_properties = options.max_source_properties;
        translation.max_added_entries     = kMetadataIptcToXmpMaxAddedEntries;
        translation.max_operations        = options.max_operations;
        translation.max_total_text_bytes  = options.max_total_text_bytes;
        const MetadataIptcToXmpResult translated
            = translate_iptc_to_xmp(scratch, translation, candidate);
        const MetadataReconcileStatus status = map_iptc_status(
            translated.status);
        if (status != MetadataReconcileStatus::Ok) {
            return translation_failure(status, translated.failed_mapping,
                                       translated.failed_source_entry,
                                       MetadataReconcileFamily::Iptc,
                                       MetadataReconcileStage::ProjectXmp);
        }
        MetadataReconcileResult result;
        result.translator_passes = 1U;
        result.entries_added     = translated.entries_added;
        result.entries_updated   = translated.entries_updated;
        result.entries_removed   = translated.entries_removed;
        return result;
    }

    static MetadataReconcileStatus
    collect_materialized_values(const MetaStore& candidate, FieldPlan* plans,
                                MetadataReconcileResult* error)
    {
        if (!plans || !error)
            return MetadataReconcileStatus::InternalError;
        for (FieldPlan& plan : std::span<FieldPlan, 5U>(plans, 5U)) {
            if (!plan.project_iptc_to_xmp)
                continue;
            plan.materialized.clear();
            for (EntryId id = 0U; id < candidate.entries().size(); ++id) {
                const Entry& entry = candidate.entry(id);
                if (entry.key.kind != MetaKeyKind::XmpProperty
                    || any(entry.flags, EntryFlags::Deleted))
                    continue;
                std::string_view schema;
                if (!xmp_namespace_text(candidate, entry, &schema))
                    continue;
                if (schema != plan.descriptor->schema_ns)
                    continue;
                std::string_view path;
                uint32_t key_bytes = 0U;
                if (!xmp_key_text(candidate, entry, &schema, &path,
                                  &key_bytes)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::ProjectXmp, id);
                    return error->status;
                }
                uint32_t index         = 0U;
                const XmpPathKind kind = xmp_path_kind(path, *plan.descriptor,
                                                       &index);
                if (kind == XmpPathKind::Other
                    || kind == XmpPathKind::LanguageSibling)
                    continue;
                if (kind != XmpPathKind::Target || !flags_valid(entry)) {
                    *error = failure(
                        MetadataReconcileStatus::UnsupportedSourceShape,
                        plan.field, MetadataReconcileFamily::Xmp,
                        MetadataReconcileStage::ProjectXmp, id);
                    return error->status;
                }
                std::string_view text;
                bool non_ascii = false;
                if (!read_text_value(candidate, entry, false, &text,
                                     &non_ascii)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::ProjectXmp, id);
                    return error->status;
                }
                plan.materialized.push_back(
                    ValueRef { id, index, entry.origin.block,
                               entry.origin.order_in_block, text });
            }
            std::sort(plan.materialized.begin(), plan.materialized.end(),
                      XmpIndexOrder {});
            if (!plan.descriptor->repeated && plan.materialized.size() > 1U) {
                *error = failure(MetadataReconcileStatus::AmbiguousSource,
                                 plan.field, MetadataReconcileFamily::Xmp,
                                 MetadataReconcileStage::ProjectXmp,
                                 plan.materialized[1U].entry);
                return error->status;
            }
            for (size_t i = 1U; i < plan.materialized.size(); ++i) {
                if (plan.materialized[i - 1U].index
                    == plan.materialized[i].index) {
                    *error = failure(MetadataReconcileStatus::AmbiguousSource,
                                     plan.field, MetadataReconcileFamily::Xmp,
                                     MetadataReconcileStage::ProjectXmp,
                                     plan.materialized[i].entry);
                    return error->status;
                }
            }
        }
        return MetadataReconcileStatus::Ok;
    }

    static bool make_indexed_path(const Descriptor& descriptor, uint32_t index,
                                  std::array<char, 96U>* storage,
                                  std::string_view* path) noexcept
    {
        if (!storage || !path)
            return false;
        if (!descriptor.repeated) {
            *path = descriptor.property_path;
            return true;
        }
        size_t offset = 0U;
        for (const char c : descriptor.property_root)
            (*storage)[offset++] = c;
        (*storage)[offset++] = '[';
        const std::to_chars_result converted
            = std::to_chars(storage->data() + offset,
                            storage->data() + storage->size() - 1U, index);
        if (converted.ec != std::errc {})
            return false;
        offset = static_cast<size_t>(converted.ptr - storage->data());
        (*storage)[offset++] = ']';
        *path                = std::string_view(storage->data(), offset);
        return true;
    }

    static MetadataReconcileResult
    merge_iptc_materialized_xmp(const MetaStore& base,
                                const MetaStore& projected,
                                const FieldPlan* plans, MetaStore* out)
    {
        if (!plans || !out)
            return failure(MetadataReconcileStatus::InternalError,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Xmp,
                           MetadataReconcileStage::ProjectXmp);
        uint32_t expected_operations = 0U;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (plan.project_iptc_to_xmp)
                expected_operations += positional_changes(plan.iptc, plan.xmp);
        }
        MetaEdit edit;
        edit.reserve_ops(expected_operations);
        MetadataReconcileResult result;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.project_iptc_to_xmp)
                continue;
            const size_t source_count = plan.materialized.size();
            const size_t target_count = plan.xmp.values.size();
            const size_t overlap      = std::min(source_count, target_count);
            for (size_t i = 0U; i < overlap; ++i) {
                if (plan.materialized[i].text == plan.xmp.values[i].text)
                    continue;
                const MetaValue value = make_text(edit.arena(),
                                                  plan.materialized[i].text,
                                                  TextEncoding::Utf8);
                edit.set_value(plan.xmp.values[i].entry, value);
                ++result.entries_updated;
            }
            for (size_t i = overlap; i < target_count; ++i) {
                edit.tombstone(plan.xmp.values[i].entry);
                ++result.entries_removed;
            }
            if (source_count <= target_count)
                continue;
            const uint32_t max_index = plan.xmp.max_index;
            if (max_index == UINT32_MAX)
                return failure(MetadataReconcileStatus::EntryLimitExceeded,
                               plan.field, MetadataReconcileFamily::Xmp,
                               MetadataReconcileStage::ProjectXmp,
                               plan.xmp.values.empty()
                                   ? plan.xmp.dirty_delete
                                   : plan.xmp.values.back().entry);
            uint32_t next_index = max_index + 1U;
            for (size_t i = target_count; i < source_count; ++i) {
                std::array<char, 96U> path_storage {};
                std::string_view path;
                if (!make_indexed_path(*plan.descriptor, next_index,
                                       &path_storage, &path))
                    return failure(MetadataReconcileStatus::InternalError,
                                   plan.field, MetadataReconcileFamily::Xmp,
                                   MetadataReconcileStage::ProjectXmp,
                                   plan.materialized[i].entry);
                const Entry& projected_entry = projected.entry(
                    plan.materialized[i].entry);
                Entry entry;
                entry.key   = make_xmp_property_key(edit.arena(),
                                                    plan.descriptor->schema_ns,
                                                    path);
                entry.value = make_text(edit.arena(), plan.materialized[i].text,
                                        TextEncoding::Utf8);
                entry.origin = projected_entry.origin;
                if (entry.origin.wire_type_name.size > 0U) {
                    if (!span_valid(projected.arena(),
                                    entry.origin.wire_type_name))
                        return failure(
                            MetadataReconcileStatus::InvalidSourceValue,
                            plan.field, MetadataReconcileFamily::Xmp,
                            MetadataReconcileStage::ProjectXmp,
                            plan.materialized[i].entry);
                    entry.origin.wire_type_name = edit.arena().append(
                        projected.arena().span(entry.origin.wire_type_name));
                }
                entry.flags = EntryFlags::Dirty;
                edit.add_entry(entry);
                ++result.entries_added;
                if (next_index == UINT32_MAX && i + 1U < source_count)
                    return failure(MetadataReconcileStatus::EntryLimitExceeded,
                                   plan.field, MetadataReconcileFamily::Xmp,
                                   MetadataReconcileStage::ProjectXmp,
                                   plan.materialized[i].entry);
                ++next_index;
            }
        }
        if (edit.ops().size() != expected_operations)
            return failure(MetadataReconcileStatus::InternalError,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Xmp,
                           MetadataReconcileStage::ProjectXmp);
        if (edit.ops().empty()) {
            *out = base;
            return result;
        }
        *out = commit(base, std::span<const MetaEdit>(&edit, 1U));
        if (out->resource_limit_exceeded())
            return failure(MetadataReconcileStatus::SourceLimitExceeded,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Xmp,
                           MetadataReconcileStage::ProjectXmp);
        return result;
    }

    static bool copy_origin_without_wire_name(const MetaStore& source,
                                              EntryId source_id,
                                              Origin* origin) noexcept
    {
        if (!origin || source_id >= source.entries().size())
            return false;
        *origin                = source.entry(source_id).origin;
        origin->wire_type_name = ByteSpan {};
        return true;
    }

    static bool copy_value_to_arena(const MetaStore& source,
                                    const MetaValue& value, ByteArena* arena,
                                    MetaValue* copied)
    {
        if (!arena || !copied)
            return false;
        *copied = value;
        if (value.kind != MetaValueKind::Array
            && value.kind != MetaValueKind::Bytes
            && value.kind != MetaValueKind::Text)
            return true;
        if (!span_valid(source.arena(), value.data.span))
            return false;
        copied->data.span = arena->append(source.arena().span(value.data.span));
        return !arena->limit_exceeded();
    }

    static uint32_t xmp_next_index(const FieldPlan& plan) noexcept
    {
        return plan.xmp.max_index == UINT32_MAX ? UINT32_MAX
                                                : plan.xmp.max_index + 1U;
    }

    static bool append_managed_xmp_entry(const MetaStore& source,
                                         const FieldPlan& plan,
                                         EntryId origin_entry,
                                         std::string_view text, bool deleted,
                                         MetaEdit* edit)
    {
        if (!edit || origin_entry >= source.entries().size())
            return false;
        std::array<char, 96U> path_storage {};
        std::string_view path;
        const uint32_t index = plan.descriptor->repeated ? xmp_next_index(plan)
                                                         : 0U;
        if (plan.descriptor->repeated
            && (index == UINT32_MAX
                || !make_indexed_path(*plan.descriptor, index, &path_storage,
                                      &path)))
            return false;
        if (!plan.descriptor->repeated)
            path = plan.descriptor->property_path;
        Entry entry;
        entry.key   = make_xmp_property_key(edit->arena(),
                                            plan.descriptor->schema_ns, path);
        entry.value = make_text(edit->arena(), text, TextEncoding::Utf8);
        if (!copy_origin_without_wire_name(source, origin_entry, &entry.origin))
            return false;
        entry.flags = deleted ? EntryFlags::Dirty | EntryFlags::Deleted
                              : EntryFlags::Dirty;
        edit->add_entry(entry);
        return true;
    }

    static MetadataReconcileResult
    seed_authoritative_canonical_xmp(const MetaStore& source,
                                     const FieldPlan* plans, MetaStore* out)
    {
        if (!plans || !out)
            return failure(MetadataReconcileStatus::InternalError,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Xmp,
                           MetadataReconcileStage::ProjectXmp);
        MetaEdit edit;
        edit.reserve_ops(15U);
        MetadataReconcileResult result;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.seed_canonical_xmp || !plan.authoritative)
                continue;
            const FamilyState& desired = final_family(plan);
            if (desired.state == ValueState::Active) {
                if (desired.values.size() != 1U)
                    return failure(
                        MetadataReconcileStatus::UnsupportedSourceShape,
                        plan.field, plan.winner,
                        MetadataReconcileStage::ProjectXmp,
                        desired.values.empty() ? kInvalidEntryId
                                               : desired.values[1U].entry);
                const std::string_view text = desired.values[0U].text;
                if (!plan.xmp.values.empty()) {
                    if (plan.xmp.values.size() != 1U)
                        return failure(
                            MetadataReconcileStatus::UnsupportedSourceShape,
                            plan.field, MetadataReconcileFamily::Xmp,
                            MetadataReconcileStage::ProjectXmp,
                            plan.xmp.values[1U].entry);
                    if (plan.xmp.values[0U].text != text) {
                        const MetaValue value = make_text(edit.arena(), text,
                                                          TextEncoding::Utf8);
                        edit.set_value(plan.xmp.values[0U].entry, value);
                        ++result.entries_updated;
                    }
                } else {
                    if (!append_managed_xmp_entry(source, plan,
                                                  desired.values[0U].entry,
                                                  text, false, &edit))
                        return failure(
                            MetadataReconcileStatus::EntryLimitExceeded,
                            plan.field, MetadataReconcileFamily::Xmp,
                            MetadataReconcileStage::ProjectXmp,
                            desired.values[0U].entry);
                    ++result.entries_added;
                }
            } else if (desired.state == ValueState::Deleted) {
                if (!plan.xmp.values.empty()) {
                    for (const ValueRef& value : plan.xmp.values) {
                        edit.tombstone(value.entry);
                        ++result.entries_removed;
                    }
                } else if (plan.xmp.deletions.empty()) {
                    EntryId origin_entry = desired.dirty_delete;
                    if (origin_entry == kInvalidEntryId
                        && !desired.values.empty())
                        origin_entry = desired.values.front().entry;
                    if (origin_entry == kInvalidEntryId
                        || !append_managed_xmp_entry(source, plan, origin_entry,
                                                     {}, true, &edit))
                        return failure(MetadataReconcileStatus::InternalError,
                                       plan.field, MetadataReconcileFamily::Xmp,
                                       MetadataReconcileStage::ProjectXmp,
                                       origin_entry);
                    ++result.entries_added;
                }
            }
        }
        if (edit.ops().empty()) {
            *out = source;
            return result;
        }
        *out = commit(source, std::span<const MetaEdit>(&edit, 1U));
        if (out->resource_limit_exceeded())
            return failure(MetadataReconcileStatus::SourceLimitExceeded,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Xmp,
                           MetadataReconcileStage::ProjectXmp);
        return result;
    }

    static MetadataReconcileResult
    synchronize_tiff_aliases(const MetaStore& source, const FieldPlan* plans,
                             MetaStore* out)
    {
        if (!plans || !out)
            return failure(MetadataReconcileStatus::InternalError,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Xmp,
                           MetadataReconcileStage::ProjectExif);
        MetaEdit edit;
        edit.reserve_ops(15U);
        MetadataReconcileResult result;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            const ExifAliasDescriptor* alias = exif_alias_for_field(plan.field);
            if (!alias || plan.winner == MetadataReconcileFamily::None
                || !plan.authoritative)
                continue;
            const FamilyState& desired = final_family(plan);
            if (desired.state == ValueState::Active) {
                if (desired.values.size() != 1U)
                    return failure(
                        MetadataReconcileStatus::UnsupportedSourceShape,
                        plan.field, plan.winner,
                        MetadataReconcileStage::ProjectExif,
                        desired.values.empty() ? kInvalidEntryId
                                               : desired.values[1U].entry);
                const std::string_view text = desired.values[0U].text;
                if (!plan.alias.values.empty()) {
                    if (plan.alias.values[0U].text != text) {
                        const MetaValue value = make_text(edit.arena(), text,
                                                          TextEncoding::Utf8);
                        edit.set_value(plan.alias.values[0U].entry, value);
                        ++result.entries_updated;
                    }
                } else {
                    Entry entry;
                    entry.key   = make_xmp_property_key(edit.arena(),
                                                        alias->schema_ns,
                                                        alias->path);
                    entry.value = make_text(edit.arena(), text,
                                            TextEncoding::Utf8);
                    if (!copy_origin_without_wire_name(source,
                                                       desired.values[0U].entry,
                                                       &entry.origin))
                        return failure(
                            MetadataReconcileStatus::InvalidSourceValue,
                            plan.field, plan.winner,
                            MetadataReconcileStage::ProjectExif,
                            desired.values[0U].entry);
                    entry.flags = EntryFlags::Dirty;
                    edit.add_entry(entry);
                    ++result.entries_added;
                }
            } else if (desired.state == ValueState::Deleted) {
                if (!plan.alias.values.empty()) {
                    edit.tombstone(plan.alias.values[0U].entry);
                    ++result.entries_removed;
                } else if (plan.alias.deletions.empty()) {
                    EntryId origin_entry = desired.dirty_delete;
                    if (origin_entry == kInvalidEntryId)
                        origin_entry = plan.xmp.dirty_delete;
                    if (origin_entry == kInvalidEntryId)
                        origin_entry = plan.exif.dirty_delete;
                    if (origin_entry == kInvalidEntryId)
                        origin_entry = plan.iptc.dirty_delete;
                    if (origin_entry >= source.entries().size())
                        return failure(MetadataReconcileStatus::InternalError,
                                       plan.field, plan.winner,
                                       MetadataReconcileStage::ProjectExif,
                                       origin_entry);
                    Entry entry;
                    entry.key   = make_xmp_property_key(edit.arena(),
                                                        alias->schema_ns,
                                                        alias->path);
                    entry.value = make_text(edit.arena(), std::string_view {},
                                            TextEncoding::Utf8);
                    if (!copy_origin_without_wire_name(source, origin_entry,
                                                       &entry.origin))
                        return failure(
                            MetadataReconcileStatus::InvalidSourceValue,
                            plan.field, plan.winner,
                            MetadataReconcileStage::ProjectExif, origin_entry);
                    entry.flags = EntryFlags::Dirty | EntryFlags::Deleted;
                    edit.add_entry(entry);
                    ++result.entries_added;
                }
            }
        }
        if (edit.ops().empty()) {
            *out = source;
            return result;
        }
        *out = commit(source, std::span<const MetaEdit>(&edit, 1U));
        if (out->resource_limit_exceeded())
            return failure(MetadataReconcileStatus::SourceLimitExceeded,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Xmp,
                           MetadataReconcileStage::ProjectExif);
        return result;
    }

    static MetadataReconcileField field_for_capture_mapping(
        MetadataCaptureTranslationMapping mapping) noexcept
    {
        switch (mapping) {
        case MetadataCaptureTranslationMapping::XmpTiffImageDescription:
            return MetadataReconcileField::Description;
        case MetadataCaptureTranslationMapping::XmpTiffArtist:
            return MetadataReconcileField::Creator;
        case MetadataCaptureTranslationMapping::XmpTiffCopyright:
            return MetadataReconcileField::Rights;
        default: return MetadataReconcileField::None;
        }
    }

    static MetadataReconcileStatus
    map_profile_status(MetadataCaptureTranslationStatus status) noexcept
    {
        switch (status) {
        case MetadataCaptureTranslationStatus::Ok:
            return MetadataReconcileStatus::Ok;
        case MetadataCaptureTranslationStatus::InvalidOptions:
            return MetadataReconcileStatus::InvalidOptions;
        case MetadataCaptureTranslationStatus::AmbiguousSource:
            return MetadataReconcileStatus::AmbiguousSource;
        case MetadataCaptureTranslationStatus::InvalidSourceValue:
            return MetadataReconcileStatus::InvalidSourceValue;
        case MetadataCaptureTranslationStatus::ValueTooLong:
            return MetadataReconcileStatus::ValueTooLong;
        case MetadataCaptureTranslationStatus::SourceLimitExceeded:
            return MetadataReconcileStatus::SourceLimitExceeded;
        case MetadataCaptureTranslationStatus::NativeConflict:
            return MetadataReconcileStatus::Conflict;
        case MetadataCaptureTranslationStatus::EntryLimitExceeded:
            return MetadataReconcileStatus::EntryLimitExceeded;
        case MetadataCaptureTranslationStatus::OperationLimitExceeded:
            return MetadataReconcileStatus::OperationLimitExceeded;
        case MetadataCaptureTranslationStatus::UnsupportedSourceShape:
            return MetadataReconcileStatus::UnsupportedSourceShape;
        case MetadataCaptureTranslationStatus::NullOutput:
        case MetadataCaptureTranslationStatus::SourceNotFinalized:
        case MetadataCaptureTranslationStatus::InvalidNumericValue:
        case MetadataCaptureTranslationStatus::ValueOutOfRange:
        case MetadataCaptureTranslationStatus::InternalError:
        case MetadataCaptureTranslationStatus::IncompleteSource:
            return MetadataReconcileStatus::ProjectionFailure;
        }
        return MetadataReconcileStatus::ProjectionFailure;
    }

    static bool append_profile_native_source(const MetaStore& source,
                                             EntryId source_id, uint16_t tag,
                                             MetaStore* profile_source)
    {
        if (!profile_source || source_id >= source.entries().size())
            return false;
        const Entry& original = source.entry(source_id);
        Entry entry           = original;
        entry.key = make_exif_tag_key(profile_source->arena(), "ifd0", tag);
        if (any(original.flags, EntryFlags::Deleted)) {
            entry.value = make_text(profile_source->arena(), {},
                                    TextEncoding::Ascii);
        } else if (!copy_value_to_arena(source, original.value,
                                        &profile_source->arena(),
                                        &entry.value)) {
            return false;
        }
        if (!copy_origin_without_wire_name(source, source_id, &entry.origin))
            return false;
        return profile_source->add_entry(entry) != kInvalidEntryId
               && !profile_source->resource_limit_exceeded();
    }

    static EntryId profile_destination_entry(const MetaStore& source,
                                             uint16_t tag) noexcept
    {
        EntryId active  = kInvalidEntryId;
        EntryId deleted = kInvalidEntryId;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind != MetaKeyKind::ExifTag
                || entry.key.data.exif_tag.tag != tag)
                continue;
            std::string_view ifd;
            if (!arena_string(source.arena(), entry.key.data.exif_tag.ifd, &ifd)
                || ifd != "ifd0")
                continue;
            if (any(entry.flags, EntryFlags::Deleted)) {
                if (deleted == kInvalidEntryId)
                    deleted = id;
            } else if (active == kInvalidEntryId) {
                active = id;
            }
        }
        return active != kInvalidEntryId ? active : deleted;
    }

    static bool exif_values_match(const MetaStore& left_store,
                                  const Entry& left,
                                  const MetaStore& right_store,
                                  const Entry& right) noexcept
    {
        std::string_view left_text;
        std::string_view right_text;
        return detail::exif_text_view(left_store.arena(), left.value, true,
                                      &left_text)
               && detail::exif_text_view(right_store.arena(), right.value, true,
                                         &right_text)
               && left_text == right_text
               && left.origin.wire_type.family == right.origin.wire_type.family
               && left.origin.wire_type.code == right.origin.wire_type.code
               && left.origin.wire_count == right.origin.wire_count;
    }

    static MetadataReconcileResult
    apply_profile_native_projection(const MetaStore& source,
                                    const MetaStore& profile_candidate,
                                    const FieldPlan* plans, MetaStore* out)
    {
        if (!plans || !out)
            return failure(MetadataReconcileStatus::InternalError,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Exif,
                           MetadataReconcileStage::ProjectExif);
        MetaEdit edit;
        edit.reserve_ops(9U);
        MetadataReconcileResult result;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            const ExifAliasDescriptor* alias = exif_alias_for_field(plan.field);
            if (!alias || !plan.project_xmp_to_exif)
                continue;
            const FamilyState& desired = final_family(plan);
            const EntryId projected_id
                = profile_destination_entry(profile_candidate, alias->tag);
            if (projected_id == kInvalidEntryId
                && desired.state == ValueState::Deleted
                && plan.exif.values.empty()
                && plan.exif.projection_dirty_delete == kInvalidEntryId
                && plan.exif.clean_delete == kInvalidEntryId) {
                Entry entry;
                entry.key = make_exif_tag_key(edit.arena(), "ifd0", alias->tag);
                entry.value = make_text(edit.arena(), {}, TextEncoding::Ascii);
                if (desired.dirty_delete == kInvalidEntryId
                    || !copy_origin_without_wire_name(source,
                                                      desired.dirty_delete,
                                                      &entry.origin))
                    return failure(MetadataReconcileStatus::InvalidSourceValue,
                                   plan.field, plan.winner,
                                   MetadataReconcileStage::ProjectExif,
                                   desired.dirty_delete);
                entry.origin.wire_type  = { WireFamily::Tiff, 2U };
                entry.origin.wire_count = 1U;
                entry.flags = EntryFlags::Dirty | EntryFlags::Deleted;
                edit.add_entry(entry);
                ++result.entries_added;
                continue;
            }
            if (projected_id == kInvalidEntryId
                || projected_id >= profile_candidate.entries().size())
                return failure(MetadataReconcileStatus::InternalError,
                               plan.field, MetadataReconcileFamily::Exif,
                               MetadataReconcileStage::ProjectExif);
            const Entry& projected = profile_candidate.entry(projected_id);
            const bool projected_deleted = any(projected.flags,
                                               EntryFlags::Deleted);
            const EntryId active_native  = plan.exif.values.empty()
                                               ? kInvalidEntryId
                                               : plan.exif.values.front().entry;

            if (desired.state == ValueState::Active) {
                if (projected_deleted || desired.values.size() != 1U)
                    return failure(MetadataReconcileStatus::InternalError,
                                   plan.field, MetadataReconcileFamily::Exif,
                                   MetadataReconcileStage::ProjectExif,
                                   projected_id);
                if (active_native != kInvalidEntryId) {
                    const Entry& original = source.entry(active_native);
                    const bool same       = exif_values_match(source, original,
                                                              profile_candidate,
                                                              projected);
                    if (same && any(original.flags, EntryFlags::Dirty))
                        continue;
                    MetaValue value;
                    if (!copy_value_to_arena(profile_candidate, projected.value,
                                             &edit.arena(), &value))
                        return failure(
                            MetadataReconcileStatus::SourceLimitExceeded,
                            plan.field, MetadataReconcileFamily::Exif,
                            MetadataReconcileStage::ProjectExif, active_native);
                    if (same) {
                        edit.set_value(active_native, value);
                    } else {
                        edit.set_value(active_native, value,
                                       projected.origin.wire_type,
                                       projected.origin.wire_count);
                    }
                    ++result.entries_updated;
                    continue;
                }
                Entry entry;
                entry.key = make_exif_tag_key(edit.arena(), "ifd0", alias->tag);
                if (!copy_value_to_arena(profile_candidate, projected.value,
                                         &edit.arena(), &entry.value)
                    || !copy_origin_without_wire_name(source,
                                                      desired.values[0U].entry,
                                                      &entry.origin))
                    return failure(MetadataReconcileStatus::InvalidSourceValue,
                                   plan.field, MetadataReconcileFamily::Exif,
                                   MetadataReconcileStage::ProjectExif,
                                   desired.values[0U].entry);
                entry.origin.wire_type  = projected.origin.wire_type;
                entry.origin.wire_count = projected.origin.wire_count;
                entry.flags             = EntryFlags::Dirty;
                edit.add_entry(entry);
                ++result.entries_added;
                continue;
            }

            if (desired.state != ValueState::Deleted || !projected_deleted)
                return failure(MetadataReconcileStatus::InternalError,
                               plan.field, MetadataReconcileFamily::Exif,
                               MetadataReconcileStage::ProjectExif,
                               projected_id);
            if (active_native != kInvalidEntryId) {
                edit.tombstone(active_native);
                ++result.entries_removed;
                continue;
            }
            if (plan.exif.projection_dirty_delete != kInvalidEntryId)
                continue;
            if (plan.exif.clean_delete != kInvalidEntryId) {
                edit.tombstone(plan.exif.clean_delete);
                ++result.entries_updated;
                continue;
            }
            Entry entry;
            entry.key = make_exif_tag_key(edit.arena(), "ifd0", alias->tag);
            // A new removal marker has no source payload to preserve.
            entry.value = make_text(edit.arena(), {}, TextEncoding::Ascii);
            if (desired.dirty_delete == kInvalidEntryId
                || !copy_origin_without_wire_name(source, desired.dirty_delete,
                                                  &entry.origin))
                return failure(MetadataReconcileStatus::InvalidSourceValue,
                               plan.field, MetadataReconcileFamily::Exif,
                               MetadataReconcileStage::ProjectExif,
                               desired.dirty_delete);
            entry.origin.wire_type  = projected.origin.wire_type;
            entry.origin.wire_count = projected.origin.wire_count;
            entry.flags             = EntryFlags::Dirty | EntryFlags::Deleted;
            edit.add_entry(entry);
            ++result.entries_added;
        }
        if (edit.ops().empty()) {
            *out = source;
            return result;
        }
        *out = commit(source, std::span<const MetaEdit>(&edit, 1U));
        if (out->resource_limit_exceeded())
            return failure(MetadataReconcileStatus::SourceLimitExceeded,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Exif,
                           MetadataReconcileStage::ProjectExif);
        return result;
    }

    static MetadataReconcileResult
    translate_batch_xmp_to_exif(const MetaStore& source, const FieldPlan* plans,
                                const MetadataReconcileOptions& options,
                                MetaStore* candidate,
                                uint64_t* staged_operations)
    {
        if (!plans || !candidate || !staged_operations)
            return failure(MetadataReconcileStatus::InternalError,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Exif,
                           MetadataReconcileStage::ProjectExif);
        *staged_operations = 0U;
        MetadataProfileTranslationOptions translation;
        translation.source_mode = MetadataCaptureTranslationSourceMode::All;
        translation.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        translation.image_description_to_exif  = false;
        translation.artist_to_exif             = false;
        translation.copyright_to_exif          = false;
        translation.color_space_to_exif        = false;
        translation.related_sound_file_to_exif = false;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.project_xmp_to_exif)
                continue;
            if (plan.field == MetadataReconcileField::Description)
                translation.image_description_to_exif = true;
            else if (plan.field == MetadataReconcileField::Creator)
                translation.artist_to_exif = true;
            else if (plan.field == MetadataReconcileField::Rights)
                translation.copyright_to_exif = true;
        }
        if (!translation.image_description_to_exif
            && !translation.artist_to_exif && !translation.copyright_to_exif) {
            *candidate = source;
            return MetadataReconcileResult {};
        }
        translation.max_added_entries
            = kMetadataProfileTranslationMaxAddedEntries;
        translation.max_operations
            = std::min(options.max_operations,
                       kMetadataCaptureTranslationMaxOperations);
        translation.max_text_bytes_per_property
            = kMetadataProfileTranslationMaxTextBytesPerProperty;
        translation.max_total_text_bytes
            = kMetadataProfileTranslationMaxTotalTextBytes;
        MetaStore profile_source;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            const ExifAliasDescriptor* alias = exif_alias_for_field(plan.field);
            if (!alias || !plan.project_xmp_to_exif)
                continue;
            const FamilyState& desired = final_family(plan);
            EntryId origin_entry       = desired.dirty_delete;
            std::string_view text;
            const bool deleted = desired.state == ValueState::Deleted;
            if (desired.state == ValueState::Active) {
                if (desired.values.size() != 1U)
                    return failure(
                        MetadataReconcileStatus::UnsupportedSourceShape,
                        plan.field, plan.winner,
                        MetadataReconcileStage::ProjectExif);
                origin_entry = desired.values[0U].entry;
                text         = desired.values[0U].text;
            } else if (deleted && origin_entry == kInvalidEntryId) {
                origin_entry = plan.alias.dirty_delete;
                if (origin_entry == kInvalidEntryId)
                    origin_entry = plan.exif.dirty_delete;
                if (origin_entry == kInvalidEntryId)
                    origin_entry = plan.iptc.dirty_delete;
                if (origin_entry == kInvalidEntryId)
                    origin_entry = plan.xmp.dirty_delete;
            }
            if (origin_entry == kInvalidEntryId)
                return failure(MetadataReconcileStatus::InternalError,
                               plan.field, plan.winner,
                               MetadataReconcileStage::ProjectExif);
            Entry alias_entry;
            alias_entry.key   = make_xmp_property_key(profile_source.arena(),
                                                      alias->schema_ns,
                                                      alias->path);
            alias_entry.value = make_text(profile_source.arena(), text,
                                          TextEncoding::Utf8);
            if (!copy_origin_without_wire_name(source, origin_entry,
                                               &alias_entry.origin))
                return failure(MetadataReconcileStatus::InvalidSourceValue,
                               plan.field, plan.winner,
                               MetadataReconcileStage::ProjectExif,
                               origin_entry);
            alias_entry.flags = deleted
                                    ? EntryFlags::Dirty | EntryFlags::Deleted
                                    : EntryFlags::Dirty;
            if (profile_source.add_entry(alias_entry) == kInvalidEntryId)
                return failure(MetadataReconcileStatus::SourceLimitExceeded,
                               plan.field, plan.winner,
                               MetadataReconcileStage::ProjectExif,
                               origin_entry);

            EntryId native_source = kInvalidEntryId;
            if (!plan.exif.values.empty())
                native_source = plan.exif.values.front().entry;
            else if (plan.exif.projection_dirty_delete != kInvalidEntryId)
                native_source = plan.exif.projection_dirty_delete;
            else if (plan.exif.clean_delete != kInvalidEntryId)
                native_source = plan.exif.clean_delete;
            if (native_source != kInvalidEntryId
                && !append_profile_native_source(source, native_source,
                                                 alias->tag, &profile_source))
                return failure(MetadataReconcileStatus::InvalidSourceValue,
                               plan.field, MetadataReconcileFamily::Exif,
                               MetadataReconcileStage::ProjectExif,
                               native_source);
        }
        if (profile_source.resource_limit_exceeded())
            return failure(MetadataReconcileStatus::SourceLimitExceeded,
                           MetadataReconcileField::None,
                           MetadataReconcileFamily::Exif,
                           MetadataReconcileStage::ProjectExif);
        profile_source.finalize();
        MetaStore profile_candidate;
        const MetadataCaptureTranslationResult translated
            = translate_xmp_profile_metadata(profile_source, translation,
                                             &profile_candidate);
        const MetadataReconcileStatus status = map_profile_status(
            translated.status);
        if (status != MetadataReconcileStatus::Ok) {
            const MetadataReconcileField field = field_for_capture_mapping(
                translated.failed_mapping);
            EntryId original_entry = kInvalidEntryId;
            for (const FieldPlan& plan :
                 std::span<const FieldPlan, 5U>(plans, 5U)) {
                if (plan.field != field || !plan.authoritative)
                    continue;
                original_entry = plan.authoritative->values.empty()
                                     ? plan.authoritative->dirty_delete
                                     : plan.authoritative->values.front().entry;
                break;
            }
            return failure(status, field, MetadataReconcileFamily::Exif,
                           MetadataReconcileStage::ProjectExif, original_entry);
        }
        MetadataReconcileResult result
            = apply_profile_native_projection(source, profile_candidate, plans,
                                              candidate);
        if (result.status != MetadataReconcileStatus::Ok)
            return result;
        *staged_operations
            = static_cast<uint64_t>(profile_source.entries().size())
              + translated.entries_added + translated.entries_updated
              + translated.entries_removed + result.entries_added
              + result.entries_updated + result.entries_removed;
        result.translator_passes = 1U;
        return result;
    }

    static MetadataReconcileStatus validate_projected_iptc_charsets(
        const MetaStore& store, const FieldPlan* plans,
        const MetadataReconcileOptions& options, Budget* budget,
        MetadataReconcileResult* error)
    {
        if (!plans || !budget || !error)
            return MetadataReconcileStatus::InternalError;
        if (!options.participate_iptc)
            return MetadataReconcileStatus::Ok;

        std::vector<BlockId> relevant_blocks;
        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.project_xmp_to_iptc)
                continue;
            for (EntryId id = 0U; id < store.entries().size(); ++id) {
                const Entry& entry = store.entry(id);
                if (entry.key.kind != MetaKeyKind::IptcDataset
                    || entry.key.data.iptc_dataset.record != 2U
                    || entry.key.data.iptc_dataset.dataset
                           != plan.descriptor->iptc_dataset
                    || any(entry.flags, EntryFlags::Deleted))
                    continue;
                relevant_blocks.push_back(entry.origin.block);
            }
        }
        if (relevant_blocks.empty())
            return MetadataReconcileStatus::Ok;
        std::sort(relevant_blocks.begin(), relevant_blocks.end(),
                  BlockOrder {});
        relevant_blocks.erase(std::unique(relevant_blocks.begin(),
                                          relevant_blocks.end()),
                              relevant_blocks.end());
        std::vector<CharsetBlock> blocks;
        blocks.reserve(relevant_blocks.size());
        for (const BlockId block : relevant_blocks) {
            if (block == kInvalidBlockId)
                continue;
            if (block >= store.block_count()) {
                *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                 MetadataReconcileField::None,
                                 MetadataReconcileFamily::Iptc,
                                 MetadataReconcileStage::ProjectIptc);
                return error->status;
            }
            blocks.push_back(CharsetBlock { block });
        }

        for (const FieldPlan& plan :
             std::span<const FieldPlan, 5U>(plans, 5U)) {
            if (!plan.project_xmp_to_iptc)
                continue;
            for (EntryId id = 0U; id < store.entries().size(); ++id) {
                const Entry& entry = store.entry(id);
                if (entry.key.kind != MetaKeyKind::IptcDataset
                    || entry.key.data.iptc_dataset.record != 2U
                    || entry.key.data.iptc_dataset.dataset
                           != plan.descriptor->iptc_dataset
                    || any(entry.flags, EntryFlags::Deleted))
                    continue;
                uint32_t value_bytes = 0U;
                if (!flags_valid(entry)
                    || !pending_value_shape(entry, store.arena(), true,
                                            &value_bytes)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Iptc,
                                     MetadataReconcileStage::ProjectIptc, id);
                    return error->status;
                }
                std::string_view text;
                bool non_ascii = false;
                if (!read_text_value(store, entry, true, &text, &non_ascii)) {
                    *error = failure(MetadataReconcileStatus::InvalidSourceValue,
                                     plan.field, MetadataReconcileFamily::Iptc,
                                     MetadataReconcileStage::ProjectIptc, id);
                    return error->status;
                }
                if (text.size() > plan.descriptor->max_bytes) {
                    *error = failure(MetadataReconcileStatus::ValueTooLong,
                                     plan.field, MetadataReconcileFamily::Iptc,
                                     MetadataReconcileStage::ProjectIptc, id);
                    return error->status;
                }
                if (non_ascii
                    && (entry.origin.block == kInvalidBlockId
                        || entry.origin.block >= store.block_count())) {
                    *error = failure(
                        MetadataReconcileStatus::SourceEncodingConflict,
                        plan.field, MetadataReconcileFamily::Iptc,
                        MetadataReconcileStage::ProjectIptc, id);
                    return error->status;
                }
                if (non_ascii
                    && charset_info_index(blocks, entry.origin.block)
                           == blocks.size()) {
                    *error = failure(
                        MetadataReconcileStatus::SourceEncodingConflict,
                        plan.field, MetadataReconcileFamily::Iptc,
                        MetadataReconcileStage::ProjectIptc, id);
                    return error->status;
                }
                if (non_ascii) {
                    const size_t block_index
                        = charset_info_index(blocks, entry.origin.block);
                    if (block_index < blocks.size())
                        blocks[block_index].marker_required = true;
                }
            }
        }

        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& entry = store.entry(id);
            if (entry.key.kind != MetaKeyKind::IptcDataset
                || entry.key.data.iptc_dataset.record != 1U
                || entry.key.data.iptc_dataset.dataset != 90U
                || entry.origin.block == kInvalidBlockId
                || any(entry.flags, EntryFlags::Deleted))
                continue;
            const size_t index = charset_info_index(blocks, entry.origin.block);
            if (index == blocks.size())
                continue;
            CharsetBlock& info = blocks[index];
            ++info.marker_count;
            if (info.marker == kInvalidEntryId)
                info.marker = id;
            uint32_t value_bytes = 0U;
            if (!flags_valid(entry)
                || !pending_value_shape(entry, store.arena(), true, &value_bytes)
                || value_bytes != kUtf8Escape.size()) {
                info.marker_shape_valid = false;
                continue;
            }
            const std::span<const std::byte> bytes = store.arena().span(
                entry.value.data.span);
            info.marker_value_valid = std::equal(bytes.begin(), bytes.end(),
                                                 kUtf8Escape.begin());
        }
        for (const CharsetBlock& info : blocks) {
            if (info.marker_count > 1U || !info.marker_shape_valid
                || (info.marker_count == 1U && !info.marker_value_valid)) {
                *error
                    = failure(MetadataReconcileStatus::SourceEncodingConflict,
                              MetadataReconcileField::None,
                              MetadataReconcileFamily::Iptc,
                              MetadataReconcileStage::ProjectIptc, info.marker);
                return error->status;
            }
            if (info.marker_required && info.marker_count != 1U) {
                MetadataReconcileField field = MetadataReconcileField::None;
                EntryId source_entry         = kInvalidEntryId;
                for (const FieldPlan& plan :
                     std::span<const FieldPlan, 5U>(plans, 5U)) {
                    if (!plan.project_xmp_to_iptc)
                        continue;
                    for (const ValueRef& value : projection_xmp(plan).values) {
                        if (value.block == info.block
                            && value_has_non_ascii(value)) {
                            field        = plan.field;
                            source_entry = value.entry;
                            break;
                        }
                    }
                    if (source_entry != kInvalidEntryId)
                        break;
                }
                *error = failure(MetadataReconcileStatus::SourceEncodingConflict,
                                 field, MetadataReconcileFamily::Xmp,
                                 MetadataReconcileStage::ProjectIptc,
                                 source_entry);
                return error->status;
            }
        }
        return MetadataReconcileStatus::Ok;
    }

    static MetadataReconcileField field_for_entry(const MetaStore& store,
                                                  EntryId id) noexcept
    {
        if (id == kInvalidEntryId || id >= store.entries().size())
            return MetadataReconcileField::None;
        const Entry& entry = store.entry(id);
        if (entry.key.kind == MetaKeyKind::IptcDataset
            && entry.key.data.iptc_dataset.record == 2U) {
            for (const Descriptor& descriptor :
                 detail::kIptcXmpPrimaryMappings) {
                if (descriptor.iptc_dataset
                    == entry.key.data.iptc_dataset.dataset)
                    return field_for_mapping(descriptor.mapping);
            }
        }
        if (entry.key.kind == MetaKeyKind::XmpProperty) {
            std::string_view schema;
            std::string_view path;
            if (!arena_string(store.arena(),
                              entry.key.data.xmp_property.schema_ns, &schema)
                || !arena_string(store.arena(),
                                 entry.key.data.xmp_property.property_path,
                                 &path))
                return MetadataReconcileField::None;
            for (const Descriptor& descriptor :
                 detail::kIptcXmpPrimaryMappings) {
                if (schema != descriptor.schema_ns)
                    continue;
                uint32_t index = 0U;
                if (xmp_path_kind(path, descriptor, &index)
                    != XmpPathKind::Other)
                    return field_for_mapping(descriptor.mapping);
            }
        }
        return MetadataReconcileField::None;
    }

    static MetadataReconcileFamily family_for_entry(const MetaStore& store,
                                                    EntryId id) noexcept
    {
        if (id == kInvalidEntryId || id >= store.entries().size())
            return MetadataReconcileFamily::None;
        const MetaKeyKind kind = store.entry(id).key.kind;
        if (kind == MetaKeyKind::XmpProperty)
            return MetadataReconcileFamily::Xmp;
        if (kind == MetaKeyKind::IptcDataset)
            return MetadataReconcileFamily::Iptc;
        if (kind == MetaKeyKind::ExifTag)
            return MetadataReconcileFamily::Exif;
        return MetadataReconcileFamily::None;
    }

    static MetadataReconcileStatus
    map_validation_status(const MetadataValidationResult& validation) noexcept
    {
        if (validation.status == MetadataValidationStatus::LimitExceeded)
            return MetadataReconcileStatus::SourceLimitExceeded;
        if (validation.status == MetadataValidationStatus::InvalidArgument)
            return MetadataReconcileStatus::InvalidOptions;
        MetadataValidationIssueCode code = MetadataValidationIssueCode::None;
        for (const MetadataValidationIssue& issue : validation.issues) {
            if (issue.severity == ValidateIssueSeverity::Error) {
                code = issue.code;
                break;
            }
        }
        switch (code) {
        case MetadataValidationIssueCode::StoreLimitExceeded:
        case MetadataValidationIssueCode::ValueLimitExceeded:
        case MetadataValidationIssueCode::IssueLimitExceeded:
        case MetadataValidationIssueCode::XmpStructureLimitExceeded:
            return MetadataReconcileStatus::SourceLimitExceeded;
        case MetadataValidationIssueCode::DuplicateSingleton:
            return MetadataReconcileStatus::AmbiguousSource;
        case MetadataValidationIssueCode::InvalidXmpNamespace:
        case MetadataValidationIssueCode::InvalidXmpPropertyPath:
        case MetadataValidationIssueCode::InvalidXmpStructure:
            return MetadataReconcileStatus::UnsupportedSourceShape;
        case MetadataValidationIssueCode::None:
            return validation.status == MetadataValidationStatus::Ok
                       ? MetadataReconcileStatus::Ok
                       : MetadataReconcileStatus::InvalidSourceValue;
        default: return MetadataReconcileStatus::InvalidSourceValue;
        }
    }

    static MetadataReconcileResult
    validation_failure(const MetaStore& store,
                       const MetadataValidationResult& validation,
                       const MetadataValidationOptions& options)
    {
        EntryId failed_entry = kInvalidEntryId;
        MetadataValidationIssueCode issue_code
            = MetadataValidationIssueCode::None;
        for (const MetadataValidationIssue& issue : validation.issues) {
            if (issue.severity != ValidateIssueSeverity::Error)
                continue;
            failed_entry = issue.entry;
            issue_code   = issue.code;
            break;
        }
        MetadataReconcileStatus status = map_validation_status(validation);
        if (failed_entry != kInvalidEntryId
            && failed_entry < store.entries().size()) {
            const Entry& entry = store.entry(failed_entry);
            if (entry.key.kind == MetaKeyKind::XmpProperty
                && (entry.key.data.xmp_property.schema_ns.size
                        > options.max_key_bytes
                    || entry.key.data.xmp_property.property_path.size
                           > options.max_key_bytes))
                status = MetadataReconcileStatus::SourceLimitExceeded;
            else if (entry.key.kind == MetaKeyKind::ExifTag
                     && entry.key.data.exif_tag.ifd.size
                            > options.max_key_bytes)
                status = MetadataReconcileStatus::SourceLimitExceeded;
            else if (entry.key.kind == MetaKeyKind::ExrAttribute
                     && entry.key.data.exr_attribute.name.size
                            > options.max_key_bytes)
                status = MetadataReconcileStatus::SourceLimitExceeded;
        }
        MetadataReconcileResult result
            = failure(status, field_for_entry(store, failed_entry),
                      family_for_entry(store, failed_entry),
                      MetadataReconcileStage::Publish, failed_entry);
        result.validation_issue = issue_code;
        return result;
    }

    static MetadataReconcileResult
    apply_projection_counts(const MetadataReconcileResult& xmp_to_iptc,
                            const MetadataReconcileResult& iptc_to_xmp,
                            uint32_t groups, uint32_t source_properties)
    {
        MetadataReconcileResult result;
        result.groups_reconciled = groups;
        result.source_properties = source_properties;
        result.translator_passes = xmp_to_iptc.translator_passes
                                   + iptc_to_xmp.translator_passes;
        result.entries_added = xmp_to_iptc.entries_added
                               + iptc_to_xmp.entries_added;
        result.entries_updated = xmp_to_iptc.entries_updated
                                 + iptc_to_xmp.entries_updated;
        result.entries_removed = xmp_to_iptc.entries_removed
                                 + iptc_to_xmp.entries_removed;
        return result;
    }

    static uint64_t
    mutation_operations(const MetadataReconcileResult& result) noexcept
    {
        return static_cast<uint64_t>(result.entries_added)
               + result.entries_updated + result.entries_removed;
    }

}  // namespace

MetadataReconcileResult
reconcile_descriptive_metadata(const MetaStore& source,
                               const MetadataReconcileOptions& options,
                               MetaStore* out_store) noexcept
{
    if (!out_store)
        return failure(MetadataReconcileStatus::NullOutput);
    if (!source.is_finalized())
        return failure(MetadataReconcileStatus::SourceNotFinalized);
    if (!options_valid(options))
        return failure(MetadataReconcileStatus::InvalidOptions);
    const uint64_t input_entry_limit = std::min<uint64_t>(
        { options.max_source_entries, options.validation.max_entries,
          kMetadataReconcileMaxSourceEntries });
    const uint64_t input_arena_limit = std::min(
        { options.max_source_arena_bytes, options.validation.max_arena_bytes,
          kMetadataReconcileMaxSourceArenaBytes });
    if (source.entries().size() > input_entry_limit
        || source.block_count() > input_entry_limit
        || source.arena().bytes().size() > input_arena_limit)
        return failure(MetadataReconcileStatus::SourceLimitExceeded,
                       MetadataReconcileField::None,
                       MetadataReconcileFamily::None,
                       MetadataReconcileStage::Preflight);
    if (source.resource_limit_exceeded())
        return failure(MetadataReconcileStatus::SourceLimitExceeded,
                       MetadataReconcileField::None,
                       MetadataReconcileFamily::None,
                       MetadataReconcileStage::Preflight);

    std::array<FieldPlan, 5U> plans {};
    for (size_t i = 0U; i < plans.size(); ++i) {
        plans[i].field      = kFields[i];
        plans[i].descriptor = descriptor_for(kFields[i]);
        plans[i].enabled    = field_enabled(kFields[i], options);
        if (!plans[i].descriptor)
            return failure(MetadataReconcileStatus::InternalError, kFields[i],
                           MetadataReconcileFamily::None,
                           MetadataReconcileStage::Preflight);
    }

    Budget budget;
    budget.charged_input_entries.reserve(
        std::min<size_t>(options.max_source_properties,
                         source.entries().size()));
    MetadataReconcileResult error;
    MetadataReconcileStatus status
        = collect_xmp_candidates(source, plans.data(), options, &budget,
                                 &error);
    if (status != MetadataReconcileStatus::Ok)
        return error;
    status = collect_iptc_candidates(source, plans.data(), options, &budget,
                                     &error);
    if (status != MetadataReconcileStatus::Ok)
        return error;
    status = collect_exif_alias_candidates(source, plans.data(), options,
                                           &budget, &error);
    if (status != MetadataReconcileStatus::Ok)
        return error;
    status = collect_exif_candidates(source, plans.data(), options, &budget,
                                     &error);
    if (status != MetadataReconcileStatus::Ok)
        return error;
    std::vector<CharsetBlock> charset_blocks;
    status = collect_charset_candidates(source, plans.data(), options, &budget,
                                        &charset_blocks, &error);
    if (status != MetadataReconcileStatus::Ok)
        return error;
    if (budget.input_text_bytes > options.max_total_text_bytes
        || budget.input_text_bytes > kMetadataReconcileMaxTotalTextBytes
        || budget.input_key_value_bytes > options.max_total_participating_bytes
        || budget.input_key_value_bytes
               > kMetadataReconcileMaxTotalParticipatingBytes)
        return failure(MetadataReconcileStatus::SourceLimitExceeded,
                       MetadataReconcileField::None,
                       MetadataReconcileFamily::None,
                       MetadataReconcileStage::Preflight);
    status = validate_charset_blocks(source, charset_blocks, &error);
    if (status != MetadataReconcileStatus::Ok) {
        error.failed_field = MetadataReconcileField::None;
        return error;
    }
    status = validate_pending_values(source, plans.data(), options,
                                     charset_blocks, &error);
    if (status != MetadataReconcileStatus::Ok)
        return error;

    uint32_t groups = 0U;
    const MetadataReconcileResult authority
        = resolve_authority(plans.data(), options, &groups);
    if (authority.status != MetadataReconcileStatus::Ok)
        return authority;
    status = preflight_costs(source, plans.data(), options, &budget,
                             charset_blocks, &error);
    if (status != MetadataReconcileStatus::Ok)
        return error;

    MetaStore stage = source;
    MetadataReconcileResult canonical_seed;
    MetadataReconcileResult xmp_to_iptc;
    MetadataReconcileResult iptc_to_xmp;
    MetadataReconcileResult aliases_synced;
    MetadataReconcileResult xmp_to_exif;
    uint64_t actual_operations      = 0U;
    uint64_t exif_staged_operations = 0U;
    bool do_seed_canonical          = false;
    for (const FieldPlan& plan : plans)
        do_seed_canonical = do_seed_canonical || plan.seed_canonical_xmp;
    if (do_seed_canonical) {
        MetaStore candidate;
        canonical_seed = seed_authoritative_canonical_xmp(stage, plans.data(),
                                                          &candidate);
        if (canonical_seed.status != MetadataReconcileStatus::Ok)
            return canonical_seed;
        actual_operations += mutation_operations(canonical_seed);
        stage = std::move(candidate);
    }
    if (budget.operations > 0U) {
        bool do_xmp_to_iptc = false;
        bool do_iptc_to_xmp = false;
        for (const FieldPlan& plan : plans) {
            do_xmp_to_iptc = do_xmp_to_iptc || plan.project_xmp_to_iptc;
            do_iptc_to_xmp = do_iptc_to_xmp || plan.project_iptc_to_xmp;
        }
        if (do_xmp_to_iptc) {
            MetaStore candidate;
            xmp_to_iptc = translate_batch_xmp_to_iptc(stage, plans.data(),
                                                      options, &candidate);
            if (xmp_to_iptc.status != MetadataReconcileStatus::Ok)
                return xmp_to_iptc;
            actual_operations += mutation_operations(xmp_to_iptc);
            stage = std::move(candidate);
        }
        if (do_iptc_to_xmp) {
            MetaStore projected_owner;
            uint32_t scratch_ops = 0U;
            iptc_to_xmp = translate_batch_iptc_to_xmp(stage, plans.data(),
                                                      options, &projected_owner,
                                                      &scratch_ops);
            if (iptc_to_xmp.status != MetadataReconcileStatus::Ok)
                return iptc_to_xmp;
            actual_operations += scratch_ops;
            actual_operations += mutation_operations(iptc_to_xmp);
            status = collect_materialized_values(projected_owner, plans.data(),
                                                 &error);
            if (status != MetadataReconcileStatus::Ok)
                return error;
            MetaStore merged;
            const MetadataReconcileResult merge
                = merge_iptc_materialized_xmp(stage, projected_owner,
                                              plans.data(), &merged);
            if (merge.status != MetadataReconcileStatus::Ok)
                return merge;
            actual_operations += mutation_operations(merge);
            iptc_to_xmp.entries_added   = merge.entries_added;
            iptc_to_xmp.entries_updated = merge.entries_updated;
            iptc_to_xmp.entries_removed = merge.entries_removed;
            stage                       = std::move(merged);
        }
    }
    if (options.enable_exif_bridge) {
        MetaStore candidate;
        aliases_synced = synchronize_tiff_aliases(stage, plans.data(),
                                                  &candidate);
        if (aliases_synced.status != MetadataReconcileStatus::Ok)
            return aliases_synced;
        actual_operations += mutation_operations(aliases_synced);
        stage = std::move(candidate);

        MetaStore exif_candidate;
        xmp_to_exif = translate_batch_xmp_to_exif(stage, plans.data(), options,
                                                  &exif_candidate,
                                                  &exif_staged_operations);
        if (xmp_to_exif.status != MetadataReconcileStatus::Ok)
            return xmp_to_exif;
        actual_operations += exif_staged_operations;
        stage = std::move(exif_candidate);
    }
    if (actual_operations > budget.operations
        || actual_operations > options.max_operations
        || actual_operations > kMetadataReconcileMaxOperations)
        return failure(MetadataReconcileStatus::OperationLimitExceeded,
                       MetadataReconcileField::None,
                       MetadataReconcileFamily::None,
                       MetadataReconcileStage::Publish);
    status = validate_projected_iptc_charsets(stage, plans.data(), options,
                                              &budget, &error);
    if (status != MetadataReconcileStatus::Ok)
        return error;
    if (stage.resource_limit_exceeded()
        || stage.entries().size() > options.max_source_entries
        || stage.entries().size() > options.validation.max_entries
        || stage.arena().bytes().size() > options.max_source_arena_bytes
        || stage.arena().bytes().size() > options.validation.max_arena_bytes)
        return failure(MetadataReconcileStatus::SourceLimitExceeded,
                       MetadataReconcileField::None,
                       MetadataReconcileFamily::None,
                       MetadataReconcileStage::Publish);

    MetadataValidationOptions validation_options = options.validation;
    validation_options.require_finalized         = true;
    const MetadataValidationResult validation
        = validate_store(stage, validation_options);
    if (!validation.ok())
        return validation_failure(stage, validation, validation_options);

    *out_store = std::move(stage);
    MetadataReconcileResult result
        = apply_projection_counts(xmp_to_iptc, iptc_to_xmp, groups,
                                  budget.source_properties);
    result.translator_passes += xmp_to_exif.translator_passes;
    result.entries_added += canonical_seed.entries_added
                            + aliases_synced.entries_added
                            + xmp_to_exif.entries_added;
    result.entries_updated += canonical_seed.entries_updated
                              + aliases_synced.entries_updated
                              + xmp_to_exif.entries_updated;
    result.entries_removed += canonical_seed.entries_removed
                              + aliases_synced.entries_removed
                              + xmp_to_exif.entries_removed;
    return result;
}

const char*
metadata_reconcile_status_name(MetadataReconcileStatus status) noexcept
{
    switch (status) {
    case MetadataReconcileStatus::Ok: return "ok";
    case MetadataReconcileStatus::NullOutput: return "null_output";
    case MetadataReconcileStatus::SourceNotFinalized:
        return "source_not_finalized";
    case MetadataReconcileStatus::InvalidOptions: return "invalid_options";
    case MetadataReconcileStatus::SourceLimitExceeded:
        return "source_limit_exceeded";
    case MetadataReconcileStatus::InvalidSourceValue:
        return "invalid_source_value";
    case MetadataReconcileStatus::ValueTooLong: return "value_too_long";
    case MetadataReconcileStatus::UnsupportedSourceShape:
        return "unsupported_source_shape";
    case MetadataReconcileStatus::AmbiguousSource: return "ambiguous_source";
    case MetadataReconcileStatus::Conflict: return "conflict";
    case MetadataReconcileStatus::SourceEncodingConflict:
        return "source_encoding_conflict";
    case MetadataReconcileStatus::EntryLimitExceeded:
        return "entry_limit_exceeded";
    case MetadataReconcileStatus::OperationLimitExceeded:
        return "operation_limit_exceeded";
    case MetadataReconcileStatus::ProjectionFailure:
        return "projection_failure";
    case MetadataReconcileStatus::InternalError: return "internal_error";
    }
    return "unknown";
}

}  // namespace openmeta
