// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"

#include "openmeta/byte_arena.h"
#include "openmeta/simple_meta.h"
#include "openmeta/vendor_raw_processing.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
#    include <zlib.h>
#endif

namespace openmeta {
namespace {

    static void append_bytes(std::vector<std::byte>* out, std::string_view s)
    {
        out->insert(out->end(), reinterpret_cast<const std::byte*>(s.data()),
                    reinterpret_cast<const std::byte*>(s.data() + s.size()));
    }


    static void append_u16le(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
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

    static void append_jpeg_segment(std::vector<std::byte>* out,
                                    uint16_t marker,
                                    std::span<const std::byte> payload)
    {
        out->push_back(std::byte { 0xFF });
        out->push_back(std::byte { static_cast<uint8_t>(marker & 0xFF) });
        const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
        append_u16be(out, seg_len);
        out->insert(out->end(), payload.begin(), payload.end());
    }


    static void write_u32le_at(std::vector<std::byte>* out, size_t off,
                               uint32_t v)
    {
        ASSERT_TRUE(out != nullptr);
        ASSERT_LE(off + 4U, out->size());
        (*out)[off + 0U] = std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) };
        (*out)[off + 1U] = std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) };
        (*out)[off + 2U] = std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) };
        (*out)[off + 3U] = std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) };
    }


    static void write_i32le_at(std::vector<std::byte>* out, size_t off,
                               int32_t v)
    {
        uint32_t bits = 0U;
        std::memcpy(&bits, &v, sizeof(bits));
        write_u32le_at(out, off, bits);
    }


    static uint32_t f32_bits(float v) noexcept
    {
        uint32_t bits = 0U;
        std::memcpy(&bits, &v, sizeof(bits));
        return bits;
    }


    static std::string_view arena_string(const ByteArena& arena,
                                         const MetaValue& v)
    {
        const std::span<const std::byte> bytes = arena.span(v.data.span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }


    struct TiffCallbackState final {
        std::span<const std::byte> bytes;
        uint64_t max_bytes   = UINT64_MAX;
        uint64_t max_request = 0U;
        uint32_t calls       = 0U;
    };


    static RandomAccessIoResult
    tiff_read_at(void* context, uint64_t offset,
                 std::span<std::byte> destination) noexcept
    {
        TiffCallbackState* state = static_cast<TiffCallbackState*>(context);
        state->calls += 1U;
        if (destination.size() > state->max_request) {
            state->max_request = destination.size();
        }
        uint64_t available = 0U;
        if (offset <= state->bytes.size()) {
            available = static_cast<uint64_t>(state->bytes.size()) - offset;
        }
        uint64_t count = static_cast<uint64_t>(destination.size());
        if (count > available) {
            count = available;
        }
        if (count > state->max_bytes) {
            count = state->max_bytes;
        }
        if (count != 0U) {
            std::memcpy(destination.data(),
                        state->bytes.data() + static_cast<size_t>(offset),
                        static_cast<size_t>(count));
        }
        return RandomAccessIoResult { RandomAccessIoCode::Ok, count };
    }


    static MetaKeyView exif_key(std::string_view ifd, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }


    TEST(ExifTiffDecodeTest, SharedArenaBudgetStopsAliasedValueAmplification)
    {
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42U);
        append_u32le(&tiff, 8U);
        append_u16le(&tiff, 2U);
        for (uint16_t tag : { uint16_t { 0x010E }, uint16_t { 0x010F } }) {
            append_u16le(&tiff, tag);
            append_u16le(&tiff, 2U);  // ASCII
            append_u32le(&tiff, 8U);
            append_u32le(&tiff, 38U);  // Both entries alias one source value.
        }
        append_u32le(&tiff, 0U);
        append_bytes(&tiff, "1234567");
        tiff.push_back(std::byte { 0 });

        ExifDecodeOptions options;
        options.limits.max_arena_bytes = 20U;
        MetaStore store;
        const ExifDecodeResult result = decode_exif_tiff(tiff, store, {},
                                                         options);

        EXPECT_EQ(result.status, ExifDecodeStatus::LimitExceeded);
        EXPECT_TRUE(store.resource_limit_exceeded());
        EXPECT_LE(store.arena().bytes().size(), 20U);
        EXPECT_LT(store.entries().size(), 2U);
    }

    struct X3fPropPair final {
        std::string_view key;
        std::string_view value;
    };

    static void write_fixed_ascii_at(std::vector<std::byte>* out, size_t off,
                                     size_t field_size, std::string_view text)
    {
        ASSERT_TRUE(out != nullptr);
        ASSERT_LE(off + field_size, out->size());
        ASSERT_LT(text.size(), field_size);
        for (size_t i = 0; i < text.size(); ++i) {
            (*out)[off + i] = std::byte { static_cast<uint8_t>(text[i]) };
        }
    }

    static uint32_t append_x3f_prop_text(std::vector<std::byte>* chars,
                                         std::string_view text)
    {
        const uint32_t pos = static_cast<uint32_t>(chars->size() / 2U);
        for (char c : text) {
            chars->push_back(std::byte { static_cast<uint8_t>(c) });
            chars->push_back(std::byte { 0 });
        }
        chars->push_back(std::byte { 0 });
        chars->push_back(std::byte { 0 });
        return pos;
    }

    static std::vector<std::byte>
    make_x3f_prop_section(std::span<const X3fPropPair> pairs)
    {
        std::array<uint32_t, 16> name_pos {};
        std::array<uint32_t, 16> value_pos {};
        EXPECT_LE(pairs.size(), name_pos.size());

        std::vector<std::byte> chars;
        const size_t count = (pairs.size() < name_pos.size()) ? pairs.size()
                                                              : name_pos.size();
        for (size_t i = 0; i < count; ++i) {
            name_pos[i]  = append_x3f_prop_text(&chars, pairs[i].key);
            value_pos[i] = append_x3f_prop_text(&chars, pairs[i].value);
        }

        std::vector<std::byte> prop;
        append_bytes(&prop, "SECp");
        append_u32le(&prop, 1U);
        append_u32le(&prop, static_cast<uint32_t>(count));
        append_u32le(&prop, 0U);
        append_u32le(&prop, 0U);
        append_u32le(&prop, static_cast<uint32_t>(chars.size() / 2U));
        for (size_t i = 0; i < count; ++i) {
            append_u32le(&prop, name_pos[i]);
            append_u32le(&prop, value_pos[i]);
        }
        prop.insert(prop.end(), chars.begin(), chars.end());
        return prop;
    }

    static MetaKeyView png_text_key(std::string_view keyword,
                                    std::string_view field)
    {
        MetaKeyView key;
        key.kind                  = MetaKeyKind::PngText;
        key.data.png_text.keyword = keyword;
        key.data.png_text.field   = field;
        return key;
    }

    static MetaKeyView comment_key() noexcept
    {
        MetaKeyView key;
        key.kind = MetaKeyKind::Comment;
        return key;
    }

    static void append_png_chunk(std::vector<std::byte>* out, uint32_t type,
                                 std::span<const std::byte> data)
    {
        append_u32be(out, static_cast<uint32_t>(data.size()));
        append_u32be(out, type);
        out->insert(out->end(), data.begin(), data.end());
        append_u32be(out, 0U);
    }

    TEST(ByteArena, AppendCopiesSelfAliasedSpanAcrossReallocation)
    {
        ByteArena arena;
        arena.reserve(4);

        const std::array<std::byte, 4> seed = {
            std::byte { 'A' },
            std::byte { 'B' },
            std::byte { 'C' },
            std::byte { 'D' },
        };
        const ByteSpan seed_span = arena.append(
            std::span<const std::byte>(seed.data(), seed.size()));
        const std::span<const std::byte> alias = arena.span(seed_span);
        ASSERT_EQ(alias.size(), 4U);

        const ByteSpan appended                = arena.append(alias);
        const std::span<const std::byte> bytes = arena.bytes();
        ASSERT_EQ(appended.offset, 4U);
        ASSERT_EQ(appended.size, 4U);
        ASSERT_EQ(bytes.size(), 8U);
        EXPECT_EQ(bytes[0], std::byte { 'A' });
        EXPECT_EQ(bytes[1], std::byte { 'B' });
        EXPECT_EQ(bytes[2], std::byte { 'C' });
        EXPECT_EQ(bytes[3], std::byte { 'D' });
        EXPECT_EQ(bytes[4], std::byte { 'A' });
        EXPECT_EQ(bytes[5], std::byte { 'B' });
        EXPECT_EQ(bytes[6], std::byte { 'C' });
        EXPECT_EQ(bytes[7], std::byte { 'D' });
    }

    TEST(ByteArena, AppendRejectsUnrepresentableSpan)
    {
        ByteArena arena;
        const std::array<std::byte, 1> seed = { std::byte { 0x11 } };
        const std::span<const std::byte> huge(seed.data(),
                                              static_cast<size_t>(UINT32_MAX)
                                                  + 1U);

        const ByteSpan span = arena.append(huge);
        EXPECT_EQ(span.offset, 0U);
        EXPECT_EQ(span.size, 0U);
        EXPECT_TRUE(arena.bytes().empty());
    }

    TEST(MetaValue, MakeBytesRejectsUnrepresentableSpan)
    {
        ByteArena arena;
        const std::array<std::byte, 1> seed = { std::byte { 0x22 } };
        const std::span<const std::byte> huge(seed.data(),
                                              static_cast<size_t>(UINT32_MAX)
                                                  + 1U);

        const MetaValue value = make_bytes(arena, huge);
        EXPECT_EQ(value.kind, MetaValueKind::Empty);
        EXPECT_TRUE(arena.bytes().empty());
    }

    TEST(MetaValue, MakeTextRejectsUnrepresentableView)
    {
        ByteArena arena;
        const char seed[1] = { 'A' };
        const std::string_view huge(seed, static_cast<size_t>(UINT32_MAX) + 1U);

        const MetaValue value = make_text(arena, huge, TextEncoding::Ascii);
        EXPECT_EQ(value.kind, MetaValueKind::Empty);
        EXPECT_TRUE(arena.bytes().empty());
    }

    TEST(MetaValue, MakeU16ArrayRejectsUnrepresentableByteSize)
    {
        ByteArena arena;
        const std::array<uint16_t, 1> seed = { 7U };
        const std::span<const uint16_t> huge(
            seed.data(), static_cast<size_t>(UINT32_MAX / 2U) + 1U);

        const MetaValue value = make_u16_array(arena, huge);
        EXPECT_EQ(value.kind, MetaValueKind::Empty);
        EXPECT_TRUE(arena.bytes().empty());
    }


    static std::vector<std::byte> make_test_tiff_le()
    {
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, 8);

        // IFD0 (offset 8).
        append_u16le(&tiff, 2);

        // Make (0x010F) ASCII "Canon\0" at offset 38.
        append_u16le(&tiff, 0x010F);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 6);
        append_u32le(&tiff, 38);

        // ExifIFDPointer (0x8769) LONG offset 44.
        append_u16le(&tiff, 0x8769);
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, 44);

        // next IFD offset.
        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 38U);
        append_bytes(&tiff, "Canon");
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), 44U);

        // ExifIFD (offset 44).
        append_u16le(&tiff, 1);

        // DateTimeOriginal (0x9003) ASCII at offset 62.
        append_u16le(&tiff, 0x9003);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 20);
        append_u32le(&tiff, 62);

        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 62U);
        append_bytes(&tiff, "2024:01:01 00:00:00");
        tiff.push_back(std::byte { 0 });
        return tiff;
    }


    static std::vector<std::byte> make_test_panasonic_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Panasonic");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });

        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0003);  // WhiteBalance
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 1);
        append_u16le(&mn, 4);  // cloudy
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);  // next IFD
        return mn;
    }


    static std::vector<std::byte> make_test_panasonic_tiff_le()
    {
        const std::vector<std::byte> mn = make_test_panasonic_makernote();

        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, 8);

        // IFD0: Make + ExifIFDPointer.
        append_u16le(&tiff, 2);

        append_u16le(&tiff, 0x010F);  // Make
        append_u16le(&tiff, 2);       // ASCII
        append_u32le(&tiff, 10);      // "Panasonic\0"
        append_u32le(&tiff, 38);

        append_u16le(&tiff, 0x8769);  // ExifIFDPointer
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, 48);

        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 38U);
        append_bytes(&tiff, "Panasonic");
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), 48U);

        // ExifIFD: DateTimeOriginal + MakerNote.
        append_u16le(&tiff, 2);

        append_u16le(&tiff, 0x9003);  // DateTimeOriginal
        append_u16le(&tiff, 2);       // ASCII
        append_u32le(&tiff, 20);
        append_u32le(&tiff, 78);

        append_u16le(&tiff, 0x927C);  // MakerNote
        append_u16le(&tiff, 7);       // UNDEFINED
        append_u32le(&tiff, static_cast<uint32_t>(mn.size()));
        append_u32le(&tiff, 98);

        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 78U);
        append_bytes(&tiff, "2024:01:01 00:00:00");
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), 98U);
        tiff.insert(tiff.end(), mn.begin(), mn.end());
        return tiff;
    }


    static std::vector<std::byte> make_test_pentax_dng_private_data()
    {
        std::vector<std::byte> data;
        append_bytes(&data, "PENTAX ");
        data.push_back(std::byte { 0 });
        append_bytes(&data, "II");

        append_u16le(&data, 1);       // entry count
        append_u16le(&data, 0x000C);  // FlashMode
        append_u16le(&data, 3);       // SHORT
        append_u32le(&data, 1);
        append_u16le(&data, 2);  // off, did not fire
        append_u16le(&data, 0);
        append_u32le(&data, 0);  // next IFD
        return data;
    }


    static std::vector<std::byte> make_test_pentax_dng_tiff_le()
    {
        const std::vector<std::byte> dng_private
            = make_test_pentax_dng_private_data();

        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, 8);

        append_u16le(&tiff, 3);

        append_u16le(&tiff, 0x010F);  // Make
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 7);  // "PENTAX\0"
        append_u32le(&tiff, 50);

        append_u16le(&tiff, 0x0110);  // Model
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 11);  // "PENTAX K-1\0"
        append_u32le(&tiff, 57);

        append_u16le(&tiff, 0xC634);  // DNGPrivateData
        append_u16le(&tiff, 1);       // BYTE
        append_u32le(&tiff, static_cast<uint32_t>(dng_private.size()));
        append_u32le(&tiff, 68);

        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 50U);
        append_bytes(&tiff, "PENTAX");
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), 57U);
        append_bytes(&tiff, "PENTAX K-1");
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), 68U);
        tiff.insert(tiff.end(), dng_private.begin(), dng_private.end());
        return tiff;
    }


    static std::vector<std::byte> make_test_nikon_nefinfo_tiff_le()
    {
        std::vector<std::byte> nefinfo;
        append_bytes(&nefinfo, "Nikon");
        nefinfo.push_back(std::byte { 0 });
        nefinfo.push_back(std::byte { 1 });
        nefinfo.push_back(std::byte { 1 });
        nefinfo.push_back(std::byte { 0 });
        nefinfo.push_back(std::byte { 0 });
        append_bytes(&nefinfo, "II");
        append_u16le(&nefinfo, 42);
        append_u32le(&nefinfo, 8);

        append_u16le(&nefinfo, 2);       // entry count
        append_u16le(&nefinfo, 0x0005);  // DistortionInfo
        append_u16le(&nefinfo, 7);       // UNDEFINED
        append_u32le(&nefinfo, 44);
        append_u32le(&nefinfo, 38);
        append_u16le(&nefinfo, 0x000B);  // Unknown NEFInfo block
        append_u16le(&nefinfo, 7);       // UNDEFINED
        append_u32le(&nefinfo, 14);
        append_u32le(&nefinfo, 82);
        append_u32le(&nefinfo, 0);
        EXPECT_EQ(nefinfo.size(), 48U);

        std::vector<std::byte> distortion(44U, std::byte { 0 });
        std::memcpy(distortion.data(), "0100", 4U);
        distortion[4] = std::byte { 1 };
        write_i32le_at(&distortion, 0x14U, -1429);
        write_u32le_at(&distortion, 0x18U, 100000U);
        write_i32le_at(&distortion, 0x1cU, 1221);
        write_u32le_at(&distortion, 0x20U, 100000U);
        write_i32le_at(&distortion, 0x24U, -615);
        write_u32le_at(&distortion, 0x28U, 100000U);
        nefinfo.insert(nefinfo.end(), distortion.begin(), distortion.end());

        const std::byte raw_nefinfo_unknown[] = {
            std::byte { '0' },  std::byte { '1' },  std::byte { '0' },
            std::byte { '0' },  std::byte { 0x0c }, std::byte { 0x00 },
            std::byte { 0x08 }, std::byte { 0x00 }, std::byte { 0x40 },
            std::byte { 0x20 }, std::byte { 0x80 }, std::byte { 0x15 },
            std::byte { 0x3d }, std::byte { 0x0e },
        };
        nefinfo.insert(nefinfo.end(), std::begin(raw_nefinfo_unknown),
                       std::end(raw_nefinfo_unknown));

        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, 8);

        append_u16le(&tiff, 3);

        append_u16le(&tiff, 0x010F);  // Make
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 6);
        append_u32le(&tiff, 50);

        append_u16le(&tiff, 0x0110);  // Model
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 10);
        append_u32le(&tiff, 56);

        append_u16le(&tiff, 0x014A);  // SubIFDs
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, 66);

        append_u32le(&tiff, 0);
        EXPECT_EQ(tiff.size(), 50U);

        append_bytes(&tiff, "Nikon");
        tiff.push_back(std::byte { 0 });
        EXPECT_EQ(tiff.size(), 56U);

        append_bytes(&tiff, "NIKON Z 5");
        tiff.push_back(std::byte { 0 });
        EXPECT_EQ(tiff.size(), 66U);

        append_u16le(&tiff, 1);
        append_u16le(&tiff, 0xC7D5);
        append_u16le(&tiff, 7);
        append_u32le(&tiff, static_cast<uint32_t>(nefinfo.size()));
        append_u32le(&tiff, 84);
        append_u32le(&tiff, 0);
        EXPECT_EQ(tiff.size(), 84U);

        tiff.insert(tiff.end(), nefinfo.begin(), nefinfo.end());
        return tiff;
    }


    static std::vector<std::byte> make_test_sigma_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "SIGMA");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });

        append_u16be(&mn, 2);  // entry count

        append_u16be(&mn, 0x0002);  // SerialNumber
        append_u16be(&mn, 2);       // ASCII
        append_u32be(&mn, 9);
        append_u32be(&mn, 38);

        append_u16be(&mn, 0x0003);  // DriveMode
        append_u16be(&mn, 2);       // ASCII
        append_u32be(&mn, 6);
        append_u32be(&mn, 47);

        append_u32be(&mn, 0);  // next IFD

        EXPECT_EQ(mn.size(), 38U);
        append_bytes(&mn, "90301541");
        mn.push_back(std::byte { 0 });

        EXPECT_EQ(mn.size(), 47U);
        append_bytes(&mn, "2 Sec");
        mn.push_back(std::byte { 0 });
        return mn;
    }


    static std::vector<std::byte> make_test_sigma_tiff_le()
    {
        const std::vector<std::byte> mn = make_test_sigma_makernote();

        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, 8);

        append_u16le(&tiff, 2);

        append_u16le(&tiff, 0x010F);  // Make
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 6);
        append_u32le(&tiff, 38);

        append_u16le(&tiff, 0x8769);  // ExifIFD
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, 44);

        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 38U);
        append_bytes(&tiff, "SIGMA");
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), 44U);
        append_u16le(&tiff, 1);

        append_u16le(&tiff, 0x927C);  // MakerNote
        append_u16le(&tiff, 7);       // UNDEFINED
        append_u32le(&tiff, static_cast<uint32_t>(mn.size()));
        append_u32le(&tiff, 62);

        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 62U);
        tiff.insert(tiff.end(), mn.begin(), mn.end());
        return tiff;
    }


    static std::vector<std::byte> make_test_sigma_wb_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "SIGMA");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });

        append_u16be(&mn, 1);       // entry count
        append_u16be(&mn, 0x0120);  // WBSettings
        append_u16be(&mn, 11);      // FLOAT
        append_u32be(&mn, 30);
        append_u32be(&mn, 26);
        append_u32be(&mn, 0);  // next IFD

        EXPECT_EQ(mn.size(), 26U);
        for (uint32_t i = 0; i < 30U; ++i) {
            append_u32be(&mn, f32_bits(static_cast<float>(i + 1U)));
        }
        return mn;
    }


    static std::vector<std::byte> make_test_sigma_wb_tiff_le()
    {
        const std::vector<std::byte> mn = make_test_sigma_wb_makernote();

        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, 8);

        append_u16le(&tiff, 2);

        append_u16le(&tiff, 0x010F);  // Make
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 6);
        append_u32le(&tiff, 38);

        append_u16le(&tiff, 0x8769);  // ExifIFD
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, 44);

        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 38U);
        append_bytes(&tiff, "SIGMA");
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), 44U);
        append_u16le(&tiff, 1);

        append_u16le(&tiff, 0x927C);  // MakerNote
        append_u16le(&tiff, 7);       // UNDEFINED
        append_u32le(&tiff, static_cast<uint32_t>(mn.size()));
        append_u32le(&tiff, 62);

        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 62U);
        tiff.insert(tiff.end(), mn.begin(), mn.end());
        return tiff;
    }


    static std::vector<std::byte> make_test_tiff_be()
    {
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "MM");
        append_u16be(&tiff, 42);
        append_u32be(&tiff, 8);

        // IFD0 (offset 8).
        append_u16be(&tiff, 2);

        // Make (0x010F) ASCII "Canon\0" at offset 38.
        append_u16be(&tiff, 0x010F);
        append_u16be(&tiff, 2);
        append_u32be(&tiff, 6);
        append_u32be(&tiff, 38);

        // ExifIFDPointer (0x8769) LONG offset 44.
        append_u16be(&tiff, 0x8769);
        append_u16be(&tiff, 4);
        append_u32be(&tiff, 1);
        append_u32be(&tiff, 44);

        // next IFD offset.
        append_u32be(&tiff, 0);

        EXPECT_EQ(tiff.size(), 38U);
        append_bytes(&tiff, "Canon");
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), 44U);

        // ExifIFD (offset 44).
        append_u16be(&tiff, 1);

        // DateTimeOriginal (0x9003) ASCII at offset 62.
        append_u16be(&tiff, 0x9003);
        append_u16be(&tiff, 2);
        append_u32be(&tiff, 20);
        append_u32be(&tiff, 62);

        append_u32be(&tiff, 0);

        EXPECT_EQ(tiff.size(), 62U);
        append_bytes(&tiff, "2024:01:01 00:00:00");
        tiff.push_back(std::byte { 0 });
        return tiff;
    }

}  // namespace

