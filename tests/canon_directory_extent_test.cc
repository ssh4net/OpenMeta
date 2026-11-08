// SPDX-License-Identifier: Apache-2.0

#include "../src/openmeta/exif_tiff_decode_internal.h"
#include "openmeta/exif_tiff_decode.h"

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

    constexpr uint64_t kMakerNoteOffset      = 128U;
    constexpr uint32_t kCameraSettingsOffset = 146U;
    constexpr uint32_t kCameraSettingsCount  = 32U;

    using CameraSettings = std::array<uint16_t, kCameraSettingsCount>;

    static CameraSettings make_expected_camera_settings() noexcept
    {
        return CameraSettings {
            64U, 1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U,
            11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U,
            22U, 23U, 24U, 25U, 26U, 27U, 28U, 29U, 30U, 31U,
        };
    }

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

    static std::vector<std::byte>
    make_overcounted_canon_tiff(bool parent_little_endian,
                                bool maker_note_little_endian)
    {
        std::vector<std::byte> tiff(256U, std::byte { 0 });
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
        write_u32(&tiff, 50U, 82U, parent_little_endian);
        write_u32(&tiff, 54U, static_cast<uint32_t>(kMakerNoteOffset),
                  parent_little_endian);
        write_u32(&tiff, 58U, 0U, parent_little_endian);

        // One real CameraSettings row, then its zero next-IFD pointer. The
        // declared second row starts at that pointer and overlaps the table.
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset), 2U,
                  maker_note_little_endian);
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset) + 2U, 0x0001U,
                  maker_note_little_endian);
        write_u16(&tiff, static_cast<size_t>(kMakerNoteOffset) + 4U, 3U,
                  maker_note_little_endian);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 6U,
                  kCameraSettingsCount, maker_note_little_endian);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 10U,
                  kCameraSettingsOffset, maker_note_little_endian);
        write_u32(&tiff, static_cast<size_t>(kMakerNoteOffset) + 14U, 0U,
                  maker_note_little_endian);

        const CameraSettings expected = make_expected_camera_settings();
        for (size_t i = 0U; i < expected.size(); ++i) {
            write_u16(&tiff, kCameraSettingsOffset + i * 2U, expected[i],
                      maker_note_little_endian);
        }
        return tiff;
    }

    static std::vector<std::byte>
    make_note_with_valid_second_entry(bool little_endian)
    {
        std::vector<std::byte> note(94U, std::byte { 0 });
        write_u16(&note, 0U, 2U, little_endian);
        write_u16(&note, 2U, 0x0001U, little_endian);
        write_u16(&note, 4U, 3U, little_endian);
        write_u32(&note, 6U, kCameraSettingsCount, little_endian);
        write_u32(&note, 10U, 158U, little_endian);
        write_u16(&note, 14U, 0x7777U, little_endian);
        write_u16(&note, 16U, 7U, little_endian);
        write_u32(&note, 18U, 1U, little_endian);
        write_u32(&note, 22U, 0x5aU, little_endian);
        write_u16(&note, 30U, 64U, little_endian);
        return note;
    }

    static std::vector<std::byte>
    make_note_with_duplicate_camera_settings(bool little_endian)
    {
        std::vector<std::byte> note(94U, std::byte { 0 });
        write_u16(&note, 0U, 3U, little_endian);
        constexpr size_t kEntryOffsets[] = { 2U, 14U };
        for (size_t entry_offset : kEntryOffsets) {
            write_u16(&note, entry_offset, 0x0001U, little_endian);
            write_u16(&note, entry_offset + 2U, 3U, little_endian);
            write_u32(&note, entry_offset + 4U, kCameraSettingsCount,
                      little_endian);
            write_u32(&note, entry_offset + 8U, 158U, little_endian);
        }
        write_u16(&note, 30U, 64U, little_endian);
        return note;
    }

    static std::vector<std::byte>
    make_note_with_unsupported_prior_type(bool little_endian)
    {
        std::vector<std::byte> note(94U, std::byte { 0 });
        write_u16(&note, 0U, 3U, little_endian);
        write_u16(&note, 2U, 0x0001U, little_endian);
        write_u16(&note, 4U, 3U, little_endian);
        write_u32(&note, 6U, kCameraSettingsCount, little_endian);
        write_u32(&note, 10U, 158U, little_endian);
        write_u16(&note, 14U, 0x7777U, little_endian);
        write_u16(&note, 16U, 0U, little_endian);
        write_u32(&note, 18U, 1U, little_endian);
        write_u32(&note, 22U, 0U, little_endian);
        write_u16(&note, 30U, 64U, little_endian);
        return note;
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

    static void expect_no_extent_recovery(const TiffConfig& cfg,
                                          std::span<const std::byte> maker_note,
                                          uint64_t maker_note_off,
                                          uint16_t declared_count)
    {
        uint16_t effective_count = 0xa55aU;
        uint64_t ifd_bytes       = 0x123456789abcdef0ULL;
        EXPECT_FALSE(exif_internal::canon_recover_ifd_extent(
            cfg, maker_note, maker_note_off, declared_count, &effective_count,
            &ifd_bytes));
        EXPECT_EQ(effective_count, 0xa55aU);
        EXPECT_EQ(ifd_bytes, 0x123456789abcdef0ULL);
    }

    static void expect_camera_settings(const MetaStore& store,
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

    TEST(CanonDirectoryExtent,
         RepairsSingleTrailingRowOvercountForBothApisAndByteOrders)
    {
        constexpr bool endian_cases[][2] = {
            { true, true },
            { false, false },
            { true, false },
            { false, true },
        };
        const CameraSettings expected = make_expected_camera_settings();

        for (const auto& endian_case : endian_cases) {
            const std::vector<std::byte> tiff
                = make_overcounted_canon_tiff(endian_case[0], endian_case[1]);
            ExifDecodeOptions options;
            options.decode_makernote = true;

            MetaStore contiguous_store;
            const ExifDecodeResult contiguous_result
                = decode_exif_tiff(tiff, contiguous_store, {}, options);
            EXPECT_EQ(contiguous_result.status, ExifDecodeStatus::Ok);
            contiguous_store.finalize();
            expect_camera_settings(contiguous_store, expected);

            std::vector<std::byte> backing(23U, std::byte { 0x5aU });
            backing.insert(backing.end(), tiff.begin(), tiff.end());
            CallbackState callback { backing };
            const RandomAccessSource source
                = make_callback_random_access_source(backing.size(), &callback,
                                                     read_callback);
            const RandomAccessSourceRange range
                = make_random_access_source_range(source, 23U, tiff.size());
            std::array<std::byte, 64> read_window {};
            std::array<std::byte, 4096> value_scratch {};
            ExifRandomAccessScratch scratch;
            scratch.read_window                       = read_window;
            scratch.value                             = value_scratch;
            scratch.window_options.minimum_read_bytes = read_window.size();
            std::array<ExifIfdRef, 16> ifds {};

            MetaStore callback_store;
            const ExifRandomAccessDecodeResult callback_result
                = decode_exif_tiff_random_access(range, callback_store, ifds,
                                                 scratch, options);
            EXPECT_TRUE(callback_result.complete());
            EXPECT_EQ(callback_result.decode.status, ExifDecodeStatus::Ok);
            EXPECT_GT(callback.calls, 0U);
            callback_store.finalize();
            expect_camera_settings(callback_store, expected);
        }
    }

    TEST(CanonDirectoryExtent,
         ExtentRepairRequiresExactSelfSizedSingleRowOvercount)
    {
        constexpr uint16_t kDeclaredCount    = 2U;
        constexpr uint64_t kExpectedIfdBytes = 18U;
        const bool endian_cases[][2]         = {
            { true, true },
            { false, false },
            { true, false },
            { false, true },
        };
        for (const auto& endian_case : endian_cases) {
            const bool parent_little_endian = endian_case[0];
            const bool note_little_endian   = endian_case[1];
            const std::vector<std::byte> tiff
                = make_overcounted_canon_tiff(parent_little_endian,
                                              note_little_endian);
            const std::span<const std::byte> note
                = std::span<const std::byte>(tiff).subspan(
                    static_cast<size_t>(kMakerNoteOffset), 82U);
            const TiffConfig cfg { note_little_endian, false };
            uint16_t effective_count = 0U;
            uint64_t ifd_bytes       = 0U;
            ASSERT_TRUE(exif_internal::canon_recover_ifd_extent(
                cfg, note, kMakerNoteOffset, kDeclaredCount, &effective_count,
                &ifd_bytes));
            EXPECT_EQ(effective_count, 1U);
            EXPECT_EQ(ifd_bytes, kExpectedIfdBytes);
        }

        constexpr bool kLittleEndian = true;
        const TiffConfig cfg { kLittleEndian, false };
        const std::vector<std::byte> valid_tiff
            = make_overcounted_canon_tiff(true, kLittleEndian);
        const std::span<const std::byte> valid_note
            = std::span<const std::byte>(valid_tiff)
                  .subspan(static_cast<size_t>(kMakerNoteOffset), 82U);

        std::vector<std::byte> missing_terminator  = valid_tiff;
        missing_terminator[kMakerNoteOffset + 14U] = std::byte { 1U };
        expect_no_extent_recovery(
            cfg,
            std::span<const std::byte>(missing_terminator)
                .subspan(static_cast<size_t>(kMakerNoteOffset), 82U),
            kMakerNoteOffset, kDeclaredCount);

        std::vector<std::byte> wrong_size_word = valid_tiff;
        write_u16(&wrong_size_word, kCameraSettingsOffset, 62U, kLittleEndian);
        expect_no_extent_recovery(
            cfg,
            std::span<const std::byte>(wrong_size_word)
                .subspan(static_cast<size_t>(kMakerNoteOffset), 82U),
            kMakerNoteOffset, kDeclaredCount);

        expect_no_extent_recovery(cfg, valid_note.first(29U), kMakerNoteOffset,
                                  kDeclaredCount);
        expect_no_extent_recovery(cfg, valid_note.first(81U), kMakerNoteOffset,
                                  kDeclaredCount);
        expect_no_extent_recovery(cfg, valid_note, kMakerNoteOffset, 1U);
        expect_no_extent_recovery(cfg, valid_note, UINT64_MAX - 17U,
                                  kDeclaredCount);

        std::vector<std::byte> wrong_offset = valid_tiff;
        write_u32(&wrong_offset, kMakerNoteOffset + 10U, 147U, kLittleEndian);
        expect_no_extent_recovery(
            cfg,
            std::span<const std::byte>(wrong_offset)
                .subspan(static_cast<size_t>(kMakerNoteOffset), 82U),
            kMakerNoteOffset, kDeclaredCount);

        std::vector<std::byte> too_small_count = valid_tiff;
        write_u32(&too_small_count, kMakerNoteOffset + 6U, 2U, kLittleEndian);
        expect_no_extent_recovery(
            cfg,
            std::span<const std::byte>(too_small_count)
                .subspan(static_cast<size_t>(kMakerNoteOffset), 82U),
            kMakerNoteOffset, kDeclaredCount);

        std::vector<std::byte> two_row_overcount = valid_tiff;
        write_u16(&two_row_overcount, kMakerNoteOffset, 3U, kLittleEndian);
        expect_no_extent_recovery(
            cfg,
            std::span<const std::byte>(two_row_overcount)
                .subspan(static_cast<size_t>(kMakerNoteOffset), 82U),
            kMakerNoteOffset, 3U);

        const std::vector<std::byte> valid_second_entry
            = make_note_with_valid_second_entry(kLittleEndian);
        expect_no_extent_recovery(cfg, valid_second_entry, kMakerNoteOffset,
                                  kDeclaredCount);

        const std::vector<std::byte> duplicate_camera_settings
            = make_note_with_duplicate_camera_settings(kLittleEndian);
        expect_no_extent_recovery(cfg, duplicate_camera_settings,
                                  kMakerNoteOffset, 3U);

        const std::vector<std::byte> unsupported_prior_type
            = make_note_with_unsupported_prior_type(kLittleEndian);
        expect_no_extent_recovery(cfg, unsupported_prior_type, kMakerNoteOffset,
                                  3U);
    }

}  // namespace
}  // namespace openmeta
