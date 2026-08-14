// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exr_adapter.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#    if !defined(WIN32_LEAN_AND_MEAN)
#        define WIN32_LEAN_AND_MEAN
#    endif
#    if !defined(NOMINMAX)
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <unistd.h>
#endif

namespace openmeta {
namespace {

    static const ExrAdapterAttribute* find_attr(const ExrAdapterBatch& batch,
                                                uint32_t part_index,
                                                std::string_view name) noexcept
    {
        for (size_t i = 0; i < batch.attributes.size(); ++i) {
            if (batch.attributes[i].part_index == part_index
                && batch.attributes[i].name == name) {
                return &batch.attributes[i];
            }
        }
        return nullptr;
    }

    static uint32_t read_u32le(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.size() < 4U) {
            return 0U;
        }
        uint32_t v = 0U;
        v |= static_cast<uint32_t>(bytes[0]) & 0xFFU;
        v |= (static_cast<uint32_t>(bytes[1]) & 0xFFU) << 8;
        v |= (static_cast<uint32_t>(bytes[2]) & 0xFFU) << 16;
        v |= (static_cast<uint32_t>(bytes[3]) & 0xFFU) << 24;
        return v;
    }

    static std::vector<std::byte>
    make_test_jpeg_with_exif_make(std::string_view make)
    {
        std::vector<std::byte> out;
        out.reserve(64U + make.size());
        out.push_back(std::byte { 0xFF });
        out.push_back(std::byte { 0xD8 });

        std::vector<std::byte> tiff;
        tiff.reserve(32U + make.size());
        tiff.push_back(std::byte { 'I' });
        tiff.push_back(std::byte { 'I' });
        tiff.push_back(std::byte { 0x2A });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x08 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x01 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x0F });
        tiff.push_back(std::byte { 0x01 });
        tiff.push_back(std::byte { 0x02 });
        tiff.push_back(std::byte { 0x00 });
        const uint32_t count = static_cast<uint32_t>(make.size() + 1U);
        tiff.push_back(std::byte { static_cast<uint8_t>(count & 0xFFU) });
        tiff.push_back(
            std::byte { static_cast<uint8_t>((count >> 8) & 0xFFU) });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x1A });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        tiff.push_back(std::byte { 0x00 });
        for (size_t i = 0; i < make.size(); ++i) {
            tiff.push_back(std::byte { static_cast<uint8_t>(make[i]) });
        }
        tiff.push_back(std::byte { 0x00 });

        const size_t app1_size  = 6U + tiff.size();
        const uint16_t app1_len = static_cast<uint16_t>(app1_size + 2U);
        out.push_back(std::byte { 0xFF });
        out.push_back(std::byte { 0xE1 });
        out.push_back(
            std::byte { static_cast<uint8_t>((app1_len >> 8) & 0xFFU) });
        out.push_back(std::byte { static_cast<uint8_t>(app1_len & 0xFFU) });
        out.push_back(std::byte { 'E' });
        out.push_back(std::byte { 'x' });
        out.push_back(std::byte { 'i' });
        out.push_back(std::byte { 'f' });
        out.push_back(std::byte { 0x00 });
        out.push_back(std::byte { 0x00 });
        out.insert(out.end(), tiff.begin(), tiff.end());
        out.push_back(std::byte { 0xFF });
        out.push_back(std::byte { 0xD9 });
        return out;
    }

    static std::string write_temp_bytes(std::span<const std::byte> bytes)
    {
#if defined(_WIN32)
        std::array<char, MAX_PATH + 1U> temp_dir {};
        const DWORD temp_dir_length
            = GetTempPathA(static_cast<DWORD>(temp_dir.size()),
                           temp_dir.data());
        EXPECT_GT(temp_dir_length, 0U);
        EXPECT_LT(temp_dir_length, temp_dir.size());
        if (temp_dir_length == 0U || temp_dir_length >= temp_dir.size()) {
            return std::string();
        }

        std::array<char, MAX_PATH + 1U> path {};
        const UINT created = GetTempFileNameA(temp_dir.data(), "oma", 0U,
                                              path.data());
        EXPECT_NE(created, 0U);
        if (created == 0U) {
            return std::string();
        }

        FILE* f                  = nullptr;
        const errno_t open_error = ::fopen_s(&f, path.data(), "wb");
        EXPECT_EQ(open_error, 0);
        EXPECT_NE(f, nullptr);
        if (open_error != 0 || !f) {
            (void)DeleteFileA(path.data());
            return std::string();
        }
#else
        char path_template[] = "/tmp/openmeta_exr_adapter_XXXXXX";
        const int fd         = ::mkstemp(path_template);
        EXPECT_NE(fd, -1);
        if (fd == -1) {
            return std::string();
        }
        FILE* f = ::fdopen(fd, "wb");
        EXPECT_NE(f, nullptr);
        if (!f) {
            ::close(fd);
            return std::string();
        }
#endif
        const size_t written = std::fwrite(bytes.data(), 1U, bytes.size(), f);
        EXPECT_EQ(written, bytes.size());
        std::fclose(f);
#if defined(_WIN32)
        return std::string(path.data());
#else
        return std::string(path_template);
#endif
    }

    struct ReplayState final {
        std::vector<std::string> events;
        uint32_t fail_part = std::numeric_limits<uint32_t>::max();
    };

    static ExrAdapterStatus replay_begin_part(void* user, uint32_t part_index,
                                              uint32_t attribute_count) noexcept
    {
        if (!user) {
            return ExrAdapterStatus::InvalidArgument;
        }
        ReplayState* state = static_cast<ReplayState*>(user);
        state->events.push_back("begin:" + std::to_string(part_index) + ":"
                                + std::to_string(attribute_count));
        return ExrAdapterStatus::Ok;
    }

    static ExrAdapterStatus
    replay_emit_attribute(void* user, uint32_t part_index,
                          const ExrAdapterAttribute* attribute) noexcept
    {
        if (!user || !attribute) {
            return ExrAdapterStatus::InvalidArgument;
        }
        ReplayState* state = static_cast<ReplayState*>(user);
        if (state->fail_part == part_index) {
            return ExrAdapterStatus::Unsupported;
        }
        state->events.push_back("attr:" + std::to_string(part_index) + ":"
                                + attribute->name);
        return ExrAdapterStatus::Ok;
    }

    static ExrAdapterStatus replay_end_part(void* user,
                                            uint32_t part_index) noexcept
    {
        if (!user) {
            return ExrAdapterStatus::InvalidArgument;
        }
        ReplayState* state = static_cast<ReplayState*>(user);
        state->events.push_back("end:" + std::to_string(part_index));
        return ExrAdapterStatus::Ok;
    }

}  // namespace


