// SPDX-License-Identifier: Apache-2.0

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace openmeta::exif_internal {

static int8_t
nikon_to_i8(uint8_t v) noexcept
{
    // Avoid implementation-defined conversions from uint8_t -> int8_t.
    return (v <= 0x7FU) ? static_cast<int8_t>(v)
                        : static_cast<int8_t>(static_cast<int>(v) - 256);
}

static bool
nikon_parse_u32_dec(std::string_view s, uint32_t* out) noexcept
{
    if (!out) {
        return false;
    }

    size_t i = 0;
    while (i < s.size() && s[i] == ' ') {
        i += 1;
    }
    if (i == s.size()) {
        return false;
    }

    uint64_t v = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') {
            return false;
        }
        v = v * 10ULL + static_cast<uint64_t>(c - '0');
        if (v > 0xFFFFFFFFULL) {
            return false;
        }
    }
    *out = static_cast<uint32_t>(v);
    return true;
}

static bool
nikon_parse_u32_digits(std::string_view s, uint32_t* out) noexcept
{
    if (!out) {
        return false;
    }

    uint64_t v     = 0;
    bool saw_digit = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') {
            continue;
        }
        saw_digit = true;
        v         = v * 10ULL + static_cast<uint64_t>(c - '0');
        if (v > 0xFFFFFFFFULL) {
            return false;
        }
    }
    if (!saw_digit) {
        return false;
    }
    *out = static_cast<uint32_t>(v);
    return true;
}

static bool
nikon_parse_fw_major(std::string_view s, uint32_t* out) noexcept
{
    if (!out) {
        return false;
    }

    size_t i = 0;
    while (i < s.size() && s[i] == ' ') {
        i += 1;
    }
    if (i == s.size() || s[i] < '0' || s[i] > '9') {
        return false;
    }

    uint32_t v    = static_cast<uint32_t>(s[i] - '0');
    size_t digits = 1;
    i += 1;
    while (i < s.size() && digits < 2 && s[i] >= '0' && s[i] <= '9') {
        v = v * 10U + static_cast<uint32_t>(s[i] - '0');
        digits += 1;
        i += 1;
    }

    *out = v;
    return true;
}

static uint8_t
nikon_ascii_lower(uint8_t c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<uint8_t>(c + 32U) : c;
}

static bool
nikon_ascii_equals_insensitive(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (nikon_ascii_lower(static_cast<uint8_t>(a[i]))
            != nikon_ascii_lower(static_cast<uint8_t>(b[i]))) {
            return false;
        }
    }
    return true;
}

static std::string_view
nikon_find_first_exif_ascii_value(const MetaStore& store, std::string_view ifd,
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
        if (arena_string(arena, e.key.data.exif_tag.ifd) != ifd) {
            continue;
        }
        if (e.value.kind == MetaValueKind::Text) {
            return arena_string(arena, e.value.data.span);
        }
    }
    return {};
}

static bool
nikonsettings_model_is_d7500(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON D7500");
}

static bool
nikonsettings_model_is_d780(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON D780");
}

static bool
nikonsettings_model_is_d850(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON D850");
}

static bool
nikonsettings_model_is_z30(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON Z 30");
}

static bool
nikonsettings_model_is_z5(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON Z 5");
}

static bool
nikonsettings_model_is_z50(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON Z 50");
}

static bool
nikonsettings_model_is_z6(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON Z 6");
}

static bool
nikonsettings_model_is_z6ii(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON Z 6_2");
}

static bool
nikonsettings_model_is_z7(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON Z 7");
}

static bool
nikonsettings_model_is_z7ii(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON Z 7_2");
}

static bool
nikonsettings_model_is_zfc(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON Z fc");
}

static bool
nikon_model_is_zf(std::string_view model) noexcept
{
    return nikon_ascii_equals_insensitive(model, "NIKON Z f");
}

static bool
nikonsettings_model_uses_iso_placeholder_names(std::string_view model) noexcept
{
    return nikonsettings_model_is_z5(model) || nikonsettings_model_is_z50(model)
           || nikonsettings_model_is_z30(model)
           || nikonsettings_model_is_z6(model)
           || nikonsettings_model_is_z6ii(model)
           || nikonsettings_model_is_zfc(model);
}

static bool
nikonsettings_model_uses_movie_func_aliases(std::string_view model) noexcept
{
    return nikonsettings_model_is_z5(model) || nikonsettings_model_is_z50(model)
           || nikonsettings_model_is_z6(model)
           || nikonsettings_model_is_z6ii(model)
           || nikonsettings_model_is_z7(model)
           || nikonsettings_model_is_z7ii(model)
           || nikonsettings_model_is_zfc(model);
}

static uint8_t
nikonsettings_main_context_variant(uint16_t tag,
                                   std::string_view model) noexcept
{
    const bool d7500 = nikonsettings_model_is_d7500(model);
    const bool d780  = nikonsettings_model_is_d780(model);
    const bool d850  = nikonsettings_model_is_d850(model);
    const bool z30   = nikonsettings_model_is_z30(model);
    const bool z_iso_placeholders
        = nikonsettings_model_uses_iso_placeholder_names(model);
    const bool z_movie_aliases = nikonsettings_model_uses_movie_func_aliases(
        model);

    switch (tag) {
    case 0x0103u:
    case 0x0104u:
    case 0x010Cu:
    case 0x013Au:
    case 0x013Cu: return 1U;
    case 0x010Bu: return 1U;
    case 0x0001u:
    case 0x0002u:
    case 0x000Du:
        if (d7500 || d780 || d850 || z30 || z_iso_placeholders) {
            return 1U;
        }
        return 0U;
    case 0x001Du:
    case 0x0020u:
    case 0x002Du:
    case 0x0034u:
    case 0x0047u:
    case 0x0052u:
    case 0x0053u:
    case 0x0054u:
    case 0x006Cu:
        if (d7500 || d780 || d850 || z30) {
            return 1U;
        }
        return 0U;
    case 0x0080u:
        if (d7500 || d780 || d850 || z30) {
            return 1U;
        }
        return 0U;
    case 0x0097u:
    case 0x00A0u:
    case 0x00A2u:
    case 0x00A3u:
    case 0x00A5u:
    case 0x00A7u:
    case 0x00B6u:
        if (d780 || d850 || z30) {
            return 1U;
        }
        return 0U;
    case 0x00B1u:
        if (d7500 || d780 || d850 || z30) {
            return 1U;
        }
        if (z_movie_aliases) {
            return 2U;
        }
        return 0U;
    case 0x00B3u:
        if (d850 || z30) {
            return 1U;
        }
        if (z_movie_aliases) {
            return 3U;
        }
        return 0U;
    default: return 0U;
    }
}

static void
maybe_mark_nikonsettings_contextual_name(const MetaStore& store, uint16_t tag,
                                         Entry* entry) noexcept
{
    if (!entry) {
        return;
    }
    const std::string_view model
        = nikon_find_first_exif_ascii_value(store, "ifd0", 0x0110);
    if (tag == 0x010Bu && nikonsettings_model_is_zfc(model)
        && entry->value.kind == MetaValueKind::Scalar
        && entry->value.elem_type == MetaElementType::U32
        && entry->value.data.u64 == 1U) {
        return;
    }
    const uint8_t variant = nikonsettings_main_context_variant(tag, model);
    if (variant == 0U) {
        return;
    }
    entry->flags |= EntryFlags::ContextualName;
    entry->origin.name_context_kind = EntryNameContextKind::NikonSettingsMain;
    entry->origin.name_context_variant = variant;
}

static bool
nikon_is_blank_serial(std::string_view s) noexcept
{
    // Some Nikon models store SerialNumber as 8 NUL bytes (or a mix of NULs and
    // spaces). ExifTool still decrypts certain blocks; treat this as "empty".
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c != '\0' && c != ' ') {
            return false;
        }
    }
    return true;
}

template<size_t N>
static std::span<const std::byte>
nikon_copy_prefix(std::array<std::byte, N>* storage,
                  std::span<const std::byte> raw, size_t count) noexcept
{
    if (!storage || count > N || count > raw.size()) {
        return {};
    }
    std::memcpy(storage->data(), raw.data(), count);
    return std::span<const std::byte>(storage->data(), count);
}

template<size_t N>
static std::span<const std::byte>
nikon_copy_subspan(std::array<std::byte, N>* storage,
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

static uint32_t
nikon_u32le(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) noexcept
{
    return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8)
           | (static_cast<uint32_t>(b2) << 16)
           | (static_cast<uint32_t>(b3) << 24);
}

static uint32_t
nikon_u32le_from_bytes(const uint8_t bytes[4]) noexcept
{
    return nikon_u32le(bytes[0], bytes[1], bytes[2], bytes[3]);
}

static uint64_t
openmeta_f64_to_bits(double v) noexcept
{
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

static bool
nikon_try_decode_preview_ifd_from_makernote(
    std::string_view mk_ifd0, MetaStore& store, bool default_le,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    ExifContext ctx(store);

    uint32_t preview_ifd_off = 0;
    if (!ctx.find_first_u32(mk_ifd0, 0x0011, &preview_ifd_off)) {
        return false;
    }

    MetaValue maker_note;
    if (!ctx.find_first_value("exififd", 0x927C, &maker_note)
        || maker_note.kind != MetaValueKind::Bytes) {
        return false;
    }

    const std::span<const std::byte> mn = store.arena().span(
        maker_note.data.span);
    if (mn.size() < 8) {
        return false;
    }

    uint64_t tiff_base = UINT64_MAX;
    TiffConfig cfg;
    cfg.le      = default_le;
    cfg.bigtiff = false;

    const uint64_t limit = (mn.size() < 128U) ? uint64_t(mn.size()) : 128U;
    for (uint64_t off = 0; off + 8U <= limit; ++off) {
        const uint8_t b0 = u8(mn[static_cast<size_t>(off + 0U)]);
        const uint8_t b1 = u8(mn[static_cast<size_t>(off + 1U)]);
        const uint8_t b2 = u8(mn[static_cast<size_t>(off + 2U)]);
        const uint8_t b3 = u8(mn[static_cast<size_t>(off + 3U)]);
        if (b0 == 'I' && b1 == 'I' && b2 == 0x2A && b3 == 0x00) {
            tiff_base = off;
            cfg.le    = true;
            break;
        }
        if (b0 == 'M' && b1 == 'M' && b2 == 0x00 && b3 == 0x2A) {
            tiff_base = off;
            cfg.le    = false;
            break;
        }
    }

    if (tiff_base == UINT64_MAX || tiff_base >= mn.size()) {
        return false;
    }

    const std::span<const std::byte> tiff_bytes = mn.subspan(
        static_cast<size_t>(tiff_base));
    const uint64_t ifd_off = uint64_t(preview_ifd_off);
    if (ifd_off >= tiff_bytes.size()) {
        return false;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token("mk_nikon", "preview", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return false;
    }

    // The nested decoder grows the arena that owns tiff_bytes.
    const std::vector<std::byte> preview_storage(tiff_bytes.begin(),
                                                 tiff_bytes.end());
    decode_classic_ifd_no_header(cfg, preview_storage, ifd_off, ifd_name, store,
                                 options, status_out, EntryFlags::Derived);
    return true;
}

static void
nikon_append_compat_u8_tag(uint16_t tag, std::span<const std::byte> mn,
                           uint16_t* tags_out, MetaValue* vals_out,
                           uint32_t capacity, uint32_t* out_count) noexcept
{
    if (!tags_out || !vals_out || !out_count) {
        return;
    }
    for (uint32_t i = 0; i < *out_count; ++i) {
        if (tags_out[i] == tag) {
            return;
        }
    }
    if (*out_count >= capacity) {
        return;
    }
    MetaValue v = make_u8(0);
    if (uint64_t(tag) < mn.size()) {
        v = make_u8(u8(mn[static_cast<size_t>(tag)]));
    }
    tags_out[*out_count] = tag;
    vals_out[*out_count] = v;
    *out_count += 1;
}

static constexpr uint8_t kNikonDecryptXlat0[256] = {
    0xC1u, 0xBFu, 0x6Du, 0x0Du, 0x59u, 0xC5u, 0x13u, 0x9Du, 0x83u, 0x61u, 0x6Bu,
    0x4Fu, 0xC7u, 0x7Fu, 0x3Du, 0x3Du, 0x53u, 0x59u, 0xE3u, 0xC7u, 0xE9u, 0x2Fu,
    0x95u, 0xA7u, 0x95u, 0x1Fu, 0xDFu, 0x7Fu, 0x2Bu, 0x29u, 0xC7u, 0x0Du, 0xDFu,
    0x07u, 0xEFu, 0x71u, 0x89u, 0x3Du, 0x13u, 0x3Du, 0x3Bu, 0x13u, 0xFBu, 0x0Du,
    0x89u, 0xC1u, 0x65u, 0x1Fu, 0xB3u, 0x0Du, 0x6Bu, 0x29u, 0xE3u, 0xFBu, 0xEFu,
    0xA3u, 0x6Bu, 0x47u, 0x7Fu, 0x95u, 0x35u, 0xA7u, 0x47u, 0x4Fu, 0xC7u, 0xF1u,
    0x59u, 0x95u, 0x35u, 0x11u, 0x29u, 0x61u, 0xF1u, 0x3Du, 0xB3u, 0x2Bu, 0x0Du,
    0x43u, 0x89u, 0xC1u, 0x9Du, 0x9Du, 0x89u, 0x65u, 0xF1u, 0xE9u, 0xDFu, 0xBFu,
    0x3Du, 0x7Fu, 0x53u, 0x97u, 0xE5u, 0xE9u, 0x95u, 0x17u, 0x1Du, 0x3Du, 0x8Bu,
    0xFBu, 0xC7u, 0xE3u, 0x67u, 0xA7u, 0x07u, 0xF1u, 0x71u, 0xA7u, 0x53u, 0xB5u,
    0x29u, 0x89u, 0xE5u, 0x2Bu, 0xA7u, 0x17u, 0x29u, 0xE9u, 0x4Fu, 0xC5u, 0x65u,
    0x6Du, 0x6Bu, 0xEFu, 0x0Du, 0x89u, 0x49u, 0x2Fu, 0xB3u, 0x43u, 0x53u, 0x65u,
    0x1Du, 0x49u, 0xA3u, 0x13u, 0x89u, 0x59u, 0xEFu, 0x6Bu, 0xEFu, 0x65u, 0x1Du,
    0x0Bu, 0x59u, 0x13u, 0xE3u, 0x4Fu, 0x9Du, 0xB3u, 0x29u, 0x43u, 0x2Bu, 0x07u,
    0x1Du, 0x95u, 0x59u, 0x59u, 0x47u, 0xFBu, 0xE5u, 0xE9u, 0x61u, 0x47u, 0x2Fu,
    0x35u, 0x7Fu, 0x17u, 0x7Fu, 0xEFu, 0x7Fu, 0x95u, 0x95u, 0x71u, 0xD3u, 0xA3u,
    0x0Bu, 0x71u, 0xA3u, 0xADu, 0x0Bu, 0x3Bu, 0xB5u, 0xFBu, 0xA3u, 0xBFu, 0x4Fu,
    0x83u, 0x1Du, 0xADu, 0xE9u, 0x2Fu, 0x71u, 0x65u, 0xA3u, 0xE5u, 0x07u, 0x35u,
    0x3Du, 0x0Du, 0xB5u, 0xE9u, 0xE5u, 0x47u, 0x3Bu, 0x9Du, 0xEFu, 0x35u, 0xA3u,
    0xBFu, 0xB3u, 0xDFu, 0x53u, 0xD3u, 0x97u, 0x53u, 0x49u, 0x71u, 0x07u, 0x35u,
    0x61u, 0x71u, 0x2Fu, 0x43u, 0x2Fu, 0x11u, 0xDFu, 0x17u, 0x97u, 0xFBu, 0x95u,
    0x3Bu, 0x7Fu, 0x6Bu, 0xD3u, 0x25u, 0xBFu, 0xADu, 0xC7u, 0xC5u, 0xC5u, 0xB5u,
    0x8Bu, 0xEFu, 0x2Fu, 0xD3u, 0x07u, 0x6Bu, 0x25u, 0x49u, 0x95u, 0x25u, 0x49u,
    0x6Du, 0x71u, 0xC7u,
};

static constexpr uint8_t kNikonDecryptXlat1[256] = {
    0xA7u, 0xBCu, 0xC9u, 0xADu, 0x91u, 0xDFu, 0x85u, 0xE5u, 0xD4u, 0x78u, 0xD5u,
    0x17u, 0x46u, 0x7Cu, 0x29u, 0x4Cu, 0x4Du, 0x03u, 0xE9u, 0x25u, 0x68u, 0x11u,
    0x86u, 0xB3u, 0xBDu, 0xF7u, 0x6Fu, 0x61u, 0x22u, 0xA2u, 0x26u, 0x34u, 0x2Au,
    0xBEu, 0x1Eu, 0x46u, 0x14u, 0x68u, 0x9Du, 0x44u, 0x18u, 0xC2u, 0x40u, 0xF4u,
    0x7Eu, 0x5Fu, 0x1Bu, 0xADu, 0x0Bu, 0x94u, 0xB6u, 0x67u, 0xB4u, 0x0Bu, 0xE1u,
    0xEAu, 0x95u, 0x9Cu, 0x66u, 0xDCu, 0xE7u, 0x5Du, 0x6Cu, 0x05u, 0xDAu, 0xD5u,
    0xDFu, 0x7Au, 0xEFu, 0xF6u, 0xDBu, 0x1Fu, 0x82u, 0x4Cu, 0xC0u, 0x68u, 0x47u,
    0xA1u, 0xBDu, 0xEEu, 0x39u, 0x50u, 0x56u, 0x4Au, 0xDDu, 0xDFu, 0xA5u, 0xF8u,
    0xC6u, 0xDAu, 0xCAu, 0x90u, 0xCAu, 0x01u, 0x42u, 0x9Du, 0x8Bu, 0x0Cu, 0x73u,
    0x43u, 0x75u, 0x05u, 0x94u, 0xDEu, 0x24u, 0xB3u, 0x80u, 0x34u, 0xE5u, 0x2Cu,
    0xDCu, 0x9Bu, 0x3Fu, 0xCAu, 0x33u, 0x45u, 0xD0u, 0xDBu, 0x5Fu, 0xF5u, 0x52u,
    0xC3u, 0x21u, 0xDAu, 0xE2u, 0x22u, 0x72u, 0x6Bu, 0x3Eu, 0xD0u, 0x5Bu, 0xA8u,
    0x87u, 0x8Cu, 0x06u, 0x5Du, 0x0Fu, 0xDDu, 0x09u, 0x19u, 0x93u, 0xD0u, 0xB9u,
    0xFCu, 0x8Bu, 0x0Fu, 0x84u, 0x60u, 0x33u, 0x1Cu, 0x9Bu, 0x45u, 0xF1u, 0xF0u,
    0xA3u, 0x94u, 0x3Au, 0x12u, 0x77u, 0x33u, 0x4Du, 0x44u, 0x78u, 0x28u, 0x3Cu,
    0x9Eu, 0xFDu, 0x65u, 0x57u, 0x16u, 0x94u, 0x6Bu, 0xFBu, 0x59u, 0xD0u, 0xC8u,
    0x22u, 0x36u, 0xDBu, 0xD2u, 0x63u, 0x98u, 0x43u, 0xA1u, 0x04u, 0x87u, 0x86u,
    0xF7u, 0xA6u, 0x26u, 0xBBu, 0xD6u, 0x59u, 0x4Du, 0xBFu, 0x6Au, 0x2Eu, 0xAAu,
    0x2Bu, 0xEFu, 0xE6u, 0x78u, 0xB6u, 0x4Eu, 0xE0u, 0x2Fu, 0xDCu, 0x7Cu, 0xBEu,
    0x57u, 0x19u, 0x32u, 0x7Eu, 0x2Au, 0xD0u, 0xB8u, 0xBAu, 0x29u, 0x00u, 0x3Cu,
    0x52u, 0x7Du, 0xA8u, 0x49u, 0x3Bu, 0x2Du, 0xEBu, 0x25u, 0x49u, 0xFAu, 0xA3u,
    0xAAu, 0x39u, 0xA7u, 0xC5u, 0xA7u, 0x50u, 0x11u, 0x36u, 0xFBu, 0xC6u, 0x67u,
    0x4Au, 0xF5u, 0xA5u, 0x12u, 0x65u, 0x7Eu, 0xB0u, 0xDFu, 0xAFu, 0x4Eu, 0xB3u,
    0x61u, 0x7Fu, 0x2Fu,
};

static bool
nikon_decrypt(std::span<const std::byte> enc, uint32_t serial_key,
              uint32_t shutter_count, std::span<std::byte> out) noexcept
{
    if (enc.size() != out.size()) {
        return false;
    }

    const uint8_t serial8 = static_cast<uint8_t>(serial_key & 0xFFu);
    const uint8_t key     = static_cast<uint8_t>(
        (shutter_count >> 0) ^ (shutter_count >> 8) ^ (shutter_count >> 16)
        ^ (shutter_count >> 24));

    const uint8_t ci0 = kNikonDecryptXlat0[serial8];
    uint8_t cj        = kNikonDecryptXlat1[key];
    uint8_t ck        = 0x60u;

    for (size_t i = 0; i < enc.size(); ++i) {
        const uint32_t prod = static_cast<uint32_t>(ci0)
                              * static_cast<uint32_t>(ck);
        cj = static_cast<uint8_t>((static_cast<uint32_t>(cj) + prod) & 0xFFu);
        ck = static_cast<uint8_t>((static_cast<uint32_t>(ck) + 1U) & 0xFFu);
        out[i] = std::byte { static_cast<uint8_t>(u8(enc[i]) ^ cj) };
    }
    return true;
}

static void
decode_nikon_bin_dir_entries(std::string_view ifd_name, MetaStore& store,
                             std::span<const uint16_t> tags,
                             std::span<const MetaValue> values,
                             const ExifDecodeLimits& limits,
                             ExifDecodeResult* status_out) noexcept
{
    emit_bin_dir_entries(ifd_name, store, tags, values, limits, status_out);
}

static void
decode_nikon_bin_dir_entries_with_context(
    std::string_view ifd_name, MetaStore& store, std::span<const uint16_t> tags,
    std::span<const MetaValue> values, std::span<const uint8_t> variants,
    EntryNameContextKind context_kind, const ExifDecodeLimits& limits,
    ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || tags.size() != values.size()
        || tags.size() != variants.size()) {
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    for (uint32_t i = 0; i < tags.size(); ++i) {
        if (status_out
            && (status_out->entries_decoded + 1U) > limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd_name, tags[i]);
        entry.origin.block          = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = values[i].count;
        entry.value                 = values[i];
        entry.flags |= EntryFlags::Derived;
        if (variants[i] != 0U) {
            entry.flags |= EntryFlags::ContextualName;
            entry.origin.name_context_kind    = context_kind;
            entry.origin.name_context_variant = variants[i];
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1U;
        }
    }
}

static void
decode_nikon_shotinfo_d850_offsets(std::string_view mk_prefix,
                                   uint32_t* idx_menusettings,
                                   uint32_t* idx_moresettings,
                                   std::span<const std::byte> dec_src,
                                   MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
{
    if (!idx_menusettings || !idx_moresettings) {
        return;
    }

    static constexpr uint32_t kMenuOffsetTag = 0x0010U;
    static constexpr uint32_t kMoreOffsetTag = 0x004CU;
    static constexpr uint64_t kMenuLen       = 2000ULL;
    static constexpr uint64_t kMoreLen       = 236ULL;

    uint32_t menu_off = 0;
    if (read_u32le(dec_src, kMenuOffsetTag, &menu_off)) {
        const uint64_t menu_start = uint64_t(menu_off);
        if (menu_start + kMenuLen <= dec_src.size()) {
            const uint64_t off = menu_start + 0x06DDULL;
            if (off < dec_src.size()) {
                char menu_buf[96];
                const std::string_view menu_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "menusettingsd850",
                                                 (*idx_menusettings)++,
                                                 std::span<char>(menu_buf));
                if (!menu_ifd.empty()) {
                    const uint16_t tags_out[]  = { 0x06DDU };
                    const MetaValue vals_out[] = { make_u8(static_cast<uint8_t>(
                        u8(dec_src[static_cast<size_t>(off)]) & 0x07U)) };
                    decode_nikon_bin_dir_entries(
                        menu_ifd, store, std::span<const uint16_t>(tags_out),
                        std::span<const MetaValue>(vals_out), options.limits,
                        status_out);
                }
            }
        }
    }

    uint32_t more_off = 0;
    if (read_u32le(dec_src, kMoreOffsetTag, &more_off)) {
        const uint64_t more_start = uint64_t(more_off);
        if (more_start + kMoreLen <= dec_src.size()) {
            const uint64_t off = more_start + 0x0025ULL;
            if (off < dec_src.size()) {
                char more_buf[96];
                const std::string_view more_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "moresettingsd850",
                                                 (*idx_moresettings)++,
                                                 std::span<char>(more_buf));
                if (!more_ifd.empty()) {
                    const uint16_t tags_out[]  = { 0x0025U };
                    const MetaValue vals_out[] = { make_u8(static_cast<uint8_t>(
                        (u8(dec_src[static_cast<size_t>(off)]) & 0x80U)
                        != 0U)) };
                    decode_nikon_bin_dir_entries(
                        more_ifd, store, std::span<const uint16_t>(tags_out),
                        std::span<const MetaValue>(vals_out), options.limits,
                        status_out);
                }
            }
        }
    }
}

static void
decode_nikon_shotinfo_d5d500_offsets(
    std::string_view mk_prefix, bool is_d500_model, uint32_t* idx_rotationinfo,
    uint32_t* idx_jpginfo, uint32_t* idx_bracketinginfo,
    uint32_t* idx_shootingmenu, uint32_t* idx_otherinfo,
    std::span<const std::byte> dec_src, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    if (!idx_rotationinfo || !idx_jpginfo || !idx_bracketinginfo
        || !idx_shootingmenu || !idx_otherinfo) {
        return;
    }

    uint32_t sub_off = 0;

    if (read_u32le(dec_src, 0x0010U, &sub_off)) {
        const uint64_t start = static_cast<uint64_t>(sub_off);
        char buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "rotationinfod500",
                                         (*idx_rotationinfo)++,
                                         std::span<char>(buf));
        if (!ifd_name.empty()) {
            uint16_t tags_out[2];
            MetaValue vals_out[2];
            uint32_t out_count = 0;

            if (start + 0x001AU < dec_src.size()) {
                tags_out[out_count] = 0x001AU;
                vals_out[out_count] = make_u8(static_cast<uint8_t>(
                    u8(dec_src[static_cast<size_t>(start + 0x001AU)]) & 0x03U));
                out_count += 1;
            }
            if (start + 0x0532U < dec_src.size()) {
                tags_out[out_count] = 0x0532U;
                vals_out[out_count] = make_u8(static_cast<uint8_t>(
                    u8(dec_src[static_cast<size_t>(start + 0x0532U)]) & 0x01U));
                out_count += 1;
            }

            decode_nikon_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count), options.limits,
                status_out);
        }
    }

    if (read_u32le(dec_src, 0x0014U, &sub_off)) {
        const uint64_t start = static_cast<uint64_t>(sub_off);
        if (start + 0x0024U < dec_src.size()) {
            char buf[96];
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "jpginfod500",
                                             (*idx_jpginfo)++,
                                             std::span<char>(buf));
            if (!ifd_name.empty()) {
                const uint16_t tags_out[]  = { 0x0024U };
                const MetaValue vals_out[] = { make_u8(static_cast<uint8_t>(
                    u8(dec_src[static_cast<size_t>(start + 0x0024U)])
                    & 0x01U)) };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
            }
        }
    }

    if (read_u32le(dec_src, 0x002CU, &sub_off)) {
        const uint64_t start = static_cast<uint64_t>(sub_off);
        char buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "bracketinginfod500",
                                         (*idx_bracketinginfo)++,
                                         std::span<char>(buf));
        if (!ifd_name.empty()) {
            uint16_t tags_out[4];
            MetaValue vals_out[4];
            uint32_t out_count = 0;

            if (start + 0x000FU < dec_src.size()) {
                tags_out[out_count] = 0x000FU;
                vals_out[out_count] = make_u8(
                    u8(dec_src[static_cast<size_t>(start + 0x000FU)]));
                out_count += 1;
            }
            if (start + 0x0010U < dec_src.size()) {
                tags_out[out_count] = 0x0010U;
                vals_out[out_count] = make_u8(
                    u8(dec_src[static_cast<size_t>(start + 0x0010U)]));
                out_count += 1;
            }
            if (start + 0x0017U < dec_src.size()) {
                tags_out[out_count] = 0x0017U;
                vals_out[out_count] = make_u8(static_cast<uint8_t>(
                    (u8(dec_src[static_cast<size_t>(start + 0x0017U)]) >> 4U)
                    & 0x0FU));
                out_count += 1;
            }
            if (start + 0x0018U < dec_src.size()) {
                tags_out[out_count] = 0x0018U;
                vals_out[out_count] = make_u8(static_cast<uint8_t>(
                    u8(dec_src[static_cast<size_t>(start + 0x0018U)]) & 0x0FU));
                out_count += 1;
            }

            decode_nikon_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count), options.limits,
                status_out);
        }
    }

    if (read_u32le(dec_src, 0x0050U, &sub_off)) {
        const uint64_t start = static_cast<uint64_t>(sub_off);
        char buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "shootingmenud500",
                                         (*idx_shootingmenu)++,
                                         std::span<char>(buf));
        if (!ifd_name.empty()) {
            uint16_t tags_out[5];
            MetaValue vals_out[5];
            uint32_t out_count = 0;

            if (start + 0x0000U < dec_src.size()) {
                tags_out[out_count] = 0x0000U;
                vals_out[out_count] = make_u8(static_cast<uint8_t>(
                    u8(dec_src[static_cast<size_t>(start + 0x0000U)]) & 0x03U));
                out_count += 1;
            }
            if (is_d500_model && start + 0x0002U < dec_src.size()) {
                tags_out[out_count] = 0x0002U;
                vals_out[out_count] = make_u8(static_cast<uint8_t>(
                    (u8(dec_src[static_cast<size_t>(start + 0x0002U)]) & 0x80U)
                    != 0U));
                out_count += 1;
            }
            if (start + 0x0004U < dec_src.size()) {
                tags_out[out_count] = 0x0004U;
                vals_out[out_count] = make_u8(static_cast<uint8_t>(
                    u8(dec_src[static_cast<size_t>(start + 0x0004U)]) & 0x3FU));
                out_count += 1;
            }
            if (start + 0x0005U < dec_src.size()) {
                tags_out[out_count] = 0x0005U;
                vals_out[out_count] = make_u8(
                    u8(dec_src[static_cast<size_t>(start + 0x0005U)]));
                out_count += 1;
            }
            if (start + 0x0007U < dec_src.size()) {
                tags_out[out_count] = 0x0007U;
                vals_out[out_count] = make_u8(static_cast<uint8_t>(
                    (u8(dec_src[static_cast<size_t>(start + 0x0007U)]) & 0x20U)
                    != 0U));
                out_count += 1;
            }

            decode_nikon_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count), options.limits,
                status_out);
        }
    }

    if (is_d500_model && read_u32le(dec_src, 0x00A8U, &sub_off)) {
        const uint64_t start = static_cast<uint64_t>(sub_off);
        if (start + 0x0214U < dec_src.size()) {
            char buf[96];
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "otherinfod500",
                                             (*idx_otherinfo)++,
                                             std::span<char>(buf));
            if (!ifd_name.empty()) {
                const uint16_t tags_out[]  = { 0x0214U };
                const MetaValue vals_out[] = { make_u8(static_cast<uint8_t>(
                    u8(dec_src[static_cast<size_t>(start + 0x0214U)])
                    & 0x03U)) };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
            }
        }
    }
}

