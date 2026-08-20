// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exr_decode.h"

#include "openmeta/meta_key.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/simple_meta.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace openmeta {
namespace {

    struct ExrCallbackState final {
        std::span<const std::byte> bytes;
        RandomAccessIoCode code  = RandomAccessIoCode::Ok;
        uint64_t max_bytes       = UINT64_MAX;
        uint64_t forbidden_begin = UINT64_MAX;
        uint64_t forbidden_end   = UINT64_MAX;
        bool touched_forbidden   = false;
        uint32_t calls           = 0U;
    };


    static RandomAccessIoResult
    exr_read_at(void* context, uint64_t offset,
                std::span<std::byte> destination) noexcept
    {
        ExrCallbackState* state = static_cast<ExrCallbackState*>(context);
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

    static void append_u32le(std::vector<std::byte>* out, uint32_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { static_cast<uint8_t>(v & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFU) });
    }


    static void append_cstr(std::vector<std::byte>* out, std::string_view s)
    {
        ASSERT_NE(out, nullptr);
        for (size_t i = 0; i < s.size(); ++i) {
            out->push_back(std::byte { static_cast<uint8_t>(s[i]) });
        }
        out->push_back(std::byte { 0 });
    }


    static void append_attr_raw(std::vector<std::byte>* out,
                                std::string_view name, std::string_view type,
                                std::span<const std::byte> value)
    {
        append_cstr(out, name);
        append_cstr(out, type);
        append_u32le(out, static_cast<uint32_t>(value.size()));
        for (size_t i = 0; i < value.size(); ++i) {
            out->push_back(value[i]);
        }
    }


    static void append_attr_text(std::vector<std::byte>* out,
                                 std::string_view name, std::string_view type,
                                 std::string_view value)
    {
        std::vector<std::byte> payload;
        payload.reserve(value.size());
        for (size_t i = 0; i < value.size(); ++i) {
            payload.push_back(std::byte { static_cast<uint8_t>(value[i]) });
        }
        append_attr_raw(out, name, type,
                        std::span<const std::byte>(payload.data(),
                                                   payload.size()));
    }


    static std::vector<std::byte> build_exr_single_part()
    {
        std::vector<std::byte> exr;
        append_u32le(&exr, 20000630U);
        append_u32le(&exr, 2U);

        append_attr_text(&exr, "owner", "string", "Vlad");

        const uint32_t one_f32_bits = std::bit_cast<uint32_t>(1.0f);
        std::array<std::byte, 4> f32_payload {
            std::byte { static_cast<uint8_t>(one_f32_bits & 0xFFU) },
            std::byte { static_cast<uint8_t>((one_f32_bits >> 8) & 0xFFU) },
            std::byte { static_cast<uint8_t>((one_f32_bits >> 16) & 0xFFU) },
            std::byte { static_cast<uint8_t>((one_f32_bits >> 24) & 0xFFU) },
        };
        append_attr_raw(&exr, "pixelAspectRatio", "float",
                        std::span<const std::byte>(f32_payload.data(),
                                                   f32_payload.size()));

        exr.push_back(std::byte { 0 });
        return exr;
    }


    static std::vector<std::byte> build_exr_multipart_two_names()
    {
        std::vector<std::byte> exr;
        append_u32le(&exr, 20000630U);
        append_u32le(&exr, 2U | 0x00001000U);

        append_attr_text(&exr, "name", "string", "left");
        exr.push_back(std::byte { 0 });
        append_attr_text(&exr, "name", "string", "right");
        exr.push_back(std::byte { 0 });
        exr.push_back(std::byte { 0 });
        return exr;
    }

    static std::vector<std::byte> build_exr_single_part_unknown_type()
    {
        std::vector<std::byte> exr;
        append_u32le(&exr, 20000630U);
        append_u32le(&exr, 2U);

        const std::array<std::byte, 5> payload {
            std::byte { 1 }, std::byte { 2 }, std::byte { 3 }, std::byte { 4 },
            std::byte { 5 }
        };
        append_attr_raw(&exr, "customA", "myVendorFoo",
                        std::span<const std::byte>(payload.data(),
                                                   payload.size()));
        exr.push_back(std::byte { 0 });
        return exr;
    }


