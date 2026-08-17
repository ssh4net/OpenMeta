// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>
#include <memory>
#include <new>

namespace openmeta::exif_internal {

bool
decode_sony_makernote(const TiffConfig& parent_cfg,
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

    // Some Sony MakerNotes start directly with a classic TIFF IFD at offset 0,
    // but use value offsets relative to the outer EXIF/TIFF stream.
    //
    // Example: ARW where the MakerNote begins with a u16 entry count (no "SONY"
    // ASCII marker) and out-of-line values use absolute offsets.
    if (looks_like_classic_ifd(parent_cfg, tiff_bytes, maker_note_off,
                               options.limits)) {
        uint16_t entry_count = 0;
        if (read_tiff_u16(parent_cfg, tiff_bytes, maker_note_off, &entry_count)
            && entry_count != 0
            && entry_count <= options.limits.max_entries_per_ifd) {
            const uint64_t ifd_table_bytes = 2U + uint64_t(entry_count) * 12ULL
                                             + 4ULL;
            const uint64_t mn_end = maker_note_off + maker_note_bytes;
            if (maker_note_off + ifd_table_bytes <= mn_end) {
                // Heuristic: require at least one out-of-line value offset to
                // land after the IFD table when interpreted as an absolute
                // offset into the outer TIFF stream.
                bool has_abs_offsets       = false;
                const uint64_t entries_off = maker_note_off + 2;
                for (uint32_t i = 0; i < entry_count; ++i) {
                    const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;
                    ClassicIfdEntry e;
                    if (!read_classic_ifd_entry(parent_cfg, tiff_bytes, eoff,
                                                &e)) {
                        break;
                    }

                    uint64_t value_bytes = 0;
                    if (!classic_ifd_entry_value_bytes(e, &value_bytes)) {
                        continue;
                    }

                    if (value_bytes <= 4U) {
                        continue;
                    }
                    const uint64_t abs_off = static_cast<uint64_t>(
                        e.value_or_off32);
                    if (abs_off >= maker_note_off + ifd_table_bytes
                        && abs_off + value_bytes <= tiff_bytes.size()) {
                        has_abs_offsets = true;
                        break;
                    }
                }

                if (has_abs_offsets) {
                    decode_classic_ifd_no_header(parent_cfg, tiff_bytes,
                                                 maker_note_off, mk_ifd0, store,
                                                 options, status_out,
                                                 EntryFlags::None);
                    return true;
                }
            }
        }
    }

    // Sony MakerNotes can also embed classic IFDs after a "SONY" prefix.
    // These typically use offsets relative to the outer EXIF/TIFF stream.
    //
    // Hasselblad-branded Sony cameras use a "VHAB     \0" prefix but still
    // store a classic IFD at offset +12, with value offsets commonly
    // relative to the outer EXIF/TIFF stream.
    if (match_bytes(mn, 0, "VHAB", 4)) {
        ClassicIfdCandidate best;
        bool found = false;

        const uint64_t ifd_off = maker_note_off + 12;
        for (int endian = 0; endian < 2; ++endian) {
            TiffConfig cfg;
            cfg.le      = (endian == 0);
            cfg.bigtiff = false;

            ClassicIfdCandidate cand;
            if (!score_classic_ifd_candidate(cfg, tiff_bytes, ifd_off,
                                             options.limits, &cand)) {
                continue;
            }

            const uint64_t table_bytes
                = 2U + (uint64_t(cand.entry_count) * 12ULL) + 4ULL;
            const uint64_t mn_end = maker_note_off + maker_note_bytes;
            if (ifd_off + table_bytes > mn_end
                || ifd_off + table_bytes > tiff_bytes.size()) {
                continue;
            }

            if (!found || cand.valid_entries > best.valid_entries) {
                best  = cand;
                found = true;
            }
        }

        if (!found) {
            return false;
        }

        TiffConfig best_cfg;
        best_cfg.le      = best.le;
        best_cfg.bigtiff = false;

        decode_classic_ifd_no_header(best_cfg, tiff_bytes, ifd_off, mk_ifd0,
                                     store, options, status_out,
                                     EntryFlags::None);
        return true;
    }

    if (!match_bytes(mn, 0, "SONY", 4)) {
        return false;
    }

    ClassicIfdCandidate best;
    bool found = false;

    const uint64_t scan_bytes = (maker_note_bytes < 256U) ? maker_note_bytes
                                                          : 256U;
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

            const uint64_t table_bytes
                = 2U + (uint64_t(cand.entry_count) * 12ULL) + 4ULL;
            if (abs_off + table_bytes > mn_end) {
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
        return false;
    }

    TiffConfig best_cfg;
    best_cfg.le      = best.le;
    best_cfg.bigtiff = false;

    decode_classic_ifd_no_header(best_cfg, tiff_bytes, best.offset, mk_ifd0,
                                 store, options, status_out, EntryFlags::None);
    return true;
}

constexpr uint32_t
sony_mod_mul_249(uint32_t a, uint32_t b) noexcept
{
    return (a * b) % 249U;
}


constexpr uint8_t
sony_mod_pow_249(uint8_t base, uint8_t exp) noexcept
{
    uint32_t result = 1;
    uint32_t cur    = base;
    uint8_t e       = exp;
    while (e != 0) {
        if ((e & 1U) != 0U) {
            result = sony_mod_mul_249(result, cur);
        }
        cur = sony_mod_mul_249(cur, cur);
        e   = static_cast<uint8_t>(e >> 1U);
    }
    return static_cast<uint8_t>(result);
}


constexpr std::array<uint8_t, 249>
make_sony_decipher_lut() noexcept
{
    std::array<uint8_t, 249> out {};
    for (uint32_t i = 0; i < out.size(); ++i) {
        // ExifTool's Sony substitution cipher uses:
        //   encipher: c = (b^3) % 249 for b in [0..248]
        // so decipher is the modular cube root: b = (c^55) % 249
        // because 3*55 == 1 (mod phi(249)=164).
        out[i] = sony_mod_pow_249(static_cast<uint8_t>(i), 55);
    }
    return out;
}


static constexpr std::array<uint8_t, 249> kSonyDecipherLut
    = make_sony_decipher_lut();


static uint8_t
sony_decipher_once(uint8_t b) noexcept
{
    if (b >= 249U) {
        return b;
    }
    return kSonyDecipherLut[b];
}


static uint8_t
sony_decipher(uint8_t b, uint32_t rounds) noexcept
{
    uint8_t out = b;
    for (uint32_t i = 0; i < rounds; ++i) {
        out = sony_decipher_once(out);
    }
    return out;
}


static bool
sony_read_u8(std::span<const std::byte> bytes, uint64_t off, uint32_t rounds,
             uint8_t* out) noexcept
{
    if (!out || off >= bytes.size()) {
        return false;
    }
    *out = sony_decipher(u8(bytes[off]), rounds);
    return true;
}


static bool
sony_read_u16le(std::span<const std::byte> bytes, uint64_t off, uint32_t rounds,
                uint16_t* out) noexcept
{
    if (!out || off + 2U > bytes.size()) {
        return false;
    }
    const uint8_t b0 = sony_decipher(u8(bytes[off + 0]), rounds);
    const uint8_t b1 = sony_decipher(u8(bytes[off + 1]), rounds);
    *out = static_cast<uint16_t>(uint16_t(b0) | (uint16_t(b1) << 8));
    return true;
}


static bool
sony_read_i16le(std::span<const std::byte> bytes, uint64_t off, uint32_t rounds,
                int16_t* out) noexcept
{
    uint16_t raw = 0;
    if (!sony_read_u16le(bytes, off, rounds, &raw)) {
        return false;
    }
    if (out) {
        *out = static_cast<int16_t>(raw);
    }
    return out != nullptr;
}


static bool
sony_read_u32le(std::span<const std::byte> bytes, uint64_t off, uint32_t rounds,
                uint32_t* out) noexcept
{
    if (!out || off + 4U > bytes.size()) {
        return false;
    }
    const uint8_t b0 = sony_decipher(u8(bytes[off + 0]), rounds);
    const uint8_t b1 = sony_decipher(u8(bytes[off + 1]), rounds);
    const uint8_t b2 = sony_decipher(u8(bytes[off + 2]), rounds);
    const uint8_t b3 = sony_decipher(u8(bytes[off + 3]), rounds);
    *out             = uint32_t(b0) | (uint32_t(b1) << 8) | (uint32_t(b2) << 16)
           | (uint32_t(b3) << 24);
    return true;
}

static MetaValue
make_sony_deciphered_bytes(ByteArena& arena, std::span<const std::byte> bytes,
                           uint64_t off, uint32_t size,
                           uint32_t rounds) noexcept
{
    MetaValue v;
    if (size == 0) {
        return v;
    }
    if (off > bytes.size() || uint64_t(size) > (bytes.size() - off)) {
        return v;
    }

    const ByteSpan span            = arena.allocate(size, 1);
    const std::span<std::byte> out = arena.span_mut(span);
    if (out.size() != size) {
        return v;
    }
    for (uint32_t i = 0; i < size; ++i) {
        out[i] = std::byte { sony_decipher(u8(bytes[off + i]), rounds) };
    }

    v.kind          = MetaValueKind::Bytes;
    v.elem_type     = MetaElementType::U8;
    v.count         = size;
    v.data.span     = span;
    v.text_encoding = TextEncoding::Unknown;
    return v;
}


static uint32_t
sony_guess_cipher_rounds(std::span<const std::byte> bytes, uint64_t off,
                         std::span<const uint8_t> allowed) noexcept
{
    uint8_t b = 0;
    if (!sony_read_u8(bytes, off, 1, &b)) {
        return 1;
    }
    for (size_t i = 0; i < allowed.size(); ++i) {
        if (b == allowed[i]) {
            return 1;
        }
    }

    if (!sony_read_u8(bytes, off, 2, &b)) {
        return 1;
    }
    for (size_t i = 0; i < allowed.size(); ++i) {
        if (b == allowed[i]) {
            return 2;
        }
    }
    return 1;
}


enum class SonyCipherFieldKind : uint8_t {
    U8,
    U16LE,
    U32LE,
    I16LE,
    U8_ARRAY,
    U16LE_ARRAY,
    I16LE_ARRAY,
    BYTES,
};

struct SonyCipherField final {
    uint16_t tag             = 0;
    SonyCipherFieldKind kind = SonyCipherFieldKind::U8;
    uint16_t count           = 0;
};

static void
sony_decode_cipher_fields(std::span<const std::byte> bytes,
                          std::string_view mk_prefix, std::string_view subtable,
                          uint32_t rounds,
                          std::span<const SonyCipherField> fields,
                          MetaStore& store, const ExifDecodeOptions& options,
                          ExifDecodeResult* status_out) noexcept
{
    if (bytes.empty() || mk_prefix.empty() || subtable.empty()) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, subtable, 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    uint16_t tags_out[64];
    MetaValue vals_out[64];
    uint32_t out_count = 0;

    static constexpr uint32_t kMaxArrayElems   = 64;
    static constexpr uint32_t kMaxU8ArrayBytes = 64;

    for (size_t i = 0; i < fields.size(); ++i) {
        if (out_count >= (sizeof(tags_out) / sizeof(tags_out[0]))) {
            break;
        }

        const SonyCipherField f = fields[i];
        if (f.tag == 0) {
            continue;
        }

        switch (f.kind) {
        case SonyCipherFieldKind::U8: {
            uint8_t v = 0;
            if (!sony_read_u8(bytes, f.tag, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = f.tag;
            vals_out[out_count] = make_u8(v);
            out_count += 1;
            break;
        }
        case SonyCipherFieldKind::U16LE: {
            uint16_t v = 0;
            if (!sony_read_u16le(bytes, f.tag, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = f.tag;
            vals_out[out_count] = make_u16(v);
            out_count += 1;
            break;
        }
        case SonyCipherFieldKind::U32LE: {
            uint32_t v = 0;
            if (!sony_read_u32le(bytes, f.tag, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = f.tag;
            vals_out[out_count] = make_u32(v);
            out_count += 1;
            break;
        }
        case SonyCipherFieldKind::I16LE: {
            int16_t v = 0;
            if (!sony_read_i16le(bytes, f.tag, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = f.tag;
            vals_out[out_count] = make_i16(v);
            out_count += 1;
            break;
        }
        case SonyCipherFieldKind::U8_ARRAY: {
            const uint32_t count = f.count;
            if (count == 0U || count > kMaxU8ArrayBytes) {
                continue;
            }
            if (uint64_t(f.tag) + uint64_t(count) > bytes.size()) {
                continue;
            }
            if (count > options.limits.max_value_bytes) {
                continue;
            }

            uint8_t tmp[kMaxU8ArrayBytes];
            bool ok = true;
            for (uint32_t j = 0; j < count; ++j) {
                ok = ok
                     && sony_read_u8(bytes, uint64_t(f.tag) + uint64_t(j),
                                     rounds, &tmp[j]);
            }
            if (!ok) {
                continue;
            }
            tags_out[out_count] = f.tag;
            vals_out[out_count]
                = make_u8_array(store.arena(),
                                std::span<const uint8_t>(tmp, count));
            out_count += 1;
            break;
        }
        case SonyCipherFieldKind::U16LE_ARRAY: {
            const uint32_t count = f.count;
            if (count == 0U || count > kMaxArrayElems) {
                continue;
            }
            const uint64_t size_bytes = uint64_t(count) * 2U;
            if (uint64_t(f.tag) + size_bytes > bytes.size()) {
                continue;
            }
            if (size_bytes > options.limits.max_value_bytes) {
                continue;
            }

            uint16_t tmp[kMaxArrayElems];
            bool ok = true;
            for (uint32_t j = 0; j < count; ++j) {
                ok = ok
                     && sony_read_u16le(bytes,
                                        uint64_t(f.tag) + uint64_t(j) * 2U,
                                        rounds, &tmp[j]);
            }
            if (!ok) {
                continue;
            }
            tags_out[out_count] = f.tag;
            vals_out[out_count]
                = make_u16_array(store.arena(),
                                 std::span<const uint16_t>(tmp, count));
            out_count += 1;
            break;
        }
        case SonyCipherFieldKind::I16LE_ARRAY: {
            const uint32_t count = f.count;
            if (count == 0U || count > kMaxArrayElems) {
                continue;
            }
            const uint64_t size_bytes = uint64_t(count) * 2U;
            if (uint64_t(f.tag) + size_bytes > bytes.size()) {
                continue;
            }
            if (size_bytes > options.limits.max_value_bytes) {
                continue;
            }

            int16_t tmp[kMaxArrayElems];
            bool ok = true;
            for (uint32_t j = 0; j < count; ++j) {
                ok = ok
                     && sony_read_i16le(bytes,
                                        uint64_t(f.tag) + uint64_t(j) * 2U,
                                        rounds, &tmp[j]);
            }
            if (!ok) {
                continue;
            }
            tags_out[out_count] = f.tag;
            vals_out[out_count]
                = make_i16_array(store.arena(),
                                 std::span<const int16_t>(tmp, count));
            out_count += 1;
            break;
        }
        case SonyCipherFieldKind::BYTES: {
            const uint32_t size = f.count;
            if (size == 0U) {
                continue;
            }
            if (uint64_t(f.tag) + uint64_t(size) > bytes.size()) {
                continue;
            }
            if (size > options.limits.max_value_bytes) {
                continue;
            }
            const MetaValue v = make_sony_deciphered_bytes(store.arena(), bytes,
                                                           f.tag, size, rounds);
            if (v.kind != MetaValueKind::Bytes) {
                continue;
            }
            tags_out[out_count] = f.tag;
            vals_out[out_count] = v;
            out_count += 1;
            break;
        }
        }
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags_out, out_count),
                         std::span<const MetaValue>(vals_out, out_count),
                         options.limits, status_out);
}

static constexpr SonyCipherField kSonyTag9402Fields[]
    = { { 0x0002, SonyCipherFieldKind::U8 },
        { 0x0004, SonyCipherFieldKind::U8 },
        { 0x0016, SonyCipherFieldKind::U8 },
        { 0x0017, SonyCipherFieldKind::U8 },
        { 0x002D, SonyCipherFieldKind::U8 } };

static constexpr SonyCipherField kSonyTag9403Fields[]
    = { { 0x0004, SonyCipherFieldKind::U8 },
        { 0x0005, SonyCipherFieldKind::U8 },
        { 0x0019, SonyCipherFieldKind::U16LE } };

static constexpr SonyCipherField kSonyTag9400aFields[] = {
    { 0x0008, SonyCipherFieldKind::U32LE },  // SequenceImageNumber
    { 0x000C, SonyCipherFieldKind::U32LE },  // SequenceFileNumber
    { 0x0010, SonyCipherFieldKind::U8 },     // ReleaseMode2
    { 0x0012, SonyCipherFieldKind::U8 },     // DigitalZoom
    { 0x001A, SonyCipherFieldKind::U32LE },  // ShotNumberSincePowerUp
    { 0x0022, SonyCipherFieldKind::U8 },     // SequenceLength
    { 0x0028, SonyCipherFieldKind::U8 },     // CameraOrientation
    { 0x0029, SonyCipherFieldKind::U8 },     // Quality2
    { 0x0044, SonyCipherFieldKind::U16LE },  // SonyImageHeight
    { 0x0052, SonyCipherFieldKind::U8 },     // ModelReleaseYear
};

static constexpr SonyCipherField kSonyTag9400bFields[] = {
    { 0x0008, SonyCipherFieldKind::U32LE },  // SequenceImageNumber
    { 0x000C, SonyCipherFieldKind::U32LE },  // SequenceFileNumber
    { 0x0010, SonyCipherFieldKind::U8 },     // ReleaseMode2
    { 0x0012, SonyCipherFieldKind::U8 },     // DigitalZoom
    { 0x0016, SonyCipherFieldKind::U32LE },  // ShotNumberSincePowerUp
    { 0x001E, SonyCipherFieldKind::U8 },     // SequenceLength
    { 0x0024, SonyCipherFieldKind::U8 },     // CameraOrientation
    { 0x0025, SonyCipherFieldKind::U8 },     // Quality2
    { 0x003F, SonyCipherFieldKind::U16LE },  // SonyImageHeight
    { 0x0046, SonyCipherFieldKind::U8 },     // ModelReleaseYear
};

static constexpr SonyCipherField kSonyTag9400cFields[] = {
    { 0x0009, SonyCipherFieldKind::U8 },     // ReleaseMode2
    { 0x000A, SonyCipherFieldKind::U32LE },  // ShotNumberSincePowerUp
    { 0x0012, SonyCipherFieldKind::U32LE },  // SequenceImageNumber
    { 0x0016, SonyCipherFieldKind::U8 },     // SequenceLength
    { 0x001A, SonyCipherFieldKind::U32LE },  // SequenceFileNumber
    { 0x001E, SonyCipherFieldKind::U8 },     // SequenceLength
    { 0x0029, SonyCipherFieldKind::U8 },     // CameraOrientation
    { 0x002A, SonyCipherFieldKind::U8 },     // Quality2
    { 0x0053, SonyCipherFieldKind::U16LE },  // ModelReleaseYear
};

static constexpr SonyCipherField kSonyTag9406Fields[]
    = { { 0x0005, SonyCipherFieldKind::U8 },
        { 0x0006, SonyCipherFieldKind::U8 },
        { 0x0007, SonyCipherFieldKind::U8 },
        { 0x0008, SonyCipherFieldKind::U8 } };

static bool
sony_tag9406_0005_prefers_battery_level_name(std::string_view model) noexcept
{
    return model == "ILCE-6700" || model == "ILCE-7CR" || model == "ILCE-7M5"
           || model == "ILCE-9M3" || model == "ZV-E10M2";
}

static constexpr SonyCipherField kSonyTag940cFields[]
    = { { 0x0008, SonyCipherFieldKind::U8 },
        { 0x0009, SonyCipherFieldKind::U16LE },
        { 0x000B, SonyCipherFieldKind::U16LE },
        { 0x000D, SonyCipherFieldKind::U16LE },
        { 0x0014, SonyCipherFieldKind::U16LE } };

static constexpr SonyCipherField kSonyTag9404cFields[]
    = { { 0x000B, SonyCipherFieldKind::U8 },
        { 0x000D, SonyCipherFieldKind::U8 } };

static constexpr SonyCipherField kSonyTag9404aFields[]
    = { { 0x000B, SonyCipherFieldKind::U8 },
        { 0x000D, SonyCipherFieldKind::U8 },
        { 0x0019, SonyCipherFieldKind::U16LE } };

static constexpr SonyCipherField kSonyTag9404bFields[]
    = { { 0x000C, SonyCipherFieldKind::U8 },
        { 0x000E, SonyCipherFieldKind::U8 },
        { 0x001E, SonyCipherFieldKind::U16LE } };

static constexpr SonyCipherField kSonyTag202aFields[]
    = { { 0x0001, SonyCipherFieldKind::U8 } };

static constexpr SonyCipherField kSonyTag2010aFields[] = {
    { 0x1128, SonyCipherFieldKind::U8 },
    { 0x112C, SonyCipherFieldKind::U8 },
    { 0x1134, SonyCipherFieldKind::U8 },
    { 0x1138, SonyCipherFieldKind::U8 },
    { 0x113E, SonyCipherFieldKind::U16LE },
    { 0x1140, SonyCipherFieldKind::U16LE },
    { 0x1144, SonyCipherFieldKind::U8 },
    { 0x1148, SonyCipherFieldKind::U8 },
    { 0x114C, SonyCipherFieldKind::I16LE },
    { 0x115E, SonyCipherFieldKind::U8 },
    { 0x115F, SonyCipherFieldKind::U8 },
    { 0x1163, SonyCipherFieldKind::U8 },
    { 0x1170, SonyCipherFieldKind::U8 },
    { 0x1174, SonyCipherFieldKind::U8 },
    { 0x1175, SonyCipherFieldKind::U8 },
    { 0x117C, SonyCipherFieldKind::U16LE_ARRAY, 3 },
};

static constexpr SonyCipherField kSonyTag9405aFields[]
    = { { 0x0600, SonyCipherFieldKind::U8 },
        { 0x0601, SonyCipherFieldKind::U8 },
        { 0x0603, SonyCipherFieldKind::U8 },
        { 0x0604, SonyCipherFieldKind::U8 },
        { 0x0605, SonyCipherFieldKind::U16LE },
        { 0x0608, SonyCipherFieldKind::U16LE },
        { 0x064A, SonyCipherFieldKind::I16LE_ARRAY, 16 },
        { 0x066A, SonyCipherFieldKind::I16LE_ARRAY, 32 },
        { 0x06CA, SonyCipherFieldKind::I16LE_ARRAY, 16 } };

static constexpr SonyCipherField kSonyTag2010bFields[] = {
    { 0x0000, SonyCipherFieldKind::U32LE },
    { 0x0004, SonyCipherFieldKind::U32LE },
    { 0x0008, SonyCipherFieldKind::U32LE },
    { 0x01B6, SonyCipherFieldKind::BYTES, 7 },
    { 0x0324, SonyCipherFieldKind::U8 },
    { 0x1128, SonyCipherFieldKind::U8 },
    { 0x112C, SonyCipherFieldKind::U8 },
    { 0x1134, SonyCipherFieldKind::U8 },
    { 0x1138, SonyCipherFieldKind::U8 },
    { 0x113E, SonyCipherFieldKind::U16LE },
    { 0x1140, SonyCipherFieldKind::U16LE },
    { 0x1144, SonyCipherFieldKind::U8 },
    { 0x1148, SonyCipherFieldKind::U8 },
    { 0x114C, SonyCipherFieldKind::I16LE },
    { 0x1162, SonyCipherFieldKind::U8 },
    { 0x1163, SonyCipherFieldKind::U8 },
    { 0x1167, SonyCipherFieldKind::U8 },
    { 0x1174, SonyCipherFieldKind::U8 },
    { 0x1178, SonyCipherFieldKind::U8 },
    { 0x1179, SonyCipherFieldKind::U8 },
    { 0x1180, SonyCipherFieldKind::U16LE_ARRAY, 3 },
    { 0x1218, SonyCipherFieldKind::U16LE },
    { 0x1A23, SonyCipherFieldKind::I16LE_ARRAY, 16 },
};

static constexpr SonyCipherField kSonyTag2010eFields[] = {
    { 0x0000, SonyCipherFieldKind::U32LE },
    { 0x0004, SonyCipherFieldKind::U32LE },
    { 0x0008, SonyCipherFieldKind::U32LE },
    { 0x021C, SonyCipherFieldKind::U8 },
    { 0x022C, SonyCipherFieldKind::BYTES, 7 },
    { 0x0328, SonyCipherFieldKind::U8 },
    { 0x115C, SonyCipherFieldKind::U8 },
    { 0x1160, SonyCipherFieldKind::U8 },
    { 0x1168, SonyCipherFieldKind::U8 },
    { 0x116C, SonyCipherFieldKind::U8 },
    { 0x1172, SonyCipherFieldKind::U16LE },
    { 0x1174, SonyCipherFieldKind::U16LE },
    { 0x1178, SonyCipherFieldKind::U8 },
    { 0x117C, SonyCipherFieldKind::U8 },
    { 0x1180, SonyCipherFieldKind::I16LE },
    { 0x1196, SonyCipherFieldKind::U8 },
    { 0x1197, SonyCipherFieldKind::U8 },
    { 0x119B, SonyCipherFieldKind::U8 },
    { 0x11A8, SonyCipherFieldKind::U8 },
    { 0x11AC, SonyCipherFieldKind::U8 },
    { 0x11AD, SonyCipherFieldKind::U8 },
    { 0x11B4, SonyCipherFieldKind::U16LE_ARRAY, 3 },
    { 0x1254, SonyCipherFieldKind::U16LE },            // SonyISO
    { 0x1870, SonyCipherFieldKind::I16LE_ARRAY, 16 },  // DistortionCorrParams
    { 0x1891, SonyCipherFieldKind::U8 },               // LensFormat
    { 0x1892, SonyCipherFieldKind::U8 },               // LensMount
    { 0x1893, SonyCipherFieldKind::U16LE },            // LensType2
    { 0x1896, SonyCipherFieldKind::U16LE },            // LensType
    { 0x1898, SonyCipherFieldKind::U8 },  // DistortionCorrParamsPresent
    { 0x1899, SonyCipherFieldKind::U8 },  // DistortionCorrParamsNumber
    { 0x192C, SonyCipherFieldKind::U8 },  // AspectRatio (most)
    { 0x1A88, SonyCipherFieldKind::U8 },  // AspectRatio (RX100/Stellar)
};

static constexpr SonyCipherField kSonyTag2010cFields[] = {
    { 0x0000, SonyCipherFieldKind::U32LE },
    { 0x0004, SonyCipherFieldKind::U32LE },
    { 0x0008, SonyCipherFieldKind::U32LE },
    { 0x0200, SonyCipherFieldKind::U16LE },
    { 0x0210, SonyCipherFieldKind::BYTES, 7 },
    { 0x0300, SonyCipherFieldKind::U8 },
    { 0x1104, SonyCipherFieldKind::U8 },
    { 0x1108, SonyCipherFieldKind::U8 },
    { 0x1110, SonyCipherFieldKind::U8 },
    { 0x1114, SonyCipherFieldKind::U8 },
    { 0x111A, SonyCipherFieldKind::U16LE },
    { 0x111C, SonyCipherFieldKind::U16LE },
    { 0x1120, SonyCipherFieldKind::U8 },
    { 0x1124, SonyCipherFieldKind::U8 },
    { 0x1128, SonyCipherFieldKind::I16LE },
    { 0x113E, SonyCipherFieldKind::U8 },
    { 0x113F, SonyCipherFieldKind::U8 },
    { 0x1143, SonyCipherFieldKind::U8 },
    { 0x1150, SonyCipherFieldKind::U8 },
    { 0x1154, SonyCipherFieldKind::U8 },
    { 0x1155, SonyCipherFieldKind::U8 },
    { 0x115C, SonyCipherFieldKind::U16LE_ARRAY, 3 },
    { 0x11F4, SonyCipherFieldKind::U16LE },
};

static constexpr SonyCipherField kSonyTag2010fFields[] = {
    { 0x0004, SonyCipherFieldKind::U32LE },
    { 0x0050, SonyCipherFieldKind::U8 },
    { 0x1014, SonyCipherFieldKind::U8 },
    { 0x1018, SonyCipherFieldKind::U8 },
    { 0x1020, SonyCipherFieldKind::U8 },
    { 0x1024, SonyCipherFieldKind::U8 },
    { 0x102A, SonyCipherFieldKind::U16LE },
    { 0x102C, SonyCipherFieldKind::U16LE },
    { 0x1030, SonyCipherFieldKind::U8 },
    { 0x1034, SonyCipherFieldKind::U8 },
    { 0x1038, SonyCipherFieldKind::I16LE },
    { 0x104E, SonyCipherFieldKind::U8 },
    { 0x104F, SonyCipherFieldKind::U8 },
    { 0x1053, SonyCipherFieldKind::U8 },
    { 0x1060, SonyCipherFieldKind::U8 },
    { 0x1064, SonyCipherFieldKind::U8 },
    { 0x1065, SonyCipherFieldKind::U8 },
    { 0x106C, SonyCipherFieldKind::U16LE_ARRAY, 3 },
    { 0x1134, SonyCipherFieldKind::U16LE },
    { 0x1136, SonyCipherFieldKind::U16LE },
    { 0x1138, SonyCipherFieldKind::U16LE },
    { 0x113C, SonyCipherFieldKind::U16LE },
    { 0x192C, SonyCipherFieldKind::U8 },
};

static constexpr SonyCipherField kSonyTag2010gFields[] = {
    { 0x0004, SonyCipherFieldKind::U32LE },
    { 0x0050, SonyCipherFieldKind::U8 },
    { 0x020C, SonyCipherFieldKind::U8 },
    { 0x0210, SonyCipherFieldKind::U8 },
    { 0x0218, SonyCipherFieldKind::U8 },
    { 0x021C, SonyCipherFieldKind::U8 },
    { 0x0222, SonyCipherFieldKind::U16LE },
    { 0x0224, SonyCipherFieldKind::U16LE },
    { 0x0228, SonyCipherFieldKind::U8 },
    { 0x022C, SonyCipherFieldKind::U8 },
    { 0x0230, SonyCipherFieldKind::I16LE },
    { 0x0246, SonyCipherFieldKind::U8 },
    { 0x0247, SonyCipherFieldKind::U8 },
    { 0x024B, SonyCipherFieldKind::U8 },
    { 0x0258, SonyCipherFieldKind::U8 },
    { 0x025C, SonyCipherFieldKind::U8 },
    { 0x025D, SonyCipherFieldKind::U8 },
    { 0x0264, SonyCipherFieldKind::U16LE_ARRAY, 3 },
    { 0x032C, SonyCipherFieldKind::U16LE },
    { 0x032E, SonyCipherFieldKind::U16LE },
    { 0x0330, SonyCipherFieldKind::U16LE },
    { 0x0344, SonyCipherFieldKind::U16LE },
    { 0x189C, SonyCipherFieldKind::I16LE_ARRAY, 16 },
    { 0x18BD, SonyCipherFieldKind::U8 },
    { 0x18BE, SonyCipherFieldKind::U8 },
    { 0x18BF, SonyCipherFieldKind::U16LE },
    { 0x18C2, SonyCipherFieldKind::U16LE },
    { 0x18C4, SonyCipherFieldKind::U8 },
    { 0x18C5, SonyCipherFieldKind::U8 },
    { 0x1958, SonyCipherFieldKind::U8 },
};

static constexpr SonyCipherField kSonyTag2010hFields[] = {
    { 0x0004, SonyCipherFieldKind::U32LE },
    { 0x0050, SonyCipherFieldKind::U8 },
    { 0x020C, SonyCipherFieldKind::U8 },
    { 0x0210, SonyCipherFieldKind::U8 },
    { 0x0218, SonyCipherFieldKind::U8 },
    { 0x021C, SonyCipherFieldKind::U8 },
    { 0x0222, SonyCipherFieldKind::U16LE },
    { 0x0224, SonyCipherFieldKind::U16LE },
    { 0x0228, SonyCipherFieldKind::U8 },
    { 0x022C, SonyCipherFieldKind::U8 },
    { 0x0230, SonyCipherFieldKind::I16LE },
    { 0x0246, SonyCipherFieldKind::U8 },
    { 0x0247, SonyCipherFieldKind::U8 },
    { 0x024B, SonyCipherFieldKind::U8 },
    { 0x0258, SonyCipherFieldKind::U8 },
    { 0x025C, SonyCipherFieldKind::U8 },
    { 0x025D, SonyCipherFieldKind::U8 },
    { 0x0264, SonyCipherFieldKind::U16LE_ARRAY, 3 },
    { 0x032C, SonyCipherFieldKind::U16LE },
    { 0x032E, SonyCipherFieldKind::U16LE },
    { 0x0330, SonyCipherFieldKind::U16LE },
    { 0x0346, SonyCipherFieldKind::U16LE },
    { 0x18CC, SonyCipherFieldKind::I16LE_ARRAY, 16 },
    { 0x18ED, SonyCipherFieldKind::U8 },
    { 0x18EE, SonyCipherFieldKind::U8 },
    { 0x18EF, SonyCipherFieldKind::U16LE },
    { 0x18F2, SonyCipherFieldKind::U16LE },
    { 0x18F4, SonyCipherFieldKind::U8 },
    { 0x18F5, SonyCipherFieldKind::U8 },
    { 0x192C, SonyCipherFieldKind::U8 },
};

static constexpr SonyCipherField kSonyTag2010iFields[] = {
    // u8 scalars.
    { 0x0004, SonyCipherFieldKind::U8 },
    { 0x004E, SonyCipherFieldKind::U8 },
    { 0x0204, SonyCipherFieldKind::U8 },
    { 0x0208, SonyCipherFieldKind::U8 },
    { 0x0210, SonyCipherFieldKind::U8 },
    { 0x0211, SonyCipherFieldKind::U8 },
    { 0x021B, SonyCipherFieldKind::U8 },
    { 0x021F, SonyCipherFieldKind::U8 },
    { 0x0237, SonyCipherFieldKind::U8 },
    { 0x0238, SonyCipherFieldKind::U8 },
    { 0x023C, SonyCipherFieldKind::U8 },
    { 0x0247, SonyCipherFieldKind::U8 },
    { 0x024B, SonyCipherFieldKind::U8 },
    { 0x024C, SonyCipherFieldKind::U8 },
    { 0x17F1, SonyCipherFieldKind::U8 },
    { 0x17F2, SonyCipherFieldKind::U8 },
    { 0x17F8, SonyCipherFieldKind::U8 },
    { 0x17F9, SonyCipherFieldKind::U8 },
    { 0x188C, SonyCipherFieldKind::U8 },

    // Fixed-point-ish fields (best-effort, stored as i16).
    { 0x0217, SonyCipherFieldKind::I16LE },
    { 0x0219, SonyCipherFieldKind::I16LE },
    { 0x0223, SonyCipherFieldKind::I16LE },

    // WB_RGBLevels u16[3].
    { 0x0252, SonyCipherFieldKind::U16LE_ARRAY, 3 },

    // Focal lengths + ISO.
    { 0x030A, SonyCipherFieldKind::U16LE },
    { 0x030C, SonyCipherFieldKind::U16LE },
    { 0x030E, SonyCipherFieldKind::U16LE },
    { 0x0320, SonyCipherFieldKind::U16LE },

    // LensType2/LensType.
    { 0x17F3, SonyCipherFieldKind::U16LE },
    { 0x17F6, SonyCipherFieldKind::U16LE },

    // DistortionCorrParams (prefix bytes).
    { 0x17D0, SonyCipherFieldKind::BYTES, 32 },
};

enum class SonyTag2010Variant : uint8_t {
    A,
    B,
    C,
    E,
    F,
    G,
    H,
    I,
};

static bool
sony_model_matches_any(std::string_view model,
                       std::span<const std::string_view> models) noexcept
{
    for (size_t i = 0; i < models.size(); ++i) {
        if (model == models[i]) {
            return true;
        }
    }
    return false;
}

static bool
sony_model_has_prefix_any(std::string_view model,
                          std::span<const std::string_view> prefixes) noexcept
{
    for (size_t i = 0; i < prefixes.size(); ++i) {
        if (model.starts_with(prefixes[i])) {
            return true;
        }
    }
    return false;
}

static std::string_view
sony_trim_model(std::string_view model) noexcept
{
    while (!model.empty()) {
        const char c = model.back();
        if (c != '\0' && c != ' ') {
            break;
        }
        model.remove_suffix(1);
    }
    return model;
}

enum class SonyTag9400Variant : uint8_t {
    A,
    B,
    C,
};

static SonyTag2010Variant
select_sony_tag2010_variant(std::string_view model) noexcept
{
    static constexpr std::string_view kTag2010bModels[]
        = { "SLT-A65", "SLT-A65V",  "SLT-A77", "SLT-A77V",
            "NEX-7",   "NEX-VG20E", "Lunar" };
    static constexpr std::string_view kTag2010cModels[]
        = { "SLT-A37", "SLT-A57", "NEX-F3" };
    static constexpr std::string_view kTag2010eModels[]
        = { "SLT-A99",   "SLT-A99V",  "HV",        "SLT-A58",
            "ILCE-3000", "ILCE-3500", "NEX-3N",    "NEX-5R",
            "NEX-5T",    "NEX-6",     "NEX-VG900", "NEX-VG30E",
            "DSC-RX100", "DSC-RX1",   "DSC-RX1R",  "Stellar" };
    static constexpr std::string_view kTag2010fModels[]
        = { "DSC-RX100M2", "DSC-QX10", "DSC-QX100" };
    static constexpr std::string_view kTag2010gModels[]
        = { "DSC-QX30",   "DSC-RX10",  "DSC-RX100M3", "DSC-HX60V", "DSC-HX350",
            "DSC-HX400V", "DSC-WX220", "DSC-WX350",   "ILCE-7",    "ILCE-7R",
            "ILCE-7S",    "ILCE-7M2",  "ILCE-5000",   "ILCE-5100", "ILCE-6000",
            "ILCE-QX1",   "ILCA-68",   "ILCA-77M2" };
    static constexpr std::string_view kTag2010hModels[] = {
        "DSC-HX80",   "DSC-HX90",   "DSC-HX90V",   "DSC-RX0",     "DSC-RX1RM2",
        "DSC-RX10M2", "DSC-RX10M3", "DSC-RX100M4", "DSC-RX100M5", "DSC-WX500",
        "ILCE-6300",  "ILCE-6500",  "ILCE-7RM2",   "ILCE-7SM2",   "ILCA-99M2"
    };
    static constexpr std::string_view kTag2010iPrefixes[] = {
        "ILCE-6100",   "ILCE-6400",    "ILCE-6600",   "ILCE-7C",  "ILCE-7M3",
        "ILCE-7RM3",   "ILCE-7RM4",    "ILCE-9",      "ILCE-9M2", "DSC-RX10M4",
        "DSC-RX100M6", "DSC-RX100M5A", "DSC-RX100M7", "DSC-HX95", "DSC-HX99",
        "DSC-RX0M2",   "ZV-1",         "ZV-E10"
    };

    if (model == "NEX-5N") {
        return SonyTag2010Variant::A;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag2010bModels,
                                          sizeof(kTag2010bModels)
                                              / sizeof(kTag2010bModels[0])))) {
        return SonyTag2010Variant::B;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag2010cModels,
                                          sizeof(kTag2010cModels)
                                              / sizeof(kTag2010cModels[0])))) {
        return SonyTag2010Variant::C;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag2010eModels,
                                          sizeof(kTag2010eModels)
                                              / sizeof(kTag2010eModels[0])))) {
        return SonyTag2010Variant::E;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag2010fModels,
                                          sizeof(kTag2010fModels)
                                              / sizeof(kTag2010fModels[0])))) {
        return SonyTag2010Variant::F;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag2010gModels,
                                          sizeof(kTag2010gModels)
                                              / sizeof(kTag2010gModels[0])))) {
        return SonyTag2010Variant::G;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag2010hModels,
                                          sizeof(kTag2010hModels)
                                              / sizeof(kTag2010hModels[0])))) {
        return SonyTag2010Variant::H;
    }
    if (sony_model_has_prefix_any(model,
                                  std::span<const std::string_view>(
                                      kTag2010iPrefixes,
                                      sizeof(kTag2010iPrefixes)
                                          / sizeof(kTag2010iPrefixes[0])))) {
        return SonyTag2010Variant::I;
    }
    return SonyTag2010Variant::I;
}

