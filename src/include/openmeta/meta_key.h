// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/byte_arena.h"

#include <cstdint>
#include <string_view>

/**
 * \file meta_key.h
 * \brief Normalized key identifiers for EXIF/IPTC/XMP/etc. metadata entries.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Namespace for different metadata key spaces.
enum class MetaKeyKind : uint8_t {
    ExifTag,
    Comment,
    ExrAttribute,
    IptcDataset,
    XmpProperty,
    IccHeaderField,
    IccTag,
    PhotoshopIrb,
    PhotoshopIrbField,
    GeotiffKey,
    PrintImField,
    BmffField,
    JumbfField,
    JumbfCborKey,
    PngText,
};

/**
 * \brief An owned metadata key.
 *
 * Uses \ref ByteSpan fields for string-like components so keys can be stored
 * compactly in a \ref ByteArena (e.g. IFD token, XMP schema namespace).
 */
struct MetaKey final {
    MetaKeyKind kind = MetaKeyKind::ExifTag;

    union Data {
        struct ExifTag final {
            ByteSpan ifd;
            uint16_t tag = 0;
        } exif_tag;

        struct Comment final {
            uint8_t reserved = 0;
        } comment;

        struct ExrAttribute final {
            uint32_t part_index = 0;
            ByteSpan name;
        } exr_attribute;

        struct IptcDataset final {
            uint16_t record  = 0;
            uint16_t dataset = 0;
        } iptc_dataset;

        struct XmpProperty final {
            ByteSpan schema_ns;
            ByteSpan property_path;
        } xmp_property;

        struct IccHeaderField final {
            uint32_t offset = 0;
        } icc_header_field;

        struct IccTag final {
            uint32_t signature = 0;
        } icc_tag;

        struct PhotoshopIrb final {
            uint16_t resource_id = 0;
        } photoshop_irb;

        struct PhotoshopIrbField final {
            uint16_t resource_id = 0;
            ByteSpan field;
        } photoshop_irb_field;

        struct GeotiffKey final {
            uint16_t key_id = 0;
        } geotiff_key;

        struct PrintImField final {
            ByteSpan field;
        } printim_field;

        struct BmffField final {
            ByteSpan field;
        } bmff_field;

        struct JumbfField final {
            ByteSpan field;
        } jumbf_field;

        struct JumbfCborKey final {
            ByteSpan key;
        } jumbf_cbor_key;

        struct PngText final {
            ByteSpan keyword;
            ByteSpan field;
        } png_text;

        Data() noexcept
            : exif_tag()
        {
        }
    } data;
};

/**
 * \brief A borrowed metadata key view.
 *
 * Intended for lookups and comparisons without allocating/copying strings.
 */
struct MetaKeyView final {
    MetaKeyKind kind = MetaKeyKind::ExifTag;

    union Data {
        struct ExifTag final {
            std::string_view ifd;
            uint16_t tag = 0;
        } exif_tag;

        struct Comment final {
            uint8_t reserved = 0;
        } comment;

        struct ExrAttribute final {
            uint32_t part_index = 0;
            std::string_view name;
        } exr_attribute;

        struct IptcDataset final {
            uint16_t record  = 0;
            uint16_t dataset = 0;
        } iptc_dataset;

        struct XmpProperty final {
            std::string_view schema_ns;
            std::string_view property_path;
        } xmp_property;

        struct IccHeaderField final {
            uint32_t offset = 0;
        } icc_header_field;

        struct IccTag final {
            uint32_t signature = 0;
        } icc_tag;

        struct PhotoshopIrb final {
            uint16_t resource_id = 0;
        } photoshop_irb;

        struct PhotoshopIrbField final {
            uint16_t resource_id = 0;
            std::string_view field;
        } photoshop_irb_field;

        struct GeotiffKey final {
            uint16_t key_id = 0;
        } geotiff_key;

        struct PrintImField final {
            std::string_view field;
        } printim_field;

        struct BmffField final {
            std::string_view field;
        } bmff_field;

        struct JumbfField final {
            std::string_view field;
        } jumbf_field;

        struct JumbfCborKey final {
            std::string_view key;
        } jumbf_cbor_key;

        struct PngText final {
            std::string_view keyword;
            std::string_view field;
        } png_text;

        Data() noexcept
            : exif_tag()
        {
        }
    } data;
};

/// Creates a key for an EXIF/TIFF tag within a named IFD token (e.g. "ifd0", "exififd").
MetaKey
make_exif_tag_key(ByteArena& arena, std::string_view ifd, uint16_t tag);
MetaKey
make_comment_key() noexcept;
/// Creates a key for an OpenEXR attribute name in a specific part.
MetaKey
make_exr_attribute_key(ByteArena& arena, uint32_t part_index,
                       std::string_view name);
MetaKey
make_iptc_dataset_key(uint16_t record, uint16_t dataset) noexcept;
MetaKey
make_xmp_property_key(ByteArena& arena, std::string_view schema_ns,
                      std::string_view property_path);
MetaKey
make_icc_header_field_key(uint32_t offset) noexcept;
MetaKey
make_icc_tag_key(uint32_t signature) noexcept;
MetaKey
make_photoshop_irb_key(uint16_t resource_id) noexcept;
MetaKey
make_photoshop_irb_field_key(ByteArena& arena, uint16_t resource_id,
                             std::string_view field);
MetaKey
make_geotiff_key(uint16_t key_id) noexcept;
MetaKey
make_printim_field_key(ByteArena& arena, std::string_view field);
MetaKey
make_bmff_field_key(ByteArena& arena, std::string_view field);
MetaKey
make_jumbf_field_key(ByteArena& arena, std::string_view field);
MetaKey
make_jumbf_cbor_key(ByteArena& arena, std::string_view key);
MetaKey
make_png_text_key(ByteArena& arena, std::string_view keyword,
                  std::string_view field);

/// Orders keys for deterministic storage/indexing.
int
compare_key(const ByteArena& arena, const MetaKey& a,
            const MetaKey& b) noexcept;
/// Orders a borrowed key against an owned key using the same ordering as \ref compare_key.
int
compare_key_view(const ByteArena& arena, const MetaKeyView& a,
                 const MetaKey& b) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
