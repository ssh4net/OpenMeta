// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    struct NikonMakerEntry final {
        uint16_t tag          = 0;
        uint16_t type         = 7;
        uint32_t count        = 0;
        uint32_t inline_value = 0;
        std::vector<std::byte> payload;
    };

    static void append_u16le(std::vector<std::byte>* out, uint16_t value)
    {
        out->push_back(std::byte { static_cast<uint8_t>(value & 0xffU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) });
    }

    static void append_u32le(std::vector<std::byte>* out, uint32_t value)
    {
        out->push_back(std::byte { static_cast<uint8_t>(value & 0xffU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 16U) & 0xffU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 24U) & 0xffU) });
    }

    static void append_text(std::vector<std::byte>* out, std::string_view text)
    {
        for (char c : text) {
            out->push_back(std::byte { static_cast<uint8_t>(c) });
        }
    }

    static void write_u16be(std::vector<std::byte>* out, size_t offset,
                            uint16_t value)
    {
        (*out)[offset]
            = std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) };
        (*out)[offset + 1U] = std::byte { static_cast<uint8_t>(value & 0xffU) };
    }

    static void write_u32le(std::vector<std::byte>* out, size_t offset,
                            uint32_t value)
    {
        (*out)[offset] = std::byte { static_cast<uint8_t>(value & 0xffU) };
        (*out)[offset + 1U]
            = std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) };
        (*out)[offset + 2U]
            = std::byte { static_cast<uint8_t>((value >> 16U) & 0xffU) };
        (*out)[offset + 3U]
            = std::byte { static_cast<uint8_t>((value >> 24U) & 0xffU) };
    }

    static std::vector<std::byte>
    make_nikon_makernote(std::span<const NikonMakerEntry> entries)
    {
        std::vector<std::byte> note;
        append_text(&note, "Nikon");
        note.push_back(std::byte { 0 });
        note.push_back(std::byte { 2 });
        note.insert(note.end(), 3U, std::byte { 0 });
        append_text(&note, "II");
        append_u16le(&note, 42U);
        append_u32le(&note, 8U);

        append_u16le(&note, static_cast<uint16_t>(entries.size()));
        const uint32_t payload_start
            = 8U + 2U + static_cast<uint32_t>(entries.size()) * 12U + 4U;
        uint32_t payload_offset = payload_start;
        for (const NikonMakerEntry& item : entries) {
            append_u16le(&note, item.tag);
            append_u16le(&note, item.type);
            append_u32le(&note, item.count);
            if (item.payload.size() > 4U) {
                append_u32le(&note, payload_offset);
                payload_offset += static_cast<uint32_t>(item.payload.size());
            } else {
                append_u32le(&note, item.inline_value);
            }
        }
        append_u32le(&note, 0U);
        for (const NikonMakerEntry& item : entries) {
            if (item.payload.size() > 4U) {
                note.insert(note.end(), item.payload.begin(),
                            item.payload.end());
            }
        }
        return note;
    }

    static std::vector<std::byte>
    make_test_tiff(std::string_view model,
                   std::span<const std::byte> maker_note)
    {
        const uint32_t ifd0_offset  = 8U;
        const uint32_t ifd0_count   = 3U;
        const uint32_t ifd0_size    = 2U + ifd0_count * 12U + 4U;
        const uint32_t make_offset  = ifd0_offset + ifd0_size;
        const uint32_t make_count   = 6U;
        const uint32_t model_offset = make_offset + make_count;
        const uint32_t model_count  = static_cast<uint32_t>(model.size() + 1U);
        const uint32_t exif_offset  = model_offset + model_count;
        const uint32_t exif_size    = 2U + 12U + 4U;
        const uint32_t maker_note_offset = exif_offset + exif_size;

        std::vector<std::byte> tiff;
        append_text(&tiff, "II");
        append_u16le(&tiff, 42U);
        append_u32le(&tiff, ifd0_offset);
        append_u16le(&tiff, static_cast<uint16_t>(ifd0_count));

        append_u16le(&tiff, 0x010fU);
        append_u16le(&tiff, 2U);
        append_u32le(&tiff, make_count);
        append_u32le(&tiff, make_offset);
        append_u16le(&tiff, 0x0110U);
        append_u16le(&tiff, 2U);
        append_u32le(&tiff, model_count);
        append_u32le(&tiff, model_offset);
        append_u16le(&tiff, 0x8769U);
        append_u16le(&tiff, 4U);
        append_u32le(&tiff, 1U);
        append_u32le(&tiff, exif_offset);
        append_u32le(&tiff, 0U);

        append_text(&tiff, "Nikon");
        tiff.push_back(std::byte { 0 });
        append_text(&tiff, model);
        tiff.push_back(std::byte { 0 });

        append_u16le(&tiff, 1U);
        append_u16le(&tiff, 0x927cU);
        append_u16le(&tiff, 7U);
        append_u32le(&tiff, static_cast<uint32_t>(maker_note.size()));
        append_u32le(&tiff, maker_note_offset);
        append_u32le(&tiff, 0U);
        tiff.insert(tiff.end(), maker_note.begin(), maker_note.end());
        return tiff;
    }

    static MetaKeyView exif_key(std::string_view ifd, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }

    static ExifDecodeResult
    decode_nikon_fixture(std::span<const std::byte> maker_note,
                         std::string_view model, MetaStore& store,
                         ExifDecodeOptions options = ExifDecodeOptions {})
    {
        const std::vector<std::byte> tiff = make_test_tiff(model, maker_note);
        std::array<ExifIfdRef, 8> ifds {};
        options.decode_makernote = true;
        return decode_exif_tiff(tiff, store, ifds, options);
    }

    static const Entry* find_one(const MetaStore& store, std::string_view ifd,
                                 uint16_t tag)
    {
        const std::span<const EntryId> ids = store.find_all(exif_key(ifd, tag));
        return ids.size() == 1U ? &store.entry(ids[0]) : nullptr;
    }

    static bool read_u16_element(const MetaStore& store, const Entry& entry,
                                 size_t index, uint16_t* value)
    {
        if (!value || entry.value.kind != MetaValueKind::Array
            || entry.value.elem_type != MetaElementType::U16
            || index >= entry.value.count) {
            return false;
        }
        const std::span<const std::byte> raw = store.arena().span(
            entry.value.data.span);
        const size_t offset = index * sizeof(uint16_t);
        if (offset + sizeof(uint16_t) > raw.size()) {
            return false;
        }
        std::memcpy(value, raw.data() + offset, sizeof(uint16_t));
        return true;
    }

    static bool read_u32_element(const MetaStore& store, const Entry& entry,
                                 size_t index, uint32_t* value)
    {
        if (!value || entry.value.kind != MetaValueKind::Array
            || entry.value.elem_type != MetaElementType::U32
            || index >= entry.value.count) {
            return false;
        }
        const std::span<const std::byte> raw = store.arena().span(
            entry.value.data.span);
        const size_t offset = index * sizeof(uint32_t);
        if (offset + sizeof(uint32_t) > raw.size()) {
            return false;
        }
        std::memcpy(value, raw.data() + offset, sizeof(uint32_t));
        return true;
    }

    static std::string_view text_value(const MetaStore& store,
                                       const Entry& entry)
    {
        if (entry.value.kind != MetaValueKind::Text) {
            return {};
        }
        const std::span<const std::byte> bytes = store.arena().span(
            entry.value.data.span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static NikonMakerEntry maker_bytes_entry(uint16_t tag,
                                             std::span<const std::byte> bytes,
                                             uint32_t count = UINT32_MAX)
    {
        NikonMakerEntry entry;
        entry.tag   = tag;
        entry.type  = 7U;
        entry.count = count == UINT32_MAX ? static_cast<uint32_t>(bytes.size())
                                          : count;
        entry.payload.assign(bytes.begin(), bytes.end());
        return entry;
    }

    static NikonMakerEntry serial_entry()
    {
        NikonMakerEntry entry;
        entry.tag          = 0x001dU;
        entry.type         = 2U;
        entry.count        = 2U;
        entry.inline_value = 0x30U;
        return entry;
    }

    static NikonMakerEntry shutter_entry()
    {
        NikonMakerEntry entry;
        entry.tag          = 0x00a7U;
        entry.type         = 4U;
        entry.count        = 1U;
        entry.inline_value = 0U;
        return entry;
    }

    static std::vector<std::byte>
    encrypt_nikon_payload(std::span<const std::byte> plain)
    {
        std::vector<std::byte> encrypted(plain.begin(), plain.end());
        constexpr uint8_t ci0 = 0xc1U;
        uint8_t cj            = 0xa7U;
        uint8_t ck            = 0x60U;
        for (size_t i = 4U; i < plain.size(); ++i) {
            cj = static_cast<uint8_t>(
                (static_cast<uint32_t>(cj)
                 + static_cast<uint32_t>(ci0) * static_cast<uint32_t>(ck))
                & 0xffU);
            ck = static_cast<uint8_t>((static_cast<uint32_t>(ck) + 1U) & 0xffU);
            encrypted[i] = std::byte { static_cast<uint8_t>(
                static_cast<uint8_t>(plain[i]) ^ cj) };
        }
        return encrypted;
    }

    TEST(NikonRawGap,
         ColorBalanceARequiresExactUndefinedPayloadAndUsesBigEndian)
    {
        std::vector<std::byte> raw(2560U, std::byte { 0 });
        write_u16be(&raw, 0x0270U, 0x9999U);
        write_u16be(&raw, 0x0280U, 0x7777U);
        write_u16be(&raw, 0x04e0U, 0x1234U);
        write_u16be(&raw, 0x04e4U, 0x2345U);
        write_u16be(&raw, 0x04e8U, 0x3456U);
        write_u16be(&raw, 0x04f4U, 0x4567U);
        write_u16be(&raw, 0x0564U, 0x6a6aU);

        std::array<NikonMakerEntry, 1> entries {
            maker_bytes_entry(0x0014U, raw),
        };
        const std::vector<std::byte> maker_note = make_nikon_makernote(entries);
        MetaStore store;
        const ExifDecodeResult result
            = decode_nikon_fixture(maker_note, "NIKON E5400", store);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        store.finalize();

        const Entry* as_shot = find_one(store, "mk_nikon_colorbalancea_0",
                                        0x0270U);
        ASSERT_NE(as_shot, nullptr);
        ASSERT_EQ(as_shot->value.count, 2U);
        uint16_t level = 0;
        ASSERT_TRUE(read_u16_element(store, *as_shot, 0U, &level));
        EXPECT_EQ(level, 0x1234U);
        const Entry* daylight = find_one(store, "mk_nikon_colorbalancea_0",
                                         0x0274U);
        ASSERT_NE(daylight, nullptr);
        ASSERT_EQ(daylight->value.count, 14U);
        ASSERT_TRUE(read_u16_element(store, *daylight, 6U, &level));
        EXPECT_EQ(level, 0x4567U);

        std::vector<std::byte> short_raw(raw.begin(), raw.end() - 1);
        std::array<NikonMakerEntry, 1> short_entries {
            maker_bytes_entry(0x0014U, short_raw),
        };
        const std::vector<std::byte> short_note = make_nikon_makernote(
            short_entries);
        MetaStore short_store;
        const ExifDecodeResult short_result
            = decode_nikon_fixture(short_note, "NIKON E5400", short_store);
        EXPECT_EQ(short_result.status, ExifDecodeStatus::Ok);
        short_store.finalize();
        EXPECT_TRUE(
            short_store.find_all(exif_key("mk_nikon_colorbalancea_0", 0x0270U))
                .empty());

        std::array<NikonMakerEntry, 1> wrong_type_entries {
            maker_bytes_entry(0x0014U, raw),
        };
        wrong_type_entries[0].type                   = 1U;
        const std::vector<std::byte> wrong_type_note = make_nikon_makernote(
            wrong_type_entries);
        MetaStore wrong_type_store;
        const ExifDecodeResult wrong_type_result
            = decode_nikon_fixture(wrong_type_note, "NIKON E5400",
                                   wrong_type_store);
        EXPECT_EQ(wrong_type_result.status, ExifDecodeStatus::Ok);
        wrong_type_store.finalize();
        EXPECT_TRUE(wrong_type_store
                        .find_all(exif_key("mk_nikon_colorbalancea_0", 0x0270U))
                        .empty());

        std::array<NikonMakerEntry, 1> e8700_entries {
            maker_bytes_entry(0x0014U, raw),
        };
        const std::vector<std::byte> e8700_note = make_nikon_makernote(
            e8700_entries);
        MetaStore e8700_store;
        const ExifDecodeResult e8700_result
            = decode_nikon_fixture(e8700_note, "E8700", e8700_store);
        EXPECT_EQ(e8700_result.status, ExifDecodeStatus::Ok);
        e8700_store.finalize();
        EXPECT_NE(find_one(e8700_store, "mk_nikon_colorbalancea_0", 0x0270U),
                  nullptr);
        EXPECT_EQ(find_one(e8700_store, "mk_nikon_colorbalancea_0", 0x02b2U),
                  nullptr);
    }

    TEST(NikonRawGap, ColorBalanceBAndOlderCUseSeparateGroupsAndVersionGates)
    {
        std::vector<std::byte> b_raw(0x1488U, std::byte { 0 });
        std::memcpy(b_raw.data(), "NRW 0100", 8U);
        write_u32le(&b_raw, 0x13e8U, 10U);
        write_u32le(&b_raw, 0x13e8U + 4U, 20U);
        write_u32le(&b_raw, 0x13e8U + 8U, 30U);
        write_u32le(&b_raw, 0x13e8U + 12U, 40U);
        std::array<NikonMakerEntry, 1> b_entries {
            maker_bytes_entry(0x0014U, b_raw),
        };
        const std::vector<std::byte> b_note = make_nikon_makernote(b_entries);
        MetaStore b_store;
        const ExifDecodeResult b_result
            = decode_nikon_fixture(b_note, "NIKON COOLPIX P6000", b_store);
        EXPECT_EQ(b_result.status, ExifDecodeStatus::Ok);
        b_store.finalize();
        const Entry* b_version = find_one(b_store, "mk_nikon_colorbalanceb_0",
                                          4U);
        ASSERT_NE(b_version, nullptr);
        EXPECT_EQ(text_value(b_store, *b_version), "0100");
        const Entry* b_levels = find_one(b_store, "mk_nikon_colorbalanceb_0",
                                         0x13e8U);
        ASSERT_NE(b_levels, nullptr);
        ASSERT_EQ(b_levels->value.count, 4U);
        uint32_t level = 0;
        ASSERT_TRUE(read_u32_element(b_store, *b_levels, 0U, &level));
        EXPECT_EQ(level, 20U);
        ASSERT_TRUE(read_u32_element(b_store, *b_levels, 3U, &level));
        EXPECT_EQ(level, 80U);
        EXPECT_TRUE(
            b_store.find_all(exif_key("mk_nikon_colorbalancec_0", 0x0038U))
                .empty());

        std::vector<std::byte> unknown_b_raw(0x1488U, std::byte { 0 });
        std::memcpy(unknown_b_raw.data(), "RAW 0100", 8U);
        std::array<NikonMakerEntry, 1> unknown_b_entries {
            maker_bytes_entry(0x0014U, unknown_b_raw),
        };
        const std::vector<std::byte> unknown_b_note = make_nikon_makernote(
            unknown_b_entries);
        MetaStore unknown_b_store;
        const ExifDecodeResult unknown_b_result
            = decode_nikon_fixture(unknown_b_note, "NIKON COOLPIX P6000",
                                   unknown_b_store);
        EXPECT_EQ(unknown_b_result.status, ExifDecodeStatus::Ok);
        unknown_b_store.finalize();
        EXPECT_TRUE(unknown_b_store
                        .find_all(exif_key("mk_nikon_colorbalanceb_0", 0x13e8U))
                        .empty());

        for (std::string_view version : { "0101", "0102" }) {
            SCOPED_TRACE(version);
            std::vector<std::byte> c_raw(0x0124U, std::byte { 0 });
            std::memcpy(c_raw.data(), "NRW ", 4U);
            std::memcpy(c_raw.data() + 4U, version.data(), version.size());
            static constexpr uint64_t kOffsets[] = {
                0x0038U, 0x004cU, 0x0060U, 0x0088U, 0x009cU,
                0x00b0U, 0x00c4U, 0x00d8U, 0x0100U, 0x0114U,
            };
            for (size_t i = 0; i < sizeof(kOffsets) / sizeof(kOffsets[0]);
                 ++i) {
                write_u32le(&c_raw, kOffsets[i], static_cast<uint32_t>(7U + i));
            }
            std::array<NikonMakerEntry, 1> c_entries {
                maker_bytes_entry(0x0014U, c_raw),
            };
            const std::vector<std::byte> c_note = make_nikon_makernote(
                c_entries);
            MetaStore c_store;
            const std::string_view model = version == "0101"
                                               ? "NIKON COOLPIX P7000"
                                               : "NIKON COOLPIX P7100";
            const ExifDecodeResult c_result
                = decode_nikon_fixture(c_note, model, c_store);
            EXPECT_EQ(c_result.status, ExifDecodeStatus::Ok);
            c_store.finalize();
            const Entry* c_version = find_one(c_store,
                                              "mk_nikon_colorbalancec_0", 4U);
            ASSERT_NE(c_version, nullptr);
            EXPECT_EQ(text_value(c_store, *c_version), version);
            for (size_t i = 0; i < sizeof(kOffsets) / sizeof(kOffsets[0]);
                 ++i) {
                const Entry* c_levels
                    = find_one(c_store, "mk_nikon_colorbalancec_0",
                               static_cast<uint16_t>(kOffsets[i]));
                ASSERT_NE(c_levels, nullptr);
                ASSERT_EQ(c_levels->value.count, 4U);
                ASSERT_TRUE(read_u32_element(c_store, *c_levels, 0U, &level));
                EXPECT_EQ(level, static_cast<uint32_t>((7U + i) * 2U));
            }
            EXPECT_TRUE(
                c_store.find_all(exif_key("mk_nikon_colorbalancec_0", 0x0074U))
                    .empty());
            EXPECT_TRUE(
                c_store.find_all(exif_key("mk_nikon_colorbalanceb_0", 0x13e8U))
                    .empty());
        }
    }

    TEST(NikonRawGap, LensData0202FocusDistanceIsScopedToThe0201Layout)
    {
        std::vector<std::byte> plain0202(20U, std::byte { 0 });
        std::memcpy(plain0202.data(), "0202", 4U);
        plain0202[9U]                              = std::byte { 109U };
        const std::vector<std::byte> encrypted0202 = encrypt_nikon_payload(
            plain0202);
        std::array<NikonMakerEntry, 3> entries0202 {
            serial_entry(),
            shutter_entry(),
            maker_bytes_entry(0x0098U, encrypted0202),
        };
        const std::vector<std::byte> note0202 = make_nikon_makernote(
            entries0202);
        MetaStore store0202;
        const ExifDecodeResult result0202
            = decode_nikon_fixture(note0202, "NIKON D60", store0202);
        EXPECT_EQ(result0202.status, ExifDecodeStatus::Ok);
        store0202.finalize();
        const Entry* focus_distance
            = find_one(store0202, "mk_nikon_lensdata0201_0", 0x0009U);
        ASSERT_NE(focus_distance, nullptr);
        EXPECT_EQ(focus_distance->value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(focus_distance->value.elem_type, MetaElementType::U8);
        EXPECT_EQ(focus_distance->value.data.u64, 109U);

        std::vector<std::byte> plain0204(20U, std::byte { 0 });
        std::memcpy(plain0204.data(), "0204", 4U);
        plain0204[10U]                             = std::byte { 37U };
        const std::vector<std::byte> encrypted0204 = encrypt_nikon_payload(
            plain0204);
        std::array<NikonMakerEntry, 3> entries0204 {
            serial_entry(),
            shutter_entry(),
            maker_bytes_entry(0x0098U, encrypted0204),
        };
        const std::vector<std::byte> note0204 = make_nikon_makernote(
            entries0204);
        MetaStore store0204;
        const ExifDecodeResult result0204
            = decode_nikon_fixture(note0204, "NIKON D4S", store0204);
        EXPECT_EQ(result0204.status, ExifDecodeStatus::Ok);
        store0204.finalize();
        EXPECT_EQ(find_one(store0204, "mk_nikon_lensdata0204_0", 0x0009U),
                  nullptr);
        const Entry* focal_length
            = find_one(store0204, "mk_nikon_lensdata0204_0", 0x000aU);
        ASSERT_NE(focal_length, nullptr);
        EXPECT_EQ(focal_length->value.data.u64, 37U);
    }

    TEST(NikonRawGap, ShotInfoD4SMultiSelectorUsesFirmwareGateAndMask)
    {
        for (const std::string_view firmware : { "1.20b", "1.00d" }) {
            SCOPED_TRACE(firmware);
            std::vector<std::byte> plain(0x18c4U, std::byte { 0 });
            std::memcpy(plain.data(), "0231", 4U);
            std::memcpy(plain.data() + 4U, firmware.data(), firmware.size());
            plain[0x18c2U]                         = std::byte { 0x80U };
            const std::vector<std::byte> encrypted = encrypt_nikon_payload(
                plain);
            std::array<NikonMakerEntry, 3> entries {
                serial_entry(),
                shutter_entry(),
                maker_bytes_entry(0x0091U, encrypted),
            };
            const std::vector<std::byte> note = make_nikon_makernote(entries);
            MetaStore store;
            const ExifDecodeResult result
                = decode_nikon_fixture(note, "NIKON D4S", store);
            EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
            store.finalize();
            const Entry* version = find_one(store, "mk_nikon_shotinfod4s_0",
                                            4U);
            ASSERT_NE(version, nullptr);
            EXPECT_EQ(text_value(store, *version), firmware);
            const Entry* selector = find_one(store, "mk_nikon_shotinfod4s_0",
                                             0x18c2U);
            if (firmware == "1.00d") {
                EXPECT_EQ(selector, nullptr);
            } else {
                ASSERT_NE(selector, nullptr);
                EXPECT_EQ(selector->value.kind, MetaValueKind::Scalar);
                EXPECT_EQ(selector->value.elem_type, MetaElementType::U8);
                EXPECT_EQ(selector->value.data.u64, 2U);
            }
            EXPECT_TRUE(
                store.find_all(exif_key("mk_nikon_settingsd4_0", 0x18c2U))
                    .empty());
        }
    }

    TEST(NikonRawGap, ColorBalanceArrayGroupHonorsEntryLimitBeforeEmission)
    {
        std::vector<std::byte> raw(2560U, std::byte { 0 });
        std::array<NikonMakerEntry, 1> entries {
            maker_bytes_entry(0x0014U, raw),
        };
        const std::vector<std::byte> note = make_nikon_makernote(entries);
        ExifDecodeOptions options;
        options.limits.max_entries_per_ifd = 7U;
        MetaStore store;
        const ExifDecodeResult result
            = decode_nikon_fixture(note, "NIKON E5400", store, options);
        EXPECT_EQ(result.status, ExifDecodeStatus::LimitExceeded);
        store.finalize();
        EXPECT_TRUE(
            store.find_all(exif_key("mk_nikon_colorbalancea_0", 0x0270U))
                .empty());
    }

}  // namespace
}  // namespace openmeta
