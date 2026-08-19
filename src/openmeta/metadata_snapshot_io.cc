// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_transfer.h"

#include <cstring>
#include <limits>
#include <utility>

namespace openmeta {
namespace {

    constexpr char kSnapshotMagic[8]
        = { 'O', 'M', 'S', 'N', 'A', 'P', '0', '1' };

    struct WireWriter final {
        std::vector<std::byte> bytes;
        uint64_t max_bytes  = 0U;
        bool limit_exceeded = false;

        bool append(std::span<const std::byte> value) noexcept
        {
            const uint64_t current = static_cast<uint64_t>(bytes.size());
            if (value.size() > bytes.max_size() - bytes.size()
                || value.size() > std::numeric_limits<uint64_t>::max() - current
                || (max_bytes != 0U
                    && current + static_cast<uint64_t>(value.size())
                           > max_bytes)) {
                limit_exceeded = true;
                return false;
            }
            bytes.insert(bytes.end(), value.begin(), value.end());
            return true;
        }

        bool u8(uint8_t value) noexcept
        {
            const std::byte b = static_cast<std::byte>(value);
            return append(std::span<const std::byte>(&b, 1U));
        }

        bool u16(uint16_t value) noexcept
        {
            const std::byte data[2] = {
                static_cast<std::byte>((value >> 0U) & 0xFFU),
                static_cast<std::byte>((value >> 8U) & 0xFFU),
            };
            return append(data);
        }

        bool u32(uint32_t value) noexcept
        {
            const std::byte data[4] = {
                static_cast<std::byte>((value >> 0U) & 0xFFU),
                static_cast<std::byte>((value >> 8U) & 0xFFU),
                static_cast<std::byte>((value >> 16U) & 0xFFU),
                static_cast<std::byte>((value >> 24U) & 0xFFU),
            };
            return append(data);
        }

        bool u64(uint64_t value) noexcept
        {
            std::byte data[8];
            for (uint32_t i = 0U; i < 8U; ++i) {
                data[i] = static_cast<std::byte>((value >> (i * 8U)) & 0xFFU);
            }
            return append(data);
        }

        bool span(ByteSpan value) noexcept
        {
            return u32(value.offset) && u32(value.size);
        }

        bool string(std::string_view value) noexcept
        {
            if (value.size() > std::numeric_limits<uint32_t>::max()) {
                limit_exceeded = true;
                return false;
            }
            return u32(static_cast<uint32_t>(value.size()))
                   && append(std::span<const std::byte>(
                       reinterpret_cast<const std::byte*>(value.data()),
                       value.size()));
        }

        bool blob(std::span<const std::byte> value) noexcept
        {
            return u64(static_cast<uint64_t>(value.size())) && append(value);
        }
    };

    struct WireReader final {
        std::span<const std::byte> bytes;
        size_t offset       = 0U;
        bool limit_exceeded = false;

        bool take(size_t count, std::span<const std::byte>* out) noexcept
        {
            if (!out || offset > bytes.size()
                || count > bytes.size() - offset) {
                return false;
            }
            *out = bytes.subspan(offset, count);
            offset += count;
            return true;
        }

        bool u8(uint8_t* out) noexcept
        {
            std::span<const std::byte> data;
            if (!out || !take(1U, &data)) {
                return false;
            }
            *out = std::to_integer<uint8_t>(data[0]);
            return true;
        }

        bool u16(uint16_t* out) noexcept
        {
            std::span<const std::byte> data;
            if (!out || !take(2U, &data)) {
                return false;
            }
            *out = static_cast<uint16_t>(
                (static_cast<uint16_t>(std::to_integer<uint8_t>(data[0])) << 0U)
                | (static_cast<uint16_t>(std::to_integer<uint8_t>(data[1]))
                   << 8U));
            return true;
        }

        bool u32(uint32_t* out) noexcept
        {
            std::span<const std::byte> data;
            if (!out || !take(4U, &data)) {
                return false;
            }
            *out = (static_cast<uint32_t>(std::to_integer<uint8_t>(data[0]))
                    << 0U)
                   | (static_cast<uint32_t>(std::to_integer<uint8_t>(data[1]))
                      << 8U)
                   | (static_cast<uint32_t>(std::to_integer<uint8_t>(data[2]))
                      << 16U)
                   | (static_cast<uint32_t>(std::to_integer<uint8_t>(data[3]))
                      << 24U);
            return true;
        }

        bool u64(uint64_t* out) noexcept
        {
            std::span<const std::byte> data;
            if (!out || !take(8U, &data)) {
                return false;
            }
            uint64_t value = 0U;
            for (uint32_t i = 0U; i < 8U; ++i) {
                value |= static_cast<uint64_t>(std::to_integer<uint8_t>(data[i]))
                         << (i * 8U);
            }
            *out = value;
            return true;
        }

