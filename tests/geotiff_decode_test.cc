// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"

#include "openmeta/geotiff_key_names.h"
#include "openmeta/meta_store.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    struct CallbackState final {
        std::span<const std::byte> bytes;
    };


    static RandomAccessIoResult
    read_at(void* context, uint64_t offset,
            std::span<std::byte> destination) noexcept
    {
        const CallbackState* state = static_cast<const CallbackState*>(context);
        if (offset > state->bytes.size()
            || destination.size() > state->bytes.size() - offset) {
            return RandomAccessIoResult { RandomAccessIoCode::Ok, 0U };
        }
        std::memcpy(destination.data(),
                    state->bytes.data() + static_cast<size_t>(offset),
                    destination.size());
        return RandomAccessIoResult { RandomAccessIoCode::Ok,
                                      destination.size() };
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
        for (uint32_t i = 0; i < 8; ++i) {
            out->push_back(
                std::byte { static_cast<uint8_t>((v >> (i * 8U)) & 0xFF) });
        }
    }


    TEST(GeoTiff, KeyNameLookup)
    {
        EXPECT_EQ(geotiff_key_name(1024), "GTModelTypeGeoKey");
        EXPECT_EQ(geotiff_key_name(4099), "VerticalUnitsGeoKey");
        EXPECT_TRUE(geotiff_key_name(0).empty());
    }


    TEST(GeoTiff, DecodeKeysFromTiffIfd0)
    {
        std::vector<std::byte> tiff;
        tiff.reserve(256);

        // TIFF header (LE) + classic IFD0 at offset 8.
        tiff.push_back(std::byte { 'I' });
        tiff.push_back(std::byte { 'I' });
        append_u16le(&tiff, 42);
        append_u32le(&tiff, 8);

        while (tiff.size() < 8) {
            tiff.push_back(std::byte { 0 });
        }

        const uint32_t entry_count = 3;
        const uint32_t ifd0_off    = 8;
        const uint32_t entries_off = ifd0_off + 2;
        const uint32_t next_off    = entries_off + entry_count * 12;
        const uint32_t data_start  = next_off + 4;

        // Compute value offsets.
        const uint32_t geo_dir_off   = data_start;
        const uint32_t geo_dir_bytes = 32;  // 16 u16
        uint32_t geo_dbl_off         = geo_dir_off + geo_dir_bytes;
        if ((geo_dbl_off & 7U) != 0U) {
            geo_dbl_off += 8U - (geo_dbl_off & 7U);
        }
        const uint32_t geo_ascii_off = geo_dbl_off + 8;

        constexpr std::string_view kAscii = "TestCitation|";

        // IFD0 entries.
        append_u16le(&tiff, static_cast<uint16_t>(entry_count));
        // GeoKeyDirectoryTag (0x87AF), SHORT[16].
        append_u16le(&tiff, 0x87AF);
        append_u16le(&tiff, 3);
        append_u32le(&tiff, 16);
        append_u32le(&tiff, geo_dir_off);
        // GeoDoubleParamsTag (0x87B0), DOUBLE[1].
        append_u16le(&tiff, 0x87B0);
        append_u16le(&tiff, 12);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, geo_dbl_off);
        // GeoAsciiParamsTag (0x87B1), ASCII[n].
        append_u16le(&tiff, 0x87B1);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, static_cast<uint32_t>(kAscii.size()));
        append_u32le(&tiff, geo_ascii_off);

        // next IFD offset = 0.
        append_u32le(&tiff, 0);

        ASSERT_EQ(tiff.size(), data_start);

        // GeoKeyDirectoryTag payload:
        // header (4) + 3 keys.
        append_u16le(&tiff, 1);  // KeyDirectoryVersion
        append_u16le(&tiff, 1);  // KeyRevision
        append_u16le(&tiff, 0);  // MinorRevision
        append_u16le(&tiff, 3);  // NumberOfKeys
        // Key 0: GTModelTypeGeoKey=1024, direct value=2.
        append_u16le(&tiff, 1024);
        append_u16le(&tiff, 0);
        append_u16le(&tiff, 1);
        append_u16le(&tiff, 2);
        // Key 1: GTCitationGeoKey=1026, ASCII[13] from GeoAsciiParamsTag offset 0.
        append_u16le(&tiff, 1026);
        append_u16le(&tiff, 0x87B1);
        append_u16le(&tiff, static_cast<uint16_t>(kAscii.size()));
        append_u16le(&tiff, 0);
        // Key 2: GeogSemiMajorAxisGeoKey=2057, DOUBLE[1] from GeoDoubleParamsTag index 0.
        append_u16le(&tiff, 2057);
        append_u16le(&tiff, 0x87B0);
        append_u16le(&tiff, 1);
        append_u16le(&tiff, 0);

        ASSERT_EQ(tiff.size(), geo_dir_off + geo_dir_bytes);

        while (tiff.size() < geo_dbl_off) {
            tiff.push_back(std::byte { 0 });
        }

        // GeoDoubleParamsTag: semi-major axis.
        const double semi_major = 6378137.0;
        uint64_t bits           = 0;
        std::memcpy(&bits, &semi_major, sizeof(bits));
        append_u64le(&tiff, bits);

        ASSERT_EQ(tiff.size(), geo_ascii_off);
        for (char c : kAscii) {
            tiff.push_back(std::byte { static_cast<uint8_t>(c) });
        }

        MetaStore store;
        std::array<ExifIfdRef, 8> ifds {};
        ExifDecodeOptions opts;
        opts.decode_geotiff = true;
        (void)decode_exif_tiff(std::span<const std::byte>(tiff.data(),
                                                          tiff.size()),
                               store, ifds, opts);
        store.finalize();

        auto find_one = [&](uint16_t key_id) -> const Entry* {
            MetaKeyView k;
            k.kind                             = MetaKeyKind::GeotiffKey;
            k.data.geotiff_key.key_id          = key_id;
            const std::span<const EntryId> ids = store.find_all(k);
            if (ids.size() != 1) {
                return nullptr;
            }
            return &store.entry(ids[0]);
        };

        const Entry* e_model = find_one(1024);
        ASSERT_NE(e_model, nullptr);
        EXPECT_EQ(e_model->value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e_model->value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e_model->value.data.u64, 2U);

        const Entry* e_cit = find_one(1026);
        ASSERT_NE(e_cit, nullptr);
        EXPECT_EQ(e_cit->value.kind, MetaValueKind::Text);
        const std::span<const std::byte> cit_bytes = store.arena().span(
            e_cit->value.data.span);
        const std::string_view cit(reinterpret_cast<const char*>(
                                       cit_bytes.data()),
                                   cit_bytes.size());
        EXPECT_EQ(cit, "TestCitation");

        const Entry* e_axis = find_one(2057);
        ASSERT_NE(e_axis, nullptr);
        EXPECT_EQ(e_axis->value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e_axis->value.elem_type, MetaElementType::F64);
        const double got = std::bit_cast<double>(e_axis->value.data.f64_bits);
        EXPECT_NEAR(got, semi_major, 1e-6);

        CallbackState callback { tiff };
        const RandomAccessSource source
            = make_callback_random_access_source(tiff.size(), &callback,
                                                 read_at);
        std::array<std::byte, 20> read_window {};
        std::array<std::byte, 128> value_scratch {};
        ExifRandomAccessScratch scratch;
        scratch.read_window                       = read_window;
        scratch.value                             = value_scratch;
        scratch.window_options.minimum_read_bytes = read_window.size();
        MetaStore callback_store;
        const ExifRandomAccessDecodeResult callback_result
            = decode_exif_tiff_random_access(make_random_access_source_range(
                                                 source),
                                             callback_store, {}, scratch, opts);
        EXPECT_TRUE(callback_result.complete());
        EXPECT_EQ(callback_result.decode.status, ExifDecodeStatus::Ok);
        callback_store.finalize();
        MetaKeyView callback_key;
        callback_key.kind                           = MetaKeyKind::GeotiffKey;
        callback_key.data.geotiff_key.key_id        = 1026U;
        const std::span<const EntryId> callback_ids = callback_store.find_all(
            callback_key);
        ASSERT_EQ(callback_ids.size(), 1U);
        const std::span<const std::byte> callback_text
            = callback_store.arena().span(
                callback_store.entry(callback_ids[0]).value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(
                                       callback_text.data()),
                                   callback_text.size()),
                  "TestCitation");
    }

}  // namespace
}  // namespace openmeta
