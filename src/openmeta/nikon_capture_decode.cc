// SPDX-License-Identifier: Apache-2.0

#include "nikon_capture_decode_internal.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <string_view>

namespace openmeta::exif_internal {
namespace {

    constexpr uint32_t kNikonCaptureSignature    = 0x7a86a940U;
    constexpr uint32_t kHistogramXmlRecordId     = 0x083a1a25U;
    constexpr uint32_t kMaxCaptureRecords        = 256U;
    constexpr std::string_view kCaptureIfdPrefix = "mk_nikon_capture_";

    enum class FieldType : uint8_t {
        U8,
        I8,
        U16,
        I16,
        U32,
        I32,
        F64,
        FixedText16,
        NullTerminatedText,
    };

    struct CaptureField final {
        uint16_t offset    = 0U;
        uint8_t name_index = 0U;
        FieldType type     = FieldType::U8;
    };

    struct CaptureTable final {
        uint32_t record_id         = 0U;
        const CaptureField* fields = nullptr;
        size_t field_count         = 0U;
        uint8_t key_unit_bytes     = 1U;
    };

    struct CaptureRecord final {
        uint32_t id           = 0U;
        size_t payload_offset = 0U;
        size_t payload_size   = 0U;
        size_t output_entries = 0U;
    };

    static constexpr CaptureField kUnsharpFields[] = {
        { 0x0000U, 14U, FieldType::U8 },  { 0x0013U, 82U, FieldType::U16 },
        { 0x0017U, 83U, FieldType::U16 }, { 0x0019U, 84U, FieldType::U16 },
        { 0x001bU, 85U, FieldType::U8 },  { 0x002eU, 86U, FieldType::U16 },
        { 0x0032U, 87U, FieldType::U16 }, { 0x0034U, 88U, FieldType::U16 },
        { 0x0036U, 89U, FieldType::U8 },  { 0x0049U, 90U, FieldType::U16 },
        { 0x004dU, 91U, FieldType::U16 }, { 0x004fU, 92U, FieldType::U16 },
        { 0x0051U, 93U, FieldType::U8 },  { 0x0064U, 94U, FieldType::U16 },
        { 0x0068U, 95U, FieldType::U16 }, { 0x006aU, 96U, FieldType::U16 },
        { 0x006cU, 97U, FieldType::U8 },
    };
    static constexpr CaptureField kCropFields[] = {
        { 0x001eU, 1U, FieldType::F64 },  { 0x0026U, 2U, FieldType::F64 },
        { 0x002eU, 3U, FieldType::F64 },  { 0x0036U, 4U, FieldType::F64 },
        { 0x008eU, 5U, FieldType::F64 },  { 0x0096U, 6U, FieldType::F64 },
        { 0x009eU, 7U, FieldType::F64 },  { 0x00aeU, 8U, FieldType::F64 },
        { 0x00b6U, 9U, FieldType::F64 },  { 0x00beU, 10U, FieldType::F64 },
        { 0x00c6U, 11U, FieldType::F64 }, { 0x00ceU, 12U, FieldType::F64 },
        { 0x00d6U, 13U, FieldType::F64 },
    };
    static constexpr CaptureField kExposureFields[] = {
        { 0x0000U, 15U, FieldType::I16 },
        { 0x0012U, 16U, FieldType::F64 },
        { 0x0024U, 17U, FieldType::U8 },
        { 0x0025U, 18U, FieldType::U8 },
    };
    static constexpr CaptureField kWhiteBalanceFields[] = {
        { 0x0000U, 19U, FieldType::F64 }, { 0x0008U, 20U, FieldType::F64 },
        { 0x0010U, 21U, FieldType::U8 },  { 0x0014U, 22U, FieldType::U16 },
        { 0x0018U, 23U, FieldType::U16 }, { 0x0025U, 24U, FieldType::I32 },
    };
    static constexpr CaptureField kNoiseReductionFields[] = {
        { 0x0004U, 25U, FieldType::U8 },  { 0x0005U, 26U, FieldType::U8 },
        { 0x0009U, 27U, FieldType::U32 }, { 0x000dU, 28U, FieldType::U32 },
        { 0x0011U, 29U, FieldType::U16 }, { 0x0015U, 30U, FieldType::U8 },
        { 0x0017U, 31U, FieldType::U8 },  { 0x0018U, 32U, FieldType::U32 },
        { 0x001cU, 33U, FieldType::U32 },
    };
    static constexpr CaptureField kColorBoostFields[] = {
        { 0x0000U, 34U, FieldType::U8 },
        { 0x0001U, 35U, FieldType::U32 },
    };
    static constexpr CaptureField kBrightnessFields[] = {
        { 0x0000U, 36U, FieldType::F64 },
        { 0x0008U, 37U, FieldType::U8 },
    };
    static constexpr CaptureField kPhotoEffectsFields[] = {
        { 0x0000U, 38U, FieldType::U8 },
        { 0x0004U, 39U, FieldType::I16 },
        { 0x0006U, 40U, FieldType::I16 },
        { 0x0008U, 41U, FieldType::I16 },
    };
    static constexpr CaptureField kPictureControlFields[] = {
        { 0x0000U, 42U, FieldType::U8 },
        { 0x0013U, 43U, FieldType::FixedText16 },
        { 0x002aU, 44U, FieldType::U8 },
        { 0x002bU, 45U, FieldType::U8 },
        { 0x002cU, 46U, FieldType::U8 },
        { 0x002dU, 47U, FieldType::U8 },
        { 0x002eU, 48U, FieldType::U8 },
        { 0x002fU, 49U, FieldType::U8 },
    };
    static constexpr CaptureField kRedEyeFields[] = {
        { 0x0000U, 50U, FieldType::U8 },
    };
    static constexpr CaptureField kDLightingHsFields[] = {
        { 0x0000U, 51U, FieldType::U32 },
        { 0x0004U, 52U, FieldType::U32 },
    };
    static constexpr CaptureField kDLightingHqFields[] = {
        { 0x0000U, 53U, FieldType::U32 },
        { 0x0004U, 54U, FieldType::U32 },
        { 0x0008U, 55U, FieldType::U32 },
    };
    static constexpr CaptureField kHighlightFields[] = {
        { 0x0000U, 56U, FieldType::I8 },
        { 0x0001U, 57U, FieldType::I8 },
        { 0x0006U, 58U, FieldType::I8 },
    };
    static constexpr CaptureField kMainScalarFields[] = {
        { 0U, 59U, FieldType::U8 },
        { 0U, 60U, FieldType::U8 },
        { 0U, 61U, FieldType::U8 },
        { 0U, 62U, FieldType::F64 },
        { 0U, 63U, FieldType::NullTerminatedText },
        { 0U, 64U, FieldType::U8 },
        { 0U, 65U, FieldType::U8 },
        { 0U, 66U, FieldType::U8 },
        { 0U, 67U, FieldType::U8 },
        { 0U, 68U, FieldType::U8 },
        { 0U, 69U, FieldType::U8 },
        { 0U, 70U, FieldType::U8 },
        { 0U, 71U, FieldType::U8 },
        { 0U, 72U, FieldType::U8 },
        { 0U, 73U, FieldType::U8 },
        { 0U, 74U, FieldType::U8 },
        { 0U, 75U, FieldType::U16 },
        { 0U, 76U, FieldType::U8 },
        { 0U, 77U, FieldType::I16 },
        { 0U, 78U, FieldType::U8 },
        { 0U, 79U, FieldType::U8 },
        { 0U, 80U, FieldType::U8 },
        { 0U, 81U, FieldType::U8 },
    };