static void
decode_nikon_shotinfo_d6_offsets(std::string_view mk_prefix,
                                 uint32_t* idx_seqinfo,
                                 uint32_t* idx_intervalinfo,
                                 std::span<const std::byte> dec_src,
                                 MetaStore& store,
                                 const ExifDecodeOptions& options,
                                 ExifDecodeResult* status_out) noexcept
{
    if (!idx_seqinfo || !idx_intervalinfo) {
        return;
    }

    uint32_t sub_off = 0;
    if (read_u32le(dec_src, 0x0030U, &sub_off)) {
        const uint64_t start = static_cast<uint64_t>(sub_off);
        char buf[96];
        const std::string_view ifd_name = make_mk_subtable_ifd_token(
            mk_prefix, "seqinfod6", (*idx_seqinfo)++, std::span<char>(buf));
        if (!ifd_name.empty()) {
            uint16_t tags_out[3];
            MetaValue vals_out[3];
            uint32_t out_count = 0;

            uint16_t v16 = 0;
            if (start + 0x0024U + 2U <= dec_src.size()
                && read_u16le(dec_src, start + 0x0024U, &v16)) {
                tags_out[out_count] = 0x0024U;
                vals_out[out_count] = make_u16(v16);
                out_count += 1;
            }
            if (start + 0x0028U + 2U <= dec_src.size()
                && read_u16le(dec_src, start + 0x0028U, &v16)) {
                tags_out[out_count] = 0x0028U;
                vals_out[out_count] = make_u16(v16);
                out_count += 1;
            }
            if (start + 0x002BU < dec_src.size()) {
                tags_out[out_count] = 0x002BU;
                vals_out[out_count] = make_u8(
                    u8(dec_src[static_cast<size_t>(start + 0x002BU)]));
                out_count += 1;
            }

            decode_nikon_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count), options.limits,
                status_out);
        }
    }

    if (read_u32le(dec_src, 0x00A4U, &sub_off)) {
        const uint64_t start = static_cast<uint64_t>(sub_off);
        char buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "intervalinfod6",
                                         (*idx_intervalinfo)++,
                                         std::span<char>(buf));
        if (!ifd_name.empty()) {
            uint16_t tags_out[18];
            MetaValue vals_out[18];
            uint32_t out_count = 0;

            const uint16_t u32_tags[] = { 0x017CU, 0x0180U };
            for (size_t k = 0; k < sizeof(u32_tags) / sizeof(u32_tags[0]);
                 ++k) {
                uint32_t v32       = 0;
                const uint64_t off = start + static_cast<uint64_t>(u32_tags[k]);
                if (off + 4U > dec_src.size()
                    || !read_u32le(dec_src, off, &v32)) {
                    continue;
                }
                tags_out[out_count] = u32_tags[k];
                vals_out[out_count] = make_u32(v32);
                out_count += 1;
            }

            const uint16_t u8_tags[] = {
                0x0184U, 0x0186U, 0x01A8U, 0x01ACU, 0x01B0U,
                0x01B4U, 0x020EU, 0x0214U, 0x021AU, 0x021EU,
                0x0228U, 0x022CU, 0x0232U, 0x0234U, 0x02CAU,
            };
            for (size_t k = 0; k < sizeof(u8_tags) / sizeof(u8_tags[0]); ++k) {
                const uint64_t off = start + static_cast<uint64_t>(u8_tags[k]);
                if (off >= dec_src.size()) {
                    continue;
                }
                tags_out[out_count] = u8_tags[k];
                vals_out[out_count] = make_u8(
                    u8(dec_src[static_cast<size_t>(off)]));
                out_count += 1;
            }

            if (start + 0x022EU < dec_src.size()) {
                tags_out[out_count] = 0x022EU;
                vals_out[out_count] = make_i8(nikon_to_i8(
                    u8(dec_src[static_cast<size_t>(start + 0x022EU)])));
                out_count += 1;
            }

            decode_nikon_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count), options.limits,
                status_out);
        }
    }
}

static uint64_t
nikon_afinfo2v0101_points_len(uint8_t schema) noexcept
{
    switch (schema) {
    case 1: return 7ULL;
    case 2: return 2ULL;
    case 7: return 20ULL;
    default: break;
    }
    return 1ULL;
}

static uint64_t
nikon_afinfo2v0300_points_len(uint8_t schema) noexcept
{
    switch (schema) {
    case 1: return 7ULL;
    case 8: return 11ULL;
    case 9: return 14ULL;
    default: break;
    }
    return 0ULL;
}

static bool
decode_nikon_colorbalancec_table(
    std::string_view mk_prefix, std::span<char> sub_ifd_buf,
    uint32_t* idx_colorbalance, std::span<const std::byte> raw_src,
    uint64_t version_offset, bool le, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    if (!idx_colorbalance || raw_src.size() < version_offset + 4ULL) {
        return false;
    }

    std::array<std::byte, 4> ver_bytes;
    std::memcpy(ver_bytes.data(), raw_src.data() + version_offset,
                ver_bytes.size());
    const std::string_view ver
        = std::string_view(reinterpret_cast<const char*>(ver_bytes.data()),
                           ver_bytes.size());
    if (ver != "0104" && ver != "0105") {
        return false;
    }

    const std::string_view ifd_name
        = make_mk_subtable_ifd_token(mk_prefix, "colorbalancec",
                                     (*idx_colorbalance)++,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return true;
    }

    std::array<uint16_t, 13> tags_out {};
    std::array<MetaValue, 13> vals_out {};
    uint32_t out_count = 0;

    tags_out[out_count] = 0x0004;
    vals_out[out_count]
        = make_fixed_ascii_text(store.arena(),
                                std::span<const std::byte>(ver_bytes.data(),
                                                           ver_bytes.size()));
    out_count += 1;

    uint16_t black_level = 0;
    if (read_u16_endian(le, raw_src, 0x0020, &black_level)) {
        tags_out[out_count] = 0x0020;
        vals_out[out_count] = make_u16(black_level);
        out_count += 1;
    }

    static constexpr struct {
        uint16_t tag;
        uint64_t offset;
    } kColorBalanceCTags[] = {
        { 0x0038, 0x0038ULL }, { 0x004C, 0x004CULL }, { 0x0060, 0x0060ULL },
        { 0x0074, 0x0074ULL }, { 0x0088, 0x0088ULL }, { 0x009C, 0x009CULL },
        { 0x00B0, 0x00B0ULL }, { 0x00C4, 0x00C4ULL }, { 0x00D8, 0x00D8ULL },
        { 0x0100, 0x0100ULL }, { 0x0114, 0x0114ULL },
    };

    for (size_t cb_idx = 0;
         cb_idx < sizeof(kColorBalanceCTags) / sizeof(kColorBalanceCTags[0]);
         ++cb_idx) {
        const uint64_t off = kColorBalanceCTags[cb_idx].offset;
        if (raw_src.size() < off + 16ULL) {
            continue;
        }
        uint32_t levels[4] {};
        bool ok = true;
        for (uint32_t k = 0; k < 4U; ++k) {
            uint32_t v = 0;
            if (!(le ? read_u32le(raw_src, off + uint64_t(k) * 4ULL, &v)
                     : read_u32be(raw_src, off + uint64_t(k) * 4ULL, &v))) {
                ok = false;
                break;
            }
            levels[k] = v;
        }
        if (!ok) {
            continue;
        }
        // ExifTool exposes NRW white-balance arrays with the red and blue
        // channels doubled relative to the stored values.
        levels[0] *= 2U;
        levels[3] *= 2U;
        tags_out[out_count] = kColorBalanceCTags[cb_idx].tag;
        vals_out[out_count] = make_u32_array(store.arena(),
                                             std::span<const uint32_t>(levels));
        out_count += 1;
    }

    decode_nikon_bin_dir_entries(
        ifd_name, store, std::span<const uint16_t>(tags_out.data(), out_count),
        std::span<const MetaValue>(vals_out.data(), out_count), options.limits,
        status_out);
    return true;
}