static SonyTag9400Variant
select_sony_tag9400_variant(std::string_view model, uint8_t deciphered_v0,
                            bool is_legacy_slt_family, bool is_lunar,
                            bool is_stellar) noexcept
{
    static constexpr std::string_view kTag9400aModels[] = {
        "NEX-5R",    "NEX-5T",    "NEX-6",     "NEX-7",   "NEX-VG20E",
        "NEX-VG30E", "NEX-VG900", "DSC-RX100", "DSC-RX1", "DSC-RX1R",
    };
    static constexpr std::string_view kTag9400bModels[] = {
        "SLT-A58",   "ILCE-3000",   "ILCE-3500", "NEX-3N",   "DSC-WX60",
        "DSC-WX300", "DSC-RX100M2", "DSC-HX50V", "DSC-QX10", "DSC-QX100",
    };

    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag9400bModels,
                                          sizeof(kTag9400bModels)
                                              / sizeof(kTag9400bModels[0])))) {
        return SonyTag9400Variant::B;
    }
    if (is_lunar || is_stellar || is_legacy_slt_family
        || sony_model_matches_any(
            model, std::span<const std::string_view>(
                       kTag9400aModels,
                       sizeof(kTag9400aModels) / sizeof(kTag9400aModels[0])))) {
        return SonyTag9400Variant::A;
    }
    if (deciphered_v0 == 0x0C) {
        return SonyTag9400Variant::B;
    }
    return SonyTag9400Variant::C;
}

