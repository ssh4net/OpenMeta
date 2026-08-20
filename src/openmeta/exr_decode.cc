// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exr_decode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static constexpr uint32_t kExrMagic            = 20000630U;
    static constexpr uint32_t kExrVersionMask      = 0x000000FFU;
    static constexpr uint32_t kExrSupportedVersion = 2U;
    static constexpr uint32_t kExrTiledFlag        = 0x00000200U;
    static constexpr uint32_t kExrLongNamesFlag    = 0x00000400U;
    static constexpr uint32_t kExrNonImageFlag     = 0x00000800U;
    static constexpr uint32_t kExrMultipartFlag    = 0x00001000U;
    static constexpr uint32_t kExrValidFlags = kExrTiledFlag | kExrLongNamesFlag
                                               | kExrNonImageFlag
                                               | kExrMultipartFlag;

    static constexpr uint16_t kExrAttrOpaque = 31U;

    struct ExrTypeCodeMap final {
        std::string_view name;
        uint16_t code = 0;
    };

    static constexpr std::array<ExrTypeCodeMap, 30> kExrTypeCodeMap = {
        ExrTypeCodeMap { "box2i", 1U },
        ExrTypeCodeMap { "box2f", 2U },
        ExrTypeCodeMap { "bytes", 3U },
        ExrTypeCodeMap { "chlist", 4U },
        ExrTypeCodeMap { "chromaticities", 5U },
        ExrTypeCodeMap { "compression", 6U },
        ExrTypeCodeMap { "double", 7U },
        ExrTypeCodeMap { "envmap", 8U },
        ExrTypeCodeMap { "float", 9U },
        ExrTypeCodeMap { "floatvector", 10U },
        ExrTypeCodeMap { "int", 11U },
        ExrTypeCodeMap { "keycode", 12U },
        ExrTypeCodeMap { "lineOrder", 13U },
        ExrTypeCodeMap { "m33f", 14U },
        ExrTypeCodeMap { "m33d", 15U },
        ExrTypeCodeMap { "m44f", 16U },
        ExrTypeCodeMap { "m44d", 17U },
        ExrTypeCodeMap { "preview", 18U },
        ExrTypeCodeMap { "rational", 19U },
        ExrTypeCodeMap { "string", 20U },
        ExrTypeCodeMap { "stringvector", 21U },
        ExrTypeCodeMap { "tiledesc", 22U },
        ExrTypeCodeMap { "timecode", 23U },
        ExrTypeCodeMap { "v2i", 24U },
        ExrTypeCodeMap { "v2f", 25U },
        ExrTypeCodeMap { "v2d", 26U },
        ExrTypeCodeMap { "v3i", 27U },
        ExrTypeCodeMap { "v3f", 28U },
        ExrTypeCodeMap { "v3d", 29U },
        ExrTypeCodeMap { "deepImageState", 30U },
    };

    enum class ParseStringStatus : uint8_t {
        Ok,
        Malformed,
        LimitExceeded,
    };

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }


    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4U > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 8;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 16;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 24;
        *out = v;
        return true;
    }


    static bool read_u64le(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (!out || offset + 8U > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(u8(bytes[offset + i])) << (8U * i);
        }
        *out = v;
        return true;
    }


    static bool read_i32le(std::span<const std::byte> bytes, uint64_t offset,
                           int32_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint32_t v = 0;
        if (!read_u32le(bytes, offset, &v)) {
            return false;
        }
        *out = static_cast<int32_t>(v);
        return true;
    }


    static ParseStringStatus
    read_cstr_with_first(std::span<const std::byte> bytes, uint64_t* io_offset,
                         uint8_t first, uint32_t max_bytes,
                         std::string* out) noexcept
    {
        if (!io_offset || !out || first == 0U) {
            return ParseStringStatus::Malformed;
        }

        out->clear();
        out->push_back(static_cast<char>(first));
        if (max_bytes != 0U && out->size() > max_bytes) {
            return ParseStringStatus::LimitExceeded;
        }

        while (true) {
            if (*io_offset >= bytes.size()) {
                return ParseStringStatus::Malformed;
            }
            const uint8_t b = u8(bytes[*io_offset]);
            *io_offset += 1;
            if (b == 0U) {
                break;
            }
            out->push_back(static_cast<char>(b));
            if (max_bytes != 0U && out->size() > max_bytes) {
                return ParseStringStatus::LimitExceeded;
            }
        }

        return ParseStringStatus::Ok;
    }


    static ParseStringStatus read_cstr(std::span<const std::byte> bytes,
                                       uint64_t* io_offset, uint32_t max_bytes,
                                       std::string* out) noexcept
    {
        if (!io_offset || *io_offset >= bytes.size()) {
            return ParseStringStatus::Malformed;
        }
        const uint8_t first = u8(bytes[*io_offset]);
        *io_offset += 1;
        return read_cstr_with_first(bytes, io_offset, first, max_bytes, out);
    }


    static bool has_nul(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == std::byte { 0 }) {
                return true;
            }
        }
        return false;
    }


    static bool bytes_ascii(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint8_t c = u8(bytes[i]);
            if (c > 0x7FU) {
                return false;
            }
        }
        return true;
    }


    static bool bytes_valid_utf8(std::span<const std::byte> bytes) noexcept
    {
        size_t i = 0;
        while (i < bytes.size()) {
            const uint8_t c0 = u8(bytes[i]);
            if ((c0 & 0x80U) == 0U) {
                i += 1;
                continue;
            }

            uint32_t needed = 0;
            uint32_t min_cp = 0;
            uint32_t cp     = 0;

            if ((c0 & 0xE0U) == 0xC0U) {
                needed = 1;
                min_cp = 0x80U;
                cp     = c0 & 0x1FU;
            } else if ((c0 & 0xF0U) == 0xE0U) {
                needed = 2;
                min_cp = 0x800U;
                cp     = c0 & 0x0FU;
            } else if ((c0 & 0xF8U) == 0xF0U) {
                needed = 3;
                min_cp = 0x10000U;
                cp     = c0 & 0x07U;
            } else {
                return false;
            }

            if (i + needed >= bytes.size()) {
                return false;
            }
            for (uint32_t j = 0; j < needed; ++j) {
                const uint8_t cx = u8(bytes[i + 1U + j]);
                if ((cx & 0xC0U) != 0x80U) {
                    return false;
                }
                cp = (cp << 6) | static_cast<uint32_t>(cx & 0x3FU);
            }

            if (cp < min_cp || cp > 0x10FFFFU) {
                return false;
            }
            if (cp >= 0xD800U && cp <= 0xDFFFU) {
                return false;
            }
            i += 1U + needed;
        }
        return true;
    }


    static TextEncoding classify_text(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.empty()) {
            return TextEncoding::Utf8;
        }
        if (bytes_ascii(bytes)) {
            return TextEncoding::Ascii;
        }
        if (bytes_valid_utf8(bytes)) {
            return TextEncoding::Utf8;
        }
        return TextEncoding::Unknown;
    }


    static uint16_t exr_type_code(std::string_view type_name) noexcept
    {
        for (size_t i = 0; i < kExrTypeCodeMap.size(); ++i) {
            if (kExrTypeCodeMap[i].name == type_name) {
                return kExrTypeCodeMap[i].code;
            }
        }
        return kExrAttrOpaque;
    }


    template<size_t N>
    static bool decode_i32_fixed(std::span<const std::byte> value_bytes,
                                 std::array<int32_t, N>* out) noexcept
    {
        if (!out || value_bytes.size() != (N * sizeof(int32_t))) {
            return false;
        }
        for (size_t i = 0; i < N; ++i) {
            int32_t v = 0;
            if (!read_i32le(value_bytes, static_cast<uint64_t>(i * 4U), &v)) {
                return false;
            }
            (*out)[i] = v;
        }
        return true;
    }


    template<size_t N>
    static bool decode_u32_fixed(std::span<const std::byte> value_bytes,
                                 std::array<uint32_t, N>* out) noexcept
    {
        if (!out || value_bytes.size() != (N * sizeof(uint32_t))) {
            return false;
        }
        for (size_t i = 0; i < N; ++i) {
            uint32_t v = 0;
            if (!read_u32le(value_bytes, static_cast<uint64_t>(i * 4U), &v)) {
                return false;
            }
            (*out)[i] = v;
        }
        return true;
    }


    static MetaValue decode_exr_value(std::string_view type_name,
                                      std::span<const std::byte> value_bytes,
                                      MetaStore& store,
                                      bool decode_known_types) noexcept
    {
        if (!decode_known_types) {
            return make_bytes(store.arena(), value_bytes);
        }

        if (type_name == "int" && value_bytes.size() == 4U) {
            int32_t v = 0;
            if (read_i32le(value_bytes, 0, &v)) {
                return make_i32(v);
            }
        }

        if (type_name == "float" && value_bytes.size() == 4U) {
            uint32_t bits = 0;
            if (read_u32le(value_bytes, 0, &bits)) {
                return make_f32_bits(bits);
            }
        }

        if (type_name == "double" && value_bytes.size() == 8U) {
            uint64_t bits = 0;
            if (read_u64le(value_bytes, 0, &bits)) {
                return make_f64_bits(bits);
            }
        }

        if ((type_name == "compression" || type_name == "envmap"
             || type_name == "lineOrder" || type_name == "deepImageState")
            && value_bytes.size() == 1U) {
            return make_u8(u8(value_bytes[0]));
        }

        if (type_name == "string" && !has_nul(value_bytes)) {
            const std::string_view s(reinterpret_cast<const char*>(
                                         value_bytes.data()),
                                     value_bytes.size());
            return make_text(store.arena(), s, classify_text(value_bytes));
        }

        if (type_name == "rational" && value_bytes.size() == 8U) {
            int32_t numer  = 0;
            uint32_t denom = 0;
            if (read_i32le(value_bytes, 0, &numer)
                && read_u32le(value_bytes, 4, &denom)
                && denom <= static_cast<uint32_t>(
                       std::numeric_limits<int32_t>::max())) {
                return make_srational(numer, static_cast<int32_t>(denom));
            }
        }

        if (type_name == "floatvector" && (value_bytes.size() % 4U) == 0U) {
            const size_t n = value_bytes.size() / 4U;
            std::vector<uint32_t> values;
            values.resize(n);
            bool ok = true;
            for (size_t i = 0; i < n; ++i) {
                ok = read_u32le(value_bytes, static_cast<uint64_t>(i * 4U),
                                &values[i]);
                if (!ok) {
                    break;
                }
            }
            if (ok) {
                return make_f32_bits_array(
                    store.arena(),
                    std::span<const uint32_t>(values.data(), values.size()));
            }
        }

        if (type_name == "box2i") {
            std::array<int32_t, 4> v {};
            if (decode_i32_fixed(value_bytes, &v)) {
                return make_i32_array(store.arena(),
                                      std::span<const int32_t>(v.data(),
                                                               v.size()));
            }
        }

        if (type_name == "box2f" || type_name == "v2f" || type_name == "v3f"
            || type_name == "m33f" || type_name == "m44f"
            || type_name == "chromaticities") {
            const size_t n = value_bytes.size() / 4U;
            if ((value_bytes.size() % 4U) == 0U) {
                std::vector<uint32_t> bits;
                bits.resize(n);
                bool ok = true;
                for (size_t i = 0; i < n; ++i) {
                    ok = read_u32le(value_bytes, static_cast<uint64_t>(i * 4U),
                                    &bits[i]);
                    if (!ok) {
                        break;
                    }
                }
                if (ok) {
                    return make_f32_bits_array(
                        store.arena(),
                        std::span<const uint32_t>(bits.data(), bits.size()));
                }
            }
        }

        if (type_name == "v2i") {
            std::array<int32_t, 2> v {};
            if (decode_i32_fixed(value_bytes, &v)) {
                return make_i32_array(store.arena(),
                                      std::span<const int32_t>(v.data(),
                                                               v.size()));
            }
        }

        if (type_name == "v3i") {
            std::array<int32_t, 3> v {};
            if (decode_i32_fixed(value_bytes, &v)) {
                return make_i32_array(store.arena(),
                                      std::span<const int32_t>(v.data(),
                                                               v.size()));
            }
        }

        if (type_name == "timecode") {
            std::array<uint32_t, 2> v {};
            if (decode_u32_fixed(value_bytes, &v)) {
                return make_u32_array(store.arena(),
                                      std::span<const uint32_t>(v.data(),
                                                                v.size()));
            }
        }

        if (type_name == "v2d" || type_name == "v3d" || type_name == "m33d"
            || type_name == "m44d") {
            const size_t n = value_bytes.size() / 8U;
            if ((value_bytes.size() % 8U) == 0U) {
                std::vector<uint64_t> bits;
                bits.resize(n);
                bool ok = true;
                for (size_t i = 0; i < n; ++i) {
                    ok = read_u64le(value_bytes, static_cast<uint64_t>(i * 8U),
                                    &bits[i]);
                    if (!ok) {
                        break;
                    }
                }
                if (ok) {
                    return make_f64_bits_array(
                        store.arena(),
                        std::span<const uint64_t>(bits.data(), bits.size()));
                }
            }
        }

        if (type_name == "keycode") {
            std::array<int32_t, 7> v {};
            if (decode_i32_fixed(value_bytes, &v)) {
                return make_i32_array(store.arena(),
                                      std::span<const int32_t>(v.data(),
                                                               v.size()));
            }
        }

        if (type_name == "tiledesc") {
            std::array<uint32_t, 2> base {};
            if (value_bytes.size() == 9U
                && decode_u32_fixed(value_bytes.first<8>(), &base)) {
                std::array<uint32_t, 3> v {
                    base[0], base[1], static_cast<uint32_t>(u8(value_bytes[8]))
                };
                return make_u32_array(store.arena(),
                                      std::span<const uint32_t>(v.data(),
                                                                v.size()));
            }
        }

        return make_bytes(store.arena(), value_bytes);
    }


    static ExrDecodeStatus parse_attribute_with_first(
        std::span<const std::byte> bytes, uint64_t* io_offset,
        uint8_t first_name_char, uint32_t part_index, BlockId block,
        uint32_t* io_order_in_block, uint32_t* io_part_attr_count,
        uint32_t* io_total_attr_count, uint64_t* io_total_attr_bytes,
        MetaStore& store, EntryFlags flags, const ExrDecodeOptions& options,
        ExrDecodeResult* out_result) noexcept
    {
        if (!io_offset || !io_order_in_block || !io_part_attr_count
            || !io_total_attr_count || !io_total_attr_bytes || !out_result) {
            return ExrDecodeStatus::Malformed;
        }

        if (options.limits.max_attributes_per_part != 0U
            && *io_part_attr_count >= options.limits.max_attributes_per_part) {
            return ExrDecodeStatus::LimitExceeded;
        }
        if (options.limits.max_attributes != 0U
            && *io_total_attr_count >= options.limits.max_attributes) {
            return ExrDecodeStatus::LimitExceeded;
        }

        std::string name;
        const ParseStringStatus name_status
            = read_cstr_with_first(bytes, io_offset, first_name_char,
                                   options.limits.max_name_bytes, &name);
        if (name_status == ParseStringStatus::Malformed) {
            return ExrDecodeStatus::Malformed;
        }
        if (name_status == ParseStringStatus::LimitExceeded) {
            return ExrDecodeStatus::LimitExceeded;
        }

        std::string type_name;
        const ParseStringStatus type_status
            = read_cstr(bytes, io_offset, options.limits.max_type_name_bytes,
                        &type_name);
        if (type_status == ParseStringStatus::Malformed) {
            return ExrDecodeStatus::Malformed;
        }
        if (type_status == ParseStringStatus::LimitExceeded) {
            return ExrDecodeStatus::LimitExceeded;
        }

        uint32_t attribute_size = 0;
        if (!read_u32le(bytes, *io_offset, &attribute_size)) {
            return ExrDecodeStatus::Malformed;
        }
        *io_offset += 4U;

        if (options.limits.max_attribute_bytes != 0U
            && attribute_size > options.limits.max_attribute_bytes) {
            return ExrDecodeStatus::LimitExceeded;
        }

        if (*io_offset + static_cast<uint64_t>(attribute_size) > bytes.size()) {
            return ExrDecodeStatus::Malformed;
        }

        const uint64_t next_total = *io_total_attr_bytes
                                    + static_cast<uint64_t>(attribute_size);
        if (next_total < *io_total_attr_bytes) {
            return ExrDecodeStatus::LimitExceeded;
        }
        if (options.limits.max_total_attribute_bytes != 0U
            && next_total > options.limits.max_total_attribute_bytes) {
            return ExrDecodeStatus::LimitExceeded;
        }

        const std::span<const std::byte> value_bytes
            = bytes.subspan(static_cast<size_t>(*io_offset),
                            static_cast<size_t>(attribute_size));
        *io_offset += static_cast<uint64_t>(attribute_size);

        Entry entry;
        entry.key   = make_exr_attribute_key(store.arena(), part_index, name);
        entry.value = decode_exr_value(type_name, value_bytes, store,
                                       options.decode_known_types);
        entry.origin.block            = block;
        entry.origin.order_in_block   = *io_order_in_block;
        entry.origin.wire_type.family = WireFamily::Other;
        const uint16_t wire_code      = exr_type_code(type_name);
        entry.origin.wire_type.code   = wire_code;
        entry.origin.wire_count       = attribute_size;
        if (options.preserve_unknown_type_name && wire_code == kExrAttrOpaque) {
            entry.origin.wire_type_name = store.arena().append_string(
                type_name);
        }
        entry.flags = flags;
        store.add_entry(entry);

        *io_order_in_block += 1U;
        *io_part_attr_count += 1U;
        *io_total_attr_count += 1U;
        *io_total_attr_bytes = next_total;
        out_result->entries_decoded += 1U;
        return ExrDecodeStatus::Ok;
    }

}  // namespace


