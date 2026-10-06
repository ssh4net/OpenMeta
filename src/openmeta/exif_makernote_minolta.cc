// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>
#include <iterator>

namespace openmeta::exif_internal {
namespace {

enum class A100FieldFormat : uint8_t {
    U8,
    I8,
    U16LE,
    U16BE,
    I16LE,
    I16BE,
    Bytes,
    Text,
};

struct A100Field final {
    uint16_t tag;
    uint32_t byte_offset;
    uint16_t count;
    A100FieldFormat format;
};

#include "minolta_a100_fields.inc"

static constexpr uint32_t kA100MaximumFieldCount = 128U;
static constexpr uint32_t kA100MaximumChildBytes = 18928U;

static const A100Field* minolta_a100_fields(uint16_t child_tag,
                                             size_t* field_count,
                                             std::string_view* table_name) noexcept
{
    if (!field_count || !table_name)
        return nullptr;
    switch (child_tag) {
    case 0x0010U:
        *field_count = std::size(kA100CameraInfoFields);
        *table_name  = "camerainfoa100";
        return kA100CameraInfoFields;
    case 0x0018U:
        *field_count = std::size(kA100ISInfoFields);
        *table_name  = "isinfoa100";
        return kA100ISInfoFields;
    case 0x0020U:
        *field_count = std::size(kA100WBInfoFields);
        *table_name  = "wbinfoa100";
        return kA100WBInfoFields;
    case 0x0114U:
        *field_count = std::size(kA100CameraSettingsFields);
        *table_name  = "camerasettingsa100";
        return kA100CameraSettingsFields;
    default: return nullptr;
    }
}

static bool minolta_a100_field_extent(const A100Field& field,
                                      uint64_t* out_bytes) noexcept
{
    if (!out_bytes || field.count == 0U)
        return false;
    uint64_t unit = 1U;
    if (field.format == A100FieldFormat::U16LE
        || field.format == A100FieldFormat::U16BE
        || field.format == A100FieldFormat::I16LE
        || field.format == A100FieldFormat::I16BE) {
        unit = 2U;
    }
    *out_bytes = unit * field.count;
    return true;
}

static uint32_t minolta_a100_present_field_count(const A100Field* fields,
                                                 size_t field_count,
                                                 uint64_t raw_bytes) noexcept
{
    if (!fields)
        return 0U;
    uint32_t count = 0U;
    for (size_t i = 0U; i < field_count; ++i) {
        uint64_t field_bytes = 0U;
        if (!minolta_a100_field_extent(fields[i], &field_bytes))
            continue;
        const uint64_t offset = fields[i].byte_offset;
        if (offset <= raw_bytes && field_bytes <= raw_bytes - offset)
            ++count;
    }
    return count;
}

static bool minolta_a100_value_bytes(const MetaValue& value,
                                     const MetaStore& store,
                                     std::span<const std::byte>* out) noexcept
{
    if (!out || (value.kind != MetaValueKind::Bytes
                 && !(value.kind == MetaValueKind::Array
                      && value.elem_type == MetaElementType::U8))) {
        return false;
    }
    const std::span<const std::byte> raw = store.arena().span(value.data.span);
    if (raw.size() != value.count)
        return false;
    *out = raw;
    return true;
}

static void minolta_a100_emit_fields(std::string_view ifd_name,
                                     const A100Field* fields,
                                     size_t field_count,
                                     std::span<const std::byte> raw,
                                     MetaStore& store,
                                     const ExifDecodeOptions& options,
                                     ExifDecodeResult* status_out) noexcept
{
    std::array<uint16_t, kA100MaximumFieldCount> tags {};
    std::array<MetaValue, kA100MaximumFieldCount> values {};
    uint32_t value_count = 0U;
    for (size_t i = 0U; i < field_count; ++i) {
        const A100Field& field = fields[i];
        uint64_t field_bytes   = 0U;
        if (value_count >= tags.size()
            || !minolta_a100_field_extent(field, &field_bytes)) {
            if (status_out)
                update_status(status_out, ExifDecodeStatus::Malformed);
            return;
        }
        const uint64_t offset = field.byte_offset;
        if (offset > raw.size() || field_bytes > raw.size() - offset)
            continue;
        const std::span<const std::byte> bytes
            = raw.subspan(static_cast<size_t>(offset),
                          static_cast<size_t>(field_bytes));
        MetaValue value;
        switch (field.format) {
        case A100FieldFormat::U8:
            if (field.count == 1U) {
                value = make_u8(u8(bytes[0]));
            } else {
                std::array<uint8_t, 128> decoded {};
                if (field.count > decoded.size()) {
                    if (status_out)
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    return;
                }
                for (uint32_t j = 0U; j < field.count; ++j)
                    decoded[j] = u8(bytes[j]);
                value = make_u8_array(
                    store.arena(),
                    std::span<const uint8_t>(decoded.data(), field.count));
            }
            break;
        case A100FieldFormat::I8: {
            int8_t signed_value = 0;
            std::memcpy(&signed_value, bytes.data(), sizeof(signed_value));
            value = make_i8(signed_value);
            break;
        }
        case A100FieldFormat::U16LE:
        case A100FieldFormat::U16BE:
        case A100FieldFormat::I16LE:
        case A100FieldFormat::I16BE: {
            const bool le = field.format == A100FieldFormat::U16LE
                            || field.format == A100FieldFormat::I16LE;
            if (field.count == 1U) {
                if (field.format == A100FieldFormat::I16LE
                    || field.format == A100FieldFormat::I16BE) {
                    int16_t signed_value = 0;
                    if (!read_i16_endian(le, bytes, 0U, &signed_value)) {
                        if (status_out)
                            update_status(status_out,
                                          ExifDecodeStatus::Malformed);
                        return;
                    }
                    value = make_i16(signed_value);
                } else {
                    uint16_t unsigned_value = 0U;
                    if (!read_u16_endian(le, bytes, 0U, &unsigned_value)) {
                        if (status_out)
                            update_status(status_out,
                                          ExifDecodeStatus::Malformed);
                        return;
                    }
                    value = make_u16(unsigned_value);
                }
            } else {
                std::array<uint16_t, 128> decoded {};
                if (field.count > decoded.size()) {
                    if (status_out)
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    return;
                }
                for (uint32_t j = 0U; j < field.count; ++j) {
                    if (!read_u16_endian(le, bytes, uint64_t(j) * 2U,
                                         &decoded[j])) {
                        if (status_out)
                            update_status(status_out,
                                          ExifDecodeStatus::Malformed);
                        return;
                    }
                }
                value = make_u16_array(
                    store.arena(),
                    std::span<const uint16_t>(decoded.data(), field.count));
            }
            break;
        }
        case A100FieldFormat::Bytes:
            value = make_bytes(store.arena(), bytes);
            break;
        case A100FieldFormat::Text:
            value = make_fixed_ascii_text(store.arena(), bytes);
            break;
        }
        if (value.kind == MetaValueKind::Empty) {
            if (status_out)
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        tags[value_count]   = field.tag;
        values[value_count] = value;
        ++value_count;
    }
    if (value_count != 0U) {
        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), value_count),
                             std::span<const MetaValue>(values.data(),
                                                        value_count),
                             options.limits, status_out);
    }
}

