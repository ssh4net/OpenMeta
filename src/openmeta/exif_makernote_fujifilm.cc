// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>

namespace openmeta::exif_internal {

namespace {

    static bool make_source_subreader(SourceTiffReader* source, uint64_t offset,
                                      uint64_t size,
                                      RandomAccessSourceRange* range_out,
                                      SourceTiffReader* reader_out) noexcept
    {
        if (!source || !source->range || !source->result || !range_out
            || !reader_out || !source_tiff_contains(*source, offset, size)
            || source->range->source_offset > UINT64_MAX - offset) {
            return false;
        }

        *range_out = make_random_access_source_range(
            source->range->source, source->range->source_offset + offset, size);
        if (!random_access_source_range_valid(*range_out)) {
            return false;
        }

        reader_out->range          = range_out;
        reader_out->window.storage = source->window.storage;
        reader_out->value_scratch  = source->value_scratch;
        reader_out->limits         = source->limits;
        reader_out->window_options = source->window_options;
        reader_out->result         = source->result;
        return true;
    }

    static bool
    decode_fuji_signature_ifd(std::span<const std::byte> maker_note_bytes,
                              std::string_view mk_ifd0, MetaStore& store,
                              const ExifDecodeOptions& options,
                              ExifDecodeResult* status_out) noexcept
    {
        if (maker_note_bytes.size() < 12U) {
            return false;
        }
        if (!match_bytes(maker_note_bytes, 0, "FUJIFILM", 8)
            && !match_bytes(maker_note_bytes, 0, "GENERALE", 8)) {
            return false;
        }

        uint32_t ifd_off32 = 0;
        if (!read_u32le(maker_note_bytes, 8, &ifd_off32)) {
            return false;
        }

        const uint64_t ifd_off = ifd_off32;
        if (ifd_off >= maker_note_bytes.size()) {
            return false;
        }

        TiffConfig fuji_cfg;
        fuji_cfg.le      = true;
        fuji_cfg.bigtiff = false;
        decode_classic_ifd_no_header(fuji_cfg, maker_note_bytes, ifd_off,
                                     mk_ifd0, store, options, status_out,
                                     EntryFlags::None);
        return true;
    }

