// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_translation.h"

#include "interop_safety_internal.h"

#include "openmeta/meta_edit.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace openmeta {
namespace {

    using Status  = MetadataGpsTranslationStatus;
    using Mapping = MetadataGpsTranslationMapping;
    using Policy  = MetadataGpsTranslationConflictPolicy;

    struct SourceProperty final {
        EntryId first     = kInvalidEntryId;
        EntryId duplicate = kInvalidEntryId;
        bool dirty        = false;
    };

    enum class GpsGroupKind : uint8_t {
        Coordinate,
        Altitude,
        Timestamp,
        Rational,
        SingleReference,
        SingleRational,
        SingleShort,
        PlainText,
        EncodedText
    };

    struct GpsGroup final {
        GpsGroupKind kind = GpsGroupKind::Coordinate;
        uint8_t native_count = 2U;
        std::string text_value;
        std::string encoded_value;
        Mapping mapping = Mapping::None;
        std::array<EntryId, 2> source_entries { kInvalidEntryId,
                                                kInvalidEntryId };
        std::array<EntryId, 2> native_entries { kInvalidEntryId,
                                                kInvalidEntryId };
        std::array<uint32_t, 2> native_counts {};
        std::array<bool, 2> matches {};
        std::array<URational, 3> components {};
        std::array<char, 10> date {};
        URational magnitude { 0U, 1U };
        char reference       = 'N';
        uint8_t altitude_ref = 0U;
        std::array<uint16_t, 2> tags {};
        bool selected = false;
        bool present  = false;
        bool apply    = false;
    };

    struct Ratio final {
        uint64_t numerator   = 0U;
        uint64_t denominator = 1U;
    };

    static std::string_view text(const ByteArena& arena,
                                 ByteSpan value) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(value);
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    static uint64_t gcd(uint64_t a, uint64_t b) noexcept
    {
        while (b != 0U) {
            const uint64_t remainder = a % b;
            a                        = b;
            b                        = remainder;
        }
        return a;
    }

    static Status digits(std::string_view value, uint64_t* out) noexcept
    {
        if (value.empty()) {
            return Status::InvalidSourceValue;
        }
        uint64_t number = 0U;
        for (const char c : value) {
            if (c < '0' || c > '9') {
                return Status::InvalidSourceValue;
            }
            const uint64_t digit = static_cast<uint64_t>(c - '0');
            if (number > (UINT64_MAX - digit) / 10U) {
                return Status::UnsupportedPrecision;
            }
            number = number * 10U + digit;
        }
        *out = number;
        return Status::Ok;
    }

    static Status decimal(std::string_view value, Ratio* out) noexcept
    {
        const size_t dot = value.find('.');
        if (dot == std::string_view::npos) {
            return digits(value, &out->numerator);
        }
        uint64_t whole = 0U;
        Status status  = digits(value.substr(0U, dot), &whole);
        if (status != Status::Ok) {
            return status;
        }
        std::string_view fraction = value.substr(dot + 1U);
        if (fraction.empty()) {
            return Status::InvalidSourceValue;
        }
        for (const char c : fraction) {
            if (c < '0' || c > '9') {
                return Status::InvalidSourceValue;
            }
        }
        while (!fraction.empty() && fraction.back() == '0') {
            fraction.remove_suffix(1U);
        }
        uint64_t numerator   = 0U;
        uint64_t denominator = 1U;
        if (!fraction.empty()) {
            status = digits(fraction, &numerator);
            if (status != Status::Ok) {
                return status;
            }
            for (size_t i = 0U; i < fraction.size(); ++i) {
                if (denominator > UINT64_MAX / 10U) {
                    return Status::UnsupportedPrecision;
                }
                denominator *= 10U;
            }
        }
        if (whole > (UINT64_MAX - numerator) / denominator) {
            return Status::UnsupportedPrecision;
        }
        out->numerator   = whole * denominator + numerator;
        out->denominator = denominator;
        return Status::Ok;
    }

    static Status rational32(Ratio value, URational* out) noexcept
    {
        if (value.denominator == 0U) {
            return Status::InvalidSourceValue;
        }
        const uint64_t divisor = gcd(value.numerator, value.denominator);
        value.numerator /= divisor;
        value.denominator /= divisor;
        if (value.numerator > UINT32_MAX || value.denominator > UINT32_MAX) {
            return Status::UnsupportedPrecision;
        }
        *out = { static_cast<uint32_t>(value.numerator),
                 static_cast<uint32_t>(value.denominator) };
        return Status::Ok;
    }