static bool
sony_model_uses_tag9405a(std::string_view model, bool is_legacy_slt_family,
                         bool is_lunar, bool is_stellar) noexcept
{
    static constexpr std::string_view kTag9405aDscModels[] = {
        "DSC-HX50V",   "DSC-HX300", "DSC-QX10",  "DSC-QX100", "DSC-RX100",
        "DSC-RX100M2", "DSC-RX1",   "DSC-RX1R",  "DSC-TX30",  "DSC-WX60",
        "DSC-WX80",    "DSC-WX200", "DSC-WX300",
    };

    if (is_legacy_slt_family || is_lunar || is_stellar
        || model.starts_with("NEX-") || model == "ILCE-3000"
        || model == "ILCE-3500") {
        return true;
    }
    return sony_model_matches_any(
        model,
        std::span<const std::string_view>(kTag9405aDscModels,
                                          sizeof(kTag9405aDscModels)
                                              / sizeof(kTag9405aDscModels[0])));
}

static uint16_t
sony_tag9405b_lens_zoom_tag(std::string_view model) noexcept
{
    static constexpr std::string_view kTag034eModels[] = {
        "DSC-RX100M5", "DSC-RX100M5A", "DSC-RX100M6", "DSC-RX100M7",
        "DSC-RX10M4",  "DSC-HX99",     "ILCE-6100",   "ILCE-6400",
        "ILCE-6600",   "ILCE-7C",      "ILCE-7M3",    "ILCE-9M2",
        "ZV-E10",
    };
    static constexpr std::string_view kTag034ePrefixes[] = {
        "ILCE-7RM3",
        "ILCE-7RM4",
    };
    static constexpr std::string_view kTag035aModels[] = {
        "ILCE-7RM2",  "ILCE-7SM2",  "DSC-HX80",    "DSC-HX90V",
        "DSC-RX10M2", "DSC-RX10M3", "DSC-RX100M4", "DSC-WX500",
    };

    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag035aModels,
                                          sizeof(kTag035aModels)
                                              / sizeof(kTag035aModels[0])))) {
        return 0x035AU;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag034eModels,
                                          sizeof(kTag034eModels)
                                              / sizeof(kTag034eModels[0])))
        || sony_model_has_prefix_any(model,
                                     std::span<const std::string_view>(
                                         kTag034ePrefixes,
                                         sizeof(kTag034ePrefixes)
                                             / sizeof(kTag034ePrefixes[0])))) {
        return 0x034EU;
    }
    return 0x0342U;
}