TEST(ExifTiffDecode, DecodesIfd0AndExifIfd_LittleEndian)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.include_pointer_tags = true;
    const ExifDecodeResult res   = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);
    EXPECT_EQ(res.ifds_written, 2U);
    EXPECT_EQ(res.entries_decoded, 3U);

    store.finalize();

    const std::span<const EntryId> make_ids = store.find_all(
        exif_key("ifd0", 0x010F));
    ASSERT_EQ(make_ids.size(), 1U);
    const Entry& make = store.entry(make_ids[0]);
    EXPECT_EQ(make.origin.wire_type.family, WireFamily::Tiff);
    EXPECT_EQ(make.origin.wire_type.code, 2U);
    EXPECT_EQ(make.origin.wire_count, 6U);
    EXPECT_EQ(make.value.kind, MetaValueKind::Text);
    EXPECT_EQ(make.value.text_encoding, TextEncoding::Ascii);
    EXPECT_EQ(arena_string(store.arena(), make.value), "Canon");

    const std::span<const EntryId> ptr_ids = store.find_all(
        exif_key("ifd0", 0x8769));
    ASSERT_EQ(ptr_ids.size(), 1U);
    const Entry& ptr = store.entry(ptr_ids[0]);
    EXPECT_EQ(ptr.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(ptr.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(ptr.value.data.u64, 44U);

    const std::span<const EntryId> dt_ids = store.find_all(
        exif_key("exififd", 0x9003));
    ASSERT_EQ(dt_ids.size(), 1U);
    const Entry& dt = store.entry(dt_ids[0]);
    EXPECT_EQ(dt.origin.wire_count, 20U);
    EXPECT_EQ(dt.value.kind, MetaValueKind::Text);
    EXPECT_EQ(arena_string(store.arena(), dt.value), "2024:01:01 00:00:00");
}


TEST(ExifTiffDecode, RandomAccessCallbackMatchesSpanThroughSourceSubrange)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();
    std::vector<std::byte> backing(11U, std::byte { 0xA5 });
    backing.insert(backing.end(), tiff.begin(), tiff.end());

    MetaStore span_store;
    std::array<ExifIfdRef, 8> span_ifds {};
    ExifDecodeOptions options;
    const ExifDecodeResult span_result = decode_exif_tiff(tiff, span_store,
                                                          span_ifds, options);

    TiffCallbackState callback { backing };
    const RandomAccessSource source
        = make_callback_random_access_source(backing.size(), &callback,
                                             tiff_read_at);
    const RandomAccessSourceRange range
        = make_random_access_source_range(source, 11U,
                                          static_cast<uint64_t>(tiff.size()));
    std::array<std::byte, 24> read_window {};
    std::array<std::byte, 64> value_scratch {};
    ExifRandomAccessScratch scratch;
    scratch.read_window                       = read_window;
    scratch.value                             = value_scratch;
    scratch.window_options.minimum_read_bytes = read_window.size();

    MetaStore callback_store;
    std::array<ExifIfdRef, 8> callback_ifds {};
    const ExifRandomAccessDecodeResult callback_result
        = decode_exif_tiff_random_access(range, callback_store, callback_ifds,
                                         scratch, options);

    EXPECT_TRUE(callback_result.complete());
    EXPECT_EQ(callback_result.decode.status, span_result.status);
    EXPECT_EQ(callback_result.decode.ifds_written, span_result.ifds_written);
    EXPECT_EQ(callback_result.decode.ifds_needed, span_result.ifds_needed);
    EXPECT_EQ(callback_result.decode.entries_decoded,
              span_result.entries_decoded);
    ASSERT_EQ(callback_result.decode.ifds_written, 2U);
    for (uint32_t i = 0U; i < callback_result.decode.ifds_written; ++i) {
        EXPECT_EQ(callback_ifds[i].kind, span_ifds[i].kind);
        EXPECT_EQ(callback_ifds[i].index, span_ifds[i].index);
        EXPECT_EQ(callback_ifds[i].offset, span_ifds[i].offset);
    }
    EXPECT_GT(callback.calls, 0U);
    EXPECT_LE(callback.max_request, read_window.size());
    EXPECT_LT(callback_result.input.bytes_requested, tiff.size() * 2U);

    callback_store.finalize();
    const std::span<const EntryId> make_ids = callback_store.find_all(
        exif_key("ifd0", 0x010F));
    ASSERT_EQ(make_ids.size(), 1U);
    EXPECT_EQ(arena_string(callback_store.arena(),
                           callback_store.entry(make_ids[0]).value),
              "Canon");
    const std::span<const EntryId> date_ids = callback_store.find_all(
        exif_key("exififd", 0x9003));
    ASSERT_EQ(date_ids.size(), 1U);
    EXPECT_EQ(arena_string(callback_store.arena(),
                           callback_store.entry(date_ids[0]).value),
              "2024:01:01 00:00:00");
}


