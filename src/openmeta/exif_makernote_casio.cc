// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <cstdint>

namespace openmeta::exif_internal {

namespace {

    static bool casio_faceinfo1_bytes(std::span<const std::byte> raw) noexcept
    {
        if (raw.size() >= 2 && u8(raw[0]) == 0x00 && u8(raw[1]) == 0x00) {
            return true;
        }
        if (raw.size() >= 5 && u8(raw[1]) == 0x02 && u8(raw[2]) == 0x80
            && u8(raw[3]) == 0x01 && u8(raw[4]) == 0xE0) {
            return true;
        }
        return false;
    }

    static bool casio_faceinfo2_bytes(std::span<const std::byte> raw) noexcept
    {
        return raw.size() >= 2 && u8(raw[0]) == 0x02 && u8(raw[1]) == 0x01;
    }

    static bool casio_frame_size_plausible(uint16_t w, uint16_t h) noexcept
    {
        if (w == 0 || h == 0) {
            return false;
        }
        if (w > 20000U || h > 20000U) {
            return false;
        }
        return true;
    }

    static bool casio_choose_endian_for_u16_pair(std::span<const std::byte> raw,
                                                 uint64_t off, bool default_le,
                                                 bool* out_le) noexcept
    {
        if (!out_le) {
            return false;
        }
        if (off + 4U > raw.size()) {
            return false;
        }

        uint16_t a_be    = 0;
        uint16_t b_be    = 0;
        const bool ok_be = read_u16_endian(false, raw, off + 0, &a_be)
                           && read_u16_endian(false, raw, off + 2, &b_be);

        uint16_t a_le    = 0;
        uint16_t b_le    = 0;
        const bool ok_le = read_u16_endian(true, raw, off + 0, &a_le)
                           && read_u16_endian(true, raw, off + 2, &b_le);

        const bool be_plausible = ok_be
                                  && casio_frame_size_plausible(a_be, b_be);
        const bool le_plausible = ok_le
                                  && casio_frame_size_plausible(a_le, b_le);

        if (be_plausible && !le_plausible) {
            *out_le = false;
            return true;
        }
        if (le_plausible && !be_plausible) {
            *out_le = true;
            return true;
        }

        *out_le = default_le;
        return true;
    }

    static bool casio_read_u16_array(std::span<const std::byte> raw,
                                     uint64_t off, bool le, uint16_t* out,
                                     uint32_t count) noexcept
    {
        if (!out || count == 0) {
            return false;
        }
        if (off > raw.size() || (raw.size() - off) < (uint64_t(count) * 2ULL)) {
            return false;
        }
        for (uint32_t i = 0; i < count; ++i) {
            uint16_t v = 0;
            if (!read_u16_endian(le, raw, off + uint64_t(i) * 2ULL, &v)) {
                return false;
            }
            out[i] = v;
        }
        return true;
    }

    static void decode_casio_faceinfo1(std::string_view ifd_name,
                                       std::span<const std::byte> raw,
                                       MetaStore& store,
                                       const ExifDecodeLimits& limits,
                                       ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || raw.empty()) {
            return;
        }

        const uint8_t faces = u8(raw[0]);

        uint16_t tags_out[16];
        MetaValue vals_out[16];
        uint32_t out_count = 0;

        tags_out[out_count] = 0x0000;
        vals_out[out_count] = make_u8(faces);
        out_count += 1;

        bool le = false;
        (void)casio_choose_endian_for_u16_pair(raw, 0x0001, false, &le);

        if (faces >= 1U && raw.size() >= 0x0001 + 4U) {
            uint16_t dims[2];
            if (casio_read_u16_array(raw, 0x0001, le, dims, 2)) {
                tags_out[out_count] = 0x0001;
                vals_out[out_count]
                    = make_u16_array(store.arena(),
                                     std::span<const uint16_t>(dims, 2));
                out_count += 1;
            }
        }

        static constexpr uint16_t kFacePosTags[10] = {
            0x000d, 0x007c, 0x00eb, 0x015a, 0x01c9,
            0x0238, 0x02a7, 0x0316, 0x0385, 0x03f4,
        };

