// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tag_names.h"
#include "../src/openmeta/sony_sr2_decode_internal.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/meta_key.h"
#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {
    static void word(std::span<std::byte> bytes, size_t offset, uint16_t v,
                     bool le = true)
    {
        bytes[offset]      = std::byte(le ? v & 255U : v >> 8U);
        bytes[offset + 1U] = std::byte(le ? v >> 8U : v & 255U);
    }
    static void dword(std::span<std::byte> bytes, size_t offset, uint32_t v)
    {
        for (uint32_t i = 0; i < 4; ++i)
            bytes[offset + i] = std::byte((v >> (8U * i)) & 255U);
    }
    static void descriptor(std::span<std::byte> bytes, size_t offset,
                           uint16_t tag, uint16_t type, uint32_t count,
                           uint32_t value)
    {
        word(bytes, offset, tag);
        word(bytes, offset + 2U, type);
        dword(bytes, offset + 4U, count);
        dword(bytes, offset + 8U, value);
    }
    static std::vector<std::byte> maker_fixture(std::string_view model,
                                                uint16_t tag,
                                                std::span<const std::byte> raw)
    {
        const size_t model_offset = 55U;
        const size_t exif_offset  = model_offset + model.size() + 1U;
        const size_t maker_offset = exif_offset + 18U;
        const size_t raw_offset   = maker_offset + 18U;
        std::vector<std::byte> bytes(raw_offset + raw.size());
        bytes[0] = std::byte { 'I' };
        bytes[1] = std::byte { 'I' };
        word(bytes, 2U, 42U);
        dword(bytes, 4U, 8U);
        word(bytes, 8U, 3U);
        descriptor(bytes, 10U, 0x010fU, 2U, 5U, 50U);
        descriptor(bytes, 22U, 0x0110U, 2U, uint32_t(model.size() + 1U),
                   uint32_t(model_offset));
        descriptor(bytes, 34U, 0x8769U, 4U, 1U, uint32_t(exif_offset));
        std::memcpy(bytes.data() + 50U, "SONY", 4U);
        std::memcpy(bytes.data() + model_offset, model.data(), model.size());
        word(bytes, exif_offset, 1U);
        descriptor(bytes, exif_offset + 2U, 0x927cU, 7U,
                   uint32_t(18U + raw.size()), uint32_t(maker_offset));
        word(bytes, maker_offset, 1U);
        descriptor(bytes, maker_offset + 2U, tag, 7U, uint32_t(raw.size()),
                   uint32_t(raw_offset));
        std::memcpy(bytes.data() + raw_offset, raw.data(), raw.size());
        return bytes;
    }
    struct Source final {
        std::span<const std::byte> bytes;
        uint64_t forbidden = UINT64_MAX;
        bool bad_read      = false;
    };
    static RandomAccessIoResult read_at(void* context, uint64_t offset,
                                        std::span<std::byte> out) noexcept
    {
        Source* s = static_cast<Source*>(context);
        if (offset > s->bytes.size() || out.size() > s->bytes.size() - offset)
            return { RandomAccessIoCode::Ok, 0U };
        if (offset >= s->forbidden || out.size() > s->forbidden - offset) {
            s->bad_read = true;
            return { RandomAccessIoCode::IoError, 0U };
        }
        std::memcpy(out.data(), s->bytes.data() + offset, out.size());
        return { RandomAccessIoCode::Ok, out.size() };
    }
    static ExifDecodeResult decode(std::span<const std::byte> bytes,
                                   MetaStore& store, bool callback,
                                   ExifDecodeLimits limits = {},
                                   uint64_t forbidden      = UINT64_MAX)
    {
        ExifDecodeOptions options;
        options.decode_makernote = true;
        options.limits           = limits;
        if (!callback)
            return decode_exif_tiff(bytes, store, {}, options);
        Source state { bytes, forbidden };
        RandomAccessSource input;
        input.size    = bytes.size();
        input.context = &state;
        input.read_at = read_at;
        std::array<std::byte, 32> window {};
        std::vector<std::byte> values(65536U);
        ExifRandomAccessScratch scratch;
        scratch.read_window                       = window;
        scratch.value                             = values;
        scratch.window_options.minimum_read_bytes = 0U;
        const auto result = decode_exif_tiff_random_access(
            make_random_access_source_range(input), store, {}, scratch,
            options);
        EXPECT_FALSE(state.bad_read);
        EXPECT_TRUE(result.complete());
        return result.decode;
    }
    static const Entry* find(const MetaStore& store, std::string_view ifd,
                             uint16_t tag, std::string_view name = {})
    {
        for (EntryId id : store.find_all(make_exif_tag_key_view(ifd, tag))) {
            const Entry& entry = store.entry(id);
            if (name.empty()
                || exif_entry_name(store, entry, ExifTagNamePolicy::Canonical)
                       == name)
                return &entry;
        }
        return nullptr;
    }
    static void scalar(const MetaStore& store, std::string_view ifd,
                       uint16_t tag, uint64_t expected,
                       std::string_view name = {})
    {
        const Entry* entry = find(store, ifd, tag, name);
        ASSERT_NE(entry, nullptr) << ifd << '/' << tag << '/' << name;
        EXPECT_EQ(entry->value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(entry->value.data.u64, expected);
        if (!name.empty())
            EXPECT_EQ(exif_entry_name(store, *entry,
                                      ExifTagNamePolicy::ExifToolCompat),
                      name);
    }
    static std::vector<std::byte> more_info(uint16_t child, uint16_t size)
    {
        std::vector<std::byte> raw(20480U);
        word(raw, 0U, 1U);
        word(raw, 2U, size + 8U);
        word(raw, 4U, child);
        word(raw, 6U, 8U);
        return raw;
    }
    static std::vector<std::byte> mri()
    {
        std::vector<std::byte> bytes(152U);
        std::memcpy(bytes.data(), "\0MRI", 4U);
        dword(bytes, 4U, 144U);
        std::memcpy(bytes.data() + 8U, "\0PRD", 4U);
        dword(bytes, 12U, 24U);
        std::memcpy(bytes.data() + 16U, "12345678", 8U);
        word(bytes, 24U, 0x0102U);
        word(bytes, 26U, 0x0304U);
        word(bytes, 28U, 0x0506U);
        word(bytes, 30U, 0x0708U);
        bytes[32U] = std::byte { 12 };
        bytes[39U] = std::byte { 1 };
        std::memcpy(bytes.data() + 40U, "\0WBG", 4U);
        dword(bytes, 44U, 12U);
        word(bytes, 52U, 0x1122U);
        word(bytes, 54U, 0x3344U);
        word(bytes, 56U, 0x5566U);
        word(bytes, 58U, 0x7788U);
        std::memcpy(bytes.data() + 60U, "\0RIF", 4U);
        dword(bytes, 64U, 84U);
        bytes[68U + 74U] = std::byte { 2 };
        bytes[68U + 76U] = std::byte { 51 };
        bytes[68U + 77U] = std::byte { 8 };
        bytes[68U + 78U] = std::byte { 63 };
        bytes[68U + 79U] = std::byte { 9 };
        dword(bytes, 68U + 80U, 0x10203040U);
        return bytes;
    }
    static std::vector<std::byte> raw_fixture(bool a100, bool valid_subifd)
    {
        std::vector<std::byte> bytes(2048U);
        bytes[0] = std::byte { 'I' };
        bytes[1] = std::byte { 'I' };
        word(bytes, 2U, 42U);
        dword(bytes, 4U, 8U);
        word(bytes, 8U, 4U);
        // Unsorted: private metadata and the image pointer precede Make/Model.
        descriptor(bytes, 10U, 0x014aU, 4U, 1U, a100 ? 1536U : 0U);
        descriptor(bytes, 22U, 0xc634U, 7U, 4U, 256U);
        descriptor(bytes, 34U, 0x0110U, 2U, 10U, 80U);
        descriptor(bytes, 46U, 0x010fU, 2U, 5U, 64U);
        std::memcpy(bytes.data() + 64U, "SONY", 4U);
        std::memcpy(bytes.data() + 80U, a100 ? "DSLR-A100" : "DSLR-A200", 9U);
        const std::vector<std::byte> native = mri();
        if (a100)
            std::memcpy(bytes.data() + 256U, native.data(), native.size());
        else {
            word(bytes, 256U, 1U);
            descriptor(bytes, 258U, 0x7250U, 3U, 128U, 384U);
            std::memcpy(bytes.data() + 384U, native.data(), native.size());
        }
        if (valid_subifd) {
            word(bytes, 1536U, 1U);
            descriptor(bytes, 1538U, 0x0100U, 4U, 1U, 123U);
        } else
            word(bytes, 1536U, 65535U);
        return bytes;
    }
    static std::vector<std::byte>
    c634_fixture(std::span<const std::byte> private_ifd)
    {
        constexpr size_t make_offset    = 64U;
        constexpr size_t model_offset   = 72U;
        constexpr size_t private_offset = 128U;
        std::vector<std::byte> bytes(private_offset + private_ifd.size());
        bytes[0] = std::byte { 'I' };
        bytes[1] = std::byte { 'I' };
        word(bytes, 2U, 42U);
        dword(bytes, 4U, 8U);
        word(bytes, 8U, 3U);
        descriptor(bytes, 10U, 0x010fU, 2U, 5U,
                   static_cast<uint32_t>(make_offset));
        descriptor(bytes, 22U, 0x0110U, 2U, 10U,
                   static_cast<uint32_t>(model_offset));
        descriptor(bytes, 34U, 0xc634U, 7U, 4U,
                   static_cast<uint32_t>(private_offset));
        std::memcpy(bytes.data() + make_offset, "SONY", 4U);
        std::memcpy(bytes.data() + model_offset, "ILCE-7RM2", 9U);
        std::memcpy(bytes.data() + private_offset, private_ifd.data(),
                    private_ifd.size());
        return bytes;
    }
}  // namespace

