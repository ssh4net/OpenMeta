// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"

#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/printim_decode.h"

#include "exif_tiff_decode_internal.h"
#include "geotiff_decode_internal.h"

#include <array>
#include <cstring>
#include <string_view>

namespace openmeta {
namespace {

    static bool checked_add_u64(uint64_t a, uint64_t b, uint64_t* out) noexcept
    {
        if (!out || b > (UINT64_MAX - a)) {
            return false;
        }
        *out = a + b;
        return true;
    }


    static bool checked_mul_u64(uint64_t a, uint64_t b, uint64_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (a == 0U || b == 0U) {
            *out = 0U;
            return true;
        }
        if (a > (UINT64_MAX / b)) {
            return false;
        }
        *out = a * b;
        return true;
    }


    static bool span_contains_bytes(std::span<const std::byte> bytes,
                                    uint64_t offset, uint64_t size) noexcept
    {
        const uint64_t total = static_cast<uint64_t>(bytes.size());
        return offset <= total && size <= (total - offset);
    }

    static constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }


    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!span_contains_bytes(bytes, offset, 2U)) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 8U)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 0U);
        *out = v;
        return true;
    }


    static bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!span_contains_bytes(bytes, offset, 2U)) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 0U)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 8U);
        *out = v;
        return true;
    }


    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!span_contains_bytes(bytes, offset, 4U)) {
            return false;
        }
        const uint32_t v
            = (static_cast<uint32_t>(u8(bytes[offset + 0])) << 24U)
              | (static_cast<uint32_t>(u8(bytes[offset + 1])) << 16U)
              | (static_cast<uint32_t>(u8(bytes[offset + 2])) << 8U)
              | (static_cast<uint32_t>(u8(bytes[offset + 3])) << 0U);
        *out = v;
        return true;
    }


    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!span_contains_bytes(bytes, offset, 4U)) {
            return false;
        }
        const uint32_t v
            = (static_cast<uint32_t>(u8(bytes[offset + 0])) << 0U)
              | (static_cast<uint32_t>(u8(bytes[offset + 1])) << 8U)
              | (static_cast<uint32_t>(u8(bytes[offset + 2])) << 16U)
              | (static_cast<uint32_t>(u8(bytes[offset + 3])) << 24U);
        *out = v;
        return true;
    }


    static bool read_i32_endian(bool le, std::span<const std::byte> bytes,
                                uint64_t offset, int32_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint32_t raw = 0;
        if (!(le ? read_u32le(bytes, offset, &raw)
                 : read_u32be(bytes, offset, &raw))) {
            return false;
        }
        *out = static_cast<int32_t>(raw);
        return true;
    }


    static bool read_u64be(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (!span_contains_bytes(bytes, offset, 8U)) {
            return false;
        }
        uint64_t v = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            v = (v << 8U) | static_cast<uint64_t>(u8(bytes[offset + i]));
        }
        *out = v;
        return true;
    }


    static bool read_u64le(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (!span_contains_bytes(bytes, offset, 8U)) {
            return false;
        }
        uint64_t v = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(u8(bytes[offset + i])) << (i * 8U);
        }
        *out = v;
        return true;
    }


    static bool read_tiff_u16(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint16_t* out) noexcept
    {
        if (cfg.le) {
            return read_u16le(bytes, offset, out);
        }
        return read_u16be(bytes, offset, out);
    }


    static bool read_tiff_u32(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint32_t* out) noexcept
    {
        if (cfg.le) {
            return read_u32le(bytes, offset, out);
        }
        return read_u32be(bytes, offset, out);
    }


    static bool read_tiff_u64(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint64_t* out) noexcept
    {
        if (cfg.le) {
            return read_u64le(bytes, offset, out);
        }
        return read_u64be(bytes, offset, out);
    }


    static bool is_classic_tiff_header(std::span<const std::byte> bytes,
                                       uint64_t offset) noexcept
    {
        if (!span_contains_bytes(bytes, offset, 8U)) {
            return false;
        }
        const uint8_t a = u8(bytes[offset + 0]);
        const uint8_t b = u8(bytes[offset + 1]);
        const uint8_t c = u8(bytes[offset + 2]);
        const uint8_t d = u8(bytes[offset + 3]);

        if (a == 'I' && b == 'I' && c == 0x2A && d == 0x00) {
            uint32_t ifd_off = 0;
            if (!read_u32le(bytes, offset + 4, &ifd_off)) {
                return false;
            }
            return static_cast<uint64_t>(ifd_off) < bytes.size();
        }
        if (a == 'M' && b == 'M' && c == 0x00 && d == 0x2A) {
            uint32_t ifd_off = 0;
            if (!read_u32be(bytes, offset + 4, &ifd_off)) {
                return false;
            }
            return static_cast<uint64_t>(ifd_off) < bytes.size();
        }
        return false;
    }


    static uint64_t find_embedded_tiff_header(std::span<const std::byte> bytes,
                                              uint64_t max_search) noexcept
    {
        const uint64_t limit = (max_search < bytes.size()) ? max_search
                                                           : bytes.size();
        for (uint64_t off = 0; off + 8 <= limit; ++off) {
            if (is_classic_tiff_header(bytes, off)) {
                return off;
            }
        }
        return UINT64_MAX;
    }


    static bool match_bytes(std::span<const std::byte> bytes, uint64_t offset,
                            const char* s, uint32_t s_len) noexcept
    {
        if (!span_contains_bytes(bytes, offset, s_len)) {
            return false;
        }
        return std::memcmp(bytes.data() + static_cast<size_t>(offset), s,
                           static_cast<size_t>(s_len))
               == 0;
    }


    static uint8_t ascii_lower(uint8_t c) noexcept
    {
        if (c >= 'A' && c <= 'Z') {
            return static_cast<uint8_t>(c + ('a' - 'A'));
        }
        return c;
    }


    static bool ascii_starts_with_insensitive(std::string_view s,
                                              std::string_view prefix) noexcept
    {
        if (prefix.size() > s.size()) {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i) {
            const uint8_t a = ascii_lower(static_cast<uint8_t>(s[i]));
            const uint8_t b = ascii_lower(static_cast<uint8_t>(prefix[i]));
            if (a != b) {
                return false;
            }
        }
        return true;
    }

    static bool ascii_equals_insensitive(std::string_view a,
                                         std::string_view b) noexcept
    {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            const uint8_t aa = ascii_lower(static_cast<uint8_t>(a[i]));
            const uint8_t bb = ascii_lower(static_cast<uint8_t>(b[i]));
            if (aa != bb) {
                return false;
            }
        }
        return true;
    }

    static bool ascii_contains_insensitive(std::string_view s,
                                           std::string_view needle) noexcept
    {
        if (needle.empty()) {
            return false;
        }
        if (needle.size() > s.size()) {
            return false;
        }
        const size_t limit = s.size() - needle.size();
        for (size_t i = 0; i <= limit; ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                const uint8_t a = ascii_lower(static_cast<uint8_t>(s[i + j]));
                const uint8_t b = ascii_lower(static_cast<uint8_t>(needle[j]));
                if (a != b) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        return false;
    }


    static bool ascii_is_digit(char c) noexcept { return c >= '0' && c <= '9'; }


    static bool phaseone_model_prefix(std::string_view model,
                                      std::string_view prefix) noexcept
    {
        if (!ascii_starts_with_insensitive(model, prefix)) {
            return false;
        }
        if (model.size() == prefix.size()) {
            return true;
        }
        const char next = model[prefix.size()];
        return next == ' ' || next == '-' || ascii_is_digit(next);
    }


    static bool looks_like_phaseone_family(std::string_view make,
                                           std::string_view model) noexcept
    {
        if (ascii_starts_with_insensitive(make, "Phase One")
            || ascii_starts_with_insensitive(make, "PhaseOne")
            || ascii_starts_with_insensitive(make, "Leaf")
            || ascii_starts_with_insensitive(make, "Mamiya")) {
            return true;
        }
        return phaseone_model_prefix(model, "Credo")
               || phaseone_model_prefix(model, "IQ");
    }


    static bool
    minolta_main_0103_prefers_placeholder(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "DiMAGE A200");
    }

    static bool
    minolta_main_0103_prefers_quality(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "DiMAGE 7Hi");
    }

    static bool
    motorola_main_6420_prefers_placeholder(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "XT1052")
               || ascii_equals_insensitive(model, "XT1060")
               || ascii_equals_insensitive(model, "XT1068")
               || ascii_equals_insensitive(model, "XT1080")
               || ascii_equals_insensitive(model, "XT1572")
               || ascii_equals_insensitive(model, "XT1580")
               || ascii_equals_insensitive(model, "Moto G (4)");
    }

    static bool
    sigma_main_0033_prefers_placeholder(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "SIGMA DP1 Merrill")
               || ascii_equals_insensitive(model, "SIGMA dp2 Quattro");
    }

    static bool sigma_main_fp_l_prefers_placeholder(std::string_view model,
                                                    uint16_t tag) noexcept
    {
        if (!ascii_equals_insensitive(model, "SIGMA fp L")) {
            return false;
        }
        switch (tag) {
        case 0x0026U:
        case 0x0027U:
        case 0x002AU:
        case 0x002BU:
        case 0x0048U:
        case 0x0056U: return true;
        default: return false;
        }
    }

    static bool
    sigma_main_sd_quattro_h_prefers_placeholder(std::string_view model,
                                                uint16_t tag) noexcept
    {
        if (!ascii_equals_insensitive(model, "sd Quattro H")) {
            return false;
        }
        return tag == 0x0034U || tag == 0x004BU;
    }

    static std::string_view sony_trim_model(std::string_view model) noexcept
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

    static bool sony_model_is_stellar(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        return ascii_equals_insensitive(model, "Stellar");
    }

    static bool sony_model_is_hasselblad_hv(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        return ascii_equals_insensitive(model, "HV");
    }

    static bool sony_model_is_dsc(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        return ascii_starts_with_insensitive(model, "DSC-");
    }

    static bool sony_model_is_zv(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        return ascii_starts_with_insensitive(model, "ZV-");
    }

    static bool sony_model_is_slt(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        return ascii_starts_with_insensitive(model, "SLT-");
    }

    static bool sony_model_is_ilca(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        return ascii_starts_with_insensitive(model, "ILCA-");
    }

    static bool sony_model_is_ilca_68_or_77m2(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        return ascii_equals_insensitive(model, "ILCA-68")
               || ascii_equals_insensitive(model, "ILCA-77M2");
    }

    static bool
    sony_main_201d_uses_semantic_name(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        if (ascii_starts_with_insensitive(model, "NEX-")
            || ascii_starts_with_insensitive(model, "ILCE-")
            || ascii_starts_with_insensitive(model, "ILME-")
            || sony_model_is_zv(model)) {
            return true;
        }
        return ascii_equals_insensitive(model, "DSC-RX10M4")
               || ascii_equals_insensitive(model, "DSC-RX100M6")
               || ascii_equals_insensitive(model, "DSC-RX100M7")
               || ascii_equals_insensitive(model, "DSC-RX100M5A")
               || ascii_equals_insensitive(model, "DSC-HX95")
               || ascii_equals_insensitive(model, "DSC-HX99")
               || ascii_equals_insensitive(model, "DSC-RX0M2");
    }

    static bool
    sony_main_focus_area_uses_semantic_name(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        if (ascii_starts_with_insensitive(model, "NEX-")
            || ascii_starts_with_insensitive(model, "ILCE-")
            || ascii_starts_with_insensitive(model, "ILME-")
            || sony_model_is_zv(model) || sony_model_is_slt(model)
            || sony_model_is_ilca(model)
            || sony_model_is_hasselblad_hv(model)) {
            return true;
        }
        return ascii_equals_insensitive(model, "DSC-RX10M4")
               || ascii_equals_insensitive(model, "DSC-RX100M6")
               || ascii_equals_insensitive(model, "DSC-RX100M7")
               || ascii_equals_insensitive(model, "DSC-RX100M5A")
               || ascii_equals_insensitive(model, "DSC-HX95")
               || ascii_equals_insensitive(model, "DSC-HX99")
               || ascii_equals_insensitive(model, "DSC-RX0M2");
    }

    static bool
    sony_model_is_model_name_placeholder(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        return ascii_equals_insensitive(model, "MODEL-NAME");
    }

    static bool
    sony_main_201b_uses_semantic_name(std::string_view model) noexcept
    {
        if (sony_model_is_model_name_placeholder(model)) {
            return true;
        }
        return sony_main_focus_area_uses_semantic_name(model);
    }

    static bool
    sony_main_201c_uses_semantic_name(std::string_view model) noexcept
    {
        return sony_main_focus_area_uses_semantic_name(model);
    }

    static bool
    sony_main_201e_uses_semantic_name(std::string_view model) noexcept
    {
        return !sony_model_is_model_name_placeholder(model);
    }

    static bool
    sony_main_2021_uses_semantic_name(std::string_view model) noexcept
    {
        if (sony_model_is_model_name_placeholder(model)) {
            return true;
        }
        return sony_main_focus_area_uses_semantic_name(model);
    }

    static bool
    sony_main_2020_uses_semantic_name(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        if (sony_model_is_ilca_68_or_77m2(model)) {
            return true;
        }
        return !sony_model_is_ilca(model) && !sony_model_is_dsc(model)
               && !sony_model_is_zv(model);
    }

    static bool
    sony_main_2022_uses_semantic_name(std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        return ascii_equals_insensitive(model, "ILCE-5100")
               || ascii_equals_insensitive(model, "ILCE-6000")
               || ascii_equals_insensitive(model, "ILCE-7M2")
               || ascii_equals_insensitive(model, "ILCE-7RM2");
    }

    static bool
    sony_main_b050_uses_semantic_name(std::string_view model) noexcept
    {
        return sony_model_is_dsc(model) || sony_model_is_stellar(model);
    }

    static bool sony_tag9406_0005_prefers_battery_level_name(
        std::string_view model) noexcept
    {
        model = sony_trim_model(model);
        return ascii_equals_insensitive(model, "ILCE-6700")
               || ascii_equals_insensitive(model, "ILCE-7CR")
               || ascii_equals_insensitive(model, "ILCE-7M5")
               || ascii_equals_insensitive(model, "ILCE-9M3")
               || ascii_equals_insensitive(model, "ZV-E10M2");
    }

    static bool nikonsettings_model_is_d7500(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON D7500");
    }

    static bool nikonsettings_model_is_d780(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON D780");
    }

    static bool nikonsettings_model_is_d850(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON D850");
    }

    static bool nikonsettings_model_is_z30(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z 30");
    }

    static bool nikonsettings_model_is_z5(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z 5");
    }

    static bool nikonsettings_model_is_z50(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z 50");
    }

    static bool nikonsettings_model_is_z6(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z 6");
    }

    static bool nikonsettings_model_is_z6ii(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z 6_2");
    }

    static bool nikonsettings_model_is_z7(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z 7");
    }

    static bool nikonsettings_model_is_z7ii(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z 7_2");
    }

    static bool nikonsettings_model_is_zfc(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z fc");
    }

    static bool nikon_model_is_z8(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z 8");
    }

    static bool nikon_model_is_zf(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z f");
    }

    static bool nikon_model_is_d800(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON D800");
    }

    static bool nikon_model_is_d800e(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON D800E");
    }

    static bool
    nikon_main_model_is_legacy_compact_type2(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "E700")
               || ascii_equals_insensitive(model, "E800")
               || ascii_equals_insensitive(model, "E900")
               || ascii_equals_insensitive(model, "E950");
    }

    static bool nikon_main_model_is_z_family(std::string_view model) noexcept
    {
        return ascii_equals_insensitive(model, "NIKON Z fc")
               || ascii_equals_insensitive(model, "NIKON Z f")
               || ascii_starts_with_insensitive(model, "NIKON Z ")
               || ascii_starts_with_insensitive(model, "NIKON Z5")
               || ascii_starts_with_insensitive(model, "NIKON Z6")
               || ascii_starts_with_insensitive(model, "NIKON Z7")
               || ascii_starts_with_insensitive(model, "NIKON Z50");
    }

    static bool nikon_main_z_tag_prefers_compat_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x002BU:
        case 0x002CU:
        case 0x002EU:
        case 0x002FU:
        case 0x0031U:
        case 0x0032U:
        case 0x0035U: return true;
        default: return false;
        }
    }

    static bool nikonsettings_model_uses_iso_placeholder_names(
        std::string_view model) noexcept
    {
        return nikonsettings_model_is_z5(model)
               || nikonsettings_model_is_z50(model)
               || nikonsettings_model_is_z30(model)
               || nikonsettings_model_is_z6(model)
               || nikonsettings_model_is_z6ii(model)
               || nikonsettings_model_is_zfc(model);
    }

    static bool
    nikonsettings_model_uses_movie_func_aliases(std::string_view model) noexcept
    {
        return nikonsettings_model_is_z5(model)
               || nikonsettings_model_is_z50(model)
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
        const bool z_movie_aliases
            = nikonsettings_model_uses_movie_func_aliases(model);

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


    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }


    static bool store_has_exif_tag(const MetaStore& store, std::string_view ifd,
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
            if (arena_string(arena, e.key.data.exif_tag.ifd) == ifd) {
                return true;
            }
        }
        return false;
    }

    static std::string_view find_first_exif_ascii_value(const MetaStore& store,
                                                        std::string_view ifd,
                                                        uint16_t tag) noexcept;

    static bool
    casio_type2_prefers_legacy_main_names(std::string_view model) noexcept
    {
        return ascii_starts_with_insensitive(model, "GV-")
               || ascii_starts_with_insensitive(model, "QV-")
               || ascii_starts_with_insensitive(model, "XV-");
    }

    static bool fujifilm_main_prefers_placeholder(uint16_t tag,
                                                  std::string_view make) noexcept
    {
        if (make.starts_with("GENERAL IMAGING")) {
            return false;
        }

        switch (tag) {
        case 0x1051U:
        case 0x1150U:
        case 0x1151U:
        case 0x1152U:
        case 0x1304U:
        case 0x144AU:
        case 0x144BU:
        case 0x144CU: return true;
        default: return false;
        }
    }

    static bool
    canon_model_matches_any(std::string_view model,
                            std::span<const std::string_view> needles) noexcept
    {
        for (size_t i = 0; i < needles.size(); ++i) {
            if (ascii_contains_insensitive(model, needles[i])) {
                return true;
            }
        }
        return false;
    }

    static bool canon_model_is_1d_family(std::string_view model) noexcept
    {
        static constexpr std::string_view kModels[] = {
            "EOS-1D",
            "EOS-1DS",
        };
        return canon_model_matches_any(model, kModels);
    }

    static bool canon_model_is_1ds(std::string_view model) noexcept
    {
        return ascii_contains_insensitive(model, "EOS-1DS");
    }

    static bool
    canon_model_is_early_kelvin_group(std::string_view model) noexcept
    {
        static constexpr std::string_view kModels[] = {
            "EOS 10D",
            "EOS 300D",
            "EOS DIGITAL REBEL",
            "EOS Kiss Digital",
        };
        return canon_model_matches_any(model, kModels);
    }

    static bool
    canon_model_is_1100d_blacklevel_group(std::string_view model) noexcept
    {
        static constexpr std::string_view kModels[] = {
            "EOS 1100D",
            "EOS Kiss X50",
            "EOS REBEL T3",
            "EOS 60D",
        };
        return canon_model_matches_any(model, kModels);
    }

    static bool
    canon_model_is_1100d_maxfocal_group(std::string_view model) noexcept
    {
        static constexpr std::string_view kModels[] = {
            "EOS 1100D",
            "EOS Kiss X50",
            "EOS REBEL T3",
        };
        return canon_model_matches_any(model, kModels);
    }

    static bool
    canon_model_is_1200d_wb_unknown7_group(std::string_view model) noexcept
    {
        static constexpr std::string_view kModels[] = {
            "EOS 1200D",
            "EOS Kiss X70",
            "EOS REBEL T5",
        };
        return canon_model_matches_any(model, kModels);
    }

    static bool
    canon_model_is_7d_colordata_psinfo_group(std::string_view model) noexcept
    {
        static constexpr std::string_view kModels[] = {
            "EOS 7D",
        };
        return canon_model_matches_any(model, kModels);
    }

    static bool
    canon_model_is_colordata7_psinfo2_group(std::string_view model) noexcept
    {
        static constexpr std::string_view kModels[] = {
            "EOS Kiss X7i",
            "EOS-1D X",
        };
        return canon_model_matches_any(model, kModels);
    }

    static bool
    canon_model_is_r1_r5m2_battery_group(std::string_view model) noexcept
    {
        static constexpr std::string_view kModels[] = {
            "EOS R1",
            "EOS R5m2",
            "EOS R5 Mark II",
        };
        return canon_model_matches_any(model, kModels);
    }

    static bool canon_main0000_prefers_focaltype(std::string_view model) noexcept
    {
        static constexpr std::string_view kModels[] = {
            "DC410", "HV30", "FS300", "XL H1", "ZR950", "DC210", "DC420",
        };
        return canon_model_matches_any(model, kModels);
    }

    static bool
    canon_main0000_prefers_afinfo_size(std::string_view model) noexcept
    {
        static constexpr std::string_view kModels[] = {
            "EOS C300",
        };
        return canon_model_matches_any(model, kModels);
    }


    static void maybe_mark_contextual_name(std::string_view ifd_name,
                                           uint16_t tag, const MetaStore& store,
                                           Entry* entry) noexcept
    {
        if (!entry) {
            return;
        }
        if (ifd_name == "mk_olympus_focusinfo_0" && tag == 0x1600u) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::OlympusFocusInfo1600;
            entry->origin.name_context_variant
                = store_has_exif_tag(store, "mk_olympus_camerasettings_0",
                                     0x0604u)
                      ? 2U
                      : 1U;
            return;
        }
        if (ifd_name == "mk_kodak0" && tag == 0x0028u
            && entry->value.kind == MetaValueKind::Text
            && store_has_exif_tag(store, "mk_kodak0", 0x0008u)) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::KodakMain0028;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (ifd_name == "mk_minolta0" && tag == 0x0103U) {
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            if (minolta_main_0103_prefers_placeholder(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::MinoltaMainCompat;
                entry->origin.name_context_variant = 1U;
                return;
            }
            if (minolta_main_0103_prefers_quality(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::MinoltaMainCompat;
                entry->origin.name_context_variant = 2U;
                return;
            }
        }
        if (ifd_name == "mk_canon0" && tag == 0x0038u
            && entry->value.kind == MetaValueKind::Bytes
            && entry->origin.wire_count <= 8U) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::CanonMain0038;
            entry->origin.name_context_variant = 1U;
            return;
        }
        if (ifd_name.starts_with("mk_canon")) {
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            if (ifd_name == "mk_canon0" && tag == 0x0000U
                && entry->origin.wire_type.family == WireFamily::Tiff
                && entry->origin.wire_type.code == 3U
                && entry->origin.wire_count == 6U
                && entry->value.kind == MetaValueKind::Array) {
                if (canon_main0000_prefers_afinfo_size(model)) {
                    entry->flags |= EntryFlags::ContextualName;
                    entry->origin.name_context_kind
                        = EntryNameContextKind::CanonMain0000;
                    entry->origin.name_context_variant = 2U;
                    return;
                }
                if (canon_main0000_prefers_focaltype(model)) {
                    entry->flags |= EntryFlags::ContextualName;
                    entry->origin.name_context_kind
                        = EntryNameContextKind::CanonMain0000;
                    entry->origin.name_context_variant = 1U;
                    return;
                }
            }
            if (ifd_name == "mk_canon_shotinfo_0" && tag == 0x000EU
                && canon_model_is_1d_family(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::CanonShotInfo000E;
                entry->origin.name_context_variant = 1U;
                return;
            }
            if (ifd_name == "mk_canon_camerasettings_0" && tag == 0x0021U
                && canon_model_is_early_kelvin_group(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::CanonCameraSettings0021;
                entry->origin.name_context_variant = 1U;
                return;
            }
            if (ifd_name == "mk_canon_colordata4_0") {
                if (tag == 0x00DAU
                    && canon_model_is_7d_colordata_psinfo_group(model)) {
                    entry->flags |= EntryFlags::ContextualName;
                    entry->origin.name_context_kind
                        = EntryNameContextKind::CanonColorData4PSInfo;
                    entry->origin.name_context_variant = 1U;
                    return;
                }
                if (tag == 0x00EAU
                    && canon_model_is_1200d_wb_unknown7_group(model)) {
                    entry->flags |= EntryFlags::ContextualName;
                    entry->origin.name_context_kind
                        = EntryNameContextKind::CanonColorData400EA;
                    entry->origin.name_context_variant = 1U;
                    return;
                }
                if (tag == 0x00EEU
                    && canon_model_is_1100d_maxfocal_group(model)) {
                    entry->flags |= EntryFlags::ContextualName;
                    entry->origin.name_context_kind
                        = EntryNameContextKind::CanonColorData400EE;
                    entry->origin.name_context_variant = 1U;
                    return;
                }
                if (tag == 0x02CFU
                    && canon_model_is_1100d_blacklevel_group(model)) {
                    entry->flags |= EntryFlags::ContextualName;
                    entry->origin.name_context_kind
                        = EntryNameContextKind::CanonColorData402CF;
                    entry->origin.name_context_variant = 1U;
                    return;
                }
            }
            if (ifd_name == "mk_canon_colordata7_0"
                && canon_model_is_colordata7_psinfo2_group(model)) {
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
                && canon_model_is_r1_r5m2_battery_group(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::CanonColorCalib0038;
                entry->origin.name_context_variant = 1U;
                return;
            }
            if (ifd_name == "mk_canon_camerainfo1d_0" && tag == 0x0048U
                && canon_model_is_1ds(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::CanonCameraInfo1D0048;
                entry->origin.name_context_variant = 1U;
                return;
            }
        }
        if (ifd_name == "mk_casio_type2_0"
            && (tag <= 0x0019U || tag == 0x001AU || tag == 0x001CU
                || tag == 0x001DU || tag == 0x001EU || tag == 0x0E00U
                || tag == 0x2023U)) {
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            if (casio_type2_prefers_legacy_main_names(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::CasioType2Legacy;
                entry->origin.name_context_variant = 1U;
                return;
            }
        }
        if (ifd_name == "mk_fuji0") {
            const std::string_view make
                = find_first_exif_ascii_value(store, "ifd0", 0x010F /* Make */);
            if (fujifilm_main_prefers_placeholder(tag, make)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::FujifilmMain1304;
                entry->origin.name_context_variant = 1U;
                return;
            }
        }
        if (ifd_name == "mk_motorola0" && tag == 0x6420u) {
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            if (motorola_main_6420_prefers_placeholder(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::MotorolaMain6420;
                entry->origin.name_context_variant = 1U;
                return;
            }
        }
        if (ifd_name == "mk_sigma0") {
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            bool prefer_placeholder = false;
            if (tag == 0x0033U && sigma_main_0033_prefers_placeholder(model)) {
                prefer_placeholder = true;
            }
            if (tag == 0x0034U
                && (sigma_main_0033_prefers_placeholder(model)
                    || sigma_main_sd_quattro_h_prefers_placeholder(model,
                                                                   tag))) {
                prefer_placeholder = true;
            }
            if (sigma_main_fp_l_prefers_placeholder(model, tag)
                || sigma_main_sd_quattro_h_prefers_placeholder(model, tag)) {
                prefer_placeholder = true;
            }
            if (prefer_placeholder) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::SigmaMainCompat;
                entry->origin.name_context_variant = 1U;
                return;
            }
        }
        if (ifd_name == "mk_sony0") {
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            bool prefer_placeholder = false;
            switch (tag) {
            case 0x201BU:
                prefer_placeholder = !sony_main_201b_uses_semantic_name(model);
                break;
            case 0x201CU:
                prefer_placeholder = !sony_main_201c_uses_semantic_name(model);
                break;
            case 0x201DU:
                prefer_placeholder = !sony_main_201d_uses_semantic_name(model);
                break;
            case 0x201EU:
                prefer_placeholder = !sony_main_201e_uses_semantic_name(model);
                break;
            case 0x2020U:
                prefer_placeholder = !sony_main_2020_uses_semantic_name(model);
                break;
            case 0x2021U:
                prefer_placeholder = !sony_main_2021_uses_semantic_name(model);
                break;
            case 0x2022U:
                prefer_placeholder = !sony_main_2022_uses_semantic_name(model);
                break;
            case 0x205CU: prefer_placeholder = true; break;
            case 0xB042U:
            case 0xB043U:
                prefer_placeholder = !sony_main_201d_uses_semantic_name(model);
                break;
            case 0xB050U:
                prefer_placeholder = !sony_main_b050_uses_semantic_name(model);
                break;
            default: break;
            }
            if (prefer_placeholder) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::SonyMainCompat;
                entry->origin.name_context_variant = 1U;
                return;
            }
        }
        if (ifd_name == "mk_sony_tag9406_0" && tag == 0x0005U) {
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            if (sony_tag9406_0005_prefers_battery_level_name(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::SonyTag94060005;
                entry->origin.name_context_variant = 1U;
                return;
            }
        }
        if (ifd_name == "mk_ricoh0") {
            const bool is_short = entry->origin.wire_type.family
                                      == WireFamily::Tiff
                                  && entry->origin.wire_type.code == 3U;
            if ((tag == 0x1002u || tag == 0x1004u) && !is_short) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::RicohMainCompat;
                entry->origin.name_context_variant = 1U;
                return;
            }
            if (tag == 0x1003u && is_short) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::RicohMainCompat;
                entry->origin.name_context_variant = 2U;
                return;
            }
        }
        if (ifd_name.starts_with("mk_nikonsettings_main_")) {
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            if (tag == 0x010BU && nikonsettings_model_is_zfc(model)
                && entry->value.kind == MetaValueKind::Scalar
                && entry->value.elem_type == MetaElementType::U32
                && entry->value.data.u64 == 1U) {
                return;
            }
            const uint8_t variant = nikonsettings_main_context_variant(tag,
                                                                       model);
            if (variant != 0U) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::NikonSettingsMain;
                entry->origin.name_context_variant = variant;
                return;
            }
        }
        if (ifd_name == "mk_nikon0") {
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            if (nikon_main_model_is_legacy_compact_type2(model)
                && (tag == 0x0002U || tag == 0x0003U || tag == 0x0004U
                    || tag == 0x0005U || tag == 0x0006U || tag == 0x0007U
                    || tag == 0x0008U || tag == 0x0009U || tag == 0x000AU
                    || tag == 0x000BU || tag == 0x0F00U)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::NikonMainCompactType2;
                entry->origin.name_context_variant = 1U;
                return;
            }
            if (nikon_main_model_is_z_family(model)
                && nikon_main_z_tag_prefers_compat_name(tag)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::NikonMainZ;
                entry->origin.name_context_variant = 1U;
                return;
            }
        }
        if (ifd_name == "mk_nikon_shotinfo_0") {
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            if (nikon_model_is_d800(model) || nikon_model_is_d800e(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::NikonShotInfoD800;
                entry->origin.name_context_variant = 1U;
                return;
            }
            if (nikonsettings_model_is_d850(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::NikonShotInfoD850;
                entry->origin.name_context_variant = 1U;
                return;
            }
            if (nikon_model_is_z8(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::NikonShotInfoZ8;
                entry->origin.name_context_variant = 1U;
                return;
            }
            if (tag == 0x0024U && nikon_model_is_zf(model)) {
                entry->flags |= EntryFlags::ContextualName;
                entry->origin.name_context_kind
                    = EntryNameContextKind::NikonShotInfoZ8;
                entry->origin.name_context_variant = 1U;
                return;
            }
        }
        if (ifd_name == "mk_canoncustom_functions2_0" && tag == 0x0103u) {
            entry->flags |= EntryFlags::ContextualName;
            entry->origin.name_context_kind
                = EntryNameContextKind::CanonCustomFunctions20103;
            entry->origin.name_context_variant = (entry->origin.wire_count > 1U)
                                                     ? 2U
                                                     : 1U;
        }
    }


    static std::string_view find_first_exif_ascii_value(const MetaStore& store,
                                                        std::string_view ifd,
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
            if (e.value.kind != MetaValueKind::Bytes) {
                continue;
            }

            const std::span<const std::byte> raw = arena.span(
                e.value.data.span);
            size_t n = 0;
            while (n < raw.size() && raw[n] != std::byte { 0 }) {
                n += 1;
            }
            while (n > 0 && raw[n - 1] == std::byte { ' ' }) {
                n -= 1;
            }
            if (n == 0) {
                continue;
            }
            return std::string_view(reinterpret_cast<const char*>(raw.data()),
                                    n);
        }
        return {};
    }


    enum class MakerNoteVendor : uint8_t {
        Unknown,
        Nikon,
        Canon,
        Sony,
        Sigma,
        Minolta,
        Fuji,
        Apple,
        Olympus,
        Pentax,
        Casio,
        Panasonic,
        Kodak,
        Flir,
        Ricoh,
        Samsung,
        Jvc,
        Dji,
        Ge,
        Motorola,
        Reconyx,
        Hp,
        Nintendo,
        PhaseOne,
    };

    static MakerNoteVendor
    detect_makernote_vendor(std::span<const std::byte> maker_note_bytes,
                            const MetaStore& store) noexcept
    {
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "Nikon\0", 6)) {
            return MakerNoteVendor::Nikon;
        }
        // Hasselblad-branded Sony cameras store Sony MakerNotes with a "VHAB"
        // prefix.
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "VHAB", 4)) {
            return MakerNoteVendor::Sony;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "SONY", 4)) {
            return MakerNoteVendor::Sony;
        }
        if (maker_note_bytes.size() >= 5
            && match_bytes(maker_note_bytes, 0, "SIGMA", 5)) {
            return MakerNoteVendor::Sigma;
        }
        if (maker_note_bytes.size() >= 8
            && match_bytes(maker_note_bytes, 0, "FUJIFILM", 8)) {
            return MakerNoteVendor::Fuji;
        }
        if (maker_note_bytes.size() >= 9
            && match_bytes(maker_note_bytes, 0, "Apple iOS", 9)) {
            return MakerNoteVendor::Apple;
        }
        if (maker_note_bytes.size() >= 9
            && match_bytes(maker_note_bytes, 0, "OM SYSTEM", 9)) {
            return MakerNoteVendor::Olympus;
        }
        // Older Olympus/Epson MakerNotes may start with "EPSON\0" (Epson) or
        // "OLYMP\0" (Olympus) and use an 8-byte header.
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "EPSON\0", 6)) {
            return MakerNoteVendor::Olympus;
        }
        // Some Minolta models use Olympus tags with a "MINOL\0" prefix.
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "MINOL\0", 6)) {
            return MakerNoteVendor::Olympus;
        }
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "OLYMP\0", 6)) {
            return MakerNoteVendor::Olympus;
        }
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "CAMER\0", 6)) {
            return MakerNoteVendor::Olympus;
        }
        if (maker_note_bytes.size() >= 8
            && match_bytes(maker_note_bytes, 0, "OLYMPUS\0", 8)) {
            return MakerNoteVendor::Olympus;
        }
        // Some Ricoh cameras (eg. GR III / WG-6 / G900SE) use Pentax MakerNotes
        // with a "RICOH\0" prefix and embedded byte-order mark.
        if (maker_note_bytes.size() >= 8
            && match_bytes(maker_note_bytes, 0, "RICOH\0", 6)
            && (match_bytes(maker_note_bytes, 6, "II", 2)
                || match_bytes(maker_note_bytes, 6, "MM", 2))) {
            return MakerNoteVendor::Pentax;
        }
        // Native Ricoh MakerNotes often begin with "Ricoh" and should not be
        // classified as Pentax based on the Make string (eg. "PENTAX RICOH IMAGING").
        if (maker_note_bytes.size() >= 5
            && match_bytes(maker_note_bytes, 0, "Ricoh", 5)) {
            return MakerNoteVendor::Ricoh;
        }
        if (maker_note_bytes.size() >= 7
            && match_bytes(maker_note_bytes, 0, "PENTAX ", 7)) {
            return MakerNoteVendor::Pentax;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "AOC\0", 4)) {
            // Pentax Optio 330RS/430RS store Casio-like maker notes (ExifTool:
            // MakerNotePentax3 -> Casio::Type2).
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            if (!model.empty()
                && (ascii_starts_with_insensitive(model, "PENTAX Optio 330RS")
                    || ascii_starts_with_insensitive(model, "PENTAX Optio330RS")
                    || ascii_starts_with_insensitive(model, "PENTAX Optio 430RS")
                    || ascii_starts_with_insensitive(model,
                                                     "PENTAX Optio430RS"))) {
                return MakerNoteVendor::Casio;
            }
            return MakerNoteVendor::Pentax;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "QVC\0", 4)) {
            return MakerNoteVendor::Casio;
        }
        // Concord cameras may store Casio Type2 MakerNotes with a "DCI\0"
        // prefix.
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "DCI\0", 4)) {
            return MakerNoteVendor::Casio;
        }
        // Phase One IIQ MakerNotes start with "IIIITwaR"/"IIIICwaR" or the
        // big-endian "MMMMRaw." variant. Detect these before the generic
        // Kodak/HP "IIII" fallbacks.
        if (maker_note_bytes.size() >= 8) {
            const std::string_view make
                = find_first_exif_ascii_value(store, "ifd0", 0x010F /* Make */);
            const std::string_view model
                = find_first_exif_ascii_value(store, "ifd0",
                                              0x0110 /* Model */);
            if (looks_like_phaseone_family(make, model)
                && ((match_bytes(maker_note_bytes, 0, "IIII", 4)
                     && match_bytes(maker_note_bytes, 5, "waR", 3))
                    || (match_bytes(maker_note_bytes, 0, "MMMM", 4)
                        && match_bytes(maker_note_bytes, 4, "Raw", 3)))) {
                return MakerNoteVendor::PhaseOne;
            }
        }
        // HP MakerNote: fixed-layout blobs that start with "IIII" followed by
        // a type byte (0x04/0x05/0x06) and a NUL (ExifTool: MakerNoteHP4/HP6).
        //
        // Note: some unrelated MakerNotes (including Kodak Type9) also begin
        // with "IIII", so we must not classify all such blobs as Kodak.
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "IIII", 4)
            && u8(maker_note_bytes[5]) == 0
            && (u8(maker_note_bytes[4]) == 0x04
                || u8(maker_note_bytes[4]) == 0x05
                || u8(maker_note_bytes[4]) == 0x06)) {
            return MakerNoteVendor::Hp;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "IIII", 4)) {
            // Kodak Type9 ("IIII...") maker notes are also seen in some
            // HP/Pentax/Minolta-branded files. Use Kodak decoding here to match
            // ExifTool's MakerNoteKodak9 table.
            return MakerNoteVendor::Kodak;
        }
        if (maker_note_bytes.size() >= 3
            && match_bytes(maker_note_bytes, 0, "KDK", 3)) {
            return MakerNoteVendor::Kodak;
        }
        if (maker_note_bytes.size() >= 20
            && match_bytes(maker_note_bytes, 8, "Eastman Kodak", 12)) {
            return MakerNoteVendor::Kodak;
        }
        // Kodak Type2 MakerNotes: two 32-byte ASCII maker/model strings and
        // big-endian image width/height at fixed offsets. These are seen in
        // some non-Kodak-branded files (eg. Minolta Dimage EX).
        if (maker_note_bytes.size() >= 0x74) {
            bool ok_maker = false;
            bool ok_model = false;

            // maker string at 0x08
            bool ascii = true;
            for (uint32_t i = 0; i < 32; ++i) {
                const uint8_t c = u8(maker_note_bytes[0x08 + i]);
                if (c == 0) {
                    break;
                }
                if (c < 0x20 || c > 0x7E) {
                    ascii = false;
                    break;
                }
                ok_maker = true;
            }
            ok_maker = ok_maker && ascii;

            // model string at 0x28
            ascii = true;
            for (uint32_t i = 0; i < 32; ++i) {
                const uint8_t c = u8(maker_note_bytes[0x28 + i]);
                if (c == 0) {
                    break;
                }
                if (c < 0x20 || c > 0x7E) {
                    ascii = false;
                    break;
                }
                ok_model = true;
            }
            ok_model = ok_model && ascii;

            if (ok_maker && ok_model) {
                uint32_t width  = 0;
                uint32_t height = 0;
                if (read_u32be(maker_note_bytes, 0x6c, &width)
                    && read_u32be(maker_note_bytes, 0x70, &height) && width > 0
                    && height > 0 && width <= 200000 && height <= 200000) {
                    return MakerNoteVendor::Kodak;
                }
            }
        }
        // Leica MakerNotes commonly use the Panasonic MakerNote format.
        if (maker_note_bytes.size() >= 8
            && match_bytes(maker_note_bytes, 0, "LEICA\0\0\0", 8)) {
            return MakerNoteVendor::Panasonic;
        }
        if (maker_note_bytes.size() >= 16
            && match_bytes(maker_note_bytes, 0, "LEICA CAMERA AG\0", 16)) {
            return MakerNoteVendor::Panasonic;
        }
        // Some GE models use a FujiFilm-compatible MakerNote with a "GE<...>"
        // prefix and a non-standard base offset.
        static constexpr char kGe2Magic[] = "GE\x0C\0\0\0\x16\0\0\0";
        if (maker_note_bytes.size() >= 10
            && match_bytes(maker_note_bytes, 0, kGe2Magic, 10)) {
            return MakerNoteVendor::Fuji;
        }
        if (maker_note_bytes.size() >= 8
            && match_bytes(maker_note_bytes, 0, "GENERALE", 8)) {
            return MakerNoteVendor::Fuji;
        }
        if (maker_note_bytes.size() >= 9
            && match_bytes(maker_note_bytes, 0, "Panasonic", 9)) {
            return MakerNoteVendor::Panasonic;
        }
        // GE MakerNote: starts with "GE\0\0" or "GENIC\0" (ExifTool: MakerNoteGE).
        // Note: some GE-branded cameras use Make="GEDSC IMAGING CORP.", so
        // detecting by Make string alone is not sufficient.
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "GE\0\0", 4)) {
            return MakerNoteVendor::Ge;
        }
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "GENIC\0", 6)) {
            return MakerNoteVendor::Ge;
        }
        if (maker_note_bytes.size() >= 7
            && match_bytes(maker_note_bytes, 0, "RECONYX", 7)) {
            return MakerNoteVendor::Reconyx;
        }
        if (maker_note_bytes.size() >= 4 && u8(maker_note_bytes[0]) == 0x01
            && u8(maker_note_bytes[1]) == 0xF1
            && (u8(maker_note_bytes[2]) == 0x03
                || u8(maker_note_bytes[2]) == 0x04)
            && u8(maker_note_bytes[3]) == 0x00) {
            return MakerNoteVendor::Reconyx;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "DJI\0", 4)) {
            return MakerNoteVendor::Dji;
        }

        const std::string_view make
            = find_first_exif_ascii_value(store, "ifd0", 0x010F /* Make */);
        const std::string_view model
            = find_first_exif_ascii_value(store, "ifd0", 0x0110 /* Model */);

        // Some modern Kodak/PixPro models use Make="JK Imaging, Ltd." but the
        // MakerNote format is still Kodak.
        if (!model.empty()
            && (ascii_contains_insensitive(model, "kodak")
                || ascii_contains_insensitive(model, "pixpro"))) {
            return MakerNoteVendor::Kodak;
        }

        if (!make.empty()) {
            if (ascii_starts_with_insensitive(make, "Nikon")) {
                return MakerNoteVendor::Nikon;
            }
            if (ascii_starts_with_insensitive(make, "Canon")) {
                return MakerNoteVendor::Canon;
            }
            if (ascii_starts_with_insensitive(make, "Sony")) {
                return MakerNoteVendor::Sony;
            }
            if (ascii_starts_with_insensitive(make, "SIGMA")) {
                return MakerNoteVendor::Sigma;
            }
            if (ascii_starts_with_insensitive(make, "Konica Minolta")
                || ascii_starts_with_insensitive(make, "Minolta")) {
                return MakerNoteVendor::Minolta;
            }
            if (ascii_starts_with_insensitive(make, "FUJIFILM")) {
                return MakerNoteVendor::Fuji;
            }
            if (ascii_starts_with_insensitive(make, "Apple")) {
                return MakerNoteVendor::Apple;
            }
            if (ascii_starts_with_insensitive(make, "OLYMPUS")) {
                return MakerNoteVendor::Olympus;
            }
            if (ascii_starts_with_insensitive(make, "OM Digital")) {
                return MakerNoteVendor::Olympus;
            }
            if (ascii_starts_with_insensitive(make, "PENTAX")) {
                return MakerNoteVendor::Pentax;
            }
            if (ascii_starts_with_insensitive(make, "Asahi")) {
                return MakerNoteVendor::Pentax;
            }
            if (ascii_starts_with_insensitive(make, "CASIO")) {
                return MakerNoteVendor::Casio;
            }
            if (ascii_starts_with_insensitive(make, "Panasonic")) {
                return MakerNoteVendor::Panasonic;
            }
            if (ascii_starts_with_insensitive(make, "Kodak")
                || ascii_starts_with_insensitive(make, "Eastman Kodak")) {
                return MakerNoteVendor::Kodak;
            }
            if (ascii_starts_with_insensitive(make, "FLIR")) {
                return MakerNoteVendor::Flir;
            }
            if (ascii_starts_with_insensitive(make, "RICOH")) {
                return MakerNoteVendor::Ricoh;
            }
            if (ascii_starts_with_insensitive(make, "SAMSUNG")) {
                return MakerNoteVendor::Samsung;
            }
            if (ascii_starts_with_insensitive(make, "JVC")) {
                return MakerNoteVendor::Jvc;
            }
            if (ascii_starts_with_insensitive(make, "DJI")) {
                return MakerNoteVendor::Dji;
            }
            if (ascii_starts_with_insensitive(make, "General Imaging")) {
                return MakerNoteVendor::Ge;
            }
            if (ascii_starts_with_insensitive(make, "Motorola")) {
                return MakerNoteVendor::Motorola;
            }
            if (ascii_starts_with_insensitive(make, "HP")
                || ascii_starts_with_insensitive(make, "hp")
                || ascii_starts_with_insensitive(make, "Hewlett-Packard")
                || ascii_starts_with_insensitive(make, "Hewlett Packard")) {
                return MakerNoteVendor::Hp;
            }
            if (ascii_starts_with_insensitive(make, "Nintendo")) {
                return MakerNoteVendor::Nintendo;
            }
            if (looks_like_phaseone_family(make, model)) {
                return MakerNoteVendor::PhaseOne;
            }
        }

        return MakerNoteVendor::Unknown;
    }


    static void set_makernote_tokens(ExifDecodeOptions* opts,
                                     MakerNoteVendor vendor) noexcept
    {
        if (!opts) {
            return;
        }

        switch (vendor) {
        case MakerNoteVendor::Nikon:
            opts->tokens.ifd_prefix        = "mk_nikon";
            opts->tokens.subifd_prefix     = "mk_nikon_subifd";
            opts->tokens.exif_ifd_token    = "mk_nikon_exififd";
            opts->tokens.gps_ifd_token     = "mk_nikon_gpsifd";
            opts->tokens.interop_ifd_token = "mk_nikon_interopifd";
            return;
        case MakerNoteVendor::Canon:
            opts->tokens.ifd_prefix        = "mk_canon";
            opts->tokens.subifd_prefix     = "mk_canon_subifd";
            opts->tokens.exif_ifd_token    = "mk_canon_exififd";
            opts->tokens.gps_ifd_token     = "mk_canon_gpsifd";
            opts->tokens.interop_ifd_token = "mk_canon_interopifd";
            return;
        case MakerNoteVendor::Sony:
            opts->tokens.ifd_prefix        = "mk_sony";
            opts->tokens.subifd_prefix     = "mk_sony_subifd";
            opts->tokens.exif_ifd_token    = "mk_sony_exififd";
            opts->tokens.gps_ifd_token     = "mk_sony_gpsifd";
            opts->tokens.interop_ifd_token = "mk_sony_interopifd";
            return;
        case MakerNoteVendor::Sigma:
            opts->tokens.ifd_prefix        = "mk_sigma";
            opts->tokens.subifd_prefix     = "mk_sigma_subifd";
            opts->tokens.exif_ifd_token    = "mk_sigma_exififd";
            opts->tokens.gps_ifd_token     = "mk_sigma_gpsifd";
            opts->tokens.interop_ifd_token = "mk_sigma_interopifd";
            return;
        case MakerNoteVendor::Minolta:
            opts->tokens.ifd_prefix        = "mk_minolta";
            opts->tokens.subifd_prefix     = "mk_minolta_subifd";
            opts->tokens.exif_ifd_token    = "mk_minolta_exififd";
            opts->tokens.gps_ifd_token     = "mk_minolta_gpsifd";
            opts->tokens.interop_ifd_token = "mk_minolta_interopifd";
            return;
        case MakerNoteVendor::Fuji:
            opts->tokens.ifd_prefix        = "mk_fuji";
            opts->tokens.subifd_prefix     = "mk_fuji_subifd";
            opts->tokens.exif_ifd_token    = "mk_fuji_exififd";
            opts->tokens.gps_ifd_token     = "mk_fuji_gpsifd";
            opts->tokens.interop_ifd_token = "mk_fuji_interopifd";
            return;
        case MakerNoteVendor::Apple:
            opts->tokens.ifd_prefix        = "mk_apple";
            opts->tokens.subifd_prefix     = "mk_apple_subifd";
            opts->tokens.exif_ifd_token    = "mk_apple_exififd";
            opts->tokens.gps_ifd_token     = "mk_apple_gpsifd";
            opts->tokens.interop_ifd_token = "mk_apple_interopifd";
            return;
        case MakerNoteVendor::Olympus:
            opts->tokens.ifd_prefix        = "mk_olympus";
            opts->tokens.subifd_prefix     = "mk_olympus_subifd";
            opts->tokens.exif_ifd_token    = "mk_olympus_exififd";
            opts->tokens.gps_ifd_token     = "mk_olympus_gpsifd";
            opts->tokens.interop_ifd_token = "mk_olympus_interopifd";
            return;
        case MakerNoteVendor::Pentax:
            opts->tokens.ifd_prefix        = "mk_pentax";
            opts->tokens.subifd_prefix     = "mk_pentax_subifd";
            opts->tokens.exif_ifd_token    = "mk_pentax_exififd";
            opts->tokens.gps_ifd_token     = "mk_pentax_gpsifd";
            opts->tokens.interop_ifd_token = "mk_pentax_interopifd";
            return;
        case MakerNoteVendor::Casio:
            // Casio MakerNote "type2" uses a non-TIFF header ("QVC\0") and
            // a big-endian directory.
            opts->tokens.ifd_prefix        = "mk_casio_type2_";
            opts->tokens.subifd_prefix     = "mk_casio_subifd_";
            opts->tokens.exif_ifd_token    = "mk_casio_exififd";
            opts->tokens.gps_ifd_token     = "mk_casio_gpsifd";
            opts->tokens.interop_ifd_token = "mk_casio_interopifd";
            return;
        case MakerNoteVendor::Panasonic:
            opts->tokens.ifd_prefix        = "mk_panasonic";
            opts->tokens.subifd_prefix     = "mk_panasonic_subifd";
            opts->tokens.exif_ifd_token    = "mk_panasonic_exififd";
            opts->tokens.gps_ifd_token     = "mk_panasonic_gpsifd";
            opts->tokens.interop_ifd_token = "mk_panasonic_interopifd";
            return;
        case MakerNoteVendor::Kodak:
            opts->tokens.ifd_prefix        = "mk_kodak";
            opts->tokens.subifd_prefix     = "mk_kodak_subifd";
            opts->tokens.exif_ifd_token    = "mk_kodak_exififd";
            opts->tokens.gps_ifd_token     = "mk_kodak_gpsifd";
            opts->tokens.interop_ifd_token = "mk_kodak_interopifd";
            return;
        case MakerNoteVendor::Flir:
            opts->tokens.ifd_prefix        = "mk_flir";
            opts->tokens.subifd_prefix     = "mk_flir_subifd";
            opts->tokens.exif_ifd_token    = "mk_flir_exififd";
            opts->tokens.gps_ifd_token     = "mk_flir_gpsifd";
            opts->tokens.interop_ifd_token = "mk_flir_interopifd";
            return;
        case MakerNoteVendor::Ricoh:
            opts->tokens.ifd_prefix        = "mk_ricoh";
            opts->tokens.subifd_prefix     = "mk_ricoh_subifd";
            opts->tokens.exif_ifd_token    = "mk_ricoh_exififd";
            opts->tokens.gps_ifd_token     = "mk_ricoh_gpsifd";
            opts->tokens.interop_ifd_token = "mk_ricoh_interopifd";
            return;
        case MakerNoteVendor::Samsung:
            opts->tokens.ifd_prefix        = "mk_samsung";
            opts->tokens.subifd_prefix     = "mk_samsung_subifd";
            opts->tokens.exif_ifd_token    = "mk_samsung_exififd";
            opts->tokens.gps_ifd_token     = "mk_samsung_gpsifd";
            opts->tokens.interop_ifd_token = "mk_samsung_interopifd";
            return;
        case MakerNoteVendor::Jvc:
            opts->tokens.ifd_prefix        = "mk_jvc";
            opts->tokens.subifd_prefix     = "mk_jvc_subifd";
            opts->tokens.exif_ifd_token    = "mk_jvc_exififd";
            opts->tokens.gps_ifd_token     = "mk_jvc_gpsifd";
            opts->tokens.interop_ifd_token = "mk_jvc_interopifd";
            return;
        case MakerNoteVendor::Dji:
            opts->tokens.ifd_prefix        = "mk_dji";
            opts->tokens.subifd_prefix     = "mk_dji_subifd";
            opts->tokens.exif_ifd_token    = "mk_dji_exififd";
            opts->tokens.gps_ifd_token     = "mk_dji_gpsifd";
            opts->tokens.interop_ifd_token = "mk_dji_interopifd";
            return;
        case MakerNoteVendor::Ge:
            opts->tokens.ifd_prefix        = "mk_ge";
            opts->tokens.subifd_prefix     = "mk_ge_subifd";
            opts->tokens.exif_ifd_token    = "mk_ge_exififd";
            opts->tokens.gps_ifd_token     = "mk_ge_gpsifd";
            opts->tokens.interop_ifd_token = "mk_ge_interopifd";
            return;
        case MakerNoteVendor::Motorola:
            opts->tokens.ifd_prefix        = "mk_motorola";
            opts->tokens.subifd_prefix     = "mk_motorola_subifd";
            opts->tokens.exif_ifd_token    = "mk_motorola_exififd";
            opts->tokens.gps_ifd_token     = "mk_motorola_gpsifd";
            opts->tokens.interop_ifd_token = "mk_motorola_interopifd";
            return;
        case MakerNoteVendor::Reconyx:
            opts->tokens.ifd_prefix        = "mk_reconyx";
            opts->tokens.subifd_prefix     = "mk_reconyx_subifd";
            opts->tokens.exif_ifd_token    = "mk_reconyx_exififd";
            opts->tokens.gps_ifd_token     = "mk_reconyx_gpsifd";
            opts->tokens.interop_ifd_token = "mk_reconyx_interopifd";
            return;
        case MakerNoteVendor::Hp:
            opts->tokens.ifd_prefix        = "mk_hp";
            opts->tokens.subifd_prefix     = "mk_hp_subifd";
            opts->tokens.exif_ifd_token    = "mk_hp_exififd";
            opts->tokens.gps_ifd_token     = "mk_hp_gpsifd";
            opts->tokens.interop_ifd_token = "mk_hp_interopifd";
            return;
        case MakerNoteVendor::Nintendo:
            opts->tokens.ifd_prefix        = "mk_nintendo";
            opts->tokens.subifd_prefix     = "mk_nintendo_subifd";
            opts->tokens.exif_ifd_token    = "mk_nintendo_exififd";
            opts->tokens.gps_ifd_token     = "mk_nintendo_gpsifd";
            opts->tokens.interop_ifd_token = "mk_nintendo_interopifd";
            return;
        case MakerNoteVendor::PhaseOne:
            opts->tokens.ifd_prefix        = "mk_phaseone";
            opts->tokens.subifd_prefix     = "mk_phaseone_subifd";
            opts->tokens.exif_ifd_token    = "mk_phaseone_exififd";
            opts->tokens.gps_ifd_token     = "mk_phaseone_gpsifd";
            opts->tokens.interop_ifd_token = "mk_phaseone_interopifd";
            return;
        case MakerNoteVendor::Unknown: break;
        }

        opts->tokens.ifd_prefix        = "mkifd";
        opts->tokens.subifd_prefix     = "mk_subifd";
        opts->tokens.exif_ifd_token    = "mk_exififd";
        opts->tokens.gps_ifd_token     = "mk_gpsifd";
        opts->tokens.interop_ifd_token = "mk_interopifd";
    }


    static uint64_t tiff_type_size(uint16_t type) noexcept;

    static void update_status(ExifDecodeResult* out,
                              ExifDecodeStatus status) noexcept;

    static void mark_limit_exceeded(ExifDecodeResult* out,
                                    ExifLimitReason reason, uint64_t ifd_offset,
                                    uint16_t tag) noexcept;

    static MetaValue decode_tiff_value(const TiffConfig& cfg,
                                       std::span<const std::byte> bytes,
                                       uint16_t type, uint64_t count,
                                       uint64_t value_off, uint64_t value_bytes,
                                       ByteArena& arena,
                                       const ExifDecodeLimits& limits,
                                       ExifDecodeResult* result) noexcept;

    static bool score_classic_ifd_candidate(const TiffConfig& cfg,
                                            std::span<const std::byte> bytes,
                                            uint64_t ifd_off,
                                            const ExifDecodeLimits& limits,
                                            ClassicIfdCandidate* out) noexcept
    {
        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, bytes, ifd_off, &entry_count)) {
            return false;
        }
        if (entry_count == 0 || entry_count > limits.max_entries_per_ifd) {
            return false;
        }
        // Heuristic scan cap: avoid quadratic work across many candidate offsets.
        if (entry_count > 512) {
            return false;
        }

        uint64_t entries_off = 0;
        if (!checked_add_u64(ifd_off, 2U, &entries_off)) {
            return false;
        }
        uint64_t table_bytes = 0;
        if (!checked_mul_u64(uint64_t(entry_count), 12ULL, &table_bytes)) {
            return false;
        }
        uint64_t table_end = 0;
        if (!checked_add_u64(entries_off, table_bytes, &table_end)) {
            return false;
        }
        uint64_t needed = 0;
        if (!checked_add_u64(table_end, 4ULL, &needed)
            || needed > bytes.size()) {
            return false;
        }

        uint32_t valid = 0;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            uint16_t type = 0;
            if (!read_tiff_u16(cfg, bytes, eoff + 2, &type)) {
                break;
            }

            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_tiff_u32(cfg, bytes, eoff + 4, &count32)
                || !read_tiff_u32(cfg, bytes, eoff + 8, &value_or_off32)) {
                break;
            }

            const uint64_t unit = tiff_type_size(type);
            if (unit == 0) {
                continue;
            }
            const uint64_t count = count32;
            if (count > (UINT64_MAX / unit)) {
                continue;
            }

            uint64_t value_bytes = 0;
            if (!checked_mul_u64(count, unit, &value_bytes)) {
                continue;
            }
            if (value_bytes > limits.max_value_bytes) {
                continue;
            }

            const uint64_t inline_cap      = 4;
            const uint64_t value_field_off = eoff + 8;
            const uint64_t value_off       = (value_bytes <= inline_cap)
                                                 ? value_field_off
                                                 : value_or_off32;

            if (!span_contains_bytes(bytes, value_off, value_bytes)) {
                continue;
            }
            valid += 1;
        }

        if (valid == 0) {
            return false;
        }
        const uint32_t min_valid = (entry_count > 4)
                                       ? (uint32_t(entry_count) / 2U)
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


    static bool find_best_classic_ifd_candidate(
        std::span<const std::byte> bytes, uint64_t max_scan_off,
        const ExifDecodeLimits& limits, ClassicIfdCandidate* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out       = ClassicIfdCandidate {};
        bool found = false;

        const uint64_t scan_cap = (max_scan_off < bytes.size()) ? max_scan_off
                                                                : bytes.size();

        for (uint64_t off = 0; off + 2 <= scan_cap; off += 2) {
            for (int endian = 0; endian < 2; ++endian) {
                TiffConfig cfg;
                cfg.le      = (endian == 0);
                cfg.bigtiff = false;
                ClassicIfdCandidate cand;
                if (!score_classic_ifd_candidate(cfg, bytes, off, limits,
                                                 &cand)) {
                    continue;
                }

                if (!found || cand.valid_entries > out->valid_entries
                    || (cand.valid_entries == out->valid_entries
                        && cand.offset < out->offset)) {
                    *out  = cand;
                    found = true;
                }
            }
        }

        return found;
    }


    static bool looks_like_classic_ifd(const TiffConfig& cfg,
                                       std::span<const std::byte> bytes,
                                       uint64_t ifd_off,
                                       const ExifDecodeLimits& limits) noexcept
    {
        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, bytes, ifd_off, &entry_count)) {
            return false;
        }
        if (entry_count == 0 || entry_count > limits.max_entries_per_ifd) {
            return false;
        }
        uint64_t entries_off = 0;
        if (!checked_add_u64(ifd_off, 2U, &entries_off)) {
            return false;
        }
        uint64_t table_bytes = 0;
        if (!checked_mul_u64(uint64_t(entry_count), 12ULL, &table_bytes)) {
            return false;
        }
        uint64_t table_end = 0;
        if (!checked_add_u64(entries_off, table_bytes, &table_end)) {
            return false;
        }
        uint64_t needed = 0;
        if (!checked_add_u64(table_end, 4ULL, &needed)) {
            return false;
        }
        return needed <= bytes.size();
    }


    static void decode_classic_ifd_no_header(
        const TiffConfig& cfg, std::span<const std::byte> bytes,
        uint64_t ifd_off, std::string_view ifd_name, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out,
        EntryFlags extra_flags) noexcept
    {
        if (ifd_name.empty()) {
            return;
        }
        if (!looks_like_classic_ifd(cfg, bytes, ifd_off, options.limits)) {
            return;
        }

        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, bytes, ifd_off, &entry_count)) {
            return;
        }
        uint64_t entries_off = 0;
        if (!checked_add_u64(ifd_off, 2U, &entries_off)) {
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        for (uint32_t i = 0; i < entry_count; ++i) {
            uint64_t entry_delta = 0;
            uint64_t eoff        = 0;
            if (!checked_mul_u64(uint64_t(i), 12ULL, &entry_delta)
                || !checked_add_u64(entries_off, entry_delta, &eoff)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }

            uint16_t tag  = 0;
            uint16_t type = 0;
            if (!read_tiff_u16(cfg, bytes, eoff + 0, &tag)
                || !read_tiff_u16(cfg, bytes, eoff + 2, &type)) {
                return;
            }

            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_tiff_u32(cfg, bytes, eoff + 4, &count32)
                || !read_tiff_u32(cfg, bytes, eoff + 8, &value_or_off32)) {
                return;
            }
            const uint64_t count = count32;

            const uint64_t unit = tiff_type_size(type);
            if (unit == 0) {
                continue;
            }
            if (count > (UINT64_MAX / unit)) {
                continue;
            }
            uint64_t value_bytes = 0;
            if (!checked_mul_u64(count, unit, &value_bytes)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                continue;
            }

            const uint64_t inline_cap      = 4;
            const uint64_t value_field_off = eoff + 8;
            const uint64_t value_off       = (value_bytes <= inline_cap)
                                                 ? value_field_off
                                                 : value_or_off32;

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                mark_limit_exceeded(status_out,
                                    ExifLimitReason::MaxTotalEntries, ifd_off,
                                    0);
                return;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);

            if (value_bytes > options.limits.max_value_bytes) {
                entry.flags |= EntryFlags::Truncated;
            } else if (!span_contains_bytes(bytes, value_off, value_bytes)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                entry.flags |= EntryFlags::Unreadable;
            } else {
                entry.value = decode_tiff_value(cfg, bytes, type, count,
                                                value_off, value_bytes,
                                                store.arena(), options.limits,
                                                status_out);
            }

            entry.flags |= extra_flags;
            maybe_mark_contextual_name(ifd_name, tag, store, &entry);

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }


    static std::string_view
    make_mk_subtable_ifd_token(std::string_view vendor_prefix,
                               std::string_view subtable, uint32_t index,
                               std::span<char> scratch) noexcept
    {
        if (vendor_prefix.empty() || subtable.empty() || scratch.empty()) {
            return {};
        }

        static constexpr size_t kMaxIndexDigits = 11;
        const uint64_t min_needed = uint64_t(vendor_prefix.size()) + 1U
                                    + uint64_t(subtable.size()) + 1U
                                    + kMaxIndexDigits;
        if (min_needed > scratch.size()) {
            return {};
        }

        size_t n = 0;
        for (size_t i = 0; i < vendor_prefix.size(); ++i) {
            scratch[n++] = vendor_prefix[i];
        }
        scratch[n++] = '_';
        for (size_t i = 0; i < subtable.size(); ++i) {
            scratch[n++] = subtable[i];
        }
        scratch[n++] = '_';

        // Decimal index suffix (at least one digit).
        char tmp[kMaxIndexDigits];
        size_t t   = 0;
        uint32_t v = index;
        do {
            tmp[t++] = static_cast<char>('0' + (v % 10U));
            v /= 10U;
        } while (v != 0U && t < kMaxIndexDigits);
        while (t > 0) {
            scratch[n++] = tmp[--t];
        }

        return std::string_view(scratch.data(), n);
    }

    static bool read_u16_endian(bool le, std::span<const std::byte> bytes,
                                uint64_t offset, uint16_t* out) noexcept
    {
        return le ? read_u16le(bytes, offset, out)
                  : read_u16be(bytes, offset, out);
    }


    static bool read_i16_endian(bool le, std::span<const std::byte> bytes,
                                uint64_t offset, int16_t* out) noexcept
    {
        uint16_t raw = 0;
        if (!read_u16_endian(le, bytes, offset, &raw)) {
            return false;
        }
        *out = static_cast<int16_t>(raw);
        return true;
    }


    static MetaValue
    make_fixed_ascii_text(ByteArena& arena,
                          std::span<const std::byte> raw) noexcept
    {
        size_t trimmed = 0;
        while (trimmed < raw.size() && raw[trimmed] != std::byte { 0 }) {
            trimmed += 1;
        }
        const std::span<const std::byte> payload = raw.subspan(0, trimmed);
        const std::string_view text(reinterpret_cast<const char*>(
                                        payload.data()),
                                    payload.size());
        return make_text(arena, text, TextEncoding::Ascii);
    }


    static void emit_bin_dir_entries(std::string_view ifd_name,
                                     MetaStore& store,
                                     std::span<const uint16_t> tags,
                                     std::span<const MetaValue> values,
                                     const ExifDecodeLimits& limits,
                                     ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || tags.size() != values.size()) {
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        for (uint32_t i = 0; i < tags.size(); ++i) {
            if (status_out
                && (status_out->entries_decoded + 1U)
                       > limits.max_total_entries) {
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
            maybe_mark_contextual_name(ifd_name, tags[i], store, &entry);

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }


    static bool phaseone_read_u32(bool le, std::span<const std::byte> bytes,
                                  uint64_t offset, uint32_t* out) noexcept
    {
        return le ? read_u32le(bytes, offset, out)
                  : read_u32be(bytes, offset, out);
    }


    static bool looks_like_phaseone_main_dir(std::span<const std::byte> bytes,
                                             uint64_t dir_start,
                                             bool* le_out) noexcept
    {
        if (!span_contains_bytes(bytes, dir_start, 12U) || !le_out) {
            return false;
        }
        if (match_bytes(bytes, dir_start, "IIII", 4)
            && match_bytes(bytes, dir_start + 5U, "waR", 3)) {
            *le_out = true;
            return true;
        }
        if (match_bytes(bytes, dir_start, "MMMM", 4)
            && match_bytes(bytes, dir_start + 4U, "Raw", 3)) {
            *le_out = false;
            return true;
        }
        return false;
    }


    static bool
    looks_like_phaseone_sensorcal_dir(std::span<const std::byte> bytes,
                                      uint64_t dir_start, bool* le_out) noexcept
    {
        if (!span_contains_bytes(bytes, dir_start, 12U) || !le_out) {
            return false;
        }
        if (match_bytes(bytes, dir_start, "IIII\x01\x00\x00\x00", 8)) {
            *le_out = true;
            return true;
        }
        if (match_bytes(bytes, dir_start, "MMMM\x00\x00\x00\x01", 8)) {
            *le_out = false;
            return true;
        }
        return false;
    }


    static MetaValue decode_phaseone_value(bool le,
                                           std::span<const std::byte> bytes,
                                           uint64_t value_off, uint32_t size,
                                           uint32_t format_size,
                                           ByteArena& arena) noexcept
    {
        if (!span_contains_bytes(bytes, value_off, size)) {
            return MetaValue {};
        }
        const std::span<const std::byte> raw
            = bytes.subspan(static_cast<size_t>(value_off),
                            static_cast<size_t>(size));
        if (format_size == 1U) {
            return make_fixed_ascii_text(arena, raw);
        }
        if (size == 1U) {
            return make_u8(u8(raw[0]));
        }
        if (size == 2U) {
            uint16_t v = 0;
            if (read_u16_endian(le, bytes, value_off, &v)) {
                if (format_size == 2U) {
                    return make_i16(static_cast<int16_t>(v));
                }
                return make_u16(v);
            }
        }
        if (size == 4U) {
            uint32_t v = 0;
            if (phaseone_read_u32(le, bytes, value_off, &v)) {
                return make_u32(v);
            }
        }
        return make_bytes(arena, raw);
    }


    static bool decode_phaseone_ifd(std::span<const std::byte> bytes,
                                    uint64_t dir_start, uint64_t dir_len,
                                    uint32_t entry_size,
                                    std::string_view ifd_name, MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || dir_len < 12U) {
            return false;
        }

        bool le = true;
        if (entry_size == 16U) {
            if (!looks_like_phaseone_main_dir(bytes, dir_start, &le)) {
                return false;
            }
        } else if (entry_size == 12U) {
            if (!looks_like_phaseone_sensorcal_dir(bytes, dir_start, &le)) {
                return false;
            }
        } else {
            return false;
        }

        uint32_t ifd_start = 0;
        if (!phaseone_read_u32(le, bytes, dir_start + 8U, &ifd_start)) {
            return false;
        }
        if (uint64_t(ifd_start) + 8U > dir_len) {
            return false;
        }

        const uint64_t count_off = dir_start + uint64_t(ifd_start);
        uint32_t entry_count     = 0;
        if (!phaseone_read_u32(le, bytes, count_off, &entry_count)) {
            return false;
        }
        if (entry_count < 1U || entry_count > 300U) {
            return false;
        }

        uint64_t table_bytes = 0;
        if (!checked_mul_u64(uint64_t(entry_count), uint64_t(entry_size),
                             &table_bytes)) {
            return false;
        }
        uint64_t ifd_end = 0;
        if (!checked_add_u64(uint64_t(ifd_start), 8U + table_bytes, &ifd_end)
            || ifd_end > dir_len) {
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return false;
        }

        for (uint32_t i = 0; i < entry_count; ++i) {
            uint64_t entry_off = 0;
            if (!checked_add_u64(count_off + 8U,
                                 uint64_t(i) * uint64_t(entry_size),
                                 &entry_off)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return false;
            }

            uint32_t tag32 = 0;
            if (!phaseone_read_u32(le, bytes, entry_off, &tag32)) {
                return false;
            }
            if (tag32 > 0xFFFFU) {
                continue;
            }
            const uint16_t tag = static_cast<uint16_t>(tag32);

            uint32_t format_size = 4;
            if (entry_size == 16U
                && !phaseone_read_u32(le, bytes, entry_off + 4U, &format_size)) {
                return false;
            }
            if (format_size != 1U && format_size != 2U && format_size != 4U) {
                format_size = 1U;
            }

            const uint64_t size_off = entry_off + uint64_t(entry_size) - 8U;
            uint32_t size           = 0;
            if (!phaseone_read_u32(le, bytes, size_off, &size)) {
                return false;
            }

            const uint64_t value_ptr_off = entry_off + uint64_t(entry_size)
                                           - 4U;
            uint64_t value_off = value_ptr_off;
            if (size > 4U) {
                uint32_t rel = 0;
                if (!phaseone_read_u32(le, bytes, value_ptr_off, &rel)) {
                    return false;
                }
                value_off = dir_start + uint64_t(rel);
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Other,
                                                uint16_t(format_size) };
            entry.origin.wire_count = (format_size != 0U) ? (size / format_size)
                                                          : size;

            uint64_t value_end = 0;
            const bool readable
                = checked_add_u64(value_off, uint64_t(size), &value_end)
                  && value_end <= (dir_start + dir_len)
                  && span_contains_bytes(bytes, value_off, size);

            if (size > options.limits.max_value_bytes) {
                entry.flags |= EntryFlags::Truncated;
            } else if (!readable) {
                entry.flags |= EntryFlags::Unreadable;
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
            } else if (size != 0U) {
                entry.value = decode_phaseone_value(le, bytes, value_off, size,
                                                    format_size, store.arena());
            }

            maybe_mark_contextual_name(ifd_name, tag, store, &entry);
            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1U;
            }

            if (entry_size == 16U && tag == 0x0110U && size != 0U && readable) {
                char scratch[64];
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token("mk_phaseone",
                                                 "sensorcalibration", 0,
                                                 std::span<char>(scratch));
                if (!sub_ifd.empty()) {
                    (void)decode_phaseone_ifd(bytes, value_off, uint64_t(size),
                                              12U, sub_ifd, store, options,
                                              status_out);
                }
            }
        }

        return true;
    }


    static bool decode_phaseone_makernote(std::span<const std::byte> mn,
                                          std::string_view mk_ifd0,
                                          MetaStore& store,
                                          const ExifDecodeOptions& options,
                                          ExifDecodeResult* status_out) noexcept
    {
        return decode_phaseone_ifd(mn, 0, uint64_t(mn.size()), 16U, mk_ifd0,
                                   store, options, status_out);
    }


    static const Entry* find_first_exif_entry(const MetaStore& store,
                                              std::string_view ifd_name,
                                              uint16_t tag) noexcept
    {
        const ByteArena& arena               = store.arena();
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (entry.key.data.exif_tag.tag != tag) {
                continue;
            }
            if (arena_string(arena, entry.key.data.exif_tag.ifd) != ifd_name) {
                continue;
            }
            return &entry;
        }
        return nullptr;
    }


    static bool
    decode_nikon_distortioninfo_block(std::span<const std::byte> raw, bool le,
                                      uint32_t index, MetaStore& store,
                                      const ExifDecodeLimits& limits,
                                      ExifDecodeResult* status_out) noexcept
    {
        if (raw.size() < 5U) {
            return false;
        }

        char ifd_buf[96];
        const std::string_view ifd_name
            = exif_internal::make_mk_subtable_ifd_token(
                "mk_nikon", "distortioninfo", index, std::span<char>(ifd_buf));
        if (ifd_name.empty()) {
            return false;
        }

        uint16_t tags_out[5];
        MetaValue vals_out[5];
        uint32_t out_count = 0;

        tags_out[out_count] = 0x0000;
        vals_out[out_count]
            = exif_internal::make_fixed_ascii_text(store.arena(), raw.first(4));
        out_count += 1;

        tags_out[out_count] = 0x0004;
        vals_out[out_count] = make_u8(u8(raw[4]));
        out_count += 1;

        static constexpr uint16_t kCoeffTags[] = { 0x0014, 0x001C, 0x0024 };
        for (size_t i = 0; i < sizeof(kCoeffTags) / sizeof(kCoeffTags[0]);
             ++i) {
            const uint16_t tag = kCoeffTags[i];
            if (static_cast<uint64_t>(tag) + 8U > raw.size()) {
                continue;
            }

            int32_t numer = 0;
            int32_t denom = 0;
            if (!read_i32_endian(le, raw, static_cast<uint64_t>(tag), &numer)
                || !read_i32_endian(le, raw, static_cast<uint64_t>(tag) + 4U,
                                    &denom)) {
                continue;
            }

            tags_out[out_count] = tag;
            vals_out[out_count] = make_srational(numer, denom);
            out_count += 1;
        }

        exif_internal::emit_bin_dir_entries(
            ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
            std::span<const MetaValue>(vals_out, out_count), limits,
            status_out);
        return true;
    }

    static bool
    decode_nikon_vignetteinfo_block(std::span<const std::byte> raw, bool le,
                                    uint32_t index, MetaStore& store,
                                    const ExifDecodeLimits& limits,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (raw.size() < 4U) {
            return false;
        }

        char ifd_buf[96];
        const std::string_view ifd_name
            = exif_internal::make_mk_subtable_ifd_token(
                "mk_nikon", "vignetteinfo", index, std::span<char>(ifd_buf));
        if (ifd_name.empty()) {
            return false;
        }

        uint16_t tags_out[4];
        MetaValue vals_out[4];
        uint32_t out_count = 0;

        tags_out[out_count] = 0x0000;
        vals_out[out_count]
            = exif_internal::make_fixed_ascii_text(store.arena(), raw.first(4));
        out_count += 1;

        static constexpr uint16_t kCoeffTags[] = { 0x0024, 0x0034, 0x0044 };
        for (size_t i = 0; i < sizeof(kCoeffTags) / sizeof(kCoeffTags[0]);
             ++i) {
            const uint16_t tag = kCoeffTags[i];
            if (static_cast<uint64_t>(tag) + 8U > raw.size()) {
                continue;
            }

            int32_t numer = 0;
            int32_t denom = 0;
            if (!read_i32_endian(le, raw, static_cast<uint64_t>(tag), &numer)
                || !read_i32_endian(le, raw, static_cast<uint64_t>(tag) + 4U,
                                    &denom)) {
                continue;
            }

            tags_out[out_count] = tag;
            vals_out[out_count] = make_srational(numer, denom);
            out_count += 1;
        }

        exif_internal::emit_bin_dir_entries(
            ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
            std::span<const MetaValue>(vals_out, out_count), limits,
            status_out);
        return true;
    }


    static void
    maybe_decode_nikon_nefinfo_blocks(MetaStore& store,
                                      const ExifDecodeOptions& options,
                                      ExifDecodeResult* status_out) noexcept
    {
        if (!options.decode_makernote) {
            return;
        }

        const std::string_view make = find_first_exif_ascii_value(store, "ifd0",
                                                                  0x010F);
        if (!ascii_starts_with_insensitive(make, "Nikon")) {
            return;
        }

        exif_internal::ExifContext ctx(store);
        static constexpr std::string_view kNefInfoIfds[] = {
            "subifd1",
            "subifd0",
            "subifd2",
        };

        uint32_t idx_nefinfo        = 0;
        uint32_t idx_distortioninfo = 0;
        uint32_t idx_vignetteinfo   = 0;
        for (size_t i = 0; i < sizeof(kNefInfoIfds) / sizeof(kNefInfoIfds[0]);
             ++i) {
            MetaValue nefinfo;
            if (!ctx.find_first_value(kNefInfoIfds[i], 0xC7D5, &nefinfo)
                || nefinfo.kind != MetaValueKind::Bytes) {
                continue;
            }

            const std::span<const std::byte> raw = store.arena().span(
                nefinfo.data.span);
            const uint64_t hdr_off = find_embedded_tiff_header(raw, 64U);
            if (hdr_off == UINT64_MAX || hdr_off >= raw.size()) {
                continue;
            }

            const std::span<const std::byte> tiff_bytes = raw.subspan(
                static_cast<size_t>(hdr_off));
            if (tiff_bytes.size() < 8U) {
                continue;
            }

            TiffConfig cfg;
            cfg.bigtiff = false;
            cfg.le = (u8(tiff_bytes[0]) == 'I' && u8(tiff_bytes[1]) == 'I');

            uint32_t ifd_off = 0;
            if (!read_tiff_u32(cfg, tiff_bytes, 4U, &ifd_off)
                || ifd_off >= tiff_bytes.size()) {
                continue;
            }

            uint16_t entry_count = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, ifd_off, &entry_count)) {
                continue;
            }

            char nefinfo_ifd_buf[96];
            const std::string_view nefinfo_ifd
                = exif_internal::make_mk_subtable_ifd_token(
                    "mk_nikon", "nefinfo", idx_nefinfo++,
                    std::span<char>(nefinfo_ifd_buf));
            if (!nefinfo_ifd.empty()) {
                decode_classic_ifd_no_header(cfg, tiff_bytes, ifd_off,
                                             nefinfo_ifd, store, options,
                                             status_out, EntryFlags::Derived);
            }

            exif_internal::MakerNoteLayout layout;
            layout.cfg   = cfg;
            layout.bytes = tiff_bytes;

            for (uint16_t entry_idx = 0; entry_idx < entry_count; ++entry_idx) {
                const uint64_t entry_off = uint64_t(ifd_off) + 2U
                                           + uint64_t(entry_idx) * 12U;
                exif_internal::ClassicIfdEntry entry;
                if (!exif_internal::read_classic_ifd_entry(cfg, tiff_bytes,
                                                           entry_off, &entry)
                    || entry.type != 7U) {
                    continue;
                }

                exif_internal::ClassicIfdValueRef ref;
                if (!exif_internal::resolve_classic_ifd_value_ref(
                        layout, entry_off, entry, &ref, status_out)
                    || !span_contains_bytes(tiff_bytes, ref.value_off,
                                            ref.value_bytes)) {
                    continue;
                }

                const std::span<const std::byte> block
                    = tiff_bytes.subspan(static_cast<size_t>(ref.value_off),
                                         static_cast<size_t>(ref.value_bytes));
                if (entry.tag == 0x0005U) {
                    (void)decode_nikon_distortioninfo_block(
                        block, cfg.le, idx_distortioninfo++, store,
                        options.limits, status_out);
                    continue;
                }
                if (entry.tag == 0x0006U) {
                    (void)decode_nikon_vignetteinfo_block(block, cfg.le,
                                                          idx_vignetteinfo++,
                                                          store, options.limits,
                                                          status_out);
                    continue;
                }
            }
        }
    }


    static bool sigma_copy_f32_triplet(const ByteArena& arena,
                                       const MetaValue& value, uint32_t start,
                                       std::array<uint32_t, 3>* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Array
            || value.elem_type != MetaElementType::F32 || value.count < 3U
            || start > (value.count - 3U)) {
            return false;
        }

        const std::span<const std::byte> raw = arena.span(value.data.span);
        const uint64_t start_byte            = uint64_t(start) * 4U;
        const uint64_t need_bytes            = start_byte + 12U;
        if (need_bytes > raw.size()) {
            return false;
        }

        for (uint32_t i = 0; i < 3U; ++i) {
            uint32_t bits = 0U;
            std::memcpy(&bits,
                        raw.data()
                            + static_cast<size_t>(start_byte + uint64_t(i) * 4U),
                        sizeof(bits));
            (*out)[i] = bits;
        }
        return true;
    }


    static void
    decode_sigma_binary_subdirs(std::string_view mk_ifd0, MetaStore& store,
                                const ExifDecodeLimits& limits,
                                ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return;
        }
        if (store_has_exif_tag(store, "mk_sigma_wbsettings_0", 0x0000U)
            || store_has_exif_tag(store, "mk_sigma_wbsettings2_0", 0x0000U)) {
            return;
        }

        struct SigmaSubdirSpec final {
            uint16_t source_tag;
            const char* subtable;
        };
        static constexpr SigmaSubdirSpec kSpecs[] = {
            { 0x0120U, "wbsettings" },
            { 0x0121U, "wbsettings2" },
        };
        static constexpr uint16_t kSigmaWbTags[] = {
            0x0000U, 0x0003U, 0x0006U, 0x0009U, 0x000CU,
            0x000FU, 0x0012U, 0x0015U, 0x0018U, 0x001BU,
        };

        char ifd_buf[64];
        for (size_t spec_i = 0; spec_i < (sizeof(kSpecs) / sizeof(kSpecs[0]));
             ++spec_i) {
            const Entry* source
                = find_first_exif_entry(store, mk_ifd0,
                                        kSpecs[spec_i].source_tag);
            if (!source) {
                continue;
            }

            std::array<MetaValue, 10> values {};
            bool ok = true;
            for (uint32_t i = 0; i < 10U; ++i) {
                std::array<uint32_t, 3> bits {};
                if (!sigma_copy_f32_triplet(store.arena(), source->value,
                                            i * 3U, &bits)) {
                    ok = false;
                    break;
                }
                values[i] = make_f32_bits_array(
                    store.arena(),
                    std::span<const uint32_t>(bits.data(), bits.size()));
            }
            if (!ok) {
                continue;
            }

            const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                "mk_sigma", kSpecs[spec_i].subtable, 0U,
                std::span<char>(ifd_buf, sizeof(ifd_buf)));
            if (sub_ifd.empty()) {
                continue;
            }
            emit_bin_dir_entries(
                sub_ifd, store,
                std::span<const uint16_t>(kSigmaWbTags,
                                          sizeof(kSigmaWbTags)
                                              / sizeof(kSigmaWbTags[0])),
                std::span<const MetaValue>(values.data(), values.size()),
                limits, status_out);
        }
    }
    static uint64_t tiff_type_size(uint16_t type) noexcept
    {
        switch (type) {
        case 1:    // BYTE
        case 2:    // ASCII
        case 6:    // SBYTE
        case 7:    // UNDEFINED
        case 129:  // UTF-8 (EXIF)
            return 1;
        case 3:  // SHORT
        case 8:  // SSHORT
            return 2;
        case 4:   // LONG
        case 9:   // SLONG
        case 11:  // FLOAT
        case 13:  // IFD
            return 4;
        case 5:   // RATIONAL
        case 10:  // SRATIONAL
        case 12:  // DOUBLE
            return 8;
        case 16:  // LONG8
        case 17:  // SLONG8
        case 18:  // IFD8
            return 8;
        default: return 0;
        }
    }


    static bool contains_nul(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == std::byte { 0 }) {
                return true;
            }
        }
        return false;
    }


    static uint32_t write_u32_decimal(char* out, uint32_t value) noexcept
    {
        char tmp[16];
        uint32_t tmp_len = 0;
        do {
            const uint32_t digit = value % 10U;
            tmp[tmp_len]         = static_cast<char>('0' + digit);
            tmp_len += 1;
            value /= 10U;
        } while (value != 0U && tmp_len < sizeof(tmp));

        for (uint32_t i = 0; i < tmp_len; ++i) {
            out[i] = tmp[tmp_len - 1 - i];
        }
        return tmp_len;
    }


    static std::string_view ifd_token(const ExifIfdTokenPolicy& tokens,
                                      ExifIfdKind kind, uint32_t index,
                                      std::span<char> scratch) noexcept
    {
        switch (kind) {
        case ExifIfdKind::Ifd: {
            const std::string_view prefix = tokens.ifd_prefix;
            if (prefix.empty()) {
                return std::string_view();
            }
            if (scratch.size() < prefix.size() + 16U) {
                return std::string_view();
            }
            std::memcpy(scratch.data(), prefix.data(), prefix.size());
            const uint32_t digits = write_u32_decimal(
                scratch.data() + static_cast<uint32_t>(prefix.size()), index);
            return std::string_view(scratch.data(), prefix.size() + digits);
        }
        case ExifIfdKind::ExifIfd: return tokens.exif_ifd_token;
        case ExifIfdKind::GpsIfd: return tokens.gps_ifd_token;
        case ExifIfdKind::InteropIfd: return tokens.interop_ifd_token;
        case ExifIfdKind::SubIfd: {
            const std::string_view prefix = tokens.subifd_prefix;
            if (prefix.empty()) {
                return std::string_view();
            }
            if (scratch.size() < prefix.size() + 16U) {
                return std::string_view();
            }
            std::memcpy(scratch.data(), prefix.data(), prefix.size());
            const uint32_t digits = write_u32_decimal(
                scratch.data() + static_cast<uint32_t>(prefix.size()), index);
            return std::string_view(scratch.data(), prefix.size() + digits);
        }
        }
        return std::string_view();
    }


    struct IfdTask final {
        ExifIfdKind kind = ExifIfdKind::Ifd;
        uint32_t index   = 0;
        uint64_t offset  = 0;
    };

    struct IfdSink final {
        ExifIfdRef* out = nullptr;
        uint32_t cap    = 0;
        ExifDecodeResult result;
    };

    static void sink_emit(IfdSink* sink, const ExifIfdRef& ref) noexcept
    {
        sink->result.ifds_needed += 1;
        if (!sink->out || sink->cap == 0U) {
            return;
        }
        if (sink->result.ifds_written < sink->cap) {
            sink->out[sink->result.ifds_written] = ref;
            sink->result.ifds_written += 1;
        } else if (sink->result.status == ExifDecodeStatus::Ok) {
            sink->result.status = ExifDecodeStatus::OutputTruncated;
        }
    }


    static uint8_t ifd_kind_bit(ExifIfdKind kind) noexcept
    {
        switch (kind) {
        case ExifIfdKind::Ifd: return 1U << 0U;
        case ExifIfdKind::ExifIfd: return 1U << 1U;
        case ExifIfdKind::GpsIfd: return 1U << 2U;
        case ExifIfdKind::InteropIfd: return 1U << 3U;
        case ExifIfdKind::SubIfd: return 1U << 4U;
        default: break;
        }
        return 0;
    }


    static uint32_t find_visited(uint64_t off,
                                 std::span<const uint64_t> visited_offs,
                                 uint32_t visited_count) noexcept
    {
        for (uint32_t i = 0; i < visited_count; ++i) {
            if (visited_offs[i] == off) {
                return i;
            }
        }
        return 0xffffffffU;
    }


    static bool allow_revisit_kind(ExifIfdKind kind,
                                   uint8_t existing_mask) noexcept
    {
        // In some malformed files, GPSInfoIFDPointer references the same IFD as
        // InteropIFDPointer. ExifTool reports both groups. Preserve that
        // behavior by allowing a second decode pass for the GPS/Interop pair.
        const uint8_t gps    = ifd_kind_bit(ExifIfdKind::GpsIfd);
        const uint8_t intero = ifd_kind_bit(ExifIfdKind::InteropIfd);

        if (kind == ExifIfdKind::GpsIfd) {
            return existing_mask == intero;
        }
        if (kind == ExifIfdKind::InteropIfd) {
            return existing_mask == gps;
        }
        return false;
    }


    static uint8_t ifd_priority(ExifIfdKind kind) noexcept
    {
        // Prefer structured sub-directories over generic IFD chain when offsets
        // collide in malformed files (observed in the ExifTool sample corpus).
        switch (kind) {
        case ExifIfdKind::ExifIfd: return 5;
        case ExifIfdKind::InteropIfd: return 4;
        case ExifIfdKind::GpsIfd: return 3;
        case ExifIfdKind::SubIfd: return 2;
        case ExifIfdKind::Ifd: return 1;
        default: break;
        }
        return 0;
    }


    static uint32_t select_next_task_index(std::span<const IfdTask> tasks,
                                           uint32_t task_count) noexcept
    {
        uint32_t best_index   = 0;
        uint8_t best_priority = 0;
        uint64_t best_off     = 0;

        for (uint32_t i = 0; i < task_count; ++i) {
            const uint8_t prio = ifd_priority(tasks[i].kind);
            const uint64_t off = tasks[i].offset;
            if (i == 0 || prio > best_priority
                || (prio == best_priority && off < best_off)) {
                best_index    = i;
                best_priority = prio;
                best_off      = off;
            }
        }
        return best_index;
    }


    static void update_status(ExifDecodeResult* out,
                              ExifDecodeStatus status) noexcept
    {
        if (!out) {
            return;
        }
        if (out->status == ExifDecodeStatus::LimitExceeded) {
            return;
        }
        if (status == ExifDecodeStatus::LimitExceeded) {
            out->status = status;
            return;
        }
        if (out->status == ExifDecodeStatus::Malformed) {
            return;
        }
        if (status == ExifDecodeStatus::Malformed) {
            out->status = status;
            return;
        }
        if (out->status == ExifDecodeStatus::Unsupported) {
            return;
        }
        if (status == ExifDecodeStatus::Unsupported) {
            out->status = status;
            return;
        }
        if (out->status == ExifDecodeStatus::OutputTruncated) {
            return;
        }
        if (status == ExifDecodeStatus::OutputTruncated) {
            out->status = status;
            return;
        }
    }


    static void mark_limit_exceeded(ExifDecodeResult* out,
                                    ExifLimitReason reason, uint64_t ifd_offset,
                                    uint16_t tag) noexcept
    {
        if (!out) {
            return;
        }
        if (out->status != ExifDecodeStatus::LimitExceeded) {
            out->status           = ExifDecodeStatus::LimitExceeded;
            out->limit_reason     = reason;
            out->limit_ifd_offset = ifd_offset;
            out->limit_tag        = tag;
            return;
        }
        if (out->limit_reason == ExifLimitReason::None
            && reason != ExifLimitReason::None) {
            out->limit_reason     = reason;
            out->limit_ifd_offset = ifd_offset;
            out->limit_tag        = tag;
        }
    }


    static bool decode_u32_or_u64_offset(const TiffConfig& cfg,
                                         std::span<const std::byte> bytes,
                                         uint16_t type, uint64_t value_off,
                                         uint64_t count,
                                         std::span<uint64_t> out_ptrs,
                                         uint32_t* out_count) noexcept
    {
        *out_count = 0;

        const uint64_t unit = tiff_type_size(type);
        if (unit == 0) {
            return false;
        }
        if (count > (UINT64_MAX / unit)) {
            return false;
        }
        const uint64_t total_bytes = count * unit;
        if (value_off + total_bytes > bytes.size()) {
            return false;
        }

        const uint32_t cap = static_cast<uint32_t>(out_ptrs.size());
        const uint32_t n   = (count < cap) ? static_cast<uint32_t>(count) : cap;
        for (uint32_t i = 0; i < n; ++i) {
            uint64_t ptr = 0;
            if (unit == 4) {
                uint32_t v = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &v)) {
                    break;
                }
                ptr = v;
            } else if (unit == 8) {
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &ptr)) {
                    break;
                }
            } else {
                break;
            }
            out_ptrs[i] = ptr;
            *out_count += 1;
        }
        return true;
    }


    static MetaValue decode_text_value(ByteArena& arena,
                                       std::span<const std::byte> raw,
                                       TextEncoding enc) noexcept
    {
        if (raw.empty()) {
            return make_text(arena, std::string_view(), enc);
        }

        size_t trimmed = raw.size();
        if (raw[trimmed - 1] == std::byte { 0 }) {
            trimmed -= 1;
        }
        const std::span<const std::byte> payload = raw.subspan(0, trimmed);
        if (contains_nul(payload)) {
            return make_bytes(arena, raw);
        }

        const std::string_view text(reinterpret_cast<const char*>(
                                        payload.data()),
                                    payload.size());
        return make_text(arena, text, enc);
    }


    static MetaValue decode_tiff_value(const TiffConfig& cfg,
                                       std::span<const std::byte> bytes,
                                       uint16_t type, uint64_t count,
                                       uint64_t value_off, uint64_t value_bytes,
                                       ByteArena& arena,
                                       const ExifDecodeLimits& limits,
                                       ExifDecodeResult* result) noexcept
    {
        if (value_bytes > limits.max_value_bytes) {
            return MetaValue {};
        }

        switch (type) {
        case 1: {  // BYTE
            if (count == 1) {
                return make_u8(u8(bytes[value_off]));
            }
            if (count > UINT32_MAX || value_bytes > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U8;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.append(
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)));
            if (value_bytes != 0U
                && v.data.span.size != static_cast<uint32_t>(value_bytes)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            return v;
        }
        case 2: {  // ASCII
            if (value_bytes > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            return decode_text_value(
                arena,
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)),
                TextEncoding::Ascii);
        }
        case 3: {  // SHORT
            if (count == 1) {
                uint16_t v = 0;
                if (!read_tiff_u16(cfg, bytes, value_off, &v)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_u16(v);
            }
            if (count > (UINT32_MAX / 2U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U16;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 2U),
                                         alignof(uint16_t));
            if (count != 0U
                && v.data.span.size != static_cast<uint32_t>(count * 2U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint16_t value = 0;
                if (!read_tiff_u16(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 2U,
                                   &value)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 2U, &value, 2U);
            }
            return v;
        }
        case 4:     // LONG
        case 13: {  // IFD
            if (count == 1) {
                uint32_t v = 0;
                if (!read_tiff_u32(cfg, bytes, value_off, &v)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_u32(v);
            }
            if (count > (UINT32_MAX / 4U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U32;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 4U),
                                         alignof(uint32_t));
            if (count != 0U
                && v.data.span.size != static_cast<uint32_t>(count * 4U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t value = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &value)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 4U, &value, 4U);
            }
            return v;
        }
        case 5: {  // RATIONAL
            if (count == 1) {
                uint32_t numer = 0;
                uint32_t denom = 0;
                if (!read_tiff_u32(cfg, bytes, value_off + 0, &numer)
                    || !read_tiff_u32(cfg, bytes, value_off + 4, &denom)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_urational(numer, denom);
            }
            if (count > (UINT32_MAX / sizeof(URational))) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::URational;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(
                                             count * sizeof(URational)),
                                         alignof(URational));
            if (count != 0U
                && v.data.span.size
                       != static_cast<uint32_t>(count * sizeof(URational))) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t numer      = 0;
                uint32_t denom      = 0;
                const uint64_t base = value_off + static_cast<uint64_t>(i) * 8U;
                if (!read_tiff_u32(cfg, bytes, base + 0, &numer)
                    || !read_tiff_u32(cfg, bytes, base + 4, &denom)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const URational r { numer, denom };
                std::memcpy(dst.data() + i * sizeof(URational), &r,
                            sizeof(URational));
            }
            return v;
        }
        case 6: {  // SBYTE
            if (count == 1) {
                return make_i8(static_cast<int8_t>(u8(bytes[value_off])));
            }
            if (count > UINT32_MAX || value_bytes > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I8;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.append(
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)));
            if (value_bytes != 0U
                && v.data.span.size != static_cast<uint32_t>(value_bytes)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            return v;
        }
        case 7: {  // UNDEFINED
            if (value_bytes > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            return make_bytes(arena,
                              bytes.subspan(value_off,
                                            static_cast<size_t>(value_bytes)));
        }
        case 8: {  // SSHORT
            if (count == 1) {
                uint16_t raw = 0;
                if (!read_tiff_u16(cfg, bytes, value_off, &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_i16(static_cast<int16_t>(raw));
            }
            if (count > (UINT32_MAX / 2U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I16;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 2U),
                                         alignof(int16_t));
            if (count != 0U
                && v.data.span.size != static_cast<uint32_t>(count * 2U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint16_t raw = 0;
                if (!read_tiff_u16(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 2U,
                                   &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const int16_t value = static_cast<int16_t>(raw);
                std::memcpy(dst.data() + i * 2U, &value, 2U);
            }
            return v;
        }
        case 9: {  // SLONG
            if (count == 1) {
                uint32_t raw = 0;
                if (!read_tiff_u32(cfg, bytes, value_off, &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_i32(static_cast<int32_t>(raw));
            }
            if (count > (UINT32_MAX / 4U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I32;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 4U),
                                         alignof(int32_t));
            if (count != 0U
                && v.data.span.size != static_cast<uint32_t>(count * 4U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t raw = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const int32_t value = static_cast<int32_t>(raw);
                std::memcpy(dst.data() + i * 4U, &value, 4U);
            }
            return v;
        }
        case 10: {  // SRATIONAL
            if (count == 1) {
                uint32_t numer_u = 0;
                uint32_t denom_u = 0;
                if (!read_tiff_u32(cfg, bytes, value_off + 0, &numer_u)
                    || !read_tiff_u32(cfg, bytes, value_off + 4, &denom_u)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_srational(static_cast<int32_t>(numer_u),
                                      static_cast<int32_t>(denom_u));
            }
            if (count > (UINT32_MAX / sizeof(SRational))) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::SRational;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(
                                             count * sizeof(SRational)),
                                         alignof(SRational));
            if (count != 0U
                && v.data.span.size
                       != static_cast<uint32_t>(count * sizeof(SRational))) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t numer_u    = 0;
                uint32_t denom_u    = 0;
                const uint64_t base = value_off + static_cast<uint64_t>(i) * 8U;
                if (!read_tiff_u32(cfg, bytes, base + 0, &numer_u)
                    || !read_tiff_u32(cfg, bytes, base + 4, &denom_u)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const SRational r { static_cast<int32_t>(numer_u),
                                    static_cast<int32_t>(denom_u) };
                std::memcpy(dst.data() + i * sizeof(SRational), &r,
                            sizeof(SRational));
            }
            return v;
        }
        case 11: {  // FLOAT
            if (count == 1) {
                uint32_t bits = 0;
                if (!read_tiff_u32(cfg, bytes, value_off, &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_f32_bits(bits);
            }
            if (count > (UINT32_MAX / 4U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::F32;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 4U),
                                         alignof(uint32_t));
            if (count != 0U
                && v.data.span.size != static_cast<uint32_t>(count * 4U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t bits = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 4U, &bits, 4U);
            }
            return v;
        }
        case 12: {  // DOUBLE
            if (count == 1) {
                uint64_t bits = 0;
                if (!read_tiff_u64(cfg, bytes, value_off, &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_f64_bits(bits);
            }
            if (count > (UINT32_MAX / 8U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::F64;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 8U),
                                         alignof(uint64_t));
            if (count != 0U
                && v.data.span.size != static_cast<uint32_t>(count * 8U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint64_t bits = 0;
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 8U, &bits, 8U);
            }
            return v;
        }
        case 16:    // LONG8
        case 18: {  // IFD8
            if (count == 1) {
                uint64_t v = 0;
                if (!read_tiff_u64(cfg, bytes, value_off, &v)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_u64(v);
            }
            if (count > (UINT32_MAX / 8U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U64;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 8U),
                                         alignof(uint64_t));
            if (count != 0U
                && v.data.span.size != static_cast<uint32_t>(count * 8U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint64_t value = 0;
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &value)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 8U, &value, 8U);
            }
            return v;
        }
        case 17: {  // SLONG8
            if (count == 1) {
                uint64_t raw = 0;
                if (!read_tiff_u64(cfg, bytes, value_off, &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_i64(static_cast<int64_t>(raw));
            }
            if (count > (UINT32_MAX / 8U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I64;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 8U),
                                         alignof(int64_t));
            if (count != 0U
                && v.data.span.size != static_cast<uint32_t>(count * 8U)) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint64_t raw = 0;
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const int64_t value = static_cast<int64_t>(raw);
                std::memcpy(dst.data() + i * 8U, &value, 8U);
            }
            return v;
        }
        case 129: {  // UTF-8 (EXIF)
            if (value_bytes > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            return decode_text_value(
                arena,
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)),
                TextEncoding::Utf8);
        }
        default: break;
        }

        return MetaValue {};
    }


    static bool
    follow_ifd_pointers(const TiffConfig& cfg, std::span<const std::byte> bytes,
                        uint16_t tag, uint16_t type, uint64_t count,
                        uint64_t value_off, std::span<IfdTask> stack,
                        uint32_t* stack_size, uint32_t* next_subifd_index,
                        const ExifDecodeLimits& limits,
                        ExifDecodeResult* result) noexcept
    {
        if (tag != 0x8769 && tag != 0x8825 && tag != 0xA005 && tag != 0x014A) {
            return true;
        }

        if (*stack_size >= limits.max_ifds) {
            mark_limit_exceeded(result, ExifLimitReason::MaxIfds, 0, tag);
            return false;
        }

        std::array<uint64_t, 32> ptrs {};
        uint32_t ptr_count = 0;
        if (!decode_u32_or_u64_offset(cfg, bytes, type, value_off, count,
                                      std::span<uint64_t>(ptrs), &ptr_count)) {
            return true;
        }

        if (tag == 0x014A) {  // SubIFDs: may be an array of offsets.
            for (uint32_t i = 0; i < ptr_count; ++i) {
                if (*stack_size >= stack.size()
                    || *stack_size >= limits.max_ifds) {
                    mark_limit_exceeded(result, ExifLimitReason::MaxIfds, 0,
                                        tag);
                    return false;
                }
                IfdTask t;
                t.kind  = ExifIfdKind::SubIfd;
                t.index = *next_subifd_index;
                *next_subifd_index += 1;
                t.offset           = ptrs[i];
                stack[*stack_size] = t;
                *stack_size += 1;
            }
            return true;
        }

        if (ptr_count == 0) {
            return true;
        }

        IfdTask t;
        t.offset = ptrs[0];
        t.index  = 0;
        if (tag == 0x8769) {
            t.kind = ExifIfdKind::ExifIfd;
        } else if (tag == 0x8825) {
            t.kind = ExifIfdKind::GpsIfd;
        } else if (tag == 0xA005) {
            t.kind = ExifIfdKind::InteropIfd;
        } else {
            return true;
        }

        if (*stack_size < stack.size() && *stack_size < limits.max_ifds) {
            stack[*stack_size] = t;
            *stack_size += 1;
        } else {
            mark_limit_exceeded(result, ExifLimitReason::MaxIfds, 0, tag);
            return false;
        }

        return true;
    }

}  // namespace

namespace exif_internal {

    ExifContext::ExifContext(const MetaStore& store) noexcept
        : store_(&store)
    {
        next_ = 0;
        for (uint32_t i = 0; i < sizeof(slots_) / sizeof(slots_[0]); ++i) {
            slots_[i].entry = kInvalidEntryId;
            slots_[i].tag   = 0;
            slots_[i].ifd   = std::string_view();
        }
    }


    void ExifContext::cache_hit(std::string_view ifd, uint16_t tag,
                                EntryId entry) noexcept
    {
        const uint32_t cap = static_cast<uint32_t>(sizeof(slots_)
                                                   / sizeof(slots_[0]));
        const uint32_t idx = next_;
        slots_[idx].ifd    = ifd;
        slots_[idx].tag    = tag;
        slots_[idx].entry  = entry;
        next_              = (idx + 1U) % cap;
    }


    bool ExifContext::find_first_entry(std::string_view ifd, uint16_t tag,
                                       EntryId* out) noexcept
    {
        if (!store_) {
            return false;
        }
        if (ifd.empty()) {
            return false;
        }

        for (uint32_t i = 0; i < sizeof(slots_) / sizeof(slots_[0]); ++i) {
            const Slot& s = slots_[i];
            if (s.entry == kInvalidEntryId) {
                continue;
            }
            if (s.tag == tag && s.ifd == ifd) {
                if (out) {
                    *out = s.entry;
                }
                return true;
            }
        }

        const ByteArena& arena               = store_->arena();
        const std::span<const Entry> entries = store_->entries();
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
            const EntryId id = static_cast<EntryId>(i);
            cache_hit(ifd, tag, id);
            if (out) {
                *out = id;
            }
            return true;
        }

        return false;
    }


    bool ExifContext::find_first_value(std::string_view ifd, uint16_t tag,
                                       MetaValue* out) noexcept
    {
        if (out) {
            *out = MetaValue {};
        }

        EntryId id = kInvalidEntryId;
        if (!find_first_entry(ifd, tag, &id) || id == kInvalidEntryId) {
            return false;
        }
        const Entry& e = store_->entry(id);
        if (out) {
            *out = e.value;
        }
        return true;
    }


    bool ExifContext::find_first_text(std::string_view ifd, uint16_t tag,
                                      std::string_view* out) noexcept
    {
        if (out) {
            *out = std::string_view();
        }

        EntryId id = kInvalidEntryId;
        if (!find_first_entry(ifd, tag, &id) || id == kInvalidEntryId) {
            return false;
        }

        const Entry& e = store_->entry(id);
        if (e.value.kind != MetaValueKind::Text) {
            return false;
        }

        if (out) {
            *out = arena_string(store_->arena(), e.value.data.span);
        }
        return true;
    }


    bool ExifContext::find_first_u32(std::string_view ifd, uint16_t tag,
                                     uint32_t* out) noexcept
    {
        if (out) {
            *out = 0;
        }

        EntryId id = kInvalidEntryId;
        if (!find_first_entry(ifd, tag, &id) || id == kInvalidEntryId) {
            return false;
        }

        const Entry& e = store_->entry(id);
        if (e.value.kind != MetaValueKind::Scalar || e.value.count != 1) {
            return false;
        }

        if (e.value.elem_type == MetaElementType::U32
            || e.value.elem_type == MetaElementType::U16
            || e.value.elem_type == MetaElementType::U8) {
            if (out) {
                *out = static_cast<uint32_t>(e.value.data.u64);
            }
            return true;
        }
        return false;
    }


    bool ExifContext::find_first_i32(std::string_view ifd, uint16_t tag,
                                     int32_t* out) noexcept
    {
        if (out) {
            *out = 0;
        }

        EntryId id = kInvalidEntryId;
        if (!find_first_entry(ifd, tag, &id) || id == kInvalidEntryId) {
            return false;
        }

        const Entry& e = store_->entry(id);
        if (e.value.kind != MetaValueKind::Scalar || e.value.count != 1) {
            return false;
        }

        switch (e.value.elem_type) {
        case MetaElementType::I32:
        case MetaElementType::I16:
        case MetaElementType::I8:
            if (e.value.data.i64 < static_cast<int64_t>(INT32_MIN)
                || e.value.data.i64 > static_cast<int64_t>(INT32_MAX)) {
                return false;
            }
            if (out) {
                *out = static_cast<int32_t>(e.value.data.i64);
            }
            return true;
        case MetaElementType::U32:
        case MetaElementType::U16:
        case MetaElementType::U8:
            if (e.value.data.u64 > static_cast<uint64_t>(INT32_MAX)) {
                return false;
            }
            if (out) {
                *out = static_cast<int32_t>(e.value.data.u64);
            }
            return true;
        default: break;
        }

        return false;
    }

    bool read_classic_ifd_entry(const TiffConfig& cfg,
                                std::span<const std::byte> bytes,
                                uint64_t entry_off,
                                ClassicIfdEntry* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (entry_off > bytes.size() || bytes.size() - entry_off < 12ULL) {
            return false;
        }

        ClassicIfdEntry e;
        if (!read_tiff_u16(cfg, bytes, entry_off + 0, &e.tag)
            || !read_tiff_u16(cfg, bytes, entry_off + 2, &e.type)
            || !read_tiff_u32(cfg, bytes, entry_off + 4, &e.count32)
            || !read_tiff_u32(cfg, bytes, entry_off + 8, &e.value_or_off32)) {
            return false;
        }
        *out = e;
        return true;
    }


    bool classic_ifd_entry_value_bytes(const ClassicIfdEntry& e,
                                       uint64_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (e.count32 == 0U) {
            *out = 0;
            return true;
        }

        const uint64_t unit = tiff_type_size(e.type);
        if (unit == 0) {
            return false;
        }
        const uint64_t count = e.count32;
        if (count > (UINT64_MAX / unit)) {
            return false;
        }

        *out = count * unit;
        return true;
    }


    bool resolve_classic_ifd_value_ref(const MakerNoteLayout& layout,
                                       uint64_t entry_off,
                                       const ClassicIfdEntry& e,
                                       ClassicIfdValueRef* out,
                                       ExifDecodeResult* status_out) noexcept
    {
        return resolve_classic_ifd_value_ref(layout.offsets, entry_off, e, out,
                                             status_out);
    }


    bool resolve_classic_ifd_value_ref(const OffsetPolicy& offsets,
                                       uint64_t entry_off,
                                       const ClassicIfdEntry& e,
                                       ClassicIfdValueRef* out,
                                       ExifDecodeResult* status_out) noexcept
    {
        if (!out) {
            return false;
        }

        const uint64_t unit = tiff_type_size(e.type);
        if (unit == 0) {
            return false;
        }
        const uint64_t count = e.count32;
        if (count != 0 && count > (UINT64_MAX / unit)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        const uint64_t value_bytes = count * unit;

        ClassicIfdValueRef ref;
        ref.value_bytes  = value_bytes;
        ref.inline_value = (value_bytes <= 4U);

        if (ref.inline_value) {
            if (entry_off > (UINT64_MAX - 8ULL)) {
                return false;
            }
            ref.value_off = entry_off + 8ULL;
        } else if (offsets.out_of_line_base_is_signed) {
            const int64_t base = offsets.out_of_line_base_i64;
            const int64_t off  = static_cast<int64_t>(
                static_cast<uint64_t>(e.value_or_off32));

            if (base > (INT64_MAX - off)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return false;
            }

            const int64_t abs_off = base + off;
            if (abs_off < 0) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return false;
            }

            ref.value_off = static_cast<uint64_t>(abs_off);
        } else {
            const uint64_t base = offsets.out_of_line_base;
            const uint64_t off  = static_cast<uint64_t>(e.value_or_off32);
            if (base > (UINT64_MAX - off)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return false;
            }
            ref.value_off = base + off;
        }

        *out = ref;
        return true;
    }

    bool match_bytes(std::span<const std::byte> bytes, uint64_t offset,
                     const char* magic, uint32_t magic_len) noexcept
    {
        return ::openmeta::match_bytes(bytes, offset, magic, magic_len);
    }

    bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                    uint16_t* out) noexcept
    {
        return ::openmeta::read_u16be(bytes, offset, out);
    }

    bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                    uint16_t* out) noexcept
    {
        return ::openmeta::read_u16le(bytes, offset, out);
    }

    bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                    uint32_t* out) noexcept
    {
        return ::openmeta::read_u32be(bytes, offset, out);
    }

    bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                    uint32_t* out) noexcept
    {
        return ::openmeta::read_u32le(bytes, offset, out);
    }

    bool read_tiff_u16(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t offset, uint16_t* out) noexcept
    {
        return ::openmeta::read_tiff_u16(cfg, bytes, offset, out);
    }

    bool read_tiff_u32(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t offset, uint32_t* out) noexcept
    {
        return ::openmeta::read_tiff_u32(cfg, bytes, offset, out);
    }

    bool read_u16_endian(bool le, std::span<const std::byte> bytes,
                         uint64_t offset, uint16_t* out) noexcept
    {
        return ::openmeta::read_u16_endian(le, bytes, offset, out);
    }

    bool read_i16_endian(bool le, std::span<const std::byte> bytes,
                         uint64_t offset, int16_t* out) noexcept
    {
        return ::openmeta::read_i16_endian(le, bytes, offset, out);
    }

    std::string_view
    make_mk_subtable_ifd_token(std::string_view vendor_prefix,
                               std::string_view subtable, uint32_t index,
                               std::span<char> scratch) noexcept
    {
        return ::openmeta::make_mk_subtable_ifd_token(vendor_prefix, subtable,
                                                      index, scratch);
    }

    MetaValue make_fixed_ascii_text(ByteArena& arena,
                                    std::span<const std::byte> raw) noexcept
    {
        return ::openmeta::make_fixed_ascii_text(arena, raw);
    }

    void emit_bin_dir_entries(std::string_view ifd_name, MetaStore& store,
                              std::span<const uint16_t> tags,
                              std::span<const MetaValue> values,
                              const ExifDecodeLimits& limits,
                              ExifDecodeResult* status_out) noexcept
    {
        ::openmeta::emit_bin_dir_entries(ifd_name, store, tags, values, limits,
                                         status_out);
    }

    uint64_t tiff_type_size(uint16_t type) noexcept
    {
        return ::openmeta::tiff_type_size(type);
    }

    void update_status(ExifDecodeResult* out, ExifDecodeStatus status) noexcept
    {
        ::openmeta::update_status(out, status);
    }

    MetaValue decode_tiff_value(const TiffConfig& cfg,
                                std::span<const std::byte> bytes, uint16_t type,
                                uint64_t count, uint64_t value_off,
                                uint64_t value_bytes, ByteArena& arena,
                                const ExifDecodeLimits& limits,
                                ExifDecodeResult* result) noexcept
    {
        return ::openmeta::decode_tiff_value(cfg, bytes, type, count, value_off,
                                             value_bytes, arena, limits,
                                             result);
    }

    bool score_classic_ifd_candidate(const TiffConfig& cfg,
                                     std::span<const std::byte> bytes,
                                     uint64_t ifd_off,
                                     const ExifDecodeLimits& limits,
                                     ClassicIfdCandidate* out) noexcept
    {
        return ::openmeta::score_classic_ifd_candidate(cfg, bytes, ifd_off,
                                                       limits, out);
    }

    bool find_best_classic_ifd_candidate(std::span<const std::byte> bytes,
                                         uint64_t scan_bytes,
                                         const ExifDecodeLimits& limits,
                                         ClassicIfdCandidate* out) noexcept
    {
        return ::openmeta::find_best_classic_ifd_candidate(bytes, scan_bytes,
                                                           limits, out);
    }

    bool looks_like_classic_ifd(const TiffConfig& cfg,
                                std::span<const std::byte> bytes,
                                uint64_t ifd_off,
                                const ExifDecodeLimits& limits) noexcept
    {
        return ::openmeta::looks_like_classic_ifd(cfg, bytes, ifd_off, limits);
    }

    void decode_classic_ifd_no_header(
        const TiffConfig& cfg, std::span<const std::byte> bytes,
        uint64_t ifd_off, std::string_view ifd_name, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out,
        EntryFlags extra_flags) noexcept
    {
        ::openmeta::decode_classic_ifd_no_header(cfg, bytes, ifd_off, ifd_name,
                                                 store, options, status_out,
                                                 extra_flags);
    }

}  // namespace exif_internal

static ExifDecodeResult
decode_exif_tiff_contiguous(std::span<const std::byte> tiff_bytes,
                            MetaStore& store, std::span<ExifIfdRef> out_ifds,
                            const ExifDecodeOptions& options) noexcept
{
    IfdSink sink;
    sink.out = out_ifds.data();
    sink.cap = static_cast<uint32_t>(out_ifds.size());

    store.constrain_resources(options.limits.max_total_entries,
                              options.limits.max_arena_bytes);
    if (store.resource_limit_exceeded()) {
        mark_limit_exceeded(&sink.result, ExifLimitReason::MaxArenaBytes, 0, 0);
        return sink.result;
    }

    if (tiff_bytes.size() < 8) {
        sink.result.status = ExifDecodeStatus::Malformed;
        return sink.result;
    }

    TiffConfig cfg;
    const uint8_t b0 = u8(tiff_bytes[0]);
    const uint8_t b1 = u8(tiff_bytes[1]);
    if (b0 == 0x49 && b1 == 0x49) {
        cfg.le = true;
    } else if (b0 == 0x4D && b1 == 0x4D) {
        cfg.le = false;
    } else {
        sink.result.status = ExifDecodeStatus::Unsupported;
        return sink.result;
    }

    uint16_t version = 0;
    if (!read_tiff_u16(cfg, tiff_bytes, 2, &version)) {
        sink.result.status = ExifDecodeStatus::Malformed;
        return sink.result;
    }
    if (version == 42) {
        cfg.bigtiff = false;
    } else if (version == 43) {
        cfg.bigtiff = true;
    } else if (version == 0x0055 || version == 0x4F52) {
        // TIFF-based RAW variants that still use classic TIFF IFD structures:
        // - Panasonic RW2: "IIU\0" (0x0055 in LE form)
        // - Olympus ORF: "IIRO" (0x4F52 in LE form)
        cfg.bigtiff = false;
    } else {
        sink.result.status = ExifDecodeStatus::Unsupported;
        return sink.result;
    }

    uint64_t first_ifd = 0;
    if (!cfg.bigtiff) {
        uint32_t off32 = 0;
        if (!read_tiff_u32(cfg, tiff_bytes, 4, &off32)) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        first_ifd = off32;
    } else {
        if (tiff_bytes.size() < 16) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        uint16_t off_size = 0;
        uint16_t reserved = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, 4, &off_size)
            || !read_tiff_u16(cfg, tiff_bytes, 6, &reserved)) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        if (off_size != 8 || reserved != 0) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        if (!read_tiff_u64(cfg, tiff_bytes, 8, &first_ifd)) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
    }

    std::array<IfdTask, 256> stack_buf {};
    std::array<uint64_t, 256> visited_offs {};
    std::array<uint8_t, 256> visited_masks {};
    uint32_t stack_size        = 0;
    uint32_t visited_count     = 0;
    uint32_t next_subifd_index = 0;

    if (first_ifd != 0) {
        stack_buf[0] = IfdTask { ExifIfdKind::Ifd, 0, first_ifd };
        stack_size   = 1;
    }

    while (stack_size > 0) {
        const uint32_t next_index
            = select_next_task_index(std::span<const IfdTask>(stack_buf),
                                     stack_size);
        IfdTask task          = stack_buf[next_index];
        stack_buf[next_index] = stack_buf[stack_size - 1];
        stack_size -= 1;

        if (task.offset == 0 || task.offset >= tiff_bytes.size()) {
            continue;
        }

        const uint8_t kind_bit = ifd_kind_bit(task.kind);
        const uint32_t vi
            = find_visited(task.offset, std::span<const uint64_t>(visited_offs),
                           visited_count);
        if (vi != 0xffffffffU) {
            const uint8_t mask = visited_masks[vi];
            if ((mask & kind_bit) != 0) {
                continue;
            }
            if (!allow_revisit_kind(task.kind, mask)) {
                continue;
            }
            visited_masks[vi] = static_cast<uint8_t>(mask | kind_bit);
        } else {
            if (visited_count < visited_offs.size()) {
                visited_offs[visited_count]  = task.offset;
                visited_masks[visited_count] = kind_bit;
                visited_count += 1;
            } else {
                mark_limit_exceeded(&sink.result, ExifLimitReason::MaxIfds,
                                    task.offset, 0);
                break;
            }
        }

        if (sink.result.ifds_needed >= options.limits.max_ifds) {
            mark_limit_exceeded(&sink.result, ExifLimitReason::MaxIfds,
                                task.offset, 0);
            break;
        }

        uint64_t entry_count      = 0;
        uint64_t entries_off      = 0;
        uint64_t entry_size       = 0;
        uint64_t next_ifd_off_pos = 0;

        if (!cfg.bigtiff) {
            uint16_t n16 = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, task.offset, &n16)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            entry_count = n16;
            if (entry_count > options.limits.max_entries_per_ifd) {
                mark_limit_exceeded(&sink.result,
                                    ExifLimitReason::MaxEntriesPerIfd,
                                    task.offset, 0);
                continue;
            }
            if (!checked_add_u64(task.offset, 2U, &entries_off)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            entry_size           = 12;
            uint64_t table_bytes = 0;
            if (!checked_mul_u64(entry_count, entry_size, &table_bytes)
                || !checked_add_u64(entries_off, table_bytes,
                                    &next_ifd_off_pos)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            if (task.kind == ExifIfdKind::Ifd) {
                if (span_contains_bytes(tiff_bytes, next_ifd_off_pos, 4U)) {
                    uint32_t next32 = 0;
                    if (read_tiff_u32(cfg, tiff_bytes, next_ifd_off_pos, &next32)
                        && next32 != 0) {
                        if (stack_size < stack_buf.size()
                            && stack_size < options.limits.max_ifds) {
                            stack_buf[stack_size] = IfdTask { ExifIfdKind::Ifd,
                                                              task.index + 1,
                                                              next32 };
                            stack_size += 1;
                        } else {
                            mark_limit_exceeded(&sink.result,
                                                ExifLimitReason::MaxIfds,
                                                task.offset, 0);
                        }
                    }
                } else {
                    // Truncated next-IFD pointer field. Decode entries anyway.
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                }
            }
        } else {
            uint64_t n64 = 0;
            if (!read_tiff_u64(cfg, tiff_bytes, task.offset, &n64)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            entry_count = n64;
            if (entry_count > options.limits.max_entries_per_ifd) {
                mark_limit_exceeded(&sink.result,
                                    ExifLimitReason::MaxEntriesPerIfd,
                                    task.offset, 0);
                continue;
            }
            if (!checked_add_u64(task.offset, 8U, &entries_off)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            entry_size           = 20;
            uint64_t table_bytes = 0;
            if (!checked_mul_u64(entry_count, entry_size, &table_bytes)
                || !checked_add_u64(entries_off, table_bytes,
                                    &next_ifd_off_pos)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            if (task.kind == ExifIfdKind::Ifd) {
                if (span_contains_bytes(tiff_bytes, next_ifd_off_pos, 8U)) {
                    uint64_t next64 = 0;
                    if (read_tiff_u64(cfg, tiff_bytes, next_ifd_off_pos, &next64)
                        && next64 != 0) {
                        if (stack_size < stack_buf.size()
                            && stack_size < options.limits.max_ifds) {
                            stack_buf[stack_size] = IfdTask { ExifIfdKind::Ifd,
                                                              task.index + 1,
                                                              next64 };
                            stack_size += 1;
                        } else {
                            mark_limit_exceeded(&sink.result,
                                                ExifLimitReason::MaxIfds,
                                                task.offset, 0);
                        }
                    }
                } else {
                    // Truncated next-IFD pointer field. Decode entries anyway.
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                }
            }
        }

        uint64_t table_bytes = 0;
        if (!checked_mul_u64(entry_count, entry_size, &table_bytes)
            || !span_contains_bytes(tiff_bytes, entries_off, table_bytes)) {
            update_status(&sink.result, ExifDecodeStatus::Malformed);
            continue;
        }
        if (sink.result.entries_decoded + entry_count
            > options.limits.max_total_entries) {
            mark_limit_exceeded(&sink.result, ExifLimitReason::MaxTotalEntries,
                                task.offset, 0);
            continue;
        }

        BlockId block = store.add_block(BlockInfo {});
        ExifIfdRef ref;
        ref.kind   = task.kind;
        ref.index  = task.index;
        ref.offset = task.offset;
        ref.block  = block;
        sink_emit(&sink, ref);

        char token_scratch_buf[64];
        const std::string_view ifd_name
            = ifd_token(options.tokens, task.kind, task.index,
                        std::span<char>(token_scratch_buf));
        if (ifd_name.empty()) {
            update_status(&sink.result, ExifDecodeStatus::Malformed);
            continue;
        }

        exif_internal::GeoTiffTagRef geotiff_dir;
        exif_internal::GeoTiffTagRef geotiff_double;
        exif_internal::GeoTiffTagRef geotiff_ascii;

        for (uint64_t i = 0; i < entry_count; ++i) {
            uint64_t entry_delta = 0;
            uint64_t eoff        = 0;
            if (!checked_mul_u64(i, entry_size, &entry_delta)
                || !checked_add_u64(entries_off, entry_delta, &eoff)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }

            uint16_t tag  = 0;
            uint16_t type = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, eoff + 0, &tag)
                || !read_tiff_u16(cfg, tiff_bytes, eoff + 2, &type)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }

            uint64_t count           = 0;
            uint64_t value_or_off    = 0;
            uint64_t value_field_off = 0;
            if (!cfg.bigtiff) {
                uint32_t c32 = 0;
                uint32_t v32 = 0;
                if (!read_tiff_u32(cfg, tiff_bytes, eoff + 4, &c32)
                    || !read_tiff_u32(cfg, tiff_bytes, eoff + 8, &v32)) {
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                    continue;
                }
                count           = c32;
                value_or_off    = v32;
                value_field_off = eoff + 8;
            } else {
                uint64_t c64 = 0;
                uint64_t v64 = 0;
                if (!read_tiff_u64(cfg, tiff_bytes, eoff + 4, &c64)
                    || !read_tiff_u64(cfg, tiff_bytes, eoff + 12, &v64)) {
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                    continue;
                }
                count           = c64;
                value_or_off    = v64;
                value_field_off = eoff + 12;
            }

            const uint64_t unit = tiff_type_size(type);
            if (unit == 0) {
                continue;
            }
            if (count > (UINT64_MAX / unit)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            uint64_t value_bytes = 0;
            if (!checked_mul_u64(count, unit, &value_bytes)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }

            uint64_t value_off        = 0;
            const uint64_t inline_cap = cfg.bigtiff ? 8U : 4U;
            if (value_bytes <= inline_cap) {
                value_off = value_field_off;
            } else {
                value_off = value_or_off;
            }
            if (!span_contains_bytes(tiff_bytes, value_off, value_bytes)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }

            if (options.decode_geotiff) {
                if (tag == 0x87AFu && count <= UINT32_MAX) {
                    geotiff_dir.present     = true;
                    geotiff_dir.type        = type;
                    geotiff_dir.count32     = static_cast<uint32_t>(count);
                    geotiff_dir.value_off   = value_off;
                    geotiff_dir.value_bytes = value_bytes;
                } else if (tag == 0x87B0u && count <= UINT32_MAX) {
                    geotiff_double.present     = true;
                    geotiff_double.type        = type;
                    geotiff_double.count32     = static_cast<uint32_t>(count);
                    geotiff_double.value_off   = value_off;
                    geotiff_double.value_bytes = value_bytes;
                } else if (tag == 0x87B1u && count <= UINT32_MAX) {
                    geotiff_ascii.present     = true;
                    geotiff_ascii.type        = type;
                    geotiff_ascii.count32     = static_cast<uint32_t>(count);
                    geotiff_ascii.value_off   = value_off;
                    geotiff_ascii.value_bytes = value_bytes;
                }
            }

            (void)follow_ifd_pointers(cfg, tiff_bytes, tag, type, count,
                                      value_off, std::span<IfdTask>(stack_buf),
                                      &stack_size, &next_subifd_index,
                                      options.limits, &sink.result);

            if (count > UINT32_MAX) {
                mark_limit_exceeded(&sink.result,
                                    ExifLimitReason::ValueCountTooLarge,
                                    task.offset, tag);
                continue;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = static_cast<uint32_t>(i);
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);
            if (value_bytes > options.limits.max_value_bytes) {
                entry.flags |= EntryFlags::Truncated;
            } else {
                entry.value = decode_tiff_value(cfg, tiff_bytes, type, count,
                                                value_off, value_bytes,
                                                store.arena(), options.limits,
                                                &sink.result);
            }

            if (!options.include_pointer_tags
                && (tag == 0x8769 || tag == 0x8825 || tag == 0xA005
                    || tag == 0x014A)) {
                continue;
            }

            maybe_mark_contextual_name(ifd_name, tag, store, &entry);
            if (store.add_entry(entry) == kInvalidEntryId) {
                mark_limit_exceeded(&sink.result,
                                    store.arena().limit_exceeded()
                                        ? ExifLimitReason::MaxArenaBytes
                                        : ExifLimitReason::MaxTotalEntries,
                                    task.offset, tag);
                continue;
            }
            sink.result.entries_decoded += 1;

            // PrintIM (0xC4A5) is an embedded binary block that ExifTool
            // exposes as a separate "PrintIM" group. Decode it into
            // MetaKeyKind::PrintImField entries as a best-effort parse.
            if (options.decode_printim && tag == 0xC4A5 && value_bytes != 0U
                && value_bytes <= options.limits.max_value_bytes) {
                PrintImDecodeLimits plim;
                plim.max_entries = options.limits.max_entries_per_ifd;
                plim.max_bytes   = options.limits.max_value_bytes;
                const PrintImDecodeResult printim = decode_printim(
                    tiff_bytes.subspan(static_cast<size_t>(value_off),
                                       static_cast<size_t>(value_bytes)),
                    store, plim);
                if (printim.status == PrintImDecodeStatus::LimitExceeded) {
                    mark_limit_exceeded(&sink.result,
                                        ExifLimitReason::MaxTotalEntries,
                                        task.offset, tag);
                }
            }

            // DNGPrivateData (0xC634) may embed a vendor MakerNote block.
            // Pentax raw DNG files store a `PENTAX \0...` block here that
            // ExifTool exposes as the Pentax MakerNote group.
            if (options.decode_makernote && tag == 0xC634 && value_bytes != 0U
                && value_bytes <= options.limits.max_value_bytes) {
                const std::span<const std::byte> dng_private
                    = tiff_bytes.subspan(static_cast<size_t>(value_off),
                                         static_cast<size_t>(value_bytes));
                const MakerNoteVendor vendor
                    = detect_makernote_vendor(dng_private, store);

                if (vendor == MakerNoteVendor::Pentax) {
                    ExifDecodeOptions mn_opts = options;
                    mn_opts.decode_printim    = false;
                    mn_opts.decode_makernote  = false;
                    set_makernote_tokens(&mn_opts, vendor);

                    char token_scratch_buf2[64];
                    const std::string_view mk_ifd0
                        = ifd_token(mn_opts.tokens, ExifIfdKind::Ifd, 0,
                                    std::span<char>(token_scratch_buf2));
                    (void)exif_internal::decode_pentax_makernote(dng_private,
                                                                 mk_ifd0, store,
                                                                 mn_opts,
                                                                 &sink.result);
                }
            }

            // MakerNote (0x927C) is vendor-defined. As a minimal starting point,
            // attempt to decode embedded TIFF headers found inside the blob
            // (covers common cases like Nikon).
            if (options.decode_makernote && tag == 0x927C
                && value_bytes != 0U) {
                const MakerNoteVendor hinted_vendor
                    = detect_makernote_vendor(std::span<const std::byte> {},
                                              store);
                if (value_bytes > options.limits.max_value_bytes
                    && hinted_vendor != MakerNoteVendor::PhaseOne) {
                    continue;
                }
                const std::span<const std::byte> mn
                    = tiff_bytes.subspan(static_cast<size_t>(value_off),
                                         static_cast<size_t>(value_bytes));
                const MakerNoteVendor vendor = detect_makernote_vendor(mn,
                                                                       store);

                ExifDecodeOptions mn_opts = options;
                mn_opts.decode_printim    = false;
                mn_opts.decode_makernote  = false;
                set_makernote_tokens(&mn_opts, vendor);

                char token_scratch_buf2[64];
                const std::string_view mk_ifd0
                    = ifd_token(mn_opts.tokens, ExifIfdKind::Ifd, 0,
                                std::span<char>(token_scratch_buf2));

                // Olympus MakerNote: classic IFD at +8, offsets relative to the
                // outer EXIF TIFF header.
                if (vendor == MakerNoteVendor::Olympus
                    && exif_internal::decode_olympus_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Pentax MakerNote: "AOC\0" header + endianness marker +
                // u16 entry count at +6, then classic IFD entries at +8.
                if (vendor == MakerNoteVendor::Pentax
                    && exif_internal::decode_pentax_makernote(mn, mk_ifd0,
                                                              store, mn_opts,
                                                              &sink.result)) {
                    continue;
                }

                // Casio MakerNote type2: "QVC\0" header + big-endian entries.
                if (vendor == MakerNoteVendor::Casio
                    && exif_internal::decode_casio_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Panasonic MakerNote: classic IFD located within the blob, but
                // value offsets are commonly relative to the outer EXIF/TIFF.
                if (vendor == MakerNoteVendor::Panasonic
                    && exif_internal::decode_panasonic_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Samsung MakerNote: either fixed-layout "STMN" blocks or
                // classic TIFF-IFD "Type2" maker notes, plus PictureWizard
                // binary subtables.
                if (vendor == MakerNoteVendor::Samsung
                    && exif_internal::decode_samsung_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Canon MakerNote: classic IFD at offset 0 (parent endianness),
                // plus Canon-specific BinaryData subdirectories.
                if (vendor == MakerNoteVendor::Canon
                    && exif_internal::decode_canon_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                if (vendor == MakerNoteVendor::Fuji
                    && exif_internal::decode_fuji_makernote(
                        tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Sony MakerNote: classic IFD located within the blob, but
                // value offsets are commonly relative to the outer EXIF/TIFF.
                if (vendor == MakerNoteVendor::Sony
                    && exif_internal::decode_sony_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    exif_internal::decode_sony_cipher_subdirs(mk_ifd0, store,
                                                              mn_opts,
                                                              &sink.result);
                    continue;
                }

                // Kodak MakerNote: supports both KDK fixed-layout blobs and
                // embedded TIFF headers with vendor sub-IFDs.
                if (vendor == MakerNoteVendor::Kodak
                    && exif_internal::decode_kodak_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // FLIR MakerNote: classic IFD at offset 0 (value offsets are
                // typically relative to the outer EXIF/TIFF).
                if (vendor == MakerNoteVendor::Flir
                    && exif_internal::decode_flir_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Ricoh MakerNote: TIFF IFD variants (including "Type2") plus
                // binary ImageInfo and RicohSubdir tables.
                if (vendor == MakerNoteVendor::Ricoh
                    && exif_internal::decode_ricoh_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Minolta MakerNote: classic IFD with nested CameraSettings
                // binary tables.
                if (vendor == MakerNoteVendor::Minolta
                    && exif_internal::decode_minolta_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // HP MakerNote: fixed-layout binary blocks (ExifTool:
                // MakerNoteHP4/MakerNoteHP6).
                if (vendor == MakerNoteVendor::Hp
                    && exif_internal::decode_hp_makernote(mn, mk_ifd0, store,
                                                          mn_opts,
                                                          &sink.result)) {
                    continue;
                }

                // Nintendo MakerNote: classic IFD at offset 0 plus a binary
                // CameraInfo subdirectory (tag 0x1101).
                if (vendor == MakerNoteVendor::Nintendo
                    && exif_internal::decode_nintendo_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Reconyx MakerNote: fixed-layout binary maker notes (HyperFire,
                // UltraFire, etc).
                if (vendor == MakerNoteVendor::Reconyx
                    && exif_internal::decode_reconyx_makernote(mn, mk_ifd0,
                                                               store, mn_opts,
                                                               &sink.result)) {
                    continue;
                }

                if (vendor == MakerNoteVendor::PhaseOne
                    && decode_phaseone_makernote(mn, mk_ifd0, store, mn_opts,
                                                 &sink.result)) {
                    continue;
                }

                // 1) Embedded TIFF header inside MakerNote (common for Nikon).
                const uint64_t hdr_off = find_embedded_tiff_header(mn, 128);
                if (hdr_off != UINT64_MAX) {
                    if (value_off > (UINT64_MAX - hdr_off)) {
                        continue;
                    }
                    const uint64_t hdr_abs = value_off + hdr_off;
                    if (hdr_abs >= tiff_bytes.size()) {
                        continue;
                    }

                    // Some real-world MakerNotes store out-of-line values
                    // beyond the declared MakerNote byte count. Decode the
                    // embedded TIFF header using the full EXIF/TIFF buffer so
                    // these values can be resolved safely (bounds-checked by
                    // the decoder limits and input span size).
                    const std::span<const std::byte> hdr_bytes
                        = tiff_bytes.subspan(static_cast<size_t>(hdr_abs));

                    std::array<ExifIfdRef, 128> mn_ifds;
                    (void)decode_exif_tiff(hdr_bytes, store,
                                           std::span<ExifIfdRef>(mn_ifds.data(),
                                                                 mn_ifds.size()),
                                           mn_opts);

                    if (vendor == MakerNoteVendor::Nikon
                        && hdr_bytes.size() >= 8U) {
                        const uint8_t hdr_b0 = u8(hdr_bytes[0]);
                        const uint8_t hdr_b1 = u8(hdr_bytes[1]);

                        TiffConfig mn_cfg;
                        mn_cfg.bigtiff = false;
                        if (hdr_b0 == 'I' && hdr_b1 == 'I') {
                            mn_cfg.le = true;
                        } else if (hdr_b0 == 'M' && hdr_b1 == 'M') {
                            mn_cfg.le = false;
                        } else {
                            mn_cfg.le = cfg.le;
                        }

                        uint32_t ifd0_off = 0;
                        (void)read_tiff_u32(mn_cfg, hdr_bytes, 4, &ifd0_off);
                        exif_internal::decode_nikon_binary_subdirs(
                            mk_ifd0, store, mn_cfg.le, mn_opts, &sink.result);
                    }
                    if (vendor == MakerNoteVendor::Pentax
                        && (hdr_abs + 2U) <= tiff_bytes.size()) {
                        const uint8_t hdr_b0 = u8(tiff_bytes[hdr_abs + 0]);
                        const uint8_t hdr_b1 = u8(tiff_bytes[hdr_abs + 1]);
                        bool le              = cfg.le;
                        if (hdr_b0 == 'I' && hdr_b1 == 'I') {
                            le = true;
                        } else if (hdr_b0 == 'M' && hdr_b1 == 'M') {
                            le = false;
                        }
                        exif_internal::decode_pentax_binary_subdirs(
                            mk_ifd0, store, le, mn_opts, &sink.result);
                    }
                    continue;
                }

                // Nikon MakerNote (older/compact cameras): classic IFD at the
                // MakerNote start, but value offsets are commonly relative to
                // the outer EXIF/TIFF header (not the MakerNote start).
                if (vendor == MakerNoteVendor::Nikon) {
                    // Nikon type1 MakerNote: "Nikon\0" + u16 version (usually 1),
                    // then a classic IFD starting at offset 8 within the
                    // MakerNote payload.
                    //
                    // The IFD value offsets are commonly TIFF-relative, so
                    // decode against the outer TIFF buffer.
                    if (mn.size() >= 10 && match_bytes(mn, 0, "Nikon\0", 6)) {
                        uint16_t ver = 0;
                        if (read_u16le(mn, 6, &ver) && ver == 1) {
                            const uint64_t ifd_off = value_off + 8ULL;
                            if (ifd_off < tiff_bytes.size()) {
                                TiffConfig mn_cfg = cfg;
                                if (!looks_like_classic_ifd(mn_cfg, tiff_bytes,
                                                            ifd_off,
                                                            options.limits)) {
                                    mn_cfg.le = !mn_cfg.le;
                                }
                                decode_classic_ifd_no_header(mn_cfg, tiff_bytes,
                                                             ifd_off, mk_ifd0,
                                                             store, mn_opts,
                                                             &sink.result,
                                                             EntryFlags::None);
                                exif_internal::decode_nikon_binary_subdirs(
                                    mk_ifd0, store, mn_cfg.le, mn_opts,
                                    &sink.result);
                                continue;
                            }
                        }
                    }

                    decode_classic_ifd_no_header(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, store, mn_opts,
                                                 &sink.result,
                                                 EntryFlags::None);
                    exif_internal::decode_nikon_binary_subdirs(mk_ifd0, store,
                                                               cfg.le, mn_opts,
                                                               &sink.result);
                    continue;
                }

                // JVC MakerNote: "JVC " header + a classic IFD at +4 (start
                // offsets are MakerNote-relative).
                if (vendor == MakerNoteVendor::Jvc && mn.size() >= 6
                    && match_bytes(mn, 0, "JVC ", 4)) {
                    static constexpr uint64_t kJvcIfdOff = 4;
                    if (kJvcIfdOff < mn.size()) {
                        TiffConfig jvc_cfg = cfg;
                        if (!looks_like_classic_ifd(jvc_cfg, mn, kJvcIfdOff,
                                                    options.limits)) {
                            jvc_cfg.le = !jvc_cfg.le;
                        }
                        decode_classic_ifd_no_header(jvc_cfg, mn, kJvcIfdOff,
                                                     mk_ifd0, store, mn_opts,
                                                     &sink.result,
                                                     EntryFlags::None);
                        continue;
                    }
                }

                // 3) Best-effort scan for a classic TIFF IFD inside MakerNote
                // (covers cases like Apple iOS, Olympus, etc.).
                ClassicIfdCandidate best;
                if (find_best_classic_ifd_candidate(mn, 256, options.limits,
                                                    &best)) {
                    TiffConfig best_cfg;
                    best_cfg.le      = best.le;
                    best_cfg.bigtiff = false;
                    decode_classic_ifd_no_header(best_cfg, mn, best.offset,
                                                 mk_ifd0, store, mn_opts,
                                                 &sink.result,
                                                 EntryFlags::None);
                    if (vendor == MakerNoteVendor::Sigma) {
                        decode_sigma_binary_subdirs(mk_ifd0, store,
                                                    mn_opts.limits,
                                                    &sink.result);
                    }
                    if (vendor == MakerNoteVendor::Sony) {
                        exif_internal::decode_sony_cipher_subdirs(mk_ifd0,
                                                                  store,
                                                                  mn_opts,
                                                                  &sink.result);
                    }
                    continue;
                }

                // 4) Canon-style MakerNotes: raw IFD starting at offset 0,
                // offsets relative to MakerNote start, using parent endianness.
                decode_classic_ifd_no_header(cfg, mn, 0, mk_ifd0, store,
                                             mn_opts, &sink.result,
                                             EntryFlags::None);
                if (vendor == MakerNoteVendor::Sigma) {
                    decode_sigma_binary_subdirs(mk_ifd0, store, mn_opts.limits,
                                                &sink.result);
                }
                if (vendor == MakerNoteVendor::Sony) {
                    exif_internal::decode_sony_cipher_subdirs(mk_ifd0, store,
                                                              mn_opts,
                                                              &sink.result);
                }
            }
        }

        if (options.decode_geotiff) {
            exif_internal::decode_geotiff_keys(cfg, tiff_bytes, geotiff_dir,
                                               geotiff_double, geotiff_ascii,
                                               store, options.limits);
        }
    }

    maybe_decode_nikon_nefinfo_blocks(store, options, &sink.result);
    exif_internal::decode_nikon_preview_aliases(store, options, &sink.result);

    if (store.resource_limit_exceeded()) {
        mark_limit_exceeded(&sink.result,
                            store.arena().limit_exceeded()
                                ? ExifLimitReason::MaxArenaBytes
                                : ExifLimitReason::MaxTotalEntries,
                            0, 0);
    }

    return sink.result;
}

namespace exif_internal {

    static void map_source_failure(SourceTiffReader* source) noexcept
    {
        if (!source || !source->result || source->result->input.ok()) {
            return;
        }

        switch (source->result->input.code) {
        case RandomAccessReadCode::RequestTooLarge:
        case RandomAccessReadCode::RequestLimitExceeded:
        case RandomAccessReadCode::ByteLimitExceeded:
        case RandomAccessReadCode::ScratchTooSmall:
            update_status(&source->result->decode,
                          ExifDecodeStatus::LimitExceeded);
            return;
        case RandomAccessReadCode::IoError:
        case RandomAccessReadCode::SourceChanged:
        case RandomAccessReadCode::Cancelled:
        case RandomAccessReadCode::ShortRead:
            update_status(&source->result->decode,
                          ExifDecodeStatus::OutputTruncated);
            return;
        case RandomAccessReadCode::InvalidArgument:
        case RandomAccessReadCode::OutOfRange:
        case RandomAccessReadCode::ContractViolation:
            update_status(&source->result->decode, ExifDecodeStatus::Malformed);
            return;
        case RandomAccessReadCode::Ok: return;
        }
    }


    bool source_tiff_view(SourceTiffReader* source, uint64_t offset,
                          uint64_t size,
                          std::span<const std::byte>* out) noexcept
    {
        if (!source || !source->range || !source->result || !out) {
            return false;
        }
        const RandomAccessViewResult view
            = random_access_read_view(*source->range, offset, size,
                                      &source->window, &source->result->input,
                                      source->limits, source->window_options);
        if (!view.ok()) {
            map_source_failure(source);
            return false;
        }
        *out = view.bytes;
        return true;
    }


    bool source_tiff_value(SourceTiffReader* source, uint64_t offset,
                           uint64_t size,
                           std::span<const std::byte>* out) noexcept
    {
        if (!source || !source->range || !source->result || !out) {
            return false;
        }
        if (size > static_cast<uint64_t>(source->value_scratch.size())) {
            if (size <= static_cast<uint64_t>(source->window.storage.size())) {
                return source_tiff_view(source, offset, size, out);
            }
            if (size > source->result->value_scratch_needed) {
                source->result->value_scratch_needed = size;
            }
            update_status(&source->result->decode,
                          ExifDecodeStatus::OutputTruncated);
            return false;
        }

        const std::span<std::byte> destination = source->value_scratch.first(
            static_cast<size_t>(size));
        const RandomAccessReadCode code
            = random_access_read_exact(*source->range, offset, destination,
                                       &source->result->input, source->limits);
        if (code != RandomAccessReadCode::Ok) {
            map_source_failure(source);
            return false;
        }
        *out = std::span<const std::byte>(destination.data(),
                                          destination.size());
        return true;
    }


    bool source_tiff_contains(const SourceTiffReader& source, uint64_t offset,
                              uint64_t size) noexcept
    {
        return source.range && offset <= source.range->size
               && size <= source.range->size - offset;
    }


    bool decode_classic_ifd_from_source(
        SourceTiffReader* source, const TiffConfig& cfg, uint64_t ifd_off,
        const OffsetPolicy& offsets, std::string_view ifd_name,
        MetaStore& store, const ExifDecodeOptions& options,
        ExifDecodeResult* status_out, EntryFlags extra_flags) noexcept
    {
        if (!source || !source->range || !source->result || cfg.bigtiff
            || ifd_name.empty()) {
            return false;
        }

        std::span<const std::byte> count_raw;
        if (!source_tiff_view(source, ifd_off, 2U, &count_raw)) {
            return false;
        }
        uint16_t entry_count = 0U;
        if (!read_tiff_u16(cfg, count_raw, 0U, &entry_count)
            || entry_count == 0U
            || entry_count > options.limits.max_entries_per_ifd) {
            return false;
        }

        uint64_t entries_off = 0U;
        uint64_t table_bytes = 0U;
        uint64_t table_end   = 0U;
        if (!checked_add_u64(ifd_off, 2U, &entries_off)
            || !checked_mul_u64(entry_count, 12U, &table_bytes)
            || !checked_add_u64(entries_off, table_bytes, &table_end)
            || !source_tiff_contains(*source, table_end, 4U)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return true;
        }

        for (uint32_t i = 0U; i < entry_count && source->result->input.ok();
             ++i) {
            uint64_t entry_delta = 0U;
            uint64_t entry_off   = 0U;
            if (!checked_mul_u64(i, 12U, &entry_delta)
                || !checked_add_u64(entries_off, entry_delta, &entry_off)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return true;
            }

            std::span<const std::byte> entry_raw;
            if (!source_tiff_view(source, entry_off, 12U, &entry_raw)) {
                return true;
            }
            ClassicIfdEntry ifd_entry;
            if (!read_classic_ifd_entry(cfg, entry_raw, 0U, &ifd_entry)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                continue;
            }

            ClassicIfdValueRef ref;
            if (!resolve_classic_ifd_value_ref(offsets, entry_off, ifd_entry,
                                               &ref, status_out)) {
                continue;
            }

            std::span<const std::byte> value_raw;
            bool have_value       = false;
            bool unreadable_value = false;
            if (ref.inline_value) {
                value_raw  = entry_raw.subspan(8U, static_cast<size_t>(
                                                      ref.value_bytes));
                have_value = true;
            } else if (!source_tiff_contains(*source, ref.value_off,
                                             ref.value_bytes)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                unreadable_value = true;
            } else if (ref.value_bytes <= options.limits.max_value_bytes) {
                have_value = source_tiff_value(source, ref.value_off,
                                               ref.value_bytes, &value_raw);
            }

            if (status_out
                && status_out->entries_decoded
                       >= options.limits.max_total_entries) {
                mark_limit_exceeded(status_out,
                                    ExifLimitReason::MaxTotalEntries, ifd_off,
                                    ifd_entry.tag);
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
            entry.flags                 = extra_flags;
            if (unreadable_value) {
                entry.flags |= EntryFlags::Unreadable;
            } else if (ref.value_bytes > options.limits.max_value_bytes
                       || !have_value) {
                entry.flags |= EntryFlags::Truncated;
            } else {
                entry.value = decode_tiff_value(cfg, value_raw, ifd_entry.type,
                                                ifd_entry.count32, 0U,
                                                ref.value_bytes, store.arena(),
                                                options.limits, status_out);
            }
            maybe_mark_contextual_name(ifd_name, ifd_entry.tag, store, &entry);
            if (store.add_entry(entry) == kInvalidEntryId) {
                mark_limit_exceeded(status_out,
                                    store.arena().limit_exceeded()
                                        ? ExifLimitReason::MaxArenaBytes
                                        : ExifLimitReason::MaxTotalEntries,
                                    ifd_off, ifd_entry.tag);
                continue;
            }
            if (status_out) {
                status_out->entries_decoded += 1U;
            }
        }
        return true;
    }

}  // namespace exif_internal

namespace {

    using exif_internal::SourceTiffReader;

    static bool range_contains_bytes(const RandomAccessSourceRange& range,
                                     uint64_t offset, uint64_t size) noexcept
    {
        return offset <= range.size && size <= range.size - offset;
    }


    static void follow_source_ifd_pointers(
        const TiffConfig& cfg, std::span<const std::byte> raw, uint16_t tag,
        uint16_t type, uint64_t count, std::span<IfdTask> stack,
        uint32_t* stack_size, uint32_t* next_subifd_index,
        const ExifDecodeLimits& limits, ExifDecodeResult* result) noexcept
    {
        (void)follow_ifd_pointers(cfg, raw, tag, type, count, 0U, stack,
                                  stack_size, next_subifd_index, limits,
                                  result);
    }


    static bool source_classic_ifd_plausible(SourceTiffReader* source,
                                             const TiffConfig& cfg,
                                             uint64_t ifd_off,
                                             const ExifDecodeLimits& limits,
                                             uint16_t* entry_count_out,
                                             uint64_t* needed_out) noexcept
    {
        std::span<const std::byte> count_raw;
        if (!source || cfg.bigtiff
            || !exif_internal::source_tiff_view(source, ifd_off, 2U,
                                                &count_raw)) {
            return false;
        }
        uint16_t entry_count = 0U;
        if (!read_tiff_u16(cfg, count_raw, 0U, &entry_count)
            || entry_count == 0U || entry_count > limits.max_entries_per_ifd) {
            return false;
        }
        uint64_t table_bytes = 0U;
        uint64_t needed      = 0U;
        if (!checked_mul_u64(entry_count, 12U, &table_bytes)
            || !checked_add_u64(6U, table_bytes, &needed)
            || !exif_internal::source_tiff_contains(*source, ifd_off, needed)) {
            return false;
        }
        if (entry_count_out) {
            *entry_count_out = entry_count;
        }
        if (needed_out) {
            *needed_out = needed;
        }
        return true;
    }


    struct SourceCanonLayout final {
        TiffConfig cfg;
        exif_internal::OffsetPolicy offsets;
        bool plausible             = false;
        bool values_within_payload = false;
    };


    static bool source_add_signed_base(int64_t base, uint32_t offset,
                                       uint64_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        const int64_t value = static_cast<int64_t>(
            static_cast<uint64_t>(offset));
        if (base > INT64_MAX - value) {
            return false;
        }
        const int64_t resolved = base + value;
        if (resolved < 0) {
            return false;
        }
        *out = static_cast<uint64_t>(resolved);
        return true;
    }


    static bool source_score_canon_bases(
        SourceTiffReader* source, const TiffConfig& cfg,
        uint64_t maker_note_off, uint64_t maker_note_bytes,
        uint16_t entry_count, uint64_t ifd_needed, const int64_t (&bases)[4],
        const bool (&enabled)[4], const ExifDecodeLimits& limits,
        uint32_t (&scores)[4], bool (&all_in_payload)[4]) noexcept
    {
        bool all_in[4] { true, true, true, true };
        bool saw_value = false;
        for (uint32_t i = 0U; i < entry_count; ++i) {
            uint64_t entries_off = 0U;
            uint64_t entry_delta = 0U;
            uint64_t entry_off   = 0U;
            if (!checked_add_u64(maker_note_off, 2U, &entries_off)
                || !checked_mul_u64(i, 12U, &entry_delta)
                || !checked_add_u64(entries_off, entry_delta, &entry_off)) {
                return false;
            }
            std::span<const std::byte> raw;
            if (!exif_internal::source_tiff_view(source, entry_off, 12U, &raw)) {
                return false;
            }
            exif_internal::ClassicIfdEntry entry;
            if (!exif_internal::read_classic_ifd_entry(cfg, raw, 0U, &entry)) {
                return false;
            }
            uint64_t value_bytes = 0U;
            if (!exif_internal::classic_ifd_entry_value_bytes(entry,
                                                              &value_bytes)
                || value_bytes <= 4U || value_bytes > limits.max_value_bytes) {
                continue;
            }
            saw_value = true;
            for (uint32_t candidate = 0U; candidate < 4U; ++candidate) {
                if (!enabled[candidate]) {
                    continue;
                }
                uint64_t value_off = 0U;
                if (!source_add_signed_base(bases[candidate],
                                            entry.value_or_off32, &value_off)
                    || !exif_internal::source_tiff_contains(*source, value_off,
                                                            value_bytes)) {
                    all_in[candidate] = false;
                    continue;
                }
                scores[candidate] += 1U;
                const bool in_payload
                    = value_off >= maker_note_off
                      && value_off - maker_note_off <= maker_note_bytes
                      && value_bytes
                             <= maker_note_bytes - (value_off - maker_note_off);
                if (in_payload) {
                    scores[candidate] += 2U;
                    if (value_off - maker_note_off >= ifd_needed) {
                        scores[candidate] += 1U;
                    }
                } else {
                    all_in[candidate] = false;
                }
            }
        }
        for (uint32_t candidate = 0U; candidate < 4U; ++candidate) {
            all_in_payload[candidate] = enabled[candidate]
                                        && (!saw_value || all_in[candidate]);
        }
        return true;
    }


    static SourceCanonLayout select_source_canon_layout(
        SourceTiffReader* source, const TiffConfig& parent_cfg,
        uint64_t maker_note_off, uint64_t maker_note_bytes,
        const MetaStore& store, const ExifDecodeOptions& options) noexcept
    {
        SourceCanonLayout choice;
        choice.cfg           = parent_cfg;
        uint16_t entry_count = 0U;
        uint64_t needed      = 0U;
        if (!source_classic_ifd_plausible(source, choice.cfg, maker_note_off,
                                          options.limits, &entry_count,
                                          &needed)) {
            choice.cfg.le = !choice.cfg.le;
            if (!source_classic_ifd_plausible(source, choice.cfg,
                                              maker_note_off, options.limits,
                                              &entry_count, &needed)) {
                return choice;
            }
        }

        uint64_t min_offset = UINT64_MAX;
        for (uint32_t i = 0U; i < entry_count; ++i) {
            uint64_t entries_off = 0U;
            uint64_t entry_delta = 0U;
            uint64_t entry_off   = 0U;
            if (!checked_add_u64(maker_note_off, 2U, &entries_off)
                || !checked_mul_u64(i, 12U, &entry_delta)
                || !checked_add_u64(entries_off, entry_delta, &entry_off)) {
                return choice;
            }
            std::span<const std::byte> raw;
            if (!exif_internal::source_tiff_view(source, entry_off, 12U, &raw)) {
                return choice;
            }
            exif_internal::ClassicIfdEntry entry;
            uint64_t value_bytes = 0U;
            if (!exif_internal::read_classic_ifd_entry(choice.cfg, raw, 0U,
                                                       &entry)
                || !exif_internal::classic_ifd_entry_value_bytes(entry,
                                                                 &value_bytes)) {
                continue;
            }
            if (value_bytes > 4U && entry.value_or_off32 >= needed
                && entry.value_or_off32 < min_offset) {
                min_offset = entry.value_or_off32;
            }
        }

        int32_t offset_schema = 0;
        exif_internal::ExifContext ctx(store);
        const bool have_offset_schema = ctx.find_first_i32("exififd", 0xEA1DU,
                                                           &offset_schema);

        int64_t candidates[4] {};
        bool enabled[4] { true, false, false, false };
        enabled[1] = maker_note_off <= static_cast<uint64_t>(INT64_MAX);
        if (enabled[1]) {
            candidates[1] = static_cast<int64_t>(maker_note_off);
        }
        if (min_offset != UINT64_MAX
            && maker_note_off <= static_cast<uint64_t>(INT64_MAX)
            && needed <= static_cast<uint64_t>(INT64_MAX)
            && min_offset <= static_cast<uint64_t>(INT64_MAX)
            && maker_note_off <= static_cast<uint64_t>(INT64_MAX) - needed) {
            candidates[2] = static_cast<int64_t>(maker_note_off + needed)
                            - static_cast<int64_t>(min_offset);
            enabled[2] = true;
        }
        if (have_offset_schema && enabled[1]) {
            const int64_t base  = candidates[1];
            const int64_t delta = offset_schema;
            if (!((delta > 0 && base > INT64_MAX - delta)
                  || (delta < 0 && base < INT64_MIN - delta))) {
                candidates[3] = base + delta;
                enabled[3]    = true;
            }
        }

        uint32_t best_score = 0U;
        uint32_t best_index = 0U;
        bool best_in        = false;
        uint32_t scores[4] {};
        bool all_in[4] {};
        if (!source_score_canon_bases(source, choice.cfg, maker_note_off,
                                      maker_note_bytes, entry_count, needed,
                                      candidates, enabled, options.limits,
                                      scores, all_in)) {
            return choice;
        }
        for (uint32_t i = 0U; i < 4U; ++i) {
            if (!enabled[i]) {
                continue;
            }
            if (scores[i] > best_score
                || (scores[i] == best_score && all_in[i] && !best_in)) {
                best_score = scores[i];
                best_index = i;
                best_in    = all_in[i];
            }
        }

        choice.offsets.out_of_line_base_is_signed = true;
        choice.offsets.out_of_line_base_i64       = candidates[best_index];
        choice.values_within_payload              = best_in;
        choice.plausible                          = true;
        return choice;
    }


    static bool merge_nested_source_input(
        SourceTiffReader* source, uint64_t nested_offset,
        const ExifRandomAccessDecodeResult& nested) noexcept
    {
        if (!source || !source->result) {
            return false;
        }
        RandomAccessReadState& outer = source->result->input;
        if (nested.input.requests_issued > UINT32_MAX - outer.requests_issued
            || nested.input.bytes_requested > UINT64_MAX - outer.bytes_requested
            || nested.input.bytes_completed
                   > UINT64_MAX - outer.bytes_completed) {
            outer.code = RandomAccessReadCode::ContractViolation;
            update_status(&source->result->decode, ExifDecodeStatus::Malformed);
            return false;
        }
        outer.requests_issued += nested.input.requests_issued;
        outer.bytes_requested += nested.input.bytes_requested;
        outer.bytes_completed += nested.input.bytes_completed;
        if (!nested.input.ok() && outer.ok()) {
            outer.code    = nested.input.code;
            outer.io_code = nested.input.io_code;
            if (!checked_add_u64(nested_offset, nested.input.failure_offset,
                                 &outer.failure_offset)) {
                outer.code           = RandomAccessReadCode::ContractViolation;
                outer.failure_offset = nested_offset;
            }
            outer.failure_request_bytes = nested.input.failure_request_bytes;
            outer.failure_bytes_read    = nested.input.failure_bytes_read;
        }
        update_status(&source->result->decode, nested.decode.status);
        if (nested.decode.limit_reason != ExifLimitReason::None
            && source->result->decode.limit_reason == ExifLimitReason::None) {
            source->result->decode.limit_reason = nested.decode.limit_reason;
            source->result->decode.limit_ifd_offset
                = nested.decode.limit_ifd_offset;
            source->result->decode.limit_tag = nested.decode.limit_tag;
        }
        if (nested.value_scratch_needed
            > source->result->value_scratch_needed) {
            source->result->value_scratch_needed = nested.value_scratch_needed;
        }
        if (nested.nested_payloads_skipped
            > UINT32_MAX - source->result->nested_payloads_skipped) {
            source->result->nested_payloads_skipped = UINT32_MAX;
            update_status(&source->result->decode,
                          ExifDecodeStatus::LimitExceeded);
        } else {
            source->result->nested_payloads_skipped
                += nested.nested_payloads_skipped;
        }
        exif_internal::map_source_failure(source);
        source->window.reset();
        return nested.complete();
    }


    static bool
    decode_source_embedded_tiff(SourceTiffReader* source,
                                uint64_t header_offset, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifRandomAccessDecodeResult* result) noexcept
    {
        if (!source || !source->range || !result
            || header_offset >= source->range->size) {
            return false;
        }
        uint64_t source_offset = 0U;
        if (!checked_add_u64(source->range->source_offset, header_offset,
                             &source_offset)) {
            update_status(&result->decode, ExifDecodeStatus::Malformed);
            return false;
        }

        RandomAccessReadLimits nested_limits = source->limits;
        if (nested_limits.max_requests != 0U) {
            if (result->input.requests_issued >= nested_limits.max_requests) {
                result->input.code = RandomAccessReadCode::RequestLimitExceeded;
                update_status(&result->decode, ExifDecodeStatus::LimitExceeded);
                return false;
            }
            nested_limits.max_requests -= result->input.requests_issued;
        }
        if (nested_limits.max_total_bytes != 0U) {
            if (result->input.bytes_requested
                >= nested_limits.max_total_bytes) {
                result->input.code = RandomAccessReadCode::ByteLimitExceeded;
                update_status(&result->decode, ExifDecodeStatus::LimitExceeded);
                return false;
            }
            nested_limits.max_total_bytes -= result->input.bytes_requested;
        }

        const RandomAccessSourceRange nested_range
            = make_random_access_source_range(source->range->source,
                                              source_offset,
                                              source->range->size
                                                  - header_offset);
        ExifRandomAccessScratch nested_scratch;
        nested_scratch.read_window    = source->window.storage;
        nested_scratch.value          = source->value_scratch;
        nested_scratch.window_options = source->window_options;
        std::array<ExifIfdRef, 128> nested_ifds {};
        const ExifRandomAccessDecodeResult nested
            = decode_exif_tiff_random_access(nested_range, store, nested_ifds,
                                             nested_scratch, options,
                                             nested_limits);
        (void)merge_nested_source_input(source, header_offset, nested);
        return nested.input.ok()
               && nested.decode.status != ExifDecodeStatus::Unsupported;
    }


    static void
    decode_source_makernote(SourceTiffReader* source, const TiffConfig& cfg,
                            uint64_t maker_note_off, uint64_t maker_note_bytes,
                            std::span<const std::byte> maker_note,
                            MetaStore& store, const ExifDecodeOptions& options,
                            ExifRandomAccessDecodeResult* result) noexcept
    {
        const MakerNoteVendor vendor = detect_makernote_vendor(maker_note,
                                                               store);
        ExifDecodeOptions mn_options = options;
        mn_options.decode_printim    = false;
        mn_options.decode_makernote  = false;
        set_makernote_tokens(&mn_options, vendor);

        char token_scratch[64];
        const std::string_view maker_ifd
            = ifd_token(mn_options.tokens, ExifIfdKind::Ifd, 0,
                        std::span<char>(token_scratch));

        if (vendor == MakerNoteVendor::Canon && source) {
            const SourceCanonLayout layout
                = select_source_canon_layout(source, cfg, maker_note_off,
                                             maker_note_bytes, store, options);
            if (layout.plausible && layout.values_within_payload
                && exif_internal::decode_canon_makernote(
                    layout.cfg, maker_note, 0U, maker_note.size(), maker_ifd,
                    store, mn_options, &result->decode)) {
                return;
            }
            if (layout.plausible
                && exif_internal::decode_classic_ifd_from_source(
                    source, layout.cfg, maker_note_off, layout.offsets,
                    maker_ifd, store, mn_options, &result->decode,
                    EntryFlags::None)) {
                // Canon's main IFD is source-backed, but derived BinaryData
                // tables still require conversion from span-only helpers when
                // their values lie outside the declared MakerNote payload.
                result->nested_payloads_skipped += 1U;
                return;
            }
        }

        if (vendor == MakerNoteVendor::Nikon && source) {
            const uint64_t header_local = find_embedded_tiff_header(maker_note,
                                                                    128U);
            if (header_local != UINT64_MAX
                && header_local <= UINT64_MAX - maker_note_off) {
                bool maker_le = cfg.le;
                if (header_local + 2U <= maker_note.size()) {
                    const uint8_t b0 = u8(maker_note[header_local]);
                    const uint8_t b1 = u8(maker_note[header_local + 1U]);
                    if (b0 == 'I' && b1 == 'I') {
                        maker_le = true;
                    } else if (b0 == 'M' && b1 == 'M') {
                        maker_le = false;
                    }
                }
                if (decode_source_embedded_tiff(source,
                                                maker_note_off + header_local,
                                                store, mn_options, result)) {
                    exif_internal::decode_nikon_binary_subdirs(maker_ifd, store,
                                                               maker_le,
                                                               mn_options,
                                                               &result->decode);
                    return;
                }
                if (!result->input.ok()) {
                    return;
                }
            }

            uint64_t ifd_off     = maker_note_off;
            TiffConfig maker_cfg = cfg;
            if (maker_note.size() >= 10U
                && match_bytes(maker_note, 0U, "Nikon\0", 6U)) {
                uint16_t version = 0U;
                if (read_u16le(maker_note, 6U, &version) && version == 1U) {
                    ifd_off += 8U;
                }
            }
            if (!source_classic_ifd_plausible(source, maker_cfg, ifd_off,
                                              options.limits, nullptr,
                                              nullptr)) {
                maker_cfg.le = !maker_cfg.le;
            }
            exif_internal::OffsetPolicy offsets;
            if (exif_internal::decode_classic_ifd_from_source(
                    source, maker_cfg, ifd_off, offsets, maker_ifd, store,
                    mn_options, &result->decode, EntryFlags::None)) {
                exif_internal::decode_nikon_binary_subdirs(maker_ifd, store,
                                                           maker_cfg.le,
                                                           mn_options,
                                                           &result->decode);
                return;
            }
        }

        if (vendor == MakerNoteVendor::Sony && source) {
            ClassicIfdCandidate best;
            bool found         = false;
            uint64_t known_ifd = UINT64_MAX;
            if (maker_note.size() >= 6U
                && match_bytes(maker_note, 0U, "SONY", 4U)) {
                known_ifd = 4U;
            } else if (maker_note.size() >= 14U
                       && match_bytes(maker_note, 0U, "VHAB", 4U)) {
                known_ifd = 12U;
            }
            if (known_ifd != UINT64_MAX
                && known_ifd <= UINT64_MAX - maker_note_off) {
                TiffConfig candidate_cfg = cfg;
                uint16_t count           = 0U;
                uint64_t needed          = 0U;
                if (!source_classic_ifd_plausible(source, candidate_cfg,
                                                  maker_note_off + known_ifd,
                                                  options.limits, &count,
                                                  &needed)) {
                    candidate_cfg.le = !candidate_cfg.le;
                }
                if (source_classic_ifd_plausible(source, candidate_cfg,
                                                 maker_note_off + known_ifd,
                                                 options.limits, &count,
                                                 &needed)) {
                    best.offset        = known_ifd;
                    best.le            = candidate_cfg.le;
                    best.entry_count   = count;
                    best.valid_entries = count;
                    found              = true;
                }
            }
            if (!found) {
                found = find_best_classic_ifd_candidate(maker_note, 256U,
                                                        options.limits, &best);
            }
            if (!found) {
                uint16_t count  = 0U;
                uint64_t needed = 0U;
                if (source_classic_ifd_plausible(source, cfg, maker_note_off,
                                                 options.limits, &count,
                                                 &needed)) {
                    best.offset        = 0U;
                    best.le            = cfg.le;
                    best.entry_count   = count;
                    best.valid_entries = count;
                    found              = true;
                }
            }
            if (found && best.offset <= UINT64_MAX - maker_note_off) {
                TiffConfig maker_cfg;
                maker_cfg.le      = best.le;
                maker_cfg.bigtiff = false;
                exif_internal::OffsetPolicy offsets;
                if (exif_internal::decode_classic_ifd_from_source(
                        source, maker_cfg, maker_note_off + best.offset,
                        offsets, maker_ifd, store, mn_options, &result->decode,
                        EntryFlags::None)) {
                    exif_internal::decode_sony_cipher_subdirs(maker_ifd, store,
                                                              mn_options,
                                                              &result->decode);
                    return;
                }
            }
        }

        if (vendor == MakerNoteVendor::Pentax
            && exif_internal::decode_pentax_makernote(maker_note, maker_ifd,
                                                      store, mn_options,
                                                      &result->decode)) {
            return;
        }
        if (vendor == MakerNoteVendor::Hp
            && exif_internal::decode_hp_makernote(maker_note, maker_ifd, store,
                                                  mn_options,
                                                  &result->decode)) {
            return;
        }
        if (vendor == MakerNoteVendor::Reconyx
            && exif_internal::decode_reconyx_makernote(maker_note, maker_ifd,
                                                       store, mn_options,
                                                       &result->decode)) {
            return;
        }
        if (vendor == MakerNoteVendor::PhaseOne
            && decode_phaseone_makernote(maker_note, maker_ifd, store,
                                         mn_options, &result->decode)) {
            return;
        }

        if (vendor == MakerNoteVendor::Jvc && maker_note.size() >= 6U
            && match_bytes(maker_note, 0U, "JVC ", 4U)) {
            static constexpr uint64_t kJvcIfdOffset = 4U;
            TiffConfig maker_cfg                    = cfg;
            if (!looks_like_classic_ifd(maker_cfg, maker_note, kJvcIfdOffset,
                                        options.limits)) {
                maker_cfg.le = !maker_cfg.le;
            }
            decode_classic_ifd_no_header(maker_cfg, maker_note, kJvcIfdOffset,
                                         maker_ifd, store, mn_options,
                                         &result->decode, EntryFlags::None);
            return;
        }

        if (vendor == MakerNoteVendor::Unknown
            || vendor == MakerNoteVendor::Sigma
            || vendor == MakerNoteVendor::Apple) {
            ClassicIfdCandidate best;
            if (find_best_classic_ifd_candidate(maker_note, 256U,
                                                options.limits, &best)) {
                TiffConfig maker_cfg;
                maker_cfg.le      = best.le;
                maker_cfg.bigtiff = false;
                decode_classic_ifd_no_header(maker_cfg, maker_note, best.offset,
                                             maker_ifd, store, mn_options,
                                             &result->decode, EntryFlags::None);
                if (vendor == MakerNoteVendor::Sigma) {
                    decode_sigma_binary_subdirs(maker_ifd, store,
                                                mn_options.limits,
                                                &result->decode);
                }
                return;
            }
        }

        result->nested_payloads_skipped += 1U;
    }


    static void
    decode_source_geotiff(const TiffConfig& cfg, SourceTiffReader* source,
                          exif_internal::GeoTiffTagRef key_directory,
                          exif_internal::GeoTiffTagRef double_params,
                          exif_internal::GeoTiffTagRef ascii_params,
                          MetaStore& store,
                          const ExifDecodeLimits& limits) noexcept
    {
        if (!key_directory.present || !source || !source->result) {
            return;
        }

        uint64_t total = 0U;
        const exif_internal::GeoTiffTagRef refs[3]
            = { key_directory, double_params, ascii_params };
        for (const exif_internal::GeoTiffTagRef& ref : refs) {
            if (ref.present
                && (!checked_add_u64(total, ref.value_bytes, &total))) {
                update_status(&source->result->decode,
                              ExifDecodeStatus::Malformed);
                return;
            }
        }
        if (total > static_cast<uint64_t>(source->value_scratch.size())) {
            if (total > source->result->value_scratch_needed) {
                source->result->value_scratch_needed = total;
            }
            update_status(&source->result->decode,
                          ExifDecodeStatus::OutputTruncated);
            return;
        }

        exif_internal::GeoTiffTagRef compact[3]
            = { key_directory, double_params, ascii_params };
        uint64_t destination_offset = 0U;
        for (uint32_t i = 0U; i < 3U; ++i) {
            if (!compact[i].present) {
                continue;
            }
            const std::span<std::byte> destination
                = source->value_scratch.subspan(
                    static_cast<size_t>(destination_offset),
                    static_cast<size_t>(compact[i].value_bytes));
            const RandomAccessReadCode code
                = random_access_read_exact(*source->range, compact[i].value_off,
                                           destination, &source->result->input,
                                           source->limits);
            if (code != RandomAccessReadCode::Ok) {
                map_source_failure(source);
                return;
            }
            compact[i].value_off = destination_offset;
            destination_offset += compact[i].value_bytes;
        }

        const std::span<const std::byte> compact_bytes(
            source->value_scratch.data(), static_cast<size_t>(total));
        exif_internal::decode_geotiff_keys(cfg, compact_bytes, compact[0],
                                           compact[1], compact[2], store,
                                           limits);
    }

}  // namespace


ExifRandomAccessDecodeResult
decode_exif_tiff_random_access(
    const RandomAccessSourceRange& tiff, MetaStore& store,
    std::span<ExifIfdRef> out_ifds, const ExifRandomAccessScratch& scratch,
    const ExifDecodeOptions& options,
    const RandomAccessReadLimits& read_limits) noexcept
{
    ExifRandomAccessDecodeResult result;
    if (!random_access_source_range_valid(tiff)) {
        result.input.code    = RandomAccessReadCode::InvalidArgument;
        result.decode.status = ExifDecodeStatus::Malformed;
        return result;
    }

    if (tiff.source.contiguous_data != nullptr) {
        const std::span<const std::byte> bytes(tiff.source.contiguous_data
                                                   + static_cast<size_t>(
                                                       tiff.source_offset),
                                               static_cast<size_t>(tiff.size));
        result.decode = decode_exif_tiff_contiguous(bytes, store, out_ifds,
                                                    options);
        return result;
    }

    IfdSink sink;
    sink.out = out_ifds.data();
    sink.cap = static_cast<uint32_t>(out_ifds.size());

    store.constrain_resources(options.limits.max_total_entries,
                              options.limits.max_arena_bytes);
    if (store.resource_limit_exceeded()) {
        mark_limit_exceeded(&sink.result, ExifLimitReason::MaxArenaBytes, 0U,
                            0U);
        result.decode = sink.result;
        return result;
    }
    if (tiff.size < 8U) {
        result.decode.status = ExifDecodeStatus::Malformed;
        return result;
    }

    SourceTiffReader source;
    source.range          = &tiff;
    source.window.storage = scratch.read_window;
    source.value_scratch  = scratch.value;
    source.limits         = read_limits;
    source.window_options = scratch.window_options;
    source.result         = &result;

    std::span<const std::byte> header;
    if (!exif_internal::source_tiff_view(&source, 0U, 8U, &header)) {
        return result;
    }

    TiffConfig cfg;
    const uint8_t byte0 = u8(header[0]);
    const uint8_t byte1 = u8(header[1]);
    if (byte0 == 0x49U && byte1 == 0x49U) {
        cfg.le = true;
    } else if (byte0 == 0x4DU && byte1 == 0x4DU) {
        cfg.le = false;
    } else {
        result.decode.status = ExifDecodeStatus::Unsupported;
        return result;
    }

    uint16_t version = 0U;
    if (!read_tiff_u16(cfg, header, 2U, &version)) {
        result.decode.status = ExifDecodeStatus::Malformed;
        return result;
    }
    if (version == 42U || version == 0x0055U || version == 0x4F52U) {
        cfg.bigtiff = false;
    } else if (version == 43U) {
        cfg.bigtiff = true;
    } else {
        result.decode.status = ExifDecodeStatus::Unsupported;
        return result;
    }

    uint64_t first_ifd = 0U;
    if (!cfg.bigtiff) {
        uint32_t offset = 0U;
        if (!read_tiff_u32(cfg, header, 4U, &offset)) {
            result.decode.status = ExifDecodeStatus::Malformed;
            return result;
        }
        first_ifd = offset;
    } else {
        if (tiff.size < 16U
            || !exif_internal::source_tiff_view(&source, 0U, 16U, &header)) {
            if (result.input.ok()) {
                result.decode.status = ExifDecodeStatus::Malformed;
            }
            return result;
        }
        uint16_t offset_size = 0U;
        uint16_t reserved    = 0U;
        if (!read_tiff_u16(cfg, header, 4U, &offset_size)
            || !read_tiff_u16(cfg, header, 6U, &reserved)
            || !read_tiff_u64(cfg, header, 8U, &first_ifd) || offset_size != 8U
            || reserved != 0U) {
            result.decode.status = ExifDecodeStatus::Malformed;
            return result;
        }
    }

    std::array<IfdTask, 256> tasks {};
    std::array<uint64_t, 256> visited_offsets {};
    std::array<uint8_t, 256> visited_masks {};
    uint32_t task_count    = 0U;
    uint32_t visited_count = 0U;
    uint32_t next_subifd   = 0U;
    if (first_ifd != 0U) {
        tasks[0]   = IfdTask { ExifIfdKind::Ifd, 0U, first_ifd };
        task_count = 1U;
    }

    while (task_count > 0U && result.input.ok()) {
        const uint32_t selected
            = select_next_task_index(std::span<const IfdTask>(tasks),
                                     task_count);
        const IfdTask task = tasks[selected];
        tasks[selected]    = tasks[task_count - 1U];
        task_count -= 1U;

        if (task.offset == 0U || task.offset >= tiff.size) {
            continue;
        }

        const uint8_t kind_bit = ifd_kind_bit(task.kind);
        const uint32_t visited
            = find_visited(task.offset,
                           std::span<const uint64_t>(visited_offsets),
                           visited_count);
        if (visited != UINT32_MAX) {
            const uint8_t mask = visited_masks[visited];
            if ((mask & kind_bit) != 0U
                || !allow_revisit_kind(task.kind, mask)) {
                continue;
            }
            visited_masks[visited] = static_cast<uint8_t>(mask | kind_bit);
        } else if (visited_count < visited_offsets.size()) {
            visited_offsets[visited_count] = task.offset;
            visited_masks[visited_count]   = kind_bit;
            visited_count += 1U;
        } else {
            mark_limit_exceeded(&result.decode, ExifLimitReason::MaxIfds,
                                task.offset, 0U);
            break;
        }

        if (sink.result.ifds_needed >= options.limits.max_ifds) {
            mark_limit_exceeded(&result.decode, ExifLimitReason::MaxIfds,
                                task.offset, 0U);
            break;
        }

        const uint64_t count_bytes = cfg.bigtiff ? 8U : 2U;
        std::span<const std::byte> count_raw;
        if (!exif_internal::source_tiff_view(&source, task.offset, count_bytes,
                                             &count_raw)) {
            break;
        }
        uint64_t entry_count = 0U;
        if (cfg.bigtiff) {
            if (!read_tiff_u64(cfg, count_raw, 0U, &entry_count)) {
                update_status(&result.decode, ExifDecodeStatus::Malformed);
                continue;
            }
        } else {
            uint16_t count16 = 0U;
            if (!read_tiff_u16(cfg, count_raw, 0U, &count16)) {
                update_status(&result.decode, ExifDecodeStatus::Malformed);
                continue;
            }
            entry_count = count16;
        }
        if (entry_count > options.limits.max_entries_per_ifd) {
            mark_limit_exceeded(&result.decode,
                                ExifLimitReason::MaxEntriesPerIfd, task.offset,
                                0U);
            continue;
        }

        uint64_t entries_offset    = 0U;
        uint64_t table_bytes       = 0U;
        uint64_t next_offset       = 0U;
        const uint64_t entry_bytes = cfg.bigtiff ? 20U : 12U;
        if (!checked_add_u64(task.offset, count_bytes, &entries_offset)
            || !checked_mul_u64(entry_count, entry_bytes, &table_bytes)
            || !checked_add_u64(entries_offset, table_bytes, &next_offset)
            || !range_contains_bytes(tiff, entries_offset, table_bytes)) {
            update_status(&result.decode, ExifDecodeStatus::Malformed);
            continue;
        }

        if (result.decode.entries_decoded + entry_count
            > options.limits.max_total_entries) {
            mark_limit_exceeded(&result.decode,
                                ExifLimitReason::MaxTotalEntries, task.offset,
                                0U);
            continue;
        }

        const BlockId block = store.add_block(BlockInfo {});
        ExifIfdRef ifd_ref;
        ifd_ref.kind   = task.kind;
        ifd_ref.index  = task.index;
        ifd_ref.offset = task.offset;
        ifd_ref.block  = block;
        sink.result    = result.decode;
        sink_emit(&sink, ifd_ref);
        result.decode = sink.result;

        char token_scratch[64];
        const std::string_view ifd_name
            = ifd_token(options.tokens, task.kind, task.index,
                        std::span<char>(token_scratch));
        if (ifd_name.empty()) {
            update_status(&result.decode, ExifDecodeStatus::Malformed);
            continue;
        }

        exif_internal::GeoTiffTagRef geotiff_directory;
        exif_internal::GeoTiffTagRef geotiff_double;
        exif_internal::GeoTiffTagRef geotiff_ascii;

        for (uint64_t index = 0U; index < entry_count && result.input.ok();
             ++index) {
            uint64_t entry_delta  = 0U;
            uint64_t entry_offset = 0U;
            if (!checked_mul_u64(index, entry_bytes, &entry_delta)
                || !checked_add_u64(entries_offset, entry_delta,
                                    &entry_offset)) {
                update_status(&result.decode, ExifDecodeStatus::Malformed);
                continue;
            }

            std::span<const std::byte> entry_raw;
            if (!exif_internal::source_tiff_view(&source, entry_offset,
                                                 entry_bytes, &entry_raw)) {
                break;
            }
            uint16_t tag  = 0U;
            uint16_t type = 0U;
            if (!read_tiff_u16(cfg, entry_raw, 0U, &tag)
                || !read_tiff_u16(cfg, entry_raw, 2U, &type)) {
                update_status(&result.decode, ExifDecodeStatus::Malformed);
                continue;
            }

            uint64_t count             = 0U;
            uint64_t value_or_offset   = 0U;
            uint64_t value_field_local = cfg.bigtiff ? 12U : 8U;
            if (cfg.bigtiff) {
                if (!read_tiff_u64(cfg, entry_raw, 4U, &count)
                    || !read_tiff_u64(cfg, entry_raw, 12U, &value_or_offset)) {
                    update_status(&result.decode, ExifDecodeStatus::Malformed);
                    continue;
                }
            } else {
                uint32_t count32 = 0U;
                uint32_t value32 = 0U;
                if (!read_tiff_u32(cfg, entry_raw, 4U, &count32)
                    || !read_tiff_u32(cfg, entry_raw, 8U, &value32)) {
                    update_status(&result.decode, ExifDecodeStatus::Malformed);
                    continue;
                }
                count           = count32;
                value_or_offset = value32;
            }

            const uint64_t unit  = tiff_type_size(type);
            uint64_t value_bytes = 0U;
            if (unit == 0U) {
                continue;
            }
            if (!checked_mul_u64(count, unit, &value_bytes)) {
                update_status(&result.decode, ExifDecodeStatus::Malformed);
                continue;
            }

            const uint64_t inline_capacity = cfg.bigtiff ? 8U : 4U;
            const bool inline_value        = value_bytes <= inline_capacity;
            uint64_t value_offset          = value_or_offset;
            std::span<const std::byte> value_raw;
            bool have_value = false;
            if (inline_value) {
                value_offset = entry_offset + value_field_local;
                value_raw
                    = entry_raw.subspan(static_cast<size_t>(value_field_local),
                                        static_cast<size_t>(value_bytes));
                have_value = true;
            } else if (!range_contains_bytes(tiff, value_offset, value_bytes)) {
                update_status(&result.decode, ExifDecodeStatus::Malformed);
                continue;
            } else if (value_bytes <= options.limits.max_value_bytes) {
                have_value
                    = exif_internal::source_tiff_value(&source, value_offset,
                                                       value_bytes, &value_raw);
            }

            if (options.decode_geotiff && count <= UINT32_MAX) {
                exif_internal::GeoTiffTagRef* geotiff = nullptr;
                if (tag == 0x87AFU) {
                    geotiff = &geotiff_directory;
                } else if (tag == 0x87B0U) {
                    geotiff = &geotiff_double;
                } else if (tag == 0x87B1U) {
                    geotiff = &geotiff_ascii;
                }
                if (geotiff) {
                    geotiff->present     = true;
                    geotiff->type        = type;
                    geotiff->count32     = static_cast<uint32_t>(count);
                    geotiff->value_off   = value_offset;
                    geotiff->value_bytes = value_bytes;
                }
            }

            if (have_value) {
                follow_source_ifd_pointers(cfg, value_raw, tag, type, count,
                                           std::span<IfdTask>(tasks),
                                           &task_count, &next_subifd,
                                           options.limits, &result.decode);
            }

            if (count > UINT32_MAX) {
                mark_limit_exceeded(&result.decode,
                                    ExifLimitReason::ValueCountTooLarge,
                                    task.offset, tag);
                continue;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = static_cast<uint32_t>(index);
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);
            if (value_bytes > options.limits.max_value_bytes || !have_value) {
                entry.flags |= EntryFlags::Truncated;
            } else {
                entry.value = decode_tiff_value(cfg, value_raw, type, count, 0U,
                                                value_bytes, store.arena(),
                                                options.limits, &result.decode);
            }

            if (!options.include_pointer_tags
                && (tag == 0x8769U || tag == 0x8825U || tag == 0xA005U
                    || tag == 0x014AU)) {
                continue;
            }
            maybe_mark_contextual_name(ifd_name, tag, store, &entry);
            if (store.add_entry(entry) == kInvalidEntryId) {
                mark_limit_exceeded(&result.decode,
                                    store.arena().limit_exceeded()
                                        ? ExifLimitReason::MaxArenaBytes
                                        : ExifLimitReason::MaxTotalEntries,
                                    task.offset, tag);
                continue;
            }
            result.decode.entries_decoded += 1U;

            if (have_value && options.decode_printim && tag == 0xC4A5U
                && value_bytes != 0U) {
                PrintImDecodeLimits print_limits;
                print_limits.max_entries = options.limits.max_entries_per_ifd;
                print_limits.max_bytes   = options.limits.max_value_bytes;
                const PrintImDecodeResult print_result
                    = decode_printim(value_raw, store, print_limits);
                if (print_result.status == PrintImDecodeStatus::LimitExceeded) {
                    mark_limit_exceeded(&result.decode,
                                        ExifLimitReason::MaxTotalEntries,
                                        task.offset, tag);
                }
            }

            if (have_value && options.decode_makernote && tag == 0xC634U
                && value_bytes != 0U) {
                const MakerNoteVendor vendor
                    = detect_makernote_vendor(value_raw, store);
                if (vendor == MakerNoteVendor::Pentax) {
                    ExifDecodeOptions maker_options = options;
                    maker_options.decode_printim    = false;
                    maker_options.decode_makernote  = false;
                    set_makernote_tokens(&maker_options, vendor);
                    char maker_token_scratch[64];
                    const std::string_view maker_ifd
                        = ifd_token(maker_options.tokens, ExifIfdKind::Ifd, 0U,
                                    std::span<char>(maker_token_scratch));
                    (void)exif_internal::decode_pentax_makernote(
                        value_raw, maker_ifd, store, maker_options,
                        &result.decode);
                }
            }

            if (have_value && options.decode_makernote && tag == 0x927CU
                && value_bytes != 0U) {
                decode_source_makernote(&source, cfg, value_offset, value_bytes,
                                        value_raw, store, options, &result);
            } else if (!have_value && options.decode_makernote
                       && (tag == 0x927CU || tag == 0xC634U)
                       && value_bytes != 0U
                       && result.value_scratch_needed == 0U) {
                // A configured metadata-value ceiling may intentionally skip
                // the bytes before vendor detection. Keep completion
                // conservative when nested decoding was requested.
                result.nested_payloads_skipped += 1U;
            }
        }

        if (task.kind == ExifIfdKind::Ifd && result.input.ok()) {
            const uint64_t pointer_bytes = cfg.bigtiff ? 8U : 4U;
            if (range_contains_bytes(tiff, next_offset, pointer_bytes)) {
                std::span<const std::byte> next_raw;
                if (!exif_internal::source_tiff_view(&source, next_offset,
                                                     pointer_bytes,
                                                     &next_raw)) {
                    break;
                }
                uint64_t following = 0U;
                if (cfg.bigtiff) {
                    (void)read_tiff_u64(cfg, next_raw, 0U, &following);
                } else {
                    uint32_t following32 = 0U;
                    (void)read_tiff_u32(cfg, next_raw, 0U, &following32);
                    following = following32;
                }
                if (following != 0U) {
                    if (task_count < tasks.size()
                        && task_count < options.limits.max_ifds) {
                        tasks[task_count] = IfdTask { ExifIfdKind::Ifd,
                                                      task.index + 1U,
                                                      following };
                        task_count += 1U;
                    } else {
                        mark_limit_exceeded(&result.decode,
                                            ExifLimitReason::MaxIfds,
                                            task.offset, 0U);
                    }
                }
            } else {
                update_status(&result.decode, ExifDecodeStatus::Malformed);
            }
        }

        if (options.decode_geotiff && result.input.ok()) {
            decode_source_geotiff(cfg, &source, geotiff_directory,
                                  geotiff_double, geotiff_ascii, store,
                                  options.limits);
        }
    }

    maybe_decode_nikon_nefinfo_blocks(store, options, &result.decode);
    exif_internal::decode_nikon_preview_aliases(store, options, &result.decode);
    if (store.resource_limit_exceeded()) {
        mark_limit_exceeded(&result.decode,
                            store.arena().limit_exceeded()
                                ? ExifLimitReason::MaxArenaBytes
                                : ExifLimitReason::MaxTotalEntries,
                            0U, 0U);
    }
    return result;
}


ExifDecodeResult
decode_exif_tiff(std::span<const std::byte> tiff_bytes, MetaStore& store,
                 std::span<ExifIfdRef> out_ifds,
                 const ExifDecodeOptions& options) noexcept
{
    const RandomAccessSource source = make_memory_random_access_source(
        tiff_bytes);
    const RandomAccessSourceRange range = make_random_access_source_range(
        source);
    return decode_exif_tiff_random_access(range, store, out_ifds,
                                          ExifRandomAccessScratch {}, options)
        .decode;
}

ExifDecodeResult
measure_exif_tiff(std::span<const std::byte> tiff_bytes,
                  const ExifDecodeOptions& options) noexcept
{
    MetaStore scratch;
    return decode_exif_tiff(tiff_bytes, scratch, std::span<ExifIfdRef> {},
                            options);
}

}  // namespace openmeta
