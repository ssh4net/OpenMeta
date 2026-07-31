// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_creation.h"

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace openmeta {
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

    static constexpr size_t kMetadataCreationFieldKindCount = 24U;

    struct CreationFieldDescriptor final {
        std::string_view schema_ns;
        std::string_view property_path;
        MetadataCreationValueKind value_kind = MetadataCreationValueKind::Text;
        bool repeated                        = false;
    };

    static bool field_descriptor(MetadataCreationFieldKind kind,
                                 CreationFieldDescriptor* out) noexcept
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
            *out = { kXmpNsDc, "creator", MetadataCreationValueKind::Text,
                     true };
            return true;
        case MetadataCreationFieldKind::Keyword:
            *out = { kXmpNsDc, "subject", MetadataCreationValueKind::Text,
                     true };
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
            *out = { kXmpNsXmp, "Rating",
                     MetadataCreationValueKind::SignedInteger, false };
            return true;
        case MetadataCreationFieldKind::Label:
            *out = { kXmpNsXmp, "Label", MetadataCreationValueKind::Text,
                     false };
            return true;
        case MetadataCreationFieldKind::CameraMake:
            *out = { kXmpNsTiff, "Make", MetadataCreationValueKind::Text,
                     false };
            return true;
        case MetadataCreationFieldKind::CameraModel:
            *out = { kXmpNsTiff, "Model", MetadataCreationValueKind::Text,
                     false };
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
            *out = { kXmpNsExif, "ISO",
                     MetadataCreationValueKind::UnsignedInteger, false };
            return true;
        case MetadataCreationFieldKind::FocalLength:
            *out = { kXmpNsExif, "FocalLength",
                     MetadataCreationValueKind::UnsignedRational, false };
            return true;
        }
        return false;
    }

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

    static bool text_is_valid_utf8_xml(std::string_view text) noexcept
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

    static bool limits_are_valid(const MetadataCreationLimits& limits) noexcept
    {
        return limits.max_fields != 0U
               && limits.max_fields <= kMetadataCreationMaxFields
               && limits.max_text_bytes_per_field != 0U
               && limits.max_text_bytes_per_field
                      <= kMetadataCreationMaxTextBytesPerField
               && limits.max_total_text_bytes != 0U
               && limits.max_total_text_bytes
                      <= kMetadataCreationMaxTotalTextBytes;
    }

    static bool field_value_is_valid(const MetadataCreationField& field) noexcept
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
            return field.unsigned_value != 0U
                   && field.unsigned_value <= 0xffffU;
        case MetadataCreationFieldKind::ExposureTime:
        case MetadataCreationFieldKind::FNumber:
        case MetadataCreationFieldKind::FocalLength:
            return field.rational.numer != 0U && field.rational.denom != 0U;
        default: return true;
        }
    }

    static std::string_view
    indexed_property_path(std::string_view base, uint32_t index,
                          std::array<char, 48U>* storage)
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

    static bool append_creation_entry(MetaStore* store, BlockId block,
                                      uint32_t order,
                                      const MetadataCreationField& field,
                                      const CreationFieldDescriptor& descriptor,
                                      uint32_t repeated_index)
    {
        std::array<char, 48U> indexed_path {};
        std::string_view property_path = descriptor.property_path;
        if (descriptor.repeated) {
            property_path = indexed_property_path(descriptor.property_path,
                                                  repeated_index,
                                                  &indexed_path);
            if (property_path.empty()) {
                return false;
            }
        }

        Entry entry;
        entry.key = make_xmp_property_key(store->arena(), descriptor.schema_ns,
                                          property_path);
        if (entry.key.data.xmp_property.schema_ns.size
                != descriptor.schema_ns.size()
            || entry.key.data.xmp_property.property_path.size
                   != property_path.size()) {
            return false;
        }

        switch (field.value_kind) {
        case MetadataCreationValueKind::Text:
            entry.value = make_text(store->arena(), field.text,
                                    TextEncoding::Utf8);
            if (entry.value.data.span.size != field.text.size()) {
                return false;
            }
            break;
        case MetadataCreationValueKind::UnsignedInteger:
            entry.value = make_u32(field.unsigned_value);
            break;
        case MetadataCreationValueKind::SignedInteger:
            entry.value = make_i32(field.signed_value);
            break;
        case MetadataCreationValueKind::UnsignedRational:
            entry.value = make_urational(field.rational.numer,
                                         field.rational.denom);
            break;
        }

        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.flags                 = EntryFlags::Dirty;
        return store->add_entry(entry) != kInvalidEntryId;
    }

    static MetadataCreationResult
    creation_error(MetadataCreationStatus status, uint32_t field_count,
                   uint32_t failed_field_index) noexcept
    {
        MetadataCreationResult result;
        result.status             = status;
        result.failed_field_index = failed_field_index;
        result.field_count        = field_count;
        return result;
    }

}  // namespace