static uint16_t
sony_tag9405b_vignetting_tag(std::string_view model) noexcept
{
    static constexpr std::string_view kTag034aModels[] = {
        "ILCA-68", "ILCA-77M2", "ILCE-5000", "ILCE-5100", "ILCE-6000",
        "ILCE-7",  "ILCE-7R",   "ILCE-7S",   "ILCE-QX1",  "Lusso",
    };
    static constexpr std::string_view kTag035cModels[] = {
        "ILCA-99M2", "ILCE-6100", "ILCE-6400", "ILCE-6500", "ILCE-6600",
        "ILCE-7C",   "ILCE-7M3",  "ILCE-9",    "ILCE-9M2",  "ZV-E10",
    };
    static constexpr std::string_view kTag035cPrefixes[] = {
        "ILCE-7RM3",
        "ILCE-7RM4",
    };
    static constexpr std::string_view kTag0368Models[] = {
        "ILCE-6300",
        "ILCE-7RM2",
        "ILCE-7SM2",
    };

    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag034aModels,
                                          sizeof(kTag034aModels)
                                              / sizeof(kTag034aModels[0])))) {
        return 0x034AU;
    }
    if (model == "ILCE-7M2") {
        return 0x0350U;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag035cModels,
                                          sizeof(kTag035cModels)
                                              / sizeof(kTag035cModels[0])))
        || sony_model_has_prefix_any(model,
                                     std::span<const std::string_view>(
                                         kTag035cPrefixes,
                                         sizeof(kTag035cPrefixes)
                                             / sizeof(kTag035cPrefixes[0])))) {
        return 0x035CU;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag0368Models,
                                          sizeof(kTag0368Models)
                                              / sizeof(kTag0368Models[0])))) {
        return 0x0368U;
    }
    return 0U;
}

static uint16_t
sony_tag9405b_chromatic_tag(std::string_view model) noexcept
{
    static constexpr std::string_view kTag037cModels[] = {
        "ILCA-68", "ILCA-77M2", "ILCE-5000", "ILCE-5100", "ILCE-6000",
        "ILCE-7",  "ILCE-7R",   "ILCE-7S",   "ILCE-QX1",  "Lusso",
    };
    static constexpr std::string_view kTag039cModels[] = {
        "ILCE-6300",
        "ILCE-7RM2",
        "ILCE-7SM2",
    };
    static constexpr std::string_view kTag03b8Models[] = {
        "ILCE-6100", "ILCE-6400", "ILCE-6600", "ILCE-7C",
        "ILCE-7M3",  "ILCE-9",    "ILCE-9M2",  "ZV-E10",
    };
    static constexpr std::string_view kTag03b8Prefixes[] = {
        "ILCE-7RM3",
        "ILCE-7RM4",
    };

    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag037cModels,
                                          sizeof(kTag037cModels)
                                              / sizeof(kTag037cModels[0])))) {
        return 0x037CU;
    }
    if (model == "ILCE-7M2") {
        return 0x0384U;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag039cModels,
                                          sizeof(kTag039cModels)
                                              / sizeof(kTag039cModels[0])))) {
        return 0x039CU;
    }
    if (model == "ILCA-99M2" || model == "ILCE-6500") {
        return 0x03B0U;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag03b8Models,
                                          sizeof(kTag03b8Models)
                                              / sizeof(kTag03b8Models[0])))
        || sony_model_has_prefix_any(model,
                                     std::span<const std::string_view>(
                                         kTag03b8Prefixes,
                                         sizeof(kTag03b8Prefixes)
                                             / sizeof(kTag03b8Prefixes[0])))) {
        return 0x03B8U;
    }
    return 0U;
}

static uint16_t
sony_tag9416_vignetting_tag(std::string_view model) noexcept
{
    if (model == "ILCE-1" || model == "ILCE-7SM3"
        || model.starts_with("ILME-FX3")) {
        return 0x088FU;
    }
    if (model == "ILCE-7M4") {
        return 0x0891U;
    }
    if (model == "ILCE-7RM6") {
        return 0x0708U;
    }
    if (model == "ILCE-1M2" || model == "ILCE-6700" || model == "ILCE-7CM2"
        || model == "ILCE-7CR" || model == "ILCE-7RM5" || model == "ILME-FX2"
        || model == "ILME-FX30" || model == "ZV-E1" || model == "ZV-E10M2") {
        return 0x089DU;
    }
    return 0U;
}

static uint16_t
sony_tag9416_apsc_tag(std::string_view model) noexcept
{
    if (model == "ILCE-1" || model == "ILCE-7SM3"
        || model.starts_with("ILME-FX3")) {
        return 0x08B5U;
    }
    if (model == "ILCE-7M4") {
        return 0x08B7U;
    }
    if (model == "ILCE-7RM6") {
        return 0x074FU;
    }
    if (model == "ILCE-1M2" || model == "ILCE-7CM2" || model == "ILCE-7CR"
        || model == "ILCE-7RM5" || model == "ZV-E1") {
        return 0x08E5U;
    }
    return 0U;
}

static uint16_t
sony_tag9416_chromatic_tag(std::string_view model) noexcept
{
    if (model == "ILCE-1" || model == "ILCE-7SM3"
        || model.starts_with("ILME-FX3")) {
        return 0x0914U;
    }
    if (model == "ILCE-7M4") {
        return 0x0916U;
    }
    if (model == "ILCE-7RM6") {
        return 0x0843U;
    }
    if (model == "ILCE-1M2" || model == "ILCE-6700" || model == "ILCE-7CM2"
        || model == "ILCE-7CR" || model == "ILCE-7RM5" || model == "ILME-FX2"
        || model == "ILME-FX30" || model == "ZV-E1" || model == "ZV-E10M2") {
        return 0x0945U;
    }
    return 0U;
}

static uint16_t
sony_tag9400c_shutter_type_tag(std::string_view model) noexcept
{
    static constexpr std::string_view kTag0133Models[] = {
        "DSC-HX350",  "DSC-HX400V", "DSC-HX60V",   "DSC-HX80",
        "DSC-HX90",   "DSC-HX90V",  "DSC-QX30",    "DSC-RX10",
        "DSC-RX10M2", "DSC-RX10M3", "DSC-RX100M3", "DSC-RX100M4",
    };
    static constexpr std::string_view kTag0139Models[] = {
        "DSC-HX95",   "DSC-HX99",    "DSC-RX0",      "DSC-RX0M2",
        "DSC-RX10M4", "DSC-RX100M5", "DSC-RX100M5A", "DSC-RX100M6",
    };
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag0133Models,
                                          sizeof(kTag0133Models)
                                              / sizeof(kTag0133Models[0])))) {
        return 0x0133U;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag0139Models,
                                          sizeof(kTag0139Models)
                                              / sizeof(kTag0139Models[0])))) {
        return 0x0139U;
    }
    if (model == "DSC-RX100M7" || model == "DSC-RX100M7A" || model == "ZV-1"
        || model == "ZV-1A" || model == "ZV-1F" || model == "ZV-1M2") {
        return 0x013FU;
    }
    return 0U;
}

enum class SonyTag9050Variant : uint8_t {
    A,
    B,
    C,
    D,
};

enum class SonyTag9404Variant : uint8_t {
    A,
    B,
    C,
    Unknown,
};

static SonyTag9050Variant
select_sony_tag9050_variant(std::string_view model, bool is_slt_family,
                            bool is_lunar) noexcept
{
    static constexpr std::string_view kTag9050bModels[]
        = { "ILCE-6300", "ILCE-6500", "ILCA-99M2", "ILCE-6600",
            "ILCE-7C",   "ILCE-7M3",  "ILCE-7RM2", "ILCE-7SM2",
            "ILCE-9",    "ILCE-9M2",  "ZV-E10" };
    static constexpr std::string_view kTag9050bPrefixes[]
        = { "ILCE-6100", "ILCE-6400", "ILCE-7RM3", "ILCE-7RM4" };
    static constexpr std::string_view kTag9050cModels[] = {
        "ILCE-7M4",
        "ILCE-7RM5",
        "ILCE-7SM3",
    };
    static constexpr std::string_view kTag9050cPrefixes[] = {
        "ILCE-1",
        "ILME-FX3",
    };
    static constexpr std::string_view kTag9050dModels[] = {
        "ILCE-6700", "ILCE-7CM2", "ILCE-7CR", "ILCE-7RM6",
        "ILME-FX2",  "ZV-E1",     "ZV-E10M2",
    };

    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag9050dModels,
                                          sizeof(kTag9050dModels)
                                              / sizeof(kTag9050dModels[0])))) {
        return SonyTag9050Variant::D;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag9050cModels,
                                          sizeof(kTag9050cModels)
                                              / sizeof(kTag9050cModels[0])))
        || sony_model_has_prefix_any(model,
                                     std::span<const std::string_view>(
                                         kTag9050cPrefixes,
                                         sizeof(kTag9050cPrefixes)
                                             / sizeof(kTag9050cPrefixes[0])))) {
        return SonyTag9050Variant::C;
    }
    if (sony_model_matches_any(model, std::span<const std::string_view>(
                                          kTag9050bModels,
                                          sizeof(kTag9050bModels)
                                              / sizeof(kTag9050bModels[0])))
        || sony_model_has_prefix_any(model,
                                     std::span<const std::string_view>(
                                         kTag9050bPrefixes,
                                         sizeof(kTag9050bPrefixes)
                                             / sizeof(kTag9050bPrefixes[0])))) {
        return SonyTag9050Variant::B;
    }
    if (is_slt_family || is_lunar) {
        return SonyTag9050Variant::A;
    }
    if (!model.empty() && !model.starts_with("DSC-") && model != "Stellar") {
        return SonyTag9050Variant::A;
    }
    return SonyTag9050Variant::B;
}

static SonyTag9404Variant
select_sony_tag9404_variant(std::span<const std::byte> bytes) noexcept
{
    uint8_t first  = 0;
    uint8_t fourth = 0;
    if (!sony_read_u8(bytes, 0x0000, 1, &first)
        || !sony_read_u8(bytes, 0x0003, 1, &fourth)) {
        return SonyTag9404Variant::Unknown;
    }
    if ((first == 4u || first == 5u) && fourth == 1u) {
        return SonyTag9404Variant::A;
    }
    if ((first == 9u || first == 12u || first == 13u || first == 15u
         || first == 16u)
        && fourth == 2u) {
        return SonyTag9404Variant::B;
    }
    if (first == 17u && fourth == 1u) {
        return SonyTag9404Variant::C;
    }
    return SonyTag9404Variant::Unknown;
}