        const uint32_t face_n = (faces < 10U) ? static_cast<uint32_t>(faces)
                                              : 10U;
        for (uint32_t i = 0; i < face_n; ++i) {
            const uint16_t tag = kFacePosTags[i];
            if (raw.size() < uint64_t(tag) + 8U) {
                continue;
            }
            uint16_t pos[4];
            if (!casio_read_u16_array(raw, tag, le, pos, 4)) {
                continue;
            }
            tags_out[out_count] = tag;
            vals_out[out_count]
                = make_u16_array(store.arena(),
                                 std::span<const uint16_t>(pos, 4));
            out_count += 1;
            if (out_count >= (sizeof(tags_out) / sizeof(tags_out[0]))) {
                break;
            }
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             limits, status_out);
    }

    static void decode_casio_faceinfo2(std::string_view ifd_name,
                                       std::span<const std::byte> raw,
                                       MetaStore& store,
                                       const ExifDecodeLimits& limits,
                                       ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || raw.size() < 3U) {
            return;
        }

        const uint8_t faces = u8(raw[2]);

        uint16_t tags_out[16];
        MetaValue vals_out[16];
        uint32_t out_count = 0;

        tags_out[out_count] = 0x0002;
        vals_out[out_count] = make_u8(faces);
        out_count += 1;

        bool le = true;
        (void)casio_choose_endian_for_u16_pair(raw, 0x0004, true, &le);

        if (faces >= 1U && raw.size() >= 0x0004 + 4U) {
            uint16_t dims[2];
            if (casio_read_u16_array(raw, 0x0004, le, dims, 2)) {
                tags_out[out_count] = 0x0004;
                vals_out[out_count]
                    = make_u16_array(store.arena(),
                                     std::span<const uint16_t>(dims, 2));
                out_count += 1;
            }

            if (raw.size() >= 0x0008 + 1U) {
                tags_out[out_count] = 0x0008;
                vals_out[out_count] = make_u8(u8(raw[0x0008]));
                out_count += 1;
            }
        }

        static constexpr uint16_t kFacePosTags[10] = {
            0x0018, 0x004c, 0x0080, 0x00b4, 0x00e8,
            0x011c, 0x0150, 0x0184, 0x01b8, 0x01ec,
        };

        const uint32_t face_n = (faces < 10U) ? static_cast<uint32_t>(faces)
                                              : 10U;
        for (uint32_t i = 0; i < face_n; ++i) {
            const uint16_t tag = kFacePosTags[i];
            if (raw.size() < uint64_t(tag) + 8U) {
                continue;
            }
            uint16_t pos[4];
            if (!casio_read_u16_array(raw, tag, le, pos, 4)) {
                continue;
            }
            tags_out[out_count] = tag;
            vals_out[out_count]
                = make_u16_array(store.arena(),
                                 std::span<const uint16_t>(pos, 4));
            out_count += 1;
            if (out_count >= (sizeof(tags_out) / sizeof(tags_out[0]))) {
                break;
            }
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             limits, status_out);
    }

    static void
    decode_casio_binary_subdirs(std::string_view mk_ifd0, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return;
        }

        const ByteArena& arena               = store.arena();
        const std::span<const Entry> entries = store.entries();

        uint32_t idx_faceinfo1 = 0;
        uint32_t idx_faceinfo2 = 0;

        char sub_ifd_buf[96];

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (arena_string(arena, e.key.data.exif_tag.ifd) != mk_ifd0) {
                continue;
            }
            if (e.key.data.exif_tag.tag != 0x2089) {
                continue;
            }
            if (e.value.kind != MetaValueKind::Bytes
                && e.value.kind != MetaValueKind::Array) {
                continue;
            }

            const ByteSpan raw_span                  = e.value.data.span;
            const std::span<const std::byte> raw_src = arena.span(raw_span);
            if (raw_src.empty()) {
                continue;
            }

            const std::string_view mk_prefix = "mk_casio";

            if (casio_faceinfo1_bytes(raw_src)) {
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "faceinfo1",
                                                 idx_faceinfo1++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_casio_faceinfo1(ifd_name, raw_src, store, options.limits,
                                       status_out);
                continue;
            }

            if (casio_faceinfo2_bytes(raw_src)) {
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "faceinfo2",
                                                 idx_faceinfo2++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_casio_faceinfo2(ifd_name, raw_src, store, options.limits,
                                       status_out);
                continue;
            }
        }
    }

    static bool
    casio_type2_uses_legacy_main_compat(const TiffConfig& cfg,
                                        std::span<const std::byte> mn,
                                        uint64_t entry_count) noexcept
    {
        bool has_legacy_printim = false;
        bool has_modern_tag     = false;

        for (uint64_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = 8U + i * 12ULL;
            ClassicIfdEntry e;
            if (!read_classic_ifd_entry(cfg, mn, eoff, &e)) {
                return false;
            }
            if (e.tag == 0x0E00U) {
                has_legacy_printim = true;
            }
            if (e.tag >= 0x2000U) {
                has_modern_tag = true;
            }
        }

        return has_legacy_printim && !has_modern_tag;
    }

}  // namespace