TEST(ExrAdapter, BuildsBatchForKnownAndOpaqueAttributes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry owner;
    owner.key          = make_exr_attribute_key(store.arena(), 0U, "owner");
    owner.value        = make_text(store.arena(), "Vlad", TextEncoding::Utf8);
    owner.origin.block = block;
    owner.origin.order_in_block   = 0U;
    owner.origin.wire_type.family = WireFamily::Other;
    owner.origin.wire_type.code   = 20U;
    (void)store.add_entry(owner);

    Entry aspect;
    aspect.key = make_exr_attribute_key(store.arena(), 0U, "pixelAspectRatio");
    aspect.value                 = make_f32_bits(std::bit_cast<uint32_t>(1.0f));
    aspect.origin.block          = block;
    aspect.origin.order_in_block = 1U;
    aspect.origin.wire_type.family = WireFamily::Other;
    aspect.origin.wire_type.code   = 9U;
    (void)store.add_entry(aspect);

    const std::array<std::byte, 5> custom_bytes {
        std::byte { 1 }, std::byte { 2 }, std::byte { 3 }, std::byte { 4 },
        std::byte { 5 }
    };
    Entry custom;
    custom.key          = make_exr_attribute_key(store.arena(), 1U, "customA");
    custom.value        = make_bytes(store.arena(),
                                     std::span<const std::byte>(custom_bytes.data(),
                                                                custom_bytes.size()));
    custom.origin.block = block;
    custom.origin.order_in_block   = 2U;
    custom.origin.wire_type.family = WireFamily::Other;
    custom.origin.wire_type.code   = 31U;
    custom.origin.wire_type_name   = store.arena().append_string("myVendorFoo");
    (void)store.add_entry(custom);

    store.finalize();

    ExrAdapterBatch batch;
    const ExrAdapterResult result = build_exr_attribute_batch(store, &batch);
    ASSERT_EQ(result.status, ExrAdapterStatus::Ok);
    EXPECT_EQ(result.exported, 3U);
    EXPECT_EQ(result.skipped, 0U);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(batch.encoding_version, kExrCanonicalEncodingVersion);
    ASSERT_EQ(batch.attributes.size(), 3U);

    const ExrAdapterAttribute* owner_attr = find_attr(batch, 0U, "owner");
    ASSERT_NE(owner_attr, nullptr);
    EXPECT_EQ(owner_attr->type_name, "string");
    EXPECT_FALSE(owner_attr->is_opaque);
    ASSERT_EQ(owner_attr->value.size(), 4U);
    EXPECT_EQ(static_cast<char>(owner_attr->value[0]), 'V');

    const ExrAdapterAttribute* aspect_attr = find_attr(batch, 0U,
                                                       "pixelAspectRatio");
    ASSERT_NE(aspect_attr, nullptr);
    EXPECT_EQ(aspect_attr->type_name, "float");
    EXPECT_FALSE(aspect_attr->is_opaque);
    ASSERT_EQ(aspect_attr->value.size(), 4U);
    EXPECT_EQ(read_u32le(std::span<const std::byte>(aspect_attr->value.data(),
                                                    aspect_attr->value.size())),
              std::bit_cast<uint32_t>(1.0f));

    const ExrAdapterAttribute* custom_attr = find_attr(batch, 1U, "customA");
    ASSERT_NE(custom_attr, nullptr);
    EXPECT_EQ(custom_attr->type_name, "myVendorFoo");
    EXPECT_TRUE(custom_attr->is_opaque);
    EXPECT_EQ(custom_attr->value,
              std::vector<std::byte>(custom_bytes.begin(), custom_bytes.end()));
}


