// SPDX-License-Identifier: Apache-2.0

#include "openmeta/container_scan.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }

    static void append_u16le(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
    }


    static void append_u32be(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }


    static void append_u32le(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
    }


    static void append_u64le(std::vector<std::byte>* out, uint64_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 32) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 40) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 48) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 56) & 0xFF) });
    }


    static void append_u64be(std::vector<std::byte>* out, uint64_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 56) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 48) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 40) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 32) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }


    static void append_fourcc(std::vector<std::byte>* out, uint32_t f)
    {
        out->push_back(std::byte { static_cast<uint8_t>((f >> 24) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((f >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((f >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((f >> 0) & 0xFF) });
    }


    static void append_bytes(std::vector<std::byte>* out, std::string_view s)
    {
        for (char c : s) {
            out->push_back(std::byte { static_cast<uint8_t>(c) });
        }
    }


    static void append_fullbox_header(std::vector<std::byte>* out,
                                      uint8_t version)
    {
        out->push_back(std::byte { version });
        out->push_back(std::byte { 0x00 });
        out->push_back(std::byte { 0x00 });
        out->push_back(std::byte { 0x00 });
    }


    static void append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                                std::span<const std::byte> payload)
    {
        append_u32be(out, static_cast<uint32_t>(8 + payload.size()));
        append_fourcc(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }


    static void append_jpeg_segment(std::vector<std::byte>* out,
                                    uint16_t marker,
                                    std::span<const std::byte> payload)
    {
        out->push_back(std::byte { 0xFF });
        out->push_back(std::byte { static_cast<uint8_t>(marker & 0xFF) });
        const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2);
        append_u16be(out, seg_len);
        out->insert(out->end(), payload.begin(), payload.end());
    }


    struct JpegCallbackState final {
        std::span<const std::byte> bytes;
        RandomAccessIoCode code  = RandomAccessIoCode::Ok;
        uint64_t max_bytes       = UINT64_MAX;
        uint32_t calls           = 0U;
        uint64_t forbidden_begin = UINT64_MAX;
        uint64_t forbidden_end   = UINT64_MAX;
        bool touched_forbidden   = false;
    };


    static RandomAccessIoResult
    jpeg_read_at(void* context, uint64_t offset,
                 std::span<std::byte> destination) noexcept
    {
        JpegCallbackState* state = static_cast<JpegCallbackState*>(context);
        state->calls += 1U;
        const uint64_t request_end
            = (destination.size() <= UINT64_MAX - offset)
                  ? offset + static_cast<uint64_t>(destination.size())
                  : UINT64_MAX;
        if (offset < state->forbidden_end
            && request_end > state->forbidden_begin) {
            state->touched_forbidden = true;
        }
        uint64_t available = 0U;
        if (offset <= state->bytes.size()) {
            available = static_cast<uint64_t>(state->bytes.size()) - offset;
        }
        uint64_t count = static_cast<uint64_t>(destination.size());
        count          = (count < available) ? count : available;
        count          = (count < state->max_bytes) ? count : state->max_bytes;
        if (count != 0U) {
            std::memcpy(destination.data(),
                        state->bytes.data() + static_cast<size_t>(offset),
                        static_cast<size_t>(count));
        }
        return RandomAccessIoResult { state->code, count };
    }


    static void expect_same_block(const ContainerBlockRef& expected,
                                  const ContainerBlockRef& actual)
    {
        EXPECT_EQ(actual.format, expected.format);
        EXPECT_EQ(actual.kind, expected.kind);
        EXPECT_EQ(actual.compression, expected.compression);
        EXPECT_EQ(actual.chunking, expected.chunking);
        EXPECT_EQ(actual.outer_offset, expected.outer_offset);
        EXPECT_EQ(actual.outer_size, expected.outer_size);
        EXPECT_EQ(actual.data_offset, expected.data_offset);
        EXPECT_EQ(actual.data_size, expected.data_size);
        EXPECT_EQ(actual.id, expected.id);
        EXPECT_EQ(actual.part_index, expected.part_index);
        EXPECT_EQ(actual.part_count, expected.part_count);
        EXPECT_EQ(actual.logical_offset, expected.logical_offset);
        EXPECT_EQ(actual.logical_size, expected.logical_size);
        EXPECT_EQ(actual.group, expected.group);
        EXPECT_EQ(actual.aux_u32, expected.aux_u32);
    }


    template<class ScanFn, class RandomScanFn, class RandomMeasureFn>
    static void expect_random_access_parity(std::span<const std::byte> bytes,
                                            uint64_t forbidden_begin,
                                            uint64_t forbidden_end, ScanFn scan,
                                            RandomScanFn random_scan,
                                            RandomMeasureFn random_measure)
    {
        std::array<ContainerBlockRef, 16> expected {};
        const ScanResult contiguous = scan(bytes, expected);
        ASSERT_EQ(contiguous.status, ScanStatus::Ok);
        ASSERT_GT(contiguous.written, 0U);

        constexpr uint64_t prefix_size = 23U;
        std::vector<std::byte> backing(prefix_size, std::byte { 0xA5 });
        backing.insert(backing.end(), bytes.begin(), bytes.end());
        JpegCallbackState callback { backing };
        callback.forbidden_begin = prefix_size + forbidden_begin;
        callback.forbidden_end   = prefix_size + forbidden_end;
        const RandomAccessSource source
            = make_callback_random_access_source(backing.size(), &callback,
                                                 jpeg_read_at, true);
        const RandomAccessSourceRange range
            = make_random_access_source_range(source, prefix_size,
                                              bytes.size());
        std::array<std::byte, 128> storage {};
        ContainerRandomAccessScratch scratch;
        scratch.read_window                       = storage;
        scratch.window_options.minimum_read_bytes = storage.size();

        std::array<ContainerBlockRef, 16> actual {};
        const ContainerRandomAccessScanResult positional
            = random_scan(range, actual, scratch);
        ASSERT_TRUE(positional.complete());
        EXPECT_EQ(positional.scan.status, contiguous.status);
        EXPECT_EQ(positional.scan.written, contiguous.written);
        EXPECT_EQ(positional.scan.needed, contiguous.needed);
        for (uint32_t i = 0U; i < contiguous.written; ++i) {
            expect_same_block(expected[i], actual[i]);
        }
        EXPECT_GT(callback.calls, 0U);
        EXPECT_FALSE(callback.touched_forbidden);
        EXPECT_LT(positional.input.bytes_requested,
                  static_cast<uint64_t>(bytes.size()));

        const ContainerRandomAccessScanResult measured
            = random_measure(range, scratch);
        EXPECT_TRUE(measured.complete());
        EXPECT_EQ(measured.scan.status, contiguous.status);
        EXPECT_EQ(measured.scan.written, 0U);
        EXPECT_EQ(measured.scan.needed, contiguous.needed);
    }


    TEST(ContainerScan, TiffJumbfTag)
    {
        // Minimal JUMBF superbox: jumb -> jumd(label) + cbor(payload).
        std::vector<std::byte> jumd_payload;
        append_bytes(&jumd_payload, "c2pa");
        jumd_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> jumd_box;
        append_bmff_box(&jumd_box, fourcc('j', 'u', 'm', 'd'), jumd_payload);

        const std::array<std::byte, 1> cbor_payload
            = { std::byte { 0xA0 } };  // {}
        std::vector<std::byte> cbor_box;
        append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'), cbor_payload);

        std::vector<std::byte> jumb_payload;
        jumb_payload.insert(jumb_payload.end(), jumd_box.begin(),
                            jumd_box.end());
        jumb_payload.insert(jumb_payload.end(), cbor_box.begin(),
                            cbor_box.end());

        std::vector<std::byte> jumb_box;
        append_bmff_box(&jumb_box, fourcc('j', 'u', 'm', 'b'), jumb_payload);

        // Minimal TIFF header + one UNDEFINED tag containing a `jumb` BMFF box.
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, 8);  // IFD0 offset

        // IFD0 at offset 8.
        append_u16le(&tiff, 1);  // entry count
        append_u16le(&tiff, 0xCD41);
        append_u16le(&tiff, 7);  // UNDEFINED
        append_u32le(&tiff, static_cast<uint32_t>(jumb_box.size()));
        const uint32_t data_off = 28;
        append_u32le(&tiff, data_off);
        append_u32le(&tiff, 0);  // next IFD

        while (tiff.size() < data_off) {
            tiff.push_back(std::byte { 0x00 });
        }
        tiff.insert(tiff.end(), jumb_box.begin(), jumb_box.end());

        std::array<ContainerBlockRef, 16> blocks {};
        const ScanResult res
            = scan_auto(std::span<const std::byte>(tiff.data(), tiff.size()),
                        std::span<ContainerBlockRef>(blocks.data(),
                                                     blocks.size()));

        ASSERT_EQ(res.status, ScanStatus::Ok);

        bool found       = false;
        const uint32_t n = (res.written < blocks.size())
                               ? res.written
                               : static_cast<uint32_t>(blocks.size());
        for (uint32_t i = 0; i < n; ++i) {
            const ContainerBlockRef& b = blocks[i];
            if (b.format == ContainerFormat::Tiff
                && b.kind == ContainerBlockKind::Jumbf && b.id == 0xCD41U) {
                found = true;
                EXPECT_EQ(b.data_offset, static_cast<uint64_t>(data_off));
                EXPECT_EQ(b.data_size, static_cast<uint64_t>(jumb_box.size()));
                break;
            }
        }

        EXPECT_TRUE(found);
    }

    TEST(ContainerScan, BigTiffRejectsOverflowedNextIfdReadBeforeEntryCap)
    {
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 43);
        append_u16le(&tiff, 8);
        append_u16le(&tiff, 0);
        append_u64le(&tiff, 16U);

        append_u64le(&tiff, 1ULL << 62);
        append_u64le(&tiff, 48U);

        while (tiff.size() < 48U) {
            tiff.push_back(std::byte { 0x00 });
        }

        append_u64le(&tiff, 1U);
        append_u16le(&tiff, 0x02BC);
        append_u16le(&tiff, 1);
        append_u64le(&tiff, 4U);
        tiff.push_back(std::byte { 'X' });
        tiff.push_back(std::byte { 'M' });
        tiff.push_back(std::byte { 'P' });
        tiff.push_back(std::byte { '!' });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        append_u64le(&tiff, 0U);

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res
            = scan_auto(std::span<const std::byte>(tiff.data(), tiff.size()),
                        std::span<ContainerBlockRef>(blocks.data(),
                                                     blocks.size()));

        EXPECT_EQ(res.status, ScanStatus::Ok);
        bool found_xmp   = false;
        const uint32_t n = (res.written < blocks.size())
                               ? res.written
                               : static_cast<uint32_t>(blocks.size());
        for (uint32_t i = 0; i < n; ++i) {
            if (blocks[i].format == ContainerFormat::Tiff
                && blocks[i].kind == ContainerBlockKind::Xmp
                && blocks[i].id == 0x02BCU) {
                found_xmp = true;
                break;
            }
        }
        EXPECT_FALSE(found_xmp);
    }


    TEST(ContainerScan, BigTiffRejectsWrappedExternalValueRange)
    {
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 43);
        append_u16le(&tiff, 8);
        append_u16le(&tiff, 0);
        append_u64le(&tiff, 16U);

        append_u64le(&tiff, 1U);
        append_u16le(&tiff, 0x02BC);  // XMP
        append_u16le(&tiff, 1);       // BYTE
        append_u64le(&tiff, 16U);
        append_u64le(&tiff, UINT64_MAX - 7U);
        append_u64le(&tiff, 0U);

        ASSERT_EQ(tiff.size(), 52U);
        std::array<ContainerBlockRef, 4> blocks {};
        const ScanResult res = scan_tiff(tiff, blocks);
        EXPECT_EQ(res.status, ScanStatus::Ok);
        for (uint32_t i = 0; i < res.written && i < blocks.size(); ++i) {
            EXPECT_FALSE(blocks[i].kind == ContainerBlockKind::Xmp
                         && blocks[i].id == 0x02BCU);
        }
    }


    TEST(ContainerScan, Jp2RejectsWrappedExtendedBoxSize)
    {
        std::vector<std::byte> jp2;
        append_u32be(&jp2, 12);
        append_fourcc(&jp2, fourcc('j', 'P', ' ', ' '));
        append_u32be(&jp2, 0x0D0A870A);
        append_u32be(&jp2, 1U);
        append_fourcc(&jp2, fourcc('j', 'p', '2', 'h'));
        append_u64be(&jp2, UINT64_MAX - 11U);

        std::array<ContainerBlockRef, 4> blocks {};
        EXPECT_EQ(scan_jp2(jp2, blocks).status, ScanStatus::Malformed);
    }


    TEST(ContainerScan, JxlRejectsWrappedExtendedBoxSize)
    {
        std::vector<std::byte> jxl;
        append_u32be(&jxl, 12);
        append_fourcc(&jxl, fourcc('J', 'X', 'L', ' '));
        append_u32be(&jxl, 0x0D0A870A);
        append_u32be(&jxl, 1U);
        append_fourcc(&jxl, fourcc('E', 'x', 'i', 'f'));
        append_u64be(&jxl, UINT64_MAX - 11U);

        std::array<ContainerBlockRef, 4> blocks {};
        EXPECT_EQ(scan_jxl(jxl, blocks).status, ScanStatus::Malformed);
    }


    TEST(ContainerScan, BmffRejectsWrappedNestedExtendedBoxSize)
    {
        std::vector<std::byte> bmff;
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0U);
        append_bmff_box(&bmff, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        append_u32be(&bmff, 1U);
        append_fourcc(&bmff, fourcc('m', 'o', 'o', 'v'));
        append_u64be(&bmff, UINT64_MAX - 15U);

        std::array<ContainerBlockRef, 4> blocks {};
        EXPECT_EQ(scan_bmff(bmff, blocks).status, ScanStatus::Malformed);
    }

    TEST(ContainerScan, RafStandaloneXmp)
    {
        std::vector<std::byte> raf;
        append_bytes(&raf, "FUJIFILMCCD-RAW ");
        while (raf.size() < 160U) {
            raf.push_back(std::byte { 0x00 });
        }

        // Embedded TIFF at +160 (minimal IFD0 with no tags).
        append_bytes(&raf, "II");
        append_u16le(&raf, 42);
        append_u32le(&raf, 8);  // IFD0 offset
        append_u16le(&raf, 0);  // entry count
        append_u32le(&raf, 0);  // next IFD

        const uint32_t sig_off = 512U;
        while (raf.size() < sig_off) {
            raf.push_back(std::byte { 0x00 });
        }

        append_bytes(&raf, "http://ns.adobe.com/xap/1.0/");
        raf.push_back(std::byte { 0x00 });
        const uint32_t data_off = sig_off + 29U;
        ASSERT_EQ(raf.size(), static_cast<size_t>(data_off));

        // Small but valid XMP packet with one Description attribute.
        append_bytes(
            &raf,
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' xmp:Rating='0'/>"
            "</rdf:RDF>"
            "</x:xmpmeta>");

        std::array<ContainerBlockRef, 16> blocks {};
        const ScanResult res
            = scan_auto(std::span<const std::byte>(raf.data(), raf.size()),
                        std::span<ContainerBlockRef>(blocks.data(),
                                                     blocks.size()));
        ASSERT_EQ(res.status, ScanStatus::Ok);

        bool found       = false;
        const uint32_t n = (res.written < blocks.size())
                               ? res.written
                               : static_cast<uint32_t>(blocks.size());
        for (uint32_t i = 0; i < n; ++i) {
            const ContainerBlockRef& b = blocks[i];
            if (b.format == ContainerFormat::Raf
                && b.kind == ContainerBlockKind::Xmp
                && b.data_offset == static_cast<uint64_t>(data_off)) {
                found = true;
                EXPECT_EQ(b.outer_offset, static_cast<uint64_t>(sig_off));
                EXPECT_EQ(b.data_offset, static_cast<uint64_t>(data_off));
                EXPECT_GT(b.data_size, 32U);
                break;
            }
        }

        EXPECT_TRUE(found);
    }

    TEST(ContainerScan, RafUsesHeaderDeclaredFujiIfdOffset)
    {
        std::vector<std::byte> raf;
        append_bytes(&raf, "FUJIFILMCCD-RAW ");
        raf.resize(0x64U, std::byte { 0x00 });
        append_u32be(&raf, 256U);
        append_u32be(&raf, 14U);
        raf.resize(256U, std::byte { 0x00 });

        append_bytes(&raf, "II");
        append_u16le(&raf, 42);
        append_u32le(&raf, 8);
        append_u16le(&raf, 0);
        append_u32le(&raf, 0);

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res
            = scan_auto(std::span<const std::byte>(raf.data(), raf.size()),
                        blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Raf);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].data_offset, 256U);
    }

    TEST(ContainerScan, RafPreviewJpegMetadataUsesRafFormat)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xff });
        jpeg.push_back(std::byte { 0xd8 });

        std::vector<std::byte> exif_payload;
        append_bytes(&exif_payload, "Exif");
        exif_payload.push_back(std::byte { 0 });
        exif_payload.push_back(std::byte { 0 });
        append_bytes(&exif_payload, "II");
        append_u16le(&exif_payload, 42);
        append_u32le(&exif_payload, 8);
        append_u16le(&exif_payload, 0);
        append_u32le(&exif_payload, 0);
        append_jpeg_segment(&jpeg, 0xffe1, exif_payload);
        jpeg.push_back(std::byte { 0xff });
        jpeg.push_back(std::byte { 0xd9 });

        const uint32_t preview_off = 128U;
        const uint32_t tiff_off    = 512U;

        std::vector<std::byte> raf;
        append_bytes(&raf, "FUJIFILMCCD-RAW ");
        raf.resize(0x54U, std::byte { 0x00 });
        append_u32be(&raf, preview_off);
        append_u32be(&raf, static_cast<uint32_t>(jpeg.size()));
        raf.resize(0x64U, std::byte { 0x00 });
        append_u32be(&raf, tiff_off);
        append_u32be(&raf, 14U);
        raf.resize(preview_off, std::byte { 0x00 });
        raf.insert(raf.end(), jpeg.begin(), jpeg.end());
        raf.resize(tiff_off, std::byte { 0x00 });
        append_bytes(&raf, "II");
        append_u16le(&raf, 42);
        append_u32le(&raf, 8);
        append_u16le(&raf, 0);
        append_u32le(&raf, 0);

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res
            = scan_auto(std::span<const std::byte>(raf.data(), raf.size()),
                        blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_GE(res.written, 2U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Raf);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_GE(blocks[0].data_offset, static_cast<uint64_t>(preview_off));
        EXPECT_LT(blocks[0].data_offset, static_cast<uint64_t>(tiff_off));
        EXPECT_EQ(blocks[1].format, ContainerFormat::Raf);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[1].data_offset, static_cast<uint64_t>(tiff_off));
    }

    TEST(ContainerScan, X3fEmbeddedExifUsesX3fFormat)
    {
        std::vector<std::byte> x3f;
        append_bytes(&x3f, "FOVb");
        x3f.resize(128, std::byte { 0 });
        append_bytes(&x3f, "Exif");
        x3f.push_back(std::byte { 0 });
        x3f.push_back(std::byte { 0 });
        append_bytes(&x3f, "II");
        append_u16le(&x3f, 42);
        append_u32le(&x3f, 8);
        append_u16le(&x3f, 0);
        append_u32le(&x3f, 0);

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res
            = scan_auto(std::span<const std::byte>(x3f.data(), x3f.size()),
                        blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::X3f);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
    }

    TEST(ContainerScan, X3fSectionJpegMetadataUsesX3fFormat)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xff });
        jpeg.push_back(std::byte { 0xd8 });

        std::vector<std::byte> exif_payload;
        append_bytes(&exif_payload, "Exif");
        exif_payload.push_back(std::byte { 0 });
        exif_payload.push_back(std::byte { 0 });
        append_bytes(&exif_payload, "II");
        append_u16le(&exif_payload, 42);
        append_u32le(&exif_payload, 8);
        append_u16le(&exif_payload, 0);
        append_u32le(&exif_payload, 0);
        append_jpeg_segment(&jpeg, 0xffe1, exif_payload);

        std::vector<std::byte> xmp_payload;
        append_bytes(&xmp_payload, "http://ns.adobe.com/xap/1.0/");
        xmp_payload.push_back(std::byte { 0 });
        append_bytes(&xmp_payload, "<x:xmpmeta/>");
        append_jpeg_segment(&jpeg, 0xffe1, xmp_payload);
        jpeg.push_back(std::byte { 0xff });
        jpeg.push_back(std::byte { 0xd9 });

        std::vector<std::byte> x3f;
        append_bytes(&x3f, "FOVb");
        append_u32le(&x3f, (2U << 16U) | 3U);
        x3f.resize(128, std::byte { 0 });

        const uint32_t section_off = static_cast<uint32_t>(x3f.size());
        append_bytes(&x3f, "SECi");
        x3f.resize(x3f.size() + 24U, std::byte { 0 });
        x3f.insert(x3f.end(), jpeg.begin(), jpeg.end());
        const uint32_t section_size = static_cast<uint32_t>(x3f.size())
                                      - section_off;

        const uint32_t dir_off = static_cast<uint32_t>(x3f.size());
        append_bytes(&x3f, "SECd");
        append_u32le(&x3f, 1U);
        append_u32le(&x3f, 1U);
        append_u32le(&x3f, section_off);
        append_u32le(&x3f, section_size);
        append_bytes(&x3f, "IMA2");
        append_u32le(&x3f, dir_off);

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res
            = scan_auto(std::span<const std::byte>(x3f.data(), x3f.size()),
                        blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 2U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::X3f);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[1].format, ContainerFormat::X3f);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_GT(blocks[1].data_offset, static_cast<uint64_t>(section_off));
    }

    TEST(ContainerScan, CrwRootBlockUsesCrwFormat)
    {
        std::vector<std::byte> crw;
        append_bytes(&crw, "II");
        append_u32le(&crw, 14U);
        append_bytes(&crw, "HEAPCCDR");

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res
            = scan_auto(std::span<const std::byte>(crw.data(), crw.size()),
                        blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Crw);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Ciff);
        EXPECT_EQ(blocks[0].aux_u32, 14U);
    }

    TEST(ContainerScan, BmffStartsWithMoovStillScans)
    {
        // Some BMFF files start with `moov`/`mdat` before `ftyp`. scan_auto()
        // should still attempt BMFF scanning and find metadata.
        std::vector<std::byte> file;

        // moov (empty)
        append_bmff_box(&file, fourcc('m', 'o', 'o', 'v'),
                        std::span<const std::byte> {});

        // ftyp (heic + mif1)
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

        // uuid XMP box (Adobe UUID) with a small XMP packet payload.
        const std::array<std::byte, 16> xmp_uuid = {
            std::byte { 0xBE }, std::byte { 0x7A }, std::byte { 0xCF },
            std::byte { 0xCB }, std::byte { 0x97 }, std::byte { 0xA9 },
            std::byte { 0x42 }, std::byte { 0xE8 }, std::byte { 0x9C },
            std::byte { 0x71 }, std::byte { 0x99 }, std::byte { 0x94 },
            std::byte { 0x91 }, std::byte { 0xE3 }, std::byte { 0xAF },
            std::byte { 0xAC },
        };

        std::vector<std::byte> xmp_payload;
        append_bytes(
            &xmp_payload,
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' xmp:Rating='0'/>"
            "</rdf:RDF>"
            "</x:xmpmeta>");

        const uint64_t uuid_off  = static_cast<uint64_t>(file.size());
        const uint32_t uuid_size = static_cast<uint32_t>(8U + 16U
                                                         + xmp_payload.size());
        append_u32be(&file, uuid_size);
        append_fourcc(&file, fourcc('u', 'u', 'i', 'd'));
        file.insert(file.end(), xmp_uuid.begin(), xmp_uuid.end());
        file.insert(file.end(), xmp_payload.begin(), xmp_payload.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res
            = scan_auto(std::span<const std::byte>(file.data(), file.size()),
                        std::span<ContainerBlockRef>(blocks.data(),
                                                     blocks.size()));
        ASSERT_EQ(res.status, ScanStatus::Ok);

        bool found       = false;
        const uint32_t n = (res.written < blocks.size())
                               ? res.written
                               : static_cast<uint32_t>(blocks.size());
        for (uint32_t i = 0; i < n; ++i) {
            const ContainerBlockRef& b = blocks[i];
            if (b.format == ContainerFormat::Heif
                && b.kind == ContainerBlockKind::Xmp
                && b.outer_offset == uuid_off) {
                found = true;
                EXPECT_EQ(b.data_offset, uuid_off + 24U);
                EXPECT_EQ(b.data_size,
                          static_cast<uint64_t>(xmp_payload.size()));
                break;
            }
        }
        EXPECT_TRUE(found);
    }

    TEST(ContainerScan, BmffUuidExifPayload)
    {
        std::vector<std::byte> file;

        // ftyp (heic + mif1)
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

        // uuid Exif box (GeoJP2/Exif UUID) with an Exif preamble + minimal TIFF.
        const std::array<std::byte, 16> exif_uuid = {
            std::byte { 0x4A }, std::byte { 0x70 }, std::byte { 0x67 },
            std::byte { 0x54 }, std::byte { 0x69 }, std::byte { 0x66 },
            std::byte { 0x66 }, std::byte { 0x45 }, std::byte { 0x78 },
            std::byte { 0x69 }, std::byte { 0x66 }, std::byte { 0x2D },
            std::byte { 0x3E }, std::byte { 0x4A }, std::byte { 0x50 },
            std::byte { 0x32 },
        };

        std::vector<std::byte> exif_payload;
        append_bytes(&exif_payload, "Exif");
        exif_payload.push_back(std::byte { 0x00 });
        exif_payload.push_back(std::byte { 0x00 });
        append_bytes(&exif_payload, "II");
        append_u16le(&exif_payload, 42);
        append_u32le(&exif_payload, 8);
        append_u16le(&exif_payload, 0);
        append_u32le(&exif_payload, 0);

        const uint64_t uuid_off  = static_cast<uint64_t>(file.size());
        const uint32_t uuid_size = static_cast<uint32_t>(8U + 16U
                                                         + exif_payload.size());
        append_u32be(&file, uuid_size);
        append_fourcc(&file, fourcc('u', 'u', 'i', 'd'));
        file.insert(file.end(), exif_uuid.begin(), exif_uuid.end());
        file.insert(file.end(), exif_payload.begin(), exif_payload.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res
            = scan_auto(std::span<const std::byte>(file.data(), file.size()),
                        std::span<ContainerBlockRef>(blocks.data(),
                                                     blocks.size()));
        ASSERT_EQ(res.status, ScanStatus::Ok);

        bool found       = false;
        const uint32_t n = (res.written < blocks.size())
                               ? res.written
                               : static_cast<uint32_t>(blocks.size());
        for (uint32_t i = 0; i < n; ++i) {
            const ContainerBlockRef& b = blocks[i];
            if (b.format == ContainerFormat::Heif
                && b.kind == ContainerBlockKind::Exif
                && b.outer_offset == uuid_off) {
                found = true;
                EXPECT_EQ(b.data_offset, uuid_off + 30U);
                EXPECT_EQ(b.data_size, 14U);
                break;
            }
        }
        EXPECT_TRUE(found);
    }


    TEST(ContainerScan, JpegSegments)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        const std::array<std::byte, 14> exif_payload = {
            std::byte { 'E' },  std::byte { 'x' },  std::byte { 'i' },
            std::byte { 'f' },  std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 'I' },  std::byte { 'I' },  std::byte { 0x2A },
            std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 },
        };
        append_jpeg_segment(&jpeg, 0xFFE1, exif_payload);

        std::vector<std::byte> xmp_payload;
        append_bytes(&xmp_payload, "http://ns.adobe.com/xap/1.0/");
        xmp_payload.push_back(std::byte { 0x00 });
        append_bytes(&xmp_payload, "<xmp/>");
        append_jpeg_segment(&jpeg, 0xFFE1, xmp_payload);

        std::vector<std::byte> icc_payload;
        append_bytes(&icc_payload, "ICC_PROFILE");
        icc_payload.push_back(std::byte { 0x00 });
        icc_payload.push_back(std::byte { 0x01 });
        icc_payload.push_back(std::byte { 0x01 });
        append_bytes(&icc_payload, "ICC");
        append_jpeg_segment(&jpeg, 0xFFE2, icc_payload);

        std::vector<std::byte> ps_payload;
        append_bytes(&ps_payload, "Photoshop 3.0");
        ps_payload.push_back(std::byte { 0x00 });
        append_bytes(&ps_payload, "DATA");
        append_jpeg_segment(&jpeg, 0xFFED, ps_payload);

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 4U);
        ASSERT_EQ(res.needed, 4U);

        EXPECT_EQ(blocks[0].format, ContainerFormat::Jpeg);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].id, 0xFFE1U);
        ASSERT_GE(blocks[0].data_size, 4U);
        EXPECT_EQ(jpeg[blocks[0].data_offset + 0], std::byte { 'I' });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 1], std::byte { 'I' });

        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[1].id, 0xFFE1U);
        ASSERT_GE(blocks[1].data_size, 5U);
        EXPECT_EQ(jpeg[blocks[1].data_offset + 0], std::byte { '<' });

        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::Icc);
        EXPECT_EQ(blocks[2].chunking, BlockChunking::JpegApp2SeqTotal);
        EXPECT_EQ(blocks[2].part_index, 0U);
        EXPECT_EQ(blocks[2].part_count, 1U);

        EXPECT_EQ(blocks[3].kind, ContainerBlockKind::PhotoshopIrB);
        EXPECT_EQ(blocks[3].chunking, BlockChunking::PsIrB8Bim);

        const ScanResult auto_res = scan_auto(jpeg, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 4U);

        const ScanResult est_jpeg = measure_scan_jpeg(jpeg);
        EXPECT_EQ(est_jpeg.status, ScanStatus::Ok);
        EXPECT_EQ(est_jpeg.written, 0U);
        EXPECT_EQ(est_jpeg.needed, 4U);

        const ScanResult est_auto = measure_scan_auto(jpeg);
        EXPECT_EQ(est_auto.status, ScanStatus::Ok);
        EXPECT_EQ(est_auto.written, 0U);
        EXPECT_EQ(est_auto.needed, 4U);
    }

    TEST(ContainerScan, JpegRandomAccessMatchesContiguousDescriptors)
    {
        std::vector<std::byte> jpeg = { std::byte { 0xFF },
                                        std::byte { 0xD8 } };

        const std::array<std::byte, 14> exif_payload = {
            std::byte { 'E' },  std::byte { 'x' },  std::byte { 'i' },
            std::byte { 'f' },  std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 'I' },  std::byte { 'I' },  std::byte { 0x2A },
            std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 },
        };
        append_jpeg_segment(&jpeg, 0xFFE1U, exif_payload);

        std::vector<std::byte> bare_xmp(490U, std::byte { ' ' });
        append_bytes(&bare_xmp, "<?xpacket begin=''?>");
        bare_xmp.insert(bare_xmp.end(), 32U, std::byte { ' ' });
        append_jpeg_segment(&jpeg, 0xFFE1U, bare_xmp);

        std::vector<std::byte> icc;
        append_bytes(&icc, "ICC_PROFILE");
        icc.push_back(std::byte { 0x00 });
        icc.push_back(std::byte { 0x01 });
        icc.push_back(std::byte { 0x01 });
        append_bytes(&icc, "profile");
        append_jpeg_segment(&jpeg, 0xFFE2U, icc);

        auto append_jumbf_part = [&](uint32_t sequence,
                                     std::string_view payload) {
            std::vector<std::byte> part;
            append_bytes(&part, "JP");
            part.push_back(std::byte { 0x00 });
            part.push_back(std::byte { 0x00 });
            append_u32be(&part, sequence);
            append_u32be(&part, 12U);
            append_fourcc(&part, fourcc('j', 'u', 'm', 'b'));
            append_bytes(&part, payload);
            append_jpeg_segment(&jpeg, 0xFFEBU, part);
        };
        append_jumbf_part(1U, "AB");
        append_jumbf_part(2U, "CD");

        auto append_ambiguous_app11 = [&](std::string_view payload) {
            std::vector<std::byte> part;
            append_bytes(&part, "JP");
            part.push_back(std::byte { 0x00 });
            part.push_back(std::byte { 0x00 });
            append_u32be(&part, 1U);
            append_u32be(&part, 12U);
            append_fourcc(&part, fourcc('c', '2', 'p', 'a'));
            append_bytes(&part, payload);
            append_jpeg_segment(&jpeg, 0xFFEBU, part);
        };
        append_ambiguous_app11("EF");
        append_ambiguous_app11("GH");

        const std::array<std::byte, 4> app4 = {
            std::byte { 'D' },
            std::byte { 'J' },
            std::byte { 'I' },
            std::byte { '1' },
        };
        append_jpeg_segment(&jpeg, 0xFFE4U, app4);

        std::vector<std::byte> photoshop;
        append_bytes(&photoshop, "Photoshop 3.0");
        photoshop.push_back(std::byte { 0x00 });
        append_bytes(&photoshop, "8BIM");
        append_jpeg_segment(&jpeg, 0xFFEDU, photoshop);

        const std::array<std::byte, 7> comment = {
            std::byte { 'c' }, std::byte { 'o' }, std::byte { 'm' },
            std::byte { 'm' }, std::byte { 'e' }, std::byte { 'n' },
            std::byte { 't' },
        };
        append_jpeg_segment(&jpeg, 0xFFFEU, comment);

        // The positional scanner must stop before entropy-coded image data.
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xDA });
        jpeg.insert(jpeg.end(), 1024U * 1024U, std::byte { 0x7F });

        std::array<ContainerBlockRef, 16> expected {};
        const ScanResult contiguous = scan_jpeg(jpeg, expected);
        ASSERT_EQ(contiguous.status, ScanStatus::Ok);
        ASSERT_GT(contiguous.written, 0U);

        constexpr size_t prefix_size = 37U;
        std::vector<std::byte> backing(prefix_size, std::byte { 0xA5 });
        backing.insert(backing.end(), jpeg.begin(), jpeg.end());
        JpegCallbackState callback { backing };
        const RandomAccessSource source
            = make_callback_random_access_source(backing.size(), &callback,
                                                 jpeg_read_at, true);
        const RandomAccessSourceRange range
            = make_random_access_source_range(source, prefix_size, jpeg.size());
        std::array<std::byte, 512> storage {};
        ContainerRandomAccessScratch scratch;
        scratch.read_window                       = storage;
        scratch.window_options.minimum_read_bytes = storage.size();

        std::array<ContainerBlockRef, 16> actual {};
        const ContainerRandomAccessScanResult positional
            = scan_jpeg_random_access(range, actual, scratch);
        ASSERT_TRUE(positional.complete());
        EXPECT_EQ(positional.scan.status, contiguous.status);
        EXPECT_EQ(positional.scan.written, contiguous.written);
        EXPECT_EQ(positional.scan.needed, contiguous.needed);
        for (uint32_t i = 0U; i < contiguous.written; ++i) {
            expect_same_block(expected[i], actual[i]);
        }
        EXPECT_GT(callback.calls, 0U);
        EXPECT_LT(positional.input.bytes_requested, 16U * 1024U);
        EXPECT_LT(positional.input.bytes_requested,
                  static_cast<uint64_t>(jpeg.size()));

        const ContainerRandomAccessScanResult measured
            = measure_scan_jpeg_random_access(range, scratch);
        EXPECT_TRUE(measured.complete());
        EXPECT_EQ(measured.scan.status, ScanStatus::Ok);
        EXPECT_EQ(measured.scan.written, 0U);
        EXPECT_EQ(measured.scan.needed, contiguous.needed);
    }


    TEST(ContainerScan, JpegRandomAccessReportsScratchAndInputFailures)
    {
        std::vector<std::byte> jpeg = { std::byte { 0xFF },
                                        std::byte { 0xD8 } };
        std::vector<std::byte> app1(600U, std::byte { 'x' });
        append_jpeg_segment(&jpeg, 0xFFE1U, app1);
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 4> blocks {};
        std::array<std::byte, 64> small_storage {};
        ContainerRandomAccessScratch small_scratch;
        small_scratch.read_window                       = small_storage;
        small_scratch.window_options.minimum_read_bytes = small_storage.size();

        JpegCallbackState small_callback { jpeg };
        RandomAccessSource source
            = make_callback_random_access_source(jpeg.size(), &small_callback,
                                                 jpeg_read_at);
        RandomAccessSourceRange range = make_random_access_source_range(source);
        const ContainerRandomAccessScanResult too_small
            = scan_jpeg_random_access(range, blocks, small_scratch);
        EXPECT_FALSE(too_small.complete());
        EXPECT_EQ(too_small.input.code, RandomAccessReadCode::ScratchTooSmall);
        EXPECT_EQ(too_small.input.failure_request_bytes, 512U);

        std::array<std::byte, 512> storage {};
        ContainerRandomAccessScratch scratch;
        scratch.read_window                       = storage;
        scratch.window_options.minimum_read_bytes = storage.size();

        JpegCallbackState io_callback { jpeg };
        io_callback.code = RandomAccessIoCode::IoError;
        source = make_callback_random_access_source(jpeg.size(), &io_callback,
                                                    jpeg_read_at);
        range  = make_random_access_source_range(source);
        const ContainerRandomAccessScanResult io_error
            = scan_jpeg_random_access(range, blocks, scratch);
        EXPECT_EQ(io_error.input.code, RandomAccessReadCode::IoError);
        EXPECT_EQ(io_error.input.io_code, RandomAccessIoCode::IoError);

        JpegCallbackState short_callback { jpeg };
        short_callback.max_bytes = 1U;
        source = make_callback_random_access_source(jpeg.size(),
                                                    &short_callback,
                                                    jpeg_read_at);
        range  = make_random_access_source_range(source);
        const ContainerRandomAccessScanResult short_read
            = scan_jpeg_random_access(range, blocks, scratch);
        EXPECT_EQ(short_read.input.code, RandomAccessReadCode::ShortRead);

        JpegCallbackState limited_callback { jpeg };
        source = make_callback_random_access_source(jpeg.size(),
                                                    &limited_callback,
                                                    jpeg_read_at);
        range  = make_random_access_source_range(source);
        RandomAccessReadLimits limits;
        limits.max_total_bytes = 64U;
        const ContainerRandomAccessScanResult limited
            = scan_jpeg_random_access(range, blocks, scratch, limits);
        EXPECT_EQ(limited.input.code, RandomAccessReadCode::ByteLimitExceeded);
    }


    TEST(ContainerScan, JpegRandomAccessPreservesMalformedAndMemoryBehavior)
    {
        const std::array<std::byte, 6> malformed = {
            std::byte { 0xFF }, std::byte { 0xD8 }, std::byte { 0xFF },
            std::byte { 0xE1 }, std::byte { 0x01 }, std::byte { 0x00 },
        };
        std::array<ContainerBlockRef, 2> blocks {};
        EXPECT_EQ(scan_jpeg(malformed, blocks).status, ScanStatus::Malformed);

        JpegCallbackState callback { malformed };
        RandomAccessSource source
            = make_callback_random_access_source(malformed.size(), &callback,
                                                 jpeg_read_at);
        RandomAccessSourceRange range = make_random_access_source_range(source);
        std::array<std::byte, 512> storage {};
        ContainerRandomAccessScratch scratch;
        scratch.read_window = storage;
        const ContainerRandomAccessScanResult positional
            = scan_jpeg_random_access(range, blocks, scratch);
        EXPECT_TRUE(positional.complete());
        EXPECT_EQ(positional.scan.status, ScanStatus::Malformed);

        const std::array<std::byte, 4> minimal = {
            std::byte { 0xFF },
            std::byte { 0xD8 },
            std::byte { 0xFF },
            std::byte { 0xD9 },
        };
        source = make_memory_random_access_source(minimal);
        range  = make_random_access_source_range(source);
        const ContainerRandomAccessScanResult memory_result
            = scan_jpeg_random_access(range, blocks,
                                      ContainerRandomAccessScratch {});
        EXPECT_TRUE(memory_result.complete());
        EXPECT_EQ(memory_result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(memory_result.input.requests_issued, 0U);
    }


    TEST(ContainerScan,
         ChunkAndBoxRandomAccessMatchesContiguousAndSkipsPayloads)
    {
        constexpr size_t payload_size = 1024U * 1024U;

        auto append_png_chunk = [](std::vector<std::byte>* png, uint32_t type,
                                   std::span<const std::byte> payload) {
            append_u32be(png, static_cast<uint32_t>(payload.size()));
            append_fourcc(png, type);
            png->insert(png->end(), payload.begin(), payload.end());
            append_u32be(png, 0U);  // CRC is outside the shallow scan contract.
        };

        std::vector<std::byte> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };
        std::vector<std::byte> itxt;
        append_bytes(&itxt, "XML:com.adobe.xmp");
        itxt.push_back(std::byte { 0U });
        itxt.push_back(std::byte { 0U });
        itxt.push_back(std::byte { 0U });
        itxt.insert(itxt.end(), 96U, std::byte { 'e' });
        itxt.push_back(std::byte { 0U });
        itxt.insert(itxt.end(), 96U, std::byte { 't' });
        itxt.push_back(std::byte { 0U });
        append_bytes(&itxt, "<x:xmpmeta/>");
        append_png_chunk(&png, fourcc('i', 'T', 'X', 't'), itxt);
        std::vector<std::byte> png_pixels(payload_size, std::byte { 0x55 });
        const uint64_t png_payload_begin = png.size() + 8U;
        append_png_chunk(&png, fourcc('I', 'D', 'A', 'T'), png_pixels);
        append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'), {});
        expect_random_access_parity(
            png, png_payload_begin + 4096U,
            png_payload_begin + payload_size - 4096U,
            [](auto bytes, auto out) { return scan_png(bytes, out); },
            [](const auto& range, auto out, const auto& scratch) {
                return scan_png_random_access(range, out, scratch);
            },
            [](const auto& range, const auto& scratch) {
                return measure_scan_png_random_access(range, scratch);
            });

        std::vector<std::byte> webp;
        append_bytes(&webp, "RIFF");
        append_u32le(&webp, 0U);
        append_bytes(&webp, "WEBP");
        std::vector<std::byte> webp_exif;
        append_bytes(&webp_exif, "Exif");
        webp_exif.push_back(std::byte { 0U });
        webp_exif.push_back(std::byte { 0U });
        append_bytes(&webp_exif, "II");
        webp_exif.push_back(std::byte { 0x2A });
        webp_exif.push_back(std::byte { 0U });
        append_u32le(&webp_exif, 8U);
        append_fourcc(&webp, fourcc('E', 'X', 'I', 'F'));
        append_u32le(&webp, static_cast<uint32_t>(webp_exif.size()));
        webp.insert(webp.end(), webp_exif.begin(), webp_exif.end());
        append_fourcc(&webp, fourcc('V', 'P', '8', ' '));
        append_u32le(&webp, static_cast<uint32_t>(payload_size));
        const uint64_t webp_payload_begin = webp.size();
        webp.insert(webp.end(), payload_size, std::byte { 0x66 });
        const uint32_t riff_size = static_cast<uint32_t>(webp.size() - 8U);
        webp[4U] = std::byte { static_cast<uint8_t>(riff_size >> 0U) };
        webp[5U] = std::byte { static_cast<uint8_t>(riff_size >> 8U) };
        webp[6U] = std::byte { static_cast<uint8_t>(riff_size >> 16U) };
        webp[7U] = std::byte { static_cast<uint8_t>(riff_size >> 24U) };
        expect_random_access_parity(
            webp, webp_payload_begin + 4096U,
            webp_payload_begin + payload_size - 4096U,
            [](auto bytes, auto out) { return scan_webp(bytes, out); },
            [](const auto& range, auto out, const auto& scratch) {
                return scan_webp_random_access(range, out, scratch);
            },
            [](const auto& range, const auto& scratch) {
                return measure_scan_webp_random_access(range, scratch);
            });

        std::vector<std::byte> jp2;
        append_u32be(&jp2, 12U);
        append_fourcc(&jp2, fourcc('j', 'P', ' ', ' '));
        append_u32be(&jp2, 0x0D0A870AU);
        append_u32be(&jp2, static_cast<uint32_t>(8U + payload_size));
        append_fourcc(&jp2, fourcc('j', 'p', '2', 'c'));
        const uint64_t jp2_payload_begin = jp2.size();
        jp2.insert(jp2.end(), payload_size, std::byte { 0x77 });
        const std::array<std::byte, 6> jp2_xmp = {
            std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' },
            std::byte { 'p' }, std::byte { '/' }, std::byte { '>' },
        };
        append_bmff_box(&jp2, fourcc('x', 'm', 'l', ' '), jp2_xmp);
        expect_random_access_parity(
            jp2, jp2_payload_begin + 4096U,
            jp2_payload_begin + payload_size - 4096U,
            [](auto bytes, auto out) { return scan_jp2(bytes, out); },
            [](const auto& range, auto out, const auto& scratch) {
                return scan_jp2_random_access(range, out, scratch);
            },
            [](const auto& range, const auto& scratch) {
                return measure_scan_jp2_random_access(range, scratch);
            });

        std::vector<std::byte> jxl;
        append_u32be(&jxl, 12U);
        append_fourcc(&jxl, fourcc('J', 'X', 'L', ' '));
        append_u32be(&jxl, 0x0D0A870AU);
        append_u32be(&jxl, static_cast<uint32_t>(8U + payload_size));
        append_fourcc(&jxl, fourcc('j', 'x', 'l', 'c'));
        const uint64_t jxl_payload_begin = jxl.size();
        jxl.insert(jxl.end(), payload_size, std::byte { 0x88 });
        const std::array<std::byte, 6> jxl_xmp = jp2_xmp;
        append_bmff_box(&jxl, fourcc('x', 'm', 'l', ' '), jxl_xmp);
        expect_random_access_parity(
            jxl, jxl_payload_begin + 4096U,
            jxl_payload_begin + payload_size - 4096U,
            [](auto bytes, auto out) { return scan_jxl(bytes, out); },
            [](const auto& range, auto out, const auto& scratch) {
                return scan_jxl_random_access(range, out, scratch);
            },
            [](const auto& range, const auto& scratch) {
                return measure_scan_jxl_random_access(range, scratch);
            });

        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2U);
        append_u16be(&infe_payload, 1U);
        append_u16be(&infe_payload, 0U);
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "xmp");
        infe_payload.push_back(std::byte { 0U });
        append_bytes(&infe_payload, "application/rdf+xml");
        infe_payload.push_back(std::byte { 0U });
        std::vector<std::byte> infe;
        append_bmff_box(&infe, fourcc('i', 'n', 'f', 'e'), infe_payload);
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 0U);
        append_u16be(&iinf_payload, 1U);
        iinf_payload.insert(iinf_payload.end(), infe.begin(), infe.end());
        std::vector<std::byte> iinf;
        append_bmff_box(&iinf, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1U);
        iloc_payload.push_back(std::byte { 0x44 });
        iloc_payload.push_back(std::byte { 0x40 });
        append_u16be(&iloc_payload, 1U);
        append_u16be(&iloc_payload, 1U);
        append_u16be(&iloc_payload, 1U);  // construction_method = idat
        append_u16be(&iloc_payload, 0U);
        append_u32be(&iloc_payload, 0U);
        append_u16be(&iloc_payload, 1U);
        append_u32be(&iloc_payload, 0U);
        append_u32be(&iloc_payload, static_cast<uint32_t>(jp2_xmp.size()));
        std::vector<std::byte> iloc;
        append_bmff_box(&iloc, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> idat;
        append_bmff_box(&idat, fourcc('i', 'd', 'a', 't'), jp2_xmp);
        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0U);
        meta_payload.insert(meta_payload.end(), iinf.begin(), iinf.end());
        meta_payload.insert(meta_payload.end(), iloc.begin(), iloc.end());
        meta_payload.insert(meta_payload.end(), idat.begin(), idat.end());

        std::vector<std::byte> bmff;
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0U);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&bmff, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        append_u32be(&bmff, static_cast<uint32_t>(8U + payload_size));
        append_fourcc(&bmff, fourcc('m', 'd', 'a', 't'));
        const uint64_t bmff_payload_begin = bmff.size();
        bmff.insert(bmff.end(), payload_size, std::byte { 0x99 });
        append_bmff_box(&bmff, fourcc('m', 'e', 't', 'a'), meta_payload);
        expect_random_access_parity(
            bmff, bmff_payload_begin + 4096U,
            bmff_payload_begin + payload_size - 4096U,
            [](auto bytes, auto out) { return scan_bmff(bytes, out); },
            [](const auto& range, auto out, const auto& scratch) {
                return scan_bmff_random_access(range, out, scratch);
            },
            [](const auto& range, const auto& scratch) {
                return measure_scan_bmff_random_access(range, scratch);
            });
    }


    TEST(ContainerScan, ChunkAndBoxRandomAccessReportsScratchAndIoFailures)
    {
        const std::array<std::byte, 8> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };
        JpegCallbackState callback { png };
        RandomAccessSource source
            = make_callback_random_access_source(png.size(), &callback,
                                                 jpeg_read_at);
        RandomAccessSourceRange range = make_random_access_source_range(source);
        std::array<ContainerBlockRef, 2> blocks {};
        std::array<std::byte, 31> small_storage {};
        ContainerRandomAccessScratch small;
        small.read_window = small_storage;
        const ContainerRandomAccessScanResult too_small
            = scan_png_random_access(range, blocks, small);
        EXPECT_EQ(too_small.input.code, RandomAccessReadCode::ScratchTooSmall);
        EXPECT_EQ(too_small.input.failure_request_bytes, 32U);

        std::array<std::byte, 32> storage {};
        ContainerRandomAccessScratch scratch;
        scratch.read_window = storage;
        callback.code       = RandomAccessIoCode::IoError;
        const ContainerRandomAccessScanResult io_error
            = scan_png_random_access(range, blocks, scratch);
        EXPECT_EQ(io_error.input.code, RandomAccessReadCode::IoError);

        source = make_memory_random_access_source(png);
        range  = make_random_access_source_range(source);
        const ContainerRandomAccessScanResult memory
            = scan_png_random_access(range, blocks,
                                     ContainerRandomAccessScratch {});
        EXPECT_TRUE(memory.complete());
        EXPECT_EQ(memory.input.requests_issued, 0U);
        EXPECT_EQ(memory.scan.status, ScanStatus::Ok);
    }


    TEST(ContainerScan, JpegApp1BareXmpPacket)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        std::vector<std::byte> xmp_payload;
        append_bytes(
            &xmp_payload,
            "<?xpacket begin=''?>"
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description rdf:about='camera'/>"
            "</rdf:RDF>"
            "</x:xmpmeta>");
        append_jpeg_segment(&jpeg, 0xFFE1, xmp_payload);

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 4> blocks {};
        const ScanResult res = scan_jpeg(jpeg, blocks);

        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Jpeg);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[0].id, 0xFFE1U);
        EXPECT_EQ(jpeg[blocks[0].data_offset], std::byte { '<' });
        EXPECT_EQ(blocks[0].data_size,
                  static_cast<uint64_t>(xmp_payload.size()));
    }

    TEST(ContainerScan, JpegApp11JumbfOneBasedReassembly)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        auto append_app11_part = [&](uint32_t seq,
                                     std::string_view payload_bytes) {
            std::vector<std::byte> seg;
            append_bytes(&seg, "JP");
            seg.push_back(std::byte { 0x00 });
            seg.push_back(std::byte { 0x00 });
            append_u32be(&seg, seq);
            append_u32be(&seg, 12U);  // 8-byte BMFF header + 4-byte payload
            append_fourcc(&seg, fourcc('j', 'u', 'm', 'b'));
            append_bytes(&seg, payload_bytes);
            append_jpeg_segment(&jpeg, 0xFFEB, seg);
        };

        append_app11_part(1U, "AB");
        append_app11_part(2U, "CD");
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 2U);

        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[0].id, 0xFFEBU);
        EXPECT_EQ(blocks[0].part_index, 0U);
        EXPECT_EQ(blocks[0].part_count, 2U);
        ASSERT_GE(blocks[0].data_size, 10U);
        EXPECT_EQ(jpeg[blocks[0].data_offset + 0], std::byte { 0x00 });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 1], std::byte { 0x00 });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 2], std::byte { 0x00 });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 3], std::byte { 0x0C });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 4], std::byte { 'j' });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 5], std::byte { 'u' });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 6], std::byte { 'm' });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 7], std::byte { 'b' });

        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[1].id, 0xFFEBU);
        EXPECT_EQ(blocks[1].part_index, 1U);
        EXPECT_EQ(blocks[1].part_count, 2U);
        ASSERT_GE(blocks[1].data_size, 2U);
        EXPECT_EQ(jpeg[blocks[1].data_offset + 0], std::byte { 'C' });
        EXPECT_EQ(jpeg[blocks[1].data_offset + 1], std::byte { 'D' });

        const ScanResult auto_res = scan_auto(jpeg, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[0].part_index, 0U);
        EXPECT_EQ(blocks[0].part_count, 2U);
        EXPECT_EQ(blocks[1].part_index, 1U);
        EXPECT_EQ(blocks[1].part_count, 2U);
    }

    TEST(ContainerScan, JpegApp11JumbfAmbiguousGroupFallsBackToStandaloneParts)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        auto append_app11_part = [&](uint32_t seq,
                                     std::string_view payload_bytes) {
            std::vector<std::byte> seg;
            append_bytes(&seg, "JP");
            seg.push_back(std::byte { 0x00 });
            seg.push_back(std::byte { 0x00 });
            append_u32be(&seg, seq);
            append_u32be(&seg, 12U);  // 8-byte BMFF header + 4-byte payload
            append_fourcc(&seg, fourcc('j', 'u', 'm', 'b'));
            append_bytes(&seg, payload_bytes);
            append_jpeg_segment(&jpeg, 0xFFEB, seg);
        };

        // Duplicate sequence values with the same APP11 header shape are
        // ambiguous for multipart reassembly; scanner should keep each segment
        // as an independent block that still includes its BMFF header bytes.
        append_app11_part(1U, "ABCD");
        append_app11_part(1U, "WXYZ");
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 2U);

        for (uint32_t i = 0U; i < 2U; ++i) {
            EXPECT_EQ(blocks[i].kind, ContainerBlockKind::Jumbf);
            EXPECT_EQ(blocks[i].id, 0xFFEBU);
            EXPECT_EQ(blocks[i].part_index, 0U);
            EXPECT_EQ(blocks[i].part_count, 1U);
            ASSERT_GE(blocks[i].data_size, 12U);
            EXPECT_EQ(jpeg[blocks[i].data_offset + 0], std::byte { 0x00 });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 1], std::byte { 0x00 });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 2], std::byte { 0x00 });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 3], std::byte { 0x0C });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 4], std::byte { 'j' });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 5], std::byte { 'u' });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 6], std::byte { 'm' });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 7], std::byte { 'b' });
        }

        const ScanResult auto_res = scan_auto(jpeg, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 2U);
        for (uint32_t i = 0U; i < 2U; ++i) {
            EXPECT_EQ(blocks[i].kind, ContainerBlockKind::Jumbf);
            EXPECT_EQ(blocks[i].part_index, 0U);
            EXPECT_EQ(blocks[i].part_count, 1U);
        }
    }

    TEST(ContainerScan,
         JpegApp11JumbfNonContiguousSequenceFallsBackToStandaloneParts)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        auto append_app11_part = [&](uint32_t seq,
                                     std::string_view payload_bytes) {
            std::vector<std::byte> seg;
            append_bytes(&seg, "JP");
            seg.push_back(std::byte { 0x00 });
            seg.push_back(std::byte { 0x00 });
            append_u32be(&seg, seq);
            append_u32be(&seg, 12U);  // 8-byte BMFF header + 4-byte payload
            append_fourcc(&seg, fourcc('j', 'u', 'm', 'b'));
            append_bytes(&seg, payload_bytes);
            append_jpeg_segment(&jpeg, 0xFFEB, seg);
        };

        // Non-contiguous sequence indices (1 and 3) are ambiguous for stream
        // reassembly and should remain standalone JUMBF segments.
        append_app11_part(1U, "ABCD");
        append_app11_part(3U, "WXYZ");
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 2U);
        for (uint32_t i = 0U; i < 2U; ++i) {
            EXPECT_EQ(blocks[i].kind, ContainerBlockKind::Jumbf);
            EXPECT_EQ(blocks[i].id, 0xFFEBU);
            EXPECT_EQ(blocks[i].part_index, 0U);
            EXPECT_EQ(blocks[i].part_count, 1U);
            ASSERT_GE(blocks[i].data_size, 12U);
            EXPECT_EQ(jpeg[blocks[i].data_offset + 0], std::byte { 0x00 });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 1], std::byte { 0x00 });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 2], std::byte { 0x00 });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 3], std::byte { 0x0C });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 4], std::byte { 'j' });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 5], std::byte { 'u' });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 6], std::byte { 'm' });
            EXPECT_EQ(jpeg[blocks[i].data_offset + 7], std::byte { 'b' });
        }

        const ScanResult auto_res = scan_auto(jpeg, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 2U);
        for (uint32_t i = 0U; i < 2U; ++i) {
            EXPECT_EQ(blocks[i].kind, ContainerBlockKind::Jumbf);
            EXPECT_EQ(blocks[i].part_index, 0U);
            EXPECT_EQ(blocks[i].part_count, 1U);
        }
    }


    TEST(ContainerScan, JpegFlirApp1)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        std::vector<std::byte> flir0;
        append_bytes(&flir0, "FLIR");
        flir0.push_back(std::byte { 0x00 });
        flir0.push_back(std::byte { 0x01 });
        flir0.push_back(std::byte { 0x00 });  // part 0
        flir0.push_back(std::byte { 0x01 });  // total-1 (2 parts)
        append_bytes(&flir0, "FFF");
        flir0.push_back(std::byte { 0x00 });
        append_jpeg_segment(&jpeg, 0xFFE1, flir0);

        std::vector<std::byte> flir1;
        append_bytes(&flir1, "FLIR");
        flir1.push_back(std::byte { 0x00 });
        flir1.push_back(std::byte { 0x01 });
        flir1.push_back(std::byte { 0x01 });  // part 1
        flir1.push_back(std::byte { 0x01 });  // total-1 (2 parts)
        append_bytes(&flir1, "DATA");
        append_jpeg_segment(&jpeg, 0xFFE1, flir1);

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 2U);

        EXPECT_EQ(blocks[0].format, ContainerFormat::Jpeg);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::MakerNote);
        EXPECT_EQ(blocks[0].id, 0xFFE1U);
        EXPECT_EQ(blocks[0].aux_u32, fourcc('F', 'L', 'I', 'R'));
        EXPECT_EQ(blocks[0].group,
                  static_cast<uint64_t>(fourcc('F', 'L', 'I', 'R')));
        EXPECT_EQ(blocks[0].part_index, 0U);
        EXPECT_EQ(blocks[0].part_count, 2U);
        ASSERT_GE(blocks[0].data_size, 4U);
        EXPECT_EQ(jpeg[blocks[0].data_offset + 0], std::byte { 'F' });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 1], std::byte { 'F' });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 2], std::byte { 'F' });

        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::MakerNote);
        EXPECT_EQ(blocks[1].aux_u32, fourcc('F', 'L', 'I', 'R'));
        EXPECT_EQ(blocks[1].part_index, 1U);
        EXPECT_EQ(blocks[1].part_count, 2U);
    }


    static void append_png_chunk(std::vector<std::byte>* out, uint32_t type,
                                 std::span<const std::byte> data)
    {
        append_u32be(out, static_cast<uint32_t>(data.size()));
        append_fourcc(out, type);
        out->insert(out->end(), data.begin(), data.end());
        append_u32be(out, 0);  // crc ignored by scanner
    }


    TEST(ContainerScan, PngChunks)
    {
        std::vector<std::byte> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };

        // Uncompressed XMP iTXt.
        std::vector<std::byte> itxt0;
        append_bytes(&itxt0, "XML:com.adobe.xmp");
        itxt0.push_back(std::byte { 0x00 });
        itxt0.push_back(std::byte { 0x00 });  // comp flag
        itxt0.push_back(std::byte { 0x00 });  // comp method
        itxt0.push_back(std::byte { 0x00 });  // lang
        itxt0.push_back(std::byte { 0x00 });  // trans
        append_bytes(&itxt0, "<xmp/>");
        append_png_chunk(&png, fourcc('i', 'T', 'X', 't'), itxt0);

        // Compressed XMP iTXt (still dummy data).
        std::vector<std::byte> itxt1;
        append_bytes(&itxt1, "XML:com.adobe.xmp");
        itxt1.push_back(std::byte { 0x00 });
        itxt1.push_back(std::byte { 0x01 });  // comp flag
        itxt1.push_back(std::byte { 0x00 });  // comp method
        itxt1.push_back(std::byte { 0x00 });  // lang
        itxt1.push_back(std::byte { 0x00 });  // trans
        append_bytes(&itxt1, "Z");
        append_png_chunk(&png, fourcc('i', 'T', 'X', 't'), itxt1);

        std::vector<std::byte> itxt2;
        append_bytes(&itxt2, "Description");
        itxt2.push_back(std::byte { 0x00 });
        itxt2.push_back(std::byte { 0x00 });  // comp flag
        itxt2.push_back(std::byte { 0x00 });  // comp method
        itxt2.push_back(std::byte { 0x00 });  // lang
        itxt2.push_back(std::byte { 0x00 });  // trans
        append_bytes(&itxt2, "OpenMeta PNG");
        append_png_chunk(&png, fourcc('i', 'T', 'X', 't'), itxt2);

        // iCCP with dummy compressed bytes.
        std::vector<std::byte> iccp;
        append_bytes(&iccp, "icc");
        iccp.push_back(std::byte { 0x00 });
        iccp.push_back(std::byte { 0x00 });
        append_bytes(&iccp, "Z");
        append_png_chunk(&png, fourcc('i', 'C', 'C', 'P'), iccp);

        // eXIf with TIFF header.
        const std::array<std::byte, 8> exif
            = { std::byte { 'I' },  std::byte { 'I' },  std::byte { 0x2A },
                std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
                std::byte { 0x00 }, std::byte { 0x00 } };
        append_png_chunk(&png, fourcc('e', 'X', 'I', 'f'), exif);

        append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'), {});

        std::array<ContainerBlockRef, 16> blocks {};
        const ScanResult res = scan_png(png, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 5U);

        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[0].compression, BlockCompression::None);
        EXPECT_EQ(png[blocks[0].data_offset], std::byte { '<' });

        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[1].compression, BlockCompression::Deflate);

        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::Text);
        EXPECT_EQ(blocks[2].compression, BlockCompression::None);

        EXPECT_EQ(blocks[3].kind, ContainerBlockKind::Icc);
        EXPECT_EQ(blocks[3].compression, BlockCompression::Deflate);

        EXPECT_EQ(blocks[4].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(png[blocks[4].data_offset + 0], std::byte { 'I' });

        const ScanResult auto_res = scan_auto(png, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 5U);
    }

    TEST(ContainerScan, PngCaBxChunkClassifiedAsJumbf)
    {
        std::vector<std::byte> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };
        const std::array<std::byte, 4> payload = {
            std::byte { 'J' },
            std::byte { 'U' },
            std::byte { 'M' },
            std::byte { 'B' },
        };
        append_png_chunk(&png, fourcc('c', 'a', 'B', 'X'),
                         std::span<const std::byte>(payload.data(),
                                                    payload.size()));
        append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'),
                         std::span<const std::byte> {});

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_png(png, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[0].id, fourcc('c', 'a', 'B', 'X'));
        EXPECT_EQ(blocks[0].data_size, payload.size());
        EXPECT_EQ(png[blocks[0].data_offset + 0], std::byte { 'J' });

        const ScanResult auto_res = scan_auto(png, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
    }


    TEST(ContainerScan, WebpRiffChunks)
    {
        std::vector<std::byte> webp;
        append_bytes(&webp, "RIFF");
        append_u32le(&webp, 0);  // placeholder
        append_bytes(&webp, "WEBP");

        std::vector<std::byte> exif;
        append_bytes(&exif, "Exif");
        exif.push_back(std::byte { 0x00 });
        exif.push_back(std::byte { 0x00 });
        append_bytes(&exif, "II");
        exif.push_back(std::byte { 0x2A });
        exif.push_back(std::byte { 0x00 });
        append_u32le(&exif, 8);

        append_fourcc(&webp, fourcc('E', 'X', 'I', 'F'));
        append_u32le(&webp, static_cast<uint32_t>(exif.size()));
        webp.insert(webp.end(), exif.begin(), exif.end());
        if ((exif.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        std::vector<std::byte> xmp;
        append_bytes(&xmp, "<xmp/>");
        append_fourcc(&webp, fourcc('X', 'M', 'P', ' '));
        append_u32le(&webp, static_cast<uint32_t>(xmp.size()));
        webp.insert(webp.end(), xmp.begin(), xmp.end());
        if ((xmp.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        std::vector<std::byte> icc;
        append_bytes(&icc, "ICC");
        append_fourcc(&webp, fourcc('I', 'C', 'C', 'P'));
        append_u32le(&webp, static_cast<uint32_t>(icc.size()));
        webp.insert(webp.end(), icc.begin(), icc.end());
        if ((icc.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        const uint32_t riff_size = static_cast<uint32_t>(webp.size() - 8);
        webp[4] = std::byte { static_cast<uint8_t>((riff_size >> 0) & 0xFF) };
        webp[5] = std::byte { static_cast<uint8_t>((riff_size >> 8) & 0xFF) };
        webp[6] = std::byte { static_cast<uint8_t>((riff_size >> 16) & 0xFF) };
        webp[7] = std::byte { static_cast<uint8_t>((riff_size >> 24) & 0xFF) };

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_webp(webp, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 3U);

        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(webp[blocks[0].data_offset], std::byte { 'I' });
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::Icc);
    }

    TEST(ContainerScan, WebpC2paChunkClassifiedAsJumbf)
    {
        std::vector<std::byte> webp;
        append_bytes(&webp, "RIFF");
        append_u32le(&webp, 0);  // placeholder
        append_bytes(&webp, "WEBP");

        const std::array<std::byte, 4> c2pa_payload = {
            std::byte { 'J' },
            std::byte { 'U' },
            std::byte { 'M' },
            std::byte { 'B' },
        };
        append_fourcc(&webp, fourcc('C', '2', 'P', 'A'));
        append_u32le(&webp, static_cast<uint32_t>(c2pa_payload.size()));
        webp.insert(webp.end(), c2pa_payload.begin(), c2pa_payload.end());
        if ((c2pa_payload.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        const uint32_t riff_size = static_cast<uint32_t>(webp.size() - 8U);
        webp[4] = std::byte { static_cast<uint8_t>((riff_size >> 0) & 0xFF) };
        webp[5] = std::byte { static_cast<uint8_t>((riff_size >> 8) & 0xFF) };
        webp[6] = std::byte { static_cast<uint8_t>((riff_size >> 16) & 0xFF) };
        webp[7] = std::byte { static_cast<uint8_t>((riff_size >> 24) & 0xFF) };

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_webp(webp, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[0].id, fourcc('C', '2', 'P', 'A'));
        EXPECT_EQ(blocks[0].data_size, c2pa_payload.size());
        EXPECT_EQ(webp[blocks[0].data_offset + 0], std::byte { 'J' });

        const ScanResult auto_res = scan_auto(webp, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
    }

    TEST(ContainerScan, WebpC2paOddSizePaddingAndFollowingChunk)
    {
        std::vector<std::byte> webp;
        append_bytes(&webp, "RIFF");
        append_u32le(&webp, 0);  // placeholder
        append_bytes(&webp, "WEBP");

        const std::array<std::byte, 5> c2pa_payload = {
            std::byte { 'J' }, std::byte { 'U' }, std::byte { 'M' },
            std::byte { 'B' }, std::byte { '!' },
        };
        append_fourcc(&webp, fourcc('C', '2', 'P', 'A'));
        append_u32le(&webp, static_cast<uint32_t>(c2pa_payload.size()));
        webp.insert(webp.end(), c2pa_payload.begin(), c2pa_payload.end());
        if ((c2pa_payload.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });  // RIFF pad byte
        }

        std::vector<std::byte> xmp;
        append_bytes(&xmp, "<x/>");
        append_fourcc(&webp, fourcc('X', 'M', 'P', ' '));
        append_u32le(&webp, static_cast<uint32_t>(xmp.size()));
        webp.insert(webp.end(), xmp.begin(), xmp.end());
        if ((xmp.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        const uint32_t riff_size = static_cast<uint32_t>(webp.size() - 8U);
        webp[4] = std::byte { static_cast<uint8_t>((riff_size >> 0) & 0xFF) };
        webp[5] = std::byte { static_cast<uint8_t>((riff_size >> 8) & 0xFF) };
        webp[6] = std::byte { static_cast<uint8_t>((riff_size >> 16) & 0xFF) };
        webp[7] = std::byte { static_cast<uint8_t>((riff_size >> 24) & 0xFF) };

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_webp(webp, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[0].id, fourcc('C', '2', 'P', 'A'));
        EXPECT_EQ(blocks[0].data_size, c2pa_payload.size());
        EXPECT_EQ(blocks[0].outer_size,
                  14U);  // 8-byte header + 5 bytes + 1 pad
        EXPECT_EQ(webp[blocks[0].data_offset + 4], std::byte { '!' });
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);

        const ScanResult auto_res = scan_auto(webp, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
    }


    TEST(ContainerScan, GifApplicationExtensions)
    {
        std::vector<std::byte> gif;
        append_bytes(&gif, "GIF89a");
        // Logical Screen Descriptor: 1x1, no global color table.
        gif.push_back(std::byte { 0x01 });
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x01 });
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x00 });

        gif.push_back(std::byte { 0x21 });
        gif.push_back(std::byte { 0xFF });
        gif.push_back(std::byte { 0x0B });
        append_bytes(&gif, "XMP Data");
        append_bytes(&gif, "XMP");
        gif.push_back(std::byte { 0x03 });
        append_bytes(&gif, "abc");
        gif.push_back(std::byte { 0x00 });

        gif.push_back(std::byte { 0x21 });
        gif.push_back(std::byte { 0xFE });
        gif.push_back(std::byte { 0x07 });
        append_bytes(&gif, "comment");
        gif.push_back(std::byte { 0x00 });

        gif.push_back(std::byte { 0x3B });

        std::array<ContainerBlockRef, 4> blocks {};
        const ScanResult res = scan_gif(gif, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[0].chunking, BlockChunking::GifSubBlocks);
        EXPECT_EQ(gif[blocks[0].data_offset], std::byte { 0x03 });
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Comment);
        EXPECT_EQ(blocks[1].chunking, BlockChunking::GifSubBlocks);
        EXPECT_EQ(gif[blocks[1].data_offset], std::byte { 0x07 });
    }


    TEST(ContainerScan, Jp2AndJxlBoxes)
    {
        std::vector<std::byte> jp2;
        append_u32be(&jp2, 12);
        append_fourcc(&jp2, fourcc('j', 'P', ' ', ' '));
        append_u32be(&jp2, 0x0D0A870A);

        // jp2h with colr (method=2 + ICC bytes).
        std::vector<std::byte> colr;
        colr.push_back(std::byte { 0x02 });
        colr.push_back(std::byte { 0x00 });
        colr.push_back(std::byte { 0x00 });
        append_bytes(&colr, "ICC");

        std::vector<std::byte> colr_box;
        append_u32be(&colr_box, static_cast<uint32_t>(8 + colr.size()));
        append_fourcc(&colr_box, fourcc('c', 'o', 'l', 'r'));
        colr_box.insert(colr_box.end(), colr.begin(), colr.end());

        std::vector<std::byte> jp2h_box;
        append_u32be(&jp2h_box, static_cast<uint32_t>(8 + colr_box.size()));
        append_fourcc(&jp2h_box, fourcc('j', 'p', '2', 'h'));
        jp2h_box.insert(jp2h_box.end(), colr_box.begin(), colr_box.end());
        jp2.insert(jp2.end(), jp2h_box.begin(), jp2h_box.end());

        // uuid (XMP) box.
        const std::array<std::byte, 16> xmp_uuid = {
            std::byte { 0xbe }, std::byte { 0x7a }, std::byte { 0xcf },
            std::byte { 0xcb }, std::byte { 0x97 }, std::byte { 0xa9 },
            std::byte { 0x42 }, std::byte { 0xe8 }, std::byte { 0x9c },
            std::byte { 0x71 }, std::byte { 0x99 }, std::byte { 0x94 },
            std::byte { 0x91 }, std::byte { 0xe3 }, std::byte { 0xaf },
            std::byte { 0xac },
        };
        std::vector<std::byte> uuid_payload;
        uuid_payload.insert(uuid_payload.end(), xmp_uuid.begin(),
                            xmp_uuid.end());
        append_bytes(&uuid_payload, "<xmp/>");
        append_u32be(&jp2, static_cast<uint32_t>(8 + uuid_payload.size()));
        append_fourcc(&jp2, fourcc('u', 'u', 'i', 'd'));
        jp2.insert(jp2.end(), uuid_payload.begin(), uuid_payload.end());

        // uuid (GeoTIFF) box.
        const std::array<std::byte, 16> geo_uuid = {
            std::byte { 0xb1 }, std::byte { 0x4b }, std::byte { 0xf8 },
            std::byte { 0xbd }, std::byte { 0x08 }, std::byte { 0x3d },
            std::byte { 0x4b }, std::byte { 0x43 }, std::byte { 0xa5 },
            std::byte { 0xae }, std::byte { 0x8c }, std::byte { 0xd7 },
            std::byte { 0xd5 }, std::byte { 0xa6 }, std::byte { 0xce },
            std::byte { 0x03 },
        };
        std::vector<std::byte> geo_payload;
        geo_payload.insert(geo_payload.end(), geo_uuid.begin(), geo_uuid.end());
        // Minimal TIFF payload: IFD0 with one ImageWidth entry.
        append_bytes(&geo_payload, "II");
        append_u16le(&geo_payload, 42);
        append_u32le(&geo_payload, 8);
        append_u16le(&geo_payload, 1);
        append_u16le(&geo_payload, 0x0100);
        append_u16le(&geo_payload, 4);
        append_u32le(&geo_payload, 1);
        append_u32le(&geo_payload, 2717);
        append_u32le(&geo_payload, 0);
        append_u32be(&jp2, static_cast<uint32_t>(8 + geo_payload.size()));
        append_fourcc(&jp2, fourcc('u', 'u', 'i', 'd'));
        jp2.insert(jp2.end(), geo_payload.begin(), geo_payload.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult jp2_res = scan_jp2(jp2, blocks);
        ASSERT_EQ(jp2_res.status, ScanStatus::Ok);
        ASSERT_EQ(jp2_res.written, 3U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Icc);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::Exif);

        // JXL container: signature + Exif + xml + brob(xml).
        std::vector<std::byte> jxl;
        append_u32be(&jxl, 12);
        append_fourcc(&jxl, fourcc('J', 'X', 'L', ' '));
        append_u32be(&jxl, 0x0D0A870A);

        std::vector<std::byte> exif_box_payload;
        append_u32be(&exif_box_payload, 0);
        append_bytes(&exif_box_payload, "II");
        exif_box_payload.push_back(std::byte { 0x2A });
        exif_box_payload.push_back(std::byte { 0x00 });
        append_u32le(&exif_box_payload, 8);
        append_u32be(&jxl, static_cast<uint32_t>(8 + exif_box_payload.size()));
        append_fourcc(&jxl, fourcc('E', 'x', 'i', 'f'));
        jxl.insert(jxl.end(), exif_box_payload.begin(), exif_box_payload.end());

        std::vector<std::byte> xml_payload;
        append_bytes(&xml_payload, "<xmp/>");
        append_u32be(&jxl, static_cast<uint32_t>(8 + xml_payload.size()));
        append_fourcc(&jxl, fourcc('x', 'm', 'l', ' '));
        jxl.insert(jxl.end(), xml_payload.begin(), xml_payload.end());

        std::vector<std::byte> brob_payload;
        append_fourcc(&brob_payload, fourcc('x', 'm', 'l', ' '));
        append_bytes(&brob_payload, "zzz");
        append_u32be(&jxl, static_cast<uint32_t>(8 + brob_payload.size()));
        append_fourcc(&jxl, fourcc('b', 'r', 'o', 'b'));
        jxl.insert(jxl.end(), brob_payload.begin(), brob_payload.end());

        const ScanResult jxl_res = scan_jxl(jxl, blocks);
        ASSERT_EQ(jxl_res.status, ScanStatus::Ok);
        ASSERT_EQ(jxl_res.written, 3U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].chunking, BlockChunking::BmffExifTiffOffsetU32Be);
        EXPECT_EQ(blocks[0].aux_u32, 0U);
        EXPECT_EQ(jxl[blocks[0].data_offset], std::byte { 'I' });
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::CompressedMetadata);
        EXPECT_EQ(blocks[2].compression, BlockCompression::Brotli);
        EXPECT_EQ(blocks[2].aux_u32, fourcc('x', 'm', 'l', ' '));
    }


    TEST(ContainerScan, Jp2DirectXmlAndExifBoxes)
    {
        std::vector<std::byte> jp2;
        append_u32be(&jp2, 12);
        append_fourcc(&jp2, fourcc('j', 'P', ' ', ' '));
        append_u32be(&jp2, 0x0D0A870A);

        std::vector<std::byte> xml_payload;
        append_bytes(&xml_payload, "<xmp/>");
        append_bmff_box(&jp2, fourcc('x', 'm', 'l', ' '),
                        std::span<const std::byte>(xml_payload.data(),
                                                   xml_payload.size()));

        std::vector<std::byte> exif_payload;
        append_u32be(&exif_payload, 0);
        append_bytes(&exif_payload, "II");
        exif_payload.push_back(std::byte { 0x2A });
        exif_payload.push_back(std::byte { 0x00 });
        append_u32le(&exif_payload, 8);
        append_bmff_box(&jp2, fourcc('E', 'x', 'i', 'f'),
                        std::span<const std::byte>(exif_payload.data(),
                                                   exif_payload.size()));

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_jp2(jp2, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 2U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Jp2);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[1].format, ContainerFormat::Jp2);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[1].chunking, BlockChunking::BmffExifTiffOffsetU32Be);
        EXPECT_EQ(blocks[1].aux_u32, 0U);
    }


    TEST(ContainerScan, BmffMetaItems)
    {
        // Build a tiny ISO-BMFF file with Exif + XMP items stored in `meta`/`idat`.
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0x00 });

        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> infe_xmp_payload;
        append_fullbox_header(&infe_xmp_payload, 2);
        append_u16be(&infe_xmp_payload, 2);  // item_ID
        append_u16be(&infe_xmp_payload, 0);  // protection
        append_fourcc(&infe_xmp_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_xmp_payload, "xmp");
        infe_xmp_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_xmp_payload, "application/xmp+xml");
        infe_xmp_payload.push_back(std::byte { 0x00 });
        // Some real-world files omit the encoding terminator (treat as empty).

        std::vector<std::byte> infe_xmp_box;
        append_bmff_box(&infe_xmp_box, fourcc('i', 'n', 'f', 'e'),
                        infe_xmp_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 2);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        iinf_payload.insert(iinf_payload.end(), infe_xmp_box.begin(),
                            infe_xmp_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_u32be(&idat_payload, 0);  // TIFF header offset
        append_bytes(&idat_payload, "II");
        idat_payload.push_back(std::byte { 0x2A });
        idat_payload.push_back(std::byte { 0x00 });
        append_u32le(&idat_payload, 8);
        const uint32_t xmp_off = static_cast<uint32_t>(idat_payload.size());
        append_bytes(&idat_payload, "<xmp/>");
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 2);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset (within idat)
        append_u32be(&iloc_payload, xmp_off);

        append_u16be(&iloc_payload, 2);        // item_ID
        append_u16be(&iloc_payload, 1);        // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);        // data_reference_index
        append_u16be(&iloc_payload, 1);        // extent_count
        append_u32be(&iloc_payload, xmp_off);  // extent_offset (within idat)
        append_u32be(&iloc_payload,
                     static_cast<uint32_t>(idat_payload.size() - xmp_off));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // Minimal item property container with an ICC profile in `colr` (prof).
        std::vector<std::byte> colr_payload;
        append_fourcc(&colr_payload, fourcc('p', 'r', 'o', 'f'));
        append_bytes(&colr_payload, "ICC");
        std::vector<std::byte> colr_box;
        append_bmff_box(&colr_box, fourcc('c', 'o', 'l', 'r'), colr_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), colr_box.begin(),
                            colr_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        struct Case final {
            uint32_t major_brand       = 0;
            ContainerFormat expect_fmt = ContainerFormat::Unknown;
        };

        const std::array<Case, 6> cases = {
            Case { fourcc('h', 'e', 'i', 'c'), ContainerFormat::Heif },
            Case { fourcc('a', 'v', 'i', 'f'), ContainerFormat::Avif },
            Case { fourcc('a', 'v', 'i', 's'), ContainerFormat::Avif },
            Case { fourcc('a', 'v', 'i', 'o'), ContainerFormat::Avif },
            Case { fourcc('c', 'r', 'x', ' '), ContainerFormat::Cr3 },
            Case { fourcc('j', 'p', 'h', ' '), ContainerFormat::Jp2 },
        };

        for (const Case& c : cases) {
            std::vector<std::byte> ftyp_payload;
            append_fourcc(&ftyp_payload, c.major_brand);
            append_u32be(&ftyp_payload, 0);
            append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
            std::vector<std::byte> file;
            append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
            file.insert(file.end(), meta_box.begin(), meta_box.end());

            std::array<ContainerBlockRef, 8> blocks {};
            const ScanResult res = scan_bmff(file, blocks);
            ASSERT_EQ(res.status, ScanStatus::Ok);
            ASSERT_EQ(res.written, 3U);

            const ContainerBlockRef* exif_block = nullptr;
            const ContainerBlockRef* xmp_block  = nullptr;
            const ContainerBlockRef* icc_block  = nullptr;
            for (uint32_t bi = 0; bi < res.written; ++bi) {
                if (blocks[bi].kind == ContainerBlockKind::Exif) {
                    exif_block = &blocks[bi];
                } else if (blocks[bi].kind == ContainerBlockKind::Xmp) {
                    xmp_block = &blocks[bi];
                } else if (blocks[bi].kind == ContainerBlockKind::Icc) {
                    icc_block = &blocks[bi];
                }
            }
            ASSERT_NE(exif_block, nullptr);
            ASSERT_NE(xmp_block, nullptr);
            ASSERT_NE(icc_block, nullptr);

            EXPECT_EQ(exif_block->format, c.expect_fmt);
            EXPECT_EQ(exif_block->chunking,
                      BlockChunking::BmffExifTiffOffsetU32Be);
            EXPECT_EQ(exif_block->aux_u32, 0U);
            EXPECT_EQ(file[exif_block->data_offset], std::byte { 'I' });

            EXPECT_EQ(xmp_block->format, c.expect_fmt);
            EXPECT_EQ(file[xmp_block->data_offset], std::byte { '<' });

            EXPECT_EQ(icc_block->format, c.expect_fmt);
            EXPECT_EQ(icc_block->id, fourcc('c', 'o', 'l', 'r'));
            EXPECT_EQ(icc_block->aux_u32, fourcc('p', 'r', 'o', 'f'));
            ASSERT_GE(icc_block->data_size, 3U);
            EXPECT_EQ(file[icc_block->data_offset + 0], std::byte { 'I' });
            EXPECT_EQ(file[icc_block->data_offset + 1], std::byte { 'C' });
            EXPECT_EQ(file[icc_block->data_offset + 2], std::byte { 'C' });

            const ScanResult auto_res = scan_auto(file, blocks);
            EXPECT_EQ(auto_res.status, ScanStatus::Ok);
            EXPECT_EQ(auto_res.written, 3U);

            const ScanResult est_bmff = measure_scan_bmff(file);
            EXPECT_EQ(est_bmff.status, ScanStatus::Ok);
            EXPECT_EQ(est_bmff.written, 0U);
            EXPECT_EQ(est_bmff.needed, 3U);
        }
    }


    TEST(ContainerScan, BmffFtypNotFirst)
    {
        // Some BMFF files include a top-level `free`/`skip` box before `ftyp`.
        // Ensure scan_auto still recognizes the container and finds meta items.

        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_u32be(&idat_payload, 0);  // Exif TIFF offset
        append_bytes(&idat_payload, "II");
        idat_payload.push_back(std::byte { 0x2A });
        idat_payload.push_back(std::byte { 0x00 });
        append_u32le(&idat_payload, 8);
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset (within idat)
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> ftyp_box;
        append_bmff_box(&ftyp_box, fourcc('f', 't', 'y', 'p'), ftyp_payload);

        std::vector<std::byte> free_payload;
        append_u32be(&free_payload, 0);
        std::vector<std::byte> free_box;
        append_bmff_box(&free_box, fourcc('f', 'r', 'e', 'e'), free_payload);

        std::vector<std::byte> file;
        file.insert(file.end(), free_box.begin(), free_box.end());
        file.insert(file.end(), ftyp_box.begin(), ftyp_box.end());
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].chunking, BlockChunking::BmffExifTiffOffsetU32Be);
        EXPECT_EQ(blocks[0].aux_u32, 0U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { 'I' });

        const ScanResult auto_res = scan_auto(file, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
    }

    TEST(ContainerScan, BmffMetaIlocMissingExtentLengthUsesIdatEnd)
    {
        // iloc len_size=0 omits extent_length. For metadata in `idat`, scan_bmff
        // should treat the single extent as spanning to the end of `idat`.

        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_u32be(&idat_payload, 0);  // Exif TIFF offset
        append_bytes(&idat_payload, "II");
        idat_payload.push_back(std::byte { 0x2A });
        idat_payload.push_back(std::byte { 0x00 });
        append_u32le(&idat_payload, 8);
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x40 });  // off_size=4, len_size=0
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset (within idat)
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].chunking, BlockChunking::BmffExifTiffOffsetU32Be);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { 'I' });
    }

    TEST(ContainerScan, BmffMetaIlocV2ConstructionMethod2FromIrefV1)
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "xmp");
        infe_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_payload, "application/xmp+xml");
        infe_payload.push_back(std::byte { 0x00 });
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_bytes(&idat_payload, "<xmp/>");
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 2);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u32be(&iloc_payload, 2);              // item_count (v2)

        // Referenced target item (not in iinf).
        append_u32be(&iloc_payload, 2);  // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));

        // Source metadata item (known XMP item in iinf), resolved via iref+iloc.
        append_u32be(&iloc_payload, 1);  // item_ID
        append_u16be(&iloc_payload, 2);  // construction_method=2
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // logical extent offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // iref version=1 uses 32-bit item IDs.
        std::vector<std::byte> iref_iloc_payload;
        append_u32be(&iref_iloc_payload, 1);  // from item id
        append_u16be(&iref_iloc_payload, 1);  // ref_count
        append_u32be(&iref_iloc_payload, 2);  // to item id
        std::vector<std::byte> iref_iloc_box;
        append_bmff_box(&iref_iloc_box, fourcc('i', 'l', 'o', 'c'),
                        iref_iloc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 1);
        iref_payload.insert(iref_payload.end(), iref_iloc_box.begin(),
                            iref_iloc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[0].group, 1U);
        ASSERT_EQ(blocks[0].data_size, idat_payload.size());
        EXPECT_EQ(file[blocks[0].data_offset + 0], std::byte { '<' });
        EXPECT_EQ(file[blocks[0].data_offset + 1], std::byte { 'x' });

        const ScanResult est = measure_scan_bmff(file);
        EXPECT_EQ(est.status, ScanStatus::Ok);
        EXPECT_EQ(est.written, 0U);
        EXPECT_EQ(est.needed, 1U);
    }

    TEST(ContainerScan, BmffMetaIlocV2ConstructionMethod2FromIrefV0)
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "xmp");
        infe_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_payload, "application/xmp+xml");
        infe_payload.push_back(std::byte { 0x00 });
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_bytes(&idat_payload, "<xmp/>");
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 2);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u32be(&iloc_payload, 2);              // item_count (v2)

        // Referenced target item (not in iinf).
        append_u32be(&iloc_payload, 2);  // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));

        // Source metadata item (known XMP item in iinf), resolved via iref+iloc.
        append_u32be(&iloc_payload, 1);  // item_ID
        append_u16be(&iloc_payload, 2);  // construction_method=2
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // logical extent offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // iref version=0 uses 16-bit item IDs.
        std::vector<std::byte> iref_iloc_payload;
        append_u16be(&iref_iloc_payload, 1);  // from item id
        append_u16be(&iref_iloc_payload, 1);  // ref_count
        append_u16be(&iref_iloc_payload, 2);  // to item id
        std::vector<std::byte> iref_iloc_box;
        append_bmff_box(&iref_iloc_box, fourcc('i', 'l', 'o', 'c'),
                        iref_iloc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), iref_iloc_box.begin(),
                            iref_iloc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[0].group, 1U);
        ASSERT_EQ(blocks[0].data_size, idat_payload.size());
        EXPECT_EQ(file[blocks[0].data_offset + 0], std::byte { '<' });
        EXPECT_EQ(file[blocks[0].data_offset + 1], std::byte { 'x' });

        const ScanResult est = measure_scan_bmff(file);
        EXPECT_EQ(est.status, ScanStatus::Ok);
        EXPECT_EQ(est.written, 0U);
        EXPECT_EQ(est.needed, 1U);
    }

    TEST(ContainerScan,
         BmffMetaIlocV2ConstructionMethod2NoIdxMapsExtentsByReferenceOrder)
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "xmp");
        infe_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_payload, "application/xmp+xml");
        infe_payload.push_back(std::byte { 0x00 });
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_bytes(&idat_payload, "<xmp/>");
        ASSERT_EQ(idat_payload.size(), 6U);
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 2);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u32be(&iloc_payload, 3);              // item_count (v2)

        // Referenced target item #2 maps to first half of idat.
        append_u32be(&iloc_payload, 2);  // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset
        append_u32be(&iloc_payload, 3);  // extent_length

        // Referenced target item #3 maps to second half of idat.
        append_u32be(&iloc_payload, 3);  // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 3);  // extent_offset
        append_u32be(&iloc_payload, 3);  // extent_length

        // Source item #1 uses method=2 with two extents, idx_size=0.
        // Scanner should map extent[0]->ref[1] and extent[1]->ref[2].
        append_u32be(&iloc_payload, 1);  // item_ID
        append_u16be(&iloc_payload, 2);  // construction_method=2
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 2);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent0 logical offset
        append_u32be(&iloc_payload, 3);  // extent0 length
        append_u32be(&iloc_payload, 0);  // extent1 logical offset
        append_u32be(&iloc_payload, 3);  // extent1 length

        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> iref_iloc_payload;
        append_u32be(&iref_iloc_payload, 1);  // from item id
        append_u16be(&iref_iloc_payload, 2);  // ref_count
        append_u32be(&iref_iloc_payload, 2);  // to item id #1
        append_u32be(&iref_iloc_payload, 3);  // to item id #2
        std::vector<std::byte> iref_iloc_box;
        append_bmff_box(&iref_iloc_box, fourcc('i', 'l', 'o', 'c'),
                        iref_iloc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 1);
        iref_payload.insert(iref_payload.end(), iref_iloc_box.begin(),
                            iref_iloc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 2U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[0].group, 1U);
        EXPECT_EQ(blocks[0].part_index, 0U);
        EXPECT_EQ(blocks[0].part_count, 2U);
        EXPECT_EQ(blocks[0].logical_offset, 0U);
        EXPECT_EQ(blocks[0].data_size, 3U);
        EXPECT_EQ(file[blocks[0].data_offset + 0], std::byte { '<' });

        EXPECT_EQ(blocks[1].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[1].group, 1U);
        EXPECT_EQ(blocks[1].part_index, 1U);
        EXPECT_EQ(blocks[1].part_count, 2U);
        EXPECT_EQ(blocks[1].logical_offset, 3U);
        EXPECT_EQ(blocks[1].data_size, 3U);
        EXPECT_EQ(file[blocks[1].data_offset + 0], std::byte { 'p' });

        const ScanResult est = measure_scan_bmff(file);
        EXPECT_EQ(est.status, ScanStatus::Ok);
        EXPECT_EQ(est.written, 0U);
        EXPECT_EQ(est.needed, 2U);
    }

    TEST(ContainerScan,
         BmffMetaIlocV2ConstructionMethod2MissingIrefMappingSkipsItem)
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "xmp");
        infe_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_payload, "application/xmp+xml");
        infe_payload.push_back(std::byte { 0x00 });
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_bytes(&idat_payload, "<xmp/>");
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 2);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u32be(&iloc_payload, 2);              // item_count (v2)

        append_u32be(&iloc_payload, 2);  // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));

        append_u32be(&iloc_payload, 1);  // item_ID
        append_u16be(&iloc_payload, 2);  // construction_method=2
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // logical extent offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        EXPECT_EQ(res.status, ScanStatus::Ok);
        EXPECT_EQ(res.written, 0U);
        EXPECT_EQ(res.needed, 0U);

        const ScanResult est = measure_scan_bmff(file);
        EXPECT_EQ(est.status, ScanStatus::Ok);
        EXPECT_EQ(est.written, 0U);
        EXPECT_EQ(est.needed, 0U);
    }

    TEST(ContainerScan,
         BmffMetaIlocV2ConstructionMethod2ExtentIndexOutOfRangeSkipsItem)
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "xmp");
        infe_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_payload, "application/xmp+xml");
        infe_payload.push_back(std::byte { 0x00 });
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_bytes(&idat_payload, "<xmp/>");
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 2);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x02 });  // base=0, idx=2
        append_u32be(&iloc_payload, 2);              // item_count (v2)

        append_u32be(&iloc_payload, 2);  // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u16be(&iloc_payload, 1);  // extent_index (ignored for method=1)
        append_u32be(&iloc_payload, 0);  // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));

        append_u32be(&iloc_payload, 1);  // item_ID
        append_u16be(&iloc_payload, 2);  // construction_method=2
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u16be(&iloc_payload, 2);  // extent_index=2 (out of range)
        append_u32be(&iloc_payload, 0);  // logical extent offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> iref_iloc_payload;
        append_u32be(&iref_iloc_payload, 1);  // from item id
        append_u16be(&iref_iloc_payload, 1);  // ref_count
        append_u32be(&iref_iloc_payload, 2);  // to item id
        std::vector<std::byte> iref_iloc_box;
        append_bmff_box(&iref_iloc_box, fourcc('i', 'l', 'o', 'c'),
                        iref_iloc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 1);
        iref_payload.insert(iref_payload.end(), iref_iloc_box.begin(),
                            iref_iloc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        EXPECT_EQ(res.status, ScanStatus::Ok);
        EXPECT_EQ(res.written, 0U);
        EXPECT_EQ(res.needed, 0U);

        const ScanResult est = measure_scan_bmff(file);
        EXPECT_EQ(est.status, ScanStatus::Ok);
        EXPECT_EQ(est.written, 0U);
        EXPECT_EQ(est.needed, 0U);
    }

    TEST(ContainerScan,
         BmffMetaIlocV2ConstructionMethod2NoIdxRefMismatchSkipsItem)
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "xmp");
        infe_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_payload, "application/xmp+xml");
        infe_payload.push_back(std::byte { 0x00 });
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_bytes(&idat_payload, "<xmp/>");
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 2);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u32be(&iloc_payload, 3);              // item_count (v2)

        append_u32be(&iloc_payload, 2);  // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset
        append_u32be(&iloc_payload, 3);  // extent_length

        append_u32be(&iloc_payload, 3);  // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 3);  // extent_offset
        append_u32be(&iloc_payload, 3);  // extent_length

        append_u32be(&iloc_payload, 1);  // item_ID
        append_u16be(&iloc_payload, 2);  // construction_method=2
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 2);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent0 logical offset
        append_u32be(&iloc_payload, 3);  // extent0 length
        append_u32be(&iloc_payload, 0);  // extent1 logical offset
        append_u32be(&iloc_payload, 3);  // extent1 length
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> iref_iloc_payload;
        append_u32be(&iref_iloc_payload, 1);  // from item id
        append_u16be(&iref_iloc_payload, 1);  // ref_count
        append_u32be(&iref_iloc_payload, 2);  // only one ref target
        std::vector<std::byte> iref_iloc_box;
        append_bmff_box(&iref_iloc_box, fourcc('i', 'l', 'o', 'c'),
                        iref_iloc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 1);
        iref_payload.insert(iref_payload.end(), iref_iloc_box.begin(),
                            iref_iloc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        EXPECT_EQ(res.status, ScanStatus::Ok);
        EXPECT_EQ(res.written, 0U);
        EXPECT_EQ(res.needed, 0U);

        const ScanResult est = measure_scan_bmff(file);
        EXPECT_EQ(est.status, ScanStatus::Ok);
        EXPECT_EQ(est.written, 0U);
        EXPECT_EQ(est.needed, 0U);
    }


    TEST(ContainerScan, BmffMetaSkipsExternalDataReferenceItems)
    {
        std::vector<std::byte> infe_exif_payload;
        append_fullbox_header(&infe_exif_payload, 2);
        append_u16be(&infe_exif_payload, 1);  // item_ID
        append_u16be(&infe_exif_payload, 0);  // protection
        append_fourcc(&infe_exif_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_exif_payload, "exif");
        infe_exif_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_exif_box;
        append_bmff_box(&infe_exif_box, fourcc('i', 'n', 'f', 'e'),
                        infe_exif_payload);

        std::vector<std::byte> infe_xmp_payload;
        append_fullbox_header(&infe_xmp_payload, 2);
        append_u16be(&infe_xmp_payload, 2);  // item_ID
        append_u16be(&infe_xmp_payload, 0);  // protection
        append_fourcc(&infe_xmp_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_xmp_payload, "xmp");
        infe_xmp_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_xmp_payload, "application/xmp+xml");
        infe_xmp_payload.push_back(std::byte { 0x00 });
        infe_xmp_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_xmp_box;
        append_bmff_box(&infe_xmp_box, fourcc('i', 'n', 'f', 'e'),
                        infe_xmp_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 2);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_exif_box.begin(),
                            infe_exif_box.end());
        iinf_payload.insert(iinf_payload.end(), infe_xmp_box.begin(),
                            infe_xmp_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_u32be(&idat_payload, 0);  // Exif TIFF offset
        append_bytes(&idat_payload, "II");
        idat_payload.push_back(std::byte { 0x2A });
        idat_payload.push_back(std::byte { 0x00 });
        append_u32le(&idat_payload, 8);
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 2);              // item_count

        append_u16be(&iloc_payload, 1);  // item_ID (Exif)
        append_u16be(&iloc_payload, 1);  // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);  // data_reference_index (local)
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));

        append_u16be(&iloc_payload, 2);            // item_ID (XMP)
        append_u16be(&iloc_payload, 0);            // construction_method=0
        append_u16be(&iloc_payload, 1);            // data_reference_index (ext)
        append_u16be(&iloc_payload, 1);            // extent_count
        append_u32be(&iloc_payload, 0xFFFFFF00U);  // bogus extent_offset
        append_u32be(&iloc_payload, 32);           // bogus extent_length

        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].chunking, BlockChunking::BmffExifTiffOffsetU32Be);
        ASSERT_GE(blocks[0].data_size, 4U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { 'I' });

        const ScanResult auto_res = scan_auto(file, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
    }


    TEST(ContainerScan, BmffMetaSkipsOutOfRangeKnownItemExtents)
    {
        std::vector<std::byte> infe_exif_payload;
        append_fullbox_header(&infe_exif_payload, 2);
        append_u16be(&infe_exif_payload, 1);  // item_ID
        append_u16be(&infe_exif_payload, 0);  // protection
        append_fourcc(&infe_exif_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_exif_payload, "exif");
        infe_exif_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_exif_box;
        append_bmff_box(&infe_exif_box, fourcc('i', 'n', 'f', 'e'),
                        infe_exif_payload);

        std::vector<std::byte> infe_xmp_payload;
        append_fullbox_header(&infe_xmp_payload, 2);
        append_u16be(&infe_xmp_payload, 2);  // item_ID
        append_u16be(&infe_xmp_payload, 0);  // protection
        append_fourcc(&infe_xmp_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_xmp_payload, "xmp");
        infe_xmp_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_xmp_payload, "application/xmp+xml");
        infe_xmp_payload.push_back(std::byte { 0x00 });
        infe_xmp_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_xmp_box;
        append_bmff_box(&infe_xmp_box, fourcc('i', 'n', 'f', 'e'),
                        infe_xmp_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 2);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_exif_box.begin(),
                            infe_exif_box.end());
        iinf_payload.insert(iinf_payload.end(), infe_xmp_box.begin(),
                            infe_xmp_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_u32be(&idat_payload, 0);  // Exif TIFF offset
        append_bytes(&idat_payload, "II");
        idat_payload.push_back(std::byte { 0x2A });
        idat_payload.push_back(std::byte { 0x00 });
        append_u32le(&idat_payload, 8);
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 2);              // item_count

        append_u16be(&iloc_payload, 1);  // item_ID (Exif)
        append_u16be(&iloc_payload, 1);  // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);  // data_reference_index (local)
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));

        append_u16be(&iloc_payload, 2);            // item_ID (XMP)
        append_u16be(&iloc_payload, 0);            // construction_method=0
        append_u16be(&iloc_payload, 0);            // data_reference_index
        append_u16be(&iloc_payload, 1);            // extent_count
        append_u32be(&iloc_payload, 0xFFFFFF00U);  // bogus extent_offset
        append_u32be(&iloc_payload, 32);           // bogus extent_length

        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].chunking, BlockChunking::BmffExifTiffOffsetU32Be);
        ASSERT_GE(blocks[0].data_size, 4U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { 'I' });

        const ScanResult auto_res = scan_auto(file, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
    }


    TEST(ContainerScan, BmffMetaJumbfItem)
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&infe_payload, "manifest");
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> jumd_payload;
        append_bytes(&jumd_payload, "c2pa");
        jumd_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> jumd_box;
        append_bmff_box(&jumd_box, fourcc('j', 'u', 'm', 'd'), jumd_payload);

        std::vector<std::byte> cbor_payload = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { 0x01 },
        };
        std::vector<std::byte> cbor_box;
        append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'), cbor_payload);

        std::vector<std::byte> jumb_payload;
        jumb_payload.insert(jumb_payload.end(), jumd_box.begin(),
                            jumd_box.end());
        jumb_payload.insert(jumb_payload.end(), cbor_box.begin(),
                            cbor_box.end());
        std::vector<std::byte> jumb_box;
        append_bmff_box(&jumb_box, fourcc('j', 'u', 'm', 'b'), jumb_payload);

        std::vector<std::byte> idat_payload(jumb_box.begin(), jumb_box.end());
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 1);              // construction_method=1
        append_u16be(&iloc_payload, 0);              // data_reference_index
        append_u16be(&iloc_payload, 1);              // extent_count
        append_u32be(&iloc_payload, 0);              // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));

        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[0].id, fourcc('j', 'u', 'm', 'b'));
        ASSERT_GE(blocks[0].data_size, 8U);
        EXPECT_EQ(file[blocks[0].data_offset + 4], std::byte { 'j' });
        EXPECT_EQ(file[blocks[0].data_offset + 5], std::byte { 'u' });
        EXPECT_EQ(file[blocks[0].data_offset + 6], std::byte { 'm' });
        EXPECT_EQ(file[blocks[0].data_offset + 7], std::byte { 'b' });
    }


    TEST(ContainerScan, BmffJp2BrandGeoTiffUuid)
    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('j', 'p', '2', ' '));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('j', 'p', '2', ' '));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);

        const std::array<std::byte, 16> geo_uuid = {
            std::byte { 0xB1 }, std::byte { 0x4B }, std::byte { 0xF8 },
            std::byte { 0xBD }, std::byte { 0x08 }, std::byte { 0x3D },
            std::byte { 0x4B }, std::byte { 0x43 }, std::byte { 0xA5 },
            std::byte { 0xAE }, std::byte { 0x8C }, std::byte { 0xD7 },
            std::byte { 0xD5 }, std::byte { 0xA6 }, std::byte { 0xCE },
            std::byte { 0x03 },
        };

        std::vector<std::byte> tiff_payload;
        append_bytes(&tiff_payload, "II");
        tiff_payload.push_back(std::byte { 0x2A });
        tiff_payload.push_back(std::byte { 0x00 });
        append_u32le(&tiff_payload, 8);
        append_u16le(&tiff_payload, 0);  // empty IFD
        append_u32le(&tiff_payload, 0);  // next IFD

        std::vector<std::byte> uuid_box;
        append_u32be(&uuid_box,
                     static_cast<uint32_t>(8 + 16 + tiff_payload.size()));
        append_fourcc(&uuid_box, fourcc('u', 'u', 'i', 'd'));
        uuid_box.insert(uuid_box.end(), geo_uuid.begin(), geo_uuid.end());
        uuid_box.insert(uuid_box.end(), tiff_payload.begin(),
                        tiff_payload.end());
        file.insert(file.end(), uuid_box.begin(), uuid_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Jp2);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].chunking, BlockChunking::Jp2UuidPayload);
        EXPECT_EQ(blocks[0].id, fourcc('u', 'u', 'i', 'd'));
        ASSERT_GE(blocks[0].data_size, 8U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { 'I' });

        const ScanResult auto_res = scan_auto(file, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
    }


    TEST(ContainerScan, BmffMetaMimeTypeCaseInsensitiveWithParameters)
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "xmp");
        infe_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_payload, "Application/RDF+XML; charset=UTF-8");
        infe_payload.push_back(std::byte { 0x00 });
        // Encoding is optional; include empty encoding for completeness.
        infe_payload.push_back(std::byte { 0x00 });

        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_bytes(&idat_payload, "<xmp/>");
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 1);              // construction_method=1
        append_u16be(&iloc_payload, 0);              // data_reference_index
        append_u16be(&iloc_payload, 1);              // extent_count
        append_u32be(&iloc_payload, 0);              // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        ASSERT_GE(blocks[0].data_size, 6U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { '<' });

        const ScanResult auto_res = scan_auto(file, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
    }


    TEST(ContainerScan, BmffMetaMimeTypeWithoutContentTypeTerminator)
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "xmp");
        infe_payload.push_back(std::byte { 0x00 });
        // Broken-but-seen-in-the-wild variant: missing terminating NUL.
        append_bytes(&infe_payload, "application/xmp+xml");

        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_bytes(&idat_payload, "<xmp/>");
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 1);              // construction_method=1
        append_u16be(&iloc_payload, 0);              // data_reference_index
        append_u16be(&iloc_payload, 1);              // extent_count
        append_u32be(&iloc_payload, 0);              // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        ASSERT_GE(blocks[0].data_size, 6U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { '<' });

        const ScanResult auto_res = scan_auto(file, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
    }


    TEST(ContainerScan, BmffMetaLegacyInfeV1ExifName)
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 1);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_bytes(&infe_payload, "Exif");
        infe_payload.push_back(std::byte { 0x00 });
        // Empty content_type/content_encoding are valid for this test path.
        infe_payload.push_back(std::byte { 0x00 });
        infe_payload.push_back(std::byte { 0x00 });

        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 1);
        append_u32be(&iinf_payload, 1);  // entry_count (v1)
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_u32be(&idat_payload, 0);  // Exif TIFF offset
        append_bytes(&idat_payload, "II");
        idat_payload.push_back(std::byte { 0x2A });
        idat_payload.push_back(std::byte { 0x00 });
        append_u32le(&idat_payload, 8);
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 1);              // construction_method=1
        append_u16be(&iloc_payload, 0);              // data_reference_index
        append_u16be(&iloc_payload, 1);              // extent_count
        append_u32be(&iloc_payload, 0);              // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Heif);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].chunking, BlockChunking::BmffExifTiffOffsetU32Be);
        EXPECT_EQ(blocks[0].aux_u32, 0U);
        ASSERT_GE(blocks[0].data_size, 4U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { 'I' });

        const ScanResult auto_res = scan_auto(file, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
    }


    TEST(ContainerScan, Cr3CanonUuidCmtBoxes)
    {
        // Minimal CR3-style BMFF: `ftyp` + `moov/uuid(Canon)` containing `CMT1` TIFF.
        std::vector<std::byte> file;

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('c', 'r', 'x', ' '));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('i', 's', 'o', 'm'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'),
                        std::span<const std::byte>(ftyp_payload.data(),
                                                   ftyp_payload.size()));

        std::vector<std::byte> cmt_payload;
        const std::array<std::byte, 4> tiff_hdr = {
            std::byte { 'I' },
            std::byte { 'I' },
            std::byte { 0x2A },
            std::byte { 0x00 },
        };
        cmt_payload.insert(cmt_payload.end(), tiff_hdr.begin(), tiff_hdr.end());
        append_u32le(&cmt_payload, 8);
        std::vector<std::byte> cmt_box;
        append_bmff_box(&cmt_box, fourcc('C', 'M', 'T', '1'),
                        std::span<const std::byte>(cmt_payload.data(),
                                                   cmt_payload.size()));

        std::vector<std::byte> uuid_box;
        const std::array<std::byte, 16> canon_uuid = {
            std::byte { 0x85 }, std::byte { 0xc0 }, std::byte { 0xb6 },
            std::byte { 0x87 }, std::byte { 0x82 }, std::byte { 0x0f },
            std::byte { 0x11 }, std::byte { 0xe0 }, std::byte { 0x81 },
            std::byte { 0x11 }, std::byte { 0xf4 }, std::byte { 0xce },
            std::byte { 0x46 }, std::byte { 0x2b }, std::byte { 0x6a },
            std::byte { 0x48 },
        };
        append_u32be(&uuid_box, static_cast<uint32_t>(8 + 16 + cmt_box.size()));
        append_fourcc(&uuid_box, fourcc('u', 'u', 'i', 'd'));
        uuid_box.insert(uuid_box.end(), canon_uuid.begin(), canon_uuid.end());
        uuid_box.insert(uuid_box.end(), cmt_box.begin(), cmt_box.end());

        std::vector<std::byte> moov_payload;
        moov_payload.insert(moov_payload.end(), uuid_box.begin(),
                            uuid_box.end());
        append_bmff_box(&file, fourcc('m', 'o', 'o', 'v'),
                        std::span<const std::byte>(moov_payload.data(),
                                                   moov_payload.size()));

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Cr3);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].id, fourcc('C', 'M', 'T', '1'));
        ASSERT_GE(blocks[0].data_size, 4U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { 'I' });

        const ScanResult auto_res = scan_auto(file, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
    }


    TEST(ContainerScan, Cr3CanonUuidCmtAndVendorMetadataBoxes)
    {
        std::vector<std::byte> file;

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('c', 'r', 'x', ' '));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('i', 's', 'o', 'm'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'),
                        std::span<const std::byte>(ftyp_payload.data(),
                                                   ftyp_payload.size()));

        std::vector<std::byte> cmt_payload;
        const std::array<std::byte, 4> tiff_hdr = {
            std::byte { 'I' },
            std::byte { 'I' },
            std::byte { 0x2A },
            std::byte { 0x00 },
        };
        cmt_payload.insert(cmt_payload.end(), tiff_hdr.begin(), tiff_hdr.end());
        append_u32le(&cmt_payload, 8);
        std::vector<std::byte> cmt_box;
        append_bmff_box(&cmt_box, fourcc('C', 'M', 'T', '1'),
                        std::span<const std::byte>(cmt_payload.data(),
                                                   cmt_payload.size()));

        std::vector<std::byte> cctp_payload;
        append_u32be(&cctp_payload, 0x00000001);
        append_u32be(&cctp_payload, 0x00000002);
        append_u32be(&cctp_payload, 0x00000003);
        std::vector<std::byte> cctp_box;
        append_bmff_box(&cctp_box, fourcc('C', 'C', 'T', 'P'),
                        std::span<const std::byte>(cctp_payload.data(),
                                                   cctp_payload.size()));

        std::vector<std::byte> uuid_box;
        const std::array<std::byte, 16> canon_uuid = {
            std::byte { 0x85 }, std::byte { 0xc0 }, std::byte { 0xb6 },
            std::byte { 0x87 }, std::byte { 0x82 }, std::byte { 0x0f },
            std::byte { 0x11 }, std::byte { 0xe0 }, std::byte { 0x81 },
            std::byte { 0x11 }, std::byte { 0xf4 }, std::byte { 0xce },
            std::byte { 0x46 }, std::byte { 0x2b }, std::byte { 0x6a },
            std::byte { 0x48 },
        };
        append_u32be(&uuid_box, static_cast<uint32_t>(8 + 16 + cmt_box.size()
                                                      + cctp_box.size()));
        append_fourcc(&uuid_box, fourcc('u', 'u', 'i', 'd'));
        uuid_box.insert(uuid_box.end(), canon_uuid.begin(), canon_uuid.end());
        uuid_box.insert(uuid_box.end(), cmt_box.begin(), cmt_box.end());
        uuid_box.insert(uuid_box.end(), cctp_box.begin(), cctp_box.end());

        std::vector<std::byte> moov_payload;
        moov_payload.insert(moov_payload.end(), uuid_box.begin(),
                            uuid_box.end());
        append_bmff_box(&file, fourcc('m', 'o', 'o', 'v'),
                        std::span<const std::byte>(moov_payload.data(),
                                                   moov_payload.size()));

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 2U);

        const ContainerBlockRef* exif_block = nullptr;
        const ContainerBlockRef* cctp_block = nullptr;
        for (uint32_t bi = 0; bi < res.written; ++bi) {
            if (blocks[bi].kind == ContainerBlockKind::Exif) {
                exif_block = &blocks[bi];
            } else if (blocks[bi].kind == ContainerBlockKind::MakerNote
                       && blocks[bi].id == fourcc('C', 'C', 'T', 'P')) {
                cctp_block = &blocks[bi];
            }
        }

        ASSERT_NE(exif_block, nullptr);
        ASSERT_NE(cctp_block, nullptr);
        EXPECT_EQ(exif_block->format, ContainerFormat::Cr3);
        EXPECT_EQ(cctp_block->format, ContainerFormat::Cr3);
        EXPECT_EQ(cctp_block->kind, ContainerBlockKind::MakerNote);
        ASSERT_EQ(cctp_block->data_size, cctp_payload.size());
        EXPECT_EQ(file[cctp_block->data_offset], std::byte { 0x00 });
    }


    TEST(ContainerScan, Cr3CanonUuidCmtBoxesWithExifPreamble)
    {
        std::vector<std::byte> file;

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('c', 'r', 'x', ' '));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('i', 's', 'o', 'm'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'),
                        std::span<const std::byte>(ftyp_payload.data(),
                                                   ftyp_payload.size()));

        std::vector<std::byte> cmt_payload;
        append_bytes(&cmt_payload, "Exif");
        cmt_payload.push_back(std::byte { 0x00 });
        cmt_payload.push_back(std::byte { 0x00 });
        const std::array<std::byte, 4> tiff_hdr = {
            std::byte { 'I' },
            std::byte { 'I' },
            std::byte { 0x2A },
            std::byte { 0x00 },
        };
        cmt_payload.insert(cmt_payload.end(), tiff_hdr.begin(), tiff_hdr.end());
        append_u32le(&cmt_payload, 8);

        std::vector<std::byte> cmt_box;
        append_bmff_box(&cmt_box, fourcc('C', 'M', 'T', '1'),
                        std::span<const std::byte>(cmt_payload.data(),
                                                   cmt_payload.size()));

        std::vector<std::byte> uuid_box;
        const std::array<std::byte, 16> canon_uuid = {
            std::byte { 0x85 }, std::byte { 0xc0 }, std::byte { 0xb6 },
            std::byte { 0x87 }, std::byte { 0x82 }, std::byte { 0x0f },
            std::byte { 0x11 }, std::byte { 0xe0 }, std::byte { 0x81 },
            std::byte { 0x11 }, std::byte { 0xf4 }, std::byte { 0xce },
            std::byte { 0x46 }, std::byte { 0x2b }, std::byte { 0x6a },
            std::byte { 0x48 },
        };
        append_u32be(&uuid_box, static_cast<uint32_t>(8 + 16 + cmt_box.size()));
        append_fourcc(&uuid_box, fourcc('u', 'u', 'i', 'd'));
        uuid_box.insert(uuid_box.end(), canon_uuid.begin(), canon_uuid.end());
        uuid_box.insert(uuid_box.end(), cmt_box.begin(), cmt_box.end());

        std::vector<std::byte> moov_payload;
        moov_payload.insert(moov_payload.end(), uuid_box.begin(),
                            uuid_box.end());
        append_bmff_box(&file, fourcc('m', 'o', 'o', 'v'),
                        std::span<const std::byte>(moov_payload.data(),
                                                   moov_payload.size()));

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Cr3);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].id, fourcc('C', 'M', 'T', '1'));
        ASSERT_GE(blocks[0].data_size, 4U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { 'I' });
    }

    TEST(ContainerScan, Cr3CanonUuidNestedCmtBoxes)
    {
        // Real-world CR3 commonly nests `CMT*` TIFF boxes under an intermediate
        // container box (e.g. `CNCV`) inside `moov/uuid(Canon)`.
        std::vector<std::byte> file;

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('c', 'r', 'x', ' '));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('i', 's', 'o', 'm'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'),
                        std::span<const std::byte>(ftyp_payload.data(),
                                                   ftyp_payload.size()));

        std::vector<std::byte> cmt_payload;
        const std::array<std::byte, 4> tiff_hdr = {
            std::byte { 'I' },
            std::byte { 'I' },
            std::byte { 0x2A },
            std::byte { 0x00 },
        };
        cmt_payload.insert(cmt_payload.end(), tiff_hdr.begin(), tiff_hdr.end());
        append_u32le(&cmt_payload, 8);
        std::vector<std::byte> cmt_box;
        append_bmff_box(&cmt_box, fourcc('C', 'M', 'T', '1'),
                        std::span<const std::byte>(cmt_payload.data(),
                                                   cmt_payload.size()));

        std::vector<std::byte> cncv_box;
        append_bmff_box(&cncv_box, fourcc('C', 'N', 'C', 'V'),
                        std::span<const std::byte>(cmt_box.data(),
                                                   cmt_box.size()));

        std::vector<std::byte> uuid_box;
        const std::array<std::byte, 16> canon_uuid = {
            std::byte { 0x85 }, std::byte { 0xc0 }, std::byte { 0xb6 },
            std::byte { 0x87 }, std::byte { 0x82 }, std::byte { 0x0f },
            std::byte { 0x11 }, std::byte { 0xe0 }, std::byte { 0x81 },
            std::byte { 0x11 }, std::byte { 0xf4 }, std::byte { 0xce },
            std::byte { 0x46 }, std::byte { 0x2b }, std::byte { 0x6a },
            std::byte { 0x48 },
        };
        append_u32be(&uuid_box,
                     static_cast<uint32_t>(8 + 16 + cncv_box.size()));
        append_fourcc(&uuid_box, fourcc('u', 'u', 'i', 'd'));
        uuid_box.insert(uuid_box.end(), canon_uuid.begin(), canon_uuid.end());
        uuid_box.insert(uuid_box.end(), cncv_box.begin(), cncv_box.end());

        std::vector<std::byte> moov_payload;
        moov_payload.insert(moov_payload.end(), uuid_box.begin(),
                            uuid_box.end());
        append_bmff_box(&file, fourcc('m', 'o', 'o', 'v'),
                        std::span<const std::byte>(moov_payload.data(),
                                                   moov_payload.size()));

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Cr3);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].id, fourcc('C', 'M', 'T', '1'));
        ASSERT_GE(blocks[0].data_size, 4U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { 'I' });

        const ScanResult auto_res = scan_auto(file, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
    }


    TEST(ContainerScan, TiffTagValues)
    {
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        tiff.push_back(std::byte { 0x2A });
        tiff.push_back(std::byte { 0x00 });
        append_u32le(&tiff, 8);

        // IFD0 at offset 8 with two entries.
        tiff.push_back(std::byte { 0x02 });
        tiff.push_back(std::byte { 0x00 });

        // XMP tag 0x02BC, type BYTE(1), count 5, offset to value (38).
        tiff.push_back(std::byte { 0xBC });
        tiff.push_back(std::byte { 0x02 });
        tiff.push_back(std::byte { 0x01 });
        tiff.push_back(std::byte { 0x00 });
        append_u32le(&tiff, 5);
        append_u32le(&tiff, 38);

        // ICC tag 0x8773, type UNDEFINED(7), count 4 inline ("ABCD").
        tiff.push_back(std::byte { 0x73 });
        tiff.push_back(std::byte { 0x87 });
        tiff.push_back(std::byte { 0x07 });
        tiff.push_back(std::byte { 0x00 });
        append_u32le(&tiff, 4);
        append_bytes(&tiff, "ABCD");

        // Next IFD offset = 0.
        append_u32le(&tiff, 0);

        // XMP value bytes at offset 38.
        ASSERT_EQ(tiff.size(), 38U);
        append_bytes(&tiff, "<xmp>");

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_tiff(tiff, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 3U);

        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].data_offset, 0U);
        EXPECT_EQ(blocks[0].data_size, tiff.size());

        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[1].data_size, 5U);
        EXPECT_EQ(tiff[blocks[1].data_offset + 0], std::byte { '<' });
        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::Icc);
        EXPECT_EQ(tiff[blocks[2].data_offset + 0], std::byte { 'A' });

        const ScanResult auto_res = scan_auto(tiff, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 3U);
    }


    TEST(ContainerScan, TiffRawVariantHeaders)
    {
        auto make_min_tiff = [&](uint16_t version_le) {
            std::vector<std::byte> t;
            append_bytes(&t, "II");
            append_u16le(&t, version_le);
            append_u32le(&t, 8);
            append_u16le(&t, 0);  // entry count
            append_u32le(&t, 0);  // next IFD
            return t;
        };

        const std::vector<std::byte> rw2 = make_min_tiff(0x0055);  // "IIU\0"
        const std::vector<std::byte> orf = make_min_tiff(0x4F52);  // "IIRO"

        for (const std::vector<std::byte>& t : { rw2, orf }) {
            std::array<ContainerBlockRef, 8> blocks {};
            const ScanResult res = scan_auto(t, blocks);
            ASSERT_EQ(res.status, ScanStatus::Ok);
            ASSERT_EQ(res.written, 1U);
            EXPECT_EQ(blocks[0].format, ContainerFormat::Tiff);
            EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
            EXPECT_EQ(blocks[0].data_offset, 0U);
            EXPECT_EQ(blocks[0].data_size, t.size());
        }
    }

}  // namespace
}  // namespace openmeta
