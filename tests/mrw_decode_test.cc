// SPDX-License-Identifier: Apache-2.0

#include "../src/openmeta/mrw_decode_internal.h"

#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_key.h"
#include "openmeta/metadata_query.h"
#include "openmeta/simple_meta.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace openmeta {
namespace {

    static void put_u16be(std::span<std::byte> bytes, size_t offset,
                          uint16_t value)
    {
        bytes[offset]      = static_cast<std::byte>((value >> 8U) & 0xffU);
        bytes[offset + 1U] = static_cast<std::byte>(value & 0xffU);
    }

    static void put_u32be(std::span<std::byte> bytes, size_t offset,
                          uint32_t value)
    {
        bytes[offset]      = static_cast<std::byte>((value >> 24U) & 0xffU);
        bytes[offset + 1U] = static_cast<std::byte>((value >> 16U) & 0xffU);
        bytes[offset + 2U] = static_cast<std::byte>((value >> 8U) & 0xffU);
        bytes[offset + 3U] = static_cast<std::byte>(value & 0xffU);
    }

    static void put_u16le(std::span<std::byte> bytes, size_t offset,
                          uint16_t value)
    {
        bytes[offset]      = static_cast<std::byte>(value & 0xffU);
        bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    }

    static void put_u32le(std::span<std::byte> bytes, size_t offset,
                          uint32_t value)
    {
        bytes[offset]      = static_cast<std::byte>(value & 0xffU);
        bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
        bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
        bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
    }

    static void append_text(std::vector<std::byte>* bytes,
                            std::string_view text)
    {
        for (size_t i = 0U; i < text.size(); ++i) {
            bytes->push_back(
                static_cast<std::byte>(static_cast<uint8_t>(text[i])));
        }
    }

    static void append_segment(std::vector<std::byte>* file,
                               std::string_view id,
                               std::span<const std::byte> payload,
                               bool le = false)
    {
        append_text(file, id);
        const size_t size_offset = file->size();
        file->resize(file->size() + 4U);
        if (le) {
            put_u32le(*file, size_offset,
                      static_cast<uint32_t>(payload.size()));
        } else {
            put_u32be(*file, size_offset,
                      static_cast<uint32_t>(payload.size()));
        }
        file->insert(file->end(), payload.begin(), payload.end());
    }

    static std::vector<std::byte> make_tiff_payload(std::string_view model = {})
    {
        const uint16_t entry_count  = model.empty() ? 1U : 2U;
        const uint32_t model_offset = 8U + 2U + (uint32_t(entry_count) * 12U)
                                      + 4U;
        std::vector<std::byte> tiff(model.empty()
                                        ? 26U
                                        : model_offset + model.size() + 1U,
                                    std::byte { 0 });
        tiff[0] = std::byte { 'I' };
        tiff[1] = std::byte { 'I' };
        put_u16le(tiff, 2U, 42U);
        put_u32le(tiff, 4U, 8U);
        put_u16le(tiff, 8U, entry_count);
        put_u16le(tiff, 10U, 0x0100U);
        put_u16le(tiff, 12U, 4U);
        put_u32le(tiff, 14U, 1U);
        put_u32le(tiff, 18U, 640U);
        if (model.empty()) {
            put_u32le(tiff, 22U, 0U);
        } else {
            const size_t model_entry = 22U;
            put_u16le(tiff, model_entry, 0x0110U);
            put_u16le(tiff, model_entry + 2U, 2U);
            put_u32le(tiff, model_entry + 4U,
                      static_cast<uint32_t>(model.size() + 1U));
            put_u32le(tiff, model_entry + 8U, model_offset);
            put_u32le(tiff, 34U, 0U);
            std::memcpy(tiff.data() + model_offset, model.data(), model.size());
            tiff[model_offset + model.size()] = std::byte { 0 };
        }
        return tiff;
    }

