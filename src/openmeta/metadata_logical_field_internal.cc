// SPDX-License-Identifier: Apache-2.0

#include "metadata_logical_field_internal.h"

#include <charconv>
#include <cstdint>

namespace openmeta::detail {
namespace {

    static constexpr std::string_view kXmpNsDc
        = "http://purl.org/dc/elements/1.1/";
    static constexpr std::string_view kXmpNsExif
        = "http://ns.adobe.com/exif/1.0/";
    static constexpr std::string_view kXmpNsPhotoshop
        = "http://ns.adobe.com/photoshop/1.0/";
    static constexpr std::string_view kXmpNsTiff
        = "http://ns.adobe.com/tiff/1.0/";
    static constexpr std::string_view kXmpNsXmp = "http://ns.adobe.com/xap/1.0/";
    static constexpr std::string_view kXmpNsXmpRights
        = "http://ns.adobe.com/xap/1.0/rights/";

    static bool xml_code_point_is_valid(uint32_t cp) noexcept
    {
        return cp == 0x09U || cp == 0x0aU || cp == 0x0dU
               || (cp >= 0x20U && cp <= 0xd7ffU)
               || (cp >= 0xe000U && cp <= 0xfffdU)
               || (cp >= 0x10000U && cp <= 0x10ffffU);
    }

