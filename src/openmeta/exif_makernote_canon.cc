// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include "openmeta/exif_tag_names.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace openmeta::exif_internal {

static bool
canon_is_printable_ascii(uint8_t c) noexcept
{
    return (c >= 0x20U && c <= 0x7EU) || c == '\t' || c == '\n' || c == '\r';
}

static bool
canon_looks_like_text(std::span<const std::byte> raw) noexcept
{
    if (raw.empty()) {
        return false;
    }

    size_t trimmed = raw.size();
    if (raw[trimmed - 1] == std::byte { 0 }) {
        trimmed -= 1;
    }
    if (trimmed == 0) {
        return false;
    }

    for (size_t i = 0; i < trimmed; ++i) {
        const uint8_t c = u8(raw[i]);
        if (c == 0) {
            return false;
        }
        if (!canon_is_printable_ascii(c)) {
            return false;
        }
    }
    return true;
}

static std::string_view
canon_arena_string(const ByteArena& arena, ByteSpan span) noexcept
{
    const std::span<const std::byte> bytes = arena.span(span);
    return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                            bytes.size());
}

static std::string_view
canon_find_first_exif_ascii_value(const MetaStore& store, std::string_view ifd,
                                  uint16_t tag) noexcept
{
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
        if (canon_arena_string(arena, e.key.data.exif_tag.ifd) != ifd) {
            continue;
        }

        if (e.value.kind == MetaValueKind::Text) {
            return canon_arena_string(arena, e.value.data.span);
        }
        if (e.value.kind != MetaValueKind::Bytes) {
            continue;
        }

        const std::span<const std::byte> raw = arena.span(e.value.data.span);
        size_t n                             = 0;
        while (n < raw.size() && raw[n] != std::byte { 0 }) {
            n += 1;
        }
        while (n > 0 && raw[n - 1] == std::byte { ' ' }) {
            n -= 1;
        }
        if (n == 0) {
            continue;
        }
        return std::string_view(reinterpret_cast<const char*>(raw.data()), n);
    }

    return {};
}

static std::string_view
canon_copy_string_view(std::string_view src, std::span<char> buf) noexcept
{
    if (src.empty() || buf.empty()) {
        return {};
    }

    size_t n = src.size();
    if (n >= buf.size()) {
        n = buf.size() - 1U;
    }
    if (n != 0U) {
        std::memcpy(buf.data(), src.data(), n);
    }
    buf[n] = '\0';
    return std::string_view(buf.data(), n);
}

static std::string_view
canon_copy_ascii_bytes(std::span<const std::byte> raw,
                       std::span<char> buf) noexcept
{
    if (raw.empty() || buf.empty()) {
        return {};
    }

    size_t n = 0;
    while (n < raw.size() && raw[n] != std::byte { 0 }) {
        n += 1;
    }
    while (n > 0 && raw[n - 1] == std::byte { ' ' }) {
        n -= 1;
    }
    if (n == 0) {
        return {};
    }
    if (!canon_looks_like_text(raw.first(n))) {
        return {};
    }

    if (n >= buf.size()) {
        n = buf.size() - 1U;
    }
    std::memcpy(buf.data(), raw.data(), n);
    buf[n] = '\0';
    return std::string_view(buf.data(), n);
}

static std::string_view
canon_find_model_text(const MetaStore& store, std::span<char> buf) noexcept
{
    const std::string_view stored_model
        = canon_find_first_exif_ascii_value(store, "ifd0", 0x0110);
    if (!stored_model.empty()) {
        return canon_copy_string_view(stored_model, buf);
    }

    const std::string_view canon_image_type
        = canon_find_first_exif_ascii_value(store, "mk_canon0", 0x0006);
    return canon_copy_string_view(canon_image_type, buf);
}

static std::string_view
canon_find_model_text_in_main_ifd(const TiffConfig& mk_cfg,
                                  std::span<const std::byte> tiff_bytes,
                                  uint64_t entries_off, uint16_t entry_count,
                                  uint64_t maker_note_off,
                                  uint64_t maker_note_ifd_bytes,
                                  const MakerNoteLayout& layout,
                                  std::span<char> buf) noexcept
{
    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        ClassicIfdEntry ifd_entry;
        if (!read_classic_ifd_entry(mk_cfg, tiff_bytes, eoff, &ifd_entry)) {
            return {};
        }
        if (ifd_entry.tag != 0x0006U) {
            continue;
        }

        ClassicIfdValueRef ref;
        if (!resolve_classic_ifd_value_ref(layout, eoff, ifd_entry, &ref,
                                           nullptr)) {
            return {};
        }
        if ((!ref.inline_value
             && canon_value_overlaps_ifd(ref.value_off, ref.value_bytes,
                                         maker_note_off, maker_note_ifd_bytes))
            || ref.value_off > tiff_bytes.size()
            || ref.value_bytes > tiff_bytes.size() - ref.value_off) {
            return {};
        }
        return canon_copy_ascii_bytes(
            tiff_bytes.subspan(static_cast<size_t>(ref.value_off),
                               static_cast<size_t>(ref.value_bytes)),
            buf);
    }

    return {};
}

static char
canon_ascii_lower(char c) noexcept
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

static bool
canon_ascii_contains_insensitive(std::string_view haystack,
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
            if (canon_ascii_lower(haystack[i + j])
                != canon_ascii_lower(needle[j])) {
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
canon_model_matches_any(std::string_view model,
                        std::span<const std::string_view> needles) noexcept
{
    if (model.empty()) {
        return false;
    }
    for (size_t i = 0; i < needles.size(); ++i) {
        if (canon_ascii_contains_insensitive(model, needles[i])) {
            return true;
        }
    }
    return false;
}

CanonAfInfo2ModelClass
classify_canon_afinfo2_model(const MetaStore& store,
                             std::span<const Entry> model_evidence) noexcept
{
    uint32_t model_candidates = 0U;
    const ByteArena& arena    = store.arena();
    std::string_view model;
    char model_buffer[128] {};
    for (size_t i = 0U; i < model_evidence.size(); ++i) {
        const Entry& entry = model_evidence[i];
        if (entry.key.kind != MetaKeyKind::ExifTag
            || entry.key.data.exif_tag.tag != 0x0110U
            || canon_arena_string(arena, entry.key.data.exif_tag.ifd)
                   != "ifd0") {
            continue;
        }
        ++model_candidates;
        if (model_candidates > 1U) {
            return CanonAfInfo2ModelClass::Unknown;
        }
        if (entry.value.kind == MetaValueKind::Text) {
            model = canon_arena_string(arena, entry.value.data.span);
        } else if (entry.value.kind == MetaValueKind::Bytes) {
            model = canon_copy_ascii_bytes(arena.span(entry.value.data.span),
                                           std::span<char>(model_buffer));
        }
    }
    if (model_candidates != 1U || model.empty()) {
        return CanonAfInfo2ModelClass::Unknown;
    }
    if (canon_ascii_contains_insensitive(model, "EOS")) {
        return CanonAfInfo2ModelClass::Eos;
    }

    static constexpr std::string_view kKnownNonEosFamilies[]
        = { "PowerShot", "IXUS", "IXY", "ELPH" };
    return canon_model_matches_any(model, kKnownNonEosFamilies)
               ? CanonAfInfo2ModelClass::KnownNonEos
               : CanonAfInfo2ModelClass::Unknown;
}

static bool
canon_custom_functions2_0701_prefers_shutter_button(
    std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS 40D",        "EOS 50D",
        "EOS 5D Mark II", "EOS-1D Mark III",
        "EOS-1D Mark IV", "EOS-1Ds Mark III",
        "EOS 450D",       "EOS 650D",
        "EOS 700D",       "EOS 750D",
        "EOS 760D",       "EOS 8000D",
        "EOS 100D",       "EOS 1200D",
        "EOS 1300D",      "EOS 2000D",
        "EOS 4000D",      "EOS M",
        "EOS M2",         "EOS Rebel SL1",
        "EOS Rebel T4i",  "EOS Rebel T5",
        "EOS Rebel T5i",  "EOS Rebel T6",
        "EOS Rebel T6i",  "EOS Rebel T6s",
        "EOS Rebel T7",   "EOS DIGITAL REBEL XSi",
        "EOS Kiss X2",    "EOS Kiss X6i",
        "EOS Kiss X7",    "EOS Kiss X7i",
        "EOS Kiss X8i",   "EOS Kiss X70",
        "EOS Kiss X90",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_custom_functions2_010c_prefers_placeholder(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS R10",  "EOS R7",          "EOS R8",   "EOS R1",
        "EOS R3",   "EOS R5 Mark II",  "EOS R5m2", "EOS R6 Mark II",
        "EOS R6m2", "EOS R6 Mark III", "EOS C50",  "PowerShot V10",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_model_is_7d_colordata_psinfo_group(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS 7D",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_model_is_colordata7_psinfo2_group(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS Kiss X7i",
        "EOS-1D X",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_custom_functions2_0701_prefers_af_and_metering(
    std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS 60D",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_custom_functions2_0510_prefers_superimposed_display(
    std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS 40D", "EOS 50D", "EOS 60D", "EOS 70D", "EOS 6D", "EOS 5D Mark II",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_camerainfo_prefers_psinfo(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS 50D",          "EOS 5D Mark II",
        "EOS 1000D",        "EOS DIGITAL REBEL XS",
        "EOS Kiss F",       "EOS-1D Mark III",
        "EOS-1Ds Mark III", "EOS 7D",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_camerainfo_prefers_psinfo2(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS 60D", "EOS 6D", "EOS 5D Mark III", "EOS Kiss X7i", "EOS-1D X",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_model_is_1d_family(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS-1D",
        "EOS-1DS",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_model_is_1ds(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS-1DS",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_model_is_early_kelvin_group(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS 10D",
        "EOS 300D",
        "EOS DIGITAL REBEL",
        "EOS Kiss Digital",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_model_is_1100d_blacklevel_group(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS 1100D",
        "EOS Kiss X50",
        "EOS REBEL T3",
        "EOS 60D",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_model_is_1100d_maxfocal_group(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS 1100D",
        "EOS Kiss X50",
        "EOS REBEL T3",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_model_is_1200d_wb_unknown7_group(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS 1200D",
        "EOS Kiss X70",
        "EOS REBEL T5",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_model_is_r1_r5m2_battery_group(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS R1",
        "EOS R5m2",
        "EOS R5 Mark II",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_main0000_prefers_focaltype(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "DC410", "HV30", "FS300", "XL H1", "ZR950", "DC210", "DC420",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static bool
canon_main0000_prefers_afinfo_size(std::string_view model) noexcept
{
    static constexpr std::string_view kCanonModels[] = {
        "EOS C300",
    };
    return canon_model_matches_any(model, kCanonModels);
}

static void
canon_maybe_mark_contextual_name(std::string_view canon_model,
                                 std::string_view ifd_name, uint16_t tag,
                                 Entry* entry) noexcept
{
    if (!entry || canon_model.empty()) {
        return;
    }

    if (ifd_name == "mk_canon0" && tag == 0x0000U
        && entry->origin.wire_type.family == WireFamily::Tiff
        && entry->origin.wire_type.code == 3U && entry->origin.wire_count == 6U
        && entry->value.kind == MetaValueKind::Array) {
        if (canon_main0000_prefers_afinfo_size(canon_model)) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::CanonMain0000;
            entry->origin.name_context_variant = 2U;
            return;
        }
        if (canon_main0000_prefers_focaltype(canon_model)) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::CanonMain0000;
            entry->origin.name_context_variant = 1U;
            return;
        }
    }

    if (ifd_name == "mk_canon_shotinfo_0" && tag == 0x000EU
        && canon_model_is_1d_family(canon_model)) {
        entry->flags |= EntryFlags::ContextualName;
        entry->origin.name_context_kind
            = EntryNameContextKind::CanonShotInfo000E;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (ifd_name == "mk_canon_camerasettings_0" && tag == 0x0021U
        && canon_model_is_early_kelvin_group(canon_model)) {
        entry->flags |= EntryFlags::ContextualName;
        entry->origin.name_context_kind
            = EntryNameContextKind::CanonCameraSettings0021;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (ifd_name == "mk_canon_colordata4_0") {
        if (tag == 0x00DAU
            && canon_model_is_7d_colordata_psinfo_group(canon_model)) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::CanonColorData4PSInfo;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (tag == 0x00EAU
            && canon_model_is_1200d_wb_unknown7_group(canon_model)) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::CanonColorData400EA;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (tag == 0x00EEU
            && canon_model_is_1100d_maxfocal_group(canon_model)) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::CanonColorData400EE;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (tag == 0x02CFU
            && canon_model_is_1100d_blacklevel_group(canon_model)) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::CanonColorData402CF;
            entry->origin.name_context_variant = 1U;
            return;
        }
    }
    if (ifd_name == "mk_canon_colordata7_0"
        && canon_model_is_colordata7_psinfo2_group(canon_model)) {
        uint8_t variant = 0U;
        switch (tag) {
        case 0x00E4U: variant = 1U; break;
        case 0x00E8U: variant = 2U; break;
        case 0x00ECU: variant = 3U; break;
        case 0x00F0U: variant = 4U; break;
        case 0x00F2U: variant = 5U; break;
        default: break;
        }
        if (variant != 0U) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::CanonColorData7PSInfo2;
            entry->origin.name_context_variant = variant;
            return;
        }
    }
    if (ifd_name == "mk_canon_colorcalib_0" && tag == 0x0038U
        && canon_model_is_r1_r5m2_battery_group(canon_model)) {
        entry->flags |= EntryFlags::ContextualName;
        entry->origin.name_context_kind
            = EntryNameContextKind::CanonColorCalib0038;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (ifd_name == "mk_canon_camerainfo1d_0" && tag == 0x0048U
        && canon_model_is_1ds(canon_model)) {
        entry->flags |= EntryFlags::ContextualName;
        entry->origin.name_context_kind
            = EntryNameContextKind::CanonCameraInfo1D0048;
        entry->origin.name_context_variant = 1U;
        return;
    }
    if (ifd_name == "mk_canon_camerainfo600d_0" && tag == 0x00EAU
        && canon_model_is_1200d_wb_unknown7_group(canon_model)) {
        entry->flags |= EntryFlags::ContextualName;
        entry->origin.name_context_kind
            = EntryNameContextKind::CanonCameraInfo600D00EA;
        entry->origin.name_context_variant = 1U;
    }
}

static std::string_view
canon_camerainfo_subtable_suffix(std::string_view model,
                                 uint32_t tag_extent) noexcept
{
    static constexpr std::string_view k1DxModels[] = {
        "EOS-1D X",
    };
    static constexpr std::string_view k1DMarkIvModels[] = {
        "EOS-1D Mark IV",
    };
    static constexpr std::string_view k1DMarkIiiModels[] = {
        "EOS-1D Mark III",
        "EOS-1Ds Mark III",
    };
    static constexpr std::string_view k1DMarkIInModels[] = {
        "EOS-1D Mark II N",
    };
    static constexpr std::string_view k1DMarkIiModels[] = {
        "EOS-1D Mark II",
        "EOS-1Ds Mark II",
    };
    static constexpr std::string_view k1DModels[] = {
        "EOS-1D",
        "EOS-1DS",
    };
    static constexpr std::string_view kUnknownModels[] = {
        "EOS 5DS", "EOS 5DS R", "EOS R1", "EOS R5m2", "EOS R5 Mark II",
    };
    static constexpr std::string_view k5DMarkIiiModels[] = {
        "EOS 5D Mark III",
    };
    static constexpr std::string_view k5DMarkIiModels[] = {
        "EOS 5D Mark II",
    };
    static constexpr std::string_view k5DModels[] = {
        "EOS 5D",
    };
    static constexpr std::string_view k80DModels[] = {
        "EOS 80D",
    };
    static constexpr std::string_view k750DModels[] = {
        "EOS 750D",      "EOS 760D",     "EOS Rebel T6i",
        "EOS Rebel T6s", "EOS Kiss X8i", "EOS 8000D",
    };
    static constexpr std::string_view k7DModels[] = {
        "EOS 7D",
    };
    static constexpr std::string_view k70DModels[] = {
        "EOS 70D",
    };
    static constexpr std::string_view k6DModels[] = {
        "EOS 6D",
    };
    static constexpr std::string_view k650DModels[] = {
        "EOS 650D",      "EOS 700D",      "EOS Rebel T4i",
        "EOS Rebel T5i", "EOS Kiss X6i",  "EOS Kiss X7i",
        "EOS 100D",      "EOS Rebel SL1", "EOS Kiss X7",
    };
    static constexpr std::string_view k1100DModels[] = {
        "EOS 1100D",
        "EOS Rebel T3",
        "EOS Kiss X50",
    };
    static constexpr std::string_view k600DModels[] = {
        "EOS 600D",     "EOS Rebel T3i", "EOS Kiss X5", "EOS 1200D",
        "EOS Rebel T5", "EOS Kiss X70",  "EOS 1300D",   "EOS Rebel T6",
        "EOS Kiss X80", "EOS 2000D",     "EOS 4000D",   "EOS Rebel T7",
        "EOS Kiss X90",
    };
    static constexpr std::string_view k60DModels[] = {
        "EOS 60D",
    };
    static constexpr std::string_view k550DModels[] = {
        "EOS 550D",
        "EOS Rebel T2i",
        "EOS Kiss X4",
    };
    static constexpr std::string_view k500DModels[] = {
        "EOS 500D",
        "EOS Rebel T1i",
        "EOS Kiss X3",
    };
    static constexpr std::string_view k50DModels[] = {
        "EOS 50D",
    };
    static constexpr std::string_view k450DModels[] = {
        "EOS 450D",
        "EOS DIGITAL REBEL XSi",
        "EOS Kiss X2",
    };
    static constexpr std::string_view k1000DModels[] = {
        "EOS 1000D",
        "EOS DIGITAL REBEL XS",
        "EOS Kiss F",
    };
    static constexpr std::string_view k40DModels[] = {
        "EOS 40D",
    };
    static constexpr std::string_view kG5XIiModels[] = {
        "PowerShot G5 X Mark II",
        "G5 X Mark II",
    };
    static constexpr std::string_view kUnknown32Models[] = {
        "PowerShot S1 IS",
        "PowerShot S1IS",
    };
    static constexpr std::string_view kCompactModels[] = {
        "PowerShot",
        "IXUS",
        "IXY",
    };

    if (canon_model_matches_any(model, k1DxModels)) {
        return "camerainfo1dx";
    }
    if (canon_model_matches_any(model, k1DMarkIvModels)) {
        return "camerainfo1dmkiv";
    }
    if (canon_model_matches_any(model, k1DMarkIiiModels)) {
        return "camerainfo1dmkiii";
    }
    if (canon_model_matches_any(model, k1DMarkIInModels)) {
        return "camerainfo1dmkiin";
    }
    if (canon_model_matches_any(model, k1DMarkIiModels)) {
        return "camerainfo1dmkii";
    }
    if (canon_model_matches_any(model, k1DModels)) {
        return "camerainfo1d";
    }
    if (canon_model_matches_any(model, kUnknownModels)) {
        return "camerainfounknown";
    }
    if (canon_model_matches_any(model, k5DMarkIiiModels)) {
        return "camerainfo5dmkiii";
    }
    if (canon_model_matches_any(model, k5DMarkIiModels)) {
        return "camerainfo5dmkii";
    }
    if (canon_model_matches_any(model, k5DModels)) {
        return "camerainfo5d";
    }
    if (canon_model_matches_any(model, k80DModels)) {
        return "camerainfo80d";
    }
    if (canon_model_matches_any(model, k750DModels)) {
        return "camerainfo750d";
    }
    if (canon_model_matches_any(model, k7DModels)) {
        return "camerainfo7d";
    }
    if (canon_model_matches_any(model, k70DModels)) {
        return "camerainfo70d";
    }
    if (canon_model_matches_any(model, k6DModels)) {
        return "camerainfo6d";
    }
    if (canon_ascii_contains_insensitive(model, "EOS Kiss X70")) {
        return "camerainfo600d";
    }
    if (canon_model_matches_any(model, k650DModels)) {
        return "camerainfo650d";
    }
    if (canon_model_matches_any(model, k600DModels)) {
        return "camerainfo600d";
    }
    if (canon_model_matches_any(model, k1100DModels)) {
        return "camerainfo1100d";
    }
    if (canon_model_matches_any(model, k60DModels)) {
        return "camerainfo60d";
    }
    if (canon_model_matches_any(model, k550DModels)) {
        return "camerainfo550d";
    }
    if (canon_model_matches_any(model, k500DModels)) {
        return "camerainfo500d";
    }
    if (canon_model_matches_any(model, k50DModels)) {
        return "camerainfo50d";
    }
    if (canon_model_matches_any(model, k450DModels)) {
        return "camerainfo450d";
    }
    if (canon_model_matches_any(model, k1000DModels)) {
        return "camerainfo1000d";
    }
    if (canon_model_matches_any(model, k40DModels)) {
        return "camerainfo40d";
    }
    if (canon_model_matches_any(model, kG5XIiModels)) {
        return "camerainfog5xii";
    }
    if (canon_model_matches_any(model, kUnknown32Models)) {
        return "camerainfounknown32";
    }
    if (canon_model_matches_any(model, kCompactModels)) {
        if (tag_extent >= 0x0099U) {
            return "camerainfopowershot2";
        }
        if (tag_extent >= 0x0091U) {
            return "camerainfopowershot";
        }
    }

    return "camerainfo";
}

static uint32_t
canon_camerainfo_candidate_tag_extent(const TiffConfig& cfg,
                                      std::span<const std::byte> bytes,
                                      const ClassicIfdCandidate& cand) noexcept
{
    if (cand.entry_count == 0) {
        return 0U;
    }

    uint32_t max_tag               = 0U;
    const uint64_t first_entry_off = cand.offset + 2U;
    for (uint32_t i = 0; i < cand.entry_count; ++i) {
        ClassicIfdEntry entry;
        const uint64_t entry_off = first_entry_off + uint64_t(i) * 12ULL;
        if (!read_classic_ifd_entry(cfg, bytes, entry_off, &entry)) {
            break;
        }
        if (entry.tag > max_tag) {
            max_tag = entry.tag;
        }
    }

    return max_tag;
}

static bool
canon_camerainfo_candidate_has_tag(const TiffConfig& cfg,
                                   std::span<const std::byte> bytes,
                                   const ClassicIfdCandidate& cand,
                                   uint16_t wanted_tag) noexcept
{
    if (cand.entry_count == 0) {
        return false;
    }

    const uint64_t first_entry_off = cand.offset + 2U;
    for (uint32_t i = 0; i < cand.entry_count; ++i) {
        ClassicIfdEntry entry;
        const uint64_t entry_off = first_entry_off + uint64_t(i) * 12ULL;
        if (!read_classic_ifd_entry(cfg, bytes, entry_off, &entry)) {
            return false;
        }
        if (entry.tag == wanted_tag) {
            return true;
        }
    }

    return false;
}

static std::string_view
canon_camerainfo_subtable_suffix_from_candidate(
    const TiffConfig& cfg, std::span<const std::byte> bytes,
    const ClassicIfdCandidate& cand) noexcept
{
    if (canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x01ACU)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x01EBU)) {
        return "camerainfo7d";
    }
    if (canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x045AU)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x04AEU)) {
        return "camerainfo80d";
    }
    if (canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x043DU)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x0449U)) {
        return "camerainfo750d";
    }
    if (canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x025EU)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x02BFU)) {
        return "camerainfo70d";
    }
    if (canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x0256U)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x02B6U)) {
        return "camerainfo6d";
    }
    if (canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x023CU)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x029CU)) {
        return "camerainfo5dmkiii";
    }
    if (canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x021BU)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x0220U)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x0280U)) {
        return "camerainfo650d";
    }
    if (canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x0199U)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x01E5U)) {
        return "camerainfo600d";
    }
    if (canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x0136U)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x017EU)) {
        return "camerainfo1dmkiii";
    }
    if (canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x010BU)
        || canon_camerainfo_candidate_has_tag(cfg, bytes, cand, 0x0143U)) {
        return "camerainfo1000d";
    }

    return {};
}