    static constexpr CaptureTable kCaptureTables[] = {
        { 0xe42b5161U, kUnsharpFields, std::size(kUnsharpFields) },
        { 0x374233e0U, kCropFields, std::size(kCropFields) },
        { 0x56a54260U, kExposureFields, std::size(kExposureFields) },
        { 0xbf3c6c20U, kWhiteBalanceFields, std::size(kWhiteBalanceFields) },
        { 0x926f13e0U, kNoiseReductionFields, std::size(kNoiseReductionFields) },
        { 0xb999a36fU, kColorBoostFields, std::size(kColorBoostFields) },
        { 0x84589434U, kBrightnessFields, std::size(kBrightnessFields) },
        { 0xb0384e1eU, kPhotoEffectsFields, std::size(kPhotoEffectsFields) },
        { 0x39c456acU, kPictureControlFields, std::size(kPictureControlFields) },
        { 0x3cfc73c6U, kRedEyeFields, std::size(kRedEyeFields) },
        { 0xe37b4337U, kDLightingHsFields, std::size(kDLightingHsFields), 4U },
        { 0x890ff591U, kDLightingHqFields, std::size(kDLightingHqFields), 4U },
        { 0x116fea21U, kHighlightFields, std::size(kHighlightFields) },
        { 0x008ae85eU, &kMainScalarFields[0], 1U },
        { 0x0c89224bU, &kMainScalarFields[1], 1U },
        { 0x2175eb78U, &kMainScalarFields[2], 1U },
        { 0x2fc08431U, &kMainScalarFields[3], 1U },
        { 0x3d136244U, &kMainScalarFields[4], 1U },
        { 0x416391c6U, &kMainScalarFields[5], 1U },
        { 0x5f0e7d23U, &kMainScalarFields[6], 1U },
        { 0x6a6e36b6U, &kMainScalarFields[7], 1U },
        { 0x753dcbc0U, &kMainScalarFields[8], 1U },
        { 0x76a43200U, &kMainScalarFields[9], 1U },
        { 0x76a43201U, &kMainScalarFields[10], 1U },
        { 0x76a43202U, &kMainScalarFields[11], 1U },
        { 0x76a43203U, &kMainScalarFields[12], 1U },
        { 0x76a43204U, &kMainScalarFields[13], 1U },
        { 0x76a43205U, &kMainScalarFields[14], 1U },
        { 0x76a43206U, &kMainScalarFields[15], 1U },
        { 0x76a43207U, &kMainScalarFields[16], 1U },
        { 0xab5eca5eU, &kMainScalarFields[17], 1U },
        { 0xac6bd5c0U, &kMainScalarFields[18], 1U },
        { 0xce5554aaU, &kMainScalarFields[19], 1U },
        { 0xe2173c47U, &kMainScalarFields[20], 1U },
        { 0xfe28a44fU, &kMainScalarFields[21], 1U },
        { 0xfe443a45U, &kMainScalarFields[22], 1U },
    };

