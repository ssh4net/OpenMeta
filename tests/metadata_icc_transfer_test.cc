// SPDX-License-Identifier: Apache-2.0

#include "icc_authoring_test_fixture.h"
#include "metadata_container_test_fixture.h"

#include "openmeta/container_payload.h"
#include "openmeta/container_scan.h"
#include "openmeta/icc_decode.h"
#include "openmeta/icc_serialize.h"
#include "openmeta/meta_flags.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace openmeta {
namespace {

    using container_test::JpegFixture;
    using container_test::kWebpFree;
    using container_test::kWebpVp8l;
    using container_test::make_jpeg_fixture;
    using container_test::make_png_fixture;
    using container_test::make_tiff_fixture;
    using container_test::make_webp_fixture;
    using container_test::TiffFixtureVariant;

    static constexpr uint32_t kFirstTagSignature     = 0x7A7A7A31U;
    static constexpr uint32_t kRemovedTagSignature   = 0x61616131U;
    static constexpr uint32_t kAddedTagSignature     = 0x6E657731U;
    static constexpr uint32_t kEmptyTagSignature     = 0x656D7031U;
    static constexpr uint32_t kMalformedTagSignature = 0x62616431U;
    static constexpr uint32_t kNewCreator            = 0x4E455721U;

    static constexpr std::array<std::byte, 15U> kEditedFirstTag = {
        std::byte { 'd' },   std::byte { 'a' },   std::byte { 't' },
        std::byte { 'a' },   std::byte { 0U },    std::byte { 0U },
        std::byte { 0U },    std::byte { 0U },    std::byte { 0x81U },
        std::byte { 0x92U }, std::byte { 0xA3U }, std::byte { 0xB4U },
        std::byte { 0xC5U }, std::byte { 0xD6U }, std::byte { 0xE7U },
    };

    static constexpr std::array<std::byte, 13U> kAddedTag = {
        std::byte { 't' },   std::byte { 'e' },   std::byte { 's' },
        std::byte { 't' },   std::byte { 0U },    std::byte { 0U },
        std::byte { 0U },    std::byte { 0U },    std::byte { 0x19U },
        std::byte { 0x2AU }, std::byte { 0x3BU }, std::byte { 0x4CU },
        std::byte { 0x5DU },
    };

    static uint32_t read_u32_be(std::span<const std::byte> bytes,
                                size_t offset) noexcept
    {
        return (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset]))
                << 24U)
               | (static_cast<uint32_t>(
                      std::to_integer<uint8_t>(bytes[offset + 1U]))
                  << 16U)
               | (static_cast<uint32_t>(
                      std::to_integer<uint8_t>(bytes[offset + 2U]))
                  << 8U)
               | static_cast<uint32_t>(
                   std::to_integer<uint8_t>(bytes[offset + 3U]));
    }

    static uint64_t read_tiff_uint(std::span<const std::byte> bytes,
                                   size_t offset, size_t width,
                                   bool big_endian) noexcept
    {
        uint64_t value = 0U;
        for (size_t index = 0U; index < width; ++index) {
            const size_t byte_index = big_endian ? index : width - 1U - index;
            value                   = (value << 8U)
                    | std::to_integer<uint8_t>(bytes[offset + byte_index]);
        }
        return value;
    }

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    static bool find_png_chunk(std::span<const std::byte> png, uint32_t type,
                               std::span<const std::byte>* data) noexcept
    {
        if (png.size() < 8U || png[0] != std::byte { 0x89U }
            || png[1] != std::byte { 'P' } || png[2] != std::byte { 'N' }
            || png[3] != std::byte { 'G' }) {
            return false;
        }
        size_t offset = 8U;
        while (offset <= png.size() && png.size() - offset >= 12U) {
            const uint32_t length     = read_u32_be(png, offset);
            const uint32_t chunk_type = read_u32_be(png, offset + 4U);
            if (length > png.size() - offset - 12U) {
                return false;
            }
            if (chunk_type == type) {
                if (data != nullptr) {
                    *data = png.subspan(offset + 8U, length);
                }
                return true;
            }
            offset += static_cast<size_t>(length) + 12U;
        }
        return false;
    }
