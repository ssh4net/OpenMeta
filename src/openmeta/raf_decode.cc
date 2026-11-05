// SPDX-License-Identifier: Apache-2.0

#include "raf_decode_internal.h"

#include "exif_tiff_decode_internal.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>

namespace openmeta::raf_internal {
namespace {

    static constexpr uint64_t kRafVersionOffset = 0x3cU;

    struct RafHeaderField final {
        uint16_t tag    = 0;
        uint64_t offset = 0;
    };

    static constexpr RafHeaderField kRafHeaderU32Fields[] = {
        { 0x0048U, 0x48U }, { 0x004cU, 0x4cU }, { 0x0054U, 0x54U },
        { 0x0058U, 0x58U }, { 0x005cU, 0x5cU }, { 0x0060U, 0x60U },
        { 0x0064U, 0x64U }, { 0x0068U, 0x68U }, { 0x006cU, 0x6cU },
        { 0x0078U, 0x78U }, { 0x007cU, 0x7cU }, { 0x0080U, 0x80U },
        { 0x0084U, 0x84U },
    };

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }

    static std::span<const std::byte>
    raf_header_span(std::span<const std::byte> bytes) noexcept
    {
        uint32_t preview_offset = 0U;
        if (exif_internal::read_u32be(bytes, 0x54U, &preview_offset)
            && preview_offset != 0U && preview_offset < bytes.size()) {
            return bytes.first(preview_offset);
        }
        return bytes;
    }