    static std::vector<std::byte> build_exr_single_part_tiledesc()
    {
        std::vector<std::byte> exr;
        append_u32le(&exr, 20000630U);
        append_u32le(&exr, 2U);

        const std::array<std::byte, 9> payload {
            std::byte { 0x40 }, std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x40 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x01 }
        };
        append_attr_raw(&exr, "tiles", "tiledesc",
                        std::span<const std::byte>(payload.data(),
                                                   payload.size()));
        exr.push_back(std::byte { 0 });
        return exr;
    }


    static std::string_view arena_string(const MetaStore& store,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = store.arena().span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

}  // namespace


TEST(ExrDecode, DecodesSinglePartHeaderAttributes)
{
    const std::vector<std::byte> exr = build_exr_single_part();

    MetaStore store;
    const ExrDecodeResult res
        = decode_exr_header(std::span<const std::byte>(exr.data(), exr.size()),
                            store);
    EXPECT_EQ(res.status, ExrDecodeStatus::Ok);
    EXPECT_EQ(res.parts_decoded, 1U);
    EXPECT_EQ(res.entries_decoded, 2U);

    store.finalize();
    EXPECT_EQ(store.block_count(), 1U);
    EXPECT_EQ(store.entries().size(), 2U);

    MetaKeyView owner_key;
    owner_key.kind                          = MetaKeyKind::ExrAttribute;
    owner_key.data.exr_attribute.part_index = 0;
    owner_key.data.exr_attribute.name       = "owner";
    const std::span<const EntryId> ids      = store.find_all(owner_key);
    ASSERT_EQ(ids.size(), 1U);

    const Entry& owner = store.entry(ids[0]);
    EXPECT_EQ(owner.origin.wire_type.family, WireFamily::Other);
    EXPECT_EQ(owner.origin.wire_type.code, 20U);
    EXPECT_EQ(owner.origin.wire_count, 4U);
    ASSERT_EQ(owner.value.kind, MetaValueKind::Text);
    EXPECT_EQ(arena_string(store, owner.value.data.span), "Vlad");
}


TEST(ExrDecode, DecodesMultipartHeaders)
{
    const std::vector<std::byte> exr = build_exr_multipart_two_names();

    MetaStore store;
    const ExrDecodeResult res
        = decode_exr_header(std::span<const std::byte>(exr.data(), exr.size()),
                            store);
    EXPECT_EQ(res.status, ExrDecodeStatus::Ok);
    EXPECT_EQ(res.parts_decoded, 2U);
    EXPECT_EQ(res.entries_decoded, 2U);

    store.finalize();
    EXPECT_EQ(store.block_count(), 2U);

    MetaKeyView p0;
    p0.kind                          = MetaKeyKind::ExrAttribute;
    p0.data.exr_attribute.part_index = 0;
    p0.data.exr_attribute.name       = "name";
    EXPECT_EQ(store.find_all(p0).size(), 1U);

    MetaKeyView p1;
    p1.kind                          = MetaKeyKind::ExrAttribute;
    p1.data.exr_attribute.part_index = 1;
    p1.data.exr_attribute.name       = "name";
    EXPECT_EQ(store.find_all(p1).size(), 1U);
}


TEST(ExrDecode, ReportsLimitExceededForMaxAttributes)
{
    const std::vector<std::byte> exr = build_exr_single_part();

    MetaStore store;
    ExrDecodeOptions options;
    options.limits.max_attributes = 1U;

    const ExrDecodeResult res
        = decode_exr_header(std::span<const std::byte>(exr.data(), exr.size()),
                            store, EntryFlags::None, options);
    EXPECT_EQ(res.status, ExrDecodeStatus::LimitExceeded);
    EXPECT_EQ(res.entries_decoded, 1U);
}