        bool span(ByteSpan* out) noexcept
        {
            return out && u32(&out->offset) && u32(&out->size);
        }

        bool string(uint32_t max_size, std::string* out) noexcept
        {
            uint32_t size = 0U;
            std::span<const std::byte> data;
            if (!out || !u32(&size)) {
                return false;
            }
            if (size > max_size) {
                limit_exceeded = true;
                return false;
            }
            if (!take(size, &data)) {
                return false;
            }
            out->assign(reinterpret_cast<const char*>(data.data()),
                        data.size());
            return true;
        }

        bool blob(uint64_t max_size, std::vector<std::byte>* out) noexcept
        {
            uint64_t size = 0U;
            if (!out || !u64(&size)) {
                return false;
            }
            if (size > max_size
                || size > static_cast<uint64_t>(
                       std::numeric_limits<size_t>::max())) {
                limit_exceeded = true;
                return false;
            }
            std::span<const std::byte> data;
            if (!take(static_cast<size_t>(size), &data)) {
                return false;
            }
            out->assign(data.begin(), data.end());
            return true;
        }
    };

    static TransferSourceSnapshotIoResult
    snapshot_io_error(TransferStatus status, TransferSourceSnapshotIoCode code,
                      const char* message) noexcept
    {
        TransferSourceSnapshotIoResult out;
        out.status  = status;
        out.code    = code;
        out.errors  = 1U;
        out.message = message ? message : "source snapshot I/O failed";
        return out;
    }

    static bool checked_add_u64(uint64_t a, uint64_t b, uint64_t* out) noexcept
    {
        if (!out || b > std::numeric_limits<uint64_t>::max() - a) {
            return false;
        }
        *out = a + b;
        return true;
    }

    static bool span_is_valid(ByteSpan span, uint64_t arena_size) noexcept
    {
        return span.offset <= arena_size
               && span.size <= arena_size - span.offset;
    }

    static bool key_is_valid(const MetaKey& key, uint64_t arena_size) noexcept
    {
        if (static_cast<uint8_t>(key.kind)
            > static_cast<uint8_t>(MetaKeyKind::PngText)) {
            return false;
        }
        switch (key.kind) {
        case MetaKeyKind::ExifTag:
            return span_is_valid(key.data.exif_tag.ifd, arena_size);
        case MetaKeyKind::Comment: return true;
        case MetaKeyKind::ExrAttribute:
            return span_is_valid(key.data.exr_attribute.name, arena_size);
        case MetaKeyKind::IptcDataset:
        case MetaKeyKind::IccHeaderField:
        case MetaKeyKind::IccTag:
        case MetaKeyKind::PhotoshopIrb:
        case MetaKeyKind::GeotiffKey: return true;
        case MetaKeyKind::XmpProperty:
            return span_is_valid(key.data.xmp_property.schema_ns, arena_size)
                   && span_is_valid(key.data.xmp_property.property_path,
                                    arena_size);
        case MetaKeyKind::PhotoshopIrbField:
            return span_is_valid(key.data.photoshop_irb_field.field,
                                 arena_size);
        case MetaKeyKind::PrintImField:
            return span_is_valid(key.data.printim_field.field, arena_size);
        case MetaKeyKind::BmffField:
            return span_is_valid(key.data.bmff_field.field, arena_size);
        case MetaKeyKind::JumbfField:
            return span_is_valid(key.data.jumbf_field.field, arena_size);
        case MetaKeyKind::JumbfCborKey:
            return span_is_valid(key.data.jumbf_cbor_key.key, arena_size);
        case MetaKeyKind::PngText:
            return span_is_valid(key.data.png_text.keyword, arena_size)
                   && span_is_valid(key.data.png_text.field, arena_size);
        }
        return false;
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

    static bool value_is_valid(const MetaValue& value,
                               uint64_t arena_size) noexcept
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
        case MetaValueKind::Empty: return value.count == 0U;
        case MetaValueKind::Scalar: return value.count == 1U;
        case MetaValueKind::Array: {
            if (!span_is_valid(value.data.span, arena_size)) {
                return false;
            }
            const uint32_t width = element_width(value.elem_type);
            return width != 0U
                   && static_cast<uint64_t>(value.count) * width
                          == value.data.span.size;
        }
        case MetaValueKind::Bytes:
        case MetaValueKind::Text:
            return span_is_valid(value.data.span, arena_size)
                   && value.count == value.data.span.size;
        }
        return false;
    }