TEST(SonyRawGaps, PrivateCountersAndSharedByteNames)
{
    for (bool callback : { false, true }) {
        std::vector<std::byte> raw(5478U);
        raw[0x0131U] = std::byte { 0x85 };
        MetaStore camera;
        EXPECT_EQ(decode(maker_fixture("DSLR-A900", 0x0010U, raw), camera,
                         callback)
                      .status,
                  ExifDecodeStatus::Ok);
        camera.finalize();
        scalar(camera, "mk_sony_camerainfo_0", 0x0131U, 1U, "AFMicroAdjMode");
        scalar(camera, "mk_sony_camerainfo_0", 0x0131U, 5U,
               "AFMicroAdjRegisteredLenses");
        const auto snapshot = build_transfer_source_snapshot(camera);
        std::vector<std::byte> serialized;
        ASSERT_EQ(
            serialize_transfer_source_snapshot(snapshot, &serialized).status,
            TransferStatus::Ok);
        TransferSourceSnapshot restored;
        ASSERT_EQ(
            deserialize_transfer_source_snapshot(serialized, &restored).status,
            TransferStatus::Ok);
        restored.store.finalize();
        scalar(restored.store, "mk_sony_camerainfo_0", 0x0131U, 5U,
               "AFMicroAdjRegisteredLenses");
        raw.assign(19154U, std::byte { 0 });
        dword(raw, 0x0846U, 0xaa123456U);
        MetaStore focus;
        EXPECT_EQ(decode(maker_fixture("DSLR-A330", 0x0020U, raw), focus,
                         callback)
                      .status,
                  ExifDecodeStatus::Ok);
        focus.finalize();
        scalar(focus, "mk_sony_focusinfo_0", 0x0846U, 0x123456U,
               "ShutterCount");
        for (std::string_view model : { "NEX-5C", "DSLR-A550", "NEX-5" }) {
            raw = more_info(0x0201U, 400U);
            dword(raw, 8U + 0x011bU, 0xee123456U);
            dword(raw, 8U + 0x0125U, 0xff654321U);
            dword(raw, 8U + 0x014aU, 0xccabcdefU);
            MetaStore more;
            EXPECT_EQ(decode(maker_fixture(model, 0x0020U, raw), more, callback)
                          .status,
                      ExifDecodeStatus::Ok);
            more.finalize();
            if (model == "DSLR-A550")
                scalar(more, "mk_sony_moreinfo0201_0", 0x014aU, 0xabcdefU);
            else {
                scalar(more, "mk_sony_moreinfo0201_0", 0x011bU, 0x123456U);
                scalar(more, "mk_sony_moreinfo0201_0", 0x0125U, 0x654321U);
            }
            raw = more_info(0x0401U, 1200U);
            dword(raw, 8U + 0x044eU, 0xab123456U);
            MetaStore shot;
            EXPECT_EQ(decode(maker_fixture(model, 0x0020U, raw), shot, callback)
                          .status,
                      ExifDecodeStatus::Ok);
            shot.finalize();
            if (model == "NEX-5")
                EXPECT_EQ(find(shot, "mk_sony_moreinfo0401_0", 0x044eU),
                          nullptr);
            else
                scalar(shot, "mk_sony_moreinfo0401_0", 0x044eU, 0x123456U);
        }
    }
}

