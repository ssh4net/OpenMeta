// SPDX-License-Identifier: Apache-2.0

#include "crw_ciff_decode_internal.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace openmeta::ciff_internal {
namespace {

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }


    struct CiffConfig final {
        bool le = true;
    };


    static bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 0U)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 8U);
        *out = v;
        return true;
    }


    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 8U)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 0U);
        *out = v;
        return true;
    }


    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4 > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 0U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 8U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 16U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 24U;
        *out = v;
        return true;
    }


    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4 > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 24U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 16U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 8U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 0U;
        *out = v;
        return true;
    }


    static bool read_u16(const CiffConfig& cfg,
                         std::span<const std::byte> bytes, uint64_t offset,
                         uint16_t* out) noexcept
    {
        return cfg.le ? read_u16le(bytes, offset, out)
                      : read_u16be(bytes, offset, out);
    }


    static bool read_u32(const CiffConfig& cfg,
                         std::span<const std::byte> bytes, uint64_t offset,
                         uint32_t* out) noexcept
    {
        return cfg.le ? read_u32le(bytes, offset, out)
                      : read_u32be(bytes, offset, out);
    }


    static bool read_i32(const CiffConfig& cfg,
                         std::span<const std::byte> bytes, uint64_t offset,
                         int32_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint32_t u = 0;
        if (!read_u32(cfg, bytes, offset, &u)) {
            return false;
        }
        *out = static_cast<int32_t>(u);
        return true;
    }


    static bool read_i16(const CiffConfig& cfg,
                         std::span<const std::byte> bytes, uint64_t offset,
                         int16_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint16_t u = 0;
        if (!read_u16(cfg, bytes, offset, &u)) {
            return false;
        }
        *out = static_cast<int16_t>(u);
        return true;
    }


    static void update_status(ExifDecodeResult* out,
                              ExifDecodeStatus in) noexcept
    {
        if (!out) {
            return;
        }
        if (out->status == ExifDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == ExifDecodeStatus::LimitExceeded) {
            out->status = in;
            return;
        }
        if (out->status == ExifDecodeStatus::Malformed) {
            return;
        }
        if (in == ExifDecodeStatus::Malformed) {
            out->status = in;
            return;
        }
        if (out->status == ExifDecodeStatus::OutputTruncated) {
            return;
        }
        if (in == ExifDecodeStatus::OutputTruncated) {
            out->status = in;
            return;
        }
        if (out->status == ExifDecodeStatus::Ok) {
            return;
        }
        if (in == ExifDecodeStatus::Ok) {
            out->status = in;
            return;
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


    static MetaValue decode_text_value(ByteArena& arena,
                                       std::span<const std::byte> raw,
                                       TextEncoding enc) noexcept
    {
        if (raw.empty()) {
            return MetaValue {};
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


    static bool extract_padded_ascii_text(std::span<const std::byte> raw,
                                          std::string_view* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out = std::string_view();
        if (raw.empty()) {
            *out = std::string_view();
            return true;
        }

        size_t end = 0U;
        while (end < raw.size() && raw[end] != std::byte { 0 }) {
            ++end;
        }
        if (end < raw.size()) {
            for (size_t i = end + 1U; i < raw.size(); ++i) {
                if (raw[i] != std::byte { 0 }) {
                    return false;
                }
            }
        }

        *out = std::string_view(reinterpret_cast<const char*>(raw.data()), end);
        return true;
    }


    static MetaValue
    decode_padded_ascii_text(ByteArena& arena,
                             std::span<const std::byte> raw) noexcept
    {
        std::string_view text;
        if (!extract_padded_ascii_text(raw, &text)) {
            return make_bytes(arena, raw);
        }
        return make_text(arena, text, TextEncoding::Ascii);
    }


    static MetaValue decode_u16_array(const CiffConfig& cfg, ByteArena& arena,
                                      std::span<const std::byte> raw,
                                      ExifDecodeResult* status_out) noexcept
    {
        if (raw.size() == 2) {
            uint16_t v = 0;
            if (!read_u16(cfg, raw, 0, &v)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return MetaValue {};
            }
            return make_u16(v);
        }
        if (raw.size() % 2U != 0) {
            return make_bytes(arena, raw);
        }
        const uint64_t count64 = raw.size() / 2U;
        if (count64 > (UINT32_MAX / 2U)) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return MetaValue {};
        }
        MetaValue v;
        v.kind      = MetaValueKind::Array;
        v.elem_type = MetaElementType::U16;
        v.count     = static_cast<uint32_t>(count64);
        v.data.span = arena.allocate(static_cast<uint32_t>(v.count * 2U),
                                     alignof(uint16_t));
        if (v.count != 0U
            && v.data.span.size != static_cast<uint32_t>(v.count * 2U)) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return MetaValue {};
        }
        const std::span<std::byte> dst = arena.span_mut(v.data.span);
        for (uint32_t i = 0; i < v.count; ++i) {
            uint16_t value = 0;
            if (!read_u16(cfg, raw, static_cast<uint64_t>(i) * 2U, &value)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                break;
            }
            std::memcpy(dst.data() + i * 2U, &value, 2U);
        }
        return v;
    }


    static MetaValue decode_u32_array(const CiffConfig& cfg, ByteArena& arena,
                                      std::span<const std::byte> raw,
                                      ExifDecodeResult* status_out) noexcept
    {
        if (raw.size() == 4) {
            uint32_t v = 0;
            if (!read_u32(cfg, raw, 0, &v)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return MetaValue {};
            }
            return make_u32(v);
        }
        if (raw.size() % 4U != 0) {
            return make_bytes(arena, raw);
        }
        const uint64_t count64 = raw.size() / 4U;
        if (count64 > (UINT32_MAX / 4U)) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return MetaValue {};
        }
        MetaValue v;
        v.kind      = MetaValueKind::Array;
        v.elem_type = MetaElementType::U32;
        v.count     = static_cast<uint32_t>(count64);
        v.data.span = arena.allocate(static_cast<uint32_t>(v.count * 4U),
                                     alignof(uint32_t));
        if (v.count != 0U
            && v.data.span.size != static_cast<uint32_t>(v.count * 4U)) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return MetaValue {};
        }
        const std::span<std::byte> dst = arena.span_mut(v.data.span);
        for (uint32_t i = 0; i < v.count; ++i) {
            uint32_t value = 0;
            if (!read_u32(cfg, raw, static_cast<uint64_t>(i) * 4U, &value)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                break;
            }
            std::memcpy(dst.data() + i * 4U, &value, 4U);
        }
        return v;
    }


    static uint16_t ciff_tag_id(uint16_t tag) noexcept
    {
        return static_cast<uint16_t>(tag & 0x3fffU);
    }

    static uint16_t ciff_type_bits(uint16_t tag) noexcept
    {
        return static_cast<uint16_t>(tag & 0x3800U);
    }

    static uint16_t ciff_loc_bits(uint16_t tag) noexcept
    {
        return static_cast<uint16_t>(tag & 0xc000U);
    }

    static bool ciff_is_directory(uint16_t tag) noexcept
    {
        const uint16_t t = ciff_type_bits(tag);
        return t == 0x2800U || t == 0x3000U;
    }


    static bool parse_ciff_dir_id(std::string_view ifd_token,
                                  uint16_t* out) noexcept
    {
        if (!out || ifd_token.size() < 10 || !ifd_token.starts_with("ciff_")
            || ifd_token[9] != '_') {
            return false;
        }
        uint16_t dir = 0;
        for (size_t i = 5; i < 9; ++i) {
            const char c    = ifd_token[i];
            uint16_t nibble = 0;
            if (c >= '0' && c <= '9') {
                nibble = static_cast<uint16_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                nibble = static_cast<uint16_t>(10 + (c - 'a'));
            } else if (c >= 'A' && c <= 'F') {
                nibble = static_cast<uint16_t>(10 + (c - 'A'));
            } else {
                return false;
            }
            dir = static_cast<uint16_t>((dir << 4U) | nibble);
        }
        *out = dir;
        return true;
    }


    static bool ciff_tag_is_padded_ascii_text(uint16_t dir_id,
                                              uint16_t tag_id) noexcept
    {
        switch (dir_id) {
        case 0x2804U: return tag_id == 0x0805U || tag_id == 0x0815U;
        case 0x2807U: return tag_id == 0x0810U;
        case 0x3004U:
            return tag_id == 0x080BU || tag_id == 0x080CU || tag_id == 0x080DU;
        case 0x300AU: return tag_id == 0x0816U || tag_id == 0x0817U;
        default: return false;
        }
    }


    static bool trailing_zero_bytes(std::span<const std::byte> raw,
                                    size_t offset) noexcept
    {
        if (offset > raw.size()) {
            return false;
        }
        for (size_t i = offset; i < raw.size(); ++i) {
            if (raw[i] != std::byte { 0 }) {
                return false;
            }
        }
        return true;
    }


    static bool decode_known_ciff_scalar_u16(const CiffConfig& cfg,
                                             std::span<const std::byte> raw,
                                             MetaValue* out) noexcept
    {
        if (!out || raw.size() < 2U || !trailing_zero_bytes(raw, 2U)) {
            return false;
        }
        uint16_t value = 0;
        if (!read_u16(cfg, raw, 0, &value)) {
            return false;
        }
        *out = make_u16(value);
        return true;
    }


    static bool decode_known_ciff_scalar_u32(const CiffConfig& cfg,
                                             std::span<const std::byte> raw,
                                             MetaValue* out) noexcept
    {
        if (!out || raw.size() < 4U || !trailing_zero_bytes(raw, 4U)) {
            return false;
        }
        uint32_t value = 0;
        if (!read_u32(cfg, raw, 0, &value)) {
            return false;
        }
        *out = make_u32(value);
        return true;
    }


    static bool decode_known_ciff_scalar_f32(const CiffConfig& cfg,
                                             std::span<const std::byte> raw,
                                             MetaValue* out) noexcept
    {
        if (!out || raw.size() < 4U || !trailing_zero_bytes(raw, 4U)) {
            return false;
        }
        uint32_t bits = 0;
        if (!read_u32(cfg, raw, 0, &bits)) {
            return false;
        }
        *out = make_f32_bits(bits);
        return true;
    }


    static bool decode_known_ciff_native_scalar_value(
        const CiffConfig& cfg, uint16_t dir_id, uint16_t tag_id,
        std::span<const std::byte> raw, MetaValue* out) noexcept
    {
        switch (dir_id) {
        case 0x3002U:
            switch (tag_id) {
            case 0x1010U:
            case 0x1011U:
            case 0x1016U: return decode_known_ciff_scalar_u16(cfg, raw, out);
            case 0x1807U: return decode_known_ciff_scalar_f32(cfg, raw, out);
            default: return false;
            }
        case 0x3003U:
            switch (tag_id) {
            case 0x1814U: return decode_known_ciff_scalar_f32(cfg, raw, out);
            default: return false;
            }
        case 0x3004U:
            switch (tag_id) {
            case 0x101CU: return decode_known_ciff_scalar_u16(cfg, raw, out);
            case 0x1834U:
            case 0x183BU: return decode_known_ciff_scalar_u32(cfg, raw, out);
            default: return false;
            }
        case 0x300AU:
            switch (tag_id) {
            case 0x100AU: return decode_known_ciff_scalar_u16(cfg, raw, out);
            case 0x1804U:
            case 0x1806U:
            case 0x1817U: return decode_known_ciff_scalar_u32(cfg, raw, out);
            default: return false;
            }
        default: return false;
        }
    }


    static uint16_t ciff_rotation_to_orientation(int32_t degrees) noexcept
    {
        switch (degrees) {
        case 0: return 1;
        case 180:
        case -180: return 3;
        case 90:
        case -270: return 6;
        case 270:
        case -90: return 8;
        default: return 1;
        }
    }


    static bool can_add_derived_entry(const ExifDecodeLimits& limits,
                                      ExifDecodeResult* status_out) noexcept
    {
        if (!status_out) {
            return true;
        }
        if (status_out->entries_decoded >= limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }
        return true;
    }


    static void add_derived_exif_entry(MetaStore& store, BlockId block,
                                       uint32_t order_in_block,
                                       std::string_view ifd, uint16_t tag,
                                       const MetaValue& value,
                                       uint16_t source_tag,
                                       const ExifDecodeLimits& limits,
                                       ExifDecodeResult* status_out) noexcept
    {
        if (!can_add_derived_entry(limits, status_out)) {
            return;
        }
        Entry entry;
        entry.key.kind              = MetaKeyKind::ExifTag;
        entry.key.data.exif_tag.ifd = store.arena().append_string(ifd);
        entry.key.data.exif_tag.tag = tag;
        entry.value                 = value;
        entry.origin.block          = block;
        entry.origin.order_in_block = order_in_block;
        entry.origin.wire_type  = WireType { WireFamily::Other, source_tag };
        entry.origin.wire_count = value.count;
        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1U;
        }
    }


    static void add_derived_ciff_entry(
        MetaStore& store, BlockId block, uint32_t order_in_block,
        std::string_view parent_ifd, std::string_view suffix, uint16_t tag,
        const MetaValue& value, uint16_t source_tag,
        const ExifDecodeLimits& limits, ExifDecodeResult* status_out) noexcept
    {
        std::array<char, 64> buf {};
        const int n = std::snprintf(buf.data(), buf.size(), "%.*s_%.*s",
                                    static_cast<int>(parent_ifd.size()),
                                    parent_ifd.data(),
                                    static_cast<int>(suffix.size()),
                                    suffix.data());
        if (n <= 0 || static_cast<size_t>(n) >= buf.size()) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        add_derived_exif_entry(store, block, order_in_block,
                               std::string_view(buf.data(),
                                                static_cast<size_t>(n)),
                               tag, value, source_tag, limits, status_out);
    }


    static bool format_exif_datetime(uint32_t unix_seconds,
                                     std::array<char, 20>* out) noexcept
    {
        if (!out) {
            return false;
        }
        const std::time_t t = static_cast<std::time_t>(unix_seconds);
        std::tm tm_out {};
#if defined(_WIN32)
        if (localtime_s(&tm_out, &t) != 0) {
            return false;
        }
#else
        if (!localtime_r(&t, &tm_out)) {
            return false;
        }
#endif
        const size_t n = std::strftime(out->data(), out->size(),
                                       "%Y:%m:%d %H:%M:%S", &tm_out);
        return n == 19;
    }


    static void add_crw_derived_entries(
        const CiffConfig& cfg, std::string_view ifd_token, uint16_t tag_id,
        std::span<const std::byte> raw, MetaStore& store, BlockId block,
        uint32_t order_in_block, const ExifDecodeLimits& limits,
        ExifDecodeResult* status_out) noexcept
    {
        uint16_t dir_id = 0;
        if (!parse_ciff_dir_id(ifd_token, &dir_id)) {
            return;
        }

        uint32_t next_order = (order_in_block < UINT32_MAX)
                                  ? (order_in_block + 1U)
                                  : order_in_block;

        if (dir_id == 0x2807U && tag_id == 0x080AU) {
            size_t make_end = 0;
            while (make_end < raw.size() && raw[make_end] != std::byte { 0 }) {
                ++make_end;
            }
            if (make_end > 0) {
                const std::string_view make(reinterpret_cast<const char*>(
                                                raw.data()),
                                            make_end);
                const MetaValue value = make_text(store.arena(), make,
                                                  TextEncoding::Ascii);
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "makemodel", 0x0000U, value, tag_id,
                                       limits, status_out);
                add_derived_exif_entry(store, block, next_order++, "ifd0",
                                       0x010FU, value, tag_id, limits,
                                       status_out);
            }

            size_t model_begin = make_end;
            if (model_begin < raw.size()
                && raw[model_begin] == std::byte { 0 }) {
                ++model_begin;
            }
            size_t model_end = model_begin;
            while (model_end < raw.size()
                   && raw[model_end] != std::byte { 0 }) {
                ++model_end;
            }
            if (model_end > model_begin) {
                const std::string_view model(reinterpret_cast<const char*>(
                                                 raw.data() + model_begin),
                                             model_end - model_begin);
                const MetaValue value = make_text(store.arena(), model,
                                                  TextEncoding::Ascii);
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "makemodel", 0x0006U, value, tag_id,
                                       limits, status_out);
                add_derived_exif_entry(store, block, next_order++, "ifd0",
                                       0x0110U, value, tag_id, limits,
                                       status_out);
            }
            return;
        }

        if (dir_id == 0x2804U && tag_id == 0x0805U) {
            std::string_view text;
            if (extract_padded_ascii_text(raw, &text) && !text.empty()) {
                const MetaValue value = make_text(store.arena(), text,
                                                  TextEncoding::Ascii);
                add_derived_exif_entry(store, block, next_order++, "ifd0",
                                       0x010EU, value, tag_id, limits,
                                       status_out);
            }
            return;
        }

        if (dir_id == 0x2807U && tag_id == 0x0810U) {
            std::string_view text;
            if (extract_padded_ascii_text(raw, &text) && !text.empty()) {
                const MetaValue value = make_text(store.arena(), text,
                                                  TextEncoding::Ascii);
                add_derived_exif_entry(store, block, next_order++, "exififd",
                                       0xA430U, value, tag_id, limits,
                                       status_out);
            }
            return;
        }

        if (dir_id == 0x300AU && tag_id == 0x180EU && raw.size() >= 4U) {
            uint32_t unix_seconds = 0;
            if (read_u32(cfg, raw, 0, &unix_seconds)) {
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "timestamp", 0x0000U,
                                       make_u32(unix_seconds), tag_id, limits,
                                       status_out);
                std::array<char, 20> buf {};
                if (format_exif_datetime(unix_seconds, &buf)) {
                    const std::string_view dt(buf.data(), 19);
                    const MetaValue value = make_text(store.arena(), dt,
                                                      TextEncoding::Ascii);
                    add_derived_exif_entry(store, block, next_order++,
                                           "exififd", 0x9003U, value, tag_id,
                                           limits, status_out);
                }
            }
            if (raw.size() >= 8U) {
                int32_t tz_code = 0;
                if (read_i32(cfg, raw, 4U, &tz_code)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "timestamp", 0x0001U,
                                           make_i32(tz_code), tag_id, limits,
                                           status_out);
                }
            }
            if (raw.size() >= 12U) {
                uint32_t tz_info = 0;
                if (read_u32(cfg, raw, 8U, &tz_info)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "timestamp", 0x0002U,
                                           make_u32(tz_info), tag_id, limits,
                                           status_out);
                }
            }
            return;
        }

        if (dir_id == 0x300AU && tag_id == 0x1810U) {
            if (raw.size() >= 4U) {
                uint32_t width = 0;
                if (read_u32(cfg, raw, 0, &width)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "imageinfo", 0x0000U,
                                           make_u32(width), tag_id, limits,
                                           status_out);
                    add_derived_exif_entry(store, block, next_order++,
                                           "exififd", 0xA002U, make_u32(width),
                                           tag_id, limits, status_out);
                }
            }
            if (raw.size() >= 8U) {
                uint32_t height = 0;
                if (read_u32(cfg, raw, 4, &height)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "imageinfo", 0x0001U,
                                           make_u32(height), tag_id, limits,
                                           status_out);
                    add_derived_exif_entry(store, block, next_order++,
                                           "exififd", 0xA003U, make_u32(height),
                                           tag_id, limits, status_out);
                }
            }
            if (raw.size() >= 12U) {
                uint32_t aspect_bits = 0;
                if (read_u32(cfg, raw, 8U, &aspect_bits)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "imageinfo", 0x0002U,
                                           make_f32_bits(aspect_bits), tag_id,
                                           limits, status_out);
                }
            }
            if (raw.size() >= 16U) {
                int32_t rotation = 0;
                if (read_i32(cfg, raw, 12, &rotation)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "imageinfo", 0x0003U,
                                           make_i32(rotation), tag_id, limits,
                                           status_out);
                    const uint16_t orientation = ciff_rotation_to_orientation(
                        rotation);
                    add_derived_exif_entry(store, block, next_order++, "ifd0",
                                           0x0112U, make_u16(orientation),
                                           tag_id, limits, status_out);
                }
            }
            if (raw.size() >= 20U) {
                uint32_t component_depth = 0;
                if (read_u32(cfg, raw, 16U, &component_depth)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "imageinfo", 0x0004U,
                                           make_u32(component_depth), tag_id,
                                           limits, status_out);
                }
            }
            if (raw.size() >= 24U) {
                uint32_t color_depth = 0;
                if (read_u32(cfg, raw, 20U, &color_depth)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "imageinfo", 0x0005U,
                                           make_u32(color_depth), tag_id,
                                           limits, status_out);
                }
            }
            if (raw.size() >= 28U) {
                uint32_t color_bw = 0;
                if (read_u32(cfg, raw, 24U, &color_bw)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "imageinfo", 0x0006U,
                                           make_u32(color_bw), tag_id, limits,
                                           status_out);
                }
            }
            return;
        }

        if (dir_id == 0x300AU && tag_id == 0x1803U && raw.size() >= 4U) {
            uint32_t file_format = 0;
            if (read_u32(cfg, raw, 0, &file_format)) {
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "imageformat", 0x0000U,
                                       make_u32(file_format), tag_id, limits,
                                       status_out);
            }
            if (raw.size() >= 8U) {
                uint32_t ratio_bits = 0;
                if (read_u32(cfg, raw, 4U, &ratio_bits)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "imageformat", 0x0001U,
                                           make_f32_bits(ratio_bits), tag_id,
                                           limits, status_out);
                }
            }
            return;
        }

        if (dir_id == 0x3002U && tag_id == 0x1807U && raw.size() >= 4U) {
            uint32_t distance = 0;
            if (read_u32(cfg, raw, 0, &distance)) {
                add_derived_exif_entry(store, block, next_order++, "exififd",
                                       0x9206U, make_u32(distance), tag_id,
                                       limits, status_out);
            }
            return;
        }

        if (dir_id == 0x3002U && tag_id == 0x1818U && raw.size() >= 12U) {
            for (uint16_t i = 0; i < 3U; ++i) {
                uint32_t bits = 0;
                if (!read_u32(cfg, raw, static_cast<uint64_t>(i) * 4U, &bits)) {
                    break;
                }
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "exposureinfo", i, make_f32_bits(bits),
                                       tag_id, limits, status_out);
            }
        }

        if (dir_id == 0x3002U && tag_id == 0x1813U && raw.size() >= 4U) {
            for (uint16_t i = 0; i < 2U; ++i) {
                const uint64_t offset = static_cast<uint64_t>(i) * 4U;
                if (offset + 4U > raw.size()) {
                    break;
                }
                uint32_t bits = 0;
                if (!read_u32(cfg, raw, offset, &bits)) {
                    break;
                }
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "flashinfo", i, make_f32_bits(bits),
                                       tag_id, limits, status_out);
            }
        }

        if (dir_id == 0x3004U && tag_id == 0x1835U && raw.size() >= 4U) {
            uint32_t table_number = 0;
            if (read_u32(cfg, raw, 0, &table_number)) {
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "decodertable", 0x0000U,
                                       make_u32(table_number), tag_id, limits,
                                       status_out);
            }
            if (raw.size() >= 12U) {
                uint32_t data_offset = 0;
                if (read_u32(cfg, raw, 8U, &data_offset)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "decodertable", 0x0002U,
                                           make_u32(data_offset), tag_id,
                                           limits, status_out);
                }
            }
            if (raw.size() >= 16U) {
                uint32_t data_length = 0;
                if (read_u32(cfg, raw, 12U, &data_length)) {
                    add_derived_ciff_entry(store, block, next_order++,
                                           ifd_token, "decodertable", 0x0003U,
                                           make_u32(data_length), tag_id,
                                           limits, status_out);
                }
            }
        }

        if (dir_id == 0x300BU && tag_id == 0x1029U && raw.size() >= 2U) {
            for (uint16_t i = 0; i < 4U; ++i) {
                const uint64_t offset = static_cast<uint64_t>(i) * 2U;
                if (offset + 2U > raw.size()) {
                    break;
                }
                uint16_t value = 0;
                if (!read_u16(cfg, raw, offset, &value)) {
                    break;
                }
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "focallength", i, make_u16(value),
                                       tag_id, limits, status_out);
            }
        }

        if (dir_id == 0x300BU && tag_id == 0x102AU && raw.size() >= 2U) {
            for (uint16_t i = 1U; i <= 10U; ++i) {
                const uint64_t offset = static_cast<uint64_t>(i - 1U) * 2U;
                if (offset + 2U > raw.size()) {
                    break;
                }
                int16_t value = 0;
                if (!read_i16(cfg, raw, offset, &value)) {
                    break;
                }
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "shotinfo", i, make_i16(value), tag_id,
                                       limits, status_out);
            }
        }

        if (dir_id == 0x300BU && tag_id == 0x10B5U && raw.size() >= 10U) {
            for (uint16_t i = 1U; i <= 4U; ++i) {
                const uint64_t offset = static_cast<uint64_t>(i) * 2U;
                if (offset + 2U > raw.size()) {
                    break;
                }
                uint16_t value = 0;
                if (!read_u16(cfg, raw, offset, &value)) {
                    break;
                }
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "rawjpginfo", i, make_u16(value), tag_id,
                                       limits, status_out);
            }
        }

        if (dir_id == 0x300BU && tag_id == 0x1030U && raw.size() >= 12U) {
            for (uint16_t i = 1U; i <= 5U; ++i) {
                const uint64_t offset = static_cast<uint64_t>(i) * 2U;
                if (offset + 2U > raw.size()) {
                    break;
                }
                uint16_t value = 0;
                if (!read_u16(cfg, raw, offset, &value)) {
                    break;
                }
                add_derived_ciff_entry(store, block, next_order++, ifd_token,
                                       "whitesample", i, make_u16(value),
                                       tag_id, limits, status_out);
            }
        }
    }


    static void decode_leaf_entry(
        const CiffConfig& cfg, std::string_view ifd_token, ByteSpan ifd_span,
        bool has_ifd_dir_id, uint16_t ifd_dir_id, uint16_t tag, uint16_t tag_id,
        std::span<const std::byte> raw, uint64_t value_bytes,
        bool value_available, MetaStore& store, BlockId block, uint32_t order,
        const ExifDecodeLimits& limits, ExifDecodeResult* status_out) noexcept
    {
        Entry entry;
        entry.key.kind              = MetaKeyKind::ExifTag;
        entry.key.data.exif_tag.ifd = ifd_span;
        entry.key.data.exif_tag.tag = tag_id;
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.origin.wire_type      = WireType { WireFamily::Other, tag };
        entry.origin.wire_count     = value_bytes > UINT32_MAX
                                          ? UINT32_MAX
                                          : static_cast<uint32_t>(value_bytes);

        if (!value_available) {
            entry.flags |= EntryFlags::Truncated;
            update_status(status_out, ExifDecodeStatus::OutputTruncated);
        } else if (value_bytes > limits.max_value_bytes) {
            entry.flags |= EntryFlags::Truncated;
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        } else if (has_ifd_dir_id
                   && decode_known_ciff_native_scalar_value(cfg, ifd_dir_id,
                                                            tag_id, raw,
                                                            &entry.value)) {
            (void)0;
        } else {
            switch (ciff_type_bits(tag)) {
            case 0x0000: {
                if (raw.size() == 1U) {
                    entry.value = make_u8(u8(raw[0]));
                } else {
                    if (raw.size() > UINT32_MAX) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                        break;
                    }
                    MetaValue value;
                    value.kind      = MetaValueKind::Array;
                    value.elem_type = MetaElementType::U8;
                    value.count     = static_cast<uint32_t>(raw.size());
                    value.data.span = store.arena().append(raw);
                    if (!raw.empty() && value.data.span.size != value.count) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                        break;
                    }
                    entry.value = value;
                }
                break;
            }
            case 0x0800:
                entry.value
                    = has_ifd_dir_id
                              && ciff_tag_is_padded_ascii_text(ifd_dir_id,
                                                               tag_id)
                          ? decode_padded_ascii_text(store.arena(), raw)
                          : decode_text_value(store.arena(), raw,
                                              TextEncoding::Ascii);
                break;
            case 0x1000:
                entry.value = decode_u16_array(cfg, store.arena(), raw,
                                               status_out);
                break;
            case 0x1800:
                entry.value = decode_u32_array(cfg, store.arena(), raw,
                                               status_out);
                break;
            case 0x2000:
                entry.value
                    = has_ifd_dir_id
                              && ciff_tag_is_padded_ascii_text(ifd_dir_id,
                                                               tag_id)
                          ? decode_padded_ascii_text(store.arena(), raw)
                          : make_bytes(store.arena(), raw);
                break;
            default: entry.value = make_bytes(store.arena(), raw); break;
            }
        }

        if (store.add_entry(entry) == kInvalidEntryId) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }
        if (status_out) {
            status_out->entries_decoded += 1U;
        }
        if (value_available && value_bytes <= limits.max_value_bytes) {
            add_crw_derived_entries(cfg, ifd_token, tag_id, raw, store, block,
                                    order, limits, status_out);
        }
    }


    static bool decode_directory(const CiffConfig& cfg,
                                 std::span<const std::byte> dir_bytes,
                                 std::string_view ifd_token, MetaStore& store,
                                 const ExifDecodeLimits& limits,
                                 ExifDecodeResult* status_out, uint32_t depth,
                                 uint32_t* dir_index) noexcept
    {
        if (dir_bytes.size() < 6) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        if (depth > 32) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }
        if (status_out && status_out->ifds_written >= limits.max_ifds) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        uint32_t entry_off32 = 0;
        if (!read_u32(cfg, dir_bytes, dir_bytes.size() - 4, &entry_off32)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        const uint64_t entry_off = entry_off32;
        if (entry_off > dir_bytes.size() - 2) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }

        uint16_t entry_count = 0;
        if (!read_u16(cfg, dir_bytes, entry_off, &entry_count)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }

        const uint64_t entries_start = entry_off + 2;
        const uint64_t needed = entries_start + uint64_t(entry_count) * 10ULL;
        if (needed > dir_bytes.size()) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        const ByteSpan ifd_span   = store.arena().append_string(ifd_token);
        uint16_t ifd_dir_id       = 0U;
        const bool has_ifd_dir_id = parse_ciff_dir_id(ifd_token, &ifd_dir_id);

        bool any = false;

        if (status_out) {
            status_out->ifds_written += 1U;
        }

        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_start + uint64_t(i) * 10ULL;

            uint16_t tag = 0;
            if (!read_u16(cfg, dir_bytes, eoff + 0, &tag)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                break;
            }

            const uint16_t tag_id = ciff_tag_id(tag);
            const uint16_t loc    = ciff_loc_bits(tag);

            uint64_t value_off   = 0;
            uint64_t value_bytes = 0;

            if (loc == 0x4000U) {  // directoryData
                value_off   = eoff + 2;
                value_bytes = 8;
            } else if (loc == 0x0000U) {  // valueData
                uint32_t size32 = 0;
                uint32_t off32  = 0;
                if (!read_u32(cfg, dir_bytes, eoff + 2, &size32)
                    || !read_u32(cfg, dir_bytes, eoff + 6, &off32)) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                    break;
                }
                value_off   = off32;
                value_bytes = size32;

                // Ensure the referenced region doesn't overlap the entry header.
                if (value_off < eoff) {
                    if (value_bytes > (eoff - value_off)) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                        continue;
                    }
                } else {
                    if (value_off < eoff + 10) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                        continue;
                    }
                }
            } else {
                update_status(status_out, ExifDecodeStatus::Malformed);
                continue;
            }

            if (value_off > dir_bytes.size()
                || value_bytes > dir_bytes.size() - value_off) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                continue;
            }

            if (ciff_is_directory(tag)) {
                if (!dir_index) {
                    continue;
                }
                const uint32_t idx = (*dir_index)++;
                std::array<char, 32> name {};
                const int n = std::snprintf(name.data(), name.size(),
                                            "ciff_%04X_%u",
                                            static_cast<unsigned>(tag_id),
                                            static_cast<unsigned>(idx));
                if (n <= 0 || static_cast<size_t>(n) >= name.size()) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                    continue;
                }
                const std::string_view child_token(name.data(),
                                                   static_cast<size_t>(n));
                const std::span<const std::byte> child
                    = dir_bytes.subspan(static_cast<size_t>(value_off),
                                        static_cast<size_t>(value_bytes));
                (void)decode_directory(cfg, child, child_token, store, limits,
                                       status_out, depth + 1, dir_index);
                any = true;
                continue;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                break;
            }

            const std::span<const std::byte> raw
                = value_bytes <= limits.max_value_bytes
                      ? dir_bytes.subspan(static_cast<size_t>(value_off),
                                          static_cast<size_t>(value_bytes))
                      : std::span<const std::byte> {};
            decode_leaf_entry(cfg, ifd_token, ifd_span, has_ifd_dir_id,
                              ifd_dir_id, tag, tag_id, raw, value_bytes, true,
                              store, block, i, limits, status_out);
            any = true;
        }

        return any;
    }


    struct CiffSourceReader final {
        const RandomAccessSourceRange* source = nullptr;
        RandomAccessReadWindow window;
        std::span<std::byte> value;
        RandomAccessReadWindowOptions window_options;
        RandomAccessReadLimits limits;
        ExifRandomAccessDecodeResult* result = nullptr;
    };


    static bool ciff_source_view(CiffSourceReader* reader, uint64_t offset,
                                 uint64_t size,
                                 std::span<const std::byte>* out) noexcept
    {
        if (!reader || !reader->source || !reader->result || !out) {
            return false;
        }
        const RandomAccessViewResult view
            = random_access_read_view(*reader->source, offset, size,
                                      &reader->window, &reader->result->input,
                                      reader->limits, reader->window_options);
        if (!view.ok()) {
            return false;
        }
        *out = view.bytes;
        return true;
    }


    static bool ciff_source_value_view(CiffSourceReader* reader,
                                       uint64_t offset, uint64_t size,
                                       std::span<const std::byte>* out) noexcept
    {
        if (!reader || !reader->result || !out) {
            return false;
        }
        if (size == 0U) {
            *out = {};
            return true;
        }
        if (size <= reader->window.storage.size()) {
            return ciff_source_view(reader, offset, size, out);
        }
        if (size > reader->value.size()) {
            reader->result->value_scratch_needed
                = size > reader->result->value_scratch_needed
                      ? size
                      : reader->result->value_scratch_needed;
            update_status(&reader->result->decode,
                          ExifDecodeStatus::OutputTruncated);
            return false;
        }
        const std::span<std::byte> destination = reader->value.first(
            static_cast<size_t>(size));
        if (random_access_read_exact(*reader->source, offset, destination,
                                     &reader->result->input, reader->limits)
            != RandomAccessReadCode::Ok) {
            return false;
        }
        *out = destination;
        return true;
    }


    static bool
    decode_source_directory(const CiffConfig& cfg, CiffSourceReader* reader,
                            uint64_t directory_base, uint64_t directory_size,
                            std::string_view ifd_token, MetaStore& store,
                            const ExifDecodeLimits& limits,
                            ExifDecodeResult* status_out, uint32_t depth,
                            uint32_t* directory_index) noexcept
    {
        if (!reader || !reader->source || directory_size < 6U
            || directory_base > reader->source->size
            || directory_size > reader->source->size - directory_base) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        if (depth > 32U) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }
        if (status_out && status_out->ifds_written >= limits.max_ifds) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        std::span<const std::byte> bytes;
        if (!ciff_source_view(reader, directory_base + directory_size - 4U, 4U,
                              &bytes)) {
            return false;
        }
        uint32_t entry_offset32 = 0U;
        if (!read_u32(cfg, bytes, 0U, &entry_offset32)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        const uint64_t entry_offset = entry_offset32;
        if (entry_offset > directory_size - 2U
            || !ciff_source_view(reader, directory_base + entry_offset, 2U,
                                 &bytes)) {
            if (reader->result->input.ok()) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return false;
        }

        uint16_t entry_count = 0U;
        if (!read_u16(cfg, bytes, 0U, &entry_count)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        const uint64_t entries_start = entry_offset + 2U;
        const uint64_t entry_bytes = static_cast<uint64_t>(entry_count) * 10ULL;
        if (entries_start > directory_size
            || entry_bytes > directory_size - entries_start) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }
        const ByteSpan ifd_span   = store.arena().append_string(ifd_token);
        uint16_t ifd_directory_id = 0U;
        const bool has_ifd_directory_id = parse_ciff_dir_id(ifd_token,
                                                            &ifd_directory_id);
        if (status_out) {
            status_out->ifds_written += 1U;
        }

        bool any = false;
        for (uint32_t i = 0U; i < entry_count; ++i) {
            const uint64_t entry_offset_in_directory
                = entries_start + static_cast<uint64_t>(i) * 10ULL;
            if (!ciff_source_view(reader,
                                  directory_base + entry_offset_in_directory,
                                  10U, &bytes)) {
                return any;
            }

            uint16_t tag = 0U;
            if (!read_u16(cfg, bytes, 0U, &tag)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return any;
            }
            const uint16_t tag_id = ciff_tag_id(tag);
            const uint16_t loc    = ciff_loc_bits(tag);
            uint64_t value_offset = 0U;
            uint64_t value_bytes  = 0U;
            if (loc == 0x4000U) {
                value_offset = entry_offset_in_directory + 2U;
                value_bytes  = 8U;
            } else if (loc == 0x0000U) {
                uint32_t size32 = 0U;
                uint32_t off32  = 0U;
                if (!read_u32(cfg, bytes, 2U, &size32)
                    || !read_u32(cfg, bytes, 6U, &off32)) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                    return any;
                }
                value_offset = off32;
                value_bytes  = size32;
                if ((value_offset < entry_offset_in_directory
                     && value_bytes > entry_offset_in_directory - value_offset)
                    || (value_offset >= entry_offset_in_directory
                        && value_offset < entry_offset_in_directory + 10U)) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                    continue;
                }
            } else {
                update_status(status_out, ExifDecodeStatus::Malformed);
                continue;
            }
            if (value_offset > directory_size
                || value_bytes > directory_size - value_offset) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                continue;
            }

            if (ciff_is_directory(tag)) {
                if (!directory_index) {
                    continue;
                }
                const uint32_t index = (*directory_index)++;
                std::array<char, 32> name {};
                const int length = std::snprintf(name.data(), name.size(),
                                                 "ciff_%04X_%u",
                                                 static_cast<unsigned>(tag_id),
                                                 static_cast<unsigned>(index));
                if (length <= 0 || static_cast<size_t>(length) >= name.size()) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                    continue;
                }
                (void)decode_source_directory(
                    cfg, reader, directory_base + value_offset, value_bytes,
                    std::string_view(name.data(), static_cast<size_t>(length)),
                    store, limits, status_out, depth + 1U, directory_index);
                any = true;
                continue;
            }

            if (status_out
                && status_out->entries_decoded + 1U
                       > limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                break;
            }
            std::span<const std::byte> raw;
            bool value_available = value_bytes > limits.max_value_bytes;
            if (value_bytes <= limits.max_value_bytes) {
                value_available = ciff_source_value_view(
                    reader, directory_base + value_offset, value_bytes, &raw);
                if (!value_available && !reader->result->input.ok()) {
                    return any;
                }
            }
            decode_leaf_entry(cfg, ifd_token, ifd_span, has_ifd_directory_id,
                              ifd_directory_id, tag, tag_id, raw, value_bytes,
                              value_available, store, block, i, limits,
                              status_out);
            any = true;
        }
        return any;
    }

}  // namespace