bool
decode_casio_makernote(const TiffConfig& parent_cfg,
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
    if (mn.size() < 8) {
        return false;
    }
    // Casio Type2 MakerNotes may start with:
    // - "QVC\0" (Casio)
    // - "DCI\0" (Concord cameras using Casio Type2)
    if (!match_bytes(mn, 0, "QVC\0", 4) && !match_bytes(mn, 0, "DCI\0", 4)) {
        return false;
    }

    const uint64_t entries_off = 8;

    // Casio "type2" MakerNote is a QVC directory. Real-world files use two
    // observed variants:
    // - big-endian: u32be entry_count at +4
    // - little-endian: u16le version at +4, u16le entry_count at +6
    //
    // Select the variant by plausibility (table fits in declared MakerNote
    // bytes), since some models (e.g. EX-FR10) mislead the u32be read.
    bool le = false;

    uint32_t entry_count_u32be = 0;
    bool be_candidate          = read_u32be(mn, 4, &entry_count_u32be);
    bool be_ok                 = false;
    if (be_candidate && entry_count_u32be != 0U) {
        const uint64_t n     = entry_count_u32be;
        const uint64_t bytes = entries_off + n * 12ULL;
        be_ok = (n <= options.limits.max_entries_per_ifd && bytes <= mn.size());
    }

    uint16_t entry_count_u16le = 0;
    bool le_ok                 = false;
    if (!be_ok) {
        uint16_t ver = 0;
        if (read_u16le(mn, 4, &ver) && read_u16le(mn, 6, &entry_count_u16le)
            && entry_count_u16le != 0U) {
            const uint64_t n     = entry_count_u16le;
            const uint64_t bytes = entries_off + n * 12ULL;
            le_ok                = (n <= options.limits.max_entries_per_ifd
                     && bytes <= mn.size());
        }
    }

    uint64_t entry_count = 0;
    if (be_ok) {
        le          = false;
        entry_count = entry_count_u32be;
    } else if (le_ok) {
        le          = true;
        entry_count = entry_count_u16le;
    } else {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::Malformed);
        }
        return true;  // Signature matched; don't attempt generic fallbacks.
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return true;
    }

    TiffConfig mn_cfg;
    mn_cfg.le      = le;
    mn_cfg.bigtiff = false;

    const bool legacy_main_compat
        = casio_type2_uses_legacy_main_compat(mn_cfg, mn, entry_count);

    for (uint64_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + i * 12ULL;

        ClassicIfdEntry e;
        if (!read_classic_ifd_entry(mn_cfg, mn, eoff, &e)) {
            return true;
        }

        uint64_t value_bytes = 0;
        if (!classic_ifd_entry_value_bytes(e, &value_bytes)) {
            continue;
        }

        const uint64_t inline_cap      = 4;
        const uint64_t value_field_off = eoff + 8;
        const bool inline_value        = (value_bytes <= inline_cap);
        const uint64_t value_off       = inline_value ? value_field_off
                                                      : uint64_t(e.value_or_off32);

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return true;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, e.tag);
        entry.origin.block = block;
        entry.origin.order_in_block = static_cast<uint32_t>(i);
        entry.origin.wire_type      = WireType { WireFamily::Tiff, e.type };
        entry.origin.wire_count     = e.count32;
        if (legacy_main_compat && (e.tag <= 0x0019U || e.tag == 0x0E00U)) {
            entry.flags |= EntryFlags::ContextualName;
            entry.origin.name_context_kind
                = EntryNameContextKind::CasioType2Legacy;
            entry.origin.name_context_variant = 1U;
        }

        if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            entry.flags |= EntryFlags::Truncated;
        } else if (inline_value) {
            if (value_off + value_bytes > mn.size()) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                entry.flags |= EntryFlags::Unreadable;
            } else {
                entry.value = decode_tiff_value(mn_cfg, mn, e.type,
                                                uint64_t(e.count32), value_off,
                                                value_bytes, store.arena(),
                                                options.limits, status_out);
            }
        } else {
            // QVC directories use TIFF-relative offsets for out-of-line values.
            // Decode against the outer EXIF/TIFF byte span (bounds checked).
            if (value_off + value_bytes > tiff_bytes.size()) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                entry.flags |= EntryFlags::Unreadable;
            } else {
                entry.value = decode_tiff_value(parent_cfg, tiff_bytes, e.type,
                                                uint64_t(e.count32), value_off,
                                                value_bytes, store.arena(),
                                                options.limits, status_out);
            }
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }

    decode_casio_binary_subdirs(mk_ifd0, store, options, status_out);

    return true;
}