    static bool origin_is_valid(const Origin& origin, uint32_t block_count,
                                uint64_t arena_size) noexcept
    {
        return (origin.block == kInvalidBlockId || origin.block < block_count)
               && static_cast<uint8_t>(origin.wire_type.family)
                      <= static_cast<uint8_t>(WireFamily::Other)
               && span_is_valid(origin.wire_type_name, arena_size)
               && static_cast<uint8_t>(origin.name_context_kind)
                      <= static_cast<uint8_t>(
                          EntryNameContextKind::CanonMain0000);
    }

    static bool write_key(WireWriter* out, const MetaKey& key) noexcept
    {
        if (!out || !out->u8(static_cast<uint8_t>(key.kind))) {
            return false;
        }
        switch (key.kind) {
        case MetaKeyKind::ExifTag:
            return out->span(key.data.exif_tag.ifd)
                   && out->u16(key.data.exif_tag.tag);
        case MetaKeyKind::Comment: return out->u8(key.data.comment.reserved);
        case MetaKeyKind::ExrAttribute:
            return out->u32(key.data.exr_attribute.part_index)
                   && out->span(key.data.exr_attribute.name);
        case MetaKeyKind::IptcDataset:
            return out->u16(key.data.iptc_dataset.record)
                   && out->u16(key.data.iptc_dataset.dataset);
        case MetaKeyKind::XmpProperty:
            return out->span(key.data.xmp_property.schema_ns)
                   && out->span(key.data.xmp_property.property_path);
        case MetaKeyKind::IccHeaderField:
            return out->u32(key.data.icc_header_field.offset);
        case MetaKeyKind::IccTag: return out->u32(key.data.icc_tag.signature);
        case MetaKeyKind::PhotoshopIrb:
            return out->u16(key.data.photoshop_irb.resource_id);
        case MetaKeyKind::PhotoshopIrbField:
            return out->u16(key.data.photoshop_irb_field.resource_id)
                   && out->span(key.data.photoshop_irb_field.field);
        case MetaKeyKind::GeotiffKey:
            return out->u16(key.data.geotiff_key.key_id);
        case MetaKeyKind::PrintImField:
            return out->span(key.data.printim_field.field);
        case MetaKeyKind::BmffField:
            return out->span(key.data.bmff_field.field);
        case MetaKeyKind::JumbfField:
            return out->span(key.data.jumbf_field.field);
        case MetaKeyKind::JumbfCborKey:
            return out->span(key.data.jumbf_cbor_key.key);
        case MetaKeyKind::PngText:
            return out->span(key.data.png_text.keyword)
                   && out->span(key.data.png_text.field);
        }
        return false;
    }

    static bool read_key(WireReader* in, MetaKey* key) noexcept
    {
        uint8_t kind = 0U;
        if (!in || !key || !in->u8(&kind)
            || kind > static_cast<uint8_t>(MetaKeyKind::PngText)) {
            return false;
        }
        key->kind = static_cast<MetaKeyKind>(kind);
        switch (key->kind) {
        case MetaKeyKind::ExifTag:
            return in->span(&key->data.exif_tag.ifd)
                   && in->u16(&key->data.exif_tag.tag);
        case MetaKeyKind::Comment: return in->u8(&key->data.comment.reserved);
        case MetaKeyKind::ExrAttribute:
            return in->u32(&key->data.exr_attribute.part_index)
                   && in->span(&key->data.exr_attribute.name);
        case MetaKeyKind::IptcDataset:
            return in->u16(&key->data.iptc_dataset.record)
                   && in->u16(&key->data.iptc_dataset.dataset);
        case MetaKeyKind::XmpProperty:
            return in->span(&key->data.xmp_property.schema_ns)
                   && in->span(&key->data.xmp_property.property_path);
        case MetaKeyKind::IccHeaderField:
            return in->u32(&key->data.icc_header_field.offset);
        case MetaKeyKind::IccTag: return in->u32(&key->data.icc_tag.signature);
        case MetaKeyKind::PhotoshopIrb:
            return in->u16(&key->data.photoshop_irb.resource_id);
        case MetaKeyKind::PhotoshopIrbField:
            return in->u16(&key->data.photoshop_irb_field.resource_id)
                   && in->span(&key->data.photoshop_irb_field.field);
        case MetaKeyKind::GeotiffKey:
            return in->u16(&key->data.geotiff_key.key_id);
        case MetaKeyKind::PrintImField:
            return in->span(&key->data.printim_field.field);
        case MetaKeyKind::BmffField:
            return in->span(&key->data.bmff_field.field);
        case MetaKeyKind::JumbfField:
            return in->span(&key->data.jumbf_field.field);
        case MetaKeyKind::JumbfCborKey:
            return in->span(&key->data.jumbf_cbor_key.key);
        case MetaKeyKind::PngText:
            return in->span(&key->data.png_text.keyword)
                   && in->span(&key->data.png_text.field);
        }
        return false;
    }

