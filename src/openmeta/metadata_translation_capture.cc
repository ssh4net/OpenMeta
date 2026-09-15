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
#include <cstring>
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
        SubjectDistance,
        DigitalZoomRatio,
        ExposureIndex,
        FlashEnergy,
        Flash,
        LightSource,
        SensitivityType,
        StandardOutputSensitivity,
        RecommendedExposureIndex,
        ISOSpeed,
        ISOSpeedLatitudeyyy,
        ISOSpeedLatitudezzz,
        LensSpecification,
        ImageUniqueID,
        ShutterSpeedValue,
        ApertureValue,
        BrightnessValue,
        MaxApertureValue,
        FocalPlaneXResolution,
        FocalPlaneYResolution,
        FocalPlaneResolutionUnit,
        SubjectArea,
        SubjectLocation,
        FocalLengthIn35mmFilm,
        FileSource,
        SceneType,
        Temperature,
        Humidity,
        Pressure,
        WaterDepth,
        Acceleration,
        CameraElevationAngle,
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
        std::array<URational, 4> lens {};
        std::array<uint16_t, 4> subject {};
        uint32_t subject_count = 0U;
        std::string_view identity;
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
                                                ExactRatio* out,
                                                bool reduce = true) noexcept
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

        if (reduce) {
            const uint64_t divisor = gcd_u64(parsed.numerator,
                                             parsed.denominator);
            parsed.numerator /= divisor;
            parsed.denominator /= divisor;
        }
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

    static MetadataCaptureTranslationStatus parse_capture_rational_source(
        const ByteArena& arena, const MetaValue& value,
        NativeCaptureField field, MetaValue* out) noexcept
    {
        using Status = MetadataCaptureTranslationStatus;
        ExactRatio ratio;
        bool wire_numerator = true;
        if (value.kind == MetaValueKind::Text) {
            const std::string_view text = arena_text(arena, value.data.span);
            if (field == NativeCaptureField::SubjectDistance
                && text == "Unknown") {
                *out = make_urational(0U, 1U);
                return Status::Ok;
            }
            if (field == NativeCaptureField::SubjectDistance
                && text == "Infinity") {
                *out = make_urational(UINT32_MAX, 1U);
                return Status::Ok;
            }
            wire_numerator = text.find_first_of(".eE")
                             == std::string_view::npos;
            const NumericParseStatus parsed = parse_exact_ratio(text, false,
                                                                &ratio, false);
            if (parsed != NumericParseStatus::Ok)
                return numeric_status(parsed);
        } else if (value.kind != MetaValueKind::Scalar || value.count != 1U) {
            return Status::InvalidSourceValue;
        } else if (value.elem_type == MetaElementType::URational) {
            ratio.numerator   = value.data.ur.numer;
            ratio.denominator = value.data.ur.denom;
        } else if (!scalar_unsigned(value, &ratio.numerator)) {
            int64_t integer = 0;
            if (!scalar_signed(value, &integer))
                return Status::InvalidSourceValue;
            if (integer < 0)
                return Status::ValueOutOfRange;
            ratio.numerator = static_cast<uint64_t>(integer);
        }
        if (ratio.denominator == 0U)
            return Status::InvalidNumericValue;
        if (ratio.negative)
            return Status::ValueOutOfRange;
        // SubjectDistance sentinels refer to the wire numerator, before reduction.
        if (field == NativeCaptureField::SubjectDistance && wire_numerator
            && ratio.numerator == UINT32_MAX) {
            if (ratio.denominator > UINT32_MAX)
                return Status::ValueOutOfRange;
            *out = make_urational(UINT32_MAX, 1U);
            return Status::Ok;
        }
        const uint64_t divisor = gcd_u64(ratio.numerator, ratio.denominator);
        ratio.numerator /= divisor;
        ratio.denominator /= divisor;
        if (ratio.numerator > UINT32_MAX || ratio.denominator > UINT32_MAX
            || (field == NativeCaptureField::ExposureIndex
                && ratio.numerator == 0U)
            || (field == NativeCaptureField::SubjectDistance
                && ratio.numerator == UINT32_MAX))
            return Status::ValueOutOfRange;
        *out = make_urational(static_cast<uint32_t>(ratio.numerator),
                              static_cast<uint32_t>(ratio.denominator));
        return Status::Ok;
    }

    static MetadataCaptureTranslationStatus
    parse_apex_source(const ByteArena& arena, const MetaValue& value,
                      NativeCaptureField field, MetaValue* out) noexcept
    {
        using Status = MetadataCaptureTranslationStatus;
        if (value.kind != MetaValueKind::Text
            && (value.kind != MetaValueKind::Scalar || value.count != 1U))
            return Status::InvalidSourceValue;
        if (field == NativeCaptureField::ApertureValue
            || field == NativeCaptureField::MaxApertureValue)
            return parse_capture_rational_source(arena, value, field, out);

        if (field == NativeCaptureField::BrightnessValue) {
            bool unknown = false;
            if (value.kind == MetaValueKind::Scalar
                && value.elem_type == MetaElementType::SRational) {
                if (value.data.sr.denom <= 0)
                    return Status::InvalidNumericValue;
                unknown = value.data.sr.numer == -1;
            } else if (value.kind == MetaValueKind::Text) {
                const std::string_view text = arena_text(arena,
                                                         value.data.span);
                unknown                     = text == "Unknown";
                if (!unknown && text.find('/') != std::string_view::npos) {
                    ExactRatio ratio;
                    const NumericParseStatus parsed
                        = parse_exact_ratio(text, true, &ratio, false);
                    if (parsed != NumericParseStatus::Ok)
                        return numeric_status(parsed);
                    unknown = ratio.negative && ratio.numerator == 1U;
                    if (unknown && ratio.denominator > INT32_MAX)
                        return Status::ValueOutOfRange;
                }
            }
            if (unknown) {
                *out = make_srational(-1, 1);
                return Status::Ok;
            }
        }
        const Status status = parse_signed_rational_source(arena, value, out);
        if (status == Status::Ok && field == NativeCaptureField::BrightnessValue
            && out->data.sr.numer == -1) {
            // The reserved wire numerator must never replace a finite value.
            if (out->data.sr.denom > INT32_MAX / 2)
                return Status::ValueOutOfRange;
            *out = make_srational(-2, out->data.sr.denom * 2);
        }
        return status;
    }

    static MetadataCaptureTranslationResult
    flash_error(MetadataCaptureTranslationStatus status,
                EntryId source = kInvalidEntryId) noexcept
    {
        MetadataCaptureTranslationResult result;
        result.status         = status;
        result.failed_mapping = MetadataCaptureTranslationMapping::XmpFlash;
        result.failed_source_entry = source;
        return result;
    }

    static MetadataCaptureTranslationStatus
    parse_flash_integer(const ByteArena& arena, const MetaValue& value,
                        bool scalar_flash, bool boolean_field,
                        uint64_t* out) noexcept
    {
        using Status = MetadataCaptureTranslationStatus;
        if (value.kind == MetaValueKind::Text) {
            const std::string_view text = arena_text(arena, value.data.span);
            if (boolean_field) {
                if (text != "True" && text != "False" && text != "0"
                    && text != "1")
                    return Status::InvalidSourceValue;
                *out = (text == "True" || text == "1") ? 1U : 0U;
                return Status::Ok;
            }
            const NumericParseStatus status = parse_digits(text, out);
            if (status == NumericParseStatus::Ok)
                return Status::Ok;
            if (scalar_flash) {
                for (uint16_t code = 0U; code <= 127U; ++code) {
                    const std::string_view label = exif_flash_name(code);
                    if (!label.empty() && text == label) {
                        *out = code;
                        return Status::Ok;
                    }
                }
            }
            return numeric_status(status);
        }
        if (value.kind != MetaValueKind::Scalar || value.count != 1U)
            return Status::InvalidSourceValue;
        if (!scalar_unsigned(value, out)) {
            int64_t signed_value = 0;
            if (!scalar_signed(value, &signed_value))
                return Status::InvalidSourceValue;
            if (signed_value < 0)
                return Status::ValueOutOfRange;
            *out = static_cast<uint64_t>(signed_value);
        }
        return Status::Ok;
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

    static MetadataCaptureTranslationStatus find_apex_source(
        const MetaStore& store, std::span<const std::string_view> paths,
        MetadataCaptureTranslationSourceMode mode, CaptureSource* out) noexcept
    {
        using Status        = MetadataCaptureTranslationStatus;
        const Status status = find_capture_source(store, paths, mode, out);
        if (status != Status::Ok)
            return status;
        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& entry = store.entry(id);
            const bool dirty   = any(entry.flags, EntryFlags::Dirty);
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || arena_text(store.arena(),
                              entry.key.data.xmp_property.schema_ns)
                       != kXmpNsExif
                || (mode == MetadataCaptureTranslationSourceMode::DirtyOnly
                    && !dirty)
                || (any(entry.flags, EntryFlags::Deleted) && !dirty))
                continue;
            const std::string_view path
                = arena_text(store.arena(),
                             entry.key.data.xmp_property.property_path);
            for (const std::string_view root : paths) {
                if (path.size() > root.size() && path.starts_with(root)
                    && (path[root.size()] == '[' || path[root.size()] == '/'
                        || path[root.size()] == '?')) {
                    out->entry_id = id;
                    return Status::UnsupportedSourceShape;
                }
            }
        }
        return Status::Ok;
    }

    static MetadataCaptureTranslationStatus find_additional_source(
        const MetaStore& store, std::span<const std::string_view> paths,
        bool environment, MetadataCaptureTranslationSourceMode mode,
        CaptureSource* out) noexcept
    {
        using Status = MetadataCaptureTranslationStatus;
        *out         = CaptureSource {};
        for (EntryId id = 0U; id < store.entries().size(); ++id) {
            const Entry& entry = store.entry(id);
            const bool dirty   = any(entry.flags, EntryFlags::Dirty);
            if (entry.key.kind != MetaKeyKind::XmpProperty
                || (mode == MetadataCaptureTranslationSourceMode::DirtyOnly
                    && !dirty)
                || (any(entry.flags, EntryFlags::Deleted) && !dirty))
                continue;
            const auto ns = arena_text(store.arena(),
                                       entry.key.data.xmp_property.schema_ns);
            if (ns != kXmpNsExif
                && !(environment && ns == "http://cipa.jp/exif/1.0/"))
                continue;
            const auto path
                = arena_text(store.arena(),
                             entry.key.data.xmp_property.property_path);
            for (const auto root : paths) {
                if (path.size() > root.size() && path.starts_with(root)
                    && (path[root.size()] == '[' || path[root.size()] == '/'
                        || path[root.size()] == '?')) {
                    out->entry_id = id;
                    return Status::UnsupportedSourceShape;
                }
                if (path != root)
                    continue;
                if (out->found)
                    return Status::AmbiguousSource;
                out->found    = true;
                out->deleted  = any(entry.flags, EntryFlags::Deleted);
                out->entry_id = id;
                out->value    = &entry.value;
            }
        }
        return Status::Ok;
    }

    static bool signed_environment_field(NativeCaptureField field) noexcept
    {
        return field == NativeCaptureField::Temperature
               || field == NativeCaptureField::WaterDepth
               || field == NativeCaptureField::CameraElevationAngle;
    }

    static MetadataCaptureTranslationStatus
    parse_environment_source(const ByteArena& arena, const MetaValue& value,
                             NativeCaptureField field, MetaValue* out) noexcept
    {
        using Status         = MetadataCaptureTranslationStatus;
        const bool is_signed = signed_environment_field(field);
        ExactRatio ratio;
        bool unknown = false;
        if (value.kind == MetaValueKind::Text) {
            auto text = arena_text(arena, value.data.span);
            if (text == "Unknown") {
                *out = is_signed ? make_srational(0, -1)
                                 : make_urational(0U, UINT32_MAX);
                return Status::Ok;
            }
            // -1 denotes the signed representation of the raw 0xffffffff bits.
            if (is_signed && text.ends_with("/-1")) {
                text.remove_suffix(3U);
                unknown = true;
            }
            const auto parsed = parse_exact_ratio(text, is_signed, &ratio,
                                                  false);
            if (parsed != NumericParseStatus::Ok)
                return numeric_status(parsed);
            // A sentinel must be an explicit integer fraction, never a decimal
            // whose power-of-ten denominator happens to reduce to this value.
            if (unknown && text.find_first_of("/.eE") != std::string_view::npos)
                return Status::InvalidNumericValue;
            unknown = unknown || ratio.denominator == UINT32_MAX;
        } else if (value.kind != MetaValueKind::Scalar || value.count != 1U) {
            return Status::InvalidSourceValue;
        } else if (is_signed && value.elem_type == MetaElementType::SRational) {
            if (value.data.sr.denom <= 0 && value.data.sr.denom != -1)
                return Status::InvalidNumericValue;
            const int64_t n   = value.data.sr.numer;
            ratio.negative    = n < 0;
            ratio.numerator   = static_cast<uint64_t>(n < 0 ? -n : n);
            unknown           = value.data.sr.denom == -1;
            ratio.denominator = unknown ? UINT32_MAX
                                        : static_cast<uint64_t>(
                                              value.data.sr.denom);
        } else if (!is_signed
                   && value.elem_type == MetaElementType::URational) {
            if (value.data.ur.denom == 0U)
                return Status::InvalidNumericValue;
            ratio.numerator   = value.data.ur.numer;
            ratio.denominator = value.data.ur.denom;
            unknown           = ratio.denominator == UINT32_MAX;
        } else {
            uint64_t u = 0U;
            int64_t n  = 0;
            if (scalar_unsigned(value, &u))
                ratio.numerator = u;
            else if (scalar_signed(value, &n)) {
                if ((!is_signed && n < 0) || n == INT64_MIN)
                    return Status::ValueOutOfRange;
                ratio.negative  = n < 0;
                ratio.numerator = static_cast<uint64_t>(n < 0 ? -n : n);
            } else
                return Status::InvalidSourceValue;
        }
        if (!unknown) {
            const uint64_t divisor = gcd_u64(ratio.numerator,
                                             ratio.denominator);
            ratio.numerator /= divisor;
            ratio.denominator /= divisor;
        }
        const uint64_t max_n = is_signed
                                   ? (ratio.negative ? uint64_t(INT32_MAX) + 1U
                                                     : uint64_t(INT32_MAX))
                                   : UINT32_MAX;
        if (ratio.numerator > max_n
            || (!unknown
                && ratio.denominator > (is_signed ? uint64_t(INT32_MAX)
                                                  : uint64_t(UINT32_MAX) - 1U)))
            return Status::ValueOutOfRange;
        if (is_signed) {
            const int64_t n = ratio.negative
                                  ? -static_cast<int64_t>(ratio.numerator)
                                  : static_cast<int64_t>(ratio.numerator);
            if (!unknown && field == NativeCaptureField::CameraElevationAngle
                && (n < -180 * static_cast<int64_t>(ratio.denominator)
                    || n >= 180 * static_cast<int64_t>(ratio.denominator)))
                return Status::ValueOutOfRange;
            *out = make_srational(static_cast<int32_t>(n),
                                  unknown ? -1
                                          : static_cast<int32_t>(
                                                ratio.denominator));
        } else {
            *out = make_urational(static_cast<uint32_t>(ratio.numerator),
                                  unknown ? UINT32_MAX
                                          : static_cast<uint32_t>(
                                                ratio.denominator));
        }
        return Status::Ok;
    }

    static MetadataCaptureTranslationStatus
    parse_additional_code(const ByteArena& arena, const MetaValue& value,
                          NativeCaptureField field, MetaValue* out) noexcept
    {
        using Status  = MetadataCaptureTranslationStatus;
        uint64_t code = 0U;
        if (value.kind == MetaValueKind::Text) {
            const auto parsed = parse_digits(arena_text(arena, value.data.span),
                                             &code);
            if (parsed != NumericParseStatus::Ok)
                return numeric_status(parsed);
        } else if (value.kind != MetaValueKind::Scalar || value.count != 1U)
            return Status::InvalidSourceValue;
        else if (!scalar_unsigned(value, &code)) {
            int64_t n = 0;
            if (!scalar_signed(value, &n))
                return Status::InvalidSourceValue;
            if (n < 0)
                return Status::ValueOutOfRange;
            code = static_cast<uint64_t>(n);
        }
        if ((field == NativeCaptureField::FocalLengthIn35mmFilm
             && code > UINT16_MAX)
            || (field == NativeCaptureField::FileSource && code > 3U)
            || (field == NativeCaptureField::SceneType && code != 1U))
            return Status::ValueOutOfRange;
        *out = make_u16(static_cast<uint16_t>(code));
        return Status::Ok;
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
        case NativeCaptureField::ShutterSpeedValue: tag = 0x9201U; break;
        case NativeCaptureField::ApertureValue: tag = 0x9202U; break;
        case NativeCaptureField::BrightnessValue: tag = 0x9203U; break;
        case NativeCaptureField::MaxApertureValue: tag = 0x9205U; break;
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
        case NativeCaptureField::SubjectDistance: tag = 0x9206U; break;
        case NativeCaptureField::DigitalZoomRatio: tag = 0xa404U; break;
        case NativeCaptureField::ExposureIndex: tag = 0xa215U; break;
        case NativeCaptureField::FlashEnergy: tag = 0xa20bU; break;
        case NativeCaptureField::Flash: tag = 0x9209U; break;
        case NativeCaptureField::LightSource: tag = 0x9208U; break;
        case NativeCaptureField::SensitivityType: tag = 0x8830U; break;
        case NativeCaptureField::StandardOutputSensitivity:
            tag = 0x8831U;
            break;
        case NativeCaptureField::RecommendedExposureIndex: tag = 0x8832U; break;
        case NativeCaptureField::ISOSpeed: tag = 0x8833U; break;
        case NativeCaptureField::ISOSpeedLatitudeyyy: tag = 0x8834U; break;
        case NativeCaptureField::ISOSpeedLatitudezzz: tag = 0x8835U; break;
        case NativeCaptureField::LensSpecification: tag = 0xa432U; break;
        case NativeCaptureField::ImageUniqueID: tag = 0xa420U; break;
        case NativeCaptureField::FocalPlaneXResolution: tag = 0xa20eU; break;
        case NativeCaptureField::FocalPlaneYResolution: tag = 0xa20fU; break;
        case NativeCaptureField::FocalPlaneResolutionUnit: tag = 0xa210U; break;
        case NativeCaptureField::SubjectArea: tag = 0x9214U; break;
        case NativeCaptureField::SubjectLocation: tag = 0xa214U; break;
        case NativeCaptureField::FocalLengthIn35mmFilm: tag = 0xa405U; break;
        case NativeCaptureField::FileSource: tag = 0xa300U; break;
        case NativeCaptureField::SceneType: tag = 0xa301U; break;
        case NativeCaptureField::Temperature: tag = 0x9400U; break;
        case NativeCaptureField::Humidity: tag = 0x9401U; break;
        case NativeCaptureField::Pressure: tag = 0x9402U; break;
        case NativeCaptureField::WaterDepth: tag = 0x9403U; break;
        case NativeCaptureField::Acceleration: tag = 0x9404U; break;
        case NativeCaptureField::CameraElevationAngle: tag = 0x9405U; break;
        }
        return entry.key.data.exif_tag.tag == tag;
    }

    static bool capture_value_matches(NativeCaptureField field,
                                      const MetaValue& actual,
                                      const MetaValue& expected) noexcept
    {
        if (actual.kind != MetaValueKind::Scalar
            || expected.kind != MetaValueKind::Scalar || actual.count != 1U
            || expected.count != 1U || actual.elem_type != expected.elem_type) {
            return false;
        }
        switch (expected.elem_type) {
        case MetaElementType::U16:
        case MetaElementType::U32: return actual.data.u64 == expected.data.u64;
        case MetaElementType::URational:
            if (actual.data.ur.denom == 0U || expected.data.ur.denom == 0U) {
                return false;
            }
            if (field >= NativeCaptureField::Temperature
                && field <= NativeCaptureField::CameraElevationAngle
                && (actual.data.ur.denom == UINT32_MAX
                    || expected.data.ur.denom == UINT32_MAX))
                return actual.data.ur.denom == expected.data.ur.denom
                       && actual.data.ur.numer == expected.data.ur.numer;
            if (field == NativeCaptureField::SubjectDistance
                && (actual.data.ur.numer == UINT32_MAX
                    || expected.data.ur.numer == UINT32_MAX)) {
                return actual.data.ur.numer == expected.data.ur.numer;
            }
            return static_cast<uint64_t>(actual.data.ur.numer)
                       * expected.data.ur.denom
                   == static_cast<uint64_t>(expected.data.ur.numer)
                          * actual.data.ur.denom;
        case MetaElementType::SRational:
            if (field >= NativeCaptureField::Temperature
                && field <= NativeCaptureField::CameraElevationAngle
                && (actual.data.sr.denom == -1 || expected.data.sr.denom == -1))
                return actual.data.sr.denom == expected.data.sr.denom
                       && actual.data.sr.numer == expected.data.sr.numer;
            if (actual.data.sr.denom <= 0 || expected.data.sr.denom <= 0) {
                return false;
            }
            if (field == NativeCaptureField::BrightnessValue
                && (actual.data.sr.numer == -1 || expected.data.sr.numer == -1))
                return actual.data.sr.numer == expected.data.sr.numer;
            return static_cast<int64_t>(actual.data.sr.numer)
                       * expected.data.sr.denom
                   == static_cast<int64_t>(expected.data.sr.numer)
                          * actual.data.sr.denom;
        default: return false;
        }
    }

    static bool group_value_matches(const ByteArena& arena,
                                    const MetaValue& actual,
                                    const CapturePlannedGroup& group) noexcept
    {
        if (group.field == NativeCaptureField::FileSource
            || group.field == NativeCaptureField::SceneType) {
            if (actual.kind != MetaValueKind::Bytes || actual.count != 1U
                || actual.data.span.size != 1U)
                return false;
            const auto bytes = arena.span(actual.data.span);
            return bytes.size() == 1U
                   && std::to_integer<uint8_t>(bytes[0])
                          == group.value.data.u64;
        }
        if (group.field == NativeCaptureField::SubjectArea
            || group.field == NativeCaptureField::SubjectLocation) {
            const size_t size = group.subject_count * sizeof(uint16_t);
            return actual.kind == MetaValueKind::Array
                   && actual.elem_type == MetaElementType::U16
                   && actual.count == group.subject_count
                   && actual.data.span.size == size
                   && arena.span(actual.data.span).size() == size
                   && std::memcmp(arena.span(actual.data.span).data(),
                                  group.subject.data(), size)
                          == 0;
        }
        if (group.field == NativeCaptureField::LensSpecification) {
            if (actual.kind != MetaValueKind::Array
                || actual.elem_type != MetaElementType::URational
                || actual.count != 4U
                || arena.span(actual.data.span).size() != sizeof(group.lens))
                return false;
            std::array<URational, 4> values {};
            std::memcpy(values.data(), arena.span(actual.data.span).data(),
                        sizeof(values));
            for (size_t i = 0U; i < values.size(); ++i) {
                const URational a = values[i];
                const URational b = group.lens[i];
                if (a.denom == 0U || b.denom == 0U) {
                    if (a.numer != 0U || b.numer != 0U || a.denom != b.denom)
                        return false;
                } else if (static_cast<uint64_t>(a.numer) * b.denom
                           != static_cast<uint64_t>(b.numer) * a.denom)
                    return false;
            }
            return true;
        }
        if (group.field == NativeCaptureField::ImageUniqueID) {
            if (actual.kind != MetaValueKind::Text
                || (actual.text_encoding != TextEncoding::Ascii
                    && actual.text_encoding != TextEncoding::Utf8))
                return false;
            std::string_view text = arena_text(arena, actual.data.span);
            if (text.size() != actual.count || text.size() != actual.data.span.size)
                return false;
            if (text.size() == 33U && text.back() == '\0')
                text.remove_suffix(1U);
            return text == group.identity;
        }
        return capture_value_matches(group.field, actual, group.value);
    }

    static MetaValue materialize_group_value(ByteArena& arena,
                                             const CapturePlannedGroup& group)
    {
        if (group.field == NativeCaptureField::FileSource
            || group.field == NativeCaptureField::SceneType) {
            const std::byte code = static_cast<std::byte>(group.value.data.u64);
            return make_bytes(arena, std::span(&code, 1U));
        }
        if (group.field == NativeCaptureField::SubjectArea
            || group.field == NativeCaptureField::SubjectLocation)
            return make_u16_array(arena, std::span(group.subject.data(),
                                                   group.subject_count));
        if (group.field == NativeCaptureField::LensSpecification)
            return make_urational_array(arena, group.lens);
        if (group.field == NativeCaptureField::ImageUniqueID)
            return make_text(arena, group.identity, TextEncoding::Ascii);
        return group.value;
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
                exact = group_value_matches(store.arena(), entry.value, *group);
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
                first_matches = group_value_matches(store.arena(), entry.value,
                                                    group);
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
        case NativeCaptureField::ShutterSpeedValue: tag = 0x9201U; break;
        case NativeCaptureField::ApertureValue: tag = 0x9202U; break;
        case NativeCaptureField::BrightnessValue: tag = 0x9203U; break;
        case NativeCaptureField::MaxApertureValue: tag = 0x9205U; break;
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
        case NativeCaptureField::SubjectDistance: tag = 0x9206U; break;
        case NativeCaptureField::DigitalZoomRatio: tag = 0xa404U; break;
        case NativeCaptureField::ExposureIndex: tag = 0xa215U; break;
        case NativeCaptureField::FlashEnergy: tag = 0xa20bU; break;
        case NativeCaptureField::Flash: tag = 0x9209U; break;
        case NativeCaptureField::LightSource: tag = 0x9208U; break;
        case NativeCaptureField::SensitivityType: tag = 0x8830U; break;
        case NativeCaptureField::StandardOutputSensitivity:
            tag = 0x8831U;
            break;
        case NativeCaptureField::RecommendedExposureIndex: tag = 0x8832U; break;
        case NativeCaptureField::ISOSpeed: tag = 0x8833U; break;
        case NativeCaptureField::ISOSpeedLatitudeyyy: tag = 0x8834U; break;
        case NativeCaptureField::ISOSpeedLatitudezzz: tag = 0x8835U; break;
        case NativeCaptureField::LensSpecification: tag = 0xa432U; break;
        case NativeCaptureField::ImageUniqueID: tag = 0xa420U; break;
        case NativeCaptureField::FocalPlaneXResolution: tag = 0xa20eU; break;
        case NativeCaptureField::FocalPlaneYResolution: tag = 0xa20fU; break;
        case NativeCaptureField::FocalPlaneResolutionUnit: tag = 0xa210U; break;
        case NativeCaptureField::SubjectArea: tag = 0x9214U; break;
        case NativeCaptureField::SubjectLocation: tag = 0xa214U; break;
        case NativeCaptureField::FocalLengthIn35mmFilm: tag = 0xa405U; break;
        case NativeCaptureField::FileSource: tag = 0xa300U; break;
        case NativeCaptureField::SceneType: tag = 0xa301U; break;
        case NativeCaptureField::Temperature: tag = 0x9400U; break;
        case NativeCaptureField::Humidity: tag = 0x9401U; break;
        case NativeCaptureField::Pressure: tag = 0x9402U; break;
        case NativeCaptureField::WaterDepth: tag = 0x9403U; break;
        case NativeCaptureField::Acceleration: tag = 0x9404U; break;
        case NativeCaptureField::CameraElevationAngle: tag = 0x9405U; break;
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
        entry.value  = materialize_group_value(edit->arena(), group);
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
            if (!group_value_matches(source.arena(), entry.value, group)) {
                edit->set_value(id,
                                materialize_group_value(edit->arena(), group),
                                WireType {}, 0U);
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
        MetaStore candidate = commit(source,
                                     std::span<const MetaEdit>(&edit, 1U));
        candidate.constrain_resources(0U, 0U);
        if (candidate.resource_limit_exceeded())
            return capture_error(
                MetadataCaptureTranslationStatus::EntryLimitExceeded);
        *out_store = std::move(candidate);
        return result;
    }

    static constexpr std::array<std::string_view, 7> kSensitivityPaths
        = { "PhotographicSensitivity",
            "SensitivityType",
            "StandardOutputSensitivity",
            "RecommendedExposureIndex",
            "ISOSpeed",
            "ISOSpeedLatitudeyyy",
            "ISOSpeedLatitudezzz" };

    static int sensitivity_member(std::string_view ns, std::string_view path,
                                  bool* malformed) noexcept
    {
        const bool standard = ns == "http://cipa.jp/exif/1.0/";
        if (!standard && ns != kXmpNsExif)
            return -1;
        for (size_t i = standard ? 0U : 1U; i < kSensitivityPaths.size(); ++i) {
            const auto name = kSensitivityPaths[i];
            if (path == name)
                return static_cast<int>(i);
            if (path.starts_with(name) && path.size() > name.size()
                && (path[name.size()] == '/' || path[name.size()] == '[')) {
                *malformed = true;
                return static_cast<int>(i);
            }
        }
        if (!standard) {
            if (path == "ISO" || path == "ISOSpeedRatings"
                || path == "ISOSpeedRatings[1]")
                return 0;
            if (path.starts_with("ISO/") || path.starts_with("ISO[")
                || path.starts_with("ISOSpeedRatings/")
                || path.starts_with("ISOSpeedRatings[")) {
                *malformed = true;
                return 0;
            }
        }
        return -1;
    }


    static int identity_member(const MetaStore& store, const Entry& entry,
                               bool lens) noexcept
    {
        if (entry.key.kind != MetaKeyKind::XmpProperty)
            return -1;
        const std::string_view ns
            = arena_text(store.arena(), entry.key.data.xmp_property.schema_ns);
        if (ns != kXmpNsExif && (!lens || ns != "http://cipa.jp/exif/1.0/"))
            return -1;
        const std::string_view path
            = arena_text(store.arena(),
                         entry.key.data.xmp_property.property_path);
        const std::string_view base = lens ? "LensSpecification"
                                           : "ImageUniqueID";
        if (path == base)
            return 0;
        if (!path.starts_with(base) || path.size() <= base.size()
            || (path[base.size()] != '[' && path[base.size()] != '/'))
            return -1;
        const std::string_view tail = path.substr(base.size());
        if (lens && tail.size() == 3U && tail[0] == '[' && tail[2] == ']'
            && tail[1] >= '1' && tail[1] <= '4')
            return tail[1] - '0';
        return -2;
    }

    static MetadataCaptureTranslationStatus
    identity_text_budget(const MetaStore& source, const MetaValue& value,
                         const MetadataIdentityTranslationOptions& options,
                         uint64_t* total)
    {
        using Status = MetadataCaptureTranslationStatus;
        if (value.kind != MetaValueKind::Text)
            return Status::Ok;
        const auto text = source.arena().span(value.data.span);
        if ((value.text_encoding != TextEncoding::Ascii
             && value.text_encoding != TextEncoding::Utf8)
            || text.size() != value.count
            || text.size() != value.data.span.size)
            return Status::InvalidSourceValue;
        if (text.size() > options.max_text_bytes_per_property)
            return Status::ValueTooLong;
        if (text.size() > options.max_total_text_bytes
            || *total > options.max_total_text_bytes - text.size())
            return Status::SourceLimitExceeded;
        *total += text.size();
        return Status::Ok;
    }

    static MetadataCaptureTranslationStatus
    parse_lens_member(const ByteArena& arena, const MetaValue& value,
                      size_t index, URational* out) noexcept
    {
        using Status = MetadataCaptureTranslationStatus;
        if (index >= 2U
            && ((value.kind == MetaValueKind::Scalar && value.count == 1U
                 && value.elem_type == MetaElementType::URational
                 && value.data.ur.numer == 0U && value.data.ur.denom == 0U)
                || (value.kind == MetaValueKind::Text
                    && arena_text(arena, value.data.span) == "0/0"))) {
            *out = { 0U, 0U };
            return Status::Ok;
        }
        if (value.kind == MetaValueKind::Scalar && value.count != 1U)
            return Status::InvalidSourceValue;
        MetaValue parsed;
        const Status status = parse_unsigned_rational_source(arena, value,
                                                             false, &parsed);
        if (status == Status::Ok)
            *out = parsed.data.ur;
        return status;
    }

    static MetadataCaptureTranslationStatus prepare_identity_group(
        const MetaStore& source,
        const MetadataIdentityTranslationOptions& options, bool lens,
        uint64_t* total, CapturePlannedGroup* group,
        MetadataCaptureTranslationResult* result, bool* found)
    {
        using Status  = MetadataCaptureTranslationStatus;
        using Mode    = MetadataCaptureTranslationSourceMode;
        *found        = false;
        bool selected = false;
        for (const Entry& entry : source.entries()) {
            if (identity_member(source, entry, lens) == -1)
                continue;
            const bool dirty = any(entry.flags, EntryFlags::Dirty);
            if ((!any(entry.flags, EntryFlags::Deleted) || dirty)
                && (dirty || options.source_mode == Mode::All))
                selected = true;
        }
        if (!selected)
            return Status::Ok;
        group->mapping
            = lens ? MetadataCaptureTranslationMapping::XmpLensSpecification
                   : MetadataCaptureTranslationMapping::XmpImageUniqueID;
        group->field           = lens ? NativeCaptureField::LensSpecification
                                      : NativeCaptureField::ImageUniqueID;
        result->failed_mapping = group->mapping;
        std::array<CaptureSource, 5> properties {};
        std::string_view selected_ns;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            const int member   = identity_member(source, entry, lens);
            if (member == -1)
                continue;
            const bool dirty   = any(entry.flags, EntryFlags::Dirty);
            const bool deleted = any(entry.flags, EntryFlags::Deleted);
            if ((deleted && !dirty)
                || (!lens && !dirty && options.source_mode == Mode::DirtyOnly))
                continue;
            result->failed_source_entry = id;
            if (member < 0)
                return Status::UnsupportedSourceShape;
            const auto ns = arena_text(source.arena(),
                                       entry.key.data.xmp_property.schema_ns);
            if (!selected_ns.empty() && selected_ns != ns)
                return Status::AmbiguousSource;
            selected_ns             = ns;
            CaptureSource& property = properties[static_cast<size_t>(member)];
            if (property.found)
                return Status::AmbiguousSource;
            property = { true, deleted, id, &entry.value };
            ++result->source_properties;
        }
        *found              = true;
        CaptureSource& root = properties[0];
        if (root.found) {
            group->source_entry         = root.entry_id;
            result->failed_source_entry = root.entry_id;
            for (size_t i = 1U; i < properties.size(); ++i)
                if (properties[i].found)
                    return Status::UnsupportedSourceShape;
            group->present = !root.deleted;
            if (root.deleted)
                return Status::Ok;
            Status status = identity_text_budget(source, *root.value, options,
                                                 total);
            if (status != Status::Ok)
                return status;
            if (!lens) {
                if (root.value->kind != MetaValueKind::Text)
                    return Status::InvalidSourceValue;
                const auto text = arena_text(source.arena(),
                                             root.value->data.span);
                if (text.size() != 32U)
                    return Status::InvalidSourceValue;
                for (char c : text)
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                          || (c >= 'A' && c <= 'F')))
                        return Status::InvalidSourceValue;
                group->identity = text;
                return Status::Ok;
            }
            const MetaValue& value = *root.value;
            if (value.kind != MetaValueKind::Array
                || value.elem_type != MetaElementType::URational
                || value.count != 4U
                || source.arena().span(value.data.span).size()
                       != sizeof(group->lens))
                return Status::InvalidSourceValue;
            std::array<URational, 4> values {};
            std::memcpy(values.data(),
                        source.arena().span(value.data.span).data(),
                        sizeof(values));
            for (size_t i = 0U; i < values.size(); ++i) {
                const MetaValue scalar = make_urational(values[i].numer,
                                                        values[i].denom);
                status = parse_lens_member(source.arena(), scalar, i,
                                           &group->lens[i]);
                if (status != Status::Ok)
                    return status;
            }
        } else {
            uint32_t deleted_count = 0U;
            for (size_t i = 1U; i < properties.size(); ++i) {
                const CaptureSource& property = properties[i];
                if (!property.found)
                    return Status::IncompleteSource;
                if (i == 1U)
                    group->source_entry = property.entry_id;
                result->failed_source_entry = property.entry_id;
                if (property.deleted) {
                    ++deleted_count;
                    continue;
                }
                Status status = identity_text_budget(source, *property.value,
                                                     options, total);
                if (status != Status::Ok)
                    return status;
                status = parse_lens_member(source.arena(), *property.value,
                                           i - 1U, &group->lens[i - 1U]);
                if (status != Status::Ok)
                    return status;
            }
            if (deleted_count != 0U && deleted_count != 4U)
                return Status::IncompleteSource;
            group->present = deleted_count == 0U;
        }
        if (group->present
            && static_cast<uint64_t>(group->lens[0].numer)
                       * group->lens[1].denom
                   > static_cast<uint64_t>(group->lens[1].numer)
                         * group->lens[0].denom)
            return Status::InvalidNumericValue;
        return Status::Ok;
    }

    static constexpr std::array<std::string_view, 5> kSpatialPaths
        = { "FocalPlaneXResolution", "FocalPlaneYResolution",
            "FocalPlaneResolutionUnit", "SubjectArea", "SubjectLocation" };

    // Member zero is the array root, or the first scalar in the focal group.
    static int spatial_member(const MetaStore& store, const Entry& entry,
                              size_t group) noexcept
    {
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || arena_text(store.arena(), entry.key.data.xmp_property.schema_ns)
                   != kXmpNsExif)
            return -1;
        const auto path    = arena_text(store.arena(),
                                        entry.key.data.xmp_property.property_path);
        const size_t begin = group == 0U ? 0U : group + 2U;
        const size_t end   = group == 0U ? 3U : begin + 1U;
        for (size_t i = begin; i < end; ++i) {
            const auto base = kSpatialPaths[i];
            if (path == base)
                return group == 0U ? static_cast<int>(i) : 0;
            if (!path.starts_with(base) || path.size() <= base.size()
                || (path[base.size()] != '[' && path[base.size()] != '/'
                    && path[base.size()] != '?'))
                continue;
            const auto tail = path.substr(base.size());
            const char last = group == 1U ? '4' : '2';
            if (group != 0U && tail.size() == 3U && tail[0] == '['
                && tail[2] == ']' && tail[1] >= '1' && tail[1] <= last)
                return tail[1] - '0';
            return -2;
        }
        return -1;
    }

    static MetadataCaptureTranslationStatus
    spatial_text_budget(const MetaStore& source, const MetaValue& value,
                        const MetadataCaptureSpatialTranslationOptions& options,
                        uint64_t* total) noexcept
    {
        using Status = MetadataCaptureTranslationStatus;
        if (value.kind != MetaValueKind::Text)
            return Status::Ok;
        const auto bytes = source.arena().span(value.data.span);
        if ((value.text_encoding != TextEncoding::Ascii
             && value.text_encoding != TextEncoding::Utf8)
            || bytes.size() != value.count
            || bytes.size() != value.data.span.size)
            return Status::InvalidSourceValue;
        if (bytes.size() > options.max_text_bytes_per_property)
            return Status::ValueTooLong;
        if (bytes.size() > options.max_total_text_bytes
            || *total > options.max_total_text_bytes - bytes.size())
            return Status::SourceLimitExceeded;
        *total += bytes.size();
        return Status::Ok;
    }

    static MetadataCaptureTranslationStatus
    parse_spatial_short(const ByteArena& arena, const MetaValue& value,
                        bool unit, uint16_t* out) noexcept
    {
        using Status    = MetadataCaptureTranslationStatus;
        uint64_t number = 0U;
        if (value.kind == MetaValueKind::Text) {
            auto text = arena_text(arena, value.data.span);
            if (unit && (text == "inches" || text == "cm")) {
                *out = text == "inches" ? 2U : 3U;
                return Status::Ok;
            }
            if (!text.empty() && text.front() == '+')
                text.remove_prefix(1U);
            const Status status = numeric_status(parse_digits(text, &number));
            if (status != Status::Ok)
                return status;
        } else if (value.kind != MetaValueKind::Scalar || value.count != 1U) {
            return Status::InvalidSourceValue;
        } else if (!scalar_unsigned(value, &number)) {
            int64_t integer = 0;
            if (!scalar_signed(value, &integer))
                return Status::InvalidSourceValue;
            if (integer < 0)
                return Status::ValueOutOfRange;
            number = static_cast<uint64_t>(integer);
        }
        if (number > UINT16_MAX || (unit && number != 2U && number != 3U))
            return Status::ValueOutOfRange;
        *out = static_cast<uint16_t>(number);
        return Status::Ok;
    }

    static MetadataCaptureTranslationStatus prepare_spatial_group(
        const MetaStore& source,
        const MetadataCaptureSpatialTranslationOptions& options, size_t index,
        std::span<CapturePlannedGroup> groups, uint64_t* total,
        MetadataCaptureTranslationResult* result, bool* found)
    {
        using Status = MetadataCaptureTranslationStatus;
        using Mode   = MetadataCaptureTranslationSourceMode;
        *found       = false;
        for (const Entry& entry : source.entries()) {
            if (spatial_member(source, entry, index) != -1
                && ((any(entry.flags, EntryFlags::Dirty))
                    || (options.source_mode == Mode::All
                        && !any(entry.flags, EntryFlags::Deleted)))) {
                *found = true;
                break;
            }
        }
        if (!*found)
            return Status::Ok;
        std::array<CaptureSource, 5> properties {};
        size_t member_count = 0U;
        for (EntryId id = 0U; id < source.entries().size(); ++id) {
            const Entry& entry = source.entry(id);
            const int member   = spatial_member(source, entry, index);
            if (member == -1
                || (any(entry.flags, EntryFlags::Deleted)
                    && !any(entry.flags, EntryFlags::Dirty)))
                continue;
            result->failed_source_entry = id;
            if (member < 0)
                return Status::UnsupportedSourceShape;
            CaptureSource& property = properties[static_cast<size_t>(member)];
            if (property.found)
                return Status::AmbiguousSource;
            property = { true, any(entry.flags, EntryFlags::Deleted), id,
                         &entry.value };
            ++result->source_properties;
            if (static_cast<size_t>(member) > member_count)
                member_count = static_cast<size_t>(member);
        }
        if (index == 0U) {
            for (size_t i = 0U; i < 3U; ++i) {
                const auto& property = properties[i];
                if (!property.found
                    || property.deleted != properties[0].deleted)
                    return Status::IncompleteSource;
                auto& group                 = groups[i];
                group.source_entry          = property.entry_id;
                result->failed_source_entry = property.entry_id;
                group.present               = !property.deleted;
                if (!group.present)
                    continue;
                Status status = spatial_text_budget(source, *property.value,
                                                    options, total);
                if (status != Status::Ok)
                    return status;
                if (i == 2U) {
                    uint16_t unit = 0U;
                    status        = parse_spatial_short(source.arena(),
                                                        *property.value, true, &unit);
                    group.value   = make_u16(unit);
                } else {
                    status = parse_capture_rational_source(source.arena(),
                                                           *property.value,
                                                           group.field,
                                                           &group.value);
                    if (status == Status::Ok && group.value.data.ur.numer == 0U)
                        status = Status::ValueOutOfRange;
                }
                if (status != Status::Ok)
                    return status;
            }
            return Status::Ok;
        }
        auto& group      = groups[0];
        const auto& root = properties[0];
        if (root.found) {
            group.source_entry          = root.entry_id;
            result->failed_source_entry = root.entry_id;
            if (member_count != 0U)
                return Status::UnsupportedSourceShape;
            group.present = !root.deleted;
            if (!group.present)
                return Status::Ok;
            const MetaValue& value = *root.value;
            if (value.kind != MetaValueKind::Array
                || value.elem_type != MetaElementType::U16 || value.count < 2U
                || value.count > (index == 1U ? 4U : 2U))
                return Status::InvalidSourceValue;
            const auto bytes = source.arena().span(value.data.span);
            if (bytes.size() != value.count * sizeof(uint16_t)
                || value.data.span.size != bytes.size())
                return Status::InvalidSourceValue;
            group.subject_count = value.count;
            std::memcpy(group.subject.data(), bytes.data(), bytes.size());
            return Status::Ok;
        }
        if (member_count < 2U)
            return Status::IncompleteSource;
        group.subject_count = static_cast<uint32_t>(member_count);
        for (size_t i = 1U; i <= member_count; ++i) {
            const auto& property = properties[i];
            if (!property.found || property.deleted != properties[1].deleted)
                return Status::IncompleteSource;
            if (i == 1U)
                group.source_entry = property.entry_id;
            result->failed_source_entry = property.entry_id;
            group.present               = !property.deleted;
            if (!group.present)
                continue;
            Status status = spatial_text_budget(source, *property.value,
                                                options, total);
            if (status != Status::Ok)
                return status;
            status = parse_spatial_short(source.arena(), *property.value, false,
                                         &group.subject[i - 1U]);
            if (status != Status::Ok)
                return status;
        }
        return Status::Ok;
    }

}  // namespace

