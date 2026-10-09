// SPDX-License-Identifier: Apache-2.0

#include "metadata_development_fields_internal.h"
#include "metadata_text_fields_internal.h"

#include "openmeta/meta_edit.h"
#include "openmeta/metadata_translation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    using Status  = MetadataCaptureTranslationStatus;
    using Mapping = MetadataCaptureTranslationMapping;
    using Mode    = MetadataCaptureTranslationSourceMode;
    using Policy  = MetadataCaptureTranslationConflictPolicy;

    constexpr std::string_view kXmpNsExifEx = "http://cipa.jp/exif/1.0/";

    struct SourceValue final {
        bool found       = false;
        bool deleted     = false;
        EntryId entry_id = kInvalidEntryId;
        const MetaValue* value = nullptr;
    };

    struct FieldPlan final {
        uint16_t tag = 0U;
        std::string_view path;
        Mapping mapping = Mapping::None;
        bool enabled = false;
        SourceValue source;
        SourceValue characteristic;
        SourceValue factory_default;
        std::vector<EntryId> native;
        EntryId clean_deleted = kInvalidEntryId;
        bool dirty_deleted = false;
        bool present = false;
        uint16_t scalar = 0U;
        std::string_view text;
        EntryId source_entry = kInvalidEntryId;
        bool apply = false;
        bool promote = false;
    };

    static MetadataCaptureTranslationResult error(Status status,
                                                  Mapping mapping = Mapping::None,
                                                  EntryId source = kInvalidEntryId)
    {
        MetadataCaptureTranslationResult result;
        result.status              = status;
        result.failed_mapping     = mapping;
        result.failed_source_entry = source;
        return result;
    }

    static std::string_view arena_text(const ByteArena& arena,
                                       ByteSpan span) noexcept
    {
        const std::span<const std::byte> raw = arena.span(span);
        return { reinterpret_cast<const char*>(raw.data()), raw.size() };
    }

    static bool source_eligible(const Entry& entry, Mode mode) noexcept
    {
        const bool dirty = any(entry.flags, EntryFlags::Dirty);
        return (mode == Mode::All || dirty)
               && (!any(entry.flags, EntryFlags::Deleted) || dirty);
    }

    static bool path_child_of(std::string_view path,
                              std::string_view root) noexcept
    {
        return path.size() > root.size() && path.starts_with(root)
               && (path[root.size()] == '/' || path[root.size()] == '[');
    }

    static bool scalar_unsigned(const MetaValue& value, uint64_t* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Scalar || value.count != 1U) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64:
            *out = value.data.u64;
            return true;
        default: return false;
        }
    }

    static bool scalar_nonnegative(const MetaValue& value,
                                   uint64_t* out) noexcept
    {
        if (scalar_unsigned(value, out)) {
            return true;
        }
        if (!out || value.kind != MetaValueKind::Scalar || value.count != 1U) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            if (value.data.i64 < 0) {
                return false;
            }
            *out = static_cast<uint64_t>(value.data.i64);
            return true;
        default: return false;
        }
    }

    static Status parse_decimal(const ByteArena& arena, const MetaValue& value,
                                uint32_t max_text, uint64_t* total_text,
                                uint64_t* out) noexcept
    {
        if (!total_text || !out) {
            return Status::InternalError;
        }
        if (value.kind == MetaValueKind::Text) {
            if (value.text_encoding != TextEncoding::Ascii
                && value.text_encoding != TextEncoding::Utf8
                && value.text_encoding != TextEncoding::Unknown) {
                return Status::InvalidSourceValue;
            }
            const std::string_view text = arena_text(arena, value.data.span);
            if (text.size() != value.count) {
                return Status::InvalidSourceValue;
            }
            if (text.size() > max_text) {
                return Status::ValueTooLong;
            }
            if (text.size() > UINT64_MAX - *total_text) {
                return Status::SourceLimitExceeded;
            }
            *total_text += text.size();
            if (text.empty()) {
                return Status::InvalidNumericValue;
            }
            uint64_t parsed = 0U;
            for (const char c : text) {
                if (c < '0' || c > '9') {
                    return Status::InvalidNumericValue;
                }
                const uint64_t digit = static_cast<uint64_t>(c - '0');
                if (parsed > (UINT64_MAX - digit) / 10U) {
                    return Status::ValueOutOfRange;
                }
                parsed = parsed * 10U + digit;
            }
            *out = parsed;
            return Status::Ok;
        }
        if (!scalar_nonnegative(value, out)) {
            return Status::InvalidSourceValue;
        }
        return Status::Ok;
    }

    static Status parse_description(const ByteArena& arena,
                                    const MetaValue& value, uint32_t max_text,
                                    uint64_t* total_text,
                                    std::string_view* out) noexcept
    {
        if (!total_text || !out || value.kind != MetaValueKind::Text
            || (value.text_encoding != TextEncoding::Ascii
                && value.text_encoding != TextEncoding::Utf8
                && value.text_encoding != TextEncoding::Unknown)) {
            return Status::InvalidSourceValue;
        }
        const std::string_view text = arena_text(arena, value.data.span);
        if (text.size() != value.count) {
            return Status::InvalidSourceValue;
        }
        if (text.empty()) {
            return Status::InvalidSourceValue;
        }
        if (text.size() > max_text) {
            return Status::ValueTooLong;
        }
        if (text.size() > UINT64_MAX - *total_text) {
            return Status::SourceLimitExceeded;
        }
        *total_text += text.size();
        if (text.starts_with("\xef\xbb\xbf")) {
            return Status::InvalidSourceValue;
        }
        bool ascii = true;
        if (!detail::exif_text_valid(text, &ascii)) {
            return Status::InvalidSourceValue;
        }
        *out = text;
        return Status::Ok;
    }

    static void add_native_ids(const MetaStore& source, FieldPlan* plan)
    {
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind != MetaKeyKind::ExifTag
                || arena_text(source.arena(), entry.key.data.exif_tag.ifd)
                       != "exififd"
                || entry.key.data.exif_tag.tag != plan->tag) {
                continue;
            }
            if (any(entry.flags, EntryFlags::Deleted)) {
                if (any(entry.flags, EntryFlags::Dirty)) {
                    plan->dirty_deleted = true;
                } else if (plan->clean_deleted == kInvalidEntryId) {
                    plan->clean_deleted = id;
                }
                continue;
            }
            plan->native.push_back(id);
        }
    }

    static bool native_equals(const MetaStore& source, const FieldPlan& plan)
    {
        if (!plan.present || plan.native.size() != 1U) {
            return false;
        }
        const Entry& entry = source.entry(plan.native.front());
        if (plan.tag == 0xa40eU) {
            std::string_view text;
            if (!detail::development_correction_value_valid(
                    source.arena(), plan.tag, entry.value)
                || !detail::exif_text_view(source.arena(), entry.value, true,
                                           &text)
                || text != plan.text) {
                return false;
            }
            const uint64_t expected_wire_count
                = static_cast<uint64_t>(text.size()) + 1U;
            return entry.origin.wire_type.family == WireFamily::Tiff
                   && entry.origin.wire_type.code == 129U
                   && entry.origin.wire_count == expected_wire_count;
        }
        return detail::development_correction_value_valid(
                   source.arena(), plan.tag, entry.value)
               && entry.value.data.u64 == plan.scalar
               && entry.origin.wire_type.family == WireFamily::Tiff
               && entry.origin.wire_type.code == 3U
               && entry.origin.wire_count == 1U;
    }

    static MetaValue copy_value_to_edit(const MetaStore& source,
                                        MetaEdit* edit,
                                        const MetaValue& value)
    {
        MetaValue copied = value;
        if (value.kind == MetaValueKind::Array
            || value.kind == MetaValueKind::Bytes
            || value.kind == MetaValueKind::Text) {
            copied.data.span
                = edit->arena().append(source.arena().span(value.data.span));
        }
        return copied;
    }

    static MetaValue make_native_value(ByteArena& arena, const FieldPlan& plan)
    {
        if (plan.tag == 0xa40eU) {
            return make_text(arena, plan.text, TextEncoding::Utf8);
        }
        return make_u16(plan.scalar);
    }

    static uint16_t native_wire_type(const FieldPlan& plan) noexcept
    {
        return plan.tag == 0xa40eU ? 129U : 3U;
    }

    static uint32_t native_wire_count(const MetaValue& value,
                                      const FieldPlan& plan) noexcept
    {
        return plan.tag == 0xa40eU ? value.count + 1U : 1U;
    }

    static void append_delete_marker(MetaEdit* edit, const FieldPlan& plan)
    {
        Entry entry;
        entry.key = make_exif_tag_key(edit->arena(), "exififd", plan.tag);
        if (plan.tag == 0xa40eU) {
            entry.value = make_text(edit->arena(), {}, TextEncoding::Utf8);
            entry.origin.wire_type = { WireFamily::Tiff, 129U };
            entry.origin.wire_count = 1U;
        } else {
            entry.value = make_u16(plan.tag == 0xa40dU ? 0x0101U : 0U);
            entry.origin.wire_type = { WireFamily::Tiff, 3U };
            entry.origin.wire_count = 1U;
        }
        entry.flags = EntryFlags::Dirty | EntryFlags::Deleted;
        edit->add_entry(entry);
    }

    static Status collect_sources(const MetaStore& source, Mode mode,
                                  std::span<FieldPlan> plans,
                                  MetadataCaptureTranslationResult* result)
    {
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || !source_eligible(entry, mode)) {
                continue;
            }
            const std::string_view ns
                = arena_text(source.arena(), entry.key.data.xmp_property.schema_ns);
            if (ns != kXmpNsExifEx) {
                continue;
            }
            const std::string_view path
                = arena_text(source.arena(), entry.key.data.xmp_property.property_path);
            for (FieldPlan& plan : plans) {
                if (!plan.enabled) {
                    continue;
                }
                if (plan.tag == 0xa40dU) {
                    SourceValue* destination = nullptr;
                    if (path == "DevelopmentType/DevelopmentCharacterstic") {
                        destination = &plan.characteristic;
                    } else if (path == "DevelopmentType/FactoryDefault") {
                        destination = &plan.factory_default;
                    } else if (path == "DevelopmentType"
                               || path_child_of(path, "DevelopmentType")) {
                        result->status = Status::UnsupportedSourceShape;
                        result->failed_mapping = plan.mapping;
                        result->failed_source_entry = id;
                        return result->status;
                    }
                    if (!destination) {
                        continue;
                    }
                    if (destination->found) {
                        result->status = Status::AmbiguousSource;
                        result->failed_mapping = plan.mapping;
                        result->failed_source_entry = id;
                        return result->status;
                    }
                    destination->found    = true;
                    destination->deleted  = any(entry.flags, EntryFlags::Deleted);
                    destination->entry_id = id;
                    destination->value    = &entry.value;
                    ++result->source_properties;
                    continue;
                }
                if (path == plan.path) {
                    if (plan.source.found) {
                        result->status = Status::AmbiguousSource;
                        result->failed_mapping = plan.mapping;
                        result->failed_source_entry = id;
                        return result->status;
                    }
                    plan.source.found    = true;
                    plan.source.deleted  = any(entry.flags, EntryFlags::Deleted);
                    plan.source.entry_id = id;
                    plan.source.value    = &entry.value;
                    ++result->source_properties;
                } else if (path_child_of(path, plan.path)) {
                    result->status = Status::UnsupportedSourceShape;
                    result->failed_mapping = plan.mapping;
                    result->failed_source_entry = id;
                    return result->status;
                }
            }
        }
        return Status::Ok;
    }

    static Status prepare_plan(const MetaStore& source,
                               const MetadataDevelopmentCorrectionTranslationOptions& options,
                               FieldPlan* plan, uint64_t* total_text,
                               MetadataCaptureTranslationResult* result)
    {
        if (plan->tag == 0xa40dU) {
            const bool any = plan->characteristic.found
                             || plan->factory_default.found;
            if (!any) {
                return Status::Ok;
            }
            if (!plan->characteristic.found || !plan->factory_default.found
                || plan->characteristic.deleted
                       != plan->factory_default.deleted) {
                result->failed_mapping = plan->mapping;
                result->failed_source_entry = plan->characteristic.found
                                                   ? plan->characteristic.entry_id
                                                   : plan->factory_default.entry_id;
                return Status::IncompleteSource;
            }
            plan->source_entry = plan->characteristic.entry_id;
            if (plan->characteristic.deleted) {
                plan->present = false;
                return Status::Ok;
            }
            uint64_t characteristic = 0U;
            uint64_t factory_default = 0U;
            Status status = parse_decimal(
                source.arena(), *plan->characteristic.value,
                options.max_text_bytes_per_property, total_text,
                &characteristic);
            if (status != Status::Ok) {
                result->failed_mapping = plan->mapping;
                result->failed_source_entry = plan->characteristic.entry_id;
                return status;
            }
            status = parse_decimal(source.arena(), *plan->factory_default.value,
                                   options.max_text_bytes_per_property,
                                   total_text, &factory_default);
            if (status != Status::Ok) {
                result->failed_mapping = plan->mapping;
                result->failed_source_entry = plan->factory_default.entry_id;
                return status;
            }
            if (!detail::development_type_choice(characteristic)
                || !detail::development_type_choice(factory_default)) {
                result->failed_mapping = plan->mapping;
                result->failed_source_entry = plan->factory_default.entry_id;
                return Status::ValueOutOfRange;
            }
            plan->scalar = static_cast<uint16_t>((characteristic << 8U)
                                                  | factory_default);
            plan->present = true;
            return Status::Ok;
        }
        if (!plan->source.found) {
            return Status::Ok;
        }
        plan->source_entry = plan->source.entry_id;
        plan->present = !plan->source.deleted;
        if (!plan->present) {
            return Status::Ok;
        }
        if (plan->tag == 0xa40eU) {
            return parse_description(source.arena(), *plan->source.value,
                                      options.max_text_bytes_per_property,
                                      total_text, &plan->text);
        }
        uint64_t value = 0U;
        const Status status = parse_decimal(
            source.arena(), *plan->source.value,
            options.max_text_bytes_per_property, total_text, &value);
        if (status != Status::Ok) {
            result->failed_mapping = plan->mapping;
            result->failed_source_entry = plan->source.entry_id;
            return status;
        }
        const uint64_t maximum = plan->tag == 0xa412U ? 3U : 1U;
        if (value > maximum || value > UINT16_MAX) {
            result->failed_mapping = plan->mapping;
            result->failed_source_entry = plan->source.entry_id;
            return Status::ValueOutOfRange;
        }
        plan->scalar = static_cast<uint16_t>(value);
        return Status::Ok;
    }

    static Status reconcile(const MetaStore& source,
                             const MetadataDevelopmentCorrectionTranslationOptions& options,
                             FieldPlan* plan,
                             MetadataCaptureTranslationResult* result) noexcept
    {
        const bool selected = plan->tag == 0xa40dU
                                  ? plan->characteristic.found
                                        || plan->factory_default.found
                                  : plan->source.found;
        if (!selected) {
            return Status::Ok;
        }
        if (!plan->present) {
            if (!plan->native.empty()) {
                if (options.conflict_policy == Policy::PreserveExisting) {
                    ++result->groups_preserved;
                    return Status::Ok;
                }
                if (options.conflict_policy == Policy::FailOnConflict) {
                    result->failed_mapping = plan->mapping;
                    result->failed_source_entry = plan->source_entry;
                    return Status::NativeConflict;
                }
                plan->apply = true;
                return Status::Ok;
            }
            if (options.conflict_policy != Policy::ReplaceExisting
                || plan->dirty_deleted) {
                ++result->groups_unchanged;
                return Status::Ok;
            }
            plan->apply = true;
            plan->promote = plan->clean_deleted != kInvalidEntryId;
            return Status::Ok;
        }
        if (options.conflict_policy == Policy::PreserveExisting
            && !plan->native.empty()) {
            ++result->groups_preserved;
            return Status::Ok;
        }
        if (native_equals(source, *plan)) {
            if (any(source.entry(plan->native.front()).flags,
                    EntryFlags::Dirty)) {
                ++result->groups_unchanged;
                return Status::Ok;
            }
            plan->promote = true;
            plan->apply = true;
            return Status::Ok;
        }
        if (options.conflict_policy == Policy::FailOnConflict
            && !plan->native.empty()) {
            result->failed_mapping = plan->mapping;
            result->failed_source_entry = plan->source_entry;
            return Status::NativeConflict;
        }
        plan->apply = true;
        return Status::Ok;
    }

}  // namespace