MetadataCreationField
make_metadata_creation_text(MetadataCreationFieldKind kind,
                            std::string_view value) noexcept
{
    MetadataCreationField field;
    field.kind       = kind;
    field.value_kind = MetadataCreationValueKind::Text;
    field.text       = value;
    return field;
}


MetadataCreationField
make_metadata_creation_u32(MetadataCreationFieldKind kind,
                           uint32_t value) noexcept
{
    MetadataCreationField field;
    field.kind           = kind;
    field.value_kind     = MetadataCreationValueKind::UnsignedInteger;
    field.unsigned_value = value;
    return field;
}


MetadataCreationField
make_metadata_creation_i32(MetadataCreationFieldKind kind,
                           int32_t value) noexcept
{
    MetadataCreationField field;
    field.kind         = kind;
    field.value_kind   = MetadataCreationValueKind::SignedInteger;
    field.signed_value = value;
    return field;
}


MetadataCreationField
make_metadata_creation_urational(MetadataCreationFieldKind kind, uint32_t numer,
                                 uint32_t denom) noexcept
{
    MetadataCreationField field;
    field.kind           = kind;
    field.value_kind     = MetadataCreationValueKind::UnsignedRational;
    field.rational.numer = numer;
    field.rational.denom = denom;
    return field;
}


MetadataCreationResult
create_metadata(const MetadataCreationRequest& request, MetaStore* out_store)
{
    const uint32_t field_count
        = request.fields.size() > std::numeric_limits<uint32_t>::max()
              ? std::numeric_limits<uint32_t>::max()
              : static_cast<uint32_t>(request.fields.size());
    if (!out_store) {
        return creation_error(MetadataCreationStatus::NullOutput, field_count,
                              kInvalidMetadataCreationFieldIndex);
    }
    if (!limits_are_valid(request.limits)) {
        return creation_error(MetadataCreationStatus::InvalidLimits,
                              field_count, kInvalidMetadataCreationFieldIndex);
    }
    if (request.fields.size() > request.limits.max_fields
        || request.fields.size() > kMetadataCreationMaxFields) {
        return creation_error(MetadataCreationStatus::TooManyFields,
                              field_count, kInvalidMetadataCreationFieldIndex);
    }

    std::array<bool, kMetadataCreationFieldKindCount> seen {};
    uint64_t total_text_bytes = 0U;
    for (uint32_t i = 0U; i < field_count; ++i) {
        const MetadataCreationField& field = request.fields[i];
        CreationFieldDescriptor descriptor;
        if (!field_descriptor(field.kind, &descriptor)
            || field.value_kind != descriptor.value_kind) {
            return creation_error(MetadataCreationStatus::WrongValueKind,
                                  field_count, i);
        }

        const size_t kind_index = static_cast<size_t>(field.kind);
        if (kind_index >= seen.size()) {
            return creation_error(MetadataCreationStatus::WrongValueKind,
                                  field_count, i);
        }
        if (!descriptor.repeated && seen[kind_index]) {
            return creation_error(MetadataCreationStatus::DuplicateSingleton,
                                  field_count, i);
        }
        seen[kind_index] = true;

        if (field.value_kind == MetadataCreationValueKind::Text) {
            if (field.text.empty()) {
                return creation_error(MetadataCreationStatus::EmptyText,
                                      field_count, i);
            }
            if (field.text.size() > request.limits.max_text_bytes_per_field
                || field.text.size() > kMetadataCreationMaxTextBytesPerField) {
                return creation_error(MetadataCreationStatus::TextTooLong,
                                      field_count, i);
            }
            total_text_bytes += field.text.size();
            if (total_text_bytes > request.limits.max_total_text_bytes
                || total_text_bytes > kMetadataCreationMaxTotalTextBytes) {
                return creation_error(MetadataCreationStatus::TotalTextTooLong,
                                      field_count, i);
            }
            if (!text_is_valid_utf8_xml(field.text)) {
                return creation_error(MetadataCreationStatus::InvalidText,
                                      field_count, i);
            }
        } else if (!field_value_is_valid(field)) {
            return creation_error(MetadataCreationStatus::InvalidValue,
                                  field_count, i);
        }
    }

    MetaStore created;
    if (field_count != 0U) {
        const BlockId block = created.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return creation_error(MetadataCreationStatus::InternalError,
                                  field_count,
                                  kInvalidMetadataCreationFieldIndex);
        }

        uint32_t creator_index = 0U;
        uint32_t keyword_index = 0U;
        for (uint32_t i = 0U; i < field_count; ++i) {
            const MetadataCreationField& field = request.fields[i];
            CreationFieldDescriptor descriptor;
            if (!field_descriptor(field.kind, &descriptor)) {
                return creation_error(MetadataCreationStatus::InternalError,
                                      field_count, i);
            }
            uint32_t repeated_index = 0U;
            if (field.kind == MetadataCreationFieldKind::Creator) {
                repeated_index = ++creator_index;
            } else if (field.kind == MetadataCreationFieldKind::Keyword) {
                repeated_index = ++keyword_index;
            }
            if (!append_creation_entry(&created, block, i, field, descriptor,
                                       repeated_index)) {
                return creation_error(MetadataCreationStatus::InternalError,
                                      field_count, i);
            }
        }
    }

    created.finalize();
    *out_store = std::move(created);

    MetadataCreationResult result;
    result.field_count     = field_count;
    result.entries_created = field_count;
    return result;
}


