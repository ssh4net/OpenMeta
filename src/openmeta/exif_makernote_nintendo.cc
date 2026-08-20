// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>

namespace openmeta::exif_internal {

namespace {

    static bool find_mk_tag_value(std::string_view ifd, uint16_t tag,
                                  const MetaStore& store,
                                  MetaValue* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out = MetaValue {};

        const ByteArena& arena               = store.arena();
        const std::span<const Entry> entries = store.entries();

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (e.key.data.exif_tag.tag != tag) {
                continue;
            }
            if (arena_string(arena, e.key.data.exif_tag.ifd) != ifd) {
                continue;
            }
            *out = e.value;
            return true;
        }
        return false;
    }

    static void
    decode_nintendo_camera_info(std::string_view mk_ifd0, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept
    {
        // ExifTool flattens Nintendo CameraInfo fields (tag 0x1101) into the
        // Nintendo group. Decode this binary subdirectory best-effort so
        // `metaread` prints the same tag ids as ExifTool (-D).
        MetaValue cam_dir;
        if (!find_mk_tag_value(mk_ifd0, 0x1101, store, &cam_dir)
            || (cam_dir.kind != MetaValueKind::Bytes
                && cam_dir.kind != MetaValueKind::Array)) {
            return;
        }

        const std::span<const std::byte> cam_src = store.arena().span(
            cam_dir.data.span);
        if (cam_src.empty()) {
            return;
        }

        // Adding derived tags may grow the arena and invalidate cam_src.
        std::array<std::byte, 256> stable {};
        if (cam_src.size() > stable.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return;
        }
        std::memcpy(stable.data(), cam_src.data(), cam_src.size());
        const std::span<const std::byte> cam(stable.data(), cam_src.size());

        char scratch[64];
        const std::string_view cam_ifd
            = make_mk_subtable_ifd_token("mk_nintendo", "camerainfo", 0,
                                         std::span<char>(scratch));
        if (cam_ifd.empty()) {
            return;
        }

        std::array<uint16_t, 5> tags {};
        std::array<MetaValue, 5> vals {};
        uint32_t n       = 0;
        ByteArena& arena = store.arena();

        if (cam.size() >= 4) {
            tags[n] = 0x0000;
            vals[n] = make_fixed_ascii_text(arena, cam.subspan(0, 4));
            n += 1;
        }

        uint32_t ts = 0;
        if (read_u32le(cam, 0x0008, &ts)) {
            tags[n] = 0x0008;
            vals[n] = make_u32(ts);
            n += 1;
        }

        if (cam.size() >= 0x0018 + 4) {
            tags[n] = 0x0018;
            vals[n] = make_bytes(arena, cam.subspan(0x0018, 4));
            n += 1;
        }

        uint32_t par_bits = 0;
        if (read_u32le(cam, 0x0028, &par_bits)) {
            tags[n] = 0x0028;
            vals[n] = make_f32_bits(par_bits);
            n += 1;
        }

        uint16_t cat = 0;
        if (read_u16le(cam, 0x0030, &cat)) {
            tags[n] = 0x0030;
            vals[n] = make_u16(cat);
            n += 1;
        }

        if (n != 0) {
            emit_bin_dir_entries(cam_ifd, store,
                                 std::span<const uint16_t>(tags.data(), n),
                                 std::span<const MetaValue>(vals.data(), n),
                                 options.limits, status_out);
        }
    }

}  // namespace

bool
decode_nintendo_makernote(const TiffConfig& parent_cfg,
                          std::span<const std::byte> tiff_bytes,
                          uint64_t maker_note_off, uint64_t maker_note_bytes,
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

    // Nintendo MakerNotes start with a classic IFD at offset 0. Some files use
    // value offsets relative to the outer EXIF/TIFF stream.
    TiffConfig cfg      = parent_cfg;
    bool ok_abs_offsets = false;
    bool ok_rel_offsets = false;
    bool ok             = false;

    for (uint32_t attempt = 0; attempt < 2; ++attempt) {
        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, maker_note_off, &entry_count)) {
            cfg.le = !cfg.le;
            continue;
        }
        if (entry_count == 0
            || entry_count > options.limits.max_entries_per_ifd) {
            cfg.le = !cfg.le;
            continue;
        }