ExrDecodeResult
decode_exr_header(std::span<const std::byte> exr_bytes, MetaStore& store,
                  EntryFlags flags, const ExrDecodeOptions& options) noexcept
{
    ExrDecodeResult result;
    result.status = ExrDecodeStatus::Unsupported;

    if (exr_bytes.size() < 8U) {
        return result;
    }

    uint32_t magic             = 0;
    uint32_t version_and_flags = 0;
    if (!read_u32le(exr_bytes, 0, &magic)
        || !read_u32le(exr_bytes, 4, &version_and_flags)) {
        return result;
    }
    if (magic != kExrMagic) {
        return result;
    }

    const uint32_t version = version_and_flags & kExrVersionMask;
    if (version != kExrSupportedVersion) {
        return result;
    }

    const uint32_t flags_only = version_and_flags & ~kExrVersionMask;
    if ((flags_only & ~kExrValidFlags) != 0U) {
        result.status = ExrDecodeStatus::Malformed;
        return result;
    }

    if (options.limits.max_parts == 0U) {
        result.status = ExrDecodeStatus::LimitExceeded;
        return result;
    }

    result.status = ExrDecodeStatus::Ok;

    const bool multipart = (flags_only & kExrMultipartFlag) != 0U;

    uint64_t offset           = 8U;
    uint32_t part_index       = 0U;
    uint32_t order_in_block   = 0U;
    uint32_t part_attr_count  = 0U;
    uint32_t total_attr_count = 0U;
    uint64_t total_attr_bytes = 0U;

    BlockId part_block   = store.add_block(BlockInfo {});
    result.parts_decoded = 1U;

    while (true) {
        if (offset >= exr_bytes.size()) {
            result.status = ExrDecodeStatus::Malformed;
            return result;
        }

        const uint8_t first = u8(exr_bytes[offset]);
        offset += 1U;

        if (first == 0U) {
            if (!multipart) {
                return result;
            }

            if (offset >= exr_bytes.size()) {
                result.status = ExrDecodeStatus::Malformed;
                return result;
            }

            const uint8_t next = u8(exr_bytes[offset]);
            offset += 1U;
            if (next == 0U) {
                return result;
            }

            part_index += 1U;
            if (part_index >= options.limits.max_parts) {
                result.status = ExrDecodeStatus::LimitExceeded;
                return result;
            }

            part_block           = store.add_block(BlockInfo {});
            result.parts_decoded = part_index + 1U;
            order_in_block       = 0U;
            part_attr_count      = 0U;

            const ExrDecodeStatus st = parse_attribute_with_first(
                exr_bytes, &offset, next, part_index, part_block,
                &order_in_block, &part_attr_count, &total_attr_count,
                &total_attr_bytes, store, flags, options, &result);
            if (st != ExrDecodeStatus::Ok) {
                result.status = st;
                return result;
            }
            continue;
        }

        const ExrDecodeStatus st = parse_attribute_with_first(
            exr_bytes, &offset, first, part_index, part_block, &order_in_block,
            &part_attr_count, &total_attr_count, &total_attr_bytes, store,
            flags, options, &result);
        if (st != ExrDecodeStatus::Ok) {
            result.status = st;
            return result;
        }
    }
}