TEST(ExifTiffDecode, RandomAccessCallbackDecodesBigTiffAndRawHeaders)
{
    std::vector<std::byte> bigtiff;
    append_bytes(&bigtiff, "II");
    append_u16le(&bigtiff, 43U);
    append_u16le(&bigtiff, 8U);
    append_u16le(&bigtiff, 0U);
    append_u64le(&bigtiff, 16U);
    append_u64le(&bigtiff, 1U);
    append_u16le(&bigtiff, 0x0100U);
    append_u16le(&bigtiff, 4U);
    append_u64le(&bigtiff, 1U);
    append_u64le(&bigtiff, 123U);
    append_u64le(&bigtiff, 0U);

    auto decode_callback = [&](std::span<const std::byte> bytes) {
        TiffCallbackState callback { bytes };
        const RandomAccessSource source
            = make_callback_random_access_source(bytes.size(), &callback,
                                                 tiff_read_at);
        std::array<std::byte, 20> read_window {};
        std::array<std::byte, 64> value_scratch {};
        ExifRandomAccessScratch scratch;
        scratch.read_window                       = read_window;
        scratch.value                             = value_scratch;
        scratch.window_options.minimum_read_bytes = read_window.size();
        MetaStore store;
        std::array<ExifIfdRef, 8> ifds {};
        const ExifRandomAccessDecodeResult result
            = decode_exif_tiff_random_access(make_random_access_source_range(
                                                 source),
                                             store, ifds, scratch,
                                             ExifDecodeOptions {});
        EXPECT_TRUE(result.complete());
        EXPECT_EQ(result.decode.status, ExifDecodeStatus::Ok);
        return result.decode.entries_decoded;
    };

    EXPECT_EQ(decode_callback(bigtiff), 1U);

    for (const uint16_t version :
         { uint16_t { 0x0055U }, uint16_t { 0x4F52U } }) {
        std::vector<std::byte> raw;
        append_bytes(&raw, "II");
        append_u16le(&raw, version);
        append_u32le(&raw, 8U);
        append_u16le(&raw, 1U);
        append_u16le(&raw, 0x0100U);
        append_u16le(&raw, 4U);
        append_u32le(&raw, 1U);
        append_u32le(&raw, 321U);
        append_u32le(&raw, 0U);
        EXPECT_EQ(decode_callback(raw), 1U);
    }
}