MetadataCaptureTranslationResult
translate_xmp_capture_spatial_metadata(
    const MetaStore& source,
    const MetadataCaptureSpatialTranslationOptions& options,
    MetaStore* out_store)
{
    using Status  = MetadataCaptureTranslationStatus;
    using Mode    = MetadataCaptureTranslationSourceMode;
    using Policy  = MetadataCaptureTranslationConflictPolicy;
    using Mapping = MetadataCaptureTranslationMapping;
    if (!out_store)
        return capture_error(Status::NullOutput);
    if (!source.is_finalized())
        return capture_error(Status::SourceNotFinalized);
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.focal_plane_to_exif && !options.subject_area_to_exif
            && !options.subject_location_to_exif)
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataCaptureSpatialTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataCaptureSpatialTranslationMaxTotalTextBytes)
        return capture_error(Status::InvalidOptions);

    constexpr std::array<NativeCaptureField, 5> fields = {
        NativeCaptureField::FocalPlaneXResolution,
        NativeCaptureField::FocalPlaneYResolution,
        NativeCaptureField::FocalPlaneResolutionUnit,
        NativeCaptureField::SubjectArea, NativeCaptureField::SubjectLocation
    };
    constexpr std::array<Mapping, 3> mappings
        = { Mapping::XmpFocalPlaneResolution, Mapping::XmpSubjectArea,
            Mapping::XmpSubjectLocation };
    const std::array<bool, 3> enabled = { options.focal_plane_to_exif,
                                          options.subject_area_to_exif,
                                          options.subject_location_to_exif };
    std::array<CapturePlannedGroup, 5> prepared {};
    std::array<CapturePlannedGroup, 5> pending {};
    size_t count        = 0U;
    uint64_t total      = 0U;
    uint32_t translated = 0U;
    MetadataCaptureTranslationResult result;
    for (size_t i = 0U; i < 3U; ++i) {
        if (!enabled[i])
            continue;
        const size_t first = i == 0U ? 0U : i + 2U;
        const size_t size  = i == 0U ? 3U : 1U;
        auto groups        = std::span(prepared.data() + first, size);
        for (size_t j = 0U; j < size; ++j) {
            groups[j].field   = fields[first + j];
            groups[j].mapping = mappings[i];
        }
        result.failed_mapping = mappings[i];
        bool found            = false;
        result.status = prepare_spatial_group(source, options, i, groups,
                                              &total, &result, &found);
        if (result.status != Status::Ok)
            return result;
        if (!found)
            continue;
        bool existing = false;
        bool exact    = true;
        for (auto& group : groups) {
            analyze_group(source, &group);
            existing = existing || group.existing_any;
            exact    = exact && group.exact_match;
        }
        if (existing && options.conflict_policy == Policy::FailOnConflict
            && !exact) {
            result.status = Status::NativeConflict;
            return result;
        }
        if (existing && options.conflict_policy == Policy::PreserveExisting) {
            ++result.groups_preserved;
        } else if (exact) {
            ++result.groups_unchanged;
        } else {
            ++translated;
            for (const auto& group : groups)
                pending[count++] = group;
        }
    }
    const uint32_t unchanged   = result.groups_unchanged;
    result.failed_mapping      = Mapping::None;
    result.failed_source_entry = kInvalidEntryId;
    result = apply_capture_groups(source, std::span(pending.data(), count),
                                  Policy::ReplaceExisting,
                                  options.max_added_entries,
                                  options.max_operations, result, out_store);
    if (result.status == Status::Ok) {
        result.groups_translated = translated;
        result.groups_unchanged  = unchanged;
    }
    return result;
}