static bool
canon_psinfo2_tail_looks_valid(const TiffConfig& cfg,
                               std::span<const std::byte> bytes,
                               uint64_t value_off,
                               uint64_t value_bytes) noexcept
{
    if (value_bytes < 0x00f4U + 2U) {
        return false;
    }
    if (value_off > bytes.size()) {
        return false;
    }
    if (value_bytes > (bytes.size() - value_off)) {
        return false;
    }

    static constexpr uint16_t kMaxPictureStyleId = 0x00ffU;
    static constexpr uint16_t kStyleTags[]       = {
        0x00f0U,
        0x00f2U,
        0x00f4U,
    };

    for (size_t i = 0; i < (sizeof(kStyleTags) / sizeof(kStyleTags[0])); ++i) {
        uint16_t v = 0;
        if (!read_tiff_u16(cfg, bytes, value_off + uint64_t(kStyleTags[i]),
                           &v)) {
            return false;
        }
        if (v > kMaxPictureStyleId) {
            return false;
        }
    }
    return true;
}

enum class CanonCameraInfoFieldKind : uint8_t {
    U8,
    U16,
    U16Rev,
    U16Array4,
    U32Array4,
    U32,
    AsciiFixed,
};

struct CanonCameraInfoField final {
    uint16_t tag;
    CanonCameraInfoFieldKind kind;
    uint8_t bytes;
};