TEST(ExifTiffDecode, RandomAccessCallbackReportsScratchAndSourceFailures)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();

    auto make_source = [](TiffCallbackState* callback) {
        return make_callback_random_access_source(callback->bytes.size(),
                                                  callback, tiff_read_at);
    };

    TiffCallbackState short_callback { tiff };
    short_callback.max_bytes = 4U;
    std::array<std::byte, 24> window {};
    std::array<std::byte, 64> value {};
    ExifRandomAccessScratch scratch;
    scratch.read_window = window;
    scratch.value       = value;
    MetaStore short_store;
    const ExifRandomAccessDecodeResult short_result
        = decode_exif_tiff_random_access(make_random_access_source_range(
                                             make_source(&short_callback)),
                                         short_store, {}, scratch,
                                         ExifDecodeOptions {});
    EXPECT_EQ(short_result.input.code, RandomAccessReadCode::ShortRead);
    EXPECT_EQ(short_result.decode.status, ExifDecodeStatus::OutputTruncated);
    EXPECT_FALSE(short_result.complete());

    TiffCallbackState budget_callback { tiff };
    RandomAccessReadLimits limits;
    limits.max_requests                       = 16U;
    limits.max_total_bytes                    = 8U;
    limits.max_single_read_bytes              = window.size();
    scratch.window_options.minimum_read_bytes = 8U;
    MetaStore budget_store;
    const ExifRandomAccessDecodeResult budget_result
        = decode_exif_tiff_random_access(make_random_access_source_range(
                                             make_source(&budget_callback)),
                                         budget_store, {}, scratch,
                                         ExifDecodeOptions {}, limits);
    EXPECT_EQ(budget_result.input.code,
              RandomAccessReadCode::ByteLimitExceeded);
    EXPECT_EQ(budget_result.decode.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_FALSE(budget_result.complete());

    std::vector<std::byte> malformed;
    append_bytes(&malformed, "II");
    append_u16le(&malformed, 42U);
    append_u32le(&malformed, 8U);
    append_u16le(&malformed, 1U);
    append_u16le(&malformed, 0x010FU);
    append_u16le(&malformed, 2U);
    append_u32le(&malformed, 16U);
    append_u32le(&malformed, UINT32_MAX - 7U);
    append_u32le(&malformed, 0U);
    TiffCallbackState malformed_callback { malformed };
    scratch.window_options.minimum_read_bytes = window.size();
    MetaStore malformed_store;
    const ExifRandomAccessDecodeResult malformed_result
        = decode_exif_tiff_random_access(make_random_access_source_range(
                                             make_source(&malformed_callback)),
                                         malformed_store, {}, scratch,
                                         ExifDecodeOptions {});
    EXPECT_TRUE(malformed_result.input.ok());
    EXPECT_TRUE(malformed_result.complete());
    EXPECT_EQ(malformed_result.decode.status, ExifDecodeStatus::Malformed);

    std::vector<std::byte> bigtiff;
    append_bytes(&bigtiff, "II");
    append_u16le(&bigtiff, 43U);
    append_u16le(&bigtiff, 8U);
    append_u16le(&bigtiff, 0U);
    append_u64le(&bigtiff, 16U);
    append_u64le(&bigtiff, 1U);
    append_u16le(&bigtiff, 0x0100U);
    append_u16le(&bigtiff, 4U);
    append_u64le(&bigtiff, 1U);
    append_u64le(&bigtiff, 123U);
    append_u64le(&bigtiff, 0U);
    TiffCallbackState scratch_callback { bigtiff };
    std::array<std::byte, 19> small_window {};
    scratch.read_window                       = small_window;
    scratch.window_options.minimum_read_bytes = small_window.size();
    MetaStore scratch_store;
    const ExifRandomAccessDecodeResult scratch_result
        = decode_exif_tiff_random_access(make_random_access_source_range(
                                             make_source(&scratch_callback)),
                                         scratch_store, {}, scratch,
                                         ExifDecodeOptions {});
    EXPECT_EQ(scratch_result.input.code, RandomAccessReadCode::ScratchTooSmall);
    EXPECT_EQ(scratch_result.decode.status, ExifDecodeStatus::LimitExceeded);
}


TEST(ExifTiffDecode, RandomAccessCallbackReportsValueScratchAndCompletesVendors)
{
    const std::vector<std::byte> pentax = make_test_pentax_dng_tiff_le();
    auto decode                         = [](std::span<const std::byte> bytes,
                     std::span<std::byte> value_scratch) {
        TiffCallbackState callback { bytes };
        const RandomAccessSource source
            = make_callback_random_access_source(bytes.size(), &callback,
                                                                         tiff_read_at);
        std::array<std::byte, 20> read_window {};
        ExifRandomAccessScratch scratch;
        scratch.read_window                       = read_window;
        scratch.value                             = value_scratch;
        scratch.window_options.minimum_read_bytes = read_window.size();
        ExifDecodeOptions options;
        options.decode_makernote = true;
        MetaStore store;
        const ExifRandomAccessDecodeResult result
            = decode_exif_tiff_random_access(make_random_access_source_range(
                                                 source),
                                                                     store, {}, scratch, options);
        return result;
    };

    std::array<std::byte, 8> too_small {};
    const ExifRandomAccessDecodeResult small_result = decode(pentax, too_small);
    EXPECT_TRUE(small_result.input.ok());
    EXPECT_GT(small_result.value_scratch_needed, too_small.size());
    EXPECT_FALSE(small_result.complete());

    std::array<std::byte, 128> enough {};
    const ExifRandomAccessDecodeResult complete_result = decode(pentax, enough);
    EXPECT_EQ(complete_result.decode.status, ExifDecodeStatus::Ok);
    EXPECT_TRUE(complete_result.complete());

    const std::vector<std::byte> sigma              = make_test_sigma_tiff_le();
    const ExifRandomAccessDecodeResult sigma_result = decode(sigma, enough);
    EXPECT_EQ(sigma_result.decode.status, ExifDecodeStatus::Ok);
    EXPECT_TRUE(sigma_result.complete());

    const std::vector<std::byte> panasonic = make_test_panasonic_tiff_le();
    const ExifRandomAccessDecodeResult panasonic_result = decode(panasonic,
                                                                 enough);
    EXPECT_EQ(panasonic_result.decode.status, ExifDecodeStatus::Ok);
    EXPECT_TRUE(panasonic_result.complete());
}


TEST(ExifTiffDecode, DecodesIfd0AndExifIfd_BigEndian)
{
    const std::vector<std::byte> tiff = make_test_tiff_be();

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.include_pointer_tags = true;
    const ExifDecodeResult res   = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);
    EXPECT_EQ(res.ifds_written, 2U);
    EXPECT_EQ(res.entries_decoded, 3U);

    store.finalize();

    const std::span<const EntryId> make_ids = store.find_all(
        exif_key("ifd0", 0x010F));
    ASSERT_EQ(make_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(make_ids[0]).value),
              "Canon");

    const std::span<const EntryId> dt_ids = store.find_all(
        exif_key("exififd", 0x9003));
    ASSERT_EQ(dt_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(dt_ids[0]).value),
              "2024:01:01 00:00:00");
}