MetadataCaptureTranslationResult
translate_xmp_development_correction_metadata(
    const MetaStore& source,
    const MetadataDevelopmentCorrectionTranslationOptions& options,
    MetaStore* out_store)
{
    if (!out_store) {
        return error(Status::NullOutput);
    }
    if (!source.is_finalized()) {
        return error(Status::SourceNotFinalized);
    }
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (options.exif_version != 0U && options.exif_version != 300U
            && options.exif_version != 310U)
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataDevelopmentCorrectionTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataDevelopmentCorrectionTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataDevelopmentCorrectionTranslationMaxTotalTextBytes
        || (!options.development_type_to_exif
            && !options.development_type_description_to_exif
            && !options.distortion_correction_to_exif
            && !options.chromatic_aberration_correction_to_exif
            && !options.shading_correction_to_exif
            && !options.noise_reduction_to_exif)) {
        return error(Status::InvalidOptions);
    }

    std::array<FieldPlan, 6U> plans {};
    plans[0].tag     = 0xa40dU;
    plans[0].path    = "DevelopmentType";
    plans[0].mapping = Mapping::XmpDevelopmentType;
    plans[0].enabled = options.development_type_to_exif;
    plans[1].tag     = 0xa40eU;
    plans[1].path    = "DevelopmentTypeDescription";
    plans[1].mapping = Mapping::XmpDevelopmentTypeDescription;
    plans[1].enabled = options.development_type_description_to_exif;
    plans[2].tag     = 0xa40fU;
    plans[2].path    = "DistortionCorrection";
    plans[2].mapping = Mapping::XmpDistortionCorrection;
    plans[2].enabled = options.distortion_correction_to_exif;
    plans[3].tag     = 0xa410U;
    plans[3].path    = "ChromaticAberrationCorrection";
    plans[3].mapping = Mapping::XmpChromaticAberrationCorrection;
    plans[3].enabled = options.chromatic_aberration_correction_to_exif;
    plans[4].tag     = 0xa411U;
    plans[4].path    = "ShadingCorrection";
    plans[4].mapping = Mapping::XmpShadingCorrection;
    plans[4].enabled = options.shading_correction_to_exif;
    plans[5].tag     = 0xa412U;
    plans[5].path    = "NoiseReduction";
    plans[5].mapping = Mapping::XmpNoiseReduction;
    plans[5].enabled = options.noise_reduction_to_exif;
    MetadataCaptureTranslationResult result;
    Status status = collect_sources(source, options.source_mode,
                                    std::span(plans), &result);
    if (status != Status::Ok) {
        return result;
    }

    uint64_t total_text = 0U;
    bool selected        = false;
    for (FieldPlan& plan : plans) {
        if (!plan.enabled) {
            continue;
        }
        status = prepare_plan(source, options, &plan, &total_text, &result);
        if (status != Status::Ok) {
            result.status = status;
            return result;
        }
        selected = selected
                   || (plan.tag == 0xa40dU
                       ? plan.characteristic.found || plan.factory_default.found
                       : plan.source.found);
        add_native_ids(source, &plan);
    }
    if (total_text > options.max_total_text_bytes) {
        result.status = Status::SourceLimitExceeded;
        return result;
    }
    if (!selected) {
        *out_store = source;
        return result;
    }
    if (options.exif_version == 0U) {
        result.status = Status::IncompleteSource;
        for (const FieldPlan& plan : plans) {
            if (plan.enabled
                && (plan.tag == 0xa40dU
                        ? plan.characteristic.found || plan.factory_default.found
                        : plan.source.found)) {
                result.failed_mapping = plan.mapping;
                result.failed_source_entry = plan.source_entry;
                break;
            }
        }
        return result;
    }
    for (FieldPlan& plan : plans) {
        status = reconcile(source, options, &plan, &result);
        if (status != Status::Ok) {
            result.status = status;
            return result;
        }
    }

    uint32_t additions  = 0U;
    uint64_t operations = 0U;
    for (const FieldPlan& plan : plans) {
        if (!plan.apply) {
            continue;
        }
        if (plan.present) {
            if (plan.promote)
                ++operations;
            else
                operations += plan.native.size();
            if (plan.native.empty()) {
                ++operations;
                ++additions;
            }
        } else if (!plan.native.empty()) {
            operations += plan.native.size();
        } else if (plan.promote) {
            ++operations;
        } else if (!plan.dirty_deleted) {
            ++operations;
            ++additions;
        }
    }
    if (additions > options.max_added_entries) {
        result.status = Status::EntryLimitExceeded;
        return result;
    }
    if (source.entries().size() > static_cast<size_t>(kInvalidEntryId)
        || static_cast<size_t>(additions)
               > static_cast<size_t>(kInvalidEntryId)
                     - source.entries().size()) {
        result.status = Status::EntryLimitExceeded;
        return result;
    }
    if (operations > options.max_operations) {
        result.status = Status::OperationLimitExceeded;
        return result;
    }

    MetaEdit edit;
    edit.reserve_ops(static_cast<size_t>(operations));
    for (FieldPlan& plan : plans) {
        if (!plan.apply) {
            continue;
        }
        ++result.groups_translated;
        MetaValue value;
        uint32_t wire_count = 0U;
        if (plan.present && !plan.promote) {
            value      = make_native_value(edit.arena(), plan);
            wire_count = native_wire_count(value, plan);
        }
        bool written = false;
        for (const EntryId id : plan.native) {
            if (plan.present && plan.promote && !written) {
                const MetaValue copied = copy_value_to_edit(
                    source, &edit, source.entry(id).value);
                edit.set_value(id, copied);
                ++result.entries_updated;
                written = true;
            } else if (plan.present && !written) {
                edit.set_value(id, value,
                               { WireFamily::Tiff, native_wire_type(plan) },
                               wire_count);
                ++result.entries_updated;
                written = true;
            } else {
                edit.tombstone(id);
                ++result.entries_removed;
            }
        }
        if (plan.present && !written) {
            Entry entry;
            entry.key = make_exif_tag_key(edit.arena(), "exififd", plan.tag);
            entry.value = value;
            entry.origin.wire_type = { WireFamily::Tiff,
                                       native_wire_type(plan) };
            entry.origin.wire_count = wire_count;
            entry.flags = EntryFlags::Dirty;
            edit.add_entry(entry);
            ++result.entries_added;
        } else if (!plan.present && plan.native.empty()) {
            if (plan.promote) {
                edit.tombstone(plan.clean_deleted);
                ++result.entries_updated;
            } else if (!plan.dirty_deleted) {
                append_delete_marker(&edit, plan);
                ++result.entries_added;
            }
        }
    }
    if (edit.ops().size() != operations || edit.arena().limit_exceeded()) {
        result.status = Status::InternalError;
        return result;
    }
    MetaStore candidate = commit(source, std::span(&edit, 1U));
    if (candidate.resource_limit_exceeded()) {
        result.status = Status::EntryLimitExceeded;
        return result;
    }
    *out_store = std::move(candidate);
    result.failed_mapping      = Mapping::None;
    result.failed_source_entry = kInvalidEntryId;
    return result;
}

}  // namespace openmeta