    static bool write_value(WireWriter* out, const MetaValue& value) noexcept
    {
        if (!out || !out->u8(static_cast<uint8_t>(value.kind))
            || !out->u8(static_cast<uint8_t>(value.elem_type))
            || !out->u8(static_cast<uint8_t>(value.text_encoding))
            || !out->u32(value.count)) {
            return false;
        }
        if (value.kind == MetaValueKind::Scalar) {
            static_assert(sizeof(MetaValue::Data) == sizeof(uint64_t));
            uint64_t raw = 0U;
            std::memcpy(&raw, &value.data, sizeof(raw));
            return out->u64(raw);
        }
        if (value.kind == MetaValueKind::Array
            || value.kind == MetaValueKind::Bytes
            || value.kind == MetaValueKind::Text) {
            return out->span(value.data.span);
        }
        return value.kind == MetaValueKind::Empty;
    }

    static bool read_value(WireReader* in, MetaValue* value) noexcept
    {
        uint8_t kind     = 0U;
        uint8_t elem     = 0U;
        uint8_t encoding = 0U;
        if (!in || !value || !in->u8(&kind) || !in->u8(&elem)
            || !in->u8(&encoding) || !in->u32(&value->count)
            || kind > static_cast<uint8_t>(MetaValueKind::Text)
            || elem > static_cast<uint8_t>(MetaElementType::SRational)
            || encoding > static_cast<uint8_t>(TextEncoding::Utf16BE)) {
            return false;
        }
        value->kind          = static_cast<MetaValueKind>(kind);
        value->elem_type     = static_cast<MetaElementType>(elem);
        value->text_encoding = static_cast<TextEncoding>(encoding);
        if (value->kind == MetaValueKind::Scalar) {
            uint64_t raw = 0U;
            if (!in->u64(&raw)) {
                return false;
            }
            std::memcpy(&value->data, &raw, sizeof(raw));
            return true;
        }
        if (value->kind == MetaValueKind::Array
            || value->kind == MetaValueKind::Bytes
            || value->kind == MetaValueKind::Text) {
            return in->span(&value->data.span);
        }
        return value->kind == MetaValueKind::Empty;
    }

    static bool write_origin(WireWriter* out, const Origin& origin) noexcept
    {
        return out && out->u32(origin.block) && out->u32(origin.order_in_block)
               && out->u8(static_cast<uint8_t>(origin.wire_type.family))
               && out->u16(origin.wire_type.code) && out->u32(origin.wire_count)
               && out->span(origin.wire_type_name)
               && out->u8(static_cast<uint8_t>(origin.name_context_kind))
               && out->u8(origin.name_context_variant);
    }

    static bool read_origin(WireReader* in, Origin* origin) noexcept
    {
        uint8_t family  = 0U;
        uint8_t context = 0U;
        if (!in || !origin || !in->u32(&origin->block)
            || !in->u32(&origin->order_in_block) || !in->u8(&family)
            || !in->u16(&origin->wire_type.code)
            || !in->u32(&origin->wire_count)
            || !in->span(&origin->wire_type_name) || !in->u8(&context)
            || !in->u8(&origin->name_context_variant)
            || family > static_cast<uint8_t>(WireFamily::Other)
            || context > static_cast<uint8_t>(
                   EntryNameContextKind::CanonMain0000)) {
            return false;
        }
        origin->wire_type.family  = static_cast<WireFamily>(family);
        origin->name_context_kind = static_cast<EntryNameContextKind>(context);
        return true;
    }

    static bool write_container_block(WireWriter* out,
                                      const ContainerBlockRef& block) noexcept
    {
        return out && out->u8(static_cast<uint8_t>(block.format))
               && out->u8(static_cast<uint8_t>(block.kind))
               && out->u8(static_cast<uint8_t>(block.compression))
               && out->u8(static_cast<uint8_t>(block.chunking))
               && out->u64(block.outer_offset) && out->u64(block.outer_size)
               && out->u64(block.data_offset) && out->u64(block.data_size)
               && out->u32(block.id) && out->u32(block.part_index)
               && out->u32(block.part_count) && out->u64(block.logical_offset)
               && out->u64(block.logical_size) && out->u64(block.group)
               && out->u32(block.aux_u32);
    }