    static constexpr std::string_view kFieldNames[] = {
        {},
        "CropLeft",
        "CropTop",
        "CropRight",
        "CropBottom",
        "CropOutputWidthInches",
        "CropOutputHeightInches",
        "CropScaledResolution",
        "CropSourceResolution",
        "CropOutputResolution",
        "CropOutputScale",
        "CropOutputWidth",
        "CropOutputHeight",
        "CropOutputPixels",
        "UnsharpCount",
        "ExposureAdj",
        "ExposureAdj2",
        "ActiveD-Lighting",
        "ActiveD-LightingMode",
        "WBAdjRedBalance",
        "WBAdjBlueBalance",
        "WBAdjMode",
        "WBAdjLighting",
        "WBAdjTemperature",
        "WBAdjTint",
        "EdgeNoiseReduction",
        "ColorMoireReductionMode",
        "NoiseReductionIntensity",
        "NoiseReductionSharpness",
        "NoiseReductionMethod",
        "ColorMoireReduction",
        "NoiseReduction",
        "ColorNoiseReductionIntensity",
        "ColorNoiseReductionSharpness",
        "ColorBoostType",
        "ColorBoostLevel",
        "BrightnessAdj",
        "EnhanceDarkTones",
        "PhotoEffectsType",
        "PhotoEffectsRed",
        "PhotoEffectsGreen",
        "PhotoEffectsBlue",
        "PictureControlActive",
        "PictureControlMode",
        "QuickAdjust",
        "SharpeningAdj",
        "ContrastAdj",
        "BrightnessAdj",
        "SaturationAdj",
        "HueAdj",
        "RedEyeCorrection",
        "D-LightingHSAdjustment",
        "D-LightingHSColorBoost",
        "D-LightingHQShadow",
        "D-LightingHQHighlight",
        "D-LightingHQColorBoost",
        "ShadowProtection",
        "SaturationAdj",
        "HighlightProtection",
        "LCHEditor",
        "ColorAberrationControl",
        "D-LightingHQ",
        "StraightenAngle",
        "EditVersionName",
        "QuickFix",
        "ColorBooster",
        "D-LightingHQSelected",
        "NoiseReduction",
        "UnsharpMask",
        "Curves",
        "ColorBalanceAdj",
        "AdvancedRaw",
        "WhiteBalanceAdj",
        "VignetteControl",
        "FlipHorizontal",
        "Rotation",
        "PhotoEffects",
        "VignetteControlIntensity",
        "D-LightingHS",
        "PictureControl",
        "AutoRedEye",
        "ImageDustOff",
        "Unsharp1Color",
        "Unsharp1Intensity",
        "Unsharp1HaloWidth",
        "Unsharp1Threshold",
        "Unsharp2Color",
        "Unsharp2Intensity",
        "Unsharp2HaloWidth",
        "Unsharp2Threshold",
        "Unsharp3Color",
        "Unsharp3Intensity",
        "Unsharp3HaloWidth",
        "Unsharp3Threshold",
        "Unsharp4Color",
        "Unsharp4Intensity",
        "Unsharp4HaloWidth",
        "Unsharp4Threshold",
    };