TEST(SonyRawGaps, FaceCountsAndDirectoryBounds)
{
    for (bool callback : { false, true }) {
        SCOPED_TRACE(callback);
        for (uint16_t faces : { 0U, 1U, 9U }) {
            std::vector<std::byte> raw = more_info(2U, 400U);
            word(raw, 12U, 257U);
            word(raw, 14U, faces);  // test2 and FacesDetected
            MetaStore store;
            EXPECT_EQ(decode(maker_fixture("DSLR-A550", 0x0020U, raw), store,
                             callback)
                          .status,
                      ExifDecodeStatus::Ok);
            store.finalize();
            scalar(store, "mk_sony_faceinfoa_0", 3U, faces <= 8U ? faces : 0U);
            EXPECT_EQ(find(store, "mk_sony_faceinfoa_0", 0x000bU) != nullptr,
                      faces == 1U);
            EXPECT_EQ(find(store, "mk_sony_faceinfoa_0", 0x005bU) != nullptr,
                      faces == 1U);
        }
        std::vector<std::byte> raw = more_info(1U, 200U);
        word(raw, 6U, 4U);
        MetaStore malformed;
        EXPECT_EQ(decode(maker_fixture("NEX-3", 0x0020U, raw), malformed,
                         callback)
                      .status,
                  ExifDecodeStatus::Malformed);
        raw = more_info(1U, 200U);
        word(raw, 0U, 2U);
        word(raw, 4U, 1U);
        word(raw, 6U, 12U);
        word(raw, 8U, 2U);
        word(raw, 10U, 12U);
        MetaStore duplicates;
        EXPECT_EQ(decode(maker_fixture("NEX-3", 0x0020U, raw), duplicates,
                         callback)
                      .status,
                  ExifDecodeStatus::Ok);
        duplicates.finalize();
        EXPECT_EQ(find(duplicates, "mk_sony_moresettings_0", 0x0001U), nullptr);
        raw.assign(364U, std::byte { 0 });
        ExifDecodeLimits limits;
        limits.max_entries_per_ifd = 3U;
        MetaStore limited;
        EXPECT_EQ(decode(maker_fixture("DSLR-A900", 0x0114U, raw), limited,
                         callback, limits)
                      .status,
                  ExifDecodeStatus::LimitExceeded);
        raw.assign(333U, std::byte { 0 });
        MetaStore unknown;
        EXPECT_EQ(decode(maker_fixture("DSLR-A330", 0x0114U, raw), unknown,
                         callback)
                      .status,
                  ExifDecodeStatus::Ok);
        unknown.finalize();
        EXPECT_EQ(find(unknown, "mk_sony_camerasettings2_0", 0U), nullptr);
    }
}