TEST(ExrDecode, EstimateMatchesDecodeCounters)
{
    const std::vector<std::byte> exr = build_exr_single_part();

    const ExrDecodeResult estimate = measure_exr_header(
        std::span<const std::byte>(exr.data(), exr.size()));
    EXPECT_EQ(estimate.status, ExrDecodeStatus::Ok);
    EXPECT_EQ(estimate.parts_decoded, 1U);
    EXPECT_EQ(estimate.entries_decoded, 2U);

    MetaStore store;
    const ExrDecodeResult decoded
        = decode_exr_header(std::span<const std::byte>(exr.data(), exr.size()),
                            store);
    EXPECT_EQ(decoded.status, estimate.status);
    EXPECT_EQ(decoded.parts_decoded, estimate.parts_decoded);
    EXPECT_EQ(decoded.entries_decoded, estimate.entries_decoded);
}


TEST(ExrDecode, RandomAccessMatchesContiguousAndStopsBeforePixelData)
{
    std::vector<std::byte> exr = build_exr_single_part();
    const uint64_t header_size = exr.size();
    exr.insert(exr.end(), 4096U, std::byte { 0xA5 });

    constexpr uint64_t prefix_size = 29U;
    std::vector<std::byte> backing(prefix_size, std::byte { 0x11 });
    backing.insert(backing.end(), exr.begin(), exr.end());
    ExrCallbackState callback { backing };
    callback.forbidden_begin = prefix_size + header_size + 32U;
    callback.forbidden_end   = backing.size();
    const RandomAccessSource source
        = make_callback_random_access_source(backing.size(), &callback,
                                             exr_read_at, true);
    const RandomAccessSourceRange range
        = make_random_access_source_range(source, prefix_size, exr.size());

    std::array<std::byte, 32> read_window {};
    std::array<std::byte, 256> value {};
    ExrRandomAccessScratch scratch;
    scratch.read_window                       = read_window;
    scratch.value                             = value;
    scratch.window_options.minimum_read_bytes = read_window.size();

    MetaStore positional_store;
    const ExrRandomAccessDecodeResult positional
        = decode_exr_header_random_access(range, positional_store, scratch);
    ASSERT_TRUE(positional.complete());
    EXPECT_EQ(positional.decode.status, ExrDecodeStatus::Ok);
    EXPECT_EQ(positional.decode.parts_decoded, 1U);
    EXPECT_EQ(positional.decode.entries_decoded, 2U);
    EXPECT_GT(callback.calls, 0U);
    EXPECT_FALSE(callback.touched_forbidden);
    EXPECT_LT(positional.input.bytes_requested,
              static_cast<uint64_t>(exr.size()));

    MetaStore contiguous_store;
    const ExrDecodeResult contiguous
        = decode_exr_header(std::span<const std::byte>(exr.data(), exr.size()),
                            contiguous_store);
    EXPECT_EQ(positional.decode.status, contiguous.status);
    EXPECT_EQ(positional.decode.parts_decoded, contiguous.parts_decoded);
    EXPECT_EQ(positional.decode.entries_decoded, contiguous.entries_decoded);

    const ExrRandomAccessDecodeResult measured
        = measure_exr_header_random_access(range, scratch);
    EXPECT_TRUE(measured.complete());
    EXPECT_EQ(measured.decode.status, ExrDecodeStatus::Ok);
    EXPECT_EQ(measured.decode.entries_decoded, 2U);
}


