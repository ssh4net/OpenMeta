// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>
#include <vector>

namespace openmeta::exif_internal {
namespace {

    static void maybe_mark_ricoh_main_contextual_name(Entry* entry) noexcept
    {
        if (!entry || entry->key.kind != MetaKeyKind::ExifTag) {
            return;
        }

        const uint16_t tag  = entry->key.data.exif_tag.tag;
        const bool is_short = entry->origin.wire_type.family == WireFamily::Tiff
                              && entry->origin.wire_type.code == 3U;
        if ((tag == 0x1002U || tag == 0x1004U) && !is_short) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::RicohMainCompat;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (tag == 0x1003U && is_short) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::RicohMainCompat;
            entry->origin.name_context_variant = 2U;
        }
    }

    static uint32_t score_ascii_blob(std::span<const std::byte> raw) noexcept;
    static uint32_t
    score_ricoh_faceinfo_blob(std::span<const std::byte> raw) noexcept;

    static void decode_ricoh_serialinfo(std::string_view mk_prefix,
                                        std::span<const std::byte> raw,
                                        MetaStore& store,
                                        const ExifDecodeOptions& options,
                                        ExifDecodeResult* status_out) noexcept
    {
        if (mk_prefix.empty() || raw.size() < 16U) {
            return;
        }
        if (raw.size() > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "serialinfo", 0,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        static constexpr uint16_t kTags[] = { 0x0000U, 0x0010U, 0x0020U,
                                              0x0030U };
        for (uint32_t i = 0; i < sizeof(kTags) / sizeof(kTags[0]); ++i) {
            const uint16_t tag = kTags[i];
            const size_t off   = static_cast<size_t>(tag);
            if (off >= raw.size()) {
                continue;
            }

            const size_t avail = raw.size() - off;
            const size_t count = (avail < 16U) ? avail : 16U;
            const std::span<const std::byte> field = raw.subspan(off, count);

            size_t text_len = 0;
            while (text_len < field.size()
                   && field[text_len] != std::byte { 0 }) {
                ++text_len;
            }
            while (text_len > 0U && field[text_len - 1U] == std::byte { ' ' }) {
                --text_len;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Other, 1 };
            entry.origin.wire_count     = static_cast<uint32_t>(count);
            entry.flags |= EntryFlags::Derived;
            entry.value = make_text(
                store.arena(),
                std::string_view(reinterpret_cast<const char*>(field.data()),
                                 text_len),
                TextEncoding::Ascii);

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1U;
            }
        }
    }

    static bool find_ricoh_header_marker(std::span<const std::byte> bytes,
                                         uint64_t* out_offset) noexcept
    {
        if (!out_offset) {
            return false;
        }
        *out_offset = UINT64_MAX;

        static constexpr char kHdr[]          = "[Ricoh Camera Info]";
        static constexpr size_t kLen          = sizeof(kHdr) - 1U;
        static constexpr unsigned char kFirst = static_cast<unsigned char>(
            kHdr[0]);

        if (bytes.size() < kLen) {
            return false;
        }

        const std::byte* const begin = bytes.data();
        const std::byte* p           = begin;
        size_t remaining             = bytes.size();
        while (remaining >= kLen) {
            const void* hit = std::memchr(p, kFirst, remaining - kLen + 1U);
            if (!hit) {
                return false;
            }
            const std::byte* const candidate = static_cast<const std::byte*>(
                hit);
            const size_t index = static_cast<size_t>(candidate - begin);
            if (match_bytes(bytes, static_cast<uint64_t>(index), kHdr,
                            static_cast<uint32_t>(kLen))) {
                *out_offset = static_cast<uint64_t>(index);
                return true;
            }
            p         = candidate + 1;
            remaining = bytes.size() - static_cast<size_t>(p - begin);
        }
        return false;
    }