#endif

    static bool find_webp_chunk(std::span<const std::byte> webp, uint32_t type,
                                std::span<const std::byte>* data) noexcept
    {
        if (webp.size() < 12U || webp[0] != std::byte { 'R' }
            || webp[1] != std::byte { 'I' } || webp[2] != std::byte { 'F' }
            || webp[3] != std::byte { 'F' } || webp[8] != std::byte { 'W' }
            || webp[9] != std::byte { 'E' } || webp[10] != std::byte { 'B' }
            || webp[11] != std::byte { 'P' }) {
            return false;
        }
        size_t offset = 12U;
        while (offset <= webp.size() && webp.size() - offset >= 8U) {
            const uint32_t chunk_type = read_u32_be(webp, offset);
            const uint32_t length     = static_cast<uint32_t>(
                read_tiff_uint(webp, offset + 4U, 4U, false));
            if (length > webp.size() - offset - 8U) {
                return false;
            }
            if (chunk_type == type) {
                if (data != nullptr) {
                    *data = webp.subspan(offset + 8U, length);
                }
                return true;
            }
            const size_t padded_length = static_cast<size_t>(length)
                                         + (length & 1U);
            if (padded_length > webp.size() - offset - 8U) {
                return false;
            }
            offset += 8U + padded_length;
        }
        return false;
    }

    static size_t tiff_type_width(uint16_t type) noexcept
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

    static bool find_tiff_entry(std::span<const std::byte> tiff,
                                TiffFixtureVariant variant, uint16_t tag,
                                uint16_t* type, uint64_t* count,
                                std::span<const std::byte>* value) noexcept
    {
        const size_t header_size = variant.big_tiff ? 16U : 8U;
        if (tiff.size() < header_size) {
            return false;
        }
        const uint16_t version = static_cast<uint16_t>(
            read_tiff_uint(tiff, 2U, 2U, variant.big_endian));
        if (version != (variant.big_tiff ? 43U : 42U)) {
            return false;
        }
        const size_t ifd_pointer_width = variant.big_tiff ? 8U : 4U;
        const uint64_t ifd_offset
            = read_tiff_uint(tiff, variant.big_tiff ? 8U : 4U,
                             ifd_pointer_width, variant.big_endian);
        const size_t count_width    = variant.big_tiff ? 8U : 2U;
        const size_t entry_width    = variant.big_tiff ? 20U : 12U;
        const size_t next_ifd_width = variant.big_tiff ? 8U : 4U;
        if (ifd_offset > tiff.size()
            || count_width > tiff.size() - static_cast<size_t>(ifd_offset)) {
            return false;
        }
        const size_t base           = static_cast<size_t>(ifd_offset);
        const uint64_t entries      = read_tiff_uint(tiff, base, count_width,
                                                     variant.big_endian);
        const size_t entries_offset = base + count_width;
        const size_t remaining      = tiff.size() - entries_offset;
        if (entries > remaining / entry_width
            || next_ifd_width
                   > remaining - static_cast<size_t>(entries) * entry_width) {
            return false;
        }
        for (uint64_t index = 0U; index < entries; ++index) {
            const size_t entry_offset
                = entries_offset + static_cast<size_t>(index) * entry_width;
            if (read_tiff_uint(tiff, entry_offset, 2U, variant.big_endian)
                != tag) {
                continue;
            }
            const uint16_t entry_type = static_cast<uint16_t>(
                read_tiff_uint(tiff, entry_offset + 2U, 2U, variant.big_endian));
            const size_t width = tiff_type_width(entry_type);
            if (width == 0U) {
                return false;
            }
            const size_t entry_count_width = variant.big_tiff ? 8U : 4U;
            const uint64_t entry_count = read_tiff_uint(tiff, entry_offset + 4U,
                                                        entry_count_width,
                                                        variant.big_endian);
            if (entry_count > SIZE_MAX / width) {
                return false;
            }
            const size_t payload_size = static_cast<size_t>(entry_count)
                                        * width;
            const size_t inline_width = variant.big_tiff ? 8U : 4U;
            const size_t value_offset = entry_offset
                                        + (variant.big_tiff ? 12U : 8U);
            const uint64_t data_offset
                = payload_size <= inline_width
                      ? value_offset
                      : read_tiff_uint(tiff, value_offset, inline_width,
                                       variant.big_endian);
            if (data_offset > tiff.size()
                || payload_size
                       > tiff.size() - static_cast<size_t>(data_offset)) {
                return false;
            }
            if (type != nullptr) {
                *type = entry_type;
            }
            if (count != nullptr) {
                *count = entry_count;
            }
            if (value != nullptr) {
                *value = tiff.subspan(static_cast<size_t>(data_offset),
                                      payload_size);
            }
            return true;
        }
        return false;
    }

    static bool read_tiff_scalar(std::span<const std::byte> tiff,
                                 TiffFixtureVariant variant, uint16_t tag,
                                 uint16_t expected_type,
                                 uint64_t* value) noexcept
    {
        uint16_t type  = 0U;
        uint64_t count = 0U;
        std::span<const std::byte> bytes;
        if (!find_tiff_entry(tiff, variant, tag, &type, &count, &bytes)
            || type != expected_type || count != 1U
            || bytes.size() != tiff_type_width(expected_type)) {
            return false;
        }
        *value = read_tiff_uint(bytes, 0U, bytes.size(), variant.big_endian);
        return true;
    }

    static void
    expect_tiff_media_and_icc(std::span<const std::byte> tiff,
                              TiffFixtureVariant variant,
                              std::span<const std::byte> expected_profile)
    {
        const std::array<std::array<uint64_t, 3U>, 8U> scalar_fields = { {
            { 0x0100U, 4U, 2U },
            { 0x0101U, 4U, 2U },
            { 0x0102U, 3U, 8U },
            { 0x0103U, 3U, 1U },
            { 0x0106U, 3U, 1U },
            { 0x0115U, 3U, 1U },
            { 0x0116U, 4U, 2U },
            { 0x0117U, 4U, 4U },
        } };
        for (const std::array<uint64_t, 3U>& field : scalar_fields) {
            uint64_t value = 0U;
            ASSERT_TRUE(read_tiff_scalar(tiff, variant,
                                         static_cast<uint16_t>(field[0U]),
                                         static_cast<uint16_t>(field[1U]),
                                         &value))
                << "tag=" << field[0U];
            EXPECT_EQ(value, field[2U]) << "tag=" << field[0U];
        }

        uint64_t strip_offset = 0U;
        ASSERT_TRUE(
            read_tiff_scalar(tiff, variant, 0x0111U, 4U, &strip_offset));
        uint16_t icc_type  = 0U;
        uint64_t icc_count = 0U;
        std::span<const std::byte> icc_tag_bytes;
        ASSERT_TRUE(find_tiff_entry(tiff, variant, 34675U, &icc_type,
                                    &icc_count, &icc_tag_bytes));
        EXPECT_EQ(icc_type, 7U);
        ASSERT_EQ(icc_count, expected_profile.size());
        ASSERT_EQ(icc_tag_bytes.size(), expected_profile.size());
        EXPECT_TRUE(std::equal(icc_tag_bytes.begin(), icc_tag_bytes.end(),
                               expected_profile.begin()));

        uint16_t strip_type  = 0U;
        uint64_t strip_count = 0U;
        std::span<const std::byte> raster;
        ASSERT_TRUE(find_tiff_entry(tiff, variant, 0x0111U, &strip_type,
                                    &strip_count, nullptr));
        EXPECT_EQ(strip_type, 4U);
        EXPECT_EQ(strip_count, 1U);
        uint64_t strip_bytes = 0U;
        ASSERT_TRUE(read_tiff_scalar(tiff, variant, 0x0117U, 4U, &strip_bytes));
        ASSERT_EQ(strip_bytes, 4U);
        ASSERT_LE(strip_offset, tiff.size());
        ASSERT_LE(static_cast<size_t>(strip_bytes),
                  tiff.size() - static_cast<size_t>(strip_offset));
        raster = tiff.subspan(static_cast<size_t>(strip_offset),
                              static_cast<size_t>(strip_bytes));
        static constexpr std::array<std::byte, 4U> expected_raster = {
            std::byte { 0x11U },
            std::byte { 0x22U },
            std::byte { 0x33U },
            std::byte { 0x44U },
        };
        EXPECT_TRUE(std::equal(expected_raster.begin(), expected_raster.end(),
                               raster.begin()));

        if (variant.dng) {
            uint16_t dng_version_type  = 0U;
            uint64_t dng_version_count = 0U;
            std::span<const std::byte> dng_version;
            ASSERT_TRUE(find_tiff_entry(tiff, variant, 0xC612U,
                                        &dng_version_type, &dng_version_count,
                                        &dng_version));
            EXPECT_EQ(dng_version_type, 1U);
            ASSERT_EQ(dng_version_count, 4U);
            ASSERT_EQ(dng_version.size(), 4U);
            EXPECT_EQ(dng_version[0U], std::byte { 1U });
            EXPECT_EQ(dng_version[1U], std::byte { 4U });
            EXPECT_EQ(dng_version[2U], std::byte { 0U });
            EXPECT_EQ(dng_version[3U], std::byte { 0U });
        }
    }

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    static void
    expect_png_icc_prepared_payload(const PreparedTransferBlock& block,
                                    std::span<const std::byte> expected_profile)
    {
        ASSERT_GE(block.payload.size(), 5U);
        EXPECT_EQ(block.payload[0U], std::byte { 'i' });
        EXPECT_EQ(block.payload[1U], std::byte { 'c' });
        EXPECT_EQ(block.payload[2U], std::byte { 'c' });
        EXPECT_EQ(block.payload[3U], std::byte { 0U });
        EXPECT_EQ(block.payload[4U], std::byte { 0U });
        ContainerBlockRef compressed_block;
        compressed_block.compression = BlockCompression::Deflate;
        compressed_block.data_offset = 5U;
        compressed_block.data_size   = block.payload.size() - 5U;
        const std::array<ContainerBlockRef, 1U> blocks = { compressed_block };
        std::array<std::byte, 4096U> profile_bytes {};
        std::array<uint32_t, 8U> scratch_indices {};
        const PayloadResult unpacked
            = extract_payload(block.payload, blocks, 0U, profile_bytes,
                              scratch_indices, PayloadOptions {});
        ASSERT_EQ(unpacked.status, PayloadStatus::Ok);
        ASSERT_EQ(unpacked.written, expected_profile.size());
        EXPECT_TRUE(std::equal(expected_profile.begin(), expected_profile.end(),
                               profile_bytes.begin()));
    }