TEST(ExifTiffDecode, EstimateMatchesDecodeCounters)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();
    ExifDecodeOptions options;
    options.include_pointer_tags = true;

    const ExifDecodeResult estimate = measure_exif_tiff(tiff, options);
    EXPECT_EQ(estimate.status, ExifDecodeStatus::Ok);
    EXPECT_EQ(estimate.ifds_needed, 2U);
    EXPECT_EQ(estimate.entries_decoded, 3U);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    const ExifDecodeResult decoded = decode_exif_tiff(tiff, store, ifds,
                                                      options);
    EXPECT_EQ(decoded.status, estimate.status);
    EXPECT_EQ(decoded.ifds_needed, estimate.ifds_needed);
    EXPECT_EQ(decoded.entries_decoded, estimate.entries_decoded);
}

TEST(ExifTiffDecode, EstimateRespectsEntryLimitOverrides)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();
    ExifDecodeOptions options;
    options.include_pointer_tags     = true;
    options.limits.max_total_entries = 2U;

    const ExifDecodeResult estimate = measure_exif_tiff(tiff, options);
    EXPECT_EQ(estimate.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(estimate.limit_reason, ExifLimitReason::MaxTotalEntries);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    const ExifDecodeResult decoded = decode_exif_tiff(tiff, store, ifds,
                                                      options);
    EXPECT_EQ(decoded.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(decoded.limit_reason, ExifLimitReason::MaxTotalEntries);
}

TEST(ExifTiffDecode, AcceptsTiffRawVariantHeaders)
{
    auto make_min = [&](uint16_t version_le) {
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, version_le);
        append_u32le(&tiff, 8);

        // IFD0 at offset 8 with a single Make tag.
        append_u16le(&tiff, 1);
        append_u16le(&tiff, 0x010F);  // Make
        append_u16le(&tiff, 2);       // ASCII
        append_u32le(&tiff, 6);       // "Canon\0"
        append_u32le(&tiff, 26);      // value offset
        append_u32le(&tiff, 0);       // next IFD

        EXPECT_EQ(tiff.size(), 26U);
        append_bytes(&tiff, "Canon");
        tiff.push_back(std::byte { 0 });
        return tiff;
    };

    // Panasonic RW2 ("IIU\0") and Olympus ORF ("IIRO") variant headers.
    const std::array<uint16_t, 2> versions = { 0x0055, 0x4F52 };

    for (const uint16_t v : versions) {
        const std::vector<std::byte> tiff = make_min(v);

        MetaStore store;
        std::array<ExifIfdRef, 8> ifds {};
        ExifDecodeOptions options;
        const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds,
                                                      options);
        EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

        store.finalize();
        const std::span<const EntryId> make_ids = store.find_all(
            exif_key("ifd0", 0x010F));
        ASSERT_EQ(make_ids.size(), 1U);
        EXPECT_EQ(arena_string(store.arena(), store.entry(make_ids[0]).value),
                  "Canon");
    }
}


TEST(ExifTiffDecode, PreservesUtf8Type129)
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42);
    append_u32le(&tiff, 8);

    // IFD0 with a single UTF-8 tag (type 129) stored inline.
    append_u16le(&tiff, 1);
    append_u16le(&tiff, 0x010E);  // ImageDescription
    append_u16le(&tiff, 129);     // UTF-8
    append_u32le(&tiff, 3);       // "Hi\0"
    append_bytes(&tiff, "Hi");
    tiff.push_back(std::byte { 0 });
    tiff.push_back(std::byte { 0 });  // pad to 4
    append_u32le(&tiff, 0);

    MetaStore store;
    std::array<ExifIfdRef, 4> ifds {};
    ExifDecodeOptions options;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x010E));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.origin.wire_type.code, 129U);
    EXPECT_EQ(e.value.kind, MetaValueKind::Text);
    EXPECT_EQ(e.value.text_encoding, TextEncoding::Utf8);
    EXPECT_EQ(arena_string(store.arena(), e.value), "Hi");
}


TEST(ExifTiffDecode, AsciiWithEmbeddedNulIsStoredAsBytes)
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42);
    append_u32le(&tiff, 8);

    // IFD0 (offset 8).
    append_u16le(&tiff, 1);

    // ImageDescription (0x010E) ASCII count=4 stored inline: "A\0B\0".
    append_u16le(&tiff, 0x010E);
    append_u16le(&tiff, 2);
    append_u32le(&tiff, 4);
    tiff.push_back(std::byte { 'A' });
    tiff.push_back(std::byte { 0 });
    tiff.push_back(std::byte { 'B' });
    tiff.push_back(std::byte { 0 });

    // next IFD offset.
    append_u32le(&tiff, 0);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.include_pointer_tags = true;
    const ExifDecodeResult res   = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x010E));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
    const std::span<const std::byte> bytes = store.arena().span(
        e.value.data.span);
    ASSERT_EQ(bytes.size(), 4U);
    EXPECT_EQ(bytes[0], std::byte { 'A' });
    EXPECT_EQ(bytes[1], std::byte { 0 });
    EXPECT_EQ(bytes[2], std::byte { 'B' });
    EXPECT_EQ(bytes[3], std::byte { 0 });
}


TEST(ExifTiffDecode, OutOfBoundsValueIsRejected)
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42);
    append_u32le(&tiff, 8);

    // IFD0 (offset 8).
    append_u16le(&tiff, 1);

    // Make (0x010F) ASCII count=6 requires an offset; point it out-of-bounds.
    append_u16le(&tiff, 0x010F);
    append_u16le(&tiff, 2);
    append_u32le(&tiff, 6);
    append_u32le(&tiff, 0x1000);

    // next IFD offset.
    append_u32le(&tiff, 0);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.include_pointer_tags = true;
    const ExifDecodeResult res   = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Malformed);

    store.finalize();
    EXPECT_TRUE(store.entries().empty());
}

TEST(ExifTiffDecode, OversizedValueIsTruncatedWithoutLimitExceeded)
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42);
    append_u32le(&tiff, 8);

    // IFD0 (offset 8), one UNDEFINED entry with 16 bytes at offset 26.
    append_u16le(&tiff, 1);
    append_u16le(&tiff, 0x9286);  // UserComment
    append_u16le(&tiff, 7);       // UNDEFINED
    append_u32le(&tiff, 16);
    append_u32le(&tiff, 26);
    append_u32le(&tiff, 0);

    for (uint32_t i = 0; i < 16; ++i) {
        tiff.push_back(std::byte { static_cast<uint8_t>(i) });
    }

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.limits.max_value_bytes = 8;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);
    EXPECT_EQ(res.limit_reason, ExifLimitReason::None);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x9286));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::Truncated));
    EXPECT_EQ(e.value.kind, MetaValueKind::Empty);
}

TEST(ExifTiffDecode, ReportsLimitReasonForMaxEntriesPerIfd)
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42);
    append_u32le(&tiff, 8);

    // IFD0 with two inline SHORT entries.
    append_u16le(&tiff, 2);
    append_u16le(&tiff, 0x0100);
    append_u16le(&tiff, 3);
    append_u32le(&tiff, 1);
    append_u16le(&tiff, 600);
    append_u16le(&tiff, 0);
    append_u16le(&tiff, 0x0101);
    append_u16le(&tiff, 3);
    append_u32le(&tiff, 1);
    append_u16le(&tiff, 400);
    append_u16le(&tiff, 0);
    append_u32le(&tiff, 0);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.limits.max_entries_per_ifd = 1;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(res.limit_reason, ExifLimitReason::MaxEntriesPerIfd);
    EXPECT_EQ(res.limit_ifd_offset, 8U);
    EXPECT_EQ(res.limit_tag, 0U);
}

TEST(ExifTiffDecode, BigTiffReportsLimitReasonForMaxEntriesPerIfd)
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 43);
    append_u16le(&tiff, 8);
    append_u16le(&tiff, 0);
    append_u64le(&tiff, 16);
    append_u64le(&tiff, std::numeric_limits<uint64_t>::max());

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.limits.max_entries_per_ifd = 4096U;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(res.limit_reason, ExifLimitReason::MaxEntriesPerIfd);
    EXPECT_EQ(res.limit_ifd_offset, 16U);
    EXPECT_EQ(res.limit_tag, 0U);
}

TEST(ExifTiffDecode, BigTiffOutOfBoundsValueOffsetIsMalformed)
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 43);
    append_u16le(&tiff, 8);
    append_u16le(&tiff, 0);
    append_u64le(&tiff, 16);

    append_u64le(&tiff, 1);
    append_u16le(&tiff, 0x010F);
    append_u16le(&tiff, 2);
    append_u64le(&tiff, 16);
    append_u64le(&tiff, std::numeric_limits<uint64_t>::max() - 7ULL);
    append_u64le(&tiff, 0);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.include_pointer_tags = true;
    const ExifDecodeResult res   = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Malformed);

    store.finalize();
    EXPECT_TRUE(store.entries().empty());
}


TEST(SimpleMetaRead, ScansAndDecodesJpegApp1Exif)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();

    std::vector<std::byte> jpeg;
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD8 });

    std::vector<std::byte> payload;
    append_bytes(&payload, "Exif");
    payload.push_back(std::byte { 0 });
    payload.push_back(std::byte { 0 });
    payload.insert(payload.end(), tiff.begin(), tiff.end());

    const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xE1 });
    jpeg.push_back(std::byte { static_cast<uint8_t>((seg_len >> 8) & 0xFF) });
    jpeg.push_back(std::byte { static_cast<uint8_t>((seg_len >> 0) & 0xFF) });
    jpeg.insert(jpeg.end(), payload.begin(), payload.end());

    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD9 });

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload_scratch {};
    std::array<uint32_t, 16> payload_parts {};
    ExifDecodeOptions exif_options;
    PayloadOptions payload_options;
    const SimpleMetaResult res
        = simple_meta_read(jpeg, store, blocks, ifds, payload_scratch,
                           payload_parts, exif_options, payload_options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("exififd", 0x9003));
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(ids[0]).value),
              "2024:01:01 00:00:00");
}