    static bool integer(const MetaValue& value, uint64_t* out) noexcept
    {
        if (value.kind != MetaValueKind::Scalar || value.count != 1U) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: *out = value.data.u64; return true;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            if (value.data.i64 >= 0) {
                *out = static_cast<uint64_t>(value.data.i64);
                return true;
            }
            return false;
        default: return false;
        }
    }

    static Status unsigned_rational_value(const MetaStore& source,
                                          const MetaValue& value,
                                          URational* out) noexcept
    {
        Ratio ratio;
        if (value.kind == MetaValueKind::Scalar && value.count == 1U
            && value.elem_type == MetaElementType::URational) {
            ratio = { value.data.ur.numer, value.data.ur.denom };
        } else if (integer(value, &ratio.numerator)) {
            // Integer values have an exact denominator of one.
        } else if (value.kind == MetaValueKind::Text) {
            const std::string_view input = text(source.arena(),
                                                value.data.span);
            const size_t slash           = input.find('/');
            Status status;
            if (slash == std::string_view::npos) {
                status = decimal(input, &ratio);
            } else {
                status = digits(input.substr(0U, slash), &ratio.numerator);
                if (status == Status::Ok) {
                    status = digits(input.substr(slash + 1U),
                                    &ratio.denominator);
                }
            }
            if (status != Status::Ok) {
                return status;
            }
        } else {
            return Status::InvalidSourceValue;
        }
        return rational32(ratio, out);
    }

    static Status timestamp_value(const MetaStore& source,
                                  const MetaValue& value,
                                  GpsGroup* group) noexcept
    {
        if (value.kind != MetaValueKind::Text) {
            return Status::InvalidSourceValue;
        }
        const std::string_view input = text(source.arena(), value.data.span);
        if (input.size() < 20U || input[4] != '-' || input[7] != '-'
            || input[10] != 'T' || input[13] != ':' || input[16] != ':'
            || input[17] < '0' || input[17] > '9' || input[18] < '0'
            || input[18] > '9') {
            return Status::InvalidSourceValue;
        }
        std::array<uint64_t, 5> parts {};
        static constexpr std::array<size_t, 5> starts { 0U, 5U, 8U, 11U, 14U };
        for (size_t i = 0U; i < parts.size(); ++i) {
            const Status status
                = digits(input.substr(starts[i], i == 0U ? 4U : 2U), &parts[i]);
            if (status != Status::Ok) {
                return status;
            }
        }
        const std::chrono::year_month_day date {
            std::chrono::year { static_cast<int>(parts[0]) },
            std::chrono::month { static_cast<unsigned>(parts[1]) },
            std::chrono::day { static_cast<unsigned>(parts[2]) }
        };
        if (parts[0] == 0U || !date.ok() || parts[3] > 23U || parts[4] > 59U) {
            return Status::ValueOutOfRange;
        }
        const size_t zone = input.find_first_of("Z+-", 19U);
        if (zone == std::string_view::npos
            || (zone != 19U && input[19] != '.')) {
            return Status::InvalidSourceValue;
        }
        Ratio seconds;
        Status status = decimal(input.substr(17U, zone - 17U), &seconds);
        if (status != Status::Ok) {
            return status;
        }
        if (seconds.numerator / seconds.denominator >= 60U) {
            return Status::ValueOutOfRange;
        }
        status = rational32(seconds, &group->components[2]);
        if (status != Status::Ok) {
            return status;
        }
        int offset = 0;
        if (input[zone] == 'Z') {
            if (zone + 1U != input.size()) {
                return Status::InvalidSourceValue;
            }
        } else {
            if (input.size() - zone != 6U || input[zone + 3U] != ':') {
                return Status::InvalidSourceValue;
            }
            uint64_t hours   = 0U;
            uint64_t minutes = 0U;
            if (digits(input.substr(zone + 1U, 2U), &hours) != Status::Ok
                || digits(input.substr(zone + 4U, 2U), &minutes)
                       != Status::Ok) {
                return Status::InvalidSourceValue;
            }
            if (hours > 23U || minutes > 59U) {
                return Status::ValueOutOfRange;
            }
            offset = static_cast<int>(hours * 60U + minutes);
            if (input[zone] == '-') {
                offset = -offset;
            }
        }
        int minute_of_day = static_cast<int>(parts[3] * 60U + parts[4])
                            - offset;
        std::chrono::sys_days utc_day { date };
        if (minute_of_day < 0) {
            minute_of_day += 1440;
            utc_day -= std::chrono::days { 1 };
        } else if (minute_of_day >= 1440) {
            minute_of_day -= 1440;
            utc_day += std::chrono::days { 1 };
        }
        const std::chrono::year_month_day utc { utc_day };
        const int year = static_cast<int>(utc.year());
        if (year < 1 || year > 9999) {
            return Status::ValueOutOfRange;
        }
        group->components[0] = { static_cast<uint32_t>(minute_of_day / 60),
                                 1U };
        group->components[1] = { static_cast<uint32_t>(minute_of_day % 60),
                                 1U };
        group->date          = {
            static_cast<char>('0' + year / 1000),
            static_cast<char>('0' + year / 100 % 10),
            static_cast<char>('0' + year / 10 % 10),
            static_cast<char>('0' + year % 10),
            ':',
            static_cast<char>('0' + static_cast<unsigned>(utc.month()) / 10U),
            static_cast<char>('0' + static_cast<unsigned>(utc.month()) % 10U),
            ':',
            static_cast<char>('0' + static_cast<unsigned>(utc.day()) / 10U),
            static_cast<char>('0' + static_cast<unsigned>(utc.day()) % 10U)
        };
        return Status::Ok;
    }

    static Status navigation_reference(const MetaStore& source,
                                       const MetaValue& value, bool speed,
                                       char* out) noexcept
    {
        if (value.kind != MetaValueKind::Text) {
            return Status::InvalidSourceValue;
        }
        const std::string_view input = text(source.arena(), value.data.span);
        if (speed) {
            if (input == "K" || input == "km/h") {
                *out = 'K';
            } else if (input == "M" || input == "mph") {
                *out = 'M';
            } else if (input == "N" || input == "knots") {
                *out = 'N';
            } else {
                return Status::InvalidSourceValue;
            }
        } else {
            if (input == "T" || input == "True North") {
                *out = 'T';
            } else if (input == "M" || input == "Magnetic North") {
                *out = 'M';
            } else {
                return Status::InvalidSourceValue;
            }
        }
        return Status::Ok;
    }

    static Status quality_value(const MetaStore& source, const MetaValue& value,
                                GpsGroup* group) noexcept
    {
        if (group->kind == GpsGroupKind::SingleRational) {
            return unsigned_rational_value(source, value, &group->magnitude);
        }
        const std::string_view input = value.kind == MetaValueKind::Text
                                           ? text(source.arena(),
                                                  value.data.span)
                                           : std::string_view {};
        if (group->mapping == Mapping::ExifGpsStatus) {
            if (input == "A" || input == "Measurement Active") {
                group->reference = 'A';
            } else if (input == "V" || input == "Measurement Void") {
                group->reference = 'V';
            } else {
                return Status::InvalidSourceValue;
            }
            return Status::Ok;
        }
        uint64_t number = 0U;
        if (value.kind == MetaValueKind::Text) {
            if (group->mapping == Mapping::ExifGpsMeasureMode) {
                if (input != "2" && input != "3") {
                    return Status::InvalidSourceValue;
                }
                number = static_cast<uint64_t>(input[0] - '0');
            } else if (input == "0" || input == "No Correction") {
                number = 0U;
            } else if (input == "1" || input == "Differential Corrected") {
                number = 1U;
            } else {
                return Status::InvalidSourceValue;
            }
        } else if (!integer(value, &number)) {
            return Status::InvalidSourceValue;
        }
        if (group->mapping == Mapping::ExifGpsMeasureMode) {
            if (number != 2U && number != 3U) {
                return Status::ValueOutOfRange;
            }
            group->reference = static_cast<char>('0' + number);
        } else {
            if (number > 1U) {
                return Status::ValueOutOfRange;
            }
            group->altitude_ref = static_cast<uint8_t>(number);
        }
        return Status::Ok;
    }

    static Status distance_reference(const MetaStore& source,
                                     const MetaValue& value, char* out) noexcept
    {
        if (value.kind != MetaValueKind::Text) {
            return Status::InvalidSourceValue;
        }
        const std::string_view input = text(source.arena(), value.data.span);
        if (input == "K" || input == "Kilometers") {
            *out = 'K';
        } else if (input == "M" || input == "Miles") {
            *out = 'M';
        } else if (input == "N" || input == "Nautical miles"
                   || input == "Knots") {
            // "Knots" is the historical portable distance label for N.
            // Preserve that spelling as an alias; no speed conversion applies.
            *out = 'N';
        } else {
            return Status::InvalidSourceValue;
        }
        return Status::Ok;
    }

    static Status coordinate_value(const MetaStore& source,
                                   const MetaValue& value, bool latitude,
                                   GpsGroup* group) noexcept
    {
        if (value.kind != MetaValueKind::Text) {
            return Status::InvalidSourceValue;
        }
        std::string_view input = text(source.arena(), value.data.span);
        if (input.empty()) {
            return Status::InvalidSourceValue;
        }
        group->reference = input.back();
        if (latitude ? (input.back() != 'N' && input.back() != 'S')
                     : (input.back() != 'E' && input.back() != 'W')) {
            return Status::InvalidSourceValue;
        }
        input.remove_suffix(1U);
        const size_t first_comma = input.find(',');
        if (first_comma == std::string_view::npos) {
            return Status::InvalidSourceValue;
        }
        uint64_t degrees = 0U;
        Status status    = digits(input.substr(0U, first_comma), &degrees);
        if (status != Status::Ok) {
            return status;
        }
        input.remove_prefix(first_comma + 1U);
        const size_t second_comma = input.find(',');
        uint64_t minutes          = 0U;
        Ratio seconds;
        if (second_comma == std::string_view::npos) {
            Ratio minute_value;
            status = decimal(input, &minute_value);
            if (status != Status::Ok) {
                return status;
            }
            minutes = minute_value.numerator / minute_value.denominator;
            seconds.numerator = minute_value.numerator
                                % minute_value.denominator;
            seconds.denominator    = minute_value.denominator;
            const uint64_t divisor = gcd(60U, seconds.denominator);
            seconds.denominator /= divisor;
            const uint64_t multiplier = 60U / divisor;
            if (seconds.numerator > UINT64_MAX / multiplier) {
                return Status::UnsupportedPrecision;
            }
            seconds.numerator *= multiplier;
        } else {
            status = digits(input.substr(0U, second_comma), &minutes);
            if (status == Status::Ok) {
                status = decimal(input.substr(second_comma + 1U), &seconds);
            }
            if (status != Status::Ok) {
                return status;
            }
        }
        const uint64_t maximum = latitude ? 90U : 180U;
        if (degrees > maximum || minutes >= 60U
            || seconds.numerator / seconds.denominator >= 60U
            || (degrees == maximum
                && (minutes != 0U || seconds.numerator != 0U))) {
            return Status::ValueOutOfRange;
        }
        group->components[0] = { static_cast<uint32_t>(degrees), 1U };
        group->components[1] = { static_cast<uint32_t>(minutes), 1U };
        return rational32(seconds, &group->components[2]);
    }

    static bool gps_tag(const MetaStore& store, const Entry& entry,
                        uint16_t tag) noexcept
    {
        return entry.key.kind == MetaKeyKind::ExifTag
               && entry.key.data.exif_tag.tag == tag
               && text(store.arena(), entry.key.data.exif_tag.ifd) == "gpsifd";
    }

    static bool ratio_matches(URational a, URational b) noexcept
    {
        return a.denom != 0U && b.denom != 0U
               && static_cast<uint64_t>(a.numer) * b.denom
                      == static_cast<uint64_t>(b.numer) * a.denom;
    }

    static bool field_matches(const MetaStore& store, const MetaValue& value,
                              const GpsGroup& group, size_t member) noexcept
    {
        if (group.kind == GpsGroupKind::PlainText) {
            if (value.kind != MetaValueKind::Text) {
                return false;
            }
            std::string_view actual = text(store.arena(), value.data.span);
            if (!actual.empty() && actual.back() == '\0') {
                actual.remove_suffix(1U);
            }
            return actual == group.text_value;
        }
        if (group.kind == GpsGroupKind::EncodedText) {
            if (value.kind != MetaValueKind::Bytes) {
                return false;
            }
            const auto bytes = store.arena().span(value.data.span);
            // Equivalent encodings cannot need more than two bytes per UTF-8 byte,
            // plus the prefix, BOM, and optional terminator.
            if (bytes.size() > 12U + 2U * group.text_value.size()) {
                return false;
            }
            std::string decoded;
            const auto status
                = interop_internal::decode_exif_prefixed_text_safe(bytes,
                                                                   &decoded);
            return status != interop_internal::SafeTextStatus::Error
                   && decoded == group.text_value;
        }
        if (group.kind == GpsGroupKind::SingleRational) {
            return value.kind == MetaValueKind::Scalar && value.count == 1U
                   && value.elem_type == MetaElementType::URational
                   && ratio_matches(value.data.ur, group.magnitude);
        }
        if (group.kind == GpsGroupKind::SingleShort) {
            return value.kind == MetaValueKind::Scalar && value.count == 1U
                   && value.elem_type == MetaElementType::U16
                   && value.data.u64 == group.altitude_ref;
        }
        if (group.kind == GpsGroupKind::Timestamp && member == 1U) {
            if (value.kind != MetaValueKind::Text) {
                return false;
            }
            std::string_view date = text(store.arena(), value.data.span);
            if (date.size() == 11U && date.back() == '\0') {
                date.remove_suffix(1U);
            }
            return date
                   == std::string_view(group.date.data(), group.date.size());
        }
        if (group.kind == GpsGroupKind::Rational && member == 1U) {
            return value.kind == MetaValueKind::Scalar && value.count == 1U
                   && value.elem_type == MetaElementType::URational
                   && ratio_matches(value.data.ur, group.magnitude);
        }
        if (group.kind == GpsGroupKind::Altitude) {
            if (member == 0U) {
                return value.kind == MetaValueKind::Scalar && value.count == 1U
                       && value.elem_type == MetaElementType::U8
                       && value.data.u64 == group.altitude_ref;
            }
            return value.kind == MetaValueKind::Scalar && value.count == 1U
                   && value.elem_type == MetaElementType::URational
                   && ratio_matches(value.data.ur, group.magnitude);
        }
        if (member == 0U && group.kind != GpsGroupKind::Timestamp) {
            if (value.kind != MetaValueKind::Text) {
                return false;
            }
            std::string_view reference = text(store.arena(), value.data.span);
            if (reference.size() == 2U && reference.back() == '\0') {
                reference.remove_suffix(1U);
            }
            return reference.size() == 1U
                   && reference.front() == group.reference;
        }
        if (value.kind != MetaValueKind::Array || value.count != 3U
            || value.elem_type != MetaElementType::URational) {
            return false;
        }
        const std::span<const std::byte> bytes = store.arena().span(
            value.data.span);
        if (bytes.size() != 3U * sizeof(URational)) {
            return false;
        }
        for (size_t i = 0U; i < 3U; ++i) {
            URational actual;
            std::memcpy(&actual, bytes.data() + i * sizeof(URational),
                        sizeof(actual));
            if (!ratio_matches(actual, group.components[i])) {
                return false;
            }
        }
        return true;
    }

    static MetaValue native_value(ByteArena& arena, const GpsGroup& group,
                                  size_t member)
    {
        if (group.kind == GpsGroupKind::PlainText) {
            return make_text(arena, group.text_value, TextEncoding::Ascii);
        }
        if (group.kind == GpsGroupKind::EncodedText) {
            return make_bytes(arena,
                              std::as_bytes(
                                  std::span(group.encoded_value.data(),
                                            group.encoded_value.size())));
        }
        if (group.kind == GpsGroupKind::SingleRational) {
            return make_urational(group.magnitude.numer, group.magnitude.denom);
        }
        if (group.kind == GpsGroupKind::SingleShort) {
            return make_u16(group.altitude_ref);
        }
        if (group.kind == GpsGroupKind::Timestamp) {
            return member == 0U ? make_urational_array(arena, group.components)
                                : make_text(arena,
                                            std::string_view(group.date.data(),
                                                             group.date.size()),
                                            TextEncoding::Ascii);
        }
        if (group.kind == GpsGroupKind::Rational && member == 1U) {
            return make_urational(group.magnitude.numer, group.magnitude.denom);
        }
        if (group.kind == GpsGroupKind::Altitude) {
            return member == 0U ? make_u8(group.altitude_ref)
                                : make_urational(group.magnitude.numer,
                                                 group.magnitude.denom);
        }
        if (member == 0U) {
            return make_text(arena, std::string_view(&group.reference, 1U),
                             TextEncoding::Ascii);
        }
        return make_urational_array(arena, group.components);
    }

    static void append_entry(const MetaStore& source, EntryId source_id,
                             uint16_t tag, MetaValue value, MetaEdit* edit)
    {
        Entry entry;
        entry.key    = make_exif_tag_key(edit->arena(), "gpsifd", tag);
        entry.value  = value;
        entry.origin = source.entry(source_id).origin;
        if (entry.origin.wire_type_name.size != 0U) {
            entry.origin.wire_type_name = edit->arena().append(
                source.arena().span(entry.origin.wire_type_name));
        }
        entry.flags = EntryFlags::Dirty;
        edit->add_entry(entry);
    }

    static MetadataGpsTranslationResult
    error(Status status, Mapping mapping = Mapping::None,
          EntryId entry = kInvalidEntryId) noexcept
    {
        MetadataGpsTranslationResult result;
        result.status              = status;
        result.failed_mapping      = mapping;
        result.failed_source_entry = entry;
        return result;
    }

    static MetadataGpsTranslationResult
    apply_gps_groups(const MetaStore& source, std::span<GpsGroup> groups,
                     Policy policy, uint32_t max_added_entries,
                     uint32_t max_operations,
                     MetadataGpsTranslationResult result, MetaStore* out_store)
    {
        const std::span<const Entry> entries = source.entries();
        bool active_selected                 = false;
        for (const GpsGroup& group : groups) {
            active_selected = active_selected
                              || (group.selected && group.present);
        }
        EntryId version_entry  = kInvalidEntryId;
        uint32_t version_count = 0U;
        std::array<uint8_t, 4> version { 2U, 3U, 0U, 0U };
        for (EntryId id = 0U; id < entries.size(); ++id) {
            if (!any(entries[id].flags, EntryFlags::Deleted)
                && gps_tag(source, entries[id], 0U)) {
                version_entry = id;
                ++version_count;
            }
        }
        if (active_selected && version_count != 0U) {
            const MetaValue& value = entries[version_entry].value;
            if (version_count != 1U || value.kind != MetaValueKind::Array
                || value.elem_type != MetaElementType::U8
                || value.count != 4U) {
                return error(Status::NativeConflict, Mapping::GpsVersion);
            }
            const std::span<const std::byte> bytes = source.arena().span(
                value.data.span);
            if (bytes.size() != 4U) {
                return error(Status::NativeConflict, Mapping::GpsVersion);
            }
            std::memcpy(version.data(), bytes.data(), version.size());
        }
        for (GpsGroup& group : groups) {
            if (!group.selected || !group.present
                || (group.mapping != Mapping::ExifGpsAltitude
                    && group.mapping != Mapping::ExifGpsTimeStamp
                    && group.mapping != Mapping::ExifGpsDifferential
                    && group.mapping != Mapping::ExifGpsHPositioningError
                    && group.mapping != Mapping::ExifGpsProcessingMethod
                    && group.mapping != Mapping::ExifGpsAreaInformation)) {
                continue;
            }
            if (version[0] != 2U || version[1] > 4U || version[2] != 0U
                || version[3] != 0U
                || ((group.mapping == Mapping::ExifGpsTimeStamp
                     || group.mapping == Mapping::ExifGpsDifferential
                     || group.mapping == Mapping::ExifGpsProcessingMethod
                     || group.mapping == Mapping::ExifGpsAreaInformation)
                    && version[1] < 2U)
                || (group.mapping == Mapping::ExifGpsHPositioningError
                    && version[1] < 3U)) {
                return error(Status::UnsupportedGpsVersion,
                             Mapping::GpsVersion);
            }
            if (group.mapping == Mapping::ExifGpsAltitude && version[1] == 4U) {
                group.altitude_ref += 2U;
            }
        }

        uint32_t added         = 0U;
        uint32_t operations    = 0U;
        bool need_version      = false;
        bool removed_group     = false;
        EntryId version_source = kInvalidEntryId;
        for (GpsGroup& group : groups) {
            if (!group.selected) {
                continue;
            }
            for (EntryId id = 0U; id < entries.size(); ++id) {
                if (any(entries[id].flags, EntryFlags::Deleted)) {
                    continue;
                }
                for (size_t j = 0U; j < group.native_count; ++j) {
                    if (!gps_tag(source, entries[id], group.tags[j])) {
                        continue;
                    }
                    if (group.native_counts[j] >= max_operations) {
                        return error(Status::OperationLimitExceeded,
                                     group.mapping);
                    }
                    if (group.native_counts[j]++ == 0U) {
                        group.native_entries[j] = id;
                        group.matches[j]        = group.present
                                           && field_matches(source,
                                                            entries[id].value,
                                                            group, j);
                    }
                }
            }
            const bool existing = group.native_counts[0] != 0U
                                  || group.native_counts[1] != 0U;
            const bool exact = group.present
                                   ? group.native_counts[0] == 1U
                                         && group.matches[0]
                                         && (group.native_count == 1U
                                             || (group.native_counts[1] == 1U
                                                 && group.matches[1]))
                                   : !existing;
            if (policy == Policy::PreserveExisting && existing) {
                ++result.groups_preserved;
                continue;
            }
            if (policy == Policy::FailOnConflict && existing && !exact) {
                return error(Status::NativeConflict, group.mapping,
                             group.source_entries[0]);
            }
            if (exact) {
                ++result.groups_unchanged;
            } else {
                group.apply = true;
            }
            if (group.present) {
                need_version   = true;
                version_source = group.source_entries[0];
            } else if (group.apply) {
                removed_group = true;
            }
            if (!group.apply) {
                continue;
            }
            for (size_t j = 0U; j < group.native_count; ++j) {
                if (!group.present) {
                    operations += group.native_counts[j];
                } else if (group.native_counts[j] == 0U) {
                    ++added;
                    ++operations;
                } else {
                    operations += group.native_counts[j] - 1U
                                  + (group.matches[j] ? 0U : 1U);
                }
            }
        }
        bool remove_version = removed_group;
        if (remove_version) {
            for (const Entry& entry : entries) {
                if (any(entry.flags, EntryFlags::Deleted)
                    || entry.key.kind != MetaKeyKind::ExifTag
                    || text(source.arena(), entry.key.data.exif_tag.ifd)
                           != "gpsifd"
                    || entry.key.data.exif_tag.tag == 0U) {
                    continue;
                }
                bool removed = false;
                for (const GpsGroup& group : groups) {
                    removed
                        = removed
                          || (group.apply && !group.present
                              && (entry.key.data.exif_tag.tag == group.tags[0]
                                  || (group.native_count == 2U
                                      && entry.key.data.exif_tag.tag
                                             == group.tags[1])));
                }
                if (!removed) {
                    remove_version = false;
                }
            }
            if (need_version) {
                remove_version = false;
            }
        }
        const bool add_version = need_version && version_count == 0U;
        if (add_version) {
            ++added;
            ++operations;
        }
        if (remove_version) {
            if (version_count > max_operations) {
                return error(Status::OperationLimitExceeded,
                             Mapping::GpsVersion);
            }
            operations += version_count;
        }
        if (added > max_added_entries
            || entries.size() > static_cast<size_t>(kInvalidEntryId) - added) {
            return error(Status::EntryLimitExceeded);
        }
        if (operations > max_operations) {
            return error(Status::OperationLimitExceeded);
        }

        MetaEdit edit;
        edit.reserve_ops(operations);
        for (const GpsGroup& group : groups) {
            if (!group.apply) {
                continue;
            }
            for (size_t j = 0U; j < group.native_count; ++j) {
                const uint16_t tag = group.tags[j];
                for (EntryId id = 0U; id < entries.size(); ++id) {
                    if (any(entries[id].flags, EntryFlags::Deleted)
                        || !gps_tag(source, entries[id], tag)) {
                        continue;
                    }
                    if (!group.present || id != group.native_entries[j]) {
                        edit.tombstone(id);
                        ++result.entries_removed;
                    } else if (!group.matches[j]) {
                        edit.set_value(id,
                                       native_value(edit.arena(), group, j));
                        ++result.entries_updated;
                    }
                }
                if (group.present && group.native_counts[j] == 0U) {
                    append_entry(source, group.source_entries[j], tag,
                                 native_value(edit.arena(), group, j), &edit);
                    ++result.entries_added;
                }
            }
            ++result.groups_translated;
        }
        if (add_version) {
            append_entry(source, version_source, 0U,
                         make_u8_array(edit.arena(), version), &edit);
            ++result.entries_added;
        }
        if (remove_version) {
            for (EntryId id = 0U; id < entries.size(); ++id) {
                if (!any(entries[id].flags, EntryFlags::Deleted)
                    && gps_tag(source, entries[id], 0U)) {
                    edit.tombstone(id);
                    ++result.entries_removed;
                }
            }
        }
        if (edit.ops().size() != operations || result.entries_added != added
            || edit.arena().limit_exceeded()) {
            return error(Status::InternalError);
        }
        *out_store = commit(source, std::span<const MetaEdit>(&edit, 1U));
        return result;
    }

}  // namespace

