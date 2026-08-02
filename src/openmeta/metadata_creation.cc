// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_creation.h"

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include "metadata_logical_field_internal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace openmeta {
namespace {

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

    static bool append_creation_entry(
        MetaStore* store, BlockId block, uint32_t order,
        const MetadataCreationField& field,
        const detail::MetadataLogicalFieldDescriptor& descriptor,
        uint32_t repeated_index)
    {
        std::array<char, 48U> indexed_path {};
        std::string_view property_path = descriptor.property_path;
        if (descriptor.repeated) {
            property_path = detail::metadata_logical_indexed_property_path(
                descriptor.property_path, repeated_index, &indexed_path);
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

    std::array<bool, detail::kMetadataLogicalFieldKindCount> seen {};
    uint64_t total_text_bytes = 0U;
    for (uint32_t i = 0U; i < field_count; ++i) {
        const MetadataCreationField& field = request.fields[i];
        detail::MetadataLogicalFieldDescriptor descriptor;
        if (!detail::metadata_logical_field_descriptor(field.kind, &descriptor)
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
            if (!detail::metadata_logical_text_is_valid(field.text)) {
                return creation_error(MetadataCreationStatus::InvalidText,
                                      field_count, i);
            }
        } else if (!detail::metadata_logical_field_value_is_valid(field)) {
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
            detail::MetadataLogicalFieldDescriptor descriptor;
            if (!detail::metadata_logical_field_descriptor(field.kind,
                                                           &descriptor)) {
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
