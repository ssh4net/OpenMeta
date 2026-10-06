// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>

namespace openmeta::exif_internal {

namespace {

    static char pentax_ascii_lower(char c) noexcept
    {
        if (c >= 'A' && c <= 'Z') {
            return static_cast<char>(c - 'A' + 'a');
        }
        return c;
    }

    static bool
    pentax_ascii_contains_insensitive(std::string_view haystack,
                                      std::string_view needle) noexcept
    {
        if (needle.empty()) {
            return true;
        }
        if (haystack.size() < needle.size()) {
            return false;
        }

        const size_t last = haystack.size() - needle.size();
        for (size_t i = 0; i <= last; ++i) {
            bool matched = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                if (pentax_ascii_lower(haystack[i + j])
                    != pentax_ascii_lower(needle[j])) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return true;
            }
        }
        return false;
    }

    static bool
    pentax_main_0062_prefers_placeholder(const MetaStore& store) noexcept
    {
        ExifContext ctx(store);
        std::string_view make;
        if (!ctx.find_first_text("ifd0", 0x010F, &make)) {
            return false;
        }
        return pentax_ascii_contains_insensitive(make, "kodak")
               || pentax_ascii_contains_insensitive(make, "samsung");
    }

    struct PentaxSubdirCandidate final {
        uint16_t tag = 0;
        MetaValue value;
    };