MetadataGpsTranslationResult
translate_xmp_gps_metadata(const MetaStore& source,
                           const MetadataGpsTranslationOptions& options,
                           MetaStore* out_store)
{
    if (!out_store) {
        return error(Status::NullOutput);
    }
    if (!source.is_finalized()) {
        return error(Status::SourceNotFinalized);
    }
    if (options.max_added_entries == 0U
        || options.max_added_entries > kMetadataGpsTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataGpsTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataGpsTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataGpsTranslationMaxTotalTextBytes
        || (options.source_mode != MetadataGpsTranslationSourceMode::DirtyOnly
            && options.source_mode != MetadataGpsTranslationSourceMode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.latitude_to_exif && !options.longitude_to_exif
            && !options.altitude_to_exif)) {
        return error(Status::InvalidOptions);
    }

    static constexpr std::array<std::string_view, 4> paths {
        "GPSLatitude", "GPSLongitude", "GPSAltitude", "GPSAltitudeRef"
    };
    const std::array<bool, 3> enabled { options.latitude_to_exif,
                                        options.longitude_to_exif,
                                        options.altitude_to_exif };
    std::array<SourceProperty, 4> properties {};
    const std::span<const Entry> entries = source.entries();
    for (EntryId id = 0U; id < entries.size(); ++id) {
        const Entry& entry = entries[id];
        const bool dirty   = any(entry.flags, EntryFlags::Dirty);
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || (any(entry.flags, EntryFlags::Deleted) && !dirty)
            || text(source.arena(), entry.key.data.xmp_property.schema_ns)
                   != "http://ns.adobe.com/exif/1.0/") {
            continue;
        }
        const std::string_view path
            = text(source.arena(), entry.key.data.xmp_property.property_path);
        for (size_t i = 0U; i < properties.size(); ++i) {
            if (path != paths[i]) {
                continue;
            }
            SourceProperty& property = properties[i];
            property.dirty           = property.dirty || dirty;
            if (property.first == kInvalidEntryId) {
                property.first = id;
            } else {
                property.duplicate = id;
            }
        }
    }

    std::array<GpsGroup, 3> groups {};
    MetadataGpsTranslationResult result;
    uint64_t text_bytes = 0U;
    for (size_t i = 0U; i < groups.size(); ++i) {
        GpsGroup& group = groups[i];
        group.kind = i < 2U ? GpsGroupKind::Coordinate : GpsGroupKind::Altitude;
        group.mapping   = static_cast<Mapping>(
            static_cast<uint8_t>(Mapping::ExifGpsLatitude) + i);
        group.tags         = { static_cast<uint16_t>(1U + 2U * i),
                               static_cast<uint16_t>(2U + 2U * i) };
        const size_t count = i == 2U ? 2U : 1U;
        bool found         = false;
        bool dirty         = false;
        for (size_t j = 0U; j < count; ++j) {
            found = found || properties[i + j].first != kInvalidEntryId;
            dirty = dirty || properties[i + j].dirty;
        }
        if (!enabled[i] || !found
            || (options.source_mode
                    == MetadataGpsTranslationSourceMode::DirtyOnly
                && !dirty)) {
            continue;
        }
        group.selected = true;
        for (size_t j = 0U; j < count; ++j) {
            const SourceProperty& property = properties[i + j];
            if (property.duplicate != kInvalidEntryId) {
                return error(Status::AmbiguousSource, group.mapping,
                             property.duplicate);
            }
            if (property.first == kInvalidEntryId) {
                return error(Status::IncompleteSource, group.mapping,
                             properties[i].first);
            }
            const Entry& entry      = entries[property.first];
            group.source_entries[j] = property.first;
            ++result.source_properties;
            const bool present = !any(entry.flags, EntryFlags::Deleted);
            if (j == 0U) {
                group.present = present;
            } else if (group.present != present) {
                return error(Status::IncompleteSource, group.mapping,
                             property.first);
            }
            if (present && entry.value.kind == MetaValueKind::Text) {
                const uint64_t size = entry.value.data.span.size;
                if (size > options.max_text_bytes_per_property) {
                    return error(Status::ValueTooLong, group.mapping,
                                 property.first);
                }
                if (size > options.max_total_text_bytes
                    || text_bytes > options.max_total_text_bytes - size) {
                    return error(Status::SourceLimitExceeded, group.mapping,
                                 property.first);
                }
                text_bytes += size;
            }
        }
        if (i < 2U) {
            group.source_entries[1] = group.source_entries[0];
        }
        if (!group.present) {
            continue;
        }
        Status status;
        if (i < 2U) {
            status = coordinate_value(source,
                                      entries[group.source_entries[0]].value,
                                      i == 0U, &group);
        } else {
            status = unsigned_rational_value(
                source, entries[group.source_entries[0]].value,
                &group.magnitude);
            uint64_t reference     = 0U;
            const MetaValue& value = entries[group.source_entries[1]].value;
            if (status == Status::Ok) {
                if (value.kind == MetaValueKind::Text) {
                    const std::string_view reference_text
                        = text(source.arena(), value.data.span);
                    status = reference_text == "0" || reference_text == "1"
                                 ? Status::Ok
                                 : Status::InvalidSourceValue;
                    if (status == Status::Ok) {
                        reference = static_cast<uint64_t>(reference_text[0]
                                                          - '0');
                    }
                } else if (!integer(value, &reference) || reference > 1U) {
                    status = Status::InvalidSourceValue;
                }
                if (status != Status::Ok) {
                    return error(status, group.mapping,
                                 group.source_entries[1]);
                }
            }
            group.altitude_ref = static_cast<uint8_t>(reference);
            // Native reference precedes altitude; provenance follows each XMP member.
            std::swap(group.source_entries[0], group.source_entries[1]);
        }
        if (status != Status::Ok) {
            return error(status, group.mapping,
                         i == 2U ? group.source_entries[1]
                                 : group.source_entries[0]);
        }
    }

    return apply_gps_groups(source, groups, options.conflict_policy,
                            options.max_added_entries, options.max_operations,
                            result, out_store);
}