    static uint8_t u8(std::byte value) noexcept
    {
        return static_cast<uint8_t>(value);
    }

    static bool read_u16le(std::span<const std::byte> bytes, size_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset > bytes.size() || bytes.size() - offset < 2U) {
            return false;
        }
        *out = static_cast<uint16_t>(
            uint32_t(u8(bytes[offset]))
            | (uint32_t(u8(bytes[offset + 1U])) << 8U));
        return true;
    }

    static bool read_u32le(std::span<const std::byte> bytes, size_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset > bytes.size() || bytes.size() - offset < 4U) {
            return false;
        }
        *out = uint32_t(u8(bytes[offset]))
               | (uint32_t(u8(bytes[offset + 1U])) << 8U)
               | (uint32_t(u8(bytes[offset + 2U])) << 16U)
               | (uint32_t(u8(bytes[offset + 3U])) << 24U);
        return true;
    }

    static bool read_u64le(std::span<const std::byte> bytes, size_t offset,
                           uint64_t* out) noexcept
    {
        if (!out || offset > bytes.size() || bytes.size() - offset < 8U) {
            return false;
        }
        uint64_t value = 0U;
        for (uint32_t i = 0U; i < 8U; ++i) {
            value |= uint64_t(u8(bytes[offset + i])) << (i * 8U);
        }
        *out = value;
        return true;
    }

    static bool has_range(std::span<const std::byte> bytes, size_t offset,
                          size_t count) noexcept
    {
        return offset <= bytes.size() && count <= bytes.size() - offset;
    }

    static size_t field_width(FieldType type) noexcept
    {
        switch (type) {
        case FieldType::U8:
        case FieldType::I8: return 1U;
        case FieldType::U16:
        case FieldType::I16: return 2U;
        case FieldType::U32:
        case FieldType::I32: return 4U;
        case FieldType::F64: return 8U;
        case FieldType::FixedText16: return 16U;
        case FieldType::NullTerminatedText: return 1U;
        }
        return 0U;
    }

    static const CaptureTable* capture_table(uint32_t id) noexcept
    {
        for (const CaptureTable& table : kCaptureTables) {
            if (table.record_id == id) {
                return &table;
            }
        }
        return nullptr;
    }

    static size_t text_length(std::span<const std::byte> payload,
                              const CaptureField& field) noexcept
    {
        if (field.type == FieldType::FixedText16) {
            return 16U;
        }
        if (field.type != FieldType::NullTerminatedText
            || !has_range(payload, field.offset, 1U)) {
            return 0U;
        }
        for (size_t i = field.offset; i < payload.size(); ++i) {
            if (payload[i] == std::byte { 0U }) {
                return i - field.offset;
            }
        }
        return payload.size() - field.offset;
    }

    static size_t table_field_count(const CaptureTable* table,
                                    std::span<const std::byte> payload) noexcept
    {
        if (!table) {
            return 0U;
        }
        size_t count = 0U;
        for (size_t i = 0U; i < table->field_count; ++i) {
            const CaptureField& field = table->fields[i];
            if (has_range(payload, field.offset, field_width(field.type))) {
                ++count;
            }
        }
        return count;
    }

    static bool add_arena_size(uint64_t addition, uint64_t* total) noexcept
    {
        if (!total || addition > UINT64_MAX - *total) {
            return false;
        }
        *total += addition;
        return true;
    }

    static uint64_t
    record_arena_bytes(uint32_t id, std::span<const std::byte> payload) noexcept
    {
        const uint64_t token_bytes = kCaptureIfdPrefix.size() + 8U;
        const CaptureTable* table  = capture_table(id);
        const size_t field_count   = table_field_count(table, payload);
        if (field_count == 0U) {
            const uint64_t raw_size = static_cast<uint64_t>(payload.size());
            return raw_size > UINT64_MAX - token_bytes ? UINT64_MAX
                                                       : token_bytes + raw_size;
        }
        uint64_t required = static_cast<uint64_t>(field_count) * token_bytes;
        for (size_t i = 0U; i < table->field_count; ++i) {
            const CaptureField& field = table->fields[i];
            if ((field.type == FieldType::FixedText16
                 || field.type == FieldType::NullTerminatedText)
                && has_range(payload, field.offset, field_width(field.type))
                && !add_arena_size(text_length(payload, field), &required)) {
                return UINT64_MAX;
            }
        }
        return required;
    }

    static std::string_view record_ifd_token(uint32_t id,
                                             std::span<char> scratch) noexcept
    {
        constexpr char kHex[] = "0123456789abcdef";
        if (scratch.size() < kCaptureIfdPrefix.size() + 8U) {
            return {};
        }
        std::memcpy(scratch.data(), kCaptureIfdPrefix.data(),
                    kCaptureIfdPrefix.size());
        for (uint32_t i = 0U; i < 8U; ++i) {
            const uint32_t shift                  = (7U - i) * 4U;
            scratch[kCaptureIfdPrefix.size() + i] = kHex[(id >> shift) & 0x0fU];
        }
        return std::string_view(scratch.data(), kCaptureIfdPrefix.size() + 8U);
    }

    static void set_status(ExifDecodeResult* result,
                           ExifDecodeStatus status) noexcept
    {
        if (!result || result->status == ExifDecodeStatus::LimitExceeded) {
            return;
        }
        if (status == ExifDecodeStatus::LimitExceeded
            || (status == ExifDecodeStatus::Malformed
                && result->status != ExifDecodeStatus::Malformed)
            || (status == ExifDecodeStatus::OutputTruncated
                && result->status != ExifDecodeStatus::Malformed)) {
            result->status = status;
        }
    }

    static bool next_record(std::span<const std::byte> root, size_t* cursor,
                            CaptureRecord* record) noexcept
    {
        if (!cursor || !record || *cursor > root.size()
            || root.size() - *cursor < 22U) {
            return false;
        }
        const size_t start     = *cursor;
        uint32_t declared_size = 0U;
        if (!read_u32le(root, start, &record->id)
            || !read_u32le(root, start + 18U, &declared_size)
            || declared_size < 4U) {
            return false;
        }
        const size_t payload_size   = static_cast<size_t>(declared_size - 4U);
        const size_t payload_offset = start + 22U;
        if (payload_size > root.size() - payload_offset) {
            return false;
        }
        record->payload_offset = payload_offset;
        record->payload_size   = payload_size;
        record->output_entries = 0U;
        *cursor                = payload_offset + payload_size;
        return true;
    }

    static bool read_capture_field(const CaptureField& field,
                                   std::span<const std::byte> payload,
                                   MetaStore& store, Entry* entry) noexcept
    {
        if (!entry
            || !has_range(payload, field.offset, field_width(field.type))) {
            return false;
        }
        entry->origin.wire_type  = WireType { WireFamily::Other, 0U };
        entry->origin.wire_count = 1U;
        entry->origin.name_context_kind
            = EntryNameContextKind::NikonCaptureField;
        entry->origin.name_context_variant = field.name_index;
        entry->flags = EntryFlags::Derived | EntryFlags::ContextualName;
        switch (field.type) {
        case FieldType::U8:
            entry->value = make_u8(u8(payload[field.offset]));
            return true;
        case FieldType::I8: {
            const uint8_t value        = u8(payload[field.offset]);
            const int16_t signed_value = value < 0x80U
                                             ? static_cast<int16_t>(value)
                                             : static_cast<int16_t>(value)
                                                   - 0x100;
            entry->value = make_i8(static_cast<int8_t>(signed_value));
            return true;
        }
        case FieldType::U16: {
            uint16_t value = 0U;
            if (!read_u16le(payload, field.offset, &value)) {
                return false;
            }
            entry->value = make_u16(value);
            return true;
        }
        case FieldType::I16: {
            uint16_t value = 0U;
            if (!read_u16le(payload, field.offset, &value)) {
                return false;
            }
            const int32_t signed_value = value < 0x8000U
                                             ? static_cast<int32_t>(value)
                                             : static_cast<int32_t>(value)
                                                   - 0x10000;
            entry->value = make_i16(static_cast<int16_t>(signed_value));
            return true;
        }
        case FieldType::U32: {
            uint32_t value = 0U;
            if (!read_u32le(payload, field.offset, &value)) {
                return false;
            }
            entry->value = make_u32(value);
            return true;
        }
        case FieldType::I32: {
            uint32_t value = 0U;
            if (!read_u32le(payload, field.offset, &value)) {
                return false;
            }
            const int64_t signed_value = value < 0x80000000U
                                             ? static_cast<int64_t>(value)
                                             : static_cast<int64_t>(value)
                                                   - 0x100000000LL;
            entry->value = make_i32(static_cast<int32_t>(signed_value));
            return true;
        }
        case FieldType::F64: {
            uint64_t bits = 0U;
            if (!read_u64le(payload, field.offset, &bits)) {
                return false;
            }
            entry->value = make_f64_bits(bits);
            return true;
        }
        case FieldType::FixedText16:
        case FieldType::NullTerminatedText: {
            const size_t length = text_length(payload, field);
            std::string_view text;
            if (length != 0U) {
                const char* data = reinterpret_cast<const char*>(
                    payload.data() + field.offset);
                text = std::string_view(data, length);
            }
            entry->value = make_text(store.arena(), text, TextEncoding::Ascii);
            return !store.arena().limit_exceeded()
                   && entry->value.kind == MetaValueKind::Text;
        }
        }
        return false;
    }

    static void emit_raw_record(const CaptureRecord& record,
                                std::span<const std::byte> payload,
                                MetaStore& store, BlockId block,
                                ExifDecodeResult* status_out) noexcept
    {
        char token_buf[kCaptureIfdPrefix.size() + 8U];
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(),
                                      record_ifd_token(record.id, token_buf),
                                      0U);
        if (store.arena().limit_exceeded()) {
            set_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        entry.origin.block          = block;
        entry.origin.order_in_block = 0U;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0U };
        entry.origin.wire_count     = static_cast<uint32_t>(payload.size());
        entry.flags                 = EntryFlags::Derived;
        entry.value                 = make_bytes(store.arena(), payload);
        if (entry.value.kind != MetaValueKind::Bytes
            || store.arena().limit_exceeded()
            || store.add_entry(entry) == kInvalidEntryId) {
            set_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        if (status_out) {
            ++status_out->entries_decoded;
        }
    }

    static void emit_typed_record(const CaptureRecord& record,
                                  std::span<const std::byte> payload,
                                  const CaptureTable& table, MetaStore& store,
                                  BlockId block,
                                  ExifDecodeResult* status_out) noexcept
    {
        char token_buf[kCaptureIfdPrefix.size() + 8U];
        const std::string_view ifd
            = record_ifd_token(record.id, std::span<char>(token_buf));
        uint32_t order = 0U;
        for (size_t i = 0U; i < table.field_count; ++i) {
            const CaptureField& field = table.fields[i];
            if (!has_range(payload, field.offset, field_width(field.type))) {
                continue;
            }
            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd,
                                          field.offset / table.key_unit_bytes);
            if (store.arena().limit_exceeded()) {
                set_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }
            entry.origin.block          = block;
            entry.origin.order_in_block = order;
            if (!read_capture_field(field, payload, store, &entry)
                || store.arena().limit_exceeded()
                || store.add_entry(entry) == kInvalidEntryId) {
                set_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }
            ++order;
            if (status_out) {
                ++status_out->entries_decoded;
            }
        }
    }

}  // namespace

