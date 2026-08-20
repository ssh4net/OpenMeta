// SPDX-License-Identifier: Apache-2.0

#include "openmeta/container_payload.h"
#include "openmeta/container_scan.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
#    include <zlib.h>
#endif

namespace openmeta {
namespace {

    struct PayloadCallbackState final {
        std::span<const std::byte> bytes;
        RandomAccessIoCode code  = RandomAccessIoCode::Ok;
        uint64_t max_bytes       = UINT64_MAX;
        uint64_t forbidden_begin = UINT64_MAX;
        uint64_t forbidden_end   = UINT64_MAX;
        bool touched_forbidden   = false;
        uint32_t calls           = 0U;
    };


    static RandomAccessIoResult
    payload_read_at(void* context, uint64_t offset,
                    std::span<std::byte> destination) noexcept
    {
        PayloadCallbackState* state = static_cast<PayloadCallbackState*>(
            context);
        state->calls += 1U;
        const uint64_t request_end
            = destination.size() <= UINT64_MAX - offset
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

    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
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


    static void append_fourcc(std::vector<std::byte>* out, uint32_t f)
    {
        append_u32be(out, f);
    }


    static void append_bytes(std::vector<std::byte>* out, std::string_view s)
    {
        for (char c : s) {
            out->push_back(std::byte { static_cast<uint8_t>(c) });
        }
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


    static void append_png_chunk(std::vector<std::byte>* out, uint32_t type,
                                 std::span<const std::byte> data)
    {
        append_u32be(out, static_cast<uint32_t>(data.size()));
        append_fourcc(out, type);
        out->insert(out->end(), data.begin(), data.end());
        append_u32be(out, 0);  // CRC ignored by scanner.
    }


    TEST(ContainerPayload, GifSubBlocks)
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

        // Application Extension for XMP.
        gif.push_back(std::byte { 0x21 });
        gif.push_back(std::byte { 0xFF });
        gif.push_back(std::byte { 0x0B });
        append_bytes(&gif, "XMP Data");
        append_bytes(&gif, "XMP");
        gif.push_back(std::byte { 0x03 });
        append_bytes(&gif, "abc");
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x3B });

        std::array<ContainerBlockRef, 4> blocks {};
        const ScanResult scan = scan_gif(gif, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 1U);

        std::array<std::byte, 16> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(gif,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        EXPECT_EQ(res.needed, 3U);
        EXPECT_EQ(res.written, 3U);
        EXPECT_EQ(out[0], std::byte { 'a' });
        EXPECT_EQ(out[1], std::byte { 'b' });
        EXPECT_EQ(out[2], std::byte { 'c' });
    }


    TEST(ContainerPayload, RandomAccessGifSubBlocksMatchContiguous)
    {
        const std::array<std::byte, 10> bytes {
            std::byte { 3U },  std::byte { 'a' }, std::byte { 'b' },
            std::byte { 'c' }, std::byte { 4U },  std::byte { 'd' },
            std::byte { 'e' }, std::byte { 'f' }, std::byte { 'g' },
            std::byte { 0U }
        };
        ContainerBlockRef block;
        block.format    = ContainerFormat::Gif;
        block.kind      = ContainerBlockKind::Comment;
        block.chunking  = BlockChunking::GifSubBlocks;
        block.data_size = bytes.size();
        const std::array<ContainerBlockRef, 1> blocks { block };

        PayloadCallbackState callback { bytes };
        const RandomAccessSource source
            = make_callback_random_access_source(bytes.size(), &callback,
                                                 payload_read_at, true);
        const RandomAccessSourceRange range = make_random_access_source_range(
            source);
        std::array<std::byte, 4> read_window {};
        PayloadRandomAccessScratch random_scratch;
        random_scratch.read_window                       = read_window;
        random_scratch.window_options.minimum_read_bytes = read_window.size();
        std::array<std::byte, 16> out {};
        std::array<uint32_t, 4> indices {};
        const PayloadRandomAccessResult result
            = extract_payload_random_access(range, blocks, 0U, out, indices,
                                            random_scratch, PayloadOptions {});
        ASSERT_TRUE(result.complete());
        EXPECT_EQ(result.payload.status, PayloadStatus::Ok);
        EXPECT_EQ(result.payload.written, 7U);
        EXPECT_EQ(std::memcmp(out.data(), "abcdefg", 7U), 0);
    }


