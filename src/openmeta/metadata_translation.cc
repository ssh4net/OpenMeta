// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_translation.h"

#include "openmeta/meta_edit.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

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
    static constexpr std::string_view kXmpNsPhotoshop
        = "http://ns.adobe.com/photoshop/1.0/";
    static constexpr std::string_view kXmpNsXmp = "http://ns.adobe.com/xap/1.0/";

    static std::string_view arena_text(const ByteArena& arena,
                                       ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    enum class NativeDateField : uint8_t {
        ExifDateTimeOriginal,
        ExifDateTimeDigitized,
        ExifOffsetTimeOriginal,
        ExifOffsetTimeDigitized,
        ExifSubSecTimeOriginal,
        ExifSubSecTimeDigitized,
        IptcDateCreated,
        IptcTimeCreated,
        IptcDigitalCreationDate,
        IptcDigitalCreationTime,
    };

    struct ParsedXmpDateTime final {
        uint16_t year      = 0U;
        uint8_t month      = 0U;
        uint8_t day        = 0U;
        uint8_t hour       = 0U;
        uint8_t minute     = 0U;
        uint8_t second     = 0U;
        bool has_time      = false;
        bool has_subsecond = false;
        std::array<char, 9U> subsecond {};
        uint8_t subsecond_size   = 0U;
        bool has_utc_offset      = false;
        bool utc_offset_negative = false;
        int16_t utc_offset_min   = 0;
    };

    struct PlannedField final {
        NativeDateField field = NativeDateField::ExifDateTimeOriginal;
        bool present          = false;
        std::array<char, 32U> value {};
        uint8_t value_size = 0U;
    };

    struct PlannedGroup final {
        MetadataDateTranslationMapping mapping
            = MetadataDateTranslationMapping::None;
        EntryId source_entry = kInvalidEntryId;
        std::array<PlannedField, 3U> fields {};
        uint8_t field_count = 0U;
        bool existing_any   = false;
        bool exact_match    = false;
        bool apply          = false;
    };

    struct SourceProperty final {
        bool found       = false;
        bool deleted     = false;
        EntryId entry_id = kInvalidEntryId;
        std::string_view text;
    };

    static bool ascii_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    static uint32_t decimal_digits(std::string_view text, size_t offset,
                                   size_t count) noexcept
    {
        uint32_t value = 0U;
        for (size_t i = 0U; i < count; ++i) {
            value = value * 10U + static_cast<uint32_t>(text[offset + i] - '0');
        }
        return value;
    }

    static bool valid_date(uint32_t year, uint32_t month, uint32_t day) noexcept
    {
        if (year == 0U || year > 9999U || month == 0U || month > 12U) {
            return false;
        }
        static constexpr std::array<uint8_t, 12U> kDaysInMonth = {
            31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
        };
        uint32_t max_day = kDaysInMonth[month - 1U];
        const bool leap  = (year % 4U == 0U)
                          && ((year % 100U) != 0U || (year % 400U) == 0U);
        if (month == 2U && leap) {
            max_day = 29U;
        }
        return day > 0U && day <= max_day;
    }

    static bool parse_xmp_datetime(std::string_view text,
                                   ParsedXmpDateTime* out) noexcept
    {
        if (!out || text.size() < 10U || text[4] != '-' || text[7] != '-') {
            return false;
        }
        static constexpr std::array<size_t, 8U> kDateDigitPositions = {
            0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U,
        };
        for (const size_t position : kDateDigitPositions) {
            if (!ascii_digit(text[position])) {
                return false;
            }
        }

        ParsedXmpDateTime parsed;
        const uint32_t year  = decimal_digits(text, 0U, 4U);
        const uint32_t month = decimal_digits(text, 5U, 2U);
        const uint32_t day   = decimal_digits(text, 8U, 2U);
        if (!valid_date(year, month, day)) {
            return false;
        }
        parsed.year  = static_cast<uint16_t>(year);
        parsed.month = static_cast<uint8_t>(month);
        parsed.day   = static_cast<uint8_t>(day);

        if (text.size() == 10U) {
            *out = parsed;
            return true;
        }
        if (text.size() < 19U || text[10] != 'T' || text[13] != ':'
            || text[16] != ':') {
            return false;
        }
        static constexpr std::array<size_t, 6U> kTimeDigitPositions = {
            11U, 12U, 14U, 15U, 17U, 18U,
        };
        for (const size_t position : kTimeDigitPositions) {
            if (!ascii_digit(text[position])) {
                return false;
            }
        }
        const uint32_t hour   = decimal_digits(text, 11U, 2U);
        const uint32_t minute = decimal_digits(text, 14U, 2U);
        const uint32_t second = decimal_digits(text, 17U, 2U);
        if (hour > 23U || minute > 59U || second > 60U) {
            return false;
        }
        parsed.has_time = true;
        parsed.hour     = static_cast<uint8_t>(hour);
        parsed.minute   = static_cast<uint8_t>(minute);
        parsed.second   = static_cast<uint8_t>(second);

        size_t position = 19U;
        if (position < text.size() && text[position] == '.') {
            ++position;
            const size_t fraction_begin = position;
            while (position < text.size() && ascii_digit(text[position])) {
                ++position;
            }
            const size_t fraction_size = position - fraction_begin;
            if (fraction_size == 0U
                || fraction_size > parsed.subsecond.size()) {
                return false;
            }
            parsed.has_subsecond  = true;
            parsed.subsecond_size = static_cast<uint8_t>(fraction_size);
            for (size_t i = 0U; i < fraction_size; ++i) {
                parsed.subsecond[i] = text[fraction_begin + i];
            }
        }

        if (position == text.size()) {
            *out = parsed;
            return true;
        }
        if (text[position] == 'Z') {
            if (position + 1U != text.size()) {
                return false;
            }
            parsed.has_utc_offset = true;
            parsed.utc_offset_min = 0;
            *out                  = parsed;
            return true;
        }
        if ((text[position] != '+' && text[position] != '-')
            || position + 6U != text.size() || text[position + 3U] != ':'
            || !ascii_digit(text[position + 1U])
            || !ascii_digit(text[position + 2U])
            || !ascii_digit(text[position + 4U])
            || !ascii_digit(text[position + 5U])) {
            return false;
        }
        const uint32_t offset_hour   = decimal_digits(text, position + 1U, 2U);
        const uint32_t offset_minute = decimal_digits(text, position + 4U, 2U);
        if (offset_hour > 23U || offset_minute > 59U) {
            return false;
        }
        int32_t offset = static_cast<int32_t>(offset_hour * 60U
                                              + offset_minute);
        if (text[position] == '-') {
            offset = -offset;
        }
        parsed.has_utc_offset      = true;
        parsed.utc_offset_negative = text[position] == '-';
        parsed.utc_offset_min      = static_cast<int16_t>(offset);
        *out                       = parsed;
        return true;
    }

    static void append_two_digits(uint32_t value, char* out) noexcept
    {
        out[0] = static_cast<char>('0' + ((value / 10U) % 10U));
        out[1] = static_cast<char>('0' + (value % 10U));
    }

    static void append_four_digits(uint32_t value, char* out) noexcept
    {
        out[0] = static_cast<char>('0' + ((value / 1000U) % 10U));
        out[1] = static_cast<char>('0' + ((value / 100U) % 10U));
        out[2] = static_cast<char>('0' + ((value / 10U) % 10U));
        out[3] = static_cast<char>('0' + (value % 10U));
    }

    static void set_exif_datetime_value(const ParsedXmpDateTime& parsed,
                                        PlannedField* field) noexcept
    {
        if (!field || !parsed.has_time) {
            return;
        }
        append_four_digits(parsed.year, field->value.data());
        field->value[4] = ':';
        append_two_digits(parsed.month, field->value.data() + 5U);
        field->value[7] = ':';
        append_two_digits(parsed.day, field->value.data() + 8U);
        field->value[10] = ' ';
        append_two_digits(parsed.hour, field->value.data() + 11U);
        field->value[13] = ':';
        append_two_digits(parsed.minute, field->value.data() + 14U);
        field->value[16] = ':';
        append_two_digits(parsed.second, field->value.data() + 17U);
        field->value_size = 19U;
        field->present    = true;
    }

    static void set_exif_offset_value(const ParsedXmpDateTime& parsed,
                                      PlannedField* field) noexcept
    {
        if (!field || !parsed.has_utc_offset) {
            return;
        }
        int32_t offset  = parsed.utc_offset_min;
        field->value[0] = parsed.utc_offset_negative ? '-' : '+';
        if (offset < 0) {
            offset = -offset;
        }
        append_two_digits(static_cast<uint32_t>(offset / 60),
                          field->value.data() + 1U);
        field->value[3] = ':';
        append_two_digits(static_cast<uint32_t>(offset % 60),
                          field->value.data() + 4U);
        field->value_size = 6U;
        field->present    = true;
    }

    static void set_exif_subsecond_value(const ParsedXmpDateTime& parsed,
                                         PlannedField* field) noexcept
    {
        if (!field || !parsed.has_subsecond) {
            return;
        }
        for (uint8_t i = 0U; i < parsed.subsecond_size; ++i) {
            field->value[i] = parsed.subsecond[i];
        }
        field->value_size = parsed.subsecond_size;
        field->present    = true;
    }

    static void set_iptc_date_value(const ParsedXmpDateTime& parsed,
                                    PlannedField* field) noexcept
    {
        if (!field) {
            return;
        }
        append_four_digits(parsed.year, field->value.data());
        append_two_digits(parsed.month, field->value.data() + 4U);
        append_two_digits(parsed.day, field->value.data() + 6U);
        field->value_size = 8U;
        field->present    = true;
    }

    static void set_iptc_time_value(const ParsedXmpDateTime& parsed,
                                    PlannedField* field) noexcept
    {
        if (!field || !parsed.has_time) {
            return;
        }
        append_two_digits(parsed.hour, field->value.data());
        append_two_digits(parsed.minute, field->value.data() + 2U);
        append_two_digits(parsed.second, field->value.data() + 4U);
        field->value_size = 6U;
        if (parsed.has_utc_offset) {
            int32_t offset  = parsed.utc_offset_min;
            field->value[6] = parsed.utc_offset_negative ? '-' : '+';
            if (offset < 0) {
                offset = -offset;
            }
            append_two_digits(static_cast<uint32_t>(offset / 60),
                              field->value.data() + 7U);
            append_two_digits(static_cast<uint32_t>(offset % 60),
                              field->value.data() + 9U);
            field->value_size = 11U;
        }
        field->present = true;
    }

    static bool xmp_key_matches(const MetaStore& store, const Entry& entry,
                                std::string_view schema_ns,
                                std::string_view property_path) noexcept
    {
        return entry.key.kind == MetaKeyKind::XmpProperty
               && arena_text(store.arena(),
                             entry.key.data.xmp_property.schema_ns)
                      == schema_ns
               && arena_text(store.arena(),
                             entry.key.data.xmp_property.property_path)
                      == property_path;
    }

    static MetadataDateTranslationStatus
    find_source_property(const MetaStore& store, std::string_view schema_ns,
                         std::string_view property_path,
                         MetadataDateTranslationSourceMode source_mode,
                         SourceProperty* out) noexcept
    {
        if (!out) {
            return MetadataDateTranslationStatus::InternalError;
        }
        *out                                 = SourceProperty {};
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (!xmp_key_matches(store, entry, schema_ns, property_path)) {
                continue;
            }
            const bool dirty   = any(entry.flags, EntryFlags::Dirty);
            const bool deleted = any(entry.flags, EntryFlags::Deleted);
            if ((source_mode == MetadataDateTranslationSourceMode::DirtyOnly
                 && !dirty)
                || (deleted && !dirty)) {
                continue;
            }
            if (out->found) {
                return MetadataDateTranslationStatus::AmbiguousSource;
            }
            out->found    = true;
            out->deleted  = deleted;
            out->entry_id = id;
            if (deleted) {
                continue;
            }
            if (entry.value.kind != MetaValueKind::Text) {
                return MetadataDateTranslationStatus::InvalidSourceValue;
            }
            out->text = arena_text(store.arena(), entry.value.data.span);
            if (out->text.empty()) {
                return MetadataDateTranslationStatus::InvalidSourceValue;
            }
        }
        return MetadataDateTranslationStatus::Ok;
    }

    static PlannedGroup make_exif_group(MetadataDateTranslationMapping mapping,
                                        EntryId source_entry,
                                        const ParsedXmpDateTime& parsed,
                                        bool deleted, bool original) noexcept
    {
        PlannedGroup group;
        group.mapping         = mapping;
        group.source_entry    = source_entry;
        group.field_count     = 3U;
        group.fields[0].field = original
                                    ? NativeDateField::ExifDateTimeOriginal
                                    : NativeDateField::ExifDateTimeDigitized;
        group.fields[1].field = original
                                    ? NativeDateField::ExifOffsetTimeOriginal
                                    : NativeDateField::ExifOffsetTimeDigitized;
        group.fields[2].field = original
                                    ? NativeDateField::ExifSubSecTimeOriginal
                                    : NativeDateField::ExifSubSecTimeDigitized;
        if (!deleted) {
            set_exif_datetime_value(parsed, &group.fields[0]);
            set_exif_offset_value(parsed, &group.fields[1]);
            set_exif_subsecond_value(parsed, &group.fields[2]);
        }
        return group;
    }

    static PlannedGroup make_iptc_group(MetadataDateTranslationMapping mapping,
                                        EntryId source_entry,
                                        const ParsedXmpDateTime& parsed,
                                        bool deleted, bool digital) noexcept
    {
        PlannedGroup group;
        group.mapping         = mapping;
        group.source_entry    = source_entry;
        group.field_count     = 2U;
        group.fields[0].field = digital
                                    ? NativeDateField::IptcDigitalCreationDate
                                    : NativeDateField::IptcDateCreated;
        group.fields[1].field = digital
                                    ? NativeDateField::IptcDigitalCreationTime
                                    : NativeDateField::IptcTimeCreated;
        if (!deleted) {
            set_iptc_date_value(parsed, &group.fields[0]);
            set_iptc_time_value(parsed, &group.fields[1]);
        }
        return group;
    }

    static bool native_field_matches(const MetaStore& store, const Entry& entry,
                                     NativeDateField field) noexcept
    {
        if (field == NativeDateField::IptcDateCreated
            || field == NativeDateField::IptcTimeCreated
            || field == NativeDateField::IptcDigitalCreationDate
            || field == NativeDateField::IptcDigitalCreationTime) {
            if (entry.key.kind != MetaKeyKind::IptcDataset
                || entry.key.data.iptc_dataset.record != 2U) {
                return false;
            }
            uint16_t dataset = 55U;
            switch (field) {
            case NativeDateField::IptcDateCreated: dataset = 55U; break;
            case NativeDateField::IptcTimeCreated: dataset = 60U; break;
            case NativeDateField::IptcDigitalCreationDate: dataset = 62U; break;
            case NativeDateField::IptcDigitalCreationTime: dataset = 63U; break;
            default: break;
            }
            return entry.key.data.iptc_dataset.dataset == dataset;
        }

        if (entry.key.kind != MetaKeyKind::ExifTag
            || arena_text(store.arena(), entry.key.data.exif_tag.ifd)
                   != "exififd") {
            return false;
        }
        uint16_t tag = 0U;
        switch (field) {
        case NativeDateField::ExifDateTimeOriginal: tag = 0x9003U; break;
        case NativeDateField::ExifDateTimeDigitized: tag = 0x9004U; break;
        case NativeDateField::ExifOffsetTimeOriginal: tag = 0x9011U; break;
        case NativeDateField::ExifOffsetTimeDigitized: tag = 0x9012U; break;
        case NativeDateField::ExifSubSecTimeOriginal: tag = 0x9291U; break;
        case NativeDateField::ExifSubSecTimeDigitized: tag = 0x9292U; break;
        default: return false;
        }
        return entry.key.data.exif_tag.tag == tag;
    }

    static bool entry_value_matches(const MetaStore& store, const Entry& entry,
                                    const PlannedField& field) noexcept
    {
        if (entry.value.kind != MetaValueKind::Text
            && entry.value.kind != MetaValueKind::Bytes) {
            return false;
        }
        std::span<const std::byte> bytes = store.arena().span(
            entry.value.data.span);
        while (!bytes.empty() && bytes.back() == std::byte { 0U }) {
            bytes = bytes.first(bytes.size() - 1U);
        }
        if (bytes.size() != field.value_size) {
            return false;
        }
        for (size_t i = 0U; i < bytes.size(); ++i) {
            if (bytes[i]
                != static_cast<std::byte>(
                    static_cast<uint8_t>(field.value[i]))) {
                return false;
            }
        }
        return true;
    }

    static void analyze_group(const MetaStore& store,
                              PlannedGroup* group) noexcept
    {
        if (!group) {
            return;
        }
        group->existing_any                  = false;
        group->exact_match                   = true;
        const std::span<const Entry> entries = store.entries();
        for (uint8_t f = 0U; f < group->field_count; ++f) {
            const PlannedField& field = group->fields[f];
            uint32_t active_count     = 0U;
            bool exact_value          = false;
            for (const Entry& entry : entries) {
                if (any(entry.flags, EntryFlags::Deleted)
                    || !native_field_matches(store, entry, field.field)) {
                    continue;
                }
                ++active_count;
                if (active_count == 1U) {
                    exact_value = entry_value_matches(store, entry, field);
                }
            }
            group->existing_any = group->existing_any || active_count > 0U;
            if (field.present) {
                group->exact_match = group->exact_match && active_count == 1U
                                     && exact_value;
            } else {
                group->exact_match = group->exact_match && active_count == 0U;
            }
        }
    }

    static uint32_t missing_present_fields(const MetaStore& store,
                                           const PlannedGroup& group) noexcept
    {
        uint32_t count                       = 0U;
        const std::span<const Entry> entries = store.entries();
        for (uint8_t f = 0U; f < group.field_count; ++f) {
            if (!group.fields[f].present) {
                continue;
            }
            bool found = false;
            for (const Entry& entry : entries) {
                if (!any(entry.flags, EntryFlags::Deleted)
                    && native_field_matches(store, entry,
                                            group.fields[f].field)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ++count;
            }
        }
        return count;
    }

    static uint32_t required_group_operations(const MetaStore& store,
                                              const PlannedGroup& group) noexcept
    {
        uint32_t count                       = 0U;
        const std::span<const Entry> entries = store.entries();
        for (uint8_t f = 0U; f < group.field_count; ++f) {
            const PlannedField& field = group.fields[f];
            uint32_t active_count     = 0U;
            bool first_matches        = false;
            for (const Entry& entry : entries) {
                if (any(entry.flags, EntryFlags::Deleted)
                    || !native_field_matches(store, entry, field.field)) {
                    continue;
                }
                ++active_count;
                if (active_count == 1U) {
                    first_matches = entry_value_matches(store, entry, field);
                }
            }
            if (!field.present) {
                count += active_count;
            } else if (active_count == 0U) {
                ++count;
            } else {
                count += active_count - 1U;
                if (!first_matches) {
                    ++count;
                }
            }
        }
        return count;
    }

    static MetaKey make_native_key(ByteArena& arena,
                                   NativeDateField field) noexcept
    {
        switch (field) {
        case NativeDateField::ExifDateTimeOriginal:
            return make_exif_tag_key(arena, "exififd", 0x9003U);
        case NativeDateField::ExifDateTimeDigitized:
            return make_exif_tag_key(arena, "exififd", 0x9004U);
        case NativeDateField::ExifOffsetTimeOriginal:
            return make_exif_tag_key(arena, "exififd", 0x9011U);
        case NativeDateField::ExifOffsetTimeDigitized:
            return make_exif_tag_key(arena, "exififd", 0x9012U);
        case NativeDateField::ExifSubSecTimeOriginal:
            return make_exif_tag_key(arena, "exififd", 0x9291U);
        case NativeDateField::ExifSubSecTimeDigitized:
            return make_exif_tag_key(arena, "exififd", 0x9292U);
        case NativeDateField::IptcDateCreated:
            return make_iptc_dataset_key(2U, 55U);
        case NativeDateField::IptcTimeCreated:
            return make_iptc_dataset_key(2U, 60U);
        case NativeDateField::IptcDigitalCreationDate:
            return make_iptc_dataset_key(2U, 62U);
        case NativeDateField::IptcDigitalCreationTime:
            return make_iptc_dataset_key(2U, 63U);
        }
        return make_iptc_dataset_key(0U, 0U);
    }

    static MetaValue make_native_value(ByteArena& arena,
                                       const PlannedField& field) noexcept
    {
        const std::string_view text(field.value.data(), field.value_size);
        if (field.field == NativeDateField::IptcDateCreated
            || field.field == NativeDateField::IptcTimeCreated
            || field.field == NativeDateField::IptcDigitalCreationDate
            || field.field == NativeDateField::IptcDigitalCreationTime) {
            return make_bytes(arena, std::span<const std::byte>(
                                         reinterpret_cast<const std::byte*>(
                                             text.data()),
                                         text.size()));
        }
        return make_text(arena, text, TextEncoding::Ascii);
    }

    static bool append_native_entry(MetaEdit* edit, const MetaStore& source,
                                    const PlannedGroup& group,
                                    const PlannedField& field,
                                    uint32_t order_delta) noexcept
    {
        if (!edit || group.source_entry >= source.entries().size()) {
            return false;
        }
        Entry entry;
        entry.key    = make_native_key(edit->arena(), field.field);
        entry.value  = make_native_value(edit->arena(), field);
        entry.origin = source.entry(group.source_entry).origin;
        if (entry.origin.wire_type_name.size > 0U) {
            entry.origin.wire_type_name = edit->arena().append(
                source.arena().span(entry.origin.wire_type_name));
        }
        if (entry.origin.order_in_block
            <= std::numeric_limits<uint32_t>::max() - order_delta) {
            entry.origin.order_in_block += order_delta;
        } else {
            entry.origin.order_in_block = std::numeric_limits<uint32_t>::max();
        }
        entry.flags = EntryFlags::Dirty;
        if (edit->arena().limit_exceeded()) {
            return false;
        }
        edit->add_entry(entry);
        return true;
    }

    static void apply_group(const MetaStore& source, const PlannedGroup& group,
                            MetaEdit* edit,
                            MetadataDateTranslationResult* result)
    {
        if (!edit || !result || !group.apply) {
            return;
        }
        const std::span<const Entry> entries = source.entries();
        for (uint8_t f = 0U; f < group.field_count; ++f) {
            const PlannedField& field = group.fields[f];
            EntryId first_active      = kInvalidEntryId;
            for (EntryId id = 0U; id < entries.size(); ++id) {
                const Entry& entry = entries[id];
                if (any(entry.flags, EntryFlags::Deleted)
                    || !native_field_matches(source, entry, field.field)) {
                    continue;
                }
                if (!field.present || first_active != kInvalidEntryId) {
                    edit->tombstone(id);
                    ++result->entries_removed;
                    continue;
                }
                first_active = id;
                if (!entry_value_matches(source, entry, field)) {
                    const MetaValue value = make_native_value(edit->arena(),
                                                              field);
                    edit->set_value(id, value);
                    ++result->entries_updated;
                }
            }
            if (field.present && first_active == kInvalidEntryId) {
                if (append_native_entry(edit, source, group, field,
                                        static_cast<uint32_t>(f) + 1U)) {
                    ++result->entries_added;
                }
            }
        }
        ++result->groups_translated;
    }

    static MetadataDateTranslationResult
    translation_error(MetadataDateTranslationStatus status,
                      MetadataDateTranslationMapping mapping
                      = MetadataDateTranslationMapping::None,
                      EntryId source_entry = kInvalidEntryId) noexcept
    {
        MetadataDateTranslationResult result;
        result.status              = status;
        result.failed_mapping      = mapping;
        result.failed_source_entry = source_entry;
        return result;
    }

    static MetadataDateTranslationStatus append_source_groups(
        const MetaStore& source, const MetadataDateTranslationOptions& options,
        std::string_view schema_ns, std::string_view property_path,
        MetadataDateTranslationMapping mapping, bool add_exif,
        bool exif_original, bool add_iptc, bool iptc_digital,
        std::array<PlannedGroup, 4U>* groups, uint8_t* group_count,
        MetadataDateTranslationResult* result) noexcept
    {
        if (!groups || !group_count || !result) {
            return MetadataDateTranslationStatus::InternalError;
        }
        SourceProperty property;
        const MetadataDateTranslationStatus source_status
            = find_source_property(source, schema_ns, property_path,
                                   options.source_mode, &property);
        if (source_status != MetadataDateTranslationStatus::Ok) {
            result->failed_mapping      = mapping;
            result->failed_source_entry = property.entry_id;
            return source_status;
        }
        if (!property.found) {
            return MetadataDateTranslationStatus::Ok;
        }
        ++result->source_properties;

        ParsedXmpDateTime parsed;
        if (!property.deleted && !parse_xmp_datetime(property.text, &parsed)) {
            result->failed_mapping      = mapping;
            result->failed_source_entry = property.entry_id;
            return MetadataDateTranslationStatus::InvalidDateTime;
        }
        if (!property.deleted && add_exif && !parsed.has_time) {
            result->failed_mapping      = mapping;
            result->failed_source_entry = property.entry_id;
            return MetadataDateTranslationStatus::UnsupportedPrecision;
        }
        if (!property.deleted && add_iptc && parsed.has_subsecond) {
            result->failed_mapping      = mapping;
            result->failed_source_entry = property.entry_id;
            return MetadataDateTranslationStatus::UnsupportedPrecision;
        }
        if (add_exif) {
            if (*group_count >= groups->size()) {
                return MetadataDateTranslationStatus::InternalError;
            }
            (*groups)[(*group_count)++]
                = make_exif_group(mapping, property.entry_id, parsed,
                                  property.deleted, exif_original);
        }
        if (add_iptc) {
            if (*group_count >= groups->size()) {
                return MetadataDateTranslationStatus::InternalError;
            }
            (*groups)[(*group_count)++]
                = make_iptc_group(mapping, property.entry_id, parsed,
                                  property.deleted, iptc_digital);
        }
        return MetadataDateTranslationStatus::Ok;
    }

}  // namespace