TEST(ExrDecode, RandomAccessMeasureSkipsLargeValuesAndDecodeReportsScratch)
{
    std::vector<std::byte> exr;
    append_u32le(&exr, 20000630U);
    append_u32le(&exr, 2U);
    std::vector<std::byte> large(4096U, std::byte { 0x4A });
    append_attr_raw(&exr, "large", "bytes", large);
    append_attr_text(&exr, "owner", "string", "Vlad");
    exr.push_back(std::byte { 0U });

    ExrCallbackState measured_callback { exr };
    measured_callback.forbidden_begin = 8U + 6U + 6U + 4U + 32U;
    measured_callback.forbidden_end = measured_callback.forbidden_begin + 4000U;
    RandomAccessSource source
        = make_callback_random_access_source(exr.size(), &measured_callback,
                                             exr_read_at, true);
    RandomAccessSourceRange range = make_random_access_source_range(source);
    std::array<std::byte, 32> read_window {};
    ExrRandomAccessScratch measure_scratch;
    measure_scratch.read_window                       = read_window;
    measure_scratch.window_options.minimum_read_bytes = read_window.size();

    const ExrRandomAccessDecodeResult measured
        = measure_exr_header_random_access(range, measure_scratch);
    EXPECT_TRUE(measured.complete());
    EXPECT_EQ(measured.decode.status, ExrDecodeStatus::Ok);
    EXPECT_EQ(measured.decode.entries_decoded, 2U);
    EXPECT_FALSE(measured_callback.touched_forbidden);

    ExrCallbackState decode_callback { exr };
    source = make_callback_random_access_source(exr.size(), &decode_callback,
                                                exr_read_at, true);
    range  = make_random_access_source_range(source);
    std::array<std::byte, 64> value {};
    ExrRandomAccessScratch decode_scratch;
    decode_scratch.read_window                       = read_window;
    decode_scratch.value                             = value;
    decode_scratch.window_options.minimum_read_bytes = read_window.size();
    MetaStore store;
    const ExrRandomAccessDecodeResult decoded
        = decode_exr_header_random_access(range, store, decode_scratch);
    EXPECT_FALSE(decoded.complete());
    EXPECT_EQ(decoded.decode.status, ExrDecodeStatus::OutputTruncated);
    EXPECT_EQ(decoded.value_scratch_needed, 4096U);
    EXPECT_EQ(decoded.decode.entries_decoded, 1U);
}


TEST(ExrDecode, RandomAccessReportsScratchAndSourceFailures)
{
    const std::vector<std::byte> exr = build_exr_single_part();
    ExrCallbackState callback { exr };
    RandomAccessSource source = make_callback_random_access_source(exr.size(),
                                                                   &callback,
                                                                   exr_read_at);
    const RandomAccessSourceRange range = make_random_access_source_range(
        source);
    std::array<std::byte, 15> small_window {};
    ExrRandomAccessScratch small;
    small.read_window = small_window;
    MetaStore store;
    const ExrRandomAccessDecodeResult too_small
        = decode_exr_header_random_access(range, store, small);
    EXPECT_EQ(too_small.input.code, RandomAccessReadCode::ScratchTooSmall);
    EXPECT_EQ(too_small.input.failure_request_bytes, 16U);

    std::array<std::byte, 32> window {};
    ExrRandomAccessScratch scratch;
    scratch.read_window = window;
    callback.code       = RandomAccessIoCode::IoError;
    const ExrRandomAccessDecodeResult io_error
        = measure_exr_header_random_access(range, scratch);
    EXPECT_EQ(io_error.input.code, RandomAccessReadCode::IoError);
}


TEST(ExrDecode, RandomAccessSnapshotAssemblyUsesNativeHeaderTraversal)
{
    std::vector<std::byte> exr = build_exr_single_part();
    const uint64_t header_size = exr.size();
    exr.insert(exr.end(), 4096U, std::byte { 0x7CU });
    ExrCallbackState callback { exr };
    callback.forbidden_begin = header_size + 32U;
    callback.forbidden_end   = exr.size();
    const RandomAccessSource source
        = make_callback_random_access_source(exr.size(), &callback, exr_read_at,
                                             true);
    const RandomAccessSourceRange range = make_random_access_source_range(
        source);
    std::array<std::byte, 32> window {};
    std::array<std::byte, 256> value {};
    ReadTransferSourceSnapshotRandomAccessScratch scratch;
    scratch.read_window                       = window;
    scratch.value                             = value;
    scratch.window_options.minimum_read_bytes = window.size();
    const ReadTransferSourceSnapshotRandomAccessResult result
        = read_transfer_source_snapshot_random_access(range,
                                                      ContainerFormat::Exr,
                                                      scratch);
    ASSERT_TRUE(result.complete());
    EXPECT_EQ(result.entry_count, 2U);
    EXPECT_EQ(result.read.exr.status, ExrDecodeStatus::Ok);
    EXPECT_FALSE(callback.touched_forbidden);
    EXPECT_TRUE(result.snapshot.store.is_finalized());
}

