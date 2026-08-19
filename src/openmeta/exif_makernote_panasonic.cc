// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>

namespace openmeta::exif_internal {

namespace {

    template<size_t N>
    static std::span<const std::byte>
    panasonic_copy_subspan(std::array<std::byte, N>* storage,
                           std::span<const std::byte> raw, uint64_t offset,
                           size_t count) noexcept
    {
        if (!storage || count > N || offset > raw.size()
            || static_cast<uint64_t>(count) > (raw.size() - offset)) {
            return {};
        }
        std::memcpy(storage->data(), raw.data() + static_cast<size_t>(offset),
                    count);
        return std::span<const std::byte>(storage->data(), count);
    }

    static bool read_u32_endian(bool le, std::span<const std::byte> bytes,
                                uint64_t offset, uint32_t* out) noexcept
    {
        return le ? read_u32le(bytes, offset, out)
                  : read_u32be(bytes, offset, out);
    }

    static bool is_printable_ascii(uint8_t c) noexcept
    {
        return c >= 0x20U && c <= 0x7EU;
    }

    static bool is_printable_ascii_text(std::span<const std::byte> bytes,
                                        uint32_t max_check) noexcept
    {
        const uint32_t n = (bytes.size() < max_check)
                               ? static_cast<uint32_t>(bytes.size())
                               : max_check;
        if (n == 0) {
            return false;
        }
        for (uint32_t i = 0; i < n; ++i) {
            if (!is_printable_ascii(u8(bytes[i]))) {
                return false;
            }
        }
        return true;
    }

    static MetaValue
    panasonic_timeinfo_datetime(ByteArena& arena,
                                std::span<const std::byte> raw) noexcept
    {
        if (raw.size() < 8U) {
            return MetaValue {};
        }
        if (raw[0] == std::byte { 0 }) {
            return MetaValue {};
        }

        // ExifTool formats as: YYYY:MM:DD HH:MM:SS.xx (BCD nibbles).
        char buf[22];
        size_t n = 0;

        for (uint32_t i = 0; i < 8; ++i) {
            const uint8_t b  = u8(raw[i]);
            const uint8_t hi = static_cast<uint8_t>((b >> 4) & 0x0FU);
            const uint8_t lo = static_cast<uint8_t>((b >> 0) & 0x0FU);
            if (hi > 9U || lo > 9U) {
                return make_bytes(arena, raw.subspan(0, 8));
            }
            const char c0 = static_cast<char>('0' + hi);
            const char c1 = static_cast<char>('0' + lo);
            if (n + 2U > sizeof(buf)) {
                return make_bytes(arena, raw.subspan(0, 8));
            }
            buf[n++] = c0;
            buf[n++] = c1;
        }
        if (n != 16U) {
            return make_bytes(arena, raw.subspan(0, 8));
        }

        char out[32];
        size_t out_n = 0;
        // YYYY
        out[out_n++] = buf[0];
        out[out_n++] = buf[1];
        out[out_n++] = buf[2];
        out[out_n++] = buf[3];
        out[out_n++] = ':';
        // MM
        out[out_n++] = buf[4];
        out[out_n++] = buf[5];
        out[out_n++] = ':';
        // DD
        out[out_n++] = buf[6];
        out[out_n++] = buf[7];
        out[out_n++] = ' ';
        // HH
        out[out_n++] = buf[8];
        out[out_n++] = buf[9];
        out[out_n++] = ':';
        // MM
        out[out_n++] = buf[10];
        out[out_n++] = buf[11];
        out[out_n++] = ':';
        // SS
        out[out_n++] = buf[12];
        out[out_n++] = buf[13];
        out[out_n++] = '.';
        // xx
        out[out_n++] = buf[14];
        out[out_n++] = buf[15];

        return make_text(arena, std::string_view(out, out_n),
                         TextEncoding::Ascii);
    }

    static void decode_panasonic_facedetinfo(
        std::string_view ifd_name, std::span<const std::byte> raw, bool le,
        MetaStore& store, const ExifDecodeLimits& limits,
        ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || raw.size() < 2U) {
            return;
        }

