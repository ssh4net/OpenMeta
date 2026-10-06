// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/meta_key.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    constexpr uint32_t kCameraInfoBytes     = 256U;
    constexpr uint32_t kISInfoBytes         = 4096U;
    constexpr uint32_t kCameraSettingsBytes = 260U;
    constexpr uint32_t kWBInfoBytes         = 18928U;

    struct Fixture final {
        std::vector<std::byte> bytes;
        uint32_t minolta_ifd = 0U;
    };

    static uint32_t align2(uint32_t value) noexcept
    {
        return (value + 1U) & ~1U;
    }

    static void put_u16(std::span<std::byte> bytes, size_t offset,
                        uint16_t value, bool le = true)
    {
        bytes[offset]      = std::byte { static_cast<uint8_t>(le ? value & 0xffU
                                                                 : value >> 8U) };
        bytes[offset + 1U] = std::byte { static_cast<uint8_t>(
            le ? value >> 8U : value & 0xffU) };
    }

    static void put_u32(std::span<std::byte> bytes, size_t offset,
                        uint32_t value)
    {
        for (uint32_t i = 0U; i < 4U; ++i) {
            bytes[offset + i] = std::byte { static_cast<uint8_t>(
                (value >> (i * 8U)) & 0xffU) };
        }
    }

    static void put_descriptor(std::span<std::byte> bytes, size_t offset,
                               uint16_t tag, uint16_t type, uint32_t count,
                               uint32_t value)
    {
        put_u16(bytes, offset, tag);
        put_u16(bytes, offset + 2U, type);
        put_u32(bytes, offset + 4U, count);
        put_u32(bytes, offset + 8U, value);
    }

    static Fixture make_fixture(std::string_view model       = "DSLR-A100",
                                bool include_maker_note      = true,
                                uint32_t b028_value          = UINT32_MAX,
                                bool malformed_camera_extent = false,
                                std::string_view make        = "SONY")
    {
        constexpr uint32_t ifd0    = 8U;
        const uint16_t ifd0_count  = include_maker_note ? 3U : 2U;
        const uint32_t make_offset = ifd0 + 2U + uint32_t(ifd0_count) * 12U
                                     + 4U;
        const uint32_t model_offset = align2(
            make_offset + static_cast<uint32_t>(make.size()) + 1U);
        const uint32_t exif_ifd = align2(
            model_offset + static_cast<uint32_t>(model.size()) + 1U);

        uint32_t maker_note   = 0U;
        uint32_t sony_ifd     = 0U;
        uint32_t minolta_ifd  = 0U;
        uint32_t camera_info  = 0U;
        uint32_t is_info      = 0U;
        uint32_t settings     = 0U;
        uint32_t wb_info      = 0U;
        uint32_t sony_payload = 0U;
        uint32_t total_size = model_offset + static_cast<uint32_t>(model.size())
                              + 1U;

        if (include_maker_note) {
            maker_note = exif_ifd + 18U;
            sony_ifd   = maker_note + 12U;
            // Keep the target IFD outside Sony's bounded 256-byte root scan
            // so the fixture selects the SONY-prefixed root at +12.
            minolta_ifd  = align2(sony_ifd + 256U);
            camera_info  = align2(minolta_ifd + 2U + 6U * 12U + 4U);
            is_info      = camera_info + kCameraInfoBytes;
            settings     = is_info + kISInfoBytes;
            wb_info      = settings + kCameraSettingsBytes;
            sony_payload = wb_info + kWBInfoBytes;
            total_size   = sony_payload + 8U;
        }

        Fixture out;
        out.bytes.resize(total_size);
        out.minolta_ifd = minolta_ifd;
        std::span<std::byte> bytes(out.bytes);
        bytes[0] = std::byte { 'I' };
        bytes[1] = std::byte { 'I' };
        put_u16(bytes, 2U, 42U);
        put_u32(bytes, 4U, ifd0);
        put_u16(bytes, ifd0, ifd0_count);
        put_descriptor(bytes, ifd0 + 2U, 0x010fU, 2U,
                       static_cast<uint32_t>(make.size() + 1U), make_offset);
        put_descriptor(bytes, ifd0 + 14U, 0x0110U, 2U,
                       static_cast<uint32_t>(model.size() + 1U), model_offset);
        if (include_maker_note) {
            put_descriptor(bytes, ifd0 + 26U, 0x8769U, 4U, 1U, exif_ifd);
        }
        std::memcpy(bytes.data() + make_offset, make.data(), make.size());
        std::memcpy(bytes.data() + model_offset, model.data(), model.size());

        if (!include_maker_note)
            return out;

        put_u16(bytes, exif_ifd, 1U);
        put_descriptor(bytes, exif_ifd + 2U, 0x927cU, 7U,
                       total_size - maker_note, maker_note);

        std::memcpy(bytes.data() + maker_note, "SONY", 4U);
        const size_t sony_header = maker_note + 12U;
        put_u16(bytes, sony_header, 2U);
        const uint32_t pointer = (b028_value == UINT32_MAX) ? minolta_ifd
                                                            : b028_value;
        put_descriptor(bytes, sony_header + 2U, 0xb028U, 4U, 1U, pointer);
        put_descriptor(bytes, sony_header + 14U, 0x0001U, 7U, 8U,
                       sony_payload - maker_note);

        put_u16(bytes, minolta_ifd, 6U);
        put_descriptor(bytes, minolta_ifd + 2U, 0x0010U, 7U, kCameraInfoBytes,
                       malformed_camera_extent ? total_size - 8U : camera_info);
        put_descriptor(bytes, minolta_ifd + 14U, 0x0018U, 7U, kISInfoBytes,
                       is_info);
        put_descriptor(bytes, minolta_ifd + 26U, 0x0020U, 7U, kWBInfoBytes,
                       wb_info);
        put_descriptor(bytes, minolta_ifd + 38U, 0x0106U, 3U, 1U, 0x0055U);
        put_descriptor(bytes, minolta_ifd + 50U, 0x010dU, 3U, 1U, 0x0066U);
        put_descriptor(bytes, minolta_ifd + 62U, 0x0114U, 7U,
                       kCameraSettingsBytes, settings);

        // CameraInfoA100 uses little-endian signed words.
        bytes[camera_info + 1U] = std::byte { 5U };
        put_u16(bytes, camera_info + 2U, 0xffecU);
        // ISInfoA100 and WBInfoA100 use big-endian words.
        put_u16(bytes, is_info, 0x1234U, false);
        bytes[wb_info + 43U] = std::byte { 7U };
        bytes[wb_info + 45U] = std::byte { 9U };
        put_u16(bytes, wb_info + 150U, 0x1020U, false);
        put_u16(bytes, wb_info + 152U, 0x3040U, false);
        put_u16(bytes, wb_info + 154U, 0x5060U, false);
        std::memcpy(bytes.data() + wb_info + 4172U, "RAW", 3U);
        std::memcpy(bytes.data() + wb_info + 18872U, "A100", 4U);
        std::memcpy(bytes.data() + wb_info + 18908U, "WB-A100", 7U);
        // CameraSettingsA100 uses big-endian words, including signed overrides.
        put_u16(bytes, settings + 100U, 0x0203U, false);
        put_u16(bytes, settings + 104U, 0x0405U, false);
        put_u16(bytes, settings + 106U, 0x0607U, false);
        put_u16(bytes, settings + 108U, 0x0809U, false);
        put_u16(bytes, settings + 112U, 0xfffeU, false);
        return out;
    }

    struct ReadState final {
        std::span<const std::byte> bytes;
    };

    static RandomAccessIoResult read_at(void* context, uint64_t offset,
                                        std::span<std::byte> output) noexcept
    {
        const auto* state = static_cast<const ReadState*>(context);
        if (offset > state->bytes.size()
            || output.size() > state->bytes.size() - offset) {
            return { RandomAccessIoCode::Ok, 0U };
        }
        std::memcpy(output.data(), state->bytes.data() + offset, output.size());
        return { RandomAccessIoCode::Ok, output.size() };
    }

    static ExifDecodeResult decode_fixture(const Fixture& fixture,
                                           MetaStore& store,
                                           ExifDecodeOptions options = {},
                                           bool callback             = false,
                                           bool finalize_store       = true)
    {
        options.decode_makernote = true;
        std::array<ExifIfdRef, 16> ifds {};
        if (!callback) {
            const ExifDecodeResult result
                = decode_exif_tiff(fixture.bytes, store, ifds, options);
            if (finalize_store)
                store.finalize();
            return result;
        }

        ReadState state { fixture.bytes };
        RandomAccessSource source;
        source.size    = fixture.bytes.size();
        source.context = &state;
        source.read_at = read_at;
        std::array<std::byte, 32> window {};
        std::vector<std::byte> value_scratch(65536U);
        ExifRandomAccessScratch scratch;
        scratch.read_window                       = window;
        scratch.value                             = value_scratch;
        scratch.window_options.minimum_read_bytes = 0U;
        const ExifRandomAccessDecodeResult result
            = decode_exif_tiff_random_access(make_random_access_source_range(
                                                 source),
                                             store, ifds, scratch, options);
        EXPECT_TRUE(result.complete());
        if (finalize_store)
            store.finalize();
        return result.decode;
    }

    static const Entry* find_one(const MetaStore& store, std::string_view ifd,
                                 uint16_t tag)
    {
        const std::span<const EntryId> ids = store.find_all(
            make_exif_tag_key_view(ifd, tag));
        return ids.size() == 1U ? &store.entry(ids[0]) : nullptr;
    }

    static void expect_u8(const MetaStore& store, std::string_view ifd,
                          uint16_t tag, uint8_t expected)
    {
        const Entry* entry = find_one(store, ifd, tag);
        ASSERT_NE(entry, nullptr) << ifd << '/' << tag;
        EXPECT_EQ(entry->value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(entry->value.elem_type, MetaElementType::U8);
        EXPECT_EQ(entry->value.data.u64, expected);
    }

    static void expect_u16(const MetaStore& store, std::string_view ifd,
                           uint16_t tag, uint16_t expected)
    {
        const Entry* entry = find_one(store, ifd, tag);
        ASSERT_NE(entry, nullptr) << ifd << '/' << tag;
        EXPECT_EQ(entry->value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(entry->value.elem_type, MetaElementType::U16);
        EXPECT_EQ(entry->value.data.u64, expected);
    }

    static void expect_a100_values(const MetaStore& store)
    {
        expect_u8(store, "mk_minolta_camerainfoa100_0", 0x0001U, 5U);
        const Entry* signed_info
            = find_one(store, "mk_minolta_camerainfoa100_0", 0x0002U);
        ASSERT_NE(signed_info, nullptr);
        EXPECT_EQ(signed_info->value.elem_type, MetaElementType::I16);
        EXPECT_EQ(signed_info->value.data.i64, -20);

        expect_u16(store, "mk_minolta_isinfoa100_0", 0x0000U, 0x1234U);
        expect_u8(store, "mk_minolta_wbinfoa100_0", 0x002bU, 7U);
        expect_u8(store, "mk_minolta_wbinfoa100_0", 0x002dU, 9U);
        const Entry* wb_array = find_one(store, "mk_minolta_wbinfoa100_0",
                                         0x0096U);
        ASSERT_NE(wb_array, nullptr);
        ASSERT_EQ(wb_array->value.kind, MetaValueKind::Array);
        EXPECT_EQ(wb_array->value.elem_type, MetaElementType::U16);
        ASSERT_EQ(wb_array->value.count, 3U);
        std::array<uint16_t, 3> wb_values {};
        const std::span<const std::byte> wb_raw = store.arena().span(
            wb_array->value.data.span);
        ASSERT_EQ(wb_raw.size(), sizeof(wb_values));
        std::memcpy(wb_values.data(), wb_raw.data(), wb_raw.size());
        EXPECT_EQ(wb_values,
                  (std::array<uint16_t, 3> { 0x1020U, 0x3040U, 0x5060U }));
        const Entry* wb_bytes = find_one(store, "mk_minolta_wbinfoa100_0",
                                         0x104cU);
        ASSERT_NE(wb_bytes, nullptr);
        ASSERT_EQ(wb_bytes->value.kind, MetaValueKind::Bytes);
        ASSERT_EQ(wb_bytes->value.count, 9600U);
        const std::span<const std::byte> wb_byte_values = store.arena().span(
            wb_bytes->value.data.span);
        EXPECT_EQ(std::string_view(
                      reinterpret_cast<const char*>(wb_byte_values.data()), 3U),
                  "RAW");
        const Entry* wb_text = find_one(store, "mk_minolta_wbinfoa100_0",
                                        0x49dcU);
        ASSERT_NE(wb_text, nullptr);
        ASSERT_EQ(wb_text->value.kind, MetaValueKind::Text);
        EXPECT_EQ(std::string_view(
                      reinterpret_cast<const char*>(
                          store.arena().span(wb_text->value.data.span).data()),
                      wb_text->value.count),
                  "WB-A100");

        expect_u16(store, "mk_minolta_camerasettingsa100_0", 0x0032U, 0x0203U);
        expect_u16(store, "mk_minolta_camerasettingsa100_0", 0x0034U, 0x0405U);
        expect_u16(store, "mk_minolta_camerasettingsa100_0", 0x0035U, 0x0607U);
        expect_u16(store, "mk_minolta_camerasettingsa100_0", 0x0036U, 0x0809U);
        const Entry* signed_setting
            = find_one(store, "mk_minolta_camerasettingsa100_0", 0x0038U);
        ASSERT_NE(signed_setting, nullptr);
        EXPECT_EQ(signed_setting->value.elem_type, MetaElementType::I16);
        EXPECT_EQ(signed_setting->value.data.i64, -2);

        expect_u16(store, "mk_minolta0", 0x0106U, 0x0055U);
        expect_u16(store, "mk_minolta0", 0x010dU, 0x0066U);
    }

    static size_t count_entries(const MetaStore& store, std::string_view ifd,
                                uint16_t tag)
    {
        return store.find_all(make_exif_tag_key_view(ifd, tag)).size();
    }

    TEST(MinoltaA100Gap, SpanAndCallbackProjectTypedFieldsAndPreserveRoot)
    {
        const Fixture fixture = make_fixture();
        MetaStore span_store;
        MetaStore callback_store;
        const ExifDecodeResult span_result = decode_fixture(fixture,
                                                            span_store);
        const ExifDecodeResult callback_result
            = decode_fixture(fixture, callback_store, {}, true);

        EXPECT_EQ(span_result.status, ExifDecodeStatus::Ok);
        EXPECT_EQ(callback_result.status, span_result.status);
        EXPECT_EQ(callback_result.entries_decoded, span_result.entries_decoded);
        EXPECT_EQ(callback_result.ifds_needed, span_result.ifds_needed);
        EXPECT_EQ(span_result.ifds_needed, 2U);
        expect_a100_values(span_store);
        expect_a100_values(callback_store);
    }

    TEST(MinoltaA100Gap, ExactIfdAndEntryBudgetsPassAndOneUnderFails)
    {
        const Fixture fixture = make_fixture();
        for (const bool callback : { false, true }) {
            SCOPED_TRACE(callback ? "callback" : "span");
            ExifDecodeOptions exact;
            exact.limits.max_ifds            = 7U;
            exact.limits.max_total_entries   = 172U;
            exact.limits.max_entries_per_ifd = 78U;
            MetaStore exact_store;
            const ExifDecodeResult exact_result
                = decode_fixture(fixture, exact_store, exact, callback);
            EXPECT_EQ(exact_result.status, ExifDecodeStatus::Ok);
            EXPECT_EQ(exact_result.entries_decoded, 172U);
            EXPECT_EQ(exact_result.ifds_needed, 2U);
            expect_a100_values(exact_store);

            ExifDecodeOptions one_ifd_short = exact;
            one_ifd_short.limits.max_ifds   = 6U;
            MetaStore ifd_store;
            const ExifDecodeResult ifd_result
                = decode_fixture(fixture, ifd_store, one_ifd_short, callback);
            EXPECT_EQ(ifd_result.status, ExifDecodeStatus::LimitExceeded);
            EXPECT_EQ(ifd_result.limit_reason, ExifLimitReason::MaxIfds);
            EXPECT_EQ(count_entries(ifd_store, "mk_minolta0", 0x0010U), 0U);

            ExifDecodeOptions one_entry_short        = exact;
            one_entry_short.limits.max_total_entries = 171U;
            MetaStore entry_store;
            const ExifDecodeResult entry_result
                = decode_fixture(fixture, entry_store, one_entry_short,
                                 callback);
            EXPECT_EQ(entry_result.status, ExifDecodeStatus::LimitExceeded);
            EXPECT_EQ(entry_result.limit_reason,
                      ExifLimitReason::MaxTotalEntries);
            EXPECT_EQ(count_entries(entry_store, "mk_minolta0", 0x0010U), 0U);

            ExifDecodeOptions one_table_entry_short          = exact;
            one_table_entry_short.limits.max_entries_per_ifd = 77U;
            MetaStore table_store;
            const ExifDecodeResult table_result
                = decode_fixture(fixture, table_store, one_table_entry_short,
                                 callback);
            EXPECT_EQ(table_result.status, ExifDecodeStatus::LimitExceeded);
            EXPECT_EQ(table_result.limit_reason,
                      ExifLimitReason::MaxEntriesPerIfd);
            EXPECT_EQ(count_entries(table_store, "mk_minolta0", 0x0010U), 0U);
        }
    }

    TEST(MinoltaA100Gap, ExactModelZeroPointerAndMalformedExtentAreGated)
    {
        for (const bool callback : { false, true }) {
            SCOPED_TRACE(callback ? "callback" : "span");
            {
                const Fixture fixture = make_fixture("DSLR-A100X");
                MetaStore store;
                const ExifDecodeResult result = decode_fixture(fixture, store,
                                                               {}, callback);
                EXPECT_NE(result.status, ExifDecodeStatus::LimitExceeded);
                EXPECT_EQ(count_entries(store, "mk_minolta0", 0x0010U), 0U);
            }
            {
                const Fixture fixture = make_fixture("DSLR-A100", true, 0U);
                MetaStore store;
                const ExifDecodeResult result = decode_fixture(fixture, store,
                                                               {}, callback);
                EXPECT_NE(result.status, ExifDecodeStatus::LimitExceeded);
                EXPECT_EQ(count_entries(store, "mk_minolta0", 0x0010U), 0U);
            }
            {
                const Fixture fixture = make_fixture("DSLR-A100", true,
                                                     UINT32_MAX, true);
                MetaStore store;
                const ExifDecodeResult result = decode_fixture(fixture, store,
                                                               {}, callback);
                EXPECT_EQ(result.status, ExifDecodeStatus::Malformed);
                EXPECT_EQ(count_entries(store, "mk_minolta0", 0x0010U), 0U);
            }
        }
    }

    TEST(MinoltaA100Gap, AppendingSourcesDoesNotReuseOldModelOrB028)
    {
        const Fixture a100               = make_fixture();
        const Fixture other_model        = make_fixture("OTHER", false);
        const Fixture no_current_pointer = make_fixture("DSLR-A100", false);

        for (const bool callback : { false, true }) {
            SCOPED_TRACE(callback ? "callback" : "span");
            for (const Fixture* current :
                 { &other_model, &no_current_pointer }) {
                MetaStore store;
                const ExifDecodeResult first = decode_fixture(a100, store, {},
                                                              callback, false);
                ASSERT_EQ(first.status, ExifDecodeStatus::Ok);
                const ExifDecodeResult second
                    = decode_fixture(*current, store, {}, callback, false);
                EXPECT_EQ(second.status, ExifDecodeStatus::Ok);
                store.finalize();
                EXPECT_EQ(count_entries(store, "mk_minolta0", 0x0010U), 1U);
                EXPECT_EQ(count_entries(store, "mk_minolta_camerainfoa100_0",
                                        0x0001U),
                          1U);
            }
        }
    }

    TEST(MinoltaA100Gap, AppendingSecondA100DoesNotReexpandFirstRoot)
    {
        const Fixture first_fixture  = make_fixture();
        const Fixture second_fixture = make_fixture();
        struct TableCheck final {
            std::string_view ifd_zero;
            std::string_view ifd_one;
            uint16_t tag;
        };
        const std::array<TableCheck, 4> checks = {
            TableCheck { "mk_minolta_camerainfoa100_0",
                         "mk_minolta_camerainfoa100_1", 0x0001U },
            TableCheck { "mk_minolta_isinfoa100_0", "mk_minolta_isinfoa100_1",
                         0x0000U },
            TableCheck { "mk_minolta_wbinfoa100_0", "mk_minolta_wbinfoa100_1",
                         0x002bU },
            TableCheck { "mk_minolta_camerasettingsa100_0",
                         "mk_minolta_camerasettingsa100_1", 0x0032U },
        };
        for (const bool callback : { false, true }) {
            SCOPED_TRACE(callback ? "callback" : "span");
            MetaStore store;
            const ExifDecodeResult first = decode_fixture(first_fixture, store,
                                                          {}, callback, false);
            ASSERT_EQ(first.status, ExifDecodeStatus::Ok);
            const ExifDecodeResult second
                = decode_fixture(second_fixture, store, {}, callback, false);
            EXPECT_EQ(second.status, ExifDecodeStatus::Ok);
            store.finalize();

            EXPECT_EQ(count_entries(store, "mk_minolta0", 0x0010U), 2U);
            for (const TableCheck& check : checks) {
                const size_t total
                    = count_entries(store, check.ifd_zero, check.tag)
                      + count_entries(store, check.ifd_one, check.tag);
                EXPECT_EQ(total, 2U);
                EXPECT_EQ(count_entries(store, check.ifd_zero, check.tag), 2U);
                EXPECT_EQ(count_entries(store, check.ifd_one, check.tag), 0U);
            }
        }
    }

    TEST(MinoltaA100Gap, EarlierNonSonyImageDoesNotSuppressCurrentA100)
    {
        for (const bool callback : { false, true }) {
            SCOPED_TRACE(callback ? "callback" : "span");
            const Fixture earlier = make_fixture("OLD-MODEL", false, UINT32_MAX,
                                                 false, "Canon");
            const Fixture current = make_fixture();
            MetaStore store;
            ASSERT_EQ(decode_fixture(earlier, store, {}, callback, false).status,
                      ExifDecodeStatus::Ok);
            const ExifDecodeResult result = decode_fixture(current, store, {},
                                                           callback, false);
            EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
            store.finalize();
            expect_a100_values(store);
        }
    }

}  // namespace
}  // namespace openmeta
