// SPDX-License-Identifier: Apache-2.0

#include "openmeta/printim_decode.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <cstdio>
#include <cstring>

namespace openmeta {
namespace {

    static constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }


    static bool match(std::span<const std::byte> bytes, uint64_t offset,
                      const char* s, uint32_t s_len) noexcept
    {
        if (offset + s_len > bytes.size()) {
            return false;
        }
        return std::memcmp(bytes.data() + static_cast<size_t>(offset), s,
                           static_cast<size_t>(s_len))
               == 0;
    }


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


    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4 > bytes.size()) {
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

}  // namespace

PrintImDecodeResult
decode_printim(std::span<const std::byte> bytes, MetaStore& store,
               const PrintImDecodeLimits& limits) noexcept
{
    PrintImDecodeResult out;
    out.status = PrintImDecodeStatus::Unsupported;

    if (limits.max_bytes != 0U && bytes.size() > limits.max_bytes) {
        out.status = PrintImDecodeStatus::LimitExceeded;
        return out;
    }

    if (!match(bytes, 0, "PrintIM\0", 8)) {
        return out;
    }
    if (bytes.size() < 16) {
        out.status = PrintImDecodeStatus::Malformed;
        return out;
    }

    // Layout:
    //   8  bytes: "PrintIM\0"
    //   4  bytes: version ASCII (e.g. "0300")
    //   2  bytes: reserved (u16 LE)
    //   2  bytes: entry count (u16 LE)
    //   N entries: u16 tag_id (LE) + u32 value (LE)
    uint16_t entry_count = 0;
    if (!read_u16le(bytes, 14, &entry_count)) {
        out.status = PrintImDecodeStatus::Malformed;
        return out;
    }
    if (entry_count > limits.max_entries) {
        out.status = PrintImDecodeStatus::LimitExceeded;
        return out;
    }

    const uint64_t needed = 16ULL + static_cast<uint64_t>(entry_count) * 6ULL;
    if (needed > bytes.size()) {
        out.status = PrintImDecodeStatus::Malformed;
        return out;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        out.status = PrintImDecodeStatus::LimitExceeded;
        return out;
    }

    // Version field (always emitted for valid PrintIM headers).
    {
        const std::string_view version(reinterpret_cast<const char*>(
                                           bytes.data() + 8),
                                       4);
        Entry e;
        e.key          = make_printim_field_key(store.arena(), "version");
        e.value        = make_text(store.arena(), version, TextEncoding::Ascii);
        e.origin.block = block;
        e.origin.order_in_block = 0;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 4;
        e.flags                 = EntryFlags::Derived;
        if (store.add_entry(e) == kInvalidEntryId) {
            out.status = PrintImDecodeStatus::LimitExceeded;
            return out;
        }
        out.entries_decoded += 1;
    }

    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t off = 16ULL + static_cast<uint64_t>(i) * 6ULL;
        uint16_t tag_id    = 0;
        uint32_t value     = 0;
        if (!read_u16le(bytes, off, &tag_id)
            || !read_u32le(bytes, off + 2, &value)) {
            out.status = PrintImDecodeStatus::Malformed;
            return out;
        }

        char key_buf[16];
        std::snprintf(key_buf, sizeof(key_buf), "0x%04X",
                      static_cast<unsigned>(tag_id));

        Entry e;
        e.key                   = make_printim_field_key(store.arena(),
                                                         std::string_view(key_buf));
        e.value                 = make_u32(value);
        e.origin.block          = block;
        e.origin.order_in_block = i + 1;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        if (store.add_entry(e) == kInvalidEntryId) {
            out.status = PrintImDecodeStatus::LimitExceeded;
            return out;
        }
        out.entries_decoded += 1;
    }

    out.status = PrintImDecodeStatus::Ok;
    return out;
}

}  // namespace openmeta
