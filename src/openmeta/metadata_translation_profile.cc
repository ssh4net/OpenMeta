// SPDX-License-Identifier: Apache-2.0

#include "metadata_text_fields_internal.h"

#include "openmeta/meta_edit.h"
#include "openmeta/metadata_translation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    using Status = MetadataCaptureTranslationStatus;
    using Mapping = MetadataCaptureTranslationMapping;
    using Mode = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;

    constexpr std::string_view kNsTiff = "http://ns.adobe.com/tiff/1.0/";
    constexpr std::string_view kNsExif = "http://ns.adobe.com/exif/1.0/";

    enum class FieldKind : uint8_t {
        Text,
        ColorSpace,
        RelatedSoundFile,
    };

    struct FieldSpec final {
        std::string_view ns;
        std::string_view path;
        std::string_view ifd;
        uint16_t tag = 0U;
        Mapping mapping = Mapping::None;
        FieldKind kind = FieldKind::Text;
        bool MetadataProfileTranslationOptions::* enabled = nullptr;
    };

    static constexpr std::array<FieldSpec, 5U> kFields = { {
        { kNsTiff, "ImageDescription", "ifd0", 0x010eU,
          Mapping::XmpTiffImageDescription, FieldKind::Text,
          &MetadataProfileTranslationOptions::image_description_to_exif },
        { kNsTiff, "Artist", "ifd0", 0x013bU, Mapping::XmpTiffArtist,
          FieldKind::Text, &MetadataProfileTranslationOptions::artist_to_exif },
        { kNsTiff, "Copyright", "ifd0", 0x8298U,
          Mapping::XmpTiffCopyright, FieldKind::Text,
          &MetadataProfileTranslationOptions::copyright_to_exif },
        { kNsExif, "ColorSpace", "exififd", 0xa001U,
          Mapping::XmpColorSpace, FieldKind::ColorSpace,
          &MetadataProfileTranslationOptions::color_space_to_exif },
        { kNsExif, "RelatedSoundFile", "exififd", 0xa004U,
          Mapping::XmpRelatedSoundFile, FieldKind::RelatedSoundFile,
          &MetadataProfileTranslationOptions::related_sound_file_to_exif },
    } };

    struct SourceValue final {
        bool found = false;
        bool deleted = false;
        EntryId entry_id = kInvalidEntryId;
        const MetaValue* value = nullptr;
    };

    struct FieldPlan final {
        const FieldSpec* spec = nullptr;
        SourceValue source;
        std::vector<EntryId> native;
        bool present = false;
        uint16_t scalar = 0U;
        std::string_view text;
        uint16_t wire_type = 2U;
        EntryId source_entry = kInvalidEntryId;
        bool apply = false;
    };

    static MetadataCaptureTranslationResult
    error(Status status, Mapping mapping = Mapping::None,
          EntryId source = kInvalidEntryId) noexcept
    {
        MetadataCaptureTranslationResult result;
        result.status = status;
        result.failed_mapping = mapping;
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

    static bool scalar_nonnegative(const MetaValue& value,
                                   uint64_t* out) noexcept
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
        if (text.empty() || text.size() > max_text)
            return text.empty() ? Status::InvalidNumericValue
                                 : Status::ValueTooLong;
        if (text.size() > UINT64_MAX - *total_text)
            return Status::SourceLimitExceeded;
        *total_text += text.size();
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

    static Status parse_color_space(const ByteArena& arena,
                                    const MetaValue& value, uint32_t max_text,
                                    uint64_t* total_text,
                                    uint64_t* out) noexcept
    {
        if (!total_text || !out)
            return Status::InternalError;
        if (value.kind == MetaValueKind::Text) {
            if (value.text_encoding != TextEncoding::Ascii
                && value.text_encoding != TextEncoding::Utf8
                && value.text_encoding != TextEncoding::Unknown)
                return Status::InvalidSourceValue;
            const std::string_view text = arena_text(arena, value.data.span);
            if (text.size() != value.count
                || value.count != value.data.span.size)
                return Status::InvalidSourceValue;
            if (text == "sRGB") {
                if (text.size() > max_text)
                    return Status::ValueTooLong;
                if (text.size() > UINT64_MAX - *total_text)
                    return Status::SourceLimitExceeded;
                *total_text += text.size();
                *out = 1U;
                return Status::Ok;
            }
            if (text == "Uncalibrated") {
                if (text.size() > max_text)
                    return Status::ValueTooLong;
                if (text.size() > UINT64_MAX - *total_text)
                    return Status::SourceLimitExceeded;
                *total_text += text.size();
                *out = 65535U;
                return Status::Ok;
            }
        }
        return parse_decimal(arena, value, max_text, total_text, out);
    }

    static bool profile_text(const ByteArena& arena, const MetaValue& value,
                             std::string_view* out, bool* ascii) noexcept
    {
        if (!out || !ascii || value.kind != MetaValueKind::Text
            || (value.text_encoding != TextEncoding::Ascii
                && value.text_encoding != TextEncoding::Utf8
                && value.text_encoding != TextEncoding::Unknown)
            || value.count != value.data.span.size)
            return false;
        const std::string_view text = arena_text(arena, value.data.span);
        if (text.empty() || text.find('\0') != std::string_view::npos
            || text.starts_with("\xef\xbb\xbf"))
            return false;
        return detail::exif_text_valid(text, ascii) && (*out = text, true);
    }

    static bool related_sound_file_valid(std::string_view text) noexcept
    {
        const size_t dot = text.find('.');
        if (dot == std::string_view::npos || dot == 0U || dot > 8U
            || text.size() - dot - 1U == 0U
            || text.size() - dot - 1U > 3U)
            return false;
        for (size_t i = 0U; i < text.size(); ++i) {
            const char c = text[i];
            if (i == dot)
                continue;
            if (c < 0x20 || c > 0x7e || c == '/' || c == '\\'
                || c == '.')
                return false;
        }
        return true;
    }

    static bool native_text_view(const ByteArena& arena, const Entry& entry,
                                 std::string_view* out) noexcept
    {
        if (!out)
            return false;
        if (entry.value.kind == MetaValueKind::Text)
            return detail::exif_text_view(arena, entry.value, true, out);
        if (entry.value.kind != MetaValueKind::Bytes
            || entry.value.count != entry.value.data.span.size)
            return false;
        std::span<const std::byte> raw = arena.span(entry.value.data.span);
        if (!raw.empty() && raw.back() == std::byte { 0 })
            raw = raw.first(raw.size() - 1U);
        for (std::byte b : raw)
            if (b == std::byte { 0 })
                return false;
        const std::string_view text(reinterpret_cast<const char*>(raw.data()),
                                    raw.size());
        bool ascii = true;
        if (!detail::exif_text_valid(text, &ascii))
            return false;
        *out = text;
        return true;
    }

    static void add_native_ids(const MetaStore& source, FieldPlan* plan)
    {
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (any(entry.flags, EntryFlags::Deleted)
                || entry.key.kind != MetaKeyKind::ExifTag
                || arena_text(source.arena(), entry.key.data.exif_tag.ifd)
                       != plan->spec->ifd
                || entry.key.data.exif_tag.tag != plan->spec->tag)
                continue;
            plan->native.push_back(id);
        }
    }

    static bool native_equals(const MetaStore& source, const FieldPlan& plan)
    {
        if (!plan.present || plan.native.size() != 1U)
            return false;
        const Entry& entry = source.entry(plan.native.front());
        if (entry.origin.wire_type.family != WireFamily::Tiff
            || entry.origin.wire_type.code != plan.wire_type)
            return false;
        if (plan.spec->kind == FieldKind::ColorSpace)
            return entry.value.kind == MetaValueKind::Scalar
                   && entry.value.count == 1U
                   && entry.value.elem_type == MetaElementType::U16
                   && entry.value.data.u64 == plan.scalar;
        std::string_view native;
        return native_text_view(source.arena(), entry, &native)
               && native == plan.text;
    }

    static Status prepare_plan(
        const MetaStore& source,
        const MetadataProfileTranslationOptions& options, FieldPlan* plan,
        uint64_t* total_text)
    {
        if (!plan->source.found)
            return Status::Ok;
        plan->source_entry = plan->source.entry_id;
        plan->present = !plan->source.deleted;
        if (!plan->present)
            return Status::Ok;
        if (plan->spec->kind == FieldKind::ColorSpace) {
            uint64_t value = 0U;
            const Status status = parse_color_space(
                source.arena(), *plan->source.value,
                options.max_text_bytes_per_property, total_text, &value);
            if (status != Status::Ok)
                return status;
            if (value != 1U && value != 65535U)
                return Status::ValueOutOfRange;
            plan->scalar = static_cast<uint16_t>(value);
            plan->wire_type = 3U;
            return Status::Ok;
        }
        bool ascii = true;
        if (!profile_text(source.arena(), *plan->source.value, &plan->text,
                          &ascii))
            return Status::InvalidSourceValue;
        if (plan->spec->kind == FieldKind::RelatedSoundFile) {
            if (!ascii || !related_sound_file_valid(plan->text))
                return Status::InvalidSourceValue;
            if (plan->text.size() > options.max_text_bytes_per_property)
                return Status::ValueTooLong;
            if (plan->text.size() > UINT64_MAX - *total_text)
                return Status::SourceLimitExceeded;
            *total_text += plan->text.size();
            plan->wire_type = 2U;
        } else {
            if (plan->text.size() > options.max_text_bytes_per_property)
                return Status::ValueTooLong;
            if (plan->text.size() > UINT64_MAX - *total_text)
                return Status::SourceLimitExceeded;
            *total_text += plan->text.size();
            plan->wire_type = ascii ? 2U : 129U;
        }
        return Status::Ok;
    }

    static Status collect_sources(
        const MetaStore& source, const MetadataProfileTranslationOptions& options,
        std::array<FieldPlan, 5U>* plans,
        MetadataCaptureTranslationResult* result)
    {
        for (FieldPlan& plan : *plans)
            add_native_ids(source, &plan);
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || !source_eligible(entry, options.source_mode))
                continue;
            const std::string_view ns
                = arena_text(source.arena(), entry.key.data.xmp_property.schema_ns);
            const std::string_view path
                = arena_text(source.arena(), entry.key.data.xmp_property.property_path);
            for (FieldPlan& plan : *plans) {
                if (!plan.spec || !(options.*plan.spec->enabled)
                    || ns != plan.spec->ns)
                    continue;
                if (path == plan.spec->path) {
                    if (plan.source.found) {
                        result->status = Status::AmbiguousSource;
                        result->failed_mapping = plan.spec->mapping;
                        result->failed_source_entry = id;
                        return result->status;
                    }
                    plan.source.found = true;
                    plan.source.deleted = any(entry.flags, EntryFlags::Deleted);
                    plan.source.entry_id = id;
                    plan.source.value = &entry.value;
                    ++result->source_properties;
                } else if (path.starts_with(plan.spec->path)
                           && path.size() > plan.spec->path.size()
                           && (path[plan.spec->path.size()] == '/'
                               || path[plan.spec->path.size()] == '[')) {
                    result->status = Status::UnsupportedSourceShape;
                    result->failed_mapping = plan.spec->mapping;
                    result->failed_source_entry = id;
                    return result->status;
                }
            }
        }
        return Status::Ok;
    }

    static Status reconcile(const MetaStore& source,
                            const MetadataProfileTranslationOptions& options,
                            FieldPlan* plan,
                            MetadataCaptureTranslationResult* result) noexcept
    {
        if (!plan->source.found)
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
            result->failed_mapping = plan->spec->mapping;
            result->failed_source_entry = plan->source_entry;
            return Status::NativeConflict;
        }
        plan->apply = true;
        return Status::Ok;
    }

}  // namespace