TEST(SimpleMetaRead, DecodesEmbeddedJpegFromRawTag002E)
{
    // Build an embedded JPEG preview containing a minimal APP1 Exif segment.
    const std::vector<std::byte> tiff = make_test_tiff_le();

    std::vector<std::byte> jpeg;
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD8 });

    std::vector<std::byte> payload;
    append_bytes(&payload, "Exif");
    payload.push_back(std::byte { 0 });
    payload.push_back(std::byte { 0 });
    payload.insert(payload.end(), tiff.begin(), tiff.end());

    const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xE1 });
    jpeg.push_back(std::byte { static_cast<uint8_t>((seg_len >> 8) & 0xFF) });
    jpeg.push_back(std::byte { static_cast<uint8_t>((seg_len >> 0) & 0xFF) });
    jpeg.insert(jpeg.end(), payload.begin(), payload.end());

    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD9 });

    // Build an outer TIFF that stores the embedded JPEG as tag 0x002E.
    std::vector<std::byte> outer;
    append_bytes(&outer, "II");
    append_u16le(&outer, 42);
    append_u32le(&outer, 8);

    // IFD0 at offset 8: one entry, then next IFD offset.
    append_u16le(&outer, 1);
    append_u16le(&outer, 0x002E);  // JpgFromRaw
    append_u16le(&outer, 7);       // UNDEFINED
    append_u32le(&outer, static_cast<uint32_t>(jpeg.size()));
    append_u32le(&outer, 26);  // value offset (right after this IFD)
    append_u32le(&outer, 0);

    EXPECT_EQ(outer.size(), 26U);
    outer.insert(outer.end(), jpeg.begin(), jpeg.end());

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::array<std::byte, 8192> payload_scratch {};
    std::array<uint32_t, 64> payload_parts {};
    ExifDecodeOptions exif_options;
    exif_options.decode_embedded_containers = true;
    PayloadOptions payload_options;

    const SimpleMetaResult res
        = simple_meta_read(outer, store, blocks, ifds, payload_scratch,
                           payload_parts, exif_options, payload_options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("exififd", 0x9003));
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(ids[0]).value),
              "2024:01:01 00:00:00");
}


TEST(SimpleMetaRead, DecodesEmbeddedJpegMakerNoteFromRawTag002E)
{
    const std::vector<std::byte> tiff = make_test_panasonic_tiff_le();

    std::vector<std::byte> jpeg;
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD8 });

    std::vector<std::byte> payload;
    append_bytes(&payload, "Exif");
    payload.push_back(std::byte { 0 });
    payload.push_back(std::byte { 0 });
    payload.insert(payload.end(), tiff.begin(), tiff.end());

    const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xE1 });
    jpeg.push_back(std::byte { static_cast<uint8_t>((seg_len >> 8) & 0xFF) });
    jpeg.push_back(std::byte { static_cast<uint8_t>((seg_len >> 0) & 0xFF) });
    jpeg.insert(jpeg.end(), payload.begin(), payload.end());

    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD9 });

    std::vector<std::byte> outer;
    append_bytes(&outer, "II");
    append_u16le(&outer, 42);
    append_u32le(&outer, 8);

    append_u16le(&outer, 1);
    append_u16le(&outer, 0x002E);  // JpgFromRaw
    append_u16le(&outer, 7);       // UNDEFINED
    append_u32le(&outer, static_cast<uint32_t>(jpeg.size()));
    append_u32le(&outer, 26);
    append_u32le(&outer, 0);

    EXPECT_EQ(outer.size(), 26U);
    outer.insert(outer.end(), jpeg.begin(), jpeg.end());

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::array<std::byte, 8192> payload_scratch {};
    std::array<uint32_t, 64> payload_parts {};
    ExifDecodeOptions exif_options;
    exif_options.decode_makernote           = true;
    exif_options.decode_embedded_containers = true;
    PayloadOptions payload_options;

    const SimpleMetaResult res
        = simple_meta_read(outer, store, blocks, ifds, payload_scratch,
                           payload_parts, exif_options, payload_options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    store.finalize();

    const std::span<const EntryId> dt_ids = store.find_all(
        exif_key("exififd", 0x9003));
    ASSERT_EQ(dt_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(dt_ids[0]).value),
              "2024:01:01 00:00:00");

    const std::span<const EntryId> wb_ids = store.find_all(
        exif_key("mk_panasonic0", 0x0003));
    ASSERT_EQ(wb_ids.size(), 1U);
    EXPECT_EQ(store.entry(wb_ids[0]).value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(store.entry(wb_ids[0]).value.elem_type, MetaElementType::U16);
    EXPECT_EQ(store.entry(wb_ids[0]).value.data.u64, 4U);
}


TEST(ExifTiffDecode, DecodesPentaxMakerNoteFromDngPrivateData)
{
    const std::vector<std::byte> tiff = make_test_pentax_dng_tiff_le();

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_pentax0", 0x000C));
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(store.entry(ids[0]).value.elem_type, MetaElementType::U16);
    EXPECT_EQ(store.entry(ids[0]).value.data.u64, 2U);
}


TEST(ExifTiffDecode, DecodesNikonNefInfoDistortionInfoAsMakerNoteFields)
{
    const std::vector<std::byte> tiff = make_test_nikon_nefinfo_tiff_le();

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    const std::span<const EntryId> version_ids = store.find_all(
        exif_key("mk_nikon_distortioninfo_0", 0x0000));
    ASSERT_EQ(version_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(version_ids[0]).value),
              "0100");

    const std::span<const EntryId> nefinfo_dist_ids = store.find_all(
        exif_key("mk_nikon_nefinfo_0", 0x0005));
    ASSERT_EQ(nefinfo_dist_ids.size(), 1U);
    EXPECT_EQ(store.entry(nefinfo_dist_ids[0]).value.kind,
              MetaValueKind::Bytes);

    const std::span<const EntryId> nefinfo_unknown_ids = store.find_all(
        exif_key("mk_nikon_nefinfo_0", 0x000B));
    ASSERT_EQ(nefinfo_unknown_ids.size(), 1U);
    EXPECT_EQ(store.entry(nefinfo_unknown_ids[0]).value.kind,
              MetaValueKind::Bytes);
    EXPECT_EQ(store.entry(nefinfo_unknown_ids[0]).value.count, 14U);

    const std::span<const EntryId> enabled_ids = store.find_all(
        exif_key("mk_nikon_distortioninfo_0", 0x0004));
    ASSERT_EQ(enabled_ids.size(), 1U);
    EXPECT_EQ(store.entry(enabled_ids[0]).value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(store.entry(enabled_ids[0]).value.elem_type, MetaElementType::U8);
    EXPECT_EQ(store.entry(enabled_ids[0]).value.data.u64, 1U);

    const std::span<const EntryId> coeff1_ids = store.find_all(
        exif_key("mk_nikon_distortioninfo_0", 0x0014));
    ASSERT_EQ(coeff1_ids.size(), 1U);
    EXPECT_EQ(store.entry(coeff1_ids[0]).value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(store.entry(coeff1_ids[0]).value.elem_type,
              MetaElementType::SRational);
    EXPECT_EQ(store.entry(coeff1_ids[0]).value.data.sr.numer, -1429);
    EXPECT_EQ(store.entry(coeff1_ids[0]).value.data.sr.denom, 100000);

    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_distortioninfo_0", 0x001C)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_distortioninfo_0", 0x0024)).size(),
        1U);
}


TEST(ExifTiffDecode, DecodesSigmaMakerNoteUnderSigmaIfd)
{
    const std::vector<std::byte> tiff = make_test_sigma_tiff_le();

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    const std::span<const EntryId> serial_ids = store.find_all(
        exif_key("mk_sigma0", 0x0002));
    ASSERT_EQ(serial_ids.size(), 1U);
    EXPECT_EQ(store.entry(serial_ids[0]).value.kind, MetaValueKind::Text);
    EXPECT_EQ(arena_string(store.arena(), store.entry(serial_ids[0]).value),
              "90301541");

    const std::span<const EntryId> drive_mode_ids = store.find_all(
        exif_key("mk_sigma0", 0x0003));
    ASSERT_EQ(drive_mode_ids.size(), 1U);

    EXPECT_TRUE(store.find_all(exif_key("mkifd0", 0x0002)).empty());
}


