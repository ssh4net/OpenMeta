// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/container_scan.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta::test_fixture {
namespace detail {

    inline void append_ascii(std::vector<std::byte>* out, std::string_view text)
    {
        for (const char c : text) {
            out->push_back(std::byte { static_cast<uint8_t>(c) });
        }
    }

    inline void append_u16le(std::vector<std::byte>* out, uint16_t value)
    {
        out->push_back(std::byte { static_cast<uint8_t>(value & 0xffU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) });
    }

    inline void append_u16be(std::vector<std::byte>* out, uint16_t value)
    {
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) });
        out->push_back(std::byte { static_cast<uint8_t>(value & 0xffU) });
    }

    inline void append_u32le(std::vector<std::byte>* out, uint32_t value)
    {
        out->push_back(std::byte { static_cast<uint8_t>(value & 0xffU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 16U) & 0xffU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 24U) & 0xffU) });
    }

    inline void append_u32be(std::vector<std::byte>* out, uint32_t value)
    {
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 24U) & 0xffU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 16U) & 0xffU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) });
        out->push_back(std::byte { static_cast<uint8_t>(value & 0xffU) });
    }

    inline void append_jpeg_segment(std::vector<std::byte>* out,
                                    uint16_t marker,
                                    std::span<const std::byte> payload)
    {
        out->push_back(
            std::byte { static_cast<uint8_t>((marker >> 8U) & 0xffU) });
        out->push_back(std::byte { static_cast<uint8_t>(marker & 0xffU) });
        append_u16be(out, static_cast<uint16_t>(payload.size() + 2U));
        out->insert(out->end(), payload.begin(), payload.end());
    }

    inline void write_u32le(std::vector<std::byte>* out, size_t offset,
                            uint32_t value)
    {
        (*out)[offset + 0U] = std::byte { static_cast<uint8_t>(value & 0xffU) };
        (*out)[offset + 1U]
            = std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) };
        (*out)[offset + 2U]
            = std::byte { static_cast<uint8_t>((value >> 16U) & 0xffU) };
        (*out)[offset + 3U]
            = std::byte { static_cast<uint8_t>((value >> 24U) & 0xffU) };
    }

    inline void write_u32be(std::vector<std::byte>* out, size_t offset,
                            uint32_t value)
    {
        (*out)[offset + 0U]
            = std::byte { static_cast<uint8_t>((value >> 24U) & 0xffU) };
        (*out)[offset + 1U]
            = std::byte { static_cast<uint8_t>((value >> 16U) & 0xffU) };
        (*out)[offset + 2U]
            = std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) };
        (*out)[offset + 3U] = std::byte { static_cast<uint8_t>(value & 0xffU) };
    }

    inline void append_box(std::vector<std::byte>* out, uint32_t type,
                           std::span<const std::byte> payload)
    {
        append_u32be(out, static_cast<uint32_t>(8U + payload.size()));
        append_u32be(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    inline std::vector<std::byte> make_nikon_makernote()
    {
        std::vector<std::byte> out;
        append_ascii(&out, "Nikon");
        out.insert(out.end(),
                   { std::byte { 0U }, std::byte { 2U }, std::byte { 0U },
                     std::byte { 0U }, std::byte { 0U } });
        append_ascii(&out, "II");
        append_u16le(&out, 42U);
        append_u32le(&out, 8U);
        append_u16le(&out, 2U);
        append_u16le(&out, 0x0001U);
        append_u16le(&out, 4U);
        append_u32le(&out, 1U);
        append_u32le(&out, 0x01020304U);
        append_u16le(&out, 0x001fU);
        append_u16le(&out, 7U);
        append_u32le(&out, 8U);
        append_u32le(&out, 38U);
        append_u32le(&out, 0U);
        append_ascii(&out, "0101");
        out.insert(out.end(), { std::byte { 1U }, std::byte { 0U },
                                std::byte { 2U }, std::byte { 0U } });
        return out;
    }

    inline std::vector<std::byte> make_tiff(bool dng)
    {
        const std::vector<std::byte> maker_note = make_nikon_makernote();
        constexpr std::string_view make         = "Nikon";
        const uint32_t ifd0_entries             = dng ? 4U : 3U;
        const uint32_t ifd0_offset              = 8U;
        const uint32_t ifd0_size                = 2U + ifd0_entries * 12U + 4U;
        const uint32_t make_offset              = ifd0_offset + ifd0_size;
        const uint32_t exif_offset              = make_offset
                                     + static_cast<uint32_t>(make.size()) + 1U;
        const uint32_t maker_offset = exif_offset + 2U + 12U + 4U;

        std::vector<std::byte> out;
        append_ascii(&out, "II");
        append_u16le(&out, 42U);
        append_u32le(&out, ifd0_offset);
        append_u16le(&out, static_cast<uint16_t>(ifd0_entries));

        append_u16le(&out, 0x010fU);
        append_u16le(&out, 2U);
        append_u32le(&out, static_cast<uint32_t>(make.size()) + 1U);
        append_u32le(&out, make_offset);

        append_u16le(&out, 0x0112U);
        append_u16le(&out, 3U);
        append_u32le(&out, 1U);
        append_u32le(&out, 6U);

        append_u16le(&out, 0x8769U);
        append_u16le(&out, 4U);
        append_u32le(&out, 1U);
        append_u32le(&out, exif_offset);

        if (dng) {
            append_u16le(&out, 0xc612U);
            append_u16le(&out, 1U);
            append_u32le(&out, 4U);
            append_u32le(&out, 0x00000401U);
        }
        append_u32le(&out, 0U);
        append_ascii(&out, make);
        out.push_back(std::byte { 0U });

        append_u16le(&out, 1U);
        append_u16le(&out, 0x927cU);
        append_u16le(&out, 7U);
        append_u32le(&out, static_cast<uint32_t>(maker_note.size()));
        append_u32le(&out, maker_offset);
        append_u32le(&out, 0U);
        out.insert(out.end(), maker_note.begin(), maker_note.end());
        return out;
    }

    inline std::vector<std::byte> make_short_tiff(uint16_t tag, uint16_t value)
    {
        std::vector<std::byte> out;
        append_ascii(&out, "II");
        append_u16le(&out, 42U);
        append_u32le(&out, 8U);
        append_u16le(&out, 1U);
        append_u16le(&out, tag);
        append_u16le(&out, 3U);
        append_u32le(&out, 1U);
        append_u16le(&out, value);
        append_u16le(&out, 0U);
        append_u32le(&out, 0U);
        return out;
    }

    struct JpegMetadataFixture final {
        std::vector<std::byte> bytes;
        uint64_t entropy_offset = 0U;
        uint64_t entropy_size   = 0U;
    };

    inline JpegMetadataFixture make_jpeg_with_exif_xmp()
    {
        JpegMetadataFixture out;
        out.bytes.push_back(std::byte { 0xffU });
        out.bytes.push_back(std::byte { 0xd8U });

        std::vector<std::byte> exif;
        append_ascii(&exif, "Exif");
        exif.push_back(std::byte { 0U });
        exif.push_back(std::byte { 0U });
        const std::vector<std::byte> tiff = make_tiff(false);
        exif.insert(exif.end(), tiff.begin(), tiff.end());
        append_jpeg_segment(&out.bytes, 0xffe1U, exif);

        std::vector<std::byte> xmp;
        append_ascii(&xmp, "http://ns.adobe.com/xap/1.0/");
        xmp.push_back(std::byte { 0U });
        append_ascii(
            &xmp, "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
                  "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
                  "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
                  "xmp:CreatorTool='OpenMeta fixture'/></rdf:RDF></x:xmpmeta>");
        append_jpeg_segment(&out.bytes, 0xffe1U, xmp);

        out.bytes.insert(out.bytes.end(),
                         { std::byte { 0xffU }, std::byte { 0xdaU },
                           std::byte { 0x00U }, std::byte { 0x08U },
                           std::byte { 0x01U }, std::byte { 0x01U },
                           std::byte { 0x00U }, std::byte { 0x00U },
                           std::byte { 0x3fU }, std::byte { 0x00U } });
        out.entropy_offset = out.bytes.size();
        out.entropy_size   = 8192U;
        out.bytes.insert(out.bytes.end(), static_cast<size_t>(out.entropy_size),
                         std::byte { 0x5aU });
        out.bytes.push_back(std::byte { 0xffU });
        out.bytes.push_back(std::byte { 0xd9U });
        return out;
    }

    inline std::vector<std::byte>
    exif_box_payload(std::span<const std::byte> tiff)
    {
        std::vector<std::byte> out;
        append_u32be(&out, 0U);
        out.insert(out.end(), tiff.begin(), tiff.end());
        return out;
    }

    inline void append_webp_chunk(std::vector<std::byte>* out, uint32_t type,
                                  std::span<const std::byte> payload)
    {
        append_u32be(out, type);
        append_u32le(out, static_cast<uint32_t>(payload.size()));
        out->insert(out->end(), payload.begin(), payload.end());
        if ((payload.size() & 1U) != 0U) {
            out->push_back(std::byte { 0U });
        }
    }

    inline uint32_t append_x3f_text(std::vector<std::byte>* chars,
                                    std::string_view text)
    {
        const uint32_t position = static_cast<uint32_t>(chars->size() / 2U);
        for (const char c : text) {
            chars->push_back(std::byte { static_cast<uint8_t>(c) });
            chars->push_back(std::byte { 0U });
        }
        chars->push_back(std::byte { 0U });
        chars->push_back(std::byte { 0U });
        return position;
    }

    inline std::vector<std::byte> make_x3f_property_section()
    {
        std::vector<std::byte> chars;
        const uint32_t name_position  = append_x3f_text(&chars, "CAMMODEL");
        const uint32_t value_position = append_x3f_text(&chars,
                                                        "Synthetic X3F");

        std::vector<std::byte> out;
        append_ascii(&out, "SECp");
        append_u32le(&out, 1U);
        append_u32le(&out, 1U);
        append_u32le(&out, 0U);
        append_u32le(&out, 0U);
        append_u32le(&out, static_cast<uint32_t>(chars.size() / 2U));
        append_u32le(&out, name_position);
        append_u32le(&out, value_position);
        out.insert(out.end(), chars.begin(), chars.end());
        return out;
    }

}  // namespace detail

struct RawEmbeddedMetadataFixture final {
    std::vector<std::byte> bytes;
    uint64_t entropy_offset = 0U;
    uint64_t entropy_size   = 0U;
};

inline std::vector<std::byte>
tiff_with_nikon_makernote()
{
    return detail::make_tiff(false);
}

inline std::vector<std::byte>
dng_with_nikon_makernote()
{
    return detail::make_tiff(true);
}

inline std::vector<std::byte>
webp_with_exif()
{
    const std::vector<std::byte> tiff = tiff_with_nikon_makernote();
    std::vector<std::byte> out;
    detail::append_ascii(&out, "RIFF");
    detail::append_u32le(&out, 0U);
    detail::append_ascii(&out, "WEBP");
    const std::vector<std::byte> vp8x(10U, std::byte { 0U });
    detail::append_webp_chunk(&out, fourcc('V', 'P', '8', 'X'), vp8x);
    detail::append_webp_chunk(&out, fourcc('E', 'X', 'I', 'F'), tiff);
    const std::array<std::byte, 1U> image { std::byte { 0U } };
    detail::append_webp_chunk(&out, fourcc('V', 'P', '8', ' '), image);
    detail::write_u32le(&out, 4U, static_cast<uint32_t>(out.size() - 8U));
    return out;
}

inline std::vector<std::byte>
jp2_with_exif()
{
    const std::vector<std::byte> tiff = tiff_with_nikon_makernote();
    const std::vector<std::byte> exif = detail::exif_box_payload(tiff);
    std::vector<std::byte> out;
    detail::append_u32be(&out, 12U);
    detail::append_u32be(&out, fourcc('j', 'P', ' ', ' '));
    detail::append_u32be(&out, 0x0d0a870aU);
    std::vector<std::byte> ftyp;
    detail::append_u32be(&ftyp, fourcc('j', 'p', '2', ' '));
    detail::append_u32be(&ftyp, 0U);
    detail::append_u32be(&ftyp, fourcc('j', 'p', '2', ' '));
    detail::append_box(&out, fourcc('f', 't', 'y', 'p'), ftyp);
    detail::append_box(&out, fourcc('E', 'x', 'i', 'f'), exif);
    return out;
}

inline std::vector<std::byte>
jxl_with_exif()
{
    const std::vector<std::byte> tiff = tiff_with_nikon_makernote();
    const std::vector<std::byte> exif = detail::exif_box_payload(tiff);
    std::vector<std::byte> out;
    detail::append_u32be(&out, 12U);
    detail::append_u32be(&out, fourcc('J', 'X', 'L', ' '));
    detail::append_u32be(&out, 0x0d0a870aU);
    detail::append_box(&out, fourcc('E', 'x', 'i', 'f'), exif);
    return out;
}

inline std::vector<std::byte>
avif_with_exif()
{
    const std::vector<std::byte> tiff = tiff_with_nikon_makernote();
    const std::vector<std::byte> exif = detail::exif_box_payload(tiff);

    std::vector<std::byte> infe_payload;
    infe_payload.insert(infe_payload.end(),
                        { std::byte { 2U }, std::byte { 0U }, std::byte { 0U },
                          std::byte { 0U } });
    detail::append_u16be(&infe_payload, 1U);
    detail::append_u16be(&infe_payload, 0U);
    detail::append_u32be(&infe_payload, fourcc('E', 'x', 'i', 'f'));
    detail::append_ascii(&infe_payload, "Exif");
    infe_payload.push_back(std::byte { 0U });
    std::vector<std::byte> infe;
    detail::append_box(&infe, fourcc('i', 'n', 'f', 'e'), infe_payload);

    std::vector<std::byte> iinf_payload { std::byte { 2U }, std::byte { 0U },
                                          std::byte { 0U }, std::byte { 0U } };
    detail::append_u32be(&iinf_payload, 1U);
    iinf_payload.insert(iinf_payload.end(), infe.begin(), infe.end());
    std::vector<std::byte> iinf;
    detail::append_box(&iinf, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iloc_payload {
        std::byte { 1U }, std::byte { 0U },    std::byte { 0U },
        std::byte { 0U }, std::byte { 0x44U }, std::byte { 0U }
    };
    detail::append_u16be(&iloc_payload, 1U);
    detail::append_u16be(&iloc_payload, 1U);
    detail::append_u16be(&iloc_payload, 1U);
    detail::append_u16be(&iloc_payload, 0U);
    detail::append_u16be(&iloc_payload, 1U);
    detail::append_u32be(&iloc_payload, 0U);
    detail::append_u32be(&iloc_payload, static_cast<uint32_t>(exif.size()));
    std::vector<std::byte> iloc;
    detail::append_box(&iloc, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> idat;
    detail::append_box(&idat, fourcc('i', 'd', 'a', 't'), exif);
    std::vector<std::byte> meta_payload(4U, std::byte { 0U });
    meta_payload.insert(meta_payload.end(), iinf.begin(), iinf.end());
    meta_payload.insert(meta_payload.end(), iloc.begin(), iloc.end());
    meta_payload.insert(meta_payload.end(), idat.begin(), idat.end());
    std::vector<std::byte> meta;
    detail::append_box(&meta, fourcc('m', 'e', 't', 'a'), meta_payload);

    std::vector<std::byte> ftyp_payload;
    detail::append_u32be(&ftyp_payload, fourcc('a', 'v', 'i', 'f'));
    detail::append_u32be(&ftyp_payload, 0U);
    detail::append_u32be(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    std::vector<std::byte> out;
    detail::append_box(&out, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    out.insert(out.end(), meta.begin(), meta.end());
    return out;
}

inline std::vector<std::byte>
raf_with_native_directory()
{
    std::vector<std::byte> directory;
    detail::append_u32be(&directory, 1U);
    detail::append_u16be(&directory, 0x0100U);
    detail::append_u16be(&directory, 4U);
    detail::append_u16be(&directory, 6048U);
    detail::append_u16be(&directory, 4032U);

    constexpr uint32_t directory_offset = 4096U;
    std::vector<std::byte> out(directory_offset, std::byte { 0x6bU });
    std::fill(out.begin(), out.begin() + 0x88U, std::byte { 0U });
    for (size_t i = 0; i < 16U; ++i) {
        out[i] = std::byte { static_cast<uint8_t>(
            std::string_view("FUJIFILMCCD-RAW ")[i]) };
    }
    out[0x3cU] = std::byte { '0' };
    out[0x3dU] = std::byte { '2' };
    out[0x3eU] = std::byte { '0' };
    out[0x3fU] = std::byte { '0' };
    detail::write_u32be(&out, 0x5cU, directory_offset);
    detail::write_u32be(&out, 0x60U, static_cast<uint32_t>(directory.size()));
    out.insert(out.end(), directory.begin(), directory.end());
    return out;
}

inline RawEmbeddedMetadataFixture
raf_with_embedded_metadata()
{
    const detail::JpegMetadataFixture jpeg = detail::make_jpeg_with_exif_xmp();
    const std::vector<std::byte> tiff = detail::make_short_tiff(0x0106U, 2U);

    std::vector<std::byte> directory;
    detail::append_u32be(&directory, 1U);
    detail::append_u16be(&directory, 0x0100U);
    detail::append_u16be(&directory, 4U);
    detail::append_u16be(&directory, 6048U);
    detail::append_u16be(&directory, 4032U);

    constexpr uint32_t preview_offset = 256U;
    const uint32_t tiff_offset        = static_cast<uint32_t>(
        (preview_offset + jpeg.bytes.size() + 255U) & ~size_t { 255U });
    const uint32_t directory_offset = static_cast<uint32_t>(
        (tiff_offset + tiff.size() + 255U) & ~size_t { 255U });

    RawEmbeddedMetadataFixture fixture;
    fixture.bytes.resize(preview_offset, std::byte { 0U });
    for (size_t i = 0U; i < 16U; ++i) {
        fixture.bytes[i] = std::byte { static_cast<uint8_t>(
            std::string_view("FUJIFILMCCD-RAW ")[i]) };
    }
    fixture.bytes[0x3cU] = std::byte { '0' };
    fixture.bytes[0x3dU] = std::byte { '2' };
    fixture.bytes[0x3eU] = std::byte { '0' };
    fixture.bytes[0x3fU] = std::byte { '0' };
    detail::write_u32be(&fixture.bytes, 0x54U, preview_offset);
    detail::write_u32be(&fixture.bytes, 0x58U,
                        static_cast<uint32_t>(jpeg.bytes.size()));
    detail::write_u32be(&fixture.bytes, 0x5cU, directory_offset);
    detail::write_u32be(&fixture.bytes, 0x60U,
                        static_cast<uint32_t>(directory.size()));
    detail::write_u32be(&fixture.bytes, 0x64U, tiff_offset);
    detail::write_u32be(&fixture.bytes, 0x68U,
                        static_cast<uint32_t>(tiff.size()));
    fixture.bytes.insert(fixture.bytes.end(), jpeg.bytes.begin(),
                         jpeg.bytes.end());
    fixture.bytes.resize(tiff_offset, std::byte { 0x35U });
    fixture.bytes.insert(fixture.bytes.end(), tiff.begin(), tiff.end());
    fixture.bytes.resize(directory_offset, std::byte { 0x6bU });
    fixture.bytes.insert(fixture.bytes.end(), directory.begin(),
                         directory.end());
    fixture.entropy_offset = preview_offset + jpeg.entropy_offset;
    fixture.entropy_size   = jpeg.entropy_size;
    return fixture;
}

inline std::vector<std::byte>
x3f_with_native_properties()
{
    constexpr uint32_t property_offset = 4096U;
    std::vector<std::byte> out(property_offset, std::byte { 0x5aU });
    std::fill(out.begin(), out.begin() + 264U, std::byte { 0U });
    out[0U] = std::byte { 'F' };
    out[1U] = std::byte { 'O' };
    out[2U] = std::byte { 'V' };
    out[3U] = std::byte { 'b' };
    detail::write_u32le(&out, 4U, (2U << 16U) | 3U);
    detail::write_u32le(&out, 28U, 2640U);
    detail::write_u32le(&out, 32U, 1760U);

    const std::vector<std::byte> property = detail::make_x3f_property_section();
    out.insert(out.end(), property.begin(), property.end());
    const uint32_t directory_offset = static_cast<uint32_t>(out.size());
    detail::append_ascii(&out, "SECd");
    detail::append_u32le(&out, 1U);
    detail::append_u32le(&out, 1U);
    detail::append_u32le(&out, property_offset);
    detail::append_u32le(&out, static_cast<uint32_t>(property.size()));
    detail::append_ascii(&out, "PROP");
    detail::append_u32le(&out, directory_offset);
    return out;
}

inline RawEmbeddedMetadataFixture
x3f_with_embedded_metadata()
{
    const detail::JpegMetadataFixture jpeg = detail::make_jpeg_with_exif_xmp();
    const std::vector<std::byte> property = detail::make_x3f_property_section();
    constexpr uint32_t property_offset    = 512U;
    const uint32_t section_offset         = static_cast<uint32_t>(
        (property_offset + property.size() + 255U) & ~size_t { 255U });

    RawEmbeddedMetadataFixture fixture;
    fixture.bytes.resize(property_offset, std::byte { 0x5aU });
    std::fill(fixture.bytes.begin(), fixture.bytes.begin() + 264U,
              std::byte { 0U });
    fixture.bytes[0U] = std::byte { 'F' };
    fixture.bytes[1U] = std::byte { 'O' };
    fixture.bytes[2U] = std::byte { 'V' };
    fixture.bytes[3U] = std::byte { 'b' };
    detail::write_u32le(&fixture.bytes, 4U, (2U << 16U) | 3U);
    detail::write_u32le(&fixture.bytes, 28U, 2640U);
    detail::write_u32le(&fixture.bytes, 32U, 1760U);
    fixture.bytes.insert(fixture.bytes.end(), property.begin(), property.end());
    fixture.bytes.resize(section_offset, std::byte { 0x45U });

    detail::append_ascii(&fixture.bytes, "SECi");
    fixture.bytes.resize(fixture.bytes.size() + 24U, std::byte { 0U });
    fixture.bytes.insert(fixture.bytes.end(), jpeg.bytes.begin(),
                         jpeg.bytes.end());
    const uint32_t section_size = static_cast<uint32_t>(fixture.bytes.size())
                                  - section_offset;

    const uint32_t directory_offset = static_cast<uint32_t>(
        fixture.bytes.size());
    detail::append_ascii(&fixture.bytes, "SECd");
    detail::append_u32le(&fixture.bytes, 1U);
    detail::append_u32le(&fixture.bytes, 2U);
    detail::append_u32le(&fixture.bytes, property_offset);
    detail::append_u32le(&fixture.bytes,
                         static_cast<uint32_t>(property.size()));
    detail::append_ascii(&fixture.bytes, "PROP");
    detail::append_u32le(&fixture.bytes, section_offset);
    detail::append_u32le(&fixture.bytes, section_size);
    detail::append_ascii(&fixture.bytes, "IMA2");
    detail::append_u32le(&fixture.bytes, directory_offset);

    fixture.entropy_offset = section_offset + 28U + jpeg.entropy_offset;
    fixture.entropy_size   = jpeg.entropy_size;
    return fixture;
}

inline std::vector<std::byte>
crw_with_native_ciff()
{
    constexpr uint32_t root_offset = 4096U;
    std::vector<std::byte> out;
    detail::append_ascii(&out, "II");
    detail::append_u32le(&out, root_offset);
    detail::append_ascii(&out, "HEAPCCDR");
    out.resize(root_offset, std::byte { 0x4cU });
    detail::append_u16le(&out, 1U);
    detail::append_u16le(&out, 0x4801U);
    detail::append_ascii(&out, "CIFFTEST");
    detail::append_u32le(&out, 0U);
    return out;
}

}  // namespace openmeta::test_fixture
