// SPDX-License-Identifier: Apache-2.0

#include "openmeta/simple_meta.h"

#include "bmff_fields_decode_internal.h"
#include "crw_ciff_decode_internal.h"
#include "exif_tiff_decode_internal.h"
#include "raf_decode_internal.h"
#include "x3f_decode_internal.h"

#include "openmeta/exr_decode.h"
#include "openmeta/icc_decode.h"
#include "openmeta/iptc_iim_decode.h"
#include "openmeta/photoshop_irb_decode.h"
#include "openmeta/xmp_decode.h"

#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

namespace openmeta {
namespace {

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }


    static void merge_payload_result(PayloadResult* out,
                                     const PayloadResult& in) noexcept
    {
        if (in.status == PayloadStatus::Ok) {
            return;
        }
        if (in.needed > out->needed) {
            out->needed = in.needed;
        }

        // Prefer actionable outcomes:
        // LimitExceeded > OutputTruncated > Unsupported > Malformed.
        if (out->status == PayloadStatus::LimitExceeded) {
            return;
        }
        if (in.status == PayloadStatus::LimitExceeded) {
            out->status = in.status;
            return;
        }
        if (out->status == PayloadStatus::OutputTruncated) {
            return;
        }
        if (in.status == PayloadStatus::OutputTruncated) {
            out->status = in.status;
            return;
        }
        if (out->status == PayloadStatus::Unsupported) {
            return;
        }
        if (in.status == PayloadStatus::Unsupported) {
            out->status = in.status;
            return;
        }
        if (out->status == PayloadStatus::Malformed) {
            return;
        }
        if (in.status == PayloadStatus::Malformed) {
            out->status = in.status;
            return;
        }
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

    static bool has_nul(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == std::byte { 0 }) {
                return true;
            }
        }
        return false;
    }

    static bool bytes_ascii(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (u8(bytes[i]) > 0x7FU) {
                return false;
            }
        }
        return true;
    }

    static bool bytes_valid_utf8(std::span<const std::byte> bytes) noexcept
    {
        size_t i = 0;
        while (i < bytes.size()) {
            const uint8_t c0 = u8(bytes[i]);
            if ((c0 & 0x80U) == 0U) {
                i += 1U;
                continue;
            }

            uint32_t needed = 0;
            uint32_t min_cp = 0;
            uint32_t cp     = 0;

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
            for (uint32_t j = 0; j < needed; ++j) {
                const uint8_t cx = u8(bytes[i + 1U + j]);
                if ((cx & 0xC0U) != 0x80U) {
                    return false;
                }
                cp = (cp << 6) | static_cast<uint32_t>(cx & 0x3FU);
            }

            if (cp < min_cp || cp > 0x10FFFFU) {
                return false;
            }
            if (cp >= 0xD800U && cp <= 0xDFFFU) {
                return false;
            }
            i += 1U + needed;
        }
        return true;
    }

    static bool comment_emit_entry(MetaStore& store,
                                   std::span<const std::byte> bytes) noexcept
    {
        const BlockId block_id = store.add_block(BlockInfo {});
        if (block_id == kInvalidBlockId) {
            return false;
        }

        Entry entry;
        entry.key                     = make_comment_key();
        entry.origin.block            = block_id;
        entry.origin.order_in_block   = 0U;
        entry.origin.wire_type.family = WireFamily::Other;

        const std::string_view text(reinterpret_cast<const char*>(bytes.data()),
                                    bytes.size());
        if (!has_nul(bytes) && bytes_ascii(bytes)) {
            entry.value = make_text(store.arena(), text, TextEncoding::Ascii);
        } else if (!has_nul(bytes) && bytes_valid_utf8(bytes)) {
            entry.value = make_text(store.arena(), text, TextEncoding::Utf8);
        } else {
            entry.value = make_bytes(store.arena(), bytes);
        }

        return store.add_entry(entry) != kInvalidEntryId;
    }

    static bool decode_comment_block(std::span<const std::byte> block_bytes,
                                     const ContainerBlockRef& block,
                                     MetaStore& store) noexcept
    {
        if (block.kind != ContainerBlockKind::Comment) {
            return false;
        }
        return comment_emit_entry(store, block_bytes);
    }

    static std::string_view
    make_string_view(std::span<const std::byte> bytes) noexcept
    {
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool
    png_text_emit_entry(MetaStore& store, BlockId block, uint32_t* io_order,
                        std::string_view keyword, std::string_view field,
                        std::string_view text, TextEncoding encoding) noexcept
    {
        if (!io_order) {
            return false;
        }
        Entry entry;
        entry.key          = make_png_text_key(store.arena(), keyword, field);
        entry.value        = make_text(store.arena(), text, encoding);
        entry.origin.block = block;
        entry.origin.order_in_block   = *io_order;
        entry.origin.wire_type.family = WireFamily::Other;
        if (store.add_entry(entry) == kInvalidEntryId) {
            return false;
        }
        *io_order += 1U;
        return true;
    }

    static bool decode_png_text_block(std::span<const std::byte> file_bytes,
                                      const ContainerBlockRef& block,
                                      std::span<const std::byte> block_bytes,
                                      bool payload_decompressed,
                                      MetaStore& store) noexcept
    {
        if (block.format != ContainerFormat::Png
            || block.kind != ContainerBlockKind::Text) {
            return false;
        }
        if (block.outer_offset > file_bytes.size()
            || block.outer_size > file_bytes.size() - block.outer_offset
            || block.outer_size < 12U) {
            return false;
        }

        const std::span<const std::byte> outer
            = file_bytes.subspan(static_cast<size_t>(block.outer_offset),
                                 static_cast<size_t>(block.outer_size));
        uint32_t data_size = 0;
        uint32_t type      = 0;
        if (!read_u32be(outer, 0, &data_size) || !read_u32be(outer, 4, &type)) {
            return false;
        }
        if (static_cast<uint64_t>(data_size) + 12U != block.outer_size) {
            return false;
        }

        const std::span<const std::byte> data
            = outer.subspan(8, static_cast<size_t>(data_size));
        if (type == fourcc('t', 'E', 'X', 't')) {
            size_t keyword_end = 0;
            while (keyword_end < data.size() && u8(data[keyword_end]) != 0U) {
                keyword_end += 1U;
            }
            if (keyword_end >= data.size()) {
                return false;
            }
            const std::string_view keyword = make_string_view(
                data.subspan(0, keyword_end));
            const std::string_view text = make_string_view(
                data.subspan(keyword_end + 1));
            const BlockId block_id = store.add_block(BlockInfo {});
            if (block_id == kInvalidBlockId) {
                return false;
            }
            uint32_t order = 0;
            return png_text_emit_entry(store, block_id, &order, keyword, "text",
                                       text, TextEncoding::Unknown);
        }

        if (type == fourcc('z', 'T', 'X', 't')) {
            if (!payload_decompressed) {
                return false;
            }
            size_t keyword_end = 0;
            while (keyword_end < data.size() && u8(data[keyword_end]) != 0U) {
                keyword_end += 1U;
            }
            if (keyword_end + 2U > data.size()) {
                return false;
            }
            const std::string_view keyword = make_string_view(
                data.subspan(0, keyword_end));
            const std::string_view text = make_string_view(block_bytes);
            const BlockId block_id      = store.add_block(BlockInfo {});
            if (block_id == kInvalidBlockId) {
                return false;
            }
            uint32_t order = 0;
            return png_text_emit_entry(store, block_id, &order, keyword, "text",
                                       text, TextEncoding::Unknown);
        }

        if (type == fourcc('i', 'T', 'X', 't')) {
            size_t p = 0;
            while (p < data.size() && u8(data[p]) != 0U) {
                p += 1U;
            }
            if (p + 3U > data.size()) {
                return false;
            }

            const std::string_view keyword = make_string_view(
                data.subspan(0, p));
            const bool compressed = (u8(data[p + 1]) != 0U);
            if (compressed && !payload_decompressed) {
                return false;
            }

            size_t lang = p + 3U;
            while (lang < data.size() && u8(data[lang]) != 0U) {
                lang += 1U;
            }
            if (lang >= data.size()) {
                return false;
            }

            const size_t translated_start = lang + 1U;
            size_t translated_end         = translated_start;
            while (translated_end < data.size()
                   && u8(data[translated_end]) != 0U) {
                translated_end += 1U;
            }
            if (translated_end >= data.size()) {
                return false;
            }

            const std::string_view language = make_string_view(
                data.subspan(p + 3U, lang - (p + 3U)));
            const std::string_view translated = make_string_view(
                data.subspan(translated_start,
                             translated_end - translated_start));
            const std::string_view text = make_string_view(block_bytes);
            const BlockId block_id      = store.add_block(BlockInfo {});
            if (block_id == kInvalidBlockId) {
                return false;
            }
            uint32_t order = 0;

            bool ok = true;
            if (!language.empty()) {
                ok = png_text_emit_entry(store, block_id, &order, keyword,
                                         "language", language,
                                         TextEncoding::Ascii)
                     && ok;
            }
            if (!translated.empty()) {
                ok = png_text_emit_entry(store, block_id, &order, keyword,
                                         "translated_keyword", translated,
                                         TextEncoding::Utf8)
                     && ok;
            }
            ok = png_text_emit_entry(store, block_id, &order, keyword, "text",
                                     text, TextEncoding::Utf8)
                 && ok;
            return ok;
        }

        return false;
    }

    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t b0 = static_cast<uint16_t>(u8(bytes[offset + 0]));
        const uint16_t b1 = static_cast<uint16_t>(u8(bytes[offset + 1]));
        const uint16_t v  = static_cast<uint16_t>((b0 << 8) | b1);
        *out              = v;
        return true;
    }

    static bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t b0 = static_cast<uint16_t>(u8(bytes[offset + 0]));
        const uint16_t b1 = static_cast<uint16_t>(u8(bytes[offset + 1]));
        const uint16_t v  = static_cast<uint16_t>(b0 | (b1 << 8));
        *out              = v;
        return true;
    }

    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4 > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 8;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 16;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 24;
        *out = v;
        return true;
    }

    static uint32_t f32_bits_from_float(float v) noexcept
    {
        uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v));
        std::memcpy(&bits, &v, sizeof(bits));
        return bits;
    }

    static float f32_from_bits(uint32_t bits) noexcept
    {
        float v = 0.0f;
        static_assert(sizeof(bits) == sizeof(v));
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    static bool float_plausible(float v, float lo, float hi) noexcept
    {
        return std::isfinite(v) && v >= lo && v <= hi;
    }

    static uint64_t find_magic_u32be(std::span<const std::byte> bytes,
                                     uint32_t magic) noexcept
    {
        if (bytes.size() < 4) {
            return UINT64_MAX;
        }
        for (uint64_t i = 0; i + 4U <= bytes.size(); ++i) {
            uint32_t v = 0;
            if (read_u32be(bytes, i, &v) && v == magic) {
                return i;
            }
        }
        return UINT64_MAX;
    }

    static bool parse_dji_thermal_params(std::span<const std::byte> app4,
                                         MetaStore& store,
                                         const ExifDecodeLimits& limits,
                                         ExifDecodeResult* status_out) noexcept
    {
        // ExifTool reference tables:
        // - ThermalParams:  magic 0xaa551206, u16 values at offsets 0x44..0x4c
        // - ThermalParams2: float values (ambient/dist/emiss/rh/refl) + IDString
        // - ThermalParams3: magic 0xaa553800, u16 values at offsets 0x04..0x0a
        //
        // Real files often store these blocks at offset 32 within APP4.

        bool any = false;

        // 1) ThermalParams3 (magic AA 55 38 00).
        const uint64_t m3 = find_magic_u32be(app4, 0xAA553800U);
        if (m3 != UINT64_MAX && m3 + 0x0cU <= app4.size()) {
            uint16_t rh_raw = 0;
            uint16_t od_raw = 0;
            uint16_t em_raw = 0;
            uint16_t rt_raw = 0;
            if (read_u16le(app4, m3 + 0x04U, &rh_raw)
                && read_u16le(app4, m3 + 0x06U, &od_raw)
                && read_u16le(app4, m3 + 0x08U, &em_raw)
                && read_u16le(app4, m3 + 0x0aU, &rt_raw)) {
                const float od = float(od_raw) / 10.0f;
                const float em = float(em_raw) / 100.0f;
                const float rt = float(rt_raw) / 10.0f;

                char scratch[64];
                const std::string_view ifd_name
                    = exif_internal::make_mk_subtable_ifd_token(
                        "mk_dji", "thermalparams3", 0,
                        std::span<char>(scratch));
                if (!ifd_name.empty()) {
                    const uint16_t tags_out[4] = { 0x0004, 0x0006, 0x0008,
                                                   0x000a };
                    const MetaValue vals_out[4]
                        = { make_u16(rh_raw),
                            make_f32_bits(f32_bits_from_float(od)),
                            make_f32_bits(f32_bits_from_float(em)),
                            make_f32_bits(f32_bits_from_float(rt)) };
                    exif_internal::emit_bin_dir_entries(
                        ifd_name, store, std::span<const uint16_t>(tags_out, 4),
                        std::span<const MetaValue>(vals_out, 4), limits,
                        status_out);
                    any = true;
                }
            }
        }

        // 2) ThermalParams (magic AA 55 12 06).
        const uint64_t m1 = find_magic_u32be(app4, 0xAA551206U);
        if (m1 != UINT64_MAX && m1 + 0x4eU <= app4.size()) {
            uint16_t od = 0;
            uint16_t rh = 0;
            uint16_t em = 0;
            uint16_t rf = 0;
            uint16_t at = 0;
            if (read_u16le(app4, m1 + 0x44U, &od)
                && read_u16le(app4, m1 + 0x46U, &rh)
                && read_u16le(app4, m1 + 0x48U, &em)
                && read_u16le(app4, m1 + 0x4aU, &rf)
                && read_u16le(app4, m1 + 0x4cU, &at)) {
                char scratch[64];
                const std::string_view ifd_name
                    = exif_internal::make_mk_subtable_ifd_token(
                        "mk_dji", "thermalparams", 0, std::span<char>(scratch));
                if (!ifd_name.empty()) {
                    const uint16_t tags_out[5]  = { 0x0044, 0x0046, 0x0048,
                                                    0x004a, 0x004c };
                    const MetaValue vals_out[5] = { make_u16(od), make_u16(rh),
                                                    make_u16(em), make_u16(rf),
                                                    make_u16(at) };
                    exif_internal::emit_bin_dir_entries(
                        ifd_name, store, std::span<const uint16_t>(tags_out, 5),
                        std::span<const MetaValue>(vals_out, 5), limits,
                        status_out);
                    any = true;
                }
            }
        }

        // 3) ThermalParams2 (float fields + IDString, no magic in observed files).
        // Try base offsets commonly seen in the wild.
        const uint64_t bases[2] = { 0U, 32U };
        for (uint32_t bi = 0; bi < 2; ++bi) {
            const uint64_t base = bases[bi];
            if (base + 0x14U > app4.size()) {
                continue;
            }

            uint32_t bits_at = 0;
            uint32_t bits_od = 0;
            uint32_t bits_em = 0;
            uint32_t bits_rh = 0;
            uint32_t bits_rt = 0;
            if (!read_u32le(app4, base + 0x00U, &bits_at)
                || !read_u32le(app4, base + 0x04U, &bits_od)
                || !read_u32le(app4, base + 0x08U, &bits_em)
                || !read_u32le(app4, base + 0x0cU, &bits_rh)
                || !read_u32le(app4, base + 0x10U, &bits_rt)) {
                continue;
            }

            const float at = f32_from_bits(bits_at);
            const float od = f32_from_bits(bits_od);
            const float em = f32_from_bits(bits_em);
            const float rh = f32_from_bits(bits_rh);
            const float rt = f32_from_bits(bits_rt);

            // Plausibility gates to avoid false positives on unrelated APP4 data.
            if (!float_plausible(at, -100.0f, 300.0f)
                || !float_plausible(rt, -100.0f, 300.0f)
                || !float_plausible(od, 0.0f, 10000.0f)
                || !float_plausible(em, 0.0f, 2.0f)
                || !float_plausible(rh, 0.0f, 1.0f)) {
                continue;
            }

            char scratch[64];
            const std::string_view ifd_name
                = exif_internal::make_mk_subtable_ifd_token(
                    "mk_dji", "thermalparams2", 0, std::span<char>(scratch));
            if (ifd_name.empty()) {
                break;
            }

            uint16_t tags_out[6] = { 0x0000, 0x0004, 0x0008,
                                     0x000c, 0x0010, 0x0065 };
            MetaValue vals_out[6]
                = { make_f32_bits(bits_at), make_f32_bits(bits_od),
                    make_f32_bits(bits_em), make_f32_bits(bits_rh),
                    make_f32_bits(bits_rt), MetaValue {} };

            if (base + 0x65U + 16U <= app4.size()) {
                const std::span<const std::byte> raw
                    = app4.subspan(static_cast<size_t>(base + 0x65U), 16U);
                size_t n = 0;
                while (n < raw.size() && raw[n] != std::byte { 0 }) {
                    n += 1;
                }
                const std::string_view s(reinterpret_cast<const char*>(
                                             raw.data()),
                                         n);
                vals_out[5] = make_text(store.arena(), s, TextEncoding::Ascii);
            }

            exif_internal::emit_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, 6),
                std::span<const MetaValue>(vals_out, 6), limits, status_out);
            any = true;
            break;
        }

        return any;
    }

    static bool parse_classic_tiff_header(std::span<const std::byte> bytes,
                                          TiffConfig* out_cfg,
                                          uint64_t* out_ifd0_off) noexcept
    {
        if (!out_cfg || !out_ifd0_off) {
            return false;
        }
        if (bytes.size() < 8) {
            return false;
        }

        const uint8_t b0 = u8(bytes[0]);
        const uint8_t b1 = u8(bytes[1]);
        const bool le    = (b0 == 'I' && b1 == 'I');
        const bool be    = (b0 == 'M' && b1 == 'M');
        if (!le && !be) {
            return false;
        }

        out_cfg->le      = le;
        out_cfg->bigtiff = false;

        uint16_t magic = 0;
        if (le) {
            if (!read_u16le(bytes, 2, &magic)) {
                return false;
            }
        } else {
            if (!read_u16be(bytes, 2, &magic)) {
                return false;
            }
        }
        if (magic != 42) {
            return false;
        }

        uint32_t ifd0_off = 0;
        if (le) {
            if (!read_u32le(bytes, 4, &ifd0_off)) {
                return false;
            }
        } else {
            if (!read_u32be(bytes, 4, &ifd0_off)) {
                return false;
            }
        }

        if (ifd0_off > static_cast<uint64_t>(bytes.size())) {
            return false;
        }
        *out_ifd0_off = static_cast<uint64_t>(ifd0_off);
        return true;
    }


    static void merge_exif_status(ExifDecodeStatus* out,
                                  ExifDecodeStatus in) noexcept
    {
        // Aggregate results across multiple EXIF blocks:
        // - Treat `Unsupported` as "no usable EXIF in this block".
        // - Promote to the worst non-Unsupported status seen.
        if (*out == ExifDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == ExifDecodeStatus::LimitExceeded) {
            *out = in;
            return;
        }
        if (*out == ExifDecodeStatus::Malformed) {
            return;
        }
        if (in == ExifDecodeStatus::Malformed) {
            *out = in;
            return;
        }
        if (*out == ExifDecodeStatus::OutputTruncated) {
            return;
        }
        if (in == ExifDecodeStatus::OutputTruncated) {
            *out = in;
            return;
        }
        if (*out == ExifDecodeStatus::Ok) {
            return;
        }
        if (in == ExifDecodeStatus::Ok) {
            *out = in;
            return;
        }
    }


    static void merge_xmp_status(XmpDecodeStatus* out,
                                 XmpDecodeStatus in) noexcept
    {
        if (!out) {
            return;
        }
        if (*out == XmpDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == XmpDecodeStatus::LimitExceeded) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::Malformed) {
            return;
        }
        if (in == XmpDecodeStatus::Malformed) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::OutputTruncated) {
            return;
        }
        if (in == XmpDecodeStatus::OutputTruncated) {
            *out = in;
            return;
        }
        // `Unsupported` means "no usable XMP in this block".
        // Promote to the best status seen across all XMP blocks.
        if (*out == XmpDecodeStatus::Ok) {
            return;
        }
        if (in == XmpDecodeStatus::Ok) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::Unsupported) {
            return;
        }
        if (in == XmpDecodeStatus::Unsupported) {
            *out = in;
        }
    }


    static void merge_jumbf_status(JumbfDecodeStatus* out,
                                   JumbfDecodeStatus in) noexcept
    {
        if (!out) {
            return;
        }
        if (*out == JumbfDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == JumbfDecodeStatus::LimitExceeded) {
            *out = in;
            return;
        }
        if (*out == JumbfDecodeStatus::Malformed) {
            return;
        }
        if (in == JumbfDecodeStatus::Malformed) {
            *out = in;
            return;
        }
        // `Unsupported` means "no decodable JUMBF in this block".
        if (*out == JumbfDecodeStatus::Ok) {
            return;
        }
        if (in == JumbfDecodeStatus::Ok) {
            *out = in;
            return;
        }
        if (*out == JumbfDecodeStatus::Unsupported) {
            return;
        }
        if (in == JumbfDecodeStatus::Unsupported) {
            *out = in;
        }
    }


    static uint8_t c2pa_verify_status_priority(C2paVerifyStatus status) noexcept
    {
        switch (status) {
        case C2paVerifyStatus::InvalidSignature: return 80U;
        case C2paVerifyStatus::VerificationFailed: return 70U;
        case C2paVerifyStatus::BackendUnavailable: return 60U;
        case C2paVerifyStatus::DisabledByBuild: return 50U;
        case C2paVerifyStatus::NoSignatures: return 40U;
        case C2paVerifyStatus::NotImplemented: return 30U;
        case C2paVerifyStatus::SignatureVerifiedOnly: return 25U;
        case C2paVerifyStatus::Verified: return 20U;
        case C2paVerifyStatus::NotRequested: return 0U;
        }
        return 0U;
    }


    static void merge_jumbf_result(JumbfDecodeResult* out,
                                   const JumbfDecodeResult& in) noexcept
    {
        if (!out) {
            return;
        }

        merge_jumbf_status(&out->status, in.status);
        out->boxes_decoded += in.boxes_decoded;
        out->cbor_items += in.cbor_items;
        out->entries_decoded += in.entries_decoded;

        const uint8_t old_priority = c2pa_verify_status_priority(
            out->verify_status);
        const uint8_t new_priority = c2pa_verify_status_priority(
            in.verify_status);
        if (new_priority > old_priority
            || (new_priority == old_priority
                && out->verify_status == C2paVerifyStatus::NotRequested
                && in.verify_status != C2paVerifyStatus::NotRequested)) {
            out->verify_status           = in.verify_status;
            out->verify_backend_selected = in.verify_backend_selected;
        }
    }


    static PayloadResult
    get_block_bytes(std::span<const std::byte> file_bytes,
                    std::span<const ContainerBlockRef> blocks,
                    uint32_t block_index, std::span<std::byte> payload,
                    std::span<uint32_t> payload_scratch_indices,
                    const PayloadOptions& payload_options,
                    std::span<const std::byte>* out) noexcept
    {
        PayloadResult res;
        if (!out || block_index >= blocks.size()) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        const ContainerBlockRef& block = blocks[block_index];

        if (block.part_count <= 1U
            && block.compression == BlockCompression::None
            && block.chunking != BlockChunking::GifSubBlocks) {
            const uint64_t end = static_cast<uint64_t>(file_bytes.size());
            if (block.data_offset > end
                || block.data_size > end - block.data_offset) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            *out = file_bytes.subspan(static_cast<size_t>(block.data_offset),
                                      static_cast<size_t>(block.data_size));
            res.status  = PayloadStatus::Ok;
            res.written = block.data_size;
            res.needed  = block.data_size;
            return res;
        }

        const PayloadResult payload_res
            = extract_payload(file_bytes, blocks, block_index, payload,
                              payload_scratch_indices, payload_options);
        if (payload_res.status != PayloadStatus::Ok) {
            return payload_res;
        }
        *out = std::span<const std::byte>(payload.data(),
                                          static_cast<size_t>(
                                              payload_res.written));
        return payload_res;
    }


    static bool looks_like_xmp_packet(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.empty()) {
            return false;
        }
        const size_t n = (bytes.size() < 8192U) ? bytes.size() : 8192U;
        const std::string_view head(reinterpret_cast<const char*>(bytes.data()),
                                    n);
        return head.find("<x:xmpmeta") != std::string_view::npos
               || head.find("<xmp:xmpmeta") != std::string_view::npos
               || head.find("<rdf:RDF") != std::string_view::npos
               || head.find("xmlns:rdf=") != std::string_view::npos;
    }


    static bool match_bytes(std::span<const std::byte> bytes, uint64_t offset,
                            const char* s, uint32_t n) noexcept
    {
        if (!s || offset + n > bytes.size()) {
            return false;
        }
        return std::memcmp(bytes.data() + static_cast<size_t>(offset), s,
                           static_cast<size_t>(n))
               == 0;
    }


    static uint64_t find_bytes(std::span<const std::byte> bytes, uint64_t begin,
                               uint64_t end, const char* s, uint32_t n) noexcept
    {
        if (!s || n == 0U) {
            return UINT64_MAX;
        }
        if (end > bytes.size()) {
            end = bytes.size();
        }
        if (begin > end || n > end - begin) {
            return UINT64_MAX;
        }
        for (uint64_t off = begin; off + n <= end; ++off) {
            if (match_bytes(bytes, off, s, n)) {
                return off;
            }
        }
        return UINT64_MAX;
    }


    static bool value_is_byte_payload(const MetaValue& value) noexcept
    {
        return value.kind == MetaValueKind::Bytes
               || (value.kind == MetaValueKind::Array
                   && value.elem_type == MetaElementType::U8);
    }


    static bool
    copy_arena_payload_to_scratch(const MetaStore& store, ByteSpan span,
                                  std::span<std::byte> scratch,
                                  std::span<const std::byte>* out) noexcept
    {
        if (!out) {
            return false;
        }
        const std::span<const std::byte> bytes = store.arena().span(span);
        if (bytes.empty() || bytes.size() > scratch.size()) {
            return false;
        }
        std::memcpy(scratch.data(), bytes.data(), bytes.size());
        *out = scratch.subspan(0U, bytes.size());
        return true;
    }


    static void decode_exif_carried_metadata(
        MetaStore& store, size_t entry_start, size_t entry_end,
        std::span<std::byte> payload,
        const SimpleMetaDecodeOptions& options) noexcept
    {
        std::array<ByteSpan, 8> icc_payloads {};
        std::array<ByteSpan, 8> iptc_payloads {};
        uint32_t icc_count  = 0;
        uint32_t iptc_count = 0;

        const std::span<const Entry> entries = store.entries();
        const size_t scan_end = (entry_end < entries.size()) ? entry_end
                                                             : entries.size();
        for (size_t ei = entry_start; ei < scan_end; ++ei) {
            const Entry& e = entries[ei];
            if (e.key.kind != MetaKeyKind::ExifTag
                || !value_is_byte_payload(e.value)
                || any(e.flags,
                       EntryFlags::Truncated | EntryFlags::Unreadable)) {
                continue;
            }
            if (e.key.data.exif_tag.tag == 0x8773U
                && icc_count < icc_payloads.size()) {
                icc_payloads[icc_count++] = e.value.data.span;
            } else if (e.key.data.exif_tag.tag == 0x83BBU
                       && iptc_count < iptc_payloads.size()) {
                iptc_payloads[iptc_count++] = e.value.data.span;
            }
        }

        for (uint32_t i = 0; i < icc_count; ++i) {
            std::span<const std::byte> bytes;
            if (!copy_arena_payload_to_scratch(store, icc_payloads[i], payload,
                                               &bytes)) {
                continue;
            }
            (void)decode_icc_profile(bytes, store, options.icc);
        }

        for (uint32_t i = 0; i < iptc_count; ++i) {
            std::span<const std::byte> bytes;
            if (!copy_arena_payload_to_scratch(store, iptc_payloads[i], payload,
                                               &bytes)) {
                continue;
            }
            (void)decode_iptc_iim(bytes, store, EntryFlags::None, options.iptc);
        }
    }


    static bool decode_embedded_xmp_packet(std::span<const std::byte> bytes,
                                           MetaStore& store,
                                           const XmpDecodeOptions& options,
                                           XmpDecodeResult* xmp) noexcept
    {
        if (!xmp) {
            return false;
        }

        static constexpr char kXmpSig[] = "http://ns.adobe.com/xap/1.0/\0";
        const uint64_t max_search = (bytes.size() < (32ULL * 1024ULL * 1024ULL))
                                        ? bytes.size()
                                        : (32ULL * 1024ULL * 1024ULL);
        const uint64_t sig_off
            = find_bytes(bytes, 0U, max_search, kXmpSig,
                         static_cast<uint32_t>(sizeof(kXmpSig) - 1U));
        if (sig_off == UINT64_MAX) {
            return false;
        }

        const uint64_t data_off = sig_off + (sizeof(kXmpSig) - 1U);
        if (data_off >= bytes.size()) {
            return false;
        }

        uint64_t packet_end = data_off + (512ULL * 1024ULL);
        if (packet_end > bytes.size()) {
            packet_end = bytes.size();
        }
        const std::span<const std::byte> packet
            = bytes.subspan(static_cast<size_t>(data_off),
                            static_cast<size_t>(packet_end - data_off));

        const XmpDecodeResult one
            = decode_xmp_packet(packet, store, EntryFlags::None, options);
        merge_xmp_status(&xmp->status, one.status);
        xmp->entries_decoded += one.entries_decoded;
        return one.entries_decoded > 0U;
    }

}  // namespace