ExrDecodeResult
measure_exr_header(std::span<const std::byte> exr_bytes,
                   const ExrDecodeOptions& options) noexcept
{
    MetaStore scratch;
    return decode_exr_header(exr_bytes, scratch, EntryFlags::None, options);
}

namespace {

    struct ExrSourceReader final {
        const RandomAccessSourceRange* range = nullptr;
        RandomAccessReadWindow window;
        ExrRandomAccessDecodeResult* result = nullptr;
        RandomAccessReadLimits limits;
        RandomAccessReadWindowOptions window_options;
        std::span<std::byte> value_scratch;
    };


    static bool source_view(ExrSourceReader* source, uint64_t offset,
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
            source->result->decode.status = ExrDecodeStatus::Malformed;
            return false;
        }
        *out = view.bytes;
        return true;
    }


    static bool source_u8(ExrSourceReader* source, uint64_t offset,
                          uint8_t* out) noexcept
    {
        std::span<const std::byte> bytes;
        if (!out || !source_view(source, offset, 1U, &bytes)) {
            return false;
        }
        *out = u8(bytes[0]);
        return true;
    }


    static bool source_u32le(ExrSourceReader* source, uint64_t offset,
                             uint32_t* out) noexcept
    {
        std::span<const std::byte> bytes;
        return source_view(source, offset, 4U, &bytes)
               && read_u32le(bytes, 0U, out);
    }


    static ParseStringStatus source_cstr_with_first(ExrSourceReader* source,
                                                    uint64_t* io_offset,
                                                    uint8_t first,
                                                    uint32_t max_bytes,
                                                    std::string* out) noexcept
    {
        if (!source || !source->range || !io_offset || !out || first == 0U) {
            return ParseStringStatus::Malformed;
        }
        out->clear();
        out->push_back(static_cast<char>(first));
        if (max_bytes != 0U && out->size() > max_bytes) {
            return ParseStringStatus::LimitExceeded;
        }
        while (*io_offset < source->range->size) {
            uint8_t b = 0U;
            if (!source_u8(source, *io_offset, &b)) {
                return ParseStringStatus::Malformed;
            }
            *io_offset += 1U;
            if (b == 0U) {
                return ParseStringStatus::Ok;
            }
            out->push_back(static_cast<char>(b));
            if (max_bytes != 0U && out->size() > max_bytes) {
                return ParseStringStatus::LimitExceeded;
            }
        }
        return ParseStringStatus::Malformed;
    }


    static ParseStringStatus source_cstr(ExrSourceReader* source,
                                         uint64_t* io_offset,
                                         uint32_t max_bytes,
                                         std::string* out) noexcept
    {
        uint8_t first = 0U;
        if (!io_offset || !source_u8(source, *io_offset, &first)) {
            return ParseStringStatus::Malformed;
        }
        *io_offset += 1U;
        return source_cstr_with_first(source, io_offset, first, max_bytes, out);
    }


    static bool source_value(ExrSourceReader* source, uint64_t offset,
                             uint64_t size,
                             std::span<const std::byte>* out) noexcept
    {
        if (!source || !source->result || !out) {
            return false;
        }
        if (size <= source->window.storage.size()) {
            return source_view(source, offset, size, out);
        }
        if (size > source->value_scratch.size()) {
            source->result->value_scratch_needed
                = (size > source->result->value_scratch_needed)
                      ? size
                      : source->result->value_scratch_needed;
            source->result->decode.status = ExrDecodeStatus::OutputTruncated;
            return false;
        }
        const std::span<std::byte> destination = source->value_scratch.first(
            static_cast<size_t>(size));
        if (random_access_read_exact(*source->range, offset, destination,
                                     &source->result->input, source->limits)
            != RandomAccessReadCode::Ok) {
            source->result->decode.status = ExrDecodeStatus::Malformed;
            return false;
        }
        *out = destination;
        return true;
    }


    static ExrRandomAccessDecodeResult
    decode_exr_source(const RandomAccessSourceRange& exr, MetaStore* store,
                      const ExrRandomAccessScratch& scratch, EntryFlags flags,
                      const ExrDecodeOptions& options,
                      const RandomAccessReadLimits& read_limits,
                      bool measure) noexcept
    {
        ExrRandomAccessDecodeResult result;
        result.decode.status = ExrDecodeStatus::Unsupported;
        if (!random_access_source_range_valid(exr) || (!measure && !store)) {
            result.input.code    = RandomAccessReadCode::InvalidArgument;
            result.decode.status = ExrDecodeStatus::Malformed;
            return result;
        }
        if (exr.source.contiguous_data != nullptr) {
            const std::span<const std::byte> bytes(
                exr.source.contiguous_data
                    + static_cast<size_t>(exr.source_offset),
                static_cast<size_t>(exr.size));
            if (measure) {
                result.decode = measure_exr_header(bytes, options);
            } else {
                result.decode = decode_exr_header(bytes, *store, flags,
                                                  options);
            }
            return result;
        }
        if (scratch.read_window.size() < 16U) {
            result.input.code = RandomAccessReadCode::ScratchTooSmall;
            result.input.failure_request_bytes = 16U;
            result.decode.status = ExrDecodeStatus::OutputTruncated;
            return result;
        }

        ExrSourceReader source;
        source.range          = &exr;
        source.window.storage = scratch.read_window;
        source.result         = &result;
        source.limits         = read_limits;
        source.window_options = scratch.window_options;
        source.value_scratch  = scratch.value;

        uint32_t magic             = 0U;
        uint32_t version_and_flags = 0U;
        if (exr.size < 8U || !source_u32le(&source, 0U, &magic)
            || !source_u32le(&source, 4U, &version_and_flags)) {
            return result;
        }
        if (magic != kExrMagic
            || (version_and_flags & kExrVersionMask) != kExrSupportedVersion) {
            return result;
        }
        const uint32_t flags_only = version_and_flags & ~kExrVersionMask;
        if ((flags_only & ~kExrValidFlags) != 0U) {
            result.decode.status = ExrDecodeStatus::Malformed;
            return result;
        }
        if (options.limits.max_parts == 0U) {
            result.decode.status = ExrDecodeStatus::LimitExceeded;
            return result;
        }

        result.decode.status        = ExrDecodeStatus::Ok;
        const bool multipart        = (flags_only & kExrMultipartFlag) != 0U;
        uint64_t offset             = 8U;
        uint32_t part_index         = 0U;
        uint32_t order_in_block     = 0U;
        uint32_t part_attr_count    = 0U;
        uint32_t total_attr_count   = 0U;
        uint64_t total_attr_bytes   = 0U;
        BlockId part_block          = measure ? kInvalidBlockId
                                              : store->add_block(BlockInfo {});
        result.decode.parts_decoded = 1U;

        while (offset < exr.size && result.input.ok()) {
            uint8_t first = 0U;
            if (!source_u8(&source, offset, &first)) {
                return result;
            }
            offset += 1U;
            if (first == 0U) {
                if (!multipart) {
                    return result;
                }
                uint8_t next = 0U;
                if (offset >= exr.size || !source_u8(&source, offset, &next)) {
                    result.decode.status = ExrDecodeStatus::Malformed;
                    return result;
                }
                offset += 1U;
                if (next == 0U) {
                    return result;
                }
                part_index += 1U;
                if (part_index >= options.limits.max_parts) {
                    result.decode.status = ExrDecodeStatus::LimitExceeded;
                    return result;
                }
                part_block                  = measure ? kInvalidBlockId
                                                      : store->add_block(BlockInfo {});
                result.decode.parts_decoded = part_index + 1U;
                order_in_block              = 0U;
                part_attr_count             = 0U;
                first                       = next;
            }

            if ((options.limits.max_attributes_per_part != 0U
                 && part_attr_count >= options.limits.max_attributes_per_part)
                || (options.limits.max_attributes != 0U
                    && total_attr_count >= options.limits.max_attributes)) {
                result.decode.status = ExrDecodeStatus::LimitExceeded;
                return result;
            }

            std::string name;
            std::string type_name;
            const ParseStringStatus name_status
                = source_cstr_with_first(&source, &offset, first,
                                         options.limits.max_name_bytes, &name);
            const ParseStringStatus type_status
                = (name_status == ParseStringStatus::Ok)
                      ? source_cstr(&source, &offset,
                                    options.limits.max_type_name_bytes,
                                    &type_name)
                      : name_status;
            if (name_status == ParseStringStatus::LimitExceeded
                || type_status == ParseStringStatus::LimitExceeded) {
                result.decode.status = ExrDecodeStatus::LimitExceeded;
                return result;
            }
            if (name_status != ParseStringStatus::Ok
                || type_status != ParseStringStatus::Ok) {
                result.decode.status = ExrDecodeStatus::Malformed;
                return result;
            }

            uint32_t attribute_size = 0U;
            if (!source_u32le(&source, offset, &attribute_size)) {
                return result;
            }
            offset += 4U;
            if ((options.limits.max_attribute_bytes != 0U
                 && attribute_size > options.limits.max_attribute_bytes)
                || attribute_size > exr.size - offset) {
                result.decode.status = (attribute_size > exr.size - offset)
                                           ? ExrDecodeStatus::Malformed
                                           : ExrDecodeStatus::LimitExceeded;
                return result;
            }
            const uint64_t next_total = total_attr_bytes
                                        + static_cast<uint64_t>(attribute_size);
            if (next_total < total_attr_bytes
                || (options.limits.max_total_attribute_bytes != 0U
                    && next_total > options.limits.max_total_attribute_bytes)) {
                result.decode.status = ExrDecodeStatus::LimitExceeded;
                return result;
            }

            if (!measure) {
                std::span<const std::byte> value_bytes;
                if (source_value(&source, offset, attribute_size,
                                 &value_bytes)) {
                    Entry entry;
                    entry.key   = make_exr_attribute_key(store->arena(),
                                                         part_index, name);
                    entry.value = decode_exr_value(type_name, value_bytes,
                                                   *store,
                                                   options.decode_known_types);
                    entry.origin.block            = part_block;
                    entry.origin.order_in_block   = order_in_block;
                    entry.origin.wire_type.family = WireFamily::Other;
                    const uint16_t wire_code      = exr_type_code(type_name);
                    entry.origin.wire_type.code   = wire_code;
                    entry.origin.wire_count       = attribute_size;
                    if (options.preserve_unknown_type_name
                        && wire_code == kExrAttrOpaque) {
                        entry.origin.wire_type_name
                            = store->arena().append_string(type_name);
                    }
                    entry.flags = flags;
                    if (store->add_entry(entry) == kInvalidEntryId) {
                        result.decode.status = ExrDecodeStatus::LimitExceeded;
                        return result;
                    }
                    result.decode.entries_decoded += 1U;
                } else if (!result.input.ok()) {
                    return result;
                }
            } else {
                result.decode.entries_decoded += 1U;
            }

            offset += static_cast<uint64_t>(attribute_size);
            order_in_block += 1U;
            part_attr_count += 1U;
            total_attr_count += 1U;
            total_attr_bytes = next_total;
        }

        if (result.input.ok()) {
            result.decode.status = ExrDecodeStatus::Malformed;
        }
        return result;
    }

}  // namespace


ExrRandomAccessDecodeResult
decode_exr_header_random_access(
    const RandomAccessSourceRange& exr, MetaStore& store,
    const ExrRandomAccessScratch& scratch, EntryFlags flags,
    const ExrDecodeOptions& options,
    const RandomAccessReadLimits& read_limits) noexcept
{
    return decode_exr_source(exr, &store, scratch, flags, options, read_limits,
                             false);
}


ExrRandomAccessDecodeResult
measure_exr_header_random_access(
    const RandomAccessSourceRange& exr, const ExrRandomAccessScratch& scratch,
    const ExrDecodeOptions& options,
    const RandomAccessReadLimits& read_limits) noexcept
{
    return decode_exr_source(exr, nullptr, scratch, EntryFlags::None, options,
                             read_limits, true);
}

}  // namespace openmeta