static void minolta_a100_mark_limit(ExifDecodeResult* result,
                                    ExifLimitReason reason,
                                    uint64_t ifd_offset,
                                    uint16_t tag) noexcept
{
    if (!result)
        return;
    if (result->status != ExifDecodeStatus::LimitExceeded) {
        result->status           = ExifDecodeStatus::LimitExceeded;
        result->limit_reason     = reason;
        result->limit_ifd_offset = ifd_offset;
        result->limit_tag        = tag;
    } else if (result->limit_reason == ExifLimitReason::None) {
        result->limit_reason     = reason;
        result->limit_ifd_offset = ifd_offset;
        result->limit_tag        = tag;
    }
}

static bool minolta_a100_field_payload_bytes(const A100Field& field,
                                             uint64_t* out_bytes) noexcept
{
    if (!out_bytes || field.count <= 1U)
        return false;
    if (field.format == A100FieldFormat::U8
        || field.format == A100FieldFormat::I8
        || field.format == A100FieldFormat::U16LE
        || field.format == A100FieldFormat::U16BE
        || field.format == A100FieldFormat::I16LE
        || field.format == A100FieldFormat::I16BE
        || field.format == A100FieldFormat::Bytes) {
        return minolta_a100_field_extent(field, out_bytes);
    }
    return false;
}

