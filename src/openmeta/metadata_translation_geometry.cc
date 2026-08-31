// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_translation.h"

#include "openmeta/meta_edit.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/orientation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr std::string_view kXmpNsExif
        = "http://ns.adobe.com/exif/1.0/";
    static constexpr std::string_view kXmpNsTiff
        = "http://ns.adobe.com/tiff/1.0/";

    enum class NativeGeometryField : uint8_t {
        ImageWidth,
        ImageLength,
        PixelXDimension,
        PixelYDimension,
        Orientation,
    };

    struct GeometrySourceAlias final {
        std::string_view schema_ns;
        std::string_view property_path;
    };

    struct GeometrySourceSet final {
        std::array<EntryId, 4U> active {};
        std::array<EntryId, 4U> deleted {};
        uint8_t active_count  = 0U;
        uint8_t deleted_count = 0U;
    };

    struct GeometryPlannedGroup final {
        MetadataGeometryTranslationMapping mapping
            = MetadataGeometryTranslationMapping::None;
        std::array<NativeGeometryField, 4U> fields {};
        std::array<MetaValue, 4U> values {};
        uint8_t field_count  = 0U;
        EntryId source_entry = kInvalidEntryId;
        bool present         = false;
        bool existing_any    = false;
        bool exact_match     = false;
        bool apply           = false;
    };

    static std::string_view arena_text(const ByteArena& arena,
                                       ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool xmp_alias_matches(const MetaStore& store, const Entry& entry,
                                  const GeometrySourceAlias& alias) noexcept
    {
        return entry.key.kind == MetaKeyKind::XmpProperty
               && arena_text(store.arena(),
                             entry.key.data.xmp_property.schema_ns)
                      == alias.schema_ns
               && arena_text(store.arena(),
                             entry.key.data.xmp_property.property_path)
                      == alias.property_path;
    }

    static bool entry_is_eligible(const Entry& entry,
                                  MetadataGeometryTranslationSourceMode mode,
                                  bool include_clean_group_members) noexcept
    {
        const bool dirty   = any(entry.flags, EntryFlags::Dirty);
        const bool deleted = any(entry.flags, EntryFlags::Deleted);
        if (deleted && !dirty) {
            return false;
        }
        return mode == MetadataGeometryTranslationSourceMode::All || dirty
               || include_clean_group_members;
    }

    static bool group_has_dirty_source(
        const MetaStore& store,
        std::span<const GeometrySourceAlias> aliases) noexcept
    {
        for (const Entry& entry : store.entries()) {
            if (!any(entry.flags, EntryFlags::Dirty)) {
                continue;
            }
            for (const GeometrySourceAlias& alias : aliases) {
                if (xmp_alias_matches(store, entry, alias)) {
                    return true;
                }
            }
        }
        return false;
    }

    static MetadataGeometryTranslationStatus
    collect_sources(const MetaStore& store,
                    std::span<const GeometrySourceAlias> aliases,
                    MetadataGeometryTranslationSourceMode mode,
                    bool include_clean_group_members, GeometrySourceSet* out,
                    MetadataGeometryTranslationResult* result) noexcept
    {
        if (!out || !result || aliases.size() > out->active.size()) {
            return MetadataGeometryTranslationStatus::InternalError;
        }
        *out = GeometrySourceSet {};
        std::array<bool, 4U> seen {};
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            for (size_t alias_index = 0U; alias_index < aliases.size();
                 ++alias_index) {
                if (!xmp_alias_matches(store, entry, aliases[alias_index])
                    || !entry_is_eligible(entry, mode,
                                          include_clean_group_members)) {
                    continue;
                }
                if (seen[alias_index]) {
                    result->failed_source_entry = id;
                    return MetadataGeometryTranslationStatus::AmbiguousSource;
                }
                seen[alias_index] = true;
                ++result->source_properties;
                if (any(entry.flags, EntryFlags::Deleted)) {
                    out->deleted[out->deleted_count++] = id;
                } else {
                    out->active[out->active_count++] = id;
                }
            }
        }
        return MetadataGeometryTranslationStatus::Ok;
    }

    static MetadataGeometryTranslationStatus
    parse_u32_source(const MetaStore& source, EntryId entry_id,
                     const MetadataGeometryTranslationOptions& options,
                     uint64_t* total_text_bytes, uint32_t* out) noexcept
    {
        if (!out || !total_text_bytes || entry_id >= source.entries().size()) {
            return MetadataGeometryTranslationStatus::InternalError;
        }
        const MetaValue& value = source.entry(entry_id).value;
        uint64_t parsed        = 0U;
        if (value.kind == MetaValueKind::Scalar) {
            switch (value.elem_type) {
            case MetaElementType::U8:
            case MetaElementType::U16:
            case MetaElementType::U32:
            case MetaElementType::U64: parsed = value.data.u64; break;
            default:
                return MetadataGeometryTranslationStatus::InvalidSourceValue;
            }
        } else if (value.kind == MetaValueKind::Text) {
            const std::string_view text = arena_text(source.arena(),
                                                     value.data.span);
            if (text.size() > options.max_text_bytes_per_property) {
                return MetadataGeometryTranslationStatus::ValueTooLong;
            }
            if (text.size() > options.max_total_text_bytes
                || *total_text_bytes
                       > options.max_total_text_bytes - text.size()) {
                return MetadataGeometryTranslationStatus::SourceLimitExceeded;
            }
            *total_text_bytes += text.size();
            if (text.empty()) {
                return MetadataGeometryTranslationStatus::InvalidNumericValue;
            }
            for (const char c : text) {
                if (c < '0' || c > '9') {
                    return MetadataGeometryTranslationStatus::InvalidNumericValue;
                }
                const uint64_t digit = static_cast<uint64_t>(c - '0');
                if (parsed
                    > (std::numeric_limits<uint32_t>::max() - digit) / 10U) {
                    return MetadataGeometryTranslationStatus::ValueOutOfRange;
                }
                parsed = parsed * 10U + digit;
            }
        } else {
            return MetadataGeometryTranslationStatus::InvalidSourceValue;
        }
        if (parsed == 0U || parsed > std::numeric_limits<uint32_t>::max()) {
            return MetadataGeometryTranslationStatus::ValueOutOfRange;
        }
        *out = static_cast<uint32_t>(parsed);
        return MetadataGeometryTranslationStatus::Ok;
    }

    static MetadataGeometryTranslationStatus
    parse_consistent_sources(const MetaStore& source,
                             const GeometrySourceSet& sources,
                             const MetadataGeometryTranslationOptions& options,
                             uint64_t* total_text_bytes, uint32_t* out,
                             EntryId* failed_source_entry) noexcept
    {
        if (!out || !failed_source_entry || sources.active_count == 0U) {
            return MetadataGeometryTranslationStatus::InternalError;
        }
        uint32_t expected = 0U;
        for (uint8_t i = 0U; i < sources.active_count; ++i) {
            uint32_t value = 0U;
            const MetadataGeometryTranslationStatus status
                = parse_u32_source(source, sources.active[i], options,
                                   total_text_bytes, &value);
            if (status != MetadataGeometryTranslationStatus::Ok) {
                *failed_source_entry = sources.active[i];
                return status;
            }
            if (i == 0U) {
                expected = value;
            } else if (value != expected) {
                *failed_source_entry = sources.active[i];
                return MetadataGeometryTranslationStatus::AmbiguousSource;
            }
        }
        *out = expected;
        return MetadataGeometryTranslationStatus::Ok;
    }

    static bool native_field_matches(const MetaStore& store, const Entry& entry,
                                     NativeGeometryField field) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        std::string_view ifd;
        uint16_t tag = 0U;
        switch (field) {
        case NativeGeometryField::ImageWidth:
            ifd = "ifd0";
            tag = 0x0100U;
            break;
        case NativeGeometryField::ImageLength:
            ifd = "ifd0";
            tag = 0x0101U;
            break;
        case NativeGeometryField::PixelXDimension:
            ifd = "exififd";
            tag = 0xA002U;
            break;
        case NativeGeometryField::PixelYDimension:
            ifd = "exififd";
            tag = 0xA003U;
            break;
        case NativeGeometryField::Orientation:
            ifd = "ifd0";
            tag = 0x0112U;
            break;
        }
        return entry.key.data.exif_tag.tag == tag
               && arena_text(store.arena(), entry.key.data.exif_tag.ifd) == ifd;
    }

    static bool native_value_matches(const MetaValue& actual,
                                     const MetaValue& expected) noexcept
    {
        return actual.kind == MetaValueKind::Scalar
               && expected.kind == MetaValueKind::Scalar
               && actual.elem_type == expected.elem_type
               && actual.data.u64 == expected.data.u64;
    }

    static void analyze_group(const MetaStore& source,
                              GeometryPlannedGroup* group) noexcept
    {
        if (!group) {
            return;
        }
        bool complete_exact = true;
        for (uint8_t field_index = 0U; field_index < group->field_count;
             ++field_index) {
            uint32_t active_count = 0U;
            bool first_exact      = false;
            for (const Entry& entry : source.entries()) {
                if (any(entry.flags, EntryFlags::Deleted)
                    || !native_field_matches(source, entry,
                                             group->fields[field_index])) {
                    continue;
                }
                ++active_count;
                if (active_count == 1U && group->present) {
                    first_exact
                        = native_value_matches(entry.value,
                                               group->values[field_index]);
                }
            }
            group->existing_any = group->existing_any || active_count > 0U;
            if (group->present) {
                complete_exact = complete_exact && active_count == 1U
                                 && first_exact;
            } else {
                complete_exact = complete_exact && active_count == 0U;
            }
        }
        group->exact_match = complete_exact;
    }

    static uint32_t missing_entries(const MetaStore& source,
                                    const GeometryPlannedGroup& group) noexcept
    {
        if (!group.present) {
            return 0U;
        }
        uint32_t missing = 0U;
        for (uint8_t field_index = 0U; field_index < group.field_count;
             ++field_index) {
            bool found = false;
            for (const Entry& entry : source.entries()) {
                if (!any(entry.flags, EntryFlags::Deleted)
                    && native_field_matches(source, entry,
                                            group.fields[field_index])) {
                    found = true;
                    break;
                }
            }
            missing += found ? 0U : 1U;
        }
        return missing;
    }

    static uint32_t
    required_operations(const MetaStore& source,
                        const GeometryPlannedGroup& group) noexcept
    {
        uint32_t operations = 0U;
        for (uint8_t field_index = 0U; field_index < group.field_count;
             ++field_index) {
            uint32_t active_count = 0U;
            bool first_exact      = false;
            for (const Entry& entry : source.entries()) {
                if (any(entry.flags, EntryFlags::Deleted)
                    || !native_field_matches(source, entry,
                                             group.fields[field_index])) {
                    continue;
                }
                ++active_count;
                if (active_count == 1U && group.present) {
                    first_exact
                        = native_value_matches(entry.value,
                                               group.values[field_index]);
                }
            }
            if (!group.present) {
                operations += active_count;
            } else if (active_count == 0U) {
                ++operations;
            } else {
                operations += active_count - 1U + (first_exact ? 0U : 1U);
            }
        }
        return operations;
    }

    static MetaKey make_native_key(ByteArena& arena,
                                   NativeGeometryField field) noexcept
    {
        switch (field) {
        case NativeGeometryField::ImageWidth:
            return make_exif_tag_key(arena, "ifd0", 0x0100U);
        case NativeGeometryField::ImageLength:
            return make_exif_tag_key(arena, "ifd0", 0x0101U);
        case NativeGeometryField::PixelXDimension:
            return make_exif_tag_key(arena, "exififd", 0xA002U);
        case NativeGeometryField::PixelYDimension:
            return make_exif_tag_key(arena, "exififd", 0xA003U);
        case NativeGeometryField::Orientation:
            return make_exif_tag_key(arena, "ifd0", 0x0112U);
        }
        return MetaKey {};
    }

    static bool append_native_entry(MetaEdit* edit, const MetaStore& source,
                                    const GeometryPlannedGroup& group,
                                    uint8_t field_index) noexcept
    {
        if (!edit || field_index >= group.field_count
            || group.source_entry >= source.entries().size()) {
            return false;
        }
        Entry entry;
        entry.key   = make_native_key(edit->arena(), group.fields[field_index]);
        entry.value = group.values[field_index];
        entry.origin = source.entry(group.source_entry).origin;
        if (entry.origin.wire_type_name.size > 0U) {
            entry.origin.wire_type_name = edit->arena().append(
                source.arena().span(entry.origin.wire_type_name));
        }
        const uint32_t offset = static_cast<uint32_t>(field_index) + 1U;
        if (entry.origin.order_in_block
            <= std::numeric_limits<uint32_t>::max() - offset) {
            entry.origin.order_in_block += offset;
        }
        entry.flags = EntryFlags::Dirty;
        if (edit->arena().limit_exceeded()) {
            return false;
        }
        edit->add_entry(entry);
        return true;
    }

    static void apply_group(const MetaStore& source,
                            const GeometryPlannedGroup& group, MetaEdit* edit,
                            MetadataGeometryTranslationResult* result)
    {
        if (!edit || !result || !group.apply) {
            return;
        }
        const std::span<const Entry> entries = source.entries();
        for (uint8_t field_index = 0U; field_index < group.field_count;
             ++field_index) {
            EntryId first_active = kInvalidEntryId;
            for (EntryId id = 0U; id < entries.size(); ++id) {
                const Entry& entry = entries[id];
                if (any(entry.flags, EntryFlags::Deleted)
                    || !native_field_matches(source, entry,
                                             group.fields[field_index])) {
                    continue;
                }
                if (!group.present || first_active != kInvalidEntryId) {
                    edit->tombstone(id);
                    ++result->entries_removed;
                    continue;
                }
                first_active = id;
                if (!native_value_matches(entry.value,
                                          group.values[field_index])) {
                    edit->set_value(id, group.values[field_index]);
                    ++result->entries_updated;
                }
            }
            if (group.present && first_active == kInvalidEntryId
                && append_native_entry(edit, source, group, field_index)) {
                ++result->entries_added;
            }
        }
        ++result->groups_translated;
    }

    static MetadataGeometryTranslationResult
    geometry_error(MetadataGeometryTranslationStatus status) noexcept
    {
        MetadataGeometryTranslationResult result;
        result.status = status;
        return result;
    }

    static MetadataGeometryTranslationStatus
    append_orientation_group(const MetaStore& source,
                             const TransferTargetImageSpec& target,
                             const MetadataGeometryTranslationOptions& options,
                             std::array<GeometryPlannedGroup, 2U>* groups,
                             uint8_t* group_count, uint64_t* total_text_bytes,
                             MetadataGeometryTranslationResult* result) noexcept
    {
        static constexpr std::array<GeometrySourceAlias, 1U> kAliases = {
            GeometrySourceAlias { kXmpNsTiff, "Orientation" },
        };
        if (!groups || !group_count || !total_text_bytes || !result
            || *group_count >= groups->size()) {
            return MetadataGeometryTranslationStatus::InternalError;
        }
        GeometrySourceSet sources;
        MetadataGeometryTranslationStatus status
            = collect_sources(source, kAliases, options.source_mode, false,
                              &sources, result);
        if (status != MetadataGeometryTranslationStatus::Ok) {
            result->failed_mapping
                = MetadataGeometryTranslationMapping::XmpOrientation;
            return status;
        }
        if (sources.active_count == 0U && sources.deleted_count == 0U) {
            return MetadataGeometryTranslationStatus::Ok;
        }
        if (sources.active_count > 0U && sources.deleted_count > 0U) {
            result->failed_mapping
                = MetadataGeometryTranslationMapping::XmpOrientation;
            result->failed_source_entry = sources.deleted[0U];
            return MetadataGeometryTranslationStatus::AmbiguousSource;
        }

        GeometryPlannedGroup group;
        group.mapping      = MetadataGeometryTranslationMapping::XmpOrientation;
        group.fields[0U]   = NativeGeometryField::Orientation;
        group.field_count  = 1U;
        group.source_entry = sources.active_count > 0U ? sources.active[0U]
                                                       : sources.deleted[0U];
        if (sources.active_count > 0U) {
            uint32_t orientation = 0U;
            status = parse_u32_source(source, sources.active[0U], options,
                                      total_text_bytes, &orientation);
            if (status != MetadataGeometryTranslationStatus::Ok) {
                result->failed_mapping      = group.mapping;
                result->failed_source_entry = sources.active[0U];
                return status;
            }
            if (orientation > std::numeric_limits<uint16_t>::max()
                || !exif_orientation_is_valid(
                    static_cast<uint16_t>(orientation))) {
                result->failed_mapping      = group.mapping;
                result->failed_source_entry = sources.active[0U];
                return MetadataGeometryTranslationStatus::ValueOutOfRange;
            }
            if (!target.has_orientation) {
                result->failed_mapping      = group.mapping;
                result->failed_source_entry = sources.active[0U];
                return MetadataGeometryTranslationStatus::TargetImageSpecRequired;
            }
            if (target.orientation != orientation) {
                result->failed_mapping      = group.mapping;
                result->failed_source_entry = sources.active[0U];
                return MetadataGeometryTranslationStatus::TargetImageSpecMismatch;
            }
            group.present    = true;
            group.values[0U] = make_u16(static_cast<uint16_t>(orientation));
        } else if (target.has_orientation) {
            result->failed_mapping      = group.mapping;
            result->failed_source_entry = sources.deleted[0U];
            return MetadataGeometryTranslationStatus::TargetImageSpecMismatch;
        }
        (*groups)[(*group_count)++] = group;
        return MetadataGeometryTranslationStatus::Ok;
    }

    static MetadataGeometryTranslationStatus
    append_dimensions_group(const MetaStore& source,
                            const TransferTargetImageSpec& target,
                            const MetadataGeometryTranslationOptions& options,
                            std::array<GeometryPlannedGroup, 2U>* groups,
                            uint8_t* group_count, uint64_t* total_text_bytes,
                            MetadataGeometryTranslationResult* result) noexcept
    {
        static constexpr std::array<GeometrySourceAlias, 3U> kWidthAliases = {
            GeometrySourceAlias { kXmpNsTiff, "ImageWidth" },
            GeometrySourceAlias { kXmpNsExif, "ExifImageWidth" },
            GeometrySourceAlias { kXmpNsExif, "PixelXDimension" },
        };
        static constexpr std::array<GeometrySourceAlias, 4U> kHeightAliases = {
            GeometrySourceAlias { kXmpNsTiff, "ImageLength" },
            GeometrySourceAlias { kXmpNsTiff, "ImageHeight" },
            GeometrySourceAlias { kXmpNsExif, "ExifImageHeight" },
            GeometrySourceAlias { kXmpNsExif, "PixelYDimension" },
        };
        static constexpr std::array<GeometrySourceAlias, 7U> kAllAliases = {
            kWidthAliases[0U],  kWidthAliases[1U],  kWidthAliases[2U],
            kHeightAliases[0U], kHeightAliases[1U], kHeightAliases[2U],
            kHeightAliases[3U],
        };
        if (!groups || !group_count || !total_text_bytes || !result
            || *group_count >= groups->size()) {
            return MetadataGeometryTranslationStatus::InternalError;
        }
        const bool include_clean_group_members
            = options.source_mode == MetadataGeometryTranslationSourceMode::All
              || group_has_dirty_source(source, kAllAliases);
        if (!include_clean_group_members) {
            return MetadataGeometryTranslationStatus::Ok;
        }

        GeometrySourceSet widths;
        GeometrySourceSet heights;
        MetadataGeometryTranslationStatus status
            = collect_sources(source, kWidthAliases, options.source_mode, true,
                              &widths, result);
        if (status == MetadataGeometryTranslationStatus::Ok) {
            status = collect_sources(source, kHeightAliases,
                                     options.source_mode, true, &heights,
                                     result);
        }
        if (status != MetadataGeometryTranslationStatus::Ok) {
            result->failed_mapping
                = MetadataGeometryTranslationMapping::XmpDimensions;
            return status;
        }

        const bool active = widths.active_count > 0U
                            || heights.active_count > 0U;
        const bool deleted = widths.deleted_count > 0U
                             || heights.deleted_count > 0U;
        if (!active && !deleted) {
            return MetadataGeometryTranslationStatus::Ok;
        }
        const bool complete_active = widths.active_count > 0U
                                     && heights.active_count > 0U
                                     && widths.deleted_count == 0U
                                     && heights.deleted_count == 0U;
        const bool complete_deleted = widths.active_count == 0U
                                      && heights.active_count == 0U
                                      && widths.deleted_count > 0U
                                      && heights.deleted_count > 0U;
        if (!complete_active && !complete_deleted) {
            result->failed_mapping
                = MetadataGeometryTranslationMapping::XmpDimensions;
            if (widths.deleted_count > 0U) {
                result->failed_source_entry = widths.deleted[0U];
            } else if (heights.deleted_count > 0U) {
                result->failed_source_entry = heights.deleted[0U];
            } else if (widths.active_count > 0U) {
                result->failed_source_entry = widths.active[0U];
            } else {
                result->failed_source_entry = heights.active[0U];
            }
            return MetadataGeometryTranslationStatus::IncompleteSourceGroup;
        }

        GeometryPlannedGroup group;
        group.mapping      = MetadataGeometryTranslationMapping::XmpDimensions;
        group.fields       = { NativeGeometryField::ImageWidth,
                               NativeGeometryField::ImageLength,
                               NativeGeometryField::PixelXDimension,
                               NativeGeometryField::PixelYDimension };
        group.field_count  = 4U;
        group.source_entry = complete_active ? widths.active[0U]
                                             : widths.deleted[0U];
        if (complete_active) {
            uint32_t width  = 0U;
            uint32_t height = 0U;
            status          = parse_consistent_sources(source, widths, options,
                                                       total_text_bytes, &width,
                                                       &result->failed_source_entry);
            if (status == MetadataGeometryTranslationStatus::Ok) {
                status = parse_consistent_sources(source, heights, options,
                                                  total_text_bytes, &height,
                                                  &result->failed_source_entry);
            }
            if (status != MetadataGeometryTranslationStatus::Ok) {
                result->failed_mapping = group.mapping;
                return status;
            }
            if (!target.has_dimensions) {
                result->failed_mapping      = group.mapping;
                result->failed_source_entry = group.source_entry;
                return MetadataGeometryTranslationStatus::TargetImageSpecRequired;
            }
            if (target.width != width || target.height != height) {
                result->failed_mapping      = group.mapping;
                result->failed_source_entry = group.source_entry;
                return MetadataGeometryTranslationStatus::TargetImageSpecMismatch;
            }
            group.present = true;
            group.values = { make_u32(width), make_u32(height), make_u32(width),
                             make_u32(height) };
        } else if (target.has_dimensions) {
            result->failed_mapping      = group.mapping;
            result->failed_source_entry = group.source_entry;
            return MetadataGeometryTranslationStatus::TargetImageSpecMismatch;
        }
        (*groups)[(*group_count)++] = group;
        return MetadataGeometryTranslationStatus::Ok;
    }

}  // namespace