static uint16_t
sony_tag2010h_meter_offset(std::string_view model) noexcept
{
    if (model == "ILCA-99M2" || model == "ILCE-6500" || model == "DSC-RX0"
        || model == "DSC-RX100M5") {
        return 0x0398u;
    }
    return 0x0388u;
}

static constexpr SonyCipherField kSonyTag9050aFields[] = {
    { 0x0000, SonyCipherFieldKind::U8 },
    { 0x0001, SonyCipherFieldKind::U8 },
    { 0x0020, SonyCipherFieldKind::U16LE_ARRAY, 3 },
    { 0x0031, SonyCipherFieldKind::U8 },
    { 0x0032, SonyCipherFieldKind::U32LE },
    { 0x003A, SonyCipherFieldKind::U16LE },
    { 0x003C, SonyCipherFieldKind::U16LE },
    { 0x003F, SonyCipherFieldKind::U8 },
    { 0x004C, SonyCipherFieldKind::U32LE },
    { 0x0051, SonyCipherFieldKind::BYTES, 6 },
    { 0x0067, SonyCipherFieldKind::U8 },
    { 0x007C, SonyCipherFieldKind::U8_ARRAY, 4 },
    { 0x00F0, SonyCipherFieldKind::U8_ARRAY, 5 },
    { 0x0105, SonyCipherFieldKind::U8 },
    { 0x0106, SonyCipherFieldKind::U8 },
    { 0x0107, SonyCipherFieldKind::U16LE },
    { 0x0109, SonyCipherFieldKind::U16LE },
    { 0x010B, SonyCipherFieldKind::U8 },
    { 0x0114, SonyCipherFieldKind::U8 },
    { 0x0116, SonyCipherFieldKind::U8_ARRAY, 2 },
    { 0x01A0, SonyCipherFieldKind::U32LE },
    { 0x01AA, SonyCipherFieldKind::U32LE },
    { 0x01BD, SonyCipherFieldKind::U32LE },
};

static void
decode_sony_meterinfo_from_tag2010(std::span<const std::byte> bytes,
                                   uint32_t rounds, uint16_t meter_off,
                                   std::string_view mk_prefix, MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
{
    static constexpr uint32_t kMeterBytes = 486U * 4U;
    if (bytes.empty()) {
        return;
    }
    if (uint64_t(meter_off) + uint64_t(kMeterBytes) > bytes.size()) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "meterinfo", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    struct Row final {
        uint16_t tag = 0;
        uint16_t off = 0;
        uint16_t len = 0;
    };
    static constexpr Row kRows[] = {
        { 0x0000, 0x0000, 0x006C }, { 0x006C, 0x006C, 0x006C },
        { 0x00D8, 0x00D8, 0x006C }, { 0x0144, 0x0144, 0x006C },
        { 0x01B0, 0x01B0, 0x006C }, { 0x021C, 0x021C, 0x006C },
        { 0x0288, 0x0288, 0x006C },

        { 0x02F4, 0x02F4, 0x0084 }, { 0x0378, 0x0378, 0x0084 },
        { 0x03FC, 0x03FC, 0x0084 }, { 0x0480, 0x0480, 0x0084 },
        { 0x0504, 0x0504, 0x0084 }, { 0x0588, 0x0588, 0x0084 },
        { 0x060C, 0x060C, 0x0084 }, { 0x0690, 0x0690, 0x0084 },
        { 0x0714, 0x0714, 0x0084 },
    };

    uint16_t tags_out[32];
    MetaValue vals_out[32];
    uint32_t out_count = 0;

    for (uint32_t i = 0; i < sizeof(kRows) / sizeof(kRows[0]); ++i) {
        if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
            break;
        }
        const Row r = kRows[i];
        if (r.len == 0) {
            continue;
        }
        if (uint32_t(r.len) > options.limits.max_value_bytes) {
            continue;
        }
        const uint64_t abs_off64 = uint64_t(meter_off) + uint64_t(r.off);
        if (abs_off64 > static_cast<uint64_t>(UINT16_MAX)
            || abs_off64 + uint64_t(r.len) > bytes.size()) {
            continue;
        }
        const uint16_t abs_off = static_cast<uint16_t>(abs_off64);
        const MetaValue v = make_sony_deciphered_bytes(store.arena(), bytes,
                                                       abs_off, r.len, rounds);
        if (v.kind != MetaValueKind::Bytes) {
            continue;
        }
        tags_out[out_count] = r.tag;
        vals_out[out_count] = v;
        out_count += 1;
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags_out, out_count),
                         std::span<const MetaValue>(vals_out, out_count),
                         options.limits, status_out);
}

static void
decode_sony_afstatus_from_afinfo(std::span<const std::byte> bytes,
                                 uint32_t rounds, uint16_t base_off,
                                 uint32_t count, std::string_view mk_prefix,
                                 std::string_view subtable, MetaStore& store,
                                 const ExifDecodeOptions& options,
                                 ExifDecodeResult* status_out) noexcept
{
    if (bytes.empty() || count == 0) {
        return;
    }
    const uint64_t bytes_needed = uint64_t(count) * 2U;
    if (uint64_t(base_off) + bytes_needed > bytes.size()) {
        return;
    }
    if (bytes_needed > options.limits.max_value_bytes) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, subtable, 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    uint16_t tags_out[96];
    MetaValue vals_out[96];
    uint32_t out_count = 0;

    for (uint32_t i = 0; i < count; ++i) {
        if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
            break;
        }
        const uint16_t tag = uint16_t(i * 2U);
        int16_t v          = 0;
        if (!sony_read_i16le(bytes, uint64_t(base_off) + uint64_t(tag), rounds,
                             &v)) {
            continue;
        }
        tags_out[out_count] = tag;
        vals_out[out_count] = make_i16(v);
        out_count += 1;
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags_out, out_count),
                         std::span<const MetaValue>(vals_out, out_count),
                         options.limits, status_out);
}