    TEST(ContainerPayload, RandomAccessTruncatedOutputDoesNotFetchSuffix)
    {
        std::vector<std::byte> bytes(1024U * 1024U, std::byte { 0x5A });
        ContainerBlockRef block;
        block.kind      = ContainerBlockKind::Xmp;
        block.data_size = bytes.size();
        const std::array<ContainerBlockRef, 1> blocks { block };
        PayloadCallbackState callback { bytes };
        callback.forbidden_begin = 16U;
        callback.forbidden_end   = bytes.size();
        const RandomAccessSource source
            = make_callback_random_access_source(bytes.size(), &callback,
                                                 payload_read_at, true);
        const RandomAccessSourceRange range = make_random_access_source_range(
            source);
        std::array<std::byte, 8> read_window {};
        std::array<std::byte, 16> out {};
        std::array<uint32_t, 1> indices {};
        PayloadRandomAccessScratch scratch;
        scratch.read_window = read_window;
        const PayloadRandomAccessResult result
            = extract_payload_random_access(range, blocks, 0U, out, indices,
                                            scratch, PayloadOptions {});
        ASSERT_TRUE(result.complete());
        EXPECT_EQ(result.payload.status, PayloadStatus::OutputTruncated);
        EXPECT_EQ(result.payload.needed, bytes.size());
        EXPECT_EQ(result.payload.written, out.size());
        EXPECT_EQ(result.input.bytes_requested, out.size());
        EXPECT_FALSE(callback.touched_forbidden);
    }


    TEST(ContainerPayload, JpegIccSeqTotal)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        std::vector<std::byte> icc0;
        append_bytes(&icc0, "ICC_PROFILE");
        icc0.push_back(std::byte { 0x00 });
        icc0.push_back(std::byte { 0x01 });  // seq
        icc0.push_back(std::byte { 0x02 });  // total
        append_bytes(&icc0, "AB");
        append_jpeg_segment(&jpeg, 0xFFE2,
                            std::span<const std::byte>(icc0.data(),
                                                       icc0.size()));

