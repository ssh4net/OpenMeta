// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace openmeta {
namespace {

    static void put16(std::vector<std::byte>& bytes, size_t off, uint16_t v,
                      bool le)
    {
        bytes[off]      = std::byte(le ? v & 255U : v >> 8U);
        bytes[off + 1U] = std::byte(le ? v >> 8U : v & 255U);
    }

    static void put32(std::vector<std::byte>& bytes, size_t off, uint32_t v,
                      bool le)
    {
        for (unsigned i = 0U; i < 4U; ++i) {
            bytes[off + i] = std::byte((v >> ((le ? i : 3U - i) * 8U)) & 255U);
        }
    }

    static void put_entry(std::vector<std::byte>& bytes, size_t off,
                          uint16_t tag, uint16_t type, uint32_t count,
                          uint32_t value, bool le)
    {
        put16(bytes, off, tag, le);
        put16(bytes, off + 2U, type, le);
        put32(bytes, off + 4U, count, le);
        put32(bytes, off + 8U, value, le);
    }

    // Independent synthetic TIFF: no camera files or reference decoder output.
    // The short note has values before, within and after its declared range.
    // The long note also contains all of the old, incorrect candidate addresses.
    static std::vector<std::byte> make_aoc(bool parent_le, bool note_le,
                                           uint32_t note_size)
    {
        std::vector<std::byte> bytes(1024U);
        bytes[0] = bytes[1] = std::byte(parent_le ? 'I' : 'M');
        put16(bytes, 2U, 42U, parent_le);
        put32(bytes, 4U, 8U, parent_le);
        put16(bytes, 8U, 2U, parent_le);
        put_entry(bytes, 10U, 0x010fU, 2U, 7U, 60U, parent_le);
        put_entry(bytes, 22U, 0x8769U, 4U, 1U, 80U, parent_le);
        std::memcpy(bytes.data() + 60U, "PENTAX", 7U);
        put16(bytes, 80U, 1U, parent_le);
        put_entry(bytes, 82U, 0x927cU, 7U, note_size, 128U, parent_le);
        std::memcpy(bytes.data() + 128U, "AOC\0", 4U);
        bytes[132U] = bytes[133U] = std::byte(note_le ? 'I' : 'M');
        put16(bytes, 134U, 4U, note_le);
        put_entry(bytes, 136U, 0x0001U, 3U, 1U, 0U, note_le);
        put16(bytes, 144U, 2U, note_le);
        put_entry(bytes, 148U, 0x0200U, 3U, 4U, 104U, note_le);
        put_entry(bytes, 160U, 0x0201U, 3U, 4U, 224U, note_le);
        put_entry(bytes, 172U, 0x0220U, 7U, 53U, 400U, note_le);
        constexpr uint16_t wb[4] { 13568U, 8192U, 8192U, 9184U };
        for (size_t i = 0U; i < 4U; ++i) {
            put16(bytes, 104U + i * 2U, 0U, note_le);
            put16(bytes, 224U + i * 2U, wb[i], note_le);
            put16(bytes, 232U + i * 2U, uint16_t(100U + i), note_le);
            put16(bytes, 352U + i * 2U, uint16_t(200U + i), note_le);
        }
        for (size_t i = 0U; i < 53U; ++i) {
            bytes[400U + i] = std::byte(i + 1U);
            bytes[528U + i] = std::byte(150U + i);
        }
        return bytes;
    }

    static const Entry* entry(const MetaStore& store, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = "mk_pentax0";
        key.data.exif_tag.tag = tag;
        const auto ids        = store.find_all(key);
        return ids.size() == 1U ? &store.entry(ids[0]) : nullptr;
    }