    static bool decode_ricoh_type2_ricoh_header_ifd(
        std::span<const std::byte> mn, std::string_view mk_prefix,
        MetaStore& store, const ExifDecodeOptions& options,
        ExifDecodeResult* status_out) noexcept
    {
        // ExifTool Ricoh::Type2: MakerNote data begins with "RICOH\0", followed by a
        // little-endian IFD-like structure (with occasional padding/format errors).
        if (mn.size() < 16 || mk_prefix.empty()) {
            return false;
        }
        if (!match_bytes(mn, 0, "RICOH", 5)) {
            return false;
        }

        // Entry count is at offset 8 for this structure (little-endian).
        TiffConfig cfg;
        cfg.bigtiff = false;
        cfg.le      = true;

        if (mn.size() < 12) {
            return false;
        }

        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, mn, 8, &entry_count)) {
            return false;
        }
        if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd
            || entry_count > 4096) {
            return false;
        }

        // Most samples include 2 bytes of padding after the entry count.
        const uint64_t entries_off = 12;
        const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
        const uint64_t needed      = entries_off + table_bytes + 4ULL;
        if (needed > mn.size()) {
            return false;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "type2", 0,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return false;
        }

        MakerNoteLayout layout;
        layout.cfg                      = cfg;
        layout.bytes                    = mn;
        layout.offsets.out_of_line_base = 0;

        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            ClassicIfdEntry e;
            if (!read_classic_ifd_entry(cfg, mn, eoff, &e)) {
                return true;
            }

            const uint64_t count = e.count32;

            ClassicIfdValueRef ref;
            const bool have_ref = resolve_classic_ifd_value_ref(layout, eoff, e,
                                                                &ref,
                                                                status_out);

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, e.tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, e.type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);

            if (!have_ref) {
                entry.flags |= EntryFlags::Unreadable;
            } else if (ref.value_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                entry.flags |= EntryFlags::Truncated;
            } else if (ref.value_off + ref.value_bytes > mn.size()) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                entry.flags |= EntryFlags::Unreadable;
            } else {
                entry.value = decode_tiff_value(cfg, mn, e.type, count,
                                                ref.value_off, ref.value_bytes,
                                                store.arena(), options.limits,
                                                status_out);
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }

        return true;
    }

    static bool
    decode_ricoh_type2_padded_ifd(std::span<const std::byte> mn,
                                  std::string_view mk_prefix, MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept
    {
        if (mn.size() < 16 || mk_prefix.empty()) {
            return false;
        }

        const uint8_t b0 = u8(mn[0]);
        const uint8_t b1 = u8(mn[1]);
        if (!((b0 == 'I' && b1 == 'I') || (b0 == 'M' && b1 == 'M'))) {
            return false;
        }

        TiffConfig cfg;
        cfg.bigtiff = false;
        cfg.le      = (b0 == 'I');

        uint16_t version = 0;
        if (!read_tiff_u16(cfg, mn, 2, &version) || version != 42) {
            return false;
        }

        uint32_t ifd0_off32 = 0;
        if (!read_tiff_u32(cfg, mn, 4, &ifd0_off32)) {
            return false;
        }
        const uint64_t ifd0_off = ifd0_off32;
        if (ifd0_off == 0 || ifd0_off + 8 > mn.size()) {
            return false;
        }

        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, mn, ifd0_off, &entry_count)) {
            return false;
        }
        if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd
            || entry_count > 4096) {
            return false;
        }

        // Some Ricoh "Type2" maker notes have an extra 2 bytes of padding after
        // the entry count. Others appear to be standard IFDs.
        const bool padded
            = (mn[static_cast<size_t>(ifd0_off + 2)] == std::byte { 0 })
              && (mn[static_cast<size_t>(ifd0_off + 3)] == std::byte { 0 });

        const uint64_t entries_off = ifd0_off + (padded ? 4 : 2);
        const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
        const uint64_t needed      = entries_off + table_bytes + 4ULL;
        if (needed > mn.size()) {
            return false;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "type2", 0,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return false;
        }

        MakerNoteLayout layout;
        layout.cfg                      = cfg;
        layout.bytes                    = mn;
        layout.offsets.out_of_line_base = 0;

        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            ClassicIfdEntry e;
            if (!read_classic_ifd_entry(cfg, mn, eoff, &e)) {
                return true;
            }

            const uint64_t count = e.count32;

            ClassicIfdValueRef ref;
            const bool have_ref = resolve_classic_ifd_value_ref(layout, eoff, e,
                                                                &ref,
                                                                status_out);

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, e.tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, e.type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);

            if (!have_ref) {
                entry.flags |= EntryFlags::Unreadable;
            } else if (ref.value_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                entry.flags |= EntryFlags::Truncated;
            } else if (ref.value_off + ref.value_bytes > mn.size()) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                entry.flags |= EntryFlags::Unreadable;
            } else {
                entry.value = decode_tiff_value(cfg, mn, e.type, count,
                                                ref.value_off, ref.value_bytes,
                                                store.arena(), options.limits,
                                                status_out);
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }

        return true;
    }

    static void decode_ricoh_main_ifd_with_fallback_offsets(
        const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
        std::span<const std::byte> mn, uint64_t ifd_off, uint64_t base,
        std::string_view ifd_name, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty()) {
            return;
        }
        if (ifd_off + 2 > mn.size()) {
            return;
        }

        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, mn, ifd_off, &entry_count)) {
            return;
        }
        if (entry_count == 0
            || entry_count > options.limits.max_entries_per_ifd) {
            return;
        }

        const uint64_t entries_off = ifd_off + 2;
        const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
        if (entries_off + table_bytes + 4ULL > mn.size()) {
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        MakerNoteLayout layout0;
        layout0.cfg                      = cfg;
        layout0.bytes                    = mn;
        layout0.offsets.out_of_line_base = 0;

        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            ClassicIfdEntry e;
            if (!read_classic_ifd_entry(cfg, mn, eoff, &e)) {
                return;
            }

            ClassicIfdValueRef ref0;
            if (!resolve_classic_ifd_value_ref(layout0, eoff, e, &ref0,
                                               status_out)) {
                continue;
            }

            const uint16_t tag         = e.tag;
            const uint16_t type        = e.type;
            const uint64_t count       = e.count32;
            const uint64_t value_bytes = ref0.value_bytes;

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);

            if (value_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                entry.flags |= EntryFlags::Truncated;
            } else if (ref0.inline_value) {
                entry.value = decode_tiff_value(cfg, mn, type, count,
                                                ref0.value_off, value_bytes,
                                                store.arena(), options.limits,
                                                status_out);
            } else {
                // Ricoh MakerNotes commonly store offsets relative to Start=$valuePtr+8,
                // but there are real-world variants:
                // - offsets relative to the MakerNote start ($valuePtr)
                // - offsets relative to Start=$valuePtr+8
                // - absolute offsets relative to the outer EXIF/TIFF header
                const uint64_t off_rel = static_cast<uint64_t>(
                    e.value_or_off32);

                bool decoded = false;

                uint64_t off_mn_base = UINT64_MAX;
                uint64_t off_mn_0    = off_rel;

                const bool have_mn_0 = (off_mn_0 + value_bytes <= mn.size());

                bool have_mn_base = false;
                if (base <= (UINT64_MAX - off_rel)) {
                    off_mn_base  = base + off_rel;
                    have_mn_base = (off_mn_base + value_bytes <= mn.size());
                }

                const bool have_abs = (off_rel + value_bytes
                                       <= tiff_bytes.size());

                uint32_t score_base = 0;
                uint32_t score_0    = 0;
                uint32_t score_abs  = 0;
                if (type == 2 /* ASCII */ || type == 129 /* UTF-8 */) {
                    if (have_mn_base) {
                        score_base = score_ascii_blob(
                            mn.subspan(static_cast<size_t>(off_mn_base),
                                       static_cast<size_t>(value_bytes)));
                    }
                    if (have_mn_0) {
                        score_0 = score_ascii_blob(
                            mn.subspan(static_cast<size_t>(off_mn_0),
                                       static_cast<size_t>(value_bytes)));
                    }
                    if (have_abs) {
                        score_abs = score_ascii_blob(tiff_bytes.subspan(
                            static_cast<size_t>(off_rel),
                            static_cast<size_t>(value_bytes)));
                    }
                }

                // Prefer the candidate with the best string score for text types.
                if ((type == 2 || type == 129)
                    && (score_base || score_0 || score_abs)) {
                    if (score_base >= score_0 && score_base >= score_abs
                        && have_mn_base) {
                        entry.value = decode_tiff_value(
                            cfg, mn, type, count, off_mn_base, value_bytes,
                            store.arena(), options.limits, status_out);
                        decoded = true;
                    } else if (score_0 >= score_abs && have_mn_0) {
                        entry.value
                            = decode_tiff_value(cfg, mn, type, count, off_mn_0,
                                                value_bytes, store.arena(),
                                                options.limits, status_out);
                        decoded = true;
                    } else if (have_abs) {
                        entry.value = decode_tiff_value(
                            cfg, tiff_bytes, type, count, off_rel, value_bytes,
                            store.arena(), options.limits, status_out);
                        decoded = true;
                    }
                }

                if (!decoded && have_mn_base) {
                    entry.value = decode_tiff_value(cfg, mn, type, count,
                                                    off_mn_base, value_bytes,
                                                    store.arena(),
                                                    options.limits, status_out);
                    decoded     = true;
                }

                if (!decoded && have_mn_0) {
                    entry.value = decode_tiff_value(cfg, mn, type, count,
                                                    off_mn_0, value_bytes,
                                                    store.arena(),
                                                    options.limits, status_out);
                    decoded     = true;
                }

                if (!decoded && have_abs) {
                    entry.value = decode_tiff_value(cfg, tiff_bytes, type,
                                                    count, off_rel, value_bytes,
                                                    store.arena(),
                                                    options.limits, status_out);
                    decoded     = true;
                }

                if (!decoded) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    entry.flags |= EntryFlags::Unreadable;
                }
            }

            maybe_mark_ricoh_main_contextual_name(&entry);
            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }

    static void decode_ricoh_imageinfo_u8_table(
        std::string_view mk_prefix, std::span<const std::byte> raw,
        MetaStore& store, const ExifDecodeOptions& options,
        ExifDecodeResult* status_out) noexcept
    {
        if (mk_prefix.empty() || raw.empty()) {
            return;
        }
        if (raw.size() > options.limits.max_entries_per_ifd) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return;
        }

        // `raw` often references `store.arena()` memory. Adding derived entries may
        // grow the arena (realloc), invalidating `raw.data()`. Copy to a stable
        // local buffer first.
        std::array<std::byte, 4096> stable_buf {};
        if (raw.size() > stable_buf.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return;
        }
        std::memcpy(stable_buf.data(), raw.data(), raw.size());
        const std::span<const std::byte> stable(stable_buf.data(), raw.size());

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "imageinfo", 0,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        uint32_t order = 0;
        for (size_t i = 0; i < stable.size(); ++i) {
            if (i > 0xFFFFu) {
                break;
            }
            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), ifd_name,
                                                   static_cast<uint16_t>(i));
            entry.origin.block = block;
            entry.origin.order_in_block = order++;
            entry.origin.wire_type      = WireType { WireFamily::Other, 1 };
            entry.origin.wire_count     = 1;
            entry.flags |= EntryFlags::Derived;
            entry.value = make_u8(u8(stable[i]));

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }

    static void decode_ricoh_faceinfo(std::string_view mk_prefix,
                                      std::span<const std::byte> raw,
                                      MetaStore& store,
                                      const ExifDecodeOptions& options,
                                      ExifDecodeResult* status_out) noexcept
    {
        // ExifTool Ricoh::FaceInfo: a binary table containing face detection
        // metadata used by some models (eg. CX4, GXR).
        if (mk_prefix.empty() || raw.size() <= 0xB6 + 4) {
            return;
        }

        // The input span may point into store.arena(); copy to keep it stable.
        std::array<std::byte, 4096> stable_buf {};
        if (raw.size() > stable_buf.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return;
        }
        std::memcpy(stable_buf.data(), raw.data(), raw.size());
        const std::span<const std::byte> stable(stable_buf.data(), raw.size());

        const uint8_t faces_detected = u8(stable[0xB5]);

        uint16_t frame[2] = { 0, 0 };
        (void)read_u16be(stable, 0xB6, &frame[0]);
        (void)read_u16be(stable, 0xB8, &frame[1]);

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "faceinfo", 0,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return;
        }

        std::array<uint16_t, 10> tags_out {};
        std::array<MetaValue, 10> vals_out {};
        size_t n = 0;

        tags_out[n] = 0x00B5;  // FacesDetected
        vals_out[n] = make_u8(faces_detected);
        n += 1;

        tags_out[n] = 0x00B6;  // FaceDetectFrameSize
        vals_out[n] = make_u16_array(store.arena(),
                                     std::span<const uint16_t>(frame));
        n += 1;

        // Face positions (optional). Only emit if faces were detected and the
        // input is large enough for the expected blocks.
        static constexpr uint16_t kFaceTags[] = {
            0x00BC,  // Face1Position
            0x00C8,  // Face2Position
            0x00D4,  // Face3Position
            0x00E0,  // Face4Position
            0x00EC,  // Face5Position
            0x00F8,  // Face6Position
            0x0104,  // Face7Position
            0x0110,  // Face8Position
        };
        const uint32_t faces = (faces_detected > 8) ? 8 : faces_detected;
        for (uint32_t fi = 0; fi < faces && n < tags_out.size(); ++fi) {
            const uint64_t pos_off = 0xBC + uint64_t(fi) * 0x0C;
            if (pos_off + 8 > stable.size()) {
                break;
            }
            uint16_t box[4] = { 0, 0, 0, 0 };
            (void)read_u16be(stable, pos_off + 0, &box[0]);
            (void)read_u16be(stable, pos_off + 2, &box[1]);
            (void)read_u16be(stable, pos_off + 4, &box[2]);
            (void)read_u16be(stable, pos_off + 6, &box[3]);

            tags_out[n] = kFaceTags[fi];
            vals_out[n] = make_u16_array(store.arena(),
                                         std::span<const uint16_t>(box));
            n += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out.data(), n),
                             std::span<const MetaValue>(vals_out.data(), n),
                             options.limits, status_out);
    }

    static uint32_t score_ascii_blob(std::span<const std::byte> raw) noexcept
    {
        // Prefer buffers that look like normal ASCII strings (digits, punctuation,
        // spaces) and contain at least one NUL terminator.
        if (raw.empty()) {
            return 0;
        }

        const size_t n = (raw.size() < 64) ? raw.size() : 64;
        uint32_t score = 0;
        bool have_nul  = false;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t b = u8(raw[i]);
            if (b == 0) {
                have_nul = true;
                score += 2;
                continue;
            }
            if (b >= 0x20 && b <= 0x7E) {
                score += 3;
            } else {
                // Penalize control/non-ASCII bytes heavily.
                if (score > 0) {
                    score -= 1;
                }
            }
        }

        if (have_nul) {
            score += 10;
        }
        return score;
    }

    static uint32_t
    score_ricoh_faceinfo_blob(std::span<const std::byte> raw) noexcept
    {
        // Prefer buffers that match ExifTool's Ricoh::FaceInfo structure:
        // - FacesDetected at 0xB5 should be a small count (<= 8)
        // - Frame size at 0xB6/0xB8 should be reasonable.
        if (raw.size() <= 0xB6 + 4) {
            return 0;
        }

        const uint8_t faces = u8(raw[0xB5]);
        if (faces > 8) {
            return 0;
        }

        uint16_t w = 0;
        uint16_t h = 0;
        (void)read_u16be(raw, 0xB6, &w);
        (void)read_u16be(raw, 0xB8, &h);

        uint32_t score = 100;
        if (faces == 0) {
            score += 50;
        } else {
            score += (8U - static_cast<uint32_t>(faces));
        }

        // Basic plausibility: frame dims often fit in 16-bit and are not tiny.
        if (w == 0 && h == 0) {
            score += 25;
        } else if (w > 16 && h > 16) {
            score += 10;
        }

        if (w <= 20000 && h <= 20000) {
            score += 5;
        }

        return score;
    }

    static bool decode_ricoh_subdir(std::string_view mk_prefix,
                                    std::span<const std::byte> tiff_bytes,
                                    std::span<const std::byte> raw,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (mk_prefix.empty() || raw.size() < 24) {
            return false;
        }

        // `raw` may point into store.arena(); decode against a stable local copy
        // because decoding may append to the arena (invalidating spans).
        std::vector<std::byte> stable_storage(raw.begin(), raw.end());
        const std::span<const std::byte> stable(stable_storage.data(),
                                                stable_storage.size());

        // ExifTool: Start => $valuePtr + 20 (skip "[Ricoh Camera Info]\0" header),
        // ByteOrder => BigEndian.
        //
        // Some samples include leading padding before the header, so locate the
        // marker string and decode relative to it.
        uint64_t hdr = UINT64_MAX;
        if (find_ricoh_header_marker(stable, &hdr)) {
            // Include the trailing NUL if present.
            hdr += 20U;
        }
        if (hdr == UINT64_MAX) {
            // ExifTool validates this block via the header marker. If we don't see
            // it, best-effort decoding tends to produce garbage.
            return false;
        }
        const uint64_t base_alt = (hdr >= 20) ? (hdr - 20) : 0;
        if (hdr >= stable.size()) {
            return false;
        }

        TiffConfig cfg;
        cfg.bigtiff = false;
        cfg.le      = false;  // BigEndian

        if (hdr + 2 > stable.size()) {
            return false;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "subdir", 0,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return false;
        }

        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, stable, hdr, &entry_count)) {
            return false;
        }
        if (entry_count == 0
            || entry_count > options.limits.max_entries_per_ifd) {
            return false;
        }

        const uint64_t entries_off = hdr + 2;
        const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
        if (entries_off + table_bytes + 4ULL > stable.size()) {
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return false;
        }

        MakerNoteLayout layout_abs;
        layout_abs.cfg                      = cfg;
        layout_abs.bytes                    = stable;
        layout_abs.offsets.out_of_line_base = 0;

        bool added = false;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            ClassicIfdEntry e;
            if (!read_classic_ifd_entry(cfg, stable, eoff, &e)) {
                return added;
            }

            const uint64_t count = e.count32;

            ClassicIfdValueRef ref_abs;
            const bool have_ref
                = resolve_classic_ifd_value_ref(layout_abs, eoff, e, &ref_abs,
                                                status_out);

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, e.tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, e.type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);

            if (!have_ref) {
                entry.flags |= EntryFlags::Unreadable;
            } else if (ref_abs.value_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                entry.flags |= EntryFlags::Truncated;
            } else {
                bool decoded = false;

                const uint64_t value_bytes = ref_abs.value_bytes;

                if (ref_abs.inline_value) {
                    // Inline value bytes live inside the entry itself.
                    if (ref_abs.value_off + value_bytes <= stable.size()) {
                        entry.value
                            = decode_tiff_value(cfg, stable, e.type, count,
                                                ref_abs.value_off, value_bytes,
                                                store.arena(), options.limits,
                                                status_out);
                        decoded = true;
                    }
                } else {
                    // ExifTool Ricoh::Subdir uses a non-standard base: offsets often
                    // point into the outer TIFF/EXIF stream (not relative to the
                    // start of this subdir block). Decode against `tiff_bytes` first.
                    const uint64_t off_abs = static_cast<uint64_t>(
                        e.value_or_off32);
                    if (off_abs + value_bytes <= tiff_bytes.size()) {
                        // FaceInfo is a binary subtable inside the Subdir block.
                        if (e.tag == 0x001A) {
                            decode_ricoh_faceinfo(
                                mk_prefix,
                                tiff_bytes.subspan(static_cast<size_t>(off_abs),
                                                   static_cast<size_t>(
                                                       value_bytes)),
                                store, options, status_out);
                        } else if (e.tag == 0x002C) {
                            decode_ricoh_serialinfo(
                                mk_prefix,
                                tiff_bytes.subspan(static_cast<size_t>(off_abs),
                                                   static_cast<size_t>(
                                                       value_bytes)),
                                store, options, status_out);
                        }

                        entry.value
                            = decode_tiff_value(cfg, tiff_bytes, e.type, count,
                                                off_abs, value_bytes,
                                                store.arena(), options.limits,
                                                status_out);
                        decoded = true;
                    } else {
                        // Fallback: treat offsets as relative to the subdir block
                        // (`hdr` or `hdr-20`) when they don't fit in the outer TIFF.
                        const uint64_t off_rel = static_cast<uint64_t>(
                            e.value_or_off32);

                        uint64_t off_a = UINT64_MAX;  // base_alt + off_rel
                        uint64_t off_b = UINT64_MAX;  // hdr + off_rel
                        if (base_alt <= (UINT64_MAX - off_rel)) {
                            off_a = base_alt + off_rel;
                        }
                        if (hdr <= (UINT64_MAX - off_rel)) {
                            off_b = hdr + off_rel;
                        }

                        const bool ok_a = (off_a != UINT64_MAX)
                                          && (off_a + value_bytes
                                              <= stable.size());
                        const bool ok_b = (off_b != UINT64_MAX)
                                          && (off_b + value_bytes
                                              <= stable.size());

                        if (ok_a || ok_b) {
                            uint64_t value_off = ok_b ? off_b : off_a;
                            if (ok_a && ok_b) {
                                const std::span<const std::byte> a
                                    = stable.subspan(static_cast<size_t>(off_a),
                                                     static_cast<size_t>(
                                                         value_bytes));
                                const std::span<const std::byte> b
                                    = stable.subspan(static_cast<size_t>(off_b),
                                                     static_cast<size_t>(
                                                         value_bytes));

                                if (e.type == 2 /* ASCII */
                                    || e.type == 129 /* UTF-8 */) {
                                    const uint32_t sa = score_ascii_blob(a);
                                    const uint32_t sb = score_ascii_blob(b);
                                    value_off = (sb >= sa) ? off_b : off_a;
                                } else if (e.tag == 0x001A
                                           && e.type == 1 /* BYTE */) {
                                    const uint32_t sa
                                        = score_ricoh_faceinfo_blob(a);
                                    const uint32_t sb
                                        = score_ricoh_faceinfo_blob(b);
                                    value_off = (sb >= sa) ? off_b : off_a;
                                } else {
                                    value_off = off_b;
                                }
                            }

                            if (value_off + value_bytes <= stable.size()) {
                                if (e.tag == 0x001A) {
                                    decode_ricoh_faceinfo(
                                        mk_prefix,
                                        stable.subspan(
                                            static_cast<size_t>(value_off),
                                            static_cast<size_t>(value_bytes)),
                                        store, options, status_out);
                                } else if (e.tag == 0x002C) {
                                    decode_ricoh_serialinfo(
                                        mk_prefix,
                                        stable.subspan(
                                            static_cast<size_t>(value_off),
                                            static_cast<size_t>(value_bytes)),
                                        store, options, status_out);
                                }

                                entry.value = decode_tiff_value(
                                    cfg, stable, e.type, count, value_off,
                                    value_bytes, store.arena(), options.limits,
                                    status_out);
                                decoded = true;
                            }
                        }
                    }
                }

                if (!decoded) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    entry.flags |= EntryFlags::Unreadable;
                }
            }

            (void)store.add_entry(entry);
            added = true;
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }

        return added;
    }

}  // namespace

