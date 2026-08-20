// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace openmeta::exif_internal {
namespace {

    static bool score_source_flir_ifd(SourceTiffReader* source,
                                      const TiffConfig& cfg, uint64_t ifd_off,
                                      const ExifDecodeLimits& limits,
                                      ClassicIfdCandidate* out) noexcept
    {
        std::span<const std::byte> count_raw;
        if (!source || cfg.bigtiff
            || !source_tiff_view(source, ifd_off, 2U, &count_raw)) {
            return false;
        }
        uint16_t entry_count = 0U;
        if (!read_tiff_u16(cfg, count_raw, 0U, &entry_count)
            || entry_count == 0U || entry_count > 512U
            || entry_count > limits.max_entries_per_ifd) {
            return false;
        }
        const uint64_t table_bytes = uint64_t(entry_count) * 12U;
        if (ifd_off > UINT64_MAX - 2U || table_bytes > UINT64_MAX - 4U
            || !source_tiff_contains(*source, ifd_off + 2U, table_bytes)) {
            return false;
        }

        uint32_t valid = 0U;
        for (uint32_t i = 0U; i < entry_count; ++i) {
            std::span<const std::byte> raw;
            if (!source_tiff_view(source, ifd_off + 2U + uint64_t(i) * 12U, 12U,
                                  &raw)) {
                return false;
            }
            ClassicIfdEntry entry;
            if (!read_classic_ifd_entry(cfg, raw, 0U, &entry)) {
                return false;
            }
            const uint64_t unit = tiff_type_size(entry.type);
            if (unit == 0U || uint64_t(entry.count32) > UINT64_MAX / unit) {
                continue;
            }
            const uint64_t value_bytes = uint64_t(entry.count32) * unit;
            if (value_bytes > limits.max_value_bytes) {
                continue;
            }
            if (value_bytes <= 4U
                || source_tiff_contains(*source, entry.value_or_off32,
                                        value_bytes)) {
                valid += 1U;
            }
        }

        const uint32_t minimum = entry_count > 4U ? uint32_t(entry_count) / 2U
                                                  : uint32_t(entry_count);
        if (valid < minimum) {
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

    static bool read_u32_endian(bool le, std::span<const std::byte> bytes,
                                uint64_t offset, uint32_t* out) noexcept
    {
        return le ? read_u32le(bytes, offset, out)
                  : read_u32be(bytes, offset, out);
    }


    static bool read_u64_endian(bool le, std::span<const std::byte> bytes,
                                uint64_t offset, uint64_t* out) noexcept
    {
        if (!out || offset + 8U > bytes.size()) {
            return false;
        }

        const uint64_t b0 = static_cast<uint64_t>(u8(bytes[offset + 0]));
        const uint64_t b1 = static_cast<uint64_t>(u8(bytes[offset + 1]));
        const uint64_t b2 = static_cast<uint64_t>(u8(bytes[offset + 2]));
        const uint64_t b3 = static_cast<uint64_t>(u8(bytes[offset + 3]));
        const uint64_t b4 = static_cast<uint64_t>(u8(bytes[offset + 4]));
        const uint64_t b5 = static_cast<uint64_t>(u8(bytes[offset + 5]));
        const uint64_t b6 = static_cast<uint64_t>(u8(bytes[offset + 6]));
        const uint64_t b7 = static_cast<uint64_t>(u8(bytes[offset + 7]));

        if (le) {
            *out = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24) | (b4 << 32)
                   | (b5 << 40) | (b6 << 48) | (b7 << 56);
            return true;
        }

        *out = (b0 << 56) | (b1 << 48) | (b2 << 40) | (b3 << 32) | (b4 << 24)
               | (b5 << 16) | (b6 << 8) | (b7 << 0);
        return true;
    }


    static bool choose_endian_by_magic_u16(bool le,
                                           std::span<const std::byte> bytes,
                                           uint64_t offset, uint16_t magic,
                                           bool* out_le) noexcept
    {
        if (!out_le) {
            return false;
        }

        uint16_t v = 0;
        if (!read_u16_endian(le, bytes, offset, &v)) {
            return false;
        }

        if (v == magic) {
            *out_le = le;
            return true;
        }

        const uint16_t swapped = static_cast<uint16_t>((magic >> 8)
                                                       | (magic << 8));
        if (v == swapped) {
            *out_le = !le;
            return true;
        }

        *out_le = le;
        return true;
    }


    static void push_tag_value(uint16_t tag, const MetaValue& v,
                               std::span<uint16_t> tags,
                               std::span<MetaValue> vals,
                               uint32_t* count) noexcept
    {
        if (!count) {
            return;
        }
        const uint32_t n = *count;
        if (n >= tags.size() || n >= vals.size()) {
            return;
        }
        tags[n] = tag;
        vals[n] = v;
        *count  = n + 1;
    }


    static void push_u16_if_present(bool le, std::span<const std::byte> bytes,
                                    uint16_t tag, uint64_t offset,
                                    std::span<uint16_t> tags,
                                    std::span<MetaValue> vals,
                                    uint32_t* count) noexcept
    {
        uint16_t v = 0;
        if (!read_u16_endian(le, bytes, offset, &v)) {
            return;
        }
        push_tag_value(tag, make_u16(v), tags, vals, count);
    }


    static void push_i16_if_present(bool le, std::span<const std::byte> bytes,
                                    uint16_t tag, uint64_t offset,
                                    std::span<uint16_t> tags,
                                    std::span<MetaValue> vals,
                                    uint32_t* count) noexcept
    {
        int16_t v = 0;
        if (!read_i16_endian(le, bytes, offset, &v)) {
            return;
        }
        push_tag_value(tag, make_i16(v), tags, vals, count);
    }


    static void push_u32_if_present(bool le, std::span<const std::byte> bytes,
                                    uint16_t tag, uint64_t offset,
                                    std::span<uint16_t> tags,
                                    std::span<MetaValue> vals,
                                    uint32_t* count) noexcept
    {
        uint32_t v = 0;
        if (!read_u32_endian(le, bytes, offset, &v)) {
            return;
        }
        push_tag_value(tag, make_u32(v), tags, vals, count);
    }


    static void push_i32_if_present(bool le, std::span<const std::byte> bytes,
                                    uint16_t tag, uint64_t offset,
                                    std::span<uint16_t> tags,
                                    std::span<MetaValue> vals,
                                    uint32_t* count) noexcept
    {
        uint32_t v = 0;
        if (!read_u32_endian(le, bytes, offset, &v)) {
            return;
        }
        push_tag_value(tag, make_i32(static_cast<int32_t>(v)), tags, vals,
                       count);
    }


    static void push_f32_if_present(bool le, std::span<const std::byte> bytes,
                                    uint16_t tag, uint64_t offset,
                                    std::span<uint16_t> tags,
                                    std::span<MetaValue> vals,
                                    uint32_t* count) noexcept
    {
        uint32_t bits = 0;
        if (!read_u32_endian(le, bytes, offset, &bits)) {
            return;
        }
        push_tag_value(tag, make_f32_bits(bits), tags, vals, count);
    }


    static void push_f64_if_present(bool le, std::span<const std::byte> bytes,
                                    uint16_t tag, uint64_t offset,
                                    std::span<uint16_t> tags,
                                    std::span<MetaValue> vals,
                                    uint32_t* count) noexcept
    {
        uint64_t bits = 0;
        if (!read_u64_endian(le, bytes, offset, &bits)) {
            return;
        }
        push_tag_value(tag, make_f64_bits(bits), tags, vals, count);
    }


    static void push_ascii_if_present(MetaStore& store,
                                      std::span<const std::byte> bytes,
                                      uint16_t tag, uint64_t offset,
                                      uint32_t nbytes, std::span<uint16_t> tags,
                                      std::span<MetaValue> vals,
                                      uint32_t* count) noexcept
    {
        if (nbytes == 0) {
            return;
        }
        if (offset > bytes.size() || nbytes > (bytes.size() - offset)) {
            return;
        }
        push_tag_value(
            tag,
            make_fixed_ascii_text(store.arena(),
                                  bytes.subspan(static_cast<size_t>(offset),
                                                static_cast<size_t>(nbytes))),
            tags, vals, count);
    }


    static void push_bytes_if_present(MetaStore& store,
                                      const ExifDecodeLimits& limits,
                                      std::span<const std::byte> bytes,
                                      uint16_t tag, uint64_t offset,
                                      uint32_t nbytes, std::span<uint16_t> tags,
                                      std::span<MetaValue> vals,
                                      uint32_t* count) noexcept
    {
        if (nbytes == 0) {
            return;
        }
        if (nbytes > limits.max_value_bytes) {
            return;
        }
        if (offset > bytes.size() || nbytes > (bytes.size() - offset)) {
            return;
        }
        push_tag_value(tag,
                       make_bytes(store.arena(),
                                  bytes.subspan(static_cast<size_t>(offset),
                                                static_cast<size_t>(nbytes))),
                       tags, vals, count);
    }


    static bool decode_flir_header(std::span<const std::byte> fff,
                                   uint32_t index, MetaStore& store,
                                   const ExifDecodeLimits& limits,
                                   ExifDecodeResult* status_out) noexcept
    {
        if (fff.size() < 0x14U) {
            return false;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token("mk_flir", "fff_header", index,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return false;
        }

        const std::array<uint16_t, 1> tags  = { 0x0004u };
        const std::array<MetaValue, 1> vals = {
            make_fixed_ascii_text(store.arena(), fff.subspan(0x04, 16)),
        };
        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), tags.size()),
                             std::span<const MetaValue>(vals.data(),
                                                        vals.size()),
                             limits, status_out);
        return true;
    }


    static void decode_flir_paletteinfo(std::span<const std::byte> rec, bool le,
                                        uint32_t index, MetaStore& store,
                                        const ExifDecodeLimits& limits,
                                        ExifDecodeResult* status_out) noexcept
    {
        std::array<uint16_t, 32> tags;
        std::array<MetaValue, 32> vals;
        uint32_t n = 0;

        push_u16_if_present(le, rec, 0x0000u, 0x00, tags, vals, &n);

        if (rec.size() >= 0x06 + 3) {
            const std::array<uint8_t, 3> above = {
                u8(rec[0x06 + 0]),
                u8(rec[0x06 + 1]),
                u8(rec[0x06 + 2]),
            };
            push_tag_value(0x0006u, make_u8_array(store.arena(), above), tags,
                           vals, &n);
        }
        if (rec.size() >= 0x09 + 3) {
            const std::array<uint8_t, 3> below = {
                u8(rec[0x09 + 0]),
                u8(rec[0x09 + 1]),
                u8(rec[0x09 + 2]),
            };
            push_tag_value(0x0009u, make_u8_array(store.arena(), below), tags,
                           vals, &n);
        }
        if (rec.size() >= 0x0c + 3) {
            const std::array<uint8_t, 3> over = {
                u8(rec[0x0c + 0]),
                u8(rec[0x0c + 1]),
                u8(rec[0x0c + 2]),
            };
            push_tag_value(0x000Cu, make_u8_array(store.arena(), over), tags,
                           vals, &n);
        }
        if (rec.size() >= 0x0f + 3) {
            const std::array<uint8_t, 3> under = {
                u8(rec[0x0f + 0]),
                u8(rec[0x0f + 1]),
                u8(rec[0x0f + 2]),
            };
            push_tag_value(0x000Fu, make_u8_array(store.arena(), under), tags,
                           vals, &n);
        }
        if (rec.size() >= 0x12 + 3) {
            const std::array<uint8_t, 3> iso1 = {
                u8(rec[0x12 + 0]),
                u8(rec[0x12 + 1]),
                u8(rec[0x12 + 2]),
            };
            push_tag_value(0x0012u, make_u8_array(store.arena(), iso1), tags,
                           vals, &n);
        }
        if (rec.size() >= 0x15 + 3) {
            const std::array<uint8_t, 3> iso2 = {
                u8(rec[0x15 + 0]),
                u8(rec[0x15 + 1]),
                u8(rec[0x15 + 2]),
            };
            push_tag_value(0x0015u, make_u8_array(store.arena(), iso2), tags,
                           vals, &n);
        }

        if (rec.size() > 0x1a) {
            push_tag_value(0x001Au, make_u8(u8(rec[0x1a])), tags, vals, &n);
        }
        if (rec.size() > 0x1b) {
            push_tag_value(0x001Bu, make_u8(u8(rec[0x1b])), tags, vals, &n);
        }

        push_ascii_if_present(store, rec, 0x0030u, 0x30, 32, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0050u, 0x50, 32, tags, vals, &n);

        uint16_t colors = 0;
        if (read_u16_endian(le, rec, 0x00, &colors)) {
            const uint32_t palette_bytes = static_cast<uint32_t>(colors) * 3U;
            if (palette_bytes > 0 && rec.size() >= 0x70
                && palette_bytes <= limits.max_value_bytes
                && (0x70ULL + palette_bytes) <= rec.size()) {
                push_bytes_if_present(store, limits, rec, 0x0070u, 0x70,
                                      palette_bytes, tags, vals, &n);
            }
        }

        if (n == 0) {
            return;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token("mk_flir", "fff_paletteinfo", index,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n), limits,
                             status_out);
    }


    static void decode_flir_rawdata(std::span<const std::byte> rec,
                                    bool file_le, uint32_t index,
                                    MetaStore& store,
                                    const ExifDecodeLimits& limits,
                                    ExifDecodeResult* status_out) noexcept
    {
        bool le = file_le;
        (void)choose_endian_by_magic_u16(file_le, rec, 0, 0x0002u, &le);

        std::array<uint16_t, 8> tags;
        std::array<MetaValue, 8> vals;
        uint32_t n = 0;

        push_u16_if_present(le, rec, 0x0001u, 0x02, tags, vals, &n);
        push_u16_if_present(le, rec, 0x0002u, 0x04, tags, vals, &n);

        if (rec.size() >= 0x20) {
            std::string_view type                    = "DAT";
            const std::span<const std::byte> payload = rec.subspan(0x20);
            if (payload.size() >= 8 && payload[0] == std::byte { 0x89 }
                && payload[1] == std::byte { 'P' }
                && payload[2] == std::byte { 'N' }
                && payload[3] == std::byte { 'G' }
                && payload[4] == std::byte { 0x0D }
                && payload[5] == std::byte { 0x0A }
                && payload[6] == std::byte { 0x1A }
                && payload[7] == std::byte { 0x0A }) {
                type = "PNG";
            } else if (payload.size() >= 4
                       && ((payload[0] == std::byte { 'I' }
                            && payload[1] == std::byte { 'I' }
                            && payload[2] == std::byte { 0x2A }
                            && payload[3] == std::byte { 0x00 })
                           || (payload[0] == std::byte { 'M' }
                               && payload[1] == std::byte { 'M' }
                               && payload[2] == std::byte { 0x00 }
                               && payload[3] == std::byte { 0x2A }))) {
                type = "TIFF";
            }
            push_tag_value(0x0010u,
                           make_text(store.arena(), type, TextEncoding::Ascii),
                           tags, vals, &n);
        }

        if (n == 0) {
            return;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token("mk_flir", "fff_rawdata", index,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n), limits,
                             status_out);
    }


    static void decode_flir_embeddedimage(std::span<const std::byte> rec,
                                          bool file_le, uint32_t index,
                                          MetaStore& store,
                                          const ExifDecodeLimits& limits,
                                          ExifDecodeResult* status_out) noexcept
    {
        bool le = file_le;
        (void)choose_endian_by_magic_u16(file_le, rec, 0, 0x0003u, &le);

        std::array<uint16_t, 8> tags;
        std::array<MetaValue, 8> vals;
        uint32_t n = 0;

        push_u16_if_present(le, rec, 0x0001u, 0x02, tags, vals, &n);
        push_u16_if_present(le, rec, 0x0002u, 0x04, tags, vals, &n);

        if (rec.size() >= 0x24) {
            std::string_view type                    = "DAT";
            const std::span<const std::byte> payload = rec.subspan(0x20);
            if (payload.size() >= 4 && payload[0] == std::byte { 0x89 }
                && payload[1] == std::byte { 'P' }
                && payload[2] == std::byte { 'N' }
                && payload[3] == std::byte { 'G' }) {
                type = "PNG";
            } else if (payload.size() >= 3 && payload[0] == std::byte { 0xFF }
                       && payload[1] == std::byte { 0xD8 }
                       && payload[2] == std::byte { 0xFF }) {
                type = "JPG";
            }
            push_tag_value(0x0010u,
                           make_text(store.arena(), type, TextEncoding::Ascii),
                           tags, vals, &n);
        }

        if (n == 0) {
            return;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token("mk_flir", "fff_embeddedimage", index,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n), limits,
                             status_out);
    }


    static void decode_flir_pip(std::span<const std::byte> rec, uint32_t index,
                                MetaStore& store,
                                const ExifDecodeLimits& limits,
                                ExifDecodeResult* status_out) noexcept
    {
        constexpr bool le = true;

        std::array<uint16_t, 16> tags;
        std::array<MetaValue, 16> vals;
        uint32_t n = 0;

        push_f32_if_present(le, rec, 0x0000u, 0, tags, vals, &n);
        push_i16_if_present(le, rec, 0x0002u, 4, tags, vals, &n);
        push_i16_if_present(le, rec, 0x0003u, 6, tags, vals, &n);
        push_i16_if_present(le, rec, 0x0004u, 8, tags, vals, &n);
        push_i16_if_present(le, rec, 0x0005u, 10, tags, vals, &n);
        push_i16_if_present(le, rec, 0x0006u, 12, tags, vals, &n);
        push_i16_if_present(le, rec, 0x0007u, 14, tags, vals, &n);

        if (n == 0) {
            return;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token("mk_flir", "fff_pip", index,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n), limits,
                             status_out);
    }


    static void decode_flir_gpsinfo(std::span<const std::byte> rec,
                                    uint32_t index, MetaStore& store,
                                    const ExifDecodeLimits& limits,
                                    ExifDecodeResult* status_out) noexcept
    {
        constexpr bool le = true;

        std::array<uint16_t, 24> tags;
        std::array<MetaValue, 24> vals;
        uint32_t n = 0;

        push_u32_if_present(le, rec, 0x0000u, 0x00, tags, vals, &n);
        push_bytes_if_present(store, limits, rec, 0x0004u, 0x04, 4, tags, vals,
                              &n);
        push_ascii_if_present(store, rec, 0x0008u, 0x08, 2, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x000Au, 0x0A, 2, tags, vals, &n);
        push_f64_if_present(le, rec, 0x0010u, 0x10, tags, vals, &n);
        push_f64_if_present(le, rec, 0x0018u, 0x18, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0020u, 0x20, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0040u, 0x40, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0044u, 0x44, 2, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0046u, 0x46, 2, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0048u, 0x48, 2, tags, vals, &n);
        push_f32_if_present(le, rec, 0x004Cu, 0x4C, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0050u, 0x50, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0054u, 0x54, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0058u, 0x58, 16, tags, vals, &n);

        if (n == 0) {
            return;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token("mk_flir", "fff_gpsinfo", index,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n), limits,
                             status_out);
    }


    static void decode_flir_meterlink(std::span<const std::byte> rec,
                                      uint32_t index, MetaStore& store,
                                      const ExifDecodeLimits& limits,
                                      ExifDecodeResult* status_out) noexcept
    {
        constexpr bool le = true;

        std::array<uint16_t, 32> tags;
        std::array<MetaValue, 32> vals;
        uint32_t n = 0;

        push_u16_if_present(le, rec, 0x001Au, 26, tags, vals, &n);
        push_u16_if_present(le, rec, 0x001Cu, 28, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0020u, 32, 16, tags, vals, &n);
        push_f64_if_present(le, rec, 0x0060u, 96, tags, vals, &n);

        push_u16_if_present(le, rec, 0x007Eu, 126, tags, vals, &n);
        push_u16_if_present(le, rec, 0x0080u, 128, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0084u, 132, 16, tags, vals, &n);
        push_f64_if_present(le, rec, 0x00C4u, 196, tags, vals, &n);

        if (n == 0) {
            return;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token("mk_flir", "fff_meterlink", index,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n), limits,
                             status_out);
    }


    static void decode_flir_camerainfo(std::span<const std::byte> rec,
                                       bool file_le, uint32_t index,
                                       MetaStore& store,
                                       const ExifDecodeLimits& limits,
                                       ExifDecodeResult* status_out) noexcept
    {
        bool le = file_le;
        (void)choose_endian_by_magic_u16(file_le, rec, 0, 0x0002u, &le);

        std::array<uint16_t, 80> tags;
        std::array<MetaValue, 80> vals;
        uint32_t n = 0;

        push_f32_if_present(le, rec, 0x0020u, 0x20, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0024u, 0x24, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0028u, 0x28, tags, vals, &n);
        push_f32_if_present(le, rec, 0x002Cu, 0x2C, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0030u, 0x30, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0034u, 0x34, tags, vals, &n);
        push_f32_if_present(le, rec, 0x003Cu, 0x3C, tags, vals, &n);

        push_f32_if_present(le, rec, 0x0058u, 0x58, tags, vals, &n);
        push_f32_if_present(le, rec, 0x005Cu, 0x5C, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0060u, 0x60, tags, vals, &n);

        push_f32_if_present(le, rec, 0x0070u, 0x70, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0074u, 0x74, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0078u, 0x78, tags, vals, &n);
        push_f32_if_present(le, rec, 0x007Cu, 0x7C, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0080u, 0x80, tags, vals, &n);

        push_f32_if_present(le, rec, 0x0090u, 0x90, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0094u, 0x94, tags, vals, &n);
        push_f32_if_present(le, rec, 0x0098u, 0x98, tags, vals, &n);
        push_f32_if_present(le, rec, 0x009Cu, 0x9C, tags, vals, &n);
        push_f32_if_present(le, rec, 0x00A0u, 0xA0, tags, vals, &n);
        push_f32_if_present(le, rec, 0x00A4u, 0xA4, tags, vals, &n);
        push_f32_if_present(le, rec, 0x00A8u, 0xA8, tags, vals, &n);
        push_f32_if_present(le, rec, 0x00ACu, 0xAC, tags, vals, &n);

        push_ascii_if_present(store, rec, 0x00D4u, 0xD4, 32, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x00F4u, 0xF4, 16, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0104u, 0x104, 16, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0114u, 0x114, 16, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0170u, 0x170, 32, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x0190u, 0x190, 16, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x01A0u, 0x1A0, 16, tags, vals, &n);
        push_f32_if_present(le, rec, 0x01B4u, 0x1B4, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x01ECu, 0x1EC, 16, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x01FCu, 0x1FC, 32, tags, vals, &n);
        push_ascii_if_present(store, rec, 0x021Cu, 0x21C, 32, tags, vals, &n);

        push_i32_if_present(le, rec, 0x0308u, 0x308, tags, vals, &n);
        push_f32_if_present(le, rec, 0x030Cu, 0x30C, tags, vals, &n);
        push_u16_if_present(le, rec, 0x0310u, 0x310, tags, vals, &n);
        push_u16_if_present(le, rec, 0x0312u, 0x312, tags, vals, &n);
        push_u16_if_present(le, rec, 0x0338u, 0x338, tags, vals, &n);
        push_u16_if_present(le, rec, 0x033Cu, 0x33C, tags, vals, &n);

        push_bytes_if_present(store, limits, rec, 0x0384u, 0x384, 10, tags,
                              vals, &n);

        push_u16_if_present(le, rec, 0x0390u, 0x390, tags, vals, &n);
        push_f32_if_present(le, rec, 0x045Cu, 0x45C, tags, vals, &n);
        push_u16_if_present(le, rec, 0x0464u, 0x464, tags, vals, &n);

        if (n == 0) {
            return;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token("mk_flir", "fff_camerainfo", index,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n), limits,
                             status_out);
    }

}  // namespace

