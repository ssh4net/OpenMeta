// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

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


    static MetaKeyView printim_key(std::string_view field)
    {
        MetaKeyView key;
        key.kind                     = MetaKeyKind::PrintImField;
        key.data.printim_field.field = field;
        return key;
    }


    static std::string_view arena_string(const ByteArena& arena,
                                         const MetaValue& v)
    {
        const std::span<const std::byte> bytes = arena.span(v.data.span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

}  // namespace

TEST(PrintImDecode, DecodesPrintImTagIntoFields)
{
    // Minimal TIFF with a single PrintIM tag (0xC4A5) in IFD0.
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42);
    append_u32le(&tiff, 8);

    // IFD0 at offset 8: 1 entry.
    append_u16le(&tiff, 1);

    // PrintIM tag: UNDEFINED bytes at offset payload_off.
    const uint32_t payload_off = 26;
    const uint16_t entry_count = 2;
    const uint32_t payload_len = 16 + static_cast<uint32_t>(entry_count) * 6;

    append_u16le(&tiff, 0xC4A5);
    append_u16le(&tiff, 7);
    append_u32le(&tiff, payload_len);
    append_u32le(&tiff, payload_off);

    append_u32le(&tiff, 0);  // next IFD

    ASSERT_EQ(tiff.size(), payload_off);

    // PrintIM payload:
    // "PrintIM\0" + "0300" + reserved(u16=0) + count(u16) + entries(tag,value)
    append_bytes(&tiff, "PrintIM");
    tiff.push_back(std::byte { 0 });
    append_bytes(&tiff, "0300");
    append_u16le(&tiff, 0);
    append_u16le(&tiff, entry_count);
    append_u16le(&tiff, 0x0001);
    append_u32le(&tiff, 0x00160016);
    append_u16le(&tiff, 0x0002);
    append_u32le(&tiff, 0x00000001);

    ASSERT_EQ(tiff.size(), payload_off + payload_len);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_printim = true;

    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    const std::span<const EntryId> version_ids = store.find_all(
        printim_key("version"));
    ASSERT_EQ(version_ids.size(), 1U);
    const Entry& version = store.entry(version_ids[0]);
    EXPECT_EQ(version.value.kind, MetaValueKind::Text);
    EXPECT_EQ(arena_string(store.arena(), version.value), "0300");

    const std::span<const EntryId> id1 = store.find_all(printim_key("0x0001"));
    ASSERT_EQ(id1.size(), 1U);
    const Entry& e1 = store.entry(id1[0]);
    EXPECT_EQ(e1.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e1.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e1.value.data.u64, 0x00160016U);
}


TEST(PrintImDecode, SharesParentEntryBudget)
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42);
    append_u32le(&tiff, 8);
    append_u16le(&tiff, 1);

    const uint32_t payload_off = 26;
    const uint16_t entry_count = 3;
    const uint32_t payload_len = 16 + static_cast<uint32_t>(entry_count) * 6;
    append_u16le(&tiff, 0xC4A5);
    append_u16le(&tiff, 7);
    append_u32le(&tiff, payload_len);
    append_u32le(&tiff, payload_off);
    append_u32le(&tiff, 0);

    append_bytes(&tiff, "PrintIM");
    tiff.push_back(std::byte { 0 });
    append_bytes(&tiff, "0300");
    append_u16le(&tiff, 0);
    append_u16le(&tiff, entry_count);
    for (uint16_t i = 0; i < entry_count; ++i) {
        append_u16le(&tiff, static_cast<uint16_t>(i + 1U));
        append_u32le(&tiff, i + 10U);
    }

    ExifDecodeOptions options;
    options.decode_printim             = true;
    options.limits.max_total_entries   = 2U;
    options.limits.max_entries_per_ifd = 8U;
    MetaStore store;
    const ExifDecodeResult result = decode_exif_tiff(tiff, store, {}, options);

    EXPECT_EQ(result.status, ExifDecodeStatus::LimitExceeded);
    EXPECT_EQ(store.entries().size(), 2U);
    EXPECT_TRUE(store.resource_limit_exceeded());
}

}  // namespace openmeta