MetadataGeometryTranslationResult
translate_xmp_image_geometry(const MetaStore& source,
                             const TransferTargetImageSpec& target_image_spec,
                             const MetadataGeometryTranslationOptions& options,
                             MetaStore* out_store)
{
    if (!out_store) {
        return geometry_error(MetadataGeometryTranslationStatus::NullOutput);
    }
    if (!source.is_finalized()) {
        return geometry_error(
            MetadataGeometryTranslationStatus::SourceNotFinalized);
    }
    if (options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataGeometryTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataGeometryTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataGeometryTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataGeometryTranslationMaxTotalTextBytes
        || (options.source_mode
                != MetadataGeometryTranslationSourceMode::DirtyOnly
            && options.source_mode != MetadataGeometryTranslationSourceMode::All)
        || (options.conflict_policy
                != MetadataGeometryTranslationConflictPolicy::PreserveExisting
            && options.conflict_policy
                   != MetadataGeometryTranslationConflictPolicy::FailOnConflict
            && options.conflict_policy
                   != MetadataGeometryTranslationConflictPolicy::ReplaceExisting)
        || (!options.orientation_to_exif && !options.dimensions_to_exif)) {
        return geometry_error(
            MetadataGeometryTranslationStatus::InvalidOptions);
    }
    if ((options.dimensions_to_exif && target_image_spec.has_dimensions
         && (target_image_spec.width == 0U || target_image_spec.height == 0U))
        || (options.orientation_to_exif && target_image_spec.has_orientation
            && !exif_orientation_is_valid(target_image_spec.orientation))) {
        return geometry_error(
            MetadataGeometryTranslationStatus::InvalidTargetImageSpec);
    }

    std::array<GeometryPlannedGroup, 2U> groups {};
    uint8_t group_count       = 0U;
    uint64_t total_text_bytes = 0U;
    MetadataGeometryTranslationResult result;
    MetadataGeometryTranslationStatus status
        = MetadataGeometryTranslationStatus::Ok;
    if (options.orientation_to_exif) {
        status = append_orientation_group(source, target_image_spec, options,
                                          &groups, &group_count,
                                          &total_text_bytes, &result);
    }
    if (status == MetadataGeometryTranslationStatus::Ok
        && options.dimensions_to_exif) {
        status = append_dimensions_group(source, target_image_spec, options,
                                         &groups, &group_count,
                                         &total_text_bytes, &result);
    }
    if (status != MetadataGeometryTranslationStatus::Ok) {
        result.status = status;
        return result;
    }

    uint32_t added_entries   = 0U;
    uint32_t operation_count = 0U;
    for (uint8_t i = 0U; i < group_count; ++i) {
        GeometryPlannedGroup& group = groups[i];
        analyze_group(source, &group);
        switch (options.conflict_policy) {
        case MetadataGeometryTranslationConflictPolicy::PreserveExisting:
            if (group.existing_any) {
                ++result.groups_preserved;
            } else if (group.exact_match) {
                ++result.groups_unchanged;
            } else {
                group.apply = true;
            }
            break;
        case MetadataGeometryTranslationConflictPolicy::FailOnConflict:
            if (group.existing_any && !group.exact_match) {
                result.status
                    = MetadataGeometryTranslationStatus::NativeConflict;
                result.failed_mapping      = group.mapping;
                result.failed_source_entry = group.source_entry;
                return result;
            }
            if (group.exact_match) {
                ++result.groups_unchanged;
            } else {
                group.apply = true;
            }
            break;
        case MetadataGeometryTranslationConflictPolicy::ReplaceExisting:
            if (group.exact_match) {
                ++result.groups_unchanged;
            } else {
                group.apply = true;
            }
            break;
        }
        if (group.apply) {
            added_entries += missing_entries(source, group);
            operation_count += required_operations(source, group);
        }
    }
    if (added_entries > options.max_added_entries
        || source.entries().size() > static_cast<size_t>(kInvalidEntryId)
        || static_cast<size_t>(added_entries)
               > static_cast<size_t>(kInvalidEntryId)
                     - source.entries().size()) {
        result.status = MetadataGeometryTranslationStatus::EntryLimitExceeded;
        return result;
    }
    if (operation_count > options.max_operations) {
        result.status
            = MetadataGeometryTranslationStatus::OperationLimitExceeded;
        return result;
    }

    MetaEdit edit;
    edit.reserve_ops(operation_count);
    for (uint8_t i = 0U; i < group_count; ++i) {
        apply_group(source, groups[i], &edit, &result);
    }
    if (edit.ops().size() != operation_count || edit.arena().limit_exceeded()
        || result.entries_added != added_entries) {
        result.status = MetadataGeometryTranslationStatus::InternalError;
        return result;
    }
    *out_store = commit(source, std::span<const MetaEdit>(&edit, 1U));
    return result;
}

