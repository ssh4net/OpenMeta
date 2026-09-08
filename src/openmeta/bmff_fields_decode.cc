// SPDX-License-Identifier: Apache-2.0

#include "bmff_fields_decode_internal.h"

#include "openmeta/container_scan.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace openmeta {
namespace {

    static constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }


    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 8)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 0);
        *out = v;
        return true;
    }


    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4 > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 24;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 16;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 8;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 0;
        *out = v;
        return true;
    }


    static bool read_i32be(std::span<const std::byte> bytes, uint64_t offset,
                           int32_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint32_t raw = 0;
        if (!read_u32be(bytes, offset, &raw)) {
            return false;
        }
        *out = static_cast<int32_t>(raw);
        return true;
    }


    static bool read_u64be(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (!out || offset + 8 > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            v = (v << 8) | static_cast<uint64_t>(u8(bytes[offset + i]));
        }
        *out = v;
        return true;
    }

    static bool read_uint_be_n(std::span<const std::byte> bytes,
                               uint64_t offset, uint8_t byte_count,
                               uint64_t* out) noexcept
    {
        const uint64_t size = static_cast<uint64_t>(bytes.size());
        if (!out || byte_count > 8U || offset > size
            || static_cast<uint64_t>(byte_count) > size - offset) {
            return false;
        }
        uint64_t v = 0U;
        for (uint8_t i = 0U; i < byte_count; ++i) {
            v = (v << 8U) | static_cast<uint64_t>(u8(bytes[offset + i]));
        }
        *out = v;
        return true;
    }


    struct BmffBox final {
        uint64_t offset      = 0;
        uint64_t size        = 0;
        uint64_t header_size = 0;
        uint32_t type        = 0;
        bool has_uuid        = false;
        std::array<std::byte, 16> uuid {};
    };

    static bool parse_bmff_box(std::span<const std::byte> bytes,
                               uint64_t offset, uint64_t parent_end,
                               BmffBox* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (offset + 8 > parent_end || offset + 8 > bytes.size()) {
            return false;
        }
        uint32_t size32 = 0;
        uint32_t type   = 0;
        if (!read_u32be(bytes, offset + 0, &size32)
            || !read_u32be(bytes, offset + 4, &type)) {
            return false;
        }

        uint64_t header_size = 8;
        uint64_t box_size    = size32;
        if (size32 == 1) {
            uint64_t size64 = 0;
            if (!read_u64be(bytes, offset + 8, &size64)) {
                return false;
            }
            header_size = 16;
            box_size    = size64;
        } else if (size32 == 0) {
            box_size = parent_end - offset;
        }

        if (box_size < header_size) {
            return false;
        }
        if (offset + box_size > parent_end
            || offset + box_size > bytes.size()) {
            return false;
        }

        bool has_uuid = false;
        std::array<std::byte, 16> uuid {};
        if (type == fourcc('u', 'u', 'i', 'd')) {
            if (header_size + 16 > box_size) {
                return false;
            }
            has_uuid                = true;
            const uint64_t uuid_off = offset + header_size;
            if (uuid_off + 16 > bytes.size()) {
                return false;
            }
            for (uint32_t i = 0; i < 16; ++i) {
                uuid[i] = bytes[uuid_off + i];
            }
            header_size += 16;
        }

        out->offset      = offset;
        out->size        = box_size;
        out->header_size = header_size;
        out->type        = type;
        out->has_uuid    = has_uuid;
        out->uuid        = uuid;
        return true;
    }


    static bool bmff_is_container_box(uint32_t type) noexcept
    {
        switch (type) {
        case fourcc('m', 'o', 'o', 'v'):
        case fourcc('t', 'r', 'a', 'k'):
        case fourcc('m', 'd', 'i', 'a'):
        case fourcc('m', 'i', 'n', 'f'):
        case fourcc('s', 't', 'b', 'l'):
        case fourcc('e', 'd', 't', 's'):
        case fourcc('d', 'i', 'n', 'f'):
        case fourcc('u', 'd', 't', 'a'): return true;
        default: return false;
        }
    }


    static void bmff_note_brand(uint32_t brand, bool* is_heif, bool* is_avif,
                                bool* is_cr3) noexcept
    {
        if (brand == fourcc('c', 'r', 'x', ' ')
            || brand == fourcc('C', 'R', '3', ' ')) {
            *is_cr3 = true;
        }

        if (brand == fourcc('a', 'v', 'i', 'f')
            || brand == fourcc('a', 'v', 'i', 's')
            || brand == fourcc('a', 'v', 'i', 'o')) {
            *is_avif = true;
        }

        if (brand == fourcc('m', 'i', 'f', '1')
            || brand == fourcc('m', 's', 'f', '1')
            || brand == fourcc('h', 'e', 'i', 'c')
            || brand == fourcc('h', 'e', 'i', 'x')
            || brand == fourcc('h', 'e', 'v', 'c')
            || brand == fourcc('h', 'e', 'v', 'x')) {
            *is_heif = true;
        }
    }


    static bool bmff_parse_ftyp(std::span<const std::byte> bytes,
                                const BmffBox& ftyp,
                                ContainerFormat* out_format,
                                uint32_t* out_major_brand,
                                uint32_t* out_minor_version,
                                std::array<uint32_t, 32>* out_compat_brands,
                                uint32_t* out_compat_count) noexcept
    {
        if (!out_format || !out_major_brand || !out_minor_version
            || !out_compat_brands || !out_compat_count) {
            return false;
        }

        const uint64_t payload_off  = ftyp.offset + ftyp.header_size;
        const uint64_t payload_size = ftyp.size - ftyp.header_size;
        if (payload_size < 8) {
            return false;
        }

        uint32_t major_brand = 0;
        uint32_t minor_ver   = 0;
        if (!read_u32be(bytes, payload_off + 0, &major_brand)
            || !read_u32be(bytes, payload_off + 4, &minor_ver)) {
            return false;
        }

        bool is_heif = false;
        bool is_avif = false;
        bool is_cr3  = false;
        bmff_note_brand(major_brand, &is_heif, &is_avif, &is_cr3);

        std::array<uint32_t, 32> compat {};
        uint32_t compat_count     = 0;
        const uint64_t brands_off = payload_off + 8;
        const uint64_t brands_end = payload_off + payload_size;
        for (uint64_t off = brands_off; off + 4 <= brands_end; off += 4) {
            uint32_t brand = 0;
            if (!read_u32be(bytes, off, &brand)) {
                return false;
            }
            bmff_note_brand(brand, &is_heif, &is_avif, &is_cr3);
            if (compat_count < compat.size()) {
                compat[compat_count++] = brand;
            }
        }

        ContainerFormat fmt = ContainerFormat::Unknown;
        if (is_cr3) {
            fmt = ContainerFormat::Cr3;
        } else if (is_avif) {
            fmt = ContainerFormat::Avif;
        } else if (is_heif) {
            fmt = ContainerFormat::Heif;
        } else {
            return false;
        }

        *out_format        = fmt;
        *out_major_brand   = major_brand;
        *out_minor_version = minor_ver;
        *out_compat_brands = compat;
        *out_compat_count  = compat_count;
        return true;
    }


    static void emit_u32_field(MetaStore& store, BlockId block, uint32_t order,
                               std::string_view field, uint32_t value) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_u32(value);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }

    static void emit_u64_field(MetaStore& store, BlockId block, uint32_t order,
                               std::string_view field, uint64_t value) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_u64(value);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_u16_field(MetaStore& store, BlockId block, uint32_t order,
                               std::string_view field, uint16_t value) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_u16(value);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_u8_field(MetaStore& store, BlockId block, uint32_t order,
                              std::string_view field, uint8_t value) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_u8(value);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_i32_field(MetaStore& store, BlockId block, uint32_t order,
                               std::string_view field, int32_t value) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_i32(value);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_text_field(MetaStore& store, BlockId block, uint32_t order,
                                std::string_view field,
                                std::string_view value) noexcept
    {
        Entry e;
        e.key          = make_bmff_field_key(store.arena(), field);
        e.value        = make_text(store.arena(), value, TextEncoding::Ascii);
        e.origin.block = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }

    static void emit_utf8_field(MetaStore& store, BlockId block, uint32_t order,
                                std::string_view field,
                                std::string_view value) noexcept
    {
        Entry e;
        e.key          = make_bmff_field_key(store.arena(), field);
        e.value        = make_text(store.arena(), value, TextEncoding::Utf8);
        e.origin.block = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }

    static bool bmff_fourcc_field_token(uint32_t type,
                                        std::array<char, 5>* out) noexcept
    {
        if (!out) {
            return false;
        }
        for (uint32_t i = 0; i < 4; ++i) {
            char c = static_cast<char>((type >> ((3U - i) * 8U)) & 0xFFU);
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            } else if (c == ' ') {
                c = '_';
            } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                         || c == '_')) {
                return false;
            }
            (*out)[i] = c;
        }
        (*out)[4] = '\0';
        return true;
    }

    static std::string bmff_fourcc_display_name(uint32_t type) noexcept
    {
        std::array<char, 5> text {};
        bool printable = true;
        for (uint32_t i = 0; i < 4; ++i) {
            const char c = static_cast<char>((type >> ((3U - i) * 8U)) & 0xFFU);
            text[i]      = c;
            if (c < 0x20 || c > 0x7e) {
                printable = false;
            }
        }
        text[4] = '\0';
        if (printable) {
            return std::string(text.data(), 4U);
        }

        char hex[11] {};
        const int n = std::snprintf(hex, sizeof(hex), "0x%08x",
                                    static_cast<unsigned>(type));
        if (n <= 0) {
            return std::string();
        }
        return std::string(hex, static_cast<size_t>(n));
    }

    static bool bmff_is_known_typed_iref_relation(uint32_t ref_type) noexcept
    {
        return ref_type == fourcc('a', 'u', 'x', 'l')
               || ref_type == fourcc('d', 'i', 'm', 'g')
               || ref_type == fourcc('t', 'h', 'm', 'b')
               || ref_type == fourcc('c', 'd', 's', 'c');
    }

    static std::string_view iref_from_role_name(uint32_t ref_type) noexcept
    {
        if (ref_type == fourcc('a', 'u', 'x', 'l')) {
            return "auxiliary_image";
        }
        if (ref_type == fourcc('d', 'i', 'm', 'g')) {
            return "derived_image";
        }
        if (ref_type == fourcc('t', 'h', 'm', 'b')) {
            return "thumbnail_image";
        }
        if (ref_type == fourcc('c', 'd', 's', 'c')) {
            return "descriptive_item";
        }
        return {};
    }

    static std::string_view iref_to_role_name(uint32_t ref_type) noexcept
    {
        if (ref_type == fourcc('a', 'u', 'x', 'l')
            || ref_type == fourcc('t', 'h', 'm', 'b')) {
            return "master_image";
        }
        if (ref_type == fourcc('d', 'i', 'm', 'g')) {
            return "source_image";
        }
        if (ref_type == fourcc('c', 'd', 's', 'c')) {
            return "described_item";
        }
        return {};
    }

    static void emit_iref_semantic_edge_fields(MetaStore& store, BlockId block,
                                               uint32_t* io_order,
                                               uint32_t ref_type,
                                               std::string_view rel_type,
                                               uint32_t from_item_id,
                                               uint32_t to_item_id) noexcept
    {
        if (!io_order || rel_type.empty()) {
            return;
        }
        const std::string_view from_role = iref_from_role_name(ref_type);
        const std::string_view to_role   = iref_to_role_name(ref_type);
        if (from_role.empty() || to_role.empty()) {
            return;
        }

        std::string field("iref.");
        field.append(rel_type);
        const size_t base_len = field.size();

        field.append(".from_role");
        emit_text_field(store, block, (*io_order)++, field, from_role);
        field.resize(base_len);
        field.append(".to_role");
        emit_text_field(store, block, (*io_order)++, field, to_role);
        field.resize(base_len);

        if (ref_type == fourcc('a', 'u', 'x', 'l')) {
            field.append(".auxiliary_item_id");
            emit_u32_field(store, block, (*io_order)++, field, from_item_id);
            field.resize(base_len);
            field.append(".master_item_id");
            emit_u32_field(store, block, (*io_order)++, field, to_item_id);
        } else if (ref_type == fourcc('d', 'i', 'm', 'g')) {
            field.append(".derived_item_id");
            emit_u32_field(store, block, (*io_order)++, field, from_item_id);
            field.resize(base_len);
            field.append(".source_item_id");
            emit_u32_field(store, block, (*io_order)++, field, to_item_id);
        } else if (ref_type == fourcc('t', 'h', 'm', 'b')) {
            field.append(".thumbnail_item_id");
            emit_u32_field(store, block, (*io_order)++, field, from_item_id);
            field.resize(base_len);
            field.append(".master_item_id");
            emit_u32_field(store, block, (*io_order)++, field, to_item_id);
        } else if (ref_type == fourcc('c', 'd', 's', 'c')) {
            field.append(".descriptive_item_id");
            emit_u32_field(store, block, (*io_order)++, field, from_item_id);
            field.resize(base_len);
            field.append(".described_item_id");
            emit_u32_field(store, block, (*io_order)++, field, to_item_id);
        }
    }

    static void emit_iref_typed_edge_fields(MetaStore& store, BlockId block,
                                            uint32_t* io_order,
                                            std::string_view rel_type,
                                            uint32_t from_item_id,
                                            uint32_t to_item_id) noexcept
    {
        if (!io_order || rel_type.empty()) {
            return;
        }
        std::string field("iref.");
        field.append(rel_type);
        const size_t base_len = field.size();

        field.append(".from_item_id");
        emit_u32_field(store, block, (*io_order)++, field, from_item_id);
        field.resize(base_len);

        field.append(".to_item_id");
        emit_u32_field(store, block, (*io_order)++, field, to_item_id);
    }

    static void
    emit_iref_typed_item_summary(MetaStore& store, BlockId block,
                                 uint32_t* io_order, std::string_view rel_type,
                                 std::span<const uint32_t> item_ids,
                                 std::span<const uint32_t> item_out_edge_counts,
                                 std::span<const uint32_t> item_in_edge_counts,
                                 uint32_t item_count) noexcept
    {
        if (!io_order || rel_type.empty()) {
            return;
        }
        const uint32_t cap = static_cast<uint32_t>(
            std::min(item_ids.size(), std::min(item_out_edge_counts.size(),
                                               item_in_edge_counts.size())));
        if (item_count == 0U || cap == 0U) {
            return;
        }
        if (item_count > cap) {
            item_count = cap;
        }

        std::string field("iref.");
        field.append(rel_type);
        const size_t base_len = field.size();

        field.append(".item_count");
        emit_u32_field(store, block, (*io_order)++, field, item_count);
        field.resize(base_len);

        for (uint32_t i = 0U; i < item_count; ++i) {
            field.append(".item_id");
            emit_u32_field(store, block, (*io_order)++, field, item_ids[i]);
            field.resize(base_len);

            field.append(".item_out_edge_count");
            emit_u32_field(store, block, (*io_order)++, field,
                           item_out_edge_counts[i]);
            field.resize(base_len);

            field.append(".item_in_edge_count");
            emit_u32_field(store, block, (*io_order)++, field,
                           item_in_edge_counts[i]);
            field.resize(base_len);
        }
    }

    static void emit_iref_typed_graph_summary(MetaStore& store, BlockId block,
                                              uint32_t* io_order,
                                              std::string_view rel_type,
                                              uint32_t edge_count,
                                              uint32_t from_unique_count,
                                              uint32_t to_unique_count) noexcept
    {
        if (!io_order || rel_type.empty() || edge_count == 0U) {
            return;
        }
        std::string field("iref.graph.");
        field.append(rel_type);
        const size_t base_len = field.size();

        field.append(".edge_count");
        emit_u32_field(store, block, (*io_order)++, field, edge_count);
        field.resize(base_len);

        field.append(".from_item_unique_count");
        emit_u32_field(store, block, (*io_order)++, field, from_unique_count);
        field.resize(base_len);

        field.append(".to_item_unique_count");
        emit_u32_field(store, block, (*io_order)++, field, to_unique_count);
    }

    static void emit_u32_array_field(MetaStore& store, BlockId block,
                                     uint32_t order, std::string_view field,
                                     std::span<const uint32_t> values) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_u32_array(store.arena(), values);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = static_cast<uint32_t>(values.size());
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_count_field_if_nonzero(MetaStore& store, BlockId block,
                                            uint32_t* io_order,
                                            std::string_view field,
                                            uint32_t count) noexcept
    {
        if (!io_order || field.empty() || count == 0U) {
            return;
        }
        emit_u32_field(store, block, (*io_order)++, field, count);
    }


    struct IspeProp final {
        uint32_t index  = 0;  // 1-based ipco index
        uint32_t width  = 0;
        uint32_t height = 0;
    };

    static constexpr uint8_t kMaxTiledImageExtraDimensions    = 8U;
    static constexpr uint8_t kMaxTilePropertyAssociations     = 64U;
    static constexpr uint8_t kMaxTiledImageOffsetRows         = 64U;
    static constexpr uint32_t kMaxTiledImageUrlBytes          = 512U;
    static constexpr uint64_t kMaxTiledImageOffsetsToValidate = 1U << 18U;

    struct TilePropertyAssociation final {
        uint32_t property_index = 0U;
        uint32_t property_type  = 0U;
        uint8_t essential       = 0U;
        bool have_property_type = false;
    };

    struct TilCProp final {
        uint32_t index                      = 0;  // 1-based ipco index
        uint32_t flags                      = 0;
        uint32_t tile_width                 = 0;
        uint32_t tile_height                = 0;
        uint32_t conditional_payload_bytes  = 0;
        uint64_t conditional_payload_offset = 0U;
        uint64_t conditional_payload_end    = 0U;
        std::array<uint32_t, kMaxTiledImageExtraDimensions> dimension_sizes {};
        uint8_t version                = 0;
        uint8_t extra_dimension_count  = 0;
        uint8_t stored_dimension_count = 0;
        bool have_header               = false;
        bool have_core                 = false;
        bool core_valid                = false;
        bool dimensions_truncated      = false;
    };

    struct IpcoProperty final {
        uint32_t index = 0U;
        uint32_t type  = 0U;
    };

    struct TiledImageUrl final {
        std::array<char, kMaxTiledImageUrlBytes> bytes {};
        uint32_t stored_bytes = 0U;
        uint32_t total_bytes  = 0U;
        bool truncated        = false;
    };

    struct DataReferenceEntry final {
        uint32_t index                       = 0U;  // 1-based dref index
        uint32_t type                        = 0U;
        uint32_t flags                       = 0U;
        uint64_t input_item_count            = 0U;
        uint64_t tile_offset_table_start     = 0U;
        uint32_t tile_offset_table_size      = 0U;
        uint64_t tile_id_start               = 0U;
        uint16_t directory_id_start          = 0U;
        uint16_t directory_id_end            = 0U;
        uint8_t version                      = 0U;
        uint8_t offset_field_bytes           = 0U;
        uint8_t size_field_bytes             = 0U;
        uint8_t input_item_count_field_bytes = 0U;
        bool have_header                     = false;
        bool syntax_valid                    = false;
        bool sequential_order                = false;
        bool external_tiles_urls             = false;
        bool directory_id_present            = false;
        TiledImageUrl base_url {};
        TiledImageUrl url_extension {};
        TiledImageUrl tile_request_template {};
    };

    struct U8Prop final {
        uint32_t index = 0;  // 1-based ipco index
        uint8_t value  = 0;
    };

    struct PaspProp final {
        uint32_t index     = 0;  // 1-based ipco index
        uint32_t h_spacing = 0;
        uint32_t v_spacing = 0;
    };

    struct PixiProp final {
        uint32_t index = 0;  // 1-based ipco index
        std::array<uint8_t, 16> bits_per_channel {};
        uint8_t channel_count = 0;
    };

    struct ClapProp final {
        uint32_t index      = 0;  // 1-based ipco index
        int32_t width_n     = 0;
        int32_t width_d     = 0;
        int32_t height_n    = 0;
        int32_t height_d    = 0;
        int32_t horiz_off_n = 0;
        int32_t horiz_off_d = 0;
        int32_t vert_off_n  = 0;
        int32_t vert_off_d  = 0;
    };

    struct ColrProp final {
        uint32_t index                    = 0;  // 1-based ipco index
        uint32_t color_type               = 0;
        bool have_nclx                    = false;
        uint16_t colour_primaries         = 0;
        uint16_t transfer_characteristics = 0;
        uint16_t matrix_coefficients      = 0;
        bool have_full_range_flag         = false;
        uint8_t full_range_flag           = 0;
        uint32_t profile_bytes            = 0;
    };

    enum class AuxSemantic : uint8_t {
        Unknown   = 0,
        Alpha     = 1,
        Depth     = 2,
        Disparity = 3,
        Matte     = 4,
    };

    enum class PrimaryLinkedRole : uint8_t {
        Auxiliary          = 0,
        Alpha              = 1,
        Depth              = 2,
        Disparity          = 3,
        Matte              = 4,
        DerivedImage       = 5,
        Thumbnail          = 6,
        ContentDescription = 7,
    };

    enum class ItemSemantic : uint8_t {
        Unknown            = 0,
        Image              = 1,
        Exif               = 2,
        Xmp                = 3,
        Jumbf              = 4,
        C2pa               = 5,
        IccProfile         = 6,
        Auxiliary          = 7,
        DerivedImage       = 8,
        Thumbnail          = 9,
        ContentDescription = 10,
        Uri                = 11,
        Json               = 12,
    };

    struct ItemSemanticCounts final {
        uint32_t known               = 0;
        uint32_t metadata            = 0;
        uint32_t image               = 0;
        uint32_t exif                = 0;
        uint32_t xmp                 = 0;
        uint32_t jumbf               = 0;
        uint32_t c2pa                = 0;
        uint32_t icc_profile         = 0;
        uint32_t auxiliary           = 0;
        uint32_t derived             = 0;
        uint32_t thumbnail           = 0;
        uint32_t content_description = 0;
        uint32_t uri                 = 0;
        uint32_t json                = 0;
    };

    struct AuxCProp final {
        uint32_t index       = 0;  // 1-based ipco index
        AuxSemantic semantic = AuxSemantic::Unknown;
        std::array<char, 96> aux_type {};
        uint16_t aux_type_len = 0;
        std::array<std::byte, 32> aux_subtype {};
        uint16_t aux_subtype_len       = 0;
        uint16_t aux_subtype_total_len = 0;
        bool aux_subtype_truncated     = false;
    };

    struct AuxItemInfo final {
        uint32_t item_id     = 0;
        AuxSemantic semantic = AuxSemantic::Unknown;
        std::array<char, 96> aux_type {};
        uint16_t aux_type_len = 0;
        std::array<std::byte, 32> aux_subtype {};
        uint16_t aux_subtype_len       = 0;
        uint16_t aux_subtype_total_len = 0;
        bool aux_subtype_truncated     = false;
    };

    struct ItemRefEdge final {
        uint32_t ref_type     = 0;
        uint32_t from_item_id = 0;
        uint32_t to_item_id   = 0;
    };

    struct ItemGroup final {
        uint32_t group_type      = 0;
        uint32_t group_id        = 0;
        uint32_t entity_count    = 0;
        uint32_t entity_id_count = 0;
        bool entity_truncated    = false;
        bool contains_primary    = false;
        std::array<uint32_t, 64> entity_ids {};
    };

    struct ItemLocationExtent final {
        uint64_t index  = 0;
        uint64_t offset = 0;
        uint64_t length = 0;
    };

    struct ItemLocation final {
        uint32_t item_id              = 0;
        uint16_t construction_method  = 0;
        uint16_t data_reference_index = 0;
        uint64_t base_offset          = 0;
        uint32_t extent_count         = 0;
        uint32_t extent_record_count  = 0;
        uint64_t total_extent_bytes   = 0;
        bool extent_truncated         = false;
        bool length_overflow          = false;
        std::array<ItemLocationExtent, 16> extents {};
    };

    struct ItemPropertyAssociation final {
        uint32_t item_id        = 0;
        uint32_t property_index = 0;
        uint32_t property_type  = 0;
        uint8_t essential       = 0;
        bool have_property_type = false;
    };

    struct ItemInfo final {
        uint32_t item_id          = 0;
        uint16_t protection_index = 0;
        bool have_type            = false;
        uint32_t item_type        = 0;
        std::array<char, 96> name {};
        uint16_t name_len = 0;
        std::array<char, 96> content_type {};
        uint16_t content_type_len = 0;
        std::array<char, 96> content_encoding {};
        uint16_t content_encoding_len = 0;
        std::array<char, 96> uri_type {};
        uint16_t uri_type_len = 0;
    };


    struct IpcoSummary final {
        uint32_t property_count       = 0;
        uint32_t known_property_count = 0;
        uint32_t ispe_count           = 0;
        uint32_t irot_count           = 0;
        uint32_t imir_count           = 0;
        uint32_t colr_count           = 0;
        uint32_t auxc_count           = 0;
        uint32_t pasp_count           = 0;
        uint32_t pixi_count           = 0;
        uint32_t clap_count           = 0;
        uint32_t tilc_count           = 0;
        bool property_truncated       = false;
    };

    struct IpmaPropertyTypeCounts final {
        uint32_t property_type     = 0;
        const char* token          = "";
        uint32_t association_count = 0;
        uint32_t primary_count     = 0;
        uint32_t essential_count   = 0;
        bool have_any_association  = false;
    };


    struct PrimaryProps final {
        bool have_item_id = false;
        uint32_t item_id  = 0;

        bool have_width_height = false;
        uint32_t width         = 0;
        uint32_t height        = 0;

        bool have_rotation        = false;
        uint16_t rotation_degrees = 0;

        bool have_mirror = false;
        uint8_t mirror   = 0;

        bool have_pixel_aspect          = false;
        uint32_t pixel_aspect_h_spacing = 0;
        uint32_t pixel_aspect_v_spacing = 0;

        bool have_pixel_depth = false;
        std::array<uint8_t, 16> pixel_depth_bits_per_channel {};
        uint8_t pixel_depth_channel_count = 0;

        bool have_clean_aperture = false;
        ClapProp clean_aperture {};

        bool have_color                   = false;
        uint32_t color_type               = 0;
        bool have_nclx                    = false;
        uint16_t colour_primaries         = 0;
        uint16_t transfer_characteristics = 0;
        uint16_t matrix_coefficients      = 0;
        bool have_full_range_flag         = false;
        uint8_t full_range_flag           = 0;
        uint32_t color_profile_bytes      = 0;

        std::array<ItemRefEdge, 512> iref_edges {};
        uint32_t iref_edge_count = 0;
        uint32_t iref_edge_total = 0;
        bool iref_truncated      = false;

        std::array<ItemGroup, 64> item_groups {};
        uint32_t item_group_count = 0;
        uint32_t item_group_total = 0;
        bool item_group_truncated = false;

        std::array<ItemLocation, 128> item_locations {};
        uint32_t item_location_count           = 0;
        uint32_t item_location_total           = 0;
        bool item_location_truncated           = false;
        bool have_item_location_sizes          = false;
        uint8_t item_location_version          = 0;
        uint8_t item_location_offset_size      = 0;
        uint8_t item_location_length_size      = 0;
        uint8_t item_location_base_offset_size = 0;
        uint8_t item_location_index_size       = 0;
        bool have_idat                         = false;
        uint64_t idat_bytes                    = 0;
        uint64_t idat_payload_offset           = 0;
        uint64_t idat_payload_end              = 0;

        std::array<DataReferenceEntry, 128> data_references {};
        uint32_t data_reference_count  = 0U;
        uint32_t data_reference_total  = 0U;
        bool have_data_references      = false;
        bool data_references_valid     = false;
        bool data_references_truncated = false;

        std::array<ItemPropertyAssociation, 512> ipma_associations {};
        uint32_t ipma_association_count = 0;
        uint32_t ipma_association_total = 0;
        bool ipma_truncated             = false;

        bool have_ipco_summary = false;
        IpcoSummary ipco_summary {};

        std::array<IspeProp, 64> ipco_ispe {};
        uint32_t ipco_ispe_count = 0;
        std::array<TilCProp, 64> ipco_tilc {};
        uint32_t ipco_tilc_count = 0;
        std::array<IpcoProperty, 256> ipco_properties {};
        uint32_t ipco_property_count   = 0U;
        bool ipco_properties_truncated = false;

        std::array<ItemInfo, 256> item_infos {};
        uint32_t item_info_count = 0;

        std::array<uint32_t, 128> primary_auxl_item_ids {};
        uint32_t primary_auxl_count = 0;
        std::array<AuxSemantic, 128> primary_auxl_semantics {};
        std::array<uint32_t, 128> primary_alpha_item_ids {};
        uint32_t primary_alpha_count = 0;
        std::array<uint32_t, 128> primary_depth_item_ids {};
        uint32_t primary_depth_count = 0;
        std::array<uint32_t, 128> primary_disparity_item_ids {};
        uint32_t primary_disparity_count = 0;
        std::array<uint32_t, 128> primary_matte_item_ids {};
        uint32_t primary_matte_count = 0;

        std::array<uint32_t, 128> primary_dimg_item_ids {};
        uint32_t primary_dimg_count = 0;
        std::array<uint32_t, 128> primary_dimg_source_item_ids {};
        uint32_t primary_dimg_source_count = 0;
        std::array<uint32_t, 128> primary_thmb_item_ids {};
        uint32_t primary_thmb_count = 0;
        std::array<uint32_t, 128> primary_cdsc_item_ids {};
        uint32_t primary_cdsc_count = 0;

        std::array<AuxItemInfo, 256> aux_items {};
        uint32_t aux_item_count = 0;
    };

    static void push_primary_rel(std::span<uint32_t> out, uint32_t* io_count,
                                 uint32_t value) noexcept
    {
        if (!io_count) {
            return;
        }
        if (*io_count < out.size()) {
            out[*io_count] = value;
            *io_count += 1;
        }
    }

    static void push_primary_rel_unique(std::span<uint32_t> out,
                                        uint32_t* io_count,
                                        uint32_t value) noexcept
    {
        if (!io_count) {
            return;
        }
        for (uint32_t i = 0; i < *io_count && i < out.size(); ++i) {
            if (out[i] == value) {
                return;
            }
        }
        push_primary_rel(out, io_count, value);
    }

    static void bump_item_edge_count(std::span<uint32_t> item_ids,
                                     std::span<uint32_t> item_counts,
                                     uint32_t* io_count,
                                     uint32_t item_id) noexcept
    {
        if (!io_count) {
            return;
        }
        const uint32_t cap = static_cast<uint32_t>(
            std::min(item_ids.size(), item_counts.size()));
        for (uint32_t i = 0; i < *io_count && i < cap; ++i) {
            if (item_ids[i] == item_id) {
                item_counts[i] += 1U;
                return;
            }
        }
        if (*io_count < cap) {
            item_ids[*io_count]    = item_id;
            item_counts[*io_count] = 1U;
            *io_count += 1U;
        }
    }

    static void emit_iref_dynamic_summary(
        MetaStore& store, BlockId block, uint32_t* io_order, uint32_t ref_type,
        std::string_view rel_type, std::span<const ItemRefEdge> edges) noexcept
    {
        if (!io_order || rel_type.empty() || edges.empty()) {
            return;
        }

        std::array<uint32_t, 512> item_ids {};
        std::array<uint32_t, 512> item_out_edge_counts {};
        std::array<uint32_t, 512> item_in_edge_counts {};
        uint32_t item_count = 0;

        std::array<uint32_t, 512> from_ids {};
        std::array<uint32_t, 512> to_ids {};
        uint32_t from_count = 0;
        uint32_t to_count   = 0;
        uint32_t edge_count = 0;

        for (uint32_t i = 0; i < edges.size(); ++i) {
            if (edges[i].ref_type != ref_type) {
                continue;
            }
            edge_count += 1U;
            push_primary_rel_unique(from_ids, &from_count,
                                    edges[i].from_item_id);
            push_primary_rel_unique(to_ids, &to_count, edges[i].to_item_id);
            bump_item_edge_count(item_ids, item_out_edge_counts, &item_count,
                                 edges[i].from_item_id);
            bump_item_edge_count(item_ids, item_in_edge_counts, &item_count,
                                 edges[i].to_item_id);
        }

        if (edge_count == 0U) {
            return;
        }

        std::string field("iref.");
        field.append(rel_type);
        const size_t base_len = field.size();

        field.append(".edge_count");
        emit_u32_field(store, block, (*io_order)++, field, edge_count);
        field.resize(base_len);

        field.append(".from_item_unique_count");
        emit_u32_field(store, block, (*io_order)++, field, from_count);
        field.resize(base_len);

        field.append(".to_item_unique_count");
        emit_u32_field(store, block, (*io_order)++, field, to_count);

        emit_iref_typed_item_summary(store, block, io_order, rel_type, item_ids,
                                     item_out_edge_counts, item_in_edge_counts,
                                     item_count);
        emit_iref_typed_graph_summary(store, block, io_order, rel_type,
                                      edge_count, from_count, to_count);
    }

    static std::string_view item_group_semantic_name(uint32_t type) noexcept
    {
        if (type == fourcc('a', 'l', 't', 'r')) {
            return "alternatives";
        }
        if (type == fourcc('s', 't', 'e', 'r')) {
            return "stereo_pair";
        }
        if (type == fourcc('p', 'y', 'm', 'd')) {
            return "image_pyramid";
        }
        return "unclassified";
    }

    static std::string_view item_group_entity_role_name(uint32_t type,
                                                        uint32_t index) noexcept
    {
        if (type == fourcc('a', 'l', 't', 'r')) {
            return "alternative";
        }
        if (type == fourcc('s', 't', 'e', 'r')) {
            if (index == 0U) {
                return "left_image";
            }
            if (index == 1U) {
                return "right_image";
            }
            return "stereo_member";
        }
        if (type == fourcc('p', 'y', 'm', 'd')) {
            return "pyramid_layer";
        }
        return "member";
    }

    static void
    emit_item_group_type_summary(MetaStore& store, BlockId block,
                                 uint32_t* io_order, uint32_t group_type,
                                 std::string_view group_token,
                                 std::span<const ItemGroup> groups) noexcept
    {
        if (!io_order || group_token.empty() || groups.empty()) {
            return;
        }

        uint32_t group_count = 0U;
        for (uint32_t i = 0U; i < groups.size(); ++i) {
            if (groups[i].group_type == group_type) {
                group_count += 1U;
            }
        }
        if (group_count == 0U) {
            return;
        }

        std::string field("item_group.");
        field.append(group_token);
        const size_t base_len = field.size();

        field.append(".count");
        emit_u32_field(store, block, (*io_order)++, field, group_count);
        field.resize(base_len);

        for (uint32_t i = 0U; i < groups.size(); ++i) {
            const ItemGroup& group = groups[i];
            if (group.group_type != group_type) {
                continue;
            }

            field.append(".id");
            emit_u32_field(store, block, (*io_order)++, field, group.group_id);
            field.resize(base_len);

            field.append(".entity_count");
            emit_u32_field(store, block, (*io_order)++, field,
                           group.entity_count);
            field.resize(base_len);

            for (uint32_t j = 0U; j < group.entity_id_count; ++j) {
                field.append(".entity_index");
                emit_u32_field(store, block, (*io_order)++, field, j);
                field.resize(base_len);

                field.append(".entity_id");
                emit_u32_field(store, block, (*io_order)++, field,
                               group.entity_ids[j]);
                field.resize(base_len);

                field.append(".entity_role");
                emit_text_field(store, block, (*io_order)++, field,
                                item_group_entity_role_name(group.group_type,
                                                            j));
                field.resize(base_len);
            }
            if (group.entity_truncated) {
                field.append(".entity_truncated");
                emit_u8_field(store, block, (*io_order)++, field, 1U);
                field.resize(base_len);
            }
        }
    }

    static void emit_item_group_fields(MetaStore& store, BlockId block,
                                       uint32_t* io_order,
                                       const PrimaryProps& p) noexcept
    {
        if (!io_order || p.item_group_total == 0U) {
            return;
        }

        emit_u32_field(store, block, (*io_order)++, "item_group.count",
                       p.item_group_total);
        if (p.item_group_truncated) {
            emit_u8_field(store, block, (*io_order)++, "item_group.truncated",
                          1U);
        }

        std::array<uint32_t, 32> dynamic_group_types {};
        std::array<std::array<char, 5>, 32> dynamic_group_tokens {};
        uint32_t dynamic_group_type_count = 0U;
        uint32_t primary_group_count      = 0U;

        for (uint32_t i = 0U; i < p.item_group_count; ++i) {
            const ItemGroup& group = p.item_groups[i];

            emit_u32_field(store, block, (*io_order)++, "item_group.type",
                           group.group_type);
            emit_text_field(store, block, (*io_order)++, "item_group.type_name",
                            bmff_fourcc_display_name(group.group_type));
            emit_text_field(store, block, (*io_order)++, "item_group.semantic",
                            item_group_semantic_name(group.group_type));
            emit_u32_field(store, block, (*io_order)++, "item_group.id",
                           group.group_id);
            emit_u32_field(store, block, (*io_order)++,
                           "item_group.entity_count", group.entity_count);
            for (uint32_t j = 0U; j < group.entity_id_count; ++j) {
                emit_u32_field(store, block, (*io_order)++,
                               "item_group.entity_index", j);
                emit_u32_field(store, block, (*io_order)++,
                               "item_group.entity_id", group.entity_ids[j]);
                emit_text_field(store, block, (*io_order)++,
                                "item_group.entity_role",
                                item_group_entity_role_name(group.group_type,
                                                            j));
            }
            if (group.entity_truncated) {
                emit_u8_field(store, block, (*io_order)++,
                              "item_group.entity_truncated", 1U);
            }

            std::array<char, 5> token {};
            if (bmff_fourcc_field_token(group.group_type, &token)) {
                bool found_dynamic = false;
                for (uint32_t ti = 0U; ti < dynamic_group_type_count; ++ti) {
                    if (dynamic_group_types[ti] == group.group_type) {
                        found_dynamic = true;
                        break;
                    }
                }
                if (!found_dynamic
                    && dynamic_group_type_count < dynamic_group_types.size()) {
                    dynamic_group_types[dynamic_group_type_count]
                        = group.group_type;
                    dynamic_group_tokens[dynamic_group_type_count] = token;
                    dynamic_group_type_count += 1U;
                }
            }

            if (p.have_item_id && group.contains_primary) {
                primary_group_count += 1U;
            }
        }

        for (uint32_t ti = 0U; ti < dynamic_group_type_count; ++ti) {
            emit_item_group_type_summary(
                store, block, io_order, dynamic_group_types[ti],
                std::string_view(dynamic_group_tokens[ti].data(), 4U),
                std::span<const ItemGroup>(p.item_groups.data(),
                                           p.item_group_count));
        }

        if (!p.have_item_id || primary_group_count == 0U) {
            return;
        }

        emit_u32_field(store, block, (*io_order)++, "primary.item_group_count",
                       primary_group_count);
        for (uint32_t i = 0U; i < p.item_group_count; ++i) {
            const ItemGroup& group = p.item_groups[i];
            if (!group.contains_primary) {
                continue;
            }

            emit_u32_field(store, block, (*io_order)++,
                           "primary.item_group_type", group.group_type);
            emit_text_field(store, block, (*io_order)++,
                            "primary.item_group_type_name",
                            bmff_fourcc_display_name(group.group_type));
            emit_text_field(store, block, (*io_order)++,
                            "primary.item_group_semantic",
                            item_group_semantic_name(group.group_type));
            emit_u32_field(store, block, (*io_order)++, "primary.item_group_id",
                           group.group_id);
            emit_u32_field(store, block, (*io_order)++,
                           "primary.item_group_entity_count",
                           group.entity_count);
            for (uint32_t j = 0U; j < group.entity_id_count; ++j) {
                emit_u32_field(store, block, (*io_order)++,
                               "primary.item_group_entity_id",
                               group.entity_ids[j]);
                emit_u32_field(store, block, (*io_order)++,
                               "primary.item_group_entity_index", j);
                emit_text_field(store, block, (*io_order)++,
                                "primary.item_group_entity_role",
                                item_group_entity_role_name(group.group_type,
                                                            j));
                if (group.entity_ids[j] == p.item_id) {
                    emit_u32_field(store, block, (*io_order)++,
                                   "primary.item_group_primary_entity_index",
                                   j);
                    emit_text_field(store, block, (*io_order)++,
                                    "primary.item_group_primary_role",
                                    item_group_entity_role_name(group.group_type,
                                                                j));
                }
            }
            if (group.entity_count > 0U) {
                emit_u32_field(store, block, (*io_order)++,
                               "primary.item_group_other_entity_count",
                               group.entity_count - 1U);
            }
            if (group.entity_truncated) {
                emit_u8_field(store, block, (*io_order)++,
                              "primary.item_group_entity_truncated", 1U);
            }
        }
    }

    static void emit_ipco_summary_fields(MetaStore& store, BlockId block,
                                         uint32_t* io_order,
                                         const PrimaryProps& p) noexcept
    {
        if (!io_order || !p.have_ipco_summary) {
            return;
        }

        const IpcoSummary& s = p.ipco_summary;
        emit_u32_field(store, block, (*io_order)++, "ipco.property_count",
                       s.property_count);
        emit_u32_field(store, block, (*io_order)++, "ipco.known_property_count",
                       s.known_property_count);
        const uint32_t unknown_count
            = (s.property_count >= s.known_property_count)
                  ? (s.property_count - s.known_property_count)
                  : 0U;
        emit_u32_field(store, block, (*io_order)++,
                       "ipco.unknown_property_count", unknown_count);
        if (s.property_truncated) {
            emit_u8_field(store, block, (*io_order)++,
                          "ipco.property_truncated", 1U);
        }
        emit_count_field_if_nonzero(store, block, io_order, "ipco.ispe_count",
                                    s.ispe_count);
        emit_count_field_if_nonzero(store, block, io_order, "ipco.irot_count",
                                    s.irot_count);
        emit_count_field_if_nonzero(store, block, io_order, "ipco.imir_count",
                                    s.imir_count);
        emit_count_field_if_nonzero(store, block, io_order, "ipco.colr_count",
                                    s.colr_count);
        emit_count_field_if_nonzero(store, block, io_order, "ipco.auxC_count",
                                    s.auxc_count);
        emit_count_field_if_nonzero(store, block, io_order, "ipco.pasp_count",
                                    s.pasp_count);
        emit_count_field_if_nonzero(store, block, io_order, "ipco.pixi_count",
                                    s.pixi_count);
        emit_count_field_if_nonzero(store, block, io_order, "ipco.clap_count",
                                    s.clap_count);
        emit_count_field_if_nonzero(store, block, io_order, "ipco.tilC_count",
                                    s.tilc_count);
    }

    static void append_ipma_summary_count_field(MetaStore& store, BlockId block,
                                                uint32_t* io_order,
                                                const char* token,
                                                const char* suffix,
                                                uint32_t count) noexcept
    {
        if (!io_order || !token || !suffix || count == 0U) {
            return;
        }
        char field[80];
        const int n = std::snprintf(field, sizeof(field), "ipma.%s.%s", token,
                                    suffix);
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(field)) {
            return;
        }
        emit_u32_field(store, block, (*io_order)++, field, count);
    }

    static void
    emit_ipma_property_type_summary_fields(MetaStore& store, BlockId block,
                                           uint32_t* io_order,
                                           const PrimaryProps& p) noexcept
    {
        if (!io_order || p.ipma_association_count == 0U) {
            return;
        }

        IpmaPropertyTypeCounts counts[] = {
            { fourcc('i', 's', 'p', 'e'), "ispe", 0U, 0U, 0U, false },
            { fourcc('i', 'r', 'o', 't'), "irot", 0U, 0U, 0U, false },
            { fourcc('i', 'm', 'i', 'r'), "imir", 0U, 0U, 0U, false },
            { fourcc('c', 'o', 'l', 'r'), "colr", 0U, 0U, 0U, false },
            { fourcc('a', 'u', 'x', 'C'), "auxC", 0U, 0U, 0U, false },
            { fourcc('p', 'a', 's', 'p'), "pasp", 0U, 0U, 0U, false },
            { fourcc('p', 'i', 'x', 'i'), "pixi", 0U, 0U, 0U, false },
            { fourcc('c', 'l', 'a', 'p'), "clap", 0U, 0U, 0U, false },
            { fourcc('t', 'i', 'l', 'C'), "tilC", 0U, 0U, 0U, false },
        };

        for (uint32_t i = 0U; i < p.ipma_association_count; ++i) {
            const ItemPropertyAssociation& assoc = p.ipma_associations[i];
            if (!assoc.have_property_type) {
                continue;
            }
            const size_t counts_size = sizeof(counts) / sizeof(counts[0]);
            for (size_t j = 0U; j < counts_size; ++j) {
                IpmaPropertyTypeCounts& c = counts[j];
                if (c.property_type != assoc.property_type) {
                    continue;
                }
                c.have_any_association = true;
                c.association_count += 1U;
                if (p.have_item_id && assoc.item_id == p.item_id) {
                    c.primary_count += 1U;
                }
                if (assoc.essential != 0U) {
                    c.essential_count += 1U;
                }
                break;
            }
        }

        const size_t counts_size = sizeof(counts) / sizeof(counts[0]);
        for (size_t i = 0U; i < counts_size; ++i) {
            const IpmaPropertyTypeCounts& c = counts[i];
            if (!c.have_any_association) {
                continue;
            }
            append_ipma_summary_count_field(store, block, io_order, c.token,
                                            "association_count",
                                            c.association_count);
            append_ipma_summary_count_field(store, block, io_order, c.token,
                                            "primary_association_count",
                                            c.primary_count);
            append_ipma_summary_count_field(store, block, io_order, c.token,
                                            "essential_count",
                                            c.essential_count);
        }
    }

    static bool primary_rotation_swaps_dimensions(const PrimaryProps& p) noexcept
    {
        if (!p.have_rotation) {
            return false;
        }
        const uint16_t rotation = static_cast<uint16_t>(p.rotation_degrees
                                                        % 360U);
        return rotation == 90U || rotation == 270U;
    }

    static bool write_primary_transform_summary(const PrimaryProps& p,
                                                char* out,
                                                size_t out_size) noexcept
    {
        if (!out || out_size == 0U) {
            return false;
        }
        const uint16_t rotation
            = p.have_rotation ? static_cast<uint16_t>(p.rotation_degrees % 360U)
                              : 0U;
        int n = 0;
        if (p.have_rotation && p.have_mirror) {
            n = std::snprintf(out, out_size, "rotate_%u_mirror_%u",
                              static_cast<unsigned>(rotation),
                              static_cast<unsigned>(p.mirror));
        } else if (p.have_rotation) {
            n = std::snprintf(out, out_size, "rotate_%u",
                              static_cast<unsigned>(rotation));
        } else if (p.have_mirror) {
            n = std::snprintf(out, out_size, "mirror_%u",
                              static_cast<unsigned>(p.mirror));
        } else {
            n = std::snprintf(out, out_size, "identity");
        }
        return n > 0 && static_cast<size_t>(n) < out_size;
    }

    static void
    emit_primary_scene_summary_fields(MetaStore& store, BlockId block,
                                      uint32_t* io_order,
                                      const PrimaryProps& p) noexcept
    {
        if (!io_order) {
            return;
        }
        if (!p.have_width_height && !p.have_rotation && !p.have_mirror) {
            return;
        }

        char transform[48];
        if (write_primary_transform_summary(p, transform, sizeof(transform))) {
            emit_text_field(store, block, (*io_order)++,
                            "primary.transform_summary",
                            std::string_view(transform, std::strlen(transform)));
        }

        if (!p.have_width_height) {
            return;
        }

        const bool swap = primary_rotation_swaps_dimensions(p);
        emit_u32_field(store, block, (*io_order)++, "primary.display_width",
                       swap ? p.height : p.width);
        emit_u32_field(store, block, (*io_order)++, "primary.display_height",
                       swap ? p.width : p.height);
        emit_u8_field(store, block, (*io_order)++,
                      "primary.display_dimensions_swapped", swap ? 1U : 0U);
    }

    static const char*
    bmff_item_construction_method_name(uint16_t method) noexcept
    {
        switch (method) {
        case 0U: return "file_offset";
        case 1U: return "idat_offset";
        case 2U: return "item_offset";
        default: return "reserved";
        }
    }

    static const ItemLocation* find_item_location(const PrimaryProps& p,
                                                  uint32_t item_id) noexcept
    {
        for (uint32_t i = 0U; i < p.item_location_count; ++i) {
            if (p.item_locations[i].item_id == item_id) {
                return &p.item_locations[i];
            }
        }
        return nullptr;
    }

    static void emit_item_location_row(MetaStore& store, BlockId block,
                                       uint32_t* io_order,
                                       std::string_view prefix,
                                       const ItemLocation& loc) noexcept
    {
        if (!io_order || prefix.empty()) {
            return;
        }

        std::string field(prefix);
        const size_t base_len = field.size();

        field.append(".item_id");
        emit_u32_field(store, block, (*io_order)++, field, loc.item_id);
        field.resize(base_len);

        field.append(".construction_method");
        emit_u16_field(store, block, (*io_order)++, field,
                       loc.construction_method);
        field.resize(base_len);

        field.append(".construction_method_name");
        emit_text_field(store, block, (*io_order)++, field,
                        bmff_item_construction_method_name(
                            loc.construction_method));
        field.resize(base_len);

        field.append(".data_reference_index");
        emit_u16_field(store, block, (*io_order)++, field,
                       loc.data_reference_index);
        field.resize(base_len);

        field.append(".base_offset");
        emit_u64_field(store, block, (*io_order)++, field, loc.base_offset);
        field.resize(base_len);

        field.append(".extent_count");
        emit_u32_field(store, block, (*io_order)++, field, loc.extent_count);
        field.resize(base_len);

        field.append(".total_extent_bytes");
        emit_u64_field(store, block, (*io_order)++, field,
                       loc.total_extent_bytes);
        field.resize(base_len);

        if (loc.length_overflow) {
            field.append(".length_overflow");
            emit_u8_field(store, block, (*io_order)++, field, 1U);
            field.resize(base_len);
        }
        if (loc.extent_truncated) {
            field.append(".extent_truncated");
            emit_u8_field(store, block, (*io_order)++, field, 1U);
            field.resize(base_len);
        }

        for (uint32_t i = 0U; i < loc.extent_record_count; ++i) {
            const ItemLocationExtent& extent = loc.extents[i];
            field.append(".extent_index");
            emit_u64_field(store, block, (*io_order)++, field, extent.index);
            field.resize(base_len);

            field.append(".extent_offset");
            emit_u64_field(store, block, (*io_order)++, field, extent.offset);
            field.resize(base_len);

            field.append(".extent_length");
            emit_u64_field(store, block, (*io_order)++, field, extent.length);
            field.resize(base_len);
        }
    }

    static void emit_item_location_fields(MetaStore& store, BlockId block,
                                          uint32_t* io_order,
                                          const PrimaryProps& p) noexcept
    {
        if (!io_order) {
            return;
        }
        if (p.have_idat) {
            emit_u64_field(store, block, (*io_order)++, "idat.bytes",
                           p.idat_bytes);
        }
        if (p.item_location_total == 0U) {
            return;
        }

        emit_u32_field(store, block, (*io_order)++, "item_location.count",
                       p.item_location_total);
        if (p.item_location_truncated) {
            emit_u8_field(store, block, (*io_order)++,
                          "item_location.truncated", 1U);
        }
        if (p.have_item_location_sizes) {
            emit_u8_field(store, block, (*io_order)++, "item_location.version",
                          p.item_location_version);
            emit_u8_field(store, block, (*io_order)++,
                          "item_location.offset_size",
                          p.item_location_offset_size);
            emit_u8_field(store, block, (*io_order)++,
                          "item_location.length_size",
                          p.item_location_length_size);
            emit_u8_field(store, block, (*io_order)++,
                          "item_location.base_offset_size",
                          p.item_location_base_offset_size);
            emit_u8_field(store, block, (*io_order)++,
                          "item_location.index_size",
                          p.item_location_index_size);
        }

        uint32_t idat_item_count = 0U;
        for (uint32_t i = 0U; i < p.item_location_count; ++i) {
            const ItemLocation& loc = p.item_locations[i];
            emit_item_location_row(store, block, io_order, "item_location",
                                   loc);
            if (loc.construction_method == 1U) {
                idat_item_count += 1U;
            }
        }
        if (idat_item_count > 0U) {
            emit_u32_field(store, block, (*io_order)++,
                           "item_location.idat_item_count", idat_item_count);
        }

        if (!p.have_item_id) {
            return;
        }
        const ItemLocation* primary = find_item_location(p, p.item_id);
        if (!primary) {
            return;
        }
        emit_item_location_row(store, block, io_order, "primary.item_location",
                               *primary);
    }

    static uint8_t ascii_to_lower(uint8_t c) noexcept
    {
        if (c >= static_cast<uint8_t>('A') && c <= static_cast<uint8_t>('Z')) {
            return static_cast<uint8_t>(c + 0x20U);
        }
        return c;
    }

    static bool ascii_ieq(std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            const uint8_t ac = ascii_to_lower(static_cast<uint8_t>(a[i]));
            const uint8_t bc = ascii_to_lower(static_cast<uint8_t>(b[i]));
            if (ac != bc) {
                return false;
            }
        }
        return true;
    }

    static bool ascii_icontains(std::string_view hay,
                                std::string_view needle) noexcept
    {
        if (needle.empty()) {
            return true;
        }
        if (hay.size() < needle.size()) {
            return false;
        }
        const size_t stop = hay.size() - needle.size();
        for (size_t i = 0; i <= stop; ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                const uint8_t hc = ascii_to_lower(
                    static_cast<uint8_t>(hay[i + j]));
                const uint8_t nc = ascii_to_lower(
                    static_cast<uint8_t>(needle[j]));
                if (hc != nc) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        return false;
    }

    static std::string_view item_semantic_name(ItemSemantic semantic) noexcept
    {
        switch (semantic) {
        case ItemSemantic::Unknown: return "unknown";
        case ItemSemantic::Image: return "image";
        case ItemSemantic::Exif: return "exif";
        case ItemSemantic::Xmp: return "xmp";
        case ItemSemantic::Jumbf: return "jumbf";
        case ItemSemantic::C2pa: return "c2pa";
        case ItemSemantic::IccProfile: return "icc_profile";
        case ItemSemantic::Auxiliary: return "auxiliary";
        case ItemSemantic::DerivedImage: return "derived";
        case ItemSemantic::Thumbnail: return "thumbnail";
        case ItemSemantic::ContentDescription: return "content_description";
        case ItemSemantic::Uri: return "uri";
        case ItemSemantic::Json: return "json";
        }
        return "unknown";
    }

    static bool item_semantic_is_known(ItemSemantic semantic) noexcept
    {
        return semantic != ItemSemantic::Unknown;
    }

    static bool item_semantic_is_metadata(ItemSemantic semantic) noexcept
    {
        switch (semantic) {
        case ItemSemantic::Exif:
        case ItemSemantic::Xmp:
        case ItemSemantic::Jumbf:
        case ItemSemantic::C2pa:
        case ItemSemantic::IccProfile:
        case ItemSemantic::ContentDescription:
        case ItemSemantic::Uri:
        case ItemSemantic::Json: return true;
        case ItemSemantic::Unknown:
        case ItemSemantic::Image:
        case ItemSemantic::Auxiliary:
        case ItemSemantic::DerivedImage:
        case ItemSemantic::Thumbnail: return false;
        }
        return false;
    }

    static void count_item_semantic(ItemSemantic semantic,
                                    ItemSemanticCounts* out) noexcept
    {
        if (!out || !item_semantic_is_known(semantic)) {
            return;
        }
        out->known += 1U;
        if (item_semantic_is_metadata(semantic)) {
            out->metadata += 1U;
        }
        switch (semantic) {
        case ItemSemantic::Image: out->image += 1U; break;
        case ItemSemantic::Exif: out->exif += 1U; break;
        case ItemSemantic::Xmp: out->xmp += 1U; break;
        case ItemSemantic::Jumbf: out->jumbf += 1U; break;
        case ItemSemantic::C2pa: out->c2pa += 1U; break;
        case ItemSemantic::IccProfile: out->icc_profile += 1U; break;
        case ItemSemantic::Auxiliary: out->auxiliary += 1U; break;
        case ItemSemantic::DerivedImage: out->derived += 1U; break;
        case ItemSemantic::Thumbnail: out->thumbnail += 1U; break;
        case ItemSemantic::ContentDescription:
            out->content_description += 1U;
            break;
        case ItemSemantic::Uri: out->uri += 1U; break;
        case ItemSemantic::Json: out->json += 1U; break;
        case ItemSemantic::Unknown: break;
        }
    }

    static void emit_item_semantic_counts(MetaStore& store, BlockId block,
                                          uint32_t* io_order,
                                          const ItemSemanticCounts& counts)
    {
        if (!io_order || counts.known == 0U) {
            return;
        }
        emit_u32_field(store, block, (*io_order)++, "item.semantic_known_count",
                       counts.known);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_metadata_count",
                                    counts.metadata);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_image_count", counts.image);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_exif_count", counts.exif);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_xmp_count", counts.xmp);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_jumbf_count", counts.jumbf);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_c2pa_count", counts.c2pa);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_icc_profile_count",
                                    counts.icc_profile);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_auxiliary_count",
                                    counts.auxiliary);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_derived_count",
                                    counts.derived);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_thumbnail_count",
                                    counts.thumbnail);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_content_description_count",
                                    counts.content_description);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_uri_count", counts.uri);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "item.semantic_json_count", counts.json);
    }

    static bool item_content_type_matches(const ItemInfo& info,
                                          std::string_view needle) noexcept
    {
        if (info.content_type_len == 0U) {
            return false;
        }
        return ascii_ieq(std::string_view(info.content_type.data(),
                                          info.content_type_len),
                         needle);
    }

    static bool item_content_type_contains(const ItemInfo& info,
                                           std::string_view needle) noexcept
    {
        if (info.content_type_len == 0U) {
            return false;
        }
        return ascii_icontains(std::string_view(info.content_type.data(),
                                                info.content_type_len),
                               needle);
    }

    static bool item_content_type_starts_with(const ItemInfo& info,
                                              std::string_view prefix) noexcept
    {
        if (info.content_type_len < prefix.size()) {
            return false;
        }
        return ascii_ieq(std::string_view(info.content_type.data(),
                                          prefix.size()),
                         prefix);
    }

    static ItemSemantic classify_item_semantic(const ItemInfo& info) noexcept
    {
        if (info.have_type) {
            if (info.item_type == fourcc('E', 'x', 'i', 'f')) {
                return ItemSemantic::Exif;
            }
            if (info.item_type == fourcc('j', 'u', 'm', 'b')) {
                return ItemSemantic::Jumbf;
            }
            if (info.item_type == fourcc('a', 'u', 'x', 'l')) {
                return ItemSemantic::Auxiliary;
            }
            if (info.item_type == fourcc('d', 'e', 'r', 'v')
                || info.item_type == fourcc('g', 'r', 'i', 'd')
                || info.item_type == fourcc('i', 'o', 'v', 'l')
                || info.item_type == fourcc('i', 'd', 'e', 'n')) {
                return ItemSemantic::DerivedImage;
            }
            if (info.item_type == fourcc('t', 'h', 'm', 'b')) {
                return ItemSemantic::Thumbnail;
            }
            if (info.item_type == fourcc('c', 'd', 's', 'c')) {
                return ItemSemantic::ContentDescription;
            }
            if (info.item_type == fourcc('u', 'r', 'i', ' ')) {
                return ItemSemantic::Uri;
            }
            if (info.item_type == fourcc('h', 'v', 'c', '1')
                || info.item_type == fourcc('a', 'v', '0', '1')
                || info.item_type == fourcc('j', 'p', 'e', 'g')
                || info.item_type == fourcc('t', 'i', 'l', 'i')) {
                return ItemSemantic::Image;
            }
        }

        if (item_content_type_matches(info, "application/c2pa")
            || item_content_type_matches(info, "application/c2pa+jumbf")) {
            return ItemSemantic::C2pa;
        }
        if (item_content_type_matches(info, "application/jumbf")) {
            return ItemSemantic::Jumbf;
        }
        if (item_content_type_matches(info, "application/rdf+xml")
            || item_content_type_matches(info, "application/xmp+xml")
            || item_content_type_contains(info, "xmp")) {
            return ItemSemantic::Xmp;
        }
        if (item_content_type_matches(info, "application/vnd.iccprofile")
            || item_content_type_matches(info, "application/vnd.icc.profile")
            || item_content_type_contains(info, "icc")) {
            return ItemSemantic::IccProfile;
        }
        if (item_content_type_matches(info, "application/json")) {
            return ItemSemantic::Json;
        }
        if (item_content_type_starts_with(info, "image/")) {
            return ItemSemantic::Image;
        }
        return ItemSemantic::Unknown;
    }

    static ItemSemantic
    scene_relation_source_semantic(uint32_t ref_type) noexcept
    {
        if (ref_type == fourcc('a', 'u', 'x', 'l')) {
            return ItemSemantic::Auxiliary;
        }
        if (ref_type == fourcc('d', 'i', 'm', 'g')) {
            return ItemSemantic::DerivedImage;
        }
        if (ref_type == fourcc('t', 'h', 'm', 'b')) {
            return ItemSemantic::Thumbnail;
        }
        if (ref_type == fourcc('c', 'd', 's', 'c')) {
            return ItemSemantic::ContentDescription;
        }
        return ItemSemantic::Unknown;
    }

    static ItemSemantic
    classify_scene_item_semantic(const PrimaryProps& p,
                                 const ItemInfo& info) noexcept
    {
        const ItemSemantic base = classify_item_semantic(info);
        if (item_semantic_is_metadata(base)) {
            return base;
        }
        for (uint32_t i = 0U; i < p.iref_edge_count; ++i) {
            if (p.iref_edges[i].from_item_id != info.item_id) {
                continue;
            }
            const ItemSemantic relation_semantic
                = scene_relation_source_semantic(p.iref_edges[i].ref_type);
            if (relation_semantic != ItemSemantic::Unknown) {
                return relation_semantic;
            }
        }
        return base;
    }

    struct SceneGraphComponentStats final {
        uint32_t node_count                        = 0;
        uint32_t image_node_count                  = 0;
        uint32_t metadata_node_count               = 0;
        uint32_t content_bound_metadata_node_count = 0;
        uint32_t edge_count                        = 0;
        ItemSemanticCounts semantic_counts {};
        uint32_t auxiliary_edge_count           = 0;
        uint32_t alpha_edge_count               = 0;
        uint32_t depth_edge_count               = 0;
        uint32_t disparity_edge_count           = 0;
        uint32_t matte_edge_count               = 0;
        uint32_t derived_image_edge_count       = 0;
        uint32_t thumbnail_edge_count           = 0;
        uint32_t content_description_edge_count = 0;
        uint32_t other_edge_count               = 0;
        bool contains_primary                   = false;
    };

    static AuxSemantic find_aux_item_semantic(const PrimaryProps& out,
                                              uint32_t item_id) noexcept;

    static std::string_view scene_component_role_name(
        const SceneGraphComponentStats& component) noexcept
    {
        if (component.contains_primary) {
            return "primary_scene";
        }
        if (component.image_node_count != 0U) {
            return "image_scene";
        }
        if (component.metadata_node_count != 0U) {
            return "metadata_only";
        }
        return "unclassified";
    }

    static void emit_scene_component_summary_fields(
        MetaStore& store, BlockId block, uint32_t* io_order,
        std::span<const SceneGraphComponentStats> components,
        std::span<const uint32_t> item_ids,
        std::span<const uint32_t> component_indices) noexcept
    {
        if (!io_order) {
            return;
        }
        for (uint32_t c = 0U; c < components.size(); ++c) {
            const SceneGraphComponentStats& component = components[c];
            const uint8_t contains_primary = component.contains_primary ? 1U
                                                                        : 0U;
            const uint8_t has_content_bound_metadata
                = component.content_bound_metadata_node_count != 0U ? 1U : 0U;
            const uint8_t multi_image_candidate
                = component.image_node_count > 1U ? 1U : 0U;

            emit_u32_field(store, block, (*io_order)++, "scene.component.index",
                           c);
            emit_text_field(store, block, (*io_order)++, "scene.component.role",
                            scene_component_role_name(component));
            emit_u32_field(store, block, (*io_order)++,
                           "scene.component.node_count", component.node_count);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.known_node_count",
                                        component.semantic_counts.known);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.unknown_node_count",
                                        component.node_count
                                            - component.semantic_counts.known);
            emit_u32_field(store, block, (*io_order)++,
                           "scene.component.image_node_count",
                           component.image_node_count);
            emit_u32_field(store, block, (*io_order)++,
                           "scene.component.metadata_node_count",
                           component.metadata_node_count);
            emit_u32_field(store, block, (*io_order)++,
                           "scene.component.content_bound_metadata_node_count",
                           component.content_bound_metadata_node_count);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.exif_node_count",
                                        component.semantic_counts.exif);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.xmp_node_count",
                                        component.semantic_counts.xmp);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.jumbf_node_count",
                                        component.semantic_counts.jumbf);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.c2pa_node_count",
                                        component.semantic_counts.c2pa);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.icc_profile_node_count",
                                        component.semantic_counts.icc_profile);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.auxiliary_node_count",
                                        component.semantic_counts.auxiliary);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "scene.component.derived_image_node_count",
                component.semantic_counts.derived);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.thumbnail_node_count",
                                        component.semantic_counts.thumbnail);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "scene.component.content_description_node_count",
                component.semantic_counts.content_description);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.uri_node_count",
                                        component.semantic_counts.uri);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.json_node_count",
                                        component.semantic_counts.json);
            emit_u32_field(store, block, (*io_order)++,
                           "scene.component.edge_count", component.edge_count);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.auxiliary_edge_count",
                                        component.auxiliary_edge_count);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.alpha_edge_count",
                                        component.alpha_edge_count);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.depth_edge_count",
                                        component.depth_edge_count);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.disparity_edge_count",
                                        component.disparity_edge_count);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.matte_edge_count",
                                        component.matte_edge_count);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "scene.component.derived_image_edge_count",
                component.derived_image_edge_count);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.thumbnail_edge_count",
                                        component.thumbnail_edge_count);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "scene.component.content_description_edge_count",
                component.content_description_edge_count);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "scene.component.other_edge_count",
                                        component.other_edge_count);
            emit_u8_field(store, block, (*io_order)++,
                          "scene.component.contains_primary", contains_primary);
            emit_u8_field(store, block, (*io_order)++,
                          "scene.component.isolated",
                          component.node_count == 1U
                                  && component.edge_count == 0U
                              ? 1U
                              : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "scene.component.has_content_bound_metadata",
                          has_content_bound_metadata);
            emit_u8_field(store, block, (*io_order)++,
                          "scene.component.multi_image_candidate",
                          multi_image_candidate);
            if (has_content_bound_metadata != 0U) {
                emit_text_field(store, block, (*io_order)++,
                                "scene.component.metadata_policy",
                                "requires_target_rewrite");
            }
            if (multi_image_candidate != 0U) {
                emit_text_field(store, block, (*io_order)++,
                                "scene.component.multi_image_policy",
                                "requires_target_rewrite");
            }
            const uint32_t membership_count = static_cast<uint32_t>(
                std::min(item_ids.size(), component_indices.size()));
            for (uint32_t i = 0U; i < membership_count; ++i) {
                if (component_indices[i] == c) {
                    emit_u32_field(store, block, (*io_order)++,
                                   "scene.component.item_id", item_ids[i]);
                }
            }
        }
    }

    static uint32_t find_scene_node_index(std::span<const uint32_t> item_ids,
                                          uint32_t item_id) noexcept
    {
        for (uint32_t i = 0U; i < item_ids.size(); ++i) {
            if (item_ids[i] == item_id) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    static uint32_t scene_find_root(std::span<uint32_t> parent,
                                    uint32_t idx) noexcept
    {
        uint32_t root = idx;
        while (root < parent.size() && parent[root] != root) {
            root = parent[root];
        }
        while (idx < parent.size() && parent[idx] != idx) {
            const uint32_t next = parent[idx];
            parent[idx]         = root;
            idx                 = next;
        }
        return root;
    }

    static void scene_union(std::span<uint32_t> parent, uint32_t a,
                            uint32_t b) noexcept
    {
        const uint32_t root_a = scene_find_root(parent, a);
        const uint32_t root_b = scene_find_root(parent, b);
        if (root_a == root_b || root_a >= parent.size()
            || root_b >= parent.size()) {
            return;
        }
        if (root_a < root_b) {
            parent[root_b] = root_a;
        } else {
            parent[root_a] = root_b;
        }
    }

    static void push_scene_node(std::span<uint32_t> item_ids,
                                std::span<ItemSemantic> semantics,
                                std::span<uint32_t> parent, uint32_t* io_count,
                                uint32_t item_id,
                                ItemSemantic semantic) noexcept
    {
        if (!io_count || item_id == 0U) {
            return;
        }
        const uint32_t cap = static_cast<uint32_t>(
            std::min(item_ids.size(),
                     std::min(semantics.size(), parent.size())));
        for (uint32_t i = 0U; i < *io_count && i < cap; ++i) {
            if (item_ids[i] == item_id) {
                if (!item_semantic_is_known(semantics[i])
                    && item_semantic_is_known(semantic)) {
                    semantics[i] = semantic;
                }
                return;
            }
        }
        if (*io_count < cap) {
            item_ids[*io_count]  = item_id;
            semantics[*io_count] = semantic;
            parent[*io_count]    = *io_count;
            *io_count += 1U;
        }
    }

    static bool semantic_is_scene_image_node(ItemSemantic semantic) noexcept
    {
        return semantic == ItemSemantic::Image
               || semantic == ItemSemantic::Auxiliary
               || semantic == ItemSemantic::DerivedImage
               || semantic == ItemSemantic::Thumbnail;
    }

    static bool
    semantic_is_content_bound_metadata(ItemSemantic semantic) noexcept
    {
        return semantic == ItemSemantic::Jumbf
               || semantic == ItemSemantic::C2pa;
    }

    static void emit_scene_policy_summary_fields(MetaStore& store,
                                                 BlockId block,
                                                 uint32_t* io_order,
                                                 const PrimaryProps& p) noexcept
    {
        if (!io_order || p.item_info_count == 0U) {
            return;
        }

        ItemSemanticCounts counts {};
        for (uint32_t i = 0U; i < p.item_info_count; ++i) {
            count_item_semantic(classify_scene_item_semantic(p, p.item_infos[i]),
                                &counts);
        }

        const uint32_t image_node_count = counts.image + counts.auxiliary
                                          + counts.derived + counts.thumbnail;
        const uint32_t content_bound_metadata_count = counts.jumbf
                                                      + counts.c2pa;

        emit_u32_field(store, block, (*io_order)++, "scene.item_count",
                       p.item_info_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.known_item_count", counts.known);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.image_node_count", image_node_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.metadata_node_count",
                                    counts.metadata);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.content_bound_metadata_node_count",
                                    content_bound_metadata_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.auxiliary_node_count",
                                    counts.auxiliary);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.derived_image_node_count",
                                    counts.derived);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.thumbnail_node_count",
                                    counts.thumbnail);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.content_description_node_count",
                                    counts.content_description);
        emit_count_field_if_nonzero(store, block, io_order, "scene.edge_count",
                                    p.iref_edge_total);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.item_group_count",
                                    p.item_group_total);

        if (content_bound_metadata_count != 0U) {
            emit_u8_field(store, block, (*io_order)++,
                          "scene.has_content_bound_metadata", 1U);
            emit_text_field(store, block, (*io_order)++,
                            "scene.content_bound_metadata_policy",
                            "requires_target_rewrite");
        }
        if (image_node_count > 1U) {
            emit_u8_field(store, block, (*io_order)++,
                          "scene.multi_image_candidate", 1U);
            emit_text_field(store, block, (*io_order)++,
                            "scene.multi_image_policy",
                            "requires_target_rewrite");
        }

        std::array<uint32_t, 512> graph_item_ids {};
        std::array<ItemSemantic, 512> graph_semantics {};
        std::array<uint32_t, 512> graph_parent {};
        uint32_t graph_node_count = 0U;

        if (p.have_item_id) {
            push_scene_node(graph_item_ids, graph_semantics, graph_parent,
                            &graph_node_count, p.item_id,
                            ItemSemantic::Unknown);
        }
        for (uint32_t i = 0U; i < p.item_info_count; ++i) {
            push_scene_node(graph_item_ids, graph_semantics, graph_parent,
                            &graph_node_count, p.item_infos[i].item_id,
                            classify_scene_item_semantic(p, p.item_infos[i]));
        }
        for (uint32_t i = 0U; i < p.iref_edge_count; ++i) {
            push_scene_node(graph_item_ids, graph_semantics, graph_parent,
                            &graph_node_count, p.iref_edges[i].from_item_id,
                            scene_relation_source_semantic(
                                p.iref_edges[i].ref_type));
            push_scene_node(graph_item_ids, graph_semantics, graph_parent,
                            &graph_node_count, p.iref_edges[i].to_item_id,
                            ItemSemantic::Unknown);
        }

        if (graph_node_count == 0U) {
            return;
        }

        std::span<uint32_t> graph_ids(graph_item_ids.data(), graph_node_count);
        std::span<ItemSemantic> semantics(graph_semantics.data(),
                                          graph_node_count);
        std::span<uint32_t> parent(graph_parent.data(), graph_node_count);

        for (uint32_t i = 0U; i < p.iref_edge_count; ++i) {
            const uint32_t from_idx
                = find_scene_node_index(graph_ids,
                                        p.iref_edges[i].from_item_id);
            const uint32_t to_idx
                = find_scene_node_index(graph_ids, p.iref_edges[i].to_item_id);
            if (from_idx != UINT32_MAX && to_idx != UINT32_MAX) {
                scene_union(parent, from_idx, to_idx);
            }
        }

        std::array<uint32_t, 512> component_roots {};
        std::array<uint32_t, 512> graph_component_indices {};
        std::array<SceneGraphComponentStats, 512> components {};
        uint32_t component_count = 0U;
        for (uint32_t i = 0U; i < graph_node_count; ++i) {
            const uint32_t root      = scene_find_root(parent, i);
            uint32_t component_index = UINT32_MAX;
            for (uint32_t c = 0U; c < component_count; ++c) {
                if (component_roots[c] == root) {
                    component_index = c;
                    break;
                }
            }
            if (component_index == UINT32_MAX) {
                component_index                  = component_count;
                component_roots[component_count] = root;
                component_count += 1U;
            }
            graph_component_indices[i]          = component_index;
            SceneGraphComponentStats& component = components[component_index];
            component.node_count += 1U;
            count_item_semantic(semantics[i], &component.semantic_counts);
            if (semantic_is_scene_image_node(semantics[i])) {
                component.image_node_count += 1U;
            }
            if (item_semantic_is_metadata(semantics[i])) {
                component.metadata_node_count += 1U;
            }
            if (semantic_is_content_bound_metadata(semantics[i])) {
                component.content_bound_metadata_node_count += 1U;
            }
            if (p.have_item_id && graph_item_ids[i] == p.item_id) {
                component.contains_primary = true;
            }
        }

        for (uint32_t i = 0U; i < p.iref_edge_count; ++i) {
            const uint32_t from_idx
                = find_scene_node_index(graph_ids,
                                        p.iref_edges[i].from_item_id);
            if (from_idx == UINT32_MAX) {
                continue;
            }
            const uint32_t root = scene_find_root(parent, from_idx);
            for (uint32_t c = 0U; c < component_count; ++c) {
                if (component_roots[c] == root) {
                    SceneGraphComponentStats& component = components[c];
                    component.edge_count += 1U;
                    const uint32_t ref_type = p.iref_edges[i].ref_type;
                    if (ref_type == fourcc('a', 'u', 'x', 'l')) {
                        component.auxiliary_edge_count += 1U;
                        switch (find_aux_item_semantic(
                            p, p.iref_edges[i].from_item_id)) {
                        case AuxSemantic::Alpha:
                            component.alpha_edge_count += 1U;
                            break;
                        case AuxSemantic::Depth:
                            component.depth_edge_count += 1U;
                            break;
                        case AuxSemantic::Disparity:
                            component.disparity_edge_count += 1U;
                            break;
                        case AuxSemantic::Matte:
                            component.matte_edge_count += 1U;
                            break;
                        case AuxSemantic::Unknown: break;
                        }
                    } else if (ref_type == fourcc('d', 'i', 'm', 'g')) {
                        component.derived_image_edge_count += 1U;
                    } else if (ref_type == fourcc('t', 'h', 'm', 'b')) {
                        component.thumbnail_edge_count += 1U;
                    } else if (ref_type == fourcc('c', 'd', 's', 'c')) {
                        component.content_description_edge_count += 1U;
                    } else {
                        component.other_edge_count += 1U;
                    }
                    break;
                }
            }
        }

        uint32_t image_component_count                    = 0U;
        uint32_t multi_image_component_count              = 0U;
        uint32_t content_bound_metadata_component_count   = 0U;
        uint32_t isolated_image_node_count                = 0U;
        const SceneGraphComponentStats* primary_component = nullptr;
        for (uint32_t c = 0U; c < component_count; ++c) {
            const SceneGraphComponentStats& component = components[c];
            if (component.image_node_count != 0U) {
                image_component_count += 1U;
            }
            if (component.image_node_count > 1U) {
                multi_image_component_count += 1U;
            }
            if (component.content_bound_metadata_node_count != 0U) {
                content_bound_metadata_component_count += 1U;
            }
            if (component.node_count == 1U && component.image_node_count == 1U
                && component.edge_count == 0U) {
                isolated_image_node_count += 1U;
            }
            if (component.contains_primary) {
                primary_component = &component;
            }
        }

        emit_u32_field(store, block, (*io_order)++, "scene.graph_node_count",
                       graph_node_count);
        emit_u32_field(store, block, (*io_order)++,
                       "scene.graph_component_count", component_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.graph_image_component_count",
                                    image_component_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.graph_multi_image_component_count",
                                    multi_image_component_count);
        emit_count_field_if_nonzero(
            store, block, io_order,
            "scene.graph_content_bound_metadata_component_count",
            content_bound_metadata_component_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.graph_isolated_image_node_count",
                                    isolated_image_node_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "scene.graph_observed_edge_count",
                                    p.iref_edge_count);
        if (p.iref_truncated) {
            emit_u8_field(store, block, (*io_order)++,
                          "scene.graph_component_truncated", 1U);
        }

        emit_scene_component_summary_fields(
            store, block, io_order,
            std::span<const SceneGraphComponentStats>(components.data(),
                                                      component_count),
            graph_ids,
            std::span<const uint32_t>(graph_component_indices.data(),
                                      graph_node_count));

        if (primary_component) {
            emit_u32_field(store, block, (*io_order)++,
                           "scene.primary_graph_component_node_count",
                           primary_component->node_count);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "scene.primary_graph_component_image_node_count",
                primary_component->image_node_count);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "scene.primary_graph_component_metadata_node_count",
                primary_component->metadata_node_count);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "scene.primary_graph_component_content_bound_metadata_"
                "node_count",
                primary_component->content_bound_metadata_node_count);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "scene.primary_graph_component_edge_count",
                primary_component->edge_count);
            if (primary_component->content_bound_metadata_node_count != 0U) {
                emit_u8_field(
                    store, block, (*io_order)++,
                    "scene.primary_graph_component_has_content_bound_metadata",
                    1U);
                emit_text_field(store, block, (*io_order)++,
                                "scene.primary_graph_component_metadata_policy",
                                "requires_target_rewrite");
            }
            if (primary_component->image_node_count > 1U) {
                emit_u8_field(
                    store, block, (*io_order)++,
                    "scene.primary_graph_component_multi_image_candidate", 1U);
                emit_text_field(
                    store, block, (*io_order)++,
                    "scene.primary_graph_component_multi_image_policy",
                    "requires_target_rewrite");
            }
        }
    }

    static AuxSemantic classify_auxc_type(std::string_view aux_type) noexcept
    {
        if (aux_type.empty()) {
            return AuxSemantic::Unknown;
        }

        if (ascii_ieq(aux_type, "urn:mpeg:hevc:2015:auxid:1")
            || ascii_icontains(aux_type, ":aux:alpha")
            || ascii_ieq(aux_type,
                         "urn:mpeg:mpegb:cicp:systems:auxiliary:alpha")) {
            return AuxSemantic::Alpha;
        }
        if (ascii_ieq(aux_type, "urn:mpeg:hevc:2015:auxid:2")
            || ascii_icontains(aux_type, ":aux:depth")
            || ascii_icontains(aux_type, "depth")) {
            return AuxSemantic::Depth;
        }
        if (ascii_ieq(aux_type, "urn:mpeg:hevc:2015:auxid:3")
            || ascii_icontains(aux_type, ":aux:disparity")
            || ascii_icontains(aux_type, "disparity")) {
            return AuxSemantic::Disparity;
        }
        if (ascii_icontains(aux_type, "portraitmatte")
            || ascii_icontains(aux_type, ":aux:matte")
            || ascii_icontains(aux_type, "matte")) {
            return AuxSemantic::Matte;
        }
        return AuxSemantic::Unknown;
    }

    static std::string_view aux_semantic_name(AuxSemantic semantic) noexcept
    {
        switch (semantic) {
        case AuxSemantic::Unknown: return "unknown";
        case AuxSemantic::Alpha: return "alpha";
        case AuxSemantic::Depth: return "depth";
        case AuxSemantic::Disparity: return "disparity";
        case AuxSemantic::Matte: return "matte";
        }
        return "unknown";
    }

    static std::string_view
    primary_linked_role_name(PrimaryLinkedRole role) noexcept
    {
        switch (role) {
        case PrimaryLinkedRole::Auxiliary: return "auxiliary";
        case PrimaryLinkedRole::Alpha: return "alpha";
        case PrimaryLinkedRole::Depth: return "depth";
        case PrimaryLinkedRole::Disparity: return "disparity";
        case PrimaryLinkedRole::Matte: return "matte";
        case PrimaryLinkedRole::DerivedImage: return "derived";
        case PrimaryLinkedRole::Thumbnail: return "thumbnail";
        case PrimaryLinkedRole::ContentDescription:
            return "content_description";
        }
        return "auxiliary";
    }

    static PrimaryLinkedRole
    primary_linked_role_for_aux_semantic(AuxSemantic semantic) noexcept
    {
        switch (semantic) {
        case AuxSemantic::Alpha: return PrimaryLinkedRole::Alpha;
        case AuxSemantic::Depth: return PrimaryLinkedRole::Depth;
        case AuxSemantic::Disparity: return PrimaryLinkedRole::Disparity;
        case AuxSemantic::Matte: return PrimaryLinkedRole::Matte;
        case AuxSemantic::Unknown: break;
        }
        return PrimaryLinkedRole::Auxiliary;
    }

    static void push_primary_linked_role(std::span<uint32_t> item_ids,
                                         std::span<PrimaryLinkedRole> roles,
                                         uint32_t* io_count, uint32_t item_id,
                                         PrimaryLinkedRole role) noexcept
    {
        if (!io_count) {
            return;
        }
        const uint32_t cap = static_cast<uint32_t>(
            std::min(item_ids.size(), roles.size()));
        for (uint32_t i = 0; i < *io_count && i < cap; ++i) {
            if (item_ids[i] == item_id && roles[i] == role) {
                return;
            }
        }
        if (*io_count < cap) {
            item_ids[*io_count] = item_id;
            roles[*io_count]    = role;
            *io_count += 1U;
        }
    }

    static void count_scene_role_item(std::span<uint32_t> item_ids,
                                      uint32_t* io_node_count,
                                      uint32_t* io_edge_count,
                                      uint32_t item_id) noexcept
    {
        if (io_edge_count) {
            *io_edge_count += 1U;
        }
        push_primary_rel_unique(item_ids, io_node_count, item_id);
    }

    static void emit_primary_linked_item_roles(MetaStore& store, BlockId block,
                                               uint32_t* io_order,
                                               const PrimaryProps& p) noexcept
    {
        if (!io_order) {
            return;
        }

        std::array<uint32_t, 256> role_item_ids {};
        std::array<PrimaryLinkedRole, 256> roles {};
        uint32_t role_count = 0;

        for (uint32_t i = 0; i < p.primary_auxl_count; ++i) {
            push_primary_linked_role(role_item_ids, roles, &role_count,
                                     p.primary_auxl_item_ids[i],
                                     primary_linked_role_for_aux_semantic(
                                         p.primary_auxl_semantics[i]));
        }
        for (uint32_t i = 0; i < p.primary_dimg_count; ++i) {
            push_primary_linked_role(role_item_ids, roles, &role_count,
                                     p.primary_dimg_item_ids[i],
                                     PrimaryLinkedRole::DerivedImage);
        }
        for (uint32_t i = 0; i < p.primary_thmb_count; ++i) {
            push_primary_linked_role(role_item_ids, roles, &role_count,
                                     p.primary_thmb_item_ids[i],
                                     PrimaryLinkedRole::Thumbnail);
        }
        for (uint32_t i = 0; i < p.primary_cdsc_count; ++i) {
            push_primary_linked_role(role_item_ids, roles, &role_count,
                                     p.primary_cdsc_item_ids[i],
                                     PrimaryLinkedRole::ContentDescription);
        }

        if (role_count == 0U) {
            return;
        }

        emit_u32_field(store, block, (*io_order)++,
                       "primary.linked_item_role_count", role_count);
        std::array<uint32_t, 256> unique_linked_item_ids {};
        uint32_t unique_linked_item_count = 0U;
        for (uint32_t i = 0; i < role_count; ++i) {
            push_primary_rel_unique(unique_linked_item_ids,
                                    &unique_linked_item_count,
                                    role_item_ids[i]);
        }
        emit_u32_field(store, block, (*io_order)++, "primary.sidecar_count",
                       unique_linked_item_count);
        emit_u32_field(store, block, (*io_order)++,
                       "primary.scene_primary_item_count", 1U);
        emit_u32_field(store, block, (*io_order)++,
                       "primary.scene_linked_item_count",
                       unique_linked_item_count);
        emit_u32_field(store, block, (*io_order)++, "primary.scene_node_count",
                       1U + unique_linked_item_count);
        emit_u32_field(store, block, (*io_order)++, "primary.scene_edge_count",
                       role_count);

        uint32_t scene_auxiliary_node_count           = 0U;
        uint32_t scene_alpha_node_count               = 0U;
        uint32_t scene_depth_node_count               = 0U;
        uint32_t scene_disparity_node_count           = 0U;
        uint32_t scene_matte_node_count               = 0U;
        uint32_t scene_derived_image_node_count       = 0U;
        uint32_t scene_thumbnail_node_count           = 0U;
        uint32_t scene_content_description_node_count = 0U;
        uint32_t scene_auxiliary_edge_count           = 0U;
        uint32_t scene_alpha_edge_count               = 0U;
        uint32_t scene_depth_edge_count               = 0U;
        uint32_t scene_disparity_edge_count           = 0U;
        uint32_t scene_matte_edge_count               = 0U;
        uint32_t scene_derived_image_edge_count       = 0U;
        uint32_t scene_thumbnail_edge_count           = 0U;
        uint32_t scene_content_description_edge_count = 0U;
        std::array<uint32_t, 256> scene_auxiliary_node_ids {};
        std::array<uint32_t, 256> scene_alpha_node_ids {};
        std::array<uint32_t, 256> scene_depth_node_ids {};
        std::array<uint32_t, 256> scene_disparity_node_ids {};
        std::array<uint32_t, 256> scene_matte_node_ids {};
        std::array<uint32_t, 256> scene_derived_image_node_ids {};
        std::array<uint32_t, 256> scene_thumbnail_node_ids {};
        std::array<uint32_t, 256> scene_content_description_node_ids {};
        for (uint32_t i = 0; i < role_count; ++i) {
            switch (roles[i]) {
            case PrimaryLinkedRole::Auxiliary:
                count_scene_role_item(scene_auxiliary_node_ids,
                                      &scene_auxiliary_node_count,
                                      &scene_auxiliary_edge_count,
                                      role_item_ids[i]);
                break;
            case PrimaryLinkedRole::Alpha:
                count_scene_role_item(scene_alpha_node_ids,
                                      &scene_alpha_node_count,
                                      &scene_alpha_edge_count,
                                      role_item_ids[i]);
                break;
            case PrimaryLinkedRole::Depth:
                count_scene_role_item(scene_depth_node_ids,
                                      &scene_depth_node_count,
                                      &scene_depth_edge_count,
                                      role_item_ids[i]);
                break;
            case PrimaryLinkedRole::Disparity:
                count_scene_role_item(scene_disparity_node_ids,
                                      &scene_disparity_node_count,
                                      &scene_disparity_edge_count,
                                      role_item_ids[i]);
                break;
            case PrimaryLinkedRole::Matte:
                count_scene_role_item(scene_matte_node_ids,
                                      &scene_matte_node_count,
                                      &scene_matte_edge_count,
                                      role_item_ids[i]);
                break;
            case PrimaryLinkedRole::DerivedImage:
                count_scene_role_item(scene_derived_image_node_ids,
                                      &scene_derived_image_node_count,
                                      &scene_derived_image_edge_count,
                                      role_item_ids[i]);
                break;
            case PrimaryLinkedRole::Thumbnail:
                count_scene_role_item(scene_thumbnail_node_ids,
                                      &scene_thumbnail_node_count,
                                      &scene_thumbnail_edge_count,
                                      role_item_ids[i]);
                break;
            case PrimaryLinkedRole::ContentDescription:
                count_scene_role_item(scene_content_description_node_ids,
                                      &scene_content_description_node_count,
                                      &scene_content_description_edge_count,
                                      role_item_ids[i]);
                break;
            }
        }
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_auxiliary_node_count",
                                    scene_auxiliary_node_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_alpha_node_count",
                                    scene_alpha_node_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_depth_node_count",
                                    scene_depth_node_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_disparity_node_count",
                                    scene_disparity_node_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_matte_node_count",
                                    scene_matte_node_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_derived_image_node_count",
                                    scene_derived_image_node_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_thumbnail_node_count",
                                    scene_thumbnail_node_count);
        emit_count_field_if_nonzero(
            store, block, io_order,
            "primary.scene_content_description_node_count",
            scene_content_description_node_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_auxiliary_edge_count",
                                    scene_auxiliary_edge_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_alpha_edge_count",
                                    scene_alpha_edge_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_depth_edge_count",
                                    scene_depth_edge_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_disparity_edge_count",
                                    scene_disparity_edge_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_matte_edge_count",
                                    scene_matte_edge_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_derived_image_edge_count",
                                    scene_derived_image_edge_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.scene_thumbnail_edge_count",
                                    scene_thumbnail_edge_count);
        emit_count_field_if_nonzero(
            store, block, io_order,
            "primary.scene_content_description_edge_count",
            scene_content_description_edge_count);

        ItemSemanticCounts linked_semantic_counts {};
        for (uint32_t i = 0; i < unique_linked_item_count; ++i) {
            const ItemInfo* info = nullptr;
            for (uint32_t item_info_index = 0;
                 item_info_index < p.item_info_count; ++item_info_index) {
                if (p.item_infos[item_info_index].item_id
                    == unique_linked_item_ids[i]) {
                    info = &p.item_infos[item_info_index];
                    break;
                }
            }
            if (info) {
                count_item_semantic(classify_scene_item_semantic(p, *info),
                                    &linked_semantic_counts);
            }
        }
        const uint32_t image_sidecar_count = linked_semantic_counts.image
                                             + linked_semantic_counts.auxiliary
                                             + linked_semantic_counts.derived
                                             + linked_semantic_counts.thumbnail;
        if (linked_semantic_counts.metadata != 0U) {
            emit_u32_field(store, block, (*io_order)++,
                           "primary.scene_metadata_node_count",
                           linked_semantic_counts.metadata);
            emit_u8_field(store, block, (*io_order)++,
                          "primary.has_metadata_sidecar", 1U);
            emit_u32_field(store, block, (*io_order)++,
                           "primary.metadata_sidecar_count",
                           linked_semantic_counts.metadata);
        }
        const uint32_t content_bound_metadata_count
            = linked_semantic_counts.jumbf + linked_semantic_counts.c2pa;
        if (content_bound_metadata_count != 0U) {
            emit_u32_field(store, block, (*io_order)++,
                           "primary.scene_content_bound_metadata_node_count",
                           content_bound_metadata_count);
            emit_u8_field(store, block, (*io_order)++,
                          "primary.has_content_bound_metadata_sidecar", 1U);
            emit_u32_field(store, block, (*io_order)++,
                           "primary.content_bound_metadata_sidecar_count",
                           content_bound_metadata_count);
            emit_text_field(store, block, (*io_order)++,
                            "primary.content_bound_metadata_policy",
                            "requires_target_rewrite");
        }
        if (image_sidecar_count != 0U) {
            emit_u32_field(store, block, (*io_order)++,
                           "primary.scene_image_node_count",
                           image_sidecar_count);
            emit_u8_field(store, block, (*io_order)++,
                          "primary.has_image_sidecar", 1U);
            emit_u32_field(store, block, (*io_order)++,
                           "primary.image_sidecar_count", image_sidecar_count);
        }
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.exif_sidecar_count",
                                    linked_semantic_counts.exif);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.xmp_sidecar_count",
                                    linked_semantic_counts.xmp);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.jumbf_sidecar_count",
                                    linked_semantic_counts.jumbf);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.c2pa_sidecar_count",
                                    linked_semantic_counts.c2pa);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.icc_profile_sidecar_count",
                                    linked_semantic_counts.icc_profile);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.auxiliary_sidecar_count",
                                    linked_semantic_counts.auxiliary);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.derived_sidecar_count",
                                    linked_semantic_counts.derived);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.thumbnail_sidecar_count",
                                    linked_semantic_counts.thumbnail);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.content_description_sidecar_count",
                                    linked_semantic_counts.content_description);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.uri_sidecar_count",
                                    linked_semantic_counts.uri);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "primary.json_sidecar_count",
                                    linked_semantic_counts.json);
        if (linked_semantic_counts.known != 0U) {
            emit_u32_field(store, block, (*io_order)++,
                           "primary.linked_item_semantic_known_count",
                           linked_semantic_counts.known);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_metadata_count",
                linked_semantic_counts.metadata);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_image_count",
                linked_semantic_counts.image);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_exif_count",
                linked_semantic_counts.exif);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "primary.linked_item_semantic_xmp_count",
                                        linked_semantic_counts.xmp);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_jumbf_count",
                linked_semantic_counts.jumbf);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_c2pa_count",
                linked_semantic_counts.c2pa);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_icc_profile_count",
                linked_semantic_counts.icc_profile);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_auxiliary_count",
                linked_semantic_counts.auxiliary);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_derived_count",
                linked_semantic_counts.derived);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_thumbnail_count",
                linked_semantic_counts.thumbnail);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_content_description_count",
                linked_semantic_counts.content_description);
            emit_count_field_if_nonzero(store, block, io_order,
                                        "primary.linked_item_semantic_uri_count",
                                        linked_semantic_counts.uri);
            emit_count_field_if_nonzero(
                store, block, io_order,
                "primary.linked_item_semantic_json_count",
                linked_semantic_counts.json);
        }

        for (uint32_t i = 0; i < role_count; ++i) {
            emit_u32_field(store, block, (*io_order)++,
                           "primary.linked_item_id", role_item_ids[i]);
            const ItemInfo* info = nullptr;
            for (uint32_t item_info_index = 0;
                 item_info_index < p.item_info_count; ++item_info_index) {
                if (p.item_infos[item_info_index].item_id == role_item_ids[i]) {
                    info = &p.item_infos[item_info_index];
                    break;
                }
            }
            if (info) {
                if (info->have_type) {
                    emit_u32_field(store, block, (*io_order)++,
                                   "primary.linked_item_type", info->item_type);
                    emit_text_field(store, block, (*io_order)++,
                                    "primary.linked_item_type_name",
                                    bmff_fourcc_display_name(info->item_type));
                }
                if (info->name_len != 0U) {
                    emit_text_field(store, block, (*io_order)++,
                                    "primary.linked_item_name",
                                    std::string_view(info->name.data(),
                                                     info->name_len));
                }
                const ItemSemantic semantic = classify_item_semantic(*info);
                if (item_semantic_is_known(semantic)) {
                    emit_text_field(store, block, (*io_order)++,
                                    "primary.linked_item_semantic",
                                    item_semantic_name(semantic));
                }
            }
            emit_text_field(store, block, (*io_order)++,
                            "primary.linked_item_role",
                            primary_linked_role_name(roles[i]));
        }
    }

    static uint32_t count_aux_items_with_semantic(const PrimaryProps& out,
                                                  AuxSemantic semantic) noexcept
    {
        if (semantic == AuxSemantic::Unknown) {
            return 0U;
        }
        uint32_t count = 0U;
        for (uint32_t i = 0; i < out.aux_item_count; ++i) {
            if (out.aux_items[i].semantic == semantic) {
                count += 1U;
            }
        }
        return count;
    }

    static uint32_t find_primary_auxl_index(const PrimaryProps& out,
                                            uint32_t item_id) noexcept
    {
        for (uint32_t i = 0; i < out.primary_auxl_count; ++i) {
            if (out.primary_auxl_item_ids[i] == item_id) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    static uint32_t find_aux_item_index(const PrimaryProps& out,
                                        uint32_t item_id) noexcept
    {
        for (uint32_t i = 0; i < out.aux_item_count; ++i) {
            if (out.aux_items[i].item_id == item_id) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    static uint32_t find_item_info_index(const PrimaryProps& out,
                                         uint32_t item_id) noexcept
    {
        for (uint32_t i = 0; i < out.item_info_count; ++i) {
            if (out.item_infos[i].item_id == item_id) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    static const ItemInfo* find_item_info(const PrimaryProps& out,
                                          uint32_t item_id) noexcept
    {
        const uint32_t idx = find_item_info_index(out, item_id);
        if (idx == UINT32_MAX || idx >= out.item_infos.size()) {
            return nullptr;
        }
        return &out.item_infos[idx];
    }

    struct ItemDataCursor final {
        std::span<const std::byte> bytes;
        const PrimaryProps* props                  = nullptr;
        uint32_t item_id                           = 0U;
        uint16_t allowed_root_data_reference_index = 0U;
        uint64_t logical_offset                    = 0U;
        uint64_t logical_size                      = 0U;
        uint32_t reference_depth                   = 0U;
    };

    struct ItemResolveContext final {
        std::span<const std::byte> bytes;
        const PrimaryProps* props                  = nullptr;
        uint32_t* remaining_steps                  = nullptr;
        uint32_t root_item_id                      = 0U;
        uint16_t allowed_root_data_reference_index = 0U;
        std::array<uint32_t, 16> path {};
        uint32_t path_count = 0U;
    };

    static constexpr uint32_t kMaxItemResolveSteps = 4096U;

    static bool item_data_reference_allowed(const ItemResolveContext& context,
                                            const ItemLocation& location,
                                            uint32_t item_id) noexcept
    {
        return location.data_reference_index == 0U
               || (item_id == context.root_item_id
                   && context.allowed_root_data_reference_index != 0U
                   && location.data_reference_index
                          == context.allowed_root_data_reference_index);
    }

    static bool checked_add_u64(uint64_t a, uint64_t b, uint64_t* out) noexcept
    {
        if (!out || b > UINT64_MAX - a) {
            return false;
        }
        *out = a + b;
        return true;
    }

    static bool checked_mul_u64(uint64_t a, uint64_t b, uint64_t* out) noexcept
    {
        if (!out || (a != 0U && b > UINT64_MAX / a)) {
            return false;
        }
        *out = a * b;
        return true;
    }

    static bool enter_item_resolve(ItemResolveContext* context,
                                   uint32_t item_id,
                                   uint32_t* io_max_depth) noexcept
    {
        if (!context || !context->remaining_steps
            || *context->remaining_steps == 0U
            || context->path_count >= context->path.size()) {
            return false;
        }
        *context->remaining_steps -= 1U;
        for (uint32_t i = 0U; i < context->path_count; ++i) {
            if (context->path[i] == item_id) {
                return false;
            }
        }
        context->path[context->path_count] = item_id;
        context->path_count += 1U;
        if (io_max_depth && context->path_count > 1U) {
            *io_max_depth = std::max(*io_max_depth, context->path_count - 1U);
        }
        return true;
    }

    static bool get_iloc_reference_item(const PrimaryProps& p,
                                        uint32_t from_item_id,
                                        uint64_t reference_index,
                                        uint32_t* out_item_id) noexcept
    {
        if (!out_item_id || reference_index == 0U || p.iref_truncated) {
            return false;
        }
        uint64_t current = 0U;
        for (uint32_t i = 0U; i < p.iref_edge_count; ++i) {
            if (p.iref_edges[i].ref_type != fourcc('i', 'l', 'o', 'c')
                || p.iref_edges[i].from_item_id != from_item_id) {
                continue;
            }
            current += 1U;
            if (current == reference_index) {
                *out_item_id = p.iref_edges[i].to_item_id;
                return true;
            }
        }
        return false;
    }

    static bool item_data_size(ItemResolveContext context, uint32_t item_id,
                               uint64_t* out_size,
                               uint32_t* io_max_depth) noexcept;

    static bool item_extent_source_size(ItemResolveContext context,
                                        const ItemLocation& location,
                                        const ItemLocationExtent& extent,
                                        uint32_t extent_ordinal,
                                        uint64_t* out_source_size,
                                        uint32_t* out_source_item_id,
                                        uint32_t* io_max_depth) noexcept
    {
        if (!out_source_size || !context.props) {
            return false;
        }
        if (location.construction_method == 0U) {
            *out_source_size = context.bytes.size();
            return true;
        }
        if (location.construction_method == 1U) {
            if (!context.props->have_idat
                || context.props->idat_payload_offset
                       > context.props->idat_payload_end
                || context.props->idat_payload_end > context.bytes.size()) {
                return false;
            }
            *out_source_size = context.props->idat_payload_end
                               - context.props->idat_payload_offset;
            return true;
        }
        if (location.construction_method != 2U) {
            return false;
        }
        const uint64_t reference_index
            = context.props->item_location_index_size == 0U
                      && location.extent_record_count > 1U
                  ? static_cast<uint64_t>(extent_ordinal) + 1U
                  : (context.props->item_location_index_size == 0U
                         ? 1U
                         : extent.index);
        uint32_t source_item_id = 0U;
        if (!get_iloc_reference_item(*context.props, location.item_id,
                                     reference_index, &source_item_id)
            || !item_data_size(context, source_item_id, out_source_size,
                               io_max_depth)) {
            return false;
        }
        if (out_source_item_id) {
            *out_source_item_id = source_item_id;
        }
        return true;
    }

    static bool item_data_size(ItemResolveContext context, uint32_t item_id,
                               uint64_t* out_size,
                               uint32_t* io_max_depth) noexcept
    {
        if (!out_size || !context.props
            || !enter_item_resolve(&context, item_id, io_max_depth)) {
            return false;
        }
        const ItemLocation* location = find_item_location(*context.props,
                                                          item_id);
        if (!location
            || !item_data_reference_allowed(context, *location, item_id)
            || location->extent_truncated || location->length_overflow
            || location->extent_count == 0U
            || location->extent_count != location->extent_record_count
            || location->construction_method > 2U) {
            return false;
        }

        uint64_t total = 0U;
        for (uint32_t i = 0U; i < location->extent_record_count; ++i) {
            const ItemLocationExtent& extent = location->extents[i];
            uint64_t source_size             = 0U;
            if (!item_extent_source_size(context, *location, extent, i,
                                         &source_size, nullptr, io_max_depth)) {
                return false;
            }
            uint64_t source_offset = 0U;
            if (!checked_add_u64(location->base_offset, extent.offset,
                                 &source_offset)
                || source_offset > source_size) {
                return false;
            }
            uint64_t extent_length = extent.length;
            if (extent_length == 0U) {
                if (location->extent_record_count != 1U) {
                    return false;
                }
                extent_length = source_size - source_offset;
            } else if (extent_length > source_size - source_offset) {
                return false;
            }
            if (!checked_add_u64(total, extent_length, &total)) {
                return false;
            }
        }
        *out_size = total;
        return true;
    }

    static bool read_item_data_byte_at(ItemResolveContext context,
                                       uint32_t item_id,
                                       uint64_t logical_offset, uint8_t* out,
                                       uint32_t* io_max_depth) noexcept
    {
        if (!out || !context.props
            || !enter_item_resolve(&context, item_id, io_max_depth)) {
            return false;
        }
        const ItemLocation* location = find_item_location(*context.props,
                                                          item_id);
        if (!location
            || !item_data_reference_allowed(context, *location, item_id)
            || location->extent_truncated || location->length_overflow
            || location->extent_count == 0U
            || location->extent_count != location->extent_record_count
            || location->construction_method > 2U) {
            return false;
        }

        uint64_t remaining = logical_offset;
        for (uint32_t i = 0U; i < location->extent_record_count; ++i) {
            const ItemLocationExtent& extent = location->extents[i];
            uint64_t source_size             = 0U;
            uint32_t source_item_id          = 0U;
            if (!item_extent_source_size(context, *location, extent, i,
                                         &source_size, &source_item_id,
                                         io_max_depth)) {
                return false;
            }
            uint64_t source_offset = 0U;
            if (!checked_add_u64(location->base_offset, extent.offset,
                                 &source_offset)
                || source_offset > source_size) {
                return false;
            }
            uint64_t extent_length = extent.length;
            if (extent_length == 0U) {
                if (location->extent_record_count != 1U) {
                    return false;
                }
                extent_length = source_size - source_offset;
            } else if (extent_length > source_size - source_offset) {
                return false;
            }
            if (remaining >= extent_length) {
                remaining -= extent_length;
                continue;
            }
            if (!checked_add_u64(source_offset, remaining, &source_offset)) {
                return false;
            }
            if (location->construction_method == 0U) {
                if (source_offset >= context.bytes.size()) {
                    return false;
                }
                *out = u8(context.bytes[source_offset]);
                return true;
            }
            if (location->construction_method == 1U) {
                uint64_t absolute = 0U;
                if (!checked_add_u64(context.props->idat_payload_offset,
                                     source_offset, &absolute)
                    || absolute >= context.props->idat_payload_end
                    || absolute >= context.bytes.size()) {
                    return false;
                }
                *out = u8(context.bytes[absolute]);
                return true;
            }
            return read_item_data_byte_at(context, source_item_id,
                                          source_offset, out, io_max_depth);
        }
        return false;
    }

    static bool init_item_data_cursor_with_reference(
        std::span<const std::byte> bytes, const PrimaryProps& p,
        uint32_t item_id, uint16_t allowed_data_reference_index,
        ItemDataCursor* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out = ItemDataCursor {};
        ItemResolveContext context;
        uint32_t remaining_steps = kMaxItemResolveSteps;
        context.bytes            = bytes;
        context.props            = &p;
        context.remaining_steps  = &remaining_steps;
        context.root_item_id     = item_id;
        context.allowed_root_data_reference_index = allowed_data_reference_index;
        uint64_t logical_size    = 0U;
        uint32_t reference_depth = 0U;
        if (!item_data_size(context, item_id, &logical_size, &reference_depth)) {
            return false;
        }
        out->bytes                             = bytes;
        out->props                             = &p;
        out->item_id                           = item_id;
        out->allowed_root_data_reference_index = allowed_data_reference_index;
        out->logical_size                      = logical_size;
        out->reference_depth                   = reference_depth;
        return true;
    }

    static bool init_item_data_cursor(std::span<const std::byte> bytes,
                                      const PrimaryProps& p, uint32_t item_id,
                                      ItemDataCursor* out) noexcept
    {
        return init_item_data_cursor_with_reference(bytes, p, item_id, 0U, out);
    }

    static bool read_item_data_u8(ItemDataCursor* cursor, uint8_t* out) noexcept
    {
        if (!cursor || !out || !cursor->props
            || cursor->logical_offset >= cursor->logical_size) {
            return false;
        }
        ItemResolveContext context;
        uint32_t remaining_steps = kMaxItemResolveSteps;
        context.bytes            = cursor->bytes;
        context.props            = cursor->props;
        context.remaining_steps  = &remaining_steps;
        context.root_item_id     = cursor->item_id;
        context.allowed_root_data_reference_index
            = cursor->allowed_root_data_reference_index;
        if (!read_item_data_byte_at(context, cursor->item_id,
                                    cursor->logical_offset, out,
                                    &cursor->reference_depth)) {
            return false;
        }
        cursor->logical_offset += 1U;
        return true;
    }

    static bool read_item_data_u16(ItemDataCursor* cursor,
                                   uint16_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint8_t a = 0U;
        uint8_t b = 0U;
        if (!read_item_data_u8(cursor, &a) || !read_item_data_u8(cursor, &b)) {
            return false;
        }
        *out = static_cast<uint16_t>((static_cast<uint16_t>(a) << 8U)
                                     | static_cast<uint16_t>(b));
        return true;
    }

    static bool read_item_data_u32(ItemDataCursor* cursor,
                                   uint32_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint8_t value[4] {};
        for (uint32_t i = 0U; i < 4U; ++i) {
            if (!read_item_data_u8(cursor, &value[i])) {
                return false;
            }
        }
        *out = (static_cast<uint32_t>(value[0]) << 24U)
               | (static_cast<uint32_t>(value[1]) << 16U)
               | (static_cast<uint32_t>(value[2]) << 8U)
               | static_cast<uint32_t>(value[3]);
        return true;
    }

    static bool read_item_data_uint_n(ItemDataCursor* cursor,
                                      uint8_t byte_count,
                                      uint64_t* out) noexcept
    {
        if (!out || byte_count > 8U) {
            return false;
        }
        uint64_t value = 0U;
        for (uint8_t i = 0U; i < byte_count; ++i) {
            uint8_t byte = 0U;
            if (!read_item_data_u8(cursor, &byte)) {
                return false;
            }
            value = (value << 8U) | static_cast<uint64_t>(byte);
        }
        *out = value;
        return true;
    }

    static bool read_item_data_i16(ItemDataCursor* cursor,
                                   int32_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint16_t raw = 0U;
        if (!read_item_data_u16(cursor, &raw)) {
            return false;
        }
        *out = raw <= 0x7FFFU ? static_cast<int32_t>(raw)
                              : static_cast<int32_t>(raw) - 0x10000;
        return true;
    }

    static bool read_item_data_i32(ItemDataCursor* cursor,
                                   int32_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint32_t raw = 0U;
        if (!read_item_data_u32(cursor, &raw)) {
            return false;
        }
        const int64_t signed_value = raw <= 0x7FFFFFFFU
                                         ? static_cast<int64_t>(raw)
                                         : static_cast<int64_t>(raw)
                                               - INT64_C(0x100000000);
        *out                       = static_cast<int32_t>(signed_value);
        return true;
    }

    static uint32_t count_dimg_sources(const PrimaryProps& p,
                                       uint32_t item_id) noexcept
    {
        uint32_t count = 0U;
        for (uint32_t i = 0U; i < p.iref_edge_count; ++i) {
            if (p.iref_edges[i].ref_type == fourcc('d', 'i', 'm', 'g')
                && p.iref_edges[i].from_item_id == item_id) {
                count += 1U;
            }
        }
        return count;
    }

    static bool get_dimg_source(const PrimaryProps& p, uint32_t item_id,
                                uint32_t source_index,
                                uint32_t* out_source_item_id) noexcept
    {
        if (!out_source_item_id) {
            return false;
        }
        uint32_t index = 0U;
        for (uint32_t i = 0U; i < p.iref_edge_count; ++i) {
            if (p.iref_edges[i].ref_type != fourcc('d', 'i', 'm', 'g')
                || p.iref_edges[i].from_item_id != item_id) {
                continue;
            }
            if (index == source_index) {
                *out_source_item_id = p.iref_edges[i].to_item_id;
                return true;
            }
            index += 1U;
        }
        return false;
    }

    struct DerivedGraphValidation final {
        bool cycle                    = false;
        bool self_reference           = false;
        bool depth_exceeded           = false;
        bool references_truncated     = false;
        uint32_t missing_source_count = 0U;
        uint32_t max_depth            = 0U;
        std::array<uint32_t, 64> path {};
        uint32_t path_count = 0U;
        std::array<uint32_t, 256> visited {};
        uint32_t visited_count = 0U;
    };

    static bool contains_item_id(std::span<const uint32_t> item_ids,
                                 uint32_t item_id) noexcept
    {
        for (uint32_t i = 0U; i < item_ids.size(); ++i) {
            if (item_ids[i] == item_id) {
                return true;
            }
        }
        return false;
    }

    static void
    validate_derived_graph_node(const PrimaryProps& p, uint32_t item_id,
                                DerivedGraphValidation* out) noexcept
    {
        if (!out || out->cycle || out->depth_exceeded) {
            return;
        }
        const std::span<const uint32_t> path(out->path.data(), out->path_count);
        if (contains_item_id(path, item_id)) {
            out->cycle = true;
            return;
        }
        const std::span<const uint32_t> visited(out->visited.data(),
                                                out->visited_count);
        if (contains_item_id(visited, item_id)) {
            return;
        }
        if (out->path_count >= out->path.size()
            || out->visited_count >= out->visited.size()) {
            out->depth_exceeded = true;
            return;
        }

        out->path[out->path_count] = item_id;
        out->path_count += 1U;
        out->max_depth = std::max(out->max_depth, out->path_count - 1U);
        for (uint32_t i = 0U; i < p.iref_edge_count; ++i) {
            const ItemRefEdge& edge = p.iref_edges[i];
            if (edge.ref_type != fourcc('d', 'i', 'm', 'g')
                || edge.from_item_id != item_id) {
                continue;
            }
            if (edge.to_item_id == item_id) {
                out->self_reference = true;
            }
            if (!find_item_info(p, edge.to_item_id)) {
                out->missing_source_count += 1U;
                continue;
            }
            validate_derived_graph_node(p, edge.to_item_id, out);
        }
        out->path_count -= 1U;
        if (out->visited_count < out->visited.size()) {
            out->visited[out->visited_count] = item_id;
            out->visited_count += 1U;
        }
    }

    static DerivedGraphValidation
    validate_derived_graph(const PrimaryProps& p, uint32_t item_id) noexcept
    {
        DerivedGraphValidation out;
        out.references_truncated = p.iref_truncated;
        validate_derived_graph_node(p, item_id, &out);
        return out;
    }

    static bool
    derived_graph_is_valid(const DerivedGraphValidation& graph) noexcept
    {
        return !graph.cycle && !graph.self_reference && !graph.depth_exceeded
               && !graph.references_truncated
               && graph.missing_source_count == 0U;
    }

    struct GridDescriptor final {
        bool available           = false;
        bool have_header         = false;
        bool valid               = false;
        uint32_t reference_depth = 0U;
        uint8_t version          = 0U;
        uint8_t flags            = 0U;
        uint16_t rows            = 0U;
        uint16_t columns         = 0U;
        uint32_t output_width    = 0U;
        uint32_t output_height   = 0U;
    };

    static GridDescriptor
    parse_grid_descriptor(std::span<const std::byte> bytes,
                          const PrimaryProps& p, uint32_t item_id) noexcept
    {
        GridDescriptor out;
        ItemDataCursor cursor;
        if (!init_item_data_cursor(bytes, p, item_id, &cursor)) {
            return out;
        }
        out.available             = true;
        out.reference_depth       = cursor.reference_depth;
        uint8_t rows_minus_one    = 0U;
        uint8_t columns_minus_one = 0U;
        if (!read_item_data_u8(&cursor, &out.version)
            || !read_item_data_u8(&cursor, &out.flags)) {
            return out;
        }
        out.have_header = true;
        if (out.version != 0U || !read_item_data_u8(&cursor, &rows_minus_one)
            || !read_item_data_u8(&cursor, &columns_minus_one)) {
            return out;
        }
        out.rows    = static_cast<uint16_t>(rows_minus_one) + 1U;
        out.columns = static_cast<uint16_t>(columns_minus_one) + 1U;
        if ((out.flags & 1U) != 0U) {
            if (!read_item_data_u32(&cursor, &out.output_width)
                || !read_item_data_u32(&cursor, &out.output_height)) {
                return out;
            }
        } else {
            uint16_t width  = 0U;
            uint16_t height = 0U;
            if (!read_item_data_u16(&cursor, &width)
                || !read_item_data_u16(&cursor, &height)) {
                return out;
            }
            out.output_width  = width;
            out.output_height = height;
        }
        out.valid = out.output_width != 0U && out.output_height != 0U;
        return out;
    }

    struct OverlayDescriptor final {
        bool available           = false;
        bool have_header         = false;
        bool valid               = false;
        uint32_t reference_depth = 0U;
        uint8_t version          = 0U;
        uint8_t flags            = 0U;
        std::array<uint16_t, 4> background {};
        uint32_t output_width  = 0U;
        uint32_t output_height = 0U;
        std::array<int32_t, 512> offset_x {};
        std::array<int32_t, 512> offset_y {};
    };

    static OverlayDescriptor
    parse_overlay_descriptor(std::span<const std::byte> bytes,
                             const PrimaryProps& p, uint32_t item_id,
                             uint32_t source_count) noexcept
    {
        OverlayDescriptor out;
        ItemDataCursor cursor;
        if (!init_item_data_cursor(bytes, p, item_id, &cursor)) {
            return out;
        }
        out.available       = true;
        out.reference_depth = cursor.reference_depth;
        if (source_count == 0U || source_count > out.offset_x.size()) {
            return out;
        }
        if (!read_item_data_u8(&cursor, &out.version)
            || !read_item_data_u8(&cursor, &out.flags)) {
            return out;
        }
        out.have_header = true;
        if (out.version != 0U) {
            return out;
        }
        for (uint32_t i = 0U; i < out.background.size(); ++i) {
            if (!read_item_data_u16(&cursor, &out.background[i])) {
                return out;
            }
        }
        const bool long_fields = (out.flags & 1U) != 0U;
        if (long_fields) {
            if (!read_item_data_u32(&cursor, &out.output_width)
                || !read_item_data_u32(&cursor, &out.output_height)) {
                return out;
            }
        } else {
            uint16_t width  = 0U;
            uint16_t height = 0U;
            if (!read_item_data_u16(&cursor, &width)
                || !read_item_data_u16(&cursor, &height)) {
                return out;
            }
            out.output_width  = width;
            out.output_height = height;
        }
        for (uint32_t i = 0U; i < source_count; ++i) {
            const bool ok
                = long_fields
                      ? (read_item_data_i32(&cursor, &out.offset_x[i])
                         && read_item_data_i32(&cursor, &out.offset_y[i]))
                      : (read_item_data_i16(&cursor, &out.offset_x[i])
                         && read_item_data_i16(&cursor, &out.offset_y[i]));
            if (!ok) {
                return out;
            }
        }
        out.valid = out.output_width != 0U && out.output_height != 0U;
        return out;
    }

    static std::string_view derived_construction_name(uint32_t type) noexcept
    {
        if (type == fourcc('g', 'r', 'i', 'd')) {
            return "grid";
        }
        if (type == fourcc('i', 'o', 'v', 'l')) {
            return "overlay";
        }
        if (type == fourcc('i', 'd', 'e', 'n')) {
            return "identity";
        }
        return {};
    }

    static void emit_derived_image_fields(MetaStore& store, BlockId block,
                                          uint32_t* io_order,
                                          std::span<const std::byte> bytes,
                                          const PrimaryProps& p) noexcept
    {
        if (!io_order) {
            return;
        }
        uint32_t item_count     = 0U;
        uint32_t grid_count     = 0U;
        uint32_t overlay_count  = 0U;
        uint32_t identity_count = 0U;
        for (uint32_t i = 0U; i < p.item_info_count; ++i) {
            const ItemInfo& info = p.item_infos[i];
            if (!info.have_type
                || derived_construction_name(info.item_type).empty()) {
                continue;
            }
            item_count += 1U;
            if (info.item_type == fourcc('g', 'r', 'i', 'd')) {
                grid_count += 1U;
            } else if (info.item_type == fourcc('i', 'o', 'v', 'l')) {
                overlay_count += 1U;
            } else {
                identity_count += 1U;
            }
        }
        if (item_count == 0U) {
            return;
        }
        emit_u32_field(store, block, (*io_order)++, "derived_image.count",
                       item_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "derived_image.grid_count", grid_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "derived_image.overlay_count",
                                    overlay_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "derived_image.identity_count",
                                    identity_count);

        uint32_t graph_invalid_count             = 0U;
        uint32_t graph_cycle_count               = 0U;
        uint32_t graph_missing_source_count      = 0U;
        uint32_t graph_self_reference_count      = 0U;
        uint32_t graph_depth_exceeded_count      = 0U;
        uint32_t graph_reference_truncated_count = 0U;
        for (uint32_t i = 0U; i < p.item_info_count; ++i) {
            const ItemInfo& info = p.item_infos[i];
            if (!info.have_type) {
                continue;
            }
            const std::string_view construction = derived_construction_name(
                info.item_type);
            if (construction.empty()) {
                continue;
            }
            const uint32_t source_count = count_dimg_sources(p, info.item_id);
            const DerivedGraphValidation graph
                = validate_derived_graph(p, info.item_id);
            const bool graph_valid = derived_graph_is_valid(graph);
            if (!graph_valid) {
                graph_invalid_count += 1U;
            }
            if (graph.cycle) {
                graph_cycle_count += 1U;
            }
            if (graph.missing_source_count != 0U) {
                graph_missing_source_count += graph.missing_source_count;
            }
            if (graph.self_reference) {
                graph_self_reference_count += 1U;
            }
            if (graph.depth_exceeded) {
                graph_depth_exceeded_count += 1U;
            }
            if (graph.references_truncated) {
                graph_reference_truncated_count += 1U;
            }
            emit_u32_field(store, block, (*io_order)++, "derived_image.item_id",
                           info.item_id);
            emit_u32_field(store, block, (*io_order)++, "derived_image.type",
                           info.item_type);
            emit_text_field(store, block, (*io_order)++,
                            "derived_image.type_name",
                            bmff_fourcc_display_name(info.item_type));
            emit_text_field(store, block, (*io_order)++,
                            "derived_image.construction", construction);
            emit_u32_field(store, block, (*io_order)++,
                           "derived_image.source_count", source_count);
            emit_u32_field(store, block, (*io_order)++,
                           "derived_image.graph_max_depth", graph.max_depth);
            emit_u32_field(store, block, (*io_order)++,
                           "derived_image.graph_missing_source_count",
                           graph.missing_source_count);
            emit_u8_field(store, block, (*io_order)++,
                          "derived_image.graph_cycle", graph.cycle ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "derived_image.graph_self_reference",
                          graph.self_reference ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "derived_image.graph_depth_exceeded",
                          graph.depth_exceeded ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "derived_image.graph_references_truncated",
                          graph.references_truncated ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "derived_image.graph_valid", graph_valid ? 1U : 0U);
            for (uint32_t source_i = 0U; source_i < source_count; ++source_i) {
                uint32_t source_id = 0U;
                if (!get_dimg_source(p, info.item_id, source_i, &source_id)) {
                    continue;
                }
                emit_u32_field(store, block, (*io_order)++,
                               "derived_image.source_index", source_i);
                emit_u32_field(store, block, (*io_order)++,
                               "derived_image.source_item_id", source_id);
            }

            bool descriptor_required            = true;
            bool descriptor_available           = false;
            bool descriptor_valid               = false;
            bool source_count_valid             = false;
            bool construction_valid             = false;
            uint32_t output_width               = 0U;
            uint32_t output_height              = 0U;
            uint16_t grid_rows                  = 0U;
            uint16_t grid_columns               = 0U;
            uint32_t descriptor_reference_depth = 0U;

            if (info.item_type == fourcc('g', 'r', 'i', 'd')) {
                const GridDescriptor grid  = parse_grid_descriptor(bytes, p,
                                                                   info.item_id);
                descriptor_available       = grid.available;
                descriptor_valid           = grid.valid;
                descriptor_reference_depth = grid.reference_depth;
                const uint32_t expected_sources
                    = static_cast<uint32_t>(grid.rows)
                      * static_cast<uint32_t>(grid.columns);
                source_count_valid = grid.valid
                                     && source_count == expected_sources;
                construction_valid = descriptor_valid && source_count_valid;
                output_width       = grid.output_width;
                output_height      = grid.output_height;
                grid_rows          = grid.rows;
                grid_columns       = grid.columns;
                emit_u32_field(store, block, (*io_order)++,
                               "derived_image.grid.item_id", info.item_id);
                if (grid.have_header) {
                    emit_u8_field(store, block, (*io_order)++,
                                  "derived_image.grid.descriptor_version",
                                  grid.version);
                    emit_u8_field(store, block, (*io_order)++,
                                  "derived_image.grid.descriptor_flags",
                                  grid.flags);
                    emit_u8_field(store, block, (*io_order)++,
                                  "derived_image.grid.field_bits",
                                  (grid.flags & 1U) != 0U ? 32U : 16U);
                }
                if (grid.valid) {
                    emit_u16_field(store, block, (*io_order)++,
                                   "derived_image.grid.rows", grid.rows);
                    emit_u16_field(store, block, (*io_order)++,
                                   "derived_image.grid.columns", grid.columns);
                    emit_u32_field(store, block, (*io_order)++,
                                   "derived_image.grid.output_width",
                                   grid.output_width);
                    emit_u32_field(store, block, (*io_order)++,
                                   "derived_image.grid.output_height",
                                   grid.output_height);
                    emit_u32_field(store, block, (*io_order)++,
                                   "derived_image.grid.expected_source_count",
                                   expected_sources);
                    for (uint32_t source_i = 0U; source_i < source_count;
                         ++source_i) {
                        uint32_t source_id = 0U;
                        if (!get_dimg_source(p, info.item_id, source_i,
                                             &source_id)) {
                            continue;
                        }
                        emit_u32_field(store, block, (*io_order)++,
                                       "derived_image.grid.source_index",
                                       source_i);
                        emit_u32_field(store, block, (*io_order)++,
                                       "derived_image.grid.source_item_id",
                                       source_id);
                        const bool source_in_grid = source_i < expected_sources;
                        emit_u8_field(store, block, (*io_order)++,
                                      "derived_image.grid.source_in_grid",
                                      source_in_grid ? 1U : 0U);
                        if (!source_in_grid) {
                            continue;
                        }
                        emit_u32_field(store, block, (*io_order)++,
                                       "derived_image.grid.source_row",
                                       source_i / grid.columns);
                        emit_u32_field(store, block, (*io_order)++,
                                       "derived_image.grid.source_column",
                                       source_i % grid.columns);
                    }
                }
            } else if (info.item_type == fourcc('i', 'o', 'v', 'l')) {
                const OverlayDescriptor overlay
                    = parse_overlay_descriptor(bytes, p, info.item_id,
                                               source_count);
                descriptor_available       = overlay.available;
                descriptor_valid           = overlay.valid;
                descriptor_reference_depth = overlay.reference_depth;
                source_count_valid         = source_count > 0U;
                construction_valid = descriptor_valid && source_count_valid;
                output_width       = overlay.output_width;
                output_height      = overlay.output_height;
                emit_u32_field(store, block, (*io_order)++,
                               "derived_image.overlay.item_id", info.item_id);
                if (overlay.have_header) {
                    emit_u8_field(store, block, (*io_order)++,
                                  "derived_image.overlay.descriptor_version",
                                  overlay.version);
                    emit_u8_field(store, block, (*io_order)++,
                                  "derived_image.overlay.descriptor_flags",
                                  overlay.flags);
                    emit_u8_field(store, block, (*io_order)++,
                                  "derived_image.overlay.field_bits",
                                  (overlay.flags & 1U) != 0U ? 32U : 16U);
                }
                if (overlay.valid) {
                    emit_u32_field(store, block, (*io_order)++,
                                   "derived_image.overlay.output_width",
                                   overlay.output_width);
                    emit_u32_field(store, block, (*io_order)++,
                                   "derived_image.overlay.output_height",
                                   overlay.output_height);
                    static constexpr const char* kBackgroundFields[4] = {
                        "derived_image.overlay.background_r",
                        "derived_image.overlay.background_g",
                        "derived_image.overlay.background_b",
                        "derived_image.overlay.background_a",
                    };
                    for (uint32_t channel = 0U; channel < 4U; ++channel) {
                        emit_u16_field(store, block, (*io_order)++,
                                       kBackgroundFields[channel],
                                       overlay.background[channel]);
                    }
                    for (uint32_t source_i = 0U; source_i < source_count;
                         ++source_i) {
                        uint32_t source_id = 0U;
                        if (!get_dimg_source(p, info.item_id, source_i,
                                             &source_id)) {
                            continue;
                        }
                        emit_u32_field(store, block, (*io_order)++,
                                       "derived_image.overlay.source_index",
                                       source_i);
                        emit_u32_field(store, block, (*io_order)++,
                                       "derived_image.overlay.source_item_id",
                                       source_id);
                        emit_i32_field(store, block, (*io_order)++,
                                       "derived_image.overlay.offset_x",
                                       overlay.offset_x[source_i]);
                        emit_i32_field(store, block, (*io_order)++,
                                       "derived_image.overlay.offset_y",
                                       overlay.offset_y[source_i]);
                    }
                }
            } else {
                descriptor_required    = false;
                descriptor_valid       = true;
                source_count_valid     = source_count == 1U;
                uint32_t source_id     = 0U;
                const bool have_source = get_dimg_source(p, info.item_id, 0U,
                                                         &source_id);
                construction_valid     = source_count_valid && have_source
                                     && source_id != info.item_id;
                emit_u32_field(store, block, (*io_order)++,
                               "derived_image.identity.item_id", info.item_id);
                if (have_source) {
                    emit_u32_field(store, block, (*io_order)++,
                                   "derived_image.identity.source_item_id",
                                   source_id);
                }
            }

            construction_valid = construction_valid && graph_valid;

            emit_u8_field(store, block, (*io_order)++,
                          "derived_image.descriptor_required",
                          descriptor_required ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "derived_image.descriptor_available",
                          descriptor_available ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "derived_image.descriptor_valid",
                          descriptor_valid ? 1U : 0U);
            if (descriptor_available) {
                emit_u32_field(store, block, (*io_order)++,
                               "derived_image.descriptor_reference_depth",
                               descriptor_reference_depth);
            }
            emit_u8_field(store, block, (*io_order)++,
                          "derived_image.source_count_valid",
                          source_count_valid ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "derived_image.construction_valid",
                          construction_valid ? 1U : 0U);

            if (p.have_item_id && info.item_id == p.item_id) {
                emit_text_field(store, block, (*io_order)++,
                                "primary.derived_construction", construction);
                emit_u8_field(store, block, (*io_order)++,
                              "primary.derived_descriptor_valid",
                              descriptor_valid ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "primary.derived_source_count_valid",
                              source_count_valid ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "primary.derived_construction_valid",
                              construction_valid ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "primary.derived_graph_valid",
                              graph_valid ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "primary.derived_graph_cycle",
                              graph.cycle ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "primary.derived_graph_self_reference",
                              graph.self_reference ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "primary.derived_graph_depth_exceeded",
                              graph.depth_exceeded ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "primary.derived_graph_references_truncated",
                              graph.references_truncated ? 1U : 0U);
                emit_u32_field(store, block, (*io_order)++,
                               "primary.derived_graph_max_depth",
                               graph.max_depth);
                emit_u32_field(store, block, (*io_order)++,
                               "primary.derived_graph_missing_source_count",
                               graph.missing_source_count);
                if (descriptor_valid
                    && info.item_type != fourcc('i', 'd', 'e', 'n')) {
                    emit_u32_field(store, block, (*io_order)++,
                                   "primary.derived_output_width",
                                   output_width);
                    emit_u32_field(store, block, (*io_order)++,
                                   "primary.derived_output_height",
                                   output_height);
                }
                if (info.item_type == fourcc('g', 'r', 'i', 'd')) {
                    if (descriptor_valid) {
                        emit_u16_field(store, block, (*io_order)++,
                                       "primary.derived_grid_rows", grid_rows);
                        emit_u16_field(store, block, (*io_order)++,
                                       "primary.derived_grid_columns",
                                       grid_columns);
                    }
                }
            }
        }
        emit_count_field_if_nonzero(store, block, io_order,
                                    "derived_image.graph_invalid_count",
                                    graph_invalid_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "derived_image.graph_cycle_count",
                                    graph_cycle_count);
        emit_count_field_if_nonzero(
            store, block, io_order,
            "derived_image.graph_missing_source_count_total",
            graph_missing_source_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "derived_image.graph_self_reference_count",
                                    graph_self_reference_count);
        emit_count_field_if_nonzero(store, block, io_order,
                                    "derived_image.graph_depth_exceeded_count",
                                    graph_depth_exceeded_count);
        emit_count_field_if_nonzero(
            store, block, io_order,
            "derived_image.graph_references_truncated_count",
            graph_reference_truncated_count);
    }

    static const AuxItemInfo* find_aux_item_info(const PrimaryProps& out,
                                                 uint32_t item_id) noexcept
    {
        const uint32_t idx = find_aux_item_index(out, item_id);
        if (idx == UINT32_MAX || idx >= out.aux_items.size()) {
            return nullptr;
        }
        return &out.aux_items[idx];
    }

    static AuxSemantic find_aux_item_semantic(const PrimaryProps& out,
                                              uint32_t item_id) noexcept
    {
        const AuxItemInfo* info = find_aux_item_info(out, item_id);
        if (!info) {
            return AuxSemantic::Unknown;
        }
        return info->semantic;
    }

    static uint32_t upsert_aux_item(PrimaryProps* out,
                                    uint32_t item_id) noexcept
    {
        if (!out) {
            return UINT32_MAX;
        }
        uint32_t idx = find_aux_item_index(*out, item_id);
        if (idx == UINT32_MAX) {
            if (out->aux_item_count >= out->aux_items.size()) {
                return UINT32_MAX;
            }
            idx                         = out->aux_item_count;
            out->aux_items[idx]         = AuxItemInfo {};
            out->aux_items[idx].item_id = item_id;
            out->aux_item_count += 1;
        }
        return idx;
    }

    static void set_aux_item_semantic(PrimaryProps* out, uint32_t item_id,
                                      AuxSemantic semantic) noexcept
    {
        if (!out || semantic == AuxSemantic::Unknown) {
            return;
        }
        const uint32_t idx = upsert_aux_item(out, item_id);
        if (idx == UINT32_MAX || idx >= out->aux_items.size()) {
            return;
        }
        if (out->aux_items[idx].semantic == AuxSemantic::Unknown) {
            out->aux_items[idx].semantic = semantic;
        }
    }

    static void set_aux_item_type(PrimaryProps* out, uint32_t item_id,
                                  std::string_view aux_type) noexcept
    {
        if (!out || aux_type.empty()) {
            return;
        }
        const uint32_t idx = upsert_aux_item(out, item_id);
        if (idx == UINT32_MAX || idx >= out->aux_items.size()) {
            return;
        }
        AuxItemInfo& info = out->aux_items[idx];
        if (info.aux_type_len != 0) {
            return;
        }
        const size_t max_copy = (aux_type.size() < info.aux_type.size())
                                    ? aux_type.size()
                                    : info.aux_type.size();
        for (size_t i = 0; i < max_copy; ++i) {
            info.aux_type[i] = aux_type[i];
        }
        info.aux_type_len = static_cast<uint16_t>(max_copy);
    }

    static void set_aux_item_subtype(PrimaryProps* out, uint32_t item_id,
                                     std::span<const std::byte> subtype,
                                     uint16_t total_len,
                                     bool truncated) noexcept
    {
        if (!out) {
            return;
        }
        const uint32_t idx = upsert_aux_item(out, item_id);
        if (idx == UINT32_MAX || idx >= out->aux_items.size()) {
            return;
        }
        AuxItemInfo& info = out->aux_items[idx];
        if (info.aux_subtype_total_len != 0) {
            return;
        }
        const size_t max_copy = (subtype.size() < info.aux_subtype.size())
                                    ? subtype.size()
                                    : info.aux_subtype.size();
        for (size_t i = 0; i < max_copy; ++i) {
            info.aux_subtype[i] = subtype[i];
        }
        info.aux_subtype_len       = static_cast<uint16_t>(max_copy);
        info.aux_subtype_total_len = total_len;
        info.aux_subtype_truncated = truncated;
    }

    static char hex_digit(uint8_t v) noexcept
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        return kHex[v & 0x0F];
    }

    static bool
    bytes_are_printable_ascii(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.empty()) {
            return false;
        }
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint8_t c = u8(bytes[i]);
            if (c < 0x20U || c > 0x7EU) {
                return false;
            }
        }
        return true;
    }

    static std::string
    bytes_to_hex_string(std::span<const std::byte> bytes) noexcept
    {
        std::string out;
        out.reserve(bytes.size() * 2U + 2U);
        out.push_back('0');
        out.push_back('x');
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint8_t b = u8(bytes[i]);
            out.push_back(hex_digit(static_cast<uint8_t>(b >> 4)));
            out.push_back(hex_digit(b));
        }
        return out;
    }

    static std::string
    bytes16_to_uuid_string(std::span<const std::byte> bytes) noexcept
    {
        std::string out;
        out.reserve(36U);
        for (size_t i = 0; i < 16; ++i) {
            const uint8_t b = u8(bytes[i]);
            out.push_back(hex_digit(static_cast<uint8_t>(b >> 4)));
            out.push_back(hex_digit(b));
            if (i == 3U || i == 5U || i == 7U || i == 9U) {
                out.push_back('-');
            }
        }
        return out;
    }

    struct AuxSubtypeInterpretation final {
        std::string_view kind;
        bool has_u32  = false;
        uint32_t u32  = 0;
        bool has_u64  = false;
        uint64_t u64  = 0;
        bool has_text = false;
        std::array<char, 80> text {};
        uint16_t text_len = 0;
    };

    static AuxSubtypeInterpretation
    interpret_aux_subtype(std::span<const std::byte> subtype,
                          uint16_t total_len, bool truncated) noexcept
    {
        AuxSubtypeInterpretation out {};
        if (total_len == 0U) {
            out.kind = "none";
            return out;
        }
        if (subtype.empty()) {
            out.kind = "bytes";
            return out;
        }

        if (total_len == 1U && subtype.size() >= 1U) {
            out.kind    = "u8";
            out.has_u32 = true;
            out.u32     = static_cast<uint32_t>(u8(subtype[0]));
            return out;
        }
        if (total_len == 2U && subtype.size() >= 2U) {
            out.kind    = "u16be";
            out.has_u32 = true;
            out.u32     = (static_cast<uint32_t>(u8(subtype[0])) << 8)
                      | static_cast<uint32_t>(u8(subtype[1]));
            return out;
        }
        if (total_len == 4U && subtype.size() >= 4U) {
            if (bytes_are_printable_ascii(subtype.first(4U))) {
                out.kind     = "fourcc";
                out.has_text = true;
                for (size_t i = 0; i < 4U; ++i) {
                    out.text[i] = static_cast<char>(u8(subtype[i]));
                }
                out.text_len = 4U;
                return out;
            }
            out.kind    = "u32be";
            out.has_u32 = true;
            out.u32     = (static_cast<uint32_t>(u8(subtype[0])) << 24)
                      | (static_cast<uint32_t>(u8(subtype[1])) << 16)
                      | (static_cast<uint32_t>(u8(subtype[2])) << 8)
                      | static_cast<uint32_t>(u8(subtype[3]));
            return out;
        }
        if (!truncated && static_cast<size_t>(total_len) == subtype.size()
            && total_len >= 2U
            && subtype[static_cast<size_t>(total_len) - 1U]
                   == std::byte { 0x00 }
            && bytes_are_printable_ascii(
                subtype.first(static_cast<size_t>(total_len) - 1U))) {
            out.kind       = "ascii_z";
            const size_t n = ((static_cast<size_t>(total_len) - 1U)
                              < out.text.size())
                                 ? (static_cast<size_t>(total_len) - 1U)
                                 : out.text.size();
            for (size_t i = 0; i < n; ++i) {
                out.text[i] = static_cast<char>(u8(subtype[i]));
            }
            out.text_len = static_cast<uint16_t>(n);
            out.has_text = (n != 0U);
            return out;
        }
        if (total_len == 8U && subtype.size() >= 8U) {
            if (bytes_are_printable_ascii(subtype.first(8U))) {
                out.kind     = "ascii";
                out.has_text = true;
                for (size_t i = 0; i < 8U; ++i) {
                    out.text[i] = static_cast<char>(u8(subtype[i]));
                }
                out.text_len = 8U;
                return out;
            }
            out.kind    = "u64be";
            out.has_u64 = true;
            out.u64     = (static_cast<uint64_t>(u8(subtype[0])) << 56)
                      | (static_cast<uint64_t>(u8(subtype[1])) << 48)
                      | (static_cast<uint64_t>(u8(subtype[2])) << 40)
                      | (static_cast<uint64_t>(u8(subtype[3])) << 32)
                      | (static_cast<uint64_t>(u8(subtype[4])) << 24)
                      | (static_cast<uint64_t>(u8(subtype[5])) << 16)
                      | (static_cast<uint64_t>(u8(subtype[6])) << 8)
                      | static_cast<uint64_t>(u8(subtype[7]));
            return out;
        }
        if (total_len == 16U && subtype.size() >= 16U) {
            out.kind               = "uuid";
            const std::string uuid = bytes16_to_uuid_string(subtype.first(16U));
            const size_t n = (uuid.size() < out.text.size()) ? uuid.size()
                                                             : out.text.size();
            for (size_t i = 0; i < n; ++i) {
                out.text[i] = uuid[i];
            }
            out.text_len = static_cast<uint16_t>(n);
            out.has_text = (n != 0U);
            return out;
        }

        if (!truncated && static_cast<size_t>(total_len) == subtype.size()
            && bytes_are_printable_ascii(subtype)) {
            out.kind       = "ascii";
            const size_t n = (subtype.size() < out.text.size())
                                 ? subtype.size()
                                 : out.text.size();
            for (size_t i = 0; i < n; ++i) {
                out.text[i] = static_cast<char>(u8(subtype[i]));
            }
            out.text_len = static_cast<uint16_t>(n);
            out.has_text = (n != 0U);
            return out;
        }

        out.kind = "bytes";
        return out;
    }

    static void set_primary_auxl_semantic(PrimaryProps* out, uint32_t item_id,
                                          AuxSemantic semantic) noexcept
    {
        if (!out || semantic == AuxSemantic::Unknown) {
            return;
        }
        const uint32_t idx = find_primary_auxl_index(*out, item_id);
        if (idx == UINT32_MAX || idx >= out->primary_auxl_semantics.size()) {
            return;
        }
        if (out->primary_auxl_semantics[idx] != AuxSemantic::Unknown) {
            return;
        }
        out->primary_auxl_semantics[idx] = semantic;
        switch (semantic) {
        case AuxSemantic::Alpha:
            push_primary_rel_unique(out->primary_alpha_item_ids,
                                    &out->primary_alpha_count, item_id);
            break;
        case AuxSemantic::Depth:
            push_primary_rel_unique(out->primary_depth_item_ids,
                                    &out->primary_depth_count, item_id);
            break;
        case AuxSemantic::Disparity:
            push_primary_rel_unique(out->primary_disparity_item_ids,
                                    &out->primary_disparity_count, item_id);
            break;
        case AuxSemantic::Matte:
            push_primary_rel_unique(out->primary_matte_item_ids,
                                    &out->primary_matte_count, item_id);
            break;
        case AuxSemantic::Unknown: break;
        }
    }

    static void add_primary_item_ref(PrimaryProps* out, uint32_t ref_type,
                                     uint32_t referring_item_id) noexcept
    {
        if (!out) {
            return;
        }
        if (ref_type == fourcc('a', 'u', 'x', 'l')) {
            push_primary_rel(out->primary_auxl_item_ids,
                             &out->primary_auxl_count, referring_item_id);
        } else if (ref_type == fourcc('d', 'i', 'm', 'g')) {
            push_primary_rel(out->primary_dimg_item_ids,
                             &out->primary_dimg_count, referring_item_id);
        } else if (ref_type == fourcc('t', 'h', 'm', 'b')) {
            push_primary_rel(out->primary_thmb_item_ids,
                             &out->primary_thmb_count, referring_item_id);
        } else if (ref_type == fourcc('c', 'd', 's', 'c')) {
            push_primary_rel(out->primary_cdsc_item_ids,
                             &out->primary_cdsc_count, referring_item_id);
        }
    }

    static void add_primary_dimg_source(PrimaryProps* out,
                                        uint32_t source_item_id) noexcept
    {
        if (!out) {
            return;
        }
        push_primary_rel(out->primary_dimg_source_item_ids,
                         &out->primary_dimg_source_count, source_item_id);
    }

    static bool append_iref_edge(PrimaryProps* out, uint32_t ref_type,
                                 uint32_t from_item_id,
                                 uint32_t to_item_id) noexcept
    {
        if (!out) {
            return false;
        }
        if (out->iref_edge_total == UINT32_MAX) {
            return false;
        }
        out->iref_edge_total += 1;
        if (out->iref_edge_count < out->iref_edges.size()) {
            out->iref_edges[out->iref_edge_count]
                = ItemRefEdge { ref_type, from_item_id, to_item_id };
            out->iref_edge_count += 1;
        } else {
            out->iref_truncated = true;
        }

        if (out->have_item_id && to_item_id == out->item_id) {
            add_primary_item_ref(out, ref_type, from_item_id);
        }
        if (out->have_item_id && from_item_id == out->item_id
            && ref_type == fourcc('d', 'i', 'm', 'g')) {
            add_primary_dimg_source(out, to_item_id);
        }
        return true;
    }

    static bool bmff_parse_pitm(std::span<const std::byte> bytes,
                                const BmffBox& pitm,
                                uint32_t* out_item_id) noexcept
    {
        if (!out_item_id) {
            return false;
        }
        const uint64_t payload_off  = pitm.offset + pitm.header_size;
        const uint64_t payload_size = pitm.size - pitm.header_size;
        if (payload_size < 6) {
            return false;
        }

        const uint8_t version = u8(bytes[payload_off + 0]);
        if (version == 0) {
            uint16_t id16 = 0;
            if (!read_u16be(bytes, payload_off + 4, &id16)) {
                return false;
            }
            *out_item_id = static_cast<uint32_t>(id16);
            return true;
        }
        if (version == 1) {
            if (payload_size < 8) {
                return false;
            }
            uint32_t id32 = 0;
            if (!read_u32be(bytes, payload_off + 4, &id32)) {
                return false;
            }
            *out_item_id = id32;
            return true;
        }

        return false;
    }

    static bool bmff_read_cstr(std::span<const std::byte> bytes,
                               uint64_t* io_off, uint64_t end, char* out,
                               size_t out_cap, uint16_t* out_len) noexcept
    {
        if (!io_off || !out || !out_len || out_cap == 0U) {
            return false;
        }
        if (*io_off > end || end > bytes.size()) {
            return false;
        }
        uint64_t p = *io_off;
        while (p < end && bytes[p] != std::byte { 0x00 }) {
            p += 1;
        }
        if (p >= end) {
            return false;
        }
        const size_t len = static_cast<size_t>(p - *io_off);
        const size_t n   = (len < out_cap) ? len : out_cap;
        for (size_t i = 0; i < n; ++i) {
            out[i] = static_cast<char>(u8(bytes[*io_off + i]));
        }
        *out_len = static_cast<uint16_t>(n);
        *io_off  = p + 1;
        return true;
    }

    static bool bmff_parse_infe(std::span<const std::byte> bytes,
                                const BmffBox& infe, ItemInfo* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out = ItemInfo {};

        const uint64_t payload_off  = infe.offset + infe.header_size;
        const uint64_t payload_size = infe.size - infe.header_size;
        const uint64_t payload_end  = payload_off + payload_size;
        if (payload_size < 4 || payload_end > bytes.size()) {
            return false;
        }

        const uint8_t version = u8(bytes[payload_off + 0]);
        uint64_t p            = payload_off + 4;

        if (version <= 1U) {
            uint16_t item_id16 = 0;
            uint16_t prot16    = 0;
            if (!read_u16be(bytes, p, &item_id16)
                || !read_u16be(bytes, p + 2, &prot16)) {
                return false;
            }
            out->item_id          = static_cast<uint32_t>(item_id16);
            out->protection_index = prot16;
            p += 4;
            if (!bmff_read_cstr(bytes, &p, payload_end, out->name.data(),
                                out->name.size(), &out->name_len)) {
                return false;
            }
            if (!bmff_read_cstr(bytes, &p, payload_end,
                                out->content_type.data(),
                                out->content_type.size(),
                                &out->content_type_len)) {
                return false;
            }
            if (!bmff_read_cstr(bytes, &p, payload_end,
                                out->content_encoding.data(),
                                out->content_encoding.size(),
                                &out->content_encoding_len)) {
                return false;
            }
            if (out->content_type_len != 0U) {
                out->have_type = true;
                out->item_type = fourcc('m', 'i', 'm', 'e');
            }
            return true;
        }

        if (version == 2U) {
            uint16_t item_id16 = 0;
            uint16_t prot16    = 0;
            uint32_t item_type = 0;
            if (!read_u16be(bytes, p, &item_id16)
                || !read_u16be(bytes, p + 2, &prot16)
                || !read_u32be(bytes, p + 4, &item_type)) {
                return false;
            }
            out->item_id          = static_cast<uint32_t>(item_id16);
            out->protection_index = prot16;
            out->have_type        = true;
            out->item_type        = item_type;
            p += 8;
        } else if (version == 3U) {
            uint32_t item_id32 = 0;
            uint16_t prot16    = 0;
            uint32_t item_type = 0;
            if (!read_u32be(bytes, p, &item_id32)
                || !read_u16be(bytes, p + 4, &prot16)
                || !read_u32be(bytes, p + 6, &item_type)) {
                return false;
            }
            out->item_id          = item_id32;
            out->protection_index = prot16;
            out->have_type        = true;
            out->item_type        = item_type;
            p += 10;
        } else {
            return false;
        }

        if (!bmff_read_cstr(bytes, &p, payload_end, out->name.data(),
                            out->name.size(), &out->name_len)) {
            return false;
        }

        if (out->item_type == fourcc('m', 'i', 'm', 'e')) {
            if (!bmff_read_cstr(bytes, &p, payload_end,
                                out->content_type.data(),
                                out->content_type.size(),
                                &out->content_type_len)) {
                return false;
            }
            if (!bmff_read_cstr(bytes, &p, payload_end,
                                out->content_encoding.data(),
                                out->content_encoding.size(),
                                &out->content_encoding_len)) {
                return false;
            }
        } else if (out->item_type == fourcc('u', 'r', 'i', ' ')) {
            if (!bmff_read_cstr(bytes, &p, payload_end, out->uri_type.data(),
                                out->uri_type.size(), &out->uri_type_len)) {
                return false;
            }
        }

        return true;
    }

    static void bmff_collect_iinf_items(std::span<const std::byte> bytes,
                                        const BmffBox& iinf,
                                        PrimaryProps* out) noexcept
    {
        if (!out) {
            return;
        }

        const uint64_t payload_off  = iinf.offset + iinf.header_size;
        const uint64_t payload_size = iinf.size - iinf.header_size;
        const uint64_t payload_end  = payload_off + payload_size;
        if (payload_size < 6 || payload_end > bytes.size()) {
            return;
        }

        const uint8_t version = u8(bytes[payload_off + 0]);
        uint64_t p            = payload_off + 4;
        uint32_t entry_count  = 0;
        if (version == 0U) {
            uint16_t entry_count16 = 0;
            if (!read_u16be(bytes, p, &entry_count16)) {
                return;
            }
            entry_count = static_cast<uint32_t>(entry_count16);
            p += 2;
        } else {
            if (!read_u32be(bytes, p, &entry_count)) {
                return;
            }
            p += 4;
        }

        const uint32_t take_entries
            = (entry_count < static_cast<uint32_t>(out->item_infos.size()))
                  ? entry_count
                  : static_cast<uint32_t>(out->item_infos.size());
        const uint32_t kMaxBoxes = 1U << 16;
        uint32_t seen            = 0;
        while (p + 8 <= payload_end && seen < kMaxBoxes
               && out->item_info_count < take_entries) {
            seen += 1;
            BmffBox child;
            if (!parse_bmff_box(bytes, p, payload_end, &child)) {
                break;
            }
            if (child.type == fourcc('i', 'n', 'f', 'e')) {
                ItemInfo info {};
                if (bmff_parse_infe(bytes, child, &info)) {
                    const uint32_t idx = find_item_info_index(*out,
                                                              info.item_id);
                    if (idx != UINT32_MAX && idx < out->item_infos.size()) {
                        out->item_infos[idx] = info;
                    } else if (out->item_info_count < out->item_infos.size()) {
                        out->item_infos[out->item_info_count] = info;
                        out->item_info_count += 1;
                    }
                }
            }
            p += child.size;
            if (child.size == 0) {
                break;
            }
        }
    }


    static bool bmff_count_ipco_property_type(uint32_t type,
                                              IpcoSummary* summary) noexcept
    {
        if (!summary) {
            return false;
        }

        switch (type) {
        case fourcc('i', 's', 'p', 'e'): summary->ispe_count += 1U; break;
        case fourcc('i', 'r', 'o', 't'): summary->irot_count += 1U; break;
        case fourcc('i', 'm', 'i', 'r'): summary->imir_count += 1U; break;
        case fourcc('c', 'o', 'l', 'r'): summary->colr_count += 1U; break;
        case fourcc('a', 'u', 'x', 'C'): summary->auxc_count += 1U; break;
        case fourcc('p', 'a', 's', 'p'): summary->pasp_count += 1U; break;
        case fourcc('p', 'i', 'x', 'i'): summary->pixi_count += 1U; break;
        case fourcc('c', 'l', 'a', 'p'): summary->clap_count += 1U; break;
        case fourcc('t', 'i', 'l', 'C'): summary->tilc_count += 1U; break;
        default: return false;
        }
        summary->known_property_count += 1U;
        return true;
    }


    static void bmff_collect_ipco_props(
        std::span<const std::byte> bytes, const BmffBox& ipco,
        std::array<IspeProp, 64>* out_ispe, uint32_t* out_ispe_count,
        std::array<U8Prop, 64>* out_irot, uint32_t* out_irot_count,
        std::array<U8Prop, 64>* out_imir, uint32_t* out_imir_count,
        std::array<ColrProp, 64>* out_colr, uint32_t* out_colr_count,
        std::array<AuxCProp, 64>* out_auxc, uint32_t* out_auxc_count,
        std::array<PaspProp, 64>* out_pasp, uint32_t* out_pasp_count,
        std::array<PixiProp, 64>* out_pixi, uint32_t* out_pixi_count,
        std::array<ClapProp, 64>* out_clap, uint32_t* out_clap_count,
        std::array<TilCProp, 64>* out_tilc, uint32_t* out_tilc_count,
        std::array<IpcoProperty, 256>* out_properties,
        uint32_t* out_property_count, bool* out_properties_truncated,
        IpcoSummary* out_summary) noexcept
    {
        if (!out_ispe || !out_ispe_count || !out_irot || !out_irot_count
            || !out_imir || !out_imir_count || !out_colr || !out_colr_count
            || !out_auxc || !out_auxc_count || !out_pasp || !out_pasp_count
            || !out_pixi || !out_pixi_count || !out_clap || !out_clap_count
            || !out_tilc || !out_tilc_count || !out_properties
            || !out_property_count || !out_properties_truncated) {
            return;
        }

        *out_ispe_count           = 0;
        *out_irot_count           = 0;
        *out_imir_count           = 0;
        *out_colr_count           = 0;
        *out_auxc_count           = 0;
        *out_pasp_count           = 0;
        *out_pixi_count           = 0;
        *out_clap_count           = 0;
        *out_tilc_count           = 0;
        *out_property_count       = 0U;
        *out_properties_truncated = false;
        if (out_summary) {
            *out_summary = IpcoSummary {};
        }

        const uint64_t payload_off = ipco.offset + ipco.header_size;
        const uint64_t payload_end = ipco.offset + ipco.size;
        if (payload_off > payload_end || payload_end > bytes.size()) {
            return;
        }

        uint64_t off             = payload_off;
        uint32_t prop_index      = 1;
        const uint32_t kMaxBoxes = 1U << 16;
        uint32_t seen            = 0;
        while (off + 8 <= payload_end) {
            seen += 1;
            if (seen > kMaxBoxes) {
                return;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, off, payload_end, &child)) {
                break;
            }
            if (out_summary) {
                if (out_summary->property_count != UINT32_MAX) {
                    out_summary->property_count += 1U;
                } else {
                    out_summary->property_truncated = true;
                }
                (void)bmff_count_ipco_property_type(child.type, out_summary);
            }
            if (*out_property_count < out_properties->size()) {
                (*out_properties)[*out_property_count]
                    = IpcoProperty { prop_index, child.type };
                *out_property_count += 1U;
            } else {
                *out_properties_truncated = true;
            }

            const uint64_t child_payload_off = child.offset + child.header_size;
            const uint64_t child_payload_size = child.size - child.header_size;
            if (child_payload_off <= bytes.size()
                && child_payload_size <= bytes.size() - child_payload_off) {
                if (child.type == fourcc('i', 's', 'p', 'e')) {
                    if (child_payload_size >= 12) {
                        uint32_t width  = 0;
                        uint32_t height = 0;
                        if (read_u32be(bytes, child_payload_off + 4, &width)
                            && read_u32be(bytes, child_payload_off + 8,
                                          &height)) {
                            if (*out_ispe_count < out_ispe->size()) {
                                (*out_ispe)[*out_ispe_count]
                                    = IspeProp { prop_index, width, height };
                                *out_ispe_count += 1;
                            }
                        }
                    }
                } else if (child.type == fourcc('i', 'r', 'o', 't')) {
                    if (child_payload_size >= 1) {
                        const uint8_t rot = u8(bytes[child_payload_off]) & 0x03;
                        if (*out_irot_count < out_irot->size()) {
                            (*out_irot)[*out_irot_count] = U8Prop { prop_index,
                                                                    rot };
                            *out_irot_count += 1;
                        }
                    }
                } else if (child.type == fourcc('i', 'm', 'i', 'r')) {
                    if (child_payload_size >= 1) {
                        const uint8_t dir = u8(bytes[child_payload_off]);
                        if (*out_imir_count < out_imir->size()) {
                            (*out_imir)[*out_imir_count] = U8Prop { prop_index,
                                                                    dir };
                            *out_imir_count += 1;
                        }
                    }
                } else if (child.type == fourcc('c', 'o', 'l', 'r')) {
                    if (child_payload_size >= 4
                        && *out_colr_count < out_colr->size()) {
                        uint32_t color_type = 0;
                        if (read_u32be(bytes, child_payload_off, &color_type)) {
                            ColrProp prop {};
                            prop.index      = prop_index;
                            prop.color_type = color_type;
                            if (((color_type == fourcc('n', 'c', 'l', 'x'))
                                 && child_payload_size >= 11U)
                                || ((color_type == fourcc('n', 'c', 'l', 'c'))
                                    && child_payload_size >= 10U)) {
                                uint16_t primaries = 0;
                                uint16_t transfer  = 0;
                                uint16_t matrix    = 0;
                                if (read_u16be(bytes, child_payload_off + 4U,
                                               &primaries)
                                    && read_u16be(bytes, child_payload_off + 6U,
                                                  &transfer)
                                    && read_u16be(bytes, child_payload_off + 8U,
                                                  &matrix)) {
                                    prop.have_nclx                = true;
                                    prop.colour_primaries         = primaries;
                                    prop.transfer_characteristics = transfer;
                                    prop.matrix_coefficients      = matrix;
                                    if (color_type
                                        == fourcc('n', 'c', 'l', 'x')) {
                                        prop.have_full_range_flag = true;
                                        prop.full_range_flag      = static_cast<
                                                 uint8_t>(
                                            (u8(bytes[child_payload_off + 10U])
                                             & 0x80U)
                                                ? 1U
                                                : 0U);
                                    }
                                }
                            } else if (color_type == fourcc('r', 'I', 'C', 'C')
                                       || color_type
                                              == fourcc('p', 'r', 'o', 'f')) {
                                const uint64_t profile_bytes
                                    = child_payload_size - 4U;
                                prop.profile_bytes
                                    = (profile_bytes > UINT32_MAX)
                                          ? UINT32_MAX
                                          : static_cast<uint32_t>(
                                                profile_bytes);
                            }
                            (*out_colr)[*out_colr_count] = prop;
                            *out_colr_count += 1;
                        }
                    }
                } else if (child.type == fourcc('a', 'u', 'x', 'C')) {
                    if (child_payload_size >= 5) {
                        uint64_t p       = child_payload_off + 4;
                        const uint64_t e = child_payload_off
                                           + child_payload_size;
                        while (p < e && bytes[p] != std::byte { 0x00 }) {
                            p += 1;
                        }
                        if (p < e) {
                            const uint64_t type_off = child_payload_off + 4;
                            const uint64_t type_len = p - type_off;
                            if (type_len > 0 && type_off <= bytes.size()
                                && type_len <= bytes.size() - type_off) {
                                const char* s = reinterpret_cast<const char*>(
                                    bytes.data() + type_off);
                                const std::string_view aux_type(s, type_len);
                                const AuxSemantic semantic = classify_auxc_type(
                                    aux_type);
                                if (*out_auxc_count < out_auxc->size()) {
                                    AuxCProp prop {};
                                    prop.index    = prop_index;
                                    prop.semantic = semantic;

                                    const size_t type_copy
                                        = (aux_type.size()
                                           < prop.aux_type.size())
                                              ? aux_type.size()
                                              : prop.aux_type.size();
                                    for (size_t ti = 0; ti < type_copy; ++ti) {
                                        prop.aux_type[ti] = aux_type[ti];
                                    }
                                    prop.aux_type_len = static_cast<uint16_t>(
                                        type_copy);

                                    const uint64_t subtype_off = p + 1;
                                    if (subtype_off <= e) {
                                        const uint64_t subtype_len_u64
                                            = e - subtype_off;
                                        const size_t subtype_len
                                            = static_cast<size_t>(
                                                subtype_len_u64);
                                        const size_t subtype_copy
                                            = (subtype_len
                                               < prop.aux_subtype.size())
                                                  ? subtype_len
                                                  : prop.aux_subtype.size();
                                        for (size_t si = 0; si < subtype_copy;
                                             ++si) {
                                            prop.aux_subtype[si]
                                                = bytes[subtype_off + si];
                                        }
                                        prop.aux_subtype_len
                                            = static_cast<uint16_t>(
                                                subtype_copy);
                                        prop.aux_subtype_total_len
                                            = (subtype_len > 0xFFFFU)
                                                  ? 0xFFFFU
                                                  : static_cast<uint16_t>(
                                                        subtype_len);
                                        prop.aux_subtype_truncated
                                            = (subtype_copy < subtype_len);
                                    }
                                    (*out_auxc)[*out_auxc_count] = prop;
                                    *out_auxc_count += 1;
                                }
                            }
                        }
                    }
                } else if (child.type == fourcc('p', 'a', 's', 'p')) {
                    if (child_payload_size >= 8
                        && *out_pasp_count < out_pasp->size()) {
                        uint32_t h_spacing = 0;
                        uint32_t v_spacing = 0;
                        if (read_u32be(bytes, child_payload_off + 0U, &h_spacing)
                            && read_u32be(bytes, child_payload_off + 4U,
                                          &v_spacing)) {
                            (*out_pasp)[*out_pasp_count]
                                = PaspProp { prop_index, h_spacing, v_spacing };
                            *out_pasp_count += 1;
                        }
                    }
                } else if (child.type == fourcc('p', 'i', 'x', 'i')) {
                    if (child_payload_size >= 5
                        && *out_pixi_count < out_pixi->size()) {
                        const uint8_t channel_count = u8(
                            bytes[child_payload_off + 4U]);
                        if (channel_count != 0U
                            && static_cast<uint64_t>(channel_count)
                                   <= child_payload_size - 5U) {
                            PixiProp prop {};
                            prop.index = prop_index;
                            const uint8_t copy_count
                                = (channel_count < prop.bits_per_channel.size())
                                      ? channel_count
                                      : static_cast<uint8_t>(
                                            prop.bits_per_channel.size());
                            for (uint8_t ci = 0U; ci < copy_count; ++ci) {
                                prop.bits_per_channel[ci] = u8(
                                    bytes[child_payload_off + 5U + ci]);
                            }
                            prop.channel_count           = copy_count;
                            (*out_pixi)[*out_pixi_count] = prop;
                            *out_pixi_count += 1;
                        }
                    }
                } else if (child.type == fourcc('c', 'l', 'a', 'p')) {
                    if (child_payload_size >= 32
                        && *out_clap_count < out_clap->size()) {
                        ClapProp prop {};
                        prop.index = prop_index;
                        if (read_i32be(bytes, child_payload_off + 0U,
                                       &prop.width_n)
                            && read_i32be(bytes, child_payload_off + 4U,
                                          &prop.width_d)
                            && read_i32be(bytes, child_payload_off + 8U,
                                          &prop.height_n)
                            && read_i32be(bytes, child_payload_off + 12U,
                                          &prop.height_d)
                            && read_i32be(bytes, child_payload_off + 16U,
                                          &prop.horiz_off_n)
                            && read_i32be(bytes, child_payload_off + 20U,
                                          &prop.horiz_off_d)
                            && read_i32be(bytes, child_payload_off + 24U,
                                          &prop.vert_off_n)
                            && read_i32be(bytes, child_payload_off + 28U,
                                          &prop.vert_off_d)) {
                            (*out_clap)[*out_clap_count] = prop;
                            *out_clap_count += 1;
                        }
                    }
                } else if (child.type == fourcc('t', 'i', 'l', 'C')
                           && *out_tilc_count < out_tilc->size()) {
                    TilCProp prop {};
                    prop.index = prop_index;
                    if (child_payload_size >= 4U) {
                        prop.have_header = true;
                        prop.version     = u8(bytes[child_payload_off]);
                        prop.flags       = (static_cast<uint32_t>(
                                          u8(bytes[child_payload_off + 1U]))
                                      << 16U)
                                     | (static_cast<uint32_t>(
                                            u8(bytes[child_payload_off + 2U]))
                                        << 8U)
                                     | static_cast<uint32_t>(
                                         u8(bytes[child_payload_off + 3U]));
                    }
                    if (child_payload_size >= 13U
                        && read_u32be(bytes, child_payload_off + 4U,
                                      &prop.tile_width)
                        && read_u32be(bytes, child_payload_off + 8U,
                                      &prop.tile_height)) {
                        prop.extra_dimension_count = u8(
                            bytes[child_payload_off + 12U]);
                        const uint64_t dimension_bytes
                            = static_cast<uint64_t>(prop.extra_dimension_count)
                              * 4U;
                        const uint64_t core_bytes = 13U + dimension_bytes;
                        prop.dimensions_truncated
                            = prop.extra_dimension_count
                              > kMaxTiledImageExtraDimensions;
                        if (core_bytes <= child_payload_size) {
                            prop.have_core = true;
                            const uint8_t store_count
                                = std::min(prop.extra_dimension_count,
                                           kMaxTiledImageExtraDimensions);
                            prop.stored_dimension_count = store_count;
                            bool dimensions_nonzero     = true;
                            for (uint8_t i = 0U; i < prop.extra_dimension_count;
                                 ++i) {
                                uint32_t dimension_size = 0U;
                                if (!read_u32be(bytes,
                                                child_payload_off + 13U
                                                    + static_cast<uint64_t>(i)
                                                          * 4U,
                                                &dimension_size)) {
                                    prop.have_core     = false;
                                    dimensions_nonzero = false;
                                    break;
                                }
                                if (dimension_size == 0U) {
                                    dimensions_nonzero = false;
                                }
                                if (i < store_count) {
                                    prop.dimension_sizes[i] = dimension_size;
                                }
                            }
                            if (prop.have_core) {
                                const uint64_t trailing_bytes
                                    = child_payload_size - core_bytes;
                                prop.conditional_payload_bytes
                                    = trailing_bytes > UINT32_MAX
                                          ? UINT32_MAX
                                          : static_cast<uint32_t>(
                                                trailing_bytes);
                                prop.conditional_payload_offset
                                    = child_payload_off + core_bytes;
                                prop.conditional_payload_end
                                    = child_payload_off + child_payload_size;
                                prop.core_valid = prop.version == 0U
                                                  && prop.flags == 0U
                                                  && prop.tile_width != 0U
                                                  && prop.tile_height != 0U
                                                  && dimensions_nonzero
                                                  && !prop.dimensions_truncated;
                            }
                        } else {
                            prop.dimensions_truncated = true;
                        }
                    }
                    (*out_tilc)[*out_tilc_count] = prop;
                    *out_tilc_count += 1U;
                }
            }

            off += child.size;
            if (child.size == 0) {
                break;
            }
            if (prop_index == UINT32_MAX) {
                break;
            }
            prop_index += 1;
        }
    }


    static const IspeProp* find_ispe(std::span<const IspeProp> props,
                                     uint32_t index) noexcept
    {
        for (size_t i = 0; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static const U8Prop* find_u8(std::span<const U8Prop> props,
                                 uint32_t index) noexcept
    {
        for (size_t i = 0; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static const ColrProp* find_colr(std::span<const ColrProp> props,
                                     uint32_t index) noexcept
    {
        for (size_t i = 0; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static const AuxCProp* find_auxc(std::span<const AuxCProp> props,
                                     uint32_t index) noexcept
    {
        for (size_t i = 0; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static const PaspProp* find_pasp(std::span<const PaspProp> props,
                                     uint32_t index) noexcept
    {
        for (size_t i = 0; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static const PixiProp* find_pixi(std::span<const PixiProp> props,
                                     uint32_t index) noexcept
    {
        for (size_t i = 0; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static const ClapProp* find_clap(std::span<const ClapProp> props,
                                     uint32_t index) noexcept
    {
        for (size_t i = 0; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static const TilCProp* find_tilc(std::span<const TilCProp> props,
                                     uint32_t index) noexcept
    {
        for (size_t i = 0U; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static bool bmff_property_type_for_index(
        uint32_t index, std::span<const IspeProp> ispe,
        std::span<const U8Prop> irot, std::span<const U8Prop> imir,
        std::span<const ColrProp> colr, std::span<const AuxCProp> auxc,
        std::span<const PaspProp> pasp, std::span<const PixiProp> pixi,
        std::span<const ClapProp> clap, std::span<const TilCProp> tilc,
        uint32_t* out_type) noexcept
    {
        if (!out_type) {
            return false;
        }
        if (find_ispe(ispe, index)) {
            *out_type = fourcc('i', 's', 'p', 'e');
            return true;
        }
        if (find_u8(irot, index)) {
            *out_type = fourcc('i', 'r', 'o', 't');
            return true;
        }
        if (find_u8(imir, index)) {
            *out_type = fourcc('i', 'm', 'i', 'r');
            return true;
        }
        if (find_colr(colr, index)) {
            *out_type = fourcc('c', 'o', 'l', 'r');
            return true;
        }
        if (find_auxc(auxc, index)) {
            *out_type = fourcc('a', 'u', 'x', 'C');
            return true;
        }
        if (find_pasp(pasp, index)) {
            *out_type = fourcc('p', 'a', 's', 'p');
            return true;
        }
        if (find_pixi(pixi, index)) {
            *out_type = fourcc('p', 'i', 'x', 'i');
            return true;
        }
        if (find_clap(clap, index)) {
            *out_type = fourcc('c', 'l', 'a', 'p');
            return true;
        }
        if (find_tilc(tilc, index)) {
            *out_type = fourcc('t', 'i', 'l', 'C');
            return true;
        }
        return false;
    }

    static void append_ipma_association(PrimaryProps* out, uint32_t item_id,
                                        uint32_t property_index,
                                        uint8_t essential,
                                        bool have_property_type,
                                        uint32_t property_type) noexcept
    {
        if (!out || property_index == 0U) {
            return;
        }
        if (out->ipma_association_total != UINT32_MAX) {
            out->ipma_association_total += 1U;
        }
        if (out->ipma_association_count >= out->ipma_associations.size()) {
            out->ipma_truncated = true;
            return;
        }
        ItemPropertyAssociation& assoc
            = out->ipma_associations[out->ipma_association_count];
        assoc.item_id            = item_id;
        assoc.property_index     = property_index;
        assoc.property_type      = property_type;
        assoc.essential          = essential;
        assoc.have_property_type = have_property_type;
        out->ipma_association_count += 1U;
    }

    static void set_primary_color(PrimaryProps* out,
                                  const ColrProp& prop) noexcept
    {
        if (!out || out->have_color) {
            return;
        }
        out->have_color               = true;
        out->color_type               = prop.color_type;
        out->have_nclx                = prop.have_nclx;
        out->colour_primaries         = prop.colour_primaries;
        out->transfer_characteristics = prop.transfer_characteristics;
        out->matrix_coefficients      = prop.matrix_coefficients;
        out->have_full_range_flag     = prop.have_full_range_flag;
        out->full_range_flag          = prop.full_range_flag;
        out->color_profile_bytes      = prop.profile_bytes;
    }

    static void set_primary_pixel_aspect(PrimaryProps* out,
                                         const PaspProp& prop) noexcept
    {
        if (!out || out->have_pixel_aspect) {
            return;
        }
        out->have_pixel_aspect      = true;
        out->pixel_aspect_h_spacing = prop.h_spacing;
        out->pixel_aspect_v_spacing = prop.v_spacing;
    }

    static void set_primary_pixel_depth(PrimaryProps* out,
                                        const PixiProp& prop) noexcept
    {
        if (!out || out->have_pixel_depth) {
            return;
        }
        out->have_pixel_depth          = true;
        out->pixel_depth_channel_count = prop.channel_count;
        for (uint8_t i = 0U; i < prop.channel_count
                             && i < out->pixel_depth_bits_per_channel.size();
             ++i) {
            out->pixel_depth_bits_per_channel[i] = prop.bits_per_channel[i];
        }
    }

    static void set_primary_clean_aperture(PrimaryProps* out,
                                           const ClapProp& prop) noexcept
    {
        if (!out || out->have_clean_aperture) {
            return;
        }
        out->have_clean_aperture = true;
        out->clean_aperture      = prop;
    }

    static bool is_primary_auxl_item(const PrimaryProps& out,
                                     uint32_t item_id) noexcept
    {
        return find_primary_auxl_index(out, item_id) != UINT32_MAX;
    }


    static void bmff_apply_ipma_primary(
        std::span<const std::byte> bytes, const BmffBox& ipma,
        uint32_t primary_item_id, std::span<const IspeProp> ispe,
        std::span<const U8Prop> irot, std::span<const U8Prop> imir,
        std::span<const ColrProp> colr, std::span<const AuxCProp> auxc,
        std::span<const PaspProp> pasp, std::span<const PixiProp> pixi,
        std::span<const ClapProp> clap, std::span<const TilCProp> tilc,
        PrimaryProps* out) noexcept
    {
        if (!out) {
            return;
        }

        const uint64_t payload_off  = ipma.offset + ipma.header_size;
        const uint64_t payload_size = ipma.size - ipma.header_size;
        if (payload_size < 8) {
            return;
        }

        const uint8_t version = u8(bytes[payload_off + 0]);

        uint32_t entry_count = 0;
        if (!read_u32be(bytes, payload_off + 4, &entry_count)) {
            return;
        }
        if (entry_count == 0) {
            return;
        }

        uint64_t off                = payload_off + 8;
        const uint64_t end          = payload_off + payload_size;
        const uint32_t kMaxEntries  = 1U << 16;
        const uint32_t take_entries = (entry_count < kMaxEntries) ? entry_count
                                                                  : kMaxEntries;
        for (uint32_t i = 0; i < take_entries; ++i) {
            uint32_t item_id = 0;
            if (version < 1) {
                if (off + 2 > end) {
                    return;
                }
                uint16_t id16 = 0;
                if (!read_u16be(bytes, off, &id16)) {
                    return;
                }
                item_id = static_cast<uint32_t>(id16);
                off += 2;
            } else {
                if (off + 4 > end) {
                    return;
                }
                if (!read_u32be(bytes, off, &item_id)) {
                    return;
                }
                off += 4;
            }

            if (off + 1 > end) {
                return;
            }
            const uint8_t assoc_count = u8(bytes[off]);
            off += 1;

            const bool is_primary = (item_id == primary_item_id);
            const bool is_primary_aux
                = (!is_primary && is_primary_auxl_item(*out, item_id));

            if (version < 1) {
                for (uint32_t j = 0; j < assoc_count; ++j) {
                    if (off + 1 > end) {
                        return;
                    }
                    const uint8_t v = u8(bytes[off]);
                    off += 1;
                    const uint8_t essential = static_cast<uint8_t>(
                        (v & 0x80U) ? 1U : 0U);
                    const uint32_t prop_index = static_cast<uint32_t>(v & 0x7F);
                    if (prop_index != 0U) {
                        uint32_t prop_type   = 0;
                        const bool have_type = bmff_property_type_for_index(
                            prop_index, ispe, irot, imir, colr, auxc, pasp,
                            pixi, clap, tilc, &prop_type);
                        append_ipma_association(out, item_id, prop_index,
                                                essential, have_type,
                                                prop_type);
                        if (is_primary) {
                            if (const IspeProp* p = find_ispe(ispe,
                                                              prop_index)) {
                                out->have_width_height = true;
                                out->width             = p->width;
                                out->height            = p->height;
                            }
                            if (const U8Prop* p = find_u8(irot, prop_index)) {
                                out->have_rotation = true;
                                out->rotation_degrees
                                    = static_cast<uint16_t>(p->value) * 90U;
                            }
                            if (const U8Prop* p = find_u8(imir, prop_index)) {
                                out->have_mirror = true;
                                out->mirror      = p->value;
                            }
                            if (const ColrProp* p = find_colr(colr,
                                                              prop_index)) {
                                set_primary_color(out, *p);
                            }
                            if (const PaspProp* p = find_pasp(pasp,
                                                              prop_index)) {
                                set_primary_pixel_aspect(out, *p);
                            }
                            if (const PixiProp* p = find_pixi(pixi,
                                                              prop_index)) {
                                set_primary_pixel_depth(out, *p);
                            }
                            if (const ClapProp* p = find_clap(clap,
                                                              prop_index)) {
                                set_primary_clean_aperture(out, *p);
                            }
                        }
                        if (is_primary_aux) {
                            if (const AuxCProp* p = find_auxc(auxc,
                                                              prop_index)) {
                                set_aux_item_semantic(out, item_id,
                                                      p->semantic);
                                if (p->aux_type_len > 0) {
                                    set_aux_item_type(
                                        out, item_id,
                                        std::string_view(p->aux_type.data(),
                                                         p->aux_type_len));
                                }
                                if (p->aux_subtype_len > 0
                                    || p->aux_subtype_total_len > 0) {
                                    set_aux_item_subtype(
                                        out, item_id,
                                        std::span<const std::byte>(
                                            p->aux_subtype.data(),
                                            p->aux_subtype_len),
                                        p->aux_subtype_total_len,
                                        p->aux_subtype_truncated);
                                }
                                set_primary_auxl_semantic(out, item_id,
                                                          p->semantic);
                            }
                        } else if (const AuxCProp* p = find_auxc(auxc,
                                                                 prop_index)) {
                            set_aux_item_semantic(out, item_id, p->semantic);
                            if (p->aux_type_len > 0) {
                                set_aux_item_type(
                                    out, item_id,
                                    std::string_view(p->aux_type.data(),
                                                     p->aux_type_len));
                            }
                            if (p->aux_subtype_len > 0
                                || p->aux_subtype_total_len > 0) {
                                set_aux_item_subtype(out, item_id,
                                                     std::span<const std::byte>(
                                                         p->aux_subtype.data(),
                                                         p->aux_subtype_len),
                                                     p->aux_subtype_total_len,
                                                     p->aux_subtype_truncated);
                            }
                        }
                    }
                }
            } else {
                for (uint32_t j = 0; j < assoc_count; ++j) {
                    if (off + 2 > end) {
                        return;
                    }
                    uint16_t v = 0;
                    if (!read_u16be(bytes, off, &v)) {
                        return;
                    }
                    off += 2;
                    const uint8_t essential = static_cast<uint8_t>(
                        (v & 0x8000U) ? 1U : 0U);
                    const uint32_t prop_index = static_cast<uint32_t>(v
                                                                      & 0x7FFF);
                    if (prop_index != 0U) {
                        uint32_t prop_type   = 0;
                        const bool have_type = bmff_property_type_for_index(
                            prop_index, ispe, irot, imir, colr, auxc, pasp,
                            pixi, clap, tilc, &prop_type);
                        append_ipma_association(out, item_id, prop_index,
                                                essential, have_type,
                                                prop_type);
                        if (is_primary) {
                            if (const IspeProp* p = find_ispe(ispe,
                                                              prop_index)) {
                                out->have_width_height = true;
                                out->width             = p->width;
                                out->height            = p->height;
                            }
                            if (const U8Prop* p = find_u8(irot, prop_index)) {
                                out->have_rotation = true;
                                out->rotation_degrees
                                    = static_cast<uint16_t>(p->value) * 90U;
                            }
                            if (const U8Prop* p = find_u8(imir, prop_index)) {
                                out->have_mirror = true;
                                out->mirror      = p->value;
                            }
                            if (const ColrProp* p = find_colr(colr,
                                                              prop_index)) {
                                set_primary_color(out, *p);
                            }
                            if (const PaspProp* p = find_pasp(pasp,
                                                              prop_index)) {
                                set_primary_pixel_aspect(out, *p);
                            }
                            if (const PixiProp* p = find_pixi(pixi,
                                                              prop_index)) {
                                set_primary_pixel_depth(out, *p);
                            }
                            if (const ClapProp* p = find_clap(clap,
                                                              prop_index)) {
                                set_primary_clean_aperture(out, *p);
                            }
                        }
                        if (is_primary_aux) {
                            if (const AuxCProp* p = find_auxc(auxc,
                                                              prop_index)) {
                                set_aux_item_semantic(out, item_id,
                                                      p->semantic);
                                if (p->aux_type_len > 0) {
                                    set_aux_item_type(
                                        out, item_id,
                                        std::string_view(p->aux_type.data(),
                                                         p->aux_type_len));
                                }
                                if (p->aux_subtype_len > 0
                                    || p->aux_subtype_total_len > 0) {
                                    set_aux_item_subtype(
                                        out, item_id,
                                        std::span<const std::byte>(
                                            p->aux_subtype.data(),
                                            p->aux_subtype_len),
                                        p->aux_subtype_total_len,
                                        p->aux_subtype_truncated);
                                }
                                set_primary_auxl_semantic(out, item_id,
                                                          p->semantic);
                            }
                        } else if (const AuxCProp* p = find_auxc(auxc,
                                                                 prop_index)) {
                            set_aux_item_semantic(out, item_id, p->semantic);
                            if (p->aux_type_len > 0) {
                                set_aux_item_type(
                                    out, item_id,
                                    std::string_view(p->aux_type.data(),
                                                     p->aux_type_len));
                            }
                            if (p->aux_subtype_len > 0
                                || p->aux_subtype_total_len > 0) {
                                set_aux_item_subtype(out, item_id,
                                                     std::span<const std::byte>(
                                                         p->aux_subtype.data(),
                                                         p->aux_subtype_len),
                                                     p->aux_subtype_total_len,
                                                     p->aux_subtype_truncated);
                            }
                        }
                    }
                }
            }
        }
    }

    static const DataReferenceEntry*
    find_data_reference(const PrimaryProps& p, uint16_t index) noexcept;

    struct TiledImageOffsetRow final {
        uint64_t tile_index   = 0U;
        uint64_t start_offset = 0U;
        uint64_t size         = 0U;
        bool have_size        = false;
        bool empty            = false;
    };

    struct TiledImageState final {
        uint32_t item_id                                = 0U;
        uint32_t configuration_count                    = 0U;
        uint32_t ispe_count                             = 0U;
        uint32_t tile_columns                           = 0U;
        uint32_t tile_rows                              = 0U;
        uint64_t expected_tile_count                    = 0U;
        const TilCProp* configuration                   = nullptr;
        const IspeProp* ispe                            = nullptr;
        const ItemLocation* location                    = nullptr;
        const DataReferenceEntry* data_reference        = nullptr;
        uint8_t configuration_essential                 = 0U;
        uint32_t tile_item_type                         = 0U;
        uint32_t tipa_flags                             = 0U;
        uint32_t tile_property_association_count        = 0U;
        uint32_t stored_tile_property_association_count = 0U;
        uint64_t logical_item_data_size                 = 0U;
        uint64_t expected_offset_table_size             = 0U;
        uint64_t empty_tile_count                       = 0U;
        uint32_t stored_offset_row_count                = 0U;
        uint8_t tipa_version                            = 0U;
        std::array<TilePropertyAssociation, kMaxTilePropertyAssociations>
            tile_property_associations {};
        std::array<TiledImageOffsetRow, kMaxTiledImageOffsetRows> offset_rows {};
        bool configuration_valid                  = false;
        bool tile_count_overflow                  = false;
        bool have_tile_grid                       = false;
        bool layout_valid                         = false;
        bool data_reference_present               = false;
        bool data_reference_valid                 = false;
        bool input_item_count_matches             = false;
        bool conditional_payload_valid            = false;
        bool tipa_present                         = false;
        bool tipa_valid                           = false;
        bool tile_property_associations_truncated = false;
        bool offset_table_present                 = false;
        bool offset_table_valid                   = false;
        bool offset_table_validation_truncated    = false;
        bool tile_offsets_valid                   = false;
        bool tile_sizes_validated                 = false;
        bool complete_configuration_valid         = false;
    };

    static const IpcoProperty* find_ipco_property(const PrimaryProps& p,
                                                  uint32_t index) noexcept
    {
        for (uint32_t i = 0U; i < p.ipco_property_count; ++i) {
            if (p.ipco_properties[i].index == index) {
                return &p.ipco_properties[i];
            }
        }
        return nullptr;
    }

    static bool parse_tiled_image_conditional(std::span<const std::byte> bytes,
                                              const PrimaryProps& p,
                                              TiledImageState* state) noexcept
    {
        if (!state || !state->configuration || !state->data_reference) {
            return false;
        }
        const TilCProp& config             = *state->configuration;
        const DataReferenceEntry& data_ref = *state->data_reference;
        if (config.conditional_payload_offset > config.conditional_payload_end
            || config.conditional_payload_end > bytes.size()) {
            return false;
        }
        if (data_ref.external_tiles_urls) {
            state->conditional_payload_valid = config.conditional_payload_offset
                                               == config.conditional_payload_end;
            return state->conditional_payload_valid;
        }

        uint64_t p_offset = config.conditional_payload_offset;
        if (!read_u32be(bytes, p_offset, &state->tile_item_type)) {
            return false;
        }
        p_offset += 4U;
        BmffBox tipa {};
        if (!parse_bmff_box(bytes, p_offset, config.conditional_payload_end,
                            &tipa)
            || tipa.type != fourcc('t', 'i', 'p', 'a')
            || tipa.offset + tipa.size != config.conditional_payload_end) {
            return false;
        }
        state->tipa_present        = true;
        const uint64_t payload_off = tipa.offset + tipa.header_size;
        const uint64_t payload_end = tipa.offset + tipa.size;
        if (payload_off + 5U > payload_end) {
            return false;
        }
        state->tipa_version = u8(bytes[payload_off]);
        state->tipa_flags
            = (static_cast<uint32_t>(u8(bytes[payload_off + 1U])) << 16U)
              | (static_cast<uint32_t>(u8(bytes[payload_off + 2U])) << 8U)
              | static_cast<uint32_t>(u8(bytes[payload_off + 3U]));
        if (state->tipa_version != 0U || state->tipa_flags > 1U) {
            return false;
        }
        state->tile_property_association_count = u8(bytes[payload_off + 4U]);
        const uint8_t association_bytes = state->tipa_flags == 0U ? 1U : 2U;
        uint64_t expected_payload_size  = 0U;
        if (!checked_mul_u64(state->tile_property_association_count,
                             association_bytes, &expected_payload_size)
            || !checked_add_u64(expected_payload_size, 5U,
                                &expected_payload_size)
            || expected_payload_size != payload_end - payload_off) {
            return false;
        }

        uint64_t association_offset = payload_off + 5U;
        bool associations_valid     = true;
        for (uint32_t i = 0U; i < state->tile_property_association_count; ++i) {
            uint16_t raw = 0U;
            if (association_bytes == 1U) {
                raw = u8(bytes[association_offset]);
            } else if (!read_u16be(bytes, association_offset, &raw)) {
                return false;
            }
            association_offset += association_bytes;
            const uint16_t essential_mask = association_bytes == 1U ? 0x80U
                                                                    : 0x8000U;
            const uint16_t index_mask     = association_bytes == 1U ? 0x7FU
                                                                    : 0x7FFFU;
            TilePropertyAssociation association {};
            association.essential      = (raw & essential_mask) != 0U ? 1U : 0U;
            association.property_index = raw & index_mask;
            if ((association.property_index == 0U && association.essential != 0U)
                || association.property_index > p.ipco_summary.property_count) {
                associations_valid = false;
            }
            if (const IpcoProperty* property
                = find_ipco_property(p, association.property_index)) {
                association.property_type      = property->type;
                association.have_property_type = true;
            }
            if (state->stored_tile_property_association_count
                < state->tile_property_associations.size()) {
                state->tile_property_associations
                    [state->stored_tile_property_association_count]
                    = association;
                state->stored_tile_property_association_count += 1U;
            } else {
                state->tile_property_associations_truncated = true;
            }
        }
        state->tipa_valid                = associations_valid;
        state->conditional_payload_valid = state->tipa_valid;
        return state->conditional_payload_valid;
    }

    static bool
    validate_tiled_image_offset_table(std::span<const std::byte> bytes,
                                      const PrimaryProps& p,
                                      TiledImageState* state) noexcept
    {
        if (!state || !state->location || !state->data_reference
            || state->data_reference->external_tiles_urls) {
            return false;
        }
        const DataReferenceEntry& data_ref = *state->data_reference;
        state->offset_table_present        = true;
        if (state->location->construction_method != 0U) {
            return false;
        }
        uint64_t record_bytes = 0U;
        if (!checked_add_u64(data_ref.offset_field_bytes,
                             data_ref.size_field_bytes, &record_bytes)
            || !checked_mul_u64(state->expected_tile_count, record_bytes,
                                &state->expected_offset_table_size)
            || state->expected_offset_table_size
                   != data_ref.tile_offset_table_size) {
            return false;
        }
        if (state->expected_tile_count > kMaxTiledImageOffsetsToValidate) {
            state->offset_table_validation_truncated = true;
            return false;
        }

        ItemDataCursor cursor {};
        if (!init_item_data_cursor_with_reference(
                bytes, p, state->item_id, state->location->data_reference_index,
                &cursor)) {
            return false;
        }
        state->logical_item_data_size = cursor.logical_size;
        uint64_t table_end            = 0U;
        if (!checked_add_u64(data_ref.tile_offset_table_start,
                             data_ref.tile_offset_table_size, &table_end)
            || table_end > cursor.logical_size) {
            return false;
        }
        cursor.logical_offset = data_ref.tile_offset_table_start;

        bool offsets_valid           = true;
        bool have_previous_offset    = false;
        uint64_t previous_offset     = 0U;
        uint32_t previous_stored_row = UINT32_MAX;
        for (uint64_t i = 0U; i < state->expected_tile_count; ++i) {
            uint64_t start_offset = 0U;
            uint64_t tile_size    = 0U;
            if (!read_item_data_uint_n(&cursor, data_ref.offset_field_bytes,
                                       &start_offset)
                || (data_ref.size_field_bytes != 0U
                    && !read_item_data_uint_n(&cursor, data_ref.size_field_bytes,
                                              &tile_size))) {
                return false;
            }
            const bool empty           = start_offset == UINT32_MAX;
            const bool offset_in_range = !empty
                                         && start_offset <= cursor.logical_size;
            if (empty) {
                state->empty_tile_count += 1U;
            } else if (start_offset > cursor.logical_size) {
                offsets_valid = false;
            } else if (data_ref.size_field_bytes != 0U) {
                if (tile_size > cursor.logical_size - start_offset) {
                    offsets_valid = false;
                }
            } else if (data_ref.sequential_order) {
                if (have_previous_offset && start_offset <= previous_offset) {
                    offsets_valid = false;
                }
                if (have_previous_offset && previous_stored_row != UINT32_MAX) {
                    TiledImageOffsetRow& previous
                        = state->offset_rows[previous_stored_row];
                    if (start_offset > previous_offset) {
                        previous.size      = start_offset - previous_offset;
                        previous.have_size = true;
                    }
                }
                have_previous_offset = true;
                previous_offset      = start_offset;
            }

            if (state->stored_offset_row_count < state->offset_rows.size()) {
                TiledImageOffsetRow& row
                    = state->offset_rows[state->stored_offset_row_count];
                row.tile_index   = i;
                row.start_offset = start_offset;
                row.size         = tile_size;
                row.have_size    = data_ref.size_field_bytes != 0U;
                row.empty        = empty;
                if (data_ref.size_field_bytes == 0U && data_ref.sequential_order
                    && offset_in_range) {
                    previous_stored_row = state->stored_offset_row_count;
                }
                state->stored_offset_row_count += 1U;
            } else if (data_ref.size_field_bytes == 0U
                       && data_ref.sequential_order && offset_in_range) {
                previous_stored_row = UINT32_MAX;
            }
        }
        if (data_ref.size_field_bytes == 0U && data_ref.sequential_order
            && have_previous_offset && previous_stored_row != UINT32_MAX) {
            TiledImageOffsetRow& last = state->offset_rows[previous_stored_row];
            last.size                 = cursor.logical_size - previous_offset;
            last.have_size            = true;
        }
        state->tile_offsets_valid   = offsets_valid;
        state->tile_sizes_validated = offsets_valid
                                      && (data_ref.size_field_bytes != 0U
                                          || data_ref.sequential_order);
        state->offset_table_valid = offsets_valid
                                    && cursor.logical_offset == table_end;
        return state->offset_table_valid;
    }

    static TiledImageState tiled_image_state(std::span<const std::byte> bytes,
                                             const PrimaryProps& p,
                                             uint32_t item_id,
                                             bool validate_complete) noexcept
    {
        TiledImageState out;
        out.item_id = item_id;
        const std::span<const TilCProp> tilc(p.ipco_tilc.data(),
                                             p.ipco_tilc_count);
        const std::span<const IspeProp> ispe(p.ipco_ispe.data(),
                                             p.ipco_ispe_count);

        for (uint32_t i = 0U; i < p.ipma_association_count; ++i) {
            const ItemPropertyAssociation& assoc = p.ipma_associations[i];
            if (assoc.item_id != item_id || !assoc.have_property_type) {
                continue;
            }
            if (assoc.property_type == fourcc('t', 'i', 'l', 'C')) {
                out.configuration_count += 1U;
                if (!out.configuration) {
                    out.configuration = find_tilc(tilc, assoc.property_index);
                    out.configuration_essential = assoc.essential;
                }
            } else if (assoc.property_type == fourcc('i', 's', 'p', 'e')) {
                out.ispe_count += 1U;
                if (!out.ispe) {
                    out.ispe = find_ispe(ispe, assoc.property_index);
                }
            }
        }

        out.configuration_valid = out.configuration_count == 1U
                                  && out.configuration
                                  && out.configuration->core_valid;
        out.location = find_item_location(p, item_id);
        if (out.location && out.location->data_reference_index != 0U) {
            out.data_reference_present = true;
            out.data_reference
                = find_data_reference(p, out.location->data_reference_index);
            out.data_reference_valid = p.data_references_valid
                                       && out.data_reference
                                       && out.data_reference->type
                                              == fourcc('d', 'e', 't', 'i')
                                       && out.data_reference->syntax_valid;
        }
        if (!out.configuration_valid || out.ispe_count != 1U || !out.ispe
            || out.ispe->width == 0U || out.ispe->height == 0U) {
            return out;
        }

        const TilCProp& config = *out.configuration;
        out.tile_columns       = out.ispe->width / config.tile_width
                           + (out.ispe->width % config.tile_width != 0U ? 1U
                                                                        : 0U);
        out.tile_rows = out.ispe->height / config.tile_height
                        + (out.ispe->height % config.tile_height != 0U ? 1U
                                                                       : 0U);
        uint64_t tile_count = 0U;
        if (!checked_mul_u64(out.tile_columns, out.tile_rows, &tile_count)) {
            out.tile_count_overflow = true;
            return out;
        }
        for (uint8_t i = 0U; i < config.stored_dimension_count; ++i) {
            if (!checked_mul_u64(tile_count, config.dimension_sizes[i],
                                 &tile_count)) {
                out.tile_count_overflow = true;
                return out;
            }
        }
        out.expected_tile_count = tile_count;
        out.have_tile_grid      = true;
        out.layout_valid        = true;

        if (!validate_complete) {
            return out;
        }
        if (!out.data_reference_valid) {
            return out;
        }
        out.input_item_count_matches = out.data_reference->input_item_count
                                       == out.expected_tile_count;
        if (!out.input_item_count_matches
            || !parse_tiled_image_conditional(bytes, p, &out)) {
            return out;
        }
        if (out.data_reference->external_tiles_urls) {
            out.complete_configuration_valid = true;
            return out;
        }
        out.complete_configuration_valid
            = validate_tiled_image_offset_table(bytes, p, &out);
        return out;
    }

    static void emit_tiled_image_fields(MetaStore& store, BlockId block,
                                        uint32_t* io_order,
                                        std::span<const std::byte> bytes,
                                        const PrimaryProps& p) noexcept
    {
        if (!io_order || p.item_info_count == 0U) {
            return;
        }

        uint32_t state_count               = 0U;
        uint32_t configuration_valid_count = 0U;
        uint32_t layout_valid_count        = 0U;
        uint32_t complete_valid_count      = 0U;
        for (uint32_t i = 0U; i < p.item_info_count; ++i) {
            const ItemInfo& item = p.item_infos[i];
            if (!item.have_type
                || item.item_type != fourcc('t', 'i', 'l', 'i')) {
                continue;
            }
            const TiledImageState state
                = tiled_image_state(bytes, p, item.item_id, false);
            if (state.configuration_valid) {
                configuration_valid_count += 1U;
            }
            if (state.layout_valid) {
                layout_valid_count += 1U;
            }
            state_count += 1U;
        }
        if (state_count == 0U) {
            return;
        }

        emit_u32_field(store, block, (*io_order)++, "tiled_image.count",
                       state_count);
        emit_u32_field(store, block, (*io_order)++,
                       "tiled_image.configuration_valid_count",
                       configuration_valid_count);
        emit_u32_field(store, block, (*io_order)++,
                       "tiled_image.configuration_invalid_count",
                       state_count - configuration_valid_count);
        emit_u32_field(store, block, (*io_order)++,
                       "tiled_image.layout_valid_count", layout_valid_count);
        emit_u32_field(store, block, (*io_order)++,
                       "tiled_image.layout_invalid_count",
                       state_count - layout_valid_count);
        for (uint32_t i = 0U; i < p.item_info_count; ++i) {
            const ItemInfo& item = p.item_infos[i];
            if (!item.have_type
                || item.item_type != fourcc('t', 'i', 'l', 'i')) {
                continue;
            }
            const TiledImageState state = tiled_image_state(bytes, p,
                                                            item.item_id, true);
            if (state.complete_configuration_valid) {
                complete_valid_count += 1U;
            }
            emit_u32_field(store, block, (*io_order)++, "tiled_image.item_id",
                           state.item_id);
            emit_u32_field(store, block, (*io_order)++,
                           "tiled_image.configuration_count",
                           state.configuration_count);
            emit_u8_field(store, block, (*io_order)++,
                          "tiled_image.configuration_present",
                          state.configuration_count != 0U ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "tiled_image.configuration_unique",
                          state.configuration_count == 1U ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "tiled_image.configuration_valid",
                          state.configuration_valid ? 1U : 0U);
            emit_u32_field(store, block, (*io_order)++,
                           "tiled_image.ispe_count", state.ispe_count);
            emit_u8_field(store, block, (*io_order)++,
                          "tiled_image.ispe_present",
                          state.ispe_count != 0U ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "tiled_image.ispe_unique",
                          state.ispe_count == 1U ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "tiled_image.layout_valid",
                          state.layout_valid ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "tiled_image.complete_configuration_valid",
                          state.complete_configuration_valid ? 1U : 0U);
            if (state.configuration_count != 0U) {
                emit_text_field(store, block, (*io_order)++,
                                "tiled_image.configuration", "tiled");
            }

            if (state.configuration) {
                const TilCProp& config = *state.configuration;
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.property_index", config.index);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.property_essential",
                              state.configuration_essential);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.configuration_header_present",
                              config.have_header ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.configuration_core_present",
                              config.have_core ? 1U : 0U);
                if (config.have_header) {
                    emit_u8_field(store, block, (*io_order)++,
                                  "tiled_image.configuration_version",
                                  config.version);
                    emit_u32_field(store, block, (*io_order)++,
                                   "tiled_image.configuration_flags",
                                   config.flags);
                }
                if (config.have_core) {
                    emit_u32_field(store, block, (*io_order)++,
                                   "tiled_image.tile_width", config.tile_width);
                    emit_u32_field(store, block, (*io_order)++,
                                   "tiled_image.tile_height",
                                   config.tile_height);
                    emit_u8_field(store, block, (*io_order)++,
                                  "tiled_image.extra_dimension_count",
                                  config.extra_dimension_count);
                    for (uint8_t j = 0U; j < config.stored_dimension_count;
                         ++j) {
                        emit_u32_field(store, block, (*io_order)++,
                                       "tiled_image.dimension_index", j);
                        emit_u32_field(store, block, (*io_order)++,
                                       "tiled_image.dimension_size",
                                       config.dimension_sizes[j]);
                    }
                    emit_u32_field(store, block, (*io_order)++,
                                   "tiled_image.conditional_payload_bytes",
                                   config.conditional_payload_bytes);
                }
                if (config.dimensions_truncated) {
                    emit_u8_field(store, block, (*io_order)++,
                                  "tiled_image.dimensions_truncated", 1U);
                }
            }
            if (state.ispe) {
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.output_width", state.ispe->width);
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.output_height", state.ispe->height);
            }
            if (state.have_tile_grid) {
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.tile_columns", state.tile_columns);
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.tile_rows", state.tile_rows);
                emit_u64_field(store, block, (*io_order)++,
                               "tiled_image.expected_tile_count",
                               state.expected_tile_count);
            }
            if (state.tile_count_overflow) {
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.tile_count_overflow", 1U);
            }
            if (state.location) {
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.data_reference_index",
                               state.location->data_reference_index);
            }
            emit_u8_field(store, block, (*io_order)++,
                          "tiled_image.data_reference_present",
                          state.data_reference_present ? 1U : 0U);
            emit_u8_field(store, block, (*io_order)++,
                          "tiled_image.data_reference_valid",
                          state.data_reference_valid ? 1U : 0U);
            if (state.data_reference) {
                const DataReferenceEntry& data_ref = *state.data_reference;
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.data_reference_type",
                               data_ref.type);
                emit_text_field(store, block, (*io_order)++,
                                "tiled_image.data_reference_type_name",
                                bmff_fourcc_display_name(data_ref.type));
                if (data_ref.have_header) {
                    emit_u8_field(store, block, (*io_order)++,
                                  "tiled_image.data_reference_version",
                                  data_ref.version);
                    emit_u32_field(store, block, (*io_order)++,
                                   "tiled_image.data_reference_flags",
                                   data_ref.flags);
                }
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.external_tiles",
                              data_ref.external_tiles_urls ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.sequential_order",
                              data_ref.sequential_order ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.offset_field_bytes",
                              data_ref.offset_field_bytes);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.size_field_bytes",
                              data_ref.size_field_bytes);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.input_item_count_field_bytes",
                              data_ref.input_item_count_field_bytes);
                emit_u64_field(store, block, (*io_order)++,
                               "tiled_image.input_item_count",
                               data_ref.input_item_count);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.input_item_count_matches",
                              state.input_item_count_matches ? 1U : 0U);

                if (data_ref.external_tiles_urls) {
                    emit_u8_field(store, block, (*io_order)++,
                                  "tiled_image.directory_id_present",
                                  data_ref.directory_id_present ? 1U : 0U);
                    if (data_ref.directory_id_present) {
                        emit_u16_field(store, block, (*io_order)++,
                                       "tiled_image.directory_id_start",
                                       data_ref.directory_id_start);
                        emit_u16_field(store, block, (*io_order)++,
                                       "tiled_image.directory_id_end",
                                       data_ref.directory_id_end);
                    }
                    emit_u64_field(store, block, (*io_order)++,
                                   "tiled_image.tile_id_start",
                                   data_ref.tile_id_start);
                    const TiledImageUrl* urls[3] {
                        &data_ref.base_url, &data_ref.url_extension,
                        &data_ref.tile_request_template
                    };
                    const char* byte_fields[3] {
                        "tiled_image.base_url_bytes",
                        "tiled_image.url_extension_bytes",
                        "tiled_image.tile_request_template_bytes"
                    };
                    const char* text_fields[3] {
                        "tiled_image.base_url", "tiled_image.url_extension",
                        "tiled_image.tile_request_template"
                    };
                    const char* truncated_fields[3] {
                        "tiled_image.base_url_truncated",
                        "tiled_image.url_extension_truncated",
                        "tiled_image.tile_request_template_truncated"
                    };
                    for (uint32_t j = 0U; j < 3U; ++j) {
                        emit_u32_field(store, block, (*io_order)++,
                                       byte_fields[j], urls[j]->total_bytes);
                        if (!urls[j]->truncated) {
                            emit_utf8_field(
                                store, block, (*io_order)++, text_fields[j],
                                std::string_view(urls[j]->bytes.data(),
                                                 urls[j]->stored_bytes));
                        } else {
                            emit_u8_field(store, block, (*io_order)++,
                                          truncated_fields[j], 1U);
                        }
                    }
                }
            }
            if (state.data_reference_valid) {
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.conditional_payload_valid",
                              state.conditional_payload_valid ? 1U : 0U);
            }
            if (state.tipa_present) {
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.tile_item_type",
                               state.tile_item_type);
                emit_text_field(store, block, (*io_order)++,
                                "tiled_image.tile_item_type_name",
                                bmff_fourcc_display_name(state.tile_item_type));
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.tipa_version", state.tipa_version);
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.tipa_flags", state.tipa_flags);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.tipa_valid",
                              state.tipa_valid ? 1U : 0U);
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.tile_property_association_count",
                               state.tile_property_association_count);
                for (uint32_t j = 0U;
                     j < state.stored_tile_property_association_count; ++j) {
                    const TilePropertyAssociation& association
                        = state.tile_property_associations[j];
                    emit_u32_field(store, block, (*io_order)++,
                                   "tiled_image.tile_property_index",
                                   association.property_index);
                    emit_u8_field(store, block, (*io_order)++,
                                  "tiled_image.tile_property_essential",
                                  association.essential);
                    if (association.have_property_type) {
                        emit_u32_field(store, block, (*io_order)++,
                                       "tiled_image.tile_property_type",
                                       association.property_type);
                        emit_text_field(store, block, (*io_order)++,
                                        "tiled_image.tile_property_type_name",
                                        bmff_fourcc_display_name(
                                            association.property_type));
                    }
                }
                if (state.tile_property_associations_truncated) {
                    emit_u8_field(
                        store, block, (*io_order)++,
                        "tiled_image.tile_property_associations_truncated", 1U);
                }
            }
            if (state.offset_table_present && state.data_reference) {
                const DataReferenceEntry& data_ref = *state.data_reference;
                emit_u64_field(store, block, (*io_order)++,
                               "tiled_image.offset_table_start",
                               data_ref.tile_offset_table_start);
                emit_u32_field(store, block, (*io_order)++,
                               "tiled_image.offset_table_size",
                               data_ref.tile_offset_table_size);
                emit_u64_field(store, block, (*io_order)++,
                               "tiled_image.expected_offset_table_size",
                               state.expected_offset_table_size);
                emit_u64_field(store, block, (*io_order)++,
                               "tiled_image.logical_item_data_size",
                               state.logical_item_data_size);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.offset_table_valid",
                              state.offset_table_valid ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.tile_offsets_valid",
                              state.tile_offsets_valid ? 1U : 0U);
                emit_u8_field(store, block, (*io_order)++,
                              "tiled_image.tile_sizes_validated",
                              state.tile_sizes_validated ? 1U : 0U);
                emit_u64_field(store, block, (*io_order)++,
                               "tiled_image.empty_tile_count",
                               state.empty_tile_count);
                for (uint32_t j = 0U; j < state.stored_offset_row_count; ++j) {
                    const TiledImageOffsetRow& row = state.offset_rows[j];
                    emit_u64_field(store, block, (*io_order)++,
                                   "tiled_image.offset.tile_index",
                                   row.tile_index);
                    emit_u64_field(store, block, (*io_order)++,
                                   "tiled_image.offset.start",
                                   row.start_offset);
                    emit_u8_field(store, block, (*io_order)++,
                                  "tiled_image.offset.empty",
                                  row.empty ? 1U : 0U);
                    if (row.have_size) {
                        emit_u64_field(store, block, (*io_order)++,
                                       "tiled_image.offset.size", row.size);
                    }
                }
                if (state.offset_table_validation_truncated) {
                    emit_u8_field(
                        store, block, (*io_order)++,
                        "tiled_image.offset_table_validation_truncated", 1U);
                }
            }
        }
        emit_u32_field(store, block, (*io_order)++,
                       "tiled_image.complete_configuration_valid_count",
                       complete_valid_count);
        emit_u32_field(store, block, (*io_order)++,
                       "tiled_image.complete_configuration_invalid_count",
                       state_count - complete_valid_count);
    }

    static bool bmff_collect_iref_edges(std::span<const std::byte> bytes,
                                        const BmffBox& iref,
                                        PrimaryProps* out) noexcept
    {
        if (!out) {
            return false;
        }

        const uint64_t payload_off = iref.offset + iref.header_size;
        const uint64_t payload_end = iref.offset + iref.size;
        if (payload_off + 4 > payload_end) {
            return false;
        }

        const uint8_t version = u8(bytes[payload_off + 0]);
        if (version > 1) {
            return false;
        }

        uint64_t off                  = payload_off + 4;  // skip FullBox header
        const uint32_t kMaxBoxes      = 1U << 16;
        uint32_t seen                 = 0;
        const uint32_t kMaxRefsPerBox = 1U << 14;
        const uint32_t kMaxTotalRefs  = 1U << 18;
        while (off + 8 <= payload_end) {
            seen += 1;
            if (seen > kMaxBoxes) {
                return false;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, off, payload_end, &child)) {
                break;
            }

            const uint64_t child_payload_off = child.offset + child.header_size;
            const uint64_t child_payload_end = child.offset + child.size;
            if (child_payload_off > child_payload_end
                || child_payload_end > bytes.size()) {
                return false;
            }

            uint64_t p            = child_payload_off;
            uint32_t from_item_id = 0;
            if (version == 0) {
                uint16_t from16 = 0;
                if (!read_u16be(bytes, p, &from16)) {
                    return false;
                }
                from_item_id = static_cast<uint32_t>(from16);
                p += 2;
            } else {
                if (!read_u32be(bytes, p, &from_item_id)) {
                    return false;
                }
                p += 4;
            }

            uint16_t ref_count = 0;
            if (!read_u16be(bytes, p, &ref_count)) {
                return false;
            }
            p += 2;
            if (ref_count > kMaxRefsPerBox) {
                return false;
            }

            for (uint32_t i = 0; i < ref_count; ++i) {
                uint32_t to_item_id = 0;
                if (version == 0) {
                    uint16_t to16 = 0;
                    if (!read_u16be(bytes, p, &to16)) {
                        return false;
                    }
                    to_item_id = static_cast<uint32_t>(to16);
                    p += 2;
                } else {
                    if (!read_u32be(bytes, p, &to_item_id)) {
                        return false;
                    }
                    p += 4;
                }

                if (!append_iref_edge(out, child.type, from_item_id,
                                      to_item_id)) {
                    return false;
                }
                if (out->iref_edge_total > kMaxTotalRefs) {
                    return false;
                }
            }

            off += child.size;
            if (child.size == 0) {
                break;
            }
        }
        return true;
    }

    static bool bmff_collect_item_groups(std::span<const std::byte> bytes,
                                         const BmffBox& grpl,
                                         PrimaryProps* out) noexcept
    {
        if (!out) {
            return false;
        }

        const uint64_t payload_off = grpl.offset + grpl.header_size;
        const uint64_t payload_end = grpl.offset + grpl.size;
        if (payload_off > payload_end || payload_end > bytes.size()) {
            return false;
        }

        uint64_t off                = payload_off;
        const uint32_t kMaxBoxes    = 1U << 16;
        const uint32_t kMaxEntities = 1U << 18;
        uint32_t seen               = 0U;
        while (off + 8 <= payload_end) {
            seen += 1U;
            if (seen > kMaxBoxes) {
                return false;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, off, payload_end, &child)) {
                break;
            }

            const uint64_t child_payload_off = child.offset + child.header_size;
            const uint64_t child_payload_end = child.offset + child.size;
            if (child_payload_off > child_payload_end
                || child_payload_end > bytes.size()
                || child_payload_off + 12U > child_payload_end) {
                return false;
            }

            const uint8_t version = u8(bytes[child_payload_off + 0U]);
            if (version != 0U) {
                off += child.size;
                if (child.size == 0U) {
                    break;
                }
                continue;
            }

            uint64_t p        = child_payload_off + 4U;  // FullBox header.
            uint32_t group_id = 0U;
            if (!read_u32be(bytes, p, &group_id)) {
                return false;
            }
            p += 4U;

            uint32_t entity_count = 0U;
            if (!read_u32be(bytes, p, &entity_count)) {
                return false;
            }
            p += 4U;
            if (entity_count > kMaxEntities
                || entity_count > ((child_payload_end - p) / 4U)) {
                return false;
            }

            if (out->item_group_total == UINT32_MAX) {
                return false;
            }
            out->item_group_total += 1U;

            ItemGroup* group = nullptr;
            if (out->item_group_count < out->item_groups.size()) {
                group               = &out->item_groups[out->item_group_count];
                *group              = ItemGroup {};
                group->group_type   = child.type;
                group->group_id     = group_id;
                group->entity_count = entity_count;
                out->item_group_count += 1U;
            } else {
                out->item_group_truncated = true;
            }

            for (uint32_t i = 0U; i < entity_count; ++i) {
                uint32_t entity_id = 0U;
                if (!read_u32be(bytes, p, &entity_id)) {
                    return false;
                }
                p += 4U;

                if (!group) {
                    continue;
                }
                if (out->have_item_id && entity_id == out->item_id) {
                    group->contains_primary = true;
                }
                if (group->entity_id_count < group->entity_ids.size()) {
                    group->entity_ids[group->entity_id_count] = entity_id;
                    group->entity_id_count += 1U;
                } else {
                    group->entity_truncated = true;
                }
            }

            off += child.size;
            if (child.size == 0U) {
                break;
            }
        }

        return true;
    }

    static bool bmff_bytes_valid_utf8(std::span<const std::byte> bytes) noexcept
    {
        size_t i = 0U;
        while (i < bytes.size()) {
            const uint8_t c0 = u8(bytes[i]);
            if ((c0 & 0x80U) == 0U) {
                i += 1U;
                continue;
            }

            uint32_t needed = 0U;
            uint32_t min_cp = 0U;
            uint32_t cp     = 0U;
            if ((c0 & 0xE0U) == 0xC0U) {
                needed = 1U;
                min_cp = 0x80U;
                cp     = c0 & 0x1FU;
            } else if ((c0 & 0xF0U) == 0xE0U) {
                needed = 2U;
                min_cp = 0x800U;
                cp     = c0 & 0x0FU;
            } else if ((c0 & 0xF8U) == 0xF0U) {
                needed = 3U;
                min_cp = 0x10000U;
                cp     = c0 & 0x07U;
            } else {
                return false;
            }
            if (i + needed >= bytes.size()) {
                return false;
            }
            for (uint32_t j = 0U; j < needed; ++j) {
                const uint8_t cx = u8(bytes[i + 1U + j]);
                if ((cx & 0xC0U) != 0x80U) {
                    return false;
                }
                cp = (cp << 6U) | static_cast<uint32_t>(cx & 0x3FU);
            }
            if (cp < min_cp || cp > 0x10FFFFU
                || (cp >= 0xD800U && cp <= 0xDFFFU)) {
                return false;
            }
            i += 1U + needed;
        }
        return true;
    }

    static bool read_tiled_image_utf8_string(std::span<const std::byte> bytes,
                                             uint64_t* io_offset, uint64_t end,
                                             TiledImageUrl* out) noexcept
    {
        static constexpr uint64_t kMaxStringScanBytes = 1U << 20U;
        if (!io_offset || !out || *io_offset > end || end > bytes.size()) {
            return false;
        }
        *out                 = TiledImageUrl {};
        const uint64_t start = *io_offset;
        uint64_t p           = start;
        while (p < end && bytes[p] != std::byte { 0U }) {
            if (p - start >= kMaxStringScanBytes) {
                return false;
            }
            p += 1U;
        }
        if (p >= end || p - start > UINT32_MAX) {
            return false;
        }
        const uint32_t length = static_cast<uint32_t>(p - start);
        const std::span<const std::byte> text(bytes.data() + start, length);
        if (!bmff_bytes_valid_utf8(text)) {
            return false;
        }
        out->total_bytes  = length;
        out->stored_bytes = std::min(length, kMaxTiledImageUrlBytes);
        out->truncated    = out->stored_bytes != length;
        for (uint32_t i = 0U; i < out->stored_bytes; ++i) {
            out->bytes[i] = static_cast<char>(u8(text[i]));
        }
        *io_offset = p + 1U;
        return true;
    }

    static bool bmff_parse_deti(std::span<const std::byte> bytes,
                                const BmffBox& box,
                                DataReferenceEntry* out) noexcept
    {
        if (!out || box.type != fourcc('d', 'e', 't', 'i')) {
            return false;
        }
        const uint64_t payload_off = box.offset + box.header_size;
        const uint64_t payload_end = box.offset + box.size;
        if (payload_off + 4U > payload_end || payload_end > bytes.size()) {
            return false;
        }

        out->have_header = true;
        out->version     = u8(bytes[payload_off]);
        out->flags = (static_cast<uint32_t>(u8(bytes[payload_off + 1U])) << 16U)
                     | (static_cast<uint32_t>(u8(bytes[payload_off + 2U]))
                        << 8U)
                     | static_cast<uint32_t>(u8(bytes[payload_off + 3U]));
        if (out->version != 0U || (out->flags & 0xFFFF00U) != 0U) {
            return false;
        }

        const uint8_t offset_code = static_cast<uint8_t>(out->flags & 0x03U);
        const uint8_t size_code   = static_cast<uint8_t>((out->flags >> 2U)
                                                         & 0x03U);
        const uint8_t count_code  = static_cast<uint8_t>((out->flags >> 5U)
                                                         & 0x03U);
        static constexpr uint8_t kOffsetBytes[4] { 4U, 5U, 6U, 8U };
        static constexpr uint8_t kSizeBytes[4] { 0U, 3U, 4U, 8U };
        static constexpr uint8_t kCountBytes[4] { 1U, 2U, 4U, 8U };
        out->offset_field_bytes           = kOffsetBytes[offset_code];
        out->size_field_bytes             = kSizeBytes[size_code];
        out->input_item_count_field_bytes = kCountBytes[count_code];
        out->sequential_order             = (out->flags & 0x10U) != 0U;
        out->external_tiles_urls          = (out->flags & 0x80U) != 0U;

        uint64_t p = payload_off + 4U;
        if (!read_uint_be_n(bytes, p, out->input_item_count_field_bytes,
                            &out->input_item_count)) {
            return false;
        }
        p += out->input_item_count_field_bytes;

        if (out->external_tiles_urls) {
            if (p >= payload_end) {
                return false;
            }
            const uint8_t directory_flags = u8(bytes[p]);
            p += 1U;
            if ((directory_flags & 0xFEU) != 0U) {
                return false;
            }
            out->directory_id_present = (directory_flags & 0x01U) != 0U;
            if (out->directory_id_present) {
                if (!read_u16be(bytes, p, &out->directory_id_start)
                    || !read_u16be(bytes, p + 2U, &out->directory_id_end)) {
                    return false;
                }
                p += 4U;
            }
            if (!read_u64be(bytes, p, &out->tile_id_start)) {
                return false;
            }
            p += 8U;
            if (!read_tiled_image_utf8_string(bytes, &p, payload_end,
                                              &out->base_url)
                || !read_tiled_image_utf8_string(bytes, &p, payload_end,
                                                 &out->url_extension)
                || !read_tiled_image_utf8_string(bytes, &p, payload_end,
                                                 &out->tile_request_template)) {
                return false;
            }
        } else {
            if (!read_uint_be_n(bytes, p, out->offset_field_bytes,
                                &out->tile_offset_table_start)) {
                return false;
            }
            p += out->offset_field_bytes;
            if (!read_u32be(bytes, p, &out->tile_offset_table_size)) {
                return false;
            }
            p += 4U;
        }
        out->syntax_valid = p == payload_end;
        return out->syntax_valid;
    }

    static bool bmff_collect_data_references(std::span<const std::byte> bytes,
                                             const BmffBox& dinf,
                                             PrimaryProps* out) noexcept
    {
        if (!out) {
            return false;
        }
        const uint64_t payload_off = dinf.offset + dinf.header_size;
        const uint64_t payload_end = dinf.offset + dinf.size;
        if (payload_off > payload_end || payload_end > bytes.size()) {
            return false;
        }

        BmffBox dref {};
        bool have_dref        = false;
        uint64_t child_offset = payload_off;
        uint32_t child_count  = 0U;
        while (child_offset + 8U <= payload_end) {
            if (child_count >= (1U << 16U)) {
                return false;
            }
            child_count += 1U;
            BmffBox child {};
            if (!parse_bmff_box(bytes, child_offset, payload_end, &child)) {
                return false;
            }
            if (child.type == fourcc('d', 'r', 'e', 'f')) {
                if (have_dref) {
                    return false;
                }
                dref      = child;
                have_dref = true;
            }
            child_offset += child.size;
        }
        if (child_offset != payload_end || !have_dref) {
            return false;
        }

        const uint64_t dref_payload_off = dref.offset + dref.header_size;
        const uint64_t dref_payload_end = dref.offset + dref.size;
        uint32_t entry_count            = 0U;
        if (dref_payload_off + 8U > dref_payload_end
            || u8(bytes[dref_payload_off]) != 0U
            || u8(bytes[dref_payload_off + 1U]) != 0U
            || u8(bytes[dref_payload_off + 2U]) != 0U
            || u8(bytes[dref_payload_off + 3U]) != 0U
            || !read_u32be(bytes, dref_payload_off + 4U, &entry_count)
            || entry_count > (1U << 16U)) {
            return false;
        }

        out->have_data_references = true;
        out->data_reference_total = entry_count;
        uint64_t p                = dref_payload_off + 8U;
        for (uint32_t i = 0U; i < entry_count; ++i) {
            if (p + 8U > dref_payload_end) {
                return false;
            }
            BmffBox entry_box {};
            if (!parse_bmff_box(bytes, p, dref_payload_end, &entry_box)) {
                return false;
            }
            DataReferenceEntry entry {};
            entry.index = i + 1U;
            entry.type  = entry_box.type;
            if (entry.type != fourcc('d', 'e', 't', 'i')) {
                entry.syntax_valid = true;
            } else {
                (void)bmff_parse_deti(bytes, entry_box, &entry);
            }
            if (out->data_reference_count < out->data_references.size()) {
                out->data_references[out->data_reference_count] = entry;
                out->data_reference_count += 1U;
            } else {
                out->data_references_truncated = true;
            }
            p += entry_box.size;
        }
        out->data_references_valid = p == dref_payload_end;
        return out->data_references_valid;
    }

    static const DataReferenceEntry*
    find_data_reference(const PrimaryProps& p, uint16_t index) noexcept
    {
        for (uint32_t i = 0U; i < p.data_reference_count; ++i) {
            if (p.data_references[i].index == index) {
                return &p.data_references[i];
            }
        }
        return nullptr;
    }

    static bool bmff_collect_item_locations(std::span<const std::byte> bytes,
                                            const BmffBox& iloc,
                                            PrimaryProps* out) noexcept
    {
        if (!out) {
            return false;
        }

        const uint64_t payload_off = iloc.offset + iloc.header_size;
        const uint64_t payload_end = iloc.offset + iloc.size;
        if (payload_off + 8U > payload_end || payload_end > bytes.size()) {
            return false;
        }

        const uint8_t version = u8(bytes[payload_off + 0U]);
        if (version > 2U) {
            return false;
        }

        uint64_t p = payload_off + 4U;  // FullBox header.
        if (p + 2U > payload_end) {
            return false;
        }
        const uint8_t size_a    = u8(bytes[p]);
        const uint8_t size_b    = u8(bytes[p + 1U]);
        const uint8_t off_size  = static_cast<uint8_t>((size_a >> 4U) & 0x0FU);
        const uint8_t len_size  = static_cast<uint8_t>(size_a & 0x0FU);
        const uint8_t base_size = static_cast<uint8_t>((size_b >> 4U) & 0x0FU);
        const uint8_t idx_size  = (version > 0U)
                                      ? static_cast<uint8_t>(size_b & 0x0FU)
                                      : 0U;
        if (off_size > 8U || len_size > 8U || base_size > 8U || idx_size > 8U) {
            return false;
        }
        p += 2U;

        uint32_t item_count = 0U;
        if (version < 2U) {
            uint16_t count16 = 0U;
            if (p + 2U > payload_end) {
                return false;
            }
            if (!read_u16be(bytes, p, &count16)) {
                return false;
            }
            item_count = static_cast<uint32_t>(count16);
            p += 2U;
        } else {
            if (p + 4U > payload_end) {
                return false;
            }
            if (!read_u32be(bytes, p, &item_count)) {
                return false;
            }
            p += 4U;
        }

        const uint32_t kMaxItems          = 1U << 18;
        const uint32_t kMaxExtentsPerItem = 1U << 14;
        if (item_count > kMaxItems) {
            return false;
        }

        out->have_item_location_sizes       = true;
        out->item_location_version          = version;
        out->item_location_offset_size      = off_size;
        out->item_location_length_size      = len_size;
        out->item_location_base_offset_size = base_size;
        out->item_location_index_size       = idx_size;

        for (uint32_t item_i = 0U; item_i < item_count; ++item_i) {
            if (p >= payload_end) {
                return false;
            }

            uint32_t item_id = 0U;
            if (version < 2U) {
                uint16_t item16 = 0U;
                if (p + 2U > payload_end) {
                    return false;
                }
                if (!read_u16be(bytes, p, &item16)) {
                    return false;
                }
                item_id = static_cast<uint32_t>(item16);
                p += 2U;
            } else {
                if (p + 4U > payload_end) {
                    return false;
                }
                if (!read_u32be(bytes, p, &item_id)) {
                    return false;
                }
                p += 4U;
            }

            uint16_t construction_method = 0U;
            if (version > 0U) {
                uint16_t raw_method = 0U;
                if (p + 2U > payload_end) {
                    return false;
                }
                if (!read_u16be(bytes, p, &raw_method)) {
                    return false;
                }
                construction_method = static_cast<uint16_t>(raw_method
                                                            & 0x000FU);
                p += 2U;
            }

            uint16_t data_ref_index = 0U;
            if (p + 2U > payload_end) {
                return false;
            }
            if (!read_u16be(bytes, p, &data_ref_index)) {
                return false;
            }
            p += 2U;

            uint64_t base_offset = 0U;
            if (p + base_size > payload_end) {
                return false;
            }
            if (!read_uint_be_n(bytes, p, base_size, &base_offset)) {
                return false;
            }
            p += base_size;

            uint16_t extent_count16 = 0U;
            if (p + 2U > payload_end) {
                return false;
            }
            if (!read_u16be(bytes, p, &extent_count16)) {
                return false;
            }
            p += 2U;
            const uint32_t extent_count = static_cast<uint32_t>(extent_count16);
            if (extent_count > kMaxExtentsPerItem) {
                return false;
            }

            if (out->item_location_total == UINT32_MAX) {
                return false;
            }
            out->item_location_total += 1U;

            ItemLocation* loc = nullptr;
            if (out->item_location_count < out->item_locations.size()) {
                loc          = &out->item_locations[out->item_location_count];
                *loc         = ItemLocation {};
                loc->item_id = item_id;
                loc->construction_method  = construction_method;
                loc->data_reference_index = data_ref_index;
                loc->base_offset          = base_offset;
                loc->extent_count         = extent_count;
                out->item_location_count += 1U;
            } else {
                out->item_location_truncated = true;
            }

            for (uint32_t extent_i = 0U; extent_i < extent_count; ++extent_i) {
                uint64_t extent_index  = 0U;
                uint64_t extent_offset = 0U;
                uint64_t extent_length = 0U;
                if (version > 0U && idx_size > 0U) {
                    if (p + idx_size > payload_end) {
                        return false;
                    }
                    if (!read_uint_be_n(bytes, p, idx_size, &extent_index)) {
                        return false;
                    }
                    p += idx_size;
                }
                if (p + off_size > payload_end) {
                    return false;
                }
                if (!read_uint_be_n(bytes, p, off_size, &extent_offset)) {
                    return false;
                }
                p += off_size;
                if (p + len_size > payload_end) {
                    return false;
                }
                if (!read_uint_be_n(bytes, p, len_size, &extent_length)) {
                    return false;
                }
                p += len_size;

                if (!loc) {
                    continue;
                }
                if (UINT64_MAX - loc->total_extent_bytes < extent_length) {
                    loc->length_overflow = true;
                } else {
                    loc->total_extent_bytes += extent_length;
                }
                if (loc->extent_record_count < loc->extents.size()) {
                    ItemLocationExtent& extent
                        = loc->extents[loc->extent_record_count];
                    extent.index  = extent_index;
                    extent.offset = extent_offset;
                    extent.length = extent_length;
                    loc->extent_record_count += 1U;
                } else {
                    loc->extent_truncated = true;
                }
            }
        }

        return true;
    }


    static bool bmff_decode_meta_primary(std::span<const std::byte> bytes,
                                         const BmffBox& meta,
                                         PrimaryProps* out) noexcept
    {
        if (!out) {
            return false;
        }
        // The sole caller supplies a fresh value-initialized PrimaryProps.

        const uint64_t payload_off  = meta.offset + meta.header_size;
        const uint64_t payload_size = meta.size - meta.header_size;
        if (payload_size < 4) {
            return false;
        }

        BmffBox pitm {};
        BmffBox iinf {};
        BmffBox iprp {};
        BmffBox iref {};
        BmffBox grpl {};
        BmffBox iloc {};
        BmffBox idat {};
        BmffBox dinf {};
        bool has_pitm = false;
        bool has_iinf = false;
        bool has_iprp = false;
        bool has_iref = false;
        bool has_grpl = false;
        bool has_iloc = false;
        bool has_idat = false;
        bool has_dinf = false;

        uint64_t child_off       = payload_off + 4;  // FullBox header.
        const uint64_t child_end = meta.offset + meta.size;
        const uint32_t kMaxBoxes = 1U << 16;
        uint32_t seen            = 0;
        while (child_off + 8 <= child_end) {
            seen += 1;
            if (seen > kMaxBoxes) {
                return false;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, child_off, child_end, &child)) {
                break;
            }

            if (child.type == fourcc('p', 'i', 't', 'm')) {
                pitm     = child;
                has_pitm = true;
            } else if (child.type == fourcc('i', 'i', 'n', 'f')) {
                iinf     = child;
                has_iinf = true;
            } else if (child.type == fourcc('i', 'p', 'r', 'p')) {
                iprp     = child;
                has_iprp = true;
            } else if (child.type == fourcc('i', 'r', 'e', 'f')) {
                iref     = child;
                has_iref = true;
            } else if (child.type == fourcc('g', 'r', 'p', 'l')) {
                grpl     = child;
                has_grpl = true;
            } else if (child.type == fourcc('i', 'l', 'o', 'c')) {
                iloc     = child;
                has_iloc = true;
            } else if (child.type == fourcc('i', 'd', 'a', 't')) {
                idat     = child;
                has_idat = true;
            } else if (child.type == fourcc('d', 'i', 'n', 'f')) {
                dinf     = child;
                has_dinf = true;
            }

            child_off += child.size;
            if (child.size == 0) {
                break;
            }
        }

        if (has_iinf) {
            bmff_collect_iinf_items(bytes, iinf, out);
        }

        if (has_pitm) {
            uint32_t primary_id = 0;
            if (!bmff_parse_pitm(bytes, pitm, &primary_id)) {
                return false;
            }
            out->have_item_id = true;
            out->item_id      = primary_id;
        }

        if (has_iref && out->have_item_id) {
            if (!bmff_collect_iref_edges(bytes, iref, out)) {
                return false;
            }
        }

        if (has_grpl) {
            if (!bmff_collect_item_groups(bytes, grpl, out)) {
                return false;
            }
        }

        if (has_idat) {
            if (idat.header_size > idat.size) {
                return false;
            }
            out->have_idat           = true;
            out->idat_bytes          = idat.size - idat.header_size;
            out->idat_payload_offset = idat.offset + idat.header_size;
            out->idat_payload_end    = idat.offset + idat.size;
        }

        if (has_iloc) {
            if (!bmff_collect_item_locations(bytes, iloc, out)) {
                return false;
            }
        }

        if (has_dinf) {
            (void)bmff_collect_data_references(bytes, dinf, out);
        }

        if (!has_pitm) {
            return (out->item_info_count > 0U || out->item_group_total > 0U
                    || out->item_location_total > 0U || out->have_idat);
        }

        if (!has_iprp) {
            return true;
        }

        BmffBox ipco {};
        BmffBox ipma {};
        bool has_ipco = false;
        bool has_ipma = false;

        const uint64_t iprp_payload_off = iprp.offset + iprp.header_size;
        const uint64_t iprp_payload_end = iprp.offset + iprp.size;
        if (iprp_payload_off > iprp_payload_end
            || iprp_payload_end > bytes.size()) {
            return true;
        }

        uint64_t off              = iprp_payload_off;
        const uint32_t kMaxBoxes2 = 1U << 16;
        uint32_t seen2            = 0;
        while (off + 8 <= iprp_payload_end) {
            seen2 += 1;
            if (seen2 > kMaxBoxes2) {
                break;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, off, iprp_payload_end, &child)) {
                break;
            }

            if (child.type == fourcc('i', 'p', 'c', 'o')) {
                ipco     = child;
                has_ipco = true;
            } else if (child.type == fourcc('i', 'p', 'm', 'a')) {
                ipma     = child;
                has_ipma = true;
            }

            off += child.size;
            if (child.size == 0) {
                break;
            }
        }

        std::array<IspeProp, 64> ispe {};
        std::array<U8Prop, 64> irot {};
        std::array<U8Prop, 64> imir {};
        std::array<ColrProp, 64> colr {};
        std::array<AuxCProp, 64> auxc {};
        std::array<PaspProp, 64> pasp {};
        std::array<PixiProp, 64> pixi {};
        std::array<ClapProp, 64> clap {};
        std::array<TilCProp, 64> tilc {};
        std::array<IpcoProperty, 256> properties {};
        uint32_t ispe_count       = 0;
        uint32_t irot_count       = 0;
        uint32_t imir_count       = 0;
        uint32_t colr_count       = 0;
        uint32_t auxc_count       = 0;
        uint32_t pasp_count       = 0;
        uint32_t pixi_count       = 0;
        uint32_t clap_count       = 0;
        uint32_t tilc_count       = 0;
        uint32_t property_count   = 0U;
        bool properties_truncated = false;
        if (has_ipco) {
            bmff_collect_ipco_props(bytes, ipco, &ispe, &ispe_count, &irot,
                                    &irot_count, &imir, &imir_count, &colr,
                                    &colr_count, &auxc, &auxc_count, &pasp,
                                    &pasp_count, &pixi, &pixi_count, &clap,
                                    &clap_count, &tilc, &tilc_count,
                                    &properties, &property_count,
                                    &properties_truncated, &out->ipco_summary);
            out->have_ipco_summary         = true;
            out->ipco_ispe                 = ispe;
            out->ipco_ispe_count           = ispe_count;
            out->ipco_tilc                 = tilc;
            out->ipco_tilc_count           = tilc_count;
            out->ipco_properties           = properties;
            out->ipco_property_count       = property_count;
            out->ipco_properties_truncated = properties_truncated;
        }

        if (!has_ipma) {
            return true;
        }

        bmff_apply_ipma_primary(
            bytes, ipma, out->item_id,
            std::span<const IspeProp>(ispe.data(), ispe_count),
            std::span<const U8Prop>(irot.data(), irot_count),
            std::span<const U8Prop>(imir.data(), imir_count),
            std::span<const ColrProp>(colr.data(), colr_count),
            std::span<const AuxCProp>(auxc.data(), auxc_count),
            std::span<const PaspProp>(pasp.data(), pasp_count),
            std::span<const PixiProp>(pixi.data(), pixi_count),
            std::span<const ClapProp>(clap.data(), clap_count),
            std::span<const TilCProp>(tilc.data(), tilc_count), out);
        return true;
    }


    struct ScanCtx final {
        std::span<const std::byte> bytes;
        MetaStore* store       = nullptr;
        BlockId block          = kInvalidBlockId;
        uint32_t* order        = nullptr;
        bool meta_done         = false;
        ContainerFormat format = ContainerFormat::Unknown;
        uint32_t seen_boxes    = 0;
    };


    // Keep large decode/emission scratch out of recursive scan frames.
    static void bmff_emit_meta(std::span<const std::byte> bytes,
                               const BmffBox& box, ScanCtx* ctx) noexcept
    {
        PrimaryProps p {};
        if (bmff_decode_meta_primary(bytes, box, &p)) {
            if (p.item_info_count > 0) {
                emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                               "item.info_count", p.item_info_count);
                ItemSemanticCounts semantic_counts {};
                for (uint32_t i = 0; i < p.item_info_count; ++i) {
                    const ItemInfo& info = p.item_infos[i];
                    emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "item.id", info.item_id);
                    emit_u16_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "item.protection_index",
                                   info.protection_index);
                    if (info.have_type) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "item.type", info.item_type);
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++, "item.type_name",
                                        bmff_fourcc_display_name(
                                            info.item_type));
                    }
                    if (info.name_len != 0U) {
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++, "item.name",
                                        std::string_view(info.name.data(),
                                                         info.name_len));
                    }
                    if (info.content_type_len != 0U) {
                        emit_text_field(
                            *ctx->store, ctx->block, (*ctx->order)++,
                            "item.content_type",
                            std::string_view(info.content_type.data(),
                                             info.content_type_len));
                    }
                    if (info.content_encoding_len != 0U) {
                        emit_text_field(
                            *ctx->store, ctx->block, (*ctx->order)++,
                            "item.content_encoding",
                            std::string_view(info.content_encoding.data(),
                                             info.content_encoding_len));
                    }
                    if (info.uri_type_len != 0U) {
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++, "item.uri_type",
                                        std::string_view(info.uri_type.data(),
                                                         info.uri_type_len));
                    }
                    const ItemSemantic semantic = classify_item_semantic(info);
                    count_item_semantic(semantic, &semantic_counts);
                    if (item_semantic_is_known(semantic)) {
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++, "item.semantic",
                                        item_semantic_name(semantic));
                    }
                }
                emit_item_semantic_counts(*ctx->store, ctx->block, ctx->order,
                                          semantic_counts);
            }
            emit_scene_policy_summary_fields(*ctx->store, ctx->block,
                                             ctx->order, p);
            emit_ipco_summary_fields(*ctx->store, ctx->block, ctx->order, p);
            if (p.ipma_association_total > 0U) {
                emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                               "ipma.association_count",
                               p.ipma_association_total);
                if (p.ipma_truncated) {
                    emit_u8_field(*ctx->store, ctx->block, (*ctx->order)++,
                                  "ipma.association_truncated", 1U);
                }
                for (uint32_t i = 0U; i < p.ipma_association_count; ++i) {
                    const ItemPropertyAssociation& assoc
                        = p.ipma_associations[i];
                    emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "ipma.item_id", assoc.item_id);
                    emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "ipma.property_index", assoc.property_index);
                    emit_u8_field(*ctx->store, ctx->block, (*ctx->order)++,
                                  "ipma.essential", assoc.essential);
                    if (assoc.have_property_type) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "ipma.property_type",
                                       assoc.property_type);
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++,
                                        "ipma.property_type_name",
                                        bmff_fourcc_display_name(
                                            assoc.property_type));
                    }
                }
                emit_ipma_property_type_summary_fields(*ctx->store, ctx->block,
                                                       ctx->order, p);
            }
            emit_item_group_fields(*ctx->store, ctx->block, ctx->order, p);
            emit_item_location_fields(*ctx->store, ctx->block, ctx->order, p);
            emit_tiled_image_fields(*ctx->store, ctx->block, ctx->order, bytes,
                                    p);
            emit_derived_image_fields(*ctx->store, ctx->block, ctx->order,
                                      bytes, p);
            if (p.have_item_id) {
                emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                               "meta.primary_item_id", p.item_id);
                if (const ItemInfo* primary = find_item_info(p, p.item_id)) {
                    emit_u16_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.protection_index",
                                   primary->protection_index);
                    if (primary->have_type) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.item_type", primary->item_type);
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++,
                                        "primary.item_type_name",
                                        bmff_fourcc_display_name(
                                            primary->item_type));
                    }
                    if (primary->name_len != 0U) {
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++, "primary.item_name",
                                        std::string_view(primary->name.data(),
                                                         primary->name_len));
                    }
                    if (primary->content_type_len != 0U) {
                        emit_text_field(
                            *ctx->store, ctx->block, (*ctx->order)++,
                            "primary.content_type",
                            std::string_view(primary->content_type.data(),
                                             primary->content_type_len));
                    }
                    if (primary->content_encoding_len != 0U) {
                        emit_text_field(
                            *ctx->store, ctx->block, (*ctx->order)++,
                            "primary.content_encoding",
                            std::string_view(primary->content_encoding.data(),
                                             primary->content_encoding_len));
                    }
                    if (primary->uri_type_len != 0U) {
                        emit_text_field(
                            *ctx->store, ctx->block, (*ctx->order)++,
                            "primary.uri_type",
                            std::string_view(primary->uri_type.data(),
                                             primary->uri_type_len));
                    }
                    const ItemSemantic primary_semantic
                        = classify_item_semantic(*primary);
                    if (item_semantic_is_known(primary_semantic)) {
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++,
                                        "primary.item_semantic",
                                        item_semantic_name(primary_semantic));
                        if (item_semantic_is_metadata(primary_semantic)) {
                            emit_u8_field(*ctx->store, ctx->block,
                                          (*ctx->order)++,
                                          "primary.metadata_carrier", 1U);
                        }
                        if (primary_semantic == ItemSemantic::C2pa) {
                            emit_u8_field(*ctx->store, ctx->block,
                                          (*ctx->order)++,
                                          "primary.c2pa_carrier", 1U);
                        } else if (primary_semantic == ItemSemantic::Jumbf) {
                            emit_u8_field(*ctx->store, ctx->block,
                                          (*ctx->order)++,
                                          "primary.jumbf_carrier", 1U);
                        }
                    }
                }
                if (p.have_width_height) {
                    emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.width", p.width);
                    emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.height", p.height);
                }
                if (p.have_rotation) {
                    emit_u16_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.rotation_degrees",
                                   p.rotation_degrees);
                }
                if (p.have_mirror) {
                    emit_u8_field(*ctx->store, ctx->block, (*ctx->order)++,
                                  "primary.mirror", p.mirror);
                }
                emit_primary_scene_summary_fields(*ctx->store, ctx->block,
                                                  ctx->order, p);
                if (p.have_pixel_aspect) {
                    emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.pixel_aspect_h_spacing",
                                   p.pixel_aspect_h_spacing);
                    emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.pixel_aspect_v_spacing",
                                   p.pixel_aspect_v_spacing);
                }
                if (p.have_pixel_depth) {
                    emit_u8_field(*ctx->store, ctx->block, (*ctx->order)++,
                                  "primary.pixel_depth_channel_count",
                                  p.pixel_depth_channel_count);
                    for (uint8_t ci = 0U;
                         ci < p.pixel_depth_channel_count
                         && ci < p.pixel_depth_bits_per_channel.size();
                         ++ci) {
                        emit_u8_field(*ctx->store, ctx->block, (*ctx->order)++,
                                      "primary.pixel_depth_bits_per_channel",
                                      p.pixel_depth_bits_per_channel[ci]);
                    }
                }
                if (p.have_clean_aperture) {
                    emit_i32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.clean_aperture_width_n",
                                   p.clean_aperture.width_n);
                    emit_i32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.clean_aperture_width_d",
                                   p.clean_aperture.width_d);
                    emit_i32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.clean_aperture_height_n",
                                   p.clean_aperture.height_n);
                    emit_i32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.clean_aperture_height_d",
                                   p.clean_aperture.height_d);
                    emit_i32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.clean_aperture_horiz_off_n",
                                   p.clean_aperture.horiz_off_n);
                    emit_i32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.clean_aperture_horiz_off_d",
                                   p.clean_aperture.horiz_off_d);
                    emit_i32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.clean_aperture_vert_off_n",
                                   p.clean_aperture.vert_off_n);
                    emit_i32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.clean_aperture_vert_off_d",
                                   p.clean_aperture.vert_off_d);
                }
                if (p.have_color) {
                    emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "primary.color_type", p.color_type);
                    emit_text_field(*ctx->store, ctx->block, (*ctx->order)++,
                                    "primary.color_type_name",
                                    bmff_fourcc_display_name(p.color_type));
                    if (p.have_nclx) {
                        emit_u16_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.nclx_colour_primaries",
                                       p.colour_primaries);
                        emit_u16_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.nclx_transfer_characteristics",
                                       p.transfer_characteristics);
                        emit_u16_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.nclx_matrix_coefficients",
                                       p.matrix_coefficients);
                        if (p.have_full_range_flag) {
                            emit_u8_field(*ctx->store, ctx->block,
                                          (*ctx->order)++,
                                          "primary.nclx_full_range_flag",
                                          p.full_range_flag);
                        }
                    }
                    if (p.color_profile_bytes != 0U) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.color_profile_bytes",
                                       p.color_profile_bytes);
                    }
                }
                if (p.iref_edge_total > 0) {
                    emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "iref.edge_count", p.iref_edge_total);
                    if (p.iref_truncated) {
                        emit_u8_field(*ctx->store, ctx->block, (*ctx->order)++,
                                      "iref.edge_truncated", 1);
                    }
                    uint32_t auxl_edge_count = 0;
                    uint32_t dimg_edge_count = 0;
                    uint32_t thmb_edge_count = 0;
                    uint32_t cdsc_edge_count = 0;
                    std::array<uint32_t, 512> auxl_from_ids {};
                    std::array<uint32_t, 512> auxl_to_ids {};
                    std::array<uint32_t, 512> dimg_from_ids {};
                    std::array<uint32_t, 512> dimg_to_ids {};
                    std::array<uint32_t, 512> thmb_from_ids {};
                    std::array<uint32_t, 512> thmb_to_ids {};
                    std::array<uint32_t, 512> cdsc_from_ids {};
                    std::array<uint32_t, 512> cdsc_to_ids {};
                    std::array<uint32_t, 512> auxl_item_ids {};
                    std::array<uint32_t, 512> auxl_item_out_counts {};
                    std::array<uint32_t, 512> auxl_item_in_counts {};
                    std::array<uint32_t, 512> dimg_item_ids {};
                    std::array<uint32_t, 512> dimg_item_out_counts {};
                    std::array<uint32_t, 512> dimg_item_in_counts {};
                    std::array<uint32_t, 512> thmb_item_ids {};
                    std::array<uint32_t, 512> thmb_item_out_counts {};
                    std::array<uint32_t, 512> thmb_item_in_counts {};
                    std::array<uint32_t, 512> cdsc_item_ids {};
                    std::array<uint32_t, 512> cdsc_item_out_counts {};
                    std::array<uint32_t, 512> cdsc_item_in_counts {};
                    uint32_t auxl_from_count = 0;
                    uint32_t auxl_to_count   = 0;
                    uint32_t dimg_from_count = 0;
                    uint32_t dimg_to_count   = 0;
                    uint32_t thmb_from_count = 0;
                    uint32_t thmb_to_count   = 0;
                    uint32_t cdsc_from_count = 0;
                    uint32_t cdsc_to_count   = 0;
                    uint32_t auxl_item_count = 0;
                    uint32_t dimg_item_count = 0;
                    uint32_t thmb_item_count = 0;
                    uint32_t cdsc_item_count = 0;
                    std::array<uint32_t, 512> iref_item_ids {};
                    std::array<uint32_t, 512> iref_item_out_edge_counts {};
                    std::array<uint32_t, 512> iref_item_in_edge_counts {};
                    uint32_t iref_item_count = 0;
                    std::array<uint32_t, 32> dynamic_iref_types {};
                    std::array<std::array<char, 5>, 32> dynamic_iref_tokens {};
                    uint32_t dynamic_iref_type_count = 0;
                    for (uint32_t i = 0; i < p.iref_edge_count; ++i) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.ref_type",
                                       p.iref_edges[i].ref_type);
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++, "iref.ref_type_name",
                                        bmff_fourcc_display_name(
                                            p.iref_edges[i].ref_type));
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.from_item_id",
                                       p.iref_edges[i].from_item_id);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.to_item_id",
                                       p.iref_edges[i].to_item_id);
                        bump_item_edge_count(iref_item_ids,
                                             iref_item_out_edge_counts,
                                             &iref_item_count,
                                             p.iref_edges[i].from_item_id);
                        bump_item_edge_count(iref_item_ids,
                                             iref_item_in_edge_counts,
                                             &iref_item_count,
                                             p.iref_edges[i].to_item_id);
                        if (!bmff_is_known_typed_iref_relation(
                                p.iref_edges[i].ref_type)) {
                            std::array<char, 5> token {};
                            if (bmff_fourcc_field_token(p.iref_edges[i].ref_type,
                                                        &token)) {
                                bool found_dynamic = false;
                                for (uint32_t ti = 0;
                                     ti < dynamic_iref_type_count; ++ti) {
                                    if (dynamic_iref_types[ti]
                                        == p.iref_edges[i].ref_type) {
                                        found_dynamic = true;
                                        break;
                                    }
                                }
                                if (!found_dynamic
                                    && dynamic_iref_type_count
                                           < dynamic_iref_types.size()) {
                                    dynamic_iref_types[dynamic_iref_type_count]
                                        = p.iref_edges[i].ref_type;
                                    dynamic_iref_tokens[dynamic_iref_type_count]
                                        = token;
                                    dynamic_iref_type_count += 1U;
                                }
                                emit_iref_typed_edge_fields(
                                    *ctx->store, ctx->block, ctx->order,
                                    std::string_view(token.data(), 4U),
                                    p.iref_edges[i].from_item_id,
                                    p.iref_edges[i].to_item_id);
                            }
                        }
                        if (p.iref_edges[i].ref_type
                            == fourcc('a', 'u', 'x', 'l')) {
                            auxl_edge_count += 1;
                            push_primary_rel_unique(
                                auxl_from_ids, &auxl_from_count,
                                p.iref_edges[i].from_item_id);
                            push_primary_rel_unique(auxl_to_ids, &auxl_to_count,
                                                    p.iref_edges[i].to_item_id);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.auxl.from_item_id",
                                           p.iref_edges[i].from_item_id);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.auxl.to_item_id",
                                           p.iref_edges[i].to_item_id);
                            emit_iref_semantic_edge_fields(
                                *ctx->store, ctx->block, ctx->order,
                                p.iref_edges[i].ref_type, "auxl",
                                p.iref_edges[i].from_item_id,
                                p.iref_edges[i].to_item_id);
                            bump_item_edge_count(auxl_item_ids,
                                                 auxl_item_out_counts,
                                                 &auxl_item_count,
                                                 p.iref_edges[i].from_item_id);
                            bump_item_edge_count(auxl_item_ids,
                                                 auxl_item_in_counts,
                                                 &auxl_item_count,
                                                 p.iref_edges[i].to_item_id);
                            emit_text_field(
                                *ctx->store, ctx->block, (*ctx->order)++,
                                "iref.auxl.semantic",
                                aux_semantic_name(find_aux_item_semantic(
                                    p, p.iref_edges[i].from_item_id)));
                            if (const AuxItemInfo* info = find_aux_item_info(
                                    p, p.iref_edges[i].from_item_id)) {
                                if (info->aux_type_len > 0) {
                                    emit_text_field(
                                        *ctx->store, ctx->block,
                                        (*ctx->order)++, "iref.auxl.type",
                                        std::string_view(info->aux_type.data(),
                                                         info->aux_type_len));
                                }
                                if (info->aux_subtype_len > 0) {
                                    const AuxSubtypeInterpretation interp
                                        = interpret_aux_subtype(
                                            std::span<const std::byte>(
                                                info->aux_subtype.data(),
                                                info->aux_subtype_len),
                                            info->aux_subtype_total_len,
                                            info->aux_subtype_truncated);
                                    emit_text_field(*ctx->store, ctx->block,
                                                    (*ctx->order)++,
                                                    "iref.auxl.subtype_kind",
                                                    interp.kind);
                                    if (interp.has_text) {
                                        emit_text_field(
                                            *ctx->store, ctx->block,
                                            (*ctx->order)++,
                                            "iref.auxl.subtype_text",
                                            std::string_view(interp.text.data(),
                                                             interp.text_len));
                                        if (interp.kind == "uuid") {
                                            emit_text_field(
                                                *ctx->store, ctx->block,
                                                (*ctx->order)++,
                                                "iref.auxl.subtype_uuid",
                                                std::string_view(
                                                    interp.text.data(),
                                                    interp.text_len));
                                        }
                                    }
                                    if (interp.has_u32) {
                                        emit_u32_field(*ctx->store, ctx->block,
                                                       (*ctx->order)++,
                                                       "iref.auxl.subtype_u32",
                                                       interp.u32);
                                    }
                                    if (interp.has_u64) {
                                        emit_u64_field(*ctx->store, ctx->block,
                                                       (*ctx->order)++,
                                                       "iref.auxl.subtype_u64",
                                                       interp.u64);
                                    }
                                    const std::string hex = bytes_to_hex_string(
                                        std::span<const std::byte>(
                                            info->aux_subtype.data(),
                                            info->aux_subtype_len));
                                    emit_text_field(*ctx->store, ctx->block,
                                                    (*ctx->order)++,
                                                    "iref.auxl.subtype_hex",
                                                    hex);
                                }
                            }
                        } else if (p.iref_edges[i].ref_type
                                   == fourcc('d', 'i', 'm', 'g')) {
                            dimg_edge_count += 1;
                            push_primary_rel_unique(
                                dimg_from_ids, &dimg_from_count,
                                p.iref_edges[i].from_item_id);
                            push_primary_rel_unique(dimg_to_ids, &dimg_to_count,
                                                    p.iref_edges[i].to_item_id);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.dimg.from_item_id",
                                           p.iref_edges[i].from_item_id);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.dimg.to_item_id",
                                           p.iref_edges[i].to_item_id);
                            emit_iref_semantic_edge_fields(
                                *ctx->store, ctx->block, ctx->order,
                                p.iref_edges[i].ref_type, "dimg",
                                p.iref_edges[i].from_item_id,
                                p.iref_edges[i].to_item_id);
                            bump_item_edge_count(dimg_item_ids,
                                                 dimg_item_out_counts,
                                                 &dimg_item_count,
                                                 p.iref_edges[i].from_item_id);
                            bump_item_edge_count(dimg_item_ids,
                                                 dimg_item_in_counts,
                                                 &dimg_item_count,
                                                 p.iref_edges[i].to_item_id);
                        } else if (p.iref_edges[i].ref_type
                                   == fourcc('t', 'h', 'm', 'b')) {
                            thmb_edge_count += 1;
                            push_primary_rel_unique(
                                thmb_from_ids, &thmb_from_count,
                                p.iref_edges[i].from_item_id);
                            push_primary_rel_unique(thmb_to_ids, &thmb_to_count,
                                                    p.iref_edges[i].to_item_id);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.thmb.from_item_id",
                                           p.iref_edges[i].from_item_id);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.thmb.to_item_id",
                                           p.iref_edges[i].to_item_id);
                            emit_iref_semantic_edge_fields(
                                *ctx->store, ctx->block, ctx->order,
                                p.iref_edges[i].ref_type, "thmb",
                                p.iref_edges[i].from_item_id,
                                p.iref_edges[i].to_item_id);
                            bump_item_edge_count(thmb_item_ids,
                                                 thmb_item_out_counts,
                                                 &thmb_item_count,
                                                 p.iref_edges[i].from_item_id);
                            bump_item_edge_count(thmb_item_ids,
                                                 thmb_item_in_counts,
                                                 &thmb_item_count,
                                                 p.iref_edges[i].to_item_id);
                        } else if (p.iref_edges[i].ref_type
                                   == fourcc('c', 'd', 's', 'c')) {
                            cdsc_edge_count += 1;
                            push_primary_rel_unique(
                                cdsc_from_ids, &cdsc_from_count,
                                p.iref_edges[i].from_item_id);
                            push_primary_rel_unique(cdsc_to_ids, &cdsc_to_count,
                                                    p.iref_edges[i].to_item_id);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.cdsc.from_item_id",
                                           p.iref_edges[i].from_item_id);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.cdsc.to_item_id",
                                           p.iref_edges[i].to_item_id);
                            emit_iref_semantic_edge_fields(
                                *ctx->store, ctx->block, ctx->order,
                                p.iref_edges[i].ref_type, "cdsc",
                                p.iref_edges[i].from_item_id,
                                p.iref_edges[i].to_item_id);
                            bump_item_edge_count(cdsc_item_ids,
                                                 cdsc_item_out_counts,
                                                 &cdsc_item_count,
                                                 p.iref_edges[i].from_item_id);
                            bump_item_edge_count(cdsc_item_ids,
                                                 cdsc_item_in_counts,
                                                 &cdsc_item_count,
                                                 p.iref_edges[i].to_item_id);
                        }
                    }
                    if (auxl_edge_count > 0) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.auxl.edge_count", auxl_edge_count);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.auxl.from_item_unique_count",
                                       auxl_from_count);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.auxl.to_item_unique_count",
                                       auxl_to_count);
                        emit_iref_typed_item_summary(*ctx->store, ctx->block,
                                                     ctx->order, "auxl",
                                                     auxl_item_ids,
                                                     auxl_item_out_counts,
                                                     auxl_item_in_counts,
                                                     auxl_item_count);
                        emit_iref_typed_graph_summary(*ctx->store, ctx->block,
                                                      ctx->order, "auxl",
                                                      auxl_edge_count,
                                                      auxl_from_count,
                                                      auxl_to_count);
                    }
                    if (dimg_edge_count > 0) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.dimg.edge_count", dimg_edge_count);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.dimg.from_item_unique_count",
                                       dimg_from_count);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.dimg.to_item_unique_count",
                                       dimg_to_count);
                        emit_iref_typed_item_summary(*ctx->store, ctx->block,
                                                     ctx->order, "dimg",
                                                     dimg_item_ids,
                                                     dimg_item_out_counts,
                                                     dimg_item_in_counts,
                                                     dimg_item_count);
                        emit_iref_typed_graph_summary(*ctx->store, ctx->block,
                                                      ctx->order, "dimg",
                                                      dimg_edge_count,
                                                      dimg_from_count,
                                                      dimg_to_count);
                    }
                    if (thmb_edge_count > 0) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.thmb.edge_count", thmb_edge_count);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.thmb.from_item_unique_count",
                                       thmb_from_count);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.thmb.to_item_unique_count",
                                       thmb_to_count);
                        emit_iref_typed_item_summary(*ctx->store, ctx->block,
                                                     ctx->order, "thmb",
                                                     thmb_item_ids,
                                                     thmb_item_out_counts,
                                                     thmb_item_in_counts,
                                                     thmb_item_count);
                        emit_iref_typed_graph_summary(*ctx->store, ctx->block,
                                                      ctx->order, "thmb",
                                                      thmb_edge_count,
                                                      thmb_from_count,
                                                      thmb_to_count);
                    }
                    if (cdsc_edge_count > 0) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.cdsc.edge_count", cdsc_edge_count);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.cdsc.from_item_unique_count",
                                       cdsc_from_count);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.cdsc.to_item_unique_count",
                                       cdsc_to_count);
                        emit_iref_typed_item_summary(*ctx->store, ctx->block,
                                                     ctx->order, "cdsc",
                                                     cdsc_item_ids,
                                                     cdsc_item_out_counts,
                                                     cdsc_item_in_counts,
                                                     cdsc_item_count);
                        emit_iref_typed_graph_summary(*ctx->store, ctx->block,
                                                      ctx->order, "cdsc",
                                                      cdsc_edge_count,
                                                      cdsc_from_count,
                                                      cdsc_to_count);
                    }
                    for (uint32_t ti = 0; ti < dynamic_iref_type_count; ++ti) {
                        emit_iref_dynamic_summary(
                            *ctx->store, ctx->block, ctx->order,
                            dynamic_iref_types[ti],
                            std::string_view(dynamic_iref_tokens[ti].data(), 4U),
                            std::span<const ItemRefEdge>(p.iref_edges.data(),
                                                         p.iref_edge_count));
                    }
                    if (iref_item_count > 0) {
                        uint32_t unique_from_count = 0;
                        uint32_t unique_to_count   = 0;
                        for (uint32_t i = 0; i < iref_item_count; ++i) {
                            if (iref_item_out_edge_counts[i] > 0) {
                                unique_from_count += 1U;
                            }
                            if (iref_item_in_edge_counts[i] > 0) {
                                unique_to_count += 1U;
                            }
                        }
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.item_count", iref_item_count);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.from_item_unique_count",
                                       unique_from_count);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.to_item_unique_count",
                                       unique_to_count);
                        for (uint32_t i = 0; i < iref_item_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++, "iref.item_id",
                                           iref_item_ids[i]);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.item_out_edge_count",
                                           iref_item_out_edge_counts[i]);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.item_in_edge_count",
                                           iref_item_in_edge_counts[i]);
                        }
                    }
                    for (uint32_t i = 0; i < p.aux_item_count; ++i) {
                        if (i == 0U) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++, "aux.item_count",
                                           p.aux_item_count);
                        }
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "aux.item_id", p.aux_items[i].item_id);
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++, "aux.semantic",
                                        aux_semantic_name(
                                            p.aux_items[i].semantic));
                        if (p.aux_items[i].aux_type_len > 0) {
                            emit_text_field(
                                *ctx->store, ctx->block, (*ctx->order)++,
                                "aux.type",
                                std::string_view(p.aux_items[i].aux_type.data(),
                                                 p.aux_items[i].aux_type_len));
                        }
                        if (p.aux_items[i].aux_subtype_len > 0) {
                            const AuxSubtypeInterpretation interp
                                = interpret_aux_subtype(
                                    std::span<const std::byte>(
                                        p.aux_items[i].aux_subtype.data(),
                                        p.aux_items[i].aux_subtype_len),
                                    p.aux_items[i].aux_subtype_total_len,
                                    p.aux_items[i].aux_subtype_truncated);
                            emit_text_field(*ctx->store, ctx->block,
                                            (*ctx->order)++, "aux.subtype_kind",
                                            interp.kind);
                            if (interp.has_text) {
                                emit_text_field(
                                    *ctx->store, ctx->block, (*ctx->order)++,
                                    "aux.subtype_text",
                                    std::string_view(interp.text.data(),
                                                     interp.text_len));
                                if (interp.kind == "uuid") {
                                    emit_text_field(
                                        *ctx->store, ctx->block,
                                        (*ctx->order)++, "aux.subtype_uuid",
                                        std::string_view(interp.text.data(),
                                                         interp.text_len));
                                }
                            }
                            if (interp.has_u32) {
                                emit_u32_field(*ctx->store, ctx->block,
                                               (*ctx->order)++,
                                               "aux.subtype_u32", interp.u32);
                            }
                            if (interp.has_u64) {
                                emit_u64_field(*ctx->store, ctx->block,
                                               (*ctx->order)++,
                                               "aux.subtype_u64", interp.u64);
                            }
                            const std::string hex = bytes_to_hex_string(
                                std::span<const std::byte>(
                                    p.aux_items[i].aux_subtype.data(),
                                    p.aux_items[i].aux_subtype_len));
                            emit_text_field(*ctx->store, ctx->block,
                                            (*ctx->order)++, "aux.subtype_hex",
                                            hex);
                            emit_u32_field(
                                *ctx->store, ctx->block, (*ctx->order)++,
                                "aux.subtype_len",
                                static_cast<uint32_t>(
                                    p.aux_items[i].aux_subtype_total_len));
                            if (p.aux_items[i].aux_subtype_truncated) {
                                emit_u8_field(*ctx->store, ctx->block,
                                              (*ctx->order)++,
                                              "aux.subtype_truncated", 1);
                            }
                        }
                    }
                    emit_count_field_if_nonzero(
                        *ctx->store, ctx->block, ctx->order, "aux.alpha_count",
                        count_aux_items_with_semantic(p, AuxSemantic::Alpha));
                    emit_count_field_if_nonzero(
                        *ctx->store, ctx->block, ctx->order, "aux.depth_count",
                        count_aux_items_with_semantic(p, AuxSemantic::Depth));
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "aux.disparity_count",
                                                count_aux_items_with_semantic(
                                                    p, AuxSemantic::Disparity));
                    emit_count_field_if_nonzero(
                        *ctx->store, ctx->block, ctx->order, "aux.matte_count",
                        count_aux_items_with_semantic(p, AuxSemantic::Matte));
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.auxl_count",
                                                p.primary_auxl_count);
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.auxiliary_image_count",
                                                p.primary_auxl_count);
                    for (uint32_t i = 0; i < p.primary_auxl_count; ++i) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.auxl_item_id",
                                       p.primary_auxl_item_ids[i]);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.auxiliary_image_item_id",
                                       p.primary_auxl_item_ids[i]);
                        emit_text_field(*ctx->store, ctx->block,
                                        (*ctx->order)++,
                                        "primary.auxl_semantic",
                                        aux_semantic_name(
                                            p.primary_auxl_semantics[i]));
                    }
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.alpha_count",
                                                p.primary_alpha_count);
                    for (uint32_t i = 0; i < p.primary_alpha_count; ++i) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.alpha_item_id",
                                       p.primary_alpha_item_ids[i]);
                    }
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.depth_count",
                                                p.primary_depth_count);
                    for (uint32_t i = 0; i < p.primary_depth_count; ++i) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.depth_item_id",
                                       p.primary_depth_item_ids[i]);
                    }
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.disparity_count",
                                                p.primary_disparity_count);
                    for (uint32_t i = 0; i < p.primary_disparity_count; ++i) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.disparity_item_id",
                                       p.primary_disparity_item_ids[i]);
                    }
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.matte_count",
                                                p.primary_matte_count);
                    for (uint32_t i = 0; i < p.primary_matte_count; ++i) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.matte_item_id",
                                       p.primary_matte_item_ids[i]);
                    }
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.dimg_count",
                                                p.primary_dimg_count);
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.derived_image_count",
                                                p.primary_dimg_count);
                    for (uint32_t i = 0; i < p.primary_dimg_count; ++i) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.dimg_item_id",
                                       p.primary_dimg_item_ids[i]);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.derived_image_item_id",
                                       p.primary_dimg_item_ids[i]);
                    }
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.dimg_source_count",
                                                p.primary_dimg_source_count);
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.source_image_count",
                                                p.primary_dimg_source_count);
                    for (uint32_t i = 0; i < p.primary_dimg_source_count; ++i) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.dimg_source_item_id",
                                       p.primary_dimg_source_item_ids[i]);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.source_image_item_id",
                                       p.primary_dimg_source_item_ids[i]);
                    }
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.thmb_count",
                                                p.primary_thmb_count);
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.thumbnail_image_count",
                                                p.primary_thmb_count);
                    for (uint32_t i = 0; i < p.primary_thmb_count; ++i) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.thmb_item_id",
                                       p.primary_thmb_item_ids[i]);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.thumbnail_image_item_id",
                                       p.primary_thmb_item_ids[i]);
                    }
                    emit_count_field_if_nonzero(*ctx->store, ctx->block,
                                                ctx->order,
                                                "primary.cdsc_count",
                                                p.primary_cdsc_count);
                    emit_count_field_if_nonzero(
                        *ctx->store, ctx->block, ctx->order,
                        "primary.descriptive_item_count", p.primary_cdsc_count);
                    for (uint32_t i = 0; i < p.primary_cdsc_count; ++i) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.cdsc_item_id",
                                       p.primary_cdsc_item_ids[i]);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.descriptive_item_id",
                                       p.primary_cdsc_item_ids[i]);
                    }
                    emit_primary_linked_item_roles(*ctx->store, ctx->block,
                                                   ctx->order, p);
                }
            }
            ctx->meta_done = true;
            return;
        }
    }


    static void bmff_scan_for_meta(std::span<const std::byte> bytes,
                                   uint64_t offset, uint64_t end,
                                   uint32_t depth, ScanCtx* ctx) noexcept
    {
        if (!ctx || ctx->meta_done) {
            return;
        }

        const uint32_t kMaxDepth = 16;
        const uint32_t kMaxBoxes = 1U << 16;
        if (depth > kMaxDepth) {
            return;
        }

        while (offset + 8 <= end) {
            ctx->seen_boxes += 1;
            if (ctx->seen_boxes > kMaxBoxes) {
                return;
            }

            BmffBox box;
            if (!parse_bmff_box(bytes, offset, end, &box)) {
                break;
            }

            if (box.type == fourcc('m', 'e', 't', 'a')) {
                bmff_emit_meta(bytes, box, ctx);
                if (ctx->meta_done) {
                    return;
                }
            } else if (bmff_is_container_box(box.type)) {
                const uint64_t child_off = box.offset + box.header_size;
                const uint64_t child_end = box.offset + box.size;
                if (child_off < child_end && child_end <= bytes.size()) {
                    bmff_scan_for_meta(bytes, child_off, child_end, depth + 1,
                                       ctx);
                    if (ctx->meta_done) {
                        return;
                    }
                }
            }

            offset += box.size;
            if (box.size == 0) {
                break;
            }
        }
    }

}  // namespace


