// SPDX-License-Identifier: Apache-2.0

#include "metadata_learning_fields_internal.h"

#include "openmeta/meta_edit.h"
#include "openmeta/metadata_translation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    using Status = MetadataCaptureTranslationStatus;
    using Mapping = MetadataCaptureTranslationMapping;
    using Mode = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;

    constexpr std::string_view kXmpNsExifEx = "http://cipa.jp/exif/1.0/";
    constexpr std::string_view kRoot = "LearningOptOutIn";
    constexpr std::string_view kNumberOfSets = "LearningOptOutIn/NumberOfSets";
    constexpr std::string_view kValues = "LearningOptOutIn/Values";
    constexpr size_t kMaxValues
        = static_cast<size_t>(kMetadataLearningOptOutInTranslationMaxSets) * 2U;

    struct SourceValue final {
        bool found = false;
        bool deleted = false;
        EntryId entry_id = kInvalidEntryId;
        const MetaValue* value = nullptr;
    };

    struct Plan final {
        SourceValue number_of_sets;
        SourceValue values_root;
        std::array<SourceValue, kMaxValues> values {};
        uint32_t indexed_values = 0U;
        std::vector<EntryId> native;
        std::array<uint16_t, kMaxValues + 1U> words {};
        uint16_t sets = 0U;
        uint32_t word_count = 0U;
        EntryId source_entry = kInvalidEntryId;
        bool present = false;
        bool apply = false;
    };

    static MetadataCaptureTranslationResult
    error(Status status, EntryId source = kInvalidEntryId) noexcept
    {
        MetadataCaptureTranslationResult result;
        result.status = status;
        result.failed_mapping = Mapping::XmpLearningOptOutIn;
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

    static bool scalar_unsigned(const MetaValue& value, uint64_t* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Scalar || value.count != 1U)
            return false;
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
        if (scalar_unsigned(value, out))
            return true;
        if (!out || value.kind != MetaValueKind::Scalar || value.count != 1U)
            return false;
        switch (value.elem_type) {
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            if (value.data.i64 < 0)
                return false;
            *out = static_cast<uint64_t>(value.data.i64);
            return true;
        default: return false;
        }
    }

    static Status parse_decimal(const ByteArena& arena, const MetaValue& value,
                                uint32_t max_text, uint64_t* total_text,
                                uint64_t* out) noexcept
    {
        if (!total_text || !out)
            return Status::InternalError;
        if (value.kind != MetaValueKind::Text) {
            if (!scalar_nonnegative(value, out))
                return Status::InvalidSourceValue;
            return Status::Ok;
        }
        if (value.text_encoding != TextEncoding::Ascii
            && value.text_encoding != TextEncoding::Utf8
            && value.text_encoding != TextEncoding::Unknown)
            return Status::InvalidSourceValue;
        const std::string_view text = arena_text(arena, value.data.span);
        if (text.size() != value.count)
            return Status::InvalidSourceValue;
        if (text.size() > max_text)
            return Status::ValueTooLong;
        if (text.size() > UINT64_MAX - *total_text)
            return Status::SourceLimitExceeded;
        *total_text += text.size();
        if (text.empty())
            return Status::InvalidNumericValue;
        uint64_t parsed = 0U;
        for (const char c : text) {
            if (c < '0' || c > '9')
                return Status::InvalidNumericValue;
            const uint64_t digit = static_cast<uint64_t>(c - '0');
            if (parsed > (UINT64_MAX - digit) / 10U)
                return Status::ValueOutOfRange;
            parsed = parsed * 10U + digit;
        }
        *out = parsed;
        return Status::Ok;
    }

    static bool array_value_at(const ByteArena& arena, const MetaValue& value,
                               uint32_t index, uint64_t* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Array || index >= value.count)
            return false;
        const std::span<const std::byte> raw = arena.span(value.data.span);
        size_t element_size = 0U;
        switch (value.elem_type) {
        case MetaElementType::U8: element_size = 1U; break;
        case MetaElementType::U16: element_size = 2U; break;
        case MetaElementType::U32: element_size = 4U; break;
        case MetaElementType::U64: element_size = 8U; break;
        case MetaElementType::I8: element_size = 1U; break;
        case MetaElementType::I16: element_size = 2U; break;
        case MetaElementType::I32: element_size = 4U; break;
        case MetaElementType::I64: element_size = 8U; break;
        default: return false;
        }
        if (value.data.span.size != value.count * element_size
            || raw.size() != value.data.span.size)
            return false;
        const std::byte* p = raw.data() + static_cast<size_t>(index) * element_size;
        uint64_t value_u = 0U;
        switch (value.elem_type) {
        case MetaElementType::U8:
            value_u = std::to_integer<uint8_t>(*p);
            break;
        case MetaElementType::U16: {
            uint16_t v = 0U;
            std::memcpy(&v, p, sizeof(v));
            value_u = v;
            break;
        }
        case MetaElementType::U32: {
            uint32_t v = 0U;
            std::memcpy(&v, p, sizeof(v));
            value_u = v;
            break;
        }
        case MetaElementType::U64:
            std::memcpy(&value_u, p, sizeof(value_u));
            break;
        case MetaElementType::I8: {
            const int8_t v = static_cast<int8_t>(std::to_integer<uint8_t>(*p));
            if (v < 0)
                return false;
            value_u = static_cast<uint64_t>(v);
            break;
        }
        case MetaElementType::I16: {
            int16_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            if (v < 0)
                return false;
            value_u = static_cast<uint64_t>(v);
            break;
        }
        case MetaElementType::I32: {
            int32_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            if (v < 0)
                return false;
            value_u = static_cast<uint64_t>(v);
            break;
        }
        case MetaElementType::I64: {
            int64_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            if (v < 0)
                return false;
            value_u = static_cast<uint64_t>(v);
            break;
        }
        default: return false;
        }
        *out = value_u;
        return true;
    }

    static Status parse_array(const ByteArena& arena, const MetaValue& value,
                              uint32_t max_values, std::span<uint16_t> out,
                              uint32_t* count) noexcept
    {
        if (!count || value.kind != MetaValueKind::Array
            || value.count > max_values || value.count > out.size())
            return Status::InvalidSourceValue;
        for (uint32_t i = 0U; i < value.count; ++i) {
            uint64_t parsed = 0U;
            if (!array_value_at(arena, value, i, &parsed))
                return Status::InvalidSourceValue;
            if (parsed > UINT16_MAX)
                return Status::ValueOutOfRange;
            out[i] = static_cast<uint16_t>(parsed);
        }
        *count = value.count;
        return Status::Ok;
    }

    static bool parse_index(std::string_view path, uint32_t* out) noexcept
    {
        if (!out || !path.starts_with(kValues) || path.size() <= kValues.size()
            || path[kValues.size()] != '[' || path.back() != ']')
            return false;
        const std::string_view digits
            = path.substr(kValues.size() + 1U,
                          path.size() - kValues.size() - 2U);
        if (digits.empty())
            return false;
        uint64_t value = 0U;
        for (char c : digits) {
            if (c < '0' || c > '9')
                return false;
            const uint64_t digit = static_cast<uint64_t>(c - '0');
            if (value > (UINT32_MAX - digit) / 10U)
                return false;
            value = value * 10U + digit;
        }
        if (value == 0U || value > kMaxValues)
            return false;
        *out = static_cast<uint32_t>(value);
        return true;
    }

    static void add_native_ids(const MetaStore& source, Plan* plan)
    {
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::ExifTag
                || arena_text(source.arena(), entry.key.data.exif_tag.ifd)
                       != "exififd"
                || entry.key.data.exif_tag.tag != detail::kLearningOptOutInTag)
                continue;
            plan->native.push_back(id);
        }
    }

    static bool native_equals(const MetaStore& source, const Plan& plan)
    {
        if (!plan.present || plan.native.size() != 1U)
            return false;
        const Entry& entry = source.entry(plan.native.front());
        if (!detail::learning_opt_out_in_value_valid(
                source.arena(), entry.value, entry.flags, plan.sets))
            return false;
        const std::span<const std::byte> raw
            = source.arena().span(entry.value.data.span);
        const bool little = !any(entry.flags, EntryFlags::ValueBigEndian);
        if (raw.size() != static_cast<size_t>(plan.word_count) * 2U)
            return false;
        for (uint32_t i = 0U; i < plan.word_count; ++i) {
            if (detail::learning_opt_out_in_u16(raw, static_cast<size_t>(i) * 2U,
                                                little)
                != plan.words[i])
                return false;
        }
        return entry.origin.wire_type.family == WireFamily::Tiff
               && entry.origin.wire_type.code == 7U
               && entry.origin.wire_count == raw.size();
    }

    static MetaValue make_native_value(ByteArena& arena, const Plan& plan)
    {
        std::array<std::byte, 4U * kMetadataLearningOptOutInTranslationMaxSets
                                   + 2U>
            raw {};
        for (uint32_t i = 0U; i < plan.word_count; ++i) {
            raw[static_cast<size_t>(i) * 2U]
                = static_cast<std::byte>(plan.words[i] & 0xffU);
            raw[static_cast<size_t>(i) * 2U + 1U]
                = static_cast<std::byte>(plan.words[i] >> 8U);
        }
        return make_bytes(arena,
                          std::span<const std::byte>(raw.data(),
                                                     plan.word_count * 2U));
    }

    static Status collect_sources(const MetaStore& source, Mode mode, Plan* plan,
                                  MetadataCaptureTranslationResult* result)
    {
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || !source_eligible(entry, mode))
                continue;
            if (arena_text(source.arena(), entry.key.data.xmp_property.schema_ns)
                != kXmpNsExifEx)
                continue;
            const std::string_view path
                = arena_text(source.arena(), entry.key.data.xmp_property.property_path);
            SourceValue* destination = nullptr;
            bool indexed = false;
            if (path == kNumberOfSets) {
                destination = &plan->number_of_sets;
            } else if (path == kValues) {
                destination = &plan->values_root;
            } else {
                uint32_t index = 0U;
                if (parse_index(path, &index)) {
                    destination = &plan->values[index - 1U];
                    indexed = true;
                } else if (path == kRoot
                           || (path.starts_with(kRoot)
                               && path.size() > kRoot.size()
                               && (path[kRoot.size()] == '/'
                                   || path[kRoot.size()] == '['))) {
                    result->status = Status::UnsupportedSourceShape;
                    result->failed_mapping = Mapping::XmpLearningOptOutIn;
                    result->failed_source_entry = id;
                    return result->status;
                } else {
                    continue;
                }
            }
            if (destination->found) {
                result->status = Status::AmbiguousSource;
                result->failed_mapping = Mapping::XmpLearningOptOutIn;
                result->failed_source_entry = id;
                return result->status;
            }
            destination->found = true;
            destination->deleted = any(entry.flags, EntryFlags::Deleted);
            destination->entry_id = id;
            destination->value = &entry.value;
            if (indexed)
                ++plan->indexed_values;
            ++result->source_properties;
        }
        return Status::Ok;
    }

    static Status prepare_plan(const MetaStore& source,
                               const MetadataLearningOptOutInTranslationOptions& options,
                               Plan* plan, uint64_t* total_text,
                               MetadataCaptureTranslationResult* result)
    {
        const bool selected = plan->number_of_sets.found
                              || plan->values_root.found
                              || plan->indexed_values != 0U;
        if (!selected)
            return Status::Ok;
        if (!plan->number_of_sets.found) {
            result->failed_source_entry = plan->values_root.found
                                               ? plan->values_root.entry_id
                                               : plan->values[0].entry_id;
            return Status::IncompleteSource;
        }
        plan->source_entry = plan->number_of_sets.entry_id;
        if (plan->number_of_sets.deleted) {
            if (plan->values_root.found || plan->indexed_values != 0U)
                return Status::IncompleteSource;
            plan->present = false;
            return Status::Ok;
        }
        uint64_t sets = 0U;
        Status status = parse_decimal(source.arena(),
                                      *plan->number_of_sets.value,
                                      options.max_text_bytes_per_property,
                                      total_text, &sets);
        if (status != Status::Ok)
            return status;
        if (sets == 0U || sets > options.max_sets || sets > UINT16_MAX)
            return Status::ValueOutOfRange;
        plan->sets = static_cast<uint16_t>(sets);
        const uint32_t expected = static_cast<uint32_t>(sets) * 2U;
        if (plan->values_root.found && plan->indexed_values != 0U)
            return Status::AmbiguousSource;
        if (plan->values_root.found) {
            if (plan->values_root.deleted)
                return Status::IncompleteSource;
            uint32_t count = 0U;
            status = parse_array(source.arena(), *plan->values_root.value,
                                 expected, std::span(plan->words).subspan(1U),
                                 &count);
            if (status != Status::Ok)
                return status;
            if (count != expected)
                return Status::IncompleteSource;
        } else {
            for (uint32_t i = 0U; i < expected; ++i) {
                const SourceValue& value = plan->values[i];
                if (!value.found || value.deleted)
                    return Status::IncompleteSource;
                uint64_t parsed = 0U;
                status = parse_decimal(source.arena(), *value.value,
                                       options.max_text_bytes_per_property,
                                       total_text, &parsed);
                if (status != Status::Ok)
                    return status;
                if (parsed > UINT16_MAX)
                    return Status::ValueOutOfRange;
                plan->words[i + 1U] = static_cast<uint16_t>(parsed);
            }
            for (uint32_t i = expected; i < kMaxValues; ++i) {
                if (plan->values[i].found)
                    return Status::IncompleteSource;
            }
        }
        plan->words[0] = plan->sets;
        plan->word_count = expected + 1U;
        if (plan->words[1] != 0U)
            return Status::ValueOutOfRange;
        uint32_t usage_mask = 0U;
        for (uint32_t i = 0U; i < plan->sets; ++i) {
            const uint16_t usage = plan->words[1U + i * 2U];
            const uint16_t intention = plan->words[2U + i * 2U];
            if (usage > 4U || intention > 2U
                || (usage_mask & (1U << usage)) != 0U)
                return Status::ValueOutOfRange;
            usage_mask |= 1U << usage;
        }
        plan->present = true;
        return Status::Ok;
    }

    static Status reconcile(const MetaStore& source,
                            const MetadataLearningOptOutInTranslationOptions& options,
                            Plan* plan,
                            MetadataCaptureTranslationResult* result) noexcept
    {
        const bool selected = plan->number_of_sets.found
                              || plan->values_root.found
                              || plan->indexed_values != 0U;
        if (!selected)
            return Status::Ok;
        if (options.conflict_policy == Policy::PreserveExisting
            && !plan->native.empty()) {
            ++result->groups_preserved;
            return Status::Ok;
        }
        if (native_equals(source, *plan)) {
            ++result->groups_unchanged;
            return Status::Ok;
        }
        if (options.conflict_policy == Policy::FailOnConflict
            && !plan->native.empty()) {
            result->failed_source_entry = plan->source_entry;
            return Status::NativeConflict;
        }
        plan->apply = true;
        return Status::Ok;
    }

}  // namespace