bool
decode_ricoh_makernote(const TiffConfig& parent_cfg,
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

    const std::string_view mk_prefix = options.tokens.ifd_prefix;

    // Ricoh::Type2 maker notes (RICOH header + little-endian IFD-like table).
    if (decode_ricoh_type2_ricoh_header_ifd(mn, mk_prefix, store, options,
                                            status_out)) {
        return true;
    }

    // Ricoh "Type2" maker notes (eg. Ricoh HZ15, Pentax XG-1).
    if (decode_ricoh_type2_padded_ifd(mn, mk_prefix, store, options,
                                      status_out)) {
        return true;
    }

    // Ricoh MakerNote IFD: ExifTool uses Start => $valuePtr + 8, but some
    // real-world samples appear to have 2 bytes of padding after the 8-byte
    // header. Try both locations and pick the best-scoring IFD.
    ClassicIfdCandidate best;
    bool have_best = false;

    const uint64_t candidates[] = { 8, 10 };
    for (uint32_t ci = 0; ci < sizeof(candidates) / sizeof(candidates[0]);
         ++ci) {
        const uint64_t off = candidates[ci];
        for (int endian = 0; endian < 2; ++endian) {
            TiffConfig cfg;
            cfg.bigtiff = false;
            cfg.le      = (endian == 0);
            ClassicIfdCandidate cand;
            if (!score_classic_ifd_candidate(cfg, mn, off, options.limits,
                                             &cand)) {
                continue;
            }
            if (!have_best || cand.valid_entries > best.valid_entries) {
                best      = cand;
                have_best = true;
            }
        }
    }

    if (!have_best) {
        ClassicIfdCandidate any;
        if (!find_best_classic_ifd_candidate(mn, 256, options.limits, &any)) {
            return false;
        }
        best      = any;
        have_best = true;
    }

    // ExifTool uses Start => $valuePtr + 8 for Ricoh MakerNotes. Many values
    // are stored relative to this base, but some models store absolute offsets
    // relative to the outer EXIF/TIFF. Decode the IFD with a per-entry fallback.
    const uint64_t base = 8;
    if (mn.size() < base + 2) {
        return false;
    }

    TiffConfig cfg;
    cfg.bigtiff = false;
    cfg.le      = best.le;
    decode_ricoh_main_ifd_with_fallback_offsets(cfg, tiff_bytes, mn,
                                                best.offset, base, mk_ifd0,
                                                store, options, status_out);

    const std::span<const std::byte> mn_body = mn.subspan(
        static_cast<size_t>(base));

    const ByteArena& arena = store.arena();

    // Decode binary substructures. We must not mutate `store` while iterating a
    // snapshot of `store.entries()` because adding derived entries can
    // reallocate the entry vector and invalidate references/spans.
    struct PendingSubdir {
        std::vector<std::byte> bytes {};
        uint64_t abs_off  = 0;
        bool pointer_form = false;
    };

    std::vector<std::vector<std::byte>> imageinfo_blobs;
    std::vector<PendingSubdir> subdir_items;
    std::vector<uint64_t> theta_abs_offsets;

    const std::span<const Entry> entries = store.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        if (arena_string(arena, e.key.data.exif_tag.ifd) != mk_ifd0) {
            continue;
        }
        const uint16_t tag = e.key.data.exif_tag.tag;
        if (tag == 0x1001 && e.origin.wire_type.family == WireFamily::Tiff
            && e.origin.wire_type.code != 3 /* SHORT */) {
            if (e.value.kind == MetaValueKind::Bytes
                || e.value.kind == MetaValueKind::Array) {
                const std::span<const std::byte> raw = arena.span(
                    e.value.data.span);
                imageinfo_blobs.emplace_back(raw.begin(), raw.end());
            }
        } else if (tag == 0x2001) {
            if (e.value.kind == MetaValueKind::Bytes) {
                const std::span<const std::byte> raw = arena.span(
                    e.value.data.span);
                PendingSubdir item;
                item.bytes.assign(raw.begin(), raw.end());
                item.pointer_form = false;
                subdir_items.emplace_back(std::move(item));
            } else if (e.value.kind == MetaValueKind::Scalar
                       && e.value.elem_type == MetaElementType::U32) {
                // Pointer form: ExifTool uses Start => $val + 20. The pointer
                // is relative to the outer EXIF/TIFF header.
                PendingSubdir item;
                item.abs_off      = static_cast<uint32_t>(e.value.data.u64);
                item.pointer_form = true;
                subdir_items.emplace_back(std::move(item));
            }
        } else if (tag == 0x4001 && e.value.kind == MetaValueKind::Scalar
                   && e.value.elem_type == MetaElementType::U32) {
            // ThetaSubdir: ExifTool Start => $val. In practice this behaves
            // like a standard EXIF SubIFD pointer, relative to the outer TIFF.
            theta_abs_offsets.emplace_back(
                static_cast<uint32_t>(e.value.data.u64));
        }
    }

    bool have_subdir = false;

    // Many real-world Ricoh MakerNotes contain an embedded RicohSubdir block
    // starting with the ASCII marker "[Ricoh Camera Info]". Prefer decoding
    // this block directly instead of guessing bases from other blobs.
    static constexpr char kSubdirHdr[]      = "[Ricoh Camera Info]";
    static constexpr uint32_t kSubdirHdrLen = sizeof(kSubdirHdr) - 1;
    uint64_t embedded                       = UINT64_MAX;
    for (uint64_t i = 0; i + kSubdirHdrLen <= mn_body.size(); ++i) {
        if (match_bytes(mn_body, i, kSubdirHdr, kSubdirHdrLen)) {
            embedded = i;
            break;
        }
    }
    if (embedded != UINT64_MAX) {
        have_subdir = decode_ricoh_subdir(mk_prefix, tiff_bytes,
                                          mn_body.subspan(
                                              static_cast<size_t>(embedded)),
                                          store, options, status_out)
                      || have_subdir;
    }

    for (const std::vector<std::byte>& blob : imageinfo_blobs) {
        decode_ricoh_imageinfo_u8_table(mk_prefix,
                                        std::span<const std::byte>(blob.data(),
                                                                   blob.size()),
                                        store, options, status_out);
    }

    for (const PendingSubdir& item : subdir_items) {
        if (!item.pointer_form) {
            have_subdir = decode_ricoh_subdir(
                              mk_prefix, tiff_bytes,
                              std::span<const std::byte>(item.bytes.data(),
                                                         item.bytes.size()),
                              store, options, status_out)
                          || have_subdir;
            continue;
        }

        if (item.abs_off < tiff_bytes.size()) {
            have_subdir
                = decode_ricoh_subdir(mk_prefix, tiff_bytes,
                                      tiff_bytes.subspan(
                                          static_cast<size_t>(item.abs_off)),
                                      store, options, status_out)
                  || have_subdir;
        }
    }

    uint32_t idx_theta = 0;
    for (uint64_t off_abs : theta_abs_offsets) {
        if (off_abs >= tiff_bytes.size()) {
            continue;
        }
        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "thetasubdir", idx_theta++,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            continue;
        }
        decode_classic_ifd_no_header(parent_cfg, tiff_bytes, off_abs, ifd_name,
                                     store, options, status_out,
                                     EntryFlags::None);
    }

    // If tag-based extraction didn't work, scan for a BigEndian IFD candidate
    // as a best-effort fallback (covers some samples with unusual Subdir bases).
    if (!have_subdir) {
        ClassicIfdCandidate best_be;
        bool have_be = false;

        TiffConfig be_cfg;
        be_cfg.bigtiff = false;
        be_cfg.le      = false;

        const uint64_t scan_bytes = (mn_body.size() < 4096) ? mn_body.size()
                                                            : 4096;
        for (uint64_t off = 0; off + 2 <= scan_bytes; off += 2) {
            ClassicIfdCandidate cand;
            if (!score_classic_ifd_candidate(be_cfg, mn_body, off,
                                             options.limits, &cand)) {
                continue;
            }
            if (!have_be || cand.valid_entries > best_be.valid_entries) {
                best_be = cand;
                have_be = true;
            }
        }

        if (have_be && best_be.valid_entries >= 4) {
            char scratch[64];
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "subdir", 0,
                                             std::span<char>(scratch));
            if (!ifd_name.empty()) {
                if (best_be.offset < mn_body.size()) {
                    // Best-effort: for these embedded BigEndian IFDs, offsets
                    // tend to be relative to the IFD start (not the outer
                    // MakerNote base). Decode against a subspan starting at
                    // the candidate IFD.
                    const std::span<const std::byte> sub = mn_body.subspan(
                        static_cast<size_t>(best_be.offset));
                    decode_classic_ifd_no_header(be_cfg, sub, 0, ifd_name,
                                                 store, options, status_out,
                                                 EntryFlags::None);
                }
            }
        }
    }

    // Best-effort decode: FaceInfo lives inside the Subdir table as tag 0x001A.
    // Prefer decoding from the already-decoded mk_* subdir entry to work across
    // both the binary-wrapper path and the generic IFD fallback.
    {
        char scratch_subdir[64];
        const std::string_view subdir_ifd
            = make_mk_subtable_ifd_token(mk_prefix, "subdir", 0,
                                         std::span<char>(scratch_subdir));

        // If we already emitted the derived table, don't emit it again.
        char scratch_faceinfo[64];
        const std::string_view face_ifd
            = make_mk_subtable_ifd_token(mk_prefix, "faceinfo", 0,
                                         std::span<char>(scratch_faceinfo));

        bool have_faceinfo = false;
        if (!face_ifd.empty()) {
            const std::span<const Entry> all = store.entries();
            for (size_t i = 0; i < all.size(); ++i) {
                const Entry& e = all[i];
                if (e.key.kind != MetaKeyKind::ExifTag) {
                    continue;
                }
                if (arena_string(arena, e.key.data.exif_tag.ifd) == face_ifd) {
                    have_faceinfo = true;
                    break;
                }
            }
        }

        if (!have_faceinfo && !subdir_ifd.empty()) {
            std::vector<std::byte> face_blob;

            const std::span<const Entry> all = store.entries();
            for (size_t i = 0; i < all.size(); ++i) {
                const Entry& e = all[i];
                if (e.key.kind != MetaKeyKind::ExifTag) {
                    continue;
                }
                if (arena_string(arena, e.key.data.exif_tag.ifd)
                    != subdir_ifd) {
                    continue;
                }
                if (e.key.data.exif_tag.tag != 0x001A) {
                    continue;
                }
                if (e.value.kind != MetaValueKind::Bytes
                    && e.value.kind != MetaValueKind::Array) {
                    continue;
                }

                const std::span<const std::byte> raw = arena.span(
                    e.value.data.span);
                face_blob.assign(raw.begin(), raw.end());
                break;
            }

            if (!face_blob.empty()) {
                decode_ricoh_faceinfo(mk_prefix,
                                      std::span<const std::byte>(
                                          face_blob.data(), face_blob.size()),
                                      store, options, status_out);
            }
        }
    }

    return true;
}


