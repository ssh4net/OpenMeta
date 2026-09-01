// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_authoring.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

namespace openmeta {
namespace {

    static uint32_t element_width(MetaElementType type) noexcept
    {
        switch (type) {
        case MetaElementType::U8:
        case MetaElementType::I8: return 1U;
        case MetaElementType::U16:
        case MetaElementType::I16: return 2U;
        case MetaElementType::U32:
        case MetaElementType::I32:
        case MetaElementType::F32: return 4U;
        case MetaElementType::U64:
        case MetaElementType::I64:
        case MetaElementType::F64:
        case MetaElementType::URational:
        case MetaElementType::SRational: return 8U;
        }
        return 0U;
    }

    static bool value_view_is_valid(const MetaValueView& value) noexcept
    {
        if (static_cast<uint8_t>(value.kind)
                > static_cast<uint8_t>(MetaValueKind::Text)
            || static_cast<uint8_t>(value.elem_type)
                   > static_cast<uint8_t>(MetaElementType::SRational)
            || static_cast<uint8_t>(value.text_encoding)
                   > static_cast<uint8_t>(TextEncoding::Utf16BE)) {
            return false;
        }
        switch (value.kind) {
        case MetaValueKind::Empty:
            return value.count == 0U && value.payload.empty();
        case MetaValueKind::Scalar:
            return value.count == 1U && value.payload.empty();
        case MetaValueKind::Array: {
            const uint32_t width = element_width(value.elem_type);
            return width != 0U
                   && static_cast<uint64_t>(value.count) * width
                          == value.payload.size();
        }
        case MetaValueKind::Bytes:
        case MetaValueKind::Text: return value.count == value.payload.size();
        }
        return false;
    }

    static uint64_t key_bytes(const MetaKeyView& key) noexcept
    {
        switch (key.kind) {
        case MetaKeyKind::ExifTag: return key.data.exif_tag.ifd.size();
        case MetaKeyKind::XmpProperty:
            return static_cast<uint64_t>(key.data.xmp_property.schema_ns.size())
                   + key.data.xmp_property.property_path.size();
        case MetaKeyKind::IptcDataset: return 0U;
        default: return std::numeric_limits<uint64_t>::max();
        }
    }

    static bool key_is_valid(const MetaKeyView& key,
                             uint32_t max_key_bytes) noexcept
    {
        switch (key.kind) {
        case MetaKeyKind::ExifTag:
            return !key.data.exif_tag.ifd.empty()
                   && key.data.exif_tag.ifd.size() <= max_key_bytes;
        case MetaKeyKind::XmpProperty:
            return !key.data.xmp_property.schema_ns.empty()
                   && !key.data.xmp_property.property_path.empty()
                   && key.data.xmp_property.schema_ns.size() <= max_key_bytes
                   && key.data.xmp_property.property_path.size()
                          <= max_key_bytes;
        case MetaKeyKind::IptcDataset:
            return key.data.iptc_dataset.record <= 255U
                   && key.data.iptc_dataset.dataset <= 255U;
        default: return false;
        }
    }

    static MetaKey copy_key(const MetaKeyView& source,
                            ByteArena* arena) noexcept
    {
        switch (source.kind) {
        case MetaKeyKind::ExifTag:
            return make_exif_tag_key(*arena, source.data.exif_tag.ifd,
                                     source.data.exif_tag.tag);
        case MetaKeyKind::XmpProperty:
            return make_xmp_property_key(*arena,
                                         source.data.xmp_property.schema_ns,
                                         source.data.xmp_property.property_path);
        case MetaKeyKind::IptcDataset:
            return make_iptc_dataset_key(source.data.iptc_dataset.record,
                                         source.data.iptc_dataset.dataset);
        default: return MetaKey {};
        }
    }