TEST(ExifTiffDecode, DecodesSigmaWbSettingsSubtables)
{
    const std::vector<std::byte> tiff = make_test_sigma_wb_tiff_le();

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    const std::span<const EntryId> auto_ids = store.find_all(
        exif_key("mk_sigma_wbsettings_0", 0x0000));
    ASSERT_EQ(auto_ids.size(), 1U);
    const Entry& auto_entry = store.entry(auto_ids[0]);
    EXPECT_EQ(auto_entry.value.kind, MetaValueKind::Array);
    EXPECT_EQ(auto_entry.value.elem_type, MetaElementType::F32);
    EXPECT_EQ(auto_entry.value.count, 3U);

    const std::span<const std::byte> auto_raw = store.arena().span(
        auto_entry.value.data.span);
    ASSERT_EQ(auto_raw.size(), 12U);
    uint32_t first_bits = 0U;
    uint32_t last_bits  = 0U;
    std::memcpy(&first_bits, auto_raw.data() + 0, sizeof(first_bits));
    std::memcpy(&last_bits, auto_raw.data() + 8, sizeof(last_bits));
    EXPECT_EQ(first_bits, f32_bits(1.0f));
    EXPECT_EQ(last_bits, f32_bits(3.0f));

    const std::span<const EntryId> custom3_ids = store.find_all(
        exif_key("mk_sigma_wbsettings_0", 0x001B));
    ASSERT_EQ(custom3_ids.size(), 1U);
    const Entry& custom3_entry = store.entry(custom3_ids[0]);
    EXPECT_EQ(custom3_entry.value.kind, MetaValueKind::Array);
    EXPECT_EQ(custom3_entry.value.elem_type, MetaElementType::F32);
    EXPECT_EQ(custom3_entry.value.count, 3U);

    const std::span<const std::byte> custom3_raw = store.arena().span(
        custom3_entry.value.data.span);
    ASSERT_EQ(custom3_raw.size(), 12U);
    std::memcpy(&first_bits, custom3_raw.data() + 0, sizeof(first_bits));
    std::memcpy(&last_bits, custom3_raw.data() + 8, sizeof(last_bits));
    EXPECT_EQ(first_bits, f32_bits(28.0f));
    EXPECT_EQ(last_bits, f32_bits(30.0f));
}


TEST(SimpleMetaRead, DecodesRafEmbeddedTiff)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();

    std::vector<std::byte> raf;
    append_bytes(&raf, "FUJIFILMCCD-RAW ");
    raf.resize(160, std::byte { 0 });
    raf.insert(raf.end(), tiff.begin(), tiff.end());

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload_scratch {};
    std::array<uint32_t, 16> payload_parts {};
    ExifDecodeOptions exif_options;
    PayloadOptions payload_options;
    const SimpleMetaResult res
        = simple_meta_read(raf, store, blocks, ifds, payload_scratch,
                           payload_parts, exif_options, payload_options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);
    ASSERT_GE(res.scan.written, 1U);
    EXPECT_EQ(blocks[0].format, ContainerFormat::Raf);
    EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x010F));
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(ids[0]).value), "Canon");
}

TEST(SimpleMetaRead, DecodesRafPreviewJpegExif)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();

    std::vector<std::byte> jpeg;
    jpeg.push_back(std::byte { 0xff });
    jpeg.push_back(std::byte { 0xd8 });
    std::vector<std::byte> exif_payload;
    append_bytes(&exif_payload, "Exif");
    exif_payload.push_back(std::byte { 0 });
    exif_payload.push_back(std::byte { 0 });
    exif_payload.insert(exif_payload.end(), tiff.begin(), tiff.end());
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

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload_scratch {};
    std::array<uint32_t, 16> payload_parts {};
    ExifDecodeOptions exif_options;
    PayloadOptions payload_options;
    const SimpleMetaResult res
        = simple_meta_read(raf, store, blocks, ifds, payload_scratch,
                           payload_parts, exif_options, payload_options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);
    ASSERT_GE(res.scan.written, 2U);
    EXPECT_EQ(blocks[0].format, ContainerFormat::Raf);
    EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);

    store.finalize();
    const std::span<const EntryId> make_ids = store.find_all(
        exif_key("ifd0", 0x010F));
    ASSERT_EQ(make_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(make_ids[0]).value),
              "Canon");

    const std::span<const EntryId> date_ids = store.find_all(
        exif_key("exififd", 0x9003));
    ASSERT_EQ(date_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(date_ids[0]).value),
              "2024:01:01 00:00:00");
}

TEST(SimpleMetaRead, DecodesRafNativeDirectory)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();

    std::vector<std::byte> dir;
    append_u32be(&dir, 3U);
    append_u16be(&dir, 0x0100U);
    append_u16be(&dir, 4U);
    append_u16be(&dir, 6048U);
    append_u16be(&dir, 4032U);
    append_u16be(&dir, 0x0110U);
    append_u16be(&dir, 4U);
    append_u16be(&dir, 12U);
    append_u16be(&dir, 8U);
    append_u16be(&dir, 0xc000U);
    append_u16be(&dir, 16U);
    append_u32be(&dir, 6048U);
    append_u32be(&dir, 6048U);
    append_u32be(&dir, 4032U);
    append_u32be(&dir, 4032U);

    const uint32_t dir_off  = 192U;
    const uint32_t tiff_off = 320U;

    std::vector<std::byte> raf;
    append_bytes(&raf, "FUJIFILMCCD-RAW ");
    raf.resize(0x3cU, std::byte { 0x00 });
    append_bytes(&raf, "0200");
    raf.resize(0x5cU, std::byte { 0x00 });
    append_u32be(&raf, dir_off);
    append_u32be(&raf, static_cast<uint32_t>(dir.size()));
    append_u32be(&raf, tiff_off);
    append_u32be(&raf, static_cast<uint32_t>(tiff.size()));
    raf.resize(dir_off, std::byte { 0x00 });
    raf.insert(raf.end(), dir.begin(), dir.end());
    raf.resize(tiff_off, std::byte { 0x00 });
    raf.insert(raf.end(), tiff.begin(), tiff.end());

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload_scratch {};
    std::array<uint32_t, 16> payload_parts {};
    ExifDecodeOptions exif_options;
    PayloadOptions payload_options;
    const SimpleMetaResult res
        = simple_meta_read(raf, store, blocks, ifds, payload_scratch,
                           payload_parts, exif_options, payload_options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);
    EXPECT_GT(res.exif.entries_decoded, 0U);

    store.finalize();

    const std::span<const EntryId> version_ids = store.find_all(
        exif_key("raf_header", 0x003c));
    ASSERT_EQ(version_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(version_ids[0]).value),
              "0200");

    const std::span<const EntryId> size_ids = store.find_all(
        exif_key("raf_0", 0x0100));
    ASSERT_EQ(size_ids.size(), 1U);
    const Entry& size_entry = store.entry(size_ids[0]);
    EXPECT_EQ(size_entry.value.kind, MetaValueKind::Array);
    EXPECT_EQ(size_entry.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(size_entry.value.count, 2U);

    const std::span<const EntryId> left_ids = store.find_all(
        exif_key("raf_0", 0x0110));
    ASSERT_EQ(left_ids.size(), 1U);
    const Entry& left_entry = store.entry(left_ids[0]);
    EXPECT_EQ(left_entry.value.kind, MetaValueKind::Array);
    EXPECT_EQ(left_entry.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(left_entry.value.count, 2U);

    const std::span<const EntryId> width_ids = store.find_all(
        exif_key("mk_fuji_rafdata_0", 0x0000));
    ASSERT_EQ(width_ids.size(), 1U);
    EXPECT_EQ(store.entry(width_ids[0]).value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(store.entry(width_ids[0]).value.elem_type, MetaElementType::U32);
    EXPECT_EQ(store.entry(width_ids[0]).value.data.u64, 6048U);

    const VendorRawProcessingSummary summary
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Fujifilm);
    EXPECT_GE(summary.fields_seen, 1U);
    EXPECT_GE(summary.geometry_fields, 1U);
    EXPECT_GE(summary.raw_data_fields, 1U);
}


TEST(SimpleMetaRead, DecodesX3fEmbeddedExifTiff)
{
    const std::vector<std::byte> tiff = make_test_tiff_be();

    std::vector<std::byte> x3f;
    append_bytes(&x3f, "FOVb");
    x3f.resize(128, std::byte { 0 });
    append_bytes(&x3f, "Exif");
    x3f.push_back(std::byte { 0 });
    x3f.push_back(std::byte { 0 });
    x3f.insert(x3f.end(), tiff.begin(), tiff.end());

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload_scratch {};
    std::array<uint32_t, 16> payload_parts {};
    ExifDecodeOptions exif_options;
    PayloadOptions payload_options;
    const SimpleMetaResult res
        = simple_meta_read(x3f, store, blocks, ifds, payload_scratch,
                           payload_parts, exif_options, payload_options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);
    ASSERT_GE(res.scan.written, 1U);
    EXPECT_EQ(blocks[0].format, ContainerFormat::X3f);
    EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x010F));
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(ids[0]).value), "Canon");
}

