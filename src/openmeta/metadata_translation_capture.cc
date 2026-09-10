// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_translation.h"

#include "openmeta/exif_value_names.h"
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

    enum class NativeCaptureField : uint8_t {
        ExposureTime,
        FNumber,
        Iso,
        FocalLength,
        ExposureBias,
        ExposureProgram,
        MeteringMode,
        SensingMethod,
        CustomRendered,
        ExposureMode,
        WhiteBalance,
        SceneCaptureType,
        GainControl,
        Contrast,
        Saturation,
        Sharpness,
        SubjectDistanceRange,
    };

    enum class NumericParseStatus : uint8_t {
        Ok,
        Invalid,
        OutOfRange,
    };

    struct ExactRatio final {
        uint64_t numerator   = 0U;
        uint64_t denominator = 1U;
        bool negative        = false;
    };

    struct CaptureSource final {
        bool found             = false;
        bool deleted           = false;
        EntryId entry_id       = kInvalidEntryId;
        const MetaValue* value = nullptr;
    };

    struct CapturePlannedGroup final {
        MetadataCaptureTranslationMapping mapping
            = MetadataCaptureTranslationMapping::None;
        NativeCaptureField field = NativeCaptureField::ExposureTime;
        EntryId source_entry     = kInvalidEntryId;
        bool present             = false;
        MetaValue value;
        bool existing_any = false;
        bool exact_match  = false;
        bool apply        = false;
    };

    static std::string_view arena_text(const ByteArena& arena,
                                       ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static uint64_t gcd_u64(uint64_t a, uint64_t b) noexcept
    {
        while (b != 0U) {
            const uint64_t next = a % b;
            a                   = b;
            b                   = next;
        }
        return a == 0U ? 1U : a;
    }

    static NumericParseStatus parse_digits(std::string_view text,
                                           uint64_t* out) noexcept
    {
        if (!out || text.empty()) {
            return NumericParseStatus::Invalid;
        }
        uint64_t value = 0U;
        for (const char c : text) {
            if (c < '0' || c > '9') {
                return NumericParseStatus::Invalid;
            }
            const uint64_t digit = static_cast<uint64_t>(c - '0');
            if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10U) {
                return NumericParseStatus::OutOfRange;
            }
            value = value * 10U + digit;
        }
        *out = value;
        return NumericParseStatus::Ok;
    }

    static bool pow10_u64(uint32_t exponent, uint64_t* out) noexcept
    {
        if (!out || exponent > 19U) {
            return false;
        }
        uint64_t value = 1U;
        for (uint32_t i = 0U; i < exponent; ++i) {
            if (value > std::numeric_limits<uint64_t>::max() / 10U) {
                return false;
            }
            value *= 10U;
        }
        *out = value;
        return true;
    }

    static NumericParseStatus parse_exact_ratio(std::string_view text,
                                                bool allow_negative,
                                                ExactRatio* out) noexcept
    {
        if (!out || text.empty()) {
            return NumericParseStatus::Invalid;
        }

        ExactRatio parsed;
        if (text.front() == '+' || text.front() == '-') {
            parsed.negative = text.front() == '-';
            if (parsed.negative && !allow_negative) {
                return NumericParseStatus::Invalid;
            }
            text.remove_prefix(1U);
            if (text.empty()) {
                return NumericParseStatus::Invalid;
            }
        }

        const size_t slash = text.find('/');
        if (slash != std::string_view::npos) {
            if (text.find('/', slash + 1U) != std::string_view::npos) {
                return NumericParseStatus::Invalid;
            }
            NumericParseStatus status = parse_digits(text.substr(0U, slash),
                                                     &parsed.numerator);
            if (status != NumericParseStatus::Ok) {
                return status;
            }
            status = parse_digits(text.substr(slash + 1U), &parsed.denominator);
            if (status != NumericParseStatus::Ok) {
                return status;
            }
            if (parsed.denominator == 0U) {
                return NumericParseStatus::Invalid;
            }
        } else {
            uint64_t mantissa          = 0U;
            uint32_t fractional_digits = 0U;
            bool decimal_seen          = false;
            bool digit_seen            = false;
            bool fractional_digit_seen = false;
            size_t position            = 0U;
            for (; position < text.size(); ++position) {
                const char c = text[position];
                if (c >= '0' && c <= '9') {
                    digit_seen = true;
                    if (decimal_seen) {
                        fractional_digit_seen = true;
                        ++fractional_digits;
                    }
                    const uint64_t digit = static_cast<uint64_t>(c - '0');
                    if (mantissa
                        > (std::numeric_limits<uint64_t>::max() - digit)
                              / 10U) {
                        return NumericParseStatus::OutOfRange;
                    }
                    mantissa = mantissa * 10U + digit;
                    continue;
                }
                if (c == '.' && !decimal_seen) {
                    if (!digit_seen) {
                        return NumericParseStatus::Invalid;
                    }
                    decimal_seen = true;
                    continue;
                }
                break;
            }
            if (!digit_seen || (decimal_seen && !fractional_digit_seen)) {
                return NumericParseStatus::Invalid;
            }

            int32_t exponent = 0;
            if (position < text.size()) {
                if (text[position] != 'e' && text[position] != 'E') {
                    return NumericParseStatus::Invalid;
                }
                ++position;
                bool exponent_negative = false;
                if (position < text.size()
                    && (text[position] == '+' || text[position] == '-')) {
                    exponent_negative = text[position] == '-';
                    ++position;
                }
                if (position == text.size()) {
                    return NumericParseStatus::Invalid;
                }
                uint64_t exponent_magnitude = 0U;
                const NumericParseStatus exponent_status
                    = parse_digits(text.substr(position), &exponent_magnitude);
                if (exponent_status != NumericParseStatus::Ok) {
                    return exponent_status;
                }
                if (exponent_magnitude > 1000U) {
                    return NumericParseStatus::OutOfRange;
                }
                exponent = static_cast<int32_t>(exponent_magnitude);
                if (exponent_negative) {
                    exponent = -exponent;
                }
            }

            parsed.numerator    = mantissa;
            const int64_t scale = static_cast<int64_t>(exponent)
                                  - static_cast<int64_t>(fractional_digits);
            if (scale >= 0) {
                uint64_t multiplier = 0U;
                if (scale > 19
                    || !pow10_u64(static_cast<uint32_t>(scale), &multiplier)
                    || (mantissa != 0U
                        && mantissa > std::numeric_limits<uint64_t>::max()
                                          / multiplier)) {
                    return NumericParseStatus::OutOfRange;
                }
                parsed.numerator *= multiplier;
                parsed.denominator = 1U;
            } else {
                const uint64_t denominator_exponent = static_cast<uint64_t>(
                    -scale);
                if (denominator_exponent > 19U
                    || !pow10_u64(static_cast<uint32_t>(denominator_exponent),
                                  &parsed.denominator)) {
                    return NumericParseStatus::OutOfRange;
                }
            }
        }

        const uint64_t divisor = gcd_u64(parsed.numerator, parsed.denominator);
        parsed.numerator /= divisor;
        parsed.denominator /= divisor;
        if (parsed.numerator == 0U) {
            parsed.negative = false;
        }
        *out = parsed;
        return NumericParseStatus::Ok;
    }

    static bool scalar_unsigned(const MetaValue& value, uint64_t* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: *out = value.data.u64; return true;
        default: return false;
        }
    }

    static bool scalar_signed(const MetaValue& value, int64_t* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64: *out = value.data.i64; return true;
        default: return false;
        }
    }

    static MetadataCaptureTranslationStatus
    numeric_status(NumericParseStatus status) noexcept
    {
        switch (status) {
        case NumericParseStatus::Ok:
            return MetadataCaptureTranslationStatus::Ok;
        case NumericParseStatus::Invalid:
            return MetadataCaptureTranslationStatus::InvalidNumericValue;
        case NumericParseStatus::OutOfRange:
            return MetadataCaptureTranslationStatus::ValueOutOfRange;
        }
        return MetadataCaptureTranslationStatus::InternalError;
    }

    static MetadataCaptureTranslationStatus
    parse_unsigned_rational_source(const ByteArena& arena,
                                   const MetaValue& value, bool allow_mm_suffix,
                                   MetaValue* out) noexcept
    {
        if (!out) {
            return MetadataCaptureTranslationStatus::InternalError;
        }

        ExactRatio ratio;
        if (value.kind == MetaValueKind::Scalar
            && value.elem_type == MetaElementType::URational) {
            if (value.data.ur.denom == 0U || value.data.ur.numer == 0U) {
                return MetadataCaptureTranslationStatus::InvalidNumericValue;
            }
            ratio.numerator   = value.data.ur.numer;
            ratio.denominator = value.data.ur.denom;
        } else {
            uint64_t integer = 0U;
            if (scalar_unsigned(value, &integer)) {
                ratio.numerator = integer;
            } else if (value.kind == MetaValueKind::Text) {
                std::string_view text = arena_text(arena, value.data.span);
                if (allow_mm_suffix && text.ends_with(" mm")) {
                    text.remove_suffix(3U);
                }
                const NumericParseStatus status = parse_exact_ratio(text, false,
                                                                    &ratio);
                if (status != NumericParseStatus::Ok) {
                    return numeric_status(status);
                }
            } else {
                return MetadataCaptureTranslationStatus::InvalidSourceValue;
            }
        }

        if (ratio.negative || ratio.numerator == 0U
            || ratio.numerator > std::numeric_limits<uint32_t>::max()
            || ratio.denominator > std::numeric_limits<uint32_t>::max()) {
            return MetadataCaptureTranslationStatus::ValueOutOfRange;
        }
        const uint64_t divisor = gcd_u64(ratio.numerator, ratio.denominator);
        *out = make_urational(static_cast<uint32_t>(ratio.numerator / divisor),
                              static_cast<uint32_t>(ratio.denominator
                                                    / divisor));
        return MetadataCaptureTranslationStatus::Ok;
    }

    static MetadataCaptureTranslationStatus
    parse_iso_source(const ByteArena& arena, const MetaValue& value,
                     MetaValue* out) noexcept
    {
        if (!out) {
            return MetadataCaptureTranslationStatus::InternalError;
        }
        uint64_t iso = 0U;
        if (!scalar_unsigned(value, &iso)) {
            if (value.kind != MetaValueKind::Text) {
                return MetadataCaptureTranslationStatus::InvalidSourceValue;
            }
            std::string_view text = arena_text(arena, value.data.span);
            if (!text.empty() && text.front() == '+') {
                text.remove_prefix(1U);
            }
            const NumericParseStatus status = parse_digits(text, &iso);
            if (status != NumericParseStatus::Ok) {
                return numeric_status(status);
            }
        }
        if (iso == 0U || iso > std::numeric_limits<uint16_t>::max()) {
            return MetadataCaptureTranslationStatus::ValueOutOfRange;
        }
        *out = make_u16(static_cast<uint16_t>(iso));
        return MetadataCaptureTranslationStatus::Ok;
    }

    static MetadataCaptureTranslationStatus
    parse_signed_rational_source(const ByteArena& arena, const MetaValue& value,
                                 MetaValue* out) noexcept
    {
        if (!out) {
            return MetadataCaptureTranslationStatus::InternalError;
        }

        ExactRatio ratio;
        if (value.kind == MetaValueKind::Scalar
            && value.elem_type == MetaElementType::SRational) {
            if (value.data.sr.denom <= 0) {
                return MetadataCaptureTranslationStatus::InvalidNumericValue;
            }
            const int64_t numerator = value.data.sr.numer;
            ratio.negative          = numerator < 0;
            ratio.numerator = ratio.negative ? static_cast<uint64_t>(-numerator)
                                             : static_cast<uint64_t>(numerator);
            ratio.denominator = static_cast<uint64_t>(value.data.sr.denom);
        } else {
            int64_t signed_integer    = 0;
            uint64_t unsigned_integer = 0U;
            if (scalar_signed(value, &signed_integer)) {
                ratio.negative = signed_integer < 0;
                if (signed_integer == std::numeric_limits<int64_t>::min()) {
                    return MetadataCaptureTranslationStatus::ValueOutOfRange;
                }
                ratio.numerator = ratio.negative
                                      ? static_cast<uint64_t>(-signed_integer)
                                      : static_cast<uint64_t>(signed_integer);
            } else if (scalar_unsigned(value, &unsigned_integer)) {
                ratio.numerator = unsigned_integer;
            } else if (value.kind == MetaValueKind::Text) {
                const NumericParseStatus status
                    = parse_exact_ratio(arena_text(arena, value.data.span),
                                        true, &ratio);
                if (status != NumericParseStatus::Ok) {
                    return numeric_status(status);
                }
            } else {
                return MetadataCaptureTranslationStatus::InvalidSourceValue;
            }
        }

        const uint64_t divisor = gcd_u64(ratio.numerator, ratio.denominator);
        ratio.numerator /= divisor;
        ratio.denominator /= divisor;
        const uint64_t max_magnitude
            = ratio.negative
                  ? static_cast<uint64_t>(std::numeric_limits<int32_t>::max())
                        + 1U
                  : static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
        if (ratio.numerator > max_magnitude
            || ratio.denominator > static_cast<uint64_t>(
                   std::numeric_limits<int32_t>::max())) {
            return MetadataCaptureTranslationStatus::ValueOutOfRange;
        }

        int32_t numerator = 0;
        if (ratio.negative) {
            numerator = ratio.numerator == max_magnitude
                            ? std::numeric_limits<int32_t>::min()
                            : -static_cast<int32_t>(ratio.numerator);
        } else {
            numerator = static_cast<int32_t>(ratio.numerator);
        }
        *out = make_srational(numerator,
                              static_cast<int32_t>(ratio.denominator));
        return MetadataCaptureTranslationStatus::Ok;
    }

    static bool xmp_path_matches(const MetaStore& store, const Entry& entry,
                                 std::string_view property_path) noexcept
    {
        return entry.key.kind == MetaKeyKind::XmpProperty
               && arena_text(store.arena(),
                             entry.key.data.xmp_property.schema_ns)
                      == kXmpNsExif
               && arena_text(store.arena(),
                             entry.key.data.xmp_property.property_path)
                      == property_path;
    }

    static MetadataCaptureTranslationStatus
    find_capture_source(const MetaStore& store,
                        std::span<const std::string_view> paths,
                        MetadataCaptureTranslationSourceMode source_mode,
                        CaptureSource* out) noexcept
    {
        if (!out) {
            return MetadataCaptureTranslationStatus::InternalError;
        }
        *out                                 = CaptureSource {};
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            bool matches       = false;
            for (const std::string_view path : paths) {
                if (xmp_path_matches(store, entry, path)) {
                    matches = true;
                    break;
                }
            }
            if (!matches) {
                continue;
            }
            const bool dirty   = any(entry.flags, EntryFlags::Dirty);
            const bool deleted = any(entry.flags, EntryFlags::Deleted);
            if ((source_mode == MetadataCaptureTranslationSourceMode::DirtyOnly
                 && !dirty)
                || (deleted && !dirty)) {
                continue;
            }
            if (out->found) {
                return MetadataCaptureTranslationStatus::AmbiguousSource;
            }
            out->found    = true;
            out->deleted  = deleted;
            out->entry_id = id;
            out->value    = &entry.value;
        }
        return MetadataCaptureTranslationStatus::Ok;
    }

    static bool is_additional_iso_member(std::string_view path) noexcept
    {
        static constexpr std::string_view kPrefix = "ISOSpeedRatings[";
        if (!path.starts_with(kPrefix) || path == "ISOSpeedRatings[1]"
            || path.size() <= kPrefix.size() + 1U || path.back() != ']') {
            return false;
        }
        path.remove_prefix(kPrefix.size());
        path.remove_suffix(1U);
        for (const char c : path) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        return true;
    }

    static EntryId find_additional_iso_source(
        const MetaStore& store,
        MetadataCaptureTranslationSourceMode source_mode) noexcept
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || arena_text(store.arena(),
                              entry.key.data.xmp_property.schema_ns)
                       != kXmpNsExif
                || !is_additional_iso_member(
                    arena_text(store.arena(),
                               entry.key.data.xmp_property.property_path))) {
                continue;
            }
            const bool dirty   = any(entry.flags, EntryFlags::Dirty);
            const bool deleted = any(entry.flags, EntryFlags::Deleted);
            if ((source_mode == MetadataCaptureTranslationSourceMode::DirtyOnly
                 && !dirty)
                || (deleted && !dirty)) {
                continue;
            }
            return id;
        }
        return kInvalidEntryId;
    }

    static bool native_field_matches(const MetaStore& store, const Entry& entry,
                                     NativeCaptureField field) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag
            || arena_text(store.arena(), entry.key.data.exif_tag.ifd)
                   != "exififd") {
            return false;
        }
        uint16_t tag = 0U;
        switch (field) {
        case NativeCaptureField::ExposureTime: tag = 0x829aU; break;
        case NativeCaptureField::FNumber: tag = 0x829dU; break;
        case NativeCaptureField::Iso: tag = 0x8827U; break;
        case NativeCaptureField::ExposureBias: tag = 0x9204U; break;
        case NativeCaptureField::FocalLength: tag = 0x920aU; break;
        case NativeCaptureField::ExposureProgram: tag = 0x8822U; break;
        case NativeCaptureField::MeteringMode: tag = 0x9207U; break;
        case NativeCaptureField::SensingMethod: tag = 0xa217U; break;
        case NativeCaptureField::CustomRendered: tag = 0xa401U; break;
        case NativeCaptureField::ExposureMode: tag = 0xa402U; break;
        case NativeCaptureField::WhiteBalance: tag = 0xa403U; break;
        case NativeCaptureField::SceneCaptureType: tag = 0xa406U; break;
        case NativeCaptureField::GainControl: tag = 0xa407U; break;
        case NativeCaptureField::Contrast: tag = 0xa408U; break;
        case NativeCaptureField::Saturation: tag = 0xa409U; break;
        case NativeCaptureField::Sharpness: tag = 0xa40aU; break;
        case NativeCaptureField::SubjectDistanceRange: tag = 0xa40cU; break;
        }
        return entry.key.data.exif_tag.tag == tag;
    }

    static bool capture_value_matches(const MetaValue& actual,
                                      const MetaValue& expected) noexcept
    {
        if (actual.kind != MetaValueKind::Scalar
            || expected.kind != MetaValueKind::Scalar || actual.count != 1U
            || expected.count != 1U || actual.elem_type != expected.elem_type) {
            return false;
        }
        switch (expected.elem_type) {
        case MetaElementType::U16: return actual.data.u64 == expected.data.u64;
        case MetaElementType::URational:
            if (actual.data.ur.denom == 0U || expected.data.ur.denom == 0U) {
                return false;
            }
            return static_cast<uint64_t>(actual.data.ur.numer)
                       * expected.data.ur.denom
                   == static_cast<uint64_t>(expected.data.ur.numer)
                          * actual.data.ur.denom;
        case MetaElementType::SRational:
            if (actual.data.sr.denom <= 0 || expected.data.sr.denom <= 0) {
                return false;
            }
            return static_cast<int64_t>(actual.data.sr.numer)
                       * expected.data.sr.denom
                   == static_cast<int64_t>(expected.data.sr.numer)
                          * actual.data.sr.denom;
        default: return false;
        }
    }

    static void analyze_group(const MetaStore& store,
                              CapturePlannedGroup* group) noexcept
    {
        if (!group) {
            return;
        }
        uint32_t active_count = 0U;
        bool exact            = false;
        for (const Entry& entry : store.entries()) {
            if (any(entry.flags, EntryFlags::Deleted)
                || !native_field_matches(store, entry, group->field)) {
                continue;
            }
            ++active_count;
            if (active_count == 1U && group->present) {
                exact = capture_value_matches(entry.value, group->value);
            }
        }
        group->existing_any = active_count > 0U;
        group->exact_match  = group->present ? active_count == 1U && exact
                                             : active_count == 0U;
    }

    static uint32_t missing_entries(const MetaStore& store,
                                    const CapturePlannedGroup& group) noexcept
    {
        if (!group.present) {
            return 0U;
        }
        for (const Entry& entry : store.entries()) {
            if (!any(entry.flags, EntryFlags::Deleted)
                && native_field_matches(store, entry, group.field)) {
                return 0U;
            }
        }
        return 1U;
    }

    static uint32_t
    required_operations(const MetaStore& store,
                        const CapturePlannedGroup& group) noexcept
    {
        uint32_t active_count = 0U;
        bool first_matches    = false;
        for (const Entry& entry : store.entries()) {
            if (any(entry.flags, EntryFlags::Deleted)
                || !native_field_matches(store, entry, group.field)) {
                continue;
            }
            ++active_count;
            if (active_count == 1U && group.present) {
                first_matches = capture_value_matches(entry.value, group.value);
            }
        }
        if (!group.present) {
            return active_count;
        }
        if (active_count == 0U) {
            return 1U;
        }
        return active_count - 1U + (first_matches ? 0U : 1U);
    }

    static MetaKey make_native_key(ByteArena& arena,
                                   NativeCaptureField field) noexcept
    {
        uint16_t tag = 0U;
        switch (field) {
        case NativeCaptureField::ExposureTime: tag = 0x829aU; break;
        case NativeCaptureField::FNumber: tag = 0x829dU; break;
        case NativeCaptureField::Iso: tag = 0x8827U; break;
        case NativeCaptureField::ExposureBias: tag = 0x9204U; break;
        case NativeCaptureField::FocalLength: tag = 0x920aU; break;
        case NativeCaptureField::ExposureProgram: tag = 0x8822U; break;
        case NativeCaptureField::MeteringMode: tag = 0x9207U; break;
        case NativeCaptureField::SensingMethod: tag = 0xa217U; break;
        case NativeCaptureField::CustomRendered: tag = 0xa401U; break;
        case NativeCaptureField::ExposureMode: tag = 0xa402U; break;
        case NativeCaptureField::WhiteBalance: tag = 0xa403U; break;
        case NativeCaptureField::SceneCaptureType: tag = 0xa406U; break;
        case NativeCaptureField::GainControl: tag = 0xa407U; break;
        case NativeCaptureField::Contrast: tag = 0xa408U; break;
        case NativeCaptureField::Saturation: tag = 0xa409U; break;
        case NativeCaptureField::Sharpness: tag = 0xa40aU; break;
        case NativeCaptureField::SubjectDistanceRange: tag = 0xa40cU; break;
        }
        return make_exif_tag_key(arena, "exififd", tag);
    }

    static bool append_native_entry(MetaEdit* edit, const MetaStore& source,
                                    const CapturePlannedGroup& group) noexcept
    {
        if (!edit || group.source_entry >= source.entries().size()) {
            return false;
        }
        Entry entry;
        entry.key    = make_native_key(edit->arena(), group.field);
        entry.value  = group.value;
        entry.origin = source.entry(group.source_entry).origin;
        if (entry.origin.wire_type_name.size > 0U) {
            entry.origin.wire_type_name = edit->arena().append(
                source.arena().span(entry.origin.wire_type_name));
        }
        if (entry.origin.order_in_block
            < std::numeric_limits<uint32_t>::max()) {
            ++entry.origin.order_in_block;
        }
        entry.flags = EntryFlags::Dirty;
        if (edit->arena().limit_exceeded()) {
            return false;
        }
        edit->add_entry(entry);
        return true;
    }

    static void apply_group(const MetaStore& source,
                            const CapturePlannedGroup& group, MetaEdit* edit,
                            MetadataCaptureTranslationResult* result)
    {
        if (!edit || !result || !group.apply) {
            return;
        }
        EntryId first_active                 = kInvalidEntryId;
        const std::span<const Entry> entries = source.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)
                || !native_field_matches(source, entry, group.field)) {
                continue;
            }
            if (!group.present || first_active != kInvalidEntryId) {
                edit->tombstone(id);
                ++result->entries_removed;
                continue;
            }
            first_active = id;
            if (!capture_value_matches(entry.value, group.value)) {
                edit->set_value(id, group.value);
                ++result->entries_updated;
            }
        }
        if (group.present && first_active == kInvalidEntryId
            && append_native_entry(edit, source, group)) {
            ++result->entries_added;
        }
        ++result->groups_translated;
    }

    static MetadataCaptureTranslationResult
    capture_error(MetadataCaptureTranslationStatus status) noexcept
    {
        MetadataCaptureTranslationResult result;
        result.status = status;
        return result;
    }

    static MetadataCaptureTranslationStatus
    append_group(const MetaStore& source,
                 const MetadataCaptureTranslationOptions& options,
                 std::span<const std::string_view> source_paths,
                 MetadataCaptureTranslationMapping mapping,
                 NativeCaptureField field,
                 std::array<CapturePlannedGroup, 5U>* groups,
                 uint8_t* group_count, uint64_t* total_text_bytes,
                 MetadataCaptureTranslationResult* result) noexcept
    {
        if (!groups || !group_count || !total_text_bytes || !result
            || *group_count >= groups->size()) {
            return MetadataCaptureTranslationStatus::InternalError;
        }

        if (mapping == MetadataCaptureTranslationMapping::XmpIso) {
            const EntryId unsupported
                = find_additional_iso_source(source, options.source_mode);
            if (unsupported != kInvalidEntryId) {
                result->failed_mapping      = mapping;
                result->failed_source_entry = unsupported;
                return MetadataCaptureTranslationStatus::InvalidSourceValue;
            }
        }

        CaptureSource property;
        MetadataCaptureTranslationStatus status
            = find_capture_source(source, source_paths, options.source_mode,
                                  &property);
        if (status != MetadataCaptureTranslationStatus::Ok) {
            result->failed_mapping      = mapping;
            result->failed_source_entry = property.entry_id;
            return status;
        }
        if (!property.found) {
            return MetadataCaptureTranslationStatus::Ok;
        }
        ++result->source_properties;

        CapturePlannedGroup group;
        group.mapping      = mapping;
        group.field        = field;
        group.source_entry = property.entry_id;
        if (!property.deleted) {
            if (!property.value) {
                return MetadataCaptureTranslationStatus::InternalError;
            }
            if (property.value->kind == MetaValueKind::Text) {
                const uint64_t size = property.value->data.span.size;
                if (size > options.max_text_bytes_per_property) {
                    result->failed_mapping      = mapping;
                    result->failed_source_entry = property.entry_id;
                    return MetadataCaptureTranslationStatus::ValueTooLong;
                }
                if (size > options.max_total_text_bytes
                    || *total_text_bytes
                           > options.max_total_text_bytes - size) {
                    result->failed_mapping      = mapping;
                    result->failed_source_entry = property.entry_id;
                    return MetadataCaptureTranslationStatus::SourceLimitExceeded;
                }
                *total_text_bytes += size;
            }

            switch (field) {
            case NativeCaptureField::ExposureTime:
            case NativeCaptureField::FNumber:
                status = parse_unsigned_rational_source(source.arena(),
                                                        *property.value, false,
                                                        &group.value);
                break;
            case NativeCaptureField::FocalLength:
                status = parse_unsigned_rational_source(source.arena(),
                                                        *property.value, true,
                                                        &group.value);
                break;
            case NativeCaptureField::Iso:
                status = parse_iso_source(source.arena(), *property.value,
                                          &group.value);
                break;
            case NativeCaptureField::ExposureBias:
                status = parse_signed_rational_source(source.arena(),
                                                      *property.value,
                                                      &group.value);
                break;
            default: return MetadataCaptureTranslationStatus::InternalError;
            }
            if (status != MetadataCaptureTranslationStatus::Ok) {
                result->failed_mapping      = mapping;
                result->failed_source_entry = property.entry_id;
                return status;
            }
            group.present = true;
        }
        (*groups)[(*group_count)++] = group;
        return MetadataCaptureTranslationStatus::Ok;
    }

    static MetadataCaptureTranslationResult apply_capture_groups(
        const MetaStore& source, std::span<CapturePlannedGroup> groups,
        MetadataCaptureTranslationConflictPolicy conflict_policy,
        uint32_t max_added_entries, uint32_t max_operations,
        MetadataCaptureTranslationResult result, MetaStore* out_store)
    {
        uint32_t added_entries   = 0U;
        uint32_t operation_count = 0U;
        for (size_t i = 0U; i < groups.size(); ++i) {
            CapturePlannedGroup& group = groups[i];
            analyze_group(source, &group);
            switch (conflict_policy) {
            case MetadataCaptureTranslationConflictPolicy::PreserveExisting:
                if (group.existing_any) {
                    ++result.groups_preserved;
                } else {
                    group.apply = true;
                }
                break;
            case MetadataCaptureTranslationConflictPolicy::FailOnConflict:
                if (group.existing_any && !group.exact_match) {
                    result.status
                        = MetadataCaptureTranslationStatus::NativeConflict;
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
            case MetadataCaptureTranslationConflictPolicy::ReplaceExisting:
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
        if (added_entries > max_added_entries
            || source.entries().size() > static_cast<size_t>(kInvalidEntryId)
            || static_cast<size_t>(added_entries)
                   > static_cast<size_t>(kInvalidEntryId)
                         - source.entries().size()) {
            result.status = MetadataCaptureTranslationStatus::EntryLimitExceeded;
            return result;
        }
        if (operation_count > max_operations) {
            result.status
                = MetadataCaptureTranslationStatus::OperationLimitExceeded;
            return result;
        }

        MetaEdit edit;
        edit.reserve_ops(operation_count);
        for (size_t i = 0U; i < groups.size(); ++i) {
            apply_group(source, groups[i], &edit, &result);
        }
        if (edit.ops().size() != operation_count
            || edit.arena().limit_exceeded()
            || result.entries_added != added_entries) {
            result.status = MetadataCaptureTranslationStatus::InternalError;
            return result;
        }
        *out_store = commit(source, std::span<const MetaEdit>(&edit, 1U));
        return result;
    }

}  // namespace

MetadataCaptureTranslationResult
translate_xmp_capture_metadata(const MetaStore& source,
                               const MetadataCaptureTranslationOptions& options,
                               MetaStore* out_store)
{
    if (!out_store) {
        return capture_error(MetadataCaptureTranslationStatus::NullOutput);
    }
    if (!source.is_finalized()) {
        return capture_error(
            MetadataCaptureTranslationStatus::SourceNotFinalized);
    }
    if (options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataCaptureTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataCaptureTranslationMaxTotalTextBytes
        || (options.source_mode
                != MetadataCaptureTranslationSourceMode::DirtyOnly
            && options.source_mode != MetadataCaptureTranslationSourceMode::All)
        || (options.conflict_policy
                != MetadataCaptureTranslationConflictPolicy::PreserveExisting
            && options.conflict_policy
                   != MetadataCaptureTranslationConflictPolicy::FailOnConflict
            && options.conflict_policy
                   != MetadataCaptureTranslationConflictPolicy::ReplaceExisting)
        || (!options.exposure_time_to_exif && !options.f_number_to_exif
            && !options.iso_to_exif && !options.focal_length_to_exif
            && !options.exposure_compensation_to_exif)) {
        return capture_error(MetadataCaptureTranslationStatus::InvalidOptions);
    }

    static constexpr std::array<std::string_view, 1U> kExposureTimePaths = {
        "ExposureTime",
    };
    static constexpr std::array<std::string_view, 1U> kFNumberPaths = {
        "FNumber",
    };
    static constexpr std::array<std::string_view, 3U> kIsoPaths = {
        "ISO",
        "ISOSpeedRatings",
        "ISOSpeedRatings[1]",
    };
    static constexpr std::array<std::string_view, 1U> kFocalLengthPaths = {
        "FocalLength",
    };
    static constexpr std::array<std::string_view, 2U> kExposureBiasPaths = {
        "ExposureCompensation",
        "ExposureBiasValue",
    };

    std::array<CapturePlannedGroup, 5U> groups {};
    uint8_t group_count       = 0U;
    uint64_t total_text_bytes = 0U;
    MetadataCaptureTranslationResult result;
    MetadataCaptureTranslationStatus status
        = MetadataCaptureTranslationStatus::Ok;
    if (options.exposure_time_to_exif) {
        status = append_group(source, options, kExposureTimePaths,
                              MetadataCaptureTranslationMapping::XmpExposureTime,
                              NativeCaptureField::ExposureTime, &groups,
                              &group_count, &total_text_bytes, &result);
    }
    if (status == MetadataCaptureTranslationStatus::Ok
        && options.f_number_to_exif) {
        status = append_group(source, options, kFNumberPaths,
                              MetadataCaptureTranslationMapping::XmpFNumber,
                              NativeCaptureField::FNumber, &groups,
                              &group_count, &total_text_bytes, &result);
    }
    if (status == MetadataCaptureTranslationStatus::Ok && options.iso_to_exif) {
        status = append_group(source, options, kIsoPaths,
                              MetadataCaptureTranslationMapping::XmpIso,
                              NativeCaptureField::Iso, &groups, &group_count,
                              &total_text_bytes, &result);
    }
    if (status == MetadataCaptureTranslationStatus::Ok
        && options.focal_length_to_exif) {
        status = append_group(source, options, kFocalLengthPaths,
                              MetadataCaptureTranslationMapping::XmpFocalLength,
                              NativeCaptureField::FocalLength, &groups,
                              &group_count, &total_text_bytes, &result);
    }
    if (status == MetadataCaptureTranslationStatus::Ok
        && options.exposure_compensation_to_exif) {
        status = append_group(
            source, options, kExposureBiasPaths,
            MetadataCaptureTranslationMapping::XmpExposureCompensation,
            NativeCaptureField::ExposureBias, &groups, &group_count,
            &total_text_bytes, &result);
    }
    if (status != MetadataCaptureTranslationStatus::Ok) {
        result.status = status;
        return result;
    }

    return apply_capture_groups(source, std::span(groups.data(), group_count),
                                options.conflict_policy,
                                options.max_added_entries,
                                options.max_operations, result, out_store);
}


MetadataCaptureTranslationResult
translate_xmp_capture_settings_metadata(
    const MetaStore& source,
    const MetadataCaptureSettingsTranslationOptions& options,
    MetaStore* out_store)
{
    using Status = MetadataCaptureTranslationStatus;
    using Mode   = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;
    if (!out_store)
        return capture_error(Status::NullOutput);
    if (!source.is_finalized())
        return capture_error(Status::SourceNotFinalized);
    const std::array enabled { options.exposure_program_to_exif,
                               options.metering_mode_to_exif,
                               options.sensing_method_to_exif,
                               options.custom_rendered_to_exif,
                               options.exposure_mode_to_exif,
                               options.white_balance_to_exif,
                               options.scene_capture_type_to_exif,
                               options.gain_control_to_exif,
                               options.contrast_to_exif,
                               options.saturation_to_exif,
                               options.sharpness_to_exif,
                               options.subject_distance_range_to_exif };
    bool any_enabled = false;
    for (const bool flag : enabled)
        any_enabled = any_enabled || flag;
    if (!any_enabled
        || (options.source_mode != Mode::DirtyOnly
            && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataCaptureSettingsTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataCaptureSettingsTranslationMaxTotalTextBytes)
        return capture_error(Status::InvalidOptions);
    struct Setting final {
        std::string_view path;
        NativeCaptureField field;
        MetadataCaptureTranslationMapping mapping;
        uint16_t tag;
        uint16_t max_code;
    };
    static constexpr std::array<Setting, 12> settings {
        { { "ExposureProgram", NativeCaptureField::ExposureProgram,
            MetadataCaptureTranslationMapping::XmpExposureProgram, 0x8822U, 8U },
          { "MeteringMode", NativeCaptureField::MeteringMode,
            MetadataCaptureTranslationMapping::XmpMeteringMode, 0x9207U, 6U },
          { "SensingMethod", NativeCaptureField::SensingMethod,
            MetadataCaptureTranslationMapping::XmpSensingMethod, 0xa217U, 8U },
          { "CustomRendered", NativeCaptureField::CustomRendered,
            MetadataCaptureTranslationMapping::XmpCustomRendered, 0xa401U, 1U },
          { "ExposureMode", NativeCaptureField::ExposureMode,
            MetadataCaptureTranslationMapping::XmpExposureMode, 0xa402U, 2U },
          { "WhiteBalance", NativeCaptureField::WhiteBalance,
            MetadataCaptureTranslationMapping::XmpWhiteBalance, 0xa403U, 1U },
          { "SceneCaptureType", NativeCaptureField::SceneCaptureType,
            MetadataCaptureTranslationMapping::XmpSceneCaptureType, 0xa406U,
            3U },
          { "GainControl", NativeCaptureField::GainControl,
            MetadataCaptureTranslationMapping::XmpGainControl, 0xa407U, 4U },
          { "Contrast", NativeCaptureField::Contrast,
            MetadataCaptureTranslationMapping::XmpContrast, 0xa408U, 2U },
          { "Saturation", NativeCaptureField::Saturation,
            MetadataCaptureTranslationMapping::XmpSaturation, 0xa409U, 2U },
          { "Sharpness", NativeCaptureField::Sharpness,
            MetadataCaptureTranslationMapping::XmpSharpness, 0xa40aU, 2U },
          { "SubjectDistanceRange", NativeCaptureField::SubjectDistanceRange,
            MetadataCaptureTranslationMapping::XmpSubjectDistanceRange, 0xa40cU,
            3U } }
    };
    std::array<CapturePlannedGroup, 12> groups {};
    size_t count        = 0U;
    uint64_t text_bytes = 0U;
    MetadataCaptureTranslationResult result;
    for (size_t i = 0U; i < settings.size(); ++i) {
        if (!enabled[i])
            continue;
        const Setting& setting = settings[i];
        CaptureSource property;
        Status status = find_capture_source(source,
                                            std::span(&setting.path, 1U),
                                            options.source_mode, &property);
        if (status == Status::Ok && !property.found)
            continue;
        CapturePlannedGroup group;
        group.mapping      = setting.mapping;
        group.field        = setting.field;
        group.source_entry = property.entry_id;
        if (status == Status::Ok && !property.deleted) {
            const MetaValue& value = *property.value;
            uint64_t code          = 0U;
            if (value.kind == MetaValueKind::Text) {
                const std::string_view input = arena_text(source.arena(),
                                                          value.data.span);
                if (input.size() > options.max_text_bytes_per_property)
                    status = Status::ValueTooLong;
                else if (input.size() > options.max_total_text_bytes
                         || text_bytes
                                > options.max_total_text_bytes - input.size())
                    status = Status::SourceLimitExceeded;
                else if (value.text_encoding != TextEncoding::Ascii
                         && value.text_encoding != TextEncoding::Utf8
                         && value.text_encoding != TextEncoding::Unknown)
                    status = Status::InvalidSourceValue;
                else {
                    text_bytes += input.size();
                    const NumericParseStatus parsed = parse_digits(input,
                                                                   &code);
                    if (parsed != NumericParseStatus::Ok) {
                        bool matched = false;
                        for (uint16_t candidate = 0U; candidate <= 255U;
                             ++candidate) {
                            const bool valid = candidate <= setting.max_code
                                               || (setting.tag == 0x9207U
                                                   && candidate == 255U);
                            if (!valid
                                || (setting.tag == 0xa217U
                                    && (candidate == 0U || candidate == 6U)))
                                continue;
                            const std::string_view label
                                = exif_tag_numeric_value_name("exififd",
                                                              setting.tag,
                                                              candidate);
                            if ((!label.empty() && input == label)
                                || (setting.tag == 0xa406U && candidate == 3U
                                    && input == "Night scene")) {
                                code    = candidate;
                                matched = true;
                                break;
                            }
                        }
                        if (!matched)
                            status = numeric_status(parsed);
                    }
                }
            } else if (value.kind != MetaValueKind::Scalar
                       || value.count != 1U) {
                status = Status::InvalidSourceValue;
            } else if (!scalar_unsigned(value, &code)) {
                int64_t signed_code = 0;
                if (!scalar_signed(value, &signed_code))
                    status = Status::InvalidSourceValue;
                else if (signed_code < 0)
                    status = Status::ValueOutOfRange;
                else
                    code = static_cast<uint64_t>(signed_code);
            }
            if (status == Status::Ok
                && ((code > setting.max_code
                     && !(setting.tag == 0x9207U && code == 255U))
                    || (setting.tag == 0xa217U && (code == 0U || code == 6U))))
                status = Status::ValueOutOfRange;
            if (status == Status::Ok) {
                group.value   = make_u16(static_cast<uint16_t>(code));
                group.present = true;
            }
        }
        if (status != Status::Ok) {
            result.status              = status;
            result.failed_mapping      = setting.mapping;
            result.failed_source_entry = property.entry_id;
            return result;
        }
        ++result.source_properties;
        groups[count++] = group;
    }
    return apply_capture_groups(source, std::span(groups.data(), count),
                                options.conflict_policy,
                                options.max_added_entries,
                                options.max_operations, result, out_store);
}

const char*
metadata_capture_translation_status_name(
    MetadataCaptureTranslationStatus status) noexcept
{
    switch (status) {
    case MetadataCaptureTranslationStatus::Ok: return "ok";
    case MetadataCaptureTranslationStatus::NullOutput: return "null_output";
    case MetadataCaptureTranslationStatus::SourceNotFinalized:
        return "source_not_finalized";
    case MetadataCaptureTranslationStatus::InvalidOptions:
        return "invalid_options";
    case MetadataCaptureTranslationStatus::AmbiguousSource:
        return "ambiguous_source";
    case MetadataCaptureTranslationStatus::InvalidSourceValue:
        return "invalid_source_value";
    case MetadataCaptureTranslationStatus::InvalidNumericValue:
        return "invalid_numeric_value";
    case MetadataCaptureTranslationStatus::ValueOutOfRange:
        return "value_out_of_range";
    case MetadataCaptureTranslationStatus::ValueTooLong:
        return "value_too_long";
    case MetadataCaptureTranslationStatus::SourceLimitExceeded:
        return "source_limit_exceeded";
    case MetadataCaptureTranslationStatus::NativeConflict:
        return "native_conflict";
    case MetadataCaptureTranslationStatus::EntryLimitExceeded:
        return "entry_limit_exceeded";
    case MetadataCaptureTranslationStatus::OperationLimitExceeded:
        return "operation_limit_exceeded";
    case MetadataCaptureTranslationStatus::InternalError:
        return "internal_error";
    }
    return "unknown";
}

const char*
metadata_capture_translation_mapping_name(
    MetadataCaptureTranslationMapping mapping) noexcept
{
    switch (mapping) {
    case MetadataCaptureTranslationMapping::None: return "none";
    case MetadataCaptureTranslationMapping::XmpExposureProgram:
        return "xmp_exposure_program";
    case MetadataCaptureTranslationMapping::XmpMeteringMode:
        return "xmp_metering_mode";
    case MetadataCaptureTranslationMapping::XmpSensingMethod:
        return "xmp_sensing_method";
    case MetadataCaptureTranslationMapping::XmpCustomRendered:
        return "xmp_custom_rendered";
    case MetadataCaptureTranslationMapping::XmpExposureMode:
        return "xmp_exposure_mode";
    case MetadataCaptureTranslationMapping::XmpWhiteBalance:
        return "xmp_white_balance";
    case MetadataCaptureTranslationMapping::XmpSceneCaptureType:
        return "xmp_scene_capture_type";
    case MetadataCaptureTranslationMapping::XmpGainControl:
        return "xmp_gain_control";
    case MetadataCaptureTranslationMapping::XmpContrast: return "xmp_contrast";
    case MetadataCaptureTranslationMapping::XmpSaturation:
        return "xmp_saturation";
    case MetadataCaptureTranslationMapping::XmpSharpness:
        return "xmp_sharpness";
    case MetadataCaptureTranslationMapping::XmpSubjectDistanceRange:
        return "xmp_subject_distance_range";
    case MetadataCaptureTranslationMapping::XmpExposureTime:
        return "xmp_exposure_time";
    case MetadataCaptureTranslationMapping::XmpFNumber: return "xmp_f_number";
    case MetadataCaptureTranslationMapping::XmpIso: return "xmp_iso";
    case MetadataCaptureTranslationMapping::XmpFocalLength:
        return "xmp_focal_length";
    case MetadataCaptureTranslationMapping::XmpExposureCompensation:
        return "xmp_exposure_compensation";
    }
    return "unknown";
}

}  // namespace openmeta