MetadataGpsTranslationResult
translate_xmp_gps_navigation_metadata(
    const MetaStore& source,
    const MetadataGpsNavigationTranslationOptions& options,
    MetaStore* out_store)
{
    if (!out_store) {
        return error(Status::NullOutput);
    }
    if (!source.is_finalized()) {
        return error(Status::SourceNotFinalized);
    }
    if (options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataGpsNavigationTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataGpsTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataGpsTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataGpsNavigationTranslationMaxTotalTextBytes
        || (options.source_mode != MetadataGpsTranslationSourceMode::DirtyOnly
            && options.source_mode != MetadataGpsTranslationSourceMode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.timestamp_to_exif && !options.speed_to_exif
            && !options.track_to_exif && !options.image_direction_to_exif)) {
        return error(Status::InvalidOptions);
    }
    static constexpr std::array<std::string_view, 7> paths {
        "GPSTimeStamp", "GPSSpeedRef",        "GPSSpeed",       "GPSTrackRef",
        "GPSTrack",     "GPSImgDirectionRef", "GPSImgDirection"
    };
    const std::array<bool, 4> enabled { options.timestamp_to_exif,
                                        options.speed_to_exif,
                                        options.track_to_exif,
                                        options.image_direction_to_exif };
    std::array<SourceProperty, 7> properties {};
    const std::span<const Entry> entries = source.entries();
    for (EntryId id = 0U; id < entries.size(); ++id) {
        const Entry& entry = entries[id];
        const bool dirty   = any(entry.flags, EntryFlags::Dirty);
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || (any(entry.flags, EntryFlags::Deleted) && !dirty)
            || text(source.arena(), entry.key.data.xmp_property.schema_ns)
                   != "http://ns.adobe.com/exif/1.0/") {
            continue;
        }
        const std::string_view path
            = text(source.arena(), entry.key.data.xmp_property.property_path);
        for (size_t i = 0U; i < properties.size(); ++i) {
            if (path == paths[i]) {
                SourceProperty& property = properties[i];
                property.dirty           = property.dirty || dirty;
                if (property.first == kInvalidEntryId) {
                    property.first = id;
                } else {
                    property.duplicate = id;
                }
            }
        }
    }
    std::array<GpsGroup, 4> groups {};
    MetadataGpsTranslationResult result;
    uint64_t text_bytes = 0U;
    for (size_t i = 0U; i < groups.size(); ++i) {
        GpsGroup& group = groups[i];
        group.kind = i == 0U ? GpsGroupKind::Timestamp : GpsGroupKind::Rational;
        group.mapping   = static_cast<Mapping>(
            static_cast<uint8_t>(Mapping::ExifGpsTimeStamp) + i);
        group.tags         = i == 0U ? std::array<uint16_t, 2> { 7U, 29U }
                                     : std::array<uint16_t, 2> {
                                   static_cast<uint16_t>(10U + 2U * i),
                                   static_cast<uint16_t>(11U + 2U * i)
                               };
        const size_t first = i == 0U ? 0U : 2U * i - 1U;
        const size_t count = i == 0U ? 1U : 2U;
        bool found         = false;
        bool dirty         = false;
        for (size_t j = 0U; j < count; ++j) {
            found = found || properties[first + j].first != kInvalidEntryId;
            dirty = dirty || properties[first + j].dirty;
        }
        if (!enabled[i] || !found
            || (options.source_mode
                    == MetadataGpsTranslationSourceMode::DirtyOnly
                && !dirty)) {
            continue;
        }
        group.selected = true;
        for (size_t j = 0U; j < count; ++j) {
            const SourceProperty& property = properties[first + j];
            if (property.duplicate != kInvalidEntryId) {
                return error(Status::AmbiguousSource, group.mapping,
                             property.duplicate);
            }
            if (property.first == kInvalidEntryId) {
                return error(Status::IncompleteSource, group.mapping,
                             properties[first].first);
            }
            const Entry& entry      = entries[property.first];
            group.source_entries[j] = property.first;
            ++result.source_properties;
            const bool present = !any(entry.flags, EntryFlags::Deleted);
            if (j == 0U) {
                group.present = present;
            } else if (group.present != present) {
                return error(Status::IncompleteSource, group.mapping,
                             property.first);
            }
            if (present && entry.value.kind == MetaValueKind::Text) {
                const uint64_t size = entry.value.data.span.size;
                if (size > options.max_text_bytes_per_property) {
                    return error(Status::ValueTooLong, group.mapping,
                                 property.first);
                }
                if (size > options.max_total_text_bytes
                    || text_bytes > options.max_total_text_bytes - size) {
                    return error(Status::SourceLimitExceeded, group.mapping,
                                 property.first);
                }
                text_bytes += size;
            }
        }
        if (i == 0U) {
            group.source_entries[1] = group.source_entries[0];
        }
        if (!group.present) {
            continue;
        }
        Status status;
        if (i == 0U) {
            status = timestamp_value(source,
                                     entries[group.source_entries[0]].value,
                                     &group);
        } else {
            status = navigation_reference(source,
                                          entries[group.source_entries[0]].value,
                                          i == 1U, &group.reference);
            if (status == Status::Ok) {
                status = unsigned_rational_value(
                    source, entries[group.source_entries[1]].value,
                    &group.magnitude);
                if (status == Status::Ok && i > 1U
                    && static_cast<uint64_t>(group.magnitude.numer) * 100U
                           > static_cast<uint64_t>(group.magnitude.denom)
                                 * 35999U) {
                    status = Status::ValueOutOfRange;
                }
                if (status != Status::Ok) {
                    return error(status, group.mapping,
                                 group.source_entries[1]);
                }
            }
        }
        if (status != Status::Ok) {
            return error(status, group.mapping, group.source_entries[0]);
        }
    }
    return apply_gps_groups(source, groups, options.conflict_policy,
                            options.max_added_entries, options.max_operations,
                            result, out_store);
}