TEST(ExrDecode, PreservesUnknownTypeNameByDefault)
{
    const std::vector<std::byte> exr = build_exr_single_part_unknown_type();

    MetaStore store;
    const ExrDecodeResult res
        = decode_exr_header(std::span<const std::byte>(exr.data(), exr.size()),
                            store);
    ASSERT_EQ(res.status, ExrDecodeStatus::Ok);
    ASSERT_EQ(res.entries_decoded, 1U);

    store.finalize();
    ASSERT_EQ(store.entries().size(), 1U);
    const Entry& e = store.entry(0);
    EXPECT_EQ(e.origin.wire_type.family, WireFamily::Other);
    EXPECT_EQ(e.origin.wire_type.code, 31U);
    ASSERT_GT(e.origin.wire_type_name.size, 0U);
    EXPECT_EQ(arena_string(store, e.origin.wire_type_name), "myVendorFoo");
}

TEST(ExrDecode, CanDisableUnknownTypeNamePreservation)
{
    const std::vector<std::byte> exr = build_exr_single_part_unknown_type();

    MetaStore store;
    ExrDecodeOptions options;
    options.preserve_unknown_type_name = false;
    const ExrDecodeResult res
        = decode_exr_header(std::span<const std::byte>(exr.data(), exr.size()),
                            store, EntryFlags::None, options);
    ASSERT_EQ(res.status, ExrDecodeStatus::Ok);

    store.finalize();
    ASSERT_EQ(store.entries().size(), 1U);
    const Entry& e = store.entry(0);
    EXPECT_EQ(e.origin.wire_type.code, 31U);
    EXPECT_EQ(e.origin.wire_type_name.size, 0U);
}


TEST(ExrDecode, DecodesTileDescAsU32Array)
{
    const std::vector<std::byte> exr = build_exr_single_part_tiledesc();

    MetaStore store;
    const ExrDecodeResult res
        = decode_exr_header(std::span<const std::byte>(exr.data(), exr.size()),
                            store);
    ASSERT_EQ(res.status, ExrDecodeStatus::Ok);
    ASSERT_EQ(res.entries_decoded, 1U);

    store.finalize();
    MetaKeyView key;
    key.kind                           = MetaKeyKind::ExrAttribute;
    key.data.exr_attribute.part_index  = 0;
    key.data.exr_attribute.name        = "tiles";
    const std::span<const EntryId> ids = store.find_all(key);
    ASSERT_EQ(ids.size(), 1U);

    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.origin.wire_type.code, 22U);
    ASSERT_EQ(e.value.kind, MetaValueKind::Array);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.count, 3U);

    const std::span<const std::byte> bytes = store.arena().span(
        e.value.data.span);
    ASSERT_EQ(bytes.size(), 12U);
    std::array<uint32_t, 3> v {};
    std::memcpy(v.data(), bytes.data(), bytes.size());
    EXPECT_EQ(v[0], 64U);
    EXPECT_EQ(v[1], 64U);
    EXPECT_EQ(v[2], 1U);
}


TEST(SimpleMetaRead, DecodesExrHeaderFallback)
{
    const std::vector<std::byte> exr = build_exr_single_part();

    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::array<std::byte, 2048> payload {};
    std::array<uint32_t, 64> payload_indices {};

    MetaStore store;
    const SimpleMetaResult read = simple_meta_read(
        std::span<const std::byte>(exr.data(), exr.size()), store,
        std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
        std::span<ExifIfdRef>(ifds.data(), ifds.size()),
        std::span<std::byte>(payload.data(), payload.size()),
        std::span<uint32_t>(payload_indices.data(), payload_indices.size()),
        ExifDecodeOptions {}, PayloadOptions {});

    EXPECT_EQ(read.exr.status, ExrDecodeStatus::Ok);
    EXPECT_EQ(read.exr.parts_decoded, 1U);
    EXPECT_EQ(read.exr.entries_decoded, 2U);
    EXPECT_EQ(read.exif.status, ExifDecodeStatus::Unsupported);
    EXPECT_EQ(read.xmp.status, XmpDecodeStatus::Unsupported);

    store.finalize();
    EXPECT_EQ(store.entries().size(), 2U);
}

}  // namespace openmeta