MetadataDateTranslationResult
translate_xmp_creation_dates(const MetaStore& source,
                             const MetadataDateTranslationOptions& options,
                             MetaStore* out_store)
{
    if (!out_store) {
        return translation_error(MetadataDateTranslationStatus::NullOutput);
    }
    if (!source.is_finalized()) {
        return translation_error(
            MetadataDateTranslationStatus::SourceNotFinalized);
    }
    if (options.max_added_entries == 0U
        || options.max_added_entries > kMetadataDateTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataDateTranslationMaxOperations
        || (options.source_mode != MetadataDateTranslationSourceMode::DirtyOnly
            && options.source_mode != MetadataDateTranslationSourceMode::All)
        || (options.conflict_policy
                != MetadataDateTranslationConflictPolicy::PreserveExisting
            && options.conflict_policy
                   != MetadataDateTranslationConflictPolicy::FailOnConflict
            && options.conflict_policy
                   != MetadataDateTranslationConflictPolicy::ReplaceExisting)
        || (!options.create_date_to_exif_digitized
            && !options.create_date_to_iptc_digital_creation
            && !options.date_created_to_iptc_created
            && !options.date_time_original_to_exif_original)) {
        return translation_error(MetadataDateTranslationStatus::InvalidOptions);
    }

    std::array<PlannedGroup, 4U> groups {};
    uint8_t group_count = 0U;
    MetadataDateTranslationResult result;

    MetadataDateTranslationStatus status
        = append_source_groups(source, options, kXmpNsXmp, "CreateDate",
                               MetadataDateTranslationMapping::XmpCreateDate,
                               options.create_date_to_exif_digitized, false,
                               options.create_date_to_iptc_digital_creation,
                               true, &groups, &group_count, &result);
    if (status == MetadataDateTranslationStatus::Ok
        && options.date_created_to_iptc_created) {
        status = append_source_groups(
            source, options, kXmpNsPhotoshop, "DateCreated",
            MetadataDateTranslationMapping::PhotoshopDateCreated, false, false,
            true, false, &groups, &group_count, &result);
    }
    if (status == MetadataDateTranslationStatus::Ok
        && options.date_time_original_to_exif_original) {
        status = append_source_groups(
            source, options, kXmpNsExif, "DateTimeOriginal",
            MetadataDateTranslationMapping::XmpDateTimeOriginal, true, true,
            false, false, &groups, &group_count, &result);
    }
    if (status != MetadataDateTranslationStatus::Ok) {
        result.status = status;
        return result;
    }

    uint32_t added_entries   = 0U;
    uint32_t operation_count = 0U;
    for (uint8_t i = 0U; i < group_count; ++i) {
        PlannedGroup& group = groups[i];
        analyze_group(source, &group);
        switch (options.conflict_policy) {
        case MetadataDateTranslationConflictPolicy::PreserveExisting:
            if (group.existing_any) {
                ++result.groups_preserved;
            } else {
                group.apply = true;
            }
            break;
        case MetadataDateTranslationConflictPolicy::FailOnConflict:
            if (group.existing_any && !group.exact_match) {
                result.status = MetadataDateTranslationStatus::NativeConflict;
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
        case MetadataDateTranslationConflictPolicy::ReplaceExisting:
            if (group.exact_match) {
                ++result.groups_unchanged;
            } else {
                group.apply = true;
            }
            break;
        }
        if (group.apply) {
            added_entries += missing_present_fields(source, group);
            operation_count += required_group_operations(source, group);
        }
    }
    if (added_entries > options.max_added_entries
        || source.entries().size() > static_cast<size_t>(kInvalidEntryId)
        || static_cast<size_t>(added_entries)
               > static_cast<size_t>(kInvalidEntryId)
                     - source.entries().size()) {
        result.status = MetadataDateTranslationStatus::EntryLimitExceeded;
        return result;
    }
    if (operation_count > options.max_operations) {
        result.status = MetadataDateTranslationStatus::OperationLimitExceeded;
        return result;
    }

    MetaEdit edit;
    edit.reserve_ops(operation_count);
    for (uint8_t i = 0U; i < group_count; ++i) {
        apply_group(source, groups[i], &edit, &result);
    }
    if (edit.ops().size() != operation_count || edit.arena().limit_exceeded()
        || result.entries_added != added_entries) {
        result.status = MetadataDateTranslationStatus::InternalError;
        return result;
    }

    *out_store = commit(source, std::span<const MetaEdit>(&edit, 1U));
    return result;
}

const char*
metadata_date_translation_status_name(
    MetadataDateTranslationStatus status) noexcept
{
    switch (status) {
    case MetadataDateTranslationStatus::Ok: return "ok";
    case MetadataDateTranslationStatus::NullOutput: return "null_output";
    case MetadataDateTranslationStatus::SourceNotFinalized:
        return "source_not_finalized";
    case MetadataDateTranslationStatus::InvalidOptions:
        return "invalid_options";
    case MetadataDateTranslationStatus::AmbiguousSource:
        return "ambiguous_source";
    case MetadataDateTranslationStatus::InvalidSourceValue:
        return "invalid_source_value";
    case MetadataDateTranslationStatus::InvalidDateTime:
        return "invalid_date_time";
    case MetadataDateTranslationStatus::UnsupportedPrecision:
        return "unsupported_precision";
    case MetadataDateTranslationStatus::NativeConflict:
        return "native_conflict";
    case MetadataDateTranslationStatus::EntryLimitExceeded:
        return "entry_limit_exceeded";
    case MetadataDateTranslationStatus::OperationLimitExceeded:
        return "operation_limit_exceeded";
    case MetadataDateTranslationStatus::InternalError: return "internal_error";
    }
    return "unknown";
}

const char*
metadata_date_translation_mapping_name(
    MetadataDateTranslationMapping mapping) noexcept
{
    switch (mapping) {
    case MetadataDateTranslationMapping::None: return "none";
    case MetadataDateTranslationMapping::XmpCreateDate:
        return "xmp_create_date";
    case MetadataDateTranslationMapping::PhotoshopDateCreated:
        return "photoshop_date_created";
    case MetadataDateTranslationMapping::XmpDateTimeOriginal:
        return "xmp_date_time_original";
    }
    return "unknown";
}

}  // namespace openmeta