MetadataGpsTranslationResult
translate_xmp_gps_destination_metadata(
    const MetaStore& source,
    const MetadataGpsDestinationTranslationOptions& options,
    MetaStore* out_store)
{
    if (!out_store) {
        return error(Status::NullOutput);
    }
    if (!source.is_finalized()) {
        return error(Status::SourceNotFinalized);
    }
    if (options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataGpsDestinationTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataGpsTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataGpsTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataGpsDestinationTranslationMaxTotalTextBytes
        || (options.source_mode != MetadataGpsTranslationSourceMode::DirtyOnly
            && options.source_mode != MetadataGpsTranslationSourceMode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.latitude_to_exif && !options.longitude_to_exif
            && !options.bearing_to_exif && !options.distance_to_exif)) {
        return error(Status::InvalidOptions);
    }
    static constexpr std::array<std::string_view, 6> paths {
        "GPSDestLatitude", "GPSDestLongitude",   "GPSDestBearingRef",
        "GPSDestBearing",  "GPSDestDistanceRef", "GPSDestDistance"
    };
    const std::array<bool, 4> enabled { options.latitude_to_exif,
                                        options.longitude_to_exif,
                                        options.bearing_to_exif,
                                        options.distance_to_exif };
    std::array<SourceProperty, 6> properties {};
    const std::span<const Entry> entries = source.entries();
    for (EntryId id = 0U; id < entries.size(); ++id) {
        const Entry& entry = entries[id];
        const bool dirty   = any(entry.flags, EntryFlags::Dirty);
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || (any(entry.flags, EntryFlags::Deleted) && !dirty)
            || text(source.arena(), entry.key.data.xmp_property.schema_ns)
                   != "http://ns.adobe.com/exif/1.0/") {
            continue;
        }
        const std::string_view path
            = text(source.arena(), entry.key.data.xmp_property.property_path);
        for (size_t i = 0U; i < properties.size(); ++i) {
            if (path == paths[i]) {
                SourceProperty& property = properties[i];
                property.dirty           = property.dirty || dirty;
                if (property.first == kInvalidEntryId) {
                    property.first = id;
                } else {
                    property.duplicate = id;
                }
            }
        }
    }
    std::array<GpsGroup, 4> groups {};
    MetadataGpsTranslationResult result;
    uint64_t text_bytes = 0U;
    for (size_t i = 0U; i < groups.size(); ++i) {
        GpsGroup& group = groups[i];
        group.kind = i < 2U ? GpsGroupKind::Coordinate : GpsGroupKind::Rational;
        group.mapping = static_cast<Mapping>(
            static_cast<uint8_t>(Mapping::ExifGpsDestLatitude) + i);
        group.tags         = { static_cast<uint16_t>(19U + 2U * i),
                               static_cast<uint16_t>(20U + 2U * i) };
        const size_t first = i < 2U ? i : 2U * i - 2U;
        const size_t count = i < 2U ? 1U : 2U;
        bool found         = false;
        bool dirty         = false;
        for (size_t j = 0U; j < count; ++j) {
            found = found || properties[first + j].first != kInvalidEntryId;
            dirty = dirty || properties[first + j].dirty;
        }
        if (!enabled[i] || !found
            || (options.source_mode
                    == MetadataGpsTranslationSourceMode::DirtyOnly
                && !dirty)) {
            continue;
        }
        group.selected = true;
        for (size_t j = 0U; j < count; ++j) {
            const SourceProperty& property = properties[first + j];
            if (property.duplicate != kInvalidEntryId) {
                return error(Status::AmbiguousSource, group.mapping,
                             property.duplicate);
            }
            if (property.first == kInvalidEntryId) {
                return error(Status::IncompleteSource, group.mapping,
                             properties[first].first);
            }
            const Entry& entry      = entries[property.first];
            group.source_entries[j] = property.first;
            ++result.source_properties;
            const bool present = !any(entry.flags, EntryFlags::Deleted);
            if (j == 0U) {
                group.present = present;
            } else if (group.present != present) {
                return error(Status::IncompleteSource, group.mapping,
                             property.first);
            }
            if (present && entry.value.kind == MetaValueKind::Text) {
                const uint64_t size = entry.value.data.span.size;
                if (size > options.max_text_bytes_per_property) {
                    return error(Status::ValueTooLong, group.mapping,
                                 property.first);
                }
                if (size > options.max_total_text_bytes
                    || text_bytes > options.max_total_text_bytes - size) {
                    return error(Status::SourceLimitExceeded, group.mapping,
                                 property.first);
                }
                text_bytes += size;
            }
        }
        if (i < 2U) {
            group.source_entries[1] = group.source_entries[0];
        }
        if (!group.present) {
            continue;
        }
        Status status;
        if (i < 2U) {
            status = coordinate_value(source,
                                      entries[group.source_entries[0]].value,
                                      i == 0U, &group);
        } else {
            status = i == 2U
                         ? navigation_reference(
                               source, entries[group.source_entries[0]].value,
                               false, &group.reference)
                         : distance_reference(
                               source, entries[group.source_entries[0]].value,
                               &group.reference);
            if (status == Status::Ok) {
                status = unsigned_rational_value(
                    source, entries[group.source_entries[1]].value,
                    &group.magnitude);
                if (status == Status::Ok && i == 2U
                    && static_cast<uint64_t>(group.magnitude.numer) * 100U
                           > static_cast<uint64_t>(group.magnitude.denom)
                                 * 35999U) {
                    status = Status::ValueOutOfRange;
                }
                if (status != Status::Ok) {
                    return error(status, group.mapping,
                                 group.source_entries[1]);
                }
            }
        }
        if (status != Status::Ok) {
            return error(status, group.mapping, group.source_entries[0]);
        }
    }
    return apply_gps_groups(source, groups, options.conflict_policy,
                            options.max_added_entries, options.max_operations,
                            result, out_store);
}