TEST(ExrAdapter, CanSkipOpaqueAttributes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<std::byte, 2> bytes { std::byte { 9 }, std::byte { 8 } };
    Entry custom;
    custom.key = make_exr_attribute_key(store.arena(), 0U, "customA");
    custom.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(bytes.data(), bytes.size()));
    custom.origin.block            = block;
    custom.origin.order_in_block   = 0U;
    custom.origin.wire_type.family = WireFamily::Other;
    custom.origin.wire_type.code   = 31U;
    custom.origin.wire_type_name   = store.arena().append_string("myVendorFoo");
    (void)store.add_entry(custom);
    store.finalize();

    ExrAdapterOptions options;
    options.include_opaque = false;

    ExrAdapterBatch batch;
    const ExrAdapterResult result = build_exr_attribute_batch(store, &batch,
                                                              options);
    EXPECT_EQ(result.status, ExrAdapterStatus::Ok);
    EXPECT_EQ(result.exported, 0U);
    EXPECT_EQ(result.skipped, 1U);
    EXPECT_TRUE(batch.attributes.empty());
}


TEST(ExrAdapter, RejectsAmbiguousArrayWithoutWireType)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint32_t, 2> values { std::bit_cast<uint32_t>(1.0f),
                                           std::bit_cast<uint32_t>(2.0f) };
    Entry attr;
    attr.key          = make_exr_attribute_key(store.arena(), 0U, "vectorA");
    attr.value        = make_f32_bits_array(store.arena(),
                                            std::span<const uint32_t>(values.data(),
                                                                      values.size()));
    attr.origin.block = block;
    attr.origin.order_in_block = 0U;
    (void)store.add_entry(attr);
    store.finalize();

    ExrAdapterBatch batch;
    const ExrAdapterResult result = build_exr_attribute_batch(store, &batch);
    EXPECT_EQ(result.status, ExrAdapterStatus::Unsupported);
    EXPECT_EQ(result.failed_entry, 0U);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.exported, 0U);
}


TEST(ExrAdapter, InfersSimpleScalarTypesWithoutWireMetadata)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry gain;
    gain.key          = make_exr_attribute_key(store.arena(), 0U, "gain");
    gain.value        = make_i32(12);
    gain.origin.block = block;
    gain.origin.order_in_block = 0U;
    (void)store.add_entry(gain);

    Entry owner;
    owner.key          = make_exr_attribute_key(store.arena(), 0U, "owner");
    owner.value        = make_text(store.arena(), "A", TextEncoding::Ascii);
    owner.origin.block = block;
    owner.origin.order_in_block = 1U;
    (void)store.add_entry(owner);

    store.finalize();

    ExrAdapterBatch batch;
    const ExrAdapterResult result = build_exr_attribute_batch(store, &batch);
    ASSERT_EQ(result.status, ExrAdapterStatus::Ok);
    ASSERT_EQ(batch.attributes.size(), 2U);

    const ExrAdapterAttribute* gain_attr = find_attr(batch, 0U, "gain");
    ASSERT_NE(gain_attr, nullptr);
    EXPECT_EQ(gain_attr->type_name, "int");
    ASSERT_EQ(gain_attr->value.size(), 4U);
    EXPECT_EQ(read_u32le(std::span<const std::byte>(gain_attr->value.data(),
                                                    gain_attr->value.size())),
              12U);

    const ExrAdapterAttribute* owner_attr = find_attr(batch, 0U, "owner");
    ASSERT_NE(owner_attr, nullptr);
    EXPECT_EQ(owner_attr->type_name, "string");
    ASSERT_EQ(owner_attr->value.size(), 1U);
    EXPECT_EQ(static_cast<char>(owner_attr->value[0]), 'A');
}