    static bool copy_value(const MetaValueView& source, ByteArena* arena,
                           MetaValue* output) noexcept
    {
        MetaValue value;
        value.kind          = source.kind;
        value.elem_type     = source.elem_type;
        value.text_encoding = source.text_encoding;
        value.count         = source.count;
        if (source.kind == MetaValueKind::Scalar) {
            value.data = source.scalar;
        } else if (source.kind == MetaValueKind::Array
                   || source.kind == MetaValueKind::Bytes
                   || source.kind == MetaValueKind::Text) {
            value.data.span = arena->append(source.payload);
            if (value.data.span.size != source.payload.size()) {
                return false;
            }
        }
        *output = value;
        return true;
    }

    static MetadataAuthoringResult error(MetadataAuthoringStatus status,
                                         uint32_t failed_entry) noexcept
    {
        MetadataAuthoringResult result;
        result.status       = status;
        result.failed_entry = failed_entry;
        return result;
    }

    static bool validation_options_are_valid(
        const MetadataValidationOptions& options) noexcept
    {
        return options.max_issues != 0U && options.max_key_bytes != 0U
               && options.max_entries != 0U && options.max_arena_bytes != 0U
               && options.max_value_bytes != 0U
               && options.max_value_bytes <= options.max_arena_bytes
               && static_cast<uint8_t>(options.unknown_exif_tags)
                      <= static_cast<uint8_t>(MetadataUnknownTagPolicy::Error)
               && (!options.context.has_dimensions
                   || (options.context.width != 0U
                       && options.context.height != 0U))
               && (!options.context.has_samples_per_pixel
                   || options.context.samples_per_pixel != 0U)
               && (!options.context.has_color_planes
                   || options.context.color_planes != 0U);
    }

}  // namespace


MetadataAuthoringResult
create_metadata_store(std::span<const MetadataAuthoringEntry> entries,
                      MetaStore* output,
                      const MetadataAuthoringOptions& options) noexcept
{
    if (!output) {
        return error(MetadataAuthoringStatus::NullOutput,
                     kInvalidMetadataAuthoringEntry);
    }
    if (options.max_entries == 0U || options.max_arena_bytes == 0U
        || options.max_arena_bytes > UINT32_MAX || options.max_value_bytes == 0U
        || options.max_value_bytes > options.max_arena_bytes
        || options.max_key_bytes == 0U
        || static_cast<uint8_t>(options.duplicate_policy)
               > static_cast<uint8_t>(
                   MetadataAuthoringDuplicatePolicy::RejectExactKeys)
        || (options.validate
            && !validation_options_are_valid(options.validation))) {
        return error(MetadataAuthoringStatus::InvalidOptions,
                     kInvalidMetadataAuthoringEntry);
    }
    if (entries.size() > options.max_entries
        || entries.size() > std::numeric_limits<uint32_t>::max()) {
        return error(MetadataAuthoringStatus::TooManyEntries,
                     kInvalidMetadataAuthoringEntry);
    }

    uint64_t arena_bytes = 0U;
    for (size_t i = 0U; i < entries.size(); ++i) {
        const MetadataAuthoringEntry& item = entries[i];
        if (item.key.kind != MetaKeyKind::ExifTag
            && item.key.kind != MetaKeyKind::XmpProperty
            && item.key.kind != MetaKeyKind::IptcDataset) {
            return error(MetadataAuthoringStatus::UnsupportedKeyKind,
                         static_cast<uint32_t>(i));
        }
        if (!key_is_valid(item.key, options.max_key_bytes)) {
            return error(MetadataAuthoringStatus::InvalidKey,
                         static_cast<uint32_t>(i));
        }
        if (!value_view_is_valid(item.value)
            || item.value.payload.size() > options.max_value_bytes) {
            return error(MetadataAuthoringStatus::InvalidValue,
                         static_cast<uint32_t>(i));
        }
        const uint64_t add = key_bytes(item.key) + item.value.payload.size();
        if (add > options.max_arena_bytes - arena_bytes) {
            return error(MetadataAuthoringStatus::LimitExceeded,
                         static_cast<uint32_t>(i));
        }
        arena_bytes += add;
    }

    MetaStore candidate;
    candidate.constrain_resources(options.max_entries, options.max_arena_bytes);
    candidate.reserve(entries.empty() ? 0U : 1U,
                      static_cast<uint32_t>(entries.size()),
                      static_cast<size_t>(arena_bytes));
    BlockId block = kInvalidBlockId;
    if (!entries.empty()) {
        block = candidate.add_block(options.block);
        if (block == kInvalidBlockId) {
            return error(MetadataAuthoringStatus::LimitExceeded, 0U);
        }
    }

    for (size_t i = 0U; i < entries.size(); ++i) {
        const MetadataAuthoringEntry& item = entries[i];
        Entry entry;
        entry.key = copy_key(item.key, &candidate.arena());
        if (!copy_value(item.value, &candidate.arena(), &entry.value)
            || candidate.resource_limit_exceeded()) {
            return error(MetadataAuthoringStatus::LimitExceeded,
                         static_cast<uint32_t>(i));
        }
        entry.origin.block          = block;
        entry.origin.order_in_block = static_cast<uint32_t>(i);
        entry.origin.wire_type      = item.wire_type;
        entry.origin.wire_count     = item.wire_count;
        entry.flags                 = EntryFlags::Dirty;
        if (candidate.add_entry(entry) == kInvalidEntryId) {
            return error(MetadataAuthoringStatus::LimitExceeded,
                         static_cast<uint32_t>(i));
        }
    }
    candidate.finalize();

    if (options.duplicate_policy
        == MetadataAuthoringDuplicatePolicy::RejectExactKeys) {
        for (size_t i = 0U; i < entries.size(); ++i) {
            if (candidate.find_all(entries[i].key).size() > 1U) {
                return error(MetadataAuthoringStatus::DuplicateKey,
                             static_cast<uint32_t>(i));
            }
        }
    }

    MetadataAuthoringResult result;
    if (options.validate) {
        MetadataValidationOptions validation = options.validation;
        validation.require_finalized         = true;
        validation.max_key_bytes   = std::min(validation.max_key_bytes,
                                              options.max_key_bytes);
        validation.max_entries     = std::min(validation.max_entries,
                                              options.max_entries);
        validation.max_arena_bytes = std::min(validation.max_arena_bytes,
                                              options.max_arena_bytes);
        validation.max_value_bytes = std::min(validation.max_value_bytes,
                                              options.max_value_bytes);
        const MetadataValidationResult validated = validate_store(candidate,
                                                                  validation);
        if (!validated.ok()) {
            result.status = MetadataAuthoringStatus::ValidationFailed;
            for (const MetadataValidationIssue& issue : validated.issues) {
                if (issue.severity == ValidateIssueSeverity::Error) {
                    result.validation_issue = issue.code;
                    result.failed_entry     = issue.entry;
                    break;
                }
            }
            return result;
        }
    }

    result.entries_created = static_cast<uint32_t>(entries.size());
    *output                = std::move(candidate);
    return result;
}


const char*
metadata_authoring_status_name(MetadataAuthoringStatus status) noexcept
{
    switch (status) {
    case MetadataAuthoringStatus::Ok: return "ok";
    case MetadataAuthoringStatus::NullOutput: return "null_output";
    case MetadataAuthoringStatus::InvalidOptions: return "invalid_options";
    case MetadataAuthoringStatus::TooManyEntries: return "too_many_entries";
    case MetadataAuthoringStatus::UnsupportedKeyKind:
        return "unsupported_key_kind";
    case MetadataAuthoringStatus::InvalidKey: return "invalid_key";
    case MetadataAuthoringStatus::InvalidValue: return "invalid_value";
    case MetadataAuthoringStatus::LimitExceeded: return "limit_exceeded";
    case MetadataAuthoringStatus::DuplicateKey: return "duplicate_key";
    case MetadataAuthoringStatus::ValidationFailed: return "validation_failed";
    }
    return "unknown";
}

}  // namespace openmeta