    static bool read_container_block(WireReader* in,
                                     ContainerBlockRef* block) noexcept
    {
        uint8_t format      = 0U;
        uint8_t kind        = 0U;
        uint8_t compression = 0U;
        uint8_t chunking    = 0U;
        if (!in || !block || !in->u8(&format) || !in->u8(&kind)
            || !in->u8(&compression) || !in->u8(&chunking)
            || !in->u64(&block->outer_offset) || !in->u64(&block->outer_size)
            || !in->u64(&block->data_offset) || !in->u64(&block->data_size)
            || !in->u32(&block->id) || !in->u32(&block->part_index)
            || !in->u32(&block->part_count) || !in->u64(&block->logical_offset)
            || !in->u64(&block->logical_size) || !in->u64(&block->group)
            || !in->u32(&block->aux_u32)
            || format > static_cast<uint8_t>(ContainerFormat::Cr3)
            || kind > static_cast<uint8_t>(
                   ContainerBlockKind::CompressedMetadata)
            || compression > static_cast<uint8_t>(BlockCompression::Brotli)
            || chunking > static_cast<uint8_t>(BlockChunking::PsIrB8Bim)) {
            return false;
        }
        block->format      = static_cast<ContainerFormat>(format);
        block->kind        = static_cast<ContainerBlockKind>(kind);
        block->compression = static_cast<BlockCompression>(compression);
        block->chunking    = static_cast<BlockChunking>(chunking);
        return true;
    }

}  // namespace

