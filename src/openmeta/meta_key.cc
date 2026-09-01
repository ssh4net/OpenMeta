// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"

#include <algorithm>
#include <cstring>

namespace openmeta {

static int
compare_bytes(std::span<const std::byte> a,
              std::span<const std::byte> b) noexcept
{
    const size_t min_size = std::min(a.size(), b.size());
    if (min_size > 0U) {
        const int cmp = std::memcmp(a.data(), b.data(), min_size);
        if (cmp != 0) {
            return cmp;
        }
    }
    if (a.size() < b.size()) {
        return -1;
    }
    if (a.size() > b.size()) {
        return 1;
    }
    return 0;
}


MetaKeyView
make_exif_tag_key_view(std::string_view ifd, uint16_t tag) noexcept
{
    MetaKeyView key;
    key.kind              = MetaKeyKind::ExifTag;
    key.data.exif_tag.ifd = ifd;
    key.data.exif_tag.tag = tag;
    return key;
}


MetaKeyView
make_iptc_dataset_key_view(uint16_t record, uint16_t dataset) noexcept
{
    MetaKeyView key;
    key.kind                      = MetaKeyKind::IptcDataset;
    key.data.iptc_dataset.record  = record;
    key.data.iptc_dataset.dataset = dataset;
    return key;
}


MetaKeyView
make_xmp_property_key_view(std::string_view schema_ns,
                           std::string_view property_path) noexcept
{
    MetaKeyView key;
    key.kind                            = MetaKeyKind::XmpProperty;
    key.data.xmp_property.schema_ns     = schema_ns;
    key.data.xmp_property.property_path = property_path;
    return key;
}


MetaKey
make_exif_tag_key(ByteArena& arena, std::string_view ifd, uint16_t tag)
{
    MetaKey key;
    key.kind              = MetaKeyKind::ExifTag;
    key.data.exif_tag.ifd = arena.append_string(ifd);
    key.data.exif_tag.tag = tag;
    return key;
}


MetaKey
make_comment_key() noexcept
{
    MetaKey key;
    key.kind = MetaKeyKind::Comment;
    return key;
}


MetaKey
make_exr_attribute_key(ByteArena& arena, uint32_t part_index,
                       std::string_view name)
{
    MetaKey key;
    key.kind                          = MetaKeyKind::ExrAttribute;
    key.data.exr_attribute.part_index = part_index;
    key.data.exr_attribute.name       = arena.append_string(name);
    return key;
}


MetaKey
make_iptc_dataset_key(uint16_t record, uint16_t dataset) noexcept
{
    MetaKey key;
    key.kind                      = MetaKeyKind::IptcDataset;
    key.data.iptc_dataset.record  = record;
    key.data.iptc_dataset.dataset = dataset;
    return key;
}


MetaKey
make_xmp_property_key(ByteArena& arena, std::string_view schema_ns,
                      std::string_view property_path)
{
    MetaKey key;
    key.kind                            = MetaKeyKind::XmpProperty;
    key.data.xmp_property.schema_ns     = arena.append_string(schema_ns);
    key.data.xmp_property.property_path = arena.append_string(property_path);
    return key;
}


MetaKey
make_icc_header_field_key(uint32_t offset) noexcept
{
    MetaKey key;
    key.kind                         = MetaKeyKind::IccHeaderField;
    key.data.icc_header_field.offset = offset;
    return key;
}


MetaKey
make_icc_tag_key(uint32_t signature) noexcept
{
    MetaKey key;
    key.kind                   = MetaKeyKind::IccTag;
    key.data.icc_tag.signature = signature;
    return key;
}


MetaKey
make_photoshop_irb_key(uint16_t resource_id) noexcept
{
    MetaKey key;
    key.kind                           = MetaKeyKind::PhotoshopIrb;
    key.data.photoshop_irb.resource_id = resource_id;
    return key;
}


MetaKey
make_photoshop_irb_field_key(ByteArena& arena, uint16_t resource_id,
                             std::string_view field)
{
    MetaKey key;
    key.kind                                 = MetaKeyKind::PhotoshopIrbField;
    key.data.photoshop_irb_field.resource_id = resource_id;
    key.data.photoshop_irb_field.field       = arena.append_string(field);
    return key;
}


MetaKey
make_geotiff_key(uint16_t key_id) noexcept
{
    MetaKey key;
    key.kind                    = MetaKeyKind::GeotiffKey;
    key.data.geotiff_key.key_id = key_id;
    return key;
}


MetaKey
make_printim_field_key(ByteArena& arena, std::string_view field)
{
    MetaKey key;
    key.kind                     = MetaKeyKind::PrintImField;
    key.data.printim_field.field = arena.append_string(field);
    return key;
}


MetaKey
make_bmff_field_key(ByteArena& arena, std::string_view field)
{
    MetaKey key;
    key.kind                  = MetaKeyKind::BmffField;
    key.data.bmff_field.field = arena.append_string(field);
    return key;
}


MetaKey
make_jumbf_field_key(ByteArena& arena, std::string_view field)
{
    MetaKey key;
    key.kind                   = MetaKeyKind::JumbfField;
    key.data.jumbf_field.field = arena.append_string(field);
    return key;
}


MetaKey
make_jumbf_cbor_key(ByteArena& arena, std::string_view key_text)
{
    MetaKey key;
    key.kind                    = MetaKeyKind::JumbfCborKey;
    key.data.jumbf_cbor_key.key = arena.append_string(key_text);
    return key;
}


MetaKey
make_png_text_key(ByteArena& arena, std::string_view keyword,
                  std::string_view field)
{
    MetaKey key;
    key.kind                  = MetaKeyKind::PngText;
    key.data.png_text.keyword = arena.append_string(keyword);
    key.data.png_text.field   = arena.append_string(field);
    return key;
}


int
compare_key(const ByteArena& arena, const MetaKey& a, const MetaKey& b) noexcept
{
    if (a.kind != b.kind) {
        return (static_cast<int>(a.kind) < static_cast<int>(b.kind)) ? -1 : 1;
    }

    switch (a.kind) {
    case MetaKeyKind::ExifTag: {
        const int ifd_cmp = compare_bytes(arena.span(a.data.exif_tag.ifd),
                                          arena.span(b.data.exif_tag.ifd));
        if (ifd_cmp != 0) {
            return ifd_cmp;
        }
        if (a.data.exif_tag.tag < b.data.exif_tag.tag) {
            return -1;
        }
        if (a.data.exif_tag.tag > b.data.exif_tag.tag) {
            return 1;
        }
        return 0;
    }
    case MetaKeyKind::Comment: return 0;
    case MetaKeyKind::ExrAttribute: {
        if (a.data.exr_attribute.part_index < b.data.exr_attribute.part_index) {
            return -1;
        }
        if (a.data.exr_attribute.part_index > b.data.exr_attribute.part_index) {
            return 1;
        }
        return compare_bytes(arena.span(a.data.exr_attribute.name),
                             arena.span(b.data.exr_attribute.name));
    }
    case MetaKeyKind::IptcDataset: {
        if (a.data.iptc_dataset.record != b.data.iptc_dataset.record) {
            return (a.data.iptc_dataset.record < b.data.iptc_dataset.record)
                       ? -1
                       : 1;
        }
        if (a.data.iptc_dataset.dataset != b.data.iptc_dataset.dataset) {
            return (a.data.iptc_dataset.dataset < b.data.iptc_dataset.dataset)
                       ? -1
                       : 1;
        }
        return 0;
    }
    case MetaKeyKind::XmpProperty: {
        const int ns_cmp
            = compare_bytes(arena.span(a.data.xmp_property.schema_ns),
                            arena.span(b.data.xmp_property.schema_ns));
        if (ns_cmp != 0) {
            return ns_cmp;
        }
        return compare_bytes(arena.span(a.data.xmp_property.property_path),
                             arena.span(b.data.xmp_property.property_path));
    }
    case MetaKeyKind::IccHeaderField:
        if (a.data.icc_header_field.offset < b.data.icc_header_field.offset) {
            return -1;
        }
        if (a.data.icc_header_field.offset > b.data.icc_header_field.offset) {
            return 1;
        }
        return 0;
    case MetaKeyKind::IccTag:
        if (a.data.icc_tag.signature < b.data.icc_tag.signature) {
            return -1;
        }
        if (a.data.icc_tag.signature > b.data.icc_tag.signature) {
            return 1;
        }
        return 0;
    case MetaKeyKind::PhotoshopIrb:
        if (a.data.photoshop_irb.resource_id
            < b.data.photoshop_irb.resource_id) {
            return -1;
        }
        if (a.data.photoshop_irb.resource_id
            > b.data.photoshop_irb.resource_id) {
            return 1;
        }
        return 0;
    case MetaKeyKind::PhotoshopIrbField: {
        if (a.data.photoshop_irb_field.resource_id
            < b.data.photoshop_irb_field.resource_id) {
            return -1;
        }
        if (a.data.photoshop_irb_field.resource_id
            > b.data.photoshop_irb_field.resource_id) {
            return 1;
        }
        return compare_bytes(arena.span(a.data.photoshop_irb_field.field),
                             arena.span(b.data.photoshop_irb_field.field));
    }
    case MetaKeyKind::GeotiffKey:
        if (a.data.geotiff_key.key_id < b.data.geotiff_key.key_id) {
            return -1;
        }
        if (a.data.geotiff_key.key_id > b.data.geotiff_key.key_id) {
            return 1;
        }
        return 0;
    case MetaKeyKind::PrintImField:
        return compare_bytes(arena.span(a.data.printim_field.field),
                             arena.span(b.data.printim_field.field));
    case MetaKeyKind::BmffField:
        return compare_bytes(arena.span(a.data.bmff_field.field),
                             arena.span(b.data.bmff_field.field));
    case MetaKeyKind::JumbfField:
        return compare_bytes(arena.span(a.data.jumbf_field.field),
                             arena.span(b.data.jumbf_field.field));
    case MetaKeyKind::JumbfCborKey:
        return compare_bytes(arena.span(a.data.jumbf_cbor_key.key),
                             arena.span(b.data.jumbf_cbor_key.key));
    case MetaKeyKind::PngText: {
        const int keyword_cmp
            = compare_bytes(arena.span(a.data.png_text.keyword),
                            arena.span(b.data.png_text.keyword));
        if (keyword_cmp != 0) {
            return keyword_cmp;
        }
        return compare_bytes(arena.span(a.data.png_text.field),
                             arena.span(b.data.png_text.field));
    }
    }
    return 0;
}

int
compare_key_view(const ByteArena& arena, const MetaKeyView& a,
                 const MetaKey& b) noexcept
{
    if (a.kind != b.kind) {
        return (static_cast<int>(a.kind) < static_cast<int>(b.kind)) ? -1 : 1;
    }

    switch (a.kind) {
    case MetaKeyKind::ExifTag: {
        const std::span<const std::byte> a_ifd(
            reinterpret_cast<const std::byte*>(a.data.exif_tag.ifd.data()),
            a.data.exif_tag.ifd.size());
        const int ifd_cmp = compare_bytes(a_ifd,
                                          arena.span(b.data.exif_tag.ifd));
        if (ifd_cmp != 0) {
            return ifd_cmp;
        }
        if (a.data.exif_tag.tag < b.data.exif_tag.tag) {
            return -1;
        }
        if (a.data.exif_tag.tag > b.data.exif_tag.tag) {
            return 1;
        }
        return 0;
    }
    case MetaKeyKind::Comment: return 0;
    case MetaKeyKind::ExrAttribute: {
        if (a.data.exr_attribute.part_index < b.data.exr_attribute.part_index) {
            return -1;
        }
        if (a.data.exr_attribute.part_index > b.data.exr_attribute.part_index) {
            return 1;
        }
        const std::span<const std::byte> a_name(
            reinterpret_cast<const std::byte*>(a.data.exr_attribute.name.data()),
            a.data.exr_attribute.name.size());
        return compare_bytes(a_name, arena.span(b.data.exr_attribute.name));
    }
    case MetaKeyKind::IptcDataset:
        if (a.data.iptc_dataset.record != b.data.iptc_dataset.record) {
            return (a.data.iptc_dataset.record < b.data.iptc_dataset.record)
                       ? -1
                       : 1;
        }
        if (a.data.iptc_dataset.dataset != b.data.iptc_dataset.dataset) {
            return (a.data.iptc_dataset.dataset < b.data.iptc_dataset.dataset)
                       ? -1
                       : 1;
        }
        return 0;
    case MetaKeyKind::XmpProperty: {
        const std::span<const std::byte> a_ns(
            reinterpret_cast<const std::byte*>(
                a.data.xmp_property.schema_ns.data()),
            a.data.xmp_property.schema_ns.size());
        const int ns_cmp
            = compare_bytes(a_ns, arena.span(b.data.xmp_property.schema_ns));
        if (ns_cmp != 0) {
            return ns_cmp;
        }
        const std::span<const std::byte> a_prop(
            reinterpret_cast<const std::byte*>(
                a.data.xmp_property.property_path.data()),
            a.data.xmp_property.property_path.size());
        return compare_bytes(a_prop,
                             arena.span(b.data.xmp_property.property_path));
    }
    case MetaKeyKind::IccHeaderField:
        if (a.data.icc_header_field.offset < b.data.icc_header_field.offset) {
            return -1;
        }
        if (a.data.icc_header_field.offset > b.data.icc_header_field.offset) {
            return 1;
        }
        return 0;
    case MetaKeyKind::IccTag:
        if (a.data.icc_tag.signature < b.data.icc_tag.signature) {
            return -1;
        }
        if (a.data.icc_tag.signature > b.data.icc_tag.signature) {
            return 1;
        }
        return 0;
    case MetaKeyKind::PhotoshopIrb:
        if (a.data.photoshop_irb.resource_id
            < b.data.photoshop_irb.resource_id) {
            return -1;
        }
        if (a.data.photoshop_irb.resource_id
            > b.data.photoshop_irb.resource_id) {
            return 1;
        }
        return 0;
    case MetaKeyKind::PhotoshopIrbField: {
        if (a.data.photoshop_irb_field.resource_id
            < b.data.photoshop_irb_field.resource_id) {
            return -1;
        }
        if (a.data.photoshop_irb_field.resource_id
            > b.data.photoshop_irb_field.resource_id) {
            return 1;
        }
        const std::span<const std::byte> a_field(
            reinterpret_cast<const std::byte*>(
                a.data.photoshop_irb_field.field.data()),
            a.data.photoshop_irb_field.field.size());
        return compare_bytes(a_field,
                             arena.span(b.data.photoshop_irb_field.field));
    }
    case MetaKeyKind::GeotiffKey:
        if (a.data.geotiff_key.key_id < b.data.geotiff_key.key_id) {
            return -1;
        }
        if (a.data.geotiff_key.key_id > b.data.geotiff_key.key_id) {
            return 1;
        }
        return 0;
    case MetaKeyKind::PrintImField: {
        const std::span<const std::byte> a_field(
            reinterpret_cast<const std::byte*>(
                a.data.printim_field.field.data()),
            a.data.printim_field.field.size());
        return compare_bytes(a_field, arena.span(b.data.printim_field.field));
    }
    case MetaKeyKind::BmffField: {
        const std::span<const std::byte> a_field(
            reinterpret_cast<const std::byte*>(a.data.bmff_field.field.data()),
            a.data.bmff_field.field.size());
        return compare_bytes(a_field, arena.span(b.data.bmff_field.field));
    }
    case MetaKeyKind::JumbfField: {
        const std::span<const std::byte> a_field(
            reinterpret_cast<const std::byte*>(a.data.jumbf_field.field.data()),
            a.data.jumbf_field.field.size());
        return compare_bytes(a_field, arena.span(b.data.jumbf_field.field));
    }
    case MetaKeyKind::JumbfCborKey: {
        const std::span<const std::byte> a_key(
            reinterpret_cast<const std::byte*>(a.data.jumbf_cbor_key.key.data()),
            a.data.jumbf_cbor_key.key.size());
        return compare_bytes(a_key, arena.span(b.data.jumbf_cbor_key.key));
    }
    case MetaKeyKind::PngText: {
        const std::span<const std::byte> a_keyword(
            reinterpret_cast<const std::byte*>(a.data.png_text.keyword.data()),
            a.data.png_text.keyword.size());
        const int keyword_cmp
            = compare_bytes(a_keyword, arena.span(b.data.png_text.keyword));
        if (keyword_cmp != 0) {
            return keyword_cmp;
        }
        const std::span<const std::byte> a_field(
            reinterpret_cast<const std::byte*>(a.data.png_text.field.data()),
            a.data.png_text.field.size());
        return compare_bytes(a_field, arena.span(b.data.png_text.field));
    }
    }
    return 0;
}

}  // namespace openmeta