const char*
metadata_creation_field_kind_name(MetadataCreationFieldKind kind) noexcept
{
    switch (kind) {
    case MetadataCreationFieldKind::Title: return "title";
    case MetadataCreationFieldKind::Description: return "description";
    case MetadataCreationFieldKind::Creator: return "creator";
    case MetadataCreationFieldKind::Keyword: return "keyword";
    case MetadataCreationFieldKind::Copyright: return "copyright";
    case MetadataCreationFieldKind::RightsUsageTerms:
        return "rights_usage_terms";
    case MetadataCreationFieldKind::Credit: return "credit";
    case MetadataCreationFieldKind::Source: return "source";
    case MetadataCreationFieldKind::CreateDate: return "create_date";
    case MetadataCreationFieldKind::ModifyDate: return "modify_date";
    case MetadataCreationFieldKind::Rating: return "rating";
    case MetadataCreationFieldKind::Label: return "label";
    case MetadataCreationFieldKind::CameraMake: return "camera_make";
    case MetadataCreationFieldKind::CameraModel: return "camera_model";
    case MetadataCreationFieldKind::Software: return "software";
    case MetadataCreationFieldKind::DateTimeOriginal:
        return "date_time_original";
    case MetadataCreationFieldKind::Orientation: return "orientation";
    case MetadataCreationFieldKind::PixelWidth: return "pixel_width";
    case MetadataCreationFieldKind::PixelHeight: return "pixel_height";
    case MetadataCreationFieldKind::ColorSpace: return "color_space";
    case MetadataCreationFieldKind::ExposureTime: return "exposure_time";
    case MetadataCreationFieldKind::FNumber: return "f_number";
    case MetadataCreationFieldKind::IsoSensitivity: return "iso_sensitivity";
    case MetadataCreationFieldKind::FocalLength: return "focal_length";
    }
    return "unknown";
}


const char*
metadata_creation_status_name(MetadataCreationStatus status) noexcept
{
    switch (status) {
    case MetadataCreationStatus::Ok: return "ok";
    case MetadataCreationStatus::NullOutput: return "null_output";
    case MetadataCreationStatus::InvalidLimits: return "invalid_limits";
    case MetadataCreationStatus::TooManyFields: return "too_many_fields";
    case MetadataCreationStatus::WrongValueKind: return "wrong_value_kind";
    case MetadataCreationStatus::EmptyText: return "empty_text";
    case MetadataCreationStatus::TextTooLong: return "text_too_long";
    case MetadataCreationStatus::TotalTextTooLong: return "total_text_too_long";
    case MetadataCreationStatus::InvalidText: return "invalid_text";
    case MetadataCreationStatus::InvalidValue: return "invalid_value";
    case MetadataCreationStatus::DuplicateSingleton:
        return "duplicate_singleton";
    case MetadataCreationStatus::InternalError: return "internal_error";
    }
    return "unknown";
}

}  // namespace openmeta