static std::span<const CanonCameraInfoField>
canon_camerainfo_extra_fields(std::string_view ifd_name) noexcept
{
    static constexpr CanonCameraInfoField k40d[] = {
        { 0x0043, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0045, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00D6, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x00D8, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00DA, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00FF, CanonCameraInfoFieldKind::AsciiFixed, 6 },
    };
    static constexpr CanonCameraInfoField k450d[] = {
        { 0x0043, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0045, CanonCameraInfoFieldKind::U16, 2 },
    };
    static constexpr CanonCameraInfoField k1000d[] = {
        { 0x0043, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0045, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00E2, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x00E4, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00E6, CanonCameraInfoFieldKind::U16, 2 },
    };
    static constexpr CanonCameraInfoField k500d[] = {
        { 0x0050, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0052, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0077, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00AB, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00BC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00BE, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F6, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x00F8, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00FA, CanonCameraInfoFieldKind::U16, 2 },
        { 0x01DF, CanonCameraInfoFieldKind::U32, 4 },
    };
    static constexpr CanonCameraInfoField k50d[] = {
        { 0x0050, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0052, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00A7, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00BD, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00BF, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EA, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x00EC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EE, CanonCameraInfoFieldKind::U16, 2 },
    };
    static constexpr CanonCameraInfoField k550d[] = {
        { 0x0054, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0056, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0078, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007C, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00B0, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00FF, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0101, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0103, CanonCameraInfoFieldKind::U16, 2 },
    };
    static constexpr CanonCameraInfoField k600d[] = {
        { 0x001E, CanonCameraInfoFieldKind::U16, 2 },
        { 0x003A, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0057, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0059, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007B, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007F, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00B3, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EA, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x00EC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EE, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0199, CanonCameraInfoFieldKind::AsciiFixed, 6 },
    };
    static constexpr CanonCameraInfoField k1100d[] = {
        { 0x001E, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0038, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0057, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0059, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007B, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007F, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00B3, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EA, CanonCameraInfoFieldKind::U16Rev, 2 },
    };
    static constexpr CanonCameraInfoField k650d[] = {
        { 0x0023, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007D, CanonCameraInfoFieldKind::U16, 2 },
        { 0x008C, CanonCameraInfoFieldKind::U16, 2 },
        { 0x008E, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00BC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00C0, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F4, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0127, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0129, CanonCameraInfoFieldKind::U16, 2 },
        { 0x012B, CanonCameraInfoFieldKind::U16, 2 },
        { 0x021B, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0220, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0270, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0274, CanonCameraInfoFieldKind::U32, 4 },
        { 0x027C, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0280, CanonCameraInfoFieldKind::U32, 4 },
    };
    static constexpr CanonCameraInfoField k60d[] = {
        { 0x0055, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0057, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007D, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00E8, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x00EA, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x01E5, CanonCameraInfoFieldKind::U32, 4 },
    };
    static constexpr CanonCameraInfoField k5dmkii[] = {
        { 0x0050, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0052, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00A7, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00BD, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00BF, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00E6, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x00E8, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EA, CanonCameraInfoFieldKind::U16, 2 },
        { 0x017E, CanonCameraInfoFieldKind::AsciiFixed, 6 },
    };
    static constexpr CanonCameraInfoField k5dmkiii[] = {
        { 0x0023, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007D, CanonCameraInfoFieldKind::U16, 2 },
        { 0x008C, CanonCameraInfoFieldKind::U16, 2 },
        { 0x008E, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00BC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00C0, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F4, CanonCameraInfoFieldKind::U16, 2 },
        { 0x023C, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x028C, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0290, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0298, CanonCameraInfoFieldKind::U32, 4 },
        { 0x029C, CanonCameraInfoFieldKind::U32, 4 },
    };
    static constexpr CanonCameraInfoField k6d[] = {
        { 0x0023, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0083, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0092, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0094, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00C2, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00C6, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00FA, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0161, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0163, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0165, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0256, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x02AA, CanonCameraInfoFieldKind::U32, 4 },
        { 0x02B6, CanonCameraInfoFieldKind::U32, 4 },
    };
    static constexpr CanonCameraInfoField k1dmkiii[] = {
        { 0x0043, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0045, CanonCameraInfoFieldKind::U16, 2 },
        { 0x005E, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0062, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0086, CanonCameraInfoFieldKind::U16, 2 },
    };
    static constexpr CanonCameraInfoField k1dmkii[] = {
        { 0x0066, CanonCameraInfoFieldKind::U8, 1 },
        { 0x0075, CanonCameraInfoFieldKind::AsciiFixed, 5 },
    };
    static constexpr CanonCameraInfoField k1dmkiin[] = {
        { 0x0074, CanonCameraInfoFieldKind::U8, 1 },
        { 0x0079, CanonCameraInfoFieldKind::AsciiFixed, 5 },
    };
    static constexpr CanonCameraInfoField k1dmkiv[] = {
        { 0x0054, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0056, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0078, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007C, CanonCameraInfoFieldKind::U16, 2 },
        { 0x022C, CanonCameraInfoFieldKind::U32, 4 },
    };
    static constexpr CanonCameraInfoField k7d[] = {
        { 0x0056, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007B, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00AF, CanonCameraInfoFieldKind::U8, 1 },
        { 0x00C9, CanonCameraInfoFieldKind::U8, 1 },
    };
    static constexpr CanonCameraInfoField k5d[] = {
        { 0x000C, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0017, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0027, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0028, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0038, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0054, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0058, CanonCameraInfoFieldKind::U16, 2 },
        { 0x006C, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0093, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0095, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0097, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x00A4, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00AC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00CC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00D0, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00E8, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00E9, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EA, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EB, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00ED, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EE, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00EF, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F0, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F1, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F2, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F3, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F4, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F5, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F6, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F7, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F8, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F9, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00FA, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00FB, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00FC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00FD, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00FE, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00FF, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0100, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0101, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0102, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0103, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0104, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0105, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0106, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0107, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0108, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0109, CanonCameraInfoFieldKind::U16, 2 },
        { 0x010A, CanonCameraInfoFieldKind::U16, 2 },
        { 0x010B, CanonCameraInfoFieldKind::U16, 2 },
        { 0x010C, CanonCameraInfoFieldKind::U16, 2 },
        { 0x010E, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0110, CanonCameraInfoFieldKind::U16, 2 },
        { 0x011C, CanonCameraInfoFieldKind::U32, 4 },
    };
    static constexpr CanonCameraInfoField k1dx[] = {
        { 0x0023, CanonCameraInfoFieldKind::U16, 2 },
        { 0x007D, CanonCameraInfoFieldKind::U16, 2 },
        { 0x008C, CanonCameraInfoFieldKind::U16, 2 },
        { 0x008E, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00BC, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00C0, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00F4, CanonCameraInfoFieldKind::U16, 2 },
        { 0x01A7, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x01A9, CanonCameraInfoFieldKind::U16, 2 },
        { 0x01AB, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0280, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x02D0, CanonCameraInfoFieldKind::U32, 4 },
        { 0x02DC, CanonCameraInfoFieldKind::U32, 4 },
    };
    static constexpr CanonCameraInfoField k70d[] = {
        { 0x0023, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0084, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0093, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0095, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00C7, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0166, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0168, CanonCameraInfoFieldKind::U16, 2 },
        { 0x016A, CanonCameraInfoFieldKind::U16, 2 },
        { 0x025E, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x02B3, CanonCameraInfoFieldKind::U32, 4 },
        { 0x02BF, CanonCameraInfoFieldKind::U32, 4 },
    };
    static constexpr CanonCameraInfoField k750d[] = {
        { 0x0023, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0096, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00A5, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00A7, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0131, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0135, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0169, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0184, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0186, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0188, CanonCameraInfoFieldKind::U16, 2 },
        { 0x043D, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0449, CanonCameraInfoFieldKind::AsciiFixed, 6 },
    };
    static constexpr CanonCameraInfoField k80d[] = {
        { 0x0023, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0096, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00A5, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00A7, CanonCameraInfoFieldKind::U16, 2 },
        { 0x013A, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0189, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x018B, CanonCameraInfoFieldKind::U16, 2 },
        { 0x018D, CanonCameraInfoFieldKind::U16, 2 },
        { 0x045A, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x04AE, CanonCameraInfoFieldKind::U32, 4 },
        { 0x04BA, CanonCameraInfoFieldKind::U32, 4 },
    };

    if (ifd_name.find("camerainfo1dx") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k1dx);
    }
    if (ifd_name.find("camerainfo80d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k80d);
    }
    if (ifd_name.find("camerainfo750d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k750d);
    }
    if (ifd_name.find("camerainfo7d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k7d);
    }
    if (ifd_name.find("camerainfo70d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k70d);
    }
    if (ifd_name.find("camerainfo6d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k6d);
    }
    if (ifd_name.find("camerainfo5dmkiii") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k5dmkiii);
    }
    if (ifd_name.find("camerainfo5dmkii") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k5dmkii);
    }
    if (ifd_name.find("camerainfo5d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k5d);
    }
    if (ifd_name.find("camerainfo1dmkiv") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k1dmkiv);
    }
    if (ifd_name.find("camerainfo1dmkiii") != std::string_view::npos
        || ifd_name.find("camerainfo1dsmkiii") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k1dmkiii);
    }
    if (ifd_name.find("camerainfo1dmkiin") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k1dmkiin);
    }
    if (ifd_name.find("camerainfo1dmkii") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k1dmkii);
    }
    if (ifd_name.find("camerainfo60d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k60d);
    }
    if (ifd_name.find("camerainfo650d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k650d);
    }
    if (ifd_name.find("camerainfo1100d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k1100d);
    }
    if (ifd_name.find("camerainfo600d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k600d);
    }
    if (ifd_name.find("camerainfo550d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k550d);
    }
    if (ifd_name.find("camerainfo50d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k50d);
    }
    if (ifd_name.find("camerainfo500d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k500d);
    }
    if (ifd_name.find("camerainfo1000d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k1000d);
    }
    if (ifd_name.find("camerainfo450d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k450d);
    }
    if (ifd_name.find("camerainfo40d") != std::string_view::npos) {
        return std::span<const CanonCameraInfoField>(k40d);
    }
    return {};
}

static uint32_t
canon_colordata_family(uint32_t count, int16_t version) noexcept
{
    switch (count) {
    case 582: return 1;
    case 653: return 2;
    case 796: return 3;
    case 674:
    case 692:
    case 702:
    case 1227:
    case 1250:
    case 1251:
    case 1337:
    case 1338:
    case 1346: return 4;
    case 5120: return 5;
    case 1273:
    case 1275: return 6;
    case 1312:
    case 1313:
    case 1316:
    case 1506: return 7;
    case 1353:
    case 1560:
    case 1592:
    case 1602: return 8;
    case 1816:
    case 1820:
    case 1824: return 9;
    case 2024:
    case 3656: return 10;
    case 3973: return 11;
    case 3778: return (version == 65) ? 12U : 11U;
    case 4528: return 12;
    default: return 0;
    }
}

static bool
canon_colordata_family_name(uint32_t family, std::string_view* out) noexcept
{
    if (!out) {
        return false;
    }
    switch (family) {
    case 1: *out = "colordata1"; return true;
    case 2: *out = "colordata2"; return true;
    case 3: *out = "colordata3"; return true;
    case 4: *out = "colordata4"; return true;
    case 5: *out = "colordata5"; return true;
    case 6: *out = "colordata6"; return true;
    case 7: *out = "colordata7"; return true;
    case 8: *out = "colordata8"; return true;
    case 9: *out = "colordata9"; return true;
    case 10: *out = "colordata10"; return true;
    case 11: *out = "colordata11"; return true;
    case 12: *out = "colordata12"; return true;
    default: return false;
    }
}

static bool
canon_add_base_and_off32(int64_t base, uint32_t off32,
                         uint64_t* abs_off_out) noexcept
{
    if (!abs_off_out) {
        return false;
    }

    const int64_t off = static_cast<int64_t>(off32);
    if (base > (INT64_MAX - off)) {
        return false;
    }

    const int64_t abs_off = base + off;
    if (abs_off < 0) {
        return false;
    }

    *abs_off_out = static_cast<uint64_t>(abs_off);
    return true;
}

static bool
canon_add_special_base_and_off32(int64_t base, uint32_t off32,
                                 uint64_t* abs_off_out) noexcept
{
    if (!abs_off_out) {
        return false;
    }

    const int64_t off = static_cast<int64_t>(off32);
    if (base > 0 && base > (INT64_MAX - off)) {
        return false;
    }

    const int64_t abs_off = base + off;
    if (abs_off < 0) {
        return false;
    }

    *abs_off_out = static_cast<uint64_t>(abs_off);
    return true;
}

static bool
canon_dir_bytes(uint16_t entry_count, uint64_t* out) noexcept
{
    if (!out) {
        return false;
    }
    if (uint64_t(entry_count) > (UINT64_MAX / 12ULL)) {
        return false;
    }
    *out = 2ULL + uint64_t(entry_count) * 12ULL + 4ULL;
    return true;
}

bool
canon_recover_ifd_extent(const TiffConfig& cfg,
                         std::span<const std::byte> maker_note,
                         uint64_t maker_note_off, uint16_t declared_count,
                         uint16_t* effective_count,
                         uint64_t* ifd_bytes) noexcept
{
    if (effective_count == nullptr || ifd_bytes == nullptr
        || declared_count < 2U) {
        return false;
    }

    uint64_t declared_ifd_bytes = 0U;
    if (!canon_dir_bytes(declared_count, &declared_ifd_bytes)
        || declared_ifd_bytes > maker_note.size()) {
        return false;
    }

    uint16_t note_entry_count = 0U;
    if (!read_tiff_u16(cfg, maker_note, 0U, &note_entry_count)
        || note_entry_count != declared_count) {
        return false;
    }

    const uint16_t candidate_count = static_cast<uint16_t>(declared_count - 1U);
    uint64_t candidate_ifd_bytes   = 0U;
    if (!canon_dir_bytes(candidate_count, &candidate_ifd_bytes)
        || candidate_ifd_bytes < 4U
        || candidate_ifd_bytes > maker_note.size()) {
        return false;
    }

    const uint64_t trailing_row_start = candidate_ifd_bytes - 4U;
    for (uint64_t i = 0U; i < 4U; ++i) {
        if (maker_note[static_cast<size_t>(trailing_row_start + i)]
            != std::byte { 0 }) {
            return false;
        }
    }

    uint32_t camera_settings_entries = 0U;
    bool camera_settings_valid       = false;
    for (uint32_t i = 0U; i < candidate_count; ++i) {
        const uint64_t entry_off = 2U + uint64_t(i) * 12U;
        ClassicIfdEntry entry;
        if (!read_classic_ifd_entry(cfg, maker_note, entry_off, &entry)
            || tiff_type_size(entry.type) == 0U) {
            return false;
        }
        if (entry.tag != 0x0001U) {
            continue;
        }

        camera_settings_entries += 1U;
        if (camera_settings_entries != 1U || entry.type != 3U
            || entry.count32 <= 2U) {
            return false;
        }

        const uint64_t payload_bytes = uint64_t(entry.count32) * 2U;
        if (payload_bytes > UINT16_MAX
            || maker_note_off > (UINT64_MAX - candidate_ifd_bytes)) {
            return false;
        }
        const uint64_t expected_payload_off = maker_note_off
                                              + candidate_ifd_bytes;
        if (uint64_t(entry.value_or_off32) != expected_payload_off
            || candidate_ifd_bytes > maker_note.size()
            || payload_bytes > (maker_note.size() - candidate_ifd_bytes)) {
            return false;
        }

        uint16_t encoded_payload_bytes = 0U;
        if (!read_tiff_u16(cfg, maker_note, candidate_ifd_bytes,
                           &encoded_payload_bytes)
            || uint64_t(encoded_payload_bytes) != payload_bytes) {
            return false;
        }
        camera_settings_valid = true;
    }

    if (camera_settings_entries != 1U || !camera_settings_valid) {
        return false;
    }

    *effective_count = candidate_count;
    *ifd_bytes       = candidate_ifd_bytes;
    return true;
}

static bool
canon_value_base_overlaps_ifd(
    const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
    uint64_t maker_note_off, uint64_t maker_note_ifd_bytes,
    uint16_t entry_count, const ExifDecodeLimits& limits, int64_t base) noexcept
{
    if (maker_note_ifd_bytes == 0U || entry_count == 0U) {
        return false;
    }

    const uint64_t entries_off = maker_note_off + 2ULL;
    for (uint32_t i = 0U; i < entry_count; ++i) {
        const uint64_t eoff     = entries_off + uint64_t(i) * 12ULL;
        uint16_t type           = 0U;
        uint32_t count32        = 0U;
        uint32_t value_or_off32 = 0U;
        if (!read_tiff_u16(cfg, tiff_bytes, eoff + 2U, &type)
            || !read_tiff_u32(cfg, tiff_bytes, eoff + 4U, &count32)
            || !read_tiff_u32(cfg, tiff_bytes, eoff + 8U, &value_or_off32)) {
            break;
        }

        const uint64_t unit  = tiff_type_size(type);
        const uint64_t count = count32;
        if (unit == 0U || count == 0U || count > (UINT64_MAX / unit)) {
            continue;
        }
        const uint64_t value_bytes = count * unit;
        if (value_bytes <= 4U || value_bytes > limits.max_value_bytes) {
            continue;
        }

        uint64_t value_off = 0U;
        if (!canon_add_base_and_off32(base, value_or_off32, &value_off)) {
            continue;
        }
        if (canon_value_overlaps_ifd(value_off, value_bytes, maker_note_off,
                                     maker_note_ifd_bytes)) {
            return true;
        }
    }
    return false;
}


static bool
canon_should_emit_unknown_table_tags(std::string_view ifd_name) noexcept
{
    // Exiv2 exposes unknown Canon table elements by numeric tag id for a few
    // compact/array-like Canon subtables. Keep this behavior constrained to
    // these tables to avoid noisy expansion in large variant tables.
    return ifd_name.find("mk_canon_camerasettings_") == 0
           || ifd_name.find("mk_canon_shotinfo_") == 0
           || ifd_name.find("mk_canon_fileinfo_") == 0
           || ifd_name.find("mk_canon_afinfo_") == 0
           || ifd_name.find("mk_canon_filterinfo_") == 0
           || ifd_name.find("mk_canon_colordata") == 0;
}

static int64_t
canon_compute_auto_value_base(const TiffConfig& cfg,
                              std::span<const std::byte> tiff_bytes,
                              uint64_t maker_note_off, uint16_t entry_count,
                              uint64_t ifd_needed_bytes,
                              const ExifDecodeLimits& limits) noexcept
{
    if (entry_count == 0 || ifd_needed_bytes == 0 || tiff_bytes.empty()) {
        return INT64_MIN;
    }

    const uint64_t entries_off = maker_note_off + 2ULL;
    uint64_t min_off32         = UINT64_MAX;

    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t type = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, eoff + 2U, &type)) {
            break;
        }

        uint32_t count32        = 0;
        uint32_t value_or_off32 = 0;
        if (!read_tiff_u32(cfg, tiff_bytes, eoff + 4U, &count32)
            || !read_tiff_u32(cfg, tiff_bytes, eoff + 8U, &value_or_off32)) {
            break;
        }

        const uint64_t unit  = tiff_type_size(type);
        const uint64_t count = count32;
        if (unit == 0 || count == 0 || count > (UINT64_MAX / unit)) {
            continue;
        }

        const uint64_t value_bytes = count * unit;
        if (value_bytes <= 4ULL || value_bytes > limits.max_value_bytes) {
            continue;
        }

        const uint64_t off = uint64_t(value_or_off32);
        if (off < ifd_needed_bytes) {
            continue;
        }
        min_off32 = (off < min_off32) ? off : min_off32;
    }

    if (min_off32 == UINT64_MAX) {
        return INT64_MIN;
    }

    const uint64_t value_area_off = maker_note_off + ifd_needed_bytes;
    if (value_area_off > uint64_t(INT64_MAX)
        || min_off32 > uint64_t(INT64_MAX)) {
        return INT64_MIN;
    }

    return static_cast<int64_t>(value_area_off)
           - static_cast<int64_t>(min_off32);
}

static int64_t
canon_compute_schema_value_base(uint64_t maker_note_off,
                                bool have_offset_schema,
                                int32_t offset_schema) noexcept
{
    if (!have_offset_schema || maker_note_off > uint64_t(INT64_MAX)) {
        return INT64_MIN;
    }

    const int64_t base_mn  = static_cast<int64_t>(maker_note_off);
    const int64_t schema64 = static_cast<int64_t>(offset_schema);
    if ((schema64 > 0 && base_mn > (INT64_MAX - schema64))
        || (schema64 < 0 && base_mn < (INT64_MIN - schema64))) {
        return INT64_MIN;
    }
    return base_mn + schema64;
}

static bool
canon_custom_functions2_signature_matches(std::span<const std::byte> tiff_bytes,
                                          uint64_t abs_value_off,
                                          uint64_t value_bytes) noexcept
{
    if (value_bytes < 8ULL || abs_value_off > tiff_bytes.size()
        || value_bytes > (tiff_bytes.size() - abs_value_off)) {
        return false;
    }

    const TiffConfig payload_cfg { true, false };
    uint16_t len16       = 0;
    uint32_t group_count = 0;
    if (!read_tiff_u16(payload_cfg, tiff_bytes, abs_value_off, &len16)
        || !read_tiff_u32(payload_cfg, tiff_bytes, abs_value_off + 4ULL,
                          &group_count)) {
        return false;
    }
    if (uint64_t(len16) != value_bytes) {
        return false;
    }
    return group_count <= 0x10000U;
}

static bool
canon_u32_binary_dir_signature_matches(std::span<const std::byte> tiff_bytes,
                                       uint64_t abs_value_off,
                                       uint64_t value_bytes) noexcept
{
    if (value_bytes < 8ULL || abs_value_off > tiff_bytes.size()
        || value_bytes > (tiff_bytes.size() - abs_value_off)) {
        return false;
    }

    const TiffConfig payload_cfg { true, false };
    uint32_t len32 = 0;
    if (!read_tiff_u32(payload_cfg, tiff_bytes, abs_value_off, &len32)) {
        return false;
    }
    return uint64_t(len32) == value_bytes;
}

static uint64_t
canon_choose_custom_functions2_offset(
    std::span<const std::byte> tiff_bytes, uint32_t value_or_off32,
    uint64_t default_abs_value_off, uint64_t value_bytes,
    uint64_t maker_note_off, uint64_t maker_note_ifd_bytes,
    int64_t auto_value_base, int64_t schema_value_base) noexcept
{
    if (!canon_value_overlaps_ifd(default_abs_value_off, value_bytes,
                                  maker_note_off, maker_note_ifd_bytes)
        && canon_custom_functions2_signature_matches(tiff_bytes,
                                                     default_abs_value_off,
                                                     value_bytes)) {
        return default_abs_value_off;
    }

    uint64_t abs_value_off = 0;
    if (auto_value_base != INT64_MIN
        && canon_add_special_base_and_off32(auto_value_base, value_or_off32,
                                            &abs_value_off)
        && abs_value_off != default_abs_value_off
        && !canon_value_overlaps_ifd(abs_value_off, value_bytes, maker_note_off,
                                     maker_note_ifd_bytes)
        && canon_custom_functions2_signature_matches(tiff_bytes, abs_value_off,
                                                     value_bytes)) {
        return abs_value_off;
    }

    if (schema_value_base != INT64_MIN
        && canon_add_special_base_and_off32(schema_value_base, value_or_off32,
                                            &abs_value_off)
        && abs_value_off != default_abs_value_off
        && !canon_value_overlaps_ifd(abs_value_off, value_bytes, maker_note_off,
                                     maker_note_ifd_bytes)
        && canon_custom_functions2_signature_matches(tiff_bytes, abs_value_off,
                                                     value_bytes)) {
        return abs_value_off;
    }

    if (maker_note_off <= uint64_t(INT64_MAX)
        && canon_add_special_base_and_off32(static_cast<int64_t>(maker_note_off),
                                            value_or_off32, &abs_value_off)
        && abs_value_off != default_abs_value_off
        && !canon_value_overlaps_ifd(abs_value_off, value_bytes, maker_note_off,
                                     maker_note_ifd_bytes)
        && canon_custom_functions2_signature_matches(tiff_bytes, abs_value_off,
                                                     value_bytes)) {
        return abs_value_off;
    }

    if (canon_add_special_base_and_off32(0, value_or_off32, &abs_value_off)
        && abs_value_off != default_abs_value_off
        && !canon_value_overlaps_ifd(abs_value_off, value_bytes, maker_note_off,
                                     maker_note_ifd_bytes)
        && canon_custom_functions2_signature_matches(tiff_bytes, abs_value_off,
                                                     value_bytes)) {
        return abs_value_off;
    }

    return default_abs_value_off;
}

static uint64_t
canon_choose_u32_binary_dir_offset(
    std::span<const std::byte> tiff_bytes, uint32_t value_or_off32,
    uint64_t default_abs_value_off, uint64_t value_bytes,
    uint64_t maker_note_off, uint64_t maker_note_ifd_bytes,
    int64_t auto_value_base, int64_t schema_value_base) noexcept
{
    if (!canon_value_overlaps_ifd(default_abs_value_off, value_bytes,
                                  maker_note_off, maker_note_ifd_bytes)
        && canon_u32_binary_dir_signature_matches(tiff_bytes,
                                                  default_abs_value_off,
                                                  value_bytes)) {
        return default_abs_value_off;
    }

    uint64_t abs_value_off = 0;
    if (auto_value_base != INT64_MIN
        && canon_add_special_base_and_off32(auto_value_base, value_or_off32,
                                            &abs_value_off)
        && abs_value_off != default_abs_value_off
        && !canon_value_overlaps_ifd(abs_value_off, value_bytes, maker_note_off,
                                     maker_note_ifd_bytes)
        && canon_u32_binary_dir_signature_matches(tiff_bytes, abs_value_off,
                                                  value_bytes)) {
        return abs_value_off;
    }

    if (schema_value_base != INT64_MIN
        && canon_add_special_base_and_off32(schema_value_base, value_or_off32,
                                            &abs_value_off)
        && abs_value_off != default_abs_value_off
        && !canon_value_overlaps_ifd(abs_value_off, value_bytes, maker_note_off,
                                     maker_note_ifd_bytes)
        && canon_u32_binary_dir_signature_matches(tiff_bytes, abs_value_off,
                                                  value_bytes)) {
        return abs_value_off;
    }

    if (maker_note_off <= uint64_t(INT64_MAX)
        && canon_add_special_base_and_off32(static_cast<int64_t>(maker_note_off),
                                            value_or_off32, &abs_value_off)
        && abs_value_off != default_abs_value_off
        && !canon_value_overlaps_ifd(abs_value_off, value_bytes, maker_note_off,
                                     maker_note_ifd_bytes)
        && canon_u32_binary_dir_signature_matches(tiff_bytes, abs_value_off,
                                                  value_bytes)) {
        return abs_value_off;
    }

    if (canon_add_special_base_and_off32(0, value_or_off32, &abs_value_off)
        && abs_value_off != default_abs_value_off
        && !canon_value_overlaps_ifd(abs_value_off, value_bytes, maker_note_off,
                                     maker_note_ifd_bytes)
        && canon_u32_binary_dir_signature_matches(tiff_bytes, abs_value_off,
                                                  value_bytes)) {
        return abs_value_off;
    }

    return default_abs_value_off;
}


static int64_t
guess_canon_value_base(const TiffConfig& cfg,
                       std::span<const std::byte> tiff_bytes,
                       uint64_t maker_note_off, uint64_t maker_note_bytes,
                       uint16_t entry_count, uint64_t ifd_needed_bytes,
                       const ExifDecodeLimits& limits, bool have_offset_schema,
                       int32_t offset_schema) noexcept
{
    if (tiff_bytes.empty() || maker_note_bytes == 0 || entry_count == 0
        || ifd_needed_bytes == 0) {
        return 0;
    }
    if (tiff_bytes.size() > static_cast<size_t>(INT64_MAX)) {
        return 0;
    }
    if (maker_note_off > tiff_bytes.size()
        || maker_note_bytes > (tiff_bytes.size() - maker_note_off)) {
        return 0;
    }

    const uint64_t entries_off = maker_note_off + 2ULL;
    if (uint64_t(entry_count) > (UINT64_MAX / 12ULL)) {
        return 0;
    }
    const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
    const uint64_t needed      = 2ULL + table_bytes + 4ULL;
    // Some Canon MakerNotes are stored as a truncated directory (count too
    // small) with out-of-line values placed elsewhere in the EXIF stream.
    // Treat maker_note_bytes as a soft bound: require only that the directory
    // itself fits in the available EXIF/TIFF buffer.
    if (needed > (tiff_bytes.size() - maker_note_off)) {
        return 0;
    }

    uint64_t min_off32 = UINT64_MAX;
    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t type = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, eoff + 2, &type)) {
            break;
        }

        uint32_t count32        = 0;
        uint32_t value_or_off32 = 0;
        if (!read_tiff_u32(cfg, tiff_bytes, eoff + 4, &count32)
            || !read_tiff_u32(cfg, tiff_bytes, eoff + 8, &value_or_off32)) {
            break;
        }

        const uint64_t count = count32;
        const uint64_t unit  = tiff_type_size(type);
        if (unit == 0 || count == 0 || count > (UINT64_MAX / unit)) {
            continue;
        }
        const uint64_t value_bytes = count * unit;
        if (value_bytes <= 4) {
            continue;
        }
        if (value_bytes > limits.max_value_bytes) {
            continue;
        }

        const uint64_t off = uint64_t(value_or_off32);
        if (off < ifd_needed_bytes) {
            // For the "auto base" heuristic, ignore offsets that point inside
            // the MakerNote directory itself. We want the earliest out-of-line
            // value offset that plausibly targets the value area.
            continue;
        }
        min_off32 = (off < min_off32) ? off : min_off32;
    }

    // Candidate bases:
    //  - 0: offsets are absolute (TIFF-relative).
    //  - maker_note_off: offsets are MakerNote-relative.
    //  - auto_base: offsets are relative to an adjusted base (ExifTool's
    //    "Adjusted MakerNotes base by ..."), chosen such that the earliest
    //    out-of-line value lands at the start of the MakerNote value area.
    const int64_t base_abs = 0;
    const int64_t base_mn  = (maker_note_off <= uint64_t(INT64_MAX))
                                 ? static_cast<int64_t>(maker_note_off)
                                 : base_abs;

    int64_t base_schema = INT64_MIN;
    if (have_offset_schema) {
        const int64_t schema64 = static_cast<int64_t>(offset_schema);
        if (!((schema64 > 0 && base_mn > (INT64_MAX - schema64))
              || (schema64 < 0 && base_mn < (INT64_MIN - schema64)))) {
            base_schema = base_mn + schema64;
        }
    }

    int64_t base_auto = INT64_MIN;
    if (min_off32 != UINT64_MAX) {
        const uint64_t value_area_off = maker_note_off + ifd_needed_bytes;
        if (value_area_off <= uint64_t(INT64_MAX)
            && min_off32 <= uint64_t(INT64_MAX)) {
            base_auto = static_cast<int64_t>(value_area_off)
                        - static_cast<int64_t>(min_off32);
        }
    }

    const auto score_signature_base = [&](int64_t base) noexcept -> uint32_t {
        uint32_t score = 0;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            uint16_t tag            = 0;
            uint16_t type           = 0;
            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, eoff + 0, &tag)
                || !read_tiff_u16(cfg, tiff_bytes, eoff + 2, &type)
                || !read_tiff_u32(cfg, tiff_bytes, eoff + 4, &count32)
                || !read_tiff_u32(cfg, tiff_bytes, eoff + 8, &value_or_off32)) {
                break;
            }

            const uint64_t unit  = tiff_type_size(type);
            const uint64_t count = count32;
            if (unit == 0 || count == 0 || count > (UINT64_MAX / unit)) {
                continue;
            }
            const uint64_t value_bytes = count * unit;
            if (value_bytes <= 4 || value_bytes > limits.max_value_bytes) {
                continue;
            }

            uint64_t abs_off = 0;
            if (!canon_add_base_and_off32(base, value_or_off32, &abs_off)) {
                continue;
            }
            if (abs_off + value_bytes > tiff_bytes.size()) {
                continue;
            }

            if (tag == 0x0099U && value_bytes >= 8U) {
                uint16_t len16 = 0;
                uint32_t len32 = 0;
                if ((read_tiff_u16(cfg, tiff_bytes, abs_off, &len16)
                     && uint64_t(len16) == value_bytes)
                    || (read_tiff_u32(cfg, tiff_bytes, abs_off, &len32)
                        && uint64_t(len32) == value_bytes)) {
                    score += 24U;
                }
                continue;
            }

            if (tag == 0x4024U && value_bytes >= 8U) {
                uint32_t len32 = 0;
                if (read_tiff_u32(cfg, tiff_bytes, abs_off, &len32)
                    && uint64_t(len32) == value_bytes) {
                    score += 24U;
                }
                continue;
            }

            if ((tag == 0x0006U || tag == 0x0007U || tag == 0x0009U
                 || tag == 0x0095U || tag == 0x0096U || tag == 0x4010U
                 || tag == 0x4012U)
                && (type == 2U || type == 129U)) {
                const std::span<const std::byte> raw
                    = tiff_bytes.subspan(static_cast<size_t>(abs_off),
                                         static_cast<size_t>(value_bytes));
                if (canon_looks_like_text(raw)) {
                    score += 8U;
                }
            }
        }
        return score;
    };

    struct Candidate final {
        int64_t base   = 0;
        uint32_t score = 0;
        uint32_t in_mn = 0;
    };

    Candidate cands[4] {};
    cands[0].base = base_abs;
    cands[1].base = base_mn;
    cands[2].base = (base_auto != INT64_MIN) ? base_auto : base_abs;
    cands[3].base = (base_schema != INT64_MIN) ? base_schema : base_abs;

    const bool cand_enabled[4]      = { true, true, base_auto != INT64_MIN,
                                        base_schema != INT64_MIN };
    const bool cand_overlaps_ifd[4] = {
        canon_value_base_overlaps_ifd(cfg, tiff_bytes, maker_note_off,
                                      ifd_needed_bytes, entry_count, limits,
                                      cands[0].base),
        canon_value_base_overlaps_ifd(cfg, tiff_bytes, maker_note_off,
                                      ifd_needed_bytes, entry_count, limits,
                                      cands[1].base),
        base_auto != INT64_MIN
            && canon_value_base_overlaps_ifd(cfg, tiff_bytes, maker_note_off,
                                             ifd_needed_bytes, entry_count,
                                             limits, cands[2].base),
        base_schema != INT64_MIN
            && canon_value_base_overlaps_ifd(cfg, tiff_bytes, maker_note_off,
                                             ifd_needed_bytes, entry_count,
                                             limits, cands[3].base),
    };

    uint32_t sig_scores[4] {};
    if (!cand_overlaps_ifd[0]) {
        sig_scores[0] = score_signature_base(base_abs);
    }
    if (!cand_overlaps_ifd[1]) {
        sig_scores[1] = score_signature_base(base_mn);
    }
    if (cand_enabled[2] && !cand_overlaps_ifd[2]) {
        sig_scores[2] = score_signature_base(base_auto);
    }
    if (cand_enabled[3] && !cand_overlaps_ifd[3]) {
        sig_scores[3] = score_signature_base(base_schema);
    }

    uint32_t best_sig     = 0U;
    size_t best_sig_index = 4U;
    bool sig_tied         = false;
    for (size_t i = 0U; i < 4U; ++i) {
        if (!cand_enabled[i] || cand_overlaps_ifd[i]) {
            continue;
        }
        if (best_sig_index == 4U || sig_scores[i] > best_sig) {
            best_sig       = sig_scores[i];
            best_sig_index = i;
            sig_tied       = false;
        } else if (sig_scores[i] == best_sig && sig_scores[i] != 0U) {
            sig_tied = true;
        }
    }

    if (best_sig != 0U && !sig_tied && best_sig_index < 4U) {
        return cands[best_sig_index].base;
    }

    for (size_t c = 0; c < 4; ++c) {
        Candidate& cand = cands[c];
        if (!cand_enabled[c] || cand_overlaps_ifd[c]) {
            continue;
        }

        uint16_t tag  = 0;
        uint16_t type = 0;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            if (!read_tiff_u16(cfg, tiff_bytes, eoff + 0, &tag)
                || !read_tiff_u16(cfg, tiff_bytes, eoff + 2, &type)) {
                break;
            }

            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_tiff_u32(cfg, tiff_bytes, eoff + 4, &count32)
                || !read_tiff_u32(cfg, tiff_bytes, eoff + 8, &value_or_off32)) {
                break;
            }

            const uint64_t count = count32;
            const uint64_t unit  = tiff_type_size(type);
            if (unit == 0 || count == 0 || count > (UINT64_MAX / unit)) {
                continue;
            }
            const uint64_t value_bytes = count * unit;
            if (value_bytes <= 4 || value_bytes > limits.max_value_bytes) {
                continue;
            }

            uint64_t abs_off = 0;
            if (!canon_add_base_and_off32(cand.base, value_or_off32, &abs_off)) {
                continue;
            }

            if (abs_off + value_bytes > tiff_bytes.size()) {
                continue;
            }

            cand.score += 1;

            // CanonCustom2 (tag 0x0099) is a strong signal for correct offset
            // base: its payload begins with a u16 length field equal to the
            // full byte size.
            if (tag == 0x0099 && value_bytes >= 8) {
                uint16_t len16 = 0;
                if (read_tiff_u16(cfg, tiff_bytes, abs_off + 0, &len16)
                    && uint64_t(len16) == value_bytes) {
                    cand.score += 8;
                }
            }

            if (abs_off >= maker_note_off
                && (abs_off + value_bytes)
                       <= (maker_note_off + maker_note_bytes)) {
                cand.in_mn += 1;
                cand.score += 1;
                if (abs_off >= (maker_note_off + ifd_needed_bytes)) {
                    cand.score += 1;
                }
            }

            if (type == 2 || type == 129) {
                const std::span<const std::byte> raw
                    = tiff_bytes.subspan(static_cast<size_t>(abs_off),
                                         static_cast<size_t>(value_bytes));
                if (canon_looks_like_text(raw)) {
                    cand.score += 3;
                }
            }
        }
    }

    Candidate best {};
    size_t best_index = 4U;
    for (size_t i = 0; i < 4; ++i) {
        if (!cand_enabled[i] || cand_overlaps_ifd[i]) {
            continue;
        }
        const Candidate cand = cands[i];
        if (best_index == 4U) {
            best       = cand;
            best_index = i;
            continue;
        }
        if (cand.score > best.score) {
            best = cand;
            best_index = i;
            continue;
        }
        if (cand.score < best.score) {
            continue;
        }
        if (cand.in_mn > best.in_mn) {
            best = cand;
            best_index = i;
            continue;
        }
    }
    if (best_index == 4U) {
        best = cands[0];
    }

    // A short declared MakerNote byte count can stop at the end of the IFD,
    // while a value whose offset equals the IFD end still uses the
    // MakerNote-relative base. In this exact-boundary case, prefer that base
    // over an equally plausible prefix address. Adjusted-base and in-payload
    // evidence above still wins.
    if (!cand_overlaps_ifd[0] && best.base == base_abs && !cand_overlaps_ifd[1]
        && cands[1].score != 0U && cands[1].score == cands[0].score
        && cands[1].in_mn == cands[0].in_mn && min_off32 == ifd_needed_bytes) {
        return cands[1].base;
    }

    return best.base;
}