namespace {

    static bool ricoh_checked_add(uint64_t a, uint64_t b,
                                  uint64_t* out) noexcept
    {
        if (!out || a > UINT64_MAX - b) {
            return false;
        }
        *out = a + b;
        return true;
    }


    static bool ricoh_source_local_value(uint64_t maker_note_off,
                                         uint64_t maker_note_bytes,
                                         uint64_t base, uint32_t offset,
                                         uint64_t value_bytes,
                                         uint64_t* source_off,
                                         uint64_t* local_off) noexcept
    {
        uint64_t rel = 0U;
        if (!ricoh_checked_add(base, offset, &rel) || rel > maker_note_bytes
            || value_bytes > maker_note_bytes - rel
            || !ricoh_checked_add(maker_note_off, rel, source_off)) {
            return false;
        }
        if (local_off) {
            *local_off = rel;
        }
        return true;
    }


    static bool score_source_ricoh_main_ifd(SourceTiffReader* source,
                                            const TiffConfig& cfg,
                                            uint64_t maker_note_off,
                                            uint64_t maker_note_bytes,
                                            uint64_t local_ifd_off,
                                            const ExifDecodeLimits& limits,
                                            ClassicIfdCandidate* out) noexcept
    {
        uint64_t ifd_off = 0U;
        if (!source || cfg.bigtiff
            || !ricoh_checked_add(maker_note_off, local_ifd_off, &ifd_off)) {
            return false;
        }
        std::span<const std::byte> count_raw;
        if (!source_tiff_view(source, ifd_off, 2U, &count_raw)) {
            return false;
        }
        uint16_t entry_count = 0U;
        if (!read_tiff_u16(cfg, count_raw, 0U, &entry_count)
            || entry_count == 0U || entry_count > 512U
            || entry_count > limits.max_entries_per_ifd) {
            return false;
        }
        const uint64_t table_bytes = uint64_t(entry_count) * 12U;
        if (local_ifd_off > maker_note_bytes
            || maker_note_bytes - local_ifd_off < 2U
            || table_bytes > maker_note_bytes - local_ifd_off - 2U
            || maker_note_bytes - local_ifd_off - 2U - table_bytes < 4U) {
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
            if (value_bytes <= 4U) {
                valid += 1U;
                continue;
            }

            uint64_t source_off = 0U;
            const bool local
                = ricoh_source_local_value(maker_note_off, maker_note_bytes, 0U,
                                           entry.value_or_off32, value_bytes,
                                           &source_off, nullptr);
            if (local) {
                valid += 1U;
            }
        }

        const uint32_t minimum = entry_count > 4U ? uint32_t(entry_count) / 2U
                                                  : uint32_t(entry_count);
        if (valid < minimum) {
            return false;
        }
        if (out) {
            out->offset        = local_ifd_off;
            out->le            = cfg.le;
            out->entry_count   = entry_count;
            out->valid_entries = valid;
        }
        return true;
    }


