// SPDX-License-Identifier: Apache-2.0

#include "x3f_decode_internal.h"

#include "openmeta/container_scan.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace openmeta::x3f_internal {
namespace {

    struct X3fSection final {
        uint32_t offset = 0;
        uint32_t size   = 0;
        uint32_t tag    = 0;
    };

    struct X3fPropMap final {
        std::string_view key;
        uint16_t tag = 0;
    };

    static constexpr uint32_t x3f_tag(char a, char b, char c, char d) noexcept
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 0U)
               | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8U)
               | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16U)
               | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24U);
    }

    static constexpr X3fPropMap kX3fPropMap[] = {
        { "AEMODE", 0x0001U },       { "AFAREA", 0x0002U },
        { "AFINFOCUS", 0x0003U },    { "AFMODE", 0x0004U },
        { "AP_DESC", 0x0005U },      { "APERTURE", 0x0006U },
        { "CAMMANUF", 0x0007U },     { "CAMMODEL", 0x0008U },
        { "CAMNAME", 0x0009U },      { "CAMSERIAL", 0x000aU },
        { "COLORSPACE", 0x000bU },   { "DRIVE", 0x000cU },
        { "EXPCOMP", 0x000dU },      { "EXPNET", 0x000eU },
        { "EXPTIME", 0x000fU },      { "FIRMVERS", 0x0010U },
        { "FLASH", 0x0011U },        { "FLENGTH", 0x0012U },
        { "FLEQ35MM", 0x0013U },     { "FOCUS", 0x0014U },
        { "IMAGERTEMP", 0x0015U },   { "ISO", 0x0016U },
        { "LENSMODEL", 0x0017U },    { "PMODE", 0x0018U },
        { "RESOLUTION", 0x0019U },   { "TIME", 0x001aU },
        { "WB_DESC", 0x001bU },      { "CM_DESC", 0x001cU },
        { "SHUTTER", 0x001dU },      { "SH_DESC", 0x001eU },
        { "LENSARANGE", 0x001fU },   { "LENSFRANGE", 0x0020U },
        { "BURST", 0x0021U },        { "BRACKET", 0x0022U },
        { "EVAL_STATE", 0x0023U },   { "IMAGERBOARDID", 0x0024U },
        { "IMAGEBOARDID", 0x0025U }, { "SENSORID", 0x0026U },
    };

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }

    static bool match(std::span<const std::byte> bytes, uint64_t offset,
                      const char* s, uint32_t n) noexcept
    {
        if (!s || offset + n > bytes.size()) {
            return false;
        }
        return std::memcmp(bytes.data() + static_cast<size_t>(offset), s, n)
               == 0;
    }

    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4U > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0U])) << 0U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1U])) << 8U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2U])) << 16U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3U])) << 24U;
        *out = v;
        return true;
    }

    static void update_status(ExifDecodeResult* out,
                              ExifDecodeStatus status) noexcept
    {
        if (!out || out->status == ExifDecodeStatus::LimitExceeded) {
            return;
        }
        if (status == ExifDecodeStatus::LimitExceeded
            || (status == ExifDecodeStatus::Malformed
                && out->status != ExifDecodeStatus::Malformed)
            || (status == ExifDecodeStatus::OutputTruncated
                && out->status != ExifDecodeStatus::Malformed)) {
            out->status = status;
        }
    }

    static bool can_emit(const ExifDecodeLimits& limits,
                         ExifDecodeResult* out) noexcept
    {
        if (!out) {
            return true;
        }
        if (out->entries_decoded + 1U > limits.max_total_entries) {
            out->status       = ExifDecodeStatus::LimitExceeded;
            out->limit_reason = ExifLimitReason::MaxTotalEntries;
            return false;
        }
        return true;
    }

    static bool emit_entry(MetaStore& store, BlockId block,
                           uint32_t order_in_block, std::string_view ifd,
                           uint16_t tag, const MetaValue& value,
                           EntryFlags flags, const ExifDecodeLimits& limits,
                           ExifDecodeResult* out) noexcept
    {
        if (ifd.empty() || !can_emit(limits, out)) {
            return false;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = order_in_block;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = value.count;
        entry.value                 = value;
        entry.flags                 = flags;
        if (store.add_entry(entry) == kInvalidEntryId) {
            if (out) {
                out->status = ExifDecodeStatus::LimitExceeded;
            }
            return false;
        }
        if (out) {
            out->entries_decoded += 1U;
        }
        return true;
    }

    static MetaValue make_fixed_ascii(ByteArena& arena,
                                      std::span<const std::byte> raw) noexcept
    {
        size_t n = 0U;
        while (n < raw.size() && raw[n] != std::byte { 0 }) {
            const uint8_t c = u8(raw[n]);
            if (c < 0x20U || c > 0x7eU) {
                return make_bytes(arena, raw);
            }
            ++n;
        }
        const std::string_view text(reinterpret_cast<const char*>(raw.data()),
                                    n);
        return make_text(arena, text, TextEncoding::Ascii);
    }

    static MetaValue make_hex_text(ByteArena& arena,
                                   std::span<const std::byte> raw) noexcept
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::array<char, 64> buf {};
        if (raw.size() * 2U > buf.size()) {
            return make_bytes(arena, raw);
        }
        for (size_t i = 0; i < raw.size(); ++i) {
            const uint8_t v  = u8(raw[i]);
            buf[i * 2U + 0U] = kHex[(v >> 4U) & 0x0fU];
            buf[i * 2U + 1U] = kHex[(v >> 0U) & 0x0fU];
        }
        return make_text(arena, std::string_view(buf.data(), raw.size() * 2U),
                         TextEncoding::Ascii);
    }

    static MetaValue make_version_text(ByteArena& arena,
                                       uint32_t version) noexcept
    {
        std::array<char, 32> buf {};
        const uint32_t major = version >> 16U;
        const uint32_t minor = version & 0xffffU;
        const int n          = std::snprintf(buf.data(), buf.size(), "%u.%u",
                                             static_cast<unsigned>(major),
                                             static_cast<unsigned>(minor));
        if (n <= 0 || static_cast<size_t>(n) >= buf.size()) {
            return make_u32(version);
        }
        return make_text(arena,
                         std::string_view(buf.data(), static_cast<size_t>(n)),
                         TextEncoding::Ascii);
    }

    static bool parse_dir(std::span<const std::byte> file_bytes,
                          std::array<X3fSection, 64>* sections,
                          uint32_t* section_count,
                          ExifDecodeResult* out) noexcept
    {
        if (!sections || !section_count) {
            return false;
        }
        *section_count = 0U;
        if (file_bytes.size() < 16U) {
            return false;
        }

        uint32_t dir_off = 0;
        if (!read_u32le(file_bytes, file_bytes.size() - 4U, &dir_off)) {
            return false;
        }
        if (dir_off > file_bytes.size() - 16U
            || !match(file_bytes, dir_off, "SECd", 4U)) {
            return false;
        }

        uint32_t entries = 0;
        if (!read_u32le(file_bytes, dir_off + 8U, &entries)) {
            return false;
        }
        if (entries > sections->size()) {
            if (out) {
                out->status       = ExifDecodeStatus::LimitExceeded;
                out->limit_reason = ExifLimitReason::MaxEntriesPerIfd;
            }
            entries = static_cast<uint32_t>(sections->size());
        }
        if (dir_off + 12ULL + static_cast<uint64_t>(entries) * 12ULL
            > file_bytes.size()) {
            if (out) {
                out->status = ExifDecodeStatus::Malformed;
            }
            return false;
        }

        for (uint32_t i = 0; i < entries; ++i) {
            const uint64_t eoff = dir_off + 12ULL
                                  + static_cast<uint64_t>(i) * 12ULL;
            X3fSection section;
            if (!read_u32le(file_bytes, eoff + 0U, &section.offset)
                || !read_u32le(file_bytes, eoff + 4U, &section.size)
                || !read_u32le(file_bytes, eoff + 8U, &section.tag)) {
                if (out) {
                    out->status = ExifDecodeStatus::Malformed;
                }
                return false;
            }
            if (section.offset > file_bytes.size()
                || section.size > file_bytes.size()
                                      - static_cast<uint64_t>(section.offset)) {
                if (out) {
                    out->status = ExifDecodeStatus::Malformed;
                }
                return false;
            }
            (*sections)[*section_count] = section;
            *section_count += 1U;
        }
        return true;
    }

    static bool ascii_from_utf16le(std::span<const std::byte> raw,
                                   uint32_t char_pos, std::span<char> out,
                                   std::string_view* text,
                                   bool* truncated) noexcept
    {
        if (!text || !truncated || out.empty()) {
            return false;
        }
        *text                = std::string_view();
        *truncated           = false;
        const uint64_t start = static_cast<uint64_t>(char_pos) * 2ULL;
        if (start >= raw.size()) {
            return false;
        }

        size_t dst = 0U;
        for (uint64_t off = start; off + 1U < raw.size(); off += 2U) {
            uint16_t ch = 0;
            ch          = static_cast<uint16_t>(u8(raw[off + 0U]))
                 | static_cast<uint16_t>(u8(raw[off + 1U]) << 8U);
            if (ch == 0U) {
                *text = std::string_view(out.data(), dst);
                return true;
            }
            if (ch < 0x20U || ch > 0x7eU) {
                return false;
            }
            if (dst + 1U >= out.size()) {
                *truncated = true;
                *text      = std::string_view(out.data(), dst);
                return true;
            }
            out[dst++] = static_cast<char>(ch);
        }
        return false;
    }

    static uint16_t prop_tag_from_key(std::string_view key) noexcept
    {
        for (size_t i = 0; i < sizeof(kX3fPropMap) / sizeof(kX3fPropMap[0]);
             ++i) {
            if (kX3fPropMap[i].key == key) {
                return kX3fPropMap[i].tag;
            }
        }
        return 0U;
    }

    static bool decode_properties(std::span<const std::byte> prop,
                                  MetaStore& store, BlockId block,
                                  const ExifDecodeLimits& limits,
                                  ExifDecodeResult* out) noexcept
    {
        if (prop.size() < 24U || !match(prop, 0U, "SECp", 4U)) {
            return false;
        }

        uint32_t entries = 0;
        uint32_t fmt     = 0;
        uint32_t len     = 0;
        if (!read_u32le(prop, 8U, &entries) || !read_u32le(prop, 12U, &fmt)
            || !read_u32le(prop, 20U, &len) || fmt != 0U) {
            return false;
        }
        if (entries > 1024U) {
            if (out) {
                out->status       = ExifDecodeStatus::LimitExceeded;
                out->limit_reason = ExifLimitReason::MaxEntriesPerIfd;
            }
            return false;
        }
        const uint64_t table_bytes = static_cast<uint64_t>(entries) * 8ULL;
        const uint64_t char_off    = 24ULL + table_bytes;
        const uint64_t char_bytes  = static_cast<uint64_t>(len) * 2ULL;
        if (char_off > prop.size() || char_bytes > prop.size() - char_off) {
            if (out) {
                out->status = ExifDecodeStatus::Malformed;
            }
            return false;
        }

        const std::span<const std::byte> chars
            = prop.subspan(static_cast<size_t>(char_off),
                           static_cast<size_t>(char_bytes));
        bool any = false;
        for (uint32_t i = 0; i < entries; ++i) {
            uint32_t name_pos = 0;
            uint32_t val_pos  = 0;
            if (!read_u32le(prop, 24ULL + static_cast<uint64_t>(i) * 8ULL,
                            &name_pos)
                || !read_u32le(prop, 28ULL + static_cast<uint64_t>(i) * 8ULL,
                               &val_pos)) {
                if (out) {
                    out->status = ExifDecodeStatus::Malformed;
                }
                return any;
            }

            std::array<char, 80> name_buf {};
            std::array<char, 512> value_buf {};
            std::string_view key;
            std::string_view value;
            bool name_truncated  = false;
            bool value_truncated = false;
            if (!ascii_from_utf16le(chars, name_pos, name_buf, &key,
                                    &name_truncated)
                || name_truncated
                || !ascii_from_utf16le(chars, val_pos, value_buf, &value,
                                       &value_truncated)) {
                continue;
            }

            const uint16_t tag = prop_tag_from_key(key);
            if (tag == 0U) {
                continue;
            }
            EntryFlags flags = EntryFlags::None;
            if (value_truncated) {
                flags |= EntryFlags::Truncated;
            }
            const MetaValue mv = make_text(store.arena(), value,
                                           TextEncoding::Ascii);
            any |= emit_entry(store, block, i, "x3f_prop", tag, mv, flags,
                              limits, out);
        }
        return any;
    }

    static bool emit_header(std::span<const std::byte> file_bytes,
                            MetaStore& store, BlockId block,
                            const ExifDecodeLimits& limits,
                            ExifDecodeResult* out) noexcept
    {
        if (file_bytes.size() < 40U) {
            return false;
        }

        uint32_t version = 0;
        if (!read_u32le(file_bytes, 4U, &version)) {
            return false;
        }
        const uint32_t major = version >> 16U;
        const uint32_t minor = version & 0xffffU;
        const bool header4   = major >= 4U;

        bool any = false;
        any |= emit_entry(store, block, 0U, "x3f_header", 0x0001U,
                          make_version_text(store.arena(), version),
                          EntryFlags::None, limits, out);

        if (file_bytes.size() >= 24U) {
            any |= emit_entry(store, block, 1U, "x3f_header", 0x0002U,
                              make_hex_text(store.arena(),
                                            file_bytes.subspan(8U, 16U)),
                              EntryFlags::None, limits, out);
        }
        if (file_bytes.size() >= 28U) {
            uint32_t mark_bits = 0;
            if (read_u32le(file_bytes, 24U, &mark_bits)) {
                any |= emit_entry(store, block, 2U, "x3f_header", 0x0006U,
                                  make_u32(mark_bits), EntryFlags::None, limits,
                                  out);
            }
        }

        const uint64_t width_off  = header4 ? 40U : 28U;
        const uint64_t height_off = header4 ? 44U : 32U;
        const uint64_t rotate_off = header4 ? 48U : 36U;
        uint32_t v                = 0;
        if (read_u32le(file_bytes, width_off, &v)) {
            any |= emit_entry(store, block, 3U, "x3f_header", 0x0007U,
                              make_u32(v), EntryFlags::None, limits, out);
        }
        if (read_u32le(file_bytes, height_off, &v)) {
            any |= emit_entry(store, block, 4U, "x3f_header", 0x0008U,
                              make_u32(v), EntryFlags::None, limits, out);
        }
        if (read_u32le(file_bytes, rotate_off, &v)) {
            any |= emit_entry(store, block, 5U, "x3f_header", 0x0009U,
                              make_u32(v), EntryFlags::None, limits, out);
        }
        if (!header4 && file_bytes.size() >= 72U) {
            any |= emit_entry(store, block, 6U, "x3f_header", 0x000aU,
                              make_fixed_ascii(store.arena(),
                                               file_bytes.subspan(40U, 32U)),
                              EntryFlags::None, limits, out);
        }
        if (!header4 && major == 2U && minor >= 3U
            && file_bytes.size() >= 104U) {
            any |= emit_entry(store, block, 7U, "x3f_header", 0x0012U,
                              make_fixed_ascii(store.arena(),
                                               file_bytes.subspan(72U, 32U)),
                              EntryFlags::None, limits, out);
        }

        uint64_t hdr_len = 0U;
        if (!header4 && major == 2U && minor >= 1U) {
            hdr_len = (minor >= 3U) ? 104U : 72U;
        }
        if (hdr_len != 0U && file_bytes.size() >= hdr_len + 160U) {
            const uint64_t tag_base = hdr_len;
            const uint64_t val_base = hdr_len + 32U;
            for (uint32_t i = 0; i < 32U; ++i) {
                const uint8_t tag = u8(file_bytes[tag_base + i]);
                if (tag == 0U || tag > 10U) {
                    continue;
                }
                uint32_t bits = 0;
                if (!read_u32le(file_bytes, val_base + i * 4U, &bits)) {
                    continue;
                }
                any |= emit_entry(store, block, 32U + i, "x3f_header_ext", tag,
                                  make_f32_bits(bits), EntryFlags::None, limits,
                                  out);
            }
        }

        return any;
    }

}  // namespace