    static void check_values(const MetaStore& store)
    {
        const Entry* scalar = entry(store, 0x0001U);
        ASSERT_NE(scalar, nullptr);
        EXPECT_EQ(scalar->value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(scalar->value.elem_type, MetaElementType::U16);
        EXPECT_EQ(scalar->value.data.u64, 2U);
        EXPECT_EQ(scalar->origin.wire_type.code, 3U);
        EXPECT_EQ(scalar->origin.wire_count, 1U);
        constexpr uint16_t expected[2][4] { { 0U, 0U, 0U, 0U },
                                            { 13568U, 8192U, 8192U, 9184U } };
        for (size_t a = 0U; a < 2U; ++a) {
            const Entry* e = entry(store, uint16_t(0x0200U + a));
            ASSERT_NE(e, nullptr);
            EXPECT_EQ(e->origin.wire_type.family, WireFamily::Tiff);
            EXPECT_EQ(e->origin.wire_type.code, 3U);
            EXPECT_EQ(e->origin.wire_count, 4U);
            EXPECT_EQ(e->value.kind, MetaValueKind::Array);
            EXPECT_EQ(e->value.elem_type, MetaElementType::U16);
            EXPECT_EQ(e->value.count, 4U);
            const auto raw = store.arena().span(e->value.data.span);
            ASSERT_EQ(raw.size(), sizeof(expected[a]));
            std::array<uint16_t, 4> words {};
            std::memcpy(words.data(), raw.data(), raw.size());
            for (size_t i = 0U; i < 4U; ++i) {
                EXPECT_EQ(words[i], expected[a][i]);
            }
            EXPECT_FALSE(
                any(e->flags, EntryFlags::Unreadable | EntryFlags::Truncated));
        }
        const Entry* bytes = entry(store, 0x0220U);
        ASSERT_NE(bytes, nullptr);
        EXPECT_EQ(bytes->origin.wire_type.code, 7U);
        EXPECT_EQ(bytes->origin.wire_count, 53U);
        EXPECT_EQ(bytes->value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(bytes->value.elem_type, MetaElementType::U8);
        EXPECT_EQ(bytes->value.count, 53U);
        const auto raw = store.arena().span(bytes->value.data.span);
        ASSERT_EQ(raw.size(), 53U);
        for (size_t i = 0U; i < raw.size(); ++i) {
            EXPECT_EQ(raw[i], std::byte(i + 1U));
        }
    }

    struct ReadState final {
        std::span<const std::byte> bytes;
        uint64_t fail_at = UINT64_MAX;
        bool short_read  = false;
        uint32_t calls   = 0U;
    };

    static RandomAccessIoResult read_at(void* opaque, uint64_t off,
                                        std::span<std::byte> dst) noexcept
    {
        ReadState& state = *static_cast<ReadState*>(opaque);
        ++state.calls;
        if (off > state.bytes.size() || dst.size() > state.bytes.size() - off) {
            return { RandomAccessIoCode::IoError, 0U };
        }
        if (off <= state.fail_at && state.fail_at - off < dst.size()) {
            return { state.short_read ? RandomAccessIoCode::Ok
                                      : RandomAccessIoCode::IoError,
                     0U };
        }
        std::memcpy(dst.data(), state.bytes.data() + off, dst.size());
        return { RandomAccessIoCode::Ok, dst.size() };
    }

    static ExifRandomAccessDecodeResult
    callback_decode(ReadState& state, size_t origin, MetaStore& store,
                    const ExifDecodeOptions& options,
                    const RandomAccessReadLimits& limits = {},
                    size_t value_capacity                = 160U)
    {
        const auto source
            = make_callback_random_access_source(state.bytes.size(), &state,
                                                 read_at);
        std::array<std::byte, 24> window {};
        std::array<std::byte, 640> value {};
        ExifRandomAccessScratch scratch;
        scratch.read_window = window;
        scratch.value       = std::span<std::byte>(value).first(value_capacity);
        scratch.window_options.minimum_read_bytes = 0U;
        return decode_exif_tiff_random_access(
            make_random_access_source_range(source, origin,
                                            state.bytes.size() - origin),
            store, {}, scratch, options, limits);
    }

    TEST(AocOffsetRegression,
         ContiguousUsesContainingTiffEvenWhenWrongAddressesFit)
    {
        for (bool parent_le : { false, true }) {
            for (bool note_le : { false, true }) {
                SCOPED_TRACE(::testing::Message()
                             << parent_le << '/' << note_le);
                const auto tiff = make_aoc(parent_le, note_le, 640U);
                MetaStore store;
                ExifDecodeOptions options;
                options.decode_makernote = true;
                const auto result = decode_exif_tiff(tiff, store, {}, options);
                ASSERT_EQ(result.status, ExifDecodeStatus::Ok);
                store.finalize();
                check_values(store);
            }
        }
    }

    TEST(AocOffsetRegression, CallbackUsesTiffOriginAndReusesSmallScratch)
    {
        for (bool parent_le : { false, true }) {
            for (bool note_le : { false, true }) {
                SCOPED_TRACE(::testing::Message()
                             << parent_le << '/' << note_le);
                const auto tiff = make_aoc(parent_le, note_le, 160U);
                std::vector<std::byte> host(37U, std::byte { 0x55 });
                host.insert(host.end(), tiff.begin(), tiff.end());
                ReadState state { host };
                MetaStore store;
                ExifDecodeOptions options;
                options.decode_makernote = true;
                const auto result = callback_decode(state, 37U, store, options);
                ASSERT_TRUE(result.complete());
                ASSERT_EQ(result.decode.status, ExifDecodeStatus::Ok);
                EXPECT_GT(state.calls, 4U);
                store.finalize();
                check_values(store);
            }
        }
    }

    TEST(AocOffsetRegression, ContiguousCanReadOutsideDeclaredNote)
    {
        for (bool le : { false, true }) {
            const auto tiff = make_aoc(le, !le, 160U);
            MetaStore store;
            ExifDecodeOptions options;
            options.decode_makernote = true;
            const auto result = decode_exif_tiff(tiff, store, {}, options);
            ASSERT_EQ(result.status, ExifDecodeStatus::Ok);
            store.finalize();
            check_values(store);
        }
    }

    TEST(AocOffsetRegression, CallbackPropagatesExternalValueReadFailures)
    {
        const auto tiff = make_aoc(true, false, 160U);
        for (bool short_read : { false, true }) {
            ReadState state { tiff, 400U, short_read };
            MetaStore store;
            ExifDecodeOptions options;
            options.decode_makernote = true;
            const auto result = callback_decode(state, 0U, store, options);
            EXPECT_FALSE(result.input.ok());
            EXPECT_FALSE(result.complete());
            EXPECT_EQ(result.decode.status, ExifDecodeStatus::OutputTruncated);
            EXPECT_EQ(result.input.code, short_read
                                             ? RandomAccessReadCode::ShortRead
                                             : RandomAccessReadCode::IoError);
            EXPECT_EQ(result.input.io_code, short_read
                                                ? RandomAccessIoCode::Ok
                                                : RandomAccessIoCode::IoError);
            EXPECT_EQ(result.input.failure_offset, 400U);
            EXPECT_EQ(result.input.failure_request_bytes, 53U);
            EXPECT_EQ(result.input.failure_bytes_read, 0U);
            store.finalize();
            const Entry* bad = entry(store, 0x0220U);
            if (bad) {
                EXPECT_TRUE(any(bad->flags, EntryFlags::Unreadable));
            }
        }
    }

    TEST(AocOffsetRegression, CallbackHonorsReadAndEntryBudgets)
    {
        const auto tiff = make_aoc(true, true, 160U);
        ExifDecodeOptions options;
        options.decode_makernote = true;
        {
            ReadState state { tiff };
            MetaStore store;
            RandomAccessReadLimits limits;
            limits.max_requests = 1U;
            const auto result   = callback_decode(state, 0U, store, options,
                                                  limits);
            EXPECT_EQ(result.input.code,
                      RandomAccessReadCode::RequestLimitExceeded);
            EXPECT_EQ(result.decode.status, ExifDecodeStatus::LimitExceeded);
            EXPECT_EQ(state.calls, 1U);
        }
        {
            ReadState state { tiff };
            MetaStore store;
            options.limits.max_total_entries = 5U;
            const auto result = callback_decode(state, 0U, store, options);
            EXPECT_EQ(result.decode.status, ExifDecodeStatus::LimitExceeded);
            EXPECT_LE(result.decode.entries_decoded, 5U);
        }
    }

    TEST(AocOffsetRegression, PentaxHeaderKeepsMakerNoteRelativeOffsets)
    {
        for (bool le : { false, true }) {
            auto tiff = make_aoc(!le, le, 640U);
            std::fill(tiff.begin() + 128U, tiff.begin() + 200U,
                      std::byte { 0 });
            std::memcpy(tiff.data() + 128U, "PENTAX \0", 8U);
            tiff[136U] = tiff[137U] = std::byte(le ? 'I' : 'M');
            put16(tiff, 138U, 4U, le);
            put_entry(tiff, 140U, 0x0001U, 3U, 1U, 0U, le);
            put16(tiff, 148U, 2U, le);
            put_entry(tiff, 152U, 0x0200U, 3U, 4U, 64U, le);
            put_entry(tiff, 164U, 0x0201U, 3U, 4U, 96U, le);
            put_entry(tiff, 176U, 0x0220U, 7U, 53U, 272U, le);
            ExifDecodeOptions options;
            options.decode_makernote = true;
            MetaStore memory;
            const auto result = decode_exif_tiff(tiff, memory, {}, options);
            ASSERT_EQ(result.status, ExifDecodeStatus::Ok);
            memory.finalize();
            check_values(memory);
            ReadState state { tiff };
            MetaStore callback;
            const auto bounded = callback_decode(state, 0U, callback, options,
                                                 {}, 640U);
            ASSERT_TRUE(bounded.complete());
            ASSERT_EQ(bounded.decode.status, ExifDecodeStatus::Ok);
            callback.finalize();
            check_values(callback);
        }
    }

    TEST(AocOffsetRegression,
         ExternalValueReportsRequiredScratchWithoutAllocation)
    {
        auto tiff = make_aoc(true, true, 160U);
        put32(tiff, 176U, 180U,
              true);  // Native UNDEFINED value, larger than scratch.
        ReadState state { tiff };
        MetaStore store;
        ExifDecodeOptions options;
        options.decode_makernote = true;
        const auto result        = callback_decode(state, 0U, store, options);
        EXPECT_TRUE(result.input.ok());
        EXPECT_FALSE(result.complete());
        EXPECT_EQ(result.decode.status, ExifDecodeStatus::OutputTruncated);
        EXPECT_EQ(result.value_scratch_needed, 180U);
        store.finalize();
        const Entry* e = entry(store, 0x0220U);
        ASSERT_NE(e, nullptr);
        EXPECT_EQ(e->origin.wire_count, 180U);
        EXPECT_TRUE(any(e->flags, EntryFlags::Unreadable));
        EXPECT_EQ(e->value.kind, MetaValueKind::Empty);
    }

    TEST(AocOffsetRegression, ModelFallbackSurvivesCallbackScratchReuse)
    {
        auto tiff = make_aoc(true, true, 160U);
        put16(tiff, 8U, 3U, true);
        put_entry(tiff, 34U, 0x0110U, 2U, 7U, 68U, true);
        std::memcpy(tiff.data() + 68U, "NB1000", 7U);
        tiff[228U] = std::byte { 0x5a };  // note-local model byte at 0x64.
        put32(tiff, 176U, 120U, true);
        std::fill(tiff.begin() + 400U, tiff.begin() + 520U, std::byte { 0xa5 });
        ExifDecodeOptions options;
        options.decode_makernote = true;
        MetaStore memory;
        ASSERT_EQ(decode_exif_tiff(tiff, memory, {}, options).status,
                  ExifDecodeStatus::Ok);
        memory.finalize();
        ReadState state { tiff };
        MetaStore bounded;
        const auto result = callback_decode(state, 0U, bounded, options);
        ASSERT_TRUE(result.complete());
        ASSERT_EQ(result.decode.status, ExifDecodeStatus::Ok);
        bounded.finalize();
        const Entry* direct   = entry(memory, 0x0064U);
        const Entry* callback = entry(bounded, 0x0064U);
        ASSERT_NE(direct, nullptr);
        ASSERT_NE(callback, nullptr);
        EXPECT_EQ(direct->value.data.u64, 0x5aU);
        EXPECT_EQ(callback->value.data.u64, 0x5aU);
        EXPECT_EQ(callback->value.elem_type, MetaElementType::U8);
    }

    TEST(AocOffsetRegression, JpegExifUsesEmbeddedTiffOrigin)
    {
        const auto tiff = make_aoc(true, false, 640U);
        std::vector<std::byte> jpeg(12U);
        jpeg[0] = jpeg[2] = std::byte { 0xff };
        jpeg[1]           = std::byte { 0xd8 };
        jpeg[3]           = std::byte { 0xe1 };
        put16(jpeg, 4U, static_cast<uint16_t>(tiff.size() + 8U), false);
        std::memcpy(jpeg.data() + 6U, "Exif\0\0", 6U);
        jpeg.insert(jpeg.end(), tiff.begin(), tiff.end());
        jpeg.push_back(std::byte { 0xff });
        jpeg.push_back(std::byte { 0xd9 });
        MetaStore store;
        std::array<ContainerBlockRef, 8> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 1024> payload {};
        std::array<uint32_t, 16> indices {};
        SimpleMetaDecodeOptions options;
        options.exif.decode_makernote = true;
        const auto result = simple_meta_read(jpeg, store, blocks, ifds, payload,
                                             indices, options);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        ASSERT_EQ(result.exif.status, ExifDecodeStatus::Ok);
        store.finalize();
        check_values(store);
    }

    TEST(AocOffsetRegression, InvalidExternalOffsetRetainsWireAndDiagnostics)
    {
        auto tiff = make_aoc(true, true, 160U);
        put32(tiff, 180U, UINT32_MAX, true);
        ExifDecodeOptions options;
        options.decode_makernote = true;
        MetaStore memory;
        EXPECT_EQ(decode_exif_tiff(tiff, memory, {}, options).status,
                  ExifDecodeStatus::Malformed);
        memory.finalize();
        ReadState state { tiff };
        MetaStore bounded;
        const auto result = callback_decode(state, 0U, bounded, options);
        EXPECT_EQ(result.decode.status, ExifDecodeStatus::Malformed);
        EXPECT_EQ(result.input.code, RandomAccessReadCode::OutOfRange);
        EXPECT_FALSE(result.complete());
        bounded.finalize();
        for (const MetaStore* store : { &memory, &bounded }) {
            const Entry* bad = entry(*store, 0x0220U);
            ASSERT_NE(bad, nullptr);
            EXPECT_EQ(bad->origin.wire_type.code, 7U);
            EXPECT_EQ(bad->origin.wire_count, 53U);
            EXPECT_EQ(bad->value.kind, MetaValueKind::Empty);
            EXPECT_TRUE(any(bad->flags, EntryFlags::Unreadable));
        }
    }

}  // namespace
}  // namespace openmeta