    static bool decode_source_ricoh_main_ifd(
        SourceTiffReader* source, const TiffConfig& cfg,
        uint64_t maker_note_off, std::span<const std::byte> maker_note,
        uint64_t local_ifd_off, std::string_view ifd_name, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
    {
        if (!source || ifd_name.empty() || local_ifd_off > maker_note.size()
            || maker_note.size() - local_ifd_off < 2U) {
            return false;
        }
        uint64_t ifd_source_off = 0U;
        std::span<const std::byte> count_raw;
        uint16_t entry_count = 0U;
        if (!ricoh_checked_add(maker_note_off, local_ifd_off, &ifd_source_off)
            || !source_tiff_view(source, ifd_source_off, 2U, &count_raw)
            || !read_tiff_u16(cfg, count_raw, 0U, &entry_count)
            || entry_count == 0U
            || entry_count > options.limits.max_entries_per_ifd) {
            return false;
        }
        const uint64_t entries_off = local_ifd_off + 2U;
        const uint64_t table_bytes = uint64_t(entry_count) * 12U;
        if (entries_off > maker_note.size()
            || table_bytes > maker_note.size() - entries_off
            || maker_note.size() - entries_off - table_bytes < 4U) {
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return true;
        }

        for (uint32_t i = 0U; i < entry_count; ++i) {
            const uint64_t entry_local = entries_off + uint64_t(i) * 12U;
            uint64_t entry_source      = 0U;
            std::span<const std::byte> entry_raw;
            ClassicIfdEntry ifd_entry;
            if (!ricoh_checked_add(maker_note_off, entry_local, &entry_source)
                || !source_tiff_view(source, entry_source, 12U, &entry_raw)
                || !read_classic_ifd_entry(cfg, entry_raw, 0U, &ifd_entry)) {
                return true;
            }
            uint64_t value_bytes = 0U;
            const bool have_ref
                = tiff_type_size(ifd_entry.type) != 0U
                  && classic_ifd_entry_value_bytes(ifd_entry, &value_bytes);
            if (!have_ref) {
                continue;
            }
            if (status_out
                && status_out->entries_decoded
                       >= options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return true;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), ifd_name,
                                                   ifd_entry.tag);
            entry.origin.block = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff,
                                                ifd_entry.type };
            entry.origin.wire_count     = ifd_entry.count32;

            if (value_bytes > options.limits.max_value_bytes) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                entry.flags |= EntryFlags::Truncated;
            } else if (value_bytes <= 4U) {
                entry.value = decode_tiff_value(cfg, entry_raw, ifd_entry.type,
                                                ifd_entry.count32, 8U,
                                                value_bytes, store.arena(),
                                                options.limits, status_out);
            } else {
                uint64_t base_source = 0U;
                const bool have_base = ricoh_source_local_value(
                    maker_note_off, maker_note.size(), 8U,
                    ifd_entry.value_or_off32, value_bytes, &base_source,
                    nullptr);
                uint64_t zero_source = 0U;
                const bool have_zero = ricoh_source_local_value(
                    maker_note_off, maker_note.size(), 0U,
                    ifd_entry.value_or_off32, value_bytes, &zero_source,
                    nullptr);
                const uint64_t absolute_source = ifd_entry.value_or_off32;
                const bool have_absolute       = source_tiff_contains(*source,
                                                                      absolute_source,
                                                                      value_bytes);

                enum class Choice : uint8_t { None, Base, Zero, Absolute };
                Choice choice = Choice::None;
                if (ifd_entry.type == 2U || ifd_entry.type == 129U) {
                    uint32_t score_base     = 0U;
                    uint32_t score_zero     = 0U;
                    uint32_t score_absolute = 0U;
                    std::span<const std::byte> raw;
                    if (have_base
                        && source_tiff_value(source, base_source, value_bytes,
                                             &raw)) {
                        score_base = score_ascii_blob(raw);
                    }
                    if (have_zero
                        && source_tiff_value(source, zero_source, value_bytes,
                                             &raw)) {
                        score_zero = score_ascii_blob(raw);
                    }
                    if (have_absolute) {
                        if (source_tiff_value(source, absolute_source,
                                              value_bytes, &raw)) {
                            score_absolute = score_ascii_blob(raw);
                        }
                    }
                    if (score_base || score_zero || score_absolute) {
                        if (score_base >= score_zero
                            && score_base >= score_absolute && have_base) {
                            choice = Choice::Base;
                        } else if (score_zero >= score_absolute && have_zero) {
                            choice = Choice::Zero;
                        } else if (have_absolute) {
                            choice = Choice::Absolute;
                        }
                    }
                }
                if (choice == Choice::None && have_base) {
                    choice = Choice::Base;
                }
                if (choice == Choice::None && have_zero) {
                    choice = Choice::Zero;
                }
                if (choice == Choice::None && have_absolute) {
                    choice = Choice::Absolute;
                }

                if (choice != Choice::None) {
                    const uint64_t selected = choice == Choice::Base
                                                  ? base_source
                                                  : (choice == Choice::Zero
                                                         ? zero_source
                                                         : absolute_source);
                    std::span<const std::byte> raw;
                    if (source_tiff_value(source, selected, value_bytes, &raw)) {
                        entry.value
                            = decode_tiff_value(cfg, raw, ifd_entry.type,
                                                ifd_entry.count32, 0U,
                                                value_bytes, store.arena(),
                                                options.limits, status_out);
                    } else {
                        entry.flags |= EntryFlags::Truncated;
                    }
                } else {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                    entry.flags |= EntryFlags::Unreadable;
                }
            }