bool
decode_crw_ciff(std::span<const std::byte> file_bytes, MetaStore& store,
                const ExifDecodeLimits& limits,
                ExifDecodeResult* status_out) noexcept
{
    if (status_out) {
        status_out->status = ExifDecodeStatus::Unsupported;
    }
    if (file_bytes.size() < 14) {
        update_status(status_out, ExifDecodeStatus::Unsupported);
        return false;
    }

    const uint8_t b0 = u8(file_bytes[0]);
    const uint8_t b1 = u8(file_bytes[1]);
    const bool le    = (b0 == 0x49 && b1 == 0x49);
    const bool be    = (b0 == 0x4D && b1 == 0x4D);
    if (!le && !be) {
        update_status(status_out, ExifDecodeStatus::Unsupported);
        return false;
    }

    if (std::memcmp(file_bytes.data() + 6, "HEAPCCDR", 8) != 0) {
        update_status(status_out, ExifDecodeStatus::Unsupported);
        return false;
    }

    CiffConfig cfg;
    cfg.le = le;

    uint32_t root_off = 0;
    if (!read_u32(cfg, file_bytes, 2, &root_off)) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return false;
    }
    if (root_off < 14U || static_cast<uint64_t>(root_off) > file_bytes.size()) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return false;
    }

    const std::span<const std::byte> root = file_bytes.subspan(
        static_cast<size_t>(root_off));
    uint32_t dir_index = 0;
    const bool any     = decode_directory(cfg, root, "ciff_root", store, limits,
                                          status_out, 0, &dir_index);
    if (any) {
        update_status(status_out, ExifDecodeStatus::Ok);
    }
    return any;
}