MetadataCaptureTranslationResult
translate_xmp_identity_metadata(
    const MetaStore& source, const MetadataIdentityTranslationOptions& options,
    MetaStore* out_store)
{
    using Status = MetadataCaptureTranslationStatus;
    using Mode   = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;
    if (!out_store)
        return capture_error(Status::NullOutput);
    if (!source.is_finalized())
        return capture_error(Status::SourceNotFinalized);
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.lens_specification_to_exif
            && !options.image_unique_id_to_exif)
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataIdentityTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataIdentityTranslationMaxTotalTextBytes)
        return capture_error(Status::InvalidOptions);
    std::array<CapturePlannedGroup, 2> groups {};
    const std::array<bool, 2> enabled = { options.lens_specification_to_exif,
                                          options.image_unique_id_to_exif };
    size_t count                      = 0U;
    uint64_t total                    = 0U;
    MetadataCaptureTranslationResult result;
    for (size_t i = 0U; i < enabled.size(); ++i) {
        if (!enabled[i])
            continue;
        bool found    = false;
        result.status = prepare_identity_group(source, options, i == 0U, &total,
                                               &groups[count], &result, &found);
        if (result.status != Status::Ok)
            return result;
        if (found)
            ++count;
    }
    result.failed_mapping      = MetadataCaptureTranslationMapping::None;
    result.failed_source_entry = kInvalidEntryId;
    return apply_capture_groups(source, std::span(groups.data(), count),
                                options.conflict_policy,
                                options.max_added_entries,
                                options.max_operations, result, out_store);
}

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