            maybe_mark_ricoh_main_contextual_name(&entry);
            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1U;
            }
        }
        return true;
    }


    static bool
    decode_source_ricoh_subdir(SourceTiffReader* source, uint64_t subdir_off,
                               std::string_view mk_prefix, MetaStore& store,
                               const ExifDecodeOptions& options,
                               ExifDecodeResult* status_out) noexcept
    {
        static constexpr char kMarker[]        = "[Ricoh Camera Info]";
        static constexpr uint64_t kHeaderBytes = sizeof(kMarker);
        std::span<const std::byte> header;
        if (!source || mk_prefix.empty()
            || !source_tiff_view(source, subdir_off, kHeaderBytes + 2U, &header)
            || !match_bytes(header, 0U, kMarker,
                            static_cast<uint32_t>(sizeof(kMarker) - 1U))) {
            return false;
        }

        TiffConfig cfg;
        cfg.le               = false;
        cfg.bigtiff          = false;
        uint16_t entry_count = 0U;
        if (!read_tiff_u16(cfg, header, kHeaderBytes, &entry_count)
            || entry_count == 0U
            || entry_count > options.limits.max_entries_per_ifd) {
            return false;
        }
        uint64_t entries_off = 0U;
        if (!ricoh_checked_add(subdir_off, kHeaderBytes + 2U, &entries_off)
            || !source_tiff_contains(*source, entries_off,
                                     uint64_t(entry_count) * 12U + 4U)) {
            return false;
        }

        char scratch[64];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "subdir", 0U,
                                         std::span<char>(scratch));
        if (ifd_name.empty()) {
            return false;
        }
        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return true;
        }

        for (uint32_t i = 0U; i < entry_count; ++i) {
            const uint64_t entry_off = entries_off + uint64_t(i) * 12U;
            std::span<const std::byte> entry_raw;
            if (!source_tiff_view(source, entry_off, 12U, &entry_raw)) {
                return true;
            }
            ClassicIfdEntry ifd_entry;
            if (!read_classic_ifd_entry(cfg, entry_raw, 0U, &ifd_entry)) {
                return true;
            }
            uint64_t value_bytes = 0U;
            const bool have_ref
                = tiff_type_size(ifd_entry.type) != 0U
                  && classic_ifd_entry_value_bytes(ifd_entry, &value_bytes);

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), ifd_name,
                                                   ifd_entry.tag);
            entry.origin.block = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff,
                                                ifd_entry.type };
            entry.origin.wire_count     = ifd_entry.count32;

            std::span<const std::byte> value_raw;
            bool have_value = false;
            if (!have_ref) {
                entry.flags |= EntryFlags::Unreadable;
            } else if (value_bytes > options.limits.max_value_bytes) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                entry.flags |= EntryFlags::Truncated;
            } else if (value_bytes <= 4U) {
                value_raw  = entry_raw.subspan(8U,
                                               static_cast<size_t>(value_bytes));
                have_value = true;
            } else {
                const uint64_t absolute = ifd_entry.value_or_off32;
                if (source_tiff_contains(*source, absolute, value_bytes)) {
                    have_value = source_tiff_value(source, absolute,
                                                   value_bytes, &value_raw);
                } else {
                    uint64_t local0  = 0U;
                    uint64_t local20 = 0U;
                    const bool have0
                        = ricoh_checked_add(subdir_off,
                                            ifd_entry.value_or_off32, &local0)
                          && source_tiff_contains(*source, local0, value_bytes);
                    const bool have20
                        = ricoh_checked_add(subdir_off, kHeaderBytes, &local20)
                          && ricoh_checked_add(local20,
                                               ifd_entry.value_or_off32,
                                               &local20)
                          && source_tiff_contains(*source, local20,
                                                  value_bytes);
                    uint64_t selected = have20 ? local20 : local0;
                    if (have0 && have20
                        && (ifd_entry.type == 2U || ifd_entry.type == 129U
                            || (ifd_entry.tag == 0x001AU
                                && ifd_entry.type == 1U))) {
                        std::span<const std::byte> raw0;
                        std::span<const std::byte> raw20;
                        uint32_t score0  = 0U;
                        uint32_t score20 = 0U;
                        if (source_tiff_value(source, local0, value_bytes,
                                              &raw0)) {
                            score0 = ifd_entry.tag == 0x001AU
                                         ? score_ricoh_faceinfo_blob(raw0)
                                         : score_ascii_blob(raw0);
                        }
                        if (source_tiff_value(source, local20, value_bytes,
                                              &raw20)) {
                            score20 = ifd_entry.tag == 0x001AU
                                          ? score_ricoh_faceinfo_blob(raw20)
                                          : score_ascii_blob(raw20);
                        }
                        selected = score20 >= score0 ? local20 : local0;
                    }
                    if (have0 || have20) {
                        have_value = source_tiff_value(source, selected,
                                                       value_bytes, &value_raw);
                    }
                }
            }

            if (have_value) {
                if (ifd_entry.tag == 0x001AU) {
                    decode_ricoh_faceinfo(mk_prefix, value_raw, store, options,
                                          status_out);
                } else if (ifd_entry.tag == 0x002CU) {
                    decode_ricoh_serialinfo(mk_prefix, value_raw, store,
                                            options, status_out);
                }
                entry.value = decode_tiff_value(cfg, value_raw, ifd_entry.type,
                                                ifd_entry.count32, 0U,
                                                value_bytes, store.arena(),
                                                options.limits, status_out);
            } else if (!any(entry.flags, EntryFlags::Truncated)
                       && !any(entry.flags, EntryFlags::Unreadable)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                entry.flags |= EntryFlags::Unreadable;
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1U;
            }
        }
        return true;
    }


    static bool find_source_ricoh_marker(SourceTiffReader* source,
                                         uint64_t range_off,
                                         uint64_t range_bytes,
                                         uint64_t* marker_off) noexcept
    {
        if (!source || !marker_off) {
            return false;
        }
        static constexpr char kMarker[]        = "[Ricoh Camera Info]";
        static constexpr uint64_t kMarkerBytes = sizeof(kMarker) - 1U;
        if (range_bytes < kMarkerBytes) {
            return false;
        }

        uint64_t chunk_bytes   = source->value_scratch.size();
        bool use_value_scratch = true;
        if (chunk_bytes < kMarkerBytes) {
            chunk_bytes       = source->window.storage.size();
            use_value_scratch = false;
        }
        if (chunk_bytes < kMarkerBytes) {
            return false;
        }
        if (chunk_bytes > 4096U) {
            chunk_bytes = 4096U;
        }
        const uint64_t step = chunk_bytes - kMarkerBytes + 1U;

        for (uint64_t local = 0U; local < range_bytes;) {
            const uint64_t remaining = range_bytes - local;
            const uint64_t request   = remaining < chunk_bytes ? remaining
                                                               : chunk_bytes;
            if (request < kMarkerBytes) {
                break;
            }
            uint64_t source_off = 0U;
            if (!ricoh_checked_add(range_off, local, &source_off)) {
                return false;
            }
            std::span<const std::byte> raw;
            const bool read_ok
                = use_value_scratch
                      ? source_tiff_value(source, source_off, request, &raw)
                      : source_tiff_view(source, source_off, request, &raw);
            if (!read_ok) {
                return false;
            }
            for (uint64_t i = 0U; i + kMarkerBytes <= raw.size(); ++i) {
                if (match_bytes(raw, i, kMarker,
                                static_cast<uint32_t>(kMarkerBytes))) {
                    *marker_off = source_off + i;
                    return true;
                }
            }
            if (remaining <= chunk_bytes) {
                break;
            }
            local += step;
        }
        return false;
    }


    static bool find_source_ricoh_tag_value_marker(
        SourceTiffReader* source, const TiffConfig& cfg,
        uint64_t maker_note_off, uint64_t maker_note_bytes,
        uint64_t local_ifd_off, uint16_t wanted_tag,
        const ExifDecodeLimits& limits, uint64_t* marker_off) noexcept
    {
        uint64_t ifd_off = 0U;
        std::span<const std::byte> count_raw;
        uint16_t entry_count = 0U;
        if (!ricoh_checked_add(maker_note_off, local_ifd_off, &ifd_off)
            || !source_tiff_view(source, ifd_off, 2U, &count_raw)
            || !read_tiff_u16(cfg, count_raw, 0U, &entry_count)
            || entry_count == 0U || entry_count > limits.max_entries_per_ifd) {
            return false;
        }
        for (uint32_t i = 0U; i < entry_count; ++i) {
            std::span<const std::byte> raw;
            if (!source_tiff_view(source, ifd_off + 2U + uint64_t(i) * 12U, 12U,
                                  &raw)) {
                return false;
            }
            ClassicIfdEntry entry;
            uint64_t value_bytes = 0U;
            if (!read_classic_ifd_entry(cfg, raw, 0U, &entry)
                || entry.tag != wanted_tag
                || !classic_ifd_entry_value_bytes(entry, &value_bytes)
                || value_bytes <= 4U || value_bytes > limits.max_value_bytes) {
                continue;
            }

            uint64_t selected = 0U;
            if (!ricoh_source_local_value(maker_note_off, maker_note_bytes, 8U,
                                          entry.value_or_off32, value_bytes,
                                          &selected, nullptr)
                && !ricoh_source_local_value(maker_note_off, maker_note_bytes,
                                             0U, entry.value_or_off32,
                                             value_bytes, &selected, nullptr)) {
                selected = entry.value_or_off32;
                if (!source_tiff_contains(*source, selected, value_bytes)) {
                    continue;
                }
            }
            return find_source_ricoh_marker(source, selected, value_bytes,
                                            marker_off);
        }
        return false;
    }

}  // namespace