TEST(SonyRawGaps, EmbeddedLittleEndianMrwAndA100ImagePointer)
{
    for (bool callback : { false, true }) {
        for (bool a100 : { false, true }) {
            for (bool valid_subifd : { false, true }) {
                MetaStore store;
                const auto result
                    = decode(raw_fixture(a100, valid_subifd), store, callback,
                             {}, a100 && !valid_subifd ? 1538U : UINT64_MAX);
                EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
                store.finalize();
                scalar(store, "mk_minoltaraw_prd_0", 8U, 0x0102U);
                scalar(store, "mk_minoltaraw_rif_0", 74U, 2U);
                scalar(store, "mk_minoltaraw_rif_0", a100 ? 76U : 78U,
                       a100 ? 51U : 63U);
                if (a100)
                    scalar(store, "mk_minoltaraw_rif_0", 80U, 0x10203040U);
                EXPECT_EQ(find(store, "mk_minoltaraw_rif_0", 60U), nullptr);
                const Entry* wb = find(store, "mk_minoltaraw_wbg_0", 4U);
                ASSERT_NE(wb, nullptr);
                ASSERT_EQ(wb->value.kind, MetaValueKind::Array);
                std::array<uint16_t, 4> expected { 0x1122U, 0x3344U, 0x5566U,
                                                   0x7788U },
                    actual {};
                const auto bytes = store.arena().span(wb->value.data.span);
                ASSERT_EQ(bytes.size(), sizeof(actual));
                std::memcpy(actual.data(), bytes.data(), bytes.size());
                EXPECT_EQ(actual, expected);
                if (a100 && valid_subifd)
                    scalar(store, "subifd0", 0x0100U, 123U);
                else
                    EXPECT_EQ(find(store, "subifd0", 0x0100U), nullptr);
            }
        }
    }
}
TEST(SonyRawGaps, A100ImageProbeHonorsConfiguredDirectoryCount)
{
    for (bool callback : { false, true }) {
        auto bytes = raw_fixture(true, false);
        word(bytes, 1536U, 33U);  // Complete table extent fits the source.
        ExifDecodeLimits limits;
        limits.max_entries_per_ifd = 32U;
        MetaStore store;
        EXPECT_EQ(decode(bytes, store, callback, limits, 1538U).status,
                  ExifDecodeStatus::Ok);
        store.finalize();
        EXPECT_EQ(find(store, "subifd0", 0x0100U), nullptr);
        scalar(store, "mk_minoltaraw_rif_0", 80U, 0x10203040U);
    }
}