MetadataGpsTranslationResult
translate_xmp_gps_quality_metadata(
    const MetaStore& source,
    const MetadataGpsQualityTranslationOptions& options, MetaStore* out_store)
{
    if (!out_store) {
        return error(Status::NullOutput);
    }
    if (!source.is_finalized()) {
        return error(Status::SourceNotFinalized);
    }
    if (options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataGpsQualityTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataGpsTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataGpsTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataGpsQualityTranslationMaxTotalTextBytes
        || (options.source_mode != MetadataGpsTranslationSourceMode::DirtyOnly
            && options.source_mode != MetadataGpsTranslationSourceMode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.status_to_exif && !options.measure_mode_to_exif
            && !options.dop_to_exif && !options.differential_to_exif
            && !options.horizontal_error_to_exif)) {
        return error(Status::InvalidOptions);
    }
    static constexpr std::array<std::string_view, 5> paths {
        "GPSStatus", "GPSMeasureMode", "GPSDOP", "GPSDifferential",
        "GPSHPositioningError"
    };
    static constexpr std::array<uint16_t, 5> tags { 9U, 10U, 11U, 30U, 31U };
    const std::array<bool, 5> enabled { options.status_to_exif,
                                        options.measure_mode_to_exif,
                                        options.dop_to_exif,
                                        options.differential_to_exif,
                                        options.horizontal_error_to_exif };
    std::array<SourceProperty, 5> properties {};
    const std::span<const Entry> entries = source.entries();
    for (EntryId id = 0U; id < entries.size(); ++id) {
        const Entry& entry = entries[id];
        const bool dirty   = any(entry.flags, EntryFlags::Dirty);
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || (any(entry.flags, EntryFlags::Deleted) && !dirty)
            || text(source.arena(), entry.key.data.xmp_property.schema_ns)
                   != "http://ns.adobe.com/exif/1.0/") {
            continue;
        }
        const std::string_view path
            = text(source.arena(), entry.key.data.xmp_property.property_path);
        for (size_t i = 0U; i < properties.size(); ++i) {
            if (path == paths[i]) {
                SourceProperty& property = properties[i];
                property.dirty           = property.dirty || dirty;
                if (property.first == kInvalidEntryId) {
                    property.first = id;
                } else {
                    property.duplicate = id;
                }
            }
        }
    }
    std::array<GpsGroup, 5> groups {};
    MetadataGpsTranslationResult result;
    uint64_t text_bytes = 0U;
    for (size_t i = 0U; i < groups.size(); ++i) {
        GpsGroup& group    = groups[i];
        group.native_count = 1U;
        group.kind         = i == 2U || i == 4U ? GpsGroupKind::SingleRational
                             : i == 3U          ? GpsGroupKind::SingleShort
                                                : GpsGroupKind::SingleReference;
        group.mapping      = static_cast<Mapping>(
            static_cast<uint8_t>(Mapping::ExifGpsStatus) + i);
        group.tags[0]      = tags[i];
        const size_t first = i;
        const size_t count = 1U;
        bool found         = false;
        bool dirty         = false;
        for (size_t j = 0U; j < count; ++j) {
            found = found || properties[first + j].first != kInvalidEntryId;
            dirty = dirty || properties[first + j].dirty;
        }
        if (!enabled[i] || !found
            || (options.source_mode
                    == MetadataGpsTranslationSourceMode::DirtyOnly
                && !dirty)) {
            continue;
        }
        group.selected = true;
        for (size_t j = 0U; j < count; ++j) {
            const SourceProperty& property = properties[first + j];
            if (property.duplicate != kInvalidEntryId) {
                return error(Status::AmbiguousSource, group.mapping,
                             property.duplicate);
            }
            if (property.first == kInvalidEntryId) {
                return error(Status::IncompleteSource, group.mapping,
                             properties[first].first);
            }
            const Entry& entry      = entries[property.first];
            group.source_entries[j] = property.first;
            ++result.source_properties;
            const bool present = !any(entry.flags, EntryFlags::Deleted);
            if (j == 0U) {
                group.present = present;
            } else if (group.present != present) {
                return error(Status::IncompleteSource, group.mapping,
                             property.first);
            }
            if (present && entry.value.kind == MetaValueKind::Text) {
                const uint64_t size = entry.value.data.span.size;
                if (size > options.max_text_bytes_per_property) {
                    return error(Status::ValueTooLong, group.mapping,
                                 property.first);
                }
                if (size > options.max_total_text_bytes
                    || text_bytes > options.max_total_text_bytes - size) {
                    return error(Status::SourceLimitExceeded, group.mapping,
                                 property.first);
                }
                text_bytes += size;
            }
        }
        if (!group.present) {
            continue;
        }
        const Status status
            = quality_value(source, entries[group.source_entries[0]].value,
                            &group);
        if (status != Status::Ok) {
            return error(status, group.mapping, group.source_entries[0]);
        }
    }
    return apply_gps_groups(source, groups, options.conflict_policy,
                            options.max_added_entries, options.max_operations,
                            result, out_store);
}