        // Values emitted below append to the same arena that commonly owns
        // raw. Keep every byte this table can inspect on the stack.
        std::array<std::byte, 42> stable_storage {};
        raw = panasonic_copy_subspan(&stable_storage, raw, 0,
                                     (raw.size() < stable_storage.size())
                                         ? raw.size()
                                         : stable_storage.size());

        uint16_t faces = 0;
        if (!read_u16_endian(le, raw, 0, &faces)) {
            return;
        }

        uint16_t tags_out[8];
        MetaValue vals_out[8];
        uint32_t out_count = 0;

        tags_out[out_count] = 0x0000;
        vals_out[out_count] = make_u16(faces);
        out_count += 1;

        static constexpr uint16_t kFaceTags[5] = { 0x0001, 0x0005, 0x0009,
                                                   0x000d, 0x0011 };
        const uint32_t face_n = (faces < 5U) ? static_cast<uint32_t>(faces)
                                             : 5U;

        for (uint32_t i = 0; i < face_n && out_count < 8U; ++i) {
            const uint16_t tag      = kFaceTags[i];
            const uint64_t byte_off = uint64_t(tag) * 2ULL;
            if (byte_off + 8U > raw.size()) {
                continue;
            }

            uint16_t pos[4];
            bool ok = true;
            for (uint32_t j = 0; j < 4; ++j) {
                if (!read_u16_endian(le, raw, byte_off + uint64_t(j) * 2ULL,
                                     &pos[j])) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                continue;
            }

            tags_out[out_count] = tag;
            vals_out[out_count]
                = make_u16_array(store.arena(),
                                 std::span<const uint16_t>(pos, 4));
            out_count += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             limits, status_out);
    }

    static void decode_panasonic_facerecinfo(
        std::string_view ifd_name, std::span<const std::byte> raw, bool le,
        MetaStore& store, const ExifDecodeLimits& limits,
        ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || raw.size() < 2U) {
            return;
        }

        std::array<std::byte, 148> stable_storage {};
        raw = panasonic_copy_subspan(&stable_storage, raw, 0,
                                     (raw.size() < stable_storage.size())
                                         ? raw.size()
                                         : stable_storage.size());

        uint16_t faces = 0;
        if (!read_u16_endian(le, raw, 0, &faces)) {
            return;
        }

        uint16_t tags_out[32];
        MetaValue vals_out[32];
        uint32_t out_count = 0;

        tags_out[out_count] = 0x0000;
        vals_out[out_count] = make_u16(faces);
        out_count += 1;