bool
looks_like_x3f(std::span<const std::byte> file_bytes) noexcept
{
    return file_bytes.size() >= 4U && match(file_bytes, 0U, "FOVb", 4U);
}

ExifDecodeResult
decode_x3f_native(std::span<const std::byte> file_bytes, MetaStore& store,
                  const ExifDecodeLimits& limits) noexcept
{
    ExifDecodeResult out;
    out.status = ExifDecodeStatus::Ok;
    if (!looks_like_x3f(file_bytes)) {
        out.status = ExifDecodeStatus::Unsupported;
        return out;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        out.status = ExifDecodeStatus::LimitExceeded;
        return out;
    }

    bool any = emit_header(file_bytes, store, block, limits, &out);

    std::array<X3fSection, 64> sections {};
    uint32_t section_count = 0U;
    if (parse_dir(file_bytes, &sections, &section_count, &out)) {
        for (uint32_t i = 0; i < section_count; ++i) {
            const X3fSection& s = sections[i];
            if (s.tag != x3f_tag('P', 'R', 'O', 'P')) {
                continue;
            }
            const std::span<const std::byte> prop
                = file_bytes.subspan(static_cast<size_t>(s.offset),
                                     static_cast<size_t>(s.size));
            any |= decode_properties(prop, store, block, limits, &out);
        }
    }

    if (!any && out.status == ExifDecodeStatus::Ok) {
        out.status = ExifDecodeStatus::Unsupported;
    }
    return out;
}