    static bool pentax_ifd_has_type2_signature(TiffConfig cfg,
                                               std::span<const std::byte> bytes,
                                               uint64_t ifd_off) noexcept
    {
        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, bytes, ifd_off, &entry_count)
            || entry_count == 0) {
            return false;
        }
        const uint64_t entries_off = ifd_off + 2U;
        const uint64_t needed = entries_off + uint64_t(entry_count) * 12ULL;
        if (needed > bytes.size()) {
            return false;
        }

        for (uint32_t i = 0; i < entry_count; ++i) {
            uint16_t tag = 0;
            if (!read_tiff_u16(cfg, bytes, entries_off + uint64_t(i) * 12ULL,
                               &tag)) {
                return false;
            }
            if (tag == 0x1000u || tag == 0x1001u) {
                return true;
            }
        }
        return false;
    }

    static bool pentax_makernote_has_type2_signature(
        std::span<const std::byte> bytes) noexcept
    {
        TiffConfig cfg;
        cfg.bigtiff = false;

        if (bytes.size() >= 8 && match_bytes(bytes, 0, "AOC\0", 4)) {
            const uint8_t b4 = u8(bytes[4]);
            const uint8_t b5 = u8(bytes[5]);
            if (b4 == 0x49 && b5 == 0x49) {
                cfg.le = true;
            } else if (b4 == 0x4D && b5 == 0x4D) {
                cfg.le = false;
            } else if (b4 == 0x20 && b5 == 0x20) {
                cfg.le = false;
            } else if (b4 == 0x00 && b5 == 0x00 && bytes.size() >= 10) {
                const uint8_t t0 = u8(bytes[8]);
                const uint8_t t1 = u8(bytes[9]);
                if (t0 == 0x01 && t1 == 0x00) {
                    cfg.le = true;
                } else if (t0 == 0x00 && t1 == 0x01) {
                    cfg.le = false;
                } else {
                    cfg.le = false;
                }
            } else {
                cfg.le = false;
            }
            return pentax_ifd_has_type2_signature(cfg, bytes, 6);
        }

        cfg.le = true;
        if (pentax_ifd_has_type2_signature(cfg, bytes, 0)) {
            return true;
        }
        cfg.le = false;
        return pentax_ifd_has_type2_signature(cfg, bytes, 0);
    }

    static bool pentax_model_uses_type2(const MetaStore& store) noexcept
    {
        ExifContext ctx(store);
        std::string_view model;
        if (!ctx.find_first_text("ifd0", 0x0110, &model)) {
            return false;
        }
        const auto exact_prefix = [](std::string_view value,
                                     std::string_view prefix) noexcept {
            if (value.size() < prefix.size()
                || value.substr(0, prefix.size()) != prefix) {
                return false;
            }
            if (value.size() == prefix.size()) {
                return true;
            }
            const char next = value[prefix.size()];
            return next == '\0';
        };
        return exact_prefix(model, "PENTAX Optio 330")
               || exact_prefix(model, "PENTAX Optio 430")
               || exact_prefix(model, "Optio 330")
               || exact_prefix(model, "Optio 430");
    }

    static bool
    pentax_has_detected_faces(std::span<const PentaxSubdirCandidate> cands,
                              const ByteArena& arena) noexcept
    {
        for (size_t i = 0; i < cands.size(); ++i) {
            const PentaxSubdirCandidate& cand = cands[i];
            if (cand.tag == 0x0060) {
                if (cand.value.kind == MetaValueKind::Scalar) {
                    return cand.value.data.u64 != 0;
                }
                if (cand.value.kind == MetaValueKind::Bytes
                    || cand.value.kind == MetaValueKind::Array) {
                    const std::span<const std::byte> raw = arena.span(
                        cand.value.data.span);
                    return !raw.empty() && u8(raw[0]) != 0;
                }
            }
            if (cand.tag == 0x0076) {
                if (cand.value.kind == MetaValueKind::Scalar) {
                    return cand.value.data.u64 != 0;
                }
                if (cand.value.kind == MetaValueKind::Bytes
                    || cand.value.kind == MetaValueKind::Array) {
                    const std::span<const std::byte> raw = arena.span(
                        cand.value.data.span);
                    return raw.size() >= 2 && u8(raw[1]) != 0;
                }
            }
        }
        return false;
    }

    static void decode_pentax_u8_table(std::string_view ifd_name,
                                       std::span<const std::byte> raw,
                                       MetaStore& store,
                                       const ExifDecodeOptions& options,
                                       ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || raw.empty()) {
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
            entry.origin.wire_type      = WireType { WireFamily::Tiff, 1 };
            entry.origin.wire_count     = 1;
            entry.flags |= EntryFlags::Derived;
            entry.value = make_u8(u8(stable[i]));

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }

    static void decode_pentax_binary_subdirs_impl(
        std::string_view mk_ifd0, MetaStore& store, bool le,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
    {
        (void)le;
        if (mk_ifd0.empty()) {
            return;
        }

        PentaxSubdirCandidate cands[48];
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
            const uint16_t tag = e.key.data.exif_tag.tag;
            switch (tag) {
            case 0x003f:  // LensRec
            case 0x005c:  // ShakeReductionInfo
            case 0x0060:  // FaceInfo
            case 0x0068:  // AWBInfo
            case 0x006b:  // TimeInfo
            case 0x007d:  // LensCorr
            case 0x0205:  // CameraSettings
            case 0x0206:  // AEInfo
            case 0x0207:  // LensInfo
            case 0x0208:  // FlashInfo
            case 0x0215:  // CameraInfo
            case 0x0216:  // BatteryInfo
            case 0x021f:  // AFInfo
            case 0x0221:  // KelvinWB
            case 0x0222:  // ColorInfo
            case 0x0224:  // EVStepInfo
            case 0x0226:  // ShotInfo
            case 0x0227:  // FacePos
            case 0x0228:  // FaceSize
            case 0x022a:  // FilterInfo
            case 0x022b:  // LevelInfo
            case 0x022d:  // WBLevels
            case 0x0239:  // LensInfoQ
            case 0x0243:  // PixelShiftInfo
            case 0x0245:  // AFPointInfo
            case 0x03ff:  // TempInfo
                break;
            default: continue;
            }
            // Most Pentax BinaryData subdirectories are stored as UNDEFINED/BYTE
            // arrays (decoded as Bytes/Array). Some models store FaceInfo as a
            // single BYTE scalar (FacesDetected only), so accept that variant too.
            if (e.value.kind != MetaValueKind::Bytes
                && e.value.kind != MetaValueKind::Array) {
                if (!(tag == 0x0060 && e.value.kind == MetaValueKind::Scalar
                      && e.value.elem_type == MetaElementType::U8)) {
                    continue;
                }
            }
            if (cand_count < sizeof(cands) / sizeof(cands[0])) {
                cands[cand_count].tag   = tag;
                cands[cand_count].value = e.value;
                cand_count += 1;
            }
        }

        if (cand_count == 0) {
            return;
        }

        char sub_ifd_buf[96];

        uint32_t idx_camerasettings    = 0;
        uint32_t idx_aeinfo            = 0;
        uint32_t idx_lensinfo          = 0;
        uint32_t idx_flashinfo         = 0;
        uint32_t idx_camerainfo        = 0;
        uint32_t idx_batteryinfo       = 0;
        uint32_t idx_afinfo            = 0;
        uint32_t idx_kelvinwb          = 0;
        uint32_t idx_colorinfo         = 0;
        uint32_t idx_evstepinfo        = 0;
        uint32_t idx_shotinfo          = 0;
        uint32_t idx_facepos           = 0;
        uint32_t idx_facesize          = 0;
        uint32_t idx_filterinfo        = 0;
        uint32_t idx_levelinfo         = 0;
        uint32_t idx_wblevels          = 0;
        uint32_t idx_lensinfoq         = 0;
        uint32_t idx_pixelshift        = 0;
        uint32_t idx_afpointinfo       = 0;
        uint32_t idx_tempinfo          = 0;
        uint32_t idx_srinfo            = 0;
        uint32_t idx_faceinfo          = 0;
        uint32_t idx_awbinfo           = 0;
        uint32_t idx_timeinfo          = 0;
        uint32_t idx_lensrec           = 0;
        uint32_t idx_lenscorr          = 0;
        uint32_t idx_lensdata          = 0;
        const bool have_detected_faces = pentax_has_detected_faces(
            std::span<const PentaxSubdirCandidate>(cands, cand_count),
            store.arena());

        const std::string_view mk_prefix = "mk_pentax";
        ExifContext context(store);
        std::string_view model;
        (void)context.find_first_text("ifd0", 0x0110U, &model);
        bool have_temp_info = false;
        static constexpr std::string_view temp_models[] = {
            "K-01", "K-3", "K-30", "K-5", "K-50", "K-500"
        };
        for (std::string_view temp_model : temp_models) {
            const size_t pos = model.find(temp_model);
            if (pos == std::string_view::npos) continue;
            const size_t end = pos + temp_model.size();
            if (end == model.size() || model[end] == ' ' || model[end] == '\0') {
                have_temp_info = true;
                break;
            }
        }

        for (uint32_t i = 0; i < cand_count; ++i) {
            const uint16_t tag  = cands[i].tag;
            const MetaValue val = cands[i].value;

            // FaceInfo may be stored as a single BYTE scalar (FacesDetected only).
            if (tag == 0x0060 && val.kind == MetaValueKind::Scalar
                && val.elem_type == MetaElementType::U8) {
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "faceinfo",
                                                 idx_faceinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                const uint8_t faces        = static_cast<uint8_t>(val.data.u64
                                                                  & 0xFFU);
                const uint16_t tags_out[]  = { 0x0000 };
                const MetaValue vals_out[] = { make_u8(faces) };
                emit_bin_dir_entries(ifd_name, store,
                                     std::span<const uint16_t>(tags_out),
                                     std::span<const MetaValue>(vals_out),
                                     options.limits, status_out);
                continue;
            }

            const ByteSpan raw_span                  = val.data.span;
            const std::span<const std::byte> raw_src = store.arena().span(
                raw_span);
            if (raw_src.empty()) {
                continue;
            }
            const size_t raw_bytes = raw_src.size();

            if (tag == 0x003f) {  // LensRec
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "lensrec",
                                                 idx_lensrec++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x005c) {  // ShakeReductionInfo
                const std::string_view subtable = (raw_bytes == 4) ? "srinfo"
                                                                   : "srinfo2";
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                                 idx_srinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0060) {  // FaceInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "faceinfo",
                                                 idx_faceinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0068) {  // AWBInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "awbinfo",
                                                 idx_awbinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x006b) {  // TimeInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "timeinfo",
                                                 idx_timeinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x007d) {  // LensCorr
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "lenscorr",
                                                 idx_lenscorr++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0205) {  // CameraSettings
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "camerasettings",
                                                 idx_camerasettings++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0206) {  // AEInfo
                std::string_view subtable;
                if (raw_bytes == 21) {
                    subtable = "aeinfo2";
                } else if (raw_bytes == 48) {
                    subtable = "aeinfo3";
                } else if (raw_bytes != 0 && raw_bytes <= 25
                           && raw_bytes != 21) {
                    subtable = "aeinfo";
                }
                if (subtable.empty()) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                                 idx_aeinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0207) {  // LensInfo
                std::string_view subtable = "lensinfo2";
                if (raw_bytes == 90) {
                    subtable = "lensinfo3";
                } else if (raw_bytes == 91) {
                    subtable = "lensinfo4";
                } else if (raw_bytes == 80 || raw_bytes == 128) {
                    subtable = "lensinfo5";
                } else if (raw_bytes == 168) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                                 idx_lensinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);

                size_t lensdata_off = 0;
                size_t lensdata_len = 0;
                if (subtable == "lensinfo") {
                    lensdata_off = 3;
                    lensdata_len = 17;
                } else if (subtable == "lensinfo2") {
                    lensdata_off = 4;
                    lensdata_len = 17;
                } else if (subtable == "lensinfo3") {
                    lensdata_off = 13;
                    lensdata_len = 17;
                } else if (subtable == "lensinfo4") {
                    lensdata_off = 12;
                    lensdata_len = 18;
                } else if (subtable == "lensinfo5") {
                    lensdata_off = 15;
                    lensdata_len = 17;
                }
                if (lensdata_len != 0U
                    && raw_bytes >= (lensdata_off + lensdata_len)) {
                    const std::string_view lensdata_ifd
                        = make_mk_subtable_ifd_token(mk_prefix, "lensdata",
                                                     idx_lensdata++,
                                                     std::span<char>(
                                                         sub_ifd_buf));
                    if (!lensdata_ifd.empty()) {
                        decode_pentax_u8_table(lensdata_ifd,
                                               raw_src.subspan(lensdata_off,
                                                               lensdata_len),
                                               store, options, status_out);
                    }
                }
                continue;
            }

            if (tag == 0x0208) {  // FlashInfo
                if (raw_bytes != 27) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "flashinfo",
                                                 idx_flashinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0215) {  // CameraInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "camerainfo",
                                                 idx_camerainfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0216) {  // BatteryInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "batteryinfo",
                                                 idx_batteryinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x021f) {  // AFInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "afinfo",
                                                 idx_afinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0221) {  // KelvinWB
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "kelvinwb",
                                                 idx_kelvinwb++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0222) {  // ColorInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "colorinfo",
                                                 idx_colorinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0224) {  // EVStepInfo
                if (raw_bytes > 200) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "evstepinfo",
                                                 idx_evstepinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0226) {  // ShotInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "shotinfo",
                                                 idx_shotinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0227) {  // FacePos
                if (!have_detected_faces) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "facepos",
                                                 idx_facepos++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0228) {  // FaceSize
                if (!have_detected_faces) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "facesize",
                                                 idx_facesize++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x022a) {  // FilterInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "filterinfo",
                                                 idx_filterinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x022b) {  // LevelInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "levelinfo",
                                                 idx_levelinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x022d) {  // WBLevels
                if (raw_bytes != 100) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "wblevels",
                                                 idx_wblevels++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0239) {  // LensInfoQ
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "lensinfoq",
                                                 idx_lensinfoq++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0243) {  // PixelShiftInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "pixelshiftinfo",
                                                 idx_pixelshift++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x0245) {  // AFPointInfo
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "afpointinfo",
                                                 idx_afpointinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }

            if (tag == 0x03ff) {  // TempInfo
                if (!have_temp_info) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "tempinfo",
                                                 idx_tempinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                       status_out);
                continue;
            }
        }
    }

}  // namespace