static bool minolta_a100_preflight(
    SourceTiffReader* source, const TiffConfig& cfg, uint32_t ifd_offset,
    MetaStore& store, const ExifDecodeOptions& options,
    ExifDecodeResult* status_out) noexcept
{
    if (!source || !status_out || cfg.bigtiff) {
        return false;
    }
    store.constrain_resources(options.limits.max_total_entries,
                              options.limits.max_arena_bytes);
    if (store.resource_limit_exceeded()) {
        minolta_a100_mark_limit(
            status_out,
            store.arena().limit_exceeded() ? ExifLimitReason::MaxArenaBytes
                                           : ExifLimitReason::MaxTotalEntries,
            ifd_offset, 0U);
        return false;
    }

    std::span<const std::byte> bytes;
    uint16_t root_entries = 0U;
    if (!source_tiff_view(source, ifd_offset, 2U, &bytes)
        || !read_tiff_u16(cfg, bytes, 0U, &root_entries)
        || root_entries == 0U) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return false;
    }
    if (root_entries > options.limits.max_entries_per_ifd) {
        minolta_a100_mark_limit(status_out,
                                ExifLimitReason::MaxEntriesPerIfd, ifd_offset,
                                0U);
        return false;
    }
    const uint64_t root_extent = 6ULL + uint64_t(root_entries) * 12ULL;
    if (!source_tiff_contains(*source, ifd_offset, root_extent)) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return false;
    }

    uint32_t child_tables       = 0U;
    uint32_t derived_entries    = 0U;
    uint32_t child_indices[4]   = {};
    uint64_t minimum_arena_need = 0U;
    const std::string_view mk_prefix = "mk_minolta";
    for (uint32_t i = 0U; i < root_entries; ++i) {
        const uint64_t entry_offset
            = uint64_t(ifd_offset) + 2ULL + uint64_t(i) * 12ULL;
        if (!source_tiff_view(source, entry_offset, 12U, &bytes)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        ClassicIfdEntry entry;
        uint64_t value_bytes = 0U;
        if (!read_classic_ifd_entry(cfg, bytes, 0U, &entry)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        const bool has_value_extent
            = classic_ifd_entry_value_bytes(entry, &value_bytes);
        size_t table_field_count = 0U;
        std::string_view table_name;
        const A100Field* fields = minolta_a100_fields(
            entry.tag, &table_field_count, &table_name);
        uint32_t table_index = 0U;
        switch (entry.tag) {
        case 0x0010U: table_index = child_indices[0]++; break;
        case 0x0018U: table_index = child_indices[1]++; break;
        case 0x0020U: table_index = child_indices[2]++; break;
        case 0x0114U: table_index = child_indices[3]++; break;
        default: break;
        }

        if (!has_value_extent) {
            if (fields) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return false;
            }
            continue;
        }
        if (tiff_type_size(entry.type) != 0U)
            minimum_arena_need += std::string_view("mk_minolta0").size();
        if (fields
            && ((entry.type != 1U && entry.type != 7U)
                || entry.count32 == 0U || value_bytes <= 4U)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        if (value_bytes <= 4U)
            continue;
        const uint64_t value_offset = entry.value_or_off32;
        if (!source_tiff_contains(*source, value_offset, value_bytes)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        if (!fields)
            continue;
        if (value_bytes > options.limits.max_value_bytes) {
            minolta_a100_mark_limit(status_out,
                                    ExifLimitReason::ValueCountTooLarge,
                                    ifd_offset, entry.tag);
            return false;
        }
        const uint32_t table_entries
            = minolta_a100_present_field_count(fields, table_field_count,
                                               value_bytes);
        if (table_entries > options.limits.max_entries_per_ifd) {
            minolta_a100_mark_limit(status_out,
                                    ExifLimitReason::MaxEntriesPerIfd,
                                    ifd_offset, entry.tag);
            return false;
        }
        if (child_tables == UINT32_MAX
            || table_entries > UINT32_MAX - derived_entries) {
            minolta_a100_mark_limit(status_out,
                                    ExifLimitReason::MaxTotalEntries,
                                    ifd_offset, entry.tag);
            return false;
        }
        ++child_tables;
        derived_entries += table_entries;
        minimum_arena_need += value_bytes;
        if (table_entries != 0U) {
            char scratch[64];
            const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                mk_prefix, table_name, table_index, std::span<char>(scratch));
            if (sub_ifd.empty()) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return false;
            }
            minimum_arena_need += uint64_t(table_entries) * sub_ifd.size();
            for (size_t j = 0U; j < table_field_count; ++j) {
                uint64_t field_extent = 0U;
                if (!minolta_a100_field_extent(fields[j], &field_extent))
                    continue;
                const uint64_t field_offset = fields[j].byte_offset;
                if (field_offset > value_bytes
                    || field_extent > value_bytes - field_offset) {
                    continue;
                }
                uint64_t payload_bytes = 0U;
                if (minolta_a100_field_payload_bytes(fields[j],
                                                     &payload_bytes)) {
                    minimum_arena_need += payload_bytes;
                }
            }
        }
    }

    const uint64_t required_ifds = uint64_t(child_tables) + 1U;
    if (uint64_t(status_out->ifds_needed) + required_ifds
        > options.limits.max_ifds) {
        minolta_a100_mark_limit(status_out, ExifLimitReason::MaxIfds,
                                ifd_offset, 0xb028U);
        return false;
    }
    const uint64_t current_entries
        = (store.entries().size() > status_out->entries_decoded)
              ? store.entries().size()
              : status_out->entries_decoded;
    const uint64_t entries_needed
        = uint64_t(root_entries) + derived_entries;
    if (current_entries + entries_needed > options.limits.max_total_entries) {
        minolta_a100_mark_limit(status_out,
                                ExifLimitReason::MaxTotalEntries, ifd_offset,
                                0xb028U);
        return false;
    }
    if (options.limits.max_arena_bytes != 0U) {
        const uint64_t arena_used = store.arena().bytes().size();
        if (arena_used > options.limits.max_arena_bytes
            || minimum_arena_need
                   > options.limits.max_arena_bytes - arena_used) {
            minolta_a100_mark_limit(status_out,
                                    ExifLimitReason::MaxArenaBytes, ifd_offset,
                                    0xb028U);
            return false;
        }
    }
    return true;
}