namespace bmff_internal {

    void decode_bmff_derived_fields(std::span<const std::byte> file_bytes,
                                    MetaStore& store) noexcept
    {
        BmffBox ftyp;
        if (!parse_bmff_box(file_bytes, 0, file_bytes.size(), &ftyp)) {
            return;
        }
        if (ftyp.type != fourcc('f', 't', 'y', 'p')) {
            return;
        }

        ContainerFormat fmt    = ContainerFormat::Unknown;
        uint32_t major_brand   = 0;
        uint32_t minor_version = 0;
        std::array<uint32_t, 32> compat {};
        uint32_t compat_count = 0;
        if (!bmff_parse_ftyp(file_bytes, ftyp, &fmt, &major_brand,
                             &minor_version, &compat, &compat_count)) {
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        uint32_t order = 0;
        emit_u32_field(store, block, order++, "ftyp.major_brand", major_brand);
        emit_text_field(store, block, order++, "ftyp.major_brand_name",
                        bmff_fourcc_display_name(major_brand));
        emit_u32_field(store, block, order++, "ftyp.minor_version",
                       minor_version);
        emit_u32_field(store, block, order++, "ftyp.compat_brand_count",
                       compat_count);
        if (compat_count > 0) {
            emit_u32_array_field(store, block, order++, "ftyp.compat_brands",
                                 std::span<const uint32_t>(compat.data(),
                                                           compat_count));
            for (uint32_t i = 0; i < compat_count; ++i) {
                emit_text_field(store, block, order++, "ftyp.compat_brand_name",
                                bmff_fourcc_display_name(compat[i]));
            }
        }

        ScanCtx ctx;
        ctx.bytes     = file_bytes;
        ctx.store     = &store;
        ctx.block     = block;
        ctx.order     = &order;
        ctx.format    = fmt;
        ctx.meta_done = false;
        bmff_scan_for_meta(file_bytes, 0, file_bytes.size(), 0, &ctx);
    }

}  // namespace bmff_internal

}  // namespace openmeta