ExifRandomAccessDecodeResult
decode_x3f_native_random_access(
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
        result.decode = decode_x3f_native(
            std::span<const std::byte>(begin, static_cast<size_t>(source.size)),
            store, limits);
        return result;
    }
    if (source.size < 4U) {
        return result;
    }

    std::array<std::byte, 264U> header {};
    const size_t header_size = static_cast<size_t>(
        source.size < header.size() ? source.size : header.size());
    if (random_access_read_exact(source, 0U,
                                 std::span<std::byte>(header).first(header_size),
                                 &result.input, read_limits)
        != RandomAccessReadCode::Ok) {
        return result;
    }
    const std::span<const std::byte> header_bytes(header.data(), header_size);
    if (!looks_like_x3f(header_bytes)) {
        return result;
    }

    result.decode.status = ExifDecodeStatus::Ok;
    const BlockId block  = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        result.decode.status = ExifDecodeStatus::LimitExceeded;
        return result;
    }
    bool any = emit_header(header_bytes, store, block, limits, &result.decode);
    if (source.size < 16U) {
        if (!any) {
            result.decode.status = ExifDecodeStatus::Unsupported;
        }
        return result;
    }

    std::array<std::byte, 4U> tail {};
    if (random_access_read_exact(source, source.size - tail.size(), tail,
                                 &result.input, read_limits)
        != RandomAccessReadCode::Ok) {
        return result;
    }
    uint32_t directory_offset = 0U;
    if (!read_u32le(tail, 0U, &directory_offset)
        || directory_offset > source.size - 16U) {
        if (!any) {
            result.decode.status = ExifDecodeStatus::Unsupported;
        }
        return result;
    }

    std::array<std::byte, 12U + 64U * 12U> directory {};
    if (random_access_read_exact(source, directory_offset,
                                 std::span<std::byte>(directory).first(12U),
                                 &result.input, read_limits)
        != RandomAccessReadCode::Ok) {
        return result;
    }
    if (!match(directory, 0U, "SECd", 4U)) {
        if (!any) {
            result.decode.status = ExifDecodeStatus::Unsupported;
        }
        return result;
    }
    uint32_t section_count = 0U;
    if (!read_u32le(directory, 8U, &section_count)) {
        update_status(&result.decode, ExifDecodeStatus::Malformed);
        return result;
    }
    if (section_count > 64U) {
        result.decode.status       = ExifDecodeStatus::LimitExceeded;
        result.decode.limit_reason = ExifLimitReason::MaxEntriesPerIfd;
        section_count              = 64U;
    }
    const uint64_t directory_size
        = 12ULL + static_cast<uint64_t>(section_count) * 12ULL;
    if (directory_size > source.size - directory_offset) {
        update_status(&result.decode, ExifDecodeStatus::Malformed);
        return result;
    }
    if (directory_size > 12U
        && random_access_read_exact(source, directory_offset + 12U,
                                    std::span<std::byte>(directory).subspan(
                                        12U, static_cast<size_t>(directory_size
                                                                 - 12U)),
                                    &result.input, read_limits)
               != RandomAccessReadCode::Ok) {
        return result;
    }

    for (uint32_t i = 0U; i < section_count; ++i) {
        const uint64_t entry_offset = 12ULL + static_cast<uint64_t>(i) * 12ULL;
        X3fSection section;
        if (!read_u32le(directory, entry_offset + 0U, &section.offset)
            || !read_u32le(directory, entry_offset + 4U, &section.size)
            || !read_u32le(directory, entry_offset + 8U, &section.tag)) {
            update_status(&result.decode, ExifDecodeStatus::Malformed);
            return result;
        }
        if (section.offset > source.size
            || section.size
                   > source.size - static_cast<uint64_t>(section.offset)) {
            update_status(&result.decode, ExifDecodeStatus::Malformed);
            return result;
        }
        if (section.tag != x3f_tag('P', 'R', 'O', 'P')) {
            continue;
        }
        if (section.size > scratch.value.size()) {
            result.value_scratch_needed = section.size
                                                  > result.value_scratch_needed
                                              ? section.size
                                              : result.value_scratch_needed;
            update_status(&result.decode, ExifDecodeStatus::OutputTruncated);
            continue;
        }
        const std::span<std::byte> property = scratch.value.first(
            static_cast<size_t>(section.size));
        if (random_access_read_exact(source, section.offset, property,
                                     &result.input, read_limits)
            != RandomAccessReadCode::Ok) {
            return result;
        }
        any |= decode_properties(property, store, block, limits,
                                 &result.decode);
    }

    if (!any && result.decode.status == ExifDecodeStatus::Ok) {
        result.decode.status = ExifDecodeStatus::Unsupported;
    }
    return result;
}

}  // namespace openmeta::x3f_internal