static void
decode_canon_camerainfo_fixed_fields(
    const TiffConfig& cfg, std::span<const std::byte> cam,
    std::string_view ifd_name, std::string_view canon_model, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || cam.empty()) {
        return;
    }

    const uint64_t value_bytes = cam.size();
    const BlockId block        = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    // ExifTool exposes CanonCameraInfo fixed-layout fields with tag ids equal
    // to byte offsets within the blob.
    static constexpr CanonCameraInfoField kFields[] = {
        { 0x0018, CanonCameraInfoFieldKind::U16Array4, 8 },  // WB_RGGBLevelsAuto
        { 0x0022, CanonCameraInfoFieldKind::U16Array4,
          8 },                                         // WB_RGGBLevelsAsShot
        { 0x0026, CanonCameraInfoFieldKind::U16, 2 },  // ColorTempAsShot
        { 0x0027, CanonCameraInfoFieldKind::U16Array4,
          8 },                                         // WB_RGGBLevelsDaylight
        { 0x002b, CanonCameraInfoFieldKind::U16, 2 },  // ColorTempDaylight
        { 0x002c, CanonCameraInfoFieldKind::U16Array4,
          8 },                                        // WB_RGGBLevelsShade
        { 0x002d, CanonCameraInfoFieldKind::U8, 1 },  // FocalType
        { 0x0031, CanonCameraInfoFieldKind::U16Array4,
          8 },                                         // WB_RGGBLevelsCloudy
        { 0x0035, CanonCameraInfoFieldKind::U16, 2 },  // ColorTempCloudy
        { 0x0036, CanonCameraInfoFieldKind::U16Array4,
          8 },                                         // WB_RGGBLevelsTungsten
        { 0x0037, CanonCameraInfoFieldKind::U16, 2 },  // ColorTemperature
        { 0x0039, CanonCameraInfoFieldKind::U8, 1 },   // CanonImageSize
        { 0x003a, CanonCameraInfoFieldKind::U16, 2 },  // ColorTempTungsten
        { 0x003b, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsFluorescent
        { 0x0045, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsFlash
        { 0x004a, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsUnknown2
        { 0x004f, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsUnknown3
        { 0x0059, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsUnknown5
        { 0x005e, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsUnknown6
        { 0x0063, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsUnknown7
        { 0x006d, CanonCameraInfoFieldKind::U16Array4,
          8 },                                        // WB_RGGBLevelsUnknown9
        { 0x006e, CanonCameraInfoFieldKind::U8, 1 },  // Saturation
        { 0x0072, CanonCameraInfoFieldKind::U8, 1 },  // Sharpness
        { 0x0077, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsUnknown11
        { 0x0081, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsUnknown13
        { 0x0086, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsUnknown14
        { 0x008b, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsUnknown15
        { 0x009a, CanonCameraInfoFieldKind::U16Array4, 8 },  // WB_RGGBLevelsPC3
        { 0x009f, CanonCameraInfoFieldKind::U16Array4,
          8 },  // WB_RGGBLevelsUnknown16
        { 0x0041, CanonCameraInfoFieldKind::U8, 1 },
        { 0x0042, CanonCameraInfoFieldKind::U8, 1 },
        { 0x0044, CanonCameraInfoFieldKind::U8, 1 },
        { 0x0048, CanonCameraInfoFieldKind::U16, 2 },
        { 0x004B, CanonCameraInfoFieldKind::U8, 1 },
        { 0x0047, CanonCameraInfoFieldKind::U8, 1 },
        { 0x004A, CanonCameraInfoFieldKind::U8, 1 },
        { 0x004E, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0051, CanonCameraInfoFieldKind::U8, 1 },
        { 0x006F, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0073, CanonCameraInfoFieldKind::U16, 2 },
        { 0x00DE, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x00A5, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0095, CanonCameraInfoFieldKind::AsciiFixed, 64 },
        { 0x0107, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x010a, CanonCameraInfoFieldKind::U8, 1 },
        { 0x010B, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x010c, CanonCameraInfoFieldKind::U8, 1 },
        { 0x010F, CanonCameraInfoFieldKind::AsciiFixed, 32 },
        { 0x0110, CanonCameraInfoFieldKind::U8, 1 },
        { 0x0133, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0136, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0137, CanonCameraInfoFieldKind::U32, 4 },
        { 0x013a, CanonCameraInfoFieldKind::U16, 2 },
        { 0x013F, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0143, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0111, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0113, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0115, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0112, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0114, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0116, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0127, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0129, CanonCameraInfoFieldKind::U16, 2 },
        { 0x012B, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0131, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0135, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0169, CanonCameraInfoFieldKind::U8, 1 },
        { 0x0184, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0186, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0188, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0190, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0199, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x019B, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x01A4, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x01D3, CanonCameraInfoFieldKind::U32, 4 },
        { 0x01D9, CanonCameraInfoFieldKind::U32, 4 },
        { 0x01DB, CanonCameraInfoFieldKind::U32, 4 },
        { 0x01E4, CanonCameraInfoFieldKind::U32, 4 },
        { 0x01E7, CanonCameraInfoFieldKind::U32, 4 },
        { 0x01ED, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x01F0, CanonCameraInfoFieldKind::U32, 4 },
        { 0x01F7, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0201, CanonCameraInfoFieldKind::U32, 4 },
        { 0x021B, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0220, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0238, CanonCameraInfoFieldKind::U32, 4 },
        { 0x023C, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0256, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x025E, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x016B, CanonCameraInfoFieldKind::AsciiFixed, 16 },
        { 0x014f, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0151, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0153, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0155, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0157, CanonCameraInfoFieldKind::U16, 2 },
        { 0x015e, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0164, CanonCameraInfoFieldKind::AsciiFixed, 16 },
        { 0x0161, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0163, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0165, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0166, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x0168, CanonCameraInfoFieldKind::U16, 2 },
        { 0x016a, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0172, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0176, CanonCameraInfoFieldKind::U32, 4 },
        { 0x017E, CanonCameraInfoFieldKind::U32, 4 },
        { 0x045E, CanonCameraInfoFieldKind::AsciiFixed, 20 },
        { 0x045A, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x04AE, CanonCameraInfoFieldKind::U32, 4 },
        { 0x04BA, CanonCameraInfoFieldKind::U32, 4 },
        { 0x05C1, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x043D, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0449, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x0270, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0274, CanonCameraInfoFieldKind::U32, 4 },
        { 0x027C, CanonCameraInfoFieldKind::U32, 4 },
        { 0x028C, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0290, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0293, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0298, CanonCameraInfoFieldKind::U32, 4 },
        { 0x029C, CanonCameraInfoFieldKind::U32, 4 },
        { 0x01A7, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x01A9, CanonCameraInfoFieldKind::U16, 2 },
        { 0x01AB, CanonCameraInfoFieldKind::U16, 2 },
        { 0x0189, CanonCameraInfoFieldKind::U16Rev, 2 },
        { 0x018B, CanonCameraInfoFieldKind::U16, 2 },
        { 0x018D, CanonCameraInfoFieldKind::U16, 2 },
        { 0x01AC, CanonCameraInfoFieldKind::AsciiFixed, 6 },
        { 0x01BB, CanonCameraInfoFieldKind::U32, 4 },
        { 0x01C7, CanonCameraInfoFieldKind::U32, 4 },
        { 0x01EB, CanonCameraInfoFieldKind::U32, 4 },
        { 0x02AA, CanonCameraInfoFieldKind::U32, 4 },
        { 0x02B6, CanonCameraInfoFieldKind::U32, 4 },
        { 0x02B3, CanonCameraInfoFieldKind::U32, 4 },
        { 0x02BF, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0933, CanonCameraInfoFieldKind::AsciiFixed, 64 },
        { 0x0937, CanonCameraInfoFieldKind::AsciiFixed, 64 },
        { 0x092B, CanonCameraInfoFieldKind::AsciiFixed, 64 },
        { 0x0AF1, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0B21, CanonCameraInfoFieldKind::U32, 4 },
        { 0x0B2D, CanonCameraInfoFieldKind::U32, 4 },
        { 0x026a, CanonCameraInfoFieldKind::U32Array4, 16 },
    };

    uint32_t order                                           = 0;
    const std::span<const CanonCameraInfoField> field_sets[] = {
        std::span<const CanonCameraInfoField>(kFields),
        canon_camerainfo_extra_fields(ifd_name),
    };

    for (size_t set_i = 0; set_i < (sizeof(field_sets) / sizeof(field_sets[0]));
         ++set_i) {
        const std::span<const CanonCameraInfoField> fields = field_sets[set_i];
        for (size_t fi = 0; fi < fields.size(); ++fi) {
            const CanonCameraInfoField f = fields[fi];
            if (uint64_t(f.tag) + f.bytes > value_bytes) {
                continue;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }

            Entry e;
            e.key          = make_exif_tag_key(store.arena(), ifd_name, f.tag);
            e.origin.block = block;
            e.origin.order_in_block = order++;
            e.flags |= EntryFlags::Derived;

            switch (f.kind) {
            case CanonCameraInfoFieldKind::U8: {
                e.origin.wire_type  = WireType { WireFamily::Tiff, 1 };
                e.origin.wire_count = 1;
                e.value             = make_u8(u8(cam[f.tag]));
                break;
            }
            case CanonCameraInfoFieldKind::U16: {
                uint16_t v    = 0;
                const bool ok = cfg.le ? read_u16le(cam, f.tag, &v)
                                       : read_u16be(cam, f.tag, &v);
                if (!ok) {
                    continue;
                }
                e.origin.wire_type  = WireType { WireFamily::Tiff, 3 };
                e.origin.wire_count = 1;
                e.value             = make_u16(v);
                break;
            }
            case CanonCameraInfoFieldKind::U16Rev: {
                uint16_t v    = 0;
                const bool ok = cfg.le ? read_u16be(cam, f.tag, &v)
                                       : read_u16le(cam, f.tag, &v);
                if (!ok) {
                    continue;
                }
                e.origin.wire_type  = WireType { WireFamily::Tiff, 3 };
                e.origin.wire_count = 1;
                e.value             = make_u16(v);
                break;
            }
            case CanonCameraInfoFieldKind::U16Array4: {
                uint16_t v[4] = {};
                bool ok       = true;
                for (uint32_t i = 0; i < 4; ++i) {
                    const uint64_t off = uint64_t(f.tag) + uint64_t(i) * 2ULL;
                    uint16_t t         = 0;
                    if (!(cfg.le ? read_u16le(cam, off, &t)
                                 : read_u16be(cam, off, &t))) {
                        ok = false;
                        break;
                    }
                    v[i] = t;
                }
                if (!ok) {
                    continue;
                }
                e.origin.wire_type  = WireType { WireFamily::Tiff, 3 };
                e.origin.wire_count = 4;
                e.value             = make_u16_array(store.arena(),
                                                     std::span<const uint16_t>(v));
                break;
            }
            case CanonCameraInfoFieldKind::U32: {
                uint32_t v    = 0;
                const bool ok = cfg.le ? read_u32le(cam, f.tag, &v)
                                       : read_u32be(cam, f.tag, &v);
                if (!ok) {
                    continue;
                }
                e.origin.wire_type  = WireType { WireFamily::Tiff, 4 };
                e.origin.wire_count = 1;
                e.value             = make_u32(v);
                break;
            }
            case CanonCameraInfoFieldKind::U32Array4: {
                uint32_t v[4] = {};
                bool ok       = true;
                for (uint32_t i = 0; i < 4; ++i) {
                    const uint64_t off = uint64_t(f.tag) + uint64_t(i) * 4ULL;
                    uint32_t t         = 0;
                    if (!(cfg.le ? read_u32le(cam, off, &t)
                                 : read_u32be(cam, off, &t))) {
                        ok = false;
                        break;
                    }
                    v[i] = t;
                }
                if (!ok) {
                    continue;
                }
                e.origin.wire_type  = WireType { WireFamily::Tiff, 4 };
                e.origin.wire_count = 4;
                e.value             = make_u32_array(store.arena(),
                                                     std::span<const uint32_t>(v));
                break;
            }
            case CanonCameraInfoFieldKind::AsciiFixed: {
                const char* p = reinterpret_cast<const char*>(cam.data()
                                                              + size_t(f.tag));
                size_t n      = 0;
                for (; n < f.bytes; ++n) {
                    if (p[n] == '\0') {
                        break;
                    }
                }
                const std::string_view txt(p, n);
                e.origin.wire_type  = WireType { WireFamily::Tiff, 2 };
                e.origin.wire_count = f.bytes;
                e.value = make_text(store.arena(), txt, TextEncoding::Ascii);
                break;
            }
            }

            canon_maybe_mark_contextual_name(canon_model, ifd_name, f.tag, &e);
            (void)store.add_entry(e);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }
}

enum class CanonCustomMode : uint8_t {
    LowByteAsU8,
    U16,
};

enum class CanonCustomTagMode : uint8_t {
    Index,
    HighByte,
};

static void
decode_canon_custom_word_table(
    const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
    uint64_t value_off, uint32_t count, std::string_view ifd_name,
    uint16_t tag_base, CanonCustomTagMode tag_mode, CanonCustomMode mode,
    std::string_view canon_model, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || count == 0) {
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

    uint16_t first = 0;
    bool has_first = read_tiff_u16(cfg, tiff_bytes, value_off, &first);

    uint32_t start = 0;
    if (has_first && count <= (UINT16_MAX / 2U)) {
        const uint16_t expected = static_cast<uint16_t>(count * 2U);
        const uint32_t first32  = static_cast<uint32_t>(first);
        const uint32_t exp32    = static_cast<uint32_t>(expected);
        if (first32 == exp32 || (first32 + 2U) == exp32) {
            start = 1;
        }
    }

    uint32_t order = 0;
    for (uint32_t i = start; i < count; ++i) {
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        uint16_t w = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, value_off + uint64_t(i) * 2ULL,
                           &w)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return;
        }

        uint16_t tag = 0;
        if (tag_mode == CanonCustomTagMode::HighByte) {
            tag = static_cast<uint16_t>((w >> 8) & 0xFFu);
        } else {
            const uint32_t tag32 = uint32_t(tag_base) + (i - start);
            if (tag32 > 0xFFFFu) {
                break;
            }
            tag = static_cast<uint16_t>(tag32);
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = order++;
        entry.flags |= EntryFlags::Derived;

        if (mode == CanonCustomMode::LowByteAsU8) {
            entry.origin.wire_type  = WireType { WireFamily::Tiff, 1 };
            entry.origin.wire_count = 1;
            entry.value             = make_u8(static_cast<uint8_t>(w & 0xFFu));
        } else {
            entry.origin.wire_type  = WireType { WireFamily::Tiff, 3 };
            entry.origin.wire_count = 1;
            entry.value             = make_u16(w);
        }

        canon_maybe_mark_contextual_name(canon_model, ifd_name, tag, &entry);
        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static void
decode_canon_u16_table(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t value_off, uint32_t count,
                       std::string_view ifd_name, MetaStore& store,
                       std::string_view canon_model,
                       const ExifDecodeOptions& options,
                       ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || count == 0) {
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

    const bool emit_unknown = canon_should_emit_unknown_table_tags(ifd_name);

    for (uint32_t i = 0; i < count; ++i) {
        if (i > 0xFFFFu) {
            break;
        }

        const uint16_t tag = static_cast<uint16_t>(i);
        if (!emit_unknown && exif_tag_name(ifd_name, tag).empty()) {
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        uint16_t v = 0;
        if (!read_tiff_u16(cfg, bytes, value_off + uint64_t(i) * 2ULL, &v)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 3 };
        entry.origin.wire_count     = 1;
        entry.value                 = make_u16(v);
        entry.flags |= EntryFlags::Derived;

        canon_maybe_mark_contextual_name(canon_model, ifd_name, tag, &entry);
        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static std::string_view
canoncustom_subtable_for_tag_0x000f(std::string_view model,
                                    uint32_t count) noexcept
{
    if (canon_ascii_contains_insensitive(model, "EOS 5D")) {
        return "functions5d";
    }
    if (canon_ascii_contains_insensitive(model, "EOS 10D")) {
        return "functions10d";
    }
    if (canon_ascii_contains_insensitive(model, "EOS 20D")) {
        return "functions20d";
    }
    if (canon_ascii_contains_insensitive(model, "EOS 30D")) {
        return "functions30d";
    }
    if (canon_ascii_contains_insensitive(model, "400D")
        || canon_ascii_contains_insensitive(model, "REBEL XTi")
        || canon_ascii_contains_insensitive(model, "Kiss Digital X")
        || canon_ascii_contains_insensitive(model, "K236")) {
        return "functions400d";
    }
    if (canon_ascii_contains_insensitive(model, "350D")
        || canon_ascii_contains_insensitive(model, "REBEL XT")
        || canon_ascii_contains_insensitive(model, "Kiss Digital N")) {
        return "functions350d";
    }
    if (canon_ascii_contains_insensitive(model, "EOS D30")) {
        return "functionsd30";
    }
    if (canon_ascii_contains_insensitive(model, "EOS D60")) {
        return "functionsd30";
    }
    switch (count) {
    case 9: return "functions350d";
    case 11: return "functions400d";
    case 15: return "functionsd30";
    case 16: return "functionsd30";
    case 17: return "functions10d";
    case 18: return "functions20d";
    case 19: return "functions30d";
    case 21: return "functions5d";
    case 22: return "functions1d";
    default: break;
    }
    return "functionsunknown";
}


static void
decode_canon_u32_table(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t value_off, uint32_t count,
                       std::string_view ifd_name, MetaStore& store,
                       const ExifDecodeOptions& options,
                       ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || count == 0) {
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

    const bool emit_unknown = canon_should_emit_unknown_table_tags(ifd_name);

    for (uint32_t i = 0; i < count; ++i) {
        if (i > 0xFFFFu) {
            break;
        }

        const uint16_t tag = static_cast<uint16_t>(i);
        if (!emit_unknown && exif_tag_name(ifd_name, tag).empty()) {
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        uint32_t v = 0;
        if (!read_tiff_u32(cfg, bytes, value_off + uint64_t(i) * 4ULL, &v)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 4 };
        entry.origin.wire_count     = 1;
        entry.value                 = make_u32(v);
        entry.flags |= EntryFlags::Derived;

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static int32_t
canon_to_i32(uint32_t v) noexcept
{
    // Avoid implementation-defined conversions from uint32_t -> int32_t.
    return (v <= 0x7FFFFFFFu)
               ? static_cast<int32_t>(v)
               : static_cast<int32_t>(static_cast<int64_t>(v) - 4294967296LL);
}

static void
decode_canon_i32_table(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t value_off, uint32_t count,
                       std::string_view ifd_name, MetaStore& store,
                       const ExifDecodeOptions& options,
                       ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || count == 0) {
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

    const bool emit_unknown = canon_should_emit_unknown_table_tags(ifd_name);

    for (uint32_t i = 0; i < count; ++i) {
        if (i > 0xFFFFu) {
            break;
        }

        const uint16_t tag = static_cast<uint16_t>(i);
        if (!emit_unknown && exif_tag_name(ifd_name, tag).empty()) {
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        uint32_t v = 0;
        if (!read_tiff_u32(cfg, bytes, value_off + uint64_t(i) * 4ULL, &v)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 9 };
        entry.origin.wire_count     = 1;
        entry.value                 = make_i32(canon_to_i32(v));
        entry.flags |= EntryFlags::Derived;

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}


static void
decode_canon_wbinfo(const TiffConfig& cfg, std::span<const std::byte> raw,
                    MetaStore& store, const ExifDecodeOptions& options,
                    ExifDecodeResult* result) noexcept
{
    static constexpr uint16_t tags[] = {
        0x0002U, 0x000aU, 0x0012U, 0x001aU, 0x0022U,
        0x002aU, 0x0032U, 0x003aU, 0x0042U, 0x004aU
    };
    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        update_status(result, ExifDecodeStatus::LimitExceeded);
        return;
    }
    uint32_t emitted = 0U;
    for (uint16_t tag : tags) {
        const size_t offset = static_cast<size_t>(tag) * 4U;
        if (offset > raw.size() || raw.size() - offset < 16U) {
            continue;
        }
        if (emitted >= options.limits.max_entries_per_ifd
            || store.entries().size() >= options.limits.max_total_entries
            || options.limits.max_value_bytes < 16U) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return;
        }
        std::array<int32_t, 4> levels {};
        for (size_t i = 0U; i < levels.size(); ++i) {
            uint32_t value = 0U;
            (void)read_tiff_u32(cfg, raw, offset + i * 4U, &value);
            levels[i] = canon_to_i32(value);
        }
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), "mk_canon_wbinfo_0", tag);
        entry.origin.block = block;
        entry.origin.order_in_block = emitted;
        entry.origin.wire_type = WireType { WireFamily::Other, 0U };
        entry.origin.wire_count = 4U;
        entry.value = make_i32_array(store.arena(), levels);
        entry.flags = EntryFlags::Derived;
        if (store.add_entry(entry) == kInvalidEntryId) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return;
        }
        ++emitted;
        if (result) {
            ++result->entries_decoded;
        }
    }
}


static void
decode_canon_psinfo_table(std::span<const std::byte> bytes, uint64_t value_off,
                          uint64_t value_bytes, std::string_view ifd_name,
                          MetaStore& store, const ExifDecodeOptions& options,
                          ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty()) {
        return;
    }
    if (value_bytes == 0) {
        return;
    }
    if (value_off > bytes.size()) {
        return;
    }
    if (value_bytes > (bytes.size() - value_off)) {
        return;
    }

    // psinfo/psinfo2: fixed-layout Canon picture style tables (byte offsets).
    // Most fields are int32, with a few u16 fields near the end.
    const bool is_psinfo2       = ifd_name.find("mk_canon_psinfo2") == 0;
    const uint16_t kUserDefTag1 = is_psinfo2 ? uint16_t(0x00f0)
                                             : uint16_t(0x00d8);
    const uint16_t kUserDefTag2 = is_psinfo2 ? uint16_t(0x00f2)
                                             : uint16_t(0x00da);
    const uint16_t kUserDefTag3 = is_psinfo2 ? uint16_t(0x00f4)
                                             : uint16_t(0x00dc);
    const uint16_t kMaxTag = is_psinfo2 ? uint16_t(0x00f4) : uint16_t(0x00dc);

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    uint32_t order = 0;
    for (uint16_t tag = 0; tag <= kMaxTag; tag = uint16_t(tag + 2U)) {
        if ((uint64_t(tag) + 2U) > value_bytes) {
            break;
        }

        if (exif_tag_name(ifd_name, tag).empty()) {
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = order++;
        entry.flags |= EntryFlags::Derived;

        if (tag == kUserDefTag1 || tag == kUserDefTag2 || tag == kUserDefTag3) {
            if ((uint64_t(tag) + 2U) > value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }

            uint16_t v = 0;
            if (!read_u16le(bytes, value_off + tag, &v)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }
            entry.origin.wire_type  = WireType { WireFamily::Tiff, 3 };
            entry.origin.wire_count = 1;
            entry.value             = make_u16(v);
        } else {
            if ((uint64_t(tag) + 4U) > value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }

            uint32_t u = 0;
            if (!read_u32le(bytes, value_off + tag, &u)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }
            entry.origin.wire_type  = WireType { WireFamily::Tiff, 9 };
            entry.origin.wire_count = 1;
            entry.value             = make_i32(static_cast<int32_t>(u));
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}


static bool
decode_canon_afinfo2_add_u16_scalar(
    const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
    uint64_t value_off, std::string_view mk_ifd0, BlockId block, uint32_t order,
    uint16_t tag, uint32_t word_index, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    if (store.entries().size() >= options.limits.max_total_entries) {
        update_status(status_out, ExifDecodeStatus::LimitExceeded);
        return false;
    }

    uint16_t v = 0;
    if (!read_tiff_u16(cfg, tiff_bytes, value_off + uint64_t(word_index) * 2ULL,
                       &v)) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::Malformed);
        }
        return false;
    }

    Entry entry;
    entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, tag);
    entry.origin.block = block;
    entry.origin.order_in_block = order;
    entry.origin.wire_type      = WireType { WireFamily::Tiff, 3 };
    entry.origin.wire_count     = 1;
    entry.value                 = make_u16(v);
    entry.flags |= EntryFlags::Derived;

    if (store.add_entry(entry) == kInvalidEntryId) {
        update_status(status_out, ExifDecodeStatus::LimitExceeded);
        return false;
    }
    if (status_out) {
        status_out->entries_decoded += 1;
    }
    return true;
}

static bool
canon_afinfo2_words_fit(uint64_t word_count, uint64_t first_word,
                        uint64_t count) noexcept
{
    return first_word <= word_count && count <= word_count - first_word;
}

static bool
decode_canon_afinfo2_add_i16_array(
    const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
    uint64_t value_off, std::string_view ifd_name, BlockId block,
    uint32_t order, uint16_t tag, uint64_t first_word, uint32_t count,
    MetaStore& store, const ExifDecodeOptions& options,
    ExifDecodeResult* status_out) noexcept
{
    if (status_out
        && status_out->entries_decoded >= options.limits.max_total_entries) {
        update_status(status_out, ExifDecodeStatus::LimitExceeded);
        return false;
    }

    if (first_word > (UINT64_MAX / 2U) || uint64_t(count) > (UINT64_MAX / 2U)) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return false;
    }
    const uint64_t byte_offset = first_word * 2U;
    const uint64_t byte_count  = uint64_t(count) * 2U;
    if (value_off > tiff_bytes.size()
        || byte_offset > tiff_bytes.size() - value_off
        || byte_count > tiff_bytes.size() - value_off - byte_offset) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return false;
    }

    Entry entry;
    entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
    entry.origin.block = block;
    entry.origin.order_in_block = order;
    entry.origin.wire_type      = WireType { WireFamily::Tiff, 8U };
    entry.origin.wire_count     = count;
    entry.value                 = decode_tiff_value(cfg, tiff_bytes, 8U, count,
                                                    value_off + byte_offset, byte_count,
                                                    store.arena(), options.limits, status_out);
    entry.flags |= EntryFlags::Derived;
    if (store.add_entry(entry) == kInvalidEntryId) {
        update_status(status_out, ExifDecodeStatus::LimitExceeded);
        return false;
    }
    if (status_out) {
        ++status_out->entries_decoded;
    }
    return true;
}

static bool
canon_afinfo2_header_looks_valid(const TiffConfig& cfg,
                                 std::span<const std::byte> header,
                                 uint64_t value_bytes,
                                 const ExifDecodeLimits& limits,
                                 CanonAfInfo2ModelClass model_class,
                                 bool afinfo3_present,
                                 bool afinfo3_context_known) noexcept
{
    if (value_bytes < 16U || (value_bytes % 2U) != 0U
        || value_bytes > limits.max_value_bytes || value_bytes > UINT16_MAX
        || header.size() < 16U) {
        return false;
    }

    const uint32_t word_count = static_cast<uint32_t>(value_bytes / 2U);
    if (word_count < 10U) {
        return false;
    }

    uint16_t size_bytes = 0;
    if (!read_tiff_u16(cfg, header, 0U, &size_bytes)
        || size_bytes != value_bytes) {
        return false;
    }

    uint16_t num_points = 0;
    if (!read_tiff_u16(cfg, header, 4U, &num_points)) {
        return false;
    }
    if (num_points == 0U || uint32_t(num_points) > limits.max_entries_per_ifd) {
        return false;
    }

    const uint64_t n          = uint64_t(num_points);
    const uint64_t mask_words = (n + 15U) / 16U;
    uint64_t required_words   = 8U + 4U * n + mask_words;
    if (model_class == CanonAfInfo2ModelClass::Eos) {
        required_words += mask_words;
    } else if (model_class == CanonAfInfo2ModelClass::KnownNonEos) {
        required_words += mask_words + 1U;
        if (afinfo3_context_known && !afinfo3_present) {
            required_words += 1U;
        }
    }
    if (uint64_t(word_count) < required_words) {
        return false;
    }

    uint16_t image_width  = 0;
    uint16_t image_height = 0;
    uint16_t af_width     = 0;
    uint16_t af_height    = 0;
    if (!read_tiff_u16(cfg, header, 8U, &image_width)
        || !read_tiff_u16(cfg, header, 10U, &image_height)
        || !read_tiff_u16(cfg, header, 12U, &af_width)
        || !read_tiff_u16(cfg, header, 14U, &af_height)) {
        return false;
    }
    return image_width != 0U && image_height != 0U && af_width != 0U
           && af_height != 0U;
}

static bool
canon_afinfo2_payload_looks_valid(const TiffConfig& cfg,
                                  std::span<const std::byte> tiff_bytes,
                                  uint64_t value_off, uint64_t value_bytes,
                                  const ExifDecodeLimits& limits,
                                  CanonAfInfo2ModelClass model_class,
                                  bool afinfo3_present,
                                  bool afinfo3_context_known) noexcept
{
    if (value_off > tiff_bytes.size()
        || value_bytes > tiff_bytes.size() - value_off || value_bytes < 16U) {
        return false;
    }
    return canon_afinfo2_header_looks_valid(
        cfg, tiff_bytes.subspan(static_cast<size_t>(value_off), 16U),
        value_bytes, limits, model_class, afinfo3_present,
        afinfo3_context_known);
}

static bool
canon_find_afinfo2_payload(
    const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
    uint64_t maker_note_off, uint64_t maker_note_ifd_bytes,
    uint64_t maker_note_span_bytes, uint64_t value_bytes,
    uint64_t default_value_off, const ExifDecodeLimits& limits,
    CanonAfInfo2ModelClass model_class, bool afinfo3_present,
    bool afinfo3_context_known, uint64_t* out_value_off) noexcept
{
    if (!out_value_off || maker_note_off > tiff_bytes.size()) {
        return false;
    }

    uint64_t span_bytes      = maker_note_span_bytes;
    const uint64_t available = tiff_bytes.size() - maker_note_off;
    if (span_bytes > available) {
        span_bytes = available;
    }
    if (value_bytes > span_bytes) {
        return false;
    }

    const uint64_t last = maker_note_off + (span_bytes - value_bytes);
    for (uint64_t off = maker_note_off; off <= last; off += 2U) {
        if (off == default_value_off) {
            continue;
        }
        if (canon_value_overlaps_ifd(off, value_bytes, maker_note_off,
                                     maker_note_ifd_bytes)) {
            continue;
        }
        if (canon_afinfo2_payload_looks_valid(cfg, tiff_bytes, off, value_bytes,
                                              limits, model_class,
                                              afinfo3_present,
                                              afinfo3_context_known)) {
            *out_value_off = off;
            return true;
        }
    }
    return false;
}

static bool
decode_canon_afinfo2(const TiffConfig& cfg,
                     std::span<const std::byte> tiff_bytes, uint64_t value_off,
                     uint64_t value_bytes, std::string_view mk_ifd0,
                     CanonAfInfo2ModelClass model_class, bool afinfo3_present,
                     bool afinfo3_context_known, MetaStore& store,
                     const ExifDecodeOptions& options,
                     ExifDecodeResult* status_out,
                     bool* fields_complete_out) noexcept
{
    if (fields_complete_out) {
        *fields_complete_out = false;
    }
    if (mk_ifd0.empty()) {
        return false;
    }
    if (value_bytes < 16U || value_bytes > UINT16_MAX) {
        return false;
    }
    if (value_bytes > options.limits.max_value_bytes) {
        return false;
    }
    if (value_off > tiff_bytes.size()
        || value_bytes > tiff_bytes.size() - value_off) {
        return false;
    }
    if ((value_bytes % 2U) != 0U) {
        return false;
    }

    const uint64_t word_count = value_bytes / 2U;
    if (word_count < 8U) {
        return false;
    }

    uint16_t size_bytes = 0;
    if (!read_tiff_u16(cfg, tiff_bytes, value_off + 0, &size_bytes)) {
        return false;
    }
    if (size_bytes != value_bytes) {
        return false;
    }

    uint16_t num_points = 0;
    if (!read_tiff_u16(cfg, tiff_bytes, value_off + 2U * 2U, &num_points)) {
        return false;
    }
    if (num_points == 0U
        || uint32_t(num_points) > options.limits.max_entries_per_ifd) {
        return false;
    }

    uint16_t image_width  = 0U;
    uint16_t image_height = 0U;
    uint16_t af_width     = 0U;
    uint16_t af_height    = 0U;
    if (!read_tiff_u16(cfg, tiff_bytes, value_off + 8U, &image_width)
        || !read_tiff_u16(cfg, tiff_bytes, value_off + 10U, &image_height)
        || !read_tiff_u16(cfg, tiff_bytes, value_off + 12U, &af_width)
        || !read_tiff_u16(cfg, tiff_bytes, value_off + 14U, &af_height)
        || image_width == 0U || image_height == 0U || af_width == 0U
        || af_height == 0U) {
        return false;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        update_status(status_out, ExifDecodeStatus::LimitExceeded);
        return true;
    }

    // Canon AFInfo2 has four signed geometry arrays followed by masks whose
    // word counts are ceil(NumAFPoints / 16). Tag 0x000d is selected-point
    // data only for EOS models; non-EOS cameras use an unknown word array
    // there and place PrimaryAFPoint after it. AFInfo3 suppresses that final
    // AFInfo2 field. Remaining declared words stay in the raw parent value.
    uint32_t order = 0;
    bool complete                 = true;
    bool stopped                  = false;
    const uint32_t n              = uint32_t(num_points);
    const uint64_t mask_words     = (uint64_t(n) + 15U) / 16U;
    const uint64_t geometry_start = 8U;
    const uint64_t mask_start     = geometry_start + 4U * uint64_t(n);

    for (uint16_t tag = 0U; tag < 8U; ++tag) {
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++, tag,
                                                 tag, store, options,
                                                 status_out)) {
            complete = false;
            stopped  = true;
            break;
        }
    }
    for (uint16_t tag = 0U; tag < 8U && !stopped; ++tag) {
        if (!decode_canon_afinfo2_add_u16_scalar(
                cfg, tiff_bytes, value_off, mk_ifd0, block, order++,
                static_cast<uint16_t>(0x2600U + tag), tag, store, options,
                status_out)) {
            complete = false;
            stopped  = true;
            break;
        }
    }

    struct ArrSpec final {
        uint16_t tag   = 0;
        uint64_t first_word = 0U;
    };
    const ArrSpec arrays[4] = {
        { 0x0008U, geometry_start + 0U * uint64_t(n) },
        { 0x0009U, geometry_start + 1U * uint64_t(n) },
        { 0x000aU, geometry_start + 2U * uint64_t(n) },
        { 0x000bU, geometry_start + 3U * uint64_t(n) },
    };

    for (uint32_t i = 0U; i < 4U && !stopped; ++i) {
        const ArrSpec& a     = arrays[i];
        if (!canon_afinfo2_words_fit(word_count, a.first_word, n)) {
            complete = false;
            continue;
        }
        if (!decode_canon_afinfo2_add_i16_array(cfg, tiff_bytes, value_off,
                                                mk_ifd0, block, order++, a.tag,
                                                a.first_word, n, store, options,
                                                status_out)) {
            complete = false;
            stopped  = true;
            break;
        }
        if (!decode_canon_afinfo2_add_i16_array(
                cfg, tiff_bytes, value_off, mk_ifd0, block, order++,
                static_cast<uint16_t>(0x2600U + a.tag), a.first_word, n, store,
                options, status_out)) {
            complete = false;
            stopped  = true;
        }
    }

    if (!stopped) {
        if (canon_afinfo2_words_fit(word_count, mask_start, mask_words)) {
            if (!decode_canon_afinfo2_add_i16_array(
                    cfg, tiff_bytes, value_off, mk_ifd0, block, order++,
                    0x000cU, mask_start, static_cast<uint32_t>(mask_words),
                    store, options, status_out)
                || !decode_canon_afinfo2_add_i16_array(
                    cfg, tiff_bytes, value_off, mk_ifd0, block, order++,
                    0x260cU, mask_start, static_cast<uint32_t>(mask_words),
                    store, options, status_out)) {
                complete = false;
                stopped  = true;
            }
        } else {
            complete = false;
        }
    }

    if (!stopped && model_class == CanonAfInfo2ModelClass::Eos) {
        const uint64_t selected_start = mask_start + mask_words;
        if (canon_afinfo2_words_fit(word_count, selected_start, mask_words)) {
            if (!decode_canon_afinfo2_add_i16_array(
                    cfg, tiff_bytes, value_off, mk_ifd0, block, order++,
                    0x000dU, selected_start, static_cast<uint32_t>(mask_words),
                    store, options, status_out)
                || !decode_canon_afinfo2_add_i16_array(
                    cfg, tiff_bytes, value_off, mk_ifd0, block, order++,
                    0x260dU, selected_start, static_cast<uint32_t>(mask_words),
                    store, options, status_out)) {
                complete = false;
            }
        } else {
            complete = false;
        }
    } else if (!stopped && model_class == CanonAfInfo2ModelClass::KnownNonEos) {
        const uint64_t unknown_start = mask_start + mask_words;
        const uint64_t unknown_words = mask_words + 1U;
        if (canon_afinfo2_words_fit(word_count, unknown_start, unknown_words)) {
            char unknown_ifd_buffer[96];
            const std::string_view unknown_ifd = make_mk_subtable_ifd_token(
                "mk_canon", "afinfo2unknown", 0U,
                std::span<char>(unknown_ifd_buffer));
            if (!decode_canon_afinfo2_add_i16_array(
                    cfg, tiff_bytes, value_off, unknown_ifd, block, order++,
                    0x000dU, unknown_start,
                    static_cast<uint32_t>(unknown_words), store, options,
                    status_out)) {
                complete = false;
                stopped  = true;
            }
        } else {
            complete = false;
        }

        if (!stopped && afinfo3_context_known && !afinfo3_present) {
            const uint64_t primary_word = unknown_start + unknown_words;
            if (canon_afinfo2_words_fit(word_count, primary_word, 1U)) {
                if (!decode_canon_afinfo2_add_u16_scalar(
                        cfg, tiff_bytes, value_off, mk_ifd0, block, order++,
                        0x000eU, static_cast<uint32_t>(primary_word), store,
                        options, status_out)
                    || !decode_canon_afinfo2_add_u16_scalar(
                        cfg, tiff_bytes, value_off, mk_ifd0, block, order++,
                        0x260eU, static_cast<uint32_t>(primary_word), store,
                        options, status_out)) {
                    complete = false;
                }
            } else {
                complete = false;
            }
        } else if (!afinfo3_present && !afinfo3_context_known) {
            complete = false;
        }
    }

    if (fields_complete_out) {
        *fields_complete_out = complete && !stopped;
    }
    return true;
}


// The fallback examines at most this many two-byte-aligned candidates. The
// limit is independent of caller I/O ceilings; exhaustion remains visible as
// LimitExceeded plus the existing MakerNote residual.
static constexpr uint32_t kCanonAfInfo2MaxScanProbes = 4096U;

static bool
canon_afinfo2_read_source_header(SourceTiffReader* source, uint64_t offset,
                                 std::span<const std::byte>* header) noexcept
{
    if (!source || !source->range || !header) {
        return false;
    }
    if (source->range->source.contiguous_data
        || source->window.storage.size() >= 16U) {
        return source_tiff_view(source, offset, 16U, header);
    }
    return source_tiff_value(source, offset, 16U, header);
}

static void
canon_afinfo2_mark_scan_limited(ExifDecodeResult* status_out) noexcept
{
    update_status(status_out, ExifDecodeStatus::LimitExceeded);
}

static SourceIfdValueExpansionStatus
canon_scan_afinfo2_from_source(const TiffConfig& cfg, uint64_t value_bytes,
                               std::string_view mk_ifd0, MetaStore& store,
                               const ExifDecodeOptions& options,
                               ExifDecodeResult* status_out,
                               CanonAfInfo2ModelClass model_class,
                               bool afinfo3_present, bool afinfo3_context_known,
                               const SourceIfdValueContext& context) noexcept
{
    SourceTiffReader* const source             = context.source;
    const SourceTiffMetadataSpan metadata_span = context.bounded_metadata_span;
    if (!source || !source->range || !source->result || mk_ifd0.empty()
        || !source->result->input.ok()
        || source->result->value_scratch_needed != 0U || value_bytes < 16U
        || value_bytes > options.limits.max_value_bytes
        || !source_tiff_contains(*source, metadata_span.offset,
                                 metadata_span.size)
        || metadata_span.size < value_bytes) {
        return SourceIfdValueExpansionStatus::Incomplete;
    }

    uint64_t bounded_source_offset = 0U;
    if (source->range->source_offset > UINT64_MAX - metadata_span.offset) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return SourceIfdValueExpansionStatus::Incomplete;
    }
    bounded_source_offset = source->range->source_offset + metadata_span.offset;
    const RandomAccessSourceRange bounded_range { source->range->source,
                                                  bounded_source_offset,
                                                  metadata_span.size };

    // The current value has been consumed. The scan reader borrows the same
    // caller window/scratch synchronously, with its range restricted to the
    // declared MakerNote value. Invalidate the parent cache before reusing its
    // storage so later structural reads cannot observe overwritten bytes.
    SourceTiffReader scan_source = *source;
    scan_source.range            = &bounded_range;
    scan_source.window.storage   = source->window.storage;
    scan_source.window.reset();
    source->window.reset();

    const uint64_t last_candidate  = metadata_span.size - value_bytes;
    const uint64_t candidate_count = last_candidate / 2U + 1U;
    const uint64_t probe_count = candidate_count < kCanonAfInfo2MaxScanProbes
                                     ? candidate_count
                                     : kCanonAfInfo2MaxScanProbes;

    for (uint64_t probe = 0U; probe < probe_count; ++probe) {
        const uint64_t candidate_offset = probe * 2U;
        if (candidate_offset > last_candidate) {
            break;
        }
        uint64_t absolute_candidate_offset = 0U;
        if (metadata_span.offset > UINT64_MAX - candidate_offset) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return SourceIfdValueExpansionStatus::Incomplete;
        }
        absolute_candidate_offset = metadata_span.offset + candidate_offset;
        if (absolute_candidate_offset == context.value_offset) {
            continue;
        }
        if (canon_value_overlaps_ifd(absolute_candidate_offset, value_bytes,
                                     context.excluded_value_span.offset,
                                     context.excluded_value_span.size)) {
            continue;
        }

        std::span<const std::byte> header;
        if (!canon_afinfo2_read_source_header(&scan_source, candidate_offset,
                                              &header)) {
            return SourceIfdValueExpansionStatus::Incomplete;
        }
        if (!canon_afinfo2_header_looks_valid(cfg, header, value_bytes,
                                              options.limits, model_class,
                                              afinfo3_present,
                                              afinfo3_context_known)) {
            continue;
        }

        std::span<const std::byte> payload;
        if (!source_tiff_value(&scan_source, candidate_offset, value_bytes,
                               &payload)) {
            return SourceIfdValueExpansionStatus::Incomplete;
        }
        if (!canon_afinfo2_header_looks_valid(cfg, payload, value_bytes,
                                              options.limits, model_class,
                                              afinfo3_present,
                                              afinfo3_context_known)) {
            continue;
        }
        bool fields_complete = false;
        if (decode_canon_afinfo2(cfg, payload, 0U, value_bytes, mk_ifd0,
                                 model_class, afinfo3_present,
                                 afinfo3_context_known, store, options,
                                 status_out, &fields_complete)) {
            return fields_complete ? SourceIfdValueExpansionStatus::Expanded
                                   : SourceIfdValueExpansionStatus::Incomplete;
        }
    }

    if (candidate_count > probe_count) {
        canon_afinfo2_mark_scan_limited(status_out);
    }
    return SourceIfdValueExpansionStatus::Incomplete;
}

static bool
decode_canon_custom_functions2(const TiffConfig& cfg,
                               std::span<const std::byte> tiff_bytes,
                               uint64_t value_off, uint64_t value_bytes,
                               std::string_view mk_ifd0, MetaStore& store,
                               const ExifDecodeOptions& options,
                               ExifDecodeResult* status_out) noexcept
{
    (void)cfg;
    if (mk_ifd0.empty()) {
        return false;
    }
    if (value_bytes < 8) {
        return false;
    }
    if (value_off + value_bytes > tiff_bytes.size()) {
        return false;
    }

    // CanonCustom2 blobs use little-endian payloads even in big-endian EXIF
    // containers, so decode the internal record stream with a fixed LE view.
    const TiffConfig payload_cfg { true, false };

    uint16_t len16 = 0;
    if (!read_tiff_u16(payload_cfg, tiff_bytes, value_off + 0, &len16)) {
        return false;
    }
    if (len16 != value_bytes) {
        return false;
    }

    uint32_t group_count = 0;
    if (!read_tiff_u32(payload_cfg, tiff_bytes, value_off + 4, &group_count)) {
        return false;
    }
    (void)group_count;

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return true;
    }

    char canon_model_buf[128];
    std::memset(canon_model_buf, 0, sizeof(canon_model_buf));
    const std::string_view canon_model
        = canon_find_model_text(store, std::span<char>(canon_model_buf));
    const uint64_t end = value_off + value_bytes;
    uint64_t pos       = value_off + 8;
    uint32_t order     = 0;

    while (pos + 12 <= end) {
        uint32_t rec_num   = 0;
        uint32_t rec_len   = 0;
        uint32_t rec_count = 0;
        if (!read_tiff_u32(payload_cfg, tiff_bytes, pos + 0, &rec_num)
            || !read_tiff_u32(payload_cfg, tiff_bytes, pos + 4, &rec_len)
            || !read_tiff_u32(payload_cfg, tiff_bytes, pos + 8, &rec_count)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }
        (void)rec_num;

        if (rec_len < 8) {
            break;
        }

        pos += 12;
        const uint64_t rec_end = pos + uint64_t(rec_len) - 8ULL;
        if (rec_end > end) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }

        uint64_t rec_pos = pos;
        uint32_t i       = 0;
        for (; rec_pos + 8 <= rec_end && i < rec_count; ++i) {
            uint32_t tag32 = 0;
            uint32_t num   = 0;
            if (!read_tiff_u32(payload_cfg, tiff_bytes, rec_pos + 0, &tag32)
                || !read_tiff_u32(payload_cfg, tiff_bytes, rec_pos + 4, &num)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return true;
            }
            if (num == 0) {
                // Skip empty records (seen in the wild).
                rec_pos += 8;
                continue;
            }

            // ExifTool workaround: some EOS-1D X Mark III files contain an
            // incorrect element count for tag 0x070c (CustomControls), which
            // would misalign parsing of subsequent records.
            if (tag32 == 0x070c && num == 0x66 && rec_pos + 8 < rec_end) {
                const uint64_t next_rec = rec_pos + 8 + uint64_t(num) * 4ULL;
                if (next_rec + 8 < rec_end) {
                    uint32_t tmp = 0;
                    if (read_tiff_u32(payload_cfg, tiff_bytes, next_rec + 4,
                                      &tmp)
                        && tmp == 0x070f) {
                        num += 1;
                    }
                }
            }
            if (num > options.limits.max_entries_per_ifd) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                break;
            }

            const uint64_t payload_bytes = uint64_t(num) * 4ULL;
            if (payload_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                break;
            }

            const uint64_t payload_off = rec_pos + 8;
            const uint64_t next        = payload_off + payload_bytes;
            if (next > rec_end) {
                break;
            }

            if (tag32 > 0xFFFFu) {
                // OpenMeta uses 16-bit EXIF tag ids. Skip unknown/extended ids.
                rec_pos = next;
                continue;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return true;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), mk_ifd0,
                                                   static_cast<uint16_t>(tag32));
            entry.origin.block = block;
            entry.origin.order_in_block = order++;
            entry.origin.wire_type      = WireType { WireFamily::Other, 4 };
            entry.origin.wire_count     = num;
            entry.flags |= EntryFlags::Derived;
            if (tag32 == 0x0103u) {
                entry.flags |= EntryFlags::ContextualName;
                entry.origin.name_context_kind
                    = EntryNameContextKind::CanonCustomFunctions20103;
                entry.origin.name_context_variant = (num > 1U) ? 2U : 1U;
            } else if (tag32 == 0x010Cu && num > 1U && num != 3U
                       && canon_custom_functions2_010c_prefers_placeholder(
                           canon_model)) {
                entry.flags |= EntryFlags::ContextualName;
                entry.origin.name_context_kind
                    = EntryNameContextKind::CanonCustomFunctions2010C;
                entry.origin.name_context_variant = 1U;
            } else if (tag32 == 0x0510u
                       && canon_custom_functions2_0510_prefers_superimposed_display(
                           canon_model)) {
                entry.flags |= EntryFlags::ContextualName;
                entry.origin.name_context_kind
                    = EntryNameContextKind::CanonCustomFunctions20510;
                entry.origin.name_context_variant = 1U;
            } else if (tag32 == 0x0701u) {
                if (canon_custom_functions2_0701_prefers_af_and_metering(
                        canon_model)) {
                    entry.flags |= EntryFlags::ContextualName;
                    entry.origin.name_context_kind
                        = EntryNameContextKind::CanonCustomFunctions20701;
                    entry.origin.name_context_variant = 2U;
                } else if (canon_custom_functions2_0701_prefers_shutter_button(
                               canon_model)) {
                    entry.flags |= EntryFlags::ContextualName;
                    entry.origin.name_context_kind
                        = EntryNameContextKind::CanonCustomFunctions20701;
                    entry.origin.name_context_variant = 1U;
                }
            }

            if (num == 1) {
                uint32_t v = 0;
                if (!read_tiff_u32(payload_cfg, tiff_bytes, payload_off, &v)) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    return true;
                }
                entry.value = make_u32(v);
            } else {
                ByteArena& arena       = store.arena();
                const uint64_t bytes64 = payload_bytes;
                if (bytes64 > UINT32_MAX) {
                    if (status_out) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                    }
                    return true;
                }
                const ByteSpan span
                    = arena.allocate(static_cast<uint32_t>(bytes64),
                                     static_cast<uint32_t>(alignof(uint32_t)));
                std::span<std::byte> out = arena.span_mut(span);
                if (out.size() != bytes64) {
                    if (status_out) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                    }
                    return true;
                }

                for (uint32_t k = 0; k < num; ++k) {
                    uint32_t v = 0;
                    if (!read_tiff_u32(payload_cfg, tiff_bytes,
                                       payload_off + uint64_t(k) * 4ULL, &v)) {
                        if (status_out) {
                            update_status(status_out,
                                          ExifDecodeStatus::Malformed);
                        }
                        return true;
                    }
                    std::memcpy(out.data() + size_t(k) * 4U, &v, 4U);
                }

                MetaValue v;
                v.kind      = MetaValueKind::Array;
                v.elem_type = MetaElementType::U32;
                v.count     = num;
                v.data.span = span;
                entry.value = v;
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }

            rec_pos = next;
        }

        pos = rec_end;
    }

    return true;
}

