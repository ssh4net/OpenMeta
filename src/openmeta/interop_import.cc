// SPDX-License-Identifier: Apache-2.0

#include "openmeta/interop_import.h"

#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    constexpr uint32_t kNoImportItem = 0xFFFFFFFFU;

    static FlatHostImportResult import_error(FlatHostImportCode code,
                                             uint32_t failed_item,
                                             const char* message) noexcept
    {
        FlatHostImportResult out;
        out.code        = code;
        out.failed_item = failed_item;
        out.errors      = 1U;
        out.message     = message ? message : "flat host import failed";
        return out;
    }

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

    static bool import_value_is_valid(const FlatHostImportValue& value) noexcept
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

    static bool copy_import_value(const FlatHostImportValue& source,
                                  ByteArena* arena, MetaValue* out) noexcept
    {
        if (!arena || !out || !import_value_is_valid(source)) {
            return false;
        }
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
        *out = value;
        return true;
    }

    static bool key_string_is_bounded(std::string_view value,
                                      uint32_t max_bytes) noexcept
    {
        return value.size() <= max_bytes
               && value.size() <= std::numeric_limits<uint32_t>::max();
    }

    static bool copy_explicit_key(const MetaKeyView& source, uint32_t max_bytes,
                                  ByteArena* arena, MetaKey* out) noexcept
    {
        if (!arena || !out
            || static_cast<uint8_t>(source.kind)
                   > static_cast<uint8_t>(MetaKeyKind::PngText)) {
            return false;
        }
        switch (source.kind) {
        case MetaKeyKind::ExifTag:
            if (!key_string_is_bounded(source.data.exif_tag.ifd, max_bytes)) {
                return false;
            }
            *out = make_exif_tag_key(*arena, source.data.exif_tag.ifd,
                                     source.data.exif_tag.tag);
            return out->data.exif_tag.ifd.size
                   == source.data.exif_tag.ifd.size();
        case MetaKeyKind::Comment:
            *out                       = make_comment_key();
            out->data.comment.reserved = source.data.comment.reserved;
            return true;
        case MetaKeyKind::ExrAttribute:
            if (!key_string_is_bounded(source.data.exr_attribute.name,
                                       max_bytes)) {
                return false;
            }
            *out = make_exr_attribute_key(*arena,
                                          source.data.exr_attribute.part_index,
                                          source.data.exr_attribute.name);
            return out->data.exr_attribute.name.size
                   == source.data.exr_attribute.name.size();
        case MetaKeyKind::IptcDataset:
            *out = make_iptc_dataset_key(source.data.iptc_dataset.record,
                                         source.data.iptc_dataset.dataset);
            return true;
        case MetaKeyKind::XmpProperty:
            if (!key_string_is_bounded(source.data.xmp_property.schema_ns,
                                       max_bytes)
                || !key_string_is_bounded(source.data.xmp_property.property_path,
                                          max_bytes)) {
                return false;
            }
            *out = make_xmp_property_key(*arena,
                                         source.data.xmp_property.schema_ns,
                                         source.data.xmp_property.property_path);
            return out->data.xmp_property.schema_ns.size
                       == source.data.xmp_property.schema_ns.size()
                   && out->data.xmp_property.property_path.size
                          == source.data.xmp_property.property_path.size();
        case MetaKeyKind::IccHeaderField:
            *out = make_icc_header_field_key(
                source.data.icc_header_field.offset);
            return true;
        case MetaKeyKind::IccTag:
            *out = make_icc_tag_key(source.data.icc_tag.signature);
            return true;
        case MetaKeyKind::PhotoshopIrb:
            *out = make_photoshop_irb_key(
                source.data.photoshop_irb.resource_id);
            return true;
        case MetaKeyKind::PhotoshopIrbField:
            if (!key_string_is_bounded(source.data.photoshop_irb_field.field,
                                       max_bytes)) {
                return false;
            }
            *out = make_photoshop_irb_field_key(
                *arena, source.data.photoshop_irb_field.resource_id,
                source.data.photoshop_irb_field.field);
            return out->data.photoshop_irb_field.field.size
                   == source.data.photoshop_irb_field.field.size();
        case MetaKeyKind::GeotiffKey:
            *out = make_geotiff_key(source.data.geotiff_key.key_id);
            return true;
        case MetaKeyKind::PrintImField:
            if (!key_string_is_bounded(source.data.printim_field.field,
                                       max_bytes)) {
                return false;
            }
            *out = make_printim_field_key(*arena,
                                          source.data.printim_field.field);
            return out->data.printim_field.field.size
                   == source.data.printim_field.field.size();
        case MetaKeyKind::BmffField:
            if (!key_string_is_bounded(source.data.bmff_field.field,
                                       max_bytes)) {
                return false;
            }
            *out = make_bmff_field_key(*arena, source.data.bmff_field.field);
            return out->data.bmff_field.field.size
                   == source.data.bmff_field.field.size();
        case MetaKeyKind::JumbfField:
            if (!key_string_is_bounded(source.data.jumbf_field.field,
                                       max_bytes)) {
                return false;
            }
            *out = make_jumbf_field_key(*arena, source.data.jumbf_field.field);
            return out->data.jumbf_field.field.size
                   == source.data.jumbf_field.field.size();
        case MetaKeyKind::JumbfCborKey:
            if (!key_string_is_bounded(source.data.jumbf_cbor_key.key,
                                       max_bytes)) {
                return false;
            }
            *out = make_jumbf_cbor_key(*arena, source.data.jumbf_cbor_key.key);
            return out->data.jumbf_cbor_key.key.size
                   == source.data.jumbf_cbor_key.key.size();
        case MetaKeyKind::PngText:
            if (!key_string_is_bounded(source.data.png_text.keyword, max_bytes)
                || !key_string_is_bounded(source.data.png_text.field,
                                          max_bytes)) {
                return false;
            }
            *out = make_png_text_key(*arena, source.data.png_text.keyword,
                                     source.data.png_text.field);
            return out->data.png_text.keyword.size
                       == source.data.png_text.keyword.size()
                   && out->data.png_text.field.size
                          == source.data.png_text.field.size();
        }
        return false;
    }

    class FlatHostNameCapture final : public MetadataSink {
    public:
        FlatHostNameCapture(std::span<const Entry> entries,
                            std::vector<std::string>* names) noexcept
            : entries_(entries)
            , names_(names)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (!names_ || !item.entry || entries_.empty()) {
                return;
            }
            const Entry* const begin = entries_.data();
            const Entry* const end   = begin + entries_.size();
            if (item.entry < begin || item.entry >= end) {
                return;
            }
            const size_t index = static_cast<size_t>(item.entry - begin);
            (*names_)[index].assign(item.name.data(), item.name.size());
        }

    private:
        std::span<const Entry> entries_;
        std::vector<std::string>* names_ = nullptr;
    };

}  // namespace

