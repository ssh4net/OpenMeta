// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>

namespace openmeta::exif_internal {
namespace {

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
                decode_minolta_u16_table(ifd_name, raw, true, store, options,
                                         status_out);
            }
            continue;
        }
    }
}

}  // namespace

bool decode_minolta_makernote(const TiffConfig& /*parent_cfg*/,
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
    if (!find_best_classic_ifd_candidate(mn, 256, options.limits, &best)) {
        return false;
    }

    TiffConfig cfg;
    cfg.bigtiff = false;
    cfg.le      = best.le;
    decode_classic_ifd_no_header(cfg, mn, best.offset, mk_ifd0, store, options,
                                 status_out, EntryFlags::None);

    decode_minolta_binary_subdirs(mk_ifd0, store, options, status_out);

    return true;
}

}  // namespace openmeta::exif_internal