static bool
decode_canon_u32_bin_dir(const TiffConfig& cfg,
                         std::span<const std::byte> tiff_bytes,
                         uint64_t value_off, uint64_t value_bytes,
                         std::string_view ifd_name, MetaStore& store,
                         const ExifDecodeOptions& options,
                         ExifDecodeResult* status_out) noexcept
{
    (void)cfg;
    if (ifd_name.empty()) {
        return false;
    }
    if (value_bytes < 8) {
        return false;
    }
    if (value_off + value_bytes > tiff_bytes.size()) {
        return false;
    }

    // Canon BinaryData directories use little-endian payloads even in
    // big-endian EXIF containers.
    const TiffConfig payload_cfg { true, false };

    uint32_t len32 = 0;
    if (!read_tiff_u32(payload_cfg, tiff_bytes, value_off + 0, &len32)) {
        return false;
    }
    if (len32 != value_bytes) {
        return false;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return true;
    }

    const uint64_t end = value_off + value_bytes;
    uint64_t pos       = value_off + 8;
    uint32_t order     = 0;

    while (pos + 12 <= end) {
        uint32_t rec_num   = 0;
        uint32_t rec_len   = 0;
        uint32_t rec_count = 0;
        if (!read_tiff_u32(payload_cfg, tiff_bytes, pos + 0, &rec_num)
            || !read_tiff_u32(payload_cfg, tiff_bytes, pos + 4, &rec_len)
            || !read_tiff_u32(payload_cfg, tiff_bytes, pos + 8, &rec_count)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }
        (void)rec_num;

        if (rec_len < 8) {
            break;
        }

        pos += 12;
        const uint64_t rec_end = pos + uint64_t(rec_len) - 8ULL;
        if (rec_end > end) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }

        uint64_t rec_pos = pos;
        uint32_t i       = 0;
        for (; rec_pos + 8 <= rec_end && i < rec_count; ++i) {
            uint32_t tag32 = 0;
            uint32_t num   = 0;
            if (!read_tiff_u32(payload_cfg, tiff_bytes, rec_pos + 0, &tag32)
                || !read_tiff_u32(payload_cfg, tiff_bytes, rec_pos + 4, &num)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return true;
            }
            if (tag32 > 0xFFFFu) {
                break;
            }
            if (num == 0) {
                break;
            }
            if (num > options.limits.max_entries_per_ifd) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                break;
            }

            const uint64_t payload_bytes = uint64_t(num) * 4ULL;
            if (payload_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                break;
            }

            const uint64_t payload_off = rec_pos + 8;
            const uint64_t next        = payload_off + payload_bytes;
            if (next > rec_end) {
                break;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return true;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), ifd_name,
                                                   static_cast<uint16_t>(tag32));
            entry.origin.block = block;
            entry.origin.order_in_block = order++;
            entry.origin.wire_type      = WireType { WireFamily::Other, 4 };
            entry.origin.wire_count     = num;
            entry.flags |= EntryFlags::Derived;

            if (num == 1) {
                uint32_t v = 0;
                if (!read_tiff_u32(payload_cfg, tiff_bytes, payload_off, &v)) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    return true;
                }
                entry.value = make_u32(v);
            } else {
                ByteArena& arena       = store.arena();
                const uint64_t bytes64 = payload_bytes;
                if (bytes64 > UINT32_MAX) {
                    if (status_out) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                    }
                    return true;
                }
                const ByteSpan span
                    = arena.allocate(static_cast<uint32_t>(bytes64),
                                     static_cast<uint32_t>(alignof(uint32_t)));
                std::span<std::byte> out = arena.span_mut(span);
                if (out.size() != bytes64) {
                    if (status_out) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                    }
                    return true;
                }

                for (uint32_t k = 0; k < num; ++k) {
                    uint32_t v = 0;
                    if (!read_tiff_u32(payload_cfg, tiff_bytes,
                                       payload_off + uint64_t(k) * 4ULL, &v)) {
                        if (status_out) {
                            update_status(status_out,
                                          ExifDecodeStatus::Malformed);
                        }
                        return true;
                    }
                    std::memcpy(out.data() + size_t(k) * 4U, &v, 4U);
                }

                MetaValue v;
                v.kind      = MetaValueKind::Array;
                v.elem_type = MetaElementType::U32;
                v.count     = num;
                v.data.span = span;
                entry.value = v;
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }

            rec_pos = next;
        }

        pos = rec_end;
    }

    return true;
}