    static std::vector<std::byte> make_mrw_fixture(
        uint8_t bayer_pattern = 4U, std::string_view tiff_model = {},
        size_t prd_payload_size = 24U, bool append_second_full_prd = false)
    {
        std::vector<std::byte> file(8U, std::byte { 0 });
        file[1] = std::byte { 'M' };
        file[2] = std::byte { 'R' };
        file[3] = std::byte { 'M' };

        const std::vector<std::byte> tiff = make_tiff_payload(tiff_model);
        append_segment(&file, std::string_view("\0TTW", 4U), tiff);

        std::array<std::byte, 24> prd {};
        for (size_t i = 0U; i < 8U; ++i) {
            prd[i] = static_cast<std::byte>(static_cast<uint8_t>('A' + i));
        }
        put_u16be(prd, 8U, 0x0102U);
        put_u16be(prd, 10U, 0x0304U);
        put_u16be(prd, 12U, 0x0506U);
        put_u16be(prd, 14U, 0x0708U);
        prd[16] = std::byte { 12 };
        prd[17] = std::byte { 14 };
        prd[18] = std::byte { 2 };
        prd[23] = static_cast<std::byte>(bayer_pattern);
        append_segment(&file, std::string_view("\0PRD", 4U),
                       std::span<const std::byte>(prd).first(
                           prd_payload_size < prd.size() ? prd_payload_size
                                                         : prd.size()));
        if (append_second_full_prd) {
            std::array<std::byte, 8> second_prd {};
            for (size_t i = 0U; i < second_prd.size(); ++i) {
                second_prd[i] = static_cast<std::byte>(
                    static_cast<uint8_t>('I' + i));
            }
            append_segment(&file, std::string_view("\0PRD", 4U), second_prd);
        }

        std::array<std::byte, 12> wbg {};
        wbg[0] = std::byte { 1 };
        wbg[1] = std::byte { 2 };
        wbg[2] = std::byte { 3 };
        wbg[3] = std::byte { 4 };
        put_u16be(wbg, 4U, 0x0102U);
        put_u16be(wbg, 6U, 0x0304U);
        put_u16be(wbg, 8U, 0x0506U);
        put_u16be(wbg, 10U, 0x0708U);
        append_segment(&file, std::string_view("\0WBG", 4U), wbg);

        std::array<std::byte, 84> rif {};
        rif[1] = std::byte { 0xfe };
        rif[2] = std::byte { 0x80 };
        rif[3] = std::byte { 0x7f };
        rif[4] = std::byte { 0x11 };
        rif[5] = std::byte { 0x22 };
        rif[6] = std::byte { 0x33 };
        rif[7] = std::byte { 0x44 };
        put_u16be(rif, 8U, 0x0123U);
        put_u16be(rif, 10U, 0x0456U);
        put_u16be(rif, 12U, 0x0789U);
        put_u16be(rif, 14U, 0x0abcU);
        put_u16be(rif, 16U, 0x0defU);
        put_u16be(rif, 18U, 0x1020U);
        put_u16be(rif, 20U, 0x3040U);
        put_u16be(rif, 22U, 0x5060U);
        put_u16be(rif, 24U, 0x7080U);
        put_u16be(rif, 26U, 0x90a0U);
        put_u16be(rif, 28U, 0xb0c0U);
        put_u16be(rif, 30U, 0xd0e0U);
        put_u16be(rif, 32U, 0x2000U);
        put_u16be(rif, 34U, 0x2001U);
        put_u16be(rif, 36U, 0x2002U);
        put_u16be(rif, 38U, 0x2003U);
        put_u16be(rif, 40U, 0x2004U);
        put_u16be(rif, 42U, 0x2005U);
        put_u16be(rif, 44U, 0x2006U);
        put_u16be(rif, 46U, 0x2007U);
        rif[56] = std::byte { 0xff };
        rif[57] = std::byte { 0x55 };
        rif[58] = std::byte { 0x66 };
        rif[59] = std::byte { 0x80 };
        rif[60] = std::byte { 0x77 };
        rif[74] = std::byte { 0x4a };
        rif[76] = std::byte { 0x4c };
        rif[77] = std::byte { 0x4d };
        rif[78] = std::byte { 0x4e };
        rif[79] = std::byte { 0x4f };
        put_u32be(rif, 80U, 0x12345678U);
        append_segment(&file, std::string_view("\0RIF", 4U), rif);

        const std::array<std::byte, 3> padding
            = { std::byte { 0xaa }, std::byte { 0xbb }, std::byte { 0xcc } };
        append_segment(&file, std::string_view("\0CSA", 4U), padding);

        const uint32_t metadata_size = static_cast<uint32_t>(file.size() - 8U);
        put_u32be(file, 4U, metadata_size);

        // This is pixel-tail data shaped like a valid RIF segment. The native
        // decoder must stop at the declared metadata end above.
        const std::array<std::byte, 61> pixel_rif = {};
        append_segment(&file, std::string_view("\0RIF", 4U), pixel_rif);
        return file;
    }

    static void put_u16le_if_range(std::span<std::byte> bytes, size_t offset,
                                   uint16_t value)
    {
        if (offset <= bytes.size() && 2U <= bytes.size() - offset) {
            put_u16le(bytes, offset, value);
        }
    }

    static void put_u32le_if_range(std::span<std::byte> bytes, size_t offset,
                                   uint32_t value)
    {
        if (offset <= bytes.size() && 4U <= bytes.size() - offset) {
            put_u32le(bytes, offset, value);
        }
    }