static void
decode_nikoncustom_u8_table(std::string_view ifd_name,
                            std::span<const uint8_t> values, MetaStore& store,
                            const ExifDecodeOptions& options,
                            ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || values.empty()) {
        return;
    }

    if (values.size() > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    uint32_t order = 0;
    for (size_t i = 0; i < values.size(); ++i) {
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        const uint32_t tag32 = static_cast<uint32_t>(i);
        if (tag32 > 0xFFFFu) {
            break;
        }

        Entry entry;
        entry.key                   = make_exif_tag_key(store.arena(), ifd_name,
                                                        static_cast<uint16_t>(tag32));
        entry.origin.block          = block;
        entry.origin.order_in_block = order++;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 1 };
        entry.origin.wire_count     = 1;
        entry.flags |= EntryFlags::Derived;
        entry.value = make_u8(values[i]);

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static void
decode_nikon_settings_dir(std::string_view ifd_name,
                          std::span<const std::byte> raw, MetaStore& store,
                          const ExifDecodeOptions& options,
                          ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty()) {
        return;
    }
    if (raw.size() < 24) {
        return;
    }
    if ((raw.size() % 8U) != 0U) {
        return;
    }

    uint32_t rec_count = 0;
    if (!read_u32le(raw, 20, &rec_count)) {
        return;
    }
    if (rec_count == 0) {
        return;
    }
    if (rec_count > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    const uint64_t rec_bytes = uint64_t(rec_count) * 8ULL;
    if (rec_bytes > (raw.size() - 24ULL)) {
        return;
    }
    if (24ULL + rec_bytes != raw.size()) {
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    for (uint32_t i = 0; i < rec_count; ++i) {
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        const uint64_t off = 24ULL + uint64_t(i) * 8ULL;
        uint16_t tag       = 0;
        uint16_t type_be   = 0;
        uint32_t val32     = 0;
        if (!read_u16le(raw, off + 0, &tag)
            || !read_u16be(raw, off + 2, &type_be)
            || !read_u32le(raw, off + 4, &val32)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type_be };
        entry.origin.wire_count     = 1;
        entry.flags |= EntryFlags::Derived;

        switch (type_be) {
        case 1: entry.value = make_u8(static_cast<uint8_t>(val32)); break;
        case 3: entry.value = make_u16(static_cast<uint16_t>(val32)); break;
        case 4: entry.value = make_u32(val32); break;
        case 8:
            entry.value = make_i16(
                static_cast<int16_t>(static_cast<uint16_t>(val32)));
            break;
        case 9: entry.value = make_i32(static_cast<int32_t>(val32)); break;
        default: entry.value = make_u32(val32); break;
        }

        maybe_mark_nikonsettings_contextual_name(store, tag, &entry);
        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}


void
decode_nikon_binary_subdirs(std::string_view mk_ifd0, MetaStore& store, bool le,
                            const ExifDecodeOptions& options,
                            ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return;
    }

    ExifContext ctx(store);

    // Nikon preview sub-IFD (tag 0x0011) is frequently used on older/compact
    // models and some malformed files where Exiv2 still reports NikonPreview
    // tags. Best-effort decode this branch first.
    (void)nikon_try_decode_preview_ifd_from_makernote(mk_ifd0, store, le,
                                                      options, status_out);

    std::string_view model;
    (void)ctx.find_first_text("ifd0", 0x0110 /* Model */, &model);
    // Resolve model-dependent choices before derived values grow the arena.
    const bool model_has_d40 = model.find("NIKON D40")
                               != std::string_view::npos;
    const bool model_has_d80 = model.find("NIKON D80")
                               != std::string_view::npos;
    const bool model_has_d300 = model.find("NIKON D300")
                                != std::string_view::npos;
    const bool model_has_d3  = model.find("NIKON D3") != std::string_view::npos;
    const bool model_has_d3x = model.find("NIKON D3X")
                               != std::string_view::npos;
    const bool model_has_d500 = model.find("NIKON D500")
                                != std::string_view::npos;
    const bool model_is_zf = nikon_model_is_zf(model);

    uint32_t serial_key     = 0;
    uint32_t shutter_count  = 0;
    bool have_serial        = false;
    bool have_shutter_count = false;
    {
        std::string_view serial_s;
        const bool have_serial_tag = ctx.find_first_text(mk_ifd0, 0x001d,
                                                         &serial_s);
        have_serial                = have_serial_tag
                      && nikon_parse_u32_dec(serial_s, &serial_key);
        if (!have_serial && have_serial_tag) {
            // Best-effort fallbacks (ExifTool decrypts even if SerialNumber
            // is blank or non-numeric on some models).
            if (nikon_is_blank_serial(serial_s)) {
                serial_key  = 0;
                have_serial = true;
            } else if (nikon_parse_u32_digits(serial_s, &serial_key)) {
                have_serial = true;
            } else {
                serial_key  = 0;
                have_serial = true;
            }
        }
        have_shutter_count = ctx.find_first_u32(mk_ifd0, 0x00a7,
                                                &shutter_count);
    }

    struct Candidate final {
        uint16_t tag = 0;
        MetaValue value;
    };

    Candidate cands[32];
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
        case 0x001F:  // VRInfo
        case 0x0014:  // NRWData
        case 0x0023:  // PictureControlData
        case 0x00BD:  // PictureControlData (alt)
        case 0x0024:  // WorldTime
        case 0x0025:  // ISOInfo
        case 0x002B:  // DistortInfo
        case 0x002C:  // UnknownInfo
        case 0x0032:  // UnknownInfo2
        case 0x0035:  // HDRInfo
        case 0x0039:  // LocationInfo
        case 0x004E:  // NikonSettings
        case 0x0088:  // AFInfo (older models)
        case 0x0091:  // ShotInfoUnknown
        case 0x0097:  // ColorBalanceUnknown2
        case 0x0098:  // LensData
        case 0x00A8:  // FlashInfo
        case 0x00B0:  // MultiExposure
        case 0x00B7:  // AFInfo2
        case 0x00B8:  // FileInfo
        case 0x00B9:  // AFTune
        case 0x00BB:  // RetouchInfo
            break;
        default: continue;
        }
        if (cand_count < sizeof(cands) / sizeof(cands[0])) {
            cands[cand_count].tag   = tag;
            cands[cand_count].value = e.value;
            cand_count += 1;
        }
    }

    char sub_ifd_buf[96];

    uint32_t idx_vrinfo          = 0;
    uint32_t idx_picturecontrol  = 0;
    uint32_t idx_worldtime       = 0;
    uint32_t idx_isoinfo         = 0;
    uint32_t idx_distortinfo     = 0;
    uint32_t idx_unknowninfo     = 0;
    uint32_t idx_unknowninfo2    = 0;
    uint32_t idx_hdrinfo         = 0;
    uint32_t idx_locationinfo    = 0;
    uint32_t idx_settings        = 0;
    uint32_t idx_nikoncustom     = 0;
    uint32_t idx_afinfo          = 0;
    uint32_t idx_shotinfo        = 0;
    uint32_t idx_seqinfo         = 0;
    uint32_t idx_intervalinfo    = 0;
    uint32_t idx_orientationinfo = 0;
    uint32_t idx_rotationinfo    = 0;
    uint32_t idx_jpginfo         = 0;
    uint32_t idx_bracketinginfo  = 0;
    uint32_t idx_shootingmenu    = 0;
    uint32_t idx_otherinfo       = 0;
    uint32_t idx_menusettings    = 0;
    uint32_t idx_moresettings    = 0;
    uint32_t idx_colorbalance    = 0;
    uint32_t idx_lensdata        = 0;
    uint32_t idx_flashinfo       = 0;
    uint32_t idx_multiexposure   = 0;
    uint32_t idx_afinfo2         = 0;
    uint32_t idx_fileinfo        = 0;
    uint32_t idx_aftune          = 0;
    uint32_t idx_retouchinfo     = 0;

    // Derived text/array values can reallocate the source arena. Allocate
    // scratch once for the largest admitted block, then reuse it per candidate.
    size_t max_source_bytes = 0;
    for (uint32_t i = 0; i < cand_count; ++i) {
        const size_t size = store.arena().span(cands[i].value.data.span).size();
        if (size > max_source_bytes) {
            max_source_bytes = size;
        }
    }
    std::vector<std::byte> source_storage(max_source_bytes);

    for (uint32_t i = 0; i < cand_count; ++i) {
        const uint16_t tag                      = cands[i].tag;
        const ByteSpan raw_span                 = cands[i].value.data.span;
        const std::span<const std::byte> source = store.arena().span(raw_span);
        if (source.empty()) {
            continue;
        }
        std::memcpy(source_storage.data(), source.data(), source.size());
        const std::span<const std::byte> raw_src(source_storage.data(),
                                                 source.size());

        const std::string_view mk_prefix = "mk_nikon";

        if (tag == 0x0014) {  // NRWData
            if (decode_nikon_colorbalancec_table(
                    mk_prefix, std::span<char>(sub_ifd_buf), &idx_colorbalance,
                    raw_src, 0x0004ULL, le, store, options, status_out)) {
                continue;
            }
        }

        if (tag == 0x001F) {  // VRInfo
            if (raw_src.size() < 7) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "vrinfo", idx_vrinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 8> raw;
            const size_t n = (raw_src.size() < raw.size()) ? raw_src.size()
                                                           : raw.size();
            std::memcpy(raw.data(), raw_src.data(), n);

            uint8_t vr_enabled = 0;
            uint8_t vr_mode    = 0;
            if (n > 4) {
                vr_enabled = u8(raw[4]);
            }
            if (n > 6) {
                vr_mode = u8(raw[6]);
            }

            const uint16_t tags_out[]  = { 0x0000, 0x0004, 0x0006 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(),
                                      std::span<const std::byte>(raw.data(), 4)),
                make_u8(vr_enabled),
                make_u8(vr_mode),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x0023 || tag == 0x00BD) {  // PictureControlData
            if (raw_src.size() < 4) {
                continue;
            }

            std::string_view subtable = "picturecontrolunknown";
            if (raw_src.size() >= 2 && u8(raw_src[0]) == '0'
                && u8(raw_src[1]) == '1') {
                subtable = "picturecontrol";
            } else if (raw_src.size() >= 2 && u8(raw_src[0]) == '0'
                       && u8(raw_src[1]) == '2') {
                subtable = "picturecontrol2";
            } else if (raw_src.size() >= 2 && u8(raw_src[0]) == '0'
                       && u8(raw_src[1]) == '3') {
                subtable = "picturecontrol3";
            }

            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                             idx_picturecontrol++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            uint16_t tags_out[32];
            MetaValue vals_out[32];
            uint32_t out_count = 0;

            std::array<std::byte, 4> version_bytes;
            const std::span<const std::byte> version
                = nikon_copy_prefix(&version_bytes, raw_src, 4);
            if (version.empty()) {
                continue;
            }

            tags_out[out_count] = 0x0000;
            vals_out[out_count] = make_fixed_ascii_text(store.arena(), version);
            out_count += 1;

            if (subtable == "picturecontrol3") {
                if (raw_src.size() < 0x001c + 20) {
                    continue;
                }
                std::array<std::byte, 20> desc_bytes;
                const std::span<const std::byte> desc
                    = nikon_copy_subspan(&desc_bytes, raw_src, 0x0008, 20);
                std::array<std::byte, 20> base_bytes;
                const std::span<const std::byte> base
                    = nikon_copy_subspan(&base_bytes, raw_src, 0x001c, 20);
                if (desc.empty() || base.empty()) {
                    continue;
                }
                tags_out[out_count] = 0x0008;
                vals_out[out_count] = make_fixed_ascii_text(store.arena(),
                                                            desc);
                out_count += 1;
                tags_out[out_count] = 0x001c;
                vals_out[out_count] = make_fixed_ascii_text(store.arena(),
                                                            base);
                out_count += 1;

                if (raw_src.size() > 0x0036) {
                    tags_out[out_count] = 0x0036;
                    vals_out[out_count] = make_u8(u8(raw_src[0x0036]));
                    out_count += 1;
                }
                if (raw_src.size() > 0x0037) {
                    tags_out[out_count] = 0x0037;
                    vals_out[out_count] = make_u8(u8(raw_src[0x0037]));
                    out_count += 1;
                }

                const uint16_t u8_tags[] = {
                    0x0039, 0x003b, 0x003d, 0x003f, 0x0041,
                    0x0043, 0x0045, 0x0047, 0x0048, 0x0049,
                };
                for (size_t k = 0; k < sizeof(u8_tags) / sizeof(u8_tags[0]);
                     ++k) {
                    const uint16_t t   = u8_tags[k];
                    const uint64_t off = t;
                    if (off >= raw_src.size()) {
                        continue;
                    }
                    if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                        break;
                    }
                    tags_out[out_count] = t;
                    vals_out[out_count] = make_u8(u8(raw_src[off]));
                    out_count += 1;
                }
            } else {
                if (raw_src.size() < 0x0018 + 20) {
                    continue;
                }
                std::array<std::byte, 20> desc_bytes;
                const std::span<const std::byte> desc
                    = nikon_copy_subspan(&desc_bytes, raw_src, 0x0004, 20);
                std::array<std::byte, 20> base_bytes;
                const std::span<const std::byte> base
                    = nikon_copy_subspan(&base_bytes, raw_src, 0x0018, 20);
                if (desc.empty() || base.empty()) {
                    continue;
                }
                tags_out[out_count] = 0x0004;
                vals_out[out_count] = make_fixed_ascii_text(store.arena(),
                                                            desc);
                out_count += 1;
                tags_out[out_count] = 0x0018;
                vals_out[out_count] = make_fixed_ascii_text(store.arena(),
                                                            base);
                out_count += 1;

                if (raw_src.size() > 0x0030) {
                    tags_out[out_count] = 0x0030;
                    vals_out[out_count] = make_u8(u8(raw_src[0x0030]));
                    out_count += 1;
                }
                if (raw_src.size() > 0x0031) {
                    tags_out[out_count] = 0x0031;
                    vals_out[out_count] = make_u8(u8(raw_src[0x0031]));
                    out_count += 1;
                }

                const uint16_t u8_tags[]
                    = { (subtable == "picturecontrol2") ? uint16_t(0x0033)
                                                        : uint16_t(0x0032),
                        (subtable == "picturecontrol2") ? uint16_t(0x0035)
                                                        : uint16_t(0x0033),
                        (subtable == "picturecontrol2") ? uint16_t(0x0037)
                                                        : uint16_t(0x0034),
                        (subtable == "picturecontrol2") ? uint16_t(0x0039)
                                                        : uint16_t(0x0035),
                        (subtable == "picturecontrol2") ? uint16_t(0x003b)
                                                        : uint16_t(0x0036),
                        (subtable == "picturecontrol2") ? uint16_t(0x003d)
                                                        : uint16_t(0x0037),
                        (subtable == "picturecontrol2") ? uint16_t(0x003f)
                                                        : uint16_t(0x0038),
                        (subtable == "picturecontrol2") ? uint16_t(0x0040)
                                                        : uint16_t(0x0039),
                        (subtable == "picturecontrol2") ? uint16_t(0x0041)
                                                        : uint16_t(0x0000) };

                const size_t tag_count = (subtable == "picturecontrol2") ? 9
                                                                         : 8;
                for (size_t k = 0; k < tag_count; ++k) {
                    const uint16_t t   = u8_tags[k];
                    const uint64_t off = t;
                    if (off >= raw_src.size()) {
                        continue;
                    }
                    if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                        break;
                    }
                    tags_out[out_count] = t;
                    vals_out[out_count] = make_u8(u8(raw_src[off]));
                    out_count += 1;
                }
            }

            decode_nikon_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count), options.limits,
                status_out);
            continue;
        }

        if (tag == 0x002B) {  // DistortInfo
            if (raw_src.size() < 5) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "distortinfo",
                                             idx_distortinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());

            const uint16_t tags_out[]  = { 0x0000, 0x0004 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(),
                                      std::span<const std::byte>(ver_bytes)),
                make_u8(u8(raw_src[4])),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x0024) {  // WorldTime
            if (raw_src.size() < 4) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "worldtime",
                                             idx_worldtime++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> raw;
            std::memcpy(raw.data(), raw_src.data(), raw.size());

            int16_t tz = 0;
            if (!read_i16_endian(le, raw, 0, &tz)) {
                continue;
            }
            const uint8_t dst = u8(raw[2]);
            const uint8_t fmt = u8(raw[3]);

            const uint16_t tags_out[]  = { 0x0000, 0x0002, 0x0003 };
            const MetaValue vals_out[] = { make_i16(tz), make_u8(dst),
                                           make_u8(fmt) };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x0025) {  // ISOInfo
            if (raw_src.size() < 12) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "isoinfo",
                                             idx_isoinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 14> raw;
            const size_t n = (raw_src.size() < raw.size()) ? raw_src.size()
                                                           : raw.size();
            std::memcpy(raw.data(), raw_src.data(), n);

            uint16_t iso_expansion  = 0;
            uint16_t iso_expansion2 = 0;
            if (!read_u16_endian(le, raw, 4, &iso_expansion)
                || !read_u16_endian(le, raw, 10, &iso_expansion2)) {
                continue;
            }
            const uint8_t iso  = u8(raw[0]);
            const uint8_t iso2 = u8(raw[6]);

            const uint16_t tags_out[]  = { 0x0000, 0x0004, 0x0006, 0x000A };
            const MetaValue vals_out[] = {
                make_u8(iso),
                make_u16(iso_expansion),
                make_u8(iso2),
                make_u16(iso_expansion2),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x00A8) {  // FlashInfo
            if (raw_src.size() < 4) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());
            const std::string_view ver = std::string_view(
                reinterpret_cast<const char*>(ver_bytes.data()),
                ver_bytes.size());

            enum NikonFlashInfoLayout {
                kNikonFlashInfo0100,
                kNikonFlashInfo0102,
                kNikonFlashInfo0103,
                kNikonFlashInfo0106,
                kNikonFlashInfo0107,
                kNikonFlashInfo0300,
                kNikonFlashInfoUnknown,
            };

            std::string_view subtable   = "flashinfo0100";
            NikonFlashInfoLayout layout = kNikonFlashInfo0100;
            if (ver == "0100" || ver == "0101") {
                subtable = "flashinfo0100";
            } else if (ver == "0102") {
                subtable = "flashinfo0102";
                layout   = kNikonFlashInfo0102;
            } else if (ver == "0103" || ver == "0104" || ver == "0105") {
                subtable = "flashinfo0103";
                layout   = kNikonFlashInfo0103;
            } else if (ver == "0106") {
                subtable = "flashinfo0106";
                layout   = kNikonFlashInfo0106;
            } else if (ver == "0107") {
                subtable = "flashinfo0107";
                layout   = kNikonFlashInfo0107;
            } else if (ver == "0108") {
                subtable = "flashinfo0107";
                layout   = kNikonFlashInfo0107;
            } else if (ver == "0300" || ver == "0301") {
                subtable = "flashinfo0300";
                layout   = kNikonFlashInfo0300;
            } else {
                subtable = "flashinfounknown";
                layout   = kNikonFlashInfoUnknown;
            }

            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                             idx_flashinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            uint16_t tags_out[24];
            MetaValue vals_out[24];
            uint8_t name_variants[24] = {};
            uint32_t out_count        = 0;

            tags_out[out_count] = 0x0000;
            vals_out[out_count]
                = make_fixed_ascii_text(store.arena(),
                                        std::span<const std::byte>(ver_bytes));
            out_count += 1;

            if (raw_src.size() > 4) {
                tags_out[out_count] = 0x0004;
                vals_out[out_count] = make_u8(u8(raw_src[4]));
                out_count += 1;
            }

            if (raw_src.size() >= 8) {
                std::array<uint8_t, 2> fw;
                fw[0]               = u8(raw_src[6]);
                fw[1]               = u8(raw_src[7]);
                tags_out[out_count] = 0x0006;
                vals_out[out_count]
                    = make_u8_array(store.arena(),
                                    std::span<const uint8_t>(fw));
                out_count += 1;
            }

            if ((layout == kNikonFlashInfo0100 || layout == kNikonFlashInfo0102
                 || layout == kNikonFlashInfo0103
                 || layout == kNikonFlashInfo0106
                 || layout == kNikonFlashInfo0300)
                && raw_src.size() > 8) {
                tags_out[out_count] = 0x0008;
                vals_out[out_count] = make_u8(u8(raw_src[8]));
                out_count += 1;
            }

            const uint8_t flash_control_mode
                = (raw_src.size() > 9U)
                      ? static_cast<uint8_t>(u8(raw_src[9U]) & 0x7FU)
                      : 0U;
            uint8_t flash_group_a_variant = 0U;
            uint8_t flash_group_b_variant = 0U;
            uint8_t flash_group_c_variant = 0U;
            uint8_t flash_group_a_mode    = 0U;
            uint8_t flash_group_b_mode    = 0U;
            uint8_t flash_group_c_mode    = 0U;
            if (layout == kNikonFlashInfo0102 && raw_src.size() > 0x11U) {
                flash_group_a_mode = static_cast<uint8_t>(u8(raw_src[0x10U])
                                                          & 0x0FU);
                flash_group_b_mode = static_cast<uint8_t>(
                    (u8(raw_src[0x11U]) >> 4U) & 0x0FU);
                flash_group_c_mode    = static_cast<uint8_t>(u8(raw_src[0x11U])
                                                             & 0x0FU);
                flash_group_a_variant = (flash_group_a_mode >= 0x06U) ? 0U : 2U;
                flash_group_b_variant = (flash_group_b_mode >= 0x06U) ? 0U : 3U;
                flash_group_c_variant = (flash_group_c_mode >= 0x06U) ? 0U : 4U;
            } else if ((layout == kNikonFlashInfo0103
                        || layout == kNikonFlashInfo0106
                        || layout == kNikonFlashInfo0107
                        || layout == kNikonFlashInfo0300)
                       && raw_src.size() > 0x12U) {
                flash_group_a_mode = static_cast<uint8_t>(u8(raw_src[0x11U])
                                                          & 0x0FU);
                flash_group_b_mode = static_cast<uint8_t>(
                    (u8(raw_src[0x12U]) >> 4U) & 0x0FU);
                flash_group_c_mode = static_cast<uint8_t>(u8(raw_src[0x12U])
                                                          & 0x0FU);
                if (layout == kNikonFlashInfo0103
                    || layout == kNikonFlashInfo0106) {
                    flash_group_a_variant = (flash_group_a_mode >= 0x06U) ? 0U
                                                                          : 2U;
                    flash_group_b_variant = (flash_group_b_mode >= 0x06U) ? 0U
                                                                          : 3U;
                    flash_group_c_variant = (flash_group_c_mode >= 0x06U) ? 0U
                                                                          : 4U;
                } else {
                    flash_group_a_variant = (flash_group_a_mode >= 0x06U) ? 2U
                                                                          : 1U;
                    flash_group_b_variant = (flash_group_b_mode >= 0x06U) ? 2U
                                                                          : 1U;
                    flash_group_c_variant = (flash_group_c_mode >= 0x06U) ? 2U
                                                                          : 1U;
                }
            }

            if (layout == kNikonFlashInfo0102 && raw_src.size() > 0x11U) {
                tags_out[out_count]      = 0x0010U;
                vals_out[out_count]      = make_u8(flash_group_a_mode);
                name_variants[out_count] = 5U;
                out_count += 1;

                tags_out[out_count]      = 0x0011U;
                vals_out[out_count]      = make_u8(flash_group_b_mode);
                name_variants[out_count] = 6U;
                out_count += 1;

                tags_out[out_count]      = 0x0011U;
                vals_out[out_count]      = make_u8(flash_group_c_mode);
                name_variants[out_count] = 7U;
                out_count += 1;
            } else if (layout == kNikonFlashInfo0103
                       && raw_src.size() > 0x12U) {
                tags_out[out_count]      = 0x0011U;
                vals_out[out_count]      = make_u8(flash_group_a_mode);
                name_variants[out_count] = 5U;
                out_count += 1;

                tags_out[out_count]      = 0x0012U;
                vals_out[out_count]      = make_u8(flash_group_b_mode);
                name_variants[out_count] = 6U;
                out_count += 1;

                tags_out[out_count]      = 0x0012U;
                vals_out[out_count]      = make_u8(flash_group_c_mode);
                name_variants[out_count] = 7U;
                out_count += 1;
            } else if ((layout == kNikonFlashInfo0107
                        || layout == kNikonFlashInfo0300)
                       && raw_src.size() > 0x12U) {
                tags_out[out_count]      = 0x0011U;
                vals_out[out_count]      = make_u8(flash_group_a_mode);
                name_variants[out_count] = 5U;
                out_count += 1;

                tags_out[out_count]      = 0x0012U;
                vals_out[out_count]      = make_u8(flash_group_b_mode);
                name_variants[out_count] = 6U;
                out_count += 1;

                tags_out[out_count]      = 0x0012U;
                vals_out[out_count]      = make_u8(flash_group_c_mode);
                name_variants[out_count] = 7U;
                out_count += 1;
            }

            const uint16_t* u8_tags                = nullptr;
            size_t u8_tag_count                    = 0;
            const uint16_t* i8_tags                = nullptr;
            size_t i8_tag_count                    = 0;
            EntryNameContextKind name_context_kind = EntryNameContextKind::None;
            static constexpr uint16_t kFlashInfo0100U8Tags[] = {
                0x0009, 0x000c, 0x000d, 0x000e, 0x000f, 0x0010,
            };
            static constexpr uint16_t kFlashInfo0100I8Tags[] = {
                0x000a, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015,
                0x001b, 0x001d, 0x0027, 0x0028, 0x0029, 0x002a,
            };
            static constexpr uint16_t kFlashInfo0102U8Tags[] = {
                0x000c,
                0x000d,
                0x000e,
                0x000f,
            };
            static constexpr uint16_t kFlashInfo0102I8Tags[] = {
                0x000a,
                0x0012,
                0x0013,
                0x0014,
            };
            static constexpr uint16_t kFlashInfo0103U8Tags[] = {
                0x000c, 0x000d, 0x000e, 0x000f, 0x0010,
            };
            static constexpr uint16_t kFlashInfo0103I8Tags[] = {
                0x000a, 0x0013, 0x0014, 0x0015, 0x001b, 0x001d, 0x0027,
            };
            static constexpr uint16_t kFlashInfo0106U8Tags[] = {
                0x0009, 0x000c, 0x000d, 0x000e, 0x000f, 0x0010,
            };
            static constexpr uint16_t kFlashInfo0106I8Tags[] = {
                0x0027,
                0x0028,
                0x0029,
                0x002a,
            };
            static constexpr uint16_t kFlashInfo0107U8Tags[] = {
                0x000c,
                0x000d,
                0x000e,
                0x000f,
            };
            static constexpr uint16_t kFlashInfo0107I8Tags[] = {
                0x000a,
                0x0028,
                0x0029,
                0x002a,
            };
            static constexpr uint16_t kFlashInfo0300U8Tags[] = {
                0x000d, 0x000e, 0x000f, 0x0010, 0x0025, 0x0026,
            };
            static constexpr uint16_t kFlashInfo0300I8Tags[] = {
                0x000a, 0x0021, 0x0028, 0x0029, 0x002a,
            };

            switch (layout) {
            case kNikonFlashInfo0102:
                u8_tags      = kFlashInfo0102U8Tags;
                u8_tag_count = sizeof(kFlashInfo0102U8Tags)
                               / sizeof(kFlashInfo0102U8Tags[0]);
                i8_tags      = kFlashInfo0102I8Tags;
                i8_tag_count = sizeof(kFlashInfo0102I8Tags)
                               / sizeof(kFlashInfo0102I8Tags[0]);
                name_context_kind = EntryNameContextKind::NikonFlashInfoLegacy;
                break;
            case kNikonFlashInfo0103:
                u8_tags      = kFlashInfo0103U8Tags;
                u8_tag_count = sizeof(kFlashInfo0103U8Tags)
                               / sizeof(kFlashInfo0103U8Tags[0]);
                i8_tags      = kFlashInfo0103I8Tags;
                i8_tag_count = sizeof(kFlashInfo0103I8Tags)
                               / sizeof(kFlashInfo0103I8Tags[0]);
                name_context_kind = EntryNameContextKind::NikonFlashInfoLegacy;
                break;
            case kNikonFlashInfo0106:
                u8_tags      = kFlashInfo0106U8Tags;
                u8_tag_count = sizeof(kFlashInfo0106U8Tags)
                               / sizeof(kFlashInfo0106U8Tags[0]);
                i8_tags      = kFlashInfo0106I8Tags;
                i8_tag_count = sizeof(kFlashInfo0106I8Tags)
                               / sizeof(kFlashInfo0106I8Tags[0]);
                name_context_kind = EntryNameContextKind::NikonFlashInfoLegacy;
                break;
            case kNikonFlashInfo0107:
                u8_tags      = kFlashInfo0107U8Tags;
                u8_tag_count = sizeof(kFlashInfo0107U8Tags)
                               / sizeof(kFlashInfo0107U8Tags[0]);
                i8_tags      = kFlashInfo0107I8Tags;
                i8_tag_count = sizeof(kFlashInfo0107I8Tags)
                               / sizeof(kFlashInfo0107I8Tags[0]);
                name_context_kind = EntryNameContextKind::NikonFlashInfoGroups;
                break;
            case kNikonFlashInfo0300:
                u8_tags      = kFlashInfo0300U8Tags;
                u8_tag_count = sizeof(kFlashInfo0300U8Tags)
                               / sizeof(kFlashInfo0300U8Tags[0]);
                i8_tags      = kFlashInfo0300I8Tags;
                i8_tag_count = sizeof(kFlashInfo0300I8Tags)
                               / sizeof(kFlashInfo0300I8Tags[0]);
                name_context_kind = EntryNameContextKind::NikonFlashInfoGroups;
                break;
            case kNikonFlashInfoUnknown: break;
            default:
                u8_tags      = kFlashInfo0100U8Tags;
                u8_tag_count = sizeof(kFlashInfo0100U8Tags)
                               / sizeof(kFlashInfo0100U8Tags[0]);
                i8_tags      = kFlashInfo0100I8Tags;
                i8_tag_count = sizeof(kFlashInfo0100I8Tags)
                               / sizeof(kFlashInfo0100I8Tags[0]);
                name_context_kind = EntryNameContextKind::NikonFlashInfoLegacy;
                break;
            }

            for (size_t k = 0; k < u8_tag_count; ++k) {
                if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                    break;
                }
                const uint16_t t    = u8_tags[k];
                const uint64_t off  = t;
                const uint64_t need = off + 1;
                if (need > raw_src.size()) {
                    continue;
                }
                tags_out[out_count] = t;
                vals_out[out_count] = make_u8(u8(raw_src[off]));
                out_count += 1;
            }

            for (size_t k = 0; k < i8_tag_count; ++k) {
                if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                    break;
                }
                const uint16_t t    = i8_tags[k];
                const uint64_t off  = t;
                const uint64_t need = off + 1;
                if (need > raw_src.size()) {
                    continue;
                }
                tags_out[out_count] = t;
                vals_out[out_count] = make_i8(nikon_to_i8(u8(raw_src[off])));
                if (name_context_kind
                    == EntryNameContextKind::NikonFlashInfoLegacy) {
                    if (layout == kNikonFlashInfo0106 && t == 0x0027U
                        && flash_control_mode < 0x06U) {
                        name_variants[out_count] = 1U;
                    } else if (layout == kNikonFlashInfo0100) {
                        if (t == 0x000AU) {
                            name_variants[out_count] = 8U;
                        } else if (t == 0x0011U) {
                            name_variants[out_count] = 2U;
                        } else if (t == 0x0012U) {
                            name_variants[out_count] = 3U;
                        }
                    } else if (layout == kNikonFlashInfo0102) {
                        if (t == 0x000AU) {
                            name_variants[out_count] = 8U;
                        } else if (t == 0x0012U) {
                            name_variants[out_count] = flash_group_a_variant;
                        } else if (t == 0x0013U) {
                            name_variants[out_count] = flash_group_b_variant;
                        } else if (t == 0x0014U) {
                            name_variants[out_count] = flash_group_c_variant;
                        }
                    } else if (layout == kNikonFlashInfo0103) {
                        if (t == 0x0013U) {
                            name_variants[out_count] = flash_group_a_variant;
                        } else if (t == 0x0014U) {
                            name_variants[out_count] = flash_group_b_variant;
                        } else if (t == 0x0015U) {
                            name_variants[out_count] = flash_group_c_variant;
                        }
                    } else if (layout == kNikonFlashInfo0106) {
                        if (t == 0x0028U) {
                            name_variants[out_count] = flash_group_a_variant;
                        } else if (t == 0x0029U) {
                            name_variants[out_count] = flash_group_b_variant;
                        } else if (t == 0x002AU) {
                            name_variants[out_count] = flash_group_c_variant;
                        }
                    }
                } else if (t == 0x0028U) {
                    name_variants[out_count] = flash_group_a_variant;
                } else if (t == 0x0029U) {
                    name_variants[out_count] = flash_group_b_variant;
                } else if (t == 0x002AU) {
                    name_variants[out_count] = flash_group_c_variant;
                }
                out_count += 1;
            }

            decode_nikon_bin_dir_entries_with_context(
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count),
                std::span<const uint8_t>(name_variants, out_count),
                name_context_kind, options.limits, status_out);
            continue;
        }

        if (tag == 0x00B0) {  // MultiExposure
            if (raw_src.size() < 16) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "multiexposure",
                                             idx_multiexposure++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());

            uint32_t mode  = 0;
            uint32_t shots = 0;
            uint32_t gain  = 0;
            if (!(le ? read_u32le(raw_src, 4, &mode)
                     : read_u32be(raw_src, 4, &mode))
                || !(le ? read_u32le(raw_src, 8, &shots)
                        : read_u32be(raw_src, 8, &shots))
                || !(le ? read_u32le(raw_src, 12, &gain)
                        : read_u32be(raw_src, 12, &gain))) {
                continue;
            }

            const uint16_t tags_out[]  = { 0x0000, 0x0001, 0x0002, 0x0003 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(),
                                      std::span<const std::byte>(ver_bytes)),
                make_u32(mode),
                make_u32(shots),
                make_u32(gain),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x002C) {  // UnknownInfo
            if (raw_src.size() < 4) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "unknowninfo",
                                             idx_unknowninfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            std::array<std::byte, 4> raw;
            std::memcpy(raw.data(), raw_src.data(), raw.size());
            const uint16_t tags_out[]  = { 0x0000 };
            const MetaValue vals_out[] = { make_fixed_ascii_text(
                store.arena(),
                std::span<const std::byte>(raw.data(), raw.size())) };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x0032) {  // UnknownInfo2
            if (raw_src.size() < 4) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "unknowninfo2",
                                             idx_unknowninfo2++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            std::array<std::byte, 4> raw;
            std::memcpy(raw.data(), raw_src.data(), raw.size());
            const uint16_t tags_out[]  = { 0x0000 };
            const MetaValue vals_out[] = { make_fixed_ascii_text(
                store.arena(),
                std::span<const std::byte>(raw.data(), raw.size())) };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x0035) {  // HDRInfo
            if (raw_src.size() < 8) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "hdrinfo",
                                             idx_hdrinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> version_bytes;
            const std::span<const std::byte> version
                = nikon_copy_prefix(&version_bytes, raw_src, 4);
            if (version.empty()) {
                continue;
            }

            const uint8_t v4 = u8(raw_src[4]);
            const uint8_t v5 = u8(raw_src[5]);
            const uint8_t v6 = u8(raw_src[6]);
            const uint8_t v7 = u8(raw_src[7]);

            const uint16_t tags_out[]  = { 0x0000, 0x0004, 0x0005, 0x0006,
                                           0x0007 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(), version),
                make_u8(v4),
                make_u8(v5),
                make_u8(v6),
                make_u8(v7),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x0039) {  // LocationInfo
            if (raw_src.size() < 9) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "locationinfo",
                                             idx_locationinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            const uint64_t loc_off = 9;
            const uint64_t loc_len = raw_src.size() - loc_off;
            const uint64_t max_loc = (loc_len < 70) ? loc_len : 70;

            std::array<std::byte, 4> version_bytes;
            const std::span<const std::byte> version
                = nikon_copy_prefix(&version_bytes, raw_src, 4);
            std::array<std::byte, 3> reserved_bytes;
            const std::span<const std::byte> reserved
                = nikon_copy_subspan(&reserved_bytes, raw_src, 5, 3);
            std::array<std::byte, 70> location_bytes;
            const std::span<const std::byte> location
                = nikon_copy_subspan(&location_bytes, raw_src, loc_off,
                                     static_cast<size_t>(max_loc));
            if (version.empty() || reserved.empty() || location.empty()) {
                continue;
            }

            const uint8_t location_source = u8(raw_src[4]);
            const uint8_t record_status   = u8(raw_src[8]);

            const uint16_t tags_out[]  = { 0x0000, 0x0004, 0x0005, 0x0008,
                                           0x0009 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(), version),
                make_u8(location_source),
                make_bytes(store.arena(), reserved),
                make_u8(record_status),
                make_bytes(store.arena(), location),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x0091) {  // ShotInfoUnknown
            if (raw_src.size() < 4) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());
            const std::string_view ver = std::string_view(
                reinterpret_cast<const char*>(ver_bytes.data()),
                ver_bytes.size());

            if (ver == "0805" && have_serial && have_shutter_count
                && raw_src.size() > 4) {
                static constexpr uint16_t kMenuSettingsZ9Tags[] = {
                    0x008c, 0x008e, 0x00bc, 0x00c0, 0x00e8, 0x00ec, 0x00f0,
                    0x00f4, 0x0112, 0x0114, 0x0134, 0x0142, 0x0144, 0x0146,
                    0x014e, 0x01a0, 0x01a2, 0x01a4, 0x01a8, 0x01aa, 0x01ae,
                    0x01b2, 0x01bc, 0x01c8, 0x0210, 0x0212, 0x0216, 0x0218,
                    0x021a, 0x022c, 0x023c, 0x023e, 0x0240, 0x025c, 0x0266,
                    0x0268, 0x026a, 0x026c, 0x02b8, 0x02ba, 0x02c0, 0x02c2,
                    0x02c4, 0x02c6, 0x02c8, 0x02cc, 0x02ce, 0x02d0, 0x02d2,
                    0x02d4, 0x02d6, 0x02ec, 0x02f2, 0x02f4, 0x02fa, 0x02fb,
                    0x02fc, 0x0592, 0x0594, 0x059a, 0x05b0, 0x0610, 0x061d,
                    0x0624, 0x0625, 0x0648, 0x0660, 0x0664, 0x066d,
                };

                constexpr uint32_t kMenuSettingsZ9MaxTag = 0x066d;
                constexpr uint32_t kMenuSettingsZ9BufSize
                    = kMenuSettingsZ9MaxTag + 1;

                std::array<std::byte, 8> fw1 {};
                std::array<std::byte, 8> fw2 {};
                std::array<std::byte, 8> fw3 {};
                uint8_t fw1_mask = 0;
                uint8_t fw2_mask = 0;
                uint8_t fw3_mask = 0;

                uint8_t num_off_bytes[4] = {};
                uint8_t num_off_mask     = 0;
                bool have_num_offsets    = false;
                uint32_t num_offsets     = 0;

                uint8_t seq_off_bytes[4] = {};
                uint8_t seq_off_mask     = 0;
                bool have_seq_off        = false;
                uint32_t seq_off         = 0;

                uint8_t orient_off_bytes[4] = {};
                uint8_t orient_off_mask     = 0;
                bool have_orient_off        = false;
                uint32_t orient_off         = 0;

                uint8_t menu_off_bytes[4] = {};
                uint8_t menu_off_mask     = 0;
                bool have_menu_off        = false;
                uint32_t menu_off         = 0;

                uint8_t menu_rel_bytes[4] = {};
                uint8_t menu_rel_mask     = 0;
                uint32_t menu_rel         = 0;

                bool have_seq_tags        = false;
                uint8_t focus_shift       = 0;
                uint8_t interval_shot[2]  = {};
                uint8_t interval_frame[2] = {};
                uint8_t seq_mask          = 0;

                bool have_orientation = false;
                std::array<uint8_t, 12> orient_bytes {};
                uint32_t orient_filled = 0;

                bool have_custom = false;
                std::array<uint8_t, 608> custom_bytes {};
                uint32_t custom_filled = 0;

                bool have_menu_settings = false;
                std::array<uint8_t, kMenuSettingsZ9BufSize>
                    menu_settings_bytes {};
                uint32_t menu_settings_filled = 0;
                uint64_t menu_settings_start  = 0;

                const uint8_t serial8 = static_cast<uint8_t>(serial_key
                                                             & 0xFFu);
                const uint8_t key     = static_cast<uint8_t>(
                    (shutter_count >> 0) ^ (shutter_count >> 8)
                    ^ (shutter_count >> 16) ^ (shutter_count >> 24));

                const uint8_t ci0 = kNikonDecryptXlat0[serial8];
                uint8_t cj        = kNikonDecryptXlat1[key];
                uint8_t ck        = 0x60u;

                const std::span<const std::byte> enc = raw_src.subspan(4);

                for (uint64_t i_enc = 0; i_enc < enc.size(); ++i_enc) {
                    const uint32_t prod = static_cast<uint32_t>(ci0)
                                          * static_cast<uint32_t>(ck);
                    cj = static_cast<uint8_t>((static_cast<uint32_t>(cj) + prod)
                                              & 0xFFu);
                    ck = static_cast<uint8_t>((static_cast<uint32_t>(ck) + 1U)
                                              & 0xFFu);

                    const uint8_t decb = static_cast<uint8_t>(
                        u8(enc[static_cast<size_t>(i_enc)]) ^ cj);
                    const uint64_t abs_off = 4ULL + i_enc;

                    if (abs_off >= 0x0004 && abs_off < 0x0004 + fw1.size()) {
                        const uint8_t bi = static_cast<uint8_t>(abs_off
                                                                - 0x0004);
                        fw1[bi]          = std::byte { decb };
                        fw1_mask |= static_cast<uint8_t>(1U << bi);
                    }
                    if (abs_off >= 0x000e && abs_off < 0x000e + fw2.size()) {
                        const uint8_t bi = static_cast<uint8_t>(abs_off
                                                                - 0x000e);
                        fw2[bi]          = std::byte { decb };
                        fw2_mask |= static_cast<uint8_t>(1U << bi);
                    }
                    if (abs_off >= 0x0018 && abs_off < 0x0018 + fw3.size()) {
                        const uint8_t bi = static_cast<uint8_t>(abs_off
                                                                - 0x0018);
                        fw3[bi]          = std::byte { decb };
                        fw3_mask |= static_cast<uint8_t>(1U << bi);
                    }

                    if (abs_off >= 0x0024 && abs_off < 0x0028) {
                        const uint8_t bi  = static_cast<uint8_t>(abs_off
                                                                 - 0x0024);
                        num_off_bytes[bi] = decb;
                        num_off_mask |= static_cast<uint8_t>(1U << bi);
                        if (num_off_mask == 0x0fU) {
                            num_offsets = nikon_u32le_from_bytes(num_off_bytes);
                            have_num_offsets = true;
                        }
                    }

                    if (abs_off >= 0x0030 && abs_off < 0x0034) {
                        const uint8_t bi  = static_cast<uint8_t>(abs_off
                                                                 - 0x0030);
                        seq_off_bytes[bi] = decb;
                        seq_off_mask |= static_cast<uint8_t>(1U << bi);
                        if (seq_off_mask == 0x0fU) {
                            seq_off = nikon_u32le_from_bytes(seq_off_bytes);
                            have_seq_off = true;
                        }
                    }

                    if (abs_off >= 0x0084 && abs_off < 0x0088) {
                        const uint8_t bi     = static_cast<uint8_t>(abs_off
                                                                    - 0x0084);
                        orient_off_bytes[bi] = decb;
                        orient_off_mask |= static_cast<uint8_t>(1U << bi);
                        if (orient_off_mask == 0x0fU) {
                            orient_off = nikon_u32le_from_bytes(
                                orient_off_bytes);
                            have_orient_off = true;
                        }
                    }

                    if (abs_off >= 0x008c && abs_off < 0x0090) {
                        const uint8_t bi   = static_cast<uint8_t>(abs_off
                                                                  - 0x008c);
                        menu_off_bytes[bi] = decb;
                        menu_off_mask |= static_cast<uint8_t>(1U << bi);
                        if (menu_off_mask == 0x0fU) {
                            menu_off = nikon_u32le_from_bytes(menu_off_bytes);
                            have_menu_off = true;
                        }
                    }

                    if (have_seq_off) {
                        const uint64_t base = uint64_t(seq_off);
                        if (abs_off == (base + 0x0020)) {
                            focus_shift = decb;
                            seq_mask |= 0x01U;
                        } else if (abs_off == (base + 0x0028)) {
                            interval_shot[0] = decb;
                            seq_mask |= 0x02U;
                        } else if (abs_off == (base + 0x0029)) {
                            interval_shot[1] = decb;
                            seq_mask |= 0x04U;
                        } else if (abs_off == (base + 0x002a)) {
                            interval_frame[0] = decb;
                            seq_mask |= 0x08U;
                        } else if (abs_off == (base + 0x002b)) {
                            interval_frame[1] = decb;
                            seq_mask |= 0x10U;
                        }
                        have_seq_tags = (seq_mask == 0x1fU);
                    }

                    if (have_orient_off) {
                        const uint64_t start = uint64_t(orient_off);
                        if (!have_orientation) {
                            if (start + orient_bytes.size() <= raw_src.size()) {
                                have_orientation = true;
                            }
                        }
                        if (have_orientation && abs_off >= start
                            && abs_off < (start + orient_bytes.size())) {
                            const uint64_t bi = abs_off - start;
                            orient_bytes[static_cast<size_t>(bi)] = decb;
                            orient_filled += 1;
                        }
                    }

                    if (have_menu_off) {
                        const uint64_t rel_off = uint64_t(menu_off) + 0x10ULL;
                        if (abs_off >= rel_off && abs_off < (rel_off + 4ULL)) {
                            const uint8_t bi   = static_cast<uint8_t>(abs_off
                                                                      - rel_off);
                            menu_rel_bytes[bi] = decb;
                            menu_rel_mask |= static_cast<uint8_t>(1U << bi);
                            if (menu_rel_mask == 0x0fU) {
                                menu_rel = nikon_u32le_from_bytes(
                                    menu_rel_bytes);
                                menu_settings_start = uint64_t(menu_off)
                                                      + uint64_t(menu_rel);
                                if (menu_settings_start
                                        + menu_settings_bytes.size()
                                    <= raw_src.size()) {
                                    have_menu_settings = true;
                                }
                            }
                        }

                        const uint64_t custom_start = uint64_t(menu_off)
                                                      + 799ULL;
                        if (custom_start + custom_bytes.size()
                            <= raw_src.size()) {
                            have_custom = true;
                            if (abs_off >= custom_start
                                && abs_off
                                       < (custom_start + custom_bytes.size())) {
                                const uint64_t bi = abs_off - custom_start;
                                custom_bytes[static_cast<size_t>(bi)] = decb;
                                custom_filled += 1;
                            }
                        }
                    }

                    if (have_menu_settings && abs_off >= menu_settings_start
                        && abs_off < (menu_settings_start
                                      + menu_settings_bytes.size())) {
                        const uint64_t bi = abs_off - menu_settings_start;
                        menu_settings_bytes[static_cast<size_t>(bi)] = decb;
                        menu_settings_filled += 1;
                    }
                }

                const std::string_view shot_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "shotinfoz9",
                                                 idx_shotinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (!shot_ifd.empty()) {
                    uint16_t tags_out[5];
                    MetaValue vals_out[5];
                    uint32_t out_count = 0;

                    tags_out[out_count] = 0x0000;
                    vals_out[out_count] = make_fixed_ascii_text(
                        store.arena(),
                        std::span<const std::byte>(ver_bytes.data(),
                                                   ver_bytes.size()));
                    out_count += 1;

                    if (fw1_mask == 0xffU) {
                        tags_out[out_count] = 0x0004;
                        vals_out[out_count] = make_fixed_ascii_text(
                            store.arena(),
                            std::span<const std::byte>(fw1.data(), fw1.size()));
                        out_count += 1;
                    }
                    if (fw2_mask == 0xffU) {
                        tags_out[out_count] = 0x000e;
                        vals_out[out_count] = make_fixed_ascii_text(
                            store.arena(),
                            std::span<const std::byte>(fw2.data(), fw2.size()));
                        out_count += 1;
                    }
                    if (fw3_mask == 0xffU) {
                        tags_out[out_count] = 0x0018;
                        vals_out[out_count] = make_fixed_ascii_text(
                            store.arena(),
                            std::span<const std::byte>(fw3.data(), fw3.size()));
                        out_count += 1;
                    }
                    if (have_num_offsets) {
                        tags_out[out_count] = 0x0024;
                        vals_out[out_count] = make_u32(num_offsets);
                        out_count += 1;
                    }

                    decode_nikon_bin_dir_entries(
                        shot_ifd, store,
                        std::span<const uint16_t>(tags_out, out_count),
                        std::span<const MetaValue>(vals_out, out_count),
                        options.limits, status_out);
                }

                if (have_seq_tags) {
                    char seq_buf[96];
                    const std::string_view seq_ifd
                        = make_mk_subtable_ifd_token(mk_prefix, "seqinfoz9",
                                                     idx_seqinfo++,
                                                     std::span<char>(seq_buf));
                    if (!seq_ifd.empty()) {
                        const uint16_t tags_out[]  = { 0x0020, 0x0028, 0x002a };
                        const MetaValue vals_out[] = {
                            make_u8(focus_shift),
                            make_u16(static_cast<uint16_t>(
                                uint16_t(interval_shot[0])
                                | (uint16_t(interval_shot[1]) << 8))),
                            make_u16(static_cast<uint16_t>(
                                uint16_t(interval_frame[0])
                                | (uint16_t(interval_frame[1]) << 8))),
                        };
                        decode_nikon_bin_dir_entries(
                            seq_ifd, store, std::span<const uint16_t>(tags_out),
                            std::span<const MetaValue>(vals_out),
                            options.limits, status_out);
                    }
                }

                if (have_orientation && orient_filled == orient_bytes.size()) {
                    char orient_buf[96];
                    const std::string_view orient_ifd
                        = make_mk_subtable_ifd_token(
                            mk_prefix, "orientationinfo", idx_orientationinfo++,
                            std::span<char>(orient_buf));
                    if (!orient_ifd.empty()) {
                        const uint32_t roll_raw
                            = nikon_u32le(orient_bytes[0], orient_bytes[1],
                                          orient_bytes[2], orient_bytes[3]);
                        const uint32_t pitch_raw
                            = nikon_u32le(orient_bytes[4], orient_bytes[5],
                                          orient_bytes[6], orient_bytes[7]);
                        const uint32_t yaw_raw
                            = nikon_u32le(orient_bytes[8], orient_bytes[9],
                                          orient_bytes[10], orient_bytes[11]);

                        const double roll  = double(roll_raw) / 65536.0;
                        const double pitch = double(pitch_raw) / 65536.0;
                        const double yaw   = double(yaw_raw) / 65536.0;

                        const uint16_t tags_out[]  = { 0x0000, 0x0004, 0x0008 };
                        const MetaValue vals_out[] = {
                            make_f64_bits(openmeta_f64_to_bits(roll)),
                            make_f64_bits(openmeta_f64_to_bits(pitch)),
                            make_f64_bits(openmeta_f64_to_bits(yaw)),
                        };
                        decode_nikon_bin_dir_entries(
                            orient_ifd, store,
                            std::span<const uint16_t>(tags_out),
                            std::span<const MetaValue>(vals_out),
                            options.limits, status_out);
                    }
                }

                if (have_menu_settings
                    && menu_settings_filled == menu_settings_bytes.size()) {
                    char menu_buf[96];
                    const std::string_view menu_ifd
                        = make_mk_subtable_ifd_token(mk_prefix,
                                                     "menusettingsz9",
                                                     idx_menusettings++,
                                                     std::span<char>(menu_buf));
                    if (!menu_ifd.empty()) {
                        uint16_t tags_out[sizeof(kMenuSettingsZ9Tags)
                                          / sizeof(kMenuSettingsZ9Tags[0])];
                        MetaValue vals_out[sizeof(kMenuSettingsZ9Tags)
                                           / sizeof(kMenuSettingsZ9Tags[0])];
                        uint32_t out_count = 0;

                        for (size_t k = 0;
                             k < sizeof(kMenuSettingsZ9Tags)
                                     / sizeof(kMenuSettingsZ9Tags[0]);
                             ++k) {
                            const uint16_t t = kMenuSettingsZ9Tags[k];
                            if (t >= menu_settings_bytes.size()) {
                                continue;
                            }
                            tags_out[out_count] = t;
                            vals_out[out_count] = make_u8(
                                menu_settings_bytes[t]);
                            out_count += 1;
                        }

                        decode_nikon_bin_dir_entries(
                            menu_ifd, store,
                            std::span<const uint16_t>(tags_out, out_count),
                            std::span<const MetaValue>(vals_out, out_count),
                            options.limits, status_out);
                    }
                }

                if (have_custom && custom_filled == custom_bytes.size()) {
                    char nikoncustom_ifd_buf[96];
                    const std::string_view nk_ifd = make_mk_subtable_ifd_token(
                        "mk_nikoncustom", "settingsz9", idx_nikoncustom++,
                        std::span<char>(nikoncustom_ifd_buf));
                    if (!nk_ifd.empty()) {
                        decode_nikoncustom_u8_table(
                            nk_ifd,
                            std::span<const uint8_t>(custom_bytes.data(),
                                                     custom_bytes.size()),
                            store, options, status_out);
                    }
                }
                continue;
            }

            if (ver == "0806" && have_serial && have_shutter_count
                && raw_src.size() > 0x8c) {
                static constexpr uint16_t kMenuSettingsZ8U8Tags[] = {
                    0x0048, 0x0098, 0x009a, 0x00d0, 0x00d2, 0x00f4, 0x00f8,
                    0x00fc, 0x0100, 0x011e, 0x0120, 0x0144, 0x0152, 0x0154,
                    0x0156, 0x01b0, 0x01b2, 0x01b4, 0x01b8, 0x0224, 0x0226,
                    0x022a, 0x022c, 0x022e, 0x023a, 0x0240, 0x0246, 0x024a,
                    0x0250, 0x0252, 0x0254, 0x026a, 0x026e, 0x027c, 0x02da,
                    0x02e4, 0x02e6, 0x02e8, 0x02ea, 0x0334, 0x0336, 0x033c,
                    0x033e, 0x0340, 0x0342, 0x0344, 0x0348, 0x034a, 0x034c,
                    0x034e, 0x0350, 0x0352, 0x0372, 0x0374, 0x0376, 0x0377,
                    0x0378, 0x0380,
                };
                static constexpr uint16_t kMenuSettingsZ8U32Tags[] = {
                    0x00b8, 0x00bc, 0x00c0, 0x00c8, 0x00cc,
                };
                static constexpr uint16_t kMenuSettingsZ8V1U8Tags[] = {
                    0x0692, 0x0694, 0x069a, 0x06b0, 0x0710,
                    0x0722, 0x0723, 0x0746, 0x0762, 0x076b,
                };
                static constexpr uint16_t kMenuSettingsZ8V1U16Tags[] = {
                    0x06b4, 0x06b6, 0x06b8, 0x06ba, 0x06bc, 0x06be, 0x06c0,
                    0x06c2, 0x06c4, 0x06c6, 0x06c8, 0x06ca, 0x06cc, 0x06ce,
                    0x06d0, 0x06d2, 0x06d4, 0x06d6, 0x06d8, 0x06da, 0x06dc,
                    0x06de, 0x06e0, 0x06e2, 0x06e4, 0x06e6, 0x06e8, 0x06ea,
                    0x06ec, 0x06ee, 0x06f0, 0x06f2, 0x06f4, 0x06f6, 0x06f8,
                    0x06fa, 0x06fc, 0x06fe, 0x0700, 0x0702,
                };
                if (raw_src.size() < 4U) {
                    continue;
                }

                std::vector<std::byte> dec;
                dec.resize(raw_src.size());
                std::memcpy(dec.data(), raw_src.data(), 4);

                const std::span<const std::byte> enc = raw_src.subspan(4);
                const std::span<std::byte> dec_out
                    = std::span<std::byte>(dec.data() + 4, dec.size() - 4);
                if (!nikon_decrypt(enc, serial_key, shutter_count, dec_out)) {
                    // Decryption failed; fall through to the generic ShotInfo
                    // block that preserves the version string.
                } else {
                    const std::span<const std::byte> dec_src
                        = std::span<const std::byte>(dec.data(), dec.size());
                    char shot_buf[96];
                    const std::string_view shot_ifd
                        = make_mk_subtable_ifd_token(mk_prefix, "shotinfoz8",
                                                     idx_shotinfo++,
                                                     std::span<char>(shot_buf));
                    if (!shot_ifd.empty()) {
                        uint16_t tags_out[8];
                        MetaValue vals_out[8];
                        uint32_t out_count = 0;

                        tags_out[out_count] = 0x0000;
                        vals_out[out_count] = make_fixed_ascii_text(
                            store.arena(),
                            std::span<const std::byte>(ver_bytes.data(),
                                                       ver_bytes.size()));
                        out_count += 1;

                        if (dec_src.size() >= 0x0004U + 8U) {
                            tags_out[out_count] = 0x0004;
                            vals_out[out_count] = make_fixed_ascii_text(
                                store.arena(), dec_src.subspan(0x0004U, 8U));
                            out_count += 1;
                        }
                        if (dec_src.size() >= 0x000eU + 8U) {
                            tags_out[out_count] = 0x000e;
                            vals_out[out_count] = make_fixed_ascii_text(
                                store.arena(), dec_src.subspan(0x000eU, 8U));
                            out_count += 1;
                        }
                        if (dec_src.size() >= 0x0018U + 8U) {
                            tags_out[out_count] = 0x0018;
                            vals_out[out_count] = make_fixed_ascii_text(
                                store.arena(), dec_src.subspan(0x0018U, 8U));
                            out_count += 1;
                        }

                        uint32_t num_offsets = 0;
                        if (read_u32le(dec_src, 0x0024U, &num_offsets)) {
                            tags_out[out_count] = 0x0024;
                            vals_out[out_count] = make_u32(num_offsets);
                            out_count += 1;
                        }

                        decode_nikon_bin_dir_entries(
                            shot_ifd, store,
                            std::span<const uint16_t>(tags_out, out_count),
                            std::span<const MetaValue>(vals_out, out_count),
                            options.limits, status_out);
                    }
                    uint32_t fw_major = 0;
                    (void)nikon_parse_fw_major(
                        std::string_view(reinterpret_cast<const char*>(
                                             dec_src.data() + 4),
                                         (dec_src.size() >= 12U) ? 8U : 0U),
                        &fw_major);

                    uint32_t seq_off = 0;
                    if (read_u32le(dec_src, 0x0030U, &seq_off)) {
                        const uint64_t seq_off64 = uint64_t(seq_off);
                        if (seq_off64 + 0x0021U <= dec_src.size()) {
                            char seq_buf[96];
                            const std::string_view seq_ifd
                                = make_mk_subtable_ifd_token(
                                    mk_prefix, "seqinfoz9", idx_seqinfo++,
                                    std::span<char>(seq_buf));
                            if (!seq_ifd.empty()) {
                                const uint16_t tags_out[] = { 0x0020U };
                                const MetaValue vals_out[]
                                    = { make_u8(u8(dec_src[static_cast<size_t>(
                                        seq_off64 + 0x0020U)])) };
                                decode_nikon_bin_dir_entries(
                                    seq_ifd, store,
                                    std::span<const uint16_t>(tags_out),
                                    std::span<const MetaValue>(vals_out),
                                    options.limits, status_out);
                            }
                        }
                    }

                    uint32_t menu_off = 0;
                    if (read_u32le(dec_src, 0x008c, &menu_off)) {
                        const uint64_t menu_off64 = uint64_t(menu_off);
                        uint32_t menu_rel         = 0;
                        if (menu_off64 + 0x0010 + 4U <= dec_src.size()
                            && read_u32le(dec_src, menu_off64 + 0x0010U,
                                          &menu_rel)) {
                            const uint64_t menu_settings_start
                                = menu_off64 + uint64_t(menu_rel);
                            const uint64_t common_len = 943U;
                            const uint64_t custom_len = (fw_major >= 2U) ? 755U
                                                                         : 730U;
                            const uint64_t custom_start = menu_settings_start
                                                          + common_len;

                            char menu_buf[96];
                            const std::string_view menu_ifd
                                = make_mk_subtable_ifd_token(mk_prefix,
                                                             "menusettingsz8",
                                                             idx_menusettings++,
                                                             std::span<char>(
                                                                 menu_buf));
                            if (!menu_ifd.empty()
                                && menu_settings_start + common_len
                                       <= dec_src.size()) {
                                uint16_t tags_out[96];
                                MetaValue vals_out[96];
                                uint32_t out_count = 0;

                                for (size_t k = 0;
                                     k < sizeof(kMenuSettingsZ8U8Tags)
                                             / sizeof(kMenuSettingsZ8U8Tags[0]);
                                     ++k) {
                                    const uint16_t t = kMenuSettingsZ8U8Tags[k];
                                    const uint64_t off = menu_settings_start
                                                         + uint64_t(t);
                                    if (off >= dec_src.size()) {
                                        continue;
                                    }
                                    tags_out[out_count] = t;
                                    vals_out[out_count] = make_u8(
                                        u8(dec_src[static_cast<size_t>(off)]));
                                    out_count += 1;
                                }

                                for (size_t k = 0;
                                     k
                                     < sizeof(kMenuSettingsZ8U32Tags)
                                           / sizeof(kMenuSettingsZ8U32Tags[0]);
                                     ++k) {
                                    const uint16_t t = kMenuSettingsZ8U32Tags[k];
                                    const uint64_t off = menu_settings_start
                                                         + uint64_t(t);
                                    uint32_t v32 = 0;
                                    if (off + 4U > dec_src.size()
                                        || !read_u32le(dec_src, off, &v32)) {
                                        continue;
                                    }
                                    tags_out[out_count] = t;
                                    vals_out[out_count] = make_u32(v32);
                                    out_count += 1;
                                }

                                {
                                    const uint64_t off = menu_settings_start
                                                         + 0x015eU;
                                    uint16_t v16 = 0;
                                    if (off + 2U <= dec_src.size()
                                        && read_u16le(dec_src, off, &v16)) {
                                        tags_out[out_count] = 0x015eU;
                                        vals_out[out_count] = make_i16(
                                            static_cast<int16_t>(v16));
                                        out_count += 1;
                                    }
                                }

                                decode_nikon_bin_dir_entries(
                                    menu_ifd, store,
                                    std::span<const uint16_t>(tags_out,
                                                              out_count),
                                    std::span<const MetaValue>(vals_out,
                                                               out_count),
                                    options.limits, status_out);
                            }

                            if (fw_major < 2U) {
                                char menu_v1_buf[96];
                                const std::string_view menu_v1_ifd
                                    = make_mk_subtable_ifd_token(
                                        mk_prefix, "menusettingsz8v1",
                                        idx_menusettings++,
                                        std::span<char>(menu_v1_buf));
                                if (!menu_v1_ifd.empty()
                                    && custom_start + custom_len
                                           <= dec_src.size()) {
                                    uint16_t tags_out[96];
                                    MetaValue vals_out[96];
                                    uint32_t out_count = 0;

                                    for (size_t k = 0;
                                         k
                                         < sizeof(kMenuSettingsZ8V1U8Tags)
                                               / sizeof(
                                                   kMenuSettingsZ8V1U8Tags[0]);
                                         ++k) {
                                        const uint16_t t
                                            = kMenuSettingsZ8V1U8Tags[k];
                                        const uint64_t off = menu_settings_start
                                                             + uint64_t(t);
                                        if (off >= dec_src.size()) {
                                            continue;
                                        }
                                        tags_out[out_count] = t;
                                        vals_out[out_count] = make_u8(u8(
                                            dec_src[static_cast<size_t>(off)]));
                                        out_count += 1;
                                    }

                                    for (size_t k = 0;
                                         k
                                         < sizeof(kMenuSettingsZ8V1U16Tags)
                                               / sizeof(
                                                   kMenuSettingsZ8V1U16Tags[0]);
                                         ++k) {
                                        const uint16_t t
                                            = kMenuSettingsZ8V1U16Tags[k];
                                        const uint64_t off = menu_settings_start
                                                             + uint64_t(t);
                                        uint16_t v16 = 0;
                                        if (off + 2U > dec_src.size()
                                            || !read_u16le(dec_src, off, &v16)) {
                                            continue;
                                        }
                                        tags_out[out_count] = t;
                                        vals_out[out_count] = make_u16(v16);
                                        out_count += 1;
                                    }

                                    decode_nikon_bin_dir_entries(
                                        menu_v1_ifd, store,
                                        std::span<const uint16_t>(tags_out,
                                                                  out_count),
                                        std::span<const MetaValue>(vals_out,
                                                                   out_count),
                                        options.limits, status_out);
                                }
                            }

                            if (custom_start + custom_len <= dec_src.size()) {
                                char nikoncustom_ifd_buf[96];
                                const std::string_view nk_ifd
                                    = make_mk_subtable_ifd_token(
                                        "mk_nikoncustom", "settingsz8",
                                        idx_nikoncustom++,
                                        std::span<char>(nikoncustom_ifd_buf));
                                if (!nk_ifd.empty()) {
                                    decode_nikoncustom_u8_table(
                                        nk_ifd,
                                        std::span<const uint8_t>(
                                            reinterpret_cast<const uint8_t*>(
                                                dec_src.data()
                                                + static_cast<size_t>(
                                                    custom_start)),
                                            static_cast<size_t>(custom_len)),
                                        store, options, status_out);
                                }
                            }
                        }
                    }
                    continue;
                }
            }

            if ((ver == "0800" || ver == "0801" || ver == "0802"
                 || ver == "0803" || ver == "0804" || ver == "0807")
                && have_serial && have_shutter_count && raw_src.size() > 4) {
                static constexpr uint16_t kMenuSettingsZ7IITags[] = {
                    0x005a, 0x005c, 0x00a0, 0x00a4, 0x00a8, 0x00b0, 0x00b4,
                    0x00b8, 0x00ba, 0x00dc, 0x00e0, 0x00e4, 0x00e8, 0x0142,
                    0x0143, 0x0146, 0x0148, 0x014e, 0x0152, 0x015a, 0x015c,
                    0x0160, 0x0162, 0x0166, 0x01f6, 0x01f8, 0x01fa, 0x01fe,
                    0x0204, 0x0238, 0x023c, 0x023e, 0x0240, 0x0241, 0x0242,
                    0x0248, 0x024e, 0x024f, 0x035a,
                };
                if (raw_src.size() < 4U) {
                    continue;
                }

                std::vector<std::byte> dec;
                dec.resize(raw_src.size());
                std::memcpy(dec.data(), raw_src.data(), 4);

                const std::span<const std::byte> enc = raw_src.subspan(4);
                const std::span<std::byte> dec_out
                    = std::span<std::byte>(dec.data() + 4, dec.size() - 4);
                if (!nikon_decrypt(enc, serial_key, shutter_count, dec_out)) {
                    // Decryption failed; fall through to the generic ShotInfo
                    // block that preserves the version string.
                } else {
                    const std::span<const std::byte> dec_src
                        = std::span<const std::byte>(dec.data(), dec.size());

                    char shot_buf[96];
                    const std::string_view shot_ifd
                        = make_mk_subtable_ifd_token(mk_prefix, "shotinfoz7ii",
                                                     idx_shotinfo++,
                                                     std::span<char>(shot_buf));
                    if (!shot_ifd.empty()) {
                        uint16_t tags_out[8];
                        MetaValue vals_out[8];
                        uint32_t out_count = 0;

                        tags_out[out_count] = 0x0000;
                        vals_out[out_count] = make_fixed_ascii_text(
                            store.arena(),
                            std::span<const std::byte>(ver_bytes.data(),
                                                       ver_bytes.size()));
                        out_count += 1;

                        if (dec_src.size() >= 0x0004 + 8) {
                            tags_out[out_count] = 0x0004;
                            vals_out[out_count] = make_fixed_ascii_text(
                                store.arena(), dec_src.subspan(0x0004, 8));
                            out_count += 1;
                        }
                        if (dec_src.size() >= 0x000e + 8) {
                            tags_out[out_count] = 0x000e;
                            vals_out[out_count] = make_fixed_ascii_text(
                                store.arena(), dec_src.subspan(0x000e, 8));
                            out_count += 1;
                        }
                        if (dec_src.size() >= 0x0018 + 8) {
                            tags_out[out_count] = 0x0018;
                            vals_out[out_count] = make_fixed_ascii_text(
                                store.arena(), dec_src.subspan(0x0018, 8));
                            out_count += 1;
                        }
                        uint32_t num_offsets = 0;
                        if (read_u32le(dec_src, 0x0024, &num_offsets)) {
                            tags_out[out_count] = 0x0024;
                            vals_out[out_count] = make_u32(num_offsets);
                            out_count += 1;
                        }

                        decode_nikon_bin_dir_entries(
                            shot_ifd, store,
                            std::span<const uint16_t>(tags_out, out_count),
                            std::span<const MetaValue>(vals_out, out_count),
                            options.limits, status_out);
                    }

                    uint32_t menu_off = 0;
                    if (read_u32le(dec_src, 0x00a0, &menu_off)) {
                        const uint64_t menu_off64 = uint64_t(menu_off);
                        uint32_t menu_rel         = 0;
                        if (menu_off64 + 0x0010 + 4 <= dec_src.size()
                            && read_u32le(dec_src, menu_off64 + 0x0010,
                                          &menu_rel)) {
                            const uint64_t ms_start = menu_off64
                                                      + uint64_t(menu_rel);

                            char menu_buf[96];
                            const std::string_view menu_ifd
                                = make_mk_subtable_ifd_token(mk_prefix,
                                                             "menusettingsz7ii",
                                                             idx_menusettings++,
                                                             std::span<char>(
                                                                 menu_buf));
                            if (!menu_ifd.empty()) {
                                uint16_t
                                    tags_out[sizeof(kMenuSettingsZ7IITags)
                                             / sizeof(kMenuSettingsZ7IITags[0])];
                                MetaValue
                                    vals_out[sizeof(kMenuSettingsZ7IITags)
                                             / sizeof(kMenuSettingsZ7IITags[0])];
                                uint32_t out_count = 0;

                                for (size_t k = 0;
                                     k < sizeof(kMenuSettingsZ7IITags)
                                             / sizeof(kMenuSettingsZ7IITags[0]);
                                     ++k) {
                                    const uint16_t t = kMenuSettingsZ7IITags[k];
                                    const uint64_t off = ms_start + uint64_t(t);

                                    if (t == 0x00a0 || t == 0x00a4
                                        || t == 0x00a8 || t == 0x00b0
                                        || t == 0x00b4) {
                                        uint32_t v32 = 0;
                                        if (off + 4 > dec_src.size()
                                            || !read_u32le(dec_src, off, &v32)) {
                                            continue;
                                        }
                                        tags_out[out_count] = t;
                                        vals_out[out_count] = make_u32(v32);
                                        out_count += 1;
                                        continue;
                                    }

                                    if (off >= dec_src.size()) {
                                        continue;
                                    }
                                    tags_out[out_count] = t;
                                    vals_out[out_count] = make_u8(
                                        u8(dec_src[static_cast<size_t>(off)]));
                                    out_count += 1;
                                }

                                decode_nikon_bin_dir_entries(
                                    menu_ifd, store,
                                    std::span<const uint16_t>(tags_out,
                                                              out_count),
                                    std::span<const MetaValue>(vals_out,
                                                               out_count),
                                    options.limits, status_out);
                            }
                        }
                    }
                    continue;
                }
            }

            std::string_view shotinfo_table = "shotinfo";
            if (ver == "0209" && model_has_d40) {
                shotinfo_table = "shotinfod40";
            } else if (ver == "0208" && model_has_d80) {
                shotinfo_table = "shotinfod80";
            } else if (ver == "0210") {
                if (model_has_d300) {
                    shotinfo_table = "shotinfod300a";
                } else if (model_has_d3) {
                    shotinfo_table = "shotinfod3a";
                }
            } else if (ver == "0213") {
                shotinfo_table = "shotinfod90";
            } else if (ver == "0214") {
                if (model_has_d3x) {
                    shotinfo_table = "shotinfod3x";
                } else if (model_has_d300) {
                    shotinfo_table = "shotinfod300b";
                } else if (model_has_d3) {
                    shotinfo_table = "shotinfod3b";
                }
            } else if (ver == "0215") {
                shotinfo_table = "shotinfod5000";
            } else if (ver == "0216") {
                shotinfo_table = "shotinfod300s";
            } else if (ver == "0218") {
                shotinfo_table = "shotinfod3s";
            } else if (ver == "0220") {
                shotinfo_table = "shotinfod7000";
            } else if (ver == "0221") {
                shotinfo_table = "shotinfod5100";
            } else if (ver == "0222") {
                shotinfo_table = "shotinfod800";
            } else if (ver == "0223") {
                shotinfo_table = "shotinfod4";
            } else if (ver == "0226") {
                shotinfo_table = "shotinfod5200";
            } else if (ver == "0231") {
                shotinfo_table = "shotinfod4s";
            } else if (ver == "0232") {
                shotinfo_table = "shotinfod610";
            } else if (ver == "0233") {
                shotinfo_table = "shotinfod810";
            } else if (ver == "0246") {
                shotinfo_table = "shotinfod6";
            } else if (ver == "0239") {
                shotinfo_table = "shotinfod500";
            } else if (ver == "0243") {
                shotinfo_table = "shotinfod850";
            }

            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, shotinfo_table,
                                             idx_shotinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (!ifd_name.empty()) {
                uint16_t tags_out[256];
                MetaValue vals_out[256];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(ver_bytes.data(),
                                               ver_bytes.size()));
                out_count += 1;

                uint32_t shotinfo_prefix_idx = 0;
                std::array<std::byte, 5> shotinfo_prefix_bytes;
                bool have_shotinfo_prefix = false;
                if (raw_src.size() >= 9) {
                    tags_out[out_count] = 0x0004;
                    shotinfo_prefix_idx = out_count;
                    vals_out[out_count] = MetaValue {};
                    std::memcpy(shotinfo_prefix_bytes.data(),
                                raw_src.data() + 4,
                                shotinfo_prefix_bytes.size());
                    have_shotinfo_prefix = true;
                    out_count += 1;
                }

                bool decoded_shotinfo_probe = false;

                if (have_serial && have_shutter_count && raw_src.size() > 4) {
                    constexpr uint64_t kProbeSize = 0x5200;

                    std::array<std::byte, kProbeSize> dec {};
                    std::memcpy(dec.data(), ver_bytes.data(), ver_bytes.size());

                    const uint64_t enc_len = (raw_src.size() > 4)
                                                 ? (raw_src.size() - 4)
                                                 : 0;
                    const uint64_t dec_len = (enc_len < (kProbeSize - 4))
                                                 ? enc_len
                                                 : (kProbeSize - 4);
                    const std::span<const std::byte> enc
                        = raw_src.subspan(4, static_cast<size_t>(dec_len));
                    const std::span<std::byte> out
                        = std::span<std::byte>(dec.data() + 4,
                                               static_cast<size_t>(dec_len));

                    if (nikon_decrypt(enc, serial_key, shutter_count, out)) {
                        const std::span<const std::byte> dec_src
                            = std::span<const std::byte>(
                                dec.data(), 4 + static_cast<size_t>(dec_len));

                        const uint16_t u8_tags[] = {
                            0x000b, 0x000c, 0x000d, 0x0012, 0x0014, 0x0015,
                            0x0017, 0x0018, 0x0019, 0x001a, 0x001b, 0x001c,
                            0x001d, 0x001e, 0x0020, 0x0021, 0x0024, 0x0026,
                            0x0028, 0x002c, 0x002d, 0x002e, 0x002f, 0x0031,
                            0x0032, 0x0033, 0x0034, 0x0036, 0x0038, 0x003a,
                            0x0046, 0x0048, 0x004a, 0x004b, 0x004c, 0x004e,
                            0x0050, 0x0051, 0x0052, 0x0053, 0x0056, 0x0057,
                            0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d,
                            0x005e, 0x005f, 0x0060, 0x0061, 0x0062, 0x0063,
                            0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069,
                            0x006a, 0x006b, 0x006e, 0x0072, 0x0075, 0x0076,
                            0x0082, 0x008e, 0x0093, 0x0103, 0x011a, 0x011b,
                            0x0128, 0x013c, 0x0159, 0x01a8, 0x01ac, 0x01ae,
                            0x01af, 0x01b0, 0x01b4, 0x01d0, 0x0201, 0x0202,
                            0x020e, 0x0213, 0x0214, 0x0221, 0x0228, 0x022c,
                            0x022e, 0x0234, 0x024e, 0x0256, 0x0257, 0x025c,
                            0x025d, 0x0265, 0x027a, 0x029f, 0x02b5, 0x02c4,
                            0x02ca, 0x02d3, 0x02e2, 0x02e3, 0x0458, 0x04c0,
                            0x04c2, 0x04c3, 0x04da, 0x04db, 0x051c, 0x0532,
                            0x06dd, 0x174c, 0x174d, 0x184d, 0x18ea, 0x18eb,
                            0x3693,
                        };
                        for (size_t k = 0;
                             k < sizeof(u8_tags) / sizeof(u8_tags[0]); ++k) {
                            if (out_count
                                >= sizeof(tags_out) / sizeof(tags_out[0])) {
                                break;
                            }
                            const uint16_t t   = u8_tags[k];
                            const uint64_t off = t;
                            if (off >= dec_src.size()) {
                                continue;
                            }
                            tags_out[out_count] = t;
                            vals_out[out_count] = make_u8(
                                u8(dec_src[static_cast<size_t>(off)]));
                            out_count += 1;
                        }

                        if (out_count
                            < sizeof(tags_out) / sizeof(tags_out[0])) {
                            const uint64_t off = 0x04d2;
                            if (off < dec_src.size()) {
                                tags_out[out_count] = 0x04d2;
                                vals_out[out_count] = make_i8(nikon_to_i8(
                                    u8(dec_src[static_cast<size_t>(off)])));
                                out_count += 1;
                            }
                        }

                        const uint16_t u16_tags[] = { 0x02d1 };
                        for (size_t k = 0;
                             k < sizeof(u16_tags) / sizeof(u16_tags[0]); ++k) {
                            if (out_count
                                >= sizeof(tags_out) / sizeof(tags_out[0])) {
                                break;
                            }
                            const uint16_t t   = u16_tags[k];
                            const uint64_t off = t;
                            uint16_t v16       = 0;
                            if (off + 2 > dec_src.size()
                                || !read_u16be(dec_src, off, &v16)) {
                                continue;
                            }
                            tags_out[out_count] = t;
                            vals_out[out_count] = make_u16(v16);
                            out_count += 1;
                        }

                        const uint16_t u32_tags[] = {
                            0x006a, 0x006e, 0x0157, 0x0242, 0x0246, 0x024a,
                            0x024d, 0x0276, 0x0279, 0x0280, 0x0286, 0x02d5,
                            0x02d6, 0x0320, 0x0321, 0x05fb, 0x0bd8,
                        };
                        for (size_t k = 0;
                             k < sizeof(u32_tags) / sizeof(u32_tags[0]); ++k) {
                            if (out_count
                                >= sizeof(tags_out) / sizeof(tags_out[0])) {
                                break;
                            }
                            const uint16_t t   = u32_tags[k];
                            const uint64_t off = t;
                            uint32_t v32       = 0;
                            if (off + 4 > dec_src.size()
                                || !read_u32be(dec_src, off, &v32)) {
                                continue;
                            }
                            tags_out[out_count] = t;
                            vals_out[out_count] = make_u32(v32);
                            out_count += 1;
                        }

                        if (ver == "0243") {
                            decode_nikon_shotinfo_d850_offsets(
                                mk_prefix, &idx_menusettings, &idx_moresettings,
                                dec_src, store, options, status_out);
                        } else if (ver == "0238" || ver == "0239") {
                            decode_nikon_shotinfo_d5d500_offsets(
                                mk_prefix, model_has_d500, &idx_rotationinfo,
                                &idx_jpginfo, &idx_bracketinginfo,
                                &idx_shootingmenu, &idx_otherinfo, dec_src,
                                store, options, status_out);
                        } else if (ver == "0246") {
                            decode_nikon_shotinfo_d6_offsets(
                                mk_prefix, &idx_seqinfo, &idx_intervalinfo,
                                dec_src, store, options, status_out);
                        }
                        decoded_shotinfo_probe = true;
                    }
                }

                if (!decoded_shotinfo_probe) {
                    const uint16_t fallback_u8_tags[] = {
                        0x0007, 0x0009, 0x000b, 0x000c, 0x000d, 0x000f, 0x0010,
                        0x0014, 0x0015, 0x0016, 0x0017, 0x0018, 0x0019, 0x001a,
                        0x001b, 0x001c, 0x001d, 0x001e, 0x0020, 0x0024, 0x0028,
                        0x006a, 0x006e, 0x0072, 0x0075, 0x0076, 0x0082, 0x0100,
                        0x0101, 0x0102, 0x0103, 0x011a, 0x011b, 0x0128, 0x0157,
                        0x0159, 0x01ae, 0x01af, 0x0201, 0x0202, 0x0213, 0x027d,
                        0x0302, 0x032e, 0x0405, 0x0458, 0x0459, 0x0483, 0x0505,
                        0x0b08, 0x0b0a, 0x2d2c, 0x3130, 0x3952, 0x5233, 0xff2c,
                    };
                    for (size_t k = 0; k < sizeof(fallback_u8_tags)
                                               / sizeof(fallback_u8_tags[0]);
                         ++k) {
                        if (out_count
                            >= sizeof(tags_out) / sizeof(tags_out[0])) {
                            break;
                        }
                        const uint16_t t   = fallback_u8_tags[k];
                        const uint64_t off = t;
                        if (off >= raw_src.size()) {
                            continue;
                        }
                        tags_out[out_count] = t;
                        vals_out[out_count] = make_u8(
                            u8(raw_src[static_cast<size_t>(off)]));
                        out_count += 1;
                    }
                }

                if (have_shotinfo_prefix) {
                    vals_out[shotinfo_prefix_idx]
                        = make_bytes(store.arena(), std::span<const std::byte>(
                                                        shotinfo_prefix_bytes));
                }

                if (shotinfo_table == "shotinfo" && model_is_zf) {
                    uint8_t variants_out[256];
                    for (uint32_t k = 0; k < out_count; ++k) {
                        variants_out[k] = (tags_out[k] == 0x0024U) ? 1U : 0U;
                    }
                    decode_nikon_bin_dir_entries_with_context(
                        ifd_name, store,
                        std::span<const uint16_t>(tags_out, out_count),
                        std::span<const MetaValue>(vals_out, out_count),
                        std::span<const uint8_t>(variants_out, out_count),
                        EntryNameContextKind::NikonShotInfoZ8, options.limits,
                        status_out);
                } else {
                    decode_nikon_bin_dir_entries(
                        ifd_name, store,
                        std::span<const uint16_t>(tags_out, out_count),
                        std::span<const MetaValue>(vals_out, out_count),
                        options.limits, status_out);
                }
            }

            // Extract NikonCustom settings blocks from encrypted ShotInfo.
            if (!have_serial || !have_shutter_count || raw_src.size() <= 4) {
                continue;
            }

            std::string_view settings_table;
            uint64_t settings_start = 0;
            uint32_t settings_len   = 0;
            bool need_dyn_u32_start = false;
            uint64_t dyn_u32_off    = 0;
            uint64_t dyn_u32_add    = 0;

            if (ver == "0208") {
                settings_table = "settingsd80";
                settings_start = 748;
                settings_len   = 17;
            } else if (ver == "0209") {
                settings_table = "settingsd40";
                settings_start = 729;
                settings_len   = 12;
            } else if (ver == "0213") {
                settings_table = "settingsd90";
                settings_start = 0x0374;
                settings_len   = 36;
            } else if (ver == "0210") {
                settings_table = "settingsd3";
                settings_len   = 24;
                if (model_has_d300) {
                    settings_start = 790;
                } else {
                    settings_start = 0x0301;
                }
            } else if (ver == "0215") {
                settings_table = "settingsd5000";
                settings_start = 0x0378;
                settings_len   = 34;
            } else if (ver == "0214") {
                settings_table = "settingsd3";
                settings_start = 0x030b;
                settings_len   = 24;
            } else if (ver == "0216") {
                settings_table = "settingsd3";
                settings_start = 804;
                settings_len   = 24;
            } else if (ver == "0218") {
                settings_table = "settingsd3";
                settings_start = 0x02ce;
                settings_len   = 27;
            } else if (ver == "0220") {
                settings_table = "settingsd7000";
                settings_start = 0x0404;
                settings_len   = 48;
            } else if (ver == "0221") {
                settings_table = "settingsd5100";
                settings_start = 0x0407;
                settings_len   = 34;
            } else if (ver == "0222") {
                settings_table = "settingsd800";
                settings_start = 0x06ec;
                settings_len   = 48;
            } else if (ver == "0223") {
                settings_table = "settingsd4";
                settings_start = 0x0751;
                settings_len   = 56;
            } else if (ver == "0226") {
                settings_table = "settingsd5200";
                settings_start = 0x0cd5;
                settings_len   = 34;
            } else if (ver == "0232") {
                settings_table = "settingsd610";
                settings_start = 0x07cf;
                settings_len   = 48;
            } else if (ver == "0231") {
                settings_table = "settingsd4";
                settings_start = 0x189d;
                settings_len   = 56;
            } else if (ver == "0233") {
                // D810: encrypted ShotInfo includes a u32 offset to the
                // CustomSettings block at 0x40.
                settings_table     = "settingsd810";
                settings_len       = 90;
                need_dyn_u32_start = true;
                dyn_u32_off        = 0x40;
                dyn_u32_add        = 0;
            } else if (ver == "0238") {
                // D5: encrypted ShotInfo includes a u32 offset to the
                // CustomSettings block at 0x58.
                settings_table     = "settingsd5";
                settings_len       = 90;
                need_dyn_u32_start = true;
                dyn_u32_off        = 0x58;
                dyn_u32_add        = 0;
            } else if (ver == "0239") {
                // D500: encrypted ShotInfo includes a u32 offset to the
                // CustomSettings block at 0x58.
                settings_table     = "settingsd500";
                settings_len       = 90;
                need_dyn_u32_start = true;
                dyn_u32_off        = 0x58;
                dyn_u32_add        = 0;
            } else if (ver == "0243") {
                // D850: encrypted ShotInfo includes a u32 offset to the
                // CustomSettings block at 0x58.
                settings_table     = "settingsd850";
                settings_len       = 90;
                need_dyn_u32_start = true;
                dyn_u32_off        = 0x58;
                dyn_u32_add        = 0;
            } else if (ver == "0805") {
                // Z9: CustomSettings base is derived from an encrypted u32
                // offset and an additional constant.
                settings_table     = "settingsz9";
                settings_len       = 608;
                need_dyn_u32_start = true;
                dyn_u32_off        = 0x8c;
                dyn_u32_add        = 799;
            }

            if (settings_table.empty() || settings_len == 0
                || settings_len > 608) {
                continue;
            }

            if (!need_dyn_u32_start) {
                if (settings_start + settings_len > raw_src.size()) {
                    // Best-effort fallback: D3 custom settings may be located at
                    // 0x30a for some firmware versions.
                    if (ver == "0210" && model_has_d3
                        && (0x030a + settings_len) <= raw_src.size()) {
                        settings_start = 0x030a;
                    } else {
                        continue;
                    }
                }
            }

            char nikoncustom_ifd_buf[96];
            const std::string_view nk_ifd = make_mk_subtable_ifd_token(
                "mk_nikoncustom", settings_table, idx_nikoncustom++,
                std::span<char>(nikoncustom_ifd_buf));
            if (nk_ifd.empty()) {
                continue;
            }

            std::array<uint8_t, 608> buf {};
            uint32_t filled = 0;

            uint8_t dyn_u32_bytes[4] = {};
            uint32_t dyn_u32_have    = 0;
            uint32_t dyn_u32         = 0;
            bool have_dyn_u32        = false;

            const uint8_t serial8 = static_cast<uint8_t>(serial_key & 0xFFu);
            const uint8_t key     = static_cast<uint8_t>(
                (shutter_count >> 0) ^ (shutter_count >> 8)
                ^ (shutter_count >> 16) ^ (shutter_count >> 24));

            const uint8_t ci0 = kNikonDecryptXlat0[serial8];
            uint8_t cj        = kNikonDecryptXlat1[key];
            uint8_t ck        = 0x60u;

            const std::span<const std::byte> enc = raw_src.subspan(4);

            uint64_t dyn_start     = settings_start;
            const uint64_t dyn_len = settings_len;
            bool have_dyn_range    = !need_dyn_u32_start;

            for (uint64_t i_enc = 0; i_enc < enc.size(); ++i_enc) {
                const uint32_t prod = static_cast<uint32_t>(ci0)
                                      * static_cast<uint32_t>(ck);
                cj = static_cast<uint8_t>((static_cast<uint32_t>(cj) + prod)
                                          & 0xFFu);
                ck = static_cast<uint8_t>((static_cast<uint32_t>(ck) + 1U)
                                          & 0xFFu);

                const uint8_t decb = static_cast<uint8_t>(
                    u8(enc[static_cast<size_t>(i_enc)]) ^ cj);
                const uint64_t abs_off = 4ULL + i_enc;

                if (need_dyn_u32_start && !have_dyn_u32
                    && abs_off >= dyn_u32_off && abs_off < (dyn_u32_off + 4)) {
                    const uint32_t bi = static_cast<uint32_t>(abs_off
                                                              - dyn_u32_off);
                    if (bi < 4) {
                        dyn_u32_bytes[bi] = decb;
                        dyn_u32_have |= (1U << bi);
                        if (dyn_u32_have == 0x0FU) {
                            dyn_u32 = static_cast<uint32_t>(dyn_u32_bytes[0])
                                      | (static_cast<uint32_t>(dyn_u32_bytes[1])
                                         << 8)
                                      | (static_cast<uint32_t>(dyn_u32_bytes[2])
                                         << 16)
                                      | (static_cast<uint32_t>(dyn_u32_bytes[3])
                                         << 24);
                            have_dyn_u32         = true;
                            const uint64_t start = uint64_t(dyn_u32)
                                                   + dyn_u32_add;
                            if (start + dyn_len <= raw_src.size()) {
                                dyn_start      = start;
                                have_dyn_range = true;
                                filled         = 0;
                            }
                        }
                    }
                }

                if (have_dyn_range && abs_off >= dyn_start
                    && abs_off < (dyn_start + dyn_len)) {
                    const uint64_t bi = abs_off - dyn_start;
                    if (bi < buf.size()) {
                        buf[static_cast<size_t>(bi)] = decb;
                        filled += 1;
                    }
                }

                if (have_dyn_range && filled == dyn_len) {
                    break;
                }
            }

            if (have_dyn_range && filled == dyn_len) {
                decode_nikoncustom_u8_table(
                    nk_ifd,
                    std::span<const uint8_t>(buf.data(),
                                             static_cast<size_t>(dyn_len)),
                    store, options, status_out);
            }
            continue;
        }

        if (tag == 0x0097) {  // ColorBalance*
            if (raw_src.size() < 4) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());
            const std::string_view ver = std::string_view(
                reinterpret_cast<const char*>(ver_bytes.data()),
                ver_bytes.size());

            if (ver == "0100") {
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "colorbalance1",
                                                 idx_colorbalance++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                uint16_t tags_out[4];
                MetaValue vals_out[4];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(ver_bytes.data(),
                                               ver_bytes.size()));
                out_count += 1;

                if (raw_src.size() >= (4ULL + 34ULL * 2ULL)) {
                    uint16_t v34[34] {};
                    bool ok = true;
                    for (uint32_t k = 0; k < 34U; ++k) {
                        if (!read_u16_endian(le, raw_src,
                                             4ULL + uint64_t(k) * 2ULL,
                                             &v34[k])) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        tags_out[out_count] = 0x0002;
                        vals_out[out_count]
                            = make_u16_array(store.arena(),
                                             std::span<const uint16_t>(v34));
                        out_count += 1;
                    }
                }

                if (raw_src.size() >= (0x24ULL + 4ULL * 2ULL)) {
                    uint16_t v4[4] {};
                    bool ok = true;
                    for (uint32_t k = 0; k < 4U; ++k) {
                        if (!read_u16_endian(le, raw_src,
                                             0x24ULL + uint64_t(k) * 2ULL,
                                             &v4[k])) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        tags_out[out_count] = 0x0024;
                        vals_out[out_count]
                            = make_u16_array(store.arena(),
                                             std::span<const uint16_t>(v4));
                        out_count += 1;
                    }
                }

                if (raw_src.size() >= (0x28ULL + 88ULL * 2ULL)) {
                    uint16_t v88[88] {};
                    bool ok = true;
                    for (uint32_t k = 0; k < 88U; ++k) {
                        if (!read_u16_endian(le, raw_src,
                                             0x28ULL + uint64_t(k) * 2ULL,
                                             &v88[k])) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        tags_out[out_count] = 0x0028;
                        vals_out[out_count]
                            = make_u16_array(store.arena(),
                                             std::span<const uint16_t>(v88));
                        out_count += 1;
                    }
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if (decode_nikon_colorbalancec_table(
                    mk_prefix, std::span<char>(sub_ifd_buf), &idx_colorbalance,
                    raw_src, 0ULL, le, store, options, status_out)) {
                continue;
            }

            // ColorBalance2/4 carry WB_*Levels (tag 0) and are typically
            // encrypted. Decrypt and decode those common tables.
            if ((ver == "0102" || ver == "0205" || ver == "0213"
                 || ver == "0219")
                && have_serial && have_shutter_count && raw_src.size() >= 12) {
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "colorbalance2",
                                                 idx_colorbalance++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 8> dec;
                const std::span<const std::byte> enc = raw_src.subspan(4, 8);
                if (nikon_decrypt(enc, serial_key, shutter_count,
                                  std::span<std::byte>(dec.data(),
                                                       dec.size()))) {
                    uint16_t levels[4];
                    for (uint32_t k = 0; k < 4; ++k) {
                        uint16_t v = 0;
                        (void)read_u16_endian(
                            le,
                            std::span<const std::byte>(dec.data(), dec.size()),
                            uint64_t(k) * 2ULL, &v);
                        levels[k] = v;
                    }
                    const uint16_t tags_out[] = { 0x0000 };
                    const MetaValue vals_out[]
                        = { make_u16_array(store.arena(),
                                           std::span<const uint16_t>(levels)) };
                    decode_nikon_bin_dir_entries(
                        ifd_name, store, std::span<const uint16_t>(tags_out),
                        std::span<const MetaValue>(vals_out), options.limits,
                        status_out);
                    continue;
                }
            }

            if ((ver == "0209" || ver == "0211" || ver == "0215"
                 || ver == "0217")
                && have_serial && have_shutter_count && raw_src.size() >= 12) {
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "colorbalance4",
                                                 idx_colorbalance++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 8> dec;
                const std::span<const std::byte> enc = raw_src.subspan(4, 8);
                if (nikon_decrypt(enc, serial_key, shutter_count,
                                  std::span<std::byte>(dec.data(),
                                                       dec.size()))) {
                    uint16_t levels[4];
                    for (uint32_t k = 0; k < 4; ++k) {
                        uint16_t v = 0;
                        (void)read_u16_endian(
                            le,
                            std::span<const std::byte>(dec.data(), dec.size()),
                            uint64_t(k) * 2ULL, &v);
                        levels[k] = v;
                    }
                    const uint16_t tags_out[] = { 0x0000 };
                    const MetaValue vals_out[]
                        = { make_u16_array(store.arena(),
                                           std::span<const uint16_t>(levels)) };
                    decode_nikon_bin_dir_entries(
                        ifd_name, store, std::span<const uint16_t>(tags_out),
                        std::span<const MetaValue>(vals_out), options.limits,
                        status_out);
                    continue;
                }
            }

            // Fallback: expose only the version string.
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "colorbalanceunknown2",
                                             idx_colorbalance++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            const uint16_t tags_out[]  = { 0x0000 };
            const MetaValue vals_out[] = { make_fixed_ascii_text(
                store.arena(), std::span<const std::byte>(ver_bytes.data(),
                                                          ver_bytes.size())) };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x0098) {  // LensData
            if (raw_src.size() < 4) {
                continue;
            }
            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());
            const std::string_view ver = std::string_view(
                reinterpret_cast<const char*>(ver_bytes.data()),
                ver_bytes.size());

            std::string_view subtable = "lensdataunknown";
            uint16_t lens_model_tag   = 0;
            uint64_t lens_model_off   = 0;
            uint64_t lens_model_bytes = 0;
            if (ver == "0100") {
                subtable = "lensdata0100";
            } else if (ver == "0101") {
                subtable = "lensdata0101";
            } else if (ver == "0201" || ver == "0202" || ver == "0203") {
                subtable = "lensdata0201";
            } else if (ver == "0204") {
                subtable = "lensdata0204";
            } else if (ver == "0400") {
                subtable         = "lensdata0400";
                lens_model_tag   = 0x018a;
                lens_model_off   = 0x018a;
                lens_model_bytes = 64;
            } else if (ver == "0401") {
                subtable         = "lensdata0400";
                lens_model_tag   = 0x018a;
                lens_model_off   = 0x018a;
                lens_model_bytes = 64;
            } else if (ver == "0402") {
                subtable         = "lensdata0402";
                lens_model_tag   = 0x018b;
                lens_model_off   = 0x018b;
                lens_model_bytes = 64;
            } else if (ver == "0403") {
                subtable         = "lensdata0403";
                lens_model_tag   = 0x02ac;
                lens_model_off   = 0x02ac;
                lens_model_bytes = 64;
            } else if (ver == "0800" || ver == "0801" || ver == "0802") {
                subtable = "lensdata0800";
            }

            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                             idx_lensdata++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            if (subtable == "lensdata0100" && raw_src.size() >= 13) {
                uint16_t tags_out[8];
                MetaValue vals_out[8];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(ver_bytes.data(),
                                               ver_bytes.size()));
                out_count += 1;

                const uint16_t want[] = { 0x0006, 0x0007, 0x0008, 0x0009,
                                          0x000a, 0x000b, 0x000c };
                for (uint32_t wi = 0; wi < sizeof(want) / sizeof(want[0]);
                     ++wi) {
                    const uint16_t t = want[wi];
                    if (static_cast<size_t>(t) >= raw_src.size()) {
                        continue;
                    }
                    tags_out[out_count] = t;
                    vals_out[out_count] = make_u8(
                        u8(raw_src[static_cast<size_t>(t)]));
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if (subtable == "lensdata0101" && raw_src.size() > 0x0012) {
                uint16_t tags_out[14];
                MetaValue vals_out[14];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(ver_bytes.data(),
                                               ver_bytes.size()));
                out_count += 1;

                const uint16_t want[]
                    = { 0x0004, 0x0005, 0x0008, 0x0009, 0x000a, 0x000b, 0x000c,
                        0x000d, 0x000e, 0x000f, 0x0010, 0x0011, 0x0012 };
                for (uint32_t wi = 0; wi < sizeof(want) / sizeof(want[0]);
                     ++wi) {
                    const uint16_t t = want[wi];
                    if (static_cast<size_t>(t) >= raw_src.size()) {
                        continue;
                    }
                    tags_out[out_count] = t;
                    vals_out[out_count] = make_u8(
                        u8(raw_src[static_cast<size_t>(t)]));
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if ((subtable == "lensdata0201" || subtable == "lensdata0204")
                && have_serial && have_shutter_count && raw_src.size() >= 20) {
                std::array<std::byte, 16> dec;
                const std::span<const std::byte> enc = raw_src.subspan(4, 16);
                if (nikon_decrypt(enc, serial_key, shutter_count,
                                  std::span<std::byte>(dec.data(),
                                                       dec.size()))) {
                    uint16_t tags_out[16];
                    MetaValue vals_out[16];
                    uint32_t out_count = 0;

                    tags_out[out_count] = 0x0000;
                    vals_out[out_count] = make_fixed_ascii_text(
                        store.arena(),
                        std::span<const std::byte>(ver_bytes.data(),
                                                   ver_bytes.size()));
                    out_count += 1;

                    const uint16_t want[] = { 0x0004, 0x0005, 0x0008, 0x000a,
                                              0x000b, 0x000c, 0x000d, 0x000e,
                                              0x000f, 0x0010, 0x0011, 0x0012,
                                              0x0013 };
                    for (uint32_t wi = 0; wi < sizeof(want) / sizeof(want[0]);
                         ++wi) {
                        const uint16_t t = want[wi];
                        if (t < 4 || t >= 20) {
                            continue;
                        }
                        if (out_count
                            >= sizeof(tags_out) / sizeof(tags_out[0])) {
                            break;
                        }
                        tags_out[out_count] = t;
                        vals_out[out_count] = make_u8(
                            u8(dec[static_cast<size_t>(t - 4)]));
                        out_count += 1;
                    }

                    decode_nikon_bin_dir_entries(
                        ifd_name, store,
                        std::span<const uint16_t>(tags_out, out_count),
                        std::span<const MetaValue>(vals_out, out_count),
                        options.limits, status_out);
                    continue;
                }
            }

            if (subtable == "lensdata0800" && have_serial && have_shutter_count
                && raw_src.size() > 0x005f) {
                constexpr uint64_t kLensData0800MaxOff = 0x006b;
                const uint64_t avail_max_off           = (raw_src.size() - 1ULL
                                                < kLensData0800MaxOff)
                                                             ? (raw_src.size() - 1ULL)
                                                             : kLensData0800MaxOff;

                const uint32_t serial8 = static_cast<uint32_t>(serial_key
                                                               & 0xFFu);
                const uint8_t key      = static_cast<uint8_t>(
                    (shutter_count >> 0) ^ (shutter_count >> 8)
                    ^ (shutter_count >> 16) ^ (shutter_count >> 24));

                const uint8_t ci0 = kNikonDecryptXlat0[serial8];
                uint8_t cj        = kNikonDecryptXlat1[key];
                uint8_t ck        = 0x60u;

                const std::span<const std::byte> enc = raw_src.subspan(4);
                const uint64_t max_i                 = (avail_max_off >= 4)
                                                           ? (avail_max_off - 4)
                                                           : 0;

                std::array<uint8_t, kLensData0800MaxOff + 1> dec_bytes {};

                for (uint64_t i_enc = 0; i_enc < enc.size() && i_enc <= max_i;
                     ++i_enc) {
                    const uint32_t prod = static_cast<uint32_t>(ci0)
                                          * static_cast<uint32_t>(ck);
                    cj = static_cast<uint8_t>((static_cast<uint32_t>(cj) + prod)
                                              & 0xFFu);
                    ck = static_cast<uint8_t>((static_cast<uint32_t>(ck) + 1U)
                                              & 0xFFu);

                    const uint8_t decb = static_cast<uint8_t>(
                        u8(enc[static_cast<size_t>(i_enc)]) ^ cj);

                    const uint64_t off = 4ULL + i_enc;
                    if (off <= avail_max_off) {
                        dec_bytes[static_cast<size_t>(off)] = decb;
                    }
                }

                uint16_t tags_out[192];
                MetaValue vals_out[192];
                uint32_t out_count = 0;
                std::array<uint8_t, kLensData0800MaxOff + 1> emitted {};

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(ver_bytes.data(),
                                               ver_bytes.size()));
                out_count += 1;

                const bool lensdata_legacy_layout
                    = (ver == "0800") && dec_bytes[0x0030] == 0
                      && dec_bytes[0x0031] == 0 && dec_bytes[0x0036] == 0
                      && dec_bytes[0x0037] == 0 && dec_bytes[0x0038] == 0
                      && dec_bytes[0x0039] == 0 && dec_bytes[0x003c] == 0
                      && dec_bytes[0x003d] == 0;

                if (lensdata_legacy_layout) {
                    const uint16_t u8_tags[] = { 0x0004, 0x0005, 0x000b, 0x000c,
                                                 0x000d, 0x000e, 0x000f, 0x0010,
                                                 0x0011, 0x0012, 0x0013, 0x0014,
                                                 0x0035 };
                    for (size_t k = 0; k < sizeof(u8_tags) / sizeof(u8_tags[0]);
                         ++k) {
                        const uint16_t t   = u8_tags[k];
                        const uint64_t off = t;
                        if (off > avail_max_off) {
                            continue;
                        }
                        if (out_count
                            >= sizeof(tags_out) / sizeof(tags_out[0])) {
                            break;
                        }
                        tags_out[out_count] = t;
                        vals_out[out_count] = make_u8(
                            dec_bytes[static_cast<size_t>(off)]);
                        emitted[static_cast<size_t>(t)] = 1;
                        out_count += 1;
                    }

                    // NewLensData (17 bytes) at 0x002f when present.
                    if (0x002f + 17 <= (avail_max_off + 1)
                        && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                        std::array<std::byte, 17> blob {};
                        for (size_t k = 0; k < blob.size(); ++k) {
                            blob[k] = std::byte {
                                dec_bytes[static_cast<size_t>(0x002f + k)]
                            };
                        }
                        tags_out[out_count] = 0x002f;
                        vals_out[out_count]
                            = make_bytes(store.arena(),
                                         std::span<const std::byte>(blob));
                        emitted[0x002f] = 1;
                        out_count += 1;
                    }
                } else {
                    const uint16_t lens_id = static_cast<uint16_t>(
                        uint16_t(dec_bytes[0x0030])
                        | (uint16_t(dec_bytes[0x0031]) << 8));
                    tags_out[out_count] = 0x0030;
                    vals_out[out_count] = make_u16(lens_id);
                    emitted[0x0030]     = 1;
                    out_count += 1;

                    tags_out[out_count] = 0x0035;
                    vals_out[out_count] = make_u8(dec_bytes[0x0035]);
                    emitted[0x0035]     = 1;
                    out_count += 1;

                    tags_out[out_count] = 0x0036;
                    vals_out[out_count] = make_u16(static_cast<uint16_t>(
                        uint16_t(dec_bytes[0x0036])
                        | (uint16_t(dec_bytes[0x0037]) << 8)));
                    emitted[0x0036]     = 1;
                    out_count += 1;

                    tags_out[out_count] = 0x0038;
                    vals_out[out_count] = make_u16(static_cast<uint16_t>(
                        uint16_t(dec_bytes[0x0038])
                        | (uint16_t(dec_bytes[0x0039]) << 8)));
                    emitted[0x0038]     = 1;
                    out_count += 1;

                    tags_out[out_count] = 0x003c;
                    vals_out[out_count] = make_u16(static_cast<uint16_t>(
                        uint16_t(dec_bytes[0x003c])
                        | (uint16_t(dec_bytes[0x003d]) << 8)));
                    emitted[0x003c]     = 1;
                    out_count += 1;

                    tags_out[out_count] = 0x004c;
                    vals_out[out_count] = make_u8(dec_bytes[0x004c]);
                    emitted[0x004c]     = 1;
                    out_count += 1;

                    tags_out[out_count] = 0x004e;
                    vals_out[out_count] = make_u16(static_cast<uint16_t>(
                        uint16_t(dec_bytes[0x004e])
                        | (uint16_t(dec_bytes[0x004f]) << 8)));
                    emitted[0x004e]     = 1;
                    out_count += 1;

                    tags_out[out_count] = 0x0056;
                    vals_out[out_count] = make_u8(dec_bytes[0x0056]);
                    emitted[0x0056]     = 1;
                    out_count += 1;

                    tags_out[out_count] = 0x0058;
                    vals_out[out_count] = make_u8(dec_bytes[0x0058]);
                    emitted[0x0058]     = 1;
                    out_count += 1;

                    const uint32_t lp_u32
                        = static_cast<uint32_t>(dec_bytes[0x005a])
                          | (static_cast<uint32_t>(dec_bytes[0x005b]) << 8)
                          | (static_cast<uint32_t>(dec_bytes[0x005c]) << 16)
                          | (static_cast<uint32_t>(dec_bytes[0x005d]) << 24);
                    const int32_t lp_i32
                        = (lp_u32 <= static_cast<uint32_t>(INT32_MAX))
                              ? static_cast<int32_t>(lp_u32)
                              : static_cast<int32_t>(
                                    static_cast<int64_t>(lp_u32)
                                    - 0x100000000LL);
                    tags_out[out_count] = 0x005a;
                    vals_out[out_count] = make_i32(lp_i32);
                    emitted[0x005a]     = 1;
                    out_count += 1;
                }

                if (lensdata_legacy_layout) {
                    for (uint16_t t = 0x0004;
                         t <= static_cast<uint16_t>(avail_max_off); ++t) {
                        if (emitted[static_cast<size_t>(t)] != 0) {
                            continue;
                        }
                        if (out_count
                            >= sizeof(tags_out) / sizeof(tags_out[0])) {
                            break;
                        }
                        tags_out[out_count] = t;
                        vals_out[out_count] = make_u8(
                            dec_bytes[static_cast<size_t>(t)]);
                        out_count += 1;
                    }
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            {
                uint16_t tags_out[2];
                MetaValue vals_out[2];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(ver_bytes.data(),
                                               ver_bytes.size()));
                out_count += 1;

                if (lens_model_tag != 0 && lens_model_off < raw_src.size()
                    && lens_model_off + lens_model_bytes <= raw_src.size()) {
                    std::array<std::byte, 64> model_bytes;
                    std::memcpy(model_bytes.data(),
                                raw_src.data()
                                    + static_cast<size_t>(lens_model_off),
                                model_bytes.size());
                    tags_out[out_count] = lens_model_tag;
                    vals_out[out_count] = make_fixed_ascii_text(
                        store.arena(),
                        std::span<const std::byte>(model_bytes.data(),
                                                   model_bytes.size()));
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
            }
            continue;
        }

        if (tag == 0x004E) {  // NikonSettings
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token("mk_nikonsettings", "main",
                                             idx_settings++,
                                             std::span<char>(sub_ifd_buf));
            decode_nikon_settings_dir(ifd_name, raw_src, store, options,
                                      status_out);
            continue;
        }

        if (tag == 0x0088) {  // AFInfo (older models)
            if (raw_src.size() < 3) {
                continue;
            }

            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "afinfo", idx_afinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            const uint16_t tags_out[]  = { 0x0000, 0x0001, 0x0002 };
            const MetaValue vals_out[] = { make_u8(u8(raw_src[0])),
                                           make_u8(u8(raw_src[1])),
                                           make_u8(u8(raw_src[2])) };

            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x00B7) {  // AFInfo2
            if (raw_src.size() < 9) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());
            const std::string_view ver = std::string_view(
                reinterpret_cast<const char*>(ver_bytes.data()),
                ver_bytes.size());

            std::string_view subtable = "afinfo2v0100";
            if (ver == "0100") {
                subtable = "afinfo2v0100";
            } else if (ver == "0101") {
                subtable = "afinfo2v0101";
            } else if (ver == "0200") {
                subtable = "afinfo2v0200";
            } else if (ver == "0300" || ver == "0301") {
                subtable = "afinfo2v0300";
            } else if (ver == "0400") {
                subtable = "afinfo2v0400";
            }

            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, subtable, idx_afinfo2++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            if (ver == "0400") {
                uint16_t tags_out[16];
                MetaValue vals_out[16];
                uint32_t out_count = 0;

                uint32_t af_points_idx = 0;
                std::array<std::byte, 5> af_points_bytes;
                bool have_af_points = false;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(), std::span<const std::byte>(ver_bytes));
                out_count += 1;

                const uint16_t u8_tags[] = { 0x0004, 0x0005, 0x0007,
                                             0x0043, 0x0045, 0x004a };
                for (size_t k = 0; k < sizeof(u8_tags) / sizeof(u8_tags[0]);
                     ++k) {
                    const uint16_t t    = u8_tags[k];
                    const uint64_t off  = t;
                    const uint64_t need = off + 1;
                    if (need > raw_src.size()) {
                        continue;
                    }
                    if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                        break;
                    }
                    tags_out[out_count] = t;
                    vals_out[out_count] = make_u8(u8(raw_src[off]));
                    out_count += 1;
                }

                // AFPointsUsed (variable length); store a short raw prefix when present.
                if (raw_src.size() >= 0x000a + 5
                    && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                    tags_out[out_count] = 0x000a;
                    af_points_idx       = out_count;
                    vals_out[out_count] = MetaValue {};
                    std::memcpy(af_points_bytes.data(), raw_src.data() + 0x000a,
                                af_points_bytes.size());
                    have_af_points = true;
                    out_count += 1;
                }

                const uint16_t u16_tags[] = { 0x003e, 0x0040, 0x0042,
                                              0x0044, 0x0046, 0x0048 };
                for (size_t k = 0; k < sizeof(u16_tags) / sizeof(u16_tags[0]);
                     ++k) {
                    const uint16_t t    = u16_tags[k];
                    const uint64_t off  = t;
                    const uint64_t need = off + 2;
                    if (need > raw_src.size()) {
                        continue;
                    }
                    if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                        break;
                    }
                    uint16_t v16 = 0;
                    if (!read_u16_endian(le, raw_src, off, &v16)) {
                        continue;
                    }
                    tags_out[out_count] = t;
                    vals_out[out_count] = make_u16(v16);
                    out_count += 1;
                }

                if (raw_src.size() > 0x0052
                    && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                    tags_out[out_count] = 0x0052;
                    vals_out[out_count] = make_u8(u8(raw_src[0x0052]));
                    out_count += 1;
                }

                if (have_af_points) {
                    vals_out[af_points_idx]
                        = make_bytes(store.arena(), std::span<const std::byte>(
                                                        af_points_bytes));
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if (ver == "0101") {
                uint16_t tags_out[16];
                MetaValue vals_out[16];
                uint32_t out_count = 0;

                uint8_t schema   = 0;
                bool have_schema = false;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(), std::span<const std::byte>(ver_bytes));
                out_count += 1;

                const uint16_t u8_tags[] = { 0x0004, 0x0005, 0x0006,
                                             0x001c, 0x0044, 0x0052 };
                for (size_t k = 0; k < sizeof(u8_tags) / sizeof(u8_tags[0]);
                     ++k) {
                    const uint16_t t    = u8_tags[k];
                    const uint64_t off  = t;
                    const uint64_t need = off + 1;
                    if (need > raw_src.size()) {
                        continue;
                    }
                    if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                        break;
                    }
                    const uint8_t v = u8(raw_src[off]);
                    if (t == 0x0006) {
                        schema      = v;
                        have_schema = true;
                    }
                    tags_out[out_count] = t;
                    vals_out[out_count] = make_u8(v);
                    out_count += 1;
                }

                const uint64_t points_len
                    = have_schema ? nikon_afinfo2v0101_points_len(schema)
                                  : 0ULL;
                if (points_len > 0 && 0x0008 + points_len <= raw_src.size()
                    && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                    tags_out[out_count] = 0x0008;
                    vals_out[out_count] = make_bytes(
                        store.arena(),
                        raw_src.subspan(0x0008,
                                        static_cast<size_t>(points_len)));
                    out_count += 1;
                }

                if (points_len > 0 && 0x0030 + points_len <= raw_src.size()
                    && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                    tags_out[out_count] = 0x0030;
                    vals_out[out_count] = make_bytes(
                        store.arena(),
                        raw_src.subspan(0x0030,
                                        static_cast<size_t>(points_len)));
                    out_count += 1;
                }

                const uint16_t u16_tags[] = { 0x0046, 0x0048, 0x004a,
                                              0x004c, 0x004e, 0x0050 };
                for (size_t k = 0; k < sizeof(u16_tags) / sizeof(u16_tags[0]);
                     ++k) {
                    const uint16_t t    = u16_tags[k];
                    const uint64_t off  = t;
                    const uint64_t need = off + 2;
                    if (need > raw_src.size()) {
                        continue;
                    }
                    if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                        break;
                    }
                    uint16_t v16 = 0;
                    if (!read_u16_endian(le, raw_src, off, &v16)) {
                        continue;
                    }
                    tags_out[out_count] = t;
                    vals_out[out_count] = make_u16(v16);
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if (ver == "0300" || ver == "0301") {
                uint16_t tags_out[16];
                MetaValue vals_out[16];
                uint32_t out_count = 0;

                uint8_t schema   = 0;
                bool have_schema = false;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(), std::span<const std::byte>(ver_bytes));
                out_count += 1;

                const uint16_t u8_tags[] = { 0x0004, 0x0005, 0x0006, 0x0007,
                                             0x0038 };
                for (size_t k = 0; k < sizeof(u8_tags) / sizeof(u8_tags[0]);
                     ++k) {
                    const uint16_t t    = u8_tags[k];
                    const uint64_t off  = t;
                    const uint64_t need = off + 1;
                    if (need > raw_src.size()) {
                        continue;
                    }
                    if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                        break;
                    }
                    const uint8_t v = u8(raw_src[off]);
                    if (t == 0x0006) {
                        schema      = v;
                        have_schema = true;
                    }
                    tags_out[out_count] = t;
                    vals_out[out_count] = make_u8(v);
                    out_count += 1;
                }

                const uint64_t points_len
                    = have_schema ? nikon_afinfo2v0300_points_len(schema)
                                  : 0ULL;
                if (points_len > 0 && 0x000a + points_len <= raw_src.size()
                    && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                    tags_out[out_count] = 0x000a;
                    vals_out[out_count] = make_bytes(
                        store.arena(),
                        raw_src.subspan(0x000a,
                                        static_cast<size_t>(points_len)));
                    out_count += 1;
                }

                uint16_t af_x             = 0;
                uint16_t af_y             = 0;
                bool have_af_x            = false;
                bool have_af_y            = false;
                const uint16_t u16_tags[] = { 0x002a, 0x002c, 0x002e,
                                              0x0030, 0x0032, 0x0034 };
                for (size_t k = 0; k < sizeof(u16_tags) / sizeof(u16_tags[0]);
                     ++k) {
                    const uint16_t t    = u16_tags[k];
                    const uint64_t off  = t;
                    const uint64_t need = off + 2;
                    if (need > raw_src.size()) {
                        continue;
                    }
                    if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                        break;
                    }
                    uint16_t v16 = 0;
                    if (!read_u16_endian(le, raw_src, off, &v16)) {
                        continue;
                    }
                    if (t == 0x002e) {
                        af_x      = v16;
                        have_af_x = true;
                    } else if (t == 0x0030) {
                        af_y      = v16;
                        have_af_y = true;
                    }
                    tags_out[out_count] = t;
                    vals_out[out_count] = make_u16(v16);
                    out_count += 1;
                }

                if (have_af_x
                    && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                    tags_out[out_count] = 0x002f;
                    vals_out[out_count] = make_u16(af_x);
                    out_count += 1;
                }
                if (have_af_y
                    && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                    tags_out[out_count] = 0x0031;
                    vals_out[out_count] = make_u16(af_y);
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            uint16_t tags_out[32];
            MetaValue vals_out[32];
            uint32_t out_count = 0;

            uint32_t af_points_idx = 0;
            std::array<std::byte, 5> af_points_bytes;
            bool have_af_points = false;

            tags_out[out_count] = 0x0000;
            vals_out[out_count]
                = make_fixed_ascii_text(store.arena(),
                                        std::span<const std::byte>(ver_bytes));
            out_count += 1;

            for (uint16_t t = 0x0004; t <= 0x0007; ++t) {
                const uint64_t off  = t;
                const uint64_t need = off + 1;
                if (need > raw_src.size()) {
                    continue;
                }
                tags_out[out_count] = t;
                vals_out[out_count] = make_u8(u8(raw_src[off]));
                out_count += 1;
            }

            // AFPointsUsed (variable length; ExifTool uses 5 bytes in
            // AFInfo2Version=0100).
            if (raw_src.size() >= 0x0008 + 5) {
                tags_out[out_count] = 0x0008;
                af_points_idx       = out_count;
                vals_out[out_count] = MetaValue {};
                std::memcpy(af_points_bytes.data(), raw_src.data() + 0x0008,
                            af_points_bytes.size());
                have_af_points = true;
                out_count += 1;
            }

            // AFInfo2Version=0100 fields (u16), kept as a best-effort fallback
            // for older layouts that don't match the dedicated 0101/03xx/04xx
            // branches above.
            const uint16_t u16_tags[] = { 0x0010, 0x0012, 0x0014,
                                          0x0016, 0x0018, 0x001a };
            for (size_t k = 0; k < sizeof(u16_tags) / sizeof(u16_tags[0]);
                 ++k) {
                const uint16_t t    = u16_tags[k];
                const uint64_t off  = t;
                const uint64_t need = off + 2;
                if (need > raw_src.size()) {
                    continue;
                }
                uint16_t v16 = 0;
                if (!read_u16_endian(le, raw_src, off, &v16)) {
                    continue;
                }
                tags_out[out_count] = t;
                vals_out[out_count] = make_u16(v16);
                out_count += 1;
            }

            // AFInfo2Version=03xx+ common fields.
            uint16_t af_x              = 0;
            uint16_t af_y              = 0;
            bool have_af_x             = false;
            bool have_af_y             = false;
            const uint16_t u16_tags2[] = { 0x002a, 0x002c, 0x002e,
                                           0x0030, 0x0032, 0x0034 };
            for (size_t k = 0; k < sizeof(u16_tags2) / sizeof(u16_tags2[0]);
                 ++k) {
                const uint16_t t    = u16_tags2[k];
                const uint64_t off  = t;
                const uint64_t need = off + 2;
                if (need > raw_src.size()) {
                    continue;
                }
                if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                    break;
                }
                uint16_t v16 = 0;
                if (!read_u16_endian(le, raw_src, off, &v16)) {
                    continue;
                }
                if (t == 0x002e) {
                    have_af_x = true;
                    af_x      = v16;
                } else if (t == 0x0030) {
                    have_af_y = true;
                    af_y      = v16;
                }
                tags_out[out_count] = t;
                vals_out[out_count] = make_u16(v16);
                out_count += 1;
            }
            if (have_af_x
                && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                tags_out[out_count] = 0x002f;
                vals_out[out_count] = make_u16(af_x);
                out_count += 1;
            }
            if (have_af_y
                && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                tags_out[out_count] = 0x0031;
                vals_out[out_count] = make_u16(af_y);
                out_count += 1;
            }

            if (raw_src.size() > 0x001c) {
                tags_out[out_count] = 0x001c;
                vals_out[out_count] = make_u8(u8(raw_src[0x001c]));
                out_count += 1;
            }

            if (raw_src.size() > 0x0052
                && out_count < sizeof(tags_out) / sizeof(tags_out[0])) {
                tags_out[out_count] = 0x0052;
                vals_out[out_count] = make_u8(u8(raw_src[0x0052]));
                out_count += 1;
            }

            if (have_af_points) {
                vals_out[af_points_idx]
                    = make_bytes(store.arena(),
                                 std::span<const std::byte>(af_points_bytes));
            }

            decode_nikon_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count), options.limits,
                status_out);
            continue;
        }

        if (tag == 0x00B8) {  // FileInfo
            if (raw_src.size() < 10) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "fileinfo",
                                             idx_fileinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());

            uint16_t card = 0;
            uint16_t dir  = 0;
            uint16_t file = 0;
            if (!read_u16_endian(le, raw_src, 4, &card)
                || !read_u16_endian(le, raw_src, 6, &dir)
                || !read_u16_endian(le, raw_src, 8, &file)) {
                continue;
            }

            const uint16_t tags_out[]  = { 0x0000, 0x0002, 0x0003, 0x0004 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(),
                                      std::span<const std::byte>(ver_bytes)),
                make_u16(card),
                make_u16(dir),
                make_u16(file),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x00B9) {  // AFTune
            if (raw_src.size() < 4) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "aftune", idx_aftune++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> raw;
            std::memcpy(raw.data(), raw_src.data(), raw.size());

            const uint16_t tags_out[]  = { 0x0000, 0x0001, 0x0002, 0x0003 };
            const MetaValue vals_out[] = {
                make_u8(u8(raw[0])),
                make_u8(u8(raw[1])),
                make_i8(static_cast<int8_t>(u8(raw[2]))),
                make_i8(static_cast<int8_t>(u8(raw[3]))),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x00BB) {  // RetouchInfo
            if (raw_src.size() < 6) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "retouchinfo",
                                             idx_retouchinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());

            const int8_t processing = nikon_to_i8(u8(raw_src[5]));

            const uint16_t tags_out[]  = { 0x0000, 0x0005 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(),
                                      std::span<const std::byte>(ver_bytes)),
                make_i8(processing),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }
    }
}