MetadataCaptureTranslationResult
translate_xmp_profile_metadata(const MetaStore& source,
                               const MetadataProfileTranslationOptions& options,
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
        || options.max_added_entries == 0U
        || options.max_added_entries > kMetadataProfileTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataProfileTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataProfileTranslationMaxTotalTextBytes
        || (!options.image_description_to_exif && !options.artist_to_exif
            && !options.copyright_to_exif && !options.color_space_to_exif
            && !options.related_sound_file_to_exif))
        return error(Status::InvalidOptions);

    std::array<FieldPlan, 5U> plans {};
    for (size_t i = 0U; i < plans.size(); ++i) {
        plans[i].spec = &kFields[i];
    }
    MetadataCaptureTranslationResult result;
    Status status = collect_sources(source, options, &plans, &result);
    if (status != Status::Ok)
        return result;
    uint64_t total_text = 0U;
    bool selected = false;
    for (FieldPlan& plan : plans) {
        if (!plan.spec || !(options.*plan.spec->enabled))
            continue;
        status = prepare_plan(source, options, &plan, &total_text);
        if (status != Status::Ok) {
            result.status = status;
            result.failed_mapping = plan.spec->mapping;
            result.failed_source_entry = plan.source_entry;
            return result;
        }
        selected = selected || plan.source.found;
    }
    if (total_text > options.max_total_text_bytes) {
        result.status = Status::SourceLimitExceeded;
        return result;
    }
    if (!selected) {
        *out_store = source;
        return result;
    }
    for (FieldPlan& plan : plans) {
        if (!plan.spec || !(options.*plan.spec->enabled))
            continue;
        status = reconcile(source, options, &plan, &result);
        if (status != Status::Ok) {
            result.status = status;
            return result;
        }
    }
    uint32_t additions = 0U;
    uint64_t operations = 0U;
    for (const FieldPlan& plan : plans) {
        if (!plan.apply)
            continue;
        operations += plan.native.size();
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
    MetaEdit edit;
    edit.reserve_ops(static_cast<size_t>(operations));
    for (const FieldPlan& plan : plans) {
        if (!plan.apply)
            continue;
        bool written = false;
        MetaValue value;
        uint32_t wire_count = 0U;
        if (plan.present) {
            if (plan.spec->kind == FieldKind::ColorSpace) {
                value = make_u16(plan.scalar);
                wire_count = 1U;
            } else {
                value = make_text(edit.arena(), plan.text,
                                  plan.wire_type == 2U ? TextEncoding::Ascii
                                                      : TextEncoding::Utf8);
                wire_count = value.count + 1U;
            }
        }
        for (EntryId id : plan.native) {
            if (plan.present && !written) {
                edit.set_value(id, value, { WireFamily::Tiff, plan.wire_type },
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
            entry.key = make_exif_tag_key(edit.arena(), plan.spec->ifd,
                                          plan.spec->tag);
            entry.value = value;
            entry.origin.wire_type = { WireFamily::Tiff, plan.wire_type };
            entry.origin.wire_count = wire_count;
            entry.flags = EntryFlags::Dirty;
            edit.add_entry(entry);
            ++result.entries_added;
        }
        ++result.groups_translated;
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
    result.failed_mapping = Mapping::None;
    result.failed_source_entry = kInvalidEntryId;
    return result;
}

}  // namespace openmeta