static uint32_t minolta_a100_child_slot(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x0010U: return 0U;
    case 0x0018U: return 1U;
    case 0x0020U: return 2U;
    case 0x0114U: return 3U;
    default: return UINT32_MAX;
    }
}

static void decode_minolta_u32_table(std::string_view ifd_name,
                                     std::span<const std::byte> raw,
                                     bool big_endian,
                                     MetaStore& store,
                                     const ExifDecodeOptions& options,
                                     ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || raw.empty()) {
        return;
    }
    if (raw.size() > options.limits.max_value_bytes) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    // `raw` often references `store.arena()` memory. Adding derived entries may
    // grow the arena (realloc), invalidating `raw.data()`. Copy to a stable
    // local buffer first.
    std::array<std::byte, 8192> stable_buf {};
    if (raw.size() > stable_buf.size()) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }
    std::memcpy(stable_buf.data(), raw.data(), raw.size());
    const std::span<const std::byte> stable(stable_buf.data(), raw.size());

    const uint32_t count = static_cast<uint32_t>(stable.size() / 4U);
    if (count == 0) {
        return;
    }
    if (count > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (i > 0xFFFFu) {
            break;
        }
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        uint32_t v = 0;
        if (big_endian) {
            (void)read_u32be(stable, static_cast<uint64_t>(i) * 4U, &v);
        } else {
            std::memcpy(&v, stable.data() + i * 4U, 4U);
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd_name,
                                      static_cast<uint16_t>(i));
        entry.origin.block          = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Other, 4 };
        entry.origin.wire_count     = 1;
        entry.flags |= EntryFlags::Derived;
        entry.value = make_u32(v);

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static void decode_minolta_u16_table(std::string_view ifd_name,
                                     std::span<const std::byte> raw,
                                     bool big_endian,
                                     MetaStore& store,
                                     const ExifDecodeOptions& options,
                                     ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || raw.empty()) {
        return;
    }
    if (raw.size() > options.limits.max_value_bytes) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    std::array<std::byte, 8192> stable_buf {};
    if (raw.size() > stable_buf.size()) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }
    std::memcpy(stable_buf.data(), raw.data(), raw.size());
    const std::span<const std::byte> stable(stable_buf.data(), raw.size());

    const uint32_t count = static_cast<uint32_t>(stable.size() / 2U);
    if (count == 0) {
        return;
    }
    if (count > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (i > 0xFFFFu) {
            break;
        }
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        uint16_t v = 0;
        if (big_endian) {
            (void)read_u16be(stable, static_cast<uint64_t>(i) * 2U, &v);
        } else {
            std::memcpy(&v, stable.data() + i * 2U, 2U);
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd_name,
                                      static_cast<uint16_t>(i));
        entry.origin.block          = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Other, 2 };
        entry.origin.wire_count     = 1;
        entry.flags |= EntryFlags::Derived;
        entry.value = make_u16(v);

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static void decode_minolta_binary_subdirs(std::string_view mk_ifd0,
                                          MetaStore& store,
                                          const ExifDecodeOptions& options,
                                          ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return;
    }

    const std::string_view mk_prefix = options.tokens.ifd_prefix;
    const size_t source_entry_count  = store.entries().size();

    uint32_t idx_settings   = 0;
    uint32_t idx_settings7d = 0;
    uint32_t idx_settings5d = 0;

    for (size_t i = 0; i < source_entry_count; ++i) {
        uint16_t tag = 0;
        MetaValue value;
        {
            // Derived-table emission can reallocate both the entry vector and
            // arena. Copy descriptors before mutating the store.
            const Entry& e = store.entries()[i];
            if (e.key.kind != MetaKeyKind::ExifTag
                || arena_string(store.arena(), e.key.data.exif_tag.ifd)
                       != mk_ifd0) {
                continue;
            }
            tag   = e.key.data.exif_tag.tag;
            value = e.value;
        }

        // 0x0001/0x0003: CameraSettings (big-endian int32u array in ExifTool).
        if (tag == 0x0001 || tag == 0x0003) {
            if (value.kind != MetaValueKind::Bytes
                && !(value.kind == MetaValueKind::Array
                     && value.elem_type == MetaElementType::U32)) {
                continue;
            }
            const std::span<const std::byte> raw
                = store.arena().span(value.data.span);

            char scratch[64];
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "camerasettings", idx_settings++,
                std::span<char>(scratch));
            if (!ifd_name.empty()) {
                const bool be = (value.kind == MetaValueKind::Bytes);
                decode_minolta_u32_table(ifd_name, raw, be, store, options,
                                         status_out);
            }
            continue;
        }

        // 0x0004: CameraSettings7D (big-endian int16u array in ExifTool).
        if (tag == 0x0004) {
            if (value.kind != MetaValueKind::Bytes
                && !(value.kind == MetaValueKind::Array
                     && value.elem_type == MetaElementType::U16)) {
                continue;
            }
            const std::span<const std::byte> raw
                = store.arena().span(value.data.span);

            char scratch[64];
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "camerasettings7d", idx_settings7d++,
                std::span<char>(scratch));
            if (!ifd_name.empty()) {
                const bool be = (value.kind == MetaValueKind::Bytes);
                decode_minolta_u16_table(ifd_name, raw, be, store, options,
                                         status_out);
            }
            continue;
        }

        // 0x0114: CameraSettings5D/A100 (big-endian int16u binary table in ExifTool).
        if (tag == 0x0114) {
            if (value.kind != MetaValueKind::Bytes
                && !(value.kind == MetaValueKind::Array
                     && value.elem_type == MetaElementType::U16)) {
                continue;
            }
            const std::span<const std::byte> raw
                = store.arena().span(value.data.span);

            char scratch[64];
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "camerasettings5d", idx_settings5d++,
                std::span<char>(scratch));
            if (!ifd_name.empty()) {
                const bool be = (value.kind == MetaValueKind::Bytes);
                decode_minolta_u16_table(ifd_name, raw, be, store, options,
                                         status_out);
            }
            continue;
        }
    }
}