void
decode_nikon_preview_aliases(MetaStore& store, const ExifDecodeOptions& options,
                             ExifDecodeResult* status_out) noexcept
{
    const std::span<const Entry> entries = store.entries();
    if (entries.empty()) {
        return;
    }

    const ByteArena& arena = store.arena();

    // Emit preview aliases when Nikon MakerNote data exists, or when this
    // looks like a Nikon file with an EXIF MakerNote blob (for legacy models
    // where sub-table decode may be sparse).
    bool have_nikon_mk       = false;
    bool have_nikon_make     = false;
    bool have_makernote_blob = false;

    ExifContext ctx(store);
    std::string_view make;
    have_nikon_make = ctx.find_first_text("ifd0", 0x010F, &make)
                      && make.starts_with("NIKON");

    MetaValue maker_note;
    have_makernote_blob = ctx.find_first_value("exififd", 0x927C, &maker_note);

    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        const std::string_view ifd = arena_string(arena,
                                                  e.key.data.exif_tag.ifd);
        if (ifd.starts_with("mk_nikon")) {
            have_nikon_mk = true;
            break;
        }
    }
    if (!have_nikon_mk && !(have_nikon_make && have_makernote_blob)) {
        return;
    }

    // Exiv2 reports NikonPreview.* entries as Nikon vendor tags. Mirror the
    // common ifd1 preview tags into mk_nikon_preview_0.
    uint16_t tags_out[64];
    MetaValue vals_out[64];
    bool seen[7]       = {};
    uint32_t out_count = 0;

    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        const std::string_view ifd = arena_string(arena,
                                                  e.key.data.exif_tag.ifd);
        if (ifd != "ifd1" && ifd != "ifd0") {
            continue;
        }

        uint32_t slot = 0xFFFFFFFFu;
        switch (e.key.data.exif_tag.tag) {
        case 0x0103: slot = 0; break;  // Compression
        case 0x011A: slot = 1; break;  // XResolution
        case 0x011B: slot = 2; break;  // YResolution
        case 0x0128: slot = 3; break;  // ResolutionUnit
        case 0x0201: slot = 4; break;  // JPEGInterchangeFormat
        case 0x0202: slot = 5; break;  // JPEGInterchangeFormatLength
        case 0x0213: slot = 6; break;  // YCbCrPositioning
        default: break;
        }
        if (ifd == "ifd0" && slot != 6) {
            continue;
        }
        if (slot == 0xFFFFFFFFu || seen[slot]) {
            continue;
        }
        seen[slot]          = true;
        tags_out[out_count] = e.key.data.exif_tag.tag;
        vals_out[out_count] = e.value;
        out_count += 1;
        if (out_count >= (sizeof(tags_out) / sizeof(tags_out[0]))) {
            break;
        }
    }

    char model_buf[64] {};
    std::string_view model;
    if (!ctx.find_first_text("ifd0", 0x0110, &model) || model.empty()) {
        MetaValue model_v;
        if (ctx.find_first_value("ifd0", 0x0110, &model_v)
            && model_v.kind == MetaValueKind::Bytes) {
            const std::span<const std::byte> raw_model = store.arena().span(
                model_v.data.span);
            uint32_t n = 0;
            while (n + 1U < sizeof(model_buf) && n < raw_model.size()) {
                const uint8_t c = u8(raw_model[static_cast<size_t>(n)]);
                if (c == 0) {
                    break;
                }
                model_buf[n] = static_cast<char>(c);
                n += 1;
            }
            model = std::string_view(model_buf, n);
        }
    }

    // Best-effort Exiv2 parity for long-tail Nikon-only IDs that come from
    // malformed legacy preview branches on a few models.
    if (have_makernote_blob && maker_note.kind == MetaValueKind::Bytes) {
        const std::span<const std::byte> mn = store.arena().span(
            maker_note.data.span);

        if (model.find("KeyMission 360") != std::string_view::npos) {
            nikon_append_compat_u8_tag(0x0007, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
            nikon_append_compat_u8_tag(0x000C, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
            nikon_append_compat_u8_tag(0x000D, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
        }
        if (model.find("D5300") != std::string_view::npos) {
            nikon_append_compat_u8_tag(0x0009, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
        }
        if (model.find("D300") != std::string_view::npos) {
            nikon_append_compat_u8_tag(0x027D, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
            nikon_append_compat_u8_tag(0x032E, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
        }
        if (model.find("D40X") != std::string_view::npos) {
            nikon_append_compat_u8_tag(0x0459, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
        }
        if (model.find("D80") != std::string_view::npos) {
            nikon_append_compat_u8_tag(0x0483, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
        }
        if (model.find("P900") != std::string_view::npos) {
            nikon_append_compat_u8_tag(0x0B08, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
            nikon_append_compat_u8_tag(0x0B0A, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
            nikon_append_compat_u8_tag(0x2D2C, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
            nikon_append_compat_u8_tag(0x3130, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
            nikon_append_compat_u8_tag(0x3952, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
            nikon_append_compat_u8_tag(0x5233, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
            nikon_append_compat_u8_tag(0xFF2C, mn, tags_out, vals_out,
                                       uint32_t(sizeof(tags_out)
                                                / sizeof(tags_out[0])),
                                       &out_count);
        }
    }

    if (out_count == 0) {
        return;
    }

    char sub_ifd_buf[96];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token("mk_nikon", "preview", 0,
                                     std::span<char>(sub_ifd_buf));
    if (ifd_name.empty()) {
        return;
    }

    decode_nikon_bin_dir_entries(ifd_name, store,
                                 std::span<const uint16_t>(tags_out, out_count),
                                 std::span<const MetaValue>(vals_out, out_count),
                                 options.limits, status_out);
}

}  // namespace openmeta::exif_internal