MetadataCaptureTranslationResult
translate_xmp_learning_opt_out_in_metadata(
    const MetaStore& source,
    const MetadataLearningOptOutInTranslationOptions& options,
    MetaStore* out_store)
{
    if (!out_store)
        return error(Status::NullOutput);
    if (!source.is_finalized())
        return error(Status::SourceNotFinalized);
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (options.exif_version != 0U && options.exif_version != 300U
            && options.exif_version != 310U)
        || options.max_sets == 0U
        || options.max_sets > kMetadataLearningOptOutInTranslationMaxSets
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataLearningOptOutInTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataCaptureTranslationMaxTotalTextBytes
        || !options.learning_opt_out_in_to_exif)
        return error(Status::InvalidOptions);

    Plan plan;
    MetadataCaptureTranslationResult result;
    Status status = collect_sources(source, options.source_mode, &plan, &result);
    if (status != Status::Ok)
        return result;
    uint64_t total_text = 0U;
    status = prepare_plan(source, options, &plan, &total_text, &result);
    if (status != Status::Ok) {
        result.status = status;
        result.failed_mapping = Mapping::XmpLearningOptOutIn;
        return result;
    }
    add_native_ids(source, &plan);
    if (total_text > options.max_total_text_bytes) {
        result.status = Status::SourceLimitExceeded;
        result.failed_mapping = Mapping::XmpLearningOptOutIn;
        return result;
    }
    const bool selected = plan.number_of_sets.found || plan.values_root.found
                          || plan.indexed_values != 0U;
    if (!selected) {
        *out_store = source;
        return result;
    }
    if (options.exif_version == 0U) {
        result.status = Status::IncompleteSource;
        result.failed_mapping = Mapping::XmpLearningOptOutIn;
        result.failed_source_entry = plan.source_entry;
        return result;
    }
    status = reconcile(source, options, &plan, &result);
    if (status != Status::Ok) {
        result.status = status;
        result.failed_mapping = Mapping::XmpLearningOptOutIn;
        return result;
    }
    uint64_t operations = 0U;
    uint32_t additions = 0U;
    if (plan.apply) {
        operations = plan.native.size();
        if (plan.present && plan.native.empty()) {
            ++operations;
            ++additions;
        }
    }
    if (additions > options.max_added_entries
        || source.entries().size() > static_cast<size_t>(kInvalidEntryId)
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
    if (!plan.apply) {
        *out_store = source;
        return result;
    }

    MetaEdit edit;
    edit.reserve_ops(static_cast<size_t>(operations));
    bool written = false;
    MetaValue value;
    if (plan.present)
        value = make_native_value(edit.arena(), plan);
    for (const EntryId id : plan.native) {
        if (plan.present && !written) {
            edit.set_value(id, value, { WireFamily::Tiff, 7U }, value.count);
            ++result.entries_updated;
            written = true;
        } else {
            edit.tombstone(id);
            ++result.entries_removed;
        }
    }
    if (plan.present && !written) {
        Entry entry;
        entry.key = make_exif_tag_key(edit.arena(), "exififd",
                                      detail::kLearningOptOutInTag);
        entry.value = value;
        entry.origin.wire_type = { WireFamily::Tiff, 7U };
        entry.origin.wire_count = value.count;
        entry.flags = EntryFlags::Dirty;
        edit.add_entry(entry);
        ++result.entries_added;
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
    ++result.groups_translated;
    result.failed_mapping = Mapping::None;
    result.failed_source_entry = kInvalidEntryId;
    return result;
}

}  // namespace openmeta