static bool select_minolta_ifd0(const TiffConfig& parent_cfg,
                                std::span<const std::byte> maker_note,
                                const ExifDecodeLimits& limits,
                                ClassicIfdCandidate* out) noexcept
{
    if (!out) {
        return false;
    }
    *out = ClassicIfdCandidate {};

    TiffConfig cfg = parent_cfg;
    cfg.bigtiff    = false;

    uint16_t entry_count = 0U;
    if (!read_tiff_u16(cfg, maker_note, 0U, &entry_count)
        || entry_count == 0U || entry_count > 512U
        || entry_count > limits.max_entries_per_ifd) {
        return false;
    }

    const uint64_t table_bytes = 2ULL + uint64_t(entry_count) * 12ULL + 4ULL;
    if (table_bytes > maker_note.size()) {
        return false;
    }

    // Minolta IFD0 begins at the note start, but out-of-line values use the
    // parent TIFF base. Only its table is validated against the note slice.
    uint32_t valid_entries = 0U;
    for (uint32_t i = 0U; i < entry_count; ++i) {
        const uint64_t entry_off = 2ULL + uint64_t(i) * 12ULL;
        ClassicIfdEntry entry;
        uint64_t value_bytes = 0U;
        if (read_classic_ifd_entry(cfg, maker_note, entry_off, &entry)
            && classic_ifd_entry_value_bytes(entry, &value_bytes)
            && value_bytes <= limits.max_value_bytes) {
            ++valid_entries;
        }
    }

    const uint32_t min_valid = (entry_count > 4U)
                                   ? (uint32_t(entry_count) / 2U)
                                   : uint32_t(entry_count);
    if (valid_entries < min_valid) {
        return false;
    }

    out->offset        = 0U;
    out->le            = cfg.le;
    out->entry_count   = entry_count;
    out->valid_entries = valid_entries;
    return true;
}

}  // namespace