    static std::vector<std::byte> make_mri_fixture(bool include_prd,
                                                   size_t rif_size = 84U)
    {
        std::vector<std::byte> file(8U, std::byte { 0 });
        file[1] = std::byte { 'M' };
        file[2] = std::byte { 'R' };
        file[3] = std::byte { 'I' };

        if (include_prd) {
            std::array<std::byte, 24> prd {};
            for (size_t i = 0U; i < 8U; ++i) {
                prd[i] = static_cast<std::byte>(static_cast<uint8_t>('A' + i));
            }
            put_u16le(prd, 8U, 0x0102U);
            put_u16le(prd, 10U, 0x0304U);
            put_u16le(prd, 12U, 0x0506U);
            put_u16le(prd, 14U, 0x0708U);
            prd[16] = std::byte { 12 };
            prd[17] = std::byte { 14 };
            prd[18] = std::byte { 2 };
            prd[23] = std::byte { 4 };
            append_segment(&file, std::string_view("\0PRD", 4U), prd, true);
        }

        std::array<std::byte, 12> wbg {};
        wbg[0] = std::byte { 1 };
        wbg[1] = std::byte { 2 };
        wbg[2] = std::byte { 3 };
        wbg[3] = std::byte { 4 };
        put_u16le(wbg, 4U, 0x0102U);
        put_u16le(wbg, 6U, 0x0304U);
        put_u16le(wbg, 8U, 0x0506U);
        put_u16le(wbg, 10U, 0x0708U);
        append_segment(&file, std::string_view("\0WBG", 4U), wbg, true);

        std::vector<std::byte> rif(rif_size, std::byte { 0 });
        if (rif.size() > 7U) {
            rif[1] = std::byte { 0xfe };
            rif[2] = std::byte { 0x80 };
            rif[3] = std::byte { 0x7f };
            rif[4] = std::byte { 0x11 };
            rif[5] = std::byte { 0x22 };
            rif[6] = std::byte { 0x33 };
            rif[7] = std::byte { 0x44 };
        }
        for (size_t i = 0U; i < 6U; ++i) {
            const size_t offset = 8U + (i * 4U);
            put_u16le_if_range(rif, offset,
                               static_cast<uint16_t>(0x1000U + i * 2U));
            put_u16le_if_range(rif, offset + 2U,
                               static_cast<uint16_t>(0x1001U + i * 2U));
        }
        for (size_t i = 0U; i < 4U; ++i) {
            const size_t offset = 32U + (i * 4U);
            put_u16le_if_range(rif, offset,
                               static_cast<uint16_t>(0x2000U + i * 2U));
            put_u16le_if_range(rif, offset + 2U,
                               static_cast<uint16_t>(0x2001U + i * 2U));
        }
        if (rif.size() > 60U) {
            rif[56] = std::byte { 0xfe };
            rif[57] = std::byte { 0x55 };
            rif[58] = std::byte { 0x66 };
            rif[59] = std::byte { 0x80 };
            rif[60] = std::byte { 0x77 };
        }
        if (rif.size() > 80U) {
            rif[74] = std::byte { 0x4a };
            rif[76] = std::byte { 0x4c };
            rif[77] = std::byte { 0x4d };
            rif[78] = std::byte { 0x4e };
            rif[79] = std::byte { 0x4f };
        }
        put_u32le_if_range(rif, 80U, 0x12345678U);
        append_segment(&file, std::string_view("\0RIF", 4U), rif, true);

        const std::array<std::byte, 3> padding
            = { std::byte { 0xaa }, std::byte { 0xbb }, std::byte { 0xcc } };
        append_segment(&file, std::string_view("\0CSA", 4U), padding, true);
        put_u32le(file, 4U, static_cast<uint32_t>(file.size() - 8U));
        return file;
    }

    static bool add_context_text(MetaStore& store, BlockId block,
                                 uint32_t order, uint16_t tag,
                                 std::string_view text)
    {
        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), "ifd0", tag);
        entry.origin.block = block;
        entry.origin.order_in_block = order;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 2U };
        entry.origin.wire_count     = static_cast<uint32_t>(text.size());
        entry.value = make_text(store.arena(), text, TextEncoding::Ascii);
        return store.add_entry(entry) != kInvalidEntryId;
    }

    static const Entry* find_entry(const MetaStore& store, std::string_view ifd,
                                   uint16_t tag)
    {
        const std::span<const EntryId> ids = store.find_all(
            make_exif_tag_key_view(ifd, tag));
        if (ids.size() != 1U) {
            return nullptr;
        }
        return &store.entry(ids[0]);
    }

    static void expect_u16_array(const MetaStore& store, const Entry& entry,
                                 std::span<const uint16_t> expected)
    {
        EXPECT_EQ(entry.value.kind, MetaValueKind::Array);
        EXPECT_EQ(entry.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(entry.value.count, expected.size());
        const std::span<const std::byte> raw = store.arena().span(
            entry.value.data.span);
        ASSERT_EQ(raw.size(), expected.size_bytes());
        std::vector<uint16_t> values(expected.size());
        std::memcpy(values.data(), raw.data(), raw.size());
        for (size_t i = 0U; i < expected.size(); ++i) {
            EXPECT_EQ(values[i], expected[i]);
        }
    }

}  // namespace