static bool canon_value_has_width(std::span<const std::byte> value,
                                  uint32_t count, uint32_t width) noexcept
{
    return width != 0U
           && uint64_t(count) * uint64_t(width)
                  <= static_cast<uint64_t>(value.size());
}

static void
decode_canon_colordata_embedded_u16_table(
    const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
    uint64_t colordata_off, uint32_t colordata_count, uint32_t word_off,
    uint32_t word_count, std::string_view mk_prefix, std::string_view subtable,
    std::string_view canon_model, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    if (word_count == 0 || word_off > colordata_count
        || word_count > (colordata_count - word_off)) {
        return;
    }

    const uint64_t sub_off = colordata_off + 2ULL * uint64_t(word_off);
    if (sub_off > tiff_bytes.size()) {
        return;
    }
    if ((uint64_t(tiff_bytes.size()) - sub_off)
        < (2ULL * uint64_t(word_count))) {
        return;
    }

    char sub_ifd_buf[64];
    const std::string_view sub_ifd
        = make_mk_subtable_ifd_token(mk_prefix, subtable, 0,
                                     std::span<char>(sub_ifd_buf));
    decode_canon_u16_table(cfg, tiff_bytes, sub_off, word_count, sub_ifd, store,
                           canon_model, options, status_out);
}

static void
decode_canon_colordata_embedded_tables(
    const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
    uint64_t colordata_off, uint32_t colordata_count, uint32_t family,
    int16_t version, std::string_view mk_prefix, std::string_view canon_model,
    MetaStore& store, const ExifDecodeOptions& options,
    ExifDecodeResult* status_out) noexcept
{
    switch (family) {
    case 1:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0x4b, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    case 2:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0xa4, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    case 3:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0x85, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    case 4:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0x3f, 105,
            mk_prefix, "colorcoefs", canon_model, store, options, status_out);
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0xa8, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    case 5:
        if (version == -3) {
            decode_canon_colordata_embedded_u16_table(
                cfg, tiff_bytes, colordata_off, colordata_count, 0x47, 115,
                mk_prefix, "colorcoefs", canon_model, store, options,
                status_out);
            decode_canon_colordata_embedded_u16_table(cfg, tiff_bytes,
                                                      colordata_off,
                                                      colordata_count, 0xba, 75,
                                                      mk_prefix, "colorcalib2",
                                                      canon_model, store,
                                                      options, status_out);
        } else if (version == -4) {
            decode_canon_colordata_embedded_u16_table(
                cfg, tiff_bytes, colordata_off, colordata_count, 0x47, 184,
                mk_prefix, "colorcoefs2", canon_model, store, options,
                status_out);
            decode_canon_colordata_embedded_u16_table(cfg, tiff_bytes,
                                                      colordata_off,
                                                      colordata_count, 0xff, 75,
                                                      mk_prefix, "colorcalib2",
                                                      canon_model, store,
                                                      options, status_out);
        }
        return;
    case 6:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0xbc, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    case 7:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0xd5, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    case 8:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0x107, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    case 9:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0x10a, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    case 10:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0x118, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    case 11:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0x12c, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    case 12:
        decode_canon_colordata_embedded_u16_table(
            cfg, tiff_bytes, colordata_off, colordata_count, 0x140, 60,
            mk_prefix, "colorcalib", canon_model, store, options, status_out);
        return;
    default: return;
    }
}

static void
decode_canon_colordata_blob(const TiffConfig& cfg,
                            std::span<const std::byte> tiff_bytes,
                            uint64_t colordata_off, uint32_t colordata_count,
                            uint32_t family_count_hint,
                            std::string_view mk_prefix,
                            std::string_view canon_model, MetaStore& store,
                            const ExifDecodeOptions& options,
                            ExifDecodeResult* status_out) noexcept
{
    bool looks_like_colorcalib = false;
    uint16_t colordata_version = 0;
    (void)read_tiff_u16(cfg, tiff_bytes, colordata_off, &colordata_version);
    const int16_t colordata_version_i16 = static_cast<int16_t>(
        colordata_version);
    if (colordata_count > 0x0107u + 3u) {
        uint16_t maybe_temp = 0;
        if (read_tiff_u16(cfg, tiff_bytes,
                          colordata_off + 2ULL * uint64_t(0x0107u + 3u),
                          &maybe_temp)) {
            looks_like_colorcalib = (maybe_temp >= 1500u
                                     && maybe_temp <= 20000u);
        }
    }

    uint32_t family = canon_colordata_family(colordata_count,
                                             colordata_version_i16);
    if (family == 0 && family_count_hint != colordata_count) {
        family = canon_colordata_family(family_count_hint,
                                        colordata_version_i16);
    }

    std::string_view table;
    if (!canon_colordata_family_name(family, &table)) {
        table = looks_like_colorcalib ? "colordata8" : "colordata";
    }

    char sub_ifd_buf[64];
    const std::string_view sub_ifd
        = make_mk_subtable_ifd_token(mk_prefix, table, 0,
                                     std::span<char>(sub_ifd_buf));
    decode_canon_u16_table(cfg, tiff_bytes, colordata_off, colordata_count,
                           sub_ifd, store, canon_model, options, status_out);

    decode_canon_colordata_embedded_tables(cfg, tiff_bytes, colordata_off,
                                           colordata_count, family,
                                           colordata_version_i16, mk_prefix,
                                           canon_model, store, options,
                                           status_out);
}

struct CanonMakerNoteValueContext final {
    std::span<const std::byte> tiff_bytes;
    uint64_t abs_value_off = 0U;
    uint64_t maker_note_off = 0U;
    uint64_t maker_note_ifd_bytes  = 0U;
    uint64_t maker_note_span_bytes = 0U;
    uint32_t value_or_off32 = 0U;
    int64_t auto_value_base = INT64_MIN;
    int64_t schema_value_base = INT64_MIN;
};