MetadataCaptureTranslationResult
translate_xmp_capture_additional_metadata(
    const MetaStore& source,
    const MetadataCaptureAdditionalTranslationOptions& options,
    MetaStore* out_store)
{
    using Status = MetadataCaptureTranslationStatus;
    using Mode   = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;
    if (!out_store)
        return capture_error(Status::NullOutput);
    if (!source.is_finalized())
        return capture_error(Status::SourceNotFinalized);
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.focal_length_in_35mm_film_to_exif
            && !options.file_source_to_exif && !options.scene_type_to_exif)
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataCaptureAdditionalTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataCaptureAdditionalTranslationMaxTotalTextBytes)
        return capture_error(Status::InvalidOptions);
    struct Mapping final {
        std::array<std::string_view, 2> paths;
        size_t path_count;
        NativeCaptureField field;
        MetadataCaptureTranslationMapping mapping;
        bool enabled;
    };
    const std::array<Mapping, 3> mappings { {
        { { "FocalLengthIn35mmFilm", "FocalLengthIn35mmFormat" },
          2U,
          NativeCaptureField::FocalLengthIn35mmFilm,
          MetadataCaptureTranslationMapping::XmpFocalLengthIn35mmFilm,
          options.focal_length_in_35mm_film_to_exif },
        { { "FileSource", {} },
          1U,
          NativeCaptureField::FileSource,
          MetadataCaptureTranslationMapping::XmpFileSource,
          options.file_source_to_exif },
        { { "SceneType", {} },
          1U,
          NativeCaptureField::SceneType,
          MetadataCaptureTranslationMapping::XmpSceneType,
          options.scene_type_to_exif },
    } };
    std::array<CapturePlannedGroup, 3> groups {};
    size_t count        = 0U;
    uint64_t text_bytes = 0U;
    MetadataCaptureTranslationResult result;
    for (const Mapping& mapping : mappings) {
        if (!mapping.enabled)
            continue;
        CaptureSource property;
        Status status = find_additional_source(
            source, std::span(mapping.paths.data(), mapping.path_count), false,
            options.source_mode, &property);
        if (status == Status::Ok && !property.found)
            continue;
        CapturePlannedGroup group;
        group.mapping      = mapping.mapping;
        group.field        = mapping.field;
        group.source_entry = property.entry_id;
        if (status == Status::Ok && !property.deleted) {
            const MetaValue& value = *property.value;
            if (value.kind == MetaValueKind::Text) {
                const auto text = arena_text(source.arena(), value.data.span);
                if (text.size() != value.count
                    || text.size() != value.data.span.size)
                    status = Status::InvalidSourceValue;
                else if (text.size() > options.max_text_bytes_per_property)
                    status = Status::ValueTooLong;
                else if (text.size() > options.max_total_text_bytes
                         || text_bytes
                                > options.max_total_text_bytes - text.size())
                    status = Status::SourceLimitExceeded;
                else if (value.text_encoding != TextEncoding::Ascii
                         && value.text_encoding != TextEncoding::Utf8
                         && value.text_encoding != TextEncoding::Unknown)
                    status = Status::InvalidSourceValue;
                else
                    text_bytes += text.size();
            }
            if (status == Status::Ok)
                status = parse_additional_code(source.arena(), value,
                                               mapping.field, &group.value);
            group.present = status == Status::Ok;
        }
        if (status != Status::Ok) {
            result.status              = status;
            result.failed_mapping      = mapping.mapping;
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

MetadataCaptureTranslationResult
translate_xmp_environment_metadata(
    const MetaStore& source,
    const MetadataEnvironmentTranslationOptions& options, MetaStore* out_store)
{
    using Status = MetadataCaptureTranslationStatus;
    using Mode   = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;
    if (!out_store)
        return capture_error(Status::NullOutput);
    if (!source.is_finalized())
        return capture_error(Status::SourceNotFinalized);
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.temperature_to_exif && !options.humidity_to_exif
            && !options.pressure_to_exif && !options.water_depth_to_exif
            && !options.acceleration_to_exif
            && !options.camera_elevation_angle_to_exif)
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataEnvironmentTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataEnvironmentTranslationMaxTotalTextBytes)
        return capture_error(Status::InvalidOptions);
    struct Mapping final {
        std::array<std::string_view, 2> paths;
        size_t path_count;
        NativeCaptureField field;
        MetadataCaptureTranslationMapping mapping;
        bool enabled;
    };
    const std::array<Mapping, 6> mappings { {
        { { "Temperature", {} },
          1U,
          NativeCaptureField::Temperature,
          MetadataCaptureTranslationMapping::XmpTemperature,
          options.temperature_to_exif },
        { { "Humidity", {} },
          1U,
          NativeCaptureField::Humidity,
          MetadataCaptureTranslationMapping::XmpHumidity,
          options.humidity_to_exif },
        { { "Pressure", {} },
          1U,
          NativeCaptureField::Pressure,
          MetadataCaptureTranslationMapping::XmpPressure,
          options.pressure_to_exif },
        { { "WaterDepth", {} },
          1U,
          NativeCaptureField::WaterDepth,
          MetadataCaptureTranslationMapping::XmpWaterDepth,
          options.water_depth_to_exif },
        { { "Acceleration", {} },
          1U,
          NativeCaptureField::Acceleration,
          MetadataCaptureTranslationMapping::XmpAcceleration,
          options.acceleration_to_exif },
        { { "CameraElevationAngle", {} },
          1U,
          NativeCaptureField::CameraElevationAngle,
          MetadataCaptureTranslationMapping::XmpCameraElevationAngle,
          options.camera_elevation_angle_to_exif },
    } };
    std::array<CapturePlannedGroup, 6> groups {};
    size_t count        = 0U;
    uint64_t text_bytes = 0U;
    MetadataCaptureTranslationResult result;
    for (const Mapping& mapping : mappings) {
        if (!mapping.enabled)
            continue;
        CaptureSource property;
        Status status = find_additional_source(
            source, std::span(mapping.paths.data(), mapping.path_count), true,
            options.source_mode, &property);
        if (status == Status::Ok && !property.found)
            continue;
        CapturePlannedGroup group;
        group.mapping      = mapping.mapping;
        group.field        = mapping.field;
        group.source_entry = property.entry_id;
        if (status == Status::Ok && !property.deleted) {
            const MetaValue& value = *property.value;
            if (value.kind == MetaValueKind::Text) {
                const auto text = arena_text(source.arena(), value.data.span);
                if (text.size() != value.count
                    || text.size() != value.data.span.size)
                    status = Status::InvalidSourceValue;
                else if (text.size() > options.max_text_bytes_per_property)
                    status = Status::ValueTooLong;
                else if (text.size() > options.max_total_text_bytes
                         || text_bytes
                                > options.max_total_text_bytes - text.size())
                    status = Status::SourceLimitExceeded;
                else if (value.text_encoding != TextEncoding::Ascii
                         && value.text_encoding != TextEncoding::Utf8
                         && value.text_encoding != TextEncoding::Unknown)
                    status = Status::InvalidSourceValue;
                else
                    text_bytes += text.size();
            }
            if (status == Status::Ok)
                status = parse_environment_source(source.arena(), value,
                                                  mapping.field, &group.value);
            group.present = status == Status::Ok;
        }
        if (status != Status::Ok) {
            result.status              = status;
            result.failed_mapping      = mapping.mapping;
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

MetadataCaptureTranslationResult
translate_xmp_apex_metadata(const MetaStore& source,
                            const MetadataApexTranslationOptions& options,
                            MetaStore* out_store)
{
    using Status = MetadataCaptureTranslationStatus;
    using Mode   = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;
    if (!out_store)
        return capture_error(Status::NullOutput);
    if (!source.is_finalized())
        return capture_error(Status::SourceNotFinalized);
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.shutter_speed_value_to_exif
            && !options.aperture_value_to_exif
            && !options.brightness_value_to_exif
            && !options.exposure_bias_value_to_exif
            && !options.max_aperture_value_to_exif)
        || options.max_added_entries == 0U
        || options.max_added_entries > kMetadataApexTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataApexTranslationMaxTotalTextBytes)
        return capture_error(Status::InvalidOptions);
    struct Mapping final {
        std::array<std::string_view, 2> paths;
        size_t path_count;
        NativeCaptureField field;
        MetadataCaptureTranslationMapping mapping;
        bool enabled;
    };
    const std::array<Mapping, 5> mappings { {
        { { "ShutterSpeedValue", {} },
          1U,
          NativeCaptureField::ShutterSpeedValue,
          MetadataCaptureTranslationMapping::XmpShutterSpeedValue,
          options.shutter_speed_value_to_exif },
        { { "ApertureValue", {} },
          1U,
          NativeCaptureField::ApertureValue,
          MetadataCaptureTranslationMapping::XmpApertureValue,
          options.aperture_value_to_exif },
        { { "BrightnessValue", {} },
          1U,
          NativeCaptureField::BrightnessValue,
          MetadataCaptureTranslationMapping::XmpBrightnessValue,
          options.brightness_value_to_exif },
        { { "ExposureBiasValue", "ExposureCompensation" },
          2U,
          NativeCaptureField::ExposureBias,
          MetadataCaptureTranslationMapping::XmpExposureCompensation,
          options.exposure_bias_value_to_exif },
        { { "MaxApertureValue", {} },
          1U,
          NativeCaptureField::MaxApertureValue,
          MetadataCaptureTranslationMapping::XmpMaxApertureValue,
          options.max_aperture_value_to_exif },
    } };
    std::array<CapturePlannedGroup, 5> groups {};
    size_t count        = 0U;
    uint64_t text_bytes = 0U;
    MetadataCaptureTranslationResult result;
    for (const Mapping& mapping : mappings) {
        if (!mapping.enabled)
            continue;
        CaptureSource property;
        Status status = find_apex_source(source,
                                         std::span(mapping.paths.data(),
                                                   mapping.path_count),
                                         options.source_mode, &property);
        if (status == Status::Ok && !property.found)
            continue;
        CapturePlannedGroup group;
        group.mapping      = mapping.mapping;
        group.field        = mapping.field;
        group.source_entry = property.entry_id;
        if (status == Status::Ok && !property.deleted) {
            const MetaValue& value = *property.value;
            if (value.kind == MetaValueKind::Text) {
                const std::string_view text = arena_text(source.arena(),
                                                         value.data.span);
                if (text.size() > options.max_text_bytes_per_property)
                    status = Status::ValueTooLong;
                else if (text.size() > options.max_total_text_bytes
                         || text_bytes
                                > options.max_total_text_bytes - text.size())
                    status = Status::SourceLimitExceeded;
                else if (text.size() != value.count
                         || text.size() != value.data.span.size)
                    status = Status::InvalidSourceValue;
                else if (value.text_encoding != TextEncoding::Ascii
                         && value.text_encoding != TextEncoding::Utf8
                         && value.text_encoding != TextEncoding::Unknown)
                    status = Status::InvalidSourceValue;
                else
                    text_bytes += text.size();
            }
            if (status == Status::Ok)
                status = parse_apex_source(source.arena(), value, mapping.field,
                                           &group.value);
            group.present = status == Status::Ok;
        }
        if (status != Status::Ok) {
            result.status              = status;
            result.failed_mapping      = mapping.mapping;
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

MetadataCaptureTranslationResult
translate_xmp_capture_rational_metadata(
    const MetaStore& source,
    const MetadataCaptureRationalTranslationOptions& options,
    MetaStore* out_store)
{
    using Status = MetadataCaptureTranslationStatus;
    using Mode   = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;
    if (!out_store)
        return capture_error(Status::NullOutput);
    if (!source.is_finalized())
        return capture_error(Status::SourceNotFinalized);
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || (!options.subject_distance_to_exif
            && !options.digital_zoom_ratio_to_exif
            && !options.exposure_index_to_exif && !options.flash_energy_to_exif)
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataCaptureRationalTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataCaptureRationalTranslationMaxTotalTextBytes)
        return capture_error(Status::InvalidOptions);
    struct Mapping final {
        std::string_view path;
        NativeCaptureField field;
        MetadataCaptureTranslationMapping mapping;
        bool enabled;
    };
    const std::array<Mapping, 4U> mappings { {
        { "SubjectDistance", NativeCaptureField::SubjectDistance,
          MetadataCaptureTranslationMapping::XmpSubjectDistance,
          options.subject_distance_to_exif },
        { "DigitalZoomRatio", NativeCaptureField::DigitalZoomRatio,
          MetadataCaptureTranslationMapping::XmpDigitalZoomRatio,
          options.digital_zoom_ratio_to_exif },
        { "ExposureIndex", NativeCaptureField::ExposureIndex,
          MetadataCaptureTranslationMapping::XmpExposureIndex,
          options.exposure_index_to_exif },
        { "FlashEnergy", NativeCaptureField::FlashEnergy,
          MetadataCaptureTranslationMapping::XmpFlashEnergy,
          options.flash_energy_to_exif },
    } };
    std::array<CapturePlannedGroup, 4U> groups {};
    size_t count        = 0U;
    uint64_t text_bytes = 0U;
    MetadataCaptureTranslationResult result;
    for (const Mapping& mapping : mappings) {
        if (!mapping.enabled)
            continue;
        CaptureSource property;
        Status status = find_capture_source(source,
                                            std::span(&mapping.path, 1U),
                                            options.source_mode, &property);
        if (status == Status::Ok && !property.found)
            continue;
        CapturePlannedGroup group;
        group.mapping      = mapping.mapping;
        group.field        = mapping.field;
        group.source_entry = property.entry_id;
        if (status == Status::Ok && !property.deleted) {
            const MetaValue& value = *property.value;
            if (value.kind == MetaValueKind::Text) {
                const std::string_view text = arena_text(source.arena(),
                                                         value.data.span);
                if (text.size() > options.max_text_bytes_per_property)
                    status = Status::ValueTooLong;
                else if (text.size() > options.max_total_text_bytes
                         || text_bytes
                                > options.max_total_text_bytes - text.size())
                    status = Status::SourceLimitExceeded;
                else if (value.text_encoding != TextEncoding::Ascii
                         && value.text_encoding != TextEncoding::Utf8
                         && value.text_encoding != TextEncoding::Unknown)
                    status = Status::InvalidSourceValue;
                else
                    text_bytes += text.size();
            }
            if (status == Status::Ok)
                status = parse_capture_rational_source(source.arena(), value,
                                                       mapping.field,
                                                       &group.value);
            group.present = status == Status::Ok;
        }
        if (status != Status::Ok) {
            result.status              = status;
            result.failed_mapping      = mapping.mapping;
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

MetadataCaptureTranslationResult
translate_xmp_flash_metadata(const MetaStore& source,
                             const MetadataFlashTranslationOptions& options,
                             MetaStore* out_store)
{
    using Status = MetadataCaptureTranslationStatus;
    using Mode   = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;
    if (!out_store)
        return flash_error(Status::NullOutput);
    if (!source.is_finalized())
        return flash_error(Status::SourceNotFinalized);
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || options.max_added_entries == 0U
        || options.max_added_entries > kMetadataFlashTranslationMaxAddedEntries
        || options.max_source_properties == 0U
        || options.max_source_properties
               > kMetadataFlashTranslationMaxSourceProperties
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataFlashTranslationMaxTotalTextBytes)
        return flash_error(Status::InvalidOptions);

    constexpr std::array<std::string_view, 5> names { "Fired", "Function",
                                                      "Mode", "RedEyeMode",
                                                      "Return" };
    std::array<CaptureSource, 6> sources {};
    Status shape_status  = Status::Ok;
    EntryId bad_shape    = kInvalidEntryId;
    EntryId first_source = kInvalidEntryId;
    uint32_t inspected   = 0U;
    bool eligible        = false;
    for (EntryId id = 0U; id < source.entries().size(); ++id) {
        const Entry& entry = source.entry(id);
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || arena_text(source.arena(), entry.key.data.xmp_property.schema_ns)
                   != kXmpNsExif)
            continue;
        const bool dirty   = any(entry.flags, EntryFlags::Dirty);
        const bool deleted = any(entry.flags, EntryFlags::Deleted);
        if (deleted && !dirty)
            continue;
        const std::string_view path
            = arena_text(source.arena(),
                         entry.key.data.xmp_property.property_path);
        if (path != "Flash" && !path.starts_with("Flash/")
            && !path.starts_with("Flash["))
            continue;
        if (++inspected > options.max_source_properties)
            return flash_error(Status::SourceLimitExceeded, id);
        if (first_source == kInvalidEntryId)
            first_source = id;
        eligible    = eligible || dirty || options.source_mode == Mode::All;
        size_t slot = sources.size();
        if (path == "Flash")
            slot = 5U;
        else if (path.starts_with("Flash/")) {
            std::string_view child = path.substr(6U);
            if (child.starts_with("exif:"))
                child.remove_prefix(5U);
            for (size_t i = 0U; i < names.size(); ++i)
                if (child == names[i])
                    slot = i;
        }
        if (slot == sources.size()) {
            if (shape_status == Status::Ok) {
                shape_status = Status::UnsupportedSourceShape;
                bad_shape    = id;
            }
            continue;
        }
        if (sources[slot].found) {
            if (shape_status == Status::Ok) {
                shape_status = Status::AmbiguousSource;
                bad_shape    = id;
            }
            continue;
        }
        sources[slot] = CaptureSource { true, deleted, id, &entry.value };
    }
    if (!eligible)
        return apply_capture_groups(source, {}, options.conflict_policy,
                                    options.max_added_entries,
                                    options.max_operations, {}, out_store);
    if (shape_status != Status::Ok)
        return flash_error(shape_status, bad_shape);
    uint32_t children         = 0U;
    uint32_t deleted_children = 0U;
    for (size_t i = 0U; i < names.size(); ++i) {
        children += sources[i].found ? 1U : 0U;
        deleted_children += sources[i].found && sources[i].deleted ? 1U : 0U;
    }
    const bool scalar = sources[5U].found;
    if (scalar && children != 0U)
        return flash_error(Status::AmbiguousSource, sources[5U].entry_id);
    if (!scalar
        && (children != 5U
            || (deleted_children != 0U && deleted_children != 5U)))
        return flash_error(Status::IncompleteSource, first_source);

    CapturePlannedGroup group;
    group.field        = NativeCaptureField::Flash;
    group.mapping      = MetadataCaptureTranslationMapping::XmpFlash;
    group.source_entry = scalar ? sources[5U].entry_id : sources[0U].entry_id;
    group.present      = scalar ? !sources[5U].deleted : deleted_children == 0U;
    std::array<uint64_t, 6> values {};
    uint64_t text_bytes = 0U;
    if (group.present) {
        const size_t begin = scalar ? 5U : 0U;
        const size_t end   = scalar ? 6U : 5U;
        for (size_t i = begin; i < end; ++i) {
            const MetaValue& value = *sources[i].value;
            if (value.kind == MetaValueKind::Text) {
                const std::string_view text = arena_text(source.arena(),
                                                         value.data.span);
                if (text.size() > options.max_text_bytes_per_property)
                    return flash_error(Status::ValueTooLong,
                                       sources[i].entry_id);
                if (text.size() > options.max_total_text_bytes
                    || text_bytes > options.max_total_text_bytes - text.size())
                    return flash_error(Status::SourceLimitExceeded,
                                       sources[i].entry_id);
                if (value.text_encoding != TextEncoding::Ascii
                    && value.text_encoding != TextEncoding::Utf8
                    && value.text_encoding != TextEncoding::Unknown)
                    return flash_error(Status::InvalidSourceValue,
                                       sources[i].entry_id);
                text_bytes += text.size();
            }
            const bool boolean_field = i == 0U || i == 1U || i == 3U;
            const Status parsed = parse_flash_integer(source.arena(), value,
                                                      scalar, boolean_field,
                                                      &values[i]);
            if (parsed != Status::Ok)
                return flash_error(parsed, sources[i].entry_id);
            if ((boolean_field && values[i] > 1U) || (i == 2U && values[i] > 3U)
                || (i == 4U && (values[i] > 3U || values[i] == 1U)))
                return flash_error(Status::ValueOutOfRange,
                                   sources[i].entry_id);
        }
        const uint64_t code = scalar ? values[5U]
                                     : values[0U] | (values[4U] << 1U)
                                           | (values[2U] << 3U)
                                           | (values[1U] << 5U)
                                           | (values[3U] << 6U);
        if (code > 127U || ((code >> 1U) & 3U) == 1U)
            return flash_error(Status::ValueOutOfRange, group.source_entry);
        group.value = make_u16(static_cast<uint16_t>(code));
    }
    MetadataCaptureTranslationResult result;
    result.source_properties = scalar ? 1U : 5U;
    return apply_capture_groups(source, std::span(&group, 1U),
                                options.conflict_policy,
                                options.max_added_entries,
                                options.max_operations, result, out_store);
}

MetadataCaptureTranslationResult
translate_xmp_light_source_metadata(
    const MetaStore& source,
    const MetadataLightSourceTranslationOptions& options, MetaStore* out_store)
{
    using Status = MetadataCaptureTranslationStatus;
    using Mode   = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;
    if (!out_store)
        return capture_error(Status::NullOutput);
    if (!source.is_finalized())
        return capture_error(Status::SourceNotFinalized);
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataLightSourceTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataLightSourceTranslationMaxTotalTextBytes)
        return capture_error(Status::InvalidOptions);

    constexpr std::string_view path = "LightSource";
    CaptureSource property;
    Status status = find_capture_source(source, std::span(&path, 1U),
                                        options.source_mode, &property);
    MetadataCaptureTranslationResult result;
    result.failed_mapping = MetadataCaptureTranslationMapping::XmpLightSource;
    result.failed_source_entry = property.entry_id;
    for (EntryId id = 0U; status == Status::Ok && id < source.entries().size();
         ++id) {
        const Entry& entry = source.entry(id);
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || arena_text(source.arena(), entry.key.data.xmp_property.schema_ns)
                   != kXmpNsExif)
            continue;
        const bool dirty = any(entry.flags, EntryFlags::Dirty);
        if ((!dirty && options.source_mode == Mode::DirtyOnly)
            || (!dirty && any(entry.flags, EntryFlags::Deleted)))
            continue;
        const std::string_view candidate
            = arena_text(source.arena(),
                         entry.key.data.xmp_property.property_path);
        if (candidate.starts_with("LightSource/")
            || candidate.starts_with("LightSource[")) {
            status                     = Status::UnsupportedSourceShape;
            result.failed_source_entry = id;
        }
    }
    CapturePlannedGroup group;
    group.mapping      = MetadataCaptureTranslationMapping::XmpLightSource;
    group.field        = NativeCaptureField::LightSource;
    group.source_entry = property.entry_id;
    if (status == Status::Ok && property.found && !property.deleted) {
        const MetaValue& value = *property.value;
        uint64_t code          = 0U;
        if (value.kind == MetaValueKind::Text) {
            const std::string_view input = arena_text(source.arena(),
                                                      value.data.span);
            if (input.size() > options.max_text_bytes_per_property)
                status = Status::ValueTooLong;
            else if (input.size() > options.max_total_text_bytes)
                status = Status::SourceLimitExceeded;
            else if (value.text_encoding != TextEncoding::Ascii
                     && value.text_encoding != TextEncoding::Utf8
                     && value.text_encoding != TextEncoding::Unknown)
                status = Status::InvalidSourceValue;
            else {
                const NumericParseStatus parsed = parse_digits(input, &code);
                if (parsed != NumericParseStatus::Ok) {
                    uint32_t matches = 0U;
                    for (uint16_t candidate = 0U; candidate <= 255U;
                         ++candidate) {
                        if (!(candidate <= 4U
                              || (candidate >= 9U && candidate <= 34U)
                              || candidate == 255U))
                            continue;
                        const std::string_view label = exif_light_source_name(
                            candidate);
                        if ((!label.empty() && input == label)
                            || (candidate == 3U && input == "Tungsten")
                            || (candidate == 10U && input == "Cloudy weather")) {
                            code = candidate;
                            ++matches;
                        }
                    }
                    if (matches > 1U)
                        status = Status::AmbiguousSource;
                    else if (matches == 0U)
                        status = numeric_status(parsed);
                }
            }
        } else if (value.kind != MetaValueKind::Scalar || value.count != 1U) {
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
            && !(code <= 4U || (code >= 9U && code <= 34U) || code == 255U))
            status = Status::ValueOutOfRange;
        if (status == Status::Ok) {
            group.value   = make_u16(static_cast<uint16_t>(code));
            group.present = true;
        }
    }
    if (status != Status::Ok) {
        result.status = status;
        return result;
    }
    result.failed_mapping      = MetadataCaptureTranslationMapping::None;
    result.failed_source_entry = kInvalidEntryId;
    result.source_properties   = property.found ? 1U : 0U;
    return apply_capture_groups(source,
                                std::span(&group, property.found ? 1U : 0U),
                                options.conflict_policy,
                                options.max_added_entries,
                                options.max_operations, result, out_store);
}

MetadataCaptureTranslationResult
translate_xmp_sensitivity_metadata(
    const MetaStore& source,
    const MetadataSensitivityTranslationOptions& options, MetaStore* out_store)
{
    using Status = MetadataCaptureTranslationStatus;
    using Mode   = MetadataCaptureTranslationSourceMode;
    using Policy = MetadataCaptureTranslationConflictPolicy;
    if (!out_store)
        return capture_error(Status::NullOutput);
    if (!source.is_finalized())
        return capture_error(Status::SourceNotFinalized);
    if ((options.source_mode != Mode::DirtyOnly
         && options.source_mode != Mode::All)
        || (options.conflict_policy != Policy::PreserveExisting
            && options.conflict_policy != Policy::FailOnConflict
            && options.conflict_policy != Policy::ReplaceExisting)
        || options.max_added_entries == 0U
        || options.max_added_entries
               > kMetadataSensitivityTranslationMaxAddedEntries
        || options.max_operations == 0U
        || options.max_operations > kMetadataCaptureTranslationMaxOperations
        || options.max_text_bytes_per_property == 0U
        || options.max_text_bytes_per_property
               > kMetadataCaptureTranslationMaxTextBytesPerProperty
        || options.max_total_text_bytes == 0U
        || options.max_total_text_bytes
               > kMetadataSensitivityTranslationMaxTotalTextBytes)
        return capture_error(Status::InvalidOptions);

    bool selected = false;
    for (const Entry& entry : source.entries()) {
        if (entry.key.kind != MetaKeyKind::XmpProperty)
            continue;
        const bool dirty = any(entry.flags, EntryFlags::Dirty);
        if ((!dirty && any(entry.flags, EntryFlags::Deleted))
            || (!dirty && options.source_mode == Mode::DirtyOnly))
            continue;
        bool malformed = false;
        if (sensitivity_member(
                arena_text(source.arena(),
                           entry.key.data.xmp_property.schema_ns),
                arena_text(source.arena(),
                           entry.key.data.xmp_property.property_path),
                &malformed)
            >= 0)
            selected = true;
    }
    if (!selected)
        return apply_capture_groups(source, {}, Policy::ReplaceExisting,
                                    options.max_added_entries,
                                    options.max_operations, {}, out_store);

    MetadataCaptureTranslationResult result;
    result.failed_mapping = MetadataCaptureTranslationMapping::XmpSensitivity;
    std::array<CaptureSource, 7> properties {};
    for (EntryId id = 0U; id < source.entries().size(); ++id) {
        const Entry& entry = source.entry(id);
        if (entry.key.kind != MetaKeyKind::XmpProperty
            || (any(entry.flags, EntryFlags::Deleted)
                && !any(entry.flags, EntryFlags::Dirty)))
            continue;
        bool malformed   = false;
        const int member = sensitivity_member(
            arena_text(source.arena(), entry.key.data.xmp_property.schema_ns),
            arena_text(source.arena(),
                       entry.key.data.xmp_property.property_path),
            &malformed);
        if (member < 0)
            continue;
        result.failed_source_entry = id;
        if (malformed) {
            result.status = Status::UnsupportedSourceShape;
            return result;
        }
        CaptureSource& property = properties[static_cast<size_t>(member)];
        if (property.found) {
            result.status = Status::AmbiguousSource;
            return result;
        }
        property = { true, any(entry.flags, EntryFlags::Deleted), id,
                     &entry.value };
        ++result.source_properties;
    }

    constexpr std::array<NativeCaptureField, 7> fields
        = { NativeCaptureField::Iso,
            NativeCaptureField::SensitivityType,
            NativeCaptureField::StandardOutputSensitivity,
            NativeCaptureField::RecommendedExposureIndex,
            NativeCaptureField::ISOSpeed,
            NativeCaptureField::ISOSpeedLatitudeyyy,
            NativeCaptureField::ISOSpeedLatitudezzz };
    std::array<CapturePlannedGroup, 7> groups {};
    std::array<uint64_t, 7> values {};
    uint64_t text_bytes = 0U;
    bool any_present    = false;
    for (size_t i = 0U; i < groups.size(); ++i) {
        const CaptureSource& property = properties[i];
        CapturePlannedGroup& group    = groups[i];
        group.mapping      = MetadataCaptureTranslationMapping::XmpSensitivity;
        group.field        = fields[i];
        group.source_entry = property.entry_id;
        group.present      = property.found && !property.deleted;
        if (!group.present)
            continue;
        any_present                = true;
        result.failed_source_entry = property.entry_id;
        const MetaValue& value     = *property.value;
        if (value.kind == MetaValueKind::Text) {
            std::string_view text = arena_text(source.arena(), value.data.span);
            if (text.size() > options.max_text_bytes_per_property) {
                result.status = Status::ValueTooLong;
                return result;
            }
            text_bytes += text.size();
            if (text_bytes > options.max_total_text_bytes) {
                result.status = Status::SourceLimitExceeded;
                return result;
            }
            if (!text.empty() && text.front() == '+')
                text.remove_prefix(1U);
            result.status = numeric_status(parse_digits(text, &values[i]));
            if (result.status != Status::Ok)
                return result;
        } else if (value.count != 1U || !scalar_unsigned(value, &values[i])) {
            result.status = Status::InvalidSourceValue;
            return result;
        }
        const uint64_t maximum = i == 0U ? 65535U : (i == 1U ? 7U : UINT32_MAX);
        if ((i != 1U && values[i] == 0U) || values[i] > maximum) {
            result.status = Status::ValueOutOfRange;
            return result;
        }
        group.value = i < 2U ? make_u16(static_cast<uint16_t>(values[i]))
                             : make_u32(static_cast<uint32_t>(values[i]));
    }
    result.failed_source_entry = properties[0].found
                                     ? properties[0].entry_id
                                     : result.failed_source_entry;
    if (any_present) {
        if (!groups[0].present || !groups[1].present
            || ((groups[5].present || groups[6].present)
                && (!groups[4].present || !groups[5].present
                    || !groups[6].present))) {
            result.status = Status::IncompleteSource;
            return result;
        }
        constexpr std::array<uint8_t, 8> masks = { 0U, 1U, 2U, 4U,
                                                   3U, 5U, 6U, 7U };
        const uint8_t mask      = masks[static_cast<size_t>(values[1])];
        uint64_t selected_value = 0U;
        for (size_t i = 2U; i <= 4U; ++i) {
            if (!groups[i].present || (mask & (1U << (i - 2U))) == 0U)
                continue;
            const uint64_t bounded = values[i] >= 65535U ? 65535U : values[i];
            if (bounded != values[0]
                || (selected_value != 0U && selected_value != values[i])) {
                result.status              = Status::InvalidNumericValue;
                result.failed_source_entry = properties[i].entry_id;
                return result;
            }
            selected_value = values[i];
        }
    } else if (!properties[0].found || !properties[0].deleted) {
        result.status = Status::IncompleteSource;
        return result;
    }

    bool existing = false;
    bool exact    = true;
    for (auto& group : groups) {
        analyze_group(source, &group);
        existing = existing || group.existing_any;
        exact    = exact && group.exact_match;
    }
    if (existing && options.conflict_policy == Policy::FailOnConflict
        && !exact) {
        result.status = Status::NativeConflict;
        return result;
    }
    result.failed_mapping      = MetadataCaptureTranslationMapping::None;
    result.failed_source_entry = kInvalidEntryId;
    if ((existing && options.conflict_policy == Policy::PreserveExisting)
        || exact) {
        if (existing && options.conflict_policy == Policy::PreserveExisting)
            result.groups_preserved = 1U;
        else
            result.groups_unchanged = 1U;
        return apply_capture_groups(source, {}, Policy::ReplaceExisting,
                                    options.max_added_entries,
                                    options.max_operations, result, out_store);
    }
    result = apply_capture_groups(source, groups, Policy::ReplaceExisting,
                                  options.max_added_entries,
                                  options.max_operations, result, out_store);
    if (result.status == Status::Ok) {
        result.groups_translated = 1U;
        result.groups_unchanged  = 0U;
    }
    return result;
}

const char*
metadata_capture_translation_status_name(
    MetadataCaptureTranslationStatus status) noexcept
{
    switch (status) {
    case MetadataCaptureTranslationStatus::IncompleteSource:
        return "incomplete_source";
    case MetadataCaptureTranslationStatus::UnsupportedSourceShape:
        return "unsupported_source_shape";
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
    case MetadataCaptureTranslationMapping::XmpFocalLengthIn35mmFilm:
        return "xmp_focal_length_in_35mm_film";
    case MetadataCaptureTranslationMapping::XmpFileSource:
        return "xmp_file_source";
    case MetadataCaptureTranslationMapping::XmpSceneType:
        return "xmp_scene_type";
    case MetadataCaptureTranslationMapping::XmpTemperature:
        return "xmp_temperature";
    case MetadataCaptureTranslationMapping::XmpHumidity: return "xmp_humidity";
    case MetadataCaptureTranslationMapping::XmpPressure: return "xmp_pressure";
    case MetadataCaptureTranslationMapping::XmpWaterDepth:
        return "xmp_water_depth";
    case MetadataCaptureTranslationMapping::XmpAcceleration:
        return "xmp_acceleration";
    case MetadataCaptureTranslationMapping::XmpCameraElevationAngle:
        return "xmp_camera_elevation_angle";
    case MetadataCaptureTranslationMapping::XmpFocalPlaneResolution:
        return "xmp_focal_plane_resolution";
    case MetadataCaptureTranslationMapping::XmpSubjectArea:
        return "xmp_subject_area";
    case MetadataCaptureTranslationMapping::XmpSubjectLocation:
        return "xmp_subject_location";
    case MetadataCaptureTranslationMapping::XmpShutterSpeedValue:
        return "xmp_shutter_speed_value";
    case MetadataCaptureTranslationMapping::XmpApertureValue:
        return "xmp_aperture_value";
    case MetadataCaptureTranslationMapping::XmpBrightnessValue:
        return "xmp_brightness_value";
    case MetadataCaptureTranslationMapping::XmpMaxApertureValue:
        return "xmp_max_aperture_value";
    case MetadataCaptureTranslationMapping::XmpLensSpecification:
        return "xmp_lens_specification";
    case MetadataCaptureTranslationMapping::XmpImageUniqueID:
        return "xmp_image_unique_id";
    case MetadataCaptureTranslationMapping::XmpSensitivity:
        return "xmp_sensitivity";
    case MetadataCaptureTranslationMapping::XmpFlash: return "xmp_flash";
    case MetadataCaptureTranslationMapping::XmpLightSource:
        return "xmp_light_source";
    case MetadataCaptureTranslationMapping::XmpSubjectDistance:
        return "xmp_subject_distance";
    case MetadataCaptureTranslationMapping::XmpDigitalZoomRatio:
        return "xmp_digital_zoom_ratio";
    case MetadataCaptureTranslationMapping::XmpExposureIndex:
        return "xmp_exposure_index";
    case MetadataCaptureTranslationMapping::XmpFlashEnergy:
        return "xmp_flash_energy";
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