TEST(SimpleMetaRead, DecodesX3fNativeHeaderAndProperties)
{
    std::vector<std::byte> x3f;
    append_bytes(&x3f, "FOVb");
    append_u32le(&x3f, (2U << 16U) | 3U);
    append_bytes(&x3f, "0123456789abcdef");
    append_u32le(&x3f, 0U);
    append_u32le(&x3f, 2640U);
    append_u32le(&x3f, 1760U);
    append_u32le(&x3f, 0U);
    x3f.resize(104U, std::byte { 0 });
    write_fixed_ascii_at(&x3f, 40U, 32U, "Auto");
    write_fixed_ascii_at(&x3f, 72U, 32U, "Standard");

    x3f.resize(264U, std::byte { 0 });
    x3f[104U] = std::byte { 1U };
    write_u32le_at(&x3f, 136U, f32_bits(0.5F));

    const std::array<X3fPropPair, 6> props
        = { { { "CAMMANUF", "SIGMA" },
              { "CAMMODEL", "SIGMA DP2" },
              { "ISO", "100" },
              { "IMAGERTEMP", "20" },
              { "WB_DESC", "Auto" },
              { "TIME", "2026:05:09 10:20:30" } } };
    const std::vector<std::byte> prop = make_x3f_prop_section(props);
    const uint32_t prop_off           = static_cast<uint32_t>(x3f.size());
    x3f.insert(x3f.end(), prop.begin(), prop.end());

    const uint32_t dir_off = static_cast<uint32_t>(x3f.size());
    append_bytes(&x3f, "SECd");
    append_u32le(&x3f, 1U);
    append_u32le(&x3f, 1U);
    append_u32le(&x3f, prop_off);
    append_u32le(&x3f, static_cast<uint32_t>(prop.size()));
    append_bytes(&x3f, "PROP");
    append_u32le(&x3f, dir_off);

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload_scratch {};
    std::array<uint32_t, 16> payload_parts {};
    ExifDecodeOptions exif_options;
    PayloadOptions payload_options;
    const SimpleMetaResult res
        = simple_meta_read(x3f, store, blocks, ifds, payload_scratch,
                           payload_parts, exif_options, payload_options);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);
    EXPECT_GT(res.exif.entries_decoded, 0U);

    store.finalize();

    const std::span<const EntryId> width_ids = store.find_all(
        exif_key("x3f_header", 0x0007));
    ASSERT_EQ(width_ids.size(), 1U);
    EXPECT_EQ(store.entry(width_ids[0]).value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(store.entry(width_ids[0]).value.elem_type, MetaElementType::U32);
    EXPECT_EQ(store.entry(width_ids[0]).value.data.u64, 2640U);

    const std::span<const EntryId> wb_ids = store.find_all(
        exif_key("x3f_header", 0x000a));
    ASSERT_EQ(wb_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(wb_ids[0]).value),
              "Auto");

    const std::span<const EntryId> scene_ids = store.find_all(
        exif_key("x3f_header", 0x0012));
    ASSERT_EQ(scene_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(scene_ids[0]).value),
              "Standard");

    const std::span<const EntryId> exposure_ids = store.find_all(
        exif_key("x3f_header_ext", 0x0001));
    ASSERT_EQ(exposure_ids.size(), 1U);
    EXPECT_EQ(store.entry(exposure_ids[0]).value.elem_type,
              MetaElementType::F32);
    EXPECT_EQ(store.entry(exposure_ids[0]).value.data.f32_bits, f32_bits(0.5F));

    const std::span<const EntryId> make_ids = store.find_all(
        exif_key("x3f_prop", 0x0007));
    ASSERT_EQ(make_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(make_ids[0]).value),
              "SIGMA");

    const std::span<const EntryId> model_ids = store.find_all(
        exif_key("x3f_prop", 0x0008));
    ASSERT_EQ(model_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(model_ids[0]).value),
              "SIGMA DP2");

    const VendorRawProcessingSummary summary
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Sigma);
    EXPECT_GE(summary.fields_seen, 1U);
    EXPECT_GE(summary.color_fields, 1U);
    EXPECT_GE(summary.white_balance_fields, 1U);
    EXPECT_GE(summary.geometry_fields, 1U);
    EXPECT_GE(summary.sensor_fields, 1U);
    EXPECT_GE(summary.private_table_fields, 1U);
}


TEST(SimpleMetaRead, PromotesPngTextChunksIntoStructuredEntries)
{
    std::vector<std::byte> png = {
        std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
        std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
        std::byte { 0x1A }, std::byte { 0x0A },
    };

    std::vector<std::byte> text_chunk;
    append_bytes(&text_chunk, "Author");
    text_chunk.push_back(std::byte { 0x00 });
    append_bytes(&text_chunk, "Alice");
    append_png_chunk(&png, fourcc('t', 'E', 'X', 't'), text_chunk);

    std::vector<std::byte> itxt;
    append_bytes(&itxt, "Description");
    itxt.push_back(std::byte { 0x00 });
    itxt.push_back(std::byte { 0x00 });
    itxt.push_back(std::byte { 0x00 });
    append_bytes(&itxt, "en");
    itxt.push_back(std::byte { 0x00 });
    append_bytes(&itxt, "Beschreibung");
    itxt.push_back(std::byte { 0x00 });
    append_bytes(&itxt, "OpenMeta PNG");
    append_png_chunk(&png, fourcc('i', 'T', 'X', 't'), itxt);

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    const std::string_view comment_text = "Shot A";
    uLongf comp_cap = compressBound(static_cast<uLong>(comment_text.size()));
    std::vector<std::byte> comp(static_cast<size_t>(comp_cap));
    const int zres
        = compress2(reinterpret_cast<Bytef*>(comp.data()), &comp_cap,
                    reinterpret_cast<const Bytef*>(comment_text.data()),
                    static_cast<uLong>(comment_text.size()),
                    Z_BEST_COMPRESSION);
    ASSERT_EQ(zres, Z_OK);
    comp.resize(static_cast<size_t>(comp_cap));

    std::vector<std::byte> ztxt;
    append_bytes(&ztxt, "Comment");
    ztxt.push_back(std::byte { 0x00 });
    ztxt.push_back(std::byte { 0x00 });
    ztxt.insert(ztxt.end(), comp.begin(), comp.end());
    append_png_chunk(&png, fourcc('z', 'T', 'X', 't'), ztxt);
#endif

    append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'), {});

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload_scratch {};
    std::array<uint32_t, 16> payload_parts {};
    SimpleMetaDecodeOptions options;
    options.payload.decompress = true;
    const SimpleMetaResult res = simple_meta_read(png, store, blocks, ifds,
                                                  payload_scratch,
                                                  payload_parts, options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);

    store.finalize();

    const std::span<const EntryId> author_ids = store.find_all(
        png_text_key("Author", "text"));
    ASSERT_EQ(author_ids.size(), 1U);
    EXPECT_EQ(store.entry(author_ids[0]).value.kind, MetaValueKind::Text);
    EXPECT_EQ(store.entry(author_ids[0]).value.text_encoding,
              TextEncoding::Unknown);
    EXPECT_EQ(arena_string(store.arena(), store.entry(author_ids[0]).value),
              "Alice");

    const std::span<const EntryId> description_text_ids = store.find_all(
        png_text_key("Description", "text"));
    ASSERT_EQ(description_text_ids.size(), 1U);
    EXPECT_EQ(store.entry(description_text_ids[0]).value.text_encoding,
              TextEncoding::Utf8);
    EXPECT_EQ(arena_string(store.arena(),
                           store.entry(description_text_ids[0]).value),
              "OpenMeta PNG");

    const std::span<const EntryId> description_lang_ids = store.find_all(
        png_text_key("Description", "language"));
    ASSERT_EQ(description_lang_ids.size(), 1U);
    EXPECT_EQ(store.entry(description_lang_ids[0]).value.text_encoding,
              TextEncoding::Ascii);
    EXPECT_EQ(arena_string(store.arena(),
                           store.entry(description_lang_ids[0]).value),
              "en");

    const std::span<const EntryId> description_translated_ids = store.find_all(
        png_text_key("Description", "translated_keyword"));
    ASSERT_EQ(description_translated_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(),
                           store.entry(description_translated_ids[0]).value),
              "Beschreibung");

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    const std::span<const EntryId> comment_ids = store.find_all(
        png_text_key("Comment", "text"));
    ASSERT_EQ(comment_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(comment_ids[0]).value),
              "Shot A");
#endif
}


TEST(SimpleMetaRead, DecodesJpegCommentBlock)
{
    std::vector<std::byte> jpeg;
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD8 });

    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xFE });
    append_u16be(&jpeg, 23U);
    append_bytes(&jpeg, "OpenMeta JPEG comment");

    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD9 });

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 256> payload_scratch {};
    std::array<uint32_t, 8> payload_parts {};
    const SimpleMetaDecodeOptions options {};
    const SimpleMetaResult res = simple_meta_read(jpeg, store, blocks, ifds,
                                                  payload_scratch,
                                                  payload_parts, options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(comment_key());
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Text);
    EXPECT_EQ(store.entry(ids[0]).value.text_encoding, TextEncoding::Ascii);
    EXPECT_EQ(arena_string(store.arena(), store.entry(ids[0]).value),
              "OpenMeta JPEG comment");
}


TEST(SimpleMetaRead, DecodesGifCommentExtension)
{
    std::vector<std::byte> gif;
    append_bytes(&gif, "GIF89a");
    gif.push_back(std::byte { 0x01 });
    gif.push_back(std::byte { 0x00 });
    gif.push_back(std::byte { 0x01 });
    gif.push_back(std::byte { 0x00 });
    gif.push_back(std::byte { 0x00 });
    gif.push_back(std::byte { 0x00 });
    gif.push_back(std::byte { 0x00 });

    gif.push_back(std::byte { 0x21 });
    gif.push_back(std::byte { 0xFE });
    gif.push_back(std::byte { 0x08 });
    append_bytes(&gif, "gif text");
    gif.push_back(std::byte { 0x00 });
    gif.push_back(std::byte { 0x3B });

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 256> payload_scratch {};
    std::array<uint32_t, 8> payload_parts {};
    const SimpleMetaDecodeOptions options {};
    const SimpleMetaResult res = simple_meta_read(gif, store, blocks, ifds,
                                                  payload_scratch,
                                                  payload_parts, options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(comment_key());
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Text);
    EXPECT_EQ(store.entry(ids[0]).value.text_encoding, TextEncoding::Ascii);
    EXPECT_EQ(arena_string(store.arena(), store.entry(ids[0]).value),
              "gif text");
}

}  // namespace openmeta