bool
decode_ricoh_makernote_from_source(SourceTiffReader* source,
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

    const std::string_view mk_prefix = options.tokens.ifd_prefix;
    if (decode_ricoh_type2_ricoh_header_ifd(maker_note, mk_prefix, store,
                                            options, status_out)
        || decode_ricoh_type2_padded_ifd(maker_note, mk_prefix, store, options,
                                         status_out)) {
        return true;
    }

    ClassicIfdCandidate best;
    bool found                 = false;
    const uint64_t preferred[] = { 8U, 10U };
    for (uint64_t local_ifd : preferred) {
        for (uint32_t endian = 0U; endian < 2U; ++endian) {
            TiffConfig cfg;
            cfg.le      = endian == 0U;
            cfg.bigtiff = false;
            ClassicIfdCandidate candidate;
            if (!score_source_ricoh_main_ifd(source, cfg, maker_note_off,
                                             maker_note.size(), local_ifd,
                                             options.limits, &candidate)) {
                continue;
            }
            if (!found || candidate.valid_entries > best.valid_entries) {
                best  = candidate;
                found = true;
            }
        }
    }

    if (!found) {
        const uint64_t scan_bytes = maker_note.size() < 256U ? maker_note.size()
                                                             : 256U;
        for (uint64_t local_ifd = 0U; local_ifd + 2U <= scan_bytes;
             ++local_ifd) {
            for (uint32_t endian = 0U; endian < 2U; ++endian) {
                TiffConfig cfg;
                cfg.le      = endian == 0U;
                cfg.bigtiff = false;
                ClassicIfdCandidate candidate;
                if (!score_source_ricoh_main_ifd(source, cfg, maker_note_off,
                                                 maker_note.size(), local_ifd,
                                                 options.limits, &candidate)) {
                    continue;
                }
                if (!found || candidate.valid_entries > best.valid_entries) {
                    best  = candidate;
                    found = true;
                }
            }
        }
    }
    if (!found) {
        return true;  // Same opaque fallback as the contiguous decoder.
    }

    TiffConfig cfg;
    cfg.le      = best.le;
    cfg.bigtiff = false;
    if (!decode_source_ricoh_main_ifd(source, cfg, maker_note_off, maker_note,
                                      best.offset, mk_ifd0, store, options,
                                      status_out)) {
        return false;
    }

    std::vector<std::vector<std::byte>> imageinfo_blobs;
    std::vector<uint64_t> subdir_pointers;
    std::vector<uint64_t> theta_pointers;
    const ByteArena& arena               = store.arena();
    const std::span<const Entry> entries = store.entries();
    for (const Entry& entry : entries) {
        if (entry.key.kind != MetaKeyKind::ExifTag
            || arena_string(arena, entry.key.data.exif_tag.ifd) != mk_ifd0) {
            continue;
        }
        const uint16_t tag = entry.key.data.exif_tag.tag;
        if (tag == 0x1001U && entry.origin.wire_type.family == WireFamily::Tiff
            && entry.origin.wire_type.code != 3U
            && (entry.value.kind == MetaValueKind::Bytes
                || entry.value.kind == MetaValueKind::Array)) {
            const std::span<const std::byte> raw = arena.span(
                entry.value.data.span);
            imageinfo_blobs.emplace_back(raw.begin(), raw.end());
        } else if (tag == 0x2001U && entry.value.kind == MetaValueKind::Scalar
                   && entry.value.elem_type == MetaElementType::U32) {
            subdir_pointers.emplace_back(
                static_cast<uint32_t>(entry.value.data.u64));
        } else if (tag == 0x4001U && entry.value.kind == MetaValueKind::Scalar
                   && entry.value.elem_type == MetaElementType::U32) {
            theta_pointers.emplace_back(
                static_cast<uint32_t>(entry.value.data.u64));
        }
    }

    bool have_subdir = false;
    uint64_t marker  = UINT64_MAX;
    if (find_source_ricoh_marker(source, maker_note_off, maker_note.size(),
                                 &marker)) {
        have_subdir = decode_source_ricoh_subdir(source, marker, mk_prefix,
                                                 store, options, status_out);
    }

    for (const std::vector<std::byte>& blob : imageinfo_blobs) {
        decode_ricoh_imageinfo_u8_table(mk_prefix,
                                        std::span<const std::byte>(blob.data(),
                                                                   blob.size()),
                                        store, options, status_out);
    }

    uint64_t value_marker = UINT64_MAX;
    if (find_source_ricoh_tag_value_marker(source, cfg, maker_note_off,
                                           maker_note.size(), best.offset,
                                           0x2001U, options.limits,
                                           &value_marker)) {
        have_subdir = decode_source_ricoh_subdir(source, value_marker,
                                                 mk_prefix, store, options,
                                                 status_out)
                      || have_subdir;
    }
    for (uint64_t pointer : subdir_pointers) {
        have_subdir = decode_source_ricoh_subdir(source, pointer, mk_prefix,
                                                 store, options, status_out)
                      || have_subdir;
    }

    uint32_t theta_index = 0U;
    for (uint64_t pointer : theta_pointers) {
        char scratch[64];
        const std::string_view ifd_name = make_mk_subtable_ifd_token(
            mk_prefix, "thetasubdir", theta_index++, std::span<char>(scratch));
        OffsetPolicy offsets;
        if (!ifd_name.empty()) {
            (void)decode_classic_ifd_from_source(source, parent_cfg, pointer,
                                                 offsets, ifd_name, store,
                                                 options, status_out,
                                                 EntryFlags::None);
        }
    }

    (void)have_subdir;
    return true;
}

}  // namespace openmeta::exif_internal