MetadataGpsTranslationResult
translate_xmp_gps_text_metadata(const MetaStore& source,
                                const MetadataGpsTextTranslationOptions& options,
                                MetaStore* out_store)
{
    if (!out_store) {
        return error(Status::NullOutput);
    }
    if (!source.is_finalized()) {
        return error(Status::SourceNotFinalized);
    }
    if (options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataGpsTextTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataGpsTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataGpsTextTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataGpsTextTranslationMaxTotalTextBytes
        || (options.source_mode != MetadataGpsTranslationSourceMode::DirtyOnly
            && options.source_mode != MetadataGpsTranslationSourceMode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.satellites_to_exif && !options.map_datum_to_exif
            && !options.processing_method_to_exif
            && !options.area_information_to_exif)) {
        return error(Status::InvalidOptions);
    }
    static constexpr std::array<std::string_view, 4> paths {
        "GPSSatellites", "GPSMapDatum", "GPSProcessingMethod",
        "GPSAreaInformation"
    };
    static constexpr std::array<uint16_t, 4> tags { 8U, 18U, 27U, 28U };
    const std::array<bool, 4> enabled { options.satellites_to_exif,
                                        options.map_datum_to_exif,
                                        options.processing_method_to_exif,
                                        options.area_information_to_exif };
    std::array<SourceProperty, 4> properties {};
    const std::span<const Entry> entries = source.entries();
    for (EntryId id = 0U; id < entries.size(); ++id) {
        const Entry& entry = entries[id];
        const bool dirty   = any(entry.flags, EntryFlags::Dirty);
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || (any(entry.flags, EntryFlags::Deleted) && !dirty)
            || text(source.arena(), entry.key.data.xmp_property.schema_ns)
                   != "http://ns.adobe.com/exif/1.0/") {
            continue;
        }
        const std::string_view path
            = text(source.arena(), entry.key.data.xmp_property.property_path);
        for (size_t i = 0U; i < properties.size(); ++i) {
            if (path == paths[i]) {
                SourceProperty& property = properties[i];
                property.dirty           = property.dirty || dirty;
                if (property.first == kInvalidEntryId) {
                    property.first = id;
                } else {
                    property.duplicate = id;
                }
            }
        }
    }
    std::array<GpsGroup, 4> groups {};
    MetadataGpsTranslationResult result;
    uint64_t text_bytes = 0U;
    for (size_t i = 0U; i < groups.size(); ++i) {
        GpsGroup& group    = groups[i];
        group.native_count = 1U;
        group.kind         = i < 2U ? GpsGroupKind::PlainText
                                    : GpsGroupKind::EncodedText;
        group.mapping      = static_cast<Mapping>(
            static_cast<uint8_t>(Mapping::ExifGpsSatellites) + i);
        group.tags[0]      = tags[i];
        const size_t first = i;
        const size_t count = 1U;
        bool found         = false;
        bool dirty         = false;
        for (size_t j = 0U; j < count; ++j) {
            found = found || properties[first + j].first != kInvalidEntryId;
            dirty = dirty || properties[first + j].dirty;
        }
        if (!enabled[i] || !found
            || (options.source_mode
                    == MetadataGpsTranslationSourceMode::DirtyOnly
                && !dirty)) {
            continue;
        }
        group.selected = true;
        for (size_t j = 0U; j < count; ++j) {
            const SourceProperty& property = properties[first + j];
            if (property.duplicate != kInvalidEntryId) {
                return error(Status::AmbiguousSource, group.mapping,
                             property.duplicate);
            }
            if (property.first == kInvalidEntryId) {
                return error(Status::IncompleteSource, group.mapping,
                             properties[first].first);
            }
            const Entry& entry      = entries[property.first];
            group.source_entries[j] = property.first;
            ++result.source_properties;
            const bool present = !any(entry.flags, EntryFlags::Deleted);
            if (j == 0U) {
                group.present = present;
            } else if (group.present != present) {
                return error(Status::IncompleteSource, group.mapping,
                             property.first);
            }
            if (present && entry.value.kind == MetaValueKind::Text) {
                const uint64_t size = entry.value.data.span.size;
                if (size > options.max_text_bytes_per_property) {
                    return error(Status::ValueTooLong, group.mapping,
                                 property.first);
                }
                if (size > options.max_total_text_bytes
                    || text_bytes > options.max_total_text_bytes - size) {
                    return error(Status::SourceLimitExceeded, group.mapping,
                                 property.first);
                }
                text_bytes += size;
            }
        }
        if (!group.present) {
            continue;
        }
        const MetaValue& value = entries[group.source_entries[0]].value;
        if (value.kind != MetaValueKind::Text
            || (value.text_encoding != TextEncoding::Ascii
                && value.text_encoding != TextEncoding::Utf8
                && value.text_encoding != TextEncoding::Unknown)) {
            return error(Status::InvalidSourceValue, group.mapping,
                         group.source_entries[0]);
        }
        const auto bytes    = source.arena().span(value.data.span);
        const auto encoding = i < 2U ? TextEncoding::Ascii
                                     : value.text_encoding;
        const auto status   = interop_internal::decode_text_to_utf8_safe(
            bytes, encoding, paths[i], paths[i], &group.text_value, nullptr);
        if (status == interop_internal::SafeTextStatus::Error) {
            return error(Status::InvalidSourceValue, group.mapping,
                         group.source_entries[0]);
        }
        if (i >= 2U) {
            interop_internal::encode_exif_prefixed_text_from_valid_utf8(
                group.text_value, &group.encoded_value);
        }
    }
    return apply_gps_groups(source, groups, options.conflict_policy,
                            options.max_added_entries, options.max_operations,
                            result, out_store);
}