TEST(MrwDecode, DecodesGroupedPrdWbgRifFieldsAndStopsAtMetadataEnd)
{
    const std::vector<std::byte> file = make_mrw_fixture();
    ASSERT_TRUE(mrw_internal::looks_like_mrw(file));

    MetaStore store;
    const ExifDecodeResult result
        = mrw_internal::decode_mrw_native(file, store, ExifDecodeLimits {});
    EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
    EXPECT_EQ(result.entries_decoded, 29U);
    store.finalize();

    const Entry* firmware = find_entry(store, "mk_minoltaraw_prd_0", 0x0000U);
    ASSERT_NE(firmware, nullptr);
    EXPECT_EQ(firmware->value.kind, MetaValueKind::Text);
    EXPECT_EQ(firmware->value.text_encoding, TextEncoding::Ascii);
    EXPECT_EQ(firmware->value.count, 8U);
    const std::span<const std::byte> firmware_bytes = store.arena().span(
        firmware->value.data.span);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                   firmware_bytes.data()),
                               firmware_bytes.size()),
              "ABCDEFGH");

    const Entry* sensor_height = find_entry(store, "mk_minoltaraw_prd_0",
                                            0x0008U);
    ASSERT_NE(sensor_height, nullptr);
    EXPECT_EQ(sensor_height->value.elem_type, MetaElementType::U16);
    EXPECT_EQ(sensor_height->value.data.u64, 0x0102U);
    const Entry* bayer = find_entry(store, "mk_minoltaraw_prd_0", 0x0017U);
    ASSERT_NE(bayer, nullptr);
    EXPECT_EQ(bayer->value.data.u64, 4U);

    const Entry* scales = find_entry(store, "mk_minoltaraw_wbg_0", 0x0000U);
    ASSERT_NE(scales, nullptr);
    EXPECT_EQ(scales->value.kind, MetaValueKind::Array);
    EXPECT_EQ(scales->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(scales->value.count, 4U);
    const std::span<const std::byte> scale_bytes = store.arena().span(
        scales->value.data.span);
    ASSERT_EQ(scale_bytes.size(), 4U);
    EXPECT_EQ(static_cast<uint8_t>(scale_bytes[0]), 1U);
    EXPECT_EQ(static_cast<uint8_t>(scale_bytes[3]), 4U);
    const Entry* gains = find_entry(store, "mk_minoltaraw_wbg_0", 0x0004U);
    ASSERT_NE(gains, nullptr);
    EXPECT_EQ(exif_tag_name("mk_minoltaraw_wbg_0", 0x0004U), "WB_RGGBLevels");
    EXPECT_EQ(exif_entry_name(store, *gains, ExifTagNamePolicy::ExifToolCompat),
              "WB_RGGBLevels");
    const std::array<uint16_t, 4> expected_gains = { 0x0102U, 0x0304U, 0x0506U,
                                                     0x0708U };
    expect_u16_array(store, *gains, expected_gains);

    const std::vector<std::byte> rggb_file = make_mrw_fixture(1U);
    MetaStore rggb_store;
    const ExifDecodeResult rggb_result
        = mrw_internal::decode_mrw_native(rggb_file, rggb_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(rggb_result.status, ExifDecodeStatus::Ok);
    rggb_store.finalize();
    const Entry* rggb_gains = find_entry(rggb_store, "mk_minoltaraw_wbg_0",
                                         0x0004U);
    ASSERT_NE(rggb_gains, nullptr);
    EXPECT_EQ(exif_entry_name(rggb_store, *rggb_gains,
                              ExifTagNamePolicy::ExifToolCompat),
              "WB_RGGBLevels");

    const Entry* saturation = find_entry(store, "mk_minoltaraw_rif_0", 0x0001U);
    ASSERT_NE(saturation, nullptr);
    EXPECT_EQ(saturation->value.elem_type, MetaElementType::I8);
    EXPECT_EQ(saturation->value.data.i64, -2);
    const Entry* color_filter = find_entry(store, "mk_minoltaraw_rif_0",
                                           0x0038U);
    ASSERT_NE(color_filter, nullptr);
    EXPECT_EQ(color_filter->value.elem_type, MetaElementType::I8);
    EXPECT_EQ(color_filter->value.data.i64, -1);
    const Entry* hue = find_entry(store, "mk_minoltaraw_rif_0", 0x003bU);
    ASSERT_NE(hue, nullptr);
    EXPECT_EQ(hue->value.elem_type, MetaElementType::I8);
    EXPECT_EQ(hue->value.data.i64, -128);
    const Entry* mode = find_entry(store, "mk_minoltaraw_rif_0", 0x0004U);
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(mode->value.data.u64, 0x11U);
    const Entry* temperature = find_entry(store, "mk_minoltaraw_rif_0",
                                          0x003cU);
    ASSERT_NE(temperature, nullptr);
    EXPECT_EQ(temperature->value.data.u64, 0x77U);
    const Entry* tungsten = find_entry(store, "mk_minoltaraw_rif_0", 0x0008U);
    ASSERT_NE(tungsten, nullptr);
    const std::array<uint16_t, 2> expected_tungsten = { 0x0123U, 0x0456U };
    expect_u16_array(store, *tungsten, expected_tungsten);

    EXPECT_EQ(store.block_count(), 3U);
    EXPECT_NE(firmware->origin.block, gains->origin.block);
    EXPECT_NE(gains->origin.block, tungsten->origin.block);
    for (const Entry& entry : store.entries()) {
        EXPECT_EQ(entry.origin.wire_type.family, WireFamily::Other);
        EXPECT_EQ(entry.origin.wire_type.code, 0U);
        EXPECT_LT(entry.origin.block, 3U);
    }
    EXPECT_EQ(
        store.find_all(make_exif_tag_key_view("mk_minoltaraw_rif_1", 0x0001U))
            .size(),
        0U);
}

TEST(MrwDecode, StandaloneMrmPreservesBigEndianA100RawNumbers)
{
    const std::vector<std::byte> file = make_mrw_fixture();
    MetaStore store;
    const BlockId context_block = store.add_block(BlockInfo {});
    ASSERT_NE(context_block, kInvalidBlockId);
    ASSERT_TRUE(add_context_text(store, context_block, 0U, 0x010fU, "SONY"));
    ASSERT_TRUE(
        add_context_text(store, context_block, 1U, 0x0110U, "DSLR-A100"));

    const ExifDecodeResult result
        = mrw_internal::decode_mrw_native(file, store, ExifDecodeLimits {});
    EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
    store.finalize();

    const Entry* a100_wb = find_entry(store, "mk_minoltaraw_rif_0", 0x0020U);
    ASSERT_NE(a100_wb, nullptr);
    const std::array<uint16_t, 2> expected_wb = { 0x2000U, 0x2001U };
    expect_u16_array(store, *a100_wb, expected_wb);
    const Entry* raw_length = find_entry(store, "mk_minoltaraw_rif_0", 0x0050U);
    ASSERT_NE(raw_length, nullptr);
    EXPECT_EQ(raw_length->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(raw_length->value.data.u64, 0x12345678U);
    const Entry* temperature = find_entry(store, "mk_minoltaraw_rif_0",
                                          0x004cU);
    ASSERT_NE(temperature, nullptr);
    EXPECT_EQ(temperature->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(temperature->value.data.u64, 0x4cU);
}

TEST(MrwDecode, StandalonePrdGateRequiresFullFirmwareIdAndScansPastShortPrd)
{
    const std::vector<std::byte> short_prd_file = make_mrw_fixture(4U, {}, 7U);
    MetaStore short_prd_store;
    const ExifDecodeResult short_prd_result
        = mrw_internal::decode_mrw_native(short_prd_file, short_prd_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(short_prd_result.status, ExifDecodeStatus::Ok);
    short_prd_store.finalize();
    EXPECT_EQ(find_entry(short_prd_store, "mk_minoltaraw_rif_0", 0x0008U),
              nullptr);

    const std::vector<std::byte> full_prd_file = make_mrw_fixture(4U, {}, 8U);
    MetaStore full_prd_store;
    const ExifDecodeResult full_prd_result
        = mrw_internal::decode_mrw_native(full_prd_file, full_prd_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(full_prd_result.status, ExifDecodeStatus::Ok);
    full_prd_store.finalize();
    EXPECT_NE(find_entry(full_prd_store, "mk_minoltaraw_rif_0", 0x0008U),
              nullptr);

    const std::vector<std::byte> short_then_full_prd_file
        = make_mrw_fixture(4U, {}, 7U, true);
    MetaStore short_then_full_prd_store;
    const ExifDecodeResult short_then_full_prd_result
        = mrw_internal::decode_mrw_native(short_then_full_prd_file,
                                          short_then_full_prd_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(short_then_full_prd_result.status, ExifDecodeStatus::Ok);
    short_then_full_prd_store.finalize();
    EXPECT_NE(find_entry(short_then_full_prd_store, "mk_minoltaraw_rif_0",
                         0x0008U),
              nullptr);
    EXPECT_NE(find_entry(short_then_full_prd_store, "mk_minoltaraw_prd_1",
                         0x0000U),
              nullptr);
}

TEST(MrwDecode, EmbeddedMriUsesSonyA100RifFieldsAndLittleEndianWords)
{
    const std::vector<std::byte> file = make_mri_fixture(true);
    MetaStore store;
    const BlockId context_block = store.add_block(BlockInfo {});
    ASSERT_NE(context_block, kInvalidBlockId);
    ASSERT_TRUE(add_context_text(store, context_block, 0U, 0x010fU, "SONY"));
    ASSERT_TRUE(
        add_context_text(store, context_block, 1U, 0x0110U, "DSLR-A100"));

    const ExifDecodeResult result
        = mrw_internal::decode_mrw_native(file, store, ExifDecodeLimits {});
    EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
    store.finalize();

    const Entry* common = find_entry(store, "mk_minoltaraw_rif_0", 0x0008U);
    ASSERT_NE(common, nullptr);
    const std::array<uint16_t, 2> expected_common = { 0x1000U, 0x1001U };
    expect_u16_array(store, *common, expected_common);

    const Entry* tungsten = find_entry(store, "mk_minoltaraw_rif_0", 0x0020U);
    ASSERT_NE(tungsten, nullptr);
    const std::array<uint16_t, 2> expected_tungsten = { 0x2000U, 0x2001U };
    expect_u16_array(store, *tungsten, expected_tungsten);
    const Entry* shade = find_entry(store, "mk_minoltaraw_rif_0", 0x002cU);
    ASSERT_NE(shade, nullptr);
    const std::array<uint16_t, 2> expected_shade = { 0x2006U, 0x2007U };
    expect_u16_array(store, *shade, expected_shade);

    const Entry* sony_zone = find_entry(store, "mk_minoltaraw_rif_0", 0x004aU);
    ASSERT_NE(sony_zone, nullptr);
    EXPECT_EQ(sony_zone->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(sony_zone->value.data.u64, 0x4aU);
    const Entry* a100_temperature = find_entry(store, "mk_minoltaraw_rif_0",
                                               0x004cU);
    ASSERT_NE(a100_temperature, nullptr);
    EXPECT_EQ(a100_temperature->value.data.u64, 0x4cU);
    const Entry* a100_filter = find_entry(store, "mk_minoltaraw_rif_0",
                                          0x004dU);
    ASSERT_NE(a100_filter, nullptr);
    EXPECT_EQ(a100_filter->value.data.u64, 0x4dU);
    const Entry* raw_length = find_entry(store, "mk_minoltaraw_rif_0", 0x0050U);
    ASSERT_NE(raw_length, nullptr);
    EXPECT_EQ(raw_length->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(raw_length->value.data.u64, 0x12345678U);

    const Entry* color_mode = find_entry(store, "mk_minoltaraw_rif_0", 0x0007U);
    ASSERT_NE(color_mode, nullptr);
    EXPECT_EQ(color_mode->value.data.u64, 0x44U);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x0038U), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x003aU), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x003cU), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x004eU), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x004fU), nullptr);
}

TEST(MrwDecode, EmbeddedMriAppliesModelAndMakeNegativeControls)
{
    const std::vector<std::byte> file = make_mri_fixture(true);

    MetaStore sony_a200_store;
    const BlockId sony_a200_context = sony_a200_store.add_block(BlockInfo {});
    ASSERT_NE(sony_a200_context, kInvalidBlockId);
    ASSERT_TRUE(add_context_text(sony_a200_store, sony_a200_context, 0U,
                                 0x010fU, "SONY"));
    ASSERT_TRUE(add_context_text(sony_a200_store, sony_a200_context, 1U,
                                 0x0110U, "DSLR-A200"));
    const ExifDecodeResult sony_a200_result
        = mrw_internal::decode_mrw_native(file, sony_a200_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(sony_a200_result.status, ExifDecodeStatus::Ok);
    sony_a200_store.finalize();
    EXPECT_EQ(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x0008U),
              nullptr);
    EXPECT_EQ(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x001cU),
              nullptr);
    EXPECT_EQ(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x0020U),
              nullptr);
    EXPECT_EQ(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x004cU),
              nullptr);
    EXPECT_EQ(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x004dU),
              nullptr);
    EXPECT_EQ(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x0050U),
              nullptr);
    const Entry* a200_temperature = find_entry(sony_a200_store,
                                               "mk_minoltaraw_rif_0", 0x004eU);
    ASSERT_NE(a200_temperature, nullptr);
    EXPECT_EQ(a200_temperature->value.data.u64, 0x4eU);
    const Entry* a200_filter = find_entry(sony_a200_store,
                                          "mk_minoltaraw_rif_0", 0x004fU);
    ASSERT_NE(a200_filter, nullptr);
    EXPECT_EQ(a200_filter->value.data.u64, 0x4fU);
    EXPECT_NE(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x004aU),
              nullptr);
    EXPECT_EQ(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x0007U),
              nullptr);
    EXPECT_EQ(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x0038U),
              nullptr);
    EXPECT_EQ(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x003aU),
              nullptr);
    EXPECT_EQ(find_entry(sony_a200_store, "mk_minoltaraw_rif_0", 0x003cU),
              nullptr);

    MetaStore sony_a700_store;
    const BlockId sony_a700_context = sony_a700_store.add_block(BlockInfo {});
    ASSERT_NE(sony_a700_context, kInvalidBlockId);
    ASSERT_TRUE(add_context_text(sony_a700_store, sony_a700_context, 0U,
                                 0x010fU, "SONY"));
    ASSERT_TRUE(add_context_text(sony_a700_store, sony_a700_context, 1U,
                                 0x0110U, "DSLR-A700"));
    const ExifDecodeResult sony_a700_result
        = mrw_internal::decode_mrw_native(file, sony_a700_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(sony_a700_result.status, ExifDecodeStatus::Ok);
    sony_a700_store.finalize();
    EXPECT_NE(find_entry(sony_a700_store, "mk_minoltaraw_rif_0", 0x004eU),
              nullptr);
    EXPECT_NE(find_entry(sony_a700_store, "mk_minoltaraw_rif_0", 0x004fU),
              nullptr);
    EXPECT_EQ(find_entry(sony_a700_store, "mk_minoltaraw_rif_0", 0x0008U),
              nullptr);
    EXPECT_EQ(find_entry(sony_a700_store, "mk_minoltaraw_rif_0", 0x001cU),
              nullptr);
    EXPECT_EQ(find_entry(sony_a700_store, "mk_minoltaraw_rif_0", 0x004cU),
              nullptr);
    EXPECT_EQ(find_entry(sony_a700_store, "mk_minoltaraw_rif_0", 0x0050U),
              nullptr);

    MetaStore sony_a200_suffix_store;
    const BlockId sony_a200_suffix_context = sony_a200_suffix_store.add_block(
        BlockInfo {});
    ASSERT_NE(sony_a200_suffix_context, kInvalidBlockId);
    ASSERT_TRUE(add_context_text(sony_a200_suffix_store,
                                 sony_a200_suffix_context, 0U, 0x010fU,
                                 "SONY"));
    ASSERT_TRUE(add_context_text(sony_a200_suffix_store,
                                 sony_a200_suffix_context, 1U, 0x0110U,
                                 "DSLR-A200X"));
    const ExifDecodeResult sony_a200_suffix_result
        = mrw_internal::decode_mrw_native(file, sony_a200_suffix_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(sony_a200_suffix_result.status, ExifDecodeStatus::Ok);
    sony_a200_suffix_store.finalize();
    EXPECT_EQ(find_entry(sony_a200_suffix_store, "mk_minoltaraw_rif_0", 0x004eU),
              nullptr);
    EXPECT_EQ(find_entry(sony_a200_suffix_store, "mk_minoltaraw_rif_0", 0x004fU),
              nullptr);

    MetaStore sony_a700_suffix_store;
    const BlockId sony_a700_suffix_context = sony_a700_suffix_store.add_block(
        BlockInfo {});
    ASSERT_NE(sony_a700_suffix_context, kInvalidBlockId);
    ASSERT_TRUE(add_context_text(sony_a700_suffix_store,
                                 sony_a700_suffix_context, 0U, 0x010fU,
                                 "SONY"));
    ASSERT_TRUE(add_context_text(sony_a700_suffix_store,
                                 sony_a700_suffix_context, 1U, 0x0110U,
                                 "DSLR-A7000"));
    const ExifDecodeResult sony_a700_suffix_result
        = mrw_internal::decode_mrw_native(file, sony_a700_suffix_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(sony_a700_suffix_result.status, ExifDecodeStatus::Ok);
    sony_a700_suffix_store.finalize();
    EXPECT_EQ(find_entry(sony_a700_suffix_store, "mk_minoltaraw_rif_0", 0x004eU),
              nullptr);
    EXPECT_EQ(find_entry(sony_a700_suffix_store, "mk_minoltaraw_rif_0", 0x004fU),
              nullptr);

    MetaStore minolta_a200_store;
    const BlockId minolta_context = minolta_a200_store.add_block(BlockInfo {});
    ASSERT_NE(minolta_context, kInvalidBlockId);
    ASSERT_TRUE(add_context_text(minolta_a200_store, minolta_context, 0U,
                                 0x010fU, "KONICA MINOLTA"));
    ASSERT_TRUE(add_context_text(minolta_a200_store, minolta_context, 1U,
                                 0x0110U, "DiMAGE A200"));
    const ExifDecodeResult minolta_result
        = mrw_internal::decode_mrw_native(file, minolta_a200_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(minolta_result.status, ExifDecodeStatus::Ok);
    minolta_a200_store.finalize();
    EXPECT_EQ(find_entry(minolta_a200_store, "mk_minoltaraw_rif_0", 0x0020U),
              nullptr);
    EXPECT_EQ(find_entry(minolta_a200_store, "mk_minoltaraw_rif_0", 0x004cU),
              nullptr);
    EXPECT_EQ(find_entry(minolta_a200_store, "mk_minoltaraw_rif_0", 0x004eU),
              nullptr);
    EXPECT_EQ(find_entry(minolta_a200_store, "mk_minoltaraw_rif_0", 0x004aU),
              nullptr);
    const Entry* minolta_filter = find_entry(minolta_a200_store,
                                             "mk_minoltaraw_rif_0", 0x0038U);
    ASSERT_NE(minolta_filter, nullptr);
    EXPECT_EQ(minolta_filter->value.elem_type, MetaElementType::I8);
    EXPECT_EQ(minolta_filter->value.data.i64, -2);
    const Entry* minolta_zone = find_entry(minolta_a200_store,
                                           "mk_minoltaraw_rif_0", 0x003aU);
    ASSERT_NE(minolta_zone, nullptr);
    EXPECT_EQ(minolta_zone->value.data.u64, 0x66U);
    const Entry* minolta_temperature
        = find_entry(minolta_a200_store, "mk_minoltaraw_rif_0", 0x003cU);
    ASSERT_NE(minolta_temperature, nullptr);
    EXPECT_EQ(minolta_temperature->value.data.u64, 0x77U);
    EXPECT_NE(find_entry(minolta_a200_store, "mk_minoltaraw_rif_0", 0x0007U),
              nullptr);
}

TEST(MrwDecode, EmbeddedMriDoesNotInferAbsentMakeOrModel)
{
    const std::vector<std::byte> file = make_mri_fixture(false);
    MetaStore store;
    const ExifDecodeResult result
        = mrw_internal::decode_mrw_native(file, store, ExifDecodeLimits {});
    EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
    store.finalize();
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x0007U), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x0008U), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x0038U), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x003aU), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x003cU), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x004aU), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x004cU), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x004eU), nullptr);
}