const char*
metadata_geometry_translation_status_name(
    MetadataGeometryTranslationStatus status) noexcept
{
    switch (status) {
    case MetadataGeometryTranslationStatus::Ok: return "ok";
    case MetadataGeometryTranslationStatus::NullOutput: return "null_output";
    case MetadataGeometryTranslationStatus::SourceNotFinalized:
        return "source_not_finalized";
    case MetadataGeometryTranslationStatus::InvalidOptions:
        return "invalid_options";
    case MetadataGeometryTranslationStatus::InvalidTargetImageSpec:
        return "invalid_target_image_spec";
    case MetadataGeometryTranslationStatus::TargetImageSpecRequired:
        return "target_image_spec_required";
    case MetadataGeometryTranslationStatus::TargetImageSpecMismatch:
        return "target_image_spec_mismatch";
    case MetadataGeometryTranslationStatus::AmbiguousSource:
        return "ambiguous_source";
    case MetadataGeometryTranslationStatus::IncompleteSourceGroup:
        return "incomplete_source_group";
    case MetadataGeometryTranslationStatus::InvalidSourceValue:
        return "invalid_source_value";
    case MetadataGeometryTranslationStatus::InvalidNumericValue:
        return "invalid_numeric_value";
    case MetadataGeometryTranslationStatus::ValueOutOfRange:
        return "value_out_of_range";
    case MetadataGeometryTranslationStatus::ValueTooLong:
        return "value_too_long";
    case MetadataGeometryTranslationStatus::SourceLimitExceeded:
        return "source_limit_exceeded";
    case MetadataGeometryTranslationStatus::NativeConflict:
        return "native_conflict";
    case MetadataGeometryTranslationStatus::EntryLimitExceeded:
        return "entry_limit_exceeded";
    case MetadataGeometryTranslationStatus::OperationLimitExceeded:
        return "operation_limit_exceeded";
    case MetadataGeometryTranslationStatus::InternalError:
        return "internal_error";
    }
    return "unknown";
}

const char*
metadata_geometry_translation_mapping_name(
    MetadataGeometryTranslationMapping mapping) noexcept
{
    switch (mapping) {
    case MetadataGeometryTranslationMapping::None: return "none";
    case MetadataGeometryTranslationMapping::XmpOrientation:
        return "xmp_orientation";
    case MetadataGeometryTranslationMapping::XmpDimensions:
        return "xmp_dimensions";
    }
    return "unknown";
}

}  // namespace openmeta