void decode_minolta_a100_subtree_from_source(
    SourceTiffReader* source, const TiffConfig& parent_cfg,
    uint32_t ifd_offset, MetaStore& store,
    const ExifDecodeOptions& options,
    ExifDecodeResult* status_out) noexcept
{
    if (!minolta_a100_preflight(source, parent_cfg, ifd_offset, store,
                                options, status_out)) {
        return;
    }

    const ExifDecodeStatus previous_status = status_out->status;
    const OffsetPolicy offsets;
    const size_t root_store_entry_start = store.entries().size();
    if (!decode_classic_ifd_from_source(
            source, parent_cfg, ifd_offset, offsets, "mk_minolta0", store,
            options, status_out, EntryFlags::None)) {
        return;
    }
    if (status_out->status == ExifDecodeStatus::LimitExceeded
        || (status_out->status == ExifDecodeStatus::Malformed
            && previous_status != ExifDecodeStatus::Malformed)) {
        return;
    }
    if (store.resource_limit_exceeded()) {
        minolta_a100_mark_limit(
            status_out,
            store.arena().limit_exceeded() ? ExifLimitReason::MaxArenaBytes
                                           : ExifLimitReason::MaxTotalEntries,
            ifd_offset, 0xb028U);
        return;
    }

    const size_t root_store_entries = store.entries().size();
    uint32_t child_indices[4] = {};
    for (size_t i = root_store_entry_start; i < root_store_entries; ++i) {
        uint16_t tag = 0U;
        MetaValue value;
        {
            const Entry& entry = store.entries()[i];
            if (entry.key.kind != MetaKeyKind::ExifTag
                || arena_string(store.arena(), entry.key.data.exif_tag.ifd)
                       != "mk_minolta0") {
                continue;
            }
            tag   = entry.key.data.exif_tag.tag;
            value = entry.value;
        }

        size_t field_count = 0U;
        std::string_view table_name;
        const A100Field* fields
            = minolta_a100_fields(tag, &field_count, &table_name);
        const uint32_t slot = minolta_a100_child_slot(tag);
        if (!fields || slot == UINT32_MAX)
            continue;
        const uint32_t table_index = child_indices[slot]++;
        std::span<const std::byte> raw;
        if (!minolta_a100_value_bytes(value, store, &raw)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return;
        }
        const uint32_t present_fields
            = minolta_a100_present_field_count(fields, field_count, raw.size());
        if (present_fields == 0U)
            continue;

        std::array<std::byte, kA100MaximumChildBytes> stable {};
        const size_t stable_size
            = (raw.size() < stable.size()) ? raw.size() : stable.size();
        std::memcpy(stable.data(), raw.data(), stable_size);
        char scratch[64];
        const std::string_view ifd_name = make_mk_subtable_ifd_token(
            "mk_minolta", table_name, table_index, std::span<char>(scratch));
        if (ifd_name.empty()) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return;
        }
        minolta_a100_emit_fields(
            ifd_name, fields, field_count,
            std::span<const std::byte>(stable.data(), stable_size), store,
            options, status_out);
        if (status_out->status == ExifDecodeStatus::LimitExceeded)
            return;
    }
}