        std::vector<std::byte> icc1;
        append_bytes(&icc1, "ICC_PROFILE");
        icc1.push_back(std::byte { 0x00 });
        icc1.push_back(std::byte { 0x02 });  // seq
        icc1.push_back(std::byte { 0x02 });  // total
        append_bytes(&icc1, "CD");
        append_jpeg_segment(&jpeg, 0xFFE2,
                            std::span<const std::byte>(icc1.data(),
                                                       icc1.size()));

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Icc);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Icc);

        std::array<std::byte, 4> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        EXPECT_EQ(res.needed, 4U);
        EXPECT_EQ(res.written, 4U);
        EXPECT_EQ(out[0], std::byte { 'A' });
        EXPECT_EQ(out[1], std::byte { 'B' });
        EXPECT_EQ(out[2], std::byte { 'C' });
        EXPECT_EQ(out[3], std::byte { 'D' });

        std::array<std::byte, 3> short_out {};
        const PayloadResult short_res
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, short_out, scratch, opts);
        EXPECT_EQ(short_res.status, PayloadStatus::OutputTruncated);
        EXPECT_EQ(short_res.needed, 4U);
        EXPECT_EQ(short_res.written, 3U);

        PayloadCallbackState callback { jpeg };
        const RandomAccessSource source
            = make_callback_random_access_source(jpeg.size(), &callback,
                                                 payload_read_at, true);
        const RandomAccessSourceRange range = make_random_access_source_range(
            source);
        std::array<std::byte, 8> read_window {};
        PayloadRandomAccessScratch random_scratch;
        random_scratch.read_window = read_window;
        out.fill(std::byte { 0U });
        const PayloadRandomAccessResult random_result
            = extract_payload_random_access(
                range,
                std::span<const ContainerBlockRef>(blocks.data(), scan.written),
                0U, out, scratch, random_scratch, opts);
        ASSERT_TRUE(random_result.complete());
        EXPECT_EQ(random_result.payload.status, PayloadStatus::Ok);
        EXPECT_EQ(std::memcmp(out.data(), "ABCD", 4U), 0);
    }


    TEST(ContainerPayload, JpegXmpExtendedGuidOffset)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        const std::string_view guid
            = "0123456789ABCDEF0123456789ABCDEF";  // 32 bytes
        const uint32_t full_len = 6;

        std::vector<std::byte> seg1;
        append_bytes(&seg1, "http://ns.adobe.com/xmp/extension/");
        seg1.push_back(std::byte { 0x00 });
        append_bytes(&seg1, guid);
        append_u32be(&seg1, full_len);
        append_u32be(&seg1, 3);
        append_bytes(&seg1, "DEF");

        std::vector<std::byte> seg0;
        append_bytes(&seg0, "http://ns.adobe.com/xmp/extension/");
        seg0.push_back(std::byte { 0x00 });
        append_bytes(&seg0, guid);
        append_u32be(&seg0, full_len);
        append_u32be(&seg0, 0);
        append_bytes(&seg0, "ABC");

        // Deliberately store out-of-order segments (offset 3, then offset 0).
        append_jpeg_segment(&jpeg, 0xFFE1,
                            std::span<const std::byte>(seg1.data(),
                                                       seg1.size()));
        append_jpeg_segment(&jpeg, 0xFFE1,
                            std::span<const std::byte>(seg0.data(),
                                                       seg0.size()));

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::XmpExtended);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::XmpExtended);

        std::array<std::byte, 16> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        EXPECT_EQ(res.needed, 6U);
        EXPECT_EQ(res.written, 6U);
        EXPECT_EQ(out[0], std::byte { 'A' });
        EXPECT_EQ(out[1], std::byte { 'B' });
        EXPECT_EQ(out[2], std::byte { 'C' });
        EXPECT_EQ(out[3], std::byte { 'D' });
        EXPECT_EQ(out[4], std::byte { 'E' });
        EXPECT_EQ(out[5], std::byte { 'F' });
    }


    TEST(ContainerPayload, JpegApp11JumbfMultipartReassembly)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        // Minimal jumb box bytes.
        std::vector<std::byte> jumb_box;
        append_u32be(&jumb_box, 12U);
        append_fourcc(&jumb_box, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&jumb_box, "ABCD");
        const std::span<const std::byte> payload(jumb_box.data() + 8U,
                                                 jumb_box.size() - 8U);

        // Split payload across 2 APP11 segments with one-based sequence values.
        const size_t mid = payload.size() / 2U;
        for (uint32_t seq = 1U; seq <= 2U; ++seq) {
            std::vector<std::byte> seg;
            append_bytes(&seg, "JP");
            seg.push_back(std::byte { 0x00 });
            seg.push_back(std::byte { 0x00 });
            append_u32be(&seg, seq);
            seg.insert(seg.end(), jumb_box.begin(), jumb_box.begin() + 8);
            if (seq == 1U) {
                seg.insert(seg.end(), payload.begin(),
                           payload.begin() + static_cast<ptrdiff_t>(mid));
            } else {
                seg.insert(seg.end(),
                           payload.begin() + static_cast<ptrdiff_t>(mid),
                           payload.end());
            }
            append_jpeg_segment(&jpeg, 0xFFEB, seg);
        }

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Jumbf);

        std::array<std::byte, 32> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        ASSERT_EQ(res.written, jumb_box.size());
        EXPECT_EQ(std::memcmp(out.data(), jumb_box.data(), jumb_box.size()), 0);
    }

    TEST(ContainerPayload, JpegApp11JumbfMultipartReassemblyViaScanAuto)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        std::vector<std::byte> jumb_box;
        append_u32be(&jumb_box, 12U);
        append_fourcc(&jumb_box, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&jumb_box, "ABCD");
        const std::span<const std::byte> payload(jumb_box.data() + 8U,
                                                 jumb_box.size() - 8U);

        const size_t mid = payload.size() / 2U;
        for (uint32_t seq = 1U; seq <= 2U; ++seq) {
            std::vector<std::byte> seg;
            append_bytes(&seg, "JP");
            seg.push_back(std::byte { 0x00 });
            seg.push_back(std::byte { 0x00 });
            append_u32be(&seg, seq);
            seg.insert(seg.end(), jumb_box.begin(), jumb_box.begin() + 8);
            if (seq == 1U) {
                seg.insert(seg.end(), payload.begin(),
                           payload.begin() + static_cast<ptrdiff_t>(mid));
            } else {
                seg.insert(seg.end(),
                           payload.begin() + static_cast<ptrdiff_t>(mid),
                           payload.end());
            }
            append_jpeg_segment(&jpeg, 0xFFEB, seg);
        }

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_auto(jpeg, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Jumbf);

        std::array<std::byte, 32> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        ASSERT_EQ(res.written, jumb_box.size());
        EXPECT_EQ(std::memcmp(out.data(), jumb_box.data(), jumb_box.size()), 0);
    }

    TEST(ContainerPayload, JpegApp11JumbfAmbiguousStandaloneSegments)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        std::vector<std::byte> seg0;
        append_bytes(&seg0, "JP");
        seg0.push_back(std::byte { 0x00 });
        seg0.push_back(std::byte { 0x00 });
        append_u32be(&seg0, 1U);
        append_u32be(&seg0, 12U);
        append_fourcc(&seg0, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&seg0, "ABCD");
        append_jpeg_segment(&jpeg, 0xFFEB, seg0);

        std::vector<std::byte> seg1;
        append_bytes(&seg1, "JP");
        seg1.push_back(std::byte { 0x00 });
        seg1.push_back(std::byte { 0x00 });
        append_u32be(&seg1, 1U);  // duplicate sequence -> ambiguous fallback
        append_u32be(&seg1, 12U);
        append_fourcc(&seg1, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&seg1, "WXYZ");
        append_jpeg_segment(&jpeg, 0xFFEB, seg1);

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[0].part_count, 1U);
        EXPECT_EQ(blocks[1].part_count, 1U);

        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        std::array<std::byte, 32> out0 {};
        const PayloadResult res0
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out0, scratch, opts);
        EXPECT_EQ(res0.status, PayloadStatus::Ok);
        ASSERT_EQ(res0.written, 12U);
        EXPECT_EQ(out0[0], std::byte { 0x00 });
        EXPECT_EQ(out0[1], std::byte { 0x00 });
        EXPECT_EQ(out0[2], std::byte { 0x00 });
        EXPECT_EQ(out0[3], std::byte { 0x0C });
        EXPECT_EQ(out0[4], std::byte { 'j' });
        EXPECT_EQ(out0[5], std::byte { 'u' });
        EXPECT_EQ(out0[6], std::byte { 'm' });
        EXPECT_EQ(out0[7], std::byte { 'b' });
        EXPECT_EQ(out0[8], std::byte { 'A' });
        EXPECT_EQ(out0[9], std::byte { 'B' });
        EXPECT_EQ(out0[10], std::byte { 'C' });
        EXPECT_EQ(out0[11], std::byte { 'D' });

        std::array<std::byte, 32> out1 {};
        const PayloadResult res1
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              1, out1, scratch, opts);
        EXPECT_EQ(res1.status, PayloadStatus::Ok);
        ASSERT_EQ(res1.written, 12U);
        EXPECT_EQ(out1[8], std::byte { 'W' });
        EXPECT_EQ(out1[9], std::byte { 'X' });
        EXPECT_EQ(out1[10], std::byte { 'Y' });
        EXPECT_EQ(out1[11], std::byte { 'Z' });
    }

    TEST(ContainerPayload, JpegApp11HeaderOnlyFirstPartReassembly)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        std::vector<std::byte> jumb_box;
        append_u32be(&jumb_box, 12U);
        append_fourcc(&jumb_box, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&jumb_box, "ABCD");
        const std::span<const std::byte> payload(jumb_box.data() + 8U,
                                                 jumb_box.size() - 8U);

        // Part 1: preamble + seq + BMFF header only.
        std::vector<std::byte> seg1;
        append_bytes(&seg1, "JP");
        seg1.push_back(std::byte { 0x00 });
        seg1.push_back(std::byte { 0x00 });
        append_u32be(&seg1, 1U);
        seg1.insert(seg1.end(), jumb_box.begin(), jumb_box.begin() + 8);
        append_jpeg_segment(&jpeg, 0xFFEB, seg1);

        // Part 2: preamble + seq + BMFF header + payload.
        std::vector<std::byte> seg2;
        append_bytes(&seg2, "JP");
        seg2.push_back(std::byte { 0x00 });
        seg2.push_back(std::byte { 0x00 });
        append_u32be(&seg2, 2U);
        seg2.insert(seg2.end(), jumb_box.begin(), jumb_box.begin() + 8);
        seg2.insert(seg2.end(), payload.begin(), payload.end());
        append_jpeg_segment(&jpeg, 0xFFEB, seg2);

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[0].part_count, 2U);
        EXPECT_EQ(blocks[0].part_index, 0U);
        EXPECT_EQ(blocks[1].part_index, 1U);

        std::array<std::byte, 32> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        ASSERT_EQ(res.written, jumb_box.size());
        EXPECT_EQ(std::memcmp(out.data(), jumb_box.data(), jumb_box.size()), 0);
    }


    TEST(ContainerPayload, PngCaBxJumbfPayload)
    {
        std::vector<std::byte> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };

        std::vector<std::byte> jumb_box;
        append_u32be(&jumb_box, 12U);
        append_fourcc(&jumb_box, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&jumb_box, "ABCD");

        append_png_chunk(&png, fourcc('c', 'a', 'B', 'X'), jumb_box);
        append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'), {});

        std::array<ContainerBlockRef, 4> blocks {};
        const ScanResult scan = scan_png(png, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 1U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);

        std::array<std::byte, 32> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(png,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        ASSERT_EQ(res.written, jumb_box.size());
        EXPECT_EQ(std::memcmp(out.data(), jumb_box.data(), jumb_box.size()), 0);
    }

    TEST(ContainerPayload, PngCaBxJumbfSplitPayload)
    {
        std::vector<std::byte> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };

        std::vector<std::byte> jumb_box;
        append_u32be(&jumb_box, 12U);
        append_fourcc(&jumb_box, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&jumb_box, "ABCD");
        const size_t mid = 8U;  // keep BMFF header in the first split chunk
        ASSERT_GT(jumb_box.size(), mid);

        append_png_chunk(&png, fourcc('c', 'a', 'B', 'X'),
                         std::span<const std::byte>(jumb_box.data(), mid));
        append_png_chunk(&png, fourcc('c', 'a', 'B', 'X'),
                         std::span<const std::byte>(
                             jumb_box.data() + static_cast<ptrdiff_t>(mid),
                             jumb_box.size() - mid));
        append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'), {});

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_png(png, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[0].part_count, 2U);
        EXPECT_EQ(blocks[0].part_index, 0U);
        EXPECT_EQ(blocks[1].part_index, 1U);

        std::array<std::byte, 32> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(png,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        ASSERT_EQ(res.written, jumb_box.size());
        EXPECT_EQ(std::memcmp(out.data(), jumb_box.data(), jumb_box.size()), 0);
    }


    TEST(ContainerPayload, WebpC2paJumbfPayload)
    {
        std::vector<std::byte> webp;
        append_bytes(&webp, "RIFF");
        append_u32le(&webp, 0U);  // placeholder
        append_bytes(&webp, "WEBP");

        std::vector<std::byte> jumb_box;
        append_u32be(&jumb_box, 12U);
        append_fourcc(&jumb_box, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&jumb_box, "ABCD");

        append_fourcc(&webp, fourcc('C', '2', 'P', 'A'));
        append_u32le(&webp, static_cast<uint32_t>(jumb_box.size()));
        webp.insert(webp.end(), jumb_box.begin(), jumb_box.end());
        if ((jumb_box.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        const uint32_t riff_size = static_cast<uint32_t>(
            (webp.size() >= 8U) ? (webp.size() - 8U) : 0U);
        webp[4] = std::byte { static_cast<uint8_t>((riff_size >> 0) & 0xFFU) };
        webp[5] = std::byte { static_cast<uint8_t>((riff_size >> 8) & 0xFFU) };
        webp[6] = std::byte { static_cast<uint8_t>((riff_size >> 16) & 0xFFU) };
        webp[7] = std::byte { static_cast<uint8_t>((riff_size >> 24) & 0xFFU) };

        std::array<ContainerBlockRef, 4> blocks {};
        const ScanResult scan = scan_webp(webp, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 1U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);

        std::array<std::byte, 32> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(webp,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        ASSERT_EQ(res.written, jumb_box.size());
        EXPECT_EQ(std::memcmp(out.data(), jumb_box.data(), jumb_box.size()), 0);
    }

    TEST(ContainerPayload, WebpC2paJumbfSplitPayload)
    {
        std::vector<std::byte> webp;
        append_bytes(&webp, "RIFF");
        append_u32le(&webp, 0U);  // placeholder
        append_bytes(&webp, "WEBP");

        std::vector<std::byte> jumb_box;
        append_u32be(&jumb_box, 12U);
        append_fourcc(&jumb_box, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&jumb_box, "ABCD");
        const size_t mid = 8U;  // keep BMFF header in the first split chunk
        ASSERT_GT(jumb_box.size(), mid);

        append_fourcc(&webp, fourcc('C', '2', 'P', 'A'));
        append_u32le(&webp, static_cast<uint32_t>(mid));
        webp.insert(webp.end(), jumb_box.begin(),
                    jumb_box.begin() + static_cast<ptrdiff_t>(mid));
        if ((mid & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        append_fourcc(&webp, fourcc('C', '2', 'P', 'A'));
        append_u32le(&webp, static_cast<uint32_t>(jumb_box.size() - mid));
        webp.insert(webp.end(), jumb_box.begin() + static_cast<ptrdiff_t>(mid),
                    jumb_box.end());
        if (((jumb_box.size() - mid) & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        const uint32_t riff_size = static_cast<uint32_t>(
            (webp.size() >= 8U) ? (webp.size() - 8U) : 0U);
        webp[4] = std::byte { static_cast<uint8_t>((riff_size >> 0) & 0xFFU) };
        webp[5] = std::byte { static_cast<uint8_t>((riff_size >> 8) & 0xFFU) };
        webp[6] = std::byte { static_cast<uint8_t>((riff_size >> 16) & 0xFFU) };
        webp[7] = std::byte { static_cast<uint8_t>((riff_size >> 24) & 0xFFU) };

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_webp(webp, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Jumbf);
        EXPECT_EQ(blocks[0].part_count, 2U);
        EXPECT_EQ(blocks[0].part_index, 0U);
        EXPECT_EQ(blocks[1].part_index, 1U);

        std::array<std::byte, 32> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(webp,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        ASSERT_EQ(res.written, jumb_box.size());
        EXPECT_EQ(std::memcmp(out.data(), jumb_box.data(), jumb_box.size()), 0);
    }


    TEST(ContainerPayload, BmffMetaItemExtents)
    {
        // ISO-BMFF file with a single Exif item split across two `iloc` extents.
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
        append_u32be(&idat_payload, 0);  // TIFF header offset
        {
            const std::array<std::byte, 4> tiff_hdr = {
                std::byte { 'I' },
                std::byte { 'I' },
                std::byte { 0x2A },
                std::byte { 0x00 },
            };
            idat_payload.insert(idat_payload.end(), tiff_hdr.begin(),
                                tiff_hdr.end());
        }
        append_u32le(&idat_payload, 8);
        // IFD0 at offset 8: 0 entries, next=0.
        append_u16be(&idat_payload, 0);
        append_u32le(&idat_payload, 0);
        ASSERT_EQ(idat_payload.size(), 18U);
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
        append_u16be(&iloc_payload, 2);              // extent_count
        append_u32be(&iloc_payload, 0);              // extent0 offset
        append_u32be(&iloc_payload, 12);             // extent0 length
        append_u32be(&iloc_payload, 12);             // extent1 offset
        append_u32be(&iloc_payload, 6);              // extent1 length
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
        const ScanResult scan = scan_bmff(file, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].part_count, 2U);

        std::array<std::byte, 64> out {};
        std::array<uint32_t, 16> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(file,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        EXPECT_EQ(res.needed, 14U);
        EXPECT_EQ(out[0], std::byte { 'I' });
        EXPECT_EQ(out[1], std::byte { 'I' });
        EXPECT_EQ(out[2], std::byte { 0x2A });
        EXPECT_EQ(out[3], std::byte { 0x00 });
    }

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    TEST(ContainerPayload, PngItxtDeflate)
    {
        std::vector<std::byte> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };

        const std::string_view xml = "<xmp/>";

        uLongf comp_cap = compressBound(static_cast<uLong>(xml.size()));
        std::vector<std::byte> comp(static_cast<size_t>(comp_cap));
        const int zres
            = compress2(reinterpret_cast<Bytef*>(comp.data()), &comp_cap,
                        reinterpret_cast<const Bytef*>(xml.data()),
                        static_cast<uLong>(xml.size()), Z_BEST_COMPRESSION);
        ASSERT_EQ(zres, Z_OK);
        comp.resize(static_cast<size_t>(comp_cap));

        std::vector<std::byte> itxt;
        append_bytes(&itxt, "XML:com.adobe.xmp");
        itxt.push_back(std::byte { 0x00 });
        itxt.push_back(std::byte { 0x01 });  // compressed
        itxt.push_back(std::byte { 0x00 });  // method=0 (zlib)
        itxt.push_back(std::byte { 0x00 });  // lang
        itxt.push_back(std::byte { 0x00 });  // trans
        itxt.insert(itxt.end(), comp.begin(), comp.end());
        append_png_chunk(&png, fourcc('i', 'T', 'X', 't'), itxt);

        append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'), {});

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_png(png, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 1U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[0].compression, BlockCompression::Deflate);

        std::array<std::byte, 64> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(png,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        ASSERT_EQ(res.written, xml.size());
        EXPECT_EQ(std::memcmp(out.data(), xml.data(), xml.size()), 0);

        PayloadCallbackState callback { png };
        const RandomAccessSource source
            = make_callback_random_access_source(png.size(), &callback,
                                                 payload_read_at, true);
        const RandomAccessSourceRange range = make_random_access_source_range(
            source);
        std::array<std::byte, 16> read_window {};
        std::array<std::byte, 4> compressed_small {};
        PayloadRandomAccessScratch random_scratch;
        random_scratch.read_window = read_window;
        random_scratch.compressed  = compressed_small;
        const PayloadRandomAccessResult needs_compressed
            = extract_payload_random_access(
                range,
                std::span<const ContainerBlockRef>(blocks.data(), scan.written),
                0U, out, scratch, random_scratch, opts);
        EXPECT_FALSE(needs_compressed.complete());
        EXPECT_EQ(needs_compressed.payload.status,
                  PayloadStatus::OutputTruncated);
        EXPECT_EQ(needs_compressed.compressed_scratch_needed, comp.size());

        std::vector<std::byte> compressed(comp.size());
        random_scratch.compressed = compressed;
        const PayloadRandomAccessResult random_result
            = extract_payload_random_access(
                range,
                std::span<const ContainerBlockRef>(blocks.data(), scan.written),
                0U, out, scratch, random_scratch, opts);
        ASSERT_TRUE(random_result.complete());
        EXPECT_EQ(random_result.payload.status, PayloadStatus::Ok);
        EXPECT_EQ(random_result.payload.written, xml.size());
        EXPECT_EQ(std::memcmp(out.data(), xml.data(), xml.size()), 0);
    }
#endif

}  // namespace
}  // namespace openmeta