TEST(SonyRawGaps, C634RequiresRecognizedSr2PrivateIfdBeforeApplyingLimits)
{
    for (bool callback : { false, true }) {
        SCOPED_TRACE(callback);
        constexpr uint16_t opaque_count = 29132U;
        std::vector<std::byte> opaque(6U + size_t(opaque_count) * 12U);
        word(opaque, 0U, opaque_count);
        opaque[2U] = std::byte { 0x37 };
        opaque[3U] = std::byte { 0x74 };
        opaque[4U] = std::byte { 0xe4 };
        opaque[5U] = std::byte { 0xfc };
        MetaStore unknown;
        EXPECT_EQ(decode(c634_fixture(opaque), unknown, callback).status,
                  ExifDecodeStatus::Ok);
        unknown.finalize();
        EXPECT_TRUE(unknown
                        .find_all(make_exif_tag_key_view("mk_sony_sr2private_0",
                                                         0x7437U))
                        .empty());

        std::vector<std::byte> recognized(54U);
        word(recognized, 0U, 4U);
        descriptor(recognized, 2U, 0x7200U, 4U, 1U, 0x100U);
        descriptor(recognized, 14U, 0x7201U, 4U, 1U, 0x200U);
        descriptor(recognized, 26U, 0x7221U, 4U, 1U, 0x12345678U);
        descriptor(recognized, 38U, 0x7240U, 4U, 1U, 0U);
        ExifDecodeLimits limits;
        limits.max_entries_per_ifd = 3U;
        MetaStore limited;
        EXPECT_EQ(
            decode(c634_fixture(recognized), limited, callback, limits).status,
            ExifDecodeStatus::LimitExceeded);
    }
}
TEST(SonyRawGaps, MoreInfoRetainsOutOfOrderOpaqueRecords)
{
    auto raw = more_info(0xffffU, 24U);
    word(raw, 0U, 2U);
    word(raw, 2U, 32U);
    word(raw, 6U, 24U);
    word(raw, 8U, 0x0101U);
    word(raw, 10U, 12U);
    std::fill(raw.begin() + 12U, raw.begin() + 24U, std::byte { 0x11 });
    std::fill(raw.begin() + 24U, raw.begin() + 32U, std::byte { 0x22 });
    for (bool callback : { false, true }) {
        MetaStore store;
        EXPECT_EQ(decode(maker_fixture("DSLR-A580", 0x0020U, raw), store,
                         callback)
                      .status,
                  ExifDecodeStatus::Ok);
        store.finalize();
        for (uint16_t tag : { 0x0101U, 0xffffU }) {
            const Entry* entry = find(store, "mk_sony_moreinfo_0", tag);
            ASSERT_NE(entry, nullptr);
            ASSERT_EQ(entry->value.kind, MetaValueKind::Bytes);
            const auto bytes = store.arena().span(entry->value.data.span);
            ASSERT_EQ(bytes.size(), tag == 0x0101U ? 12U : 8U);
            for (std::byte byte : bytes)
                EXPECT_EQ(byte, tag == 0x0101U ? std::byte { 0x11 }
                                               : std::byte { 0x22 });
        }
    }
}
TEST(SonyRawGaps, MeteringMetadataRetainsEncodedSourceBytes)
{
    std::vector<std::byte> raw(19154U);
    std::fill(raw.begin() + 0x1110U, raw.begin() + 0x1110U + 9600U,
              std::byte { 0x5a });
    for (bool callback : { false, true }) {
        MetaStore store;
        EXPECT_EQ(decode(maker_fixture("DSLR-A350", 0x0020U, raw), store,
                         callback)
                      .status,
                  ExifDecodeStatus::Ok);
        store.finalize();
        const Entry* entry = find(store, "mk_sony_focusinfo_0", 0x1110U,
                                  "TiffMeteringImage");
        ASSERT_NE(entry, nullptr);
        ASSERT_EQ(entry->value.kind, MetaValueKind::Bytes);
        const auto bytes = store.arena().span(entry->value.data.span);
        ASSERT_EQ(bytes.size(), 9600U);
        for (std::byte byte : bytes)
            EXPECT_EQ(byte, std::byte { 0x5a });
    }
}
TEST(SonyRawGaps, Tag900bUsesCipherPrefixAndLegacyModelGate)
{
    for (bool callback : { false, true }) {
        for (uint32_t variant = 0U; variant < 3U; ++variant) {
            std::vector<std::byte> raw(192U);
            raw[0]     = std::byte(variant == 2U ? 1U : 0xaeU);
            raw[2]     = std::byte((98U * 98U * 98U) % 249U);
            raw[0xbdU] = std::byte { 1 };
            MetaStore store;
            EXPECT_EQ(decode(maker_fixture(variant == 1U ? "DSLR-A550"
                                                         : "DSLR-A580",
                                           0x900bU, raw),
                             store, callback)
                          .status,
                      ExifDecodeStatus::Ok);
            store.finalize();
            if (variant != 2U)
                scalar(store, "mk_sony_tag900b_0", 2U, 98U, "FacesDetected");
            else
                EXPECT_EQ(find(store, "mk_sony_tag900b_0", 2U), nullptr);
            if (variant == 0U)
                scalar(store, "mk_sony_tag900b_0", 0xbdU, 1U, "FaceDetection");
            else
                EXPECT_EQ(find(store, "mk_sony_tag900b_0", 0xbdU), nullptr);
        }
    }
}
TEST(SonyRawGaps, ExtraInfoUsesFullFrameModelBoundary)
{
    std::vector<std::byte> raw(11U);
    raw[10U] = std::byte { 207 };
    for (bool callback : { false, true }) {
        for (std::string_view model :
             { "DSLR-A850", "DSLR-A900", "DSLR-A850-X", "DSLR-A850X",
               "DSLR-A900_2", "DSLR-A580" }) {
            MetaStore store;
            EXPECT_EQ(decode(maker_fixture(model, 0x0116U, raw), store, callback)
                          .status,
                      ExifDecodeStatus::Ok);
            store.finalize();
            if (model == "DSLR-A850" || model == "DSLR-A900"
                || model == "DSLR-A850-X")
                scalar(store, "mk_sony_extrainfo_0", 10U, 207U,
                       "ImageStabilization2");
            else
                EXPECT_EQ(find(store, "mk_sony_extrainfo_0", 10U), nullptr);
        }
    }
}
}  // namespace openmeta