TEST(MrwDecode, SkipsTruncatedA100ArraysBeforeEntryLimitChecks)
{
    const std::vector<std::byte> file = make_mri_fixture(false, 35U);
    MetaStore store;
    const BlockId context_block = store.add_block(BlockInfo {});
    ASSERT_NE(context_block, kInvalidBlockId);
    ASSERT_TRUE(add_context_text(store, context_block, 0U, 0x010fU, "SONY"));
    ASSERT_TRUE(
        add_context_text(store, context_block, 1U, 0x0110U, "DSLR-A100"));

    ExifDecodeLimits limits;
    limits.max_entries_per_ifd    = 13U;
    const ExifDecodeResult result = mrw_internal::decode_mrw_native(file, store,
                                                                    limits);
    EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
    store.finalize();
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x0020U), nullptr);
    EXPECT_EQ(find_entry(store, "mk_minoltaraw_rif_0", 0x0050U), nullptr);
}

TEST(MrwDecode, SimpleMetaOptionGatesNativeGroupsAndKeepsTtwExif)
{
    const std::vector<std::byte> file = make_mrw_fixture(4U, "DiMAGE A200");
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::array<std::byte, 64> payload {};
    std::array<uint32_t, 16> payload_indices {};
    SimpleMetaDecodeOptions options;
    options.exif.decode_makernote = true;

    MetaStore native_store;
    const SimpleMetaResult native_result
        = simple_meta_read(file, native_store, blocks, ifds, payload,
                           payload_indices, options);
    EXPECT_EQ(native_result.scan.status, ScanStatus::Ok);
    EXPECT_EQ(native_result.exif.status, ExifDecodeStatus::Ok);
    native_store.finalize();
    EXPECT_NE(find_entry(native_store, "mk_minoltaraw_prd_0", 0x0008U),
              nullptr);
    const Entry* tiff_width = find_entry(native_store, "ifd0", 0x0100U);
    ASSERT_NE(tiff_width, nullptr);
    EXPECT_EQ(tiff_width->value.data.u64, 640U);

    const MetadataQueryResult raw_query = query_raw_processing_metadata(
        native_store);
    bool saw_minolta_prd = false;
    bool saw_a200_wbg    = false;
    for (size_t i = 0U; i < raw_query.matches.size(); ++i) {
        const MetadataQueryMatch& match = raw_query.matches[i];
        if (match.group == "mk_minoltaraw_prd_0"
            && match.name == "SensorHeight") {
            saw_minolta_prd = true;
        }
        if (match.group == "mk_minoltaraw_wbg_0"
            && match.name == "WB_GBRGLevels") {
            saw_a200_wbg = true;
        }
    }
    EXPECT_TRUE(saw_minolta_prd);
    EXPECT_TRUE(saw_a200_wbg);

    options.exif.decode_makernote = false;
    MetaStore ordinary_store;
    const SimpleMetaResult ordinary_result
        = simple_meta_read(file, ordinary_store, blocks, ifds, payload,
                           payload_indices, options);
    EXPECT_EQ(ordinary_result.scan.status, ScanStatus::Ok);
    EXPECT_EQ(ordinary_result.exif.status, ExifDecodeStatus::Ok);
    ordinary_store.finalize();
    EXPECT_EQ(find_entry(ordinary_store, "mk_minoltaraw_prd_0", 0x0008U),
              nullptr);
    const Entry* ordinary_tiff_width = find_entry(ordinary_store, "ifd0",
                                                  0x0100U);
    ASSERT_NE(ordinary_tiff_width, nullptr);
    EXPECT_EQ(ordinary_tiff_width->value.data.u64, 640U);
}