TEST(ExrAdapter, BuildsGroupedPartSpans)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry a0;
    a0.key          = make_exr_attribute_key(store.arena(), 2U, "owner");
    a0.value        = make_text(store.arena(), "C", TextEncoding::Ascii);
    a0.origin.block = block;
    a0.origin.order_in_block   = 0U;
    a0.origin.wire_type.family = WireFamily::Other;
    a0.origin.wire_type.code   = 20U;
    (void)store.add_entry(a0);

    Entry a1;
    a1.key          = make_exr_attribute_key(store.arena(), 0U, "gain");
    a1.value        = make_i32(7);
    a1.origin.block = block;
    a1.origin.order_in_block   = 1U;
    a1.origin.wire_type.family = WireFamily::Other;
    a1.origin.wire_type.code   = 11U;
    (void)store.add_entry(a1);

    Entry a2;
    a2.key          = make_exr_attribute_key(store.arena(), 2U, "owner2");
    a2.value        = make_text(store.arena(), "D", TextEncoding::Ascii);
    a2.origin.block = block;
    a2.origin.order_in_block   = 2U;
    a2.origin.wire_type.family = WireFamily::Other;
    a2.origin.wire_type.code   = 20U;
    (void)store.add_entry(a2);

    store.finalize();

    ExrAdapterBatch batch;
    const ExrAdapterResult build = build_exr_attribute_batch(store, &batch);
    ASSERT_EQ(build.status, ExrAdapterStatus::Ok);
    ASSERT_EQ(batch.attributes.size(), 3U);
    EXPECT_EQ(batch.attributes[0].part_index, 0U);
    EXPECT_EQ(batch.attributes[1].part_index, 2U);
    EXPECT_EQ(batch.attributes[2].part_index, 2U);

    std::vector<ExrAdapterPartSpan> spans;
    const ExrAdapterStatus span_status = build_exr_attribute_part_spans(batch,
                                                                        &spans);
    ASSERT_EQ(span_status, ExrAdapterStatus::Ok);
    ASSERT_EQ(spans.size(), 2U);
    EXPECT_EQ(spans[0].part_index, 0U);
    EXPECT_EQ(spans[0].first_attribute, 0U);
    EXPECT_EQ(spans[0].attribute_count, 1U);
    EXPECT_EQ(spans[1].part_index, 2U);
    EXPECT_EQ(spans[1].first_attribute, 1U);
    EXPECT_EQ(spans[1].attribute_count, 2U);
}


TEST(ExrAdapter, ReplaysGroupedBatchByPart)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry a0;
    a0.key          = make_exr_attribute_key(store.arena(), 1U, "owner");
    a0.value        = make_text(store.arena(), "A", TextEncoding::Ascii);
    a0.origin.block = block;
    a0.origin.order_in_block   = 0U;
    a0.origin.wire_type.family = WireFamily::Other;
    a0.origin.wire_type.code   = 20U;
    (void)store.add_entry(a0);

    Entry a1;
    a1.key          = make_exr_attribute_key(store.arena(), 0U, "gain");
    a1.value        = make_i32(4);
    a1.origin.block = block;
    a1.origin.order_in_block   = 1U;
    a1.origin.wire_type.family = WireFamily::Other;
    a1.origin.wire_type.code   = 11U;
    (void)store.add_entry(a1);

    Entry a2;
    a2.key          = make_exr_attribute_key(store.arena(), 1U, "owner2");
    a2.value        = make_text(store.arena(), "B", TextEncoding::Ascii);
    a2.origin.block = block;
    a2.origin.order_in_block   = 2U;
    a2.origin.wire_type.family = WireFamily::Other;
    a2.origin.wire_type.code   = 20U;
    (void)store.add_entry(a2);

    store.finalize();

    ExrAdapterBatch batch;
    const ExrAdapterResult build = build_exr_attribute_batch(store, &batch);
    ASSERT_EQ(build.status, ExrAdapterStatus::Ok);

    ReplayState state;
    ExrAdapterReplayCallbacks callbacks;
    callbacks.begin_part     = replay_begin_part;
    callbacks.emit_attribute = replay_emit_attribute;
    callbacks.end_part       = replay_end_part;
    callbacks.user           = &state;

    const ExrAdapterReplayResult replay = replay_exr_attribute_batch(batch,
                                                                     callbacks);
    ASSERT_EQ(replay.status, ExrAdapterStatus::Ok);
    EXPECT_EQ(replay.replayed_parts, 2U);
    EXPECT_EQ(replay.replayed_attributes, 3U);
    const std::vector<std::string> expect {
        "begin:0:1",    "attr:0:gain",   "end:0", "begin:1:2",
        "attr:1:owner", "attr:1:owner2", "end:1",
    };
    EXPECT_EQ(state.events, expect);
}