bool decode_minolta_makernote(const TiffConfig& parent_cfg,
                              std::span<const std::byte> tiff_bytes,
                              uint64_t maker_note_off,
                              uint64_t maker_note_bytes,
                              std::string_view mk_ifd0, MetaStore& store,
                              const ExifDecodeOptions& options,
                              ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return false;
    }
    if (maker_note_off > tiff_bytes.size()) {
        return false;
    }
    if (maker_note_bytes > (tiff_bytes.size() - maker_note_off)) {
        return false;
    }

    const std::span<const std::byte> mn
        = tiff_bytes.subspan(static_cast<size_t>(maker_note_off),
                             static_cast<size_t>(maker_note_bytes));

    ClassicIfdCandidate best;
    if (!select_minolta_ifd0(parent_cfg, mn, options.limits, &best)) {
        return true;
    }

    TiffConfig cfg       = parent_cfg;
    cfg.bigtiff          = false;
    cfg.le               = best.le;
    const uint64_t ifd_off = maker_note_off;
    decode_classic_ifd_no_header(cfg, tiff_bytes, ifd_off, mk_ifd0, store,
                                 options, status_out, EntryFlags::None);

    decode_minolta_binary_subdirs(mk_ifd0, store, options, status_out);

    return true;
}

bool decode_minolta_makernote_from_source(
    SourceTiffReader* source, const TiffConfig& parent_cfg,
    uint64_t maker_note_off, std::span<const std::byte> maker_note,
    std::string_view mk_ifd0, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    if (!source || !source->result || mk_ifd0.empty() || maker_note.empty()
        || !source_tiff_contains(*source, maker_note_off, maker_note.size())) {
        return false;
    }

    ClassicIfdCandidate best;
    if (!select_minolta_ifd0(parent_cfg, maker_note, options.limits, &best)) {
        return false;
    }

    TiffConfig cfg = parent_cfg;
    cfg.bigtiff    = false;
    cfg.le         = best.le;
    const OffsetPolicy offsets;
    if (!decode_classic_ifd_from_source(source, cfg,
                                        maker_note_off, offsets,
                                        mk_ifd0, store, options, status_out,
                                        EntryFlags::None)) {
        return false;
    }

    decode_minolta_binary_subdirs(mk_ifd0, store, options, status_out);
    return true;
}

}  // namespace openmeta::exif_internal
