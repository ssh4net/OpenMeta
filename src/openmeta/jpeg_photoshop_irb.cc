// SPDX-License-Identifier: Apache-2.0

#include "jpeg_photoshop_irb.h"

#include <cassert>
#include <cstring>

namespace openmeta::detail {
namespace {

    constexpr size_t kPhotoshopHeaderSize = 14U;
    constexpr size_t kPhotoshopPayloadMax = 65533U;
    constexpr uint16_t kIptcResourceId    = 0x0404U;

    static uint8_t read_u8(std::span<const std::byte> bytes,
                           size_t offset) noexcept
    {
        return std::to_integer<uint8_t>(bytes[offset]);
    }


    static uint16_t read_u16be(std::span<const std::byte> bytes,
                               size_t offset) noexcept
    {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(read_u8(bytes, offset)) << 8U)
            | static_cast<uint16_t>(read_u8(bytes, offset + 1U)));
    }


    static uint32_t read_u32be(std::span<const std::byte> bytes,
                               size_t offset) noexcept
    {
        return (static_cast<uint32_t>(read_u8(bytes, offset)) << 24U)
               | (static_cast<uint32_t>(read_u8(bytes, offset + 1U)) << 16U)
               | (static_cast<uint32_t>(read_u8(bytes, offset + 2U)) << 8U)
               | static_cast<uint32_t>(read_u8(bytes, offset + 3U));
    }


    static bool has_bytes(std::span<const std::byte> bytes, size_t offset,
                          size_t count) noexcept
    {
        return offset <= bytes.size() && count <= bytes.size() - offset;
    }


    static bool has_photoshop_header(std::span<const std::byte> payload) noexcept
    {
        static constexpr char kHeader[] = "Photoshop 3.0";
        if (!has_bytes(payload, 0U, kPhotoshopHeaderSize)) {
            return false;
        }
        for (size_t index = 0U; index + 1U < sizeof(kHeader); ++index) {
            if (payload[index]
                != static_cast<std::byte>(
                    static_cast<uint8_t>(kHeader[index]))) {
                return false;
            }
        }
        return payload[sizeof(kHeader) - 1U] == std::byte { 0U };
    }


    static bool has_8bim_signature(std::span<const std::byte> payload,
                                   size_t offset) noexcept
    {
        return payload[offset] == std::byte { '8' }
               && payload[offset + 1U] == std::byte { 'B' }
               && payload[offset + 2U] == std::byte { 'I' }
               && payload[offset + 3U] == std::byte { 'M' };
    }


    static size_t padded_pascal_name_size(uint8_t name_length) noexcept
    {
        size_t size = 1U + static_cast<size_t>(name_length);
        if ((size & 1U) != 0U) {
            size += 1U;
        }
        return size;
    }

}  // namespace

bool
inspect_jpeg_photoshop_iptc(std::span<const std::byte> payload,
                            JpegPhotoshopIptcInfo* out) noexcept
{
    if (!out || payload.size() > kPhotoshopPayloadMax
        || !has_photoshop_header(payload)) {
        return false;
    }

    JpegPhotoshopIptcInfo info;
    info.retained_payload_size = static_cast<uint32_t>(kPhotoshopHeaderSize);
    size_t offset              = kPhotoshopHeaderSize;
    while (offset < payload.size()) {
        const size_t record_start = offset;
        if (!has_bytes(payload, offset, 6U)
            || !has_8bim_signature(payload, offset)) {
            return false;
        }
        const uint16_t resource_id = read_u16be(payload, offset + 4U);
        offset += 6U;

        if (!has_bytes(payload, offset, 1U)) {
            return false;
        }
        const uint8_t name_length = read_u8(payload, offset);
        const size_t name_size    = padded_pascal_name_size(name_length);
        if (!has_bytes(payload, offset, name_size)) {
            return false;
        }
        offset += name_size;

        if (!has_bytes(payload, offset, 4U)) {
            return false;
        }
        const uint32_t data_length = read_u32be(payload, offset);
        offset += 4U;
        if (!has_bytes(payload, offset, data_length)) {
            return false;
        }
        offset += static_cast<size_t>(data_length);
        if ((data_length & 1U) != 0U) {
            if (!has_bytes(payload, offset, 1U)) {
                return false;
            }
            offset += 1U;
        }

        if (resource_id == kIptcResourceId) {
            info.iptc_resources += 1U;
        } else {
            info.other_resources += 1U;
            info.retained_payload_size += static_cast<uint32_t>(offset
                                                                - record_start);
        }
    }

    *out = info;
    return true;
}


void
copy_jpeg_photoshop_without_iptc(std::span<const std::byte> payload,
                                 std::span<std::byte> output) noexcept
{
    assert(payload.size() <= kPhotoshopPayloadMax);
    assert(output.size() >= kPhotoshopHeaderSize);
    std::memcpy(output.data(), payload.data(), kPhotoshopHeaderSize);

    size_t payload_offset = kPhotoshopHeaderSize;
    size_t output_offset  = kPhotoshopHeaderSize;
    while (payload_offset < payload.size()) {
        const size_t record_start  = payload_offset;
        const uint16_t resource_id = read_u16be(payload, payload_offset + 4U);
        const uint8_t name_length  = read_u8(payload, payload_offset + 6U);
        const size_t name_size     = padded_pascal_name_size(name_length);
        const size_t data_length_offset = payload_offset + 6U + name_size;
        const uint32_t data_length = read_u32be(payload, data_length_offset);
        const size_t record_size   = 10U + name_size
                                   + static_cast<size_t>(data_length)
                                   + static_cast<size_t>(data_length & 1U);

        if (resource_id != kIptcResourceId) {
            std::memcpy(output.data() + output_offset,
                        payload.data() + record_start, record_size);
            output_offset += record_size;
        }
        payload_offset += record_size;
    }
    assert(output_offset == output.size());
}

}  // namespace openmeta::detail