static SourceIfdValueExpansionStatus
expand_canon_makernote_value(
    const TiffConfig& cfg, const ClassicIfdEntry& entry, bool have_value,
    std::span<const std::byte> value, std::string_view ifd_name,
    MetaStore& store, const ExifDecodeOptions& options,
    ExifDecodeResult* status_out, const CanonMakerNoteValueContext* context,
    bool* afinfo2_recognized_out, CanonAfInfo2ModelClass model_class,
    bool afinfo3_present, bool afinfo3_context_known) noexcept
{
    if (afinfo2_recognized_out) {
        *afinfo2_recognized_out = false;
    }
    if (!ifd_name.starts_with("mk_canon") || entry.count32 == 0U) {
        return SourceIfdValueExpansionStatus::NotApplicable;
    }

    char model_buffer[128] {};
    const std::string_view model
        = canon_find_model_text(store, std::span<char>(model_buffer));
    const bool known_tag
        = (entry.type == 1U && entry.tag == 0x0029U
           && model == "Canon PowerShot G9")
          || (entry.type == 3U
              && (entry.tag == 0x0001U || entry.tag == 0x0002U
                  || entry.tag == 0x0004U || entry.tag == 0x0005U
                  || entry.tag == 0x000fU || entry.tag == 0x0012U
                  || entry.tag == 0x001dU || entry.tag == 0x0026U
                  || entry.tag == 0x0090U || entry.tag == 0x0091U
                  || entry.tag == 0x0092U || entry.tag == 0x0093U
                  || entry.tag == 0x0098U || entry.tag == 0x00a0U
                  || entry.tag == 0x00aaU || entry.tag == 0x00e0U
                  || entry.tag == 0x4001U))
          || (entry.type == 4U
              && (entry.tag == 0x000dU || entry.tag == 0x0035U
                  || entry.tag == 0x009aU || entry.tag == 0x4013U
                  || entry.tag == 0x4016U || entry.tag == 0x4018U
                  || entry.tag == 0x4020U || entry.tag == 0x4021U
                  || entry.tag == 0x4024U || entry.tag == 0x4025U
                  || entry.tag == 0x4028U || entry.tag == 0x403fU))
          || (entry.type == 7U
              && (entry.tag == 0x000dU || entry.tag == 0x0099U
                  || entry.tag == 0x4011U || entry.tag == 0x4019U
                  || entry.tag == 0x4001U))
          || entry.tag == 0x0099U
          || (entry.tag == 0x0029U
              && (entry.type == 1U || entry.type == 4U || entry.type == 7U)
              && model == "Canon PowerShot G9");
    if (!known_tag) {
        return SourceIfdValueExpansionStatus::NotApplicable;
    }
    if (!have_value || value.empty()
        || value.size() > options.limits.max_value_bytes) {
        return SourceIfdValueExpansionStatus::Incomplete;
    }

    std::span<const std::byte> tiff_bytes = value;
    uint64_t abs_value_off = 0U;
    if (context) {
        tiff_bytes = context->tiff_bytes;
        abs_value_off = context->abs_value_off;
        if (abs_value_off > tiff_bytes.size()
            || value.size() > tiff_bytes.size() - abs_value_off) {
            return SourceIfdValueExpansionStatus::Incomplete;
        }
    }

    const std::string_view prefix = "mk_canon";
    char sub_ifd_buffer[96];

    if (entry.tag == 0x0029U
        && (entry.type == 1U || entry.type == 4U || entry.type == 7U)
        && model == "Canon PowerShot G9") {
        decode_canon_wbinfo(cfg, value, store, options, status_out);
        return SourceIfdValueExpansionStatus::Expanded;
    }

    if (entry.type == 7U && entry.tag == 0x000dU) {
        const std::span<const std::byte> cam = value;
        ClassicIfdCandidate best;
        const bool has_best
            = find_best_classic_ifd_candidate(cam, 512U, options.limits,
                                              &best)
              && best.valid_entries == best.entry_count;
        uint32_t cam_tag_extent = cam.empty()
                                      ? 0U
                                      : static_cast<uint32_t>(cam.size() - 1U);
        if (has_best) {
            TiffConfig cam_cfg;
            cam_cfg.le = best.le;
            cam_cfg.bigtiff = false;
            const uint32_t ifd_tag_extent
                = canon_camerainfo_candidate_tag_extent(cam_cfg, cam, best);
            if (ifd_tag_extent > cam_tag_extent) {
                cam_tag_extent = ifd_tag_extent;
            }
        }
        std::string_view cam_subtable
            = canon_camerainfo_subtable_suffix(model, cam_tag_extent);
        if (cam_subtable == "camerainfo" && has_best) {
            TiffConfig cam_cfg;
            cam_cfg.le = best.le;
            cam_cfg.bigtiff = false;
            const std::string_view fallback_subtable
                = canon_camerainfo_subtable_suffix_from_candidate(cam_cfg, cam,
                                                                  best);
            if (!fallback_subtable.empty()) {
                cam_subtable = fallback_subtable;
            }
        }
        const std::string_view sub_ifd
            = make_mk_subtable_ifd_token(prefix, cam_subtable, 0U,
                                         std::span<char>(sub_ifd_buffer));
        if (has_best) {
            TiffConfig cam_cfg;
            cam_cfg.le = best.le;
            cam_cfg.bigtiff = false;
            decode_classic_ifd_no_header(cam_cfg, cam, best.offset, sub_ifd,
                                         store, options, status_out,
                                         EntryFlags::Derived);
        }
        decode_canon_camerainfo_fixed_fields(cfg, cam, sub_ifd, model, store,
                                             options, status_out);

        if (cam.size() > 0x025bU) {
            const uint64_t ps_bytes = cam.size() - 0x025bU;
            if (ps_bytes >= 0x00deU) {
                const bool use_psinfo2
                    = canon_camerainfo_prefers_psinfo2(model)
                          ? true
                          : (canon_camerainfo_prefers_psinfo(model)
                                 ? false
                                 : canon_psinfo2_tail_looks_valid(
                                       cfg, cam, 0x025bU, ps_bytes));
                const std::string_view ps_table
                    = use_psinfo2 ? "psinfo2" : "psinfo";
                const std::string_view ps_ifd = make_mk_subtable_ifd_token(
                    prefix, ps_table, 0U, std::span<char>(sub_ifd_buffer));
                decode_canon_psinfo_table(cam, 0x025bU, ps_bytes, ps_ifd,
                                          store, options, status_out);
            }
        }
        return SourceIfdValueExpansionStatus::Expanded;
    }

    if (entry.tag == 0x4019U && entry.type == 7U) {
        const size_t serial_bytes = value.size() < 5U ? value.size() : 5U;
        if (serial_bytes == 0U) {
            return SourceIfdValueExpansionStatus::Incomplete;
        }
        const std::string_view sub_ifd = make_mk_subtable_ifd_token(
            prefix, "lensinfo", 0U, std::span<char>(sub_ifd_buffer));
        const uint16_t tags_out[] = { 0x0000U };
        const MetaValue vals_out[] = { make_bytes(
            store.arena(), value.first(serial_bytes)) };
        emit_bin_dir_entries(sub_ifd, store,
                             std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out),
                             options.limits, status_out);
        return SourceIfdValueExpansionStatus::Expanded;
    }

    if (entry.tag == 0x0099U) {
        uint64_t custom_off = abs_value_off;
        if (context) {
            custom_off = canon_choose_custom_functions2_offset(
                tiff_bytes, context->value_or_off32, abs_value_off,
                value.size(), context->maker_note_off,
                context->maker_note_ifd_bytes, context->auto_value_base,
                context->schema_value_base);
        }
        char canoncustom_ifd_buffer[96];
        const std::string_view canoncustom_ifd = make_mk_subtable_ifd_token(
            "mk_canoncustom", "functions2", 0U,
            std::span<char>(canoncustom_ifd_buffer));
        if (!decode_canon_custom_functions2(cfg, tiff_bytes, custom_off,
                                            value.size(), canoncustom_ifd,
                                            store, options, status_out)) {
            return SourceIfdValueExpansionStatus::Incomplete;
        }
        return SourceIfdValueExpansionStatus::Expanded;
    }

    if (entry.type == 7U && entry.tag == 0x4011U) {
        if (value.size() < 2U || (value.size() % 2U) != 0U) {
            return SourceIfdValueExpansionStatus::Incomplete;
        }
        const uint32_t count16 = static_cast<uint32_t>(value.size() / 2U);
        const std::string_view sub_ifd = make_mk_subtable_ifd_token(
            prefix, "vignettingcorr", 0U, std::span<char>(sub_ifd_buffer));
        decode_canon_u16_table(cfg, tiff_bytes, abs_value_off, count16, sub_ifd,
                               store, model, options, status_out);
        return SourceIfdValueExpansionStatus::Expanded;
    }
    if (entry.tag == 0x4001U
        && (entry.type == 3U || entry.type == 7U)) {
        if (value.size() < 2U || (value.size() % 2U) != 0U) {
            return SourceIfdValueExpansionStatus::Incomplete;
        }
        const uint32_t count16 = static_cast<uint32_t>(value.size() / 2U);
        decode_canon_colordata_blob(cfg, tiff_bytes, abs_value_off, count16,
                                    entry.count32, prefix, model, store,
                                    options, status_out);
        return SourceIfdValueExpansionStatus::Expanded;
    }

    std::string_view table;
    bool u16_table = false;
    bool u32_table = false;
    bool i32_table = false;
    if (entry.type == 3U) {
        switch (entry.tag) {
        case 0x0001U: table = "camerasettings"; break;
        case 0x0004U: table = "shotinfo"; break;
        case 0x0005U: table = "panorama"; break;
        case 0x0012U: table = "afinfo"; break;
        case 0x001dU: table = "mycolors"; break;
        case 0x0093U: table = "fileinfo"; break;
        case 0x0098U: table = "cropinfo"; break;
        case 0x00a0U: table = "processing"; break;
        case 0x00e0U: table = "sensorinfo"; break;
        case 0x0002U:
            table = "focallength";
            if (entry.count32 > 3U) {
                uint16_t x = 0U;
                uint16_t y = 0U;
                if (read_tiff_u16(cfg, value, 4U, &x)
                    && read_tiff_u16(cfg, value, 6U, &y)
                    && !(x > 0U && y > 0U && x <= 5000U && y <= 5000U)) {
                    table = "focallength_unknown";
                }
            }
            break;
        default: break;
        }
        if (!table.empty()) {
            if (!canon_value_has_width(value, entry.count32, 2U)
                || entry.count32 > options.limits.max_entries_per_ifd) {
                return SourceIfdValueExpansionStatus::Incomplete;
            }
            u16_table = true;
        }
    } else if (entry.type == 4U) {
        switch (entry.tag) {
        case 0x0035U: table = "timeinfo"; break;
        case 0x009aU: table = "aspectinfo"; break;
        case 0x000dU:
            table = canon_camerainfo_subtable_suffix(model,
                                                      entry.count32 - 1U);
            break;
        case 0x4016U: table = "vignettingcorr2"; break;
        case 0x4018U: table = "lightingopt"; break;
        case 0x4020U: table = "ambience"; break;
        case 0x4021U: table = "multiexp"; i32_table = true; break;
        case 0x4024U: table = "filterinfo"; break;
        case 0x4025U: table = "hdrinfo"; break;
        case 0x4028U: table = "afconfig"; i32_table = true; break;
        case 0x403fU: table = "rawburstinfo"; break;
        default: break;
        }
        if (!table.empty()) {
            if (!canon_value_has_width(value, entry.count32, 4U)
                || entry.count32 > options.limits.max_entries_per_ifd) {
                return SourceIfdValueExpansionStatus::Incomplete;
            }
            u32_table = !i32_table;
        }
    }
    if (!table.empty()) {
        const std::string_view sub_ifd = make_mk_subtable_ifd_token(
            prefix, table, 0U, std::span<char>(sub_ifd_buffer));
        bool binary_directory_decoded = true;
        if (u16_table) {
            decode_canon_u16_table(cfg, tiff_bytes, abs_value_off,
                                   entry.count32, sub_ifd, store, model,
                                   options, status_out);
        } else if (i32_table) {
            decode_canon_i32_table(cfg, tiff_bytes, abs_value_off,
                                   entry.count32, sub_ifd, store, options,
                                   status_out);
        } else if (u32_table) {
            if (entry.tag == 0x4024U) {
                uint64_t filterinfo_off = abs_value_off;
                if (context) {
                    filterinfo_off = canon_choose_u32_binary_dir_offset(
                        tiff_bytes, context->value_or_off32, abs_value_off,
                        value.size(), context->maker_note_off,
                        context->maker_note_ifd_bytes, context->auto_value_base,
                        context->schema_value_base);
                }
                binary_directory_decoded = decode_canon_u32_bin_dir(
                    cfg, tiff_bytes, filterinfo_off, value.size(), sub_ifd,
                    store, options, status_out);
            }
            decode_canon_u32_table(cfg, tiff_bytes, abs_value_off,
                                   entry.count32, sub_ifd, store, options,
                                   status_out);
        }
        return binary_directory_decoded
                   ? SourceIfdValueExpansionStatus::Expanded
                   : SourceIfdValueExpansionStatus::Incomplete;
    }

    if (entry.type == 3U && entry.tag == 0x0090U) {
        char canoncustom_ifd_buffer[96];
        const std::string_view canoncustom_ifd = make_mk_subtable_ifd_token(
            "mk_canoncustom", "functions1d", 0U,
            std::span<char>(canoncustom_ifd_buffer));
        if (!canon_value_has_width(value, entry.count32, 2U)) {
            return SourceIfdValueExpansionStatus::Incomplete;
        }
        decode_canon_custom_word_table(
            cfg, tiff_bytes, abs_value_off, entry.count32, canoncustom_ifd,
            0x0000U, CanonCustomTagMode::HighByte,
            CanonCustomMode::LowByteAsU8, model, store, options, status_out);
        return SourceIfdValueExpansionStatus::Expanded;
    }
    if (entry.type == 3U && entry.tag == 0x000fU) {
        uint32_t effective_count = entry.count32;
        uint16_t first_word = 0U;
        if (read_tiff_u16(cfg, value, 0U, &first_word)) {
            const uint16_t expected
                = static_cast<uint16_t>(entry.count32 * 2U);
            if (first_word == expected || first_word + 2U == expected) {
                --effective_count;
            }
        }
        const std::string_view subtable
            = canoncustom_subtable_for_tag_0x000f(model, effective_count);
        char canoncustom_ifd_buffer[96];
        const std::string_view canoncustom_ifd = make_mk_subtable_ifd_token(
            "mk_canoncustom", subtable, 0U,
            std::span<char>(canoncustom_ifd_buffer));
        if (!canon_value_has_width(value, entry.count32, 2U)) {
            return SourceIfdValueExpansionStatus::Incomplete;
        }
        decode_canon_custom_word_table(
            cfg, tiff_bytes, abs_value_off, entry.count32, canoncustom_ifd,
            0x0000U, CanonCustomTagMode::HighByte,
            CanonCustomMode::LowByteAsU8, model, store, options, status_out);
        return SourceIfdValueExpansionStatus::Expanded;
    }
    if (entry.type == 3U
        && (entry.tag == 0x0091U || entry.tag == 0x0092U)) {
        const std::string_view subtable
            = entry.tag == 0x0091U ? "personalfuncs" : "personalfuncvalues";
        char canoncustom_ifd_buffer[96];
        const std::string_view canoncustom_ifd = make_mk_subtable_ifd_token(
            "mk_canoncustom", subtable, 0U,
            std::span<char>(canoncustom_ifd_buffer));
        if (!canon_value_has_width(value, entry.count32, 2U)) {
            return SourceIfdValueExpansionStatus::Incomplete;
        }
        decode_canon_custom_word_table(
            cfg, tiff_bytes, abs_value_off, entry.count32, canoncustom_ifd,
            0x0001U, CanonCustomTagMode::Index, CanonCustomMode::U16, model,
            store, options, status_out);
        return SourceIfdValueExpansionStatus::Expanded;
    }
    if (entry.type == 3U && entry.tag == 0x0026U) {
        const std::string_view sub_ifd = make_mk_subtable_ifd_token(
            prefix, "afinfo2", 0U, std::span<char>(sub_ifd_buffer));
        bool fields_complete = false;
        bool decoded
            = decode_canon_afinfo2(cfg, tiff_bytes, abs_value_off, value.size(),
                                   sub_ifd, model_class, afinfo3_present,
                                   afinfo3_context_known, store, options,
                                   status_out, &fields_complete);
        if (!decoded && context) {
            uint64_t alternate_off = 0U;
            if (canon_find_afinfo2_payload(
                    cfg, tiff_bytes, context->maker_note_off,
                    context->maker_note_ifd_bytes,
                    context->maker_note_span_bytes, value.size(), abs_value_off,
                    options.limits, model_class, afinfo3_present,
                    afinfo3_context_known, &alternate_off)) {
                fields_complete = false;
                decoded = decode_canon_afinfo2(cfg, tiff_bytes, alternate_off,
                                               value.size(), sub_ifd,
                                               model_class, afinfo3_present,
                                               afinfo3_context_known, store,
                                               options, status_out,
                                               &fields_complete);
            }
        }
        if (afinfo2_recognized_out) {
            *afinfo2_recognized_out = decoded;
        }
        return decoded && fields_complete
                   ? SourceIfdValueExpansionStatus::Expanded
                   : SourceIfdValueExpansionStatus::Incomplete;
    }
    if (entry.type == 3U && entry.tag == 0x00aaU) {
        const std::string_view sub_ifd = make_mk_subtable_ifd_token(
            prefix, "measuredcolor", 0U, std::span<char>(sub_ifd_buffer));
        if (entry.count32 >= 5U) {
            std::array<uint16_t, 4> values {};
            for (uint32_t i = 0U; i < values.size(); ++i) {
                if (!read_tiff_u16(cfg, value, 2U * (i + 1U), &values[i])) {
                    return SourceIfdValueExpansionStatus::Incomplete;
                }
            }
            const BlockId block = store.add_block(BlockInfo {});
            if (block != kInvalidBlockId) {
                Entry derived;
                derived.key = make_exif_tag_key(store.arena(), sub_ifd, 0x0001U);
                derived.origin.block = block;
                derived.origin.order_in_block = 0U;
                derived.origin.wire_type = WireType { WireFamily::Other, 2U };
                derived.origin.wire_count = 4U;
                derived.value = make_u16_array(store.arena(), values);
                derived.flags = EntryFlags::Derived;
                (void)store.add_entry(derived);
                if (status_out) {
                    ++status_out->entries_decoded;
                }
            }
        } else {
            if (!canon_value_has_width(value, entry.count32, 2U)) {
                return SourceIfdValueExpansionStatus::Incomplete;
            }
            decode_canon_u16_table(cfg, tiff_bytes, abs_value_off,
                                   entry.count32, sub_ifd, store, model,
                                   options, status_out);
        }
        return SourceIfdValueExpansionStatus::Expanded;
    }
    if (entry.type == 4U && entry.tag == 0x4013U) {
        const std::string_view sub_ifd = make_mk_subtable_ifd_token(
            prefix, "afmicroadj", 0U, std::span<char>(sub_ifd_buffer));
        uint32_t mode = 0U;
        uint32_t numer = 0U;
        uint32_t denom = 0U;
        if (value.size() >= 16U
            && read_tiff_u32(cfg, value, 4U, &mode)
            && read_tiff_u32(cfg, value, 8U, &numer)
            && read_tiff_u32(cfg, value, 12U, &denom)) {
            const BlockId block = store.add_block(BlockInfo {});
            if (block != kInvalidBlockId) {
                Entry mode_entry;
                mode_entry.key = make_exif_tag_key(store.arena(), sub_ifd,
                                                   0x0001U);
                mode_entry.origin.block = block;
                mode_entry.origin.order_in_block = 0U;
                mode_entry.origin.wire_type = WireType { WireFamily::Other,
                                                         4U };
                mode_entry.origin.wire_count = 1U;
                mode_entry.value = make_u32(mode);
                mode_entry.flags = EntryFlags::Derived;
                (void)store.add_entry(mode_entry);

                Entry value_entry;
                value_entry.key = make_exif_tag_key(store.arena(), sub_ifd,
                                                    0x0002U);
                value_entry.origin.block = block;
                value_entry.origin.order_in_block = 1U;
                value_entry.origin.wire_type = WireType { WireFamily::Other,
                                                          10U };
                value_entry.origin.wire_count = 1U;
                value_entry.value = make_urational(numer, denom);
                value_entry.flags = EntryFlags::Derived;
                (void)store.add_entry(value_entry);
                if (status_out) {
                    status_out->entries_decoded += 2U;
                }
            }
        } else {
            if (!canon_value_has_width(value, entry.count32, 4U)) {
                return SourceIfdValueExpansionStatus::Incomplete;
            }
            decode_canon_u32_table(cfg, tiff_bytes, abs_value_off,
                                   entry.count32, sub_ifd, store, options,
                                   status_out);
        }
        return SourceIfdValueExpansionStatus::Expanded;
    }

    return SourceIfdValueExpansionStatus::NotApplicable;
}

SourceIfdValueExpansionStatus
decode_canon_makernote_value(const TiffConfig& cfg,
                             const ClassicIfdEntry& entry, bool have_value,
                             std::span<const std::byte> value,
                             std::string_view ifd_name, MetaStore& store,
                             const ExifDecodeOptions& options,
                             ExifDecodeResult* status_out,
                             const SourceIfdValueContext& context) noexcept
{
    bool afinfo2_recognized = false;
    const SourceIfdValueExpansionStatus expansion
        = expand_canon_makernote_value(cfg, entry, have_value, value, ifd_name,
                                       store, options, status_out, nullptr,
                                       &afinfo2_recognized,
                                       context.canon_afinfo2_model,
                                       context.canon_afinfo3_present,
                                       context.canon_afinfo3_context_known);
    if (expansion == SourceIfdValueExpansionStatus::Incomplete
        && !afinfo2_recognized && entry.tag == 0x0026U && entry.type == 3U) {
        uint64_t value_bytes = 0U;
        if (!classic_ifd_entry_value_bytes(entry, &value_bytes)
            || value_bytes < 16U
            || value_bytes > options.limits.max_value_bytes) {
            return expansion;
        }
        char sub_ifd_buffer[96];
        const std::string_view sub_ifd
            = make_mk_subtable_ifd_token("mk_canon", "afinfo2", 0U,
                                         std::span<char>(sub_ifd_buffer));
        return canon_scan_afinfo2_from_source(
            cfg, value_bytes, sub_ifd, store, options, status_out,
            context.canon_afinfo2_model, context.canon_afinfo3_present,
            context.canon_afinfo3_context_known, context);
    }

    const bool filter_info      = entry.tag == 0x4024U && entry.type == 4U;
    const bool custom_functions = entry.tag == 0x0099U;
    if (expansion != SourceIfdValueExpansionStatus::Incomplete || !have_value
        || (!filter_info && !custom_functions)
        || (filter_info && entry.count32 > options.limits.max_entries_per_ifd)
        || value.size() < 8U || value.size() > options.limits.max_value_bytes
        || !context.source) {
        return expansion;
    }

    // The expansion has consumed the borrowed value. Further reads may replace
    // its scratch/window storage; retain only the declared size from here on.
    const uint64_t value_bytes = value.size();
    uint64_t probed_offsets[4] {};
    size_t probed_count = 0U;
    for (int64_t base : context.alternate_value_bases) {
        uint64_t offset = 0U;
        if (base == INT64_MIN
            || !canon_add_special_base_and_off32(base, entry.value_or_off32,
                                                 &offset)
            || offset == context.value_offset
            || canon_value_overlaps_ifd(offset, value_bytes,
                                        context.excluded_value_span.offset,
                                        context.excluded_value_span.size)
            || !source_tiff_contains(*context.source, offset, value_bytes)) {
            continue;
        }
        bool already_probed = false;
        for (size_t i = 0U; i < probed_count; ++i) {
            already_probed |= probed_offsets[i] == offset;
        }
        if (already_probed) {
            continue;
        }
        if (probed_count == 4U) {
            break;
        }
        probed_offsets[probed_count++] = offset;

        std::span<const std::byte> header;
        if (!source_tiff_value(context.source, offset,
                               custom_functions ? 8U : 4U, &header)) {
            return expansion;
        }
        const TiffConfig payload_cfg { true, false };
        uint32_t declared_bytes = 0U;
        if (custom_functions) {
            uint16_t short_length = 0U;
            uint32_t group_count = 0U;
            if (!read_tiff_u16(payload_cfg, header, 0U, &short_length)
                || !read_tiff_u32(payload_cfg, header, 4U, &group_count)
                || group_count > 0x10000U) {
                continue;
            }
            declared_bytes = short_length;
        } else if (!read_tiff_u32(payload_cfg, header, 0U, &declared_bytes)) {
            continue;
        }
        if (declared_bytes != value_bytes) {
            continue;
        }
        std::span<const std::byte> directory;
        if (!source_tiff_value(context.source, offset, value_bytes,
                               &directory)) {
            return expansion;
        }
        char token[96];
        const std::string_view ifd = make_mk_subtable_ifd_token(
            custom_functions ? "mk_canoncustom" : "mk_canon",
            custom_functions ? "functions2" : "filterinfo", 0U,
            std::span<char>(token));
        const bool decoded = custom_functions
            ? decode_canon_custom_functions2(cfg, directory, 0U, value_bytes,
                                             ifd, store, options, status_out)
            : decode_canon_u32_bin_dir(cfg, directory, 0U, value_bytes, ifd,
                                       store, options, status_out);
        return decoded
                   ? SourceIfdValueExpansionStatus::Expanded
                   : SourceIfdValueExpansionStatus::Incomplete;
    }
    return expansion;
}