bool
decode_flir_fff(std::span<const std::byte> fff_bytes, MetaStore& store,
                const ExifDecodeLimits& limits,
                ExifDecodeResult* status_out) noexcept
{
    if (fff_bytes.size() < 0x40U) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return false;
    }

    if (!match_bytes(fff_bytes, 0, "FFF\0", 4)
        && !match_bytes(fff_bytes, 0, "AFF\0", 4)) {
        return false;
    }

    uint32_t ver_be = 0;
    uint32_t ver_le = 0;
    if (!read_u32be(fff_bytes, 0x14, &ver_be)
        || !read_u32le(fff_bytes, 0x14, &ver_le)) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return true;
    }

    bool le = false;
    if (ver_be >= 100U && ver_be < 200U) {
        le = false;
    } else if (ver_le >= 100U && ver_le < 200U) {
        le = true;
    } else {
        update_status(status_out, ExifDecodeStatus::Unsupported);
        return true;
    }

    uint32_t dir_off32 = 0;
    uint32_t dir_num32 = 0;
    if (!read_u32_endian(le, fff_bytes, 0x18, &dir_off32)
        || !read_u32_endian(le, fff_bytes, 0x1C, &dir_num32)) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return true;
    }

    const uint64_t dir_off = dir_off32;
    const uint64_t dir_num = dir_num32;
    if (dir_off > fff_bytes.size()) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return true;
    }
    if (dir_num > (UINT64_MAX / 0x20ULL)) {
        update_status(status_out, ExifDecodeStatus::LimitExceeded);
        return true;
    }

    const uint64_t dir_bytes = dir_num * 0x20ULL;
    if (dir_bytes > (fff_bytes.size() - dir_off)) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return true;
    }

    (void)decode_flir_header(fff_bytes, 0, store, limits, status_out);

    uint32_t embedded_idx = 0;
    uint32_t raw_idx      = 0;
    uint32_t pip_idx      = 0;
    uint32_t camera_idx   = 0;
    uint32_t gps_idx      = 0;
    uint32_t meter_idx    = 0;
    uint32_t palette_idx  = 0;

    for (uint32_t i = 0; i < dir_num; ++i) {
        const uint64_t entry_off = dir_off + uint64_t(i) * 0x20ULL;

        uint16_t rec_type = 0;
        if (!read_u16_endian(le, fff_bytes, entry_off + 0x00, &rec_type)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return true;
        }
        if (rec_type == 0) {
            continue;
        }

        uint32_t rec_off32 = 0;
        uint32_t rec_len32 = 0;
        if (!read_u32_endian(le, fff_bytes, entry_off + 0x0C, &rec_off32)
            || !read_u32_endian(le, fff_bytes, entry_off + 0x10, &rec_len32)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return true;
        }

        const uint64_t rec_off = rec_off32;
        const uint64_t rec_len = rec_len32;
        if (rec_off > fff_bytes.size()
            || rec_len > (fff_bytes.size() - rec_off)) {
            continue;
        }
        if (rec_len > limits.max_value_bytes) {
            continue;
        }

        const std::span<const std::byte> rec
            = fff_bytes.subspan(static_cast<size_t>(rec_off),
                                static_cast<size_t>(rec_len));

        switch (rec_type) {
        case 0x0001u:
            decode_flir_rawdata(rec, le, raw_idx++, store, limits, status_out);
            break;
        case 0x000Eu:
            decode_flir_embeddedimage(rec, le, embedded_idx++, store, limits,
                                      status_out);
            break;
        case 0x0020u:
            decode_flir_camerainfo(rec, le, camera_idx++, store, limits,
                                   status_out);
            break;
        case 0x0022u:
            decode_flir_paletteinfo(rec, le, palette_idx++, store, limits,
                                    status_out);
            break;
        case 0x002Au:
            decode_flir_pip(rec, pip_idx++, store, limits, status_out);
            break;
        case 0x002Bu:
            decode_flir_gpsinfo(rec, gps_idx++, store, limits, status_out);
            break;
        case 0x002Cu:
            decode_flir_meterlink(rec, meter_idx++, store, limits, status_out);
            break;
        default: break;
        }
    }

    return true;
}