TEST(ExrAdapter, ReplayReportsCallbackFailure)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry a0;
    a0.key          = make_exr_attribute_key(store.arena(), 0U, "gain");
    a0.value        = make_i32(4);
    a0.origin.block = block;
    a0.origin.order_in_block   = 0U;
    a0.origin.wire_type.family = WireFamily::Other;
    a0.origin.wire_type.code   = 11U;
    (void)store.add_entry(a0);

    Entry a1;
    a1.key          = make_exr_attribute_key(store.arena(), 1U, "owner");
    a1.value        = make_text(store.arena(), "A", TextEncoding::Ascii);
    a1.origin.block = block;
    a1.origin.order_in_block   = 1U;
    a1.origin.wire_type.family = WireFamily::Other;
    a1.origin.wire_type.code   = 20U;
    (void)store.add_entry(a1);

    store.finalize();

    ExrAdapterBatch batch;
    const ExrAdapterResult build = build_exr_attribute_batch(store, &batch);
    ASSERT_EQ(build.status, ExrAdapterStatus::Ok);

    ReplayState state;
    state.fail_part = 1U;

    ExrAdapterReplayCallbacks callbacks;
    callbacks.begin_part     = replay_begin_part;
    callbacks.emit_attribute = replay_emit_attribute;
    callbacks.end_part       = replay_end_part;
    callbacks.user           = &state;

    const ExrAdapterReplayResult replay = replay_exr_attribute_batch(batch,
                                                                     callbacks);
    EXPECT_EQ(replay.status, ExrAdapterStatus::Unsupported);
    EXPECT_EQ(replay.replayed_parts, 1U);
    EXPECT_EQ(replay.replayed_attributes, 1U);
    EXPECT_EQ(replay.failed_part_index, 1U);
    EXPECT_EQ(replay.failed_attribute_index, 1U);
    EXPECT_EQ(replay.message, "emit_attribute callback failed");
}


TEST(ExrAdapter, BuildsGroupedPartViews)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry a0;
    a0.key          = make_exr_attribute_key(store.arena(), 2U, "owner");
    a0.value        = make_text(store.arena(), "A", TextEncoding::Ascii);
    a0.origin.block = block;
    a0.origin.order_in_block   = 0U;
    a0.origin.wire_type.family = WireFamily::Other;
    a0.origin.wire_type.code   = 20U;
    (void)store.add_entry(a0);

    Entry a1;
    a1.key          = make_exr_attribute_key(store.arena(), 0U, "gain");
    a1.value        = make_i32(1);
    a1.origin.block = block;
    a1.origin.order_in_block   = 1U;
    a1.origin.wire_type.family = WireFamily::Other;
    a1.origin.wire_type.code   = 11U;
    (void)store.add_entry(a1);

    Entry a2;
    a2.key          = make_exr_attribute_key(store.arena(), 2U, "owner2");
    a2.value        = make_text(store.arena(), "B", TextEncoding::Ascii);
    a2.origin.block = block;
    a2.origin.order_in_block   = 2U;
    a2.origin.wire_type.family = WireFamily::Other;
    a2.origin.wire_type.code   = 20U;
    (void)store.add_entry(a2);

    store.finalize();

    ExrAdapterBatch batch;
    const ExrAdapterResult build = build_exr_attribute_batch(store, &batch);
    ASSERT_EQ(build.status, ExrAdapterStatus::Ok);

    std::vector<ExrAdapterPartView> views;
    const ExrAdapterStatus status = build_exr_attribute_part_views(batch,
                                                                   &views);
    ASSERT_EQ(status, ExrAdapterStatus::Ok);
    ASSERT_EQ(views.size(), 2U);
    EXPECT_EQ(views[0].part_index, 0U);
    ASSERT_EQ(views[0].attributes.size(), 1U);
    EXPECT_EQ(views[0].attributes[0].name, "gain");
    EXPECT_EQ(views[1].part_index, 2U);
    ASSERT_EQ(views[1].attributes.size(), 2U);
    EXPECT_EQ(views[1].attributes[0].name, "owner");
    EXPECT_EQ(views[1].attributes[1].name, "owner2");
}