        const uint32_t face_n = (faces < 3U) ? static_cast<uint32_t>(faces)
                                             : 3U;
        for (uint32_t i = 0; i < face_n && out_count + 3U < 32U; ++i) {
            const uint64_t name_off = 4ULL + uint64_t(i) * 48ULL;
            const uint64_t pos_off  = 24ULL + uint64_t(i) * 48ULL;
            const uint64_t age_off  = 32ULL + uint64_t(i) * 48ULL;

            if (name_off + 20U <= raw.size() && 20U <= limits.max_value_bytes) {
                std::array<std::byte, 20> name_bytes;
                const std::span<const std::byte> name
                    = panasonic_copy_subspan(&name_bytes, raw, name_off, 20);
                if (name.empty()) {
                    continue;
                }
                tags_out[out_count] = static_cast<uint16_t>(name_off);
                vals_out[out_count] = make_fixed_ascii_text(store.arena(),
                                                            name);
                out_count += 1;
            }

            if (pos_off + 8U <= raw.size()) {
                uint16_t pos[4];
                bool ok = true;
                for (uint32_t j = 0; j < 4; ++j) {
                    if (!read_u16_endian(le, raw, pos_off + uint64_t(j) * 2ULL,
                                         &pos[j])) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    tags_out[out_count] = static_cast<uint16_t>(pos_off);
                    vals_out[out_count]
                        = make_u16_array(store.arena(),
                                         std::span<const uint16_t>(pos, 4));
                    out_count += 1;
                }
            }

            if (age_off + 20U <= raw.size() && 20U <= limits.max_value_bytes) {
                std::array<std::byte, 20> age_bytes;
                const std::span<const std::byte> age
                    = panasonic_copy_subspan(&age_bytes, raw, age_off, 20);
                if (age.empty()) {
                    continue;
                }
                tags_out[out_count] = static_cast<uint16_t>(age_off);
                vals_out[out_count] = make_fixed_ascii_text(store.arena(), age);
                out_count += 1;
            }
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             limits, status_out);
    }

    static void decode_panasonic_timeinfo(std::string_view ifd_name,
                                          std::span<const std::byte> raw,
                                          bool le, MetaStore& store,
                                          const ExifDecodeLimits& limits,
                                          ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || raw.empty()) {
            return;
        }

        std::array<std::byte, 20> stable_storage {};
        raw = panasonic_copy_subspan(&stable_storage, raw, 0,
                                     (raw.size() < stable_storage.size())
                                         ? raw.size()
                                         : stable_storage.size());

        uint16_t tags_out[4];
        MetaValue vals_out[4];
        uint32_t out_count = 0;

        MetaValue dt = panasonic_timeinfo_datetime(store.arena(), raw);
        if (dt.kind != MetaValueKind::Empty) {
            tags_out[out_count] = 0x0000;
            vals_out[out_count] = dt;
            out_count += 1;
        }

        if (raw.size() >= 20U) {
            uint32_t shot = 0;
            if (read_u32_endian(le, raw, 16, &shot)) {
                tags_out[out_count] = 0x0010;
                vals_out[out_count] = make_u32(shot);
                out_count += 1;
            }
        }

        if (out_count == 0) {
            return;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             limits, status_out);
    }

    static bool decode_panasonic_type2(std::span<const std::byte> mn_decl,
                                       std::string_view mk_prefix, bool le,
                                       MetaStore& store,
                                       const ExifDecodeLimits& limits,
                                       ExifDecodeResult* status_out) noexcept
    {
        if (mn_decl.size() < 4U) {
            return false;
        }

        std::array<std::byte, 8> stable_storage {};
        mn_decl = panasonic_copy_subspan(&stable_storage, mn_decl, 0,
                                         (mn_decl.size() < stable_storage.size())
                                             ? mn_decl.size()
                                             : stable_storage.size());

        // Type2 is a small fixed-layout blob. Be conservative: require the
        // 4-byte type string to be printable ASCII.
        const std::span<const std::byte> type = mn_decl.subspan(0, 4);
        if (!is_printable_ascii_text(type, 4)) {
            return false;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "type2", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return false;
        }

        uint16_t tags_out[2];
        MetaValue vals_out[2];
        uint32_t out_count = 0;

        tags_out[out_count] = 0x0000;
        vals_out[out_count] = make_fixed_ascii_text(store.arena(), type);
        out_count += 1;

        uint16_t gain           = 0;
        const uint64_t gain_off = 3ULL * 2ULL;
        if (gain_off + 2U <= mn_decl.size()
            && read_u16_endian(le, mn_decl, gain_off, &gain)) {
            tags_out[out_count] = 0x0003;
            vals_out[out_count] = make_u16(gain);
            out_count += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             limits, status_out);
        return true;
    }

    static void decode_panasonic_binary_subdirs(
        std::string_view mk_ifd0, bool le, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return;
        }

        const size_t source_entry_count = store.entries().size();

        uint32_t idx_facedet = 0;
        uint32_t idx_facerec = 0;
        uint32_t idx_time    = 0;

        char sub_ifd_buf[96];
        const std::string_view mk_prefix = "mk_panasonic";

        for (size_t i = 0; i < source_entry_count; ++i) {
            uint16_t tag = 0;
            ByteSpan raw_span;
            {
                const Entry& e = store.entries()[i];
                if (e.key.kind != MetaKeyKind::ExifTag
                    || arena_string(store.arena(), e.key.data.exif_tag.ifd)
                           != mk_ifd0
                    || (e.value.kind != MetaValueKind::Bytes
                        && e.value.kind != MetaValueKind::Array)) {
                    continue;
                }
                tag      = e.key.data.exif_tag.tag;
                raw_span = e.value.data.span;
            }

            const std::span<const std::byte> raw = store.arena().span(raw_span);
            if (raw.empty()) {
                continue;
            }

            if (tag == 0x004e) {  // FaceDetInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "facedetinfo",
                                                 idx_facedet++,
                                                 std::span<char>(sub_ifd_buf));
                decode_panasonic_facedetinfo(ifd_name, raw, le, store,
                                             options.limits, status_out);
                continue;
            }

            if (tag == 0x0061) {  // FaceRecInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "facerecinfo",
                                                 idx_facerec++,
                                                 std::span<char>(sub_ifd_buf));
                decode_panasonic_facerecinfo(ifd_name, raw, le, store,
                                             options.limits, status_out);
                continue;
            }

            if (tag == 0x2003) {  // TimeInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "timeinfo",
                                                 idx_time++,
                                                 std::span<char>(sub_ifd_buf));
                decode_panasonic_timeinfo(ifd_name, raw, le, store,
                                          options.limits, status_out);
                continue;
            }
        }
    }


    static bool score_panasonic_source_candidate(
        const SourceTiffReader& source, const TiffConfig& cfg,
        std::span<const std::byte> maker_note, uint64_t ifd_off,
        const ExifDecodeLimits& limits, ClassicIfdCandidate* out) noexcept
    {
        uint16_t entry_count = 0U;
        if (!read_tiff_u16(cfg, maker_note, ifd_off, &entry_count)
            || entry_count == 0U || entry_count > limits.max_entries_per_ifd
            || entry_count > 512U || ifd_off > UINT64_MAX - 2ULL) {
            return false;
        }

        const uint64_t entries_off = ifd_off + 2ULL;
        const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
        if (entries_off > maker_note.size()
            || table_bytes > maker_note.size() - entries_off) {
            return false;
        }

        uint32_t valid = 0U;
        for (uint32_t i = 0U; i < entry_count; ++i) {
            const uint64_t entry_off = entries_off + uint64_t(i) * 12ULL;
            ClassicIfdEntry entry;
            uint64_t value_bytes = 0U;
            if (!read_classic_ifd_entry(cfg, maker_note, entry_off, &entry)
                || !classic_ifd_entry_value_bytes(entry, &value_bytes)
                || value_bytes > limits.max_value_bytes) {
                continue;
            }

            if (value_bytes <= 4U) {
                if (entry_off <= maker_note.size()
                    && maker_note.size() - entry_off >= 8U
                    && value_bytes <= maker_note.size() - entry_off - 8U) {
                    valid += 1U;
                }
                continue;
            }
            if (source_tiff_contains(source, entry.value_or_off32,
                                     value_bytes)) {
                valid += 1U;
            }
        }

        const uint32_t min_valid = (entry_count > 4U)
                                       ? uint32_t(entry_count) / 2U
                                       : uint32_t(entry_count);
        if (valid < min_valid) {
            return false;
        }
        if (out) {
            out->offset        = ifd_off;
            out->le            = cfg.le;
            out->entry_count   = entry_count;
            out->valid_entries = valid;
        }
        return true;
    }


    static bool find_panasonic_source_candidate(
        const SourceTiffReader& source, std::span<const std::byte> maker_note,
        const ExifDecodeLimits& limits, ClassicIfdCandidate* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out       = ClassicIfdCandidate {};
        bool found = false;

        uint64_t preferred_offset = 0U;
        if (maker_note.size() >= 12U
            && match_bytes(maker_note, 0U, "Panasonic", 9U)) {
            preferred_offset = 12U;
        }
        for (uint32_t endian = 0U; endian < 2U; ++endian) {
            TiffConfig cfg;
            cfg.le      = endian == 0U;
            cfg.bigtiff = false;
            ClassicIfdCandidate candidate;
            if (!score_panasonic_source_candidate(source, cfg, maker_note,
                                                  preferred_offset, limits,
                                                  &candidate)) {
                continue;
            }
            if (!found || candidate.valid_entries > out->valid_entries) {
                *out  = candidate;
                found = true;
            }
        }
        if (found) {
            return true;
        }

        const uint64_t scan_bytes = (maker_note.size() < 512U)
                                        ? maker_note.size()
                                        : 512U;
        for (uint64_t offset = 0U; offset + 2U <= scan_bytes; offset += 2U) {
            for (uint32_t endian = 0U; endian < 2U; ++endian) {
                TiffConfig cfg;
                cfg.le      = endian == 0U;
                cfg.bigtiff = false;
                ClassicIfdCandidate candidate;
                if (!score_panasonic_source_candidate(source, cfg, maker_note,
                                                      offset, limits,
                                                      &candidate)) {
                    continue;
                }
                if (!found || candidate.valid_entries > out->valid_entries
                    || (candidate.valid_entries == out->valid_entries
                        && candidate.offset < out->offset)) {
                    *out  = candidate;
                    found = true;
                }
            }
        }
        return found;
    }

}  // namespace