#endif

    static bool extract_icc_profile(std::span<const std::byte> container,
                                    std::vector<std::byte>* profile)
    {
        if (profile == nullptr) {
            return false;
        }
        std::array<ContainerBlockRef, 128U> scan_blocks {};
        const ScanResult scan = scan_auto(container, scan_blocks);
        if (scan.status != ScanStatus::Ok
            || scan.written > scan_blocks.size()) {
            return false;
        }
        const std::span<const ContainerBlockRef> blocks(scan_blocks.data(),
                                                        static_cast<size_t>(
                                                            scan.written));
        uint32_t icc_index = UINT32_MAX;
        for (uint32_t index = 0U; index < blocks.size(); ++index) {
            if (blocks[index].kind != ContainerBlockKind::Icc) {
                continue;
            }
            if (icc_index != UINT32_MAX) {
                return false;
            }
            icc_index = index;
        }
        if (icc_index == UINT32_MAX) {
            return false;
        }

        std::array<std::byte, 4096U> payload {};
        std::array<uint32_t, 128U> payload_indices {};
        const PayloadResult extracted
            = extract_payload(container, blocks, icc_index, payload,
                              payload_indices, PayloadOptions {});
        if (extracted.status != PayloadStatus::Ok
            || extracted.written > payload.size()) {
            return false;
        }
        profile->assign(payload.begin(),
                        payload.begin()
                            + static_cast<size_t>(extracted.written));
        return true;
    }

    static bool store_has_icc_bytes(const MetaStore& store, uint32_t signature,
                                    std::span<const std::byte> expected)
    {
        const std::span<const EntryId> ids = store.find_all(
            make_icc_tag_key_view(signature));
        if (ids.size() != 1U) {
            return false;
        }
        const Entry& entry = store.entry(ids.front());
        if (entry.value.kind != MetaValueKind::Bytes) {
            return false;
        }
        return store.arena().span(entry.value.data.span).size()
                   == expected.size()
               && (expected.empty()
                   || std::memcmp(
                          store.arena().span(entry.value.data.span).data(),
                          expected.data(), expected.size())
                          == 0);
    }

    static bool
    clone_clean_icc_store_with_legacy_edge_entries(const MetaStore& source,
                                                   MetaStore* destination)
    {
        static constexpr std::array<std::byte, 13U> duplicate_first_tag = {
            std::byte { 'd' },   std::byte { 'a' },   std::byte { 't' },
            std::byte { 'a' },   std::byte { 0U },    std::byte { 0U },
            std::byte { 0U },    std::byte { 0U },    std::byte { 0xEEU },
            std::byte { 0xDDU }, std::byte { 0xCCU }, std::byte { 0xBBU },
            std::byte { 0xAAU },
        };
        static constexpr std::array<uint32_t, 1U> malformed_payload = {
            0x10203040U,
        };

        if (destination == nullptr || !source.is_finalized()) {
            return false;
        }

        const size_t additional_arena_bytes = duplicate_first_tag.size()
                                              + sizeof(malformed_payload[0U]);
        destination->reserve(source.block_count(),
                             static_cast<uint32_t>(source.entries().size() + 4U),
                             source.arena().bytes().size()
                                 + additional_arena_bytes);
        for (BlockId block = 0U; block < source.block_count(); ++block) {
            if (destination->add_block(source.block_info(block)) != block) {
                return false;
            }
        }

        Origin creator_origin;
        Origin first_tag_origin;
        bool has_creator_origin   = false;
        bool has_first_tag_origin = false;
        for (const Entry& source_entry : source.entries()) {
            if (source_entry.flags != EntryFlags::None
                || (source_entry.key.kind != MetaKeyKind::IccHeaderField
                    && source_entry.key.kind != MetaKeyKind::IccTag)) {
                return false;
            }

            Entry copied_entry                 = source_entry;
            copied_entry.origin.wire_type_name = {};
            if (source_entry.value.kind == MetaValueKind::Array
                || source_entry.value.kind == MetaValueKind::Bytes
                || source_entry.value.kind == MetaValueKind::Text) {
                const std::span<const std::byte> payload = source.arena().span(
                    source_entry.value.data.span);
                if (payload.size() != source_entry.value.data.span.size) {
                    return false;
                }
                copied_entry.value.data.span = destination->arena().append(
                    payload);
                if (copied_entry.value.data.span.size != payload.size()) {
                    return false;
                }
            }
            if (source_entry.origin.wire_type_name.size != 0U) {
                const std::span<const std::byte> wire_type_name
                    = source.arena().span(source_entry.origin.wire_type_name);
                if (wire_type_name.size()
                    != source_entry.origin.wire_type_name.size) {
                    return false;
                }
                copied_entry.origin.wire_type_name
                    = destination->arena().append(wire_type_name);
                if (copied_entry.origin.wire_type_name.size
                    != wire_type_name.size()) {
                    return false;
                }
            }
            copied_entry.flags = EntryFlags::None;
            if (destination->add_entry(copied_entry) == kInvalidEntryId) {
                return false;
            }

            if (source_entry.key.kind == MetaKeyKind::IccHeaderField
                && source_entry.key.data.icc_header_field.offset == 80U) {
                creator_origin     = source_entry.origin;
                has_creator_origin = true;
            }
            if (source_entry.key.kind == MetaKeyKind::IccTag
                && source_entry.key.data.icc_tag.signature
                       == kFirstTagSignature) {
                first_tag_origin     = source_entry.origin;
                has_first_tag_origin = true;
            }
        }
        if (!has_creator_origin || !has_first_tag_origin) {
            return false;
        }

        Entry duplicate_creator;
        duplicate_creator.key          = make_icc_header_field_key(80U);
        duplicate_creator.value        = make_u32(kNewCreator);
        duplicate_creator.origin.block = creator_origin.block;
        duplicate_creator.origin.order_in_block = creator_origin.order_in_block;
        duplicate_creator.flags                 = EntryFlags::None;
        if (destination->add_entry(duplicate_creator) == kInvalidEntryId) {
            return false;
        }

        Entry duplicate_tag;
        duplicate_tag.key          = make_icc_tag_key(kFirstTagSignature);
        duplicate_tag.value        = make_bytes(destination->arena(),
                                                duplicate_first_tag);
        duplicate_tag.origin.block = first_tag_origin.block;
        duplicate_tag.origin.order_in_block = first_tag_origin.order_in_block;
        duplicate_tag.flags                 = EntryFlags::None;
        if (destination->add_entry(duplicate_tag) == kInvalidEntryId) {
            return false;
        }

        Entry empty_tag;
        empty_tag.key   = make_icc_tag_key(kEmptyTagSignature);
        empty_tag.flags = EntryFlags::None;
        if (destination->add_entry(empty_tag) == kInvalidEntryId) {
            return false;
        }

        Entry malformed_tag;
        malformed_tag.key        = make_icc_tag_key(kMalformedTagSignature);
        malformed_tag.value.kind = MetaValueKind::Array;
        malformed_tag.value.elem_type = MetaElementType::U32;
        malformed_tag.value.count     = 2U;
        malformed_tag.value.data.span = destination->arena().append(
            std::as_bytes(std::span(malformed_payload)));
        malformed_tag.flags = EntryFlags::None;
        if (malformed_tag.value.data.span.size != sizeof(malformed_payload[0U])
            || destination->add_entry(malformed_tag) == kInvalidEntryId) {
            return false;
        }

        destination->finalize();
        return true;
    }

    static void
    expect_profile_structure_and_readback(std::span<const std::byte> profile)
    {
        ASSERT_EQ(profile.size(), 188U);
        ASSERT_GE(profile.size(), 156U);
        EXPECT_EQ(read_u32_be(profile, 0U), profile.size());
        EXPECT_EQ(profile[36U], std::byte { 'a' });
        EXPECT_EQ(profile[37U], std::byte { 'c' });
        EXPECT_EQ(profile[38U], std::byte { 's' });
        EXPECT_EQ(profile[39U], std::byte { 'p' });
        EXPECT_EQ(read_u32_be(profile, 80U), kNewCreator);
        EXPECT_EQ(read_u32_be(profile, 68U), 63190U);
        EXPECT_EQ(read_u32_be(profile, 72U), 65536U);
        EXPECT_EQ(read_u32_be(profile, 76U), 54061U);
        for (size_t index = 84U; index < 100U; ++index) {
            EXPECT_EQ(profile[index], std::byte { 0U });
        }

        ASSERT_EQ(read_u32_be(profile, 128U), 2U);
        const size_t first_record  = 132U;
        const size_t second_record = 144U;
        EXPECT_EQ(read_u32_be(profile, first_record), kAddedTagSignature);
        EXPECT_EQ(read_u32_be(profile, second_record), kFirstTagSignature);
        const uint32_t added_offset   = read_u32_be(profile, first_record + 4U);
        const uint32_t added_size     = read_u32_be(profile, first_record + 8U);
        const uint32_t changed_offset = read_u32_be(profile,
                                                    second_record + 4U);
        const uint32_t changed_size = read_u32_be(profile, second_record + 8U);
        ASSERT_EQ(added_size, kAddedTag.size());
        ASSERT_EQ(changed_size, kEditedFirstTag.size());
        EXPECT_EQ(added_offset, 156U);
        EXPECT_EQ(changed_offset, 172U);
        EXPECT_EQ(added_offset % 4U, 0U);
        EXPECT_EQ(changed_offset % 4U, 0U);
        ASSERT_LE(static_cast<size_t>(added_offset), profile.size());
        ASSERT_LE(static_cast<size_t>(changed_offset), profile.size());
        ASSERT_LE(static_cast<size_t>(added_size),
                  profile.size() - static_cast<size_t>(added_offset));
        ASSERT_LE(static_cast<size_t>(changed_size),
                  profile.size() - static_cast<size_t>(changed_offset));
        EXPECT_TRUE(std::equal(kAddedTag.begin(), kAddedTag.end(),
                               profile.begin() + added_offset));
        EXPECT_TRUE(std::equal(kEditedFirstTag.begin(), kEditedFirstTag.end(),
                               profile.begin() + changed_offset));

        MetaStore decoded;
        const IccDecodeResult decoded_result = decode_icc_profile(profile,
                                                                  decoded);
        ASSERT_EQ(decoded_result.status, IccDecodeStatus::Ok);
        decoded.finalize();
        const std::span<const EntryId> creator_ids = decoded.find_all(
            make_icc_header_field_key_view(80U));
        ASSERT_EQ(creator_ids.size(), 1U);
        const Entry& creator = decoded.entry(creator_ids.front());
        ASSERT_EQ(creator.value.kind, MetaValueKind::Scalar);
        ASSERT_EQ(creator.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(creator.value.data.u64, kNewCreator);

        const std::span<const EntryId> illuminant_ids = decoded.find_all(
            make_icc_header_field_key_view(68U));
        ASSERT_EQ(illuminant_ids.size(), 1U);
        const Entry& illuminant = decoded.entry(illuminant_ids.front());
        ASSERT_EQ(illuminant.value.kind, MetaValueKind::Array);
        ASSERT_EQ(illuminant.value.elem_type, MetaElementType::SRational);
        ASSERT_EQ(illuminant.value.count, 3U);
        std::array<SRational, 3U> values {};
        const std::span<const std::byte> illuminant_bytes
            = decoded.arena().span(illuminant.value.data.span);
        ASSERT_EQ(illuminant_bytes.size(), sizeof(values));
        std::memcpy(values.data(), illuminant_bytes.data(), sizeof(values));
        EXPECT_EQ(values[0].numer, 63190);
        EXPECT_EQ(values[0].denom, 65536);
        EXPECT_EQ(values[1].numer, 65536);
        EXPECT_EQ(values[1].denom, 65536);
        EXPECT_EQ(values[2].numer, 54061);
        EXPECT_EQ(values[2].denom, 65536);
        EXPECT_TRUE(
            store_has_icc_bytes(decoded, kAddedTagSignature, kAddedTag));
        EXPECT_TRUE(
            store_has_icc_bytes(decoded, kFirstTagSignature, kEditedFirstTag));
        EXPECT_TRUE(
            decoded.find_all(make_icc_tag_key_view(kRemovedTagSignature))
                .empty());
    }

    static void expect_container_profile_matches(
        std::span<const std::byte> container,
        std::span<const std::byte> expected_profile)
    {
        std::vector<std::byte> extracted;
        ASSERT_TRUE(extract_icc_profile(container, &extracted));
        ASSERT_EQ(extracted.size(), expected_profile.size());
        EXPECT_TRUE(std::equal(extracted.begin(), extracted.end(),
                               expected_profile.begin()));
        expect_profile_structure_and_readback(extracted);
    }

    static const PreparedTransferBlock*
    find_icc_transfer_block(const PreparedTransferBundle& bundle) noexcept
    {
        for (const PreparedTransferBlock& block : bundle.blocks) {
            if (block.kind == TransferBlockKind::Icc) {
                return &block;
            }
        }
        return nullptr;
    }

    static void
    expect_jpeg_icc_payload(const PreparedTransferBlock& block,
                            std::span<const std::byte> expected_profile)
    {
        static constexpr std::array<std::byte, 12U> signature = {
            std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
            std::byte { '_' }, std::byte { 'P' }, std::byte { 'R' },
            std::byte { 'O' }, std::byte { 'F' }, std::byte { 'I' },
            std::byte { 'L' }, std::byte { 'E' }, std::byte { 0U },
        };
        ASSERT_EQ(block.payload.size(), expected_profile.size() + 14U);
        EXPECT_TRUE(std::equal(signature.begin(), signature.end(),
                               block.payload.begin()));
        EXPECT_EQ(block.payload[12U], std::byte { 1U });
        EXPECT_EQ(block.payload[13U], std::byte { 1U });
        EXPECT_TRUE(std::equal(expected_profile.begin(), expected_profile.end(),
                               block.payload.begin() + 14U));
    }

    TEST(MetadataIccTransfer,
         FreshAuthoredEditedProfileRoundTripsSupportedCarriers)
    {
        test::IccAuthoringFixtureData fixture_data;
        const std::array<MetadataAuthoringEntry, 10U> authored_entries
            = test::icc_authoring_fixture_entries(fixture_data);
        MetaStore authored;
        const MetadataAuthoringResult authoring
            = create_metadata_store(authored_entries, &authored);
        ASSERT_TRUE(authoring.ok())
            << metadata_authoring_status_name(authoring.status);

        std::array<MetadataTypedEditingOperation, 4U> operations {};
        operations[0].kind        = MetadataEditingOperationKind::Set;
        operations[0].entry.key   = make_icc_header_field_key_view(80U);
        operations[0].entry.value = make_value_view_u32(kNewCreator);
        operations[1].kind        = MetadataEditingOperationKind::Set;
        operations[1].entry.key   = make_icc_tag_key_view(kFirstTagSignature);
        operations[1].entry.value = make_value_view_bytes(kEditedFirstTag);
        operations[2].kind        = MetadataEditingOperationKind::Remove;
        operations[2].entry.key   = make_icc_tag_key_view(kRemovedTagSignature);
        operations[3].kind        = MetadataEditingOperationKind::Add;
        operations[3].entry.key   = make_icc_tag_key_view(kAddedTagSignature);
        operations[3].entry.value = make_value_view_bytes(kAddedTag);
        MetaStore edited;
        const MetadataTypedEditingResult editing
            = edit_metadata_typed(authored, operations, &edited);
        ASSERT_TRUE(editing.ok())
            << metadata_typed_editing_status_name(editing.status);
        EXPECT_EQ(editing.entries_updated, 2U);
        EXPECT_EQ(editing.entries_removed, 1U);
        EXPECT_EQ(editing.entries_added, 1U);

        const std::span<const EntryId> source_creator_ids = authored.find_all(
            make_icc_header_field_key_view(80U));
        ASSERT_EQ(source_creator_ids.size(), 1U);
        EXPECT_EQ(authored.entry(source_creator_ids.front()).value.data.u64,
                  0x4F4D5441U);
        EXPECT_TRUE(store_has_icc_bytes(authored, kFirstTagSignature,
                                        fixture_data.first_tag));
        EXPECT_TRUE(store_has_icc_bytes(authored, kRemovedTagSignature,
                                        fixture_data.second_tag));

        const IccProfileSerializeResult measured = serialize_icc_profile(edited,
                                                                         {});
        ASSERT_EQ(measured.status, IccProfileSerializeStatus::OutputTruncated);
        ASSERT_GT(measured.needed, 0U);
        std::vector<std::byte> canonical_profile(
            static_cast<size_t>(measured.needed));
        const IccProfileSerializeResult serialized
            = serialize_icc_profile(edited, canonical_profile);
        ASSERT_TRUE(serialized.ok())
            << icc_profile_serialize_status_name(serialized.status);
        ASSERT_EQ(serialized.written, canonical_profile.size());
        expect_profile_structure_and_readback(canonical_profile);

        PrepareTransferRequest jpeg_request;
        jpeg_request.target_format      = TransferTargetFormat::Jpeg;
        jpeg_request.include_exif_app1  = false;
        jpeg_request.include_xmp_app1   = false;
        jpeg_request.include_iptc_app13 = false;
        PreparedTransferBundle jpeg_bundle;
        ASSERT_EQ(prepare_metadata_for_target(edited, jpeg_request, &jpeg_bundle)
                      .status,
                  TransferStatus::Ok);
        const PreparedTransferBlock* jpeg_icc = find_icc_transfer_block(
            jpeg_bundle);
        ASSERT_NE(jpeg_icc, nullptr);
        expect_jpeg_icc_payload(*jpeg_icc, canonical_profile);

        const JpegFixture jpeg_input = make_jpeg_fixture();
        const JpegEditPlan jpeg_plan
            = plan_prepared_bundle_jpeg_edit(jpeg_input.bytes, jpeg_bundle);
        ASSERT_EQ(jpeg_plan.status, TransferStatus::Ok);
        std::vector<std::byte> jpeg_output;
        const EmitTransferResult jpeg_applied
            = apply_prepared_bundle_jpeg_edit(jpeg_input.bytes, jpeg_bundle,
                                              jpeg_plan, &jpeg_output);
        ASSERT_EQ(jpeg_applied.status, TransferStatus::Ok);
        ASSERT_GE(jpeg_output.size(),
                  jpeg_input.bytes.size() - jpeg_input.scan_offset);
        const std::span<const std::byte> comment_segment(
            jpeg_input.bytes.data() + 2U, jpeg_input.scan_offset - 2U);
        const auto comment_position
            = std::search(jpeg_output.begin(), jpeg_output.end(),
                          comment_segment.begin(), comment_segment.end());
        EXPECT_NE(comment_position, jpeg_output.end());
        EXPECT_TRUE(std::equal(
            jpeg_input.bytes.begin()
                + static_cast<std::ptrdiff_t>(jpeg_input.scan_offset),
            jpeg_input.bytes.end(),
            jpeg_output.end()
                - static_cast<std::ptrdiff_t>(jpeg_input.bytes.size()
                                              - jpeg_input.scan_offset)));
        expect_container_profile_matches(jpeg_output, canonical_profile);

        constexpr std::array<TiffFixtureVariant, 4U> tiff_variants = { {
            { false, false, false },
            { false, true, false },
            { true, false, false },
            { true, true, false },
        } };
        for (uint32_t target_index = 0U; target_index < 2U; ++target_index) {
            const bool is_dng = target_index != 0U;
            SCOPED_TRACE(is_dng ? "DNG target" : "TIFF target");
            PrepareTransferRequest tiff_request = jpeg_request;
            tiff_request.target_format = is_dng ? TransferTargetFormat::Dng
                                                : TransferTargetFormat::Tiff;
            if (is_dng) {
                tiff_request.dng_target_mode = DngTargetMode::ExistingTarget;
            }
            PreparedTransferBundle tiff_bundle;
            ASSERT_EQ(prepare_metadata_for_target(edited, tiff_request,
                                                  &tiff_bundle)
                          .status,
                      TransferStatus::Ok);
            const PreparedTransferBlock* tiff_icc = find_icc_transfer_block(
                tiff_bundle);
            ASSERT_NE(tiff_icc, nullptr);
            EXPECT_EQ(tiff_icc->payload, canonical_profile);

            for (uint32_t variant_index = 0U;
                 variant_index < tiff_variants.size(); ++variant_index) {
                SCOPED_TRACE(variant_index);
                TiffFixtureVariant variant = tiff_variants[variant_index];
                variant.dng                = is_dng;
                const std::vector<std::byte> tiff_input = make_tiff_fixture(
                    variant);
                const std::vector<std::byte> tiff_original = tiff_input;
                const TiffEditPlan tiff_plan
                    = plan_prepared_bundle_tiff_edit(tiff_input, tiff_bundle);
                ASSERT_EQ(tiff_plan.status, TransferStatus::Ok);
                std::vector<std::byte> tiff_output;
                const EmitTransferResult tiff_applied
                    = apply_prepared_bundle_tiff_edit(tiff_input, tiff_bundle,
                                                      tiff_plan, &tiff_output);
                ASSERT_EQ(tiff_applied.status, TransferStatus::Ok);
                EXPECT_EQ(tiff_input, tiff_original);
                expect_container_profile_matches(tiff_output,
                                                 canonical_profile);
                expect_tiff_media_and_icc(tiff_output, variant,
                                          canonical_profile);
            }
        }

        ExecutePreparedTransferOptions container_edit_options;
        container_edit_options.edit_requested = true;
        container_edit_options.edit_apply     = true;
#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
        PrepareTransferRequest png_request = jpeg_request;
        png_request.target_format          = TransferTargetFormat::Png;
        PreparedTransferBundle png_bundle;
        ASSERT_EQ(prepare_metadata_for_target(edited, png_request, &png_bundle)
                      .status,
                  TransferStatus::Ok);
        const PreparedTransferBlock* png_icc = find_icc_transfer_block(
            png_bundle);
        ASSERT_NE(png_icc, nullptr);
        expect_png_icc_prepared_payload(*png_icc, canonical_profile);

        const std::vector<std::byte> png_input = make_png_fixture();
        std::span<const std::byte> input_idat;
        std::span<const std::byte> input_text;
        ASSERT_TRUE(
            find_png_chunk(png_input, fourcc('I', 'D', 'A', 'T'), &input_idat));
        ASSERT_TRUE(
            find_png_chunk(png_input, fourcc('t', 'E', 'X', 't'), &input_text));
        std::span<const std::byte> input_ihdr;
        ASSERT_TRUE(
            find_png_chunk(png_input, fourcc('I', 'H', 'D', 'R'), &input_ihdr));
        ASSERT_EQ(input_ihdr.size(), 13U);
        EXPECT_EQ(read_u32_be(input_ihdr, 0U), 2U);
        EXPECT_EQ(read_u32_be(input_ihdr, 4U), 2U);
        EXPECT_EQ(input_ihdr[8U], std::byte { 8U });
        EXPECT_EQ(input_ihdr[9U], std::byte { 0U });

        const ExecutePreparedTransferResult png_edit
            = execute_prepared_transfer(&png_bundle, png_input,
                                        container_edit_options);
        ASSERT_EQ(png_edit.edit_plan_status, TransferStatus::Ok);
        ASSERT_EQ(png_edit.edit_apply.status, TransferStatus::Ok);
        const std::span<const std::byte> png_output(png_edit.edited_output);
        std::span<const std::byte> output_idat;
        std::span<const std::byte> output_text;
        std::span<const std::byte> output_ihdr;
        ASSERT_TRUE(find_png_chunk(png_output, fourcc('I', 'D', 'A', 'T'),
                                   &output_idat));
        ASSERT_TRUE(find_png_chunk(png_output, fourcc('t', 'E', 'X', 't'),
                                   &output_text));
        ASSERT_TRUE(find_png_chunk(png_output, fourcc('I', 'H', 'D', 'R'),
                                   &output_ihdr));
        ASSERT_EQ(output_idat.size(), input_idat.size());
        ASSERT_EQ(output_text.size(), input_text.size());
        ASSERT_EQ(output_ihdr.size(), input_ihdr.size());
        EXPECT_TRUE(std::equal(input_idat.begin(), input_idat.end(),
                               output_idat.begin()));
        EXPECT_TRUE(std::equal(input_text.begin(), input_text.end(),
                               output_text.begin()));
        EXPECT_TRUE(std::equal(input_ihdr.begin(), input_ihdr.end(),
                               output_ihdr.begin()));
        expect_container_profile_matches(png_output, canonical_profile);
#endif

        PrepareTransferRequest webp_request = jpeg_request;
        webp_request.target_format          = TransferTargetFormat::Webp;
        PreparedTransferBundle webp_bundle;
        ASSERT_EQ(prepare_metadata_for_target(edited, webp_request, &webp_bundle)
                      .status,
                  TransferStatus::Ok);
        const PreparedTransferBlock* webp_icc = find_icc_transfer_block(
            webp_bundle);
        ASSERT_NE(webp_icc, nullptr);
        EXPECT_EQ(webp_icc->payload, canonical_profile);

        const std::vector<std::byte> webp_input = make_webp_fixture();
        const ExecutePreparedTransferResult webp_edit
            = execute_prepared_transfer(&webp_bundle, webp_input,
                                        container_edit_options);
        ASSERT_EQ(webp_edit.edit_plan_status, TransferStatus::Ok);
        ASSERT_EQ(webp_edit.edit_apply.status, TransferStatus::Ok);
        const std::span<const std::byte> webp_output(webp_edit.edited_output);
        std::span<const std::byte> vp8x;
        std::span<const std::byte> vp8l;
        std::span<const std::byte> free_chunk;
        ASSERT_TRUE(
            find_webp_chunk(webp_output, fourcc('V', 'P', '8', 'X'), &vp8x));
        ASSERT_TRUE(
            find_webp_chunk(webp_output, fourcc('V', 'P', '8', 'L'), &vp8l));
        ASSERT_TRUE(find_webp_chunk(webp_output, fourcc('F', 'R', 'E', 'E'),
                                    &free_chunk));
        ASSERT_EQ(vp8x.size(), 10U);
        EXPECT_NE(std::to_integer<uint8_t>(vp8x[0U]) & 0x20U, 0U);
        EXPECT_EQ(read_tiff_uint(vp8x, 4U, 3U, false) + 1U, 2U);
        EXPECT_EQ(read_tiff_uint(vp8x, 7U, 3U, false) + 1U, 2U);
        ASSERT_EQ(vp8l.size(), kWebpVp8l.size());
        ASSERT_EQ(free_chunk.size(), kWebpFree.size());
        EXPECT_TRUE(
            std::equal(kWebpVp8l.begin(), kWebpVp8l.end(), vp8l.begin()));
        EXPECT_TRUE(
            std::equal(kWebpFree.begin(), kWebpFree.end(), free_chunk.begin()));
        expect_container_profile_matches(webp_output, canonical_profile);
    }

    TEST(MetadataIccTransfer,
         CleanJxlPreservesProfileIdAndRemoveOnlySwitchesToStrictClearId)
    {
        test::IccAuthoringFixtureData fixture_data;
        const std::array<MetadataAuthoringEntry, 10U> authored_entries
            = test::icc_authoring_fixture_entries(fixture_data);
        MetaStore authored;
        ASSERT_TRUE(create_metadata_store(authored_entries, &authored).ok());

        IccProfileSerializeOptions preserve_id;
        preserve_id.profile_id_policy = IccProfileIdPolicy::Preserve;
        const IccProfileSerializeResult source_measure
            = serialize_icc_profile(authored, {}, preserve_id);
        ASSERT_EQ(source_measure.status,
                  IccProfileSerializeStatus::OutputTruncated);
        std::vector<std::byte> source_profile(
            static_cast<size_t>(source_measure.needed));
        const IccProfileSerializeResult source_serialized
            = serialize_icc_profile(authored, source_profile, preserve_id);
        ASSERT_TRUE(source_serialized.ok());
        ASSERT_EQ(source_profile.size(), 184U);
        ASSERT_GE(source_profile.size(), 156U);
        EXPECT_EQ(read_u32_be(source_profile, 0U), source_profile.size());
        EXPECT_EQ(read_u32_be(source_profile, 68U), 63190U);
        EXPECT_EQ(read_u32_be(source_profile, 72U), 65536U);
        EXPECT_EQ(read_u32_be(source_profile, 76U), 54061U);
        EXPECT_EQ(read_u32_be(source_profile, 80U), 0x4F4D5441U);
        EXPECT_TRUE(std::equal(fixture_data.profile_id.begin(),
                               fixture_data.profile_id.end(),
                               source_profile.begin() + 84U));

        MetaStore clean_store;
        const IccDecodeResult source_decoded
            = decode_icc_profile(source_profile, clean_store);
        ASSERT_EQ(source_decoded.status, IccDecodeStatus::Ok);
        clean_store.finalize();
        for (const Entry& entry : clean_store.entries()) {
            EXPECT_FALSE(any(entry.flags, EntryFlags::Dirty));
        }

        PrepareTransferRequest jxl_request;
        jxl_request.target_format      = TransferTargetFormat::Jxl;
        jxl_request.include_exif_app1  = false;
        jxl_request.include_xmp_app1   = false;
        jxl_request.include_iptc_app13 = false;
        PreparedTransferBundle clean_bundle;
        ASSERT_EQ(prepare_metadata_for_target(clean_store, jxl_request,
                                              &clean_bundle)
                      .status,
                  TransferStatus::Ok);
        const PreparedTransferBlock* clean_icc = find_icc_transfer_block(
            clean_bundle);
        ASSERT_NE(clean_icc, nullptr);
        EXPECT_EQ(clean_icc->route, "jxl:icc-profile");
        const std::span<const std::byte> clean_profile(clean_icc->payload);
        ASSERT_EQ(clean_profile.size(), 181U);
        ASSERT_GE(clean_profile.size(), 156U);
        EXPECT_EQ(read_u32_be(clean_profile, 0U), clean_profile.size());
        EXPECT_EQ(read_u32_be(clean_profile, 68U), 63190U);
        EXPECT_EQ(read_u32_be(clean_profile, 72U), 65536U);
        EXPECT_EQ(read_u32_be(clean_profile, 76U), 54061U);
        EXPECT_EQ(read_u32_be(clean_profile, 80U), 0x4F4D5441U);
        EXPECT_TRUE(std::equal(fixture_data.profile_id.begin(),
                               fixture_data.profile_id.end(),
                               clean_profile.begin() + 84U));
        ASSERT_EQ(read_u32_be(clean_profile, 128U), 2U);

        MetaStore compatibility_store;
        ASSERT_TRUE(clone_clean_icc_store_with_legacy_edge_entries(
            clean_store, &compatibility_store));
        for (const Entry& entry : compatibility_store.entries()) {
            EXPECT_EQ(entry.flags, EntryFlags::None);
        }
        EXPECT_EQ(compatibility_store
                      .find_all(make_icc_header_field_key_view(80U))
                      .size(),
                  2U);
        EXPECT_EQ(compatibility_store
                      .find_all(make_icc_tag_key_view(kFirstTagSignature))
                      .size(),
                  2U);

        PreparedTransferBundle compatibility_bundle;
        const PrepareTransferResult compatibility_preparation
            = prepare_metadata_for_target(compatibility_store, jxl_request,
                                          &compatibility_bundle);
        ASSERT_EQ(compatibility_preparation.status, TransferStatus::Ok);
        const PreparedTransferBlock* compatibility_icc
            = find_icc_transfer_block(compatibility_bundle);
        ASSERT_NE(compatibility_icc, nullptr);
        ASSERT_EQ(compatibility_icc->payload.size(), 181U);
        EXPECT_EQ(compatibility_icc->payload, clean_icc->payload);
        EXPECT_GT(compatibility_preparation.warnings, 0U);
        EXPECT_NE(compatibility_preparation.message.find(
                      "icc serializer skipped 2 unsupported icc entries"),
                  std::string::npos);

        std::array<MetadataTypedEditingOperation, 1U> remove_one {};
        remove_one[0U].kind      = MetadataEditingOperationKind::Remove;
        remove_one[0U].entry.key = make_icc_tag_key_view(kRemovedTagSignature);
        MetaStore removed_only;
        const MetadataTypedEditingResult removal
            = edit_metadata_typed(clean_store, remove_one, &removed_only);
        ASSERT_TRUE(removal.ok())
            << metadata_typed_editing_status_name(removal.status);
        ASSERT_EQ(removal.entries_removed, 1U);
        bool saw_dirty_tombstone = false;
        for (const Entry& entry : removed_only.entries()) {
            if (entry.key.kind == MetaKeyKind::IccTag
                && entry.key.data.icc_tag.signature == kRemovedTagSignature
                && any(entry.flags, EntryFlags::Deleted)
                && any(entry.flags, EntryFlags::Dirty)) {
                saw_dirty_tombstone = true;
            }
        }
        ASSERT_TRUE(saw_dirty_tombstone);

        PreparedTransferBundle strict_bundle;
        ASSERT_EQ(prepare_metadata_for_target(removed_only, jxl_request,
                                              &strict_bundle)
                      .status,
                  TransferStatus::Ok);
        const PreparedTransferBlock* strict_icc = find_icc_transfer_block(
            strict_bundle);
        ASSERT_NE(strict_icc, nullptr);
        EXPECT_EQ(strict_icc->route, "jxl:icc-profile");

        const IccProfileSerializeResult strict_measure
            = serialize_icc_profile(removed_only, {});
        ASSERT_EQ(strict_measure.status,
                  IccProfileSerializeStatus::OutputTruncated);
        ASSERT_EQ(strict_measure.needed, 160U);
        std::vector<std::byte> strict_profile(
            static_cast<size_t>(strict_measure.needed));
        const IccProfileSerializeResult strict_serialized
            = serialize_icc_profile(removed_only, strict_profile);
        ASSERT_TRUE(strict_serialized.ok());
        EXPECT_EQ(strict_icc->payload, strict_profile);
        ASSERT_EQ(strict_profile.size(), 160U);
        EXPECT_EQ(read_u32_be(strict_profile, 0U), strict_profile.size());
        EXPECT_EQ(read_u32_be(strict_profile, 68U), 63190U);
        EXPECT_EQ(read_u32_be(strict_profile, 72U), 65536U);
        EXPECT_EQ(read_u32_be(strict_profile, 76U), 54061U);
        EXPECT_EQ(read_u32_be(strict_profile, 80U), 0x4F4D5441U);
        for (size_t index = 84U; index < 100U; ++index) {
            EXPECT_EQ(strict_profile[index], std::byte { 0U });
        }
        ASSERT_EQ(read_u32_be(strict_profile, 128U), 1U);
        EXPECT_EQ(read_u32_be(strict_profile, 132U), kFirstTagSignature);
        const uint32_t tag_offset = read_u32_be(strict_profile, 136U);
        const uint32_t tag_size   = read_u32_be(strict_profile, 140U);
        ASSERT_EQ(tag_offset, 144U);
        ASSERT_EQ(tag_size, fixture_data.first_tag.size());
        ASSERT_LE(static_cast<size_t>(tag_offset), strict_profile.size());
        ASSERT_LE(static_cast<size_t>(tag_size),
                  strict_profile.size() - static_cast<size_t>(tag_offset));
        EXPECT_TRUE(std::equal(fixture_data.first_tag.begin(),
                               fixture_data.first_tag.end(),
                               strict_profile.begin() + tag_offset));

        std::array<MetadataTypedEditingOperation, 1U> remove_last {};
        remove_last[0U].kind      = MetadataEditingOperationKind::Remove;
        remove_last[0U].entry.key = make_icc_tag_key_view(kFirstTagSignature);
        MetaStore empty_carrier_candidate;
        const MetadataTypedEditingResult remove_last_result
            = edit_metadata_typed(removed_only, remove_last,
                                  &empty_carrier_candidate);
        ASSERT_TRUE(remove_last_result.ok());
        PrepareTransferRequest empty_carrier_request = jxl_request;
        PreparedTransferBundle empty_carrier_bundle;
        const PrepareTransferResult empty_carrier_preparation
            = prepare_metadata_for_target(empty_carrier_candidate,
                                          empty_carrier_request,
                                          &empty_carrier_bundle);
        EXPECT_EQ(empty_carrier_preparation.status,
                  TransferStatus::Unsupported);
        EXPECT_EQ(empty_carrier_preparation.code,
                  PrepareTransferCode::IccPackFailed);
        EXPECT_EQ(find_icc_transfer_block(empty_carrier_bundle), nullptr);

        static constexpr std::array<uint32_t, 17U> kIccHeaderOffsets = {
            0U,  4U,  8U,  12U, 16U, 20U, 24U, 36U, 40U,
            44U, 48U, 52U, 56U, 64U, 68U, 80U, 84U,
        };
        std::array<MetadataTypedEditingOperation, kIccHeaderOffsets.size()>
            remove_headers {};
        for (size_t index = 0U; index < kIccHeaderOffsets.size(); ++index) {
            remove_headers[index].kind = MetadataEditingOperationKind::Remove;
            remove_headers[index].entry.key = make_icc_header_field_key_view(
                kIccHeaderOffsets[index]);
        }
        MetaStore only_tombstones;
        const MetadataTypedEditingResult remove_headers_result
            = edit_metadata_typed(empty_carrier_candidate, remove_headers,
                                  &only_tombstones);
        ASSERT_TRUE(remove_headers_result.ok())
            << metadata_typed_editing_status_name(remove_headers_result.status);
        ASSERT_EQ(remove_headers_result.entries_removed,
                  kIccHeaderOffsets.size());
        ASSERT_EQ(only_tombstones.entries().size(),
                  empty_carrier_candidate.entries().size());
        ASSERT_FALSE(only_tombstones.entries().empty());
        for (const Entry& entry : only_tombstones.entries()) {
            EXPECT_TRUE(entry.key.kind == MetaKeyKind::IccHeaderField
                        || entry.key.kind == MetaKeyKind::IccTag);
            EXPECT_TRUE(any(entry.flags, EntryFlags::Deleted));
            EXPECT_TRUE(any(entry.flags, EntryFlags::Dirty));
        }

        PreparedTransferBundle only_tombstones_bundle;
        const PrepareTransferResult only_tombstones_preparation
            = prepare_metadata_for_target(only_tombstones, jxl_request,
                                          &only_tombstones_bundle);
        EXPECT_EQ(only_tombstones_preparation.status,
                  TransferStatus::Unsupported);
        EXPECT_EQ(only_tombstones_preparation.code,
                  PrepareTransferCode::IccPackFailed);
        EXPECT_EQ(find_icc_transfer_block(only_tombstones_bundle), nullptr);
    }

}  // namespace
}  // namespace openmeta
