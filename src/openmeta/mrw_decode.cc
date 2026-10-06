// SPDX-License-Identifier: Apache-2.0

#include "mrw_decode_internal.h"

#include "exif_tiff_decode_internal.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <span>
#include <string_view>

namespace openmeta::mrw_internal {
namespace {

    struct ByteField final {
        uint8_t offset = 0;
        uint16_t tag   = 0;
    };

    struct WordPairField final {
        uint8_t offset = 0;
        uint16_t tag   = 0;
    };

    static constexpr ByteField kPrdByteFields[] = {
        { 16U, 0x0010U },
        { 17U, 0x0011U },
        { 18U, 0x0012U },
        { 23U, 0x0017U },
    };

    static constexpr ByteField kRifSignedByteFields[] = {
        { 1U, 0x0001U },
        { 2U, 0x0002U },
        { 3U, 0x0003U },
        { 59U, 0x003bU },
    };

    static constexpr ByteField kRifUnsignedByteFields[] = {
        { 4U, 0x0004U },
        { 5U, 0x0005U },
        { 6U, 0x0006U },
        { 57U, 0x0039U },
    };

    static constexpr WordPairField kRifWordPairFields[] = {
        { 8U, 0x0008U },  { 12U, 0x000cU }, { 16U, 0x0010U },
        { 20U, 0x0014U }, { 24U, 0x0018U }, { 28U, 0x001cU },
    };

    static constexpr WordPairField kRifA100WordPairFields[] = {
        { 32U, 0x0020U },
        { 36U, 0x0024U },
        { 40U, 0x0028U },
        { 44U, 0x002cU },
    };

    struct RifContext final {
        bool non_sony_make      = false;
        bool sony_make          = false;
        bool a100_model         = false;
        bool a200_or_a700_model = false;
        bool prd_present        = false;
    };

    static uint8_t u8(std::byte value) noexcept
    {
        return static_cast<uint8_t>(value);
    }

    static bool has_range(std::span<const std::byte> bytes, uint64_t offset,
                          uint64_t count) noexcept
    {
        return offset <= bytes.size()
               && count <= bytes.size() - static_cast<size_t>(offset);
    }

    static bool read_word(std::span<const std::byte> bytes, uint64_t offset,
                          bool le, uint16_t* out) noexcept
    {
        if (!out || !has_range(bytes, offset, 2U)) {
            return false;
        }
        const uint16_t a = u8(bytes[static_cast<size_t>(offset)]);
        const uint16_t b = u8(bytes[static_cast<size_t>(offset + 1U)]);
        *out = static_cast<uint16_t>(le ? a | (b << 8U) : (a << 8U) | b);
        return true;
    }

    static bool read_dword(std::span<const std::byte> bytes, uint64_t offset,
                           bool le, uint32_t* out) noexcept
    {
        if (!out || !has_range(bytes, offset, 4U)) {
            return false;
        }
        uint32_t value = 0U;
        for (uint32_t i = 0U; i < 4U; ++i) {
            const uint32_t shift = le ? i * 8U : (3U - i) * 8U;
            value |= uint32_t(u8(bytes[static_cast<size_t>(offset + i)]))
                     << shift;
        }
        *out = value;
        return true;
    }

    static bool matches(std::span<const std::byte> bytes, uint64_t offset,
                        const char* text, uint64_t count) noexcept
    {
        return text && has_range(bytes, offset, count)
               && std::memcmp(bytes.data() + static_cast<size_t>(offset), text,
                              static_cast<size_t>(count))
                      == 0;
    }