TEST(MrwDecode, EnforcesEntryAndValueLimitsAndRejectsInvalidDeclaredBounds)
{
    const std::vector<std::byte> file = make_mrw_fixture();

    MetaStore entry_limited_store;
    ExifDecodeLimits entry_limits;
    entry_limits.max_total_entries = 3U;
    const ExifDecodeResult entry_result
        = mrw_internal::decode_mrw_native(file, entry_limited_store,
                                          entry_limits);
    EXPECT_EQ(entry_result.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(entry_result.limit_reason, ExifLimitReason::MaxTotalEntries);
    EXPECT_EQ(entry_result.entries_decoded, 3U);

    MetaStore ifd_limited_store;
    ExifDecodeLimits ifd_limits;
    ifd_limits.max_entries_per_ifd = 2U;
    const ExifDecodeResult ifd_result
        = mrw_internal::decode_mrw_native(file, ifd_limited_store, ifd_limits);
    EXPECT_EQ(ifd_result.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(ifd_result.limit_reason, ExifLimitReason::MaxEntriesPerIfd);
    EXPECT_EQ(ifd_result.entries_decoded, 2U);

    MetaStore block_limited_store;
    ExifDecodeLimits block_limits;
    block_limits.max_ifds = 1U;
    const ExifDecodeResult block_result
        = mrw_internal::decode_mrw_native(file, block_limited_store,
                                          block_limits);
    EXPECT_EQ(block_result.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(block_result.limit_reason, ExifLimitReason::MaxIfds);
    EXPECT_EQ(block_limited_store.block_count(), 1U);
    block_limited_store.finalize();
    EXPECT_NE(find_entry(block_limited_store, "mk_minoltaraw_prd_0", 0x0008U),
              nullptr);
    EXPECT_EQ(find_entry(block_limited_store, "mk_minoltaraw_wbg_0", 0x0004U),
              nullptr);

    MetaStore value_limited_store;
    ExifDecodeLimits value_limits;
    value_limits.max_value_bytes = 3U;
    const ExifDecodeResult value_result
        = mrw_internal::decode_mrw_native(file, value_limited_store,
                                          value_limits);
    EXPECT_EQ(value_result.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(value_result.limit_reason, ExifLimitReason::ValueCountTooLarge);
    value_limited_store.finalize();
    EXPECT_EQ(find_entry(value_limited_store, "mk_minoltaraw_prd_0", 0x0000U),
              nullptr);

    MetaStore arena_limited_store;
    ExifDecodeLimits arena_limits;
    arena_limits.max_arena_bytes = 1U;
    const ExifDecodeResult arena_result
        = mrw_internal::decode_mrw_native(file, arena_limited_store,
                                          arena_limits);
    EXPECT_EQ(arena_result.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(arena_result.limit_reason, ExifLimitReason::MaxArenaBytes);
    EXPECT_EQ(arena_limited_store.entries().size(), 0U);

    std::vector<std::byte> wbg_only(8U, std::byte { 0 });
    wbg_only[1]                                 = std::byte { 'M' };
    wbg_only[2]                                 = std::byte { 'R' };
    wbg_only[3]                                 = std::byte { 'M' };
    const std::array<std::byte, 12> wbg_payload = {};
    append_segment(&wbg_only, std::string_view("\0WBG", 4U), wbg_payload);
    put_u32be(wbg_only, 4U, static_cast<uint32_t>(wbg_only.size() - 8U));
    MetaStore array_arena_limited_store;
    const ExifDecodeResult array_arena_result
        = mrw_internal::decode_mrw_native(wbg_only, array_arena_limited_store,
                                          arena_limits);
    EXPECT_EQ(array_arena_result.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(array_arena_result.limit_reason, ExifLimitReason::MaxArenaBytes);
    EXPECT_EQ(array_arena_limited_store.entries().size(), 0U);

    std::vector<std::byte> invalid = file;
    put_u32be(invalid, 4U, static_cast<uint32_t>(invalid.size()));
    MetaStore invalid_store;
    const ExifDecodeResult invalid_result
        = mrw_internal::decode_mrw_native(invalid, invalid_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(invalid_result.status, ExifDecodeStatus::Malformed);
    EXPECT_EQ(invalid_store.entries().size(), 0U);

    const std::array<std::byte, 4> unsupported
        = { std::byte { 'N' }, std::byte { 'O' }, std::byte { 'P' },
            std::byte { 'E' } };
    MetaStore unsupported_store;
    const ExifDecodeResult unsupported_result
        = mrw_internal::decode_mrw_native(unsupported, unsupported_store,
                                          ExifDecodeLimits {});
    EXPECT_EQ(unsupported_result.status, ExifDecodeStatus::Unsupported);
}

}  // namespace openmeta