namespace openmeta {
namespace {
    static constexpr std::array<uint8_t, 48> sr2_reference_cipher {{
        0x84U, 0x66U, 0x3bU, 0x08U, 0xc3U, 0x26U, 0xe5U, 0x48U,
        0x78U, 0x10U, 0x55U, 0x4eU, 0xbdU, 0x04U, 0x5bU, 0xacU,
        0xffU, 0xecU, 0x98U, 0x68U, 0xfaU, 0x45U, 0x59U, 0x2eU,
        0x09U, 0xf9U, 0xd2U, 0x4eU, 0x8eU, 0x82U, 0x51U, 0xe1U,
        0x15U, 0x2bU, 0x9fU, 0x4eU, 0xe9U, 0x8cU, 0x40U, 0x9fU,
        0xc7U, 0xa7U, 0x79U, 0x05U, 0x42U, 0x19U, 0x20U, 0xf9U,
    }};

    static std::vector<std::byte> encrypted_sr2_fixture(uint32_t key = 886953816U,
                                                        uint32_t size = 48U,
                                                        uint32_t offset = 256U)
    {
        std::array<std::byte, 42> root {};
        word(root, 0U, 3U);
        descriptor(root, 2U, 0x7200U, 4U, 1U, offset);
        descriptor(root, 14U, 0x7201U, 4U, 1U, size);
        descriptor(root, 26U, 0x7221U, 7U, 4U, key);
        std::vector<std::byte> bytes = c634_fixture(root);
        bytes.resize(8192U, std::byte {0xe3});
        if (offset <= bytes.size() && sr2_reference_cipher.size() <= bytes.size() - offset) {
            for (size_t i = 0U; i < sr2_reference_cipher.size(); ++i)
                bytes[offset + i] = static_cast<std::byte>(sr2_reference_cipher[i]);
        }
        return bytes;
    }