    static bool continuation(uint8_t byte) noexcept
    {
        return (byte & 0xc0U) == 0x80U;
    }

}  // namespace

bool
metadata_logical_field_descriptor(MetadataCreationFieldKind kind,
                                  MetadataLogicalFieldDescriptor* out) noexcept
{
    if (!out) {
        return false;
    }

    switch (kind) {
    case MetadataCreationFieldKind::Title:
        *out = { kXmpNsDc, "title[@xml:lang=x-default]",
                 MetadataCreationValueKind::Text, false };
        return true;
    case MetadataCreationFieldKind::Description:
        *out = { kXmpNsDc, "description[@xml:lang=x-default]",
                 MetadataCreationValueKind::Text, false };
        return true;
    case MetadataCreationFieldKind::Creator:
        *out = { kXmpNsDc, "creator", MetadataCreationValueKind::Text, true };
        return true;
    case MetadataCreationFieldKind::Keyword:
        *out = { kXmpNsDc, "subject", MetadataCreationValueKind::Text, true };
        return true;
    case MetadataCreationFieldKind::Copyright:
        *out = { kXmpNsDc, "rights[@xml:lang=x-default]",
                 MetadataCreationValueKind::Text, false };
        return true;
    case MetadataCreationFieldKind::RightsUsageTerms:
        *out = { kXmpNsXmpRights, "UsageTerms[@xml:lang=x-default]",
                 MetadataCreationValueKind::Text, false };
        return true;
    case MetadataCreationFieldKind::Credit:
        *out = { kXmpNsPhotoshop, "Credit", MetadataCreationValueKind::Text,
                 false };
        return true;
    case MetadataCreationFieldKind::Source:
        *out = { kXmpNsPhotoshop, "Source", MetadataCreationValueKind::Text,
                 false };
        return true;
    case MetadataCreationFieldKind::CreateDate:
        *out = { kXmpNsXmp, "CreateDate", MetadataCreationValueKind::Text,
                 false };
        return true;
    case MetadataCreationFieldKind::ModifyDate:
        *out = { kXmpNsXmp, "ModifyDate", MetadataCreationValueKind::Text,
                 false };
        return true;
    case MetadataCreationFieldKind::Rating:
        *out = { kXmpNsXmp, "Rating", MetadataCreationValueKind::SignedInteger,
                 false };
        return true;
    case MetadataCreationFieldKind::Label:
        *out = { kXmpNsXmp, "Label", MetadataCreationValueKind::Text, false };
        return true;
    case MetadataCreationFieldKind::CameraMake:
        *out = { kXmpNsTiff, "Make", MetadataCreationValueKind::Text, false };
        return true;
    case MetadataCreationFieldKind::CameraModel:
        *out = { kXmpNsTiff, "Model", MetadataCreationValueKind::Text, false };
        return true;
    case MetadataCreationFieldKind::Software:
        *out = { kXmpNsXmp, "CreatorTool", MetadataCreationValueKind::Text,
                 false };
        return true;
    case MetadataCreationFieldKind::DateTimeOriginal:
        *out = { kXmpNsExif, "DateTimeOriginal",
                 MetadataCreationValueKind::Text, false };
        return true;
    case MetadataCreationFieldKind::Orientation:
        *out = { kXmpNsTiff, "Orientation",
                 MetadataCreationValueKind::UnsignedInteger, false };
        return true;
    case MetadataCreationFieldKind::PixelWidth:
        *out = { kXmpNsExif, "ExifImageWidth",
                 MetadataCreationValueKind::UnsignedInteger, false };
        return true;
    case MetadataCreationFieldKind::PixelHeight:
        *out = { kXmpNsExif, "ExifImageHeight",
                 MetadataCreationValueKind::UnsignedInteger, false };
        return true;
    case MetadataCreationFieldKind::ColorSpace:
        *out = { kXmpNsExif, "ColorSpace",
                 MetadataCreationValueKind::UnsignedInteger, false };
        return true;
    case MetadataCreationFieldKind::ExposureTime:
        *out = { kXmpNsExif, "ExposureTime",
                 MetadataCreationValueKind::UnsignedRational, false };
        return true;
    case MetadataCreationFieldKind::FNumber:
        *out = { kXmpNsExif, "FNumber",
                 MetadataCreationValueKind::UnsignedRational, false };
        return true;
    case MetadataCreationFieldKind::IsoSensitivity:
        *out = { kXmpNsExif, "ISO", MetadataCreationValueKind::UnsignedInteger,
                 false };
        return true;
    case MetadataCreationFieldKind::FocalLength:
        *out = { kXmpNsExif, "FocalLength",
                 MetadataCreationValueKind::UnsignedRational, false };
        return true;
    }
    return false;
}

bool
metadata_logical_text_is_valid(std::string_view text) noexcept
{
    size_t i = 0U;
    while (i < text.size()) {
        const uint8_t a = static_cast<uint8_t>(text[i]);
        uint32_t cp     = 0U;
        size_t count    = 0U;

        if (a <= 0x7fU) {
            cp    = a;
            count = 1U;
        } else if (a >= 0xc2U && a <= 0xdfU) {
            if (i + 1U >= text.size()) {
                return false;
            }
            const uint8_t b = static_cast<uint8_t>(text[i + 1U]);
            if (!continuation(b)) {
                return false;
            }
            cp = (static_cast<uint32_t>(a & 0x1fU) << 6U)
                 | static_cast<uint32_t>(b & 0x3fU);
            count = 2U;
        } else if (a >= 0xe0U && a <= 0xefU) {
            if (i + 2U >= text.size()) {
                return false;
            }
            const uint8_t b = static_cast<uint8_t>(text[i + 1U]);
            const uint8_t c = static_cast<uint8_t>(text[i + 2U]);
            if (!continuation(b) || !continuation(c)
                || (a == 0xe0U && b < 0xa0U) || (a == 0xedU && b > 0x9fU)) {
                return false;
            }
            cp = (static_cast<uint32_t>(a & 0x0fU) << 12U)
                 | (static_cast<uint32_t>(b & 0x3fU) << 6U)
                 | static_cast<uint32_t>(c & 0x3fU);
            count = 3U;
        } else if (a >= 0xf0U && a <= 0xf4U) {
            if (i + 3U >= text.size()) {
                return false;
            }
            const uint8_t b = static_cast<uint8_t>(text[i + 1U]);
            const uint8_t c = static_cast<uint8_t>(text[i + 2U]);
            const uint8_t d = static_cast<uint8_t>(text[i + 3U]);
            if (!continuation(b) || !continuation(c) || !continuation(d)
                || (a == 0xf0U && b < 0x90U) || (a == 0xf4U && b > 0x8fU)) {
                return false;
            }
            cp = (static_cast<uint32_t>(a & 0x07U) << 18U)
                 | (static_cast<uint32_t>(b & 0x3fU) << 12U)
                 | (static_cast<uint32_t>(c & 0x3fU) << 6U)
                 | static_cast<uint32_t>(d & 0x3fU);
            count = 4U;
        } else {
            return false;
        }

        if (!xml_code_point_is_valid(cp)) {
            return false;
        }
        i += count;
    }
    return true;
}

bool
metadata_logical_field_value_is_valid(const MetadataCreationField& field) noexcept
{
    switch (field.kind) {
    case MetadataCreationFieldKind::Rating:
        return field.signed_value >= -1 && field.signed_value <= 5;
    case MetadataCreationFieldKind::Orientation:
        return field.unsigned_value >= 1U && field.unsigned_value <= 8U;
    case MetadataCreationFieldKind::PixelWidth:
    case MetadataCreationFieldKind::PixelHeight:
    case MetadataCreationFieldKind::IsoSensitivity:
        return field.unsigned_value != 0U;
    case MetadataCreationFieldKind::ColorSpace:
        return field.unsigned_value != 0U && field.unsigned_value <= 0xffffU;
    case MetadataCreationFieldKind::ExposureTime:
    case MetadataCreationFieldKind::FNumber:
    case MetadataCreationFieldKind::FocalLength:
        return field.rational.numer != 0U && field.rational.denom != 0U;
    default: return true;
    }
}

std::string_view
metadata_logical_indexed_property_path(std::string_view base, uint32_t index,
                                       std::array<char, 48U>* storage) noexcept
{
    if (!storage || base.size() + 12U > storage->size()) {
        return {};
    }

    size_t written = 0U;
    for (size_t i = 0U; i < base.size(); ++i) {
        (*storage)[written++] = base[i];
    }
    (*storage)[written++] = '[';
    const std::to_chars_result converted
        = std::to_chars(storage->data() + written,
                        storage->data() + storage->size() - 1U, index);
    if (converted.ec != std::errc()) {
        return {};
    }
    written = static_cast<size_t>(converted.ptr - storage->data());
    (*storage)[written++] = ']';
    return std::string_view(storage->data(), written);
}

}  // namespace openmeta::detail