static void
decode_sony_afinfo_from_tag940e(std::span<const std::byte> bytes,
                                std::string_view mk_prefix, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept
{
    if (bytes.empty()) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "afinfo", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    const uint8_t allowed_af_type[] = { 0, 1, 2, 3, 6, 9, 11 };
    const uint32_t rounds
        = sony_guess_cipher_rounds(bytes, 0x0002,
                                   std::span<const uint8_t>(allowed_af_type));

    uint8_t af_type = 0;
    if (!sony_read_u8(bytes, 0x0002, rounds, &af_type)) {
        return;
    }

    uint16_t tags_out[32];
    MetaValue vals_out[32];
    uint32_t out_count = 0;

    uint8_t u8v = 0;
    if (af_type == 3) {
        const uint16_t u8_tags[] = { 0x0002, 0x0005, 0x0037, 0x0038,
                                     0x0039, 0x003A, 0x003B, 0x0043 };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            const uint16_t t = u8_tags[i];
            if (!sony_read_u8(bytes, t, rounds, &u8v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(u8v);
            out_count += 1;
        }
        if (0x0010U + 10U <= bytes.size()) {
            uint8_t af_points_used[10];
            bool ok = true;
            for (uint32_t i = 0; i < 10U; ++i) {
                ok = ok
                     && sony_read_u8(bytes, uint16_t(0x0010U + i), rounds,
                                     &af_points_used[i]);
            }
            if (ok) {
                tags_out[out_count] = 0x0010U;
                vals_out[out_count]
                    = make_u8_array(store.arena(),
                                    std::span<const uint8_t>(af_points_used));
                out_count += 1;
            }
        }
        uint8_t tmp_u8 = 0;
        if (sony_read_u8(bytes, 0x0050, rounds, &tmp_u8)) {
            tags_out[out_count] = 0x0050;
            vals_out[out_count] = make_i8(static_cast<int8_t>(tmp_u8));
            out_count += 1;
        }
    } else {
        const uint16_t u8_tags[] = { 0x0002, 0x0004, 0x0007, 0x0008,
                                     0x0009, 0x000A, 0x000B };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            const uint16_t t = u8_tags[i];
            if (!sony_read_u8(bytes, t, rounds, &u8v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(u8v);
            out_count += 1;
        }

        uint32_t u32 = 0;
        if (sony_read_u32le(bytes, 0x016E, rounds, &u32)) {
            tags_out[out_count] = 0x016E;
            vals_out[out_count] = make_u32(u32);
            out_count += 1;
        }

        uint8_t tmp_u8 = 0;
        if (sony_read_u8(bytes, 0x017D, rounds, &tmp_u8)) {
            tags_out[out_count] = 0x017D;
            vals_out[out_count] = make_i8(static_cast<int8_t>(tmp_u8));
            out_count += 1;
        }
        if (sony_read_u8(bytes, 0x017E, rounds, &tmp_u8)) {
            tags_out[out_count] = 0x017E;
            vals_out[out_count] = make_u8(tmp_u8);
            out_count += 1;
        }
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags_out, out_count),
                         std::span<const MetaValue>(vals_out, out_count),
                         options.limits, status_out);

    if (af_type == 2) {
        decode_sony_afstatus_from_afinfo(bytes, rounds, 0x0011, 30, mk_prefix,
                                         "afstatus19", store, options,
                                         status_out);
    } else if (af_type == 1) {
        decode_sony_afstatus_from_afinfo(bytes, rounds, 0x0011, 18, mk_prefix,
                                         "afstatus15", store, options,
                                         status_out);
    } else if (af_type == 3) {
        decode_sony_afstatus_from_afinfo(bytes, rounds, 0x007D, 95, mk_prefix,
                                         "afstatus79", store, options,
                                         status_out);
    }
}

static constexpr SonyCipherField kSonyTag9050bFields[] = {
    // u8 scalars.
    { 0x0000, SonyCipherFieldKind::U8 },
    { 0x0001, SonyCipherFieldKind::U8 },
    { 0x0039, SonyCipherFieldKind::U8 },
    { 0x004B, SonyCipherFieldKind::U8 },
    { 0x006B, SonyCipherFieldKind::U8 },
    { 0x006D, SonyCipherFieldKind::U8 },
    { 0x0073, SonyCipherFieldKind::U8 },
    { 0x0105, SonyCipherFieldKind::U8 },
    { 0x0106, SonyCipherFieldKind::U8 },
    { 0x010B, SonyCipherFieldKind::U8 },
    { 0x0114, SonyCipherFieldKind::U8 },
    { 0x01EB, SonyCipherFieldKind::U8 },
    { 0x01EE, SonyCipherFieldKind::U8 },
    { 0x021A, SonyCipherFieldKind::U8 },

    // u16 scalars.
    { 0x0046, SonyCipherFieldKind::U16LE },
    { 0x0048, SonyCipherFieldKind::U16LE },

    // Shutter u16[3].
    { 0x0026, SonyCipherFieldKind::U16LE_ARRAY, 3 },

    // u32 counters.
    { 0x003A, SonyCipherFieldKind::U32LE },
    { 0x0050, SonyCipherFieldKind::U32LE },
    { 0x0052, SonyCipherFieldKind::U32LE },
    { 0x0058, SonyCipherFieldKind::U32LE },
    { 0x019F, SonyCipherFieldKind::U32LE },
    { 0x01CB, SonyCipherFieldKind::U32LE },
    { 0x01CD, SonyCipherFieldKind::U32LE },

    // LensType2/LensType (int16u).
    { 0x0107, SonyCipherFieldKind::U16LE },
    { 0x0109, SonyCipherFieldKind::U16LE },

    // SonyTimeMinSec (2 raw bytes).
    { 0x0061, SonyCipherFieldKind::U8_ARRAY, 2 },

    // InternalSerialNumber (6 bytes).
    { 0x0088, SonyCipherFieldKind::U8_ARRAY, 6 },

    // LensSpecFeatures (undef[2]) at known offsets.
    { 0x0116, SonyCipherFieldKind::U8_ARRAY, 2 },
    { 0x01ED, SonyCipherFieldKind::U8_ARRAY, 2 },
    { 0x01F0, SonyCipherFieldKind::U8_ARRAY, 2 },
    { 0x021C, SonyCipherFieldKind::U8_ARRAY, 2 },
    { 0x021E, SonyCipherFieldKind::U8_ARRAY, 2 },
};

static constexpr SonyCipherField kSonyTag9050cFields[] = {
    // Shutter u16[3].
    { 0x0026, SonyCipherFieldKind::U16LE_ARRAY, 3 },

    // u8 scalars.
    { 0x0039, SonyCipherFieldKind::U8 },
    { 0x004B, SonyCipherFieldKind::U8 },
    { 0x006B, SonyCipherFieldKind::U8 },

    // u16 scalars.
    { 0x0046, SonyCipherFieldKind::U16LE },
    { 0x0048, SonyCipherFieldKind::U16LE },
    { 0x0066, SonyCipherFieldKind::U16LE },
    { 0x0068, SonyCipherFieldKind::U16LE },

    // u32 counters.
    { 0x003A, SonyCipherFieldKind::U32LE },
    { 0x0050, SonyCipherFieldKind::U32LE },

    // InternalSerialNumber (6 bytes).
    { 0x0088, SonyCipherFieldKind::U8_ARRAY, 6 },
};

static constexpr SonyCipherField kSonyTag9050dFields[] = {
    { 0x000A, SonyCipherFieldKind::U32LE },
    { 0x001A, SonyCipherFieldKind::U16LE },
    { 0x001C, SonyCipherFieldKind::U16LE },
    { 0x001F, SonyCipherFieldKind::U8 },
    { 0x0038, SonyCipherFieldKind::U8_ARRAY, 6 },
};


static void
decode_sony_tag9400(std::span<const std::byte> bytes,
                    std::string_view mk_prefix, std::string_view model,
                    bool is_legacy_slt_family, bool is_lunar, bool is_stellar,
                    MetaStore& store, const ExifDecodeOptions& options,
                    ExifDecodeResult* status_out) noexcept
{
    if (bytes.empty()) {
        return;
    }

    // Tag9400 variant selection (based on first deciphered byte).
    const uint8_t allowed[] = { 0x07, 0x09, 0x0A, 0x0C, 0x23, 0x24, 0x26,
                                0x28, 0x31, 0x32, 0x33, 0x41, 0x43 };
    const uint32_t rounds
        = sony_guess_cipher_rounds(bytes, 0, std::span<const uint8_t>(allowed));

    uint8_t v0 = 0;
    if (!sony_read_u8(bytes, 0, rounds, &v0)) {
        return;
    }

    switch (select_sony_tag9400_variant(model, v0, is_legacy_slt_family,
                                        is_lunar, is_stellar)) {
    case SonyTag9400Variant::A:
        sony_decode_cipher_fields(bytes, mk_prefix, "tag9400a", rounds,
                                  std::span<const SonyCipherField>(
                                      kSonyTag9400aFields,
                                      sizeof(kSonyTag9400aFields)
                                          / sizeof(kSonyTag9400aFields[0])),
                                  store, options, status_out);
        break;
    case SonyTag9400Variant::B:
        sony_decode_cipher_fields(bytes, mk_prefix, "tag9400b", rounds,
                                  std::span<const SonyCipherField>(
                                      kSonyTag9400bFields,
                                      sizeof(kSonyTag9400bFields)
                                          / sizeof(kSonyTag9400bFields[0])),
                                  store, options, status_out);
        break;
    case SonyTag9400Variant::C:
        sony_decode_cipher_fields(bytes, mk_prefix, "tag9400c", rounds,
                                  std::span<const SonyCipherField>(
                                      kSonyTag9400cFields,
                                      sizeof(kSonyTag9400cFields)
                                          / sizeof(kSonyTag9400cFields[0])),
                                  store, options, status_out);
        {
            const uint16_t shutter_tag = sony_tag9400c_shutter_type_tag(model);
            if (shutter_tag != 0U) {
                char sub_ifd_buf[96];
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "tag9400c", 0,
                                                 std::span<char>(sub_ifd_buf));
                uint8_t shutter_type = 0;
                if (!ifd_name.empty()
                    && sony_read_u8(bytes, shutter_tag, rounds, &shutter_type)) {
                    const uint16_t tags_out[]  = { shutter_tag };
                    const MetaValue vals_out[] = { make_u8(shutter_type) };
                    emit_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
                }
            }
        }
        break;
    }
}
static void
decode_sony_isoinfo_from_tag9401(std::span<const std::byte> bytes,
                                 std::string_view mk_prefix, MetaStore& store,
                                 const ExifDecodeOptions& options,
                                 ExifDecodeResult* status_out) noexcept
{
    if (bytes.empty()) {
        return;
    }

    const uint32_t rounds = 1;

    // Try a set of known ISOInfo locations inside Tag9401 and pick the first
    // plausible candidate.
    const uint16_t iso_offsets[] = {
        0x03E2, 0x03F4, 0x044E, 0x0498, 0x049D, 0x049E, 0x04A1, 0x04A2, 0x04BA,
        0x059D, 0x0634, 0x0636, 0x064C, 0x0653, 0x0678, 0x06B8, 0x06DE, 0x06E7,
    };

    uint16_t best_off        = 0;
    uint32_t best_score      = 0;
    uint8_t best_iso_setting = 0;
    uint8_t best_iso_min     = 0;
    uint8_t best_iso_max     = 0;

    for (uint32_t i = 0; i < sizeof(iso_offsets) / sizeof(iso_offsets[0]);
         ++i) {
        const uint64_t base = iso_offsets[i];
        if (base + 5U > bytes.size()) {
            continue;
        }

        uint8_t iso_setting = 0;
        uint8_t iso_min     = 0;
        uint8_t iso_max     = 0;
        if (!sony_read_u8(bytes, base + 0, rounds, &iso_setting)
            || !sony_read_u8(bytes, base + 2, rounds, &iso_min)
            || !sony_read_u8(bytes, base + 4, rounds, &iso_max)) {
            continue;
        }

        // Heuristic: ISO codes are small enum values.
        uint32_t score = 0;
        if (iso_setting <= 80U) {
            score += 1;
        }
        if (iso_min <= 80U) {
            score += 1;
        }
        if (iso_max <= 80U) {
            score += 1;
        }
        if (iso_setting == 0U) {
            score += 1;
        }

        if (score > best_score) {
            best_score       = score;
            best_off         = iso_offsets[i];
            best_iso_setting = iso_setting;
            best_iso_min     = iso_min;
            best_iso_max     = iso_max;
        }
    }

    if (best_score == 0) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "isoinfo", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    const uint16_t tags_out[]  = { 0x0000, 0x0002, 0x0004 };
    const MetaValue vals_out[] = { make_u8(best_iso_setting),
                                   make_u8(best_iso_min),
                                   make_u8(best_iso_max) };
    emit_bin_dir_entries(ifd_name, store, std::span<const uint16_t>(tags_out),
                         std::span<const MetaValue>(vals_out), options.limits,
                         status_out);
    (void)best_off;
}

static void
decode_sony_faceinfo_from_shotinfo(bool le, std::span<const std::byte> bytes,
                                   uint16_t face_off, uint16_t face_len,
                                   uint16_t faces_detected,
                                   std::string_view mk_prefix, MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
{
    if (faces_detected == 0) {
        return;
    }

    const bool is_faceinfo1 = (face_off == 0x0048u && face_len == 0x0020u);
    const bool is_faceinfo2 = (face_off == 0x005Eu && face_len == 0x0025u);
    if (!is_faceinfo1 && !is_faceinfo2) {
        return;
    }

    const uint16_t max_faces   = 8;
    const uint16_t face_count  = (faces_detected < max_faces) ? faces_detected
                                                              : max_faces;
    const uint64_t total_bytes = uint64_t(face_off)
                                 + uint64_t(face_len) * uint64_t(face_count);
    if (total_bytes > bytes.size()) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix,
                                     is_faceinfo1 ? "faceinfo1" : "faceinfo2",
                                     0, std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    uint16_t tags_out[max_faces];
    MetaValue vals_out[max_faces];
    uint32_t out_count = 0;

    for (uint16_t i = 0; i < face_count; ++i) {
        const uint32_t tag32 = static_cast<uint32_t>(face_len)
                               * static_cast<uint32_t>(i);
        if (tag32 > static_cast<uint32_t>(UINT16_MAX)) {
            continue;
        }
        const uint16_t tag = static_cast<uint16_t>(tag32);
        const uint64_t off = uint64_t(face_off) + uint64_t(face_len) * i;
        uint16_t rect[4];
        bool ok = true;
        for (uint32_t j = 0; j < 4; ++j) {
            ok = ok
                 && read_u16_endian(le, bytes, off + uint64_t(j) * 2u,
                                    &rect[j]);
        }
        if (!ok) {
            continue;
        }
        tags_out[out_count] = tag;
        vals_out[out_count]
            = make_u16_array(store.arena(), std::span<const uint16_t>(rect, 4));
        out_count += 1;
    }

    if (out_count == 0) {
        return;
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags_out, out_count),
                         std::span<const MetaValue>(vals_out, out_count),
                         options.limits, status_out);
}

static void
decode_sony_shotinfo_from_tag3000(std::span<const std::byte> bytes,
                                  std::string_view mk_prefix, MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept
{
    // Tag3000 is a small binary directory where offsets correspond to tag
    // ids (e.g. tag 0x001C stored at byte offset 0x001C).
    if (bytes.size() < 0x0044) {
        return;
    }

    const uint8_t b0 = u8(bytes[0]);
    const uint8_t b1 = u8(bytes[1]);
    bool le          = true;
    if (b0 == 'I' && b1 == 'I') {
        le = true;
    } else if (b0 == 'M' && b1 == 'M') {
        le = false;
    } else {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "shotinfo", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    uint16_t tags_out[16];
    MetaValue vals_out[16];
    uint32_t out_count      = 0;
    uint16_t face_info_off  = 0;
    uint16_t face_info_len  = 0;
    uint16_t faces_detected = 0;

    uint16_t u16v = 0;
    if (read_u16_endian(le, bytes, 0x0002, &u16v)) {
        face_info_off       = u16v;
        tags_out[out_count] = 0x0002;
        vals_out[out_count] = make_u16(u16v);
        out_count += 1;
    }

    if (0x0006 + 20U <= bytes.size() && 20U <= options.limits.max_value_bytes) {
        tags_out[out_count] = 0x0006;
        vals_out[out_count] = make_fixed_ascii_text(store.arena(),
                                                    bytes.subspan(0x0006, 20));
        out_count += 1;
    }

    if (read_u16_endian(le, bytes, 0x001A, &u16v)) {
        tags_out[out_count] = 0x001A;
        vals_out[out_count] = make_u16(u16v);
        out_count += 1;
    }
    if (read_u16_endian(le, bytes, 0x001C, &u16v)) {
        tags_out[out_count] = 0x001C;
        vals_out[out_count] = make_u16(u16v);
        out_count += 1;
    }

    if (read_u16_endian(le, bytes, 0x0030, &u16v)) {
        faces_detected      = u16v;
        tags_out[out_count] = 0x0030;
        vals_out[out_count] = make_u16(u16v);
        out_count += 1;
    }
    if (read_u16_endian(le, bytes, 0x0032, &u16v)) {
        face_info_len       = u16v;
        tags_out[out_count] = 0x0032;
        vals_out[out_count] = make_u16(u16v);
        out_count += 1;
    }

    if (0x0034 + 16U <= bytes.size() && 16U <= options.limits.max_value_bytes) {
        tags_out[out_count] = 0x0034;
        vals_out[out_count] = make_fixed_ascii_text(store.arena(),
                                                    bytes.subspan(0x0034, 16));
        out_count += 1;
    }

    if (out_count == 0) {
        return;
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags_out, out_count),
                         std::span<const MetaValue>(vals_out, out_count),
                         options.limits, status_out);

    decode_sony_faceinfo_from_shotinfo(le, bytes, face_info_off, face_info_len,
                                       faces_detected, mk_prefix, store,
                                       options, status_out);
}


static void
decode_sony_meterinfo9_from_tag2010(std::span<const std::byte> bytes,
                                    std::string_view mk_prefix,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
{
    if (bytes.empty()) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "meterinfo9", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    const uint32_t rounds = 1;

    struct Row final {
        uint16_t tag  = 0;
        uint16_t size = 0;
    };

    // MeterInfo row ranges are inferred from the known tag spacing:
    // - MeterInfo1 rows: 0x5A bytes each
    // - MeterInfo2 rows: 0x6E bytes each
    const Row rows[] = {
        { 0x0000, 0x005A }, { 0x005A, 0x005A }, { 0x00B4, 0x005A },
        { 0x010E, 0x005A }, { 0x0168, 0x005A }, { 0x01C2, 0x005A },
        { 0x021C, 0x005A }, { 0x0276, 0x006E }, { 0x02E4, 0x006E },
        { 0x0352, 0x006E }, { 0x03C0, 0x006E }, { 0x042E, 0x006E },
        { 0x049C, 0x006E }, { 0x050A, 0x006E }, { 0x0578, 0x006E },
        { 0x05E6, 0x006E },
    };

    uint16_t tags_out[32];
    MetaValue vals_out[32];
    uint32_t out_count = 0;

    for (uint32_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        const uint16_t tag = rows[i].tag;
        const uint16_t len = rows[i].size;
        if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
            break;
        }
        if (tag + len > bytes.size()) {
            continue;
        }
        if (len == 0 || len > options.limits.max_value_bytes) {
            continue;
        }

        const MetaValue v = make_sony_deciphered_bytes(store.arena(), bytes,
                                                       tag, len, rounds);
        if (v.kind != MetaValueKind::Bytes) {
            continue;
        }
        tags_out[out_count] = tag;
        vals_out[out_count] = v;
        out_count += 1;
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags_out, out_count),
                         std::span<const MetaValue>(vals_out, out_count),
                         options.limits, status_out);
}


static void
decode_sony_tag940e(std::span<const std::byte> bytes,
                    std::string_view mk_prefix, MetaStore& store,
                    const ExifDecodeOptions& options,
                    ExifDecodeResult* status_out) noexcept
{
    if (bytes.empty()) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "tag940e", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    // Tag940e contains the metering image; width/height are u8s at 0x1A06/0x1A07.
    // Data is commonly ciphered with the Sony substitution cipher.
    uint8_t w       = 0;
    uint8_t h       = 0;
    uint32_t rounds = 1;
    bool ok         = sony_read_u8(bytes, 0x1A06, 1, &w)
              && sony_read_u8(bytes, 0x1A07, 1, &h);
    if (!ok || w == 0 || h == 0) {
        rounds = 2;
        ok     = sony_read_u8(bytes, 0x1A06, 2, &w)
             && sony_read_u8(bytes, 0x1A07, 2, &h);
    }
    if (!ok || w == 0 || h == 0) {
        return;
    }

    const uint32_t image_bytes = uint32_t(w) * uint32_t(h) * 2U;
    if (image_bytes == 0U || image_bytes > options.limits.max_value_bytes) {
        return;
    }
    if (0x1A08 + uint64_t(image_bytes) > bytes.size()) {
        return;
    }

    const MetaValue img = make_sony_deciphered_bytes(store.arena(), bytes,
                                                     0x1A08, image_bytes,
                                                     rounds);
    if (img.kind != MetaValueKind::Bytes) {
        return;
    }

    const uint16_t tags_out[]  = { 0x1A06, 0x1A07, 0x1A08 };
    const MetaValue vals_out[] = { make_u8(w), make_u8(h), img };
    emit_bin_dir_entries(ifd_name, store, std::span<const uint16_t>(tags_out),
                         std::span<const MetaValue>(vals_out), options.limits,
                         status_out);
}


static void
decode_sony_tag9405b(std::span<const std::byte> bytes,
                     std::string_view mk_prefix, std::string_view model,
                     MetaStore& store, const ExifDecodeOptions& options,
                     ExifDecodeResult* status_out) noexcept
{
    if (bytes.empty()) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "tag9405b", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    const uint32_t rounds = 1;

    uint16_t tags_out[128];
    MetaValue vals_out[128];
    uint32_t out_count = 0;

    uint16_t u16v             = 0;
    const uint16_t u16_tags[] = { 0x0004, 0x0006, 0x000A, 0x000E,
                                  0x0014, 0x0016, 0x003E, 0x0040 };
    for (uint32_t i = 0; i < sizeof(u16_tags) / sizeof(u16_tags[0]); ++i) {
        const uint16_t t = u16_tags[i];
        if (!sony_read_u16le(bytes, t, rounds, &u16v)) {
            continue;
        }
        tags_out[out_count] = t;
        vals_out[out_count] = make_u16(u16v);
        out_count += 1;
    }

    uint8_t u8v              = 0;
    const uint16_t u8_tags[] = { 0x0034, 0x0042, 0x0044, 0x0046, 0x0048, 0x004A,
                                 0x0052, 0x005A, 0x005B, 0x005D, 0x005E };
    for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
        const uint16_t t = u8_tags[i];
        if (!sony_read_u8(bytes, t, rounds, &u8v)) {
            continue;
        }
        tags_out[out_count] = t;
        vals_out[out_count] = make_u8(u8v);
        out_count += 1;
    }

    const uint16_t lens_zoom_tag = sony_tag9405b_lens_zoom_tag(model);
    if (lens_zoom_tag != 0U
        && sony_read_u16le(bytes, lens_zoom_tag, rounds, &u16v)) {
        tags_out[out_count] = lens_zoom_tag;
        vals_out[out_count] = make_u16(u16v);
        out_count += 1;
    }

    // ExposureTime rational32u at 0x0010.
    {
        uint32_t numer = 0;
        uint32_t denom = 0;
        if (sony_read_u32le(bytes, 0x0010, rounds, &numer)
            && sony_read_u32le(bytes, 0x0010 + 4U, rounds, &denom)) {
            tags_out[out_count] = 0x0010;
            vals_out[out_count] = make_urational(numer, denom);
            out_count += 1;
        }
    }

    // SequenceImageNumber (int32u) at 0x0024.
    {
        uint32_t v = 0;
        if (sony_read_u32le(bytes, 0x0024, rounds, &v)) {
            tags_out[out_count] = 0x0024;
            vals_out[out_count] = make_u32(v);
            out_count += 1;
        }
    }

    // LensMount (u8) at 0x005e already decoded above.

    // LensType2/LensType (int16u) at 0x0060/0x0062.
    const uint16_t lens_u16_tags[] = { 0x0060, 0x0062 };
    for (uint32_t i = 0; i < sizeof(lens_u16_tags) / sizeof(lens_u16_tags[0]);
         ++i) {
        const uint16_t t = lens_u16_tags[i];
        uint16_t v       = 0;
        if (!sony_read_u16le(bytes, t, rounds, &v)) {
            continue;
        }
        tags_out[out_count] = t;
        vals_out[out_count] = make_u16(v);
        out_count += 1;
    }

    // DistortionCorrParams int16s[16] at 0x0064.
    {
        std::array<int16_t, 16> v {};
        bool ok = true;
        for (uint32_t i = 0; i < v.size(); ++i) {
            ok = ok && sony_read_i16le(bytes, 0x0064 + i * 2U, rounds, &v[i]);
        }
        if (ok) {
            tags_out[out_count] = 0x0064;
            vals_out[out_count] = make_i16_array(store.arena(),
                                                 std::span<const int16_t>(v));
            out_count += 1;
        }
    }

    const uint16_t vignetting_tag = sony_tag9405b_vignetting_tag(model);
    if (vignetting_tag != 0U) {
        std::array<int16_t, 16> v {};
        bool ok = true;
        for (uint32_t i = 0; i < v.size(); ++i) {
            ok = ok
                 && sony_read_i16le(bytes, vignetting_tag + i * 2U, rounds,
                                    &v[i]);
        }
        if (ok) {
            tags_out[out_count] = vignetting_tag;
            vals_out[out_count] = make_i16_array(store.arena(),
                                                 std::span<const int16_t>(v));
            out_count += 1;
        }
    }

    const uint16_t chromatic_tag = sony_tag9405b_chromatic_tag(model);
    if (chromatic_tag != 0U) {
        std::array<int16_t, 32> v {};
        bool ok = true;
        for (uint32_t i = 0; i < v.size(); ++i) {
            ok = ok
                 && sony_read_i16le(bytes, chromatic_tag + i * 2U, rounds,
                                    &v[i]);
        }
        if (ok) {
            tags_out[out_count] = chromatic_tag;
            vals_out[out_count] = make_i16_array(store.arena(),
                                                 std::span<const int16_t>(v));
            out_count += 1;
        }
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags_out, out_count),
                         std::span<const MetaValue>(vals_out, out_count),
                         options.limits, status_out);
}