bool
decode_casio_makernote_from_source(SourceTiffReader* source,
                                   const TiffConfig& parent_cfg,
                                   uint64_t maker_note_off,
                                   std::span<const std::byte> maker_note,
                                   std::string_view mk_ifd0, MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
{
    if (!source || !source->result || mk_ifd0.empty()
        || !source_tiff_contains(*source, maker_note_off, maker_note.size())) {
        return false;
    }

    if (maker_note.size() < 8U
        || (!match_bytes(maker_note, 0U, "QVC\0", 4U)
            && !match_bytes(maker_note, 0U, "DCI\0", 4U))) {
        // Some late Casio files carry a textual firmware diagnostic block in
        // MakerNote. It has no TIFF entries and is intentionally kept opaque.
        if (maker_note.size() >= 3U && match_bytes(maker_note, 0U, "RC:", 3U)) {
            return true;
        }
        // Older Casio notes are ordinary MakerNote-relative IFDs, so their
        // complete declared payload is already sufficient for decoding.
        ClassicIfdCandidate best;
        if (!find_best_classic_ifd_candidate(maker_note, 256U, options.limits,
                                             &best)) {
            return false;
        }
        TiffConfig cfg;
        cfg.le      = best.le;
        cfg.bigtiff = false;
        decode_classic_ifd_no_header(cfg, maker_note, best.offset, mk_ifd0,
                                     store, options, status_out,
                                     EntryFlags::None);
        decode_casio_binary_subdirs(mk_ifd0, store, options, status_out);
        return true;
    }

    bool le              = false;
    uint64_t entry_count = 0U;
    uint32_t count_be    = 0U;
    if (read_u32be(maker_note, 4U, &count_be) && count_be != 0U
        && count_be <= options.limits.max_entries_per_ifd
        && uint64_t(count_be) <= (maker_note.size() - 8U) / 12U) {
        entry_count = count_be;
    } else {
        uint16_t version  = 0U;
        uint16_t count_le = 0U;
        if (!read_u16le(maker_note, 4U, &version)
            || !read_u16le(maker_note, 6U, &count_le) || count_le == 0U
            || count_le > options.limits.max_entries_per_ifd
            || uint64_t(count_le) > (maker_note.size() - 8U) / 12U) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return true;
        }
        le          = true;
        entry_count = count_le;
    }

    TiffConfig cfg = parent_cfg;
    cfg.le         = le;
    cfg.bigtiff    = false;
    const bool legacy_main_compat
        = casio_type2_uses_legacy_main_compat(cfg, maker_note, entry_count);
    OffsetPolicy offsets;
    if (!decode_classic_ifd_from_source(source, cfg, maker_note_off + 6U,
                                        offsets, mk_ifd0, store, options,
                                        status_out, EntryFlags::None, true,
                                        static_cast<uint16_t>(entry_count),
                                        &parent_cfg, legacy_main_compat,
                                        true)) {
        return false;
    }
    decode_casio_binary_subdirs(mk_ifd0, store, options, status_out);
    return true;
}