        const uint64_t ifd_table_bytes = 2U + uint64_t(entry_count) * 12ULL
                                         + 4ULL;
        const uint64_t mn_end = maker_note_off + maker_note_bytes;
        if (maker_note_off + ifd_table_bytes > mn_end) {
            cfg.le = !cfg.le;
            continue;
        }

        // Decide whether out-of-line value offsets are absolute (into the
        // outer TIFF stream) or relative to the MakerNote blob.
        ok_abs_offsets = false;
        ok_rel_offsets = false;

        const uint64_t entries_off = maker_note_off + 2U;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            ClassicIfdEntry e;
            if (!read_classic_ifd_entry(cfg, tiff_bytes, eoff, &e)) {
                break;
            }

            uint64_t value_bytes = 0;
            if (!classic_ifd_entry_value_bytes(e, &value_bytes)) {
                continue;
            }
            if (value_bytes <= 4) {
                continue;  // inline
            }

            const uint64_t rel_off = uint64_t(e.value_or_off32);
            const uint64_t abs_off = uint64_t(e.value_or_off32);

            if (rel_off + value_bytes <= maker_note_bytes) {
                ok_rel_offsets = true;
            }
            if (abs_off + value_bytes <= tiff_bytes.size()) {
                ok_abs_offsets = true;
            }

            // If any out-of-line offset is beyond the MakerNote byte count,
            // it can't be a relative offset.
            if (rel_off >= maker_note_bytes && ok_abs_offsets) {
                ok_rel_offsets = false;
                break;
            }
        }

        ok = true;
        break;
    }
    if (!ok) {
        return false;
    }

    if (ok_abs_offsets && !ok_rel_offsets) {
        decode_classic_ifd_no_header(cfg, tiff_bytes, maker_note_off, mk_ifd0,
                                     store, options, status_out,
                                     EntryFlags::None);
    } else {
        decode_classic_ifd_no_header(cfg, mn, 0, mk_ifd0, store, options,
                                     status_out, EntryFlags::None);
    }

    decode_nintendo_camera_info(mk_ifd0, store, options, status_out);

    return true;
}


bool
decode_nintendo_makernote_from_source(
    SourceTiffReader* source, const TiffConfig& parent_cfg,
    uint64_t maker_note_off, std::span<const std::byte> maker_note,
    std::string_view mk_ifd0, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    if (!source || !source->result || mk_ifd0.empty()
        || !source_tiff_contains(*source, maker_note_off, maker_note.size())) {
        return false;
    }

    TiffConfig cfg = parent_cfg;
    for (uint32_t attempt = 0; attempt < 2; ++attempt) {
        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, maker_note, 0U, &entry_count)
            || entry_count == 0U
            || entry_count > options.limits.max_entries_per_ifd) {
            cfg.le = !cfg.le;
            continue;
        }

        const uint64_t table_bytes = 2U + uint64_t(entry_count) * 12ULL + 4U;
        if (table_bytes > maker_note.size()) {
            cfg.le = !cfg.le;
            continue;
        }

        bool ok_abs_offsets = false;
        bool ok_rel_offsets = false;
        for (uint32_t i = 0; i < entry_count; ++i) {
            ClassicIfdEntry entry;
            if (!read_classic_ifd_entry(cfg, maker_note, 2U + uint64_t(i) * 12U,
                                        &entry)) {
                return false;
            }
            uint64_t value_bytes = 0;
            if (!classic_ifd_entry_value_bytes(entry, &value_bytes)
                || value_bytes <= 4U) {
                continue;
            }
            const uint64_t off = entry.value_or_off32;
            if (off <= maker_note.size()
                && value_bytes <= maker_note.size() - off) {
                ok_rel_offsets = true;
            }
            if (source_tiff_contains(*source, off, value_bytes)) {
                ok_abs_offsets = true;
            }
            if (off >= maker_note.size() && ok_abs_offsets) {
                ok_rel_offsets = false;
                break;
            }
        }

        OffsetPolicy offsets;
        if (!(ok_abs_offsets && !ok_rel_offsets)) {
            offsets.out_of_line_base = maker_note_off;
        }
        if (!decode_classic_ifd_from_source(source, cfg, maker_note_off,
                                            offsets, mk_ifd0, store, options,
                                            status_out, EntryFlags::None)) {
            return false;
        }
        decode_nintendo_camera_info(mk_ifd0, store, options, status_out);
        return true;
    }
    return false;
}

}  // namespace openmeta::exif_internal
