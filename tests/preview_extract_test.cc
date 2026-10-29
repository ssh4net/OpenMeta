// SPDX-License-Identifier: Apache-2.0

#include "openmeta/preview_extract.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <span>
#include <vector>

namespace openmeta {
namespace {

    static void append_u16le(std::vector<std::byte>* out, uint16_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte(static_cast<uint8_t>(v & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 8) & 0xFFU)));
    }

    static void append_u32le(std::vector<std::byte>* out, uint32_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte(static_cast<uint8_t>(v & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 8) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 16) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 24) & 0xFFU)));
    }

    static void append_u32be(std::vector<std::byte>* out, uint32_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte(static_cast<uint8_t>((v >> 24) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 16) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 8) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>(v & 0xFFU)));
    }

    static void append_u64be(std::vector<std::byte>* out, uint64_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte(static_cast<uint8_t>((v >> 56) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 48) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 40) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 32) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 24) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 16) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 8) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>(v & 0xFFU)));
    }

    static void append_bytes(std::vector<std::byte>* out, const char* s)
    {
        ASSERT_NE(out, nullptr);
        ASSERT_NE(s, nullptr);
        for (size_t i = 0; s[i] != '\0'; ++i) {
            out->push_back(std::byte(static_cast<uint8_t>(s[i])));
        }
    }

    static std::vector<std::byte> make_tiff_with_ifd1_jpeg_preview()
    {
        std::vector<std::byte> bytes;

        append_bytes(&bytes, "II");
        append_u16le(&bytes, 42U);
        append_u32le(&bytes, 8U);  // ifd0

        append_u16le(&bytes, 0U);   // ifd0 entries
        append_u32le(&bytes, 14U);  // next ifd -> ifd1

        append_u16le(&bytes, 2U);       // ifd1 entries
        append_u16le(&bytes, 0x0201U);  // JPEGInterchangeFormat
        append_u16le(&bytes, 4U);       // LONG
        append_u32le(&bytes, 1U);
        append_u32le(&bytes, 44U);  // preview offset

        append_u16le(&bytes, 0x0202U);  // JPEGInterchangeFormatLength
        append_u16le(&bytes, 4U);       // LONG
        append_u32le(&bytes, 1U);
        append_u32le(&bytes, 4U);  // preview length

        append_u32le(&bytes, 0U);  // no next ifd

        bytes.push_back(std::byte { 0xFF });
        bytes.push_back(std::byte { 0xD8 });
        bytes.push_back(std::byte { 0xFF });
        bytes.push_back(std::byte { 0xD9 });
        return bytes;
    }

    static std::vector<std::byte> make_tiff_with_jpg_from_raw(bool jpeg_soi)
    {
        std::vector<std::byte> bytes;

        append_bytes(&bytes, "II");
        append_u16le(&bytes, 42U);
        append_u32le(&bytes, 8U);  // ifd0

        append_u16le(&bytes, 1U);       // ifd0 entries
        append_u16le(&bytes, 0x002EU);  // JpgFromRaw
        append_u16le(&bytes, 7U);       // UNDEFINED
        append_u32le(&bytes, 6U);       // count
        append_u32le(&bytes, 26U);      // value offset
        append_u32le(&bytes, 0U);       // no next ifd

        if (jpeg_soi) {
            bytes.push_back(std::byte { 0xFF });
            bytes.push_back(std::byte { 0xD8 });
        } else {
            bytes.push_back(std::byte { 0x00 });
            bytes.push_back(std::byte { 0x11 });
        }
        bytes.push_back(std::byte { 0x01 });
        bytes.push_back(std::byte { 0x02 });
        bytes.push_back(std::byte { 0xFF });
        bytes.push_back(std::byte { 0xD9 });
        return bytes;
    }

    static std::vector<std::byte> make_cr3_with_uuid_prvw_jpeg_preview()
    {
        // Minimal ISO-BMFF file with:
        // - ftyp brand 'crx ' (CR3)
        // - uuid box with PRVW stream containing a tiny JPEG (FFD8..FFD9)
        std::vector<std::byte> bytes;

        // ftyp box (24 bytes)
        append_u32be(&bytes, 24U);
        append_bytes(&bytes, "ftyp");
        append_bytes(&bytes, "crx ");  // major brand
        append_u32be(&bytes, 0U);      // minor version
        append_bytes(&bytes, "crx ");  // compatible brand
        append_bytes(&bytes, "isom");  // compatible brand

        // uuid box (60 bytes)
        append_u32be(&bytes, 60U);
        append_bytes(&bytes, "uuid");

        const uint8_t uuid[16] = {
            0xEA, 0xF4, 0x2B, 0x5E, 0x1C, 0x98, 0x4B, 0x88,
            0xB9, 0xFB, 0xB7, 0xDC, 0x40, 0x6E, 0x4D, 0x16,
        };
        for (uint32_t i = 0; i < 16U; ++i) {
            bytes.push_back(std::byte { uuid[i] });
        }

        append_u64be(&bytes, 1U);  // uuid payload header

        // PRVW inner box: size=28 bytes, with JPEG bytes at payload+16.
        append_u32be(&bytes, 28U);
        append_bytes(&bytes, "PRVW");

        for (uint32_t i = 0; i < 12U; ++i) {  // padding/fields
            bytes.push_back(std::byte { 0x00 });
        }

        append_u32be(&bytes, 4U);  // JPEG length
        bytes.push_back(std::byte { 0xFF });
        bytes.push_back(std::byte { 0xD8 });
        bytes.push_back(std::byte { 0xFF });
        bytes.push_back(std::byte { 0xD9 });

        return bytes;
    }


    static std::vector<std::byte> make_cr3_with_wrapping_size64_box()
    {
        std::vector<std::byte> bytes;

        // The trailing zero word makes a sum-based range check resume one byte
        // before the oversized box as a zero-sized box.
        append_u32be(&bytes, 28U);
        append_bytes(&bytes, "ftyp");
        append_bytes(&bytes, "crx ");
        append_u32be(&bytes, 0U);
        append_bytes(&bytes, "isom");
        append_bytes(&bytes, "crx ");
        append_u32be(&bytes, 0U);

        append_u32be(&bytes, 1U);
        append_bytes(&bytes, "free");
        append_u64be(&bytes, std::numeric_limits<uint64_t>::max());
        return bytes;
    }

}  // namespace