FlatHostImportResult
import_flat_host_metadata(const MetaStore& source,
                          std::span<const FlatHostImportItem> items,
                          const FlatHostImportOptions& options) noexcept
{
    if (!source.is_finalized()) {
        return import_error(FlatHostImportCode::SourceNotFinalized,
                            kNoImportItem,
                            "flat host import source is not finalized");
    }
    const std::span<const Entry> source_entries   = source.entries();
    const std::span<const std::byte> source_arena = source.arena().bytes();
    if (items.size() > options.max_items
        || source_entries.size() > options.max_entries
        || source_arena.size() > options.max_arena_bytes) {
        return import_error(FlatHostImportCode::LimitExceeded, kNoImportItem,
                            "flat host import exceeds resource limits");
    }

    std::vector<std::string> exported_names(source_entries.size());
    FlatHostNameCapture capture(source_entries, &exported_names);
    ExportOptions export_options;
    export_options.style              = ExportNameStyle::FlatHost;
    export_options.name_policy        = options.name_policy;
    export_options.include_makernotes = options.include_makernotes;
    visit_metadata(source, export_options, capture);

    std::unordered_map<std::string, EntryId> unique_names;
    unique_names.reserve(exported_names.size());
    for (size_t i = 0U; i < exported_names.size(); ++i) {
        if (exported_names[i].empty()) {
            continue;
        }
        const auto inserted = unique_names.emplace(exported_names[i],
                                                   static_cast<EntryId>(i));
        if (!inserted.second) {
            inserted.first->second = kInvalidEntryId;
        }
    }

    std::vector<uint32_t> updates(source_entries.size(), kNoImportItem);
    std::vector<uint32_t> additions;
    additions.reserve(items.size());
    uint64_t value_bytes = 0U;
    for (size_t i = 0U; i < items.size(); ++i) {
        const FlatHostImportItem& item = items[i];
        const uint32_t item_index      = static_cast<uint32_t>(i);
        if (item.name.empty() || item.name.size() > options.max_name_bytes
            || static_cast<uint8_t>(item.target)
                   > static_cast<uint8_t>(FlatHostImportTarget::ExplicitKey)) {
            return import_error(FlatHostImportCode::InvalidArgument, item_index,
                                "flat host import item identity is invalid");
        }
        if (!import_value_is_valid(item.value)) {
            return import_error(FlatHostImportCode::InvalidValue, item_index,
                                "flat host import value is invalid");
        }
        if (item.value.payload.size()
            > std::numeric_limits<uint64_t>::max() - value_bytes) {
            return import_error(FlatHostImportCode::LimitExceeded, item_index,
                                "flat host import value size overflows");
        }
        value_bytes += item.value.payload.size();
        if (value_bytes > options.max_value_bytes) {
            return import_error(FlatHostImportCode::LimitExceeded, item_index,
                                "flat host import values exceed byte limit");
        }

        if (item.target == FlatHostImportTarget::ExplicitKey) {
            additions.push_back(item_index);
            continue;
        }

        EntryId target = kInvalidEntryId;
        if (item.target == FlatHostImportTarget::SourceEntry) {
            target = item.source_entry;
            if (target >= source_entries.size()) {
                return import_error(FlatHostImportCode::EntryNotFound,
                                    item_index,
                                    "flat host source entry was not found");
            }
            if (exported_names[target].empty()) {
                return import_error(FlatHostImportCode::EntryNotExported,
                                    item_index,
                                    "flat host source entry is not exportable");
            }
            if (exported_names[target] != item.name) {
                return import_error(
                    FlatHostImportCode::NameMismatch, item_index,
                    "flat host source entry name does not match");
            }
        } else {
            const auto found = unique_names.find(std::string(item.name));
            if (found == unique_names.end()) {
                return import_error(FlatHostImportCode::EntryNotFound,
                                    item_index, "flat host name was not found");
            }
            if (found->second == kInvalidEntryId) {
                return import_error(FlatHostImportCode::AmbiguousName,
                                    item_index,
                                    "flat host name matches multiple entries");
            }
            target = found->second;
        }
        if (updates[target] != kNoImportItem) {
            return import_error(FlatHostImportCode::DuplicateTarget, item_index,
                                "flat host entry is updated more than once");
        }
        updates[target] = item_index;
    }

    if (additions.size() > options.max_entries - source_entries.size()) {
        return import_error(FlatHostImportCode::LimitExceeded, kNoImportItem,
                            "flat host additions exceed entry limit");
    }

    MetaStore imported;
    imported.constrain_resources(options.max_entries, options.max_arena_bytes);
    const ByteSpan arena_copy = imported.arena().append(source_arena);
    if (arena_copy.offset != 0U || arena_copy.size != source_arena.size()
        || imported.resource_limit_exceeded()) {
        return import_error(FlatHostImportCode::LimitExceeded, kNoImportItem,
                            "flat host source arena exceeds limits");
    }
    for (uint32_t i = 0U; i < source.block_count(); ++i) {
        if (imported.add_block(source.block_info(i)) == kInvalidBlockId) {
            return import_error(FlatHostImportCode::LimitExceeded,
                                kNoImportItem,
                                "flat host source block was rejected");
        }
    }

    uint32_t updated = 0U;
    for (size_t i = 0U; i < source_entries.size(); ++i) {
        Entry entry = source_entries[i];
        if (updates[i] != kNoImportItem) {
            const FlatHostImportItem& item = items[updates[i]];
            if (!copy_import_value(item.value, &imported.arena(),
                                   &entry.value)) {
                return import_error(FlatHostImportCode::LimitExceeded,
                                    updates[i],
                                    "flat host imported value was rejected");
            }
            entry.flags |= EntryFlags::Dirty;
            updated += 1U;
        }
        if (imported.add_entry(entry) == kInvalidEntryId) {
            return import_error(FlatHostImportCode::LimitExceeded, updates[i],
                                "flat host source entry was rejected");
        }
    }

    for (uint32_t item_index : additions) {
        const FlatHostImportItem& item = items[item_index];
        Entry entry;
        if (!copy_explicit_key(item.explicit_key, options.max_name_bytes,
                               &imported.arena(), &entry.key)) {
            return import_error(FlatHostImportCode::InvalidKey, item_index,
                                "flat host explicit key is invalid");
        }
        if (!copy_import_value(item.value, &imported.arena(), &entry.value)) {
            return import_error(FlatHostImportCode::LimitExceeded, item_index,
                                "flat host explicit value was rejected");
        }
        entry.flags = EntryFlags::Dirty;
        if (imported.add_entry(entry) == kInvalidEntryId) {
            return import_error(FlatHostImportCode::LimitExceeded, item_index,
                                "flat host explicit entry was rejected");
        }
    }

    imported.finalize();
    FlatHostImportResult out;
    out.code     = FlatHostImportCode::None;
    out.imported = static_cast<uint32_t>(items.size());
    out.updated  = updated;
    out.added    = static_cast<uint32_t>(additions.size());
    out.message  = "flat host metadata imported";
    out.store    = std::move(imported);
    return out;
}

}  // namespace openmeta