bool
decode_panasonic_makernote(const TiffConfig& parent_cfg,
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

    const std::span<const std::byte> mn_decl
        = tiff_bytes.subspan(static_cast<size_t>(maker_note_off),
                             static_cast<size_t>(maker_note_bytes));

    ClassicIfdCandidate best;
    bool found = false;

    const uint64_t scan_bytes = (maker_note_bytes < 512U) ? maker_note_bytes
                                                          : 512U;
    const uint64_t scan_end   = maker_note_off + scan_bytes;
    const uint64_t mn_end     = maker_note_off + maker_note_bytes;

    for (uint64_t abs_off = maker_note_off; abs_off + 2 <= scan_end;
         abs_off += 2) {
        for (int endian = 0; endian < 2; ++endian) {
            TiffConfig cfg;
            cfg.le      = (endian == 0);
            cfg.bigtiff = false;

            ClassicIfdCandidate cand;
            if (!score_classic_ifd_candidate(cfg, tiff_bytes, abs_off,
                                             options.limits, &cand)) {
                continue;
            }

            // Some real-world Panasonic MakerNotes report a byte count that
            // truncates the trailing next-IFD pointer (4 bytes). Allow the
            // entry table itself to fit even if the final pointer doesn't.
            const uint64_t needed = 2U + (uint64_t(cand.entry_count) * 12ULL);
            if (abs_off + needed > mn_end) {
                continue;
            }

            if (!found || cand.valid_entries > best.valid_entries
                || (cand.valid_entries == best.valid_entries
                    && cand.offset < best.offset)) {
                best  = cand;
                found = true;
            }
        }
    }

    if (!found) {
        // Panasonic Type2: fixed-layout binary MakerNote.
        if (decode_panasonic_type2(mn_decl, "mk_panasonic", parent_cfg.le,
                                   store, options.limits, status_out)) {
            return true;
        }
        return false;
    }

    TiffConfig best_cfg;
    best_cfg.le      = best.le;
    best_cfg.bigtiff = false;

    decode_classic_ifd_no_header(best_cfg, tiff_bytes, best.offset, mk_ifd0,
                                 store, options, status_out, EntryFlags::None);
    decode_panasonic_binary_subdirs(mk_ifd0, best_cfg.le, store, options,
                                    status_out);
    return true;
}


bool
decode_panasonic_makernote_from_source(
    SourceTiffReader* source, const TiffConfig& parent_cfg,
    uint64_t maker_note_off, std::span<const std::byte> maker_note,
    std::string_view mk_ifd0, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    if (!source || mk_ifd0.empty() || maker_note.empty()) {
        return false;
    }

    ClassicIfdCandidate best;
    if (!find_panasonic_source_candidate(*source, maker_note, options.limits,
                                         &best)) {
        return decode_panasonic_makernote(parent_cfg, maker_note, 0U,
                                          maker_note.size(), mk_ifd0, store,
                                          options, status_out);
    }
    if (maker_note_off > UINT64_MAX - best.offset) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return false;
    }

    TiffConfig best_cfg;
    best_cfg.le      = best.le;
    best_cfg.bigtiff = false;
    const OffsetPolicy offsets;
    if (!decode_classic_ifd_from_source(source, best_cfg,
                                        maker_note_off + best.offset, offsets,
                                        mk_ifd0, store, options, status_out,
                                        EntryFlags::None, true)) {
        return false;
    }
    decode_panasonic_binary_subdirs(mk_ifd0, best_cfg.le, store, options,
                                    status_out);
    return true;
}

}  // namespace openmeta::exif_internal