TransferSourceSnapshotIoResult
serialize_transfer_source_snapshot(
    const TransferSourceSnapshot& snapshot, std::vector<std::byte>* out_bytes,
    const TransferSourceSnapshotIoOptions& options) noexcept
{
    if (!out_bytes) {
        return snapshot_io_error(TransferStatus::InvalidArgument,
                                 TransferSourceSnapshotIoCode::InvalidArgument,
                                 "out_bytes is null");
    }
    if (!snapshot.store.is_finalized()) {
        return snapshot_io_error(
            TransferStatus::InvalidArgument,
            TransferSourceSnapshotIoCode::SnapshotNotFinalized,
            "source snapshot store is not finalized");
    }

    const uint32_t block_count             = snapshot.store.block_count();
    const std::span<const Entry> entries   = snapshot.store.entries();
    const std::span<const std::byte> arena = snapshot.store.arena().bytes();
    if (block_count > options.max_blocks || entries.size() > options.max_entries
        || arena.size() > options.max_arena_bytes
        || snapshot.raw_carriers.size() > options.max_raw_carriers) {
        return snapshot_io_error(TransferStatus::LimitExceeded,
                                 TransferSourceSnapshotIoCode::LimitExceeded,
                                 "source snapshot exceeds serialization limits");
    }

    uint64_t payload_bytes = 0U;
    uint64_t decoded_links = 0U;
    for (const TransferSourceRawCarrier& carrier : snapshot.raw_carriers) {
        if (!checked_add_u64(payload_bytes, carrier.payload.size(),
                             &payload_bytes)
            || !checked_add_u64(decoded_links, carrier.decoded_entry_ids.size(),
                                &decoded_links)) {
            return snapshot_io_error(TransferStatus::LimitExceeded,
                                     TransferSourceSnapshotIoCode::LimitExceeded,
                                     "source snapshot carrier totals overflow");
        }
    }
    if (payload_bytes > options.max_raw_carrier_bytes
        || decoded_links > options.max_decoded_entry_links) {
        return snapshot_io_error(TransferStatus::LimitExceeded,
                                 TransferSourceSnapshotIoCode::LimitExceeded,
                                 "source snapshot carrier data exceeds limits");
    }
    if (snapshot.raw_carrier_bytes != payload_bytes) {
        return snapshot_io_error(
            TransferStatus::InvalidArgument,
            TransferSourceSnapshotIoCode::InvalidSnapshot,
            "source snapshot raw carrier byte count is inconsistent");
    }

    for (const Entry& entry : entries) {
        if (!key_is_valid(entry.key, arena.size())
            || !value_is_valid(entry.value, arena.size())
            || !origin_is_valid(entry.origin, block_count, arena.size())
            || (static_cast<uint8_t>(entry.flags) & 0xC0U) != 0U) {
            return snapshot_io_error(
                TransferStatus::InvalidArgument,
                TransferSourceSnapshotIoCode::InvalidSnapshot,
                "source snapshot contains an invalid entry");
        }
    }
    for (const TransferSourceRawCarrier& carrier : snapshot.raw_carriers) {
        if (carrier.route.size() > options.max_route_bytes) {
            return snapshot_io_error(
                TransferStatus::LimitExceeded,
                TransferSourceSnapshotIoCode::LimitExceeded,
                "source snapshot carrier route exceeds limit");
        }
        if (static_cast<uint8_t>(carrier.block.format)
                > static_cast<uint8_t>(ContainerFormat::Cr3)
            || static_cast<uint8_t>(carrier.block.kind) > static_cast<uint8_t>(
                   ContainerBlockKind::CompressedMetadata)
            || static_cast<uint8_t>(carrier.block.compression)
                   > static_cast<uint8_t>(BlockCompression::Brotli)
            || static_cast<uint8_t>(carrier.block.chunking)
                   > static_cast<uint8_t>(BlockChunking::PsIrB8Bim)
            || static_cast<uint8_t>(carrier.semantic_kind)
                   > static_cast<uint8_t>(TransferBlockKind::Other)
            || (!carrier.payload_preserved && !carrier.payload.empty())) {
            return snapshot_io_error(
                TransferStatus::InvalidArgument,
                TransferSourceSnapshotIoCode::InvalidSnapshot,
                "source snapshot contains an invalid carrier");
        }
        for (EntryId id : carrier.decoded_entry_ids) {
            if (id >= entries.size()) {
                return snapshot_io_error(
                    TransferStatus::InvalidArgument,
                    TransferSourceSnapshotIoCode::InvalidSnapshot,
                    "source snapshot carrier references an invalid entry");
            }
        }
    }

    WireWriter writer;
    writer.max_bytes             = options.max_serialized_bytes;
    const uint64_t initial_bytes = static_cast<uint64_t>(arena.size())
                                   + payload_bytes;
    if (initial_bytes <= static_cast<uint64_t>(writer.bytes.max_size())
        && (options.max_serialized_bytes == 0U
            || initial_bytes <= options.max_serialized_bytes)) {
        writer.bytes.reserve(static_cast<size_t>(initial_bytes));
    }
    if (!writer.append(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(kSnapshotMagic),
            sizeof(kSnapshotMagic)))
        || !writer.u32(kTransferSourceSnapshotSerializationVersion)
        || !writer.u32(block_count)
        || !writer.u32(static_cast<uint32_t>(entries.size()))
        || !writer.u64(static_cast<uint64_t>(arena.size()))
        || !writer.u32(static_cast<uint32_t>(snapshot.raw_carriers.size()))
        || !writer.u64(snapshot.raw_carrier_bytes)
        || !writer.u8(snapshot.raw_carrier_bytes_truncated ? 1U : 0U)
        || !writer.u64(payload_bytes) || !writer.u64(decoded_links)
        || !writer.append(arena)) {
        return snapshot_io_error(
            TransferStatus::LimitExceeded,
            TransferSourceSnapshotIoCode::LimitExceeded,
            "serialized source snapshot exceeds byte limit");
    }

    bool write_ok = true;
    for (uint32_t i = 0U; i < block_count && write_ok; ++i) {
        const BlockInfo& block = snapshot.store.block_info(i);
        write_ok = writer.u32(block.format) && writer.u32(block.container)
                   && writer.u32(block.id);
    }
    for (const Entry& entry : entries) {
        write_ok = write_ok && write_key(&writer, entry.key)
                   && write_value(&writer, entry.value)
                   && write_origin(&writer, entry.origin)
                   && writer.u8(static_cast<uint8_t>(entry.flags));
    }
    for (const TransferSourceRawCarrier& carrier : snapshot.raw_carriers) {
        write_ok = write_ok && write_container_block(&writer, carrier.block)
                   && writer.u8(static_cast<uint8_t>(carrier.semantic_kind))
                   && writer.u32(carrier.order) && writer.string(carrier.route)
                   && writer.u8(carrier.payload_preserved ? 1U : 0U)
                   && writer.blob(
                       std::span<const std::byte>(carrier.payload.data(),
                                                  carrier.payload.size()))
                   && writer.u32(
                       static_cast<uint32_t>(carrier.decoded_entry_ids.size()));
        for (EntryId id : carrier.decoded_entry_ids) {
            write_ok = write_ok && writer.u32(id);
        }
    }
    if (!write_ok || writer.limit_exceeded) {
        return snapshot_io_error(
            TransferStatus::LimitExceeded,
            TransferSourceSnapshotIoCode::LimitExceeded,
            "serialized source snapshot exceeds byte limit");
    }

    *out_bytes = std::move(writer.bytes);
    TransferSourceSnapshotIoResult out;
    out.status  = TransferStatus::Ok;
    out.code    = TransferSourceSnapshotIoCode::None;
    out.bytes   = static_cast<uint64_t>(out_bytes->size());
    out.message = "source snapshot serialized";
    return out;
}

