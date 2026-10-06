// SPDX-License-Identifier: Apache-2.0

#include "../src/openmeta/nikon_capture_decode_internal.h"

#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_key.h"
#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    constexpr uint32_t kCropDataRecordId      = 0x374233e0U;
    constexpr uint32_t kNikonCaptureSignature = 0x7a86a940U;
    constexpr uint32_t kHistogramXmlRecordId  = 0x083a1a25U;

    static void write_u32le(std::span<std::byte> bytes, size_t offset,
                            uint32_t value)
    {
        for (uint32_t i = 0U; i < 4U; ++i) {
            bytes[offset + i]
                = std::byte { static_cast<uint8_t>(value >> (i * 8U)) };
        }
    }

    static void write_u64le(std::span<std::byte> bytes, size_t offset,
                            uint64_t value)
    {
        for (uint32_t i = 0U; i < 8U; ++i) {
            bytes[offset + i]
                = std::byte { static_cast<uint8_t>(value >> (i * 8U)) };
        }
    }

    static void write_u16le(std::span<std::byte> bytes, size_t offset,
                            uint16_t value)
    {
        bytes[offset]      = std::byte { static_cast<uint8_t>(value) };
        bytes[offset + 1U] = std::byte { static_cast<uint8_t>(value >> 8U) };
    }

    static void append_capture_record(std::vector<std::byte>* root,
                                      uint32_t record_id,
                                      std::span<const std::byte> payload)
    {
        const size_t start = root->size();
        root->resize(start + 22U + payload.size(), std::byte { 0 });
        write_u32le(*root, start, record_id);
        write_u32le(*root, start + 18U,
                    static_cast<uint32_t>(payload.size() + 4U));
        if (!payload.empty()) {
            std::memcpy(root->data() + start + 22U, payload.data(),
                        payload.size());
        }
        write_u32le(*root, 18U, static_cast<uint32_t>(root->size() - 18U));
    }

    static std::vector<std::byte> make_capture_root()
    {
        std::vector<std::byte> root(22U, std::byte { 0 });
        write_u32le(root, 0U, kNikonCaptureSignature);
        write_u32le(root, 18U, 4U);
        return root;
    }

    static void append_d60_terminal_and_outer_pad(std::vector<std::byte>* root)
    {
        const std::array<std::byte, 4> partial {
            std::byte { 'g' },
            std::byte { 'e' },
            std::byte { 'r' },
            std::byte { '>' },
        };
        root->insert(root->end(), partial.begin(), partial.end());
        write_u32le(*root, 18U, static_cast<uint32_t>(root->size() - 18U));

        std::array<std::byte, 429> outer_pad {};
        outer_pad[3U]   = std::byte { 0xa4U };
        outer_pad[64U]  = std::byte { 0x19U };
        outer_pad[428U] = std::byte { 0xffU };
        root->insert(root->end(), outer_pad.begin(), outer_pad.end());
    }

    static std::vector<std::byte> make_d60_crop_record()
    {
        // Nikon D60 CropData (record 0x374233e0) stores 237 bytes. CropRight
        // at byte offset 0x2e is raw double 6016 in the original witness.
        std::vector<std::byte> crop_payload(237U, std::byte { 0 });
        write_u64le(crop_payload, 0x002eU, 0x40b7800000000000ULL);
        return crop_payload;
    }

    static EntryId capture_entry(const MetaStore& store, uint32_t record_id,
                                 uint16_t tag)
    {
        char token[std::string_view("mk_nikon_capture_").size() + 8U];
        constexpr std::string_view prefix = "mk_nikon_capture_";
        constexpr char hex[]              = "0123456789abcdef";
        std::memcpy(token, prefix.data(), prefix.size());
        for (uint32_t i = 0U; i < 8U; ++i) {
            token[prefix.size() + i]
                = hex[(record_id >> ((7U - i) * 4U)) & 0x0fU];
        }
        const std::string_view ifd(token, sizeof(token));
        const auto entries = store.find_all(make_exif_tag_key_view(ifd, tag));
        return entries.empty() ? kInvalidEntryId : entries.front();
    }

    static std::vector<std::byte> make_payload(size_t size, uint8_t fill = 0U)
    {
        return std::vector<std::byte>(size, std::byte { fill });
    }

    static void expect_main_u8(const MetaStore& store, uint32_t record_id,
                               uint8_t value, std::string_view name)
    {
        const EntryId id = capture_entry(store, record_id, 0U);
        ASSERT_NE(id, kInvalidEntryId);
        const Entry& entry = store.entry(id);
        EXPECT_EQ(entry.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(entry.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(entry.value.data.u64, value);
        EXPECT_EQ(entry.origin.name_context_kind,
                  EntryNameContextKind::NikonCaptureField);
        EXPECT_EQ(entry.origin.wire_count, 1U);
        EXPECT_EQ(store.block_info(entry.origin.block).id, record_id);
        EXPECT_EQ(exif_entry_name(store, entry, ExifTagNamePolicy::Canonical),
                  name);
    }

    static void expect_no_output(std::span<const std::byte> payload,
                                 ExifDecodeStatus expected,
                                 ExifDecodeOptions options = {})
    {
        MetaStore store;
        ExifDecodeResult result;
        exif_internal::decode_nikon_capture(payload, store, options, &result);
        EXPECT_EQ(result.status, expected);
        EXPECT_TRUE(store.entries().empty());
        EXPECT_EQ(store.block_count(), 0U);
    }

    TEST(NikonCaptureDecode, DecodesD60CropFieldsAndRetainsUnknownRecord)
    {
        std::vector<std::byte> raw                = make_capture_root();
        const std::vector<std::byte> crop_payload = make_d60_crop_record();
        append_capture_record(&raw, kCropDataRecordId, crop_payload);
        const std::array<std::byte, 3> unknown_payload {
            std::byte { 0x91U },
            std::byte { 0x00U },
            std::byte { 0xfeU },
        };
        append_capture_record(&raw, 0x12345678U, unknown_payload);
        append_d60_terminal_and_outer_pad(&raw);

        MetaStore store;
        const BlockId parent_block = store.add_block(BlockInfo {});
        ASSERT_NE(parent_block, kInvalidBlockId);
        Entry parent;
        parent.key   = make_exif_tag_key(store.arena(), "mk_nikon0", 0x0e01U);
        parent.value = make_bytes(store.arena(), raw);
        parent.origin.block      = parent_block;
        parent.origin.wire_type  = WireType { WireFamily::Tiff, 7U };
        parent.origin.wire_count = static_cast<uint32_t>(raw.size());
        ASSERT_NE(store.add_entry(parent), kInvalidEntryId);

        ExifDecodeOptions options;
        ExifDecodeResult result;
        exif_internal::decode_nikon_capture(raw, store, options, &result);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        EXPECT_EQ(result.entries_decoded, 14U);
        EXPECT_EQ(store.block_count(), 3U);

        store.finalize();
        const auto crop_ids = store.find_all(
            make_exif_tag_key_view("mk_nikon_capture_374233e0", 0x002eU));
        ASSERT_EQ(crop_ids.size(), 1U);
        const Entry& crop_right = store.entry(crop_ids.front());
        EXPECT_EQ(crop_right.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(crop_right.value.elem_type, MetaElementType::F64);
        EXPECT_EQ(crop_right.value.data.f64_bits, 0x40b7800000000000ULL);
        EXPECT_EQ(crop_right.origin.name_context_kind,
                  EntryNameContextKind::NikonCaptureField);
        EXPECT_EQ(crop_right.origin.name_context_variant, 3U);
        EXPECT_NE(
            crop_right.origin.block,
            store.entry(capture_entry(store, 0x12345678U, 0U)).origin.block);
        EXPECT_EQ(store.block_info(crop_right.origin.block).id,
                  kCropDataRecordId);
        EXPECT_EQ(exif_internal::nikon_capture_field_name(3U), "CropRight");
        EXPECT_EQ(exif_entry_name(store, crop_right,
                                  ExifTagNamePolicy::Canonical),
                  "CropRight");

        const auto unknown_ids = store.find_all(
            make_exif_tag_key_view("mk_nikon_capture_12345678", 0U));
        ASSERT_EQ(unknown_ids.size(), 1U);
        const Entry& unknown = store.entry(unknown_ids.front());
        ASSERT_EQ(unknown.value.kind, MetaValueKind::Bytes);
        ASSERT_EQ(unknown.value.count, unknown_payload.size());
        const std::span<const std::byte> saved_unknown = store.arena().span(
            unknown.value.data.span);
        ASSERT_EQ(saved_unknown.size(), unknown_payload.size());
        EXPECT_TRUE(std::equal(saved_unknown.begin(), saved_unknown.end(),
                               unknown_payload.begin()));
        EXPECT_EQ(unknown.origin.name_context_kind, EntryNameContextKind::None);

        const auto parent_ids = store.find_all(
            make_exif_tag_key_view("mk_nikon0", 0x0e01U));
        ASSERT_EQ(parent_ids.size(), 1U);
        const Entry& saved_parent = store.entry(parent_ids.front());
        const std::span<const std::byte> parent_bytes = store.arena().span(
            saved_parent.value.data.span);
        ASSERT_EQ(parent_bytes.size(), raw.size());
        EXPECT_TRUE(
            std::equal(parent_bytes.begin(), parent_bytes.end(), raw.begin()));
    }

    TEST(NikonCaptureDecode, RejectsTruncatedHeadersAndInvalidDeclaredSizes)
    {
        std::array<std::byte, 21> short_file_header {};
        expect_no_output(short_file_header, ExifDecodeStatus::Malformed);

        std::array<std::byte, 44> undersized_declared_record {};
        write_u32le(undersized_declared_record, 0U, kNikonCaptureSignature);
        write_u32le(undersized_declared_record, 18U, 26U);
        write_u32le(undersized_declared_record, 22U, kCropDataRecordId);
        write_u32le(undersized_declared_record, 40U, 3U);
        expect_no_output(undersized_declared_record,
                         ExifDecodeStatus::Malformed);

        std::vector<std::byte> valid_then_bad = make_capture_root();
        const std::array<std::byte, 1> first_payload { std::byte { 0x5aU } };
        append_capture_record(&valid_then_bad, 0x10203040U, first_payload);
        const size_t bad_record_offset = valid_then_bad.size();
        valid_then_bad.resize(bad_record_offset + 22U, std::byte { 0U });
        write_u32le(valid_then_bad, bad_record_offset, 0x50607080U);
        write_u32le(valid_then_bad, bad_record_offset + 18U, 3U);
        write_u32le(valid_then_bad, 18U,
                    static_cast<uint32_t>(valid_then_bad.size() - 18U));
        expect_no_output(valid_then_bad, ExifDecodeStatus::Malformed);

        std::array<std::byte, 46> overrun_record {};
        write_u32le(overrun_record, 0U, kNikonCaptureSignature);
        write_u32le(overrun_record, 18U, 28U);
        write_u32le(overrun_record, 22U, kCropDataRecordId);
        write_u32le(overrun_record, 40U, 10U);
        expect_no_output(overrun_record, ExifDecodeStatus::Malformed);

        std::array<std::byte, 22> oversized_root {};
        write_u32le(oversized_root, 0U, kNikonCaptureSignature);
        write_u32le(oversized_root, 18U, 100U);
        expect_no_output(oversized_root, ExifDecodeStatus::Malformed);

        std::array<std::byte, 22> invalid_signature {};
        write_u32le(invalid_signature, 18U, 4U);
        expect_no_output(invalid_signature, ExifDecodeStatus::Malformed);
    }

    TEST(NikonCaptureDecode, AcceptsZeroLengthRecordAndShortOpaqueTail)
    {
        std::vector<std::byte> raw = make_capture_root();
        append_capture_record(&raw, 0x10203040U, std::span<const std::byte> {});
        raw.insert(raw.end(), { std::byte { 0xdeU }, std::byte { 0xadU },
                                std::byte { 0xbeU }, std::byte { 0xefU } });
        write_u32le(raw, 18U, static_cast<uint32_t>(raw.size() - 18U));

        MetaStore store;
        ExifDecodeResult result;
        exif_internal::decode_nikon_capture(raw, store, ExifDecodeOptions {},
                                            &result);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        EXPECT_EQ(result.entries_decoded, 1U);
        EXPECT_EQ(store.block_count(), 1U);
        store.finalize();
        const EntryId id = capture_entry(store, 0x10203040U, 0U);
        ASSERT_NE(id, kInvalidEntryId);
        const Entry& entry = store.entry(id);
        EXPECT_EQ(entry.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(entry.value.count, 0U);
        EXPECT_EQ(entry.origin.wire_count, 0U);
    }

    TEST(NikonCaptureDecode, DecodesTypedD60ChildTablesAndSnapshotNames)
    {
        std::vector<std::byte> raw = make_capture_root();

        const std::array<std::byte, 1> unsharp { std::byte { 42U } };
        append_capture_record(&raw, 0xe42b5161U, unsharp);
        append_capture_record(&raw, kCropDataRecordId, make_d60_crop_record());

        std::vector<std::byte> exposure = make_payload(38U);
        write_u16le(exposure, 0U, 0xff85U);  // -123, no /100 conversion.
        write_u64le(exposure, 0x12U, 0x3ff4000000000000ULL);
        exposure[0x24U] = std::byte { 1U };
        exposure[0x25U] = std::byte { 6U };
        append_capture_record(&raw, 0x56a54260U, exposure);

        std::vector<std::byte> white_balance = make_payload(41U);
        write_u64le(white_balance, 0U, 0x3ff4000000000000ULL);
        write_u64le(white_balance, 8U, 0xbfe8000000000000ULL);
        white_balance[0x10U] = std::byte { 7U };
        write_u16le(white_balance, 0x14U, 0x1234U);
        write_u16le(white_balance, 0x18U, 0xabcdU);
        write_u32le(white_balance, 0x25U, 0xfffffff9U);  // signed -7.
        append_capture_record(&raw, 0xbf3c6c20U, white_balance);

        std::vector<std::byte> noise = make_payload(32U);
        noise[4U]                    = std::byte { 1U };
        noise[5U]                    = std::byte { 2U };
        write_u32le(noise, 9U, 0x89abcdefU);
        write_u32le(noise, 13U, 0x01234567U);
        write_u16le(noise, 17U, 0x7654U);
        noise[21U] = std::byte { 3U };
        noise[23U] = std::byte { 4U };
        write_u32le(noise, 24U, 0xfedcba98U);
        write_u32le(noise, 28U, 0x10293847U);
        append_capture_record(&raw, 0x926f13e0U, noise);

        std::vector<std::byte> color_boost = make_payload(5U);
        color_boost[0U]                    = std::byte { 1U };
        write_u32le(color_boost, 1U, 0x12345678U);
        append_capture_record(&raw, 0xb999a36fU, color_boost);

        std::vector<std::byte> brightness = make_payload(9U);
        write_u64le(brightness, 0U, 0xc004000000000000ULL);
        brightness[8U] = std::byte { 0xa5U };
        append_capture_record(&raw, 0x84589434U, brightness);

        std::vector<std::byte> effects = make_payload(10U);
        effects[0U]                    = std::byte { 3U };
        write_u16le(effects, 4U, 0xff85U);
        write_u16le(effects, 6U, 0x01c8U);
        write_u16le(effects, 8U, 0xfcdbU);
        append_capture_record(&raw, 0xb0384e1eU, effects);

        std::vector<std::byte> picture = make_payload(48U, 0x80U);
        picture[0U]                    = std::byte { 1U };
        std::memcpy(picture.data() + 0x13U, "Vivid", 5U);
        picture[0x18U] = std::byte { 0U };
        std::memcpy(picture.data() + 0x19U, "TAIL", 4U);
        picture[0x2aU] = std::byte { 0U };
        picture[0x2bU] = std::byte { 0U };
        picture[0x2cU] = std::byte { 0x7fU };
        picture[0x2dU] = std::byte { 0x80U };
        picture[0x2eU] = std::byte { 0xffU };
        picture[0x2fU] = std::byte { 0x81U };
        append_capture_record(&raw, 0x39c456acU, picture);

        const std::array<std::byte, 1> red_eye { std::byte { 2U } };
        append_capture_record(&raw, 0x3cfc73c6U, red_eye);
        std::vector<std::byte> dlighting_hs = make_payload(8U);
        write_u32le(dlighting_hs, 0U, 0x11223344U);
        write_u32le(dlighting_hs, 4U, 0x55667788U);
        append_capture_record(&raw, 0xe37b4337U, dlighting_hs);
        std::vector<std::byte> dlighting_hq = make_payload(12U);
        write_u32le(dlighting_hq, 0U, 0x01020304U);
        write_u32le(dlighting_hq, 4U, 0x05060708U);
        write_u32le(dlighting_hq, 8U, 0x090a0b0cU);
        append_capture_record(&raw, 0x890ff591U, dlighting_hq);
        std::vector<std::byte> highlight = make_payload(7U);
        highlight[0U]                    = std::byte { 0x80U };
        highlight[1U]                    = std::byte { 0xffU };
        highlight[6U]                    = std::byte { 0x7fU };
        append_capture_record(&raw, 0x116fea21U, highlight);

        MetaStore store;
        ExifDecodeResult result;
        exif_internal::decode_nikon_capture(raw, store, ExifDecodeOptions {},
                                            &result);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        EXPECT_EQ(result.entries_decoded, 58U);
        EXPECT_EQ(store.block_count(), 13U);
        store.finalize();
        for (const Entry& entry : store.entries()) {
            if (entry.origin.name_context_kind
                == EntryNameContextKind::NikonCaptureField) {
                EXPECT_FALSE(
                    exif_entry_name(store, entry, ExifTagNamePolicy::Canonical)
                        .empty());
            }
        }

        const Entry& crop_right = store.entry(
            capture_entry(store, kCropDataRecordId, 0x002eU));
        EXPECT_EQ(crop_right.value.elem_type, MetaElementType::F64);
        EXPECT_EQ(crop_right.value.data.f64_bits, 0x40b7800000000000ULL);
        EXPECT_EQ(exif_entry_name(store, crop_right,
                                  ExifTagNamePolicy::Canonical),
                  "CropRight");
        const Entry& unsharp_entry = store.entry(
            capture_entry(store, 0xe42b5161U, 0U));
        EXPECT_EQ(unsharp_entry.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(unsharp_entry.value.data.u64, 42U);
        EXPECT_EQ(store.block_info(unsharp_entry.origin.block).id, 0xe42b5161U);

        const Entry& exposure_adj = store.entry(
            capture_entry(store, 0x56a54260U, 0U));
        EXPECT_EQ(exposure_adj.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(exposure_adj.value.data.i64, -123);
        const Entry& exposure_double = store.entry(
            capture_entry(store, 0x56a54260U, 0x12U));
        EXPECT_EQ(exposure_double.value.data.f64_bits, 0x3ff4000000000000ULL);
        const Entry& tint = store.entry(
            capture_entry(store, 0xbf3c6c20U, 0x25U));
        EXPECT_EQ(tint.value.elem_type, MetaElementType::I32);
        EXPECT_EQ(tint.value.data.i64, -7);
        const Entry& wb_lighting = store.entry(
            capture_entry(store, 0xbf3c6c20U, 0x14U));
        EXPECT_EQ(wb_lighting.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(wb_lighting.value.data.u64, 0x1234U);
        const Entry& noise_intensity = store.entry(
            capture_entry(store, 0x926f13e0U, 0x09U));
        EXPECT_EQ(noise_intensity.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(noise_intensity.value.data.u64, 0x89abcdefU);
        const Entry& boost_level = store.entry(
            capture_entry(store, 0xb999a36fU, 1U));
        EXPECT_EQ(boost_level.value.data.u64, 0x12345678U);
        const Entry& brightness_adj = store.entry(
            capture_entry(store, 0x84589434U, 0U));
        EXPECT_EQ(brightness_adj.value.data.f64_bits, 0xc004000000000000ULL);
        const Entry& effect_red = store.entry(
            capture_entry(store, 0xb0384e1eU, 4U));
        EXPECT_EQ(effect_red.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(effect_red.value.data.i64, -123);
        const Entry& picture_text = store.entry(
            capture_entry(store, 0x39c456acU, 0x13U));
        EXPECT_EQ(picture_text.value.kind, MetaValueKind::Text);
        EXPECT_EQ(picture_text.value.count, 16U);
        const std::span<const std::byte> picture_text_bytes
            = store.arena().span(picture_text.value.data.span);
        ASSERT_EQ(picture_text_bytes.size(), 16U);
        EXPECT_EQ(picture_text_bytes[0U], std::byte { 'V' });
        EXPECT_EQ(picture_text_bytes[4U], std::byte { 'd' });
        EXPECT_EQ(picture_text_bytes[5U], std::byte { 0U });
        EXPECT_EQ(picture_text_bytes[6U], std::byte { 'T' });
        EXPECT_EQ(picture_text_bytes[9U], std::byte { 'L' });
        EXPECT_EQ(picture_text_bytes[10U], std::byte { 0x80U });
        const Entry& picture_auto = store.entry(
            capture_entry(store, 0x39c456acU, 0x2bU));
        EXPECT_EQ(picture_auto.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(picture_auto.value.data.u64, 0U);
        const Entry& picture_hue = store.entry(
            capture_entry(store, 0x39c456acU, 0x2fU));
        EXPECT_EQ(picture_hue.value.data.u64, 0x81U);
        const Entry& hs_color = store.entry(
            capture_entry(store, 0xe37b4337U, 1U));
        EXPECT_EQ(hs_color.value.data.u64, 0x55667788U);
        EXPECT_EQ(exif_entry_name(store, hs_color, ExifTagNamePolicy::Canonical),
                  "D-LightingHSColorBoost");
        const Entry& hq_color = store.entry(
            capture_entry(store, 0x890ff591U, 2U));
        EXPECT_EQ(hq_color.value.data.u64, 0x090a0b0cU);
        EXPECT_EQ(exif_entry_name(store, hq_color, ExifTagNamePolicy::Canonical),
                  "D-LightingHQColorBoost");
        EXPECT_EQ(capture_entry(store, 0xe37b4337U, 4U), kInvalidEntryId);
        EXPECT_EQ(capture_entry(store, 0x890ff591U, 8U), kInvalidEntryId);
        const Entry& shadow = store.entry(
            capture_entry(store, 0x116fea21U, 0U));
        EXPECT_EQ(shadow.value.elem_type, MetaElementType::I8);
        EXPECT_EQ(shadow.value.data.i64, -128);
        const Entry& highlight_protection = store.entry(
            capture_entry(store, 0x116fea21U, 6U));
        EXPECT_EQ(highlight_protection.value.data.i64, 127);

        const TransferSourceSnapshot snapshot = build_transfer_source_snapshot(
            store);
        std::vector<std::byte> serialized;
        ASSERT_EQ(
            serialize_transfer_source_snapshot(snapshot, &serialized).status,
            TransferStatus::Ok);
        TransferSourceSnapshot restored;
        ASSERT_EQ(
            deserialize_transfer_source_snapshot(serialized, &restored).status,
            TransferStatus::Ok);
        const EntryId restored_id = capture_entry(restored.store, 0x39c456acU,
                                                  0x2bU);
        ASSERT_NE(restored_id, kInvalidEntryId);
        const Entry& restored_entry = restored.store.entry(restored_id);
        EXPECT_EQ(restored_entry.origin.name_context_kind,
                  EntryNameContextKind::NikonCaptureField);
        EXPECT_EQ(exif_entry_name(restored.store, restored_entry,
                                  ExifTagNamePolicy::Canonical),
                  "SharpeningAdj");
    }

    TEST(NikonCaptureDecode,
         UsesObservedE5700FieldExtentsAndKeepsHistogramOpaque)
    {
        std::vector<std::byte> e5700    = make_capture_root();
        std::vector<std::byte> exposure = make_payload(26U);
        write_u16le(exposure, 0U, 0x0085U);
        write_u64le(exposure, 0x12U, 0x4010000000000000ULL);
        append_capture_record(&e5700, 0x56a54260U, exposure);
        std::vector<std::byte> white_balance = make_payload(26U);
        write_u64le(white_balance, 0U, 0x3ff0000000000000ULL);
        write_u64le(white_balance, 8U, 0x4000000000000000ULL);
        white_balance[0x10U] = std::byte { 5U };
        write_u16le(white_balance, 0x14U, 0x0202U);
        write_u16le(white_balance, 0x18U, 5200U);
        append_capture_record(&e5700, 0xbf3c6c20U, white_balance);
        std::vector<std::byte> noise = make_payload(6U);
        noise[4U]                    = std::byte { 1U };
        noise[5U]                    = std::byte { 3U };
        append_capture_record(&e5700, 0x926f13e0U, noise);

        MetaStore e5700_store;
        ExifDecodeResult e5700_result;
        exif_internal::decode_nikon_capture(e5700, e5700_store,
                                            ExifDecodeOptions {},
                                            &e5700_result);
        EXPECT_EQ(e5700_result.status, ExifDecodeStatus::Ok);
        e5700_store.finalize();
        EXPECT_NE(capture_entry(e5700_store, 0x56a54260U, 0U), kInvalidEntryId);
        EXPECT_EQ(capture_entry(e5700_store, 0x56a54260U, 0x24U),
                  kInvalidEntryId);
        EXPECT_EQ(capture_entry(e5700_store, 0xbf3c6c20U, 0x25U),
                  kInvalidEntryId);
        EXPECT_EQ(capture_entry(e5700_store, 0x926f13e0U, 0x09U),
                  kInvalidEntryId);

        std::vector<std::byte> d60       = make_capture_root();
        std::vector<std::byte> histogram = make_payload(9599U, 0x3cU);
        histogram[0U]                    = std::byte { '<' };
        histogram[1U]                    = std::byte { 'x' };
        append_capture_record(&d60, kHistogramXmlRecordId, histogram);
        append_d60_terminal_and_outer_pad(&d60);
        MetaStore d60_store;
        ExifDecodeResult d60_result;
        exif_internal::decode_nikon_capture(d60, d60_store,
                                            ExifDecodeOptions {}, &d60_result);
        EXPECT_EQ(d60_result.status, ExifDecodeStatus::Ok);
        d60_store.finalize();
        const EntryId histogram_id = capture_entry(d60_store,
                                                   kHistogramXmlRecordId, 0U);
        ASSERT_NE(histogram_id, kInvalidEntryId);
        const Entry& histogram_entry = d60_store.entry(histogram_id);
        EXPECT_EQ(histogram_entry.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(histogram_entry.value.count, 9603U);
        const std::span<const std::byte> histogram_bytes
            = d60_store.arena().span(histogram_entry.value.data.span);
        ASSERT_EQ(histogram_bytes.size(), 9603U);
        EXPECT_EQ(histogram_bytes[0], std::byte { '<' });
        EXPECT_EQ(histogram_bytes[1], std::byte { 'x' });
        EXPECT_EQ(histogram_bytes[9598U], std::byte { 0x3cU });
        EXPECT_EQ(histogram_bytes[9599U], std::byte { 'g' });
        EXPECT_EQ(histogram_bytes[9600U], std::byte { 'e' });
        EXPECT_EQ(histogram_bytes[9601U], std::byte { 'r' });
        EXPECT_EQ(histogram_bytes[9602U], std::byte { '>' });
    }

    TEST(NikonCaptureDecode, DecodesMainScalarsAndFullUnsharpDataTable)
    {
        std::vector<std::byte> raw = make_capture_root();
        static constexpr struct {
            uint32_t record_id;
            uint8_t value;
            std::string_view name;
        } kU8Records[] = {
            { 0x008ae85eU, 0x80U, "LCHEditor" },
            { 0x0c89224bU, 0x7fU, "ColorAberrationControl" },
            { 0x2175eb78U, 0x02U, "D-LightingHQ" },
            { 0x416391c6U, 0x05U, "QuickFix" },
            { 0x5f0e7d23U, 0x00U, "ColorBooster" },
            { 0x6a6e36b6U, 0x01U, "D-LightingHQSelected" },
            { 0x753dcbc0U, 0x02U, "NoiseReduction" },
            { 0x76a43200U, 0x03U, "UnsharpMask" },
            { 0x76a43201U, 0x04U, "Curves" },
            { 0x76a43202U, 0x05U, "ColorBalanceAdj" },
            { 0x76a43203U, 0x06U, "AdvancedRaw" },
            { 0x76a43204U, 0x07U, "WhiteBalanceAdj" },
            { 0x76a43205U, 0x08U, "VignetteControl" },
            { 0x76a43206U, 0x09U, "FlipHorizontal" },
            { 0xab5eca5eU, 0x0aU, "PhotoEffects" },
            { 0xce5554aaU, 0x0bU, "D-LightingHS" },
            { 0xe2173c47U, 0x0cU, "PictureControl" },
            { 0xfe28a44fU, 28U, "AutoRedEye" },
            { 0xfe443a45U, 0x0eU, "ImageDustOff" },
        };
        for (const auto& item : kU8Records) {
            const std::array<std::byte, 1> value { std::byte { item.value } };
            append_capture_record(&raw, item.record_id, value);
        }

        std::array<std::byte, 8> straighten_raw {};
        write_u64le(straighten_raw, 0U, 0x3ff4000000000000ULL);
        append_capture_record(&raw, 0x2fc08431U, straighten_raw);
        std::array<std::byte, 12> edit_version {};
        std::memcpy(edit_version.data(), "Capture NX", 10U);
        edit_version[10U] = std::byte { 0U };
        edit_version[11U] = std::byte { 'x' };
        append_capture_record(&raw, 0x3d136244U, edit_version);
        std::array<std::byte, 2> rotation {};
        write_u16le(rotation, 0U, 270U);
        append_capture_record(&raw, 0x76a43207U, rotation);
        std::array<std::byte, 2> vignette_intensity {};
        write_u16le(vignette_intensity, 0U, 0xfffdU);
        append_capture_record(&raw, 0xac6bd5c0U, vignette_intensity);

        std::vector<std::byte> unsharp = make_payload(109U);
        unsharp[0U]
            = std::byte { 0U };  // Count does not gate size-valid fields.
        write_u16le(unsharp, 19U, 6U);
        write_u16le(unsharp, 23U, 0x1234U);
        write_u16le(unsharp, 25U, 0x5678U);
        unsharp[27U] = std::byte { 0x9aU };
        write_u16le(unsharp, 46U, 5U);
        write_u16le(unsharp, 50U, 0x2345U);
        write_u16le(unsharp, 52U, 0x6789U);
        unsharp[54U] = std::byte { 0xabU };
        write_u16le(unsharp, 73U, 4U);
        write_u16le(unsharp, 77U, 0x3456U);
        write_u16le(unsharp, 79U, 0x789aU);
        unsharp[81U] = std::byte { 0xbcU };
        write_u16le(unsharp, 100U, 3U);
        write_u16le(unsharp, 104U, 0x4567U);
        write_u16le(unsharp, 106U, 0x89abU);
        unsharp[108U] = std::byte { 0xcdU };
        append_capture_record(&raw, 0xe42b5161U, unsharp);

        MetaStore store;
        ExifDecodeResult result;
        exif_internal::decode_nikon_capture(raw, store, ExifDecodeOptions {},
                                            &result);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        EXPECT_EQ(result.entries_decoded, 40U);
        EXPECT_EQ(store.block_count(), 24U);
        store.finalize();
        for (const Entry& entry : store.entries()) {
            if (entry.origin.name_context_kind
                == EntryNameContextKind::NikonCaptureField) {
                EXPECT_FALSE(
                    exif_entry_name(store, entry, ExifTagNamePolicy::Canonical)
                        .empty());
            }
        }

        for (const auto& item : kU8Records) {
            expect_main_u8(store, item.record_id, item.value, item.name);
        }

        const Entry& straighten = store.entry(
            capture_entry(store, 0x2fc08431U, 0U));
        EXPECT_EQ(straighten.value.elem_type, MetaElementType::F64);
        EXPECT_EQ(straighten.value.data.f64_bits, 0x3ff4000000000000ULL);
        EXPECT_EQ(straighten.origin.name_context_kind,
                  EntryNameContextKind::NikonCaptureField);
        EXPECT_EQ(straighten.origin.wire_count, 1U);
        EXPECT_EQ(store.block_info(straighten.origin.block).id, 0x2fc08431U);
        EXPECT_EQ(exif_entry_name(store, straighten,
                                  ExifTagNamePolicy::Canonical),
                  "StraightenAngle");
        const Entry& version = store.entry(
            capture_entry(store, 0x3d136244U, 0U));
        EXPECT_EQ(version.value.kind, MetaValueKind::Text);
        EXPECT_EQ(version.value.count, 10U);
        EXPECT_EQ(version.origin.name_context_kind,
                  EntryNameContextKind::NikonCaptureField);
        EXPECT_EQ(exif_entry_name(store, version, ExifTagNamePolicy::Canonical),
                  "EditVersionName");
        EXPECT_EQ(version.origin.wire_count, 1U);
        EXPECT_EQ(store.block_info(version.origin.block).id, 0x3d136244U);
        const std::span<const std::byte> version_text = store.arena().span(
            version.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                       version_text.data()),
                                   version_text.size()),
                  "Capture NX");
        const Entry& rotation_value = store.entry(
            capture_entry(store, 0x76a43207U, 0U));
        EXPECT_EQ(rotation_value.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(rotation_value.value.data.u64, 270U);
        EXPECT_EQ(rotation_value.origin.name_context_kind,
                  EntryNameContextKind::NikonCaptureField);
        EXPECT_EQ(store.block_info(rotation_value.origin.block).id,
                  0x76a43207U);
        const Entry& intensity = store.entry(
            capture_entry(store, 0xac6bd5c0U, 0U));
        EXPECT_EQ(intensity.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(intensity.value.data.i64, -3);
        EXPECT_EQ(intensity.origin.name_context_kind,
                  EntryNameContextKind::NikonCaptureField);
        EXPECT_EQ(store.block_info(intensity.origin.block).id, 0xac6bd5c0U);

        const Entry& unsharp_color = store.entry(
            capture_entry(store, 0xe42b5161U, 19U));
        EXPECT_EQ(unsharp_color.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(unsharp_color.value.data.u64, 6U);
        EXPECT_EQ(exif_entry_name(store, unsharp_color,
                                  ExifTagNamePolicy::Canonical),
                  "Unsharp1Color");
        const Entry& unsharp_fourth_threshold = store.entry(
            capture_entry(store, 0xe42b5161U, 108U));
        EXPECT_EQ(unsharp_fourth_threshold.value.elem_type,
                  MetaElementType::U8);
        EXPECT_EQ(unsharp_fourth_threshold.value.data.u64, 0xcdU);

        std::vector<std::byte> short_root = make_capture_root();
        const std::array<std::byte, 1> short_unsharp { std::byte { 0U } };
        append_capture_record(&short_root, 0xe42b5161U, short_unsharp);
        MetaStore short_store;
        ExifDecodeResult short_result;
        exif_internal::decode_nikon_capture(short_root, short_store,
                                            ExifDecodeOptions {},
                                            &short_result);
        EXPECT_EQ(short_result.status, ExifDecodeStatus::Ok);
        EXPECT_EQ(short_result.entries_decoded, 1U);
        short_store.finalize();
        EXPECT_EQ(capture_entry(short_store, 0xe42b5161U, 19U),
                  kInvalidEntryId);

        ExifDecodeOptions no_ifds;
        no_ifds.limits.max_ifds = 0U;
        expect_no_output(raw, ExifDecodeStatus::LimitExceeded, no_ifds);
        ExifDecodeOptions no_entries;
        no_entries.limits.max_entries_per_ifd = 0U;
        expect_no_output(raw, ExifDecodeStatus::LimitExceeded, no_entries);
        ExifDecodeOptions small_arena;
        small_arena.limits.max_arena_bytes = 1U;
        expect_no_output(raw, ExifDecodeStatus::LimitExceeded, small_arena);
    }

    TEST(NikonCaptureDecode, SkipsIncompleteMainScalarExtents)
    {
        std::vector<std::byte> raw = make_capture_root();
        const std::array<std::byte, 4> short_double {};
        append_capture_record(&raw, 0x2fc08431U, short_double);
        const std::array<std::byte, 1> short_word { std::byte { 0xffU } };
        append_capture_record(&raw, 0x76a43207U, short_word);
        const std::array<std::byte, 1> empty_version { std::byte { 0U } };
        append_capture_record(&raw, 0x3d136244U, empty_version);

        MetaStore store;
        ExifDecodeResult result;
        exif_internal::decode_nikon_capture(raw, store, ExifDecodeOptions {},
                                            &result);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        EXPECT_EQ(result.entries_decoded, 3U);
        store.finalize();

        const std::array<uint32_t, 2> short_scalar_ids {
            0x2fc08431U,
            0x76a43207U,
        };
        for (const uint32_t id : short_scalar_ids) {
            const EntryId raw_id = capture_entry(store, id, 0U);
            ASSERT_NE(raw_id, kInvalidEntryId);
            const Entry& raw_entry = store.entry(raw_id);
            EXPECT_EQ(raw_entry.value.kind, MetaValueKind::Bytes);
            EXPECT_EQ(raw_entry.origin.name_context_kind,
                      EntryNameContextKind::None);
        }
        const EntryId version_id = capture_entry(store, 0x3d136244U, 0U);
        ASSERT_NE(version_id, kInvalidEntryId);
        EXPECT_EQ(store.entry(version_id).value.kind, MetaValueKind::Text);
        EXPECT_EQ(store.entry(version_id).value.count, 0U);
    }

    TEST(NikonCaptureDecode,
         AppliesRecordFieldValueAndArenaLimitsBeforeEmission)
    {
        std::vector<std::byte> raw                = make_capture_root();
        const std::vector<std::byte> crop_payload = make_d60_crop_record();
        append_capture_record(&raw, kCropDataRecordId, crop_payload);
        const std::array<std::byte, 2> unknown_payload {
            std::byte { 0x11U },
            std::byte { 0x22U },
        };
        append_capture_record(&raw, 0x12345678U, unknown_payload);
        append_d60_terminal_and_outer_pad(&raw);

        ExifDecodeOptions record_limit;
        record_limit.limits.max_entries_per_ifd = 1U;
        expect_no_output(raw, ExifDecodeStatus::LimitExceeded, record_limit);

        std::vector<std::byte> only_crop = make_capture_root();
        append_capture_record(&only_crop, kCropDataRecordId, crop_payload);
        ExifDecodeOptions field_limit;
        field_limit.limits.max_entries_per_ifd = 12U;
        expect_no_output(only_crop, ExifDecodeStatus::LimitExceeded,
                         field_limit);

        ExifDecodeOptions total_limit;
        total_limit.limits.max_total_entries = 13U;
        expect_no_output(raw, ExifDecodeStatus::LimitExceeded, total_limit);

        ExifDecodeOptions value_limit;
        value_limit.limits.max_value_bytes = raw.size() - 1U;
        expect_no_output(raw, ExifDecodeStatus::LimitExceeded, value_limit);

        ExifDecodeOptions arena_limit;
        arena_limit.limits.max_arena_bytes = 1U;
        expect_no_output(raw, ExifDecodeStatus::LimitExceeded, arena_limit);

        ExifDecodeOptions ifd_limit;
        ifd_limit.limits.max_ifds = 1U;
        expect_no_output(raw, ExifDecodeStatus::LimitExceeded, ifd_limit);

        ExifDecodeOptions zero_ifds;
        zero_ifds.limits.max_ifds = 0U;
        expect_no_output(raw, ExifDecodeStatus::LimitExceeded, zero_ifds);
    }

    TEST(NikonCaptureDecode, AcceptsViewNxFullLengthHeaderConvention)
    {
        std::vector<std::byte> raw                = make_capture_root();
        const std::vector<std::byte> crop_payload = make_d60_crop_record();
        append_capture_record(&raw, kCropDataRecordId, crop_payload);
        write_u32le(raw, 18U, static_cast<uint32_t>(raw.size()));

        MetaStore store;
        ExifDecodeResult result;
        exif_internal::decode_nikon_capture(raw, store, ExifDecodeOptions {},
                                            &result);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        store.finalize();
        const auto crop_ids = store.find_all(
            make_exif_tag_key_view("mk_nikon_capture_374233e0", 0x002eU));
        ASSERT_EQ(crop_ids.size(), 1U);
        EXPECT_EQ(store.entry(crop_ids.front()).value.data.f64_bits,
                  0x40b7800000000000ULL);
    }

}  // namespace
}  // namespace openmeta