    static bool is_printable_ascii(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint8_t c = u8(bytes[i]);
            if (c < 0x20U || c > 0x7eU) {
                return false;
            }
        }
        return true;
    }

    static bool make_raf_ifd_name(uint32_t index, std::span<char> scratch,
                                  std::string_view* out) noexcept
    {
        if (!out || scratch.empty()) {
            return false;
        }
        const int n = std::snprintf(scratch.data(), scratch.size(), "raf_%u",
                                    static_cast<unsigned>(index));
        if (n <= 0 || static_cast<size_t>(n) >= scratch.size()) {
            return false;
        }
        *out = std::string_view(scratch.data(), static_cast<size_t>(n));
        return true;
    }

    static bool make_rafdata_ifd_name(uint32_t index, std::span<char> scratch,
                                      std::string_view* out) noexcept
    {
        if (!out || scratch.empty()) {
            return false;
        }
        const int n = std::snprintf(scratch.data(), scratch.size(),
                                    "mk_fuji_rafdata_%u",
                                    static_cast<unsigned>(index));
        if (n <= 0 || static_cast<size_t>(n) >= scratch.size()) {
            return false;
        }
        *out = std::string_view(scratch.data(), static_cast<size_t>(n));
        return true;
    }

    static void update_status(ExifDecodeResult* out,
                              ExifDecodeStatus status) noexcept
    {
        exif_internal::update_status(out, status);
    }

    static bool can_emit(const ExifDecodeLimits& limits,
                         ExifDecodeResult* out) noexcept
    {
        if (!out) {
            return true;
        }
        if (out->entries_decoded + 1U > limits.max_total_entries) {
            update_status(out, ExifDecodeStatus::LimitExceeded);
            out->limit_reason = ExifLimitReason::MaxTotalEntries;
            return false;
        }
        return true;
    }

    static bool emit_entry(MetaStore& store, BlockId block,
                           uint32_t order_in_block, std::string_view ifd,
                           uint16_t tag, WireType wire_type,
                           uint32_t wire_count, const MetaValue& value,
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
        entry.origin.wire_type      = wire_type;
        entry.origin.wire_count     = wire_count;
        entry.value                 = value;
        entry.flags                 = flags;

        if (store.add_entry(entry) == kInvalidEntryId) {
            update_status(out, ExifDecodeStatus::LimitExceeded);
            return false;
        }
        if (out) {
            out->entries_decoded += 1U;
        }
        return true;
    }

    static MetaValue make_u16_be_array(ByteArena& arena,
                                       std::span<const std::byte> raw,
                                       ExifDecodeResult* out) noexcept
    {
        if ((raw.size() % 2U) != 0U || raw.size() > UINT32_MAX) {
            update_status(out, ExifDecodeStatus::Malformed);
            return make_bytes(arena, raw);
        }

        std::array<uint16_t, 64> values {};
        const size_t count = raw.size() / 2U;
        if (count > values.size()) {
            update_status(out, ExifDecodeStatus::LimitExceeded);
            return make_bytes(arena, raw);
        }

        for (size_t i = 0; i < count; ++i) {
            uint16_t v = 0;
            if (!exif_internal::read_u16be(raw, i * 2U, &v)) {
                update_status(out, ExifDecodeStatus::Malformed);
                return make_bytes(arena, raw);
            }
            values[i] = v;
        }
        return make_u16_array(arena,
                              std::span<const uint16_t>(values.data(), count));
    }

    static MetaValue decode_raf_value(MetaStore& store, uint16_t tag,
                                      std::span<const std::byte> raw,
                                      const ExifDecodeLimits& limits,
                                      ExifDecodeResult* out) noexcept
    {
        if (raw.size() > limits.max_value_bytes) {
            update_status(out, ExifDecodeStatus::LimitExceeded);
            return MetaValue {};
        }

        switch (tag) {
        case 0x0100U:
        case 0x0110U:
        case 0x0111U:
        case 0x0115U:
        case 0x0118U:
        case 0x0119U:
        case 0x0121U:
            if (raw.size() == 4U) {
                return make_u16_be_array(store.arena(), raw, out);
            }
            break;
        case 0x0117U: {
            uint32_t v = 0;
            if (raw.size() == 4U && exif_internal::read_u32be(raw, 0, &v)) {
                return make_u32(v);
            }
            break;
        }
        case 0x0130U:
            if (raw.size() == 1U) {
                return make_u8(u8(raw[0]));
            }
            break;
        case 0x0131U:
            return make_u8_array(
                store.arena(),
                std::span<const uint8_t>(
                    reinterpret_cast<const uint8_t*>(raw.data()), raw.size()));
        case 0x9200U:
        case 0x9650U: {
            uint32_t numer = 0;
            uint32_t denom = 0;
            if (raw.size() == 8U && exif_internal::read_u32be(raw, 0, &numer)
                && exif_internal::read_u32be(raw, 4, &denom)) {
                return make_srational(static_cast<int32_t>(numer),
                                      static_cast<int32_t>(denom));
            }
            break;
        }
        default: break;
        }

        return make_bytes(store.arena(), raw);
    }

    static void decode_rafdata(std::span<const std::byte> raw, uint32_t index,
                               MetaStore& store, BlockId block,
                               uint32_t order_base,
                               const ExifDecodeLimits& limits,
                               ExifDecodeResult* out) noexcept
    {
        if (raw.size() < 16U) {
            return;
        }

        char ifd_buf[32];
        std::string_view ifd_name;
        if (!make_rafdata_ifd_name(index, std::span<char>(ifd_buf), &ifd_name)) {
            update_status(out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        static constexpr uint16_t kTags[] = { 0x0000U, 0x0004U, 0x0008U,
                                              0x000cU };
        for (uint32_t i = 0; i < 4U; ++i) {
            uint32_t v = 0;
            if (!exif_internal::read_u32be(raw, static_cast<uint64_t>(i) * 4U,
                                           &v)) {
                update_status(out, ExifDecodeStatus::Malformed);
                return;
            }
            (void)emit_entry(store, block, order_base + i, ifd_name, kTags[i],
                             WireType { WireFamily::Other, 0 }, 1U, make_u32(v),
                             EntryFlags::Derived, limits, out);
        }
    }

    static bool decode_raf_directory(std::span<const std::byte> dir_bytes,
                                     uint32_t index, MetaStore& store,
                                     BlockId block,
                                     const ExifDecodeLimits& limits,
                                     ExifDecodeResult* out) noexcept
    {
        if (dir_bytes.size() < 4U) {
            update_status(out, ExifDecodeStatus::Malformed);
            return false;
        }

        uint32_t count = 0;
        if (!exif_internal::read_u32be(dir_bytes, 0, &count)) {
            update_status(out, ExifDecodeStatus::Malformed);
            return false;
        }
        if (count > 255U || count > limits.max_entries_per_ifd) {
            update_status(out, ExifDecodeStatus::LimitExceeded);
            if (out) {
                out->limit_reason = ExifLimitReason::MaxEntriesPerIfd;
            }
            return false;
        }

        char ifd_buf[16];
        std::string_view ifd_name;
        if (!make_raf_ifd_name(index, std::span<char>(ifd_buf), &ifd_name)) {
            update_status(out, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        uint64_t off = 4U;
        bool any     = false;
        for (uint32_t i = 0; i < count; ++i) {
            uint16_t tag = 0;
            uint16_t len = 0;
            if (!exif_internal::read_u16be(dir_bytes, off, &tag)
                || !exif_internal::read_u16be(dir_bytes, off + 2U, &len)) {
                update_status(out, ExifDecodeStatus::Malformed);
                return any;
            }
            off += 4U;
            if (off > dir_bytes.size() || len > dir_bytes.size() - off) {
                update_status(out, ExifDecodeStatus::Malformed);
                return any;
            }

            const std::span<const std::byte> raw
                = dir_bytes.subspan(static_cast<size_t>(off),
                                    static_cast<size_t>(len));

            EntryFlags flags = EntryFlags::None;
            MetaValue value;
            if (raw.size() > limits.max_value_bytes) {
                flags |= EntryFlags::Truncated;
                update_status(out, ExifDecodeStatus::LimitExceeded);
            } else {
                value = decode_raf_value(store, tag, raw, limits, out);
            }

            (void)emit_entry(store, block, i, ifd_name, tag,
                             WireType { WireFamily::Other, 0 }, len, value,
                             flags, limits, out);
            if (tag == 0xc000U && raw.size() <= limits.max_value_bytes) {
                decode_rafdata(raw, index, store, block, i + 1024U, limits,
                               out);
            }

            off += len;
            any = true;
        }

        return any;
    }

    static bool emit_header_fields(std::span<const std::byte> file_bytes,
                                   MetaStore& store, BlockId block,
                                   const ExifDecodeLimits& limits,
                                   ExifDecodeResult* out) noexcept
    {
        bool any = false;
        if (kRafVersionOffset + 4U <= file_bytes.size()) {
            const std::span<const std::byte> version
                = file_bytes.subspan(static_cast<size_t>(kRafVersionOffset),
                                     4U);
            if (is_printable_ascii(version)) {
                const std::string_view text(reinterpret_cast<const char*>(
                                                version.data()),
                                            version.size());
                any |= emit_entry(store, block, 0U, "raf_header", 0x003cU,
                                  WireType { WireFamily::Other, 0 }, 4U,
                                  make_text(store.arena(), text,
                                            TextEncoding::Ascii),
                                  EntryFlags::None, limits, out);
            }
        }

        for (uint32_t i = 0; i < std::size(kRafHeaderU32Fields); ++i) {
            uint32_t v              = 0;
            const RafHeaderField& f = kRafHeaderU32Fields[i];
            if (exif_internal::read_u32be(file_bytes, f.offset, &v)
                && v != 0U) {
                any |= emit_entry(store, block, i + 1U, "raf_header", f.tag,
                                  WireType { WireFamily::Other, 0 }, 1U,
                                  make_u32(v), EntryFlags::None, limits, out);
            }
        }
        return any;
    }

    static void decode_raf_private_ifds(
        const RandomAccessSourceRange& raf, std::span<const std::byte> header,
        MetaStore& store, const ExifRandomAccessScratch& scratch,
        const ExifDecodeLimits& limits, const RandomAccessReadLimits& read_limits,
        ExifRandomAccessDecodeResult* result) noexcept
    {
        static constexpr uint32_t offsets[] = { 0x64U, 0x80U };
        std::array<uint32_t, 2> previous {};
        uint32_t total_ifds = 0U;
        for (uint32_t stream = 0U; stream < 2U && result->input.ok(); ++stream) {
            uint32_t start = 0U, size = 0U;
            if (!exif_internal::read_u32be(header, offsets[stream], &start)
                || !exif_internal::read_u32be(header, offsets[stream] + 4U, &size)
                || start == 0U || size == 0U) continue;
            if (stream != 0U && start == previous[0]) continue;
            previous[stream] = start;
            if (start > raf.size || size > raf.size - start || size < 8U) {
                update_status(&result->decode, ExifDecodeStatus::Malformed);
                return;
            }
            const RandomAccessSourceRange range = make_random_access_source_range(
                raf.source, raf.source_offset + start, size);
            exif_internal::SourceTiffReader reader;
            reader.range = &range;
            reader.result = result;
            reader.limits = read_limits;
            reader.window.storage = scratch.read_window;
            reader.window_options = scratch.window_options;
            reader.window_options.minimum_read_bytes = 0U;
            reader.value_scratch = scratch.value;
            std::span<const std::byte> raw;
            if (!exif_internal::source_tiff_view(&reader, 0U, 8U, &raw)) return;
            TiffConfig cfg;
            if (u8(raw[0]) == 'I' && u8(raw[1]) == 'I') cfg.le = true;
            else if (u8(raw[0]) == 'M' && u8(raw[1]) == 'M') cfg.le = false;
            else continue;
            uint16_t magic = 0U;
            uint32_t first = 0U;
            if (!exif_internal::read_tiff_u16(cfg, raw, 2U, &magic)
                || magic != 42U) continue;
            if (!exif_internal::read_tiff_u32(cfg, raw, 4U, &first)) return;
            std::array<uint32_t, 128> pending {}, visited {};
            uint32_t pending_count = 0U, visited_count = 0U;
            if (first != 0U) pending[pending_count++] = first;
            ExifDecodeOptions options;
            options.decode_printim = false;
            options.decode_geotiff = false;
            options.limits = limits;
            while (pending_count != 0U && result->input.ok()) {
                const uint32_t at = pending[--pending_count];
                bool seen = false;
                for (uint32_t i = 0U; i < visited_count; ++i) seen |= visited[i] == at;
                if (seen) {
                    update_status(&result->decode, ExifDecodeStatus::Malformed);
                    return;
                }
                if (visited_count == visited.size() || total_ifds >= limits.max_ifds) {
                    update_status(&result->decode, ExifDecodeStatus::LimitExceeded);
                    result->decode.limit_reason = ExifLimitReason::MaxIfds;
                    result->decode.limit_ifd_offset = uint64_t(start) + at;
                    result->decode.limit_tag = 0xf000U;
                    return;
                }
                ++total_ifds;
                visited[visited_count++] = at;
                if (!exif_internal::source_tiff_view(&reader, at, 2U, &raw)) return;
                uint16_t count = 0U;
                if (!exif_internal::read_tiff_u16(cfg, raw, 0U, &count)) return;
                if (count > limits.max_entries_per_ifd) {
                    update_status(&result->decode, ExifDecodeStatus::LimitExceeded);
                    result->decode.limit_reason = ExifLimitReason::MaxEntriesPerIfd;
                    result->decode.limit_ifd_offset = uint64_t(start) + at;
                    result->decode.limit_tag = 0xf000U;
                    return;
                }
                const uint64_t table_size = 2U + uint64_t(count) * 12U + 4U;
                if (!exif_internal::source_tiff_contains(reader, at, table_size)) {
                    update_status(&result->decode, ExifDecodeStatus::Malformed);
                    return;
                }
                if (at != first && count != 0U) {
                    for (uint32_t i = 0U; i < count; ++i) {
                        if (!exif_internal::source_tiff_view(&reader,
                              uint64_t(at) + 2U + uint64_t(i) * 12U, 12U, &raw)) return;
                        exif_internal::ClassicIfdEntry entry;
                        if (!exif_internal::read_classic_ifd_entry(cfg, raw, 0U, &entry)) return;
                        if (uint64_t(entry.count32) * exif_internal::tiff_type_size(entry.type)
                            > limits.max_value_bytes) {
                            update_status(&result->decode, ExifDecodeStatus::LimitExceeded);
                            result->decode.limit_reason = ExifLimitReason::ValueCountTooLarge;
                            result->decode.limit_ifd_offset = uint64_t(start) + at;
                            result->decode.limit_tag = entry.tag;
                        }
                    }
                    char token[48];
                    const int written = std::snprintf(token, sizeof(token), "raf_tiff_%u_%u",
                                                      stream, visited_count - 2U);
                    if (written <= 0 || static_cast<size_t>(written) >= sizeof(token)) return;
                    (void)exif_internal::decode_classic_ifd_from_source(
                        &reader, cfg, at, exif_internal::OffsetPolicy {}, token,
                        store, options, &result->decode, EntryFlags::None);
                    if (!result->input.ok()) return;
                }
                for (uint32_t i = 0U; i < count; ++i) {
                    if (!exif_internal::source_tiff_view(&reader,
                            uint64_t(at) + 2U + uint64_t(i) * 12U, 12U, &raw)) return;
                    exif_internal::ClassicIfdEntry entry;
                    if (!exif_internal::read_classic_ifd_entry(cfg, raw, 0U, &entry)) return;
                    if (entry.tag != 0xf000U) continue;
                    if ((entry.type != 4U && entry.type != 13U) || entry.count32 != 1U) {
                        update_status(&result->decode, ExifDecodeStatus::Malformed);
                        return;
                    }
                    if (entry.value_or_off32 == 0U) continue;
                    if (pending_count == pending.size()) {
                        update_status(&result->decode, ExifDecodeStatus::LimitExceeded);
                        result->decode.limit_reason = ExifLimitReason::MaxIfds;
                        result->decode.limit_tag = 0xf000U;
                        return;
                    }
                    pending[pending_count++] = entry.value_or_off32;
                }
            }
        }
    }

}  // namespace

bool
looks_like_raf(std::span<const std::byte> file_bytes) noexcept
{
    return file_bytes.size() >= 16U
           && exif_internal::match_bytes(file_bytes, 0, "FUJIFILMCCD-RAW ",
                                         16U);
}

ExifDecodeResult
decode_raf_native(std::span<const std::byte> file_bytes, MetaStore& store,
                  const ExifDecodeLimits& limits) noexcept
{
    ExifDecodeResult out;
    out.status = ExifDecodeStatus::Ok;

    if (!looks_like_raf(file_bytes)) {
        out.status = ExifDecodeStatus::Unsupported;
        return out;
    }

    store.constrain_resources(limits.max_total_entries, limits.max_arena_bytes);
    if (store.resource_limit_exceeded()) {
        out.status = ExifDecodeStatus::LimitExceeded;
        out.limit_reason = ExifLimitReason::MaxArenaBytes;
        return out;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        out.status = ExifDecodeStatus::LimitExceeded;
        return out;
    }

    const std::span<const std::byte> header = raf_header_span(file_bytes);
    bool any = emit_header_fields(header, store, block, limits, &out);

    static constexpr uint64_t kDirOffsetFields[] = { 0x5cU, 0x78U };
    static constexpr uint64_t kDirLengthFields[] = { 0x60U, 0x7cU };
    for (uint32_t i = 0; i < std::size(kDirOffsetFields); ++i) {
        uint32_t dir_off = 0;
        uint32_t dir_len = 0;
        if (!exif_internal::read_u32be(header, kDirOffsetFields[i], &dir_off)
            || !exif_internal::read_u32be(header, kDirLengthFields[i],
                                          &dir_len)) {
            continue;
        }
        if (dir_off == 0U || dir_len == 0U) {
            continue;
        }
        if (dir_off > file_bytes.size()
            || dir_len > file_bytes.size() - static_cast<uint64_t>(dir_off)) {
            update_status(&out, ExifDecodeStatus::Malformed);
            continue;
        }

        const std::span<const std::byte> dir
            = file_bytes.subspan(static_cast<size_t>(dir_off),
                                 static_cast<size_t>(dir_len));
        any |= decode_raf_directory(dir, i, store, block, limits, &out);
    }

    ExifRandomAccessDecodeResult private_result;
    private_result.decode = out;
    const RandomAccessSourceRange range = make_random_access_source_range(
        make_memory_random_access_source(file_bytes));
    decode_raf_private_ifds(range, header, store, {}, limits, {}, &private_result);
    out = private_result.decode;
    any |= out.entries_decoded != 0U;
    if (!any && out.status == ExifDecodeStatus::Ok) {
        out.status = ExifDecodeStatus::Unsupported;
    }
    return out;
}

ExifRandomAccessDecodeResult
decode_raf_native_random_access(
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
        result.decode = decode_raf_native(
            std::span<const std::byte>(begin, static_cast<size_t>(source.size)),
            store, limits);
        return result;
    }
    if (source.size < 16U) {
        return result;
    }

    std::array<std::byte, 0x88U> header {};
    const size_t header_size = static_cast<size_t>(
        source.size < header.size() ? source.size : header.size());
    if (random_access_read_exact(source, 0U,
                                 std::span<std::byte>(header).first(header_size),
                                 &result.input, read_limits)
        != RandomAccessReadCode::Ok) {
        return result;
    }
    const std::span<const std::byte> header_bytes = raf_header_span(
        std::span<const std::byte>(header.data(), header_size));
    if (!looks_like_raf(header_bytes)) {
        return result;
    }

    result.decode.status = ExifDecodeStatus::Ok;
    store.constrain_resources(limits.max_total_entries, limits.max_arena_bytes);
    if (store.resource_limit_exceeded()) {
        result.decode.status = ExifDecodeStatus::LimitExceeded;
        result.decode.limit_reason = ExifLimitReason::MaxArenaBytes;
        return result;
    }
    const BlockId block  = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        result.decode.status = ExifDecodeStatus::LimitExceeded;
        return result;
    }
    bool any = emit_header_fields(header_bytes, store, block, limits,
                                  &result.decode);

    static constexpr uint64_t kDirOffsetFields[] = { 0x5cU, 0x78U };
    static constexpr uint64_t kDirLengthFields[] = { 0x60U, 0x7cU };
    for (uint32_t i = 0U; i < std::size(kDirOffsetFields); ++i) {
        uint32_t dir_offset = 0U;
        uint32_t dir_size   = 0U;
        if (!exif_internal::read_u32be(header_bytes, kDirOffsetFields[i],
                                       &dir_offset)
            || !exif_internal::read_u32be(header_bytes, kDirLengthFields[i],
                                          &dir_size)
            || dir_offset == 0U || dir_size == 0U) {
            continue;
        }
        if (dir_offset > source.size
            || dir_size > source.size - static_cast<uint64_t>(dir_offset)) {
            update_status(&result.decode, ExifDecodeStatus::Malformed);
            continue;
        }
        if (dir_size > scratch.value.size()) {
            result.value_scratch_needed = dir_size > result.value_scratch_needed
                                              ? dir_size
                                              : result.value_scratch_needed;
            update_status(&result.decode, ExifDecodeStatus::OutputTruncated);
            continue;
        }
        const std::span<std::byte> directory = scratch.value.first(
            static_cast<size_t>(dir_size));
        if (random_access_read_exact(source, dir_offset, directory,
                                     &result.input, read_limits)
            != RandomAccessReadCode::Ok) {
            return result;
        }
        any |= decode_raf_directory(directory, i, store, block, limits,
                                    &result.decode);
    }

    decode_raf_private_ifds(source, header_bytes, store, scratch, limits,
                            read_limits, &result);
    any |= result.decode.entries_decoded != 0U;
    if (!any && result.decode.status == ExifDecodeStatus::Ok) {
        result.decode.status = ExifDecodeStatus::Unsupported;
    }
    return result;
}

}  // namespace openmeta::raf_internal