    static std::string_view snapshot_ifd0_text(const MetaStore& store,
                                               uint16_t tag,
                                               std::span<char> scratch) noexcept
    {
        const MetaKeyView key = make_exif_tag_key_view("ifd0", tag);
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (compare_key_view(store.arena(), key, entry.key) != 0
                || entry.value.kind != MetaValueKind::Text
                || (entry.value.text_encoding != TextEncoding::Ascii
                    && entry.value.text_encoding != TextEncoding::Utf8)
                || entry.value.count != entry.value.data.span.size) {
                continue;
            }
            const std::span<const std::byte> raw = store.arena().span(
                entry.value.data.span);
            if (raw.size() != entry.value.count) {
                continue;
            }
            size_t size = raw.size();
            for (size_t j = 0U; j < raw.size(); ++j) {
                if (raw[j] == std::byte { 0 }) {
                    size = j;
                    break;
                }
            }
            while (size > 0U && raw[size - 1U] == std::byte { ' ' }) {
                --size;
            }
            if (size == 0U || size >= scratch.size()) {
                return {};
            }
            std::memcpy(scratch.data(), raw.data(), size);
            return std::string_view(scratch.data(), size);
        }
        return {};
    }

    static bool contains_prd_segment(std::span<const std::byte> file_bytes,
                                     uint64_t metadata_end, bool le) noexcept
    {
        // ExifTool sets MinoltaPRD from an 8-byte FirmwareID only for
        // standalone MRW; the caller applies that file-type gate. This
        // pre-scan finds the gate; the main walk validates the full stream.
        uint64_t offset = 8U;
        while (offset < metadata_end) {
            if (metadata_end - offset < 8U) {
                return false;
            }
            uint32_t segment_size = 0U;
            if (!read_dword(file_bytes, offset + 4U, le, &segment_size)) {
                return false;
            }
            const uint64_t payload_offset = offset + 8U;
            if (segment_size > metadata_end - payload_offset) {
                return false;
            }
            if (matches(file_bytes, offset, "\0PRD", 4U)
                && segment_size >= 8U) {
                return true;
            }
            offset = payload_offset + static_cast<uint64_t>(segment_size);
        }
        return false;
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

    static void set_limit(ExifDecodeResult* out,
                          ExifLimitReason reason) noexcept
    {
        update_status(out, ExifDecodeStatus::LimitExceeded);
        if (out) {
            out->limit_reason = reason;
        }
    }

    static bool can_emit(const MetaStore& store, uint32_t order,
                         uint32_t order_base, const ExifDecodeLimits& limits,
                         ExifDecodeResult* out) noexcept
    {
        if (order - order_base >= limits.max_entries_per_ifd) {
            set_limit(out, ExifLimitReason::MaxEntriesPerIfd);
            return false;
        }
        if (store.entries().size() >= limits.max_total_entries) {
            set_limit(out, ExifLimitReason::MaxTotalEntries);
            return false;
        }
        if (store.resource_limit_exceeded()) {
            set_limit(out, ExifLimitReason::MaxArenaBytes);
            return false;
        }
        return true;
    }

    static bool emit_entry(MetaStore& store, BlockId block,
                           uint32_t* order_in_block, uint32_t order_base,
                           std::string_view ifd, uint16_t tag,
                           const MetaValue& value,
                           const ExifDecodeLimits& limits,
                           ExifDecodeResult* out) noexcept
    {
        if (!order_in_block || ifd.empty()
            || !can_emit(store, *order_in_block, order_base, limits, out)) {
            return false;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = *order_in_block;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0U };
        entry.origin.wire_count     = value.count;
        entry.value                 = value;

        if (store.add_entry(entry) == kInvalidEntryId) {
            if (*order_in_block - order_base >= limits.max_entries_per_ifd) {
                set_limit(out, ExifLimitReason::MaxEntriesPerIfd);
            } else if (store.entries().size() >= limits.max_total_entries) {
                set_limit(out, ExifLimitReason::MaxTotalEntries);
            } else if (store.resource_limit_exceeded()) {
                set_limit(out, ExifLimitReason::MaxArenaBytes);
            } else {
                set_limit(out, ExifLimitReason::MaxArenaBytes);
            }
            return false;
        }
        ++(*order_in_block);
        if (out) {
            ++out->entries_decoded;
        }
        return true;
    }

    static bool make_ifd_token(std::string_view table, uint32_t index,
                               std::span<char> scratch,
                               std::string_view* out) noexcept
    {
        if (!out || scratch.empty()) {
            return false;
        }
        const int n = std::snprintf(scratch.data(), scratch.size(),
                                    "mk_minoltaraw_%.*s_%u",
                                    static_cast<int>(table.size()),
                                    table.data(), static_cast<unsigned>(index));
        if (n <= 0 || static_cast<size_t>(n) >= scratch.size()) {
            return false;
        }
        *out = std::string_view(scratch.data(), static_cast<size_t>(n));
        return true;
    }

    static bool value_size_allowed(size_t bytes, const ExifDecodeLimits& limits,
                                   ExifDecodeResult* out) noexcept
    {
        if (bytes <= limits.max_value_bytes) {
            return true;
        }
        set_limit(out, ExifLimitReason::ValueCountTooLarge);
        return false;
    }

    static void emit_u8(MetaStore& store, BlockId block, uint32_t* order,
                        uint32_t order_base, std::string_view ifd, uint16_t tag,
                        std::span<const std::byte> raw, uint8_t offset,
                        bool /*le*/, const ExifDecodeLimits& limits,
                        ExifDecodeResult* out) noexcept
    {
        if (!has_range(raw, offset, 1U)
            || !value_size_allowed(1U, limits, out)) {
            return;
        }
        (void)emit_entry(store, block, order, order_base, ifd, tag,
                         make_u8(u8(raw[offset])), limits, out);
    }

    static void emit_i8(MetaStore& store, BlockId block, uint32_t* order,
                        uint32_t order_base, std::string_view ifd, uint16_t tag,
                        std::span<const std::byte> raw, uint8_t offset,
                        bool /*le*/, const ExifDecodeLimits& limits,
                        ExifDecodeResult* out) noexcept
    {
        if (!has_range(raw, offset, 1U)
            || !value_size_allowed(1U, limits, out)) {
            return;
        }
        const int16_t byte         = static_cast<int16_t>(u8(raw[offset]));
        const int16_t signed_value = byte < 0x80
                                         ? byte
                                         : static_cast<int16_t>(byte - 0x100);
        (void)emit_entry(store, block, order, order_base, ifd, tag,
                         make_i8(static_cast<int8_t>(signed_value)), limits,
                         out);
    }

    static void emit_u16be(MetaStore& store, BlockId block, uint32_t* order,
                           uint32_t order_base, std::string_view ifd,
                           uint16_t tag, std::span<const std::byte> raw,
                           uint8_t offset, bool le,
                           const ExifDecodeLimits& limits,
                           ExifDecodeResult* out) noexcept
    {
        uint16_t value = 0U;
        if (!read_word(raw, offset, le, &value)
            || !value_size_allowed(2U, limits, out)) {
            return;
        }
        (void)emit_entry(store, block, order, order_base, ifd, tag,
                         make_u16(value), limits, out);
    }

    static void emit_u8_array4(MetaStore& store, BlockId block, uint32_t* order,
                               uint32_t order_base, std::string_view ifd,
                               uint16_t tag, std::span<const std::byte> raw,
                               uint8_t offset, bool /*le*/,
                               const ExifDecodeLimits& limits,
                               ExifDecodeResult* out) noexcept
    {
        if (!has_range(raw, offset, 4U) || !value_size_allowed(4U, limits, out)
            || !can_emit(store, *order, order_base, limits, out)) {
            return;
        }
        std::array<uint8_t, 4> values {};
        for (size_t i = 0U; i < values.size(); ++i) {
            values[i] = u8(raw[static_cast<size_t>(offset) + i]);
        }
        (void)emit_entry(store, block, order, order_base, ifd, tag,
                         make_u8_array(store.arena(), values), limits, out);
    }

    static void emit_u16be_array2(MetaStore& store, BlockId block,
                                  uint32_t* order, uint32_t order_base,
                                  std::string_view ifd, uint16_t tag,
                                  std::span<const std::byte> raw,
                                  uint8_t offset, bool le,
                                  const ExifDecodeLimits& limits,
                                  ExifDecodeResult* out) noexcept
    {
        if (!has_range(raw, offset, 4U) || !value_size_allowed(4U, limits, out)
            || !can_emit(store, *order, order_base, limits, out)) {
            return;
        }
        std::array<uint16_t, 2> values {};
        if (!read_word(raw, offset, le, &values[0])
            || !read_word(raw, static_cast<uint64_t>(offset) + 2U, le,
                          &values[1])) {
            return;
        }
        (void)emit_entry(store, block, order, order_base, ifd, tag,
                         make_u16_array(store.arena(), values), limits, out);
    }

    static void emit_u32(MetaStore& store, BlockId block, uint32_t* order,
                         uint32_t order_base, std::string_view ifd,
                         uint16_t tag, std::span<const std::byte> raw,
                         uint8_t offset, bool le,
                         const ExifDecodeLimits& limits,
                         ExifDecodeResult* out) noexcept
    {
        uint32_t value = 0U;
        if (!read_dword(raw, offset, le, &value)
            || !value_size_allowed(4U, limits, out)) {
            return;
        }
        (void)emit_entry(store, block, order, order_base, ifd, tag,
                         make_u32(value), limits, out);
    }

    static void decode_prd(std::span<const std::byte> raw, MetaStore& store,
                           BlockId block, uint32_t* order, uint32_t order_base,
                           std::string_view ifd, bool le,
                           const ExifDecodeLimits& limits,
                           ExifDecodeResult* out) noexcept
    {
        if (has_range(raw, 0U, 8U) && value_size_allowed(8U, limits, out)
            && can_emit(store, *order, order_base, limits, out)) {
            const std::string_view text(reinterpret_cast<const char*>(
                                            raw.data()),
                                        8U);
            (void)emit_entry(store, block, order, order_base, ifd, 0x0000U,
                             make_text(store.arena(), text, TextEncoding::Ascii),
                             limits, out);
        }

        static constexpr uint8_t kWordOffsets[] = { 8U, 10U, 12U, 14U };
        static constexpr uint16_t kWordTags[]   = { 0x0008U, 0x000aU, 0x000cU,
                                                    0x000eU };
        for (size_t i = 0U; i < std::size(kWordOffsets); ++i) {
            emit_u16be(store, block, order, order_base, ifd, kWordTags[i], raw,
                       kWordOffsets[i], le, limits, out);
        }
        for (size_t i = 0U; i < std::size(kPrdByteFields); ++i) {
            emit_u8(store, block, order, order_base, ifd, kPrdByteFields[i].tag,
                    raw, kPrdByteFields[i].offset, le, limits, out);
        }
    }

    static void decode_wbg(std::span<const std::byte> raw, MetaStore& store,
                           BlockId block, uint32_t* order, uint32_t order_base,
                           std::string_view ifd, bool le,
                           const ExifDecodeLimits& limits,
                           ExifDecodeResult* out) noexcept
    {
        emit_u8_array4(store, block, order, order_base, ifd, 0x0000U, raw, 0U,
                       le, limits, out);
        if (!has_range(raw, 4U, 8U) || !value_size_allowed(8U, limits, out)
            || !can_emit(store, *order, order_base, limits, out)) {
            return;
        }
        std::array<uint16_t, 4> values {};
        for (size_t i = 0U; i < values.size(); ++i) {
            if (!read_word(raw, 4U + (i * 2U), le, &values[i])) {
                return;
            }
        }
        (void)emit_entry(store, block, order, order_base, ifd, 0x0004U,
                         make_u16_array(store.arena(), values), limits, out);
    }

    static void decode_rif(std::span<const std::byte> raw, MetaStore& store,
                           BlockId block, uint32_t* order, uint32_t order_base,
                           std::string_view ifd, bool le,
                           const RifContext& context,
                           const ExifDecodeLimits& limits,
                           ExifDecodeResult* out) noexcept
    {
        for (size_t i = 0U; i < std::size(kRifSignedByteFields); ++i) {
            emit_i8(store, block, order, order_base, ifd,
                    kRifSignedByteFields[i].tag, raw,
                    kRifSignedByteFields[i].offset, le, limits, out);
        }
        for (size_t i = 0U; i < std::size(kRifUnsignedByteFields); ++i) {
            emit_u8(store, block, order, order_base, ifd,
                    kRifUnsignedByteFields[i].tag, raw,
                    kRifUnsignedByteFields[i].offset, le, limits, out);
        }
        if (context.non_sony_make || context.a100_model) {
            emit_u8(store, block, order, order_base, ifd, 0x0007U, raw, 7U, le,
                    limits, out);
        }
        if (context.a100_model || context.prd_present) {
            for (size_t i = 0U; i < std::size(kRifWordPairFields); ++i) {
                emit_u16be_array2(store, block, order, order_base, ifd,
                                  kRifWordPairFields[i].tag, raw,
                                  kRifWordPairFields[i].offset, le, limits,
                                  out);
            }
        }
        if (context.a100_model) {
            for (size_t i = 0U; i < std::size(kRifA100WordPairFields); ++i) {
                emit_u16be_array2(store, block, order, order_base, ifd,
                                  kRifA100WordPairFields[i].tag, raw,
                                  kRifA100WordPairFields[i].offset, le, limits,
                                  out);
            }
            emit_u8(store, block, order, order_base, ifd, 0x004cU, raw, 76U, le,
                    limits, out);
            emit_u8(store, block, order, order_base, ifd, 0x004dU, raw, 77U, le,
                    limits, out);
            emit_u32(store, block, order, order_base, ifd, 0x0050U, raw, 80U,
                     le, limits, out);
        } else if (context.a200_or_a700_model) {
            emit_u8(store, block, order, order_base, ifd, 0x004eU, raw, 78U, le,
                    limits, out);
            emit_u8(store, block, order, order_base, ifd, 0x004fU, raw, 79U, le,
                    limits, out);
        }
        if (context.non_sony_make) {
            emit_i8(store, block, order, order_base, ifd, 0x0038U, raw, 56U, le,
                    limits, out);
            emit_u8(store, block, order, order_base, ifd, 0x003aU, raw, 58U, le,
                    limits, out);
            emit_u8(store, block, order, order_base, ifd, 0x003cU, raw, 60U, le,
                    limits, out);
        }
        if (context.sony_make) {
            emit_u8(store, block, order, order_base, ifd, 0x004aU, raw, 74U, le,
                    limits, out);
        }
    }

    static void decode_segment(std::string_view table,
                               std::span<const std::byte> raw, uint32_t index,
                               MetaStore& store, BlockId block, uint32_t* order,
                               bool le, const RifContext& rif_context,
                               const ExifDecodeLimits& limits,
                               ExifDecodeResult* out) noexcept
    {
        char ifd_buffer[48];
        std::string_view ifd;
        if (!make_ifd_token(table, index, std::span<char>(ifd_buffer), &ifd)) {
            set_limit(out, ExifLimitReason::MaxTotalEntries);
            return;
        }
        const uint32_t order_base = *order;
        if (table == "prd") {
            decode_prd(raw, store, block, order, order_base, ifd, le, limits,
                       out);
        } else if (table == "wbg") {
            decode_wbg(raw, store, block, order, order_base, ifd, le, limits,
                       out);
        } else if (table == "rif") {
            decode_rif(raw, store, block, order, order_base, ifd, le,
                       rif_context, limits, out);
        }
    }

}  // namespace

bool
looks_like_mrw(std::span<const std::byte> file_bytes) noexcept
{
    return matches(file_bytes, 0U, "\0MRM", 4U)
           || matches(file_bytes, 0U, "\0MRI", 4U);
}

ExifDecodeResult
decode_mrw_native(std::span<const std::byte> file_bytes, MetaStore& store,
                  const ExifDecodeLimits& limits) noexcept
{
    ExifDecodeResult out;
    out.status = ExifDecodeStatus::Ok;
    if (!looks_like_mrw(file_bytes)) {
        out.status = ExifDecodeStatus::Unsupported;
        return out;
    }
    if (file_bytes.size() < 8U) {
        out.status = ExifDecodeStatus::Malformed;
        return out;
    }

    const bool le = matches(file_bytes, 0U, "\0MRI", 4U);
    std::array<char, 64> make_scratch {};
    std::array<char, 64> model_scratch {};
    const std::string_view make  = snapshot_ifd0_text(store, 0x010fU,
                                                      make_scratch);
    const std::string_view model = snapshot_ifd0_text(store, 0x0110U,
                                                      model_scratch);
    const bool standalone_mrm    = !le;
    const bool sony_make         = make.starts_with("SONY");
    const bool non_sony_make     = make.empty() ? standalone_mrm : !sony_make;
    RifContext rif_context;
    rif_context.non_sony_make      = non_sony_make;
    rif_context.sony_make          = sony_make;
    rif_context.a100_model         = model == "DSLR-A100";
    rif_context.a200_or_a700_model = model == "DSLR-A200"
                                     || model == "DSLR-A700";
    uint32_t metadata_size = 0U;
    if (!read_dword(file_bytes, 4U, le, &metadata_size)
        || metadata_size > file_bytes.size() - 8U) {
        out.status = ExifDecodeStatus::Malformed;
        return out;
    }
    const uint64_t metadata_end = 8U + static_cast<uint64_t>(metadata_size);
    rif_context.prd_present
        = standalone_mrm && contains_prd_segment(file_bytes, metadata_end, le);
    store.constrain_resources(limits.max_total_entries, limits.max_arena_bytes);
    if (store.resource_limit_exceeded()) {
        set_limit(&out, ExifLimitReason::MaxArenaBytes);
        return out;
    }
    uint32_t prd_index    = 0U;
    uint32_t wbg_index    = 0U;
    uint32_t rif_index    = 0U;
    uint32_t known_blocks = 0U;
    uint64_t offset       = 8U;
    while (offset < metadata_end) {
        if (metadata_end - offset < 8U) {
            update_status(&out, ExifDecodeStatus::Malformed);
            break;
        }
        uint32_t segment_size = 0U;
        if (!read_dword(file_bytes, offset + 4U, le, &segment_size)) {
            update_status(&out, ExifDecodeStatus::Malformed);
            break;
        }
        const uint64_t payload_offset = offset + 8U;
        if (segment_size > metadata_end - payload_offset) {
            update_status(&out, ExifDecodeStatus::Malformed);
            break;
        }
        const std::span<const std::byte> segment
            = file_bytes.subspan(static_cast<size_t>(payload_offset),
                                 static_cast<size_t>(segment_size));

        const bool known_segment = matches(file_bytes, offset, "\0PRD", 4U)
                                   || matches(file_bytes, offset, "\0WBG", 4U)
                                   || matches(file_bytes, offset, "\0RIF", 4U);
        BlockId block  = kInvalidBlockId;
        uint32_t order = 0U;
        if (known_segment) {
            if (known_blocks >= limits.max_ifds) {
                set_limit(&out, ExifLimitReason::MaxIfds);
                break;
            }
            ++known_blocks;
            block = store.add_block(BlockInfo {});
            if (block == kInvalidBlockId) {
                set_limit(&out, ExifLimitReason::MaxArenaBytes);
                break;
            }
        }
        if (matches(file_bytes, offset, "\0PRD", 4U)) {
            decode_segment("prd", segment, prd_index++, store, block, &order,
                           le, rif_context, limits, &out);
        } else if (matches(file_bytes, offset, "\0WBG", 4U)) {
            decode_segment("wbg", segment, wbg_index++, store, block, &order,
                           le, rif_context, limits, &out);
        } else if (matches(file_bytes, offset, "\0RIF", 4U)) {
            decode_segment("rif", segment, rif_index++, store, block, &order,
                           le, rif_context, limits, &out);
        }

        if (out.status == ExifDecodeStatus::LimitExceeded) {
            break;
        }
        offset = payload_offset + static_cast<uint64_t>(segment_size);
    }

    if (out.status == ExifDecodeStatus::Ok && out.entries_decoded == 0U) {
        out.status = ExifDecodeStatus::Unsupported;
    }
    return out;
}

}  // namespace openmeta::mrw_internal