TEST(PreviewExtract, FindsExifJpegInterchangeCandidate)
{
    const std::vector<std::byte> bytes = make_tiff_with_ifd1_jpeg_preview();

    std::array<ContainerBlockRef, 8> blocks {};
    std::array<PreviewCandidate, 8> previews {};
    const PreviewScanResult res = scan_preview_candidates(
        std::span<const std::byte>(bytes.data(), bytes.size()),
        std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
        std::span<PreviewCandidate>(previews.data(), previews.size()),
        PreviewScanOptions {});

    ASSERT_EQ(res.status, PreviewScanStatus::Ok);
    ASSERT_EQ(res.written, 1U);
    const PreviewCandidate& p = previews[0];
    EXPECT_EQ(p.kind, PreviewKind::ExifJpegInterchange);
    EXPECT_EQ(p.offset_tag, 0x0201U);
    EXPECT_EQ(p.length_tag, 0x0202U);
    EXPECT_EQ(p.file_offset, 44U);
    EXPECT_EQ(p.size, 4U);
    EXPECT_TRUE(p.has_jpeg_soi_signature);

    std::array<std::byte, 4> out {};
    const PreviewExtractResult er = extract_preview_candidate(
        std::span<const std::byte>(bytes.data(), bytes.size()), p,
        std::span<std::byte>(out.data(), out.size()), PreviewExtractOptions {});
    ASSERT_EQ(er.status, PreviewExtractStatus::Ok);
    EXPECT_EQ(er.written, 4U);
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xFFU);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0xD8U);
}

TEST(PreviewExtract, FindsJpgFromRawCandidate)
{
    const std::vector<std::byte> bytes = make_tiff_with_jpg_from_raw(true);

    std::array<ContainerBlockRef, 8> blocks {};
    std::array<PreviewCandidate, 8> previews {};
    PreviewScanOptions options;
    options.include_exif_jpeg_interchange = false;
    options.include_jpg_from_raw          = true;

    const PreviewScanResult res = scan_preview_candidates(
        std::span<const std::byte>(bytes.data(), bytes.size()),
        std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
        std::span<PreviewCandidate>(previews.data(), previews.size()), options);

    ASSERT_EQ(res.status, PreviewScanStatus::Ok);
    ASSERT_EQ(res.written, 1U);
    const PreviewCandidate& p = previews[0];
    EXPECT_EQ(p.kind, PreviewKind::ExifJpgFromRaw);
    EXPECT_EQ(p.offset_tag, 0x002EU);
    EXPECT_EQ(p.file_offset, 26U);
    EXPECT_EQ(p.size, 6U);
    EXPECT_TRUE(p.has_jpeg_soi_signature);
}