std::string_view
nikon_capture_field_name(uint8_t index) noexcept
{
    return index < std::size(kFieldNames) ? kFieldNames[index]
                                          : std::string_view {};
}

void
decode_nikon_capture(std::span<const std::byte> stable_payload,
                     MetaStore& store, const ExifDecodeOptions& options,
                     ExifDecodeResult* status_out) noexcept
{
    if (options.limits.max_ifds == 0U) {
        set_status(status_out, ExifDecodeStatus::LimitExceeded);
        return;
    }
    if (stable_payload.size() < 22U) {
        set_status(status_out, ExifDecodeStatus::Malformed);
        return;
    }
    if (stable_payload.size() > options.limits.max_value_bytes
        || stable_payload.size() > UINT32_MAX) {
        set_status(status_out, ExifDecodeStatus::LimitExceeded);
        return;
    }
    uint32_t signature     = 0U;
    uint32_t declared_size = 0U;
    if (!read_u32le(stable_payload, 0U, &signature)
        || signature != kNikonCaptureSignature
        || !read_u32le(stable_payload, 18U, &declared_size)
        || declared_size < 4U) {
        set_status(status_out, ExifDecodeStatus::Malformed);
        return;
    }
    // Most files count the first 18 fixed header bytes outside the size
    // field. ViewNX 2.1.1 instead stores the full tag length in that field.
    const uint64_t metadata_end_wide = declared_size == stable_payload.size()
                                           ? static_cast<uint64_t>(
                                                 declared_size)
                                           : 18ULL + declared_size;
    if (metadata_end_wide < 22ULL
        || metadata_end_wide > stable_payload.size()) {
        set_status(status_out, ExifDecodeStatus::Malformed);
        return;
    }
    const std::span<const std::byte> metadata = stable_payload.first(
        static_cast<size_t>(metadata_end_wide));

    CaptureRecord records[kMaxCaptureRecords] {};
    size_t record_count = 0U;
    size_t cursor       = 22U;
    while (cursor <= metadata.size() && metadata.size() - cursor >= 22U) {
        if (record_count >= kMaxCaptureRecords) {
            set_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        if (!next_record(metadata, &cursor, &records[record_count])) {
            set_status(status_out, ExifDecodeStatus::Malformed);
            return;
        }
        if (records[record_count].payload_size > options.limits.max_value_bytes
            || records[record_count].payload_size > UINT32_MAX) {
            set_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        ++record_count;
    }

    // ExifTool's HistogramXML table includes Nikon's four-byte size fixup.
    // In the D60 witness, those bytes are the final 4 bytes of declared
    // metadata after the last complete record, before the external 429-byte
    // tag padding. Do not consume other record tails or bytes outside metadata.
    if (record_count != 0U && metadata.size() - cursor == 4U
        && records[record_count - 1U].id == kHistogramXmlRecordId) {
        records[record_count - 1U].payload_size += 4U;
        if (records[record_count - 1U].payload_size
                > options.limits.max_value_bytes
            || records[record_count - 1U].payload_size > UINT32_MAX) {
            set_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        cursor += 4U;
    }

    uint64_t output_entries = 0U;
    uint64_t arena_addition = 0U;
    for (size_t i = 0U; i < record_count; ++i) {
        CaptureRecord& record = records[i];
        const std::span<const std::byte> payload
            = metadata.subspan(record.payload_offset, record.payload_size);
        const CaptureTable* table = capture_table(record.id);
        const size_t typed_count  = table_field_count(table, payload);
        record.output_entries     = typed_count != 0U ? typed_count : 1U;
        if (record.output_entries > options.limits.max_entries_per_ifd) {
            set_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        if (record.output_entries > UINT64_MAX - output_entries) {
            set_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        output_entries += record.output_entries;
        const uint64_t record_bytes = record_arena_bytes(record.id, payload);
        if (!add_arena_size(record_bytes, &arena_addition)) {
            set_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
    }

    const size_t current_entries     = store.entries().size();
    const size_t current_arena_bytes = store.arena().bytes().size();
    const uint32_t current_blocks    = store.block_count();
    if (current_entries > options.limits.max_total_entries
        || output_entries > options.limits.max_total_entries - current_entries
        || current_arena_bytes > options.limits.max_arena_bytes
        || arena_addition > options.limits.max_arena_bytes - current_arena_bytes
        || arena_addition
               > static_cast<uint64_t>(SIZE_MAX - current_arena_bytes)
        || record_count > options.limits.max_ifds
        || record_count > UINT32_MAX - current_blocks
        || current_blocks > options.limits.max_ifds
        || record_count > options.limits.max_ifds - current_blocks
        || output_entries > UINT32_MAX - current_entries) {
        set_status(status_out, ExifDecodeStatus::LimitExceeded);
        return;
    }
    if (record_count == 0U) {
        return;
    }
    store.reserve(current_blocks + static_cast<uint32_t>(record_count),
                  static_cast<uint32_t>(current_entries + output_entries),
                  current_arena_bytes + static_cast<size_t>(arena_addition));
    if (store.resource_limit_exceeded()) {
        set_status(status_out, ExifDecodeStatus::LimitExceeded);
        return;
    }

    for (size_t i = 0U; i < record_count; ++i) {
        const CaptureRecord& record = records[i];
        const std::span<const std::byte> payload
            = metadata.subspan(record.payload_offset, record.payload_size);
        const CaptureTable* table = capture_table(record.id);
        const bool has_fields     = table_field_count(table, payload) != 0U;
        const BlockId block = store.add_block(BlockInfo { 0U, 0U, record.id });
        if (block == kInvalidBlockId) {
            set_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        if (has_fields) {
            emit_typed_record(record, payload, *table, store, block,
                              status_out);
        } else {
            emit_raw_record(record, payload, store, block, status_out);
        }
        if (status_out
            && status_out->status == ExifDecodeStatus::LimitExceeded) {
            return;
        }
    }
}

}  // namespace openmeta::exif_internal