    static bool decode_fuji_ge_type2_ifd(
        std::span<const std::byte> tiff_bytes, uint64_t maker_note_off,
        uint64_t maker_note_bytes, std::string_view mk_ifd0, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
    {
        static constexpr char kGe2Magic[]        = "GE\x0C\0\0\0\x16\0\0\0";
        static constexpr uint16_t kGe2EntryCount = 25;

        if (maker_note_off > tiff_bytes.size()
            || maker_note_bytes > (tiff_bytes.size() - maker_note_off)) {
            return false;
        }

        const std::span<const std::byte> maker_note
            = tiff_bytes.subspan(static_cast<size_t>(maker_note_off),
                                 static_cast<size_t>(maker_note_bytes));
        if (maker_note.size() < 12U
            || !match_bytes(maker_note, 0, kGe2Magic, 10)) {
            return false;
        }

        TiffConfig fuji_cfg;
        fuji_cfg.le      = true;
        fuji_cfg.bigtiff = false;

        std::array<std::byte, 4096> patched;

        if (maker_note_off <= (UINT64_MAX - 6ULL) && maker_note_bytes >= 6ULL) {
            const uint64_t base0 = maker_note_off + 6ULL;
            const uint64_t n0    = maker_note_bytes - 6ULL;
            if (n0 >= 8ULL && n0 <= patched.size() && base0 <= tiff_bytes.size()
                && n0 <= (tiff_bytes.size() - base0)) {
                const std::span<const std::byte> src0
                    = tiff_bytes.subspan(static_cast<size_t>(base0),
                                         static_cast<size_t>(n0));
                std::memcpy(patched.data(), src0.data(), src0.size());
                patched[6] = std::byte { static_cast<uint8_t>(kGe2EntryCount
                                                              & 0xFFU) };
                patched[7] = std::byte { static_cast<uint8_t>(
                    (kGe2EntryCount >> 8) & 0xFFU) };

                decode_classic_ifd_no_header(
                    fuji_cfg,
                    std::span<const std::byte>(patched.data(),
                                               static_cast<size_t>(n0)),
                    6, mk_ifd0, store, options, status_out, EntryFlags::None);
            }
        }

        if (maker_note_off >= 204ULL
            && maker_note_bytes <= (UINT64_MAX - 204ULL)) {
            const uint64_t base1 = maker_note_off - 204ULL;
            const uint64_t n1    = maker_note_bytes + 204ULL;
            if (n1 >= 218ULL && n1 <= patched.size()
                && base1 <= tiff_bytes.size()
                && n1 <= (tiff_bytes.size() - base1)) {
                const std::span<const std::byte> src1
                    = tiff_bytes.subspan(static_cast<size_t>(base1),
                                         static_cast<size_t>(n1));
                std::memcpy(patched.data(), src1.data(), src1.size());
                patched[216] = std::byte { static_cast<uint8_t>(kGe2EntryCount
                                                                & 0xFFU) };
                patched[217] = std::byte { static_cast<uint8_t>(
                    (kGe2EntryCount >> 8) & 0xFFU) };

                decode_classic_ifd_no_header(
                    fuji_cfg,
                    std::span<const std::byte>(patched.data(),
                                               static_cast<size_t>(n1)),
                    216, mk_ifd0, store, options, status_out, EntryFlags::None);
            }
        }

        return true;
    }

    static bool decode_fuji_signature_ifd_from_source(
        SourceTiffReader* source, uint64_t maker_note_off,
        std::span<const std::byte> maker_note, std::string_view mk_ifd0,
        MetaStore& store, const ExifDecodeOptions& options,
        ExifDecodeResult* status_out) noexcept
    {
        if (maker_note.size() < 12U
            || (!match_bytes(maker_note, 0U, "FUJIFILM", 8U)
                && !match_bytes(maker_note, 0U, "GENERALE", 8U))) {
            return false;
        }

        uint32_t ifd_off32 = 0U;
        if (!read_u32le(maker_note, 8U, &ifd_off32)
            || ifd_off32 >= maker_note.size()) {
            return false;
        }

        RandomAccessSourceRange maker_range;
        SourceTiffReader maker_source;
        if (!make_source_subreader(source, maker_note_off, maker_note.size(),
                                   &maker_range, &maker_source)) {
            return false;
        }

        TiffConfig fuji_cfg;
        fuji_cfg.le      = true;
        fuji_cfg.bigtiff = false;
        OffsetPolicy offsets;
        const bool decoded
            = decode_classic_ifd_from_source(&maker_source, fuji_cfg, ifd_off32,
                                             offsets, mk_ifd0, store, options,
                                             status_out, EntryFlags::None);
        source->window.reset();
        return decoded;
    }

    static bool decode_fuji_ge_type2_ifd_from_source(
        SourceTiffReader* source, uint64_t maker_note_off,
        std::span<const std::byte> maker_note, std::string_view mk_ifd0,
        MetaStore& store, const ExifDecodeOptions& options,
        ExifDecodeResult* status_out) noexcept
    {
        static constexpr char kGe2Magic[]        = "GE\x0C\0\0\0\x16\0\0\0";
        static constexpr uint16_t kGe2EntryCount = 25U;
        if (maker_note.size() < 12U
            || !match_bytes(maker_note, 0U, kGe2Magic, 10U)) {
            return false;
        }

        TiffConfig fuji_cfg;
        fuji_cfg.le      = true;
        fuji_cfg.bigtiff = false;
        OffsetPolicy offsets;

        if (maker_note.size() >= 6U) {
            RandomAccessSourceRange local_range;
            SourceTiffReader local_source;
            if (make_source_subreader(source, maker_note_off + 6U,
                                      maker_note.size() - 6U, &local_range,
                                      &local_source)) {
                (void)decode_classic_ifd_from_source(&local_source, fuji_cfg,
                                                     6U, offsets, mk_ifd0,
                                                     store, options, status_out,
                                                     EntryFlags::None, false,
                                                     kGe2EntryCount);
                source->window.reset();
            }
        }

        if (maker_note_off >= 204U && maker_note.size() <= UINT64_MAX - 204U) {
            RandomAccessSourceRange outer_range;
            SourceTiffReader outer_source;
            if (make_source_subreader(source, maker_note_off - 204U,
                                      maker_note.size() + 204U, &outer_range,
                                      &outer_source)) {
                (void)decode_classic_ifd_from_source(&outer_source, fuji_cfg,
                                                     216U, offsets, mk_ifd0,
                                                     store, options, status_out,
                                                     EntryFlags::None, false,
                                                     kGe2EntryCount);
                source->window.reset();
            }
        }

        return true;
    }

}  // namespace

bool
decode_fuji_makernote(std::span<const std::byte> tiff_bytes,
                      uint64_t maker_note_off, uint64_t maker_note_bytes,
                      std::string_view mk_ifd0, MetaStore& store,
                      const ExifDecodeOptions& options,
                      ExifDecodeResult* status_out) noexcept
{
    if (maker_note_off > tiff_bytes.size()
        || maker_note_bytes > (tiff_bytes.size() - maker_note_off)) {
        return false;
    }

    if (decode_fuji_ge_type2_ifd(tiff_bytes, maker_note_off, maker_note_bytes,
                                 mk_ifd0, store, options, status_out)) {
        return true;
    }

    const std::span<const std::byte> maker_note
        = tiff_bytes.subspan(static_cast<size_t>(maker_note_off),
                             static_cast<size_t>(maker_note_bytes));
    return decode_fuji_signature_ifd(maker_note, mk_ifd0, store, options,
                                     status_out);
}

bool
decode_fuji_makernote_from_source(SourceTiffReader* source,
                                  uint64_t maker_note_off,
                                  std::span<const std::byte> maker_note,
                                  std::string_view mk_ifd0, MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept
{
    if (!source
        || !source_tiff_contains(*source, maker_note_off, maker_note.size())) {
        return false;
    }
    if (decode_fuji_ge_type2_ifd_from_source(source, maker_note_off, maker_note,
                                             mk_ifd0, store, options,
                                             status_out)) {
        return true;
    }
    return decode_fuji_signature_ifd_from_source(source, maker_note_off,
                                                 maker_note, mk_ifd0, store,
                                                 options, status_out);
}

}  // namespace openmeta::exif_internal
