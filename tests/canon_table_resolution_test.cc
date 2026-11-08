// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tiff_decode.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace openmeta {
namespace {

    constexpr uint64_t kMakerNoteOffset      = 128U;
    constexpr uint64_t kDirectoryBytes       = 18U;
    constexpr uint32_t kCameraSettingsOffset = 128U;
    constexpr uint32_t kCameraSettingsCount  = 32U;

    static void write_u16(std::vector<std::byte>* bytes, size_t offset,
                          uint16_t value, bool little_endian) noexcept
    {
        if (little_endian) {
            (*bytes)[offset] = std::byte { static_cast<uint8_t>(value & 0xffU) };
            (*bytes)[offset + 1U]
                = std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) };
        } else {
            (*bytes)[offset]
                = std::byte { static_cast<uint8_t>((value >> 8U) & 0xffU) };
            (*bytes)[offset + 1U]
                = std::byte { static_cast<uint8_t>(value & 0xffU) };
        }
    }

    static void write_u32(std::vector<std::byte>* bytes, size_t offset,
                          uint32_t value, bool little_endian) noexcept
    {
        if (little_endian) {
            for (uint32_t i = 0U; i < 4U; ++i) {
                (*bytes)[offset + i] = std::byte { static_cast<uint8_t>(
                    (value >> (8U * i)) & 0xffU) };
            }
        } else {
            for (uint32_t i = 0U; i < 4U; ++i) {
                (*bytes)[offset + i] = std::byte { static_cast<uint8_t>(
                    (value >> (8U * (3U - i))) & 0xffU) };
            }
        }
    }

    using CameraSettings = std::array<uint16_t, kCameraSettingsCount>;

    static CameraSettings
    make_sequential_camera_settings(uint16_t first) noexcept
    {
        CameraSettings values {};
        for (uint32_t i = 0U; i < kCameraSettingsCount; ++i) {
            values[i] = static_cast<uint16_t>(first + i);
        }
        return values;
    }

    static CameraSettings make_expected_relative_camera_settings() noexcept
    {
        return CameraSettings {
            64U, 1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U,
            11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U,
            22U, 23U, 24U, 25U, 26U, 27U, 28U, 29U, 30U, 31U,
        };
    }

    static void write_camera_settings(std::vector<std::byte>* bytes,
                                      size_t offset,
                                      std::span<const uint16_t> values,
                                      bool little_endian) noexcept
    {
        for (size_t i = 0U; i < values.size(); ++i) {
            write_u16(bytes, offset + i * 2U, values[i], little_endian);
        }
    }

    static std::vector<std::byte>
    make_canon_tiff(bool parent_little_endian, bool maker_note_little_endian,
                    uint32_t stored_offset, uint32_t maker_note_count,
                    size_t selected_table_offset,
                    std::span<const uint16_t> selected_values,
                    size_t alternate_table_offset,
                    std::span<const uint16_t> alternate_values)
    {
        std::vector<std::byte> tiff(512U, std::byte { 0 });

        tiff[0] = parent_little_endian ? std::byte { 'I' } : std::byte { 'M' };
        tiff[1] = tiff[0];
        write_u16(&tiff, 2U, 42U, parent_little_endian);
        write_u32(&tiff, 4U, 8U, parent_little_endian);

        // IFD0: Make and ExifIFD pointer.
        write_u16(&tiff, 8U, 2U, parent_little_endian);
        write_u16(&tiff, 10U, 0x010fU, parent_little_endian);
        write_u16(&tiff, 12U, 2U, parent_little_endian);
        write_u32(&tiff, 14U, 6U, parent_little_endian);
        write_u32(&tiff, 18U, 38U, parent_little_endian);
        write_u16(&tiff, 22U, 0x8769U, parent_little_endian);
        write_u16(&tiff, 24U, 4U, parent_little_endian);
        write_u32(&tiff, 26U, 1U, parent_little_endian);
        write_u32(&tiff, 30U, 44U, parent_little_endian);
        write_u32(&tiff, 34U, 0U, parent_little_endian);
        tiff[38U] = std::byte { 'C' };
        tiff[39U] = std::byte { 'a' };
        tiff[40U] = std::byte { 'n' };
        tiff[41U] = std::byte { 'o' };
        tiff[42U] = std::byte { 'n' };

        // ExifIFD: MakerNote tag 0x927c at TIFF offset 44.
        write_u16(&tiff, 44U, 1U, parent_little_endian);
        write_u16(&tiff, 46U, 0x927cU, parent_little_endian);
        write_u16(&tiff, 48U, 7U, parent_little_endian);
        write_u32(&tiff, 50U, maker_note_count, parent_little_endian);
        write_u32(&tiff, 54U, static_cast<uint32_t>(kMakerNoteOffset),
                  parent_little_endian);
        write_u32(&tiff, 58U, 0U, parent_little_endian);

        // Canon main IFD: one SHORT[32] CameraSettings entry.
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset), 1U,
                  maker_note_little_endian);
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset) + 2U, 0x0001U,
                  maker_note_little_endian);
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset) + 4U, 3U,
                  maker_note_little_endian);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 6U,
                  kCameraSettingsCount, maker_note_little_endian);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 10U,
                  stored_offset, maker_note_little_endian);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 14U, 0U,
                  maker_note_little_endian);

        write_camera_settings(&tiff, selected_table_offset, selected_values,
                              maker_note_little_endian);
        if (alternate_table_offset != selected_table_offset) {
            write_camera_settings(&tiff, alternate_table_offset,
                                  alternate_values, maker_note_little_endian);
        }
        return tiff;
    }

    static std::vector<std::byte> make_canon_mixed_table_and_ascii_tiff()
    {
        const CameraSettings expected = make_expected_relative_camera_settings();
        std::vector<std::byte> tiff
            = make_canon_tiff(true, true, kCameraSettingsOffset, 30U, 256U,
                              expected, 256U, expected);

        // Add an ASCII value whose absolute address would strongly favor the same
        // base that points CameraSettings into its directory.
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset), 2U, true);
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset) + 14U, 0x0006U,
                  true);
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset) + 16U, 2U, true);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 18U, 8U, true);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 22U, 320U,
                  true);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 26U, 0U, true);

        constexpr uint8_t kAbsoluteAscii[] = { 'E', 'O', 'S', 'M',
                                               'I', 'X', 'E', 'D' };
        for (size_t i = 0U; i < sizeof(kAbsoluteAscii); ++i) {
            tiff[320U + i] = std::byte { kAbsoluteAscii[i] };
            tiff[350U + i] = std::byte { 0x01U };
            tiff[448U + i] = std::byte { 0x02U };
        }
        return tiff;
    }

    static std::vector<std::byte> make_canon_no_eligible_base_tiff()
    {
        const CameraSettings values = make_expected_relative_camera_settings();
        std::vector<std::byte> tiff
            = make_canon_tiff(true, true, kCameraSettingsOffset, 30U, 256U,
                              values, 256U, values);
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset), 2U, true);
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset) + 14U, 0x7777U,
                  true);
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset) + 16U, 7U, true);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 18U, 192U,
                  true);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 22U, 0U, true);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 26U, 0U, true);
        return tiff;
    }

    static MetaKeyView canon_key(std::string_view ifd, uint16_t tag) noexcept
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }

    static const Entry* find_unique_entry(const MetaStore& store,
                                          std::string_view ifd,
                                          uint16_t tag) noexcept
    {
        const std::span<const EntryId> ids = store.find_all(
            canon_key(ifd, tag));
        if (ids.size() != 1U) {
            return nullptr;
        }
        return &store.entry(ids[0]);
    }

    static bool read_u16_array(const MetaStore& store, const Entry& entry,
                               std::span<uint16_t> values) noexcept
    {
        if (entry.value.kind != MetaValueKind::Array
            || entry.value.elem_type != MetaElementType::U16
            || entry.value.count != values.size()) {
            return false;
        }
        const std::span<const std::byte> raw = store.arena().span(
            entry.value.data.span);
        if (raw.size() != values.size_bytes()) {
            return false;
        }
        std::memcpy(values.data(), raw.data(), raw.size());
        return true;
    }

    static void expect_canon_camera_settings(const MetaStore& store,
                                             std::span<const uint16_t> expected)
    {
        ASSERT_EQ(expected.size(), kCameraSettingsCount);
        const Entry* raw = find_unique_entry(store, "mk_canon0", 0x0001U);
        ASSERT_NE(raw, nullptr);
        ASSERT_EQ(raw->value.kind, MetaValueKind::Array);
        ASSERT_EQ(raw->value.elem_type, MetaElementType::U16);
        ASSERT_EQ(raw->value.count, kCameraSettingsCount);
        EXPECT_EQ(raw->origin.wire_type.family, WireFamily::Tiff);
        EXPECT_EQ(raw->origin.wire_type.code, 3U);
        EXPECT_EQ(raw->origin.wire_count, kCameraSettingsCount);

        std::array<uint16_t, kCameraSettingsCount> raw_values {};
        ASSERT_TRUE(read_u16_array(store, *raw, raw_values));
        for (uint32_t i = 0U; i < kCameraSettingsCount; ++i) {
            EXPECT_EQ(raw_values[i], expected[i])
                << "raw CameraSettings index " << i;

            const Entry* derived
                = find_unique_entry(store, "mk_canon_camerasettings_0",
                                    static_cast<uint16_t>(i));
            ASSERT_NE(derived, nullptr) << "derived CameraSettings index " << i;
            EXPECT_EQ(derived->value.kind, MetaValueKind::Scalar);
            EXPECT_EQ(derived->value.elem_type, MetaElementType::U16);
            EXPECT_EQ(derived->value.data.u64, expected[i]);
            EXPECT_TRUE(any(derived->flags, EntryFlags::Derived));
        }
    }

    static void expect_no_eligible_base_fallback(const MetaStore& store)
    {
        const Entry* camera_settings = find_unique_entry(store, "mk_canon0",
                                                         0x0001U);
        ASSERT_NE(camera_settings, nullptr);
        EXPECT_EQ(camera_settings->value.kind, MetaValueKind::Empty);
        EXPECT_NE(camera_settings->origin.block, kInvalidBlockId);
        EXPECT_EQ(camera_settings->origin.order_in_block, 0U);
        EXPECT_EQ(camera_settings->origin.wire_type.family, WireFamily::Tiff);
        EXPECT_EQ(camera_settings->origin.wire_type.code, 3U);
        EXPECT_EQ(camera_settings->origin.wire_count, kCameraSettingsCount);
        EXPECT_TRUE(any(camera_settings->flags, EntryFlags::Unreadable));
        for (uint32_t i = 0U; i < kCameraSettingsCount; ++i) {
            EXPECT_TRUE(store
                            .find_all(canon_key("mk_canon_camerasettings_0",
                                                static_cast<uint16_t>(i)))
                            .empty());
        }

        const Entry* opaque = find_unique_entry(store, "mk_canon0", 0x7777U);
        ASSERT_NE(opaque, nullptr);
        EXPECT_EQ(opaque->value.kind, MetaValueKind::Empty);
        EXPECT_EQ(opaque->origin.block, camera_settings->origin.block);
        EXPECT_EQ(opaque->origin.order_in_block, 1U);
        EXPECT_EQ(opaque->origin.wire_type.family, WireFamily::Tiff);
        EXPECT_EQ(opaque->origin.wire_type.code, 7U);
        EXPECT_EQ(opaque->origin.wire_count, 192U);
        EXPECT_TRUE(any(opaque->flags, EntryFlags::Unreadable));
    }

    struct CallbackState final {
        std::span<const std::byte> bytes;
        uint32_t calls = 0U;
    };

    static RandomAccessIoResult
    read_callback(void* context, uint64_t offset,
                  std::span<std::byte> destination) noexcept
    {
        CallbackState* state = static_cast<CallbackState*>(context);
        ++state->calls;
        if (offset > state->bytes.size()
            || destination.size() > state->bytes.size() - offset) {
            return RandomAccessIoResult { RandomAccessIoCode::Ok, 0U };
        }
        if (!destination.empty()) {
            std::memcpy(destination.data(),
                        state->bytes.data() + static_cast<size_t>(offset),
                        destination.size());
        }
        return RandomAccessIoResult {
            RandomAccessIoCode::Ok, static_cast<uint64_t>(destination.size())
        };
    }

    static ExifRandomAccessDecodeResult
    decode_with_callback(std::span<const std::byte> tiff, MetaStore& store,
                         uint32_t* callback_calls_out)
    {
        std::vector<std::byte> backing(23U, std::byte { 0x5aU });
        backing.insert(backing.end(), tiff.begin(), tiff.end());
        CallbackState callback_state { backing };
        const RandomAccessSource source = make_callback_random_access_source(
            backing.size(), &callback_state, read_callback);
        const RandomAccessSourceRange range
            = make_random_access_source_range(source, 23U, tiff.size());
        std::array<std::byte, 64> read_window_storage {};
        std::array<std::byte, 4096> value_storage {};
        ExifRandomAccessScratch scratch;
        scratch.read_window                       = read_window_storage;
        scratch.value                             = value_storage;
        scratch.window_options.minimum_read_bytes = 64U;
        std::array<ExifIfdRef, 16> ifds {};
        ExifDecodeOptions options;
        options.decode_makernote = true;
        const ExifRandomAccessDecodeResult result
            = decode_exif_tiff_random_access(range, store, ifds, scratch,
                                             options);
        if (callback_calls_out != nullptr) {
            *callback_calls_out = callback_state.calls;
        }
        return result;
    }

    static bool
    read_owned_relative_settings_fixture(std::vector<std::byte>* bytes)
    {
        if (bytes == nullptr) {
            return false;
        }
        const std::filesystem::path path
            = std::filesystem::path(__FILE__).parent_path()
              / "fuzz/corpus/exif_tiff_decode/canon_external_relative_settings.bin";
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) {
            return false;
        }
        const std::streampos end = input.tellg();
        if (end <= std::streampos(0)) {
            return false;
        }
        const std::streamsize byte_count = static_cast<std::streamsize>(end);
        bytes->resize(static_cast<size_t>(byte_count));
        input.seekg(0, std::ios::beg);
        input.read(reinterpret_cast<char*>(bytes->data()), byte_count);
        return input.gcount() == byte_count;
    }

    TEST(CanonTableResolution,
         RelativeCameraSettingsSkipsDirectoryForAllByteOrdersAndApis)
    {
        const CameraSettings expected = make_expected_relative_camera_settings();
        const bool endian_cases[][2] = {
            { true, true },
            { false, false },
            { true, false },
            { false, true },
        };
        for (const auto& endian_case : endian_cases) {
            const bool parent_little_endian     = endian_case[0];
            const bool maker_note_little_endian = endian_case[1];
            const std::vector<std::byte> tiff   = make_canon_tiff(
                parent_little_endian, maker_note_little_endian,
                kCameraSettingsOffset, static_cast<uint32_t>(kDirectoryBytes),
                256U, expected, 256U, expected);

            ExifDecodeOptions options;
            options.decode_makernote = true;
            MetaStore contiguous_store;
            const ExifDecodeResult contiguous_result
                = decode_exif_tiff(tiff, contiguous_store, {}, options);
            ASSERT_EQ(contiguous_result.status, ExifDecodeStatus::Ok);
            contiguous_store.finalize();
            expect_canon_camera_settings(contiguous_store, expected);

            uint32_t callback_calls = 0U;
            MetaStore callback_store;
            const ExifRandomAccessDecodeResult callback_result
                = decode_with_callback(tiff, callback_store, &callback_calls);
            ASSERT_TRUE(callback_result.complete());
            EXPECT_EQ(callback_result.decode.status, ExifDecodeStatus::Ok);
            EXPECT_GT(callback_calls, 0U);
            callback_store.finalize();
            expect_canon_camera_settings(callback_store, expected);
        }
    }

    TEST(CanonTableResolution,
         OwnedRelativeSettingsFixtureDecodesExactValuesThroughBothApis)
    {
        std::vector<std::byte> tiff;
        ASSERT_TRUE(read_owned_relative_settings_fixture(&tiff));

        ExifDecodeOptions options;
        options.decode_makernote = true;
        MetaStore contiguous_store;
        const ExifDecodeResult contiguous_result
            = decode_exif_tiff(tiff, contiguous_store, {}, options);
        EXPECT_EQ(contiguous_result.status, ExifDecodeStatus::Ok);
        contiguous_store.finalize();
        const CameraSettings expected = make_expected_relative_camera_settings();
        expect_canon_camera_settings(contiguous_store, expected);

        uint32_t callback_calls = 0U;
        MetaStore callback_store;
        const ExifRandomAccessDecodeResult callback_result
            = decode_with_callback(tiff, callback_store, &callback_calls);
        EXPECT_TRUE(callback_result.complete());
        EXPECT_EQ(callback_result.decode.status, ExifDecodeStatus::Ok);
        EXPECT_GT(callback_calls, 0U);
        callback_store.finalize();
        expect_canon_camera_settings(callback_store, expected);
    }

    TEST(CanonTableResolution,
         CallbackKeepsAbsoluteBaseWhenBothTablesFitDeclaredNote)
    {
        const CameraSettings absolute = make_sequential_camera_settings(1000U);
        const CameraSettings relative = make_sequential_camera_settings(2000U);
        constexpr bool endian_cases[][2] = {
            { true, true },
            { false, false },
            { true, false },
            { false, true },
        };
        for (const auto& endian_case : endian_cases) {
            // Both candidates fit the note. Its 12-byte value-area gap must
            // survive slicing the note away from its parent TIFF origin.
            const std::vector<std::byte> tiff
                = make_canon_tiff(endian_case[0], endian_case[1], 158U, 350U,
                                  158U, absolute, 286U, relative);
            ExifDecodeOptions options;
            options.decode_makernote = true;
            MetaStore contiguous_store;
            const ExifDecodeResult result
                = decode_exif_tiff(tiff, contiguous_store, {}, options);
            EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
            contiguous_store.finalize();
            expect_canon_camera_settings(contiguous_store, absolute);

            MetaStore callback_store;
            uint32_t calls = 0U;
            const ExifRandomAccessDecodeResult callback
                = decode_with_callback(tiff, callback_store, &calls);
            EXPECT_TRUE(callback.complete());
            EXPECT_EQ(callback.decode.status, ExifDecodeStatus::Ok);
            EXPECT_GT(calls, 0U);
            callback_store.finalize();
            expect_canon_camera_settings(callback_store, absolute);
        }
    }

    TEST(CanonTableResolution,
         AbsoluteCameraSettingsCandidateWinsWithMakerNotePayloadEvidence)
    {
        const CameraSettings absolute = make_sequential_camera_settings(1000U);
        const CameraSettings relative = make_sequential_camera_settings(2000U);
        const std::vector<std::byte> tiff = make_canon_tiff(true, true, 146U,
                                                            82U, 146U, absolute,
                                                            274U, relative);
        ExifDecodeOptions options;
        options.decode_makernote = true;
        MetaStore store;
        const ExifDecodeResult result = decode_exif_tiff(tiff, store, {},
                                                         options);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        store.finalize();
        expect_canon_camera_settings(store, absolute);

        uint32_t callback_calls = 0U;
        MetaStore callback_store;
        const ExifRandomAccessDecodeResult callback_result
            = decode_with_callback(tiff, callback_store, &callback_calls);
        EXPECT_TRUE(callback_result.complete());
        EXPECT_EQ(callback_result.decode.status, ExifDecodeStatus::Ok);
        EXPECT_GT(callback_calls, 0U);
        callback_store.finalize();
        expect_canon_camera_settings(callback_store, absolute);
    }

    TEST(CanonTableResolution,
         StructuralOverlapDisqualifiesBaseDespiteOtherSignatureEvidence)
    {
        const CameraSettings expected = make_expected_relative_camera_settings();
        const std::vector<std::byte> tiff
            = make_canon_mixed_table_and_ascii_tiff();
        ExifDecodeOptions options;
        options.decode_makernote = true;
        MetaStore store;
        const ExifDecodeResult result = decode_exif_tiff(tiff, store, {},
                                                         options);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        store.finalize();
        expect_canon_camera_settings(store, expected);

        uint32_t callback_calls = 0U;
        MetaStore callback_store;
        const ExifRandomAccessDecodeResult callback_result
            = decode_with_callback(tiff, callback_store, &callback_calls);
        EXPECT_TRUE(callback_result.complete());
        EXPECT_EQ(callback_result.decode.status, ExifDecodeStatus::Ok);
        EXPECT_GT(callback_calls, 0U);
        callback_store.finalize();
        expect_canon_camera_settings(callback_store, expected);
    }

    TEST(CanonTableResolution,
         EqualNonDirectoryCandidatesKeepDeterministicAbsoluteFallback)
    {
        const CameraSettings absolute = make_sequential_camera_settings(3000U);
        const CameraSettings relative = make_sequential_camera_settings(4000U);
        const std::vector<std::byte> tiff
            = make_canon_tiff(true, true, 146U,
                              static_cast<uint32_t>(kDirectoryBytes), 146U,
                              absolute, 274U, relative);
        ExifDecodeOptions options;
        options.decode_makernote = true;
        MetaStore store;
        const ExifDecodeResult result = decode_exif_tiff(tiff, store, {},
                                                         options);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        store.finalize();

        // Both complete tables are in bounds and outside the MakerNote directory.
        // Equal candidate scores retain the existing absolute-base tie order.
        expect_canon_camera_settings(store, absolute);

        uint32_t callback_calls = 0U;
        MetaStore callback_store;
        const ExifRandomAccessDecodeResult callback_result
            = decode_with_callback(tiff, callback_store, &callback_calls);
        EXPECT_TRUE(callback_result.complete());
        EXPECT_EQ(callback_result.decode.status, ExifDecodeStatus::Ok);
        EXPECT_GT(callback_calls, 0U);
        callback_store.finalize();
        expect_canon_camera_settings(callback_store, absolute);
    }

    TEST(CanonTableResolution,
         AbsoluteTableEndingAtMakerNoteStartDoesNotOverlapItsDirectory)
    {
        const CameraSettings absolute = make_sequential_camera_settings(5000U);
        const CameraSettings relative = make_sequential_camera_settings(6000U);
        const std::vector<std::byte> tiff
            = make_canon_tiff(true, true, 64U,
                              static_cast<uint32_t>(kDirectoryBytes), 64U,
                              absolute, 192U, relative);
        ExifDecodeOptions options;
        options.decode_makernote = true;
        MetaStore store;
        const ExifDecodeResult result = decode_exif_tiff(tiff, store, {},
                                                         options);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        store.finalize();
        expect_canon_camera_settings(store, absolute);

        uint32_t callback_calls = 0U;
        MetaStore callback_store;
        const ExifRandomAccessDecodeResult callback_result
            = decode_with_callback(tiff, callback_store, &callback_calls);
        EXPECT_TRUE(callback_result.complete());
        EXPECT_EQ(callback_result.decode.status, ExifDecodeStatus::Ok);
        EXPECT_GT(callback_calls, 0U);
        callback_store.finalize();
        expect_canon_camera_settings(callback_store, absolute);
    }

    TEST(CanonTableResolution,
         NoEligibleBaseKeepsWireEntryUnreadableWithoutDerivedTable)
    {
        const std::vector<std::byte> tiff = make_canon_no_eligible_base_tiff();
        ExifDecodeOptions options;
        options.decode_makernote = true;
        MetaStore store;
        const ExifDecodeResult result = decode_exif_tiff(tiff, store, {},
                                                         options);
        EXPECT_EQ(result.status, ExifDecodeStatus::Malformed);
        store.finalize();
        expect_no_eligible_base_fallback(store);

        uint32_t callback_calls = 0U;
        MetaStore callback_store;
        const ExifRandomAccessDecodeResult callback_result
            = decode_with_callback(tiff, callback_store, &callback_calls);
        EXPECT_TRUE(callback_result.input.ok());
        EXPECT_EQ(callback_result.decode.status, ExifDecodeStatus::Malformed);
        EXPECT_GT(callback_result.maker_note_paths_skipped, 0U);
        // Source I/O succeeded; the skipped nested MakerNote path keeps the
        // callback result incomplete after the raw entry is retained.
        EXPECT_FALSE(callback_result.complete());
        EXPECT_GT(callback_calls, 0U);
        callback_store.finalize();
        expect_no_eligible_base_fallback(callback_store);
    }

}  // namespace
}  // namespace openmeta