TEST(ExrAdapter, BuildsBatchFromPreparedTransferBundle)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry make;
    make.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    make.value        = make_text(store.arena(), "Vendor", TextEncoding::Ascii);
    make.origin.block = block;
    make.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(make), kInvalidEntryId);
    store.finalize();

    PrepareTransferRequest request;
    request.target_format      = TransferTargetFormat::Exr;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    PreparedTransferBundle bundle;
    const PrepareTransferResult prepared
        = prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, TransferStatus::Ok);

    ExrAdapterBatch batch;
    const ExrAdapterResult result = build_prepared_exr_attribute_batch(bundle,
                                                                       &batch);
    ASSERT_EQ(result.status, ExrAdapterStatus::Ok);
    EXPECT_EQ(result.exported, 1U);
    ASSERT_EQ(batch.attributes.size(), 1U);
    EXPECT_EQ(batch.attributes[0].part_index, 0U);
    EXPECT_EQ(batch.attributes[0].name, "Make");
    EXPECT_EQ(batch.attributes[0].type_name, "string");
    EXPECT_FALSE(batch.attributes[0].is_opaque);
    ASSERT_EQ(batch.attributes[0].value.size(), 6U);
    EXPECT_EQ(static_cast<char>(batch.attributes[0].value[0]), 'V');
}

TEST(ExrAdapter, RejectsMalformedPreparedTransferPayload)
{
    PreparedTransferBundle bundle;
    bundle.target_format = TransferTargetFormat::Exr;

    PreparedTransferBlock block;
    block.kind    = TransferBlockKind::ExrAttribute;
    block.route   = "exr:attribute-string";
    block.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(std::move(block));

    ExrAdapterBatch batch;
    const ExrAdapterResult result = build_prepared_exr_attribute_batch(bundle,
                                                                       &batch);
    EXPECT_EQ(result.status, ExrAdapterStatus::Unsupported);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.message, "prepared exr attribute payload is malformed");
}

TEST(ExrAdapter, BuildsBatchFromFileHelper)
{
    const std::vector<std::byte> jpeg = make_test_jpeg_with_exif_make("Vendor");
    const std::string path            = write_temp_bytes(
        std::span<const std::byte>(jpeg.data(), jpeg.size()));
    ASSERT_FALSE(path.empty());

    ExrAdapterBatch batch;
    BuildExrAttributeBatchFileOptions options;
    options.prepare.prepare.include_xmp_app1   = false;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    const BuildExrAttributeBatchFileResult result
        = build_exr_attribute_batch_from_file(path.c_str(), &batch, options);

    EXPECT_EQ(result.prepared.file_status, TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, TransferStatus::Ok);
    EXPECT_EQ(result.adapter.status, ExrAdapterStatus::Ok);
    EXPECT_EQ(result.adapter.exported, 1U);
    ASSERT_EQ(batch.attributes.size(), 1U);
    EXPECT_EQ(batch.attributes[0].part_index, 0U);
    EXPECT_EQ(batch.attributes[0].name, "Make");
    EXPECT_EQ(batch.attributes[0].type_name, "string");
    EXPECT_FALSE(batch.attributes[0].is_opaque);
    ASSERT_EQ(batch.attributes[0].value.size(), 6U);
    EXPECT_EQ(std::memcmp(batch.attributes[0].value.data(), "Vendor", 6U), 0);

    (void)std::remove(path.c_str());
}

TEST(ExrAdapter, FileHelperRejectsEmptyPath)
{
    ExrAdapterBatch batch;
    const BuildExrAttributeBatchFileResult result
        = build_exr_attribute_batch_from_file("", &batch);
    EXPECT_EQ(result.prepared.file_status, TransferFileStatus::InvalidArgument);
    EXPECT_EQ(result.prepared.prepare.status, TransferStatus::InvalidArgument);
    EXPECT_EQ(result.adapter.status, ExrAdapterStatus::InvalidArgument);
    EXPECT_EQ(result.adapter.errors, 1U);
}

}  // namespace openmeta
