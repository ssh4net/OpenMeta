// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/container_scan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace openmeta::container_test {

inline void
append_bytes(std::vector<std::byte>* output, std::span<const std::byte> bytes)
{
    output->insert(output->end(), bytes.begin(), bytes.end());
}

inline void
append_u32_be(std::vector<std::byte>* bytes, uint32_t value)
{
    for (uint32_t shift = 24U;; shift -= 8U) {
        bytes->push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
        if (shift == 0U) {
            break;
        }
    }
}

inline void
append_u32_le(std::vector<std::byte>* bytes, uint32_t value)
{
    for (uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes->push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

inline uint32_t
png_crc32(uint32_t type, std::span<const std::byte> data) noexcept
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t shift = 24U;; shift -= 8U) {
        crc ^= (type >> shift) & 0xFFU;
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc                 = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
        if (shift == 0U) {
            break;
        }
    }
    for (const std::byte value : data) {
        crc ^= std::to_integer<uint8_t>(value);
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc                 = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

inline void
append_png_chunk(std::vector<std::byte>* png, uint32_t type,
                 std::span<const std::byte> data)
{
    append_u32_be(png, static_cast<uint32_t>(data.size()));
    append_u32_be(png, type);
    append_bytes(png, data);
    append_u32_be(png, png_crc32(type, data));
}

inline constexpr std::array<std::byte, 14U> kPngIdat = {
    std::byte { 0x78U }, std::byte { 0xDAU }, std::byte { 0x63U },
    std::byte { 0x10U }, std::byte { 0x54U }, std::byte { 0x62U },
    std::byte { 0x30U }, std::byte { 0x76U }, std::byte { 0x01U },
    std::byte { 0x00U }, std::byte { 0x01U }, std::byte { 0x8DU },
    std::byte { 0x00U }, std::byte { 0xABU },
};

inline constexpr std::array<std::byte, 12U> kPngText = {
    std::byte { 'C' }, std::byte { 'o' }, std::byte { 'm' }, std::byte { 'm' },
    std::byte { 'e' }, std::byte { 'n' }, std::byte { 't' }, std::byte { 0U },
    std::byte { 'k' }, std::byte { 'e' }, std::byte { 'e' }, std::byte { 'p' },
};

inline std::vector<std::byte>
make_png_fixture()
{
    std::vector<std::byte> png = {
        std::byte { 0x89U }, std::byte { 'P' },   std::byte { 'N' },
        std::byte { 'G' },   std::byte { 0x0DU }, std::byte { 0x0AU },
        std::byte { 0x1AU }, std::byte { 0x0AU },
    };
    std::vector<std::byte> ihdr;
    append_u32_be(&ihdr, 2U);
    append_u32_be(&ihdr, 2U);
    ihdr.push_back(std::byte { 8U });
    ihdr.push_back(std::byte { 0U });
    ihdr.push_back(std::byte { 0U });
    ihdr.push_back(std::byte { 0U });
    ihdr.push_back(std::byte { 0U });
    append_png_chunk(&png, fourcc('I', 'H', 'D', 'R'), ihdr);
    append_png_chunk(&png, fourcc('I', 'D', 'A', 'T'), kPngIdat);
    append_png_chunk(&png, fourcc('t', 'E', 'X', 't'), kPngText);
    append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'), {});
    return png;
}

inline void
append_webp_chunk(std::vector<std::byte>* webp, uint32_t type,
                  std::span<const std::byte> data)
{
    append_u32_be(webp, type);
    append_u32_le(webp, static_cast<uint32_t>(data.size()));
    append_bytes(webp, data);
    if ((data.size() & 1U) != 0U) {
        webp->push_back(std::byte { 0U });
    }
}

inline constexpr std::array<std::byte, 3U> kOldWebpIcc = {
    std::byte { 'o' },
    std::byte { 'l' },
    std::byte { 'd' },
};

inline constexpr std::array<std::byte, 4U> kWebpFree = {
    std::byte { 'k' },
    std::byte { 'e' },
    std::byte { 'e' },
    std::byte { 'p' },
};

inline constexpr std::array<std::byte, 6U> kWebpVp8l = {
    std::byte { 0x2FU }, std::byte { 0x01U }, std::byte { 0x40U },
    std::byte { 0U },    std::byte { 0U },    std::byte { 0U },
};

inline std::vector<std::byte>
make_webp_fixture()
{
    std::vector<std::byte> webp = {
        std::byte { 'R' }, std::byte { 'I' }, std::byte { 'F' },
        std::byte { 'F' }, std::byte { 0U },  std::byte { 0U },
        std::byte { 0U },  std::byte { 0U },  std::byte { 'W' },
        std::byte { 'E' }, std::byte { 'B' }, std::byte { 'P' },
    };
    std::array<std::byte, 10U> vp8x {};
    vp8x[0U] = std::byte { 0x20U };
    vp8x[4U] = std::byte { 1U };
    vp8x[7U] = std::byte { 1U };
    append_webp_chunk(&webp, fourcc('V', 'P', '8', 'X'), vp8x);
    append_webp_chunk(&webp, fourcc('I', 'C', 'C', 'P'), kOldWebpIcc);
    append_webp_chunk(&webp, fourcc('F', 'R', 'E', 'E'), kWebpFree);
    append_webp_chunk(&webp, fourcc('V', 'P', '8', 'L'), kWebpVp8l);
    const uint32_t riff_size = static_cast<uint32_t>(webp.size() - 8U);
    webp[4U]                 = static_cast<std::byte>(riff_size & 0xFFU);
    webp[5U] = static_cast<std::byte>((riff_size >> 8U) & 0xFFU);
    webp[6U] = static_cast<std::byte>((riff_size >> 16U) & 0xFFU);
    webp[7U] = static_cast<std::byte>((riff_size >> 24U) & 0xFFU);
    return webp;
}

struct JpegFixture final {
    std::vector<std::byte> bytes;
    size_t scan_offset = 0U;
};

inline JpegFixture
make_jpeg_fixture()
{
    JpegFixture fixture;
    fixture.bytes       = { std::byte { 0xFFU }, std::byte { 0xD8U },
                            std::byte { 0xFFU }, std::byte { 0xFEU },
                            std::byte { 0x00U }, std::byte { 0x06U },
                            std::byte { 'k' },   std::byte { 'e' },
                            std::byte { 'e' },   std::byte { 'p' } };
    fixture.scan_offset = fixture.bytes.size();
    const std::array<std::byte, 6U> scan_header = {
        std::byte { 1U }, std::byte { 1U },  std::byte { 0U },
        std::byte { 0U }, std::byte { 63U }, std::byte { 0U },
    };
    fixture.bytes.push_back(std::byte { 0xFFU });
    fixture.bytes.push_back(std::byte { 0xDAU });
    fixture.bytes.push_back(std::byte { 0x00U });
    fixture.bytes.push_back(std::byte { 0x08U });
    append_bytes(&fixture.bytes, scan_header);
    fixture.bytes.push_back(std::byte { 0x12U });
    fixture.bytes.push_back(std::byte { 0xFFU });
    fixture.bytes.push_back(std::byte { 0x00U });
    fixture.bytes.push_back(std::byte { 0xA5U });
    fixture.bytes.push_back(std::byte { 0xFFU });
    fixture.bytes.push_back(std::byte { 0xD0U });
    fixture.bytes.push_back(std::byte { 0x31U });
    fixture.bytes.push_back(std::byte { 0xFFU });
    fixture.bytes.push_back(std::byte { 0xD9U });
    return fixture;
}

struct TiffFixtureVariant final {
    bool big_tiff   = false;
    bool big_endian = false;
    bool dng        = false;
};

inline size_t
tiff_type_width(uint16_t type) noexcept
{
    switch (type) {
    case 1U:
    case 2U:
    case 6U:
    case 7U: return 1U;
    case 3U:
    case 8U: return 2U;
    case 4U:
    case 9U:
    case 11U:
    case 13U: return 4U;
    case 5U:
    case 10U:
    case 12U:
    case 16U:
    case 17U:
    case 18U: return 8U;
    default: return 0U;
    }
}

struct TiffEntrySpan final {
    uint16_t type  = 0U;
    uint64_t count = 0U;
    std::span<const std::byte> value;
};

inline bool
decode_big_endian_uint(std::span<const std::byte> bytes,
                       uint64_t* value) noexcept
{
    if (value == nullptr || bytes.empty() || bytes.size() > 8U) {
        return false;
    }
    uint64_t decoded = 0U;
    for (const std::byte byte : bytes) {
        decoded = (decoded << 8U) | std::to_integer<uint8_t>(byte);
    }
    *value = decoded;
    return true;
}

inline bool
find_big_endian_bigtiff_entry(std::span<const std::byte> tiff,
                              uint16_t wanted_tag,
                              TiffEntrySpan* result) noexcept
{
    if (result == nullptr || tiff.size() < 16U || tiff[0U] != std::byte { 'M' }
        || tiff[1U] != std::byte { 'M' }) {
        return false;
    }
    uint64_t version     = 0U;
    uint64_t offset_size = 0U;
    uint64_t reserved    = 0U;
    uint64_t ifd_offset  = 0U;
    if (!decode_big_endian_uint(tiff.subspan(2U, 2U), &version)
        || !decode_big_endian_uint(tiff.subspan(4U, 2U), &offset_size)
        || !decode_big_endian_uint(tiff.subspan(6U, 2U), &reserved)
        || !decode_big_endian_uint(tiff.subspan(8U, 8U), &ifd_offset)
        || version != 43U || offset_size != 8U || reserved != 0U
        || ifd_offset > std::numeric_limits<size_t>::max()
        || ifd_offset > tiff.size()) {
        return false;
    }

    const size_t ifd_base = static_cast<size_t>(ifd_offset);
    if (tiff.size() - ifd_base < 16U) {
        return false;
    }
    uint64_t entry_count = 0U;
    if (!decode_big_endian_uint(tiff.subspan(ifd_base, 8U), &entry_count)) {
        return false;
    }
    const size_t entries_offset = ifd_base + 8U;
    const size_t remaining      = tiff.size() - entries_offset;
    if (remaining < 8U || entry_count > (remaining - 8U) / 20U) {
        return false;
    }

    for (uint64_t index = 0U; index < entry_count; ++index) {
        const size_t entry_offset = entries_offset
                                    + static_cast<size_t>(index) * 20U;
        uint64_t tag   = 0U;
        uint64_t type  = 0U;
        uint64_t count = 0U;
        if (!decode_big_endian_uint(tiff.subspan(entry_offset, 2U), &tag)
            || tag != wanted_tag) {
            continue;
        }
        if (!decode_big_endian_uint(tiff.subspan(entry_offset + 2U, 2U), &type)
            || type > std::numeric_limits<uint16_t>::max()
            || !decode_big_endian_uint(tiff.subspan(entry_offset + 4U, 8U),
                                       &count)) {
            return false;
        }
        const size_t width = tiff_type_width(static_cast<uint16_t>(type));
        if (width == 0U || count > std::numeric_limits<size_t>::max() / width) {
            return false;
        }
        const size_t payload_size       = static_cast<size_t>(count) * width;
        const size_t value_field_offset = entry_offset + 12U;
        uint64_t value_offset           = value_field_offset;
        if (payload_size > 8U
            && !decode_big_endian_uint(tiff.subspan(value_field_offset, 8U),
                                       &value_offset)) {
            return false;
        }
        if (value_offset > std::numeric_limits<size_t>::max()
            || value_offset > tiff.size()
            || payload_size > tiff.size() - static_cast<size_t>(value_offset)) {
            return false;
        }
        result->type  = static_cast<uint16_t>(type);
        result->count = count;
        result->value = tiff.subspan(static_cast<size_t>(value_offset),
                                     payload_size);
        return true;
    }
    return false;
}

inline bool
decode_tiff_scalar(const TiffEntrySpan& entry, uint64_t* value) noexcept
{
    return entry.count == 1U && decode_big_endian_uint(entry.value, value);
}

inline void
append_tiff_uint(std::vector<std::byte>* bytes, size_t width, uint64_t value,
                 bool big_endian)
{
    for (size_t index = 0U; index < width; ++index) {
        const size_t shift = big_endian ? (width - 1U - index) * 8U
                                        : index * 8U;
        bytes->push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

inline std::array<std::byte, 8U>
encode_tiff_uint(uint64_t value, size_t width, bool big_endian) noexcept
{
    std::array<std::byte, 8U> encoded {};
    for (size_t index = 0U; index < width; ++index) {
        const size_t shift = big_endian ? (width - 1U - index) * 8U
                                        : index * 8U;
        encoded[index]     = static_cast<std::byte>((value >> shift) & 0xFFU);
    }
    return encoded;
}

inline void
append_tiff_entry(std::vector<std::byte>* tiff, TiffFixtureVariant variant,
                  uint16_t tag, uint16_t type, uint64_t count,
                  std::span<const std::byte> value_bytes,
                  uint64_t external_offset)
{
    const size_t inline_width = variant.big_tiff ? 8U : 4U;
    const size_t count_width  = variant.big_tiff ? 8U : 4U;
    const size_t type_width   = tiff_type_width(type);
    append_tiff_uint(tiff, 2U, tag, variant.big_endian);
    append_tiff_uint(tiff, 2U, type, variant.big_endian);
    append_tiff_uint(tiff, count_width, count, variant.big_endian);
    const uint64_t payload_size = count * type_width;
    if (payload_size <= inline_width) {
        tiff->insert(tiff->end(), value_bytes.begin(), value_bytes.end());
        for (size_t index = value_bytes.size(); index < inline_width; ++index) {
            tiff->push_back(std::byte { 0U });
        }
    } else {
        append_tiff_uint(tiff, inline_width, external_offset,
                         variant.big_endian);
    }
}

inline void
append_tiff_scalar_entry(std::vector<std::byte>* tiff,
                         TiffFixtureVariant variant, uint16_t tag,
                         uint16_t type, uint64_t value)
{
    const size_t width = tiff_type_width(type);
    const std::array<std::byte, 8U> encoded
        = encode_tiff_uint(value, width, variant.big_endian);
    append_tiff_entry(tiff, variant, tag, type, 1U,
                      std::span<const std::byte>(encoded.data(), width), 0U);
}

inline std::vector<std::byte>
make_tiff_fixture(TiffFixtureVariant variant)
{
    std::vector<std::byte> tiff;
    const uint64_t ifd_offset     = variant.big_tiff ? 16U : 8U;
    const uint64_t entry_count    = variant.dng ? 10U : 9U;
    const uint64_t count_width    = variant.big_tiff ? 8U : 2U;
    const uint64_t entry_width    = variant.big_tiff ? 20U : 12U;
    const uint64_t next_ifd_width = variant.big_tiff ? 8U : 4U;
    const uint64_t end_of_ifd     = ifd_offset + count_width
                                + entry_count * entry_width + next_ifd_width;
    const uint64_t strip_offset = (end_of_ifd + 3U) & ~uint64_t { 3U };

    tiff.push_back(variant.big_endian ? std::byte { 'M' } : std::byte { 'I' });
    tiff.push_back(variant.big_endian ? std::byte { 'M' } : std::byte { 'I' });
    append_tiff_uint(&tiff, 2U, variant.big_tiff ? 43U : 42U,
                     variant.big_endian);
    if (variant.big_tiff) {
        append_tiff_uint(&tiff, 2U, 8U, variant.big_endian);
        append_tiff_uint(&tiff, 2U, 0U, variant.big_endian);
        append_tiff_uint(&tiff, 8U, ifd_offset, variant.big_endian);
    } else {
        append_tiff_uint(&tiff, 4U, ifd_offset, variant.big_endian);
    }
    append_tiff_uint(&tiff, variant.big_tiff ? 8U : 2U, entry_count,
                     variant.big_endian);

    append_tiff_scalar_entry(&tiff, variant, 0x0100U, 4U, 2U);
    append_tiff_scalar_entry(&tiff, variant, 0x0101U, 4U, 2U);
    append_tiff_scalar_entry(&tiff, variant, 0x0102U, 3U, 8U);
    append_tiff_scalar_entry(&tiff, variant, 0x0103U, 3U, 1U);
    append_tiff_scalar_entry(&tiff, variant, 0x0106U, 3U, 1U);
    const std::array<std::byte, 8U> strip_offset_bytes
        = encode_tiff_uint(strip_offset, 4U, variant.big_endian);
    append_tiff_entry(&tiff, variant, 0x0111U, 4U, 1U,
                      std::span<const std::byte>(strip_offset_bytes.data(), 4U),
                      0U);
    append_tiff_scalar_entry(&tiff, variant, 0x0115U, 3U, 1U);
    append_tiff_scalar_entry(&tiff, variant, 0x0116U, 4U, 2U);
    append_tiff_scalar_entry(&tiff, variant, 0x0117U, 4U, 4U);
    if (variant.dng) {
        const std::array<std::byte, 4U> dng_version = {
            std::byte { 1U },
            std::byte { 4U },
            std::byte { 0U },
            std::byte { 0U },
        };
        append_tiff_entry(&tiff, variant, 0xC612U, 1U, 4U, dng_version, 0U);
    }
    append_tiff_uint(&tiff, variant.big_tiff ? 8U : 4U, 0U, variant.big_endian);
    while (tiff.size() < strip_offset) {
        tiff.push_back(std::byte { 0U });
    }
    tiff.push_back(std::byte { 0x11U });
    tiff.push_back(std::byte { 0x22U });
    tiff.push_back(std::byte { 0x33U });
    tiff.push_back(std::byte { 0x44U });
    return tiff;
}

}  // namespace openmeta::container_test