static void
decode_sony_tag9416(std::span<const std::byte> bytes,
                    std::string_view mk_prefix, std::string_view model,
                    MetaStore& store, const ExifDecodeOptions& options,
                    ExifDecodeResult* status_out) noexcept
{
    if (bytes.empty()) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "tag9416", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    const uint8_t allowed_versions[] = { 0x06, 0x07, 0x08, 0x09, 0x0C, 0x0D,
                                         0x0F, 0x10, 0x11, 0x17, 0x1B };
    const uint32_t rounds
        = sony_guess_cipher_rounds(bytes, 0x0000,
                                   std::span<const uint8_t>(allowed_versions));

    uint16_t tags_out[128];
    MetaValue vals_out[128];
    uint32_t out_count = 0;

    // u8 tags.
    uint8_t u8v              = 0;
    const uint16_t u8_tags[] = { 0x0000, 0x002B, 0x0035, 0x0037,
                                 0x0048, 0x0049, 0x004A, 0x0070 };
    for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
        const uint16_t t = u8_tags[i];
        if (!sony_read_u8(bytes, t, rounds, &u8v)) {
            continue;
        }
        tags_out[out_count] = t;
        vals_out[out_count] = make_u8(u8v);
        out_count += 1;
    }

    // u16 tags.
    uint16_t u16v             = 0;
    const uint16_t u16_tags[] = { 0x0004, 0x0006, 0x000A, 0x0010, 0x0012,
                                  0x004B, 0x0071, 0x0073, 0x0075 };
    for (uint32_t i = 0; i < sizeof(u16_tags) / sizeof(u16_tags[0]); ++i) {
        const uint16_t t = u16_tags[i];
        if (!sony_read_u16le(bytes, t, rounds, &u16v)) {
            continue;
        }
        tags_out[out_count] = t;
        vals_out[out_count] = make_u16(u16v);
        out_count += 1;
    }

    // SequenceImageNumber int32u at 0x001d.
    {
        uint32_t v = 0;
        if (sony_read_u32le(bytes, 0x001D, rounds, &v)) {
            tags_out[out_count] = 0x001D;
            vals_out[out_count] = make_u32(v);
            out_count += 1;
        }
    }

    // ExposureTime rational32u at 0x000c.
    {
        uint32_t numer = 0;
        uint32_t denom = 0;
        if (sony_read_u32le(bytes, 0x000C, rounds, &numer)
            && sony_read_u32le(bytes, 0x000C + 4U, rounds, &denom)) {
            tags_out[out_count] = 0x000C;
            vals_out[out_count] = make_urational(numer, denom);
            out_count += 1;
        }
    }

    // DistortionCorrParams int16s[16] at 0x004f.
    {
        std::array<int16_t, 16> v {};
        bool ok = true;
        for (uint32_t i = 0; i < v.size(); ++i) {
            ok = ok && sony_read_i16le(bytes, 0x004F + i * 2U, rounds, &v[i]);
        }
        if (ok) {
            tags_out[out_count] = 0x004F;
            vals_out[out_count] = make_i16_array(store.arena(),
                                                 std::span<const int16_t>(v));
            out_count += 1;
        }
    }

    const uint16_t vignetting_tag = sony_tag9416_vignetting_tag(model);
    if (vignetting_tag != 0U) {
        if (vignetting_tag == 0x0708U || vignetting_tag == 0x089DU) {
            std::array<int16_t, 32> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                ok = ok
                     && sony_read_i16le(bytes, vignetting_tag + i * 2U, rounds,
                                        &v[i]);
            }
            if (ok) {
                tags_out[out_count] = vignetting_tag;
                vals_out[out_count]
                    = make_i16_array(store.arena(),
                                     std::span<const int16_t>(v));
                out_count += 1;
            }
        } else {
            std::array<int16_t, 16> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                ok = ok
                     && sony_read_i16le(bytes, vignetting_tag + i * 2U, rounds,
                                        &v[i]);
            }
            if (ok) {
                tags_out[out_count] = vignetting_tag;
                vals_out[out_count]
                    = make_i16_array(store.arena(),
                                     std::span<const int16_t>(v));
                out_count += 1;
            }
        }
    }

    const uint16_t apsc_tag = sony_tag9416_apsc_tag(model);
    if (apsc_tag != 0U && sony_read_u8(bytes, apsc_tag, rounds, &u8v)) {
        tags_out[out_count] = apsc_tag;
        vals_out[out_count] = make_u8(u8v);
        out_count += 1;
    }

    const uint16_t chromatic_tag = sony_tag9416_chromatic_tag(model);
    if (chromatic_tag != 0U) {
        std::array<int16_t, 32> v {};
        bool ok = true;
        for (uint32_t i = 0; i < v.size(); ++i) {
            ok = ok
                 && sony_read_i16le(bytes, chromatic_tag + i * 2U, rounds,
                                    &v[i]);
        }
        if (ok) {
            tags_out[out_count] = chromatic_tag;
            vals_out[out_count] = make_i16_array(store.arena(),
                                                 std::span<const int16_t>(v));
            out_count += 1;
        }
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags_out, out_count),
                         std::span<const MetaValue>(vals_out, out_count),
                         options.limits, status_out);
}

static void
decode_sony_tag2010e(std::span<const std::byte> bytes,
                     std::string_view mk_prefix, std::string_view model,
                     MetaStore& store, const ExifDecodeOptions& options,
                     ExifDecodeResult* status_out) noexcept
{
    const uint32_t rounds = 1;
    sony_decode_cipher_fields(
        bytes, mk_prefix, "tag2010e", rounds,
        std::span<const SonyCipherField>(kSonyTag2010eFields,
                                         sizeof(kSonyTag2010eFields)
                                             / sizeof(kSonyTag2010eFields[0])),
        store, options, status_out);
    decode_sony_meterinfo_from_tag2010(bytes, rounds, 0x04B8, mk_prefix, store,
                                       options, status_out);

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "tag2010e", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    uint16_t tags_out[5];
    MetaValue vals_out[5];
    uint32_t out_count = 0;

    if ((model == "SLT-A99" || model == "SLT-A99V" || model == "HV"
         || model == "NEX-5R" || model == "NEX-5T" || model == "NEX-6"
         || model == "NEX-VG900" || model == "NEX-VG30E" || model == "DSC-RX100"
         || model == "Stellar")) {
        uint16_t v = 0;
        if (sony_read_u16le(bytes, 0x1254, rounds, &v)) {
            tags_out[out_count] = 0x1254;
            vals_out[out_count] = make_u16(v);
            out_count += 1;
        }
    }
    if ((model == "DSC-RX1" || model == "DSC-RX1R")) {
        uint16_t v = 0;
        if (sony_read_u16le(bytes, 0x1258, rounds, &v)) {
            tags_out[out_count] = 0x1258;
            vals_out[out_count] = make_u16(v);
            out_count += 1;
        }
    }
    if ((model == "SLT-A58" || model == "ILCE-3000" || model == "ILCE-3500"
         || model == "NEX-3N" || model == "DSC-HX300" || model == "DSC-HX50V"
         || model == "DSC-WX60" || model == "DSC-WX80" || model == "DSC-WX200"
         || model == "DSC-WX300" || model == "DSC-TX30")) {
        const uint16_t focal_tags[] = { 0x1278, 0x127A, 0x127C, 0x1280 };
        for (uint32_t i = 0; i < sizeof(focal_tags) / sizeof(focal_tags[0]);
             ++i) {
            uint16_t v = 0;
            if (!sony_read_u16le(bytes, focal_tags[i], rounds, &v)) {
                continue;
            }
            tags_out[out_count] = focal_tags[i];
            vals_out[out_count] = make_u16(v);
            out_count += 1;
        }
    }

    if (out_count != 0U) {
        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }
}

static void
decode_sony_tag9406(std::span<const std::byte> bytes,
                    std::string_view mk_prefix, std::string_view model,
                    MetaStore& store, const ExifDecodeOptions& options,
                    ExifDecodeResult* status_out) noexcept
{
    if (bytes.empty() || mk_prefix.empty()) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "tag9406", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    uint16_t tags_out[8];
    MetaValue vals_out[8];
    uint32_t out_count = 0;
    for (size_t i = 0;
         i < sizeof(kSonyTag9406Fields) / sizeof(kSonyTag9406Fields[0]); ++i) {
        uint8_t v = 0;
        if (!sony_read_u8(bytes, kSonyTag9406Fields[i].tag, 1, &v)) {
            continue;
        }
        tags_out[out_count] = kSonyTag9406Fields[i].tag;
        vals_out[out_count] = make_u8(v);
        out_count += 1;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    for (uint32_t i = 0; i < out_count; ++i) {
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd_name, tags_out[i]);
        entry.origin.block          = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = vals_out[i].count;
        entry.value                 = vals_out[i];
        entry.flags |= EntryFlags::Derived;
        if (tags_out[i] == 0x0005U
            && sony_tag9406_0005_prefers_battery_level_name(model)) {
            entry.flags |= EntryFlags::ContextualName;
            entry.origin.name_context_kind
                = EntryNameContextKind::SonyTag94060005;
            entry.origin.name_context_variant = 1U;
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1U;
        }
    }
}