ExifRandomAccessDecodeResult
decode_crw_ciff_random_access(
    const RandomAccessSourceRange& source, MetaStore& store,
    const ExifRandomAccessScratch& scratch, const ExifDecodeLimits& limits,
    const RandomAccessReadLimits& read_limits) noexcept
{
    ExifRandomAccessDecodeResult result;
    result.decode.status = ExifDecodeStatus::Unsupported;
    if (!random_access_source_range_valid(source)) {
        result.input.code = RandomAccessReadCode::InvalidArgument;
        return result;
    }
    if (source.source.contiguous_data != nullptr) {
        const std::byte* begin = source.source.contiguous_data
                                 + static_cast<size_t>(source.source_offset);
        (void)decode_crw_ciff(
            std::span<const std::byte>(begin, static_cast<size_t>(source.size)),
            store, limits, &result.decode);
        return result;
    }
    if (source.size < 14U) {
        return result;
    }

    std::array<std::byte, 14U> header {};
    if (random_access_read_exact(source, 0U, header, &result.input, read_limits)
        != RandomAccessReadCode::Ok) {
        return result;
    }
    const uint8_t b0 = u8(header[0U]);
    const uint8_t b1 = u8(header[1U]);
    const bool le    = b0 == 0x49U && b1 == 0x49U;
    const bool be    = b0 == 0x4dU && b1 == 0x4dU;
    if ((!le && !be) || std::memcmp(header.data() + 6U, "HEAPCCDR", 8U) != 0) {
        return result;
    }

    CiffConfig cfg;
    cfg.le               = le;
    uint32_t root_offset = 0U;
    if (!read_u32(cfg, header, 2U, &root_offset) || root_offset < 14U
        || root_offset > source.size) {
        result.decode.status = ExifDecodeStatus::Malformed;
        return result;
    }

    result.decode.status = ExifDecodeStatus::Ok;
    CiffSourceReader reader;
    reader.source            = &source;
    reader.window.storage    = scratch.read_window;
    reader.value             = scratch.value;
    reader.window_options    = scratch.window_options;
    reader.limits            = read_limits;
    reader.result            = &result;
    uint32_t directory_index = 0U;
    const bool any
        = decode_source_directory(cfg, &reader, root_offset,
                                  source.size - root_offset, "ciff_root", store,
                                  limits, &result.decode, 0U, &directory_index);
    if (any) {
        update_status(&result.decode, ExifDecodeStatus::Ok);
    } else if (result.decode.status == ExifDecodeStatus::Ok) {
        result.decode.status = ExifDecodeStatus::Unsupported;
    }
    return result;
}

}  // namespace openmeta::ciff_internal