SimpleMetaResult
simple_meta_read(std::span<const std::byte> file_bytes, MetaStore& store,
                 std::span<ContainerBlockRef> out_blocks,
                 std::span<ExifIfdRef> out_ifds, std::span<std::byte> payload,
                 std::span<uint32_t> payload_scratch_indices,
                 const SimpleMetaDecodeOptions& options) noexcept
{
    SimpleMetaResult result;
    store.constrain_resources(options.max_total_entries,
                              options.max_arena_bytes);
    if (store.resource_limit_exceeded()) {
        result.exif.status       = ExifDecodeStatus::LimitExceeded;
        result.exif.limit_reason = ExifLimitReason::MaxArenaBytes;
        result.exr.status        = ExrDecodeStatus::LimitExceeded;
        result.jumbf.status      = JumbfDecodeStatus::LimitExceeded;
        result.xmp.status        = XmpDecodeStatus::LimitExceeded;
        return result;
    }
    result.scan            = scan_auto(file_bytes, out_blocks);
    result.payload.status  = PayloadStatus::Ok;
    result.payload.written = 0;
    result.payload.needed  = 0;

    // Container-derived fields (currently: ISO-BMFF/HEIF/AVIF/CR3).
    bmff_internal::decode_bmff_derived_fields(file_bytes, store);

    ExifDecodeResult exif;
    exif.status          = ExifDecodeStatus::Unsupported;
    exif.ifds_written    = 0;
    exif.ifds_needed     = 0;
    exif.entries_decoded = 0;

    XmpDecodeResult xmp;
    xmp.status          = XmpDecodeStatus::Unsupported;
    xmp.entries_decoded = 0;

    JumbfDecodeResult jumbf;
    jumbf.status          = JumbfDecodeStatus::Unsupported;
    jumbf.boxes_decoded   = 0;
    jumbf.cbor_items      = 0;
    jumbf.entries_decoded = 0;

    ExrDecodeResult exr;
    exr.status          = ExrDecodeStatus::Unsupported;
    exr.parts_decoded   = 0;
    exr.entries_decoded = 0;

    uint32_t ifd_write_pos    = 0;
    uint32_t casio_qvci_index = 0;
    bool any_exif             = false;
    bool any_xmp              = false;
    if (raf_internal::looks_like_raf(file_bytes)) {
        const ExifDecodeResult raf
            = raf_internal::decode_raf_native(file_bytes, store,
                                              options.exif.limits);
        if (raf.status != ExifDecodeStatus::Unsupported
            || raf.entries_decoded > 0U) {
            any_exif = true;
            merge_exif_status(&exif.status, raf.status);
            exif.entries_decoded += raf.entries_decoded;
        }
    }
    if (x3f_internal::looks_like_x3f(file_bytes)) {
        const ExifDecodeResult x3f
            = x3f_internal::decode_x3f_native(file_bytes, store,
                                              options.exif.limits);
        if (x3f.status != ExifDecodeStatus::Unsupported
            || x3f.entries_decoded > 0U) {
            any_exif = true;
            merge_exif_status(&exif.status, x3f.status);
            exif.entries_decoded += x3f.entries_decoded;
        }
    }

    const uint32_t blocks_written = (result.scan.written < out_blocks.size())
                                        ? result.scan.written
                                        : static_cast<uint32_t>(
                                              out_blocks.size());
    const std::span<const ContainerBlockRef> blocks_view(out_blocks.data(),
                                                         static_cast<size_t>(
                                                             blocks_written));
    for (uint32_t i = 0; i < blocks_written; ++i) {
        const ContainerBlockRef& block = out_blocks[i];
        if (block.part_count > 1U && block.part_index != 0U) {
            continue;
        }

        std::span<const std::byte> block_bytes;
        const PayloadResult payload_one
            = get_block_bytes(file_bytes, blocks_view, i, payload,
                              payload_scratch_indices, options.payload,
                              &block_bytes);
        merge_payload_result(&result.payload, payload_one);
        if (payload_one.status != PayloadStatus::Ok) {
            if (block.kind == ContainerBlockKind::Exif
                || (block.kind == ContainerBlockKind::CompressedMetadata
                    && block.compression == BlockCompression::Brotli
                    && block.aux_u32 == fourcc('E', 'x', 'i', 'f'))) {
                switch (payload_one.status) {
                case PayloadStatus::Ok: break;
                case PayloadStatus::OutputTruncated:
                    merge_exif_status(&exif.status,
                                      ExifDecodeStatus::OutputTruncated);
                    break;
                case PayloadStatus::Unsupported:
                    merge_exif_status(&exif.status,
                                      ExifDecodeStatus::Unsupported);
                    break;
                case PayloadStatus::Malformed:
                    merge_exif_status(&exif.status,
                                      ExifDecodeStatus::Malformed);
                    break;
                case PayloadStatus::LimitExceeded:
                    merge_exif_status(&exif.status,
                                      ExifDecodeStatus::LimitExceeded);
                    break;
                }
            }
            if (block.kind == ContainerBlockKind::Xmp
                || block.kind == ContainerBlockKind::XmpExtended
                || (block.kind == ContainerBlockKind::CompressedMetadata
                    && block.compression == BlockCompression::Brotli
                    && block.aux_u32 == fourcc('x', 'm', 'l', ' '))) {
                switch (payload_one.status) {
                case PayloadStatus::Ok: break;
                case PayloadStatus::OutputTruncated:
                    merge_xmp_status(&xmp.status,
                                     XmpDecodeStatus::OutputTruncated);
                    break;
                case PayloadStatus::Unsupported:
                    merge_xmp_status(&xmp.status, XmpDecodeStatus::Unsupported);
                    break;
                case PayloadStatus::Malformed:
                    merge_xmp_status(&xmp.status, XmpDecodeStatus::Malformed);
                    break;
                case PayloadStatus::LimitExceeded:
                    merge_xmp_status(&xmp.status,
                                     XmpDecodeStatus::LimitExceeded);
                    break;
                }
            }
            if (block.kind == ContainerBlockKind::Jumbf
                || (block.kind == ContainerBlockKind::CompressedMetadata
                    && block.compression == BlockCompression::Brotli
                    && (block.aux_u32 == fourcc('j', 'u', 'm', 'b')
                        || block.aux_u32 == fourcc('c', '2', 'p', 'a')))) {
                switch (payload_one.status) {
                case PayloadStatus::Ok: break;
                case PayloadStatus::OutputTruncated:
                    merge_jumbf_status(&jumbf.status,
                                       JumbfDecodeStatus::LimitExceeded);
                    break;
                case PayloadStatus::Unsupported:
                    merge_jumbf_status(&jumbf.status,
                                       JumbfDecodeStatus::Unsupported);
                    break;
                case PayloadStatus::Malformed:
                    merge_jumbf_status(&jumbf.status,
                                       JumbfDecodeStatus::Malformed);
                    break;
                case PayloadStatus::LimitExceeded:
                    merge_jumbf_status(&jumbf.status,
                                       JumbfDecodeStatus::LimitExceeded);
                    break;
                }
            }
            continue;
        }

        if (block.kind == ContainerBlockKind::Exif) {
            // CR3: some Canon metadata is stored in a dedicated TIFF stream
            // (`CMT3`) rather than in the standard MakerNote tag (0x927C).
            // When MakerNote decoding is enabled, decode that directory as a
            // Canon MakerNote block and expand known BinaryData subtables.
            if (block.format == ContainerFormat::Cr3
                && block.id == fourcc('C', 'M', 'T', '3')) {
                if (!options.exif.decode_makernote) {
                    continue;
                }

                any_exif = true;

                TiffConfig cfg;
                uint64_t ifd0_off = 0;
                if (parse_classic_tiff_header(block_bytes, &cfg, &ifd0_off)
                    && ifd0_off < block_bytes.size()) {
                    ExifDecodeResult one;
                    one.status          = ExifDecodeStatus::Ok;
                    one.ifds_written    = 0;
                    one.ifds_needed     = 0;
                    one.entries_decoded = 0;

                    ExifDecodeOptions mn_opts        = options.exif;
                    mn_opts.decode_printim           = false;
                    mn_opts.decode_makernote         = false;
                    mn_opts.tokens.ifd_prefix        = "mk_canon";
                    mn_opts.tokens.subifd_prefix     = "mk_canon_subifd";
                    mn_opts.tokens.exif_ifd_token    = "mk_canon_exififd";
                    mn_opts.tokens.gps_ifd_token     = "mk_canon_gpsifd";
                    mn_opts.tokens.interop_ifd_token = "mk_canon_interopifd";

                    const uint64_t bytes_rem = static_cast<uint64_t>(
                        block_bytes.size() - static_cast<size_t>(ifd0_off));
                    if (exif_internal::decode_canon_makernote(
                            cfg, block_bytes, ifd0_off, bytes_rem, "mk_canon0",
                            store, mn_opts, &one)) {
                        merge_exif_status(&exif.status, one.status);
                        exif.entries_decoded += one.entries_decoded;
                        continue;
                    }

                    // Fallback: decode the TIFF stream into mk_canon* tags
                    // without BinaryData expansion.
                    std::span<ExifIfdRef> ifd_slice;
                    if (ifd_write_pos < out_ifds.size()) {
                        ifd_slice = out_ifds.subspan(ifd_write_pos);
                    }

                    const ExifDecodeResult fallback
                        = decode_exif_tiff(block_bytes, store, ifd_slice,
                                           mn_opts);
                    merge_exif_status(&exif.status, fallback.status);
                    exif.ifds_needed += fallback.ifds_needed;
                    exif.entries_decoded += fallback.entries_decoded;

                    const uint32_t room     = (ifd_write_pos < out_ifds.size())
                                                  ? static_cast<uint32_t>(
                                                    out_ifds.size()
                                                    - ifd_write_pos)
                                                  : 0U;
                    const uint32_t advanced = (fallback.ifds_written < room)
                                                  ? fallback.ifds_written
                                                  : room;
                    ifd_write_pos += advanced;
                    exif.ifds_written = ifd_write_pos;
                }
                continue;
            }

            any_exif = true;

            std::span<ExifIfdRef> ifd_slice;
            if (ifd_write_pos < out_ifds.size()) {
                ifd_slice = out_ifds.subspan(ifd_write_pos);
            }

            const size_t entry_start = store.entries().size();
            const ExifDecodeResult one
                = decode_exif_tiff(block_bytes, store, ifd_slice, options.exif);
            const size_t entry_end = store.entries().size();
            merge_exif_status(&exif.status, one.status);
            exif.ifds_needed += one.ifds_needed;
            exif.entries_decoded += one.entries_decoded;

            const uint32_t room     = (ifd_write_pos < out_ifds.size())
                                          ? static_cast<uint32_t>(out_ifds.size()
                                                                  - ifd_write_pos)
                                          : 0U;
            const uint32_t advanced = (one.ifds_written < room)
                                          ? one.ifds_written
                                          : room;
            ifd_write_pos += advanced;
            exif.ifds_written = ifd_write_pos;

            decode_exif_carried_metadata(store, entry_start, entry_end, payload,
                                         options);

            if (options.exif.decode_embedded_containers) {
                if (decode_embedded_xmp_packet(block_bytes, store, options.xmp,
                                               &xmp)) {
                    any_xmp = true;
                }
            }

            // Some TIFF-based RAW formats store an embedded JPEG preview as a
            // byte blob within a TIFF tag (for example, Panasonic RW2
            // `JpgFromRaw` tag 0x002E). ExifTool reports many common EXIF tags
            // from this preview; decode best-effort when enabled.
            if (options.exif.decode_embedded_containers
                && entry_end > entry_start) {
                // Phase 1: collect candidate blobs without mutating the arena.
                std::array<ByteSpan, 8> candidates {};
                uint32_t cand_count                  = 0;
                const std::span<const Entry> entries = store.entries();
                const size_t scan_end = (entry_end < entries.size())
                                            ? entry_end
                                            : entries.size();
                for (size_t ei = entry_start;
                     ei < scan_end && cand_count < candidates.size(); ++ei) {
                    const Entry& e = entries[ei];
                    if (e.key.kind != MetaKeyKind::ExifTag) {
                        continue;
                    }
                    if (e.key.data.exif_tag.tag != 0x002EU) {
                        continue;
                    }
                    if (any(e.flags,
                            EntryFlags::Truncated | EntryFlags::Unreadable)) {
                        continue;
                    }
                    const bool ok_kind
                        = (e.value.kind == MetaValueKind::Bytes)
                          || (e.value.kind == MetaValueKind::Array
                              && e.value.elem_type == MetaElementType::U8);
                    if (!ok_kind || e.value.count < 2) {
                        continue;
                    }
                    candidates[cand_count++] = e.value.data.span;
                }

                // Phase 2: copy + decode each embedded JPEG.
                for (uint32_t ci = 0; ci < cand_count; ++ci) {
                    const std::span<const std::byte> blob = store.arena().span(
                        candidates[ci]);
                    if (blob.size() < 2 || u8(blob[0]) != 0xFFU
                        || u8(blob[1]) != 0xD8U) {
                        continue;
                    }
                    if (blob.size() > payload.size()) {
                        merge_exif_status(&exif.status,
                                          ExifDecodeStatus::OutputTruncated);
                        continue;
                    }

                    std::memcpy(payload.data(), blob.data(), blob.size());
                    const std::span<const std::byte> jpeg_bytes(payload.data(),
                                                                blob.size());

                    std::array<ContainerBlockRef, 64> embed_blocks {};
                    const ScanResult scan_embed = scan_jpeg(jpeg_bytes,
                                                            embed_blocks);
                    if (scan_embed.status == ScanStatus::Malformed) {
                        merge_exif_status(&exif.status,
                                          ExifDecodeStatus::Malformed);
                        continue;
                    }
                    if (scan_embed.status == ScanStatus::OutputTruncated) {
                        merge_exif_status(&exif.status,
                                          ExifDecodeStatus::OutputTruncated);
                    }

                    const uint32_t embed_written
                        = (scan_embed.written < embed_blocks.size())
                              ? scan_embed.written
                              : static_cast<uint32_t>(embed_blocks.size());
                    for (uint32_t bi = 0; bi < embed_written; ++bi) {
                        const ContainerBlockRef& b = embed_blocks[bi];
                        if (b.part_count > 1U && b.part_index != 0U) {
                            continue;
                        }
                        if (b.data_offset > jpeg_bytes.size()
                            || b.data_size
                                   > jpeg_bytes.size() - b.data_offset) {
                            merge_exif_status(&exif.status,
                                              ExifDecodeStatus::Malformed);
                            continue;
                        }
                        const std::span<const std::byte> inner
                            = jpeg_bytes.subspan(
                                static_cast<size_t>(b.data_offset),
                                static_cast<size_t>(b.data_size));

                        if (b.kind == ContainerBlockKind::Exif) {
                            any_exif = true;

                            ExifDecodeOptions embed_opts = options.exif;
                            embed_opts.decode_printim    = false;
                            embed_opts.decode_embedded_containers = false;

                            std::span<ExifIfdRef> embed_ifds;
                            if (ifd_write_pos < out_ifds.size()) {
                                embed_ifds = out_ifds.subspan(ifd_write_pos);
                            }

                            const ExifDecodeResult inner_res
                                = decode_exif_tiff(inner, store, embed_ifds,
                                                   embed_opts);
                            merge_exif_status(&exif.status, inner_res.status);
                            exif.ifds_needed += inner_res.ifds_needed;
                            exif.entries_decoded += inner_res.entries_decoded;

                            const uint32_t inner_room
                                = (ifd_write_pos < out_ifds.size())
                                      ? static_cast<uint32_t>(out_ifds.size()
                                                              - ifd_write_pos)
                                      : 0U;
                            const uint32_t inner_advanced
                                = (inner_res.ifds_written < inner_room)
                                      ? inner_res.ifds_written
                                      : inner_room;
                            ifd_write_pos += inner_advanced;
                            exif.ifds_written = ifd_write_pos;
                        } else if (b.kind == ContainerBlockKind::Xmp) {
                            any_xmp                  = true;
                            const XmpDecodeResult xr = decode_xmp_packet(
                                inner, store, EntryFlags::None, options.xmp);
                            merge_xmp_status(&xmp.status, xr.status);
                            xmp.entries_decoded += xr.entries_decoded;
                        }
                    }
                }
            }
        } else if (block.kind == ContainerBlockKind::Mpf) {
            // JPEG APP2 MPF: TIFF-IFD stream used by MPO (multi-picture) files.
            // Decode as EXIF/TIFF tags into a separate IFD token namespace.
            std::array<ExifIfdRef, 64> mpf_ifds;
            ExifDecodeOptions mpf_options        = options.exif;
            mpf_options.tokens.ifd_prefix        = "mpf";
            mpf_options.tokens.subifd_prefix     = "mpf_subifd";
            mpf_options.tokens.exif_ifd_token    = "mpf_exififd";
            mpf_options.tokens.gps_ifd_token     = "mpf_gpsifd";
            mpf_options.tokens.interop_ifd_token = "mpf_interopifd";
            (void)decode_exif_tiff(block_bytes, store,
                                   std::span<ExifIfdRef>(mpf_ifds.data(),
                                                         mpf_ifds.size()),
                                   mpf_options);
        } else if (block.kind == ContainerBlockKind::Ciff) {
            any_exif = true;

            ExifDecodeResult one;
            one.status          = ExifDecodeStatus::Ok;
            one.ifds_written    = 0;
            one.ifds_needed     = 0;
            one.entries_decoded = 0;

            if (ciff_internal::decode_crw_ciff(block_bytes, store,
                                               options.exif.limits, &one)) {
                merge_exif_status(&exif.status, one.status);
                exif.entries_decoded += one.entries_decoded;
            } else {
                merge_exif_status(&exif.status, one.status);
            }
        } else if (block.kind == ContainerBlockKind::Xmp
                   || block.kind == ContainerBlockKind::XmpExtended) {
            any_xmp                   = true;
            const XmpDecodeResult one = decode_xmp_packet(block_bytes, store,
                                                          EntryFlags::None,
                                                          options.xmp);
            merge_xmp_status(&xmp.status, one.status);
            xmp.entries_decoded += one.entries_decoded;
        } else if (block.kind == ContainerBlockKind::Text) {
            const bool payload_decompressed = (block.compression
                                               == BlockCompression::None)
                                              || options.payload.decompress;
            (void)decode_png_text_block(file_bytes, block, block_bytes,
                                        payload_decompressed, store);
        } else if (block.kind == ContainerBlockKind::Comment) {
            (void)decode_comment_block(block_bytes, block, store);
        } else if (block.kind == ContainerBlockKind::Jumbf) {
            const JumbfDecodeResult one
                = decode_jumbf_payload(block_bytes, store, EntryFlags::None,
                                       options.jumbf);
            merge_jumbf_result(&jumbf, one);
        } else if (block.kind == ContainerBlockKind::Icc) {
            (void)decode_icc_profile(block_bytes, store, options.icc);
        } else if (block.kind == ContainerBlockKind::PhotoshopIrB) {
            (void)decode_photoshop_irb(block_bytes, store,
                                       options.photoshop_irb);
        } else if (block.kind == ContainerBlockKind::IptcIim) {
            (void)decode_iptc_iim(block_bytes, store, EntryFlags::None,
                                  options.iptc);
        } else if (block.kind == ContainerBlockKind::MakerNote) {
            if (!options.exif.decode_makernote) {
                continue;
            }

            // JPEG APP4: DJI thermal parameter blocks (and potentially other
            // vendor-specific metadata). Decode best-effort when recognized.
            if (block.format == ContainerFormat::Jpeg && block.id == 0xFFE4U) {
                ExifDecodeResult one;
                one.status          = ExifDecodeStatus::Ok;
                one.ifds_written    = 0;
                one.ifds_needed     = 0;
                one.entries_decoded = 0;

                if (parse_dji_thermal_params(block_bytes, store,
                                             options.exif.limits, &one)) {
                    any_exif = true;
                    merge_exif_status(&exif.status, one.status);
                    exif.entries_decoded += one.entries_decoded;
                }
            }

            // JPEG APP1 "QVCI" block found in some Casio files (QV-7000SX).
            if (block.format == ContainerFormat::Jpeg
                && block.aux_u32 == fourcc('Q', 'V', 'C', 'I')) {
                any_exif = true;

                ExifDecodeResult one;
                one.status          = ExifDecodeStatus::Ok;
                one.ifds_written    = 0;
                one.ifds_needed     = 0;
                one.entries_decoded = 0;

                char scratch[64];
                const std::string_view ifd_name
                    = exif_internal::make_mk_subtable_ifd_token(
                        "mk_casio", "qvci", casio_qvci_index++,
                        std::span<char>(scratch));
                if (ifd_name.empty()) {
                    continue;
                }

                (void)exif_internal::decode_casio_qvci(block_bytes, ifd_name,
                                                       store,
                                                       options.exif.limits,
                                                       &one);
                merge_exif_status(&exif.status, one.status);
                exif.entries_decoded += one.entries_decoded;
            }

            // JPEG APP1 "FLIR" multi-part stream containing an FFF/AFF payload.
            if (block.format == ContainerFormat::Jpeg
                && block.aux_u32 == fourcc('F', 'L', 'I', 'R')) {
                any_exif = true;

                ExifDecodeResult one;
                one.status          = ExifDecodeStatus::Ok;
                one.ifds_written    = 0;
                one.ifds_needed     = 0;
                one.entries_decoded = 0;

                if (exif_internal::decode_flir_fff(block_bytes, store,
                                                   options.exif.limits, &one)) {
                    merge_exif_status(&exif.status, one.status);
                    exif.entries_decoded += one.entries_decoded;
                }
            }
        } else if (block.kind == ContainerBlockKind::CompressedMetadata
                   && block.compression == BlockCompression::Brotli) {
            // JPEG XL "brob" box containing Brotli-compressed metadata payload.
            if (!options.payload.decompress) {
                continue;
            }

            if (block.aux_u32 == fourcc('E', 'x', 'i', 'f')) {
                // Exif box payload begins with a big-endian u32 TIFF offset.
                if (block_bytes.size() < 4) {
                    merge_exif_status(&exif.status,
                                      ExifDecodeStatus::Malformed);
                    continue;
                }
                uint32_t off = 0;
                if (!read_u32be(block_bytes, 0, &off)
                    || static_cast<uint64_t>(off) >= block_bytes.size()) {
                    merge_exif_status(&exif.status,
                                      ExifDecodeStatus::Malformed);
                    continue;
                }
                const std::span<const std::byte> tiff = block_bytes.subspan(
                    static_cast<size_t>(off),
                    static_cast<size_t>(block_bytes.size()
                                        - static_cast<size_t>(off)));

                any_exif = true;

                std::span<ExifIfdRef> ifd_slice;
                if (ifd_write_pos < out_ifds.size()) {
                    ifd_slice = out_ifds.subspan(ifd_write_pos);
                }

                const ExifDecodeResult one
                    = decode_exif_tiff(tiff, store, ifd_slice, options.exif);
                merge_exif_status(&exif.status, one.status);
                exif.ifds_needed += one.ifds_needed;
                exif.entries_decoded += one.entries_decoded;

                const uint32_t room     = (ifd_write_pos < out_ifds.size())
                                              ? static_cast<uint32_t>(
                                                out_ifds.size() - ifd_write_pos)
                                              : 0U;
                const uint32_t advanced = (one.ifds_written < room)
                                              ? one.ifds_written
                                              : room;
                ifd_write_pos += advanced;
                exif.ifds_written = ifd_write_pos;
            } else if (block.aux_u32 == fourcc('x', 'm', 'l', ' ')) {
                any_xmp = true;
                const XmpDecodeResult one
                    = decode_xmp_packet(block_bytes, store, EntryFlags::None,
                                        options.xmp);
                merge_xmp_status(&xmp.status, one.status);
                xmp.entries_decoded += one.entries_decoded;
            } else if (block.aux_u32 == fourcc('j', 'u', 'm', 'b')
                       || block.aux_u32 == fourcc('c', '2', 'p', 'a')) {
                const JumbfDecodeResult one
                    = decode_jumbf_payload(block_bytes, store, EntryFlags::None,
                                           options.jumbf);
                merge_jumbf_result(&jumbf, one);
            }
        }
    }

    // Standalone .xmp sidecar packets are containerless; scan_auto won't emit
    // dedicated XMP blocks for them. Decode directly when packet signatures are
    // present and no XMP block was seen.
    if (!any_xmp && looks_like_xmp_packet(file_bytes)) {
        any_xmp                   = true;
        const XmpDecodeResult one = decode_xmp_packet(file_bytes, store,
                                                      EntryFlags::None,
                                                      options.xmp);
        merge_xmp_status(&xmp.status, one.status);
        xmp.entries_decoded += one.entries_decoded;
    }

    if (!any_exif) {
        exif.status = ExifDecodeStatus::Unsupported;
    }
    if (!any_xmp) {
        xmp.status = XmpDecodeStatus::Unsupported;
    }

    exr = decode_exr_header(file_bytes, store, EntryFlags::None, options.exr);

    if (store.resource_limit_exceeded()) {
        exif.status       = ExifDecodeStatus::LimitExceeded;
        exif.limit_reason = ExifLimitReason::MaxArenaBytes;
        exr.status        = ExrDecodeStatus::LimitExceeded;
        jumbf.status      = JumbfDecodeStatus::LimitExceeded;
        xmp.status        = XmpDecodeStatus::LimitExceeded;
    }

    // If EXR decode succeeded, preserve "unsupported" EXIF/XMP statuses: EXR
    // metadata is a separate key space and may be the only metadata in file.
    result.exif  = exif;
    result.exr   = exr;
    result.jumbf = jumbf;
    result.xmp   = xmp;
    return result;
}

SimpleMetaResult
simple_meta_read(std::span<const std::byte> file_bytes, MetaStore& store,
                 std::span<ContainerBlockRef> out_blocks,
                 std::span<ExifIfdRef> out_ifds, std::span<std::byte> payload,
                 std::span<uint32_t> payload_scratch_indices,
                 const ExifDecodeOptions& exif_options,
                 const PayloadOptions& payload_options) noexcept
{
    SimpleMetaDecodeOptions options;
    options.exif    = exif_options;
    options.payload = payload_options;
    return simple_meta_read(file_bytes, store, out_blocks, out_ifds, payload,
                            payload_scratch_indices, options);
}

}  // namespace openmeta