TransferSourceSnapshotIoResult
deserialize_transfer_source_snapshot(
    std::span<const std::byte> bytes, TransferSourceSnapshot* out_snapshot,
    const TransferSourceSnapshotIoOptions& options) noexcept
{
    if (!out_snapshot) {
        return snapshot_io_error(TransferStatus::InvalidArgument,
                                 TransferSourceSnapshotIoCode::InvalidArgument,
                                 "out_snapshot is null");
    }
    if (options.max_serialized_bytes != 0U
        && bytes.size() > options.max_serialized_bytes) {
        return snapshot_io_error(
            TransferStatus::LimitExceeded,
            TransferSourceSnapshotIoCode::LimitExceeded,
            "serialized source snapshot exceeds byte limit");
    }

    WireReader reader { bytes };
    std::span<const std::byte> magic;
    if (!reader.take(sizeof(kSnapshotMagic), &magic)) {
        return snapshot_io_error(TransferStatus::Malformed,
                                 TransferSourceSnapshotIoCode::Malformed,
                                 "serialized source snapshot is truncated");
    }
    if (std::memcmp(magic.data(), kSnapshotMagic, sizeof(kSnapshotMagic))
        != 0) {
        return snapshot_io_error(TransferStatus::Malformed,
                                 TransferSourceSnapshotIoCode::InvalidMagic,
                                 "serialized source snapshot magic is invalid");
    }

    uint32_t version           = 0U;
    uint32_t block_count       = 0U;
    uint32_t entry_count       = 0U;
    uint64_t arena_size        = 0U;
    uint32_t carrier_count     = 0U;
    uint64_t raw_carrier_bytes = 0U;
    uint8_t raw_truncated      = 0U;
    uint64_t payload_bytes     = 0U;
    uint64_t decoded_links     = 0U;
    if (!reader.u32(&version)) {
        return snapshot_io_error(
            TransferStatus::Malformed, TransferSourceSnapshotIoCode::Malformed,
            "serialized source snapshot header is truncated");
    }
    if (version != kTransferSourceSnapshotSerializationVersion) {
        return snapshot_io_error(
            TransferStatus::Unsupported,
            TransferSourceSnapshotIoCode::UnsupportedVersion,
            "serialized source snapshot version is unsupported");
    }
    if (!reader.u32(&block_count) || !reader.u32(&entry_count)
        || !reader.u64(&arena_size) || !reader.u32(&carrier_count)
        || !reader.u64(&raw_carrier_bytes) || !reader.u8(&raw_truncated)
        || raw_truncated > 1U || !reader.u64(&payload_bytes)
        || !reader.u64(&decoded_links)) {
        return snapshot_io_error(
            TransferStatus::Malformed, TransferSourceSnapshotIoCode::Malformed,
            "serialized source snapshot header is malformed");
    }
    if (block_count > options.max_blocks || entry_count > options.max_entries
        || arena_size > options.max_arena_bytes
        || carrier_count > options.max_raw_carriers
        || raw_carrier_bytes > options.max_raw_carrier_bytes
        || payload_bytes > options.max_raw_carrier_bytes
        || decoded_links > options.max_decoded_entry_links) {
        return snapshot_io_error(
            TransferStatus::LimitExceeded,
            TransferSourceSnapshotIoCode::LimitExceeded,
            "serialized source snapshot exceeds parse limits");
    }
    if (raw_carrier_bytes != payload_bytes
        || arena_size
               > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return snapshot_io_error(
            TransferStatus::Malformed, TransferSourceSnapshotIoCode::Malformed,
            "serialized source snapshot totals are invalid");
    }

    std::span<const std::byte> arena;
    if (!reader.take(static_cast<size_t>(arena_size), &arena)) {
        return snapshot_io_error(
            TransferStatus::Malformed, TransferSourceSnapshotIoCode::Malformed,
            "serialized source snapshot arena is truncated");
    }

    TransferSourceSnapshot snapshot;
    snapshot.store.constrain_resources(options.max_entries,
                                       options.max_arena_bytes);
    const ByteSpan stored_arena = snapshot.store.arena().append(arena);
    if (stored_arena.offset != 0U || stored_arena.size != arena.size()
        || snapshot.store.resource_limit_exceeded()) {
        return snapshot_io_error(
            TransferStatus::LimitExceeded,
            TransferSourceSnapshotIoCode::LimitExceeded,
            "serialized source snapshot arena exceeds limits");
    }

    for (uint32_t i = 0U; i < block_count; ++i) {
        BlockInfo block;
        if (!reader.u32(&block.format) || !reader.u32(&block.container)
            || !reader.u32(&block.id)) {
            return snapshot_io_error(
                TransferStatus::Malformed,
                TransferSourceSnapshotIoCode::Malformed,
                "serialized source snapshot block list is truncated");
        }
        if (snapshot.store.add_block(block) == kInvalidBlockId) {
            return snapshot_io_error(
                TransferStatus::LimitExceeded,
                TransferSourceSnapshotIoCode::LimitExceeded,
                "serialized source snapshot block was rejected");
        }
    }

    for (uint32_t i = 0U; i < entry_count; ++i) {
        Entry entry;
        uint8_t flags = 0U;
        if (!read_key(&reader, &entry.key) || !read_value(&reader, &entry.value)
            || !read_origin(&reader, &entry.origin) || !reader.u8(&flags)
            || (flags & 0xC0U) != 0U) {
            return snapshot_io_error(
                TransferStatus::Malformed,
                TransferSourceSnapshotIoCode::Malformed,
                "serialized source snapshot entry is malformed");
        }
        entry.flags = static_cast<EntryFlags>(flags);
        if (!key_is_valid(entry.key, arena_size)
            || !value_is_valid(entry.value, arena_size)
            || !origin_is_valid(entry.origin, block_count, arena_size)) {
            return snapshot_io_error(
                TransferStatus::Malformed,
                TransferSourceSnapshotIoCode::Malformed,
                "serialized source snapshot entry references invalid data");
        }
        if (snapshot.store.add_entry(entry) == kInvalidEntryId) {
            return snapshot_io_error(
                TransferStatus::LimitExceeded,
                TransferSourceSnapshotIoCode::LimitExceeded,
                "serialized source snapshot entry was rejected");
        }
    }

    snapshot.raw_carriers.reserve(carrier_count);
    uint64_t parsed_payload_bytes = 0U;
    uint64_t parsed_links         = 0U;
    for (uint32_t i = 0U; i < carrier_count; ++i) {
        TransferSourceRawCarrier carrier;
        uint8_t semantic_kind     = 0U;
        uint8_t payload_preserved = 0U;
        uint32_t link_count       = 0U;
        if (!read_container_block(&reader, &carrier.block)
            || !reader.u8(&semantic_kind)
            || semantic_kind > static_cast<uint8_t>(TransferBlockKind::Other)
            || !reader.u32(&carrier.order)
            || !reader.string(options.max_route_bytes, &carrier.route)
            || !reader.u8(&payload_preserved) || payload_preserved > 1U
            || !reader.blob(payload_bytes - parsed_payload_bytes,
                            &carrier.payload)
            || !reader.u32(&link_count)) {
            if (reader.limit_exceeded) {
                return snapshot_io_error(
                    TransferStatus::LimitExceeded,
                    TransferSourceSnapshotIoCode::LimitExceeded,
                    "serialized source snapshot carrier exceeds parse limits");
            }
            return snapshot_io_error(
                TransferStatus::Malformed,
                TransferSourceSnapshotIoCode::Malformed,
                "serialized source snapshot carrier is malformed");
        }
        carrier.semantic_kind = static_cast<TransferBlockKind>(semantic_kind);
        carrier.payload_preserved = payload_preserved != 0U;
        if (!carrier.payload_preserved && !carrier.payload.empty()) {
            return snapshot_io_error(
                TransferStatus::Malformed,
                TransferSourceSnapshotIoCode::Malformed,
                "serialized source snapshot carrier payload state is invalid");
        }
        if (!checked_add_u64(parsed_payload_bytes, carrier.payload.size(),
                             &parsed_payload_bytes)
            || !checked_add_u64(parsed_links, link_count, &parsed_links)
            || parsed_payload_bytes > payload_bytes
            || parsed_links > decoded_links) {
            return snapshot_io_error(
                TransferStatus::Malformed,
                TransferSourceSnapshotIoCode::Malformed,
                "serialized source snapshot carrier totals overflow");
        }
        carrier.decoded_entry_ids.resize(link_count);
        for (uint32_t j = 0U; j < link_count; ++j) {
            if (!reader.u32(&carrier.decoded_entry_ids[j])
                || carrier.decoded_entry_ids[j] >= entry_count) {
                return snapshot_io_error(
                    TransferStatus::Malformed,
                    TransferSourceSnapshotIoCode::Malformed,
                    "serialized source snapshot carrier entry link is invalid");
            }
        }
        snapshot.raw_carriers.push_back(std::move(carrier));
    }
    if (parsed_payload_bytes != payload_bytes || parsed_links != decoded_links
        || reader.offset != bytes.size()) {
        return snapshot_io_error(
            TransferStatus::Malformed, TransferSourceSnapshotIoCode::Malformed,
            "serialized source snapshot has inconsistent totals or trailing bytes");
    }

    snapshot.raw_carrier_bytes           = raw_carrier_bytes;
    snapshot.raw_carrier_bytes_truncated = raw_truncated != 0U;
    snapshot.store.finalize();
    *out_snapshot = std::move(snapshot);

    TransferSourceSnapshotIoResult out;
    out.status  = TransferStatus::Ok;
    out.code    = TransferSourceSnapshotIoCode::None;
    out.bytes   = static_cast<uint64_t>(bytes.size());
    out.message = "source snapshot parsed";
    return out;
}

}  // namespace openmeta