const char*
metadata_gps_translation_status_name(MetadataGpsTranslationStatus status) noexcept
{
    switch (status) {
    case Status::Ok: return "ok";
    case Status::NullOutput: return "null_output";
    case Status::SourceNotFinalized: return "source_not_finalized";
    case Status::InvalidOptions: return "invalid_options";
    case Status::AmbiguousSource: return "ambiguous_source";
    case Status::IncompleteSource: return "incomplete_source";
    case Status::InvalidSourceValue: return "invalid_source_value";
    case Status::ValueOutOfRange: return "value_out_of_range";
    case Status::UnsupportedPrecision: return "unsupported_precision";
    case Status::UnsupportedGpsVersion: return "unsupported_gps_version";
    case Status::ValueTooLong: return "value_too_long";
    case Status::SourceLimitExceeded: return "source_limit_exceeded";
    case Status::NativeConflict: return "native_conflict";
    case Status::EntryLimitExceeded: return "entry_limit_exceeded";
    case Status::OperationLimitExceeded: return "operation_limit_exceeded";
    case Status::InternalError: return "internal_error";
    }
    return "unknown";
}

const char*
metadata_gps_translation_mapping_name(
    MetadataGpsTranslationMapping mapping) noexcept
{
    switch (mapping) {
    case Mapping::None: return "none";
    case Mapping::ExifGpsLatitude: return "exif_gps_latitude";
    case Mapping::ExifGpsLongitude: return "exif_gps_longitude";
    case Mapping::ExifGpsAltitude: return "exif_gps_altitude";
    case Mapping::GpsVersion: return "gps_version";
    case Mapping::ExifGpsTimeStamp: return "exif_gps_timestamp";
    case Mapping::ExifGpsSpeed: return "exif_gps_speed";
    case Mapping::ExifGpsTrack: return "exif_gps_track";
    case Mapping::ExifGpsImgDirection: return "exif_gps_image_direction";
    case Mapping::ExifGpsDestLatitude: return "exif_gps_dest_latitude";
    case Mapping::ExifGpsDestLongitude: return "exif_gps_dest_longitude";
    case Mapping::ExifGpsDestBearing: return "exif_gps_dest_bearing";
    case Mapping::ExifGpsDestDistance: return "exif_gps_dest_distance";
    case Mapping::ExifGpsStatus: return "exif_gps_status";
    case Mapping::ExifGpsMeasureMode: return "exif_gps_measure_mode";
    case Mapping::ExifGpsDop: return "exif_gps_dop";
    case Mapping::ExifGpsDifferential: return "exif_gps_differential";
    case Mapping::ExifGpsHPositioningError: return "exif_gps_horizontal_error";
    case Mapping::ExifGpsSatellites: return "exif_gps_satellites";
    case Mapping::ExifGpsMapDatum: return "exif_gps_map_datum";
    case Mapping::ExifGpsProcessingMethod: return "exif_gps_processing_method";
    case Mapping::ExifGpsAreaInformation: return "exif_gps_area_information";
    }
    return "unknown";
}

}  // namespace openmeta