static MetaValue
casio_qvci_datetime(ByteArena& arena, std::span<const std::byte> raw) noexcept
{
    char buf[32];
    size_t n = 0;
    while (n < raw.size() && n + 1U < sizeof(buf)
           && raw[n] != std::byte { 0 }) {
        char c = static_cast<char>(u8(raw[n]));
        if (c == '.') {
            c = ':';
        }
        buf[n] = c;
        n += 1;
    }
    if (n >= 11U && buf[10] == ':') {
        buf[10] = ' ';
    }
    return make_text(arena, std::string_view(buf, n), TextEncoding::Ascii);
}

static void
casio_qvci_add_entry(uint16_t tag, MetaValue value, std::string_view ifd_name,
                     BlockId block, uint32_t* order_io, MetaStore& store,
                     const ExifDecodeLimits& limits,
                     ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || block == kInvalidBlockId || !order_io) {
        return;
    }
    if (status_out
        && (status_out->entries_decoded + 1U) > limits.max_total_entries) {
        update_status(status_out, ExifDecodeStatus::LimitExceeded);
        return;
    }

    Entry e;
    e.key                   = make_exif_tag_key(store.arena(), ifd_name, tag);
    e.origin.block          = block;
    e.origin.order_in_block = (*order_io)++;
    e.origin.wire_type      = WireType { WireFamily::Other, 0 };
    e.origin.wire_count     = value.count;
    e.value                 = value;
    (void)store.add_entry(e);
    if (status_out) {
        status_out->entries_decoded += 1;
    }
}

bool
decode_casio_qvci(std::span<const std::byte> qvci_bytes,
                  std::string_view mk_ifd0, MetaStore& store,
                  const ExifDecodeLimits& limits,
                  ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return false;
    }
    if (qvci_bytes.size() < 4U || !match_bytes(qvci_bytes, 0, "QVCI", 4)) {
        return false;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return true;
    }

    uint32_t order = 0;

    if (qvci_bytes.size() > 0x002c) {
        casio_qvci_add_entry(0x002c, make_u8(u8(qvci_bytes[0x002c])), mk_ifd0,
                             block, &order, store, limits, status_out);
    }
    if (qvci_bytes.size() > 0x0037) {
        casio_qvci_add_entry(0x0037, make_u8(u8(qvci_bytes[0x0037])), mk_ifd0,
                             block, &order, store, limits, status_out);
    }
    if (qvci_bytes.size() >= 0x004d + 20U) {
        casio_qvci_add_entry(
            0x004d,
            casio_qvci_datetime(store.arena(), qvci_bytes.subspan(0x004d, 20U)),
            mk_ifd0, block, &order, store, limits, status_out);
    }
    if (qvci_bytes.size() >= 0x0062 + 7U) {
        casio_qvci_add_entry(0x0062,
                             make_fixed_ascii_text(store.arena(),
                                                   qvci_bytes.subspan(0x0062,
                                                                      7U)),
                             mk_ifd0, block, &order, store, limits, status_out);
    }
    if (qvci_bytes.size() >= 0x0072 + 9U) {
        casio_qvci_add_entry(0x0072,
                             make_fixed_ascii_text(store.arena(),
                                                   qvci_bytes.subspan(0x0072,
                                                                      9U)),
                             mk_ifd0, block, &order, store, limits, status_out);
    }
    if (qvci_bytes.size() >= 0x007c + 9U) {
        casio_qvci_add_entry(0x007c,
                             make_fixed_ascii_text(store.arena(),
                                                   qvci_bytes.subspan(0x007c,
                                                                      9U)),
                             mk_ifd0, block, &order, store, limits, status_out);
    }

    return true;
}

}  // namespace openmeta::exif_internal