static void
decode_sony_tag9050c_extras(std::span<const std::byte> bytes,
                            std::string_view mk_prefix, std::string_view model,
                            MetaStore& store, const ExifDecodeOptions& options,
                            ExifDecodeResult* status_out) noexcept
{
    if (model != "ILCE-1") {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "tag9050c", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    const MetaValue v = make_sony_deciphered_bytes(store.arena(), bytes, 0x008A,
                                                   6, 1);
    if (v.kind != MetaValueKind::Bytes) {
        return;
    }

    const uint16_t tags_out[]  = { 0x008A };
    const MetaValue vals_out[] = { v };
    emit_bin_dir_entries(ifd_name, store, std::span<const uint16_t>(tags_out),
                         std::span<const MetaValue>(vals_out), options.limits,
                         status_out);
}


void
decode_sony_cipher_subdirs(std::string_view mk_ifd0, MetaStore& store,
                           const ExifDecodeOptions& options,
                           ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return;
    }

    struct Candidate final {
        uint16_t tag = 0;
        MetaValue value;
    };

    Candidate cands[16];
    uint32_t cand_count = 0;

    const ByteArena& arena               = store.arena();
    const std::span<const Entry> entries = store.entries();

    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        if (arena_string(arena, e.key.data.exif_tag.ifd) != mk_ifd0) {
            continue;
        }
        if (e.value.kind != MetaValueKind::Bytes) {
            continue;
        }
        const uint16_t tag = e.key.data.exif_tag.tag;
        switch (tag) {
        case 0x9050:  // Tag9050*
        case 0x3000:  // ShotInfo
        case 0x9400:  // Tag9400*
        case 0x9401:  // Tag9401 (ISOInfo)
        case 0x9402:  // Tag9402
        case 0x9403:  // Tag9403
        case 0x9404:  // Tag9404*
        case 0x9405:  // Tag9405*
        case 0x9406:  // Tag9406*
        case 0x940C:  // Tag940c
        case 0x940E:  // Tag940e
        case 0x9416:  // Tag9416
        case 0x2010:  // Tag2010*
        case 0x202A:  // Tag202a
            break;
        default: continue;
        }
        if (cand_count < sizeof(cands) / sizeof(cands[0])) {
            cands[cand_count].tag   = tag;
            cands[cand_count].value = e.value;
            cand_count += 1;
        }
    }

    const std::string_view mk_prefix = "mk_sony";
    ExifContext ctx(store);
    std::string_view model;
    (void)ctx.find_first_text("ifd0", 0x0110 /* Model */, &model);
    model = sony_trim_model(model);
    char model_buf[64];
    const size_t model_n = (model.size() < sizeof(model_buf) - 1U)
                               ? model.size()
                               : (sizeof(model_buf) - 1U);
    for (size_t i = 0; i < model_n; ++i) {
        model_buf[i] = model[i];
    }
    model_buf[model_n]              = '\0';
    model                           = std::string_view(model_buf, model_n);
    const bool is_ilca_family       = model.starts_with("ILCA-");
    const bool is_legacy_slt_family = model.starts_with("SLT-")
                                      || (model == "HV");
    const bool is_slt_family = is_legacy_slt_family || is_ilca_family;
    const bool is_lunar      = (model == "Lunar");
    const bool is_stellar    = (model == "Stellar");

    for (uint32_t i = 0; i < cand_count; ++i) {
        const uint16_t tag      = cands[i].tag;
        const ByteSpan raw_span = cands[i].value.data.span;
        const std::span<const std::byte> arena_raw
            = store.arena().span(raw_span);
        if (arena_raw.empty()) {
            continue;
        }

        // Sony subdecoders emit arena-backed arrays before all source fields
        // have necessarily been read. Keep the selected cipher payload
        // independent from arena reallocations for the entire decode.
        std::unique_ptr<std::byte[]> stable_storage(
            new (std::nothrow) std::byte[arena_raw.size()]);
        if (!stable_storage) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            continue;
        }
        std::memcpy(stable_storage.get(), arena_raw.data(), arena_raw.size());
        const std::span<const std::byte> raw(stable_storage.get(),
                                             arena_raw.size());

        if (tag == 0x3000) {
            decode_sony_shotinfo_from_tag3000(raw, mk_prefix, store, options,
                                              status_out);
            continue;
        }
        if (tag == 0x2010) {
            const uint32_t rounds = 1;
            switch (select_sony_tag2010_variant(model)) {
            case SonyTag2010Variant::A:
                sony_decode_cipher_fields(
                    raw, mk_prefix, "tag2010a", rounds,
                    std::span<const SonyCipherField>(
                        kSonyTag2010aFields,
                        sizeof(kSonyTag2010aFields)
                            / sizeof(kSonyTag2010aFields[0])),
                    store, options, status_out);
                decode_sony_meterinfo_from_tag2010(
                    raw, rounds, 0x04B0, mk_prefix, store, options, status_out);
                break;
            case SonyTag2010Variant::B:
                sony_decode_cipher_fields(raw, mk_prefix, "tag2010b", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag2010bFields,
                                              sizeof(kSonyTag2010bFields)
                                                  / sizeof(
                                                      kSonyTag2010bFields[0])),
                                          store, options, status_out);
                decode_sony_meterinfo_from_tag2010(raw, rounds, 0x04B4,
                                                   mk_prefix, store, options,
                                                   status_out);
                break;
            case SonyTag2010Variant::C:
                sony_decode_cipher_fields(raw, mk_prefix, "tag2010c", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag2010cFields,
                                              sizeof(kSonyTag2010cFields)
                                                  / sizeof(
                                                      kSonyTag2010cFields[0])),
                                          store, options, status_out);
                decode_sony_meterinfo_from_tag2010(raw, rounds, 0x0490,
                                                   mk_prefix, store, options,
                                                   status_out);
                break;
            case SonyTag2010Variant::E:
                decode_sony_tag2010e(raw, mk_prefix, model, store, options,
                                     status_out);
                break;
            case SonyTag2010Variant::F:
                sony_decode_cipher_fields(raw, mk_prefix, "tag2010f", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag2010fFields,
                                              sizeof(kSonyTag2010fFields)
                                                  / sizeof(
                                                      kSonyTag2010fFields[0])),
                                          store, options, status_out);
                decode_sony_meterinfo_from_tag2010(raw, rounds, 0x01E0,
                                                   mk_prefix, store, options,
                                                   status_out);
                break;
            case SonyTag2010Variant::G:
                sony_decode_cipher_fields(raw, mk_prefix, "tag2010g", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag2010gFields,
                                              sizeof(kSonyTag2010gFields)
                                                  / sizeof(
                                                      kSonyTag2010gFields[0])),
                                          store, options, status_out);
                decode_sony_meterinfo_from_tag2010(raw, rounds, 0x0388,
                                                   mk_prefix, store, options,
                                                   status_out);
                break;
            case SonyTag2010Variant::H:
                sony_decode_cipher_fields(raw, mk_prefix, "tag2010h", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag2010hFields,
                                              sizeof(kSonyTag2010hFields)
                                                  / sizeof(
                                                      kSonyTag2010hFields[0])),
                                          store, options, status_out);
                decode_sony_meterinfo_from_tag2010(
                    raw, rounds, sony_tag2010h_meter_offset(model), mk_prefix,
                    store, options, status_out);
                break;
            case SonyTag2010Variant::I:
                sony_decode_cipher_fields(raw, mk_prefix, "tag2010i", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag2010iFields,
                                              sizeof(kSonyTag2010iFields)
                                                  / sizeof(
                                                      kSonyTag2010iFields[0])),
                                          store, options, status_out);
                decode_sony_meterinfo9_from_tag2010(raw, mk_prefix, store,
                                                    options, status_out);
                break;
            }
            continue;
        }
        if (tag == 0x202A) {
            sony_decode_cipher_fields(raw, mk_prefix, "tag202a", 1,
                                      std::span<const SonyCipherField>(
                                          kSonyTag202aFields,
                                          sizeof(kSonyTag202aFields)
                                              / sizeof(kSonyTag202aFields[0])),
                                      store, options, status_out);
            continue;
        }
        if (tag == 0x9404) {
            SonyTag9404Variant variant = select_sony_tag9404_variant(raw);
            if (variant == SonyTag9404Variant::Unknown) {
                variant = (is_lunar || is_stellar) ? SonyTag9404Variant::B
                                                   : SonyTag9404Variant::C;
            }
            if (variant == SonyTag9404Variant::A) {
                sony_decode_cipher_fields(raw, mk_prefix, "tag9404a", 1,
                                          std::span<const SonyCipherField>(
                                              kSonyTag9404aFields,
                                              sizeof(kSonyTag9404aFields)
                                                  / sizeof(
                                                      kSonyTag9404aFields[0])),
                                          store, options, status_out);
            } else if (variant == SonyTag9404Variant::B) {
                sony_decode_cipher_fields(raw, mk_prefix, "tag9404b", 1,
                                          std::span<const SonyCipherField>(
                                              kSonyTag9404bFields,
                                              sizeof(kSonyTag9404bFields)
                                                  / sizeof(
                                                      kSonyTag9404bFields[0])),
                                          store, options, status_out);
            } else {
                sony_decode_cipher_fields(raw, mk_prefix, "tag9404c", 1,
                                          std::span<const SonyCipherField>(
                                              kSonyTag9404cFields,
                                              sizeof(kSonyTag9404cFields)
                                                  / sizeof(
                                                      kSonyTag9404cFields[0])),
                                          store, options, status_out);
            }
            continue;
        }
        if (tag == 0x940E) {
            if (is_slt_family) {
                decode_sony_afinfo_from_tag940e(raw, mk_prefix, store, options,
                                                status_out);
            } else {
                decode_sony_tag940e(raw, mk_prefix, store, options, status_out);
            }
            continue;
        }
        if (tag == 0x9400) {
            decode_sony_tag9400(raw, mk_prefix, model, is_legacy_slt_family,
                                is_lunar, is_stellar, store, options,
                                status_out);
            continue;
        }
        if (tag == 0x9401) {
            decode_sony_isoinfo_from_tag9401(raw, mk_prefix, store, options,
                                             status_out);
            continue;
        }
        if (tag == 0x9402) {
            sony_decode_cipher_fields(raw, mk_prefix, "tag9402", 1,
                                      std::span<const SonyCipherField>(
                                          kSonyTag9402Fields,
                                          sizeof(kSonyTag9402Fields)
                                              / sizeof(kSonyTag9402Fields[0])),
                                      store, options, status_out);
            continue;
        }
        if (tag == 0x9403) {
            sony_decode_cipher_fields(raw, mk_prefix, "tag9403", 1,
                                      std::span<const SonyCipherField>(
                                          kSonyTag9403Fields,
                                          sizeof(kSonyTag9403Fields)
                                              / sizeof(kSonyTag9403Fields[0])),
                                      store, options, status_out);
            continue;
        }
        if (tag == 0x9406) {
            decode_sony_tag9406(raw, mk_prefix, model, store, options,
                                status_out);
            continue;
        }
        if (tag == 0x940C) {
            sony_decode_cipher_fields(raw, mk_prefix, "tag940c", 1,
                                      std::span<const SonyCipherField>(
                                          kSonyTag940cFields,
                                          sizeof(kSonyTag940cFields)
                                              / sizeof(kSonyTag940cFields[0])),
                                      store, options, status_out);
            continue;
        }
        if (tag == 0x9405) {
            const uint32_t rounds = 1;
            if (sony_model_uses_tag9405a(model, is_legacy_slt_family, is_lunar,
                                         is_stellar)) {
                sony_decode_cipher_fields(raw, mk_prefix, "tag9405a", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag9405aFields,
                                              sizeof(kSonyTag9405aFields)
                                                  / sizeof(
                                                      kSonyTag9405aFields[0])),
                                          store, options, status_out);
            } else {
                // Best-effort: Tag9405b is common for newer ILCE/DSC bodies.
                decode_sony_tag9405b(raw, mk_prefix, model, store, options,
                                     status_out);
            }
            continue;
        }
        if (tag == 0x9416) {
            decode_sony_tag9416(raw, mk_prefix, model, store, options,
                                status_out);
            continue;
        }
        if (tag == 0x9050) {
            const uint32_t rounds = 1;
            switch (
                select_sony_tag9050_variant(model, is_slt_family, is_lunar)) {
            case SonyTag9050Variant::A:
                sony_decode_cipher_fields(raw, mk_prefix, "tag9050a", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag9050aFields,
                                              sizeof(kSonyTag9050aFields)
                                                  / sizeof(
                                                      kSonyTag9050aFields[0])),
                                          store, options, status_out);
                break;
            case SonyTag9050Variant::C:
                sony_decode_cipher_fields(raw, mk_prefix, "tag9050c", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag9050cFields,
                                              sizeof(kSonyTag9050cFields)
                                                  / sizeof(
                                                      kSonyTag9050cFields[0])),
                                          store, options, status_out);
                decode_sony_tag9050c_extras(raw, mk_prefix, model, store,
                                            options, status_out);
                break;
            case SonyTag9050Variant::D:
                sony_decode_cipher_fields(raw, mk_prefix, "tag9050d", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag9050dFields,
                                              sizeof(kSonyTag9050dFields)
                                                  / sizeof(
                                                      kSonyTag9050dFields[0])),
                                          store, options, status_out);
                break;
            case SonyTag9050Variant::B:
                sony_decode_cipher_fields(raw, mk_prefix, "tag9050b", rounds,
                                          std::span<const SonyCipherField>(
                                              kSonyTag9050bFields,
                                              sizeof(kSonyTag9050bFields)
                                                  / sizeof(
                                                      kSonyTag9050bFields[0])),
                                          store, options, status_out);
                break;
            }
            continue;
        }
    }
}

}  // namespace openmeta::exif_internal