bool
decode_canon_makernote(const TiffConfig& cfg,
                       std::span<const std::byte> tiff_bytes,
                       uint64_t maker_note_off, uint64_t maker_note_bytes,
                       std::string_view mk_ifd0, MetaStore& store,
                       const ExifDecodeOptions& options,
                       ExifDecodeResult* status_out,
                       CanonMakerNoteResiduals* residuals,
                       CanonAfInfo2ModelClass afinfo2_model,
                       const int64_t* selected_value_base) noexcept
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

    TiffConfig mk_cfg = cfg;

    uint16_t entry_count = 0;
    if (!read_tiff_u16(mk_cfg, tiff_bytes, maker_note_off, &entry_count)) {
        return false;
    }

    uint64_t needed        = 0;
    bool ok_needed         = canon_dir_bytes(entry_count, &needed);
    uint64_t max_dir_bytes = maker_note_bytes;
    if (max_dir_bytes != 0) {
        // Treat declared MakerNote byte count as a soft bound (some files
        // declare too small), but reject obviously-wrong endianness/layouts.
        max_dir_bytes = (max_dir_bytes <= (UINT64_MAX / 8ULL))
                            ? (max_dir_bytes * 8ULL)
                            : UINT64_MAX;
    }
    bool plausible = ok_needed && entry_count != 0
                     && entry_count <= options.limits.max_entries_per_ifd
                     && needed <= (tiff_bytes.size() - maker_note_off)
                     && (max_dir_bytes == 0 || needed <= max_dir_bytes);

    if (!plausible) {
        // Some Canon MakerNotes are little-endian even when the outer EXIF
        // stream is big-endian. Prefer the endianness whose directory fits in
        // the MakerNote payload.
        mk_cfg.le = !mk_cfg.le;
        if (!read_tiff_u16(mk_cfg, tiff_bytes, maker_note_off, &entry_count)) {
            return false;
        }

        ok_needed     = canon_dir_bytes(entry_count, &needed);
        max_dir_bytes = maker_note_bytes;
        if (max_dir_bytes != 0) {
            max_dir_bytes = (max_dir_bytes <= (UINT64_MAX / 8ULL))
                                ? (max_dir_bytes * 8ULL)
                                : UINT64_MAX;
        }
        plausible = ok_needed && entry_count != 0
                    && entry_count <= options.limits.max_entries_per_ifd
                    && needed <= (tiff_bytes.size() - maker_note_off)
                    && (max_dir_bytes == 0 || needed <= max_dir_bytes);
    }
    if (!plausible) {
        return false;
    }

    const std::span<const std::byte> maker_note
        = tiff_bytes.subspan(static_cast<size_t>(maker_note_off),
                             static_cast<size_t>(maker_note_bytes));
    uint16_t effective_entry_count = entry_count;
    uint64_t effective_ifd_bytes   = needed;
    if (canon_recover_ifd_extent(mk_cfg, maker_note, maker_note_off,
                                 entry_count, &effective_entry_count,
                                 &effective_ifd_bytes)) {
        entry_count = effective_entry_count;
        needed      = effective_ifd_bytes;
    }

    const uint64_t entries_off = maker_note_off + 2ULL;

    // Some Canon MakerNotes are stored as a truncated directory (count too
    // small) with out-of-line values placed elsewhere in the EXIF stream.
    const uint64_t maker_note_span_bytes = (maker_note_bytes < needed)
                                               ? needed
                                               : maker_note_bytes;

    ExifContext ctx(store);

    char model_buf[128];
    std::memset(model_buf, 0, sizeof(model_buf));
    std::string_view model = canon_find_model_text(store,
                                                   std::span<char>(model_buf));

    int32_t offset_schema         = 0;
    const bool have_offset_schema = ctx.find_first_i32("exififd", 0xea1d,
                                                       &offset_schema);

    const int64_t value_base
        = selected_value_base
              ? *selected_value_base
              : guess_canon_value_base(mk_cfg, tiff_bytes, maker_note_off,
                                       maker_note_span_bytes, entry_count,
                                       needed, options.limits,
                                       have_offset_schema, offset_schema);
    const int64_t auto_value_base
        = canon_compute_auto_value_base(mk_cfg, tiff_bytes, maker_note_off,
                                        entry_count, needed, options.limits);
    const int64_t schema_value_base
        = canon_compute_schema_value_base(maker_note_off, have_offset_schema,
                                          offset_schema);

    MakerNoteLayout layout;
    layout.cfg                                = mk_cfg;
    layout.bytes                              = tiff_bytes;
    layout.offsets.out_of_line_base_is_signed = true;
    layout.offsets.out_of_line_base_i64       = value_base;

    if (model.empty()) {
        model = canon_find_model_text_in_main_ifd(mk_cfg, tiff_bytes,
                                                  entries_off, entry_count,
                                                  maker_note_off, needed,
                                                  layout,
                                                  std::span<char>(model_buf));
    }

    bool afinfo3_present       = false;
    bool afinfo3_context_known = maker_note_bytes != 0U && needed >= 4U
                                 && maker_note_bytes >= needed - 4U;
    if (afinfo3_context_known) {
        for (uint32_t i = 0U; i < entry_count; ++i) {
            const uint64_t entry_off = entries_off + uint64_t(i) * 12U;
            ClassicIfdEntry entry;
            if (!read_classic_ifd_entry(mk_cfg, tiff_bytes, entry_off, &entry)) {
                afinfo3_context_known = false;
                afinfo3_present       = false;
                break;
            }
            afinfo3_present |= entry.tag == 0x003cU;
        }
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return true;
    }

    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        ClassicIfdEntry ifd_entry;
        if (!read_classic_ifd_entry(mk_cfg, tiff_bytes, eoff, &ifd_entry)) {
            return true;
        }

        const uint16_t tag     = ifd_entry.tag;
        const uint16_t type    = ifd_entry.type;
        const uint32_t count32 = ifd_entry.count32;
        const uint64_t count   = count32;

        ClassicIfdValueRef ref;
        if (!resolve_classic_ifd_value_ref(layout, eoff, ifd_entry, &ref,
                                           status_out)) {
            continue;
        }

        const uint64_t value_bytes = ref.value_bytes;
        if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            continue;
        }

        const bool structural_overlap
            = !ref.inline_value
              && canon_value_overlaps_ifd(ref.value_off, value_bytes,
                                          maker_note_off, needed);
        const uint64_t abs_value_off = ref.value_off;
        if (!structural_overlap
            && (abs_value_off > tiff_bytes.size()
                || value_bytes > tiff_bytes.size() - abs_value_off)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return true;
        }

        if (structural_overlap) {
            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, tag);
            entry.origin.block = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);
            entry.flags |= EntryFlags::Unreadable;
            canon_maybe_mark_contextual_name(model, mk_ifd0, tag, &entry);
            (void)store.add_entry(entry);
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                status_out->entries_decoded += 1U;
            }
            continue;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
        entry.origin.wire_count     = static_cast<uint32_t>(count);
        entry.value = decode_tiff_value(mk_cfg, tiff_bytes, type, count,
                                        abs_value_off, value_bytes,
                                        store.arena(), options.limits,
                                        status_out);
        if (tag == 0x0038u && type == 7 && count <= 8U
            && entry.value.kind == MetaValueKind::Bytes) {
            entry.flags |= EntryFlags::ContextualName;
            entry.origin.name_context_kind = EntryNameContextKind::CanonMain0038;
            entry.origin.name_context_variant = 1U;
        }
        canon_maybe_mark_contextual_name(model, mk_ifd0, tag, &entry);

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }

        if (tag == 0x0006 && model.empty()) {
            model = canon_find_model_text(store, std::span<char>(model_buf));
        }

        const std::span<const std::byte> value_raw
            = tiff_bytes.subspan(static_cast<size_t>(abs_value_off),
                                 static_cast<size_t>(value_bytes));
        const CanonMakerNoteValueContext value_context {
            tiff_bytes,      abs_value_off,         maker_note_off,
            needed,          maker_note_span_bytes, ifd_entry.value_or_off32,
            auto_value_base, schema_value_base
        };
        const SourceIfdValueExpansionStatus expansion
            = expand_canon_makernote_value(mk_cfg, ifd_entry, true, value_raw,
                                           mk_ifd0, store, options, status_out,
                                           &value_context, nullptr,
                                           afinfo2_model, afinfo3_present,
                                           afinfo3_context_known);
        if (expansion == SourceIfdValueExpansionStatus::Incomplete && residuals) {
            if (residuals->paths_skipped != UINT32_MAX) {
                ++residuals->paths_skipped;
            } else if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            if (!residuals->location_known) {
                residuals->first_offset = abs_value_off;
                residuals->first_tag = ifd_entry.tag;
                residuals->location_known = true;
            }
        }
    }

    return true;
}

ExifDecodeResult
decode_canon_cr3_tiff(std::span<const std::byte> bytes, MetaStore& store,
                      std::span<ExifIfdRef> out_ifds,
                      const ExifDecodeOptions& options,
                      CanonMakerNoteResiduals* residuals,
                      CanonAfInfo2ModelClass afinfo2_model) noexcept
{
    store.constrain_resources(options.limits.max_total_entries,
                              options.limits.max_arena_bytes);
    if (options.limits.max_ifds == 0U) {
        ExifDecodeResult result;
        result.status = ExifDecodeStatus::LimitExceeded;
        result.limit_reason = ExifLimitReason::MaxIfds;
        return result;
    }
    ExifDecodeOptions canon = options;
    canon.decode_printim = false;
    canon.decode_makernote = false;
    canon.tokens.ifd_prefix = "mk_canon";
    canon.tokens.subifd_prefix = "mk_canon_subifd";
    canon.tokens.exif_ifd_token = "mk_canon_exififd";
    canon.tokens.gps_ifd_token = "mk_canon_gpsifd";
    canon.tokens.interop_ifd_token = "mk_canon_interopifd";
    TiffConfig cfg;
    uint16_t magic = 0U;
    uint32_t offset = 0U;
    if (bytes.size() >= 8U
        && ((bytes[0] == std::byte { 'I' } && bytes[1] == std::byte { 'I' })
            || (bytes[0] == std::byte { 'M' } && bytes[1] == std::byte { 'M' }))) {
        cfg.le = bytes[0] == std::byte { 'I' };
        cfg.bigtiff = false;
        if (read_tiff_u16(cfg, bytes, 2U, &magic) && magic == 42U
            && read_tiff_u32(cfg, bytes, 4U, &offset) && offset < bytes.size()) {
            ExifDecodeResult result;
            if (decode_canon_makernote(cfg, bytes, offset,
                                       bytes.size() - offset, "mk_canon0",
                                       store, canon, &result, residuals,
                                       afinfo2_model)) {
                if (store.resource_limit_exceeded()) {
                    update_status(&result, ExifDecodeStatus::LimitExceeded);
                    result.limit_reason = store.arena().limit_exceeded()
                        ? ExifLimitReason::MaxArenaBytes
                        : ExifLimitReason::MaxTotalEntries;
                    result.limit_ifd_offset = offset;
                }
                return result;
            }
        }
    }
    return decode_exif_tiff(bytes, store, out_ifds, canon);
}

void
decode_canon_ciff_binary_table(std::span<const std::byte> raw, bool le,
                               uint16_t source_tag, uint32_t index,
                               std::string_view model, MetaStore& store,
                               const ExifDecodeLimits& limits,
                               ExifDecodeResult* result) noexcept
{
    if (raw.empty() || raw.size() % 2U != 0U)
        return;
    const size_t count = raw.size() / 2U;
    if (count > limits.max_entries_per_ifd
        || raw.size() > limits.max_value_bytes) {
        update_status(result, ExifDecodeStatus::LimitExceeded);
        return;
    }
    store.constrain_resources(limits.max_total_entries, limits.max_arena_bytes);
    if (store.resource_limit_exceeded()) {
        update_status(result, ExifDecodeStatus::LimitExceeded);
        return;
    }
    const TiffConfig cfg { le, false };
    char token[96];
    auto family = [&](std::string_view name) noexcept {
        for (size_t i = 0U; i + name.size() <= model.size(); ++i) {
            if (!canon_ascii_contains_insensitive(model.substr(i, name.size()),
                                                  name))
                continue;
            const size_t end = i + name.size();
            const char c     = end < model.size() ? model[end] : '\0';
            const bool word  = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                              || (c >= '0' && c <= '9') || c == '_';
            if (!word)
                return true;
        }
        return false;
    };
    if (source_tag == 0x1033U) {
        uint16_t declared = 0U;
        (void)read_tiff_u16(cfg, raw, 0U, &declared);
        const bool d60 = family("EOS D60");
        if (declared != raw.size()
            && !(d60 && size_t(declared) + 2U == raw.size())) {
            update_status(result, ExifDecodeStatus::Malformed);
            return;
        }
        const std::string_view suffix = family("EOS 10D")
                                            ? "functions10d"
                                            : ((family("EOS D30") || d60)
                                                   ? "functionsd30"
                                                   : "functionsunknown");
        const auto ifd = make_mk_subtable_ifd_token("mk_canoncustom", suffix,
                                                    index, token);
        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return;
        }
        for (uint32_t i = 1U; i < count; ++i) {
            if (store.entries().size() >= limits.max_total_entries) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return;
            }
            uint16_t word = 0U;
            (void)read_tiff_u16(cfg, raw, size_t(i) * 2U, &word);
            const uint16_t tag = uint16_t(word >> 8U);
            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), ifd, tag);
            entry.value        = make_u8(uint8_t(word & 0xffU));
            entry.flags        = EntryFlags::Derived;
            entry.origin.block = block;
            entry.origin.order_in_block = i - 1U;
            entry.origin.wire_type = WireType { WireFamily::Other, source_tag };
            entry.origin.wire_count = 1U;
            canon_maybe_mark_contextual_name(model, ifd, tag, &entry);
            if (store.add_entry(entry) == kInvalidEntryId) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return;
            }
            if (result)
                ++result->entries_decoded;
        }
        return;
    }
    if (source_tag == 0x1038U) {
        uint16_t points = 0U;
        (void)read_tiff_u16(cfg, raw, 0U, &points);
        // These point counts define the documented older serial AF layout.
        if (points != 1U && points != 5U && points != 7U && points != 9U
            && points != 15U && points != 45U && points != 53U)
            return;
        const uint32_t focus_words = (uint32_t(points) + 15U) / 16U;
        const size_t minimum = 16U + size_t(points) * 4U + focus_words * 2U;
        if (raw.size() < minimum) {
            update_status(result, ExifDecodeStatus::Malformed);
            return;
        }
        const bool eos = canon_ascii_contains_insensitive(model, "EOS");
        const bool primary_after_unknown = !eos && count == 36U;
        const size_t primary_offset      = minimum
                                      + (primary_after_unknown ? 16U : 0U);
        const bool primary       = !eos && primary_offset + 2U <= raw.size();
        const bool unknown_array = primary_after_unknown
                                   && minimum + 16U <= raw.size();
        const uint32_t fields = 11U + (primary ? 1U : 0U)
                                + (unknown_array ? 1U : 0U);
        if (fields > limits.max_entries_per_ifd
            || store.entries().size() > limits.max_total_entries
            || fields > limits.max_total_entries - store.entries().size()) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return;
        }
        const auto ifd = make_mk_subtable_ifd_token("mk_canon", "afinfo", index,
                                                    token);
        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return;
        }
        uint32_t order = 0U;
        auto emit      = [&](uint16_t tag, const MetaValue& value) noexcept {
            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), ifd, tag);
            entry.value        = value;
            entry.flags        = EntryFlags::Derived;
            entry.origin.block = block;
            entry.origin.order_in_block = order;
            entry.origin.wire_type = WireType { WireFamily::Other, source_tag };
            entry.origin.wire_count = value.count;
            if (tag == 11U && unknown_array) {
                entry.flags |= EntryFlags::ContextualName;
                entry.origin.name_context_kind
                    = EntryNameContextKind::CanonAfInfo0011;
            }
            if (store.add_entry(entry) == kInvalidEntryId) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return false;
            }
            ++order;
            if (result)
                ++result->entries_decoded;
            return true;
        };
        for (uint16_t tag = 0U; tag < 8U; ++tag) {
            uint16_t value = 0U;
            (void)read_tiff_u16(cfg, raw, size_t(tag) * 2U, &value);
            if (!emit(tag, make_u16(value)))
                return;
        }
        size_t offset = 16U;
        for (uint16_t tag = 8U; tag <= 10U; ++tag) {
            const uint32_t elements = tag == 10U ? focus_words : points;
            if (elements * 2U > limits.max_value_bytes) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return;
            }
            std::array<int16_t, 53> values {};
            for (uint32_t i = 0U; i < elements; ++i) {
                uint16_t bits = 0U;
                (void)read_tiff_u16(cfg, raw, offset + i * 2U, &bits);
                values[i] = static_cast<int16_t>(
                    bits < 32768U ? int32_t(bits) : int32_t(bits) - 65536);
            }
            if (!emit(tag, make_i16_array(store.arena(),
                                          std::span<const int16_t>(values.data(),
                                                                   elements))))
                return;
            offset += elements * 2U;
        }
        if (unknown_array) {
            std::array<uint16_t, 8> values {};
            for (size_t i = 0U; i < values.size(); ++i)
                (void)read_tiff_u16(cfg, raw, minimum + i * 2U, &values[i]);
            if (!emit(11U, make_u16_array(store.arena(), values)))
                return;
        }
        if (primary) {
            uint16_t value = 0U;
            (void)read_tiff_u16(cfg, raw, primary_offset, &value);
            (void)emit(primary_after_unknown ? 12U : 11U, make_u16(value));
        }
        return;
    }
    if (source_tag == 0x10a9U) {
        const auto ifd = make_mk_subtable_ifd_token("mk_canon", "colorbalance",
                                                    index, token);
        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return;
        }
        uint32_t order = 0U;
        for (uint16_t tag = 1U; tag <= 37U; tag += 4U) {
            const size_t offset = size_t(tag) * 2U;
            if (offset > raw.size() || 8U > raw.size() - offset)
                continue;
            if (order >= limits.max_entries_per_ifd
                || store.entries().size() >= limits.max_total_entries) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return;
            }
            std::array<int16_t, 4> levels {};
            for (size_t i = 0; i < levels.size(); ++i) {
                uint16_t bits = 0U;
                (void)read_tiff_u16(cfg, raw, offset + i * 2U, &bits);
                levels[i] = static_cast<int16_t>(
                    bits < 32768U ? int32_t(bits) : int32_t(bits) - 65536);
            }
            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), ifd, tag);
            entry.origin.block = block;
            entry.origin.order_in_block = order++;
            entry.origin.wire_type = WireType { WireFamily::Other, source_tag };
            entry.origin.wire_count = 4U;
            entry.flags             = EntryFlags::Derived;
            if (tag == 29U && family("EOS D60")) {
                entry.flags |= EntryFlags::ContextualName;
                entry.origin.name_context_kind
                    = EntryNameContextKind::CanonColorBalance001D;
            }
            entry.value = make_i16_array(store.arena(), levels);
            if (store.add_entry(entry) == kInvalidEntryId) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return;
            }
            if (result)
                ++result->entries_decoded;
        }
        return;
    }
    std::string_view suffix;
    switch (source_tag) {
    case 0x1029U: suffix = "focallength"; break;
    case 0x102dU: suffix = "camerasettings"; break;
    case 0x102aU: suffix = "shotinfo"; break;
    case 0x1031U: suffix = "sensorinfo"; break;
    default: return;
    }
    const auto ifd      = make_mk_subtable_ifd_token("mk_canon", suffix, index,
                                                     token);
    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        update_status(result, ExifDecodeStatus::LimitExceeded);
        return;
    }
    for (uint32_t tag = 0U; tag < count && tag <= UINT16_MAX; ++tag) {
        if (exif_tag_name(ifd, uint16_t(tag)).empty())
            continue;
        if (store.entries().size() >= limits.max_total_entries) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return;
        }
        uint16_t bits = 0U;
        (void)read_tiff_u16(cfg, raw, size_t(tag) * 2U, &bits);
        const bool unsigned_field
            = source_tag == 0x1029U
              || (source_tag == 0x102dU && tag >= 22U && tag <= 24U)
              || (source_tag == 0x102aU && (tag == 19U || tag == 20U));
        const int16_t signed_value = static_cast<int16_t>(
            bits < 32768U ? int32_t(bits) : int32_t(bits) - 65536);
        Entry entry;
        entry.key   = make_exif_tag_key(store.arena(), ifd, uint16_t(tag));
        entry.value = unsigned_field ? make_u16(bits) : make_i16(signed_value);
        entry.flags = EntryFlags::Derived;
        entry.origin.block          = block;
        entry.origin.order_in_block = tag;
        entry.origin.wire_type  = WireType { WireFamily::Other, source_tag };
        entry.origin.wire_count = 1U;
        canon_maybe_mark_contextual_name(model, ifd, uint16_t(tag), &entry);
        if (store.add_entry(entry) == kInvalidEntryId) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return;
        }
        if (result)
            ++result->entries_decoded;
    }
}
}  // namespace openmeta::exif_internal