void
decode_pentax_binary_subdirs(std::string_view mk_ifd0, MetaStore& store,
                             bool le, const ExifDecodeOptions& options,
                             ExifDecodeResult* status_out) noexcept
{
    decode_pentax_binary_subdirs_impl(mk_ifd0, store, le, options, status_out);
}

bool
decode_pentax_makernote(std::span<const std::byte> maker_note_bytes,
                        std::string_view mk_ifd0, MetaStore& store,
                        const ExifDecodeOptions& options,
                        ExifDecodeResult* status_out) noexcept
{
    if (maker_note_bytes.size() < 16) {
        return false;
    }

    char alt_ifd_buf[48] {};
    bool use_type2 = pentax_makernote_has_type2_signature(maker_note_bytes)
                     || pentax_model_uses_type2(store);
    const bool pentax_main_0062_placeholder
        = !use_type2 && pentax_main_0062_prefers_placeholder(store);
    std::string_view pentax_ifd0 = mk_ifd0;
    if (use_type2) {
        pentax_ifd0 = make_mk_subtable_ifd_token("mk_pentax", "type2", 0,
                                                 std::span<char>(alt_ifd_buf));
        if (pentax_ifd0.empty()) {
            use_type2   = false;
            pentax_ifd0 = mk_ifd0;
        }
    }

    // Ricoh-branded cameras (eg. GR III, WG-6) may store Pentax MakerNotes with
    // a "RICOH\0" prefix and a byte-order mark at +6. The IFD begins at +8.
    if (maker_note_bytes.size() >= 10
        && match_bytes(maker_note_bytes, 0, "RICOH\0", 6)
        && (match_bytes(maker_note_bytes, 6, "II", 2)
            || match_bytes(maker_note_bytes, 6, "MM", 2))) {
        TiffConfig cfg;
        cfg.bigtiff = false;
        cfg.le      = (u8(maker_note_bytes[6]) == 'I');

        const uint64_t ifd_off = 8;
        if (!looks_like_classic_ifd(cfg, maker_note_bytes, ifd_off,
                                    options.limits)) {
            return false;
        }

        decode_classic_ifd_no_header(cfg, maker_note_bytes, ifd_off,
                                     pentax_ifd0, store, options, status_out,
                                     EntryFlags::None);
        if (!use_type2) {
            decode_pentax_binary_subdirs(pentax_ifd0, store, cfg.le, options,
                                         status_out);
        }
        return true;
    }

    if (!match_bytes(maker_note_bytes, 0, "AOC\0", 4)) {
        if (match_bytes(maker_note_bytes, 0, "PENTAX \0", 8)
            || match_bytes(maker_note_bytes, 0, "SAMSUNG\0", 8)) {
            TiffConfig cfg;
            cfg.bigtiff = false;
            cfg.le = match_bytes(maker_note_bytes, 8U, "II", 2U);
            if (!looks_like_classic_ifd(cfg, maker_note_bytes, 10U, options.limits)) {
                cfg.le = !cfg.le;
                if (!looks_like_classic_ifd(cfg, maker_note_bytes, 10U, options.limits)) {
                    return false;
                }
            }
            // DNG private offsets are relative to the complete vendor block.
            decode_classic_ifd_no_header(cfg, maker_note_bytes, 10U, pentax_ifd0,
                                         store, options, status_out, EntryFlags::None);
            if (!use_type2) {
                decode_pentax_binary_subdirs(pentax_ifd0, store, cfg.le, options, status_out);
            }
            return true;
        }

        if (maker_note_bytes.size() >= 4) {
            const uint8_t b0 = u8(maker_note_bytes[0]);
            const uint8_t b1 = u8(maker_note_bytes[1]);
            const uint8_t b2 = u8(maker_note_bytes[2]);
            const uint8_t b3 = u8(maker_note_bytes[3]);
            if ((b0 == 0x49 && b1 == 0x49 && b2 == 0x2A && b3 == 0x00)
                || (b0 == 0x4D && b1 == 0x4D && b2 == 0x00 && b3 == 0x2A)) {
                return false;
            }
        }

        TiffConfig alt_cfg;
        alt_cfg.bigtiff = false;
        alt_cfg.le      = true;
        if (!looks_like_classic_ifd(alt_cfg, maker_note_bytes, 0,
                                    options.limits)) {
            alt_cfg.le = false;
            if (!looks_like_classic_ifd(alt_cfg, maker_note_bytes, 0,
                                        options.limits)) {
                return false;
            }
        }
        decode_classic_ifd_no_header(alt_cfg, maker_note_bytes, 0, pentax_ifd0,
                                     store, options, status_out,
                                     EntryFlags::None);
        if (!use_type2) {
            decode_pentax_binary_subdirs(pentax_ifd0, store, alt_cfg.le,
                                         options, status_out);
        }
        return true;
    }

    const uint8_t b4 = u8(maker_note_bytes[4]);
    const uint8_t b5 = u8(maker_note_bytes[5]);

    TiffConfig cfg;
    cfg.bigtiff = false;
    if (b4 == 0x49 && b5 == 0x49) {  // "II"
        cfg.le = true;
    } else if (b4 == 0x4D && b5 == 0x4D) {  // "MM"
        cfg.le = false;
    } else if (b4 == 0x20 && b5 == 0x20) {  // "  "
        cfg.le = false;
    } else if (b4 == 0x00 && b5 == 0x00 && maker_note_bytes.size() >= 10) {
        const uint8_t t0 = u8(maker_note_bytes[8]);
        const uint8_t t1 = u8(maker_note_bytes[9]);
        if (t0 == 0x01 && t1 == 0x00) {
            cfg.le = true;
        } else if (t0 == 0x00 && t1 == 0x01) {
            cfg.le = false;
        } else {
            cfg.le = false;
        }
    } else {
        // Default to big-endian for unknown AOC header variants.
        cfg.le = false;
    }

    uint16_t entry_count = 0;
    if (!read_tiff_u16(cfg, maker_note_bytes, 6, &entry_count)) {
        return false;
    }
    if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd) {
        return false;
    }
    if (entry_count > 2048) {
        return false;
    }

    const uint64_t entries_off = 8;
    const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
    const uint64_t needed      = entries_off + table_bytes + 4ULL;
    if (needed > maker_note_bytes.size()) {
        return false;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return false;
    }

    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t tag  = 0;
        uint16_t type = 0;
        if (!read_tiff_u16(cfg, maker_note_bytes, eoff + 0, &tag)
            || !read_tiff_u16(cfg, maker_note_bytes, eoff + 2, &type)) {
            return true;
        }

        uint32_t count32        = 0;
        uint32_t value_or_off32 = 0;
        if (!read_tiff_u32(cfg, maker_note_bytes, eoff + 4, &count32)
            || !read_tiff_u32(cfg, maker_note_bytes, eoff + 8,
                              &value_or_off32)) {
            return true;
        }
        const uint64_t count = count32;

        const uint64_t unit = tiff_type_size(type);
        if (unit == 0) {
            continue;
        }
        if (count > (UINT64_MAX / unit)) {
            continue;
        }
        const uint64_t value_bytes = count * unit;

        const uint64_t inline_cap      = 4;
        const uint64_t value_field_off = eoff + 8;
        const uint64_t value_off = (value_bytes <= inline_cap) ? value_field_off
                                                               : value_or_off32;

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return true;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), pentax_ifd0, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
        entry.origin.wire_count     = static_cast<uint32_t>(count);

        if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            entry.flags |= EntryFlags::Truncated;
        } else if (value_off + value_bytes > maker_note_bytes.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            entry.flags |= EntryFlags::Unreadable;
        } else {
            entry.value = decode_tiff_value(cfg, maker_note_bytes, type, count,
                                            value_off, value_bytes,
                                            store.arena(), options.limits,
                                            status_out);
        }

        if (pentax_main_0062_placeholder && tag == 0x0062U) {
            entry.flags |= EntryFlags::ContextualName;
            entry.origin.name_context_kind
                = EntryNameContextKind::PentaxMain0062;
            entry.origin.name_context_variant = 1U;
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }

    if (!use_type2 && !pentax_ifd0.empty()) {
        bool have_tag_0000 = false;
        bool have_tag_0064 = false;

        const ByteArena& arena               = store.arena();
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (arena_string(arena, e.key.data.exif_tag.ifd) != pentax_ifd0) {
                continue;
            }
            if (e.key.data.exif_tag.tag == 0x0000) {
                have_tag_0000 = true;
            } else if (e.key.data.exif_tag.tag == 0x0064) {
                have_tag_0064 = true;
            }
        }

        uint16_t tags_out[2];
        MetaValue vals_out[2];
        uint32_t out_count = 0;

        if (!have_tag_0000) {
            tags_out[out_count] = 0x0000;
            vals_out[out_count] = make_u16(0);
            out_count += 1;
        }

        if (!have_tag_0064) {
            ExifContext ctx(store);
            std::string_view model;
            if (ctx.find_first_text("ifd0", 0x0110, &model)
                && model.find("NB1000") != std::string_view::npos) {
                MetaValue v = make_u8(0);
                if (maker_note_bytes.size() > 0x64U) {
                    v = make_u8(u8(maker_note_bytes[0x64]));
                }
                tags_out[out_count] = 0x0064;
                vals_out[out_count] = v;
                out_count += 1;
            }
        }

        if (out_count > 0) {
            emit_bin_dir_entries(pentax_ifd0, store,
                                 std::span<const uint16_t>(tags_out, out_count),
                                 std::span<const MetaValue>(vals_out, out_count),
                                 options.limits, status_out);
        }
    }

    if (!use_type2) {
        decode_pentax_binary_subdirs(pentax_ifd0, store, cfg.le, options,
                                     status_out);
    }

    return true;
}

}  // namespace openmeta::exif_internal