bool
decode_flir_makernote(const TiffConfig& parent_cfg,
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
    if (maker_note_bytes < 8) {
        return false;
    }

    ClassicIfdCandidate best;
    bool found = false;

    for (int endian = 0; endian < 2; ++endian) {
        TiffConfig cfg = parent_cfg;
        cfg.le         = (endian == 0) ? parent_cfg.le : !parent_cfg.le;
        cfg.bigtiff    = false;

        ClassicIfdCandidate cand;
        if (!score_classic_ifd_candidate(cfg, tiff_bytes, maker_note_off,
                                         options.limits, &cand)) {
            continue;
        }

        if (!found || cand.valid_entries > best.valid_entries
            || (cand.valid_entries == best.valid_entries
                && cand.offset < best.offset)) {
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

    decode_classic_ifd_no_header(best_cfg, tiff_bytes, maker_note_off, mk_ifd0,
                                 store, options, status_out, EntryFlags::None);
    return true;
}


bool
decode_flir_makernote_from_source(SourceTiffReader* source,
                                  const TiffConfig& parent_cfg,
                                  uint64_t maker_note_off,
                                  std::span<const std::byte> maker_note,
                                  std::string_view mk_ifd0, MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept
{
    if (!source || !source->result || mk_ifd0.empty() || maker_note.size() < 8U
        || !source_tiff_contains(*source, maker_note_off, maker_note.size())) {
        return false;
    }

    ClassicIfdCandidate best;
    TiffConfig best_cfg;
    bool found = false;
    for (uint32_t endian = 0U; endian < 2U; ++endian) {
        TiffConfig cfg = parent_cfg;
        cfg.le         = endian == 0U ? parent_cfg.le : !parent_cfg.le;
        cfg.bigtiff    = false;
        ClassicIfdCandidate candidate;
        if (!score_source_flir_ifd(source, cfg, maker_note_off, options.limits,
                                   &candidate)) {
            continue;
        }
        if (!found || candidate.valid_entries > best.valid_entries) {
            best     = candidate;
            best_cfg = cfg;
            found    = true;
        }
    }
    if (!found) {
        // Some early FLIR files contain a structurally recognizable directory
        // whose advertised value offsets are all outside the EXIF block. The
        // contiguous decoder preserves that payload as opaque metadata too.
        for (uint32_t endian = 0U; endian < 2U; ++endian) {
            TiffConfig cfg = parent_cfg;
            cfg.le         = endian == 0U ? parent_cfg.le : !parent_cfg.le;
            cfg.bigtiff    = false;
            uint16_t count = 0U;
            if (read_tiff_u16(cfg, maker_note, 0U, &count) && count != 0U
                && count <= options.limits.max_entries_per_ifd
                && uint64_t(count) <= (maker_note.size() - 2U) / 12U) {
                return true;
            }
        }
        return true;
    }

    OffsetPolicy offsets;
    return decode_classic_ifd_from_source(source, best_cfg, maker_note_off,
                                          offsets, mk_ifd0, store, options,
                                          status_out, EntryFlags::None, true);
}

}  // namespace openmeta::exif_internal