    static void expect_sr2_levels(const MetaStore& store)
    {
        const Entry* black = find(store, "mk_sony_sr2subifd_0", 0x7300U);
        const Entry* white = find(store, "mk_sony_sr2subifd_0", 0x7302U);
        ASSERT_NE(black, nullptr);
        ASSERT_NE(white, nullptr);
        EXPECT_EQ(black->origin.wire_type.family, WireFamily::Tiff);
        EXPECT_EQ(black->origin.wire_type.code, 3U);
        EXPECT_EQ(black->origin.wire_count, 4U);
        EXPECT_EQ(black->value.count, 4U);
        const std::array<uint16_t, 4> expected_black { 511, 515, 512, 513 };
        const std::array<uint16_t, 4> expected_white { 256, 485, 396, 256 };
        std::array<uint16_t, 4> actual {};
        ASSERT_EQ(store.arena().span(black->value.data.span).size(), sizeof(actual));
        std::memcpy(actual.data(), store.arena().span(black->value.data.span).data(), sizeof(actual));
        EXPECT_EQ(actual, expected_black);
        ASSERT_EQ(store.arena().span(white->value.data.span).size(), sizeof(actual));
        std::memcpy(actual.data(), store.arena().span(white->value.data.span).data(), sizeof(actual));
        EXPECT_EQ(actual, expected_white);
    }
}

TEST(SonyRawGaps, Sr2CipherMatchesIndependentReference)
{
    std::array<std::byte, 48> bytes {};
    for (size_t i = 0U; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::byte>(sr2_reference_cipher[i]);
    ASSERT_TRUE(exif_internal::sony_sr2_decrypt(bytes, 886953816U));
    std::array<std::byte, 48> expected {};
    word(expected, 0U, 2U);
    descriptor(expected, 2U, 0x7300U, 3U, 4U, 288U);
    descriptor(expected, 14U, 0x7302U, 3U, 4U, 296U);
    const std::array<uint16_t, 8> values { 511, 515, 512, 513, 256, 485, 396, 256 };
    for (size_t i = 0U; i < values.size(); ++i) word(expected, 32U + i * 2U, values[i]);
    EXPECT_EQ(bytes, expected);
    std::array<std::byte, 3> malformed {};
    EXPECT_FALSE(exif_internal::sony_sr2_decrypt(malformed, 886953816U));
    EXPECT_EQ(malformed, (std::array<std::byte, 3> {}));
}

TEST(SonyRawGaps, Sr2PrivateSubtreeMatchesMemoryAndPrefixedCallback)
{
    const std::vector<std::byte> bytes = encrypted_sr2_fixture();
    const std::vector<std::byte> original = bytes;
    MetaStore memory;
    ASSERT_EQ(decode(bytes, memory, false).status, ExifDecodeStatus::Ok);
    memory.finalize();
    expect_sr2_levels(memory);
    EXPECT_EQ(bytes, original);
    constexpr uint64_t prefix = 37U;
    std::vector<std::byte> backing(prefix, std::byte {0x5a});
    backing.insert(backing.end(), bytes.begin(), bytes.end());
    Source state { backing, prefix + 304U };
    const RandomAccessSource source = make_callback_random_access_source(backing.size(), &state, read_at, false);
    std::array<std::byte, 32> window {};
    std::array<std::byte, 64> value {};
    ExifRandomAccessScratch scratch;
    scratch.read_window = window;
    scratch.value = value;
    scratch.window_options.minimum_read_bytes = 0U;
    ExifDecodeOptions options;
    options.decode_makernote = true;
    MetaStore callback;
    const ExifRandomAccessDecodeResult result = decode_exif_tiff_random_access(
        make_random_access_source_range(source, prefix, bytes.size()), callback, {}, scratch, options);
    ASSERT_EQ(result.decode.status, ExifDecodeStatus::Ok);
    EXPECT_TRUE(result.complete());
    EXPECT_FALSE(state.bad_read);
    callback.finalize();
    expect_sr2_levels(callback);
}

TEST(SonyRawGaps, Sr2PrivateRejectsWrongKeyLengthAndOffsetBase)
{
    for (bool callback : {false, true}) {
        for (uint32_t key : {886953816U ^ 1U}) {
            MetaStore store;
            EXPECT_EQ(decode(encrypted_sr2_fixture(key), store, callback).status,
                      ExifDecodeStatus::Malformed);
        }
        MetaStore unaligned, outside, wrong_base, limited;
        EXPECT_EQ(decode(encrypted_sr2_fixture(886953816U, 47U), unaligned, callback).status,
                  ExifDecodeStatus::Malformed);
        EXPECT_EQ(decode(encrypted_sr2_fixture(886953816U, 48U, 9000U), outside, callback).status,
                  ExifDecodeStatus::Malformed);
        EXPECT_EQ(decode(encrypted_sr2_fixture(886953816U, 48U, 512U), wrong_base, callback).status,
                  ExifDecodeStatus::Malformed);
        ExifDecodeLimits limits;
        limits.max_value_bytes = 40U;
        EXPECT_EQ(decode(encrypted_sr2_fixture(), limited, callback, limits).status,
                  ExifDecodeStatus::LimitExceeded);
    }
}

TEST(SonyRawGaps, Sr2PrivateReportsExactDecryptionScratch)
{
    const std::vector<std::byte> bytes = encrypted_sr2_fixture();
    Source state { bytes, 304U };
    const RandomAccessSource source = make_callback_random_access_source(bytes.size(), &state, read_at, false);
    std::array<std::byte, 32> window {};
    std::array<std::byte, 8> value {};
    ExifRandomAccessScratch scratch;
    scratch.read_window = window;
    scratch.value = value;
    scratch.window_options.minimum_read_bytes = 0U;
    ExifDecodeOptions options;
    options.decode_makernote = true;
    MetaStore store;
    const ExifRandomAccessDecodeResult result = decode_exif_tiff_random_access(
        make_random_access_source_range(source), store, {}, scratch, options);
    EXPECT_FALSE(result.complete());
    EXPECT_EQ(result.value_scratch_needed, 48U);
    EXPECT_FALSE(state.bad_read);
}
}  // namespace openmeta

namespace openmeta {
TEST(SonyRawGaps, Sr2PrivateSharesOuterIfdBudget)
{
    for (bool callback : { false, true }) for (uint32_t max_ifds : {1U,2U,3U}) {
        MetaStore store; ExifDecodeLimits limits; limits.max_ifds=max_ifds;
        const auto r=decode(encrypted_sr2_fixture(),store,callback,limits,304U);
        EXPECT_EQ(r.status,max_ifds<3U?ExifDecodeStatus::LimitExceeded:ExifDecodeStatus::Ok);
        store.finalize();
        EXPECT_EQ(find(store,"mk_sony_sr2subifd_0",0x7300U)!=nullptr,max_ifds==3U);
    }
}
}

namespace openmeta {
TEST(SonyRawGaps, Sr2PrivateStructuralReadsStopBeforePixelTailWithDefaultWindow)
{
    auto bytes=encrypted_sr2_fixture();
    bytes.resize(16384U);
    // Move only the C634 wrapper beside the pixel boundary. Outer TIFF
    // structure remains far enough away for the caller's normal read-ahead.
    constexpr uint32_t root=8138U;
    std::memcpy(bytes.data()+root,bytes.data()+128U,42U);
    dword(bytes,42U,root);
    Source state{bytes,8192U};
    const auto input=make_callback_random_access_source(bytes.size(),&state,read_at);
    std::array<std::byte,4096> window{};
    std::array<std::byte,64> value{};
    ExifRandomAccessScratch scratch; scratch.read_window=window; scratch.value=value;
    ExifDecodeOptions options; options.decode_makernote=true;
    MetaStore store; const auto r=decode_exif_tiff_random_access(
        make_random_access_source_range(input),store,{},scratch,options);
    EXPECT_FALSE(state.bad_read); EXPECT_TRUE(r.complete());
    EXPECT_EQ(r.decode.status,ExifDecodeStatus::Ok);
    store.finalize(); expect_sr2_levels(store);
}
}
