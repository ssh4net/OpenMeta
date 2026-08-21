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

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        out.status = ExifDecodeStatus::LimitExceeded;
        return out;
    }

    bool any = emit_header_fields(file_bytes, store, block, limits, &out);

    static constexpr uint64_t kDirOffsetFields[] = { 0x5cU, 0x78U };
    static constexpr uint64_t kDirLengthFields[] = { 0x60U, 0x7cU };
    for (uint32_t i = 0; i < std::size(kDirOffsetFields); ++i) {
        uint32_t dir_off = 0;
        uint32_t dir_len = 0;
        if (!exif_internal::read_u32be(file_bytes, kDirOffsetFields[i], &dir_off)
            || !exif_internal::read_u32be(file_bytes, kDirLengthFields[i],
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
    const std::span<const std::byte> header_bytes(header.data(), header_size);
    if (!looks_like_raf(header_bytes)) {
        return result;
    }

    result.decode.status = ExifDecodeStatus::Ok;
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

    if (!any && result.decode.status == ExifDecodeStatus::Ok) {
        result.decode.status = ExifDecodeStatus::Unsupported;
    }
    return result;
}

}  // namespace openmeta::raf_internal