TEST(PreviewExtract, RequireJpegSoiFiltersNonJpegCandidate)
{
    const std::vector<std::byte> bytes = make_tiff_with_jpg_from_raw(false);

    std::array<ContainerBlockRef, 8> blocks {};
    std::array<PreviewCandidate, 8> previews {};
    PreviewScanOptions options;
    options.include_exif_jpeg_interchange = false;
    options.include_jpg_from_raw          = true;
    options.require_jpeg_soi              = true;

    const PreviewScanResult res = scan_preview_candidates(
        std::span<const std::byte>(bytes.data(), bytes.size()),
        std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
        std::span<PreviewCandidate>(previews.data(), previews.size()), options);

    ASSERT_EQ(res.status, PreviewScanStatus::Ok);
    EXPECT_EQ(res.written, 0U);
}

TEST(PreviewExtract, ExtractionChecksOutputAndLimits)
{
    const std::vector<std::byte> bytes = make_tiff_with_ifd1_jpeg_preview();

    std::array<ContainerBlockRef, 8> blocks {};
    std::array<PreviewCandidate, 8> previews {};
    const PreviewScanResult scan = scan_preview_candidates(
        std::span<const std::byte>(bytes.data(), bytes.size()),
        std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
        std::span<PreviewCandidate>(previews.data(), previews.size()),
        PreviewScanOptions {});
    ASSERT_EQ(scan.status, PreviewScanStatus::Ok);
    ASSERT_EQ(scan.written, 1U);

    const PreviewCandidate& p = previews[0];
    std::array<std::byte, 2> too_small {};
    PreviewExtractOptions options;
    options.max_output_bytes         = 1024U;
    const PreviewExtractResult small = extract_preview_candidate(
        std::span<const std::byte>(bytes.data(), bytes.size()), p,
        std::span<std::byte>(too_small.data(), too_small.size()), options);
    EXPECT_EQ(small.status, PreviewExtractStatus::OutputTruncated);
    EXPECT_EQ(small.needed, 4U);

    options.max_output_bytes = 3U;
    std::array<std::byte, 8> out {};
    const PreviewExtractResult limited = extract_preview_candidate(
        std::span<const std::byte>(bytes.data(), bytes.size()), p,
        std::span<std::byte>(out.data(), out.size()), options);
    EXPECT_EQ(limited.status, PreviewExtractStatus::LimitExceeded);
}

TEST(PreviewExtract, FindsCr3PrvwJpegCandidate)
{
    const std::vector<std::byte> bytes = make_cr3_with_uuid_prvw_jpeg_preview();

    std::array<ContainerBlockRef, 8> blocks {};
    std::array<PreviewCandidate, 8> previews {};
    PreviewScanOptions options;
    options.include_exif_jpeg_interchange = false;
    options.include_jpg_from_raw          = false;
    options.include_cr3_prvw_jpeg         = true;

    const PreviewScanResult res = scan_preview_candidates(
        std::span<const std::byte>(bytes.data(), bytes.size()),
        std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
        std::span<PreviewCandidate>(previews.data(), previews.size()), options);

    ASSERT_EQ(res.status, PreviewScanStatus::Ok);
    ASSERT_EQ(res.written, 1U);
    const PreviewCandidate& p = previews[0];
    EXPECT_EQ(p.kind, PreviewKind::Cr3PrvwJpeg);
    EXPECT_EQ(p.format, ContainerFormat::Cr3);
    EXPECT_EQ(p.file_offset, 80U);
    EXPECT_EQ(p.size, 4U);
    EXPECT_TRUE(p.has_jpeg_soi_signature);

    std::array<std::byte, 4> out {};
    const PreviewExtractResult er = extract_preview_candidate(
        std::span<const std::byte>(bytes.data(), bytes.size()), p,
        std::span<std::byte>(out.data(), out.size()), PreviewExtractOptions {});
    ASSERT_EQ(er.status, PreviewExtractStatus::Ok);
    EXPECT_EQ(er.written, 4U);
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xFFU);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0xD8U);
    EXPECT_EQ(static_cast<uint8_t>(out[3]), 0xD9U);
}


TEST(PreviewExtract, RejectsCr3BoxWithWrappingSize64)
{
    const std::vector<std::byte> bytes = make_cr3_with_wrapping_size64_box();
    std::array<PreviewCandidate, 4U> previews {};
    PreviewScanOptions options;
    options.include_exif_jpeg_interchange = false;
    options.include_jpg_from_raw          = false;
    options.include_cr3_prvw_jpeg         = true;

    const PreviewScanResult res = find_preview_candidates(
        std::span<const std::byte>(bytes.data(), bytes.size()),
        std::span<const ContainerBlockRef> {},
        std::span<PreviewCandidate>(previews.data(), previews.size()), options);

    EXPECT_EQ(res.status, PreviewScanStatus::Malformed);
    EXPECT_EQ(res.written, 0U);
}

}  // namespace openmeta
