// SPDX-License-Identifier: Apache-2.0

#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static void append_bytes(std::vector<std::byte>* out, std::string_view text)
    {
        for (const char c : text) {
            out->push_back(std::byte { static_cast<uint8_t>(c) });
        }
    }

    static void append_u16le(std::vector<std::byte>* out, uint16_t value)
    {
        out->push_back(std::byte { static_cast<uint8_t>(value & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
    }

    static void append_u16be(std::vector<std::byte>* out, uint16_t value)
    {
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>(value & 0xFFU) });
    }

    static void append_u32le(std::vector<std::byte>* out, uint32_t value)
    {
        out->push_back(std::byte { static_cast<uint8_t>(value & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 16U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 24U) & 0xFFU) });
    }

    static void append_u32be(std::vector<std::byte>* out, uint32_t value)
    {
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 24U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 16U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>(value & 0xFFU) });
    }

    static void append_fourcc(std::vector<std::byte>* out, uint32_t value)
    {
        append_u32be(out, value);
    }

    static void append_fullbox_header(std::vector<std::byte>* out,
                                      uint8_t version)
    {
        out->push_back(std::byte { version });
        out->push_back(std::byte { 0x00 });
        out->push_back(std::byte { 0x00 });
        out->push_back(std::byte { 0x00 });
    }

    static void append_box(std::vector<std::byte>* out, uint32_t type,
                           std::span<const std::byte> payload)
    {
        append_u32be(out, static_cast<uint32_t>(8U + payload.size()));
        append_fourcc(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static void append_ifd_entry(std::vector<std::byte>* out, uint16_t tag,
                                 uint16_t type, uint32_t count, uint32_t value)
    {
        append_u16le(out, tag);
        append_u16le(out, type);
        append_u32le(out, count);
        append_u32le(out, value);
    }

    static std::vector<std::byte> make_sony_tiff(std::string_view model)
    {
        constexpr std::string_view kMake = "SONY";
        constexpr uint32_t kEntryCount   = 3U;
        constexpr uint32_t kDataOffset   = 8U + 2U + (12U * kEntryCount) + 4U;

        const uint32_t make_offset = kDataOffset;
        const uint32_t model_offset
            = make_offset + static_cast<uint32_t>(kMake.size()) + 1U;

        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42U);
        append_u32le(&tiff, 8U);
        append_u16le(&tiff, static_cast<uint16_t>(kEntryCount));
        append_ifd_entry(&tiff, 0x010FU, 2U,
                         static_cast<uint32_t>(kMake.size()) + 1U, make_offset);
        append_ifd_entry(&tiff, 0x0110U, 2U,
                         static_cast<uint32_t>(model.size()) + 1U,
                         model_offset);
        append_ifd_entry(&tiff, 0x0112U, 3U, 1U, 6U);
        append_u32le(&tiff, 0U);
        append_bytes(&tiff, kMake);
        tiff.push_back(std::byte { 0x00 });
        append_bytes(&tiff, model);
        tiff.push_back(std::byte { 0x00 });
        return tiff;
    }

    static std::vector<std::byte> make_exif_item_payload(std::string_view model)
    {
        std::vector<std::byte> payload;
        append_u32be(&payload, 0U);
        const std::vector<std::byte> tiff = make_sony_tiff(model);
        payload.insert(payload.end(), tiff.begin(), tiff.end());
        return payload;
    }

    static std::vector<std::byte> make_avif()
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2U);
        append_u16be(&infe_payload, 1U);
        append_u16be(&infe_payload, 0U);
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "Exif");
        infe_payload.push_back(std::byte { 0x00 });

        std::vector<std::byte> infe_box;
        append_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2U);
        append_u32be(&iinf_payload, 1U);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        const std::vector<std::byte> idat_payload = make_exif_item_payload(
            "Synthetic AVIF");
        std::vector<std::byte> idat_box;
        append_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1U);
        iloc_payload.push_back(std::byte { 0x44 });
        iloc_payload.push_back(std::byte { 0x00 });
        append_u16be(&iloc_payload, 1U);
        append_u16be(&iloc_payload, 1U);
        append_u16be(&iloc_payload, 1U);
        append_u16be(&iloc_payload, 0U);
        append_u16be(&iloc_payload, 1U);
        append_u32be(&iloc_payload, 0U);
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0U);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('a', 'v', 'i', 'f'));
        append_u32be(&ftyp_payload, 0U);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));

        std::vector<std::byte> file;
        append_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());
        return file;
    }

    static std::vector<std::byte> make_gif()
    {
        std::vector<std::byte> gif;
        append_bytes(&gif, "GIF89a");
        gif.insert(gif.end(),
                   { std::byte { 0x01 }, std::byte { 0x00 }, std::byte { 0x01 },
                     std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 0x00 }, std::byte { 0x21 }, std::byte { 0xFE },
                     std::byte { 0x0D } });
        append_bytes(&gif, "OpenMeta lane");
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x3B });
        return gif;
    }

    static std::vector<std::byte> make_jxl()
    {
        std::vector<std::byte> jxl;
        append_u32be(&jxl, 12U);
        append_fourcc(&jxl, fourcc('J', 'X', 'L', ' '));
        append_u32be(&jxl, 0x0D0A870AU);
        const std::vector<std::byte> exif = make_exif_item_payload(
            "Synthetic JXL");
        append_box(&jxl, fourcc('E', 'x', 'i', 'f'), exif);
        return jxl;
    }

    static void expect_exif_lane(std::span<const std::byte> bytes,
                                 ContainerFormat expected_format)
    {
        MetaStore store;
        std::array<ContainerBlockRef, 16> blocks {};
        std::array<ExifIfdRef, 16> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 32> payload_parts {};
        const SimpleMetaDecodeOptions options {};
        const SimpleMetaResult result
            = simple_meta_read(bytes, store, blocks, ifds, payload,
                               payload_parts, options);

        EXPECT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_GT(result.exif.entries_decoded, 0U);
        EXPECT_FALSE(store.entries().empty());

        bool found_exif = false;
        for (uint32_t i = 0U; i < result.scan.written && i < blocks.size();
             ++i) {
            if (blocks[i].kind == ContainerBlockKind::Exif) {
                EXPECT_EQ(blocks[i].format, expected_format);
                found_exif = true;
            }
        }
        EXPECT_TRUE(found_exif);
    }

    TEST(ReadLaneCoverage, AvifMetadataDecode)
    {
        const std::vector<std::byte> file = make_avif();
        expect_exif_lane(file, ContainerFormat::Avif);
    }

    TEST(ReadLaneCoverage, GifMetadataDecode)
    {
        const std::vector<std::byte> file = make_gif();
        MetaStore store;
        std::array<ContainerBlockRef, 8> blocks {};
        std::array<ExifIfdRef, 4> ifds {};
        std::array<std::byte, 256> payload {};
        std::array<uint32_t, 8> payload_parts {};
        const SimpleMetaDecodeOptions options {};
        const SimpleMetaResult result
            = simple_meta_read(file, store, blocks, ifds, payload,
                               payload_parts, options);

        EXPECT_EQ(result.scan.status, ScanStatus::Ok);
        ASSERT_EQ(result.scan.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Gif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Comment);
        EXPECT_EQ(store.entries().size(), 1U);
    }

    TEST(ReadLaneCoverage, JxlMetadataDecode)
    {
        const std::vector<std::byte> file = make_jxl();
        expect_exif_lane(file, ContainerFormat::Jxl);
    }

    TEST(ReadLaneCoverage, SonySr2CarrierUsesTiffDecode)
    {
        const std::vector<std::byte> file = make_sony_tiff("Synthetic SR2");
        expect_exif_lane(file, ContainerFormat::Tiff);
    }

    TEST(ReadLaneCoverage, SonySrfCarrierUsesTiffDecode)
    {
        const std::vector<std::byte> file = make_sony_tiff("Synthetic SRF");
        expect_exif_lane(file, ContainerFormat::Tiff);
    }

}  // namespace
}  // namespace openmeta
