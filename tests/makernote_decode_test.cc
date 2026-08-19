// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_tag_names.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static std::string_view arena_string(const ByteArena& arena,
                                         const MetaValue& value) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(value.data.span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

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


    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }


    static void append_u32be(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }


    static void write_u32le_at(std::vector<std::byte>* out, size_t off,
                               uint32_t v)
    {
        ASSERT_TRUE(out);
        ASSERT_GE(out->size(), off + 4U);
        (*out)[off + 0] = std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) };
        (*out)[off + 1] = std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) };
        (*out)[off + 2] = std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) };
        (*out)[off + 3] = std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) };
    }

    static void write_u32be_at(std::vector<std::byte>* out, size_t off,
                               uint32_t v)
    {
        ASSERT_TRUE(out);
        ASSERT_GE(out->size(), off + 4U);
        (*out)[off + 0] = std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) };
        (*out)[off + 1] = std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) };
        (*out)[off + 2] = std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) };
        (*out)[off + 3] = std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) };
    }


    static void write_u16le_at(std::vector<std::byte>* out, size_t off,
                               uint16_t v)
    {
        ASSERT_TRUE(out);
        ASSERT_GE(out->size(), off + 2U);
        (*out)[off + 0] = std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) };
        (*out)[off + 1] = std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) };
    }

    static void write_u16be_at(std::vector<std::byte>* out, size_t off,
                               uint16_t v)
    {
        ASSERT_TRUE(out);
        ASSERT_GE(out->size(), off + 2U);
        (*out)[off + 0] = std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) };
        (*out)[off + 1] = std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) };
    }


    struct MakerNoteCallbackState final {
        std::span<const std::byte> bytes;
        uint32_t calls = 0U;
    };


    static RandomAccessIoResult
    maker_note_read_at(void* context, uint64_t offset,
                       std::span<std::byte> destination) noexcept
    {
        MakerNoteCallbackState* state = static_cast<MakerNoteCallbackState*>(
            context);
        state->calls += 1U;
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
    decode_makernote_callback(std::span<const std::byte> tiff, MetaStore& store,
                              const RandomAccessReadLimits& limits
                              = RandomAccessReadLimits {}) noexcept
    {
        MakerNoteCallbackState state { tiff };
        const RandomAccessSource source
            = make_callback_random_access_source(tiff.size(), &state,
                                                 maker_note_read_at);
        std::array<std::byte, 64> read_window {};
        std::array<std::byte, 4096> value_scratch {};
        ExifRandomAccessScratch scratch;
        scratch.read_window                       = read_window;
        scratch.value                             = value_scratch;
        scratch.window_options.minimum_read_bytes = read_window.size();
        ExifDecodeOptions options;
        options.decode_makernote = true;
        std::array<ExifIfdRef, 16> ifds {};
        return decode_exif_tiff_random_access(make_random_access_source_range(
                                                  source),
                                              store, ifds, scratch, options,
                                              limits);
    }

    static uint8_t sony_encipher_byte(uint8_t b) noexcept
    {
        if (b >= 249U) {
            return b;
        }
        const uint32_t m  = 249U;
        const uint32_t x  = b;
        const uint32_t x2 = (x * x) % m;
        const uint32_t x3 = (x2 * x) % m;
        return static_cast<uint8_t>(x3);
    }


    static MetaKeyView exif_key(std::string_view ifd, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }

    static bool read_u16le_at(std::span<const std::byte> bytes, size_t off,
                              uint16_t* out) noexcept
    {
        if (!out || off + 2U > bytes.size()) {
            return false;
        }
        const uint16_t b0 = static_cast<uint16_t>(bytes[off + 0]);
        const uint16_t b1 = static_cast<uint16_t>(bytes[off + 1]);
        *out              = static_cast<uint16_t>(b0 | (b1 << 8));
        return true;
    }

    static bool read_u32le_at(std::span<const std::byte> bytes, size_t off,
                              uint32_t* out) noexcept
    {
        if (!out || off + 4U > bytes.size()) {
            return false;
        }
        const uint32_t b0 = static_cast<uint32_t>(bytes[off + 0]);
        const uint32_t b1 = static_cast<uint32_t>(bytes[off + 1]);
        const uint32_t b2 = static_cast<uint32_t>(bytes[off + 2]);
        const uint32_t b3 = static_cast<uint32_t>(bytes[off + 3]);
        *out              = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        return true;
    }

    static bool patch_sony_makernote_value_offset_in_tiff(
        std::vector<std::byte>* tiff) noexcept
    {
        if (!tiff || tiff->size() < 8U) {
            return false;
        }
        const std::span<const std::byte> bytes(tiff->data(), tiff->size());
        if (bytes[0] != std::byte { 'I' } || bytes[1] != std::byte { 'I' }) {
            return false;
        }

        uint32_t ifd0_off = 0;
        if (!read_u32le_at(bytes, 4, &ifd0_off)) {
            return false;
        }
        if (ifd0_off + 2U > bytes.size()) {
            return false;
        }
        uint16_t ifd0_count = 0;
        if (!read_u16le_at(bytes, ifd0_off, &ifd0_count) || ifd0_count == 0) {
            return false;
        }

        uint32_t exif_ifd_off            = 0;
        const size_t ifd0_entries_off    = size_t(ifd0_off) + 2U;
        const size_t ifd0_entries_bytes  = size_t(ifd0_count) * 12U;
        const size_t ifd0_entries_needed = ifd0_entries_off
                                           + ifd0_entries_bytes;
        if (ifd0_entries_needed > bytes.size()) {
            return false;
        }

        for (uint16_t i = 0; i < ifd0_count; ++i) {
            const size_t eoff = ifd0_entries_off + size_t(i) * 12U;
            uint16_t tag      = 0;
            if (!read_u16le_at(bytes, eoff + 0, &tag)) {
                return false;
            }
            if (tag == 0x8769) {  // ExifIFDPointer
                if (!read_u32le_at(bytes, eoff + 8, &exif_ifd_off)) {
                    return false;
                }
                break;
            }
        }
        if (exif_ifd_off == 0 || exif_ifd_off + 2U > bytes.size()) {
            return false;
        }

        uint16_t exif_count = 0;
        if (!read_u16le_at(bytes, exif_ifd_off, &exif_count)
            || exif_count == 0) {
            return false;
        }

        uint32_t maker_note_off          = 0;
        const size_t exif_entries_off    = size_t(exif_ifd_off) + 2U;
        const size_t exif_entries_bytes  = size_t(exif_count) * 12U;
        const size_t exif_entries_needed = exif_entries_off
                                           + exif_entries_bytes;
        if (exif_entries_needed > bytes.size()) {
            return false;
        }

        for (uint16_t i = 0; i < exif_count; ++i) {
            const size_t eoff = exif_entries_off + size_t(i) * 12U;
            uint16_t tag      = 0;
            if (!read_u16le_at(bytes, eoff + 0, &tag)) {
                return false;
            }
            if (tag == 0x927C) {  // MakerNote
                if (!read_u32le_at(bytes, eoff + 8, &maker_note_off)) {
                    return false;
                }
                break;
            }
        }
        if (maker_note_off == 0 || maker_note_off + 18U > bytes.size()) {
            return false;
        }

        // Sony MakerNote: out-of-line value offsets are absolute (relative to
        // the outer TIFF stream). Our test MakerNote consists of:
        //   u16(entry_count=1) + IFD entry(12) + next_ifd(4) + payload...
        // so the payload starts at +18 bytes.
        const uint32_t value_off_abs = maker_note_off + 18U;
        const size_t patch_off       = size_t(maker_note_off) + 10U;
        if (patch_off + 4U > tiff->size()) {
            return false;
        }
        (*tiff)[patch_off + 0]
            = std::byte { static_cast<uint8_t>((value_off_abs >> 0) & 0xFF) };
        (*tiff)[patch_off + 1]
            = std::byte { static_cast<uint8_t>((value_off_abs >> 8) & 0xFF) };
        (*tiff)[patch_off + 2]
            = std::byte { static_cast<uint8_t>((value_off_abs >> 16) & 0xFF) };
        (*tiff)[patch_off + 3]
            = std::byte { static_cast<uint8_t>((value_off_abs >> 24) & 0xFF) };
        return true;
    }

    static bool patch_makernote_count_in_tiff(std::vector<std::byte>* tiff,
                                              uint32_t new_count) noexcept
    {
        if (!tiff || tiff->size() < 8U) {
            return false;
        }
        const std::span<const std::byte> bytes(tiff->data(), tiff->size());
        if (bytes[0] != std::byte { 'I' } || bytes[1] != std::byte { 'I' }) {
            return false;
        }

        uint32_t ifd0_off = 0;
        if (!read_u32le_at(bytes, 4, &ifd0_off)) {
            return false;
        }
        if (ifd0_off + 2U > bytes.size()) {
            return false;
        }
        uint16_t ifd0_count = 0;
        if (!read_u16le_at(bytes, ifd0_off, &ifd0_count) || ifd0_count == 0) {
            return false;
        }

        uint32_t exif_ifd_off            = 0;
        const size_t ifd0_entries_off    = size_t(ifd0_off) + 2U;
        const size_t ifd0_entries_bytes  = size_t(ifd0_count) * 12U;
        const size_t ifd0_entries_needed = ifd0_entries_off
                                           + ifd0_entries_bytes;
        if (ifd0_entries_needed > bytes.size()) {
            return false;
        }

        for (uint16_t i = 0; i < ifd0_count; ++i) {
            const size_t eoff = ifd0_entries_off + size_t(i) * 12U;
            uint16_t tag      = 0;
            if (!read_u16le_at(bytes, eoff + 0, &tag)) {
                return false;
            }
            if (tag == 0x8769) {  // ExifIFDPointer
                if (!read_u32le_at(bytes, eoff + 8, &exif_ifd_off)) {
                    return false;
                }
                break;
            }
        }
        if (exif_ifd_off == 0 || exif_ifd_off + 2U > bytes.size()) {
            return false;
        }

        uint16_t exif_count = 0;
        if (!read_u16le_at(bytes, exif_ifd_off, &exif_count)
            || exif_count == 0) {
            return false;
        }

        const size_t exif_entries_off    = size_t(exif_ifd_off) + 2U;
        const size_t exif_entries_bytes  = size_t(exif_count) * 12U;
        const size_t exif_entries_needed = exif_entries_off
                                           + exif_entries_bytes;
        if (exif_entries_needed > bytes.size()) {
            return false;
        }

        for (uint16_t i = 0; i < exif_count; ++i) {
            const size_t eoff = exif_entries_off + size_t(i) * 12U;
            uint16_t tag      = 0;
            if (!read_u16le_at(bytes, eoff + 0, &tag)) {
                return false;
            }
            if (tag == 0x927C) {  // MakerNote
                write_u32le_at(tiff, eoff + 4, new_count);
                return true;
            }
        }

        return false;
    }


    static std::vector<std::byte>
    make_test_tiff_with_makernote(std::string_view make,
                                  std::span<const std::byte> maker_note)
    {
        // TIFF header (8 bytes) + IFD0 (2 entries) + Make string + ExifIFD (1 entry)
        // + MakerNote bytes.
        const uint32_t ifd0_off       = 8;
        const uint32_t ifd0_entry_cnt = 2;
        const uint32_t ifd0_size      = 2U + ifd0_entry_cnt * 12U + 4U;
        const uint32_t make_off       = ifd0_off + ifd0_size;
        const uint32_t make_count     = static_cast<uint32_t>(make.size() + 1U);
        const uint32_t exif_ifd_off   = make_off + make_count;
        const uint32_t exif_entry_cnt = 1;
        const uint32_t exif_ifd_size  = 2U + exif_entry_cnt * 12U + 4U;
        const uint32_t maker_note_off = exif_ifd_off + exif_ifd_size;
        const uint32_t maker_note_count = (maker_note.size() > UINT32_MAX)
                                              ? UINT32_MAX
                                              : static_cast<uint32_t>(
                                                    maker_note.size());

        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, ifd0_off);

        // IFD0.
        append_u16le(&tiff, static_cast<uint16_t>(ifd0_entry_cnt));

        // Make (0x010F) ASCII at make_off.
        append_u16le(&tiff, 0x010F);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, make_count);
        append_u32le(&tiff, make_off);

        // ExifIFDPointer (0x8769) LONG -> exif_ifd_off.
        append_u16le(&tiff, 0x8769);
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, exif_ifd_off);

        append_u32le(&tiff, 0);  // next IFD

        EXPECT_EQ(tiff.size(), make_off);
        append_bytes(&tiff, make);
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), exif_ifd_off);

        // ExifIFD.
        append_u16le(&tiff, static_cast<uint16_t>(exif_entry_cnt));

        // MakerNote (0x927C) UNDEFINED bytes at maker_note_off.
        append_u16le(&tiff, 0x927C);
        append_u16le(&tiff, 7);
        append_u32le(&tiff, maker_note_count);
        append_u32le(&tiff, maker_note_off);

        append_u32le(&tiff, 0);  // next IFD

        EXPECT_EQ(tiff.size(), maker_note_off);
        tiff.insert(tiff.end(), maker_note.begin(), maker_note.end());

        return tiff;
    }

    static std::vector<std::byte>
    make_test_tiff_with_makernote_big_endian_parent(
        std::string_view make, std::span<const std::byte> maker_note)
    {
        const uint32_t ifd0_off       = 8;
        const uint32_t ifd0_entry_cnt = 2;
        const uint32_t ifd0_size      = 2U + ifd0_entry_cnt * 12U + 4U;
        const uint32_t make_off       = ifd0_off + ifd0_size;
        const uint32_t make_count     = static_cast<uint32_t>(make.size() + 1U);
        const uint32_t exif_ifd_off   = make_off + make_count;
        const uint32_t exif_entry_cnt = 1;
        const uint32_t exif_ifd_size  = 2U + exif_entry_cnt * 12U + 4U;
        const uint32_t maker_note_off = exif_ifd_off + exif_ifd_size;
        const uint32_t maker_note_count = (maker_note.size() > UINT32_MAX)
                                              ? UINT32_MAX
                                              : static_cast<uint32_t>(
                                                    maker_note.size());

        std::vector<std::byte> tiff;
        append_bytes(&tiff, "MM");
        append_u16be(&tiff, 42);
        append_u32be(&tiff, ifd0_off);

        append_u16be(&tiff, static_cast<uint16_t>(ifd0_entry_cnt));

        append_u16be(&tiff, 0x010F);
        append_u16be(&tiff, 2);
        append_u32be(&tiff, make_count);
        append_u32be(&tiff, make_off);

        append_u16be(&tiff, 0x8769);
        append_u16be(&tiff, 4);
        append_u32be(&tiff, 1);
        append_u32be(&tiff, exif_ifd_off);

        append_u32be(&tiff, 0);

        EXPECT_EQ(tiff.size(), make_off);
        append_bytes(&tiff, make);
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), exif_ifd_off);

        append_u16be(&tiff, static_cast<uint16_t>(exif_entry_cnt));
        append_u16be(&tiff, 0x927C);
        append_u16be(&tiff, 7);
        append_u32be(&tiff, maker_note_count);
        append_u32be(&tiff, maker_note_off);
        append_u32be(&tiff, 0);

        EXPECT_EQ(tiff.size(), maker_note_off);
        tiff.insert(tiff.end(), maker_note.begin(), maker_note.end());
        return tiff;
    }

    static std::vector<std::byte> make_test_tiff_with_makernote_and_model(
        std::string_view make, std::string_view model,
        std::span<const std::byte> maker_note)
    {
        // TIFF header (8 bytes) + IFD0 (3 entries: Make, Model, ExifIFDPointer)
        // + Make string + Model string + ExifIFD (1 entry) + MakerNote bytes.
        const uint32_t ifd0_off       = 8;
        const uint32_t ifd0_entry_cnt = 3;
        const uint32_t ifd0_size      = 2U + ifd0_entry_cnt * 12U + 4U;
        const uint32_t make_off       = ifd0_off + ifd0_size;
        const uint32_t make_count     = static_cast<uint32_t>(make.size() + 1U);
        const uint32_t model_off      = make_off + make_count;
        const uint32_t model_count  = static_cast<uint32_t>(model.size() + 1U);
        const uint32_t exif_ifd_off = model_off + model_count;
        const uint32_t exif_entry_cnt   = 1;
        const uint32_t exif_ifd_size    = 2U + exif_entry_cnt * 12U + 4U;
        const uint32_t maker_note_off   = exif_ifd_off + exif_ifd_size;
        const uint32_t maker_note_count = (maker_note.size() > UINT32_MAX)
                                              ? UINT32_MAX
                                              : static_cast<uint32_t>(
                                                    maker_note.size());

        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, ifd0_off);

        // IFD0.
        append_u16le(&tiff, static_cast<uint16_t>(ifd0_entry_cnt));

        // Make (0x010F) ASCII at make_off.
        append_u16le(&tiff, 0x010F);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, make_count);
        append_u32le(&tiff, make_off);

        // Model (0x0110) ASCII at model_off.
        append_u16le(&tiff, 0x0110);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, model_count);
        append_u32le(&tiff, model_off);

        // ExifIFDPointer (0x8769) LONG -> exif_ifd_off.
        append_u16le(&tiff, 0x8769);
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, exif_ifd_off);

        append_u32le(&tiff, 0);  // next IFD

        EXPECT_EQ(tiff.size(), make_off);
        append_bytes(&tiff, make);
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), model_off);
        append_bytes(&tiff, model);
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), exif_ifd_off);

        // ExifIFD.
        append_u16le(&tiff, static_cast<uint16_t>(exif_entry_cnt));

        // MakerNote (0x927C) UNDEFINED bytes at maker_note_off.
        append_u16le(&tiff, 0x927C);
        append_u16le(&tiff, 7);
        append_u32le(&tiff, maker_note_count);
        append_u32le(&tiff, maker_note_off);

        append_u32le(&tiff, 0);  // next IFD

        EXPECT_EQ(tiff.size(), maker_note_off);
        tiff.insert(tiff.end(), maker_note.begin(), maker_note.end());

        return tiff;
    }


    static std::vector<std::byte> make_canon_makernote()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x12345678U);
        append_u32le(&mn, 0);
        return mn;
    }

    static std::vector<std::byte> make_canon_makernote_unknown_main_tag()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x001F);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x00000001U);
        append_u32le(&mn, 0);
        return mn;
    }

    static std::vector<std::byte> make_sony_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "SONY");

        // Classic IFD starting at offset 4, with offsets relative to the outer
        // TIFF stream (patched by the test after placement).
        append_u16le(&mn, 2);       // entry count
        append_u16le(&mn, 0x0102);  // Quality
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 1);       // count
        append_u32le(&mn, 7);       // value (inline)

        append_u16le(&mn, 0xB020);  // CreativeStyle
        append_u16le(&mn, 2);       // ASCII
        append_u32le(&mn, 9);       // count ("Standard\0")
        append_u32le(&mn, 0);       // value offset placeholder (absolute)

        append_u32le(&mn, 0);  // next IFD
        EXPECT_EQ(mn.size(), 34U);

        append_bytes(&mn, "Standard");
        mn.push_back(std::byte { 0 });
        return mn;
    }

    static std::vector<std::byte> make_sony_makernote_main_compat_tags()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "SONY");
        append_u16le(&mn, 8);  // entry count

        append_u16le(&mn, 0x201B);  // FocusMode / Sony_0x201b
        append_u16le(&mn, 1);       // BYTE
        append_u32le(&mn, 1);
        append_u32le(&mn, 0);

        append_u16le(&mn, 0x201C);  // AFAreaModeSetting / Sony_0x201c
        append_u16le(&mn, 1);       // BYTE
        append_u32le(&mn, 1);
        append_u32le(&mn, 3);

        append_u16le(&mn, 0x201D);  // FlexibleSpotPosition / Sony_0x201d
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 2);
        append_u16le(&mn, 305);
        append_u16le(&mn, 170);

        append_u16le(&mn, 0x201E);  // AFPointSelected / Sony_0x201e
        append_u16le(&mn, 1);       // BYTE
        append_u32le(&mn, 1);
        append_u32le(&mn, 0);

        append_u16le(&mn, 0x2021);  // AFTracking / Sony_0x2021
        append_u16le(&mn, 1);       // BYTE
        append_u32le(&mn, 1);
        append_u32le(&mn, 0);

        append_u16le(&mn, 0x205C);  // StepCropShooting / Sony_0x205c
        append_u16le(&mn, 1);       // BYTE
        append_u32le(&mn, 1);
        append_u32le(&mn, 0);

        append_u16le(&mn, 0xB042);  // FocusMode / Sony_0xb042
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 1);
        append_u32le(&mn, 0);

        append_u16le(&mn, 0xB043);  // AFAreaMode / Sony_0xb043
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 1);
        append_u32le(&mn, 0);

        append_u32le(&mn, 0);
        return mn;
    }

    static std::vector<std::byte> make_minolta_makernote()
    {
        // Minolta/Konica-Minolta MakerNote is often a classic TIFF IFD.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0100);  // SceneMode
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, 1);       // count
        append_u32le(&mn, 13);      // value (inline)
        append_u32le(&mn, 0);       // next IFD
        return mn;
    }

    static std::vector<std::byte> make_minolta_makernote_image_size()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0103);  // MinoltaImageSize / Minolta_0x0103
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 1);       // count
        append_u16le(&mn, 3);       // value (inline)
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);  // next IFD
        return mn;
    }

    static std::vector<std::byte> make_motorola_makernote_custom_rendered()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x6420);  // CustomRendered / Motorola_0x6420
        append_u16le(&mn, 9);       // SLONG
        append_u32le(&mn, 1);       // count
        append_u32le(&mn, 0);       // value (inline)
        append_u32le(&mn, 0);       // next IFD
        return mn;
    }

    static std::vector<std::byte> make_minolta_makernote_with_binary_subdirs()
    {
        static constexpr uint16_t kEntryCount = 3;
        static constexpr uint32_t kHeaderSize = 2U + kEntryCount * 12U + 4U;
        static constexpr uint32_t kCsOff      = kHeaderSize;
        static constexpr uint32_t kCs7dOff    = kCsOff + 16U;
        static constexpr uint32_t kCs5dOff    = kCs7dOff + 8U;

        std::vector<std::byte> mn;
        append_u16le(&mn, kEntryCount);

        append_u16le(&mn, 0x0001);  // CameraSettings
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, 4U);
        append_u32le(&mn, kCsOff);

        append_u16le(&mn, 0x0004);  // CameraSettings7D
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 4U);
        append_u32le(&mn, kCs7dOff);

        append_u16le(&mn, 0x0114);  // CameraSettings5D/A100
        append_u16le(&mn, 7);       // UNDEFINED
        append_u32le(&mn, 8U);
        append_u32le(&mn, kCs5dOff);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), static_cast<size_t>(kHeaderSize));

        append_u32le(&mn, 1U);
        append_u32le(&mn, 2U);
        append_u32le(&mn, 0x12345678U);
        append_u32le(&mn, 9U);

        append_u16le(&mn, 0x1111U);
        append_u16le(&mn, 0x2222U);
        append_u16le(&mn, 0x3333U);
        append_u16le(&mn, 0x4444U);

        append_u16be(&mn, 0x0102U);
        append_u16be(&mn, 0x0304U);
        append_u16be(&mn, 0x0506U);
        append_u16be(&mn, 0x0708U);
        return mn;
    }


    static std::vector<std::byte> make_canon_camera_settings_makernote()
    {
        // Canon MakerNote with a single SHORT array tag (0x0001) stored
        // out-of-line, to exercise Canon BinaryData subdirectory decoding.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0001);  // CanonCameraSettings
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 3);       // count
        append_u32le(&mn, 18);      // value offset (MakerNote-relative)
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 18U);
        append_u16le(&mn, 0);
        append_u16le(&mn, 11);
        append_u16le(&mn, 22);
        return mn;
    }

    static std::vector<std::byte>
    make_canon_camera_settings_makernote_adjusted_base()
    {
        // Canon MakerNote where the out-of-line value offset is not relative to
        // the MakerNote start, but to an adjusted base. This occurs in some
        // real-world Canon JPEGs where ExifTool reports:
        //   "Adjusted MakerNotes base by <N>".
        //
        // The value bytes are placed immediately after the IFD table (at +18),
        // but the offset field stores a larger value (50). The decoder should
        // infer a base shift so that base+50 == maker_note_off+18.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0001);  // CanonCameraSettings
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 3);       // count
        append_u32le(&mn, 50);      // value offset (adjusted base)
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 18U);
        append_u16le(&mn, 0);
        append_u16le(&mn, 11);
        append_u16le(&mn, 22);
        return mn;
    }


    static std::vector<std::byte> make_canon_custom_functions2_makernote()
    {
        // Canon MakerNote with a minimal CustomFunctions2 blob (0x0099),
        // following the CanonCustom2 group record structure.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0099);  // CustomFunctions2
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, 8);       // count (32 bytes / 4)
        append_u32le(&mn, 18);      // value offset (MakerNote-relative)
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 18U);

        // CanonCustom2 blob (32 bytes total):
        // u16 size, u16 reserved, u32 group_count, then 1 group with 1 entry.
        append_u16le(&mn, 32);
        append_u16le(&mn, 0);
        append_u32le(&mn, 1);   // group count
        append_u32le(&mn, 1);   // recNum
        append_u32le(&mn, 20);  // recLen (excludes recCount)
        append_u32le(&mn, 1);   // recCount
        append_u32le(&mn, 0x0101);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0);

        EXPECT_EQ(mn.size(), 18U + 32U);
        return mn;
    }

    static std::vector<std::byte>
    make_canon_custom_functions2_tag0103_makernote(uint32_t wire_count)
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0099);
        append_u16le(&mn, 4);
        const uint32_t payload_size = (wire_count <= 1U) ? 32U : 40U;
        append_u32le(&mn, payload_size / 4U);
        append_u32le(&mn, 18U);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);

        append_u16le(&mn, static_cast<uint16_t>(payload_size));
        append_u16le(&mn, 0);
        append_u32le(&mn, 1);
        append_u32le(&mn, 1);
        append_u32le(&mn, 16U + wire_count * 4U);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x0103);
        append_u32le(&mn, wire_count);
        append_u32le(&mn, 0U);
        if (wire_count > 1U) {
            for (uint32_t i = 1; i < wire_count; ++i) {
                append_u32le(&mn, i);
            }
        }

        EXPECT_EQ(mn.size(), 18U + payload_size);
        return mn;
    }

    static std::vector<std::byte>
    make_canon_custom_functions2_single_entry_makernote(
        uint32_t tag, std::span<const uint32_t> values)
    {
        const uint32_t wire_count = static_cast<uint32_t>(values.size());
        EXPECT_GT(wire_count, 0U);

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0099);
        append_u16le(&mn, 4);
        const uint32_t payload_size = 28U + wire_count * 4U;
        append_u32le(&mn, payload_size / 4U);
        append_u32le(&mn, 18U);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);

        append_u16le(&mn, static_cast<uint16_t>(payload_size));
        append_u16le(&mn, 0);
        append_u32le(&mn, 1);
        append_u32le(&mn, 1);
        append_u32le(&mn, 16U + wire_count * 4U);
        append_u32le(&mn, 1);
        append_u32le(&mn, tag);
        append_u32le(&mn, wire_count);
        for (size_t i = 0; i < values.size(); ++i) {
            append_u32le(&mn, values[i]);
        }

        EXPECT_EQ(mn.size(), 18U + payload_size);
        return mn;
    }

    static std::vector<std::byte>
    make_canon_custom_functions_0x000f_makernote(uint32_t count)
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x000f);
        append_u16le(&mn, 3);
        append_u32le(&mn, count);
        append_u32le(&mn, 18U);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);

        for (uint32_t i = 0; i < count; ++i) {
            append_u16le(&mn, static_cast<uint16_t>(i << 8));
        }

        EXPECT_EQ(mn.size(), 18U + count * 2U);
        return mn;
    }

    static std::vector<std::byte>
    make_canon_custom_functions_0x000f_with_size_header_makernote(
        uint32_t entries)
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x000f);
        append_u16le(&mn, 3);
        append_u32le(&mn, entries + 1U);
        append_u32le(&mn, 18U);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);

        append_u16le(&mn, static_cast<uint16_t>((entries + 1U) * 2U));
        for (uint32_t i = 0; i < entries; ++i) {
            append_u16le(&mn, static_cast<uint16_t>(i << 8));
        }

        EXPECT_EQ(mn.size(), 18U + (entries + 1U) * 2U);
        return mn;
    }

    static std::vector<std::byte> make_canon_makernote_main_tag0038_bytes()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0038);
        append_u16le(&mn, 7);
        append_u32le(&mn, 8);
        append_u32le(&mn, 18U);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);
        append_u32le(&mn, 0x00000008U);
        append_u32le(&mn, 0x0003EAE6U);
        return mn;
    }

    static std::vector<std::byte> make_canon_makernote_main_tag0038_long_bytes()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0038);
        append_u16le(&mn, 7);
        append_u32le(&mn, 76);
        append_u32le(&mn, 18U);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);
        append_u32le(&mn, 76U);
        append_bytes(&mn, "LP-E6NH");
        while (mn.size() < 18U + 76U) {
            mn.push_back(std::byte { 0 });
        }
        return mn;
    }

    static std::vector<std::byte> make_canon_custom_functions1d_makernote()
    {
        // Canon MakerNote with:
        // - CustomFunctions1D (0x0090): u16 array where each u16 encodes
        //   (tag_id << 8) | value, preceded by a byte-size header.
        // - PersonalFunctions (0x0091): u16 values preceded by size header.
        // - PersonalFunctionValues (0x0092): u16 values preceded by size header.
        std::vector<std::byte> mn;
        append_u16le(&mn, 3);       // entry count
        append_u16le(&mn, 0x0090);  // CustomFunctions1D
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 6);       // count
        append_u32le(&mn, 42);      // value offset (MakerNote-relative)
        append_u16le(&mn, 0x0091);  // PersonalFunctions
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 4);       // count
        append_u32le(&mn, 54);      // value offset
        append_u16le(&mn, 0x0092);  // PersonalFunctionValues
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 4);       // count
        append_u32le(&mn, 62);      // value offset
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 42U);

        // CustomFunctions1D (6*2 = 12 bytes).
        append_u16le(&mn, 12);      // size in bytes
        append_u16le(&mn, 0x0001);  // tag0=1
        append_u16le(&mn, 0x0100);  // tag1=0
        append_u16le(&mn, 0x0200);  // tag2=0
        append_u16le(&mn, 0x0301);  // tag3=1
        append_u16le(&mn, 0x0400);  // tag4=0

        // PersonalFunctions (4*2 = 8 bytes).
        append_u16le(&mn, 8);
        append_u16le(&mn, 0);
        append_u16le(&mn, 0);
        append_u16le(&mn, 0);

        // PersonalFunctionValues (4*2 = 8 bytes).
        append_u16le(&mn, 8);
        append_u16le(&mn, 63);
        append_u16le(&mn, 240);
        append_u16le(&mn, 16);

        EXPECT_EQ(mn.size(), 70U);
        return mn;
    }

    static std::vector<std::byte> make_canon_truncated_count_makernote()
    {
        // Canon MakerNote with a classic IFD that stores an out-of-line ASCII
        // value immediately after the directory. Used to test cases where the
        // declared MakerNote byte count is smaller than the directory size.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0006);  // CanonImageType
        append_u16le(&mn, 2);       // ASCII
        append_u32le(&mn, 13);      // count
        append_u32le(&mn, 18);      // value offset (MakerNote-relative)
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 18U);

        append_bytes(&mn, "IMG:TEST JPEG");
        mn.push_back(std::byte { 0 });
        EXPECT_GE(mn.size(), 18U + 13U);
        return mn;
    }


    static std::vector<std::byte> make_canon_camera_info_psinfo_makernote()
    {
        // Canon MakerNote with a single CameraInfo blob tag (0x000d) that
        // contains a PictureStyleInfo table at offset 0x025b.
        const size_t cam_bytes = 0x025b + 0x00de;
        std::vector<std::byte> cam(cam_bytes, std::byte { 0 });

        // Minimal embedded classic IFD at the start of the blob:
        // tag 0x0003 SHORT value 42.
        write_u16le_at(&cam, 0, 1);       // entry count
        write_u16le_at(&cam, 2, 0x0003);  // tag
        write_u16le_at(&cam, 4, 3);       // type SHORT
        write_u32le_at(&cam, 6, 1);       // count
        write_u16le_at(&cam, 10, 42);     // value (inline)
        write_u16le_at(&cam, 12, 0);      // padding
        write_u32le_at(&cam, 14, 0);      // next IFD

        write_u32le_at(&cam, 0x025b + 0x0000, 0);
        write_u32le_at(&cam, 0x025b + 0x0004, 3);
        write_u16le_at(&cam, 0x025b + 0x00d8, 129);

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x000d);  // CanonCameraInfo* blob
        append_u16le(&mn, 7);       // UNDEFINED bytes
        append_u32le(&mn, static_cast<uint32_t>(cam_bytes));
        append_u32le(&mn, 18);  // value offset (MakerNote-relative)
        append_u32le(&mn, 0);   // next IFD
        EXPECT_EQ(mn.size(), 18U);
        mn.insert(mn.end(), cam.begin(), cam.end());
        return mn;
    }


    static std::vector<std::byte> make_canon_camera_info_psinfo2_makernote()
    {
        const size_t cam_bytes = 0x025b + 0x00f6;
        std::vector<std::byte> cam(cam_bytes, std::byte { 0 });

        write_u16le_at(&cam, 0, 1);
        write_u16le_at(&cam, 2, 0x0003);
        write_u16le_at(&cam, 4, 3);
        write_u32le_at(&cam, 6, 1);
        write_u16le_at(&cam, 10, 42);
        write_u16le_at(&cam, 12, 0);
        write_u32le_at(&cam, 14, 0);

        write_u32le_at(&cam, 0x025b + 0x0004, 3);
        write_u32le_at(&cam, 0x025b + 0x0090, 7);
        write_u32le_at(&cam, 0x025b + 0x00e4, 2);
        write_u16le_at(&cam, 0x025b + 0x00f0, 129);

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x000d);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(cam_bytes));
        append_u32le(&mn, 18);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);
        mn.insert(mn.end(), cam.begin(), cam.end());
        return mn;
    }

    static std::vector<std::byte>
    make_canon_camera_info_blob_makernote(std::span<const std::byte> cam)
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x000d);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(cam.size()));
        append_u32le(&mn, 18);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);
        mn.insert(mn.end(), cam.begin(), cam.end());
        return mn;
    }

    static std::vector<std::byte>
    make_canon_camera_info_u32_table_makernote(uint32_t count, uint16_t tag,
                                               uint32_t value)
    {
        EXPECT_GT(count, static_cast<uint32_t>(tag));
        std::vector<std::byte> words(static_cast<size_t>(count) * 4U,
                                     std::byte { 0 });
        write_u32le_at(&words, static_cast<size_t>(tag) * 4U, value);

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x000d);
        append_u16le(&mn, 4);
        append_u32le(&mn, count);
        append_u32le(&mn, 18);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);
        mn.insert(mn.end(), words.begin(), words.end());
        return mn;
    }

    static std::vector<std::byte>
    make_canon_camera_info_blob_with_u16(uint16_t tag, uint16_t value)
    {
        std::vector<std::byte> cam(static_cast<size_t>(tag) + 2U,
                                   std::byte { 0 });
        write_u16le_at(&cam, tag, value);
        return cam;
    }

    static std::vector<std::byte>
    make_canon_camera_info_blob_with_u32(uint16_t tag, uint32_t value)
    {
        std::vector<std::byte> cam(static_cast<size_t>(tag) + 4U,
                                   std::byte { 0 });
        write_u32le_at(&cam, tag, value);
        return cam;
    }

    static std::vector<std::byte>
    make_canon_camera_info_blob_with_ascii(uint16_t tag, std::string_view text,
                                           size_t width)
    {
        std::vector<std::byte> cam(static_cast<size_t>(tag) + width,
                                   std::byte { 0 });
        if (text.size() > width) {
            return {};
        }
        std::memcpy(cam.data() + tag, text.data(), text.size());
        return cam;
    }

    static std::vector<std::byte>
    make_canon_camera_info_blob_with_ifd_short(uint16_t tag, uint16_t value)
    {
        std::vector<std::byte> cam(18U, std::byte { 0 });
        write_u16le_at(&cam, 0, 1);
        write_u16le_at(&cam, 2, tag);
        write_u16le_at(&cam, 4, 3);
        write_u32le_at(&cam, 6, 1);
        write_u16le_at(&cam, 10, value);
        write_u16le_at(&cam, 12, 0);
        write_u32le_at(&cam, 14, 0);
        return cam;
    }

    static std::vector<std::byte> make_canon_camera_info_blob_with_ifd_ascii(
        uint16_t tag, std::string_view text, size_t width)
    {
        if (width <= 4U || text.size() > width) {
            return {};
        }

        std::vector<std::byte> cam(18U + width, std::byte { 0 });
        write_u16le_at(&cam, 0, 1);
        write_u16le_at(&cam, 2, tag);
        write_u16le_at(&cam, 4, 2);
        write_u32le_at(&cam, 6, static_cast<uint32_t>(width));
        write_u32le_at(&cam, 10, 18U);
        write_u32le_at(&cam, 14, 0);
        std::memcpy(cam.data() + 18U, text.data(), text.size());
        return cam;
    }


    static std::vector<std::byte>
    make_canon_camera_info_psinfo_with_invalid_psinfo2_tail_makernote()
    {
        std::vector<std::byte> mn = make_canon_camera_info_psinfo_makernote();
        const size_t kTargetBytes = 18U + 0x025bU + 0x00f6U;
        mn.resize(kTargetBytes, std::byte { 0 });
        write_u32le_at(&mn, 6, static_cast<uint32_t>(kTargetBytes - 18U));
        write_u16le_at(&mn, 18U + 0x025bU + 0x00f4U, 0x0400U);
        return mn;
    }


    static std::vector<std::byte> make_canon_afinfo2_makernote()
    {
        // Canon MakerNote with CanonAFInfo2 (0x0026), stored out-of-line.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0026);  // CanonAFInfo2
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 48);      // count (96 bytes)
        append_u32le(&mn, 18);      // value offset (MakerNote-relative)
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 18U);

        // Word layout:
        // [0]=size(bytes), [1]=AFAreaMode, [2]=NumAFPoints, [3]=ValidAFPoints,
        // [4..7]=image dimensions, then 4 arrays of length NumAFPoints,
        // then two scalar fields.
        append_u16le(&mn, 96);    // size
        append_u16le(&mn, 2);     // AFAreaMode
        append_u16le(&mn, 9);     // NumAFPoints
        append_u16le(&mn, 9);     // ValidAFPoints
        append_u16le(&mn, 3888);  // CanonImageWidth
        append_u16le(&mn, 2592);  // CanonImageHeight
        append_u16le(&mn, 3888);  // AFImageWidth
        append_u16le(&mn, 2592);  // AFImageHeight

        for (int i = 0; i < 9; ++i)
            append_u16le(&mn, 97);  // widths
        for (int i = 0; i < 9; ++i)
            append_u16le(&mn, 98);  // heights

        const int16_t x_pos[9] = { 0, -649, 649, -1034, 0, 1034, -649, 649, 0 };
        for (int i = 0; i < 9; ++i) {
            append_u16le(&mn, static_cast<uint16_t>(x_pos[i]));
        }
        const int16_t y_pos[9] = { 562, 298, 298, 0, 0, 0, -298, -298, -562 };
        for (int i = 0; i < 9; ++i) {
            append_u16le(&mn, static_cast<uint16_t>(y_pos[i]));
        }

        append_u16le(&mn, 4);  // AFPointsInFocus
        append_u16le(&mn, 4);  // AFPointsSelected
        append_u16le(&mn, 0);  // padding
        append_u16le(&mn, 0);  // padding

        EXPECT_EQ(mn.size(), 18U + 96U);
        return mn;
    }

    static std::vector<std::byte>
    make_canon_afinfo2_makernote_with_bad_primary_offset()
    {
        std::vector<std::byte> mn = make_canon_afinfo2_makernote();
        write_u32le_at(&mn, 10U, 128U);
        mn.resize(128U + 96U, std::byte { 0 });
        return mn;
    }

    static std::vector<std::byte> make_canon_main_tag0000_makernote()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0000);  // Canon main 0x0000
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 6);       // count
        append_u32le(&mn, 18);      // value offset (MakerNote-relative)
        append_u32le(&mn, 0);       // next IFD

        append_u16le(&mn, 12);
        append_u16le(&mn, 1);
        append_u16le(&mn, 3);
        append_u16le(&mn, 46);
        append_u16le(&mn, 0);
        append_u16le(&mn, 1070);
        return mn;
    }

    static std::vector<std::byte> make_canon_colordata8_makernote()
    {
        // Canon MakerNote with a ColorData blob that matches the ColorData8
        // heuristic and exposes the derived ColorCalib table.
        static constexpr uint32_t kColorCount = 1353U;
        std::vector<std::byte> color(size_t(kColorCount) * 2U, std::byte { 0 });

        write_u16le_at(&color, 0x0000U * 2U, 14);
        write_u16le_at(&color, 0x003FU * 2U, 777);
        write_u16le_at(&color, 0x0043U * 2U, 6100);

        write_u16le_at(&color, 0x0107U * 2U, 100);
        write_u16le_at(&color, 0x0108U * 2U,
                       static_cast<uint16_t>(0xFFFFU - 24U));
        write_u16le_at(&color, 0x0109U * 2U, 300);
        write_u16le_at(&color, 0x010AU * 2U, 5200);

        write_u16le_at(&color, 0x010BU * 2U,
                       static_cast<uint16_t>(0xFFFFU - 9U));
        write_u16le_at(&color, 0x010CU * 2U, 20);
        write_u16le_at(&color, 0x010DU * 2U,
                       static_cast<uint16_t>(0xFFFFU - 29U));
        write_u16le_at(&color, 0x010EU * 2U, 40);

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x4001);  // CanonColorData
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, kColorCount);
        append_u32le(&mn, 18);  // value offset (MakerNote-relative)
        append_u32le(&mn, 0);   // next IFD
        EXPECT_EQ(mn.size(), 18U);
        mn.insert(mn.end(), color.begin(), color.end());
        return mn;
    }

    static std::vector<std::byte> make_canon_colordata_words(uint32_t count,
                                                             uint16_t version)
    {
        std::vector<std::byte> color(size_t(count) * 2U, std::byte { 0 });

        write_u16le_at(&color, 0x0000U * 2U, version);

        if (count > 0x0061U) {
            write_u16le_at(&color, 0x0061U * 2U, 6200U + version);
        }
        if (count > 0x00e0U) {
            write_u16le_at(&color, 0x00e0U * 2U, 150U);
            write_u16le_at(&color, 0x00e1U * 2U, 250U);
            write_u16le_at(&color, 0x00e2U * 2U, 350U);
            write_u16le_at(&color, 0x00e3U * 2U, 4500U);
        }
        if (count > 0x0042U) {
            write_u16le_at(&color, 0x003fU * 2U, 1700U + version);
            write_u16le_at(&color, 0x0040U * 2U, 1800U + version);
            write_u16le_at(&color, 0x0041U * 2U, 1900U + version);
            write_u16le_at(&color, 0x0042U * 2U, 2000U + version);
        }
        if (count > 0x010dU) {
            write_u16le_at(&color, 0x010dU * 2U, 410U);
            write_u16le_at(&color, 0x010eU * 2U, 510U);
            write_u16le_at(&color, 0x010fU * 2U, 610U);
            write_u16le_at(&color, 0x0110U * 2U, 5200U);
        }
        if (count > 0x0076U) {
            write_u16le_at(&color, 0x0073U * 2U, 2700U + version);
            write_u16le_at(&color, 0x0074U * 2U, 2800U + version);
            write_u16le_at(&color, 0x0075U * 2U, 2900U + version);
            write_u16le_at(&color, 0x0076U * 2U, 3000U + version);
        }
        if (count > 0x017bU) {
            write_u16le_at(&color, 0x0178U * 2U, 710U);
            write_u16le_at(&color, 0x0179U * 2U, 810U);
            write_u16le_at(&color, 0x017aU * 2U, 910U);
            write_u16le_at(&color, 0x017bU * 2U, 6100U);
        }

        return color;
    }

    static std::vector<std::byte>
    make_canon_colordata_counted_makernote(uint32_t count, uint16_t version)
    {
        std::vector<std::byte> color = make_canon_colordata_words(count,
                                                                  version);

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x4001);
        append_u16le(&mn, 3);
        append_u32le(&mn, count);
        append_u32le(&mn, 18U);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);
        mn.insert(mn.end(), color.begin(), color.end());
        return mn;
    }

    static std::vector<std::byte>
    make_canon_colordata_undefined_makernote(uint32_t byte_count,
                                             uint16_t version)
    {
        EXPECT_EQ(byte_count % 2U, 0U);
        const uint32_t word_count    = byte_count / 2U;
        std::vector<std::byte> color = make_canon_colordata_words(word_count,
                                                                  version);
        if (word_count > 0x0569U) {
            write_u16le_at(&color, 0x0569U * 2U,
                           static_cast<uint16_t>(16000U + word_count / 256U));
        }
        if (version == static_cast<uint16_t>(0xfffd)) {
            if (word_count > (0x0047U + 0x0037U)) {
                write_u16le_at(&color, (0x0047U + 0x0037U) * 2U, 6303U);
            }
            if (word_count > (0x00baU + 0x0037U)) {
                write_u16le_at(&color, (0x00baU + 0x0037U) * 2U, 703U);
            }
        } else if (version == static_cast<uint16_t>(0xfffc)) {
            if (word_count > (0x0047U + 0x0057U)) {
                write_u16le_at(&color, (0x0047U + 0x0057U) * 2U, 6404U);
            }
            if (word_count > (0x00ffU + 0x0037U)) {
                write_u16le_at(&color, (0x00ffU + 0x0037U) * 2U, 704U);
            }
        }

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x4001);
        append_u16le(&mn, 7);
        append_u32le(&mn, byte_count);
        append_u32le(&mn, 18U);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), 18U);
        mn.insert(mn.end(), color.begin(), color.end());
        return mn;
    }

    static std::vector<std::byte> make_canon_filterinfo_makernote()
    {
        // Canon MakerNote with a FilterInfo BinaryData directory:
        // - MiniatureFilterOrientation (0x0402) -> scalar 2
        // - MiniatureFilterPosition (0x0403) -> u32[2] {300, 700}
        std::vector<std::byte> filter;
        append_u32le(&filter, 0);   // length patched below
        append_u32le(&filter, 0);   // reserved
        append_u32le(&filter, 4);   // recNum
        append_u32le(&filter, 36);  // recLen
        append_u32le(&filter, 2);   // recCount

        append_u32le(&filter, 0x0402);
        append_u32le(&filter, 1);
        append_u32le(&filter, 2);

        append_u32le(&filter, 0x0403);
        append_u32le(&filter, 2);
        append_u32le(&filter, 300);
        append_u32le(&filter, 700);

        write_u32le_at(&filter, 0, static_cast<uint32_t>(filter.size()));
        EXPECT_EQ(filter.size(), 48U);

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x4024);  // CanonFilterInfo
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, static_cast<uint32_t>(filter.size() / 4U));
        append_u32le(&mn, 18);  // value offset (MakerNote-relative)
        append_u32le(&mn, 0);   // next IFD
        EXPECT_EQ(mn.size(), 18U);
        mn.insert(mn.end(), filter.begin(), filter.end());
        return mn;
    }

    static std::vector<std::byte> make_canon_timeinfo_makernote()
    {
        // Canon MakerNote with a minimal TimeInfo LONG table.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0035);  // CanonTimeInfo
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, 4);
        append_u32le(&mn, 18);  // value offset (MakerNote-relative)
        append_u32le(&mn, 0);   // next IFD
        EXPECT_EQ(mn.size(), 18U);

        append_u32le(&mn, 0);
        append_u32le(&mn, 540);
        append_u32le(&mn, 1234);
        append_u32le(&mn, 1);
        return mn;
    }

    static std::vector<std::byte> make_canon_extended_tables_makernote()
    {
        static constexpr uint32_t kEntryCount    = 4U;
        static constexpr uint32_t kHeaderSize    = 2U + kEntryCount * 12U + 4U;
        static constexpr uint32_t kAfInfoCount   = 3U;
        static constexpr uint32_t kAspectCount   = 5U;
        static constexpr uint32_t kLightingCount = 4U;
        static constexpr uint32_t kAfConfigCount = 4U;
        static constexpr uint32_t kAfInfoOff     = kHeaderSize;
        static constexpr uint32_t kAspectOff   = kAfInfoOff + kAfInfoCount * 2U;
        static constexpr uint32_t kLightingOff = kAspectOff + kAspectCount * 4U;
        static constexpr uint32_t kAfConfigOff = kLightingOff
                                                 + kLightingCount * 4U;

        std::vector<std::byte> mn;
        append_u16le(&mn, static_cast<uint16_t>(kEntryCount));

        append_u16le(&mn, 0x0012);  // CanonAFInfo (older models)
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, kAfInfoCount);
        append_u32le(&mn, kAfInfoOff);

        append_u16le(&mn, 0x009A);  // AspectInfo
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, kAspectCount);
        append_u32le(&mn, kAspectOff);

        append_u16le(&mn, 0x4018);  // LightingOpt
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, kLightingCount);
        append_u32le(&mn, kLightingOff);

        append_u16le(&mn, 0x4028);  // AFConfig
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, kAfConfigCount);
        append_u32le(&mn, kAfConfigOff);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kHeaderSize);

        append_u16le(&mn, 9);
        append_u16le(&mn, 8);
        append_u16le(&mn, 6000);

        append_u32le(&mn, 3);
        append_u32le(&mn, 4000);
        append_u32le(&mn, 3000);
        append_u32le(&mn, 100);
        append_u32le(&mn, 50);

        append_u32le(&mn, 0);
        append_u32le(&mn, 11);
        append_u32le(&mn, 22);
        append_u32le(&mn, 33);

        append_u32le(&mn, 0);
        append_u32le(&mn, 1);
        append_u32le(&mn, static_cast<uint32_t>(int32_t(-2)));
        append_u32le(&mn, 7);

        return mn;
    }


    static std::vector<std::byte> make_casio_type2_makernote()
    {
        // Casio MakerNote type2: "QVC\0" header + big-endian entry table.
        std::vector<std::byte> mn;
        append_bytes(&mn, "QVC");
        mn.push_back(std::byte { 0 });
        append_u32be(&mn, 1);  // entry count

        // Tag 0x0002 (PreviewImageSize), SHORT[2] stored inline.
        append_u16be(&mn, 0x0002);
        append_u16be(&mn, 3);
        append_u32be(&mn, 2);
        append_u16be(&mn, 320);
        append_u16be(&mn, 240);

        return mn;
    }

    static std::vector<std::byte> make_casio_type2_legacy_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "QVC");
        mn.push_back(std::byte { 0 });
        append_u32be(&mn, 5);  // entry count

        append_u16be(&mn, 0x0002);
        append_u16be(&mn, 3);
        append_u32be(&mn, 1);
        append_u16be(&mn, 3);
        append_u16be(&mn, 0);

        append_u16be(&mn, 0x0008);
        append_u16be(&mn, 3);
        append_u32be(&mn, 1);
        append_u16be(&mn, 1);
        append_u16be(&mn, 0);

        append_u16be(&mn, 0x001C);
        append_u16be(&mn, 3);
        append_u32be(&mn, 1);
        append_u16be(&mn, 1);
        append_u16be(&mn, 0);

        append_u16be(&mn, 0x001D);
        append_u16be(&mn, 3);
        append_u32be(&mn, 1);
        append_u16be(&mn, 1);
        append_u16be(&mn, 0);

        append_u16be(&mn, 0x0E00);
        append_u16be(&mn, 7);
        append_u32be(&mn, 4);
        append_u32be(&mn, 0x01020304U);

        return mn;
    }

    static std::vector<std::byte> make_casio_type2_legacy_ifd_makernote()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 8);

        append_u16le(&mn, 0x0002);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 3);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x0008);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 1);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x001C);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 1);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x001D);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 1);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x001A);
        append_u16le(&mn, 7);
        append_u32le(&mn, 20);
        append_u32le(&mn, 0x0000006A);

        append_u16le(&mn, 0x001E);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 1);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x2023);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 1);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x0E00);
        append_u16le(&mn, 7);
        append_u32le(&mn, 4);
        append_u32le(&mn, 0x04030201U);

        append_u32le(&mn, 0);

        while (mn.size() < 0x6AU) {
            mn.push_back(std::byte { 0 });
        }
        for (uint32_t i = 0; i < 20U; ++i) {
            mn.push_back(std::byte { static_cast<uint8_t>(i + 1U) });
        }
        return mn;
    }

    static std::vector<std::byte> make_casio_type2_makernote_fr10_variant()
    {
        // Casio MakerNote "QVC" directory variant observed in some models:
        // "QVC\0" + u16le version + u16le entry_count, then little-endian
        // IFD-style entries (12 bytes each).
        std::vector<std::byte> mn;
        append_bytes(&mn, "QVC");
        mn.push_back(std::byte { 0 });
        append_u16le(&mn, 0);  // version
        append_u16le(&mn, 1);  // entry count

        // Tag 0x0002 (PreviewImageSize), SHORT[2] stored inline (LE).
        append_u16le(&mn, 0x0002);
        append_u16le(&mn, 3);
        append_u32le(&mn, 2);
        append_u16le(&mn, 320);
        append_u16le(&mn, 240);

        return mn;
    }

    static std::vector<std::byte>
    make_casio_type2_makernote_out_of_line(uint32_t payload_off_tiff_relative)
    {
        // One out-of-line UNDEFINED value to validate TIFF-relative offsets.
        std::vector<std::byte> mn;
        append_bytes(&mn, "QVC");
        mn.push_back(std::byte { 0 });
        append_u32be(&mn, 1);  // entry count

        // Tag 0x2000 (PreviewImage), UNDEFINED[6] stored out-of-line.
        append_u16be(&mn, 0x2000);
        append_u16be(&mn, 7);
        append_u32be(&mn, 6);
        append_u32be(&mn, payload_off_tiff_relative);
        return mn;
    }

    static std::vector<std::byte>
    make_casio_type2_makernote_faceinfo1(uint32_t payload_off_tiff_relative,
                                         uint32_t payload_bytes)
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "QVC");
        mn.push_back(std::byte { 0 });
        append_u32be(&mn, 1);  // entry count

        // Tag 0x2089 (FaceInfo1), UNDEFINED stored out-of-line.
        append_u16be(&mn, 0x2089);
        append_u16be(&mn, 7);
        append_u32be(&mn, payload_bytes);
        append_u32be(&mn, payload_off_tiff_relative);
        return mn;
    }

    static std::vector<std::byte>
    make_casio_type2_makernote_faceinfo2(uint32_t payload_off_tiff_relative,
                                         uint32_t payload_bytes)
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "DCI");
        mn.push_back(std::byte { 0 });
        append_u32be(&mn, 1);  // entry count

        // Tag 0x2089 (FaceInfo2), UNDEFINED stored out-of-line.
        append_u16be(&mn, 0x2089);
        append_u16be(&mn, 7);
        append_u32be(&mn, payload_bytes);
        append_u32be(&mn, payload_off_tiff_relative);
        return mn;
    }


    static std::vector<std::byte> make_fuji_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "FUJIFILM");
        append_u32le(&mn, 12);
        EXPECT_EQ(mn.size(), 12U);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x00000042U);
        append_u32le(&mn, 0);
        return mn;
    }

    static std::vector<std::byte> make_fuji_makernote_tag_1200()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "FUJIFILM");
        append_u32le(&mn, 12);
        EXPECT_EQ(mn.size(), 12U);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x1200);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 0U);
        append_u16le(&mn, 0U);
        append_u32le(&mn, 0U);
        return mn;
    }

    static std::vector<std::byte> make_fuji_makernote_extended()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "FUJIFILM");
        append_u32le(&mn, 12);
        EXPECT_EQ(mn.size(), 12U);

        static constexpr uint16_t kEntryCount = 4;
        append_u16le(&mn, kEntryCount);

        const uint32_t version_off
            = 12U + 2U + static_cast<uint32_t>(kEntryCount) * 12U + 4U;
        const uint32_t focus_off = version_off + 5U;

        append_u16le(&mn, 0x0000);
        append_u16le(&mn, 2);
        append_u32le(&mn, 5U);
        append_u32le(&mn, version_off);

        append_u16le(&mn, 0x1000);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 2U);
        append_u16le(&mn, 0U);

        append_u16le(&mn, 0x1023);
        append_u16le(&mn, 3);
        append_u32le(&mn, 3U);
        append_u32le(&mn, focus_off);

        append_u16le(&mn, 0x1438);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1U);
        append_u32le(&mn, 321U);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), static_cast<size_t>(version_off));

        append_bytes(&mn, "0130");
        mn.push_back(std::byte { 0 });

        EXPECT_EQ(mn.size(), static_cast<size_t>(focus_off));
        append_u16le(&mn, 100U);
        append_u16le(&mn, 200U);
        append_u16le(&mn, 300U);
        return mn;
    }

    static std::vector<std::byte> make_fuji_makernote_placeholder_fields()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "FUJIFILM");
        append_u32le(&mn, 12);
        EXPECT_EQ(mn.size(), 12U);

        static constexpr uint16_t kEntryCount = 5;
        append_u16le(&mn, kEntryCount);

        append_u16le(&mn, 0x1051);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 0U);
        append_u16le(&mn, 0U);

        append_u16le(&mn, 0x1150);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1U);
        append_u32le(&mn, 32U);

        append_u16le(&mn, 0x1151);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 155U);
        append_u16le(&mn, 0U);

        append_u16le(&mn, 0x1152);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 155U);
        append_u16le(&mn, 0U);

        append_u16le(&mn, 0x144A);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 419U);
        append_u16le(&mn, 0U);

        append_u32le(&mn, 0U);
        return mn;
    }

    static std::vector<std::byte> make_fuji_ge2_makernote()
    {
        std::vector<std::byte> mn(318, std::byte { 0 });

        static constexpr char kGe2Magic[] = "GE\x0C\0\0\0\x16\0\0\0";
        std::memcpy(mn.data(), kGe2Magic, 10);

        const size_t entry0 = 14;
        mn[entry0 + 0]      = std::byte { 0x01 };
        mn[entry0 + 1]      = std::byte { 0x00 };
        mn[entry0 + 2]      = std::byte { 0x03 };
        mn[entry0 + 3]      = std::byte { 0x00 };
        mn[entry0 + 4]      = std::byte { 0x01 };
        mn[entry0 + 5]      = std::byte { 0x00 };
        mn[entry0 + 6]      = std::byte { 0x00 };
        mn[entry0 + 7]      = std::byte { 0x00 };
        mn[entry0 + 8]      = std::byte { 0x42 };
        mn[entry0 + 9]      = std::byte { 0x00 };
        mn[entry0 + 10]     = std::byte { 0x00 };
        mn[entry0 + 11]     = std::byte { 0x00 };

        return mn;
    }

    static std::vector<std::byte> make_fuji_ge2_makernote_extended()
    {
        std::vector<std::byte> mn = make_fuji_ge2_makernote();

        const size_t entry1 = 26U;
        mn[entry1 + 0]      = std::byte { 0x04 };
        mn[entry1 + 1]      = std::byte { 0x13 };
        mn[entry1 + 2]      = std::byte { 0x04 };
        mn[entry1 + 3]      = std::byte { 0x00 };
        mn[entry1 + 4]      = std::byte { 0x01 };
        mn[entry1 + 5]      = std::byte { 0x00 };
        mn[entry1 + 6]      = std::byte { 0x00 };
        mn[entry1 + 7]      = std::byte { 0x00 };
        mn[entry1 + 8]      = std::byte { 0x44 };
        mn[entry1 + 9]      = std::byte { 0x33 };
        mn[entry1 + 10]     = std::byte { 0x22 };
        mn[entry1 + 11]     = std::byte { 0x11 };

        return mn;
    }

    static std::vector<std::byte> make_kodak_kdk_makernote()
    {
        std::vector<std::byte> mn(0x70U, std::byte { 0 });
        append_bytes(&mn, "");
        mn[0] = std::byte { 'K' };
        mn[1] = std::byte { 'D' };
        mn[2] = std::byte { 'K' };

        const char model[] = "CX6330";
        std::memcpy(mn.data() + 0x08, model, sizeof(model) - 1U);
        mn[0x11] = std::byte { 3 };
        mn[0x12] = std::byte { 4 };

        write_u16le_at(&mn, 0x14, 800U);
        write_u16le_at(&mn, 0x16, 600U);
        write_u16le_at(&mn, 0x18, 2025U);
        mn[0x1A] = std::byte { 1 };
        mn[0x1B] = std::byte { 2 };
        mn[0x1C] = std::byte { 3 };
        mn[0x1D] = std::byte { 4 };
        mn[0x1E] = std::byte { 5 };
        mn[0x1F] = std::byte { 6 };
        return mn;
    }

    static std::vector<std::byte> make_kodak_type2_makernote()
    {
        std::vector<std::byte> mn(0x74U, std::byte { 0 });

        const char maker[] = "Hewlett-Packard";
        const char model[] = "HP PhotoSmart 618";
        std::memcpy(mn.data() + 0x08, maker, sizeof(maker) - 1U);
        std::memcpy(mn.data() + 0x28, model, sizeof(model) - 1U);

        write_u32be_at(&mn, 0x6c, 800U);
        write_u32be_at(&mn, 0x70, 600U);
        return mn;
    }

    static std::vector<std::byte> make_kodak_type5_makernote()
    {
        std::vector<std::byte> mn(0x2cU, std::byte { 0 });
        write_u32be_at(&mn, 0x14, 33333U);
        mn[0x1a] = std::byte { 4 };
        write_u16be_at(&mn, 0x1c, 45U);
        write_u16be_at(&mn, 0x1e, 140U);
        write_u16be_at(&mn, 0x20, 1U);
        write_u16be_at(&mn, 0x22, 2U);
        mn[0x27] = std::byte { 1 };
        mn[0x2a] = std::byte { 0 };
        mn[0x2b] = std::byte { 1 };
        return mn;
    }

    static std::vector<std::byte> make_kodak_absolute_ifd_makernote()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 4);

        append_u16le(&mn, 0x0104);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 9);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x0200);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 1);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x0203);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 2);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x0300);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 3);
        append_u16le(&mn, 0);

        append_u32le(&mn, 0);
        return mn;
    }

    static std::vector<std::byte> make_reconyx_h2_makernote()
    {
        std::vector<std::byte> mn(0x009CU, std::byte { 0 });
        append_bytes(&mn, "");
        const char hdr[] = "RECONYXH2";
        std::memcpy(mn.data(), hdr, sizeof(hdr) - 1U);
        mn[9] = std::byte { 0 };

        write_u16le_at(&mn, 0x0010, 7U);
        write_u16le_at(&mn, 0x0012, 9U);
        write_u16le_at(&mn, 0x0014, 11U);
        write_u16le_at(&mn, 0x0016, 22U);
        write_u16le_at(&mn, 0x003E, 2025U);
        write_u16le_at(&mn, 0x0040, 3U);
        write_u16le_at(&mn, 0x0042, 16U);
        write_u16le_at(&mn, 0x0044, 12U);
        write_u16le_at(&mn, 0x0046, 34U);
        write_u16le_at(&mn, 0x0048, 56U);
        append_bytes(&mn, "");
        mn[0x0034]         = std::byte { 'M' };
        mn[0x0035]         = std::byte { 'D' };
        const char label[] = "NIGHT_RUN";
        std::memcpy(mn.data() + 0x0068, label, sizeof(label) - 1U);
        const char serial_utf16le[] = {
            'R', 0, 'X', 0, '0', 0, '1', 0, 0, 0,
        };
        std::memcpy(mn.data() + 0x007E, serial_utf16le, sizeof(serial_utf16le));
        return mn;
    }

    static std::vector<std::byte> make_flir_makernote()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0001);  // scalar tag
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, 1U);
        append_u32le(&mn, 99U);
        append_u32le(&mn, 0U);
        return mn;
    }

    static std::vector<std::byte> make_nintendo_makernote()
    {
        static constexpr uint32_t kPayloadOff  = 18U;
        static constexpr uint32_t kPayloadSize = 0x34U;

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x1101);  // CameraInfo
        append_u16le(&mn, 7);       // UNDEFINED
        append_u32le(&mn, kPayloadSize);
        append_u32le(&mn, kPayloadOff);
        append_u32le(&mn, 0U);
        EXPECT_EQ(mn.size(), static_cast<size_t>(kPayloadOff));

        mn.resize(kPayloadOff + kPayloadSize, std::byte { 0 });
        mn[0x12] = std::byte { '3' };
        mn[0x13] = std::byte { 'D' };
        mn[0x14] = std::byte { 'S' };
        mn[0x15] = std::byte { '1' };
        write_u32le_at(&mn, kPayloadOff + 0x08U, 0x12345678U);
        mn[kPayloadOff + 0x18U] = std::byte { 0xAA };
        mn[kPayloadOff + 0x19U] = std::byte { 0xBB };
        mn[kPayloadOff + 0x1AU] = std::byte { 0xCC };
        mn[kPayloadOff + 0x1BU] = std::byte { 0xDD };
        write_u32le_at(&mn, kPayloadOff + 0x28U, 0x3FC00000U);  // 1.5f
        write_u16le_at(&mn, kPayloadOff + 0x30U, 5U);
        return mn;
    }

    static std::vector<std::byte> make_hp_type6_makernote()
    {
        std::vector<std::byte> mn(0x80U, std::byte { 0 });
        mn[0] = std::byte { 'I' };
        mn[1] = std::byte { 'I' };
        mn[2] = std::byte { 'I' };
        mn[3] = std::byte { 'I' };
        mn[4] = std::byte { 0x06 };
        mn[5] = std::byte { 0x00 };

        write_u16le_at(&mn, 0x000cU, 28U);
        write_u32le_at(&mn, 0x0010U, 50000U);
        std::memcpy(mn.data() + 0x0014U, "2025:03:16 12:34:56", 19U);
        write_u16le_at(&mn, 0x0034U, 200U);
        std::memcpy(mn.data() + 0x0058U, "SERIAL NUMBER:HP-12345", 22U);
        return mn;
    }

    static std::vector<std::byte> make_sigma_makernote_main_u16(uint16_t tag,
                                                                uint16_t value)
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1U);
        append_u16le(&mn, tag);
        append_u16le(&mn, 3U);
        append_u32le(&mn, 1U);
        append_u16le(&mn, value);
        append_u16le(&mn, 0U);
        append_u32le(&mn, 0U);
        return mn;
    }

    static std::vector<std::byte> make_phaseone_makernote_main()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "IIIICwaR");
        append_u32le(&mn, 0x20U);
        while (mn.size() < 0x20U) {
            mn.push_back(std::byte { 0 });
        }

        append_u32le(&mn, 3U);
        append_u32le(&mn, 0U);

        append_u32le(&mn, 0x0100U);
        append_u32le(&mn, 4U);
        append_u32le(&mn, 4U);
        append_u32le(&mn, 1U);

        append_u32le(&mn, 0x0105U);
        append_u32le(&mn, 4U);
        append_u32le(&mn, 4U);
        append_u32le(&mn, 35U);

        append_u32le(&mn, 0x0110U);
        append_u32le(&mn, 4U);
        append_u32le(&mn, 32U);
        append_u32le(&mn, 0x60U);

        while (mn.size() < 0x60U) {
            mn.push_back(std::byte { 0 });
        }

        append_bytes(&mn, std::string_view("IIII\x01\x00\x00\x00", 8));
        append_u32le(&mn, 0x0CU);
        append_u32le(&mn, 1U);
        append_u32le(&mn, 0U);
        append_u32le(&mn, 0x0400U);
        append_u32le(&mn, 4U);
        append_u32le(&mn, 7U);
        return mn;
    }


    static std::vector<std::byte> make_nikon_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        // IFD0 at offset 8 (relative to TIFF header start).
        append_u16le(&mn, 2);
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x01020304U);

        // VRInfo (0x001f) UNDEFINED[8] stored out-of-line at offset 38.
        append_u16le(&mn, 0x001f);
        append_u16le(&mn, 7);
        append_u32le(&mn, 8);
        append_u32le(&mn, 38);

        append_u32le(&mn, 0);

        EXPECT_EQ(mn.size(), 48U);
        append_bytes(&mn, "0101");
        mn.push_back(std::byte { 1 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });

        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_binary_subdirs()
    {
        // Nikon MakerNote with an embedded TIFF header, and a number of
        // BinaryData subdirectory tags (UNDEFINED byte blobs) commonly seen in
        // real-world Nikon RAW files.
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        static constexpr uint16_t kEntryCount = 8;
        append_u16le(&mn, kEntryCount);

        uint32_t value_off = 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U
                             + 4U;

        // Tag 0x0001 LONG value 0x01020304 (inline).
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x01020304U);

        // VRInfo (0x001f) UNDEFINED[8].
        append_u16le(&mn, 0x001f);
        append_u16le(&mn, 7);
        append_u32le(&mn, 8);
        append_u32le(&mn, value_off);
        value_off += 8;

        // DistortInfo (0x002b) UNDEFINED[16].
        append_u16le(&mn, 0x002b);
        append_u16le(&mn, 7);
        append_u32le(&mn, 16);
        append_u32le(&mn, value_off);
        value_off += 16;

        // FlashInfo (0x00a8) UNDEFINED[49].
        append_u16le(&mn, 0x00a8);
        append_u16le(&mn, 7);
        append_u32le(&mn, 49);
        append_u32le(&mn, value_off);
        value_off += 49;

        // MultiExposure (0x00b0) UNDEFINED[16].
        append_u16le(&mn, 0x00b0);
        append_u16le(&mn, 7);
        append_u32le(&mn, 16);
        append_u32le(&mn, value_off);
        value_off += 16;

        // AFInfo2 (0x00b7) UNDEFINED[30].
        append_u16le(&mn, 0x00b7);
        append_u16le(&mn, 7);
        append_u32le(&mn, 30);
        append_u32le(&mn, value_off);
        value_off += 30;

        // FileInfo (0x00b8) UNDEFINED[10].
        append_u16le(&mn, 0x00b8);
        append_u16le(&mn, 7);
        append_u32le(&mn, 10);
        append_u32le(&mn, value_off);
        value_off += 10;

        // RetouchInfo (0x00bb) UNDEFINED[8].
        append_u16le(&mn, 0x00bb);
        append_u16le(&mn, 7);
        append_u32le(&mn, 8);
        append_u32le(&mn, value_off);
        value_off += 8;

        append_u32le(&mn, 0);  // next IFD

        const uint32_t expected_value_start
            = 10U + 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U + 4U;
        EXPECT_EQ(mn.size(), expected_value_start);

        // VRInfo value bytes (version "0101", vr_enabled=1, vr_mode=2).
        append_bytes(&mn, "0101");
        mn.push_back(std::byte { 1 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });

        // DistortInfo value bytes (version "0100", AutoDistortionControl=1).
        {
            std::array<std::byte, 16> raw {};
            raw[0] = std::byte { '0' };
            raw[1] = std::byte { '1' };
            raw[2] = std::byte { '0' };
            raw[3] = std::byte { '0' };
            raw[4] = std::byte { 1 };
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        // FlashInfo0106 value bytes (mostly zeros, but include a few fields).
        {
            std::array<std::byte, 49> raw {};
            raw[0]      = std::byte { '0' };
            raw[1]      = std::byte { '1' };
            raw[2]      = std::byte { '0' };
            raw[3]      = std::byte { '6' };
            raw[4]      = std::byte { 1 };     // FlashSource
            raw[6]      = std::byte { 0x11 };  // ExternalFlashFirmware[0]
            raw[7]      = std::byte { 0x22 };  // ExternalFlashFirmware[1]
            raw[8]      = std::byte { 0x33 };  // ExternalFlashFlags
            raw[0x27]   = std::byte { 0x80 };  // FlashCompensation (-128)
            raw[0x002a] = std::byte { 0xFF };  // FlashGroupCCompensation (-1)
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        // MultiExposure value bytes (version "0100", mode=1, shots=2, gain=3).
        append_bytes(&mn, "0100");
        append_u32le(&mn, 1);
        append_u32le(&mn, 2);
        append_u32le(&mn, 3);

        // AFInfo2 value bytes (version "0100", some fields + offsets).
        {
            std::array<std::byte, 30> raw {};
            raw[0]    = std::byte { '0' };
            raw[1]    = std::byte { '1' };
            raw[2]    = std::byte { '0' };
            raw[3]    = std::byte { '0' };
            raw[4]    = std::byte { 1 };  // ContrastDetectAF
            raw[5]    = std::byte { 8 };  // AFAreaMode
            raw[6]    = std::byte { 3 };  // PhaseDetectAF
            raw[7]    = std::byte { 2 };  // PrimaryAFPoint
            raw[8]    = std::byte { 0xaa };
            raw[9]    = std::byte { 0xbb };
            raw[10]   = std::byte { 0xcc };
            raw[11]   = std::byte { 0xdd };
            raw[12]   = std::byte { 0xee };
            raw[0x10] = std::byte { 0x34 };  // AFImageWidth (0x1234 le)
            raw[0x11] = std::byte { 0x12 };
            raw[0x12] = std::byte { 0x78 };  // AFImageHeight (0x5678 le)
            raw[0x13] = std::byte { 0x56 };
            raw[0x1c] = std::byte { 1 };  // ContrastDetectAFInFocus
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        // FileInfo value bytes (version "0100", card=2, dir=99, file=8).
        append_bytes(&mn, "0100");
        append_u16le(&mn, 2);
        append_u16le(&mn, 99);
        append_u16le(&mn, 8);

        // RetouchInfo value bytes (version "0200", processing=-1).
        {
            std::array<std::byte, 8> raw {};
            raw[0] = std::byte { '0' };
            raw[1] = std::byte { '2' };
            raw[2] = std::byte { '0' };
            raw[3] = std::byte { '0' };
            raw[5] = std::byte { 0xFF };
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        EXPECT_EQ(mn.size(), static_cast<size_t>(10U + value_off));
        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_flashinfo_0108()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x00a8);
        append_u16le(&mn, 7);
        append_u32le(&mn, 49);
        append_u32le(&mn, 26);
        append_u32le(&mn, 0);

        std::array<std::byte, 49> raw {};
        raw[0]      = std::byte { '0' };
        raw[1]      = std::byte { '1' };
        raw[2]      = std::byte { '0' };
        raw[3]      = std::byte { '8' };
        raw[4]      = std::byte { 1 };
        raw[6]      = std::byte { 0x12 };
        raw[7]      = std::byte { 0x34 };
        raw[0x000a] = std::byte { 0x82 };
        raw[0x0011] = std::byte { 0x02 };
        raw[0x0012] = std::byte { 0x34 };
        raw[0x0028] = std::byte { 0x7f };
        raw[0x0029] = std::byte { 0x7e };
        raw[0x002a] = std::byte { 0x7d };
        raw[0x0014] = std::byte { 0x66 };
        raw[0x0015] = std::byte { 0x55 };
        mn.insert(mn.end(), raw.begin(), raw.end());
        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_flashinfo_0102()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x00a8);
        append_u16le(&mn, 7);
        append_u32le(&mn, 21);
        append_u32le(&mn, 26);
        append_u32le(&mn, 0);

        std::array<std::byte, 21> raw {};
        raw[0]      = std::byte { '0' };
        raw[1]      = std::byte { '1' };
        raw[2]      = std::byte { '0' };
        raw[3]      = std::byte { '2' };
        raw[4]      = std::byte { 1 };
        raw[0x000a] = std::byte { 0x11 };
        raw[0x0010] = std::byte { 0x00 };
        raw[0x0011] = std::byte { 0x00 };
        raw[0x0012] = std::byte { 0x22 };
        raw[0x0013] = std::byte { 0x33 };
        raw[0x0014] = std::byte { 0x44 };
        mn.insert(mn.end(), raw.begin(), raw.end());
        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_flashinfo_0101()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x00a8);
        append_u16le(&mn, 7);
        append_u32le(&mn, 19);
        append_u32le(&mn, 26);
        append_u32le(&mn, 0);

        std::array<std::byte, 19> raw {};
        raw[0]      = std::byte { '0' };
        raw[1]      = std::byte { '1' };
        raw[2]      = std::byte { '0' };
        raw[3]      = std::byte { '1' };
        raw[4]      = std::byte { 1 };
        raw[0x000a] = std::byte { 0x11 };
        raw[0x000f] = std::byte { 0x02 };
        raw[0x0010] = std::byte { 0x03 };
        raw[0x0011] = std::byte { 0x22 };
        raw[0x0012] = std::byte { 0x33 };
        mn.insert(mn.end(), raw.begin(), raw.end());
        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_flashinfo_0103()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x00a8);
        append_u16le(&mn, 7);
        append_u32le(&mn, 40);
        append_u32le(&mn, 26);
        append_u32le(&mn, 0);

        std::array<std::byte, 40> raw {};
        raw[0]      = std::byte { '0' };
        raw[1]      = std::byte { '1' };
        raw[2]      = std::byte { '0' };
        raw[3]      = std::byte { '5' };
        raw[4]      = std::byte { 2 };
        raw[0x000a] = std::byte { 0x11 };
        raw[0x0011] = std::byte { 0x02 };
        raw[0x0012] = std::byte { 0x34 };
        raw[0x0013] = std::byte { 0x22 };
        raw[0x0014] = std::byte { 0x33 };
        raw[0x0015] = std::byte { 0x44 };
        raw[0x001b] = std::byte { 0x55 };
        raw[0x001d] = std::byte { 0x66 };
        raw[0x0027] = std::byte { 0x77 };
        mn.insert(mn.end(), raw.begin(), raw.end());
        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_flashinfo_0300()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x00a8);
        append_u16le(&mn, 7);
        append_u32le(&mn, 49);
        append_u32le(&mn, 26);
        append_u32le(&mn, 0);

        std::array<std::byte, 49> raw {};
        raw[0]      = std::byte { '0' };
        raw[1]      = std::byte { '3' };
        raw[2]      = std::byte { '0' };
        raw[3]      = std::byte { '0' };
        raw[4]      = std::byte { 1 };
        raw[8]      = std::byte { 0x90 };
        raw[0x000a] = std::byte { 0x81 };
        raw[0x0011] = std::byte { 0x02 };
        raw[0x0012] = std::byte { 0x34 };
        raw[0x0021] = std::byte { 0x40 };
        raw[0x0025] = std::byte { 2 };
        raw[0x0026] = std::byte { 35 };
        raw[0x0028] = std::byte { 0x41 };
        raw[0x0029] = std::byte { 0x42 };
        raw[0x002a] = std::byte { 0x43 };
        raw[0x0014] = std::byte { 0x22 };
        mn.insert(mn.end(), raw.begin(), raw.end());
        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_flashinfo_0106()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x00a8);
        append_u16le(&mn, 7);
        append_u32le(&mn, 43);
        append_u32le(&mn, 26);
        append_u32le(&mn, 0);

        std::array<std::byte, 43> raw {};
        raw[0]      = std::byte { '0' };
        raw[1]      = std::byte { '1' };
        raw[2]      = std::byte { '0' };
        raw[3]      = std::byte { '6' };
        raw[0x0027] = std::byte { 0x12 };
        raw[0x0028] = std::byte { 0x21 };
        raw[0x0029] = std::byte { 0x22 };
        raw[0x002a] = std::byte { 0x23 };
        mn.insert(mn.end(), raw.begin(), raw.end());
        return mn;
    }


    static std::vector<std::byte> make_apple_makernote()
    {
        // Minimal "Apple iOS" MakerNote sample:
        // prefix + endian marker + classic big-endian IFD.
        std::vector<std::byte> mn;
        append_bytes(&mn, "Apple iOS");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 1 });
        append_bytes(&mn, "MM");
        EXPECT_EQ(mn.size(), 14U);

        // IFD at offset 14, big-endian.
        append_u16be(&mn, 2);  // entry count

        // Tag 0x0001 LONG value 17 (inline).
        append_u16be(&mn, 0x0001);
        append_u16be(&mn, 4);
        append_u32be(&mn, 1);
        append_u32be(&mn, 17);

        // Tag 0x0004 SHORT value 2 (inline).
        append_u16be(&mn, 0x0004);
        append_u16be(&mn, 3);
        append_u32be(&mn, 1);
        append_u16be(&mn, 2);
        append_u16be(&mn, 0);

        append_u32be(&mn, 0);  // next IFD
        return mn;
    }

    static std::vector<std::byte> make_apple_makernote_extended()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Apple iOS");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 1 });
        append_bytes(&mn, "MM");
        EXPECT_EQ(mn.size(), 14U);

        static constexpr uint16_t kEntryCount = 4;
        const uint32_t array_off = 14U + 2U + uint32_t(kEntryCount) * 12U + 4U;
        const uint32_t text_off  = array_off + 6U;

        append_u16be(&mn, kEntryCount);

        append_u16be(&mn, 0x0001);
        append_u16be(&mn, 4);
        append_u32be(&mn, 1U);
        append_u32be(&mn, 17U);

        append_u16be(&mn, 0x0004);
        append_u16be(&mn, 3);
        append_u32be(&mn, 1U);
        append_u16be(&mn, 2U);
        append_u16be(&mn, 0U);

        append_u16be(&mn, 0x0007);
        append_u16be(&mn, 3);
        append_u32be(&mn, 3U);
        append_u32be(&mn, array_off);

        append_u16be(&mn, 0x0008);
        append_u16be(&mn, 2);
        append_u32be(&mn, 6U);
        append_u32be(&mn, text_off);

        append_u32be(&mn, 0);
        EXPECT_EQ(mn.size(), static_cast<size_t>(array_off));

        append_u16be(&mn, 100U);
        append_u16be(&mn, 200U);
        append_u16be(&mn, 300U);

        EXPECT_EQ(mn.size(), static_cast<size_t>(text_off));
        append_bytes(&mn, "HELLO");
        mn.push_back(std::byte { 0 });
        return mn;
    }


    static std::vector<std::byte> make_olympus_makernote()
    {
        // Minimal Olympus MakerNote sample:
        // "OLYMP\0" + u16(version) + classic IFD at +8.
        //
        // Note: Many Olympus MakerNotes use offsets relative to the outer EXIF
        // TIFF header, so we patch the value offset in the test to be an
        // absolute offset in the generated TIFF bytes.
        std::vector<std::byte> mn;
        append_bytes(&mn, "OLYMP");
        mn.push_back(std::byte { 0 });
        append_u16le(&mn, 1);  // version
        EXPECT_EQ(mn.size(), 8U);

        // IFD0 at offset +8.
        append_u16le(&mn, 1);  // entry count

        // Tag 0x0200 LONG[3] stored out-of-line (absolute offset patched later).
        append_u16le(&mn, 0x0200);
        append_u16le(&mn, 4);
        append_u32le(&mn, 3);
        append_u32le(&mn, 0);  // value offset placeholder

        append_u32le(&mn, 0);  // next IFD
        return mn;
    }

    static std::vector<std::byte> make_olympus_makernote_olympus_signature()
    {
        // Olympus MakerNote variant:
        // "OLYMPUS\0" + byte order marker + u16 + classic IFD at +12.
        // Sub-IFD offsets are relative to the MakerNote start, and are often
        // stored as LONG (not IFD) pointers.
        std::vector<std::byte> mn;
        append_bytes(&mn, "OLYMPUS");
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");
        append_u16le(&mn, 3);
        EXPECT_EQ(mn.size(), 12U);

        // Root IFD at +12.
        append_u16le(&mn, 1);  // entry count

        const uint32_t sub_ifd_off = 12U + 18U;  // directly after this IFD

        // Tag 0x4000 (Main) LONG offset to sub-IFD (MakerNote-relative).
        append_u16le(&mn, 0x4000);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, sub_ifd_off);

        append_u32le(&mn, 0);  // next IFD
        EXPECT_EQ(mn.size(), sub_ifd_off);

        // Sub-IFD containing a single main tag (Quality).
        append_u16le(&mn, 1);  // entry count
        append_u16le(&mn, 0x0201);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 2);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);  // next IFD

        return mn;
    }

    static std::vector<std::byte>
    make_olympus_makernote_omsystem_nested_subifds()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "OM SYSTEM");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");
        append_u16le(&mn, 3);
        EXPECT_EQ(mn.size(), 16U);

        static constexpr uint32_t kMainIfdOff     = 16U;
        static constexpr uint32_t kMainEntryCount = 2U;
        static constexpr uint32_t kMainIfdSize    = 2U + kMainEntryCount * 12U
                                                 + 4U;
        static constexpr uint32_t kEquipmentIfdOff = kMainIfdOff + kMainIfdSize;
        static constexpr uint32_t kEquipmentIfdSize     = 18U;
        static constexpr uint32_t kCameraSettingsIfdOff = kEquipmentIfdOff
                                                          + kEquipmentIfdSize;
        static constexpr uint32_t kCameraSettingsEntryCount = 3U;
        static constexpr uint32_t kCameraSettingsIfdSize
            = 2U + kCameraSettingsEntryCount * 12U + 4U;
        static constexpr uint32_t kAFTargetIfdOff = kCameraSettingsIfdOff
                                                    + kCameraSettingsIfdSize;
        static constexpr uint32_t kSubjectDetectIfdOff = kAFTargetIfdOff + 18U;

        append_u16le(&mn, static_cast<uint16_t>(kMainEntryCount));

        append_u16le(&mn, 0x2010);  // Equipment
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kEquipmentIfdOff);

        append_u16le(&mn, 0x2020);  // CameraSettings
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kCameraSettingsIfdOff);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kEquipmentIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0100);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 7);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kCameraSettingsIfdOff);

        append_u16le(&mn, static_cast<uint16_t>(kCameraSettingsEntryCount));

        append_u16le(&mn, 0x0100);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 1);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x030A);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, kAFTargetIfdOff);

        append_u16le(&mn, 0x030B);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, kSubjectDetectIfdOff);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kAFTargetIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0000);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 11);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kSubjectDetectIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x000A);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 33);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);

        return mn;
    }

    static std::vector<std::byte>
    make_olympus_makernote_oldstyle_nested_subifds(uint32_t maker_note_off)
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "OLYMP");
        mn.push_back(std::byte { 0 });
        append_u16le(&mn, 1);
        EXPECT_EQ(mn.size(), 8U);

        static constexpr uint32_t kMainIfdOff     = 8U;
        static constexpr uint32_t kMainEntryCount = 1U;
        static constexpr uint32_t kMainIfdSize    = 2U + kMainEntryCount * 12U
                                                 + 4U;
        static constexpr uint32_t kCameraSettingsIfdOff = kMainIfdOff
                                                          + kMainIfdSize;
        static constexpr uint32_t kCameraSettingsEntryCount = 3U;
        static constexpr uint32_t kCameraSettingsIfdSize
            = 2U + kCameraSettingsEntryCount * 12U + 4U;
        static constexpr uint32_t kAFTargetIfdOff = kCameraSettingsIfdOff
                                                    + kCameraSettingsIfdSize;
        static constexpr uint32_t kSubjectDetectIfdOff = kAFTargetIfdOff + 18U;

        append_u16le(&mn, static_cast<uint16_t>(kMainEntryCount));
        append_u16le(&mn, 0x2020);  // CameraSettings
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, maker_note_off + kCameraSettingsIfdOff);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kCameraSettingsIfdOff);

        append_u16le(&mn, static_cast<uint16_t>(kCameraSettingsEntryCount));

        append_u16le(&mn, 0x0100);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 2);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x030A);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, maker_note_off + kAFTargetIfdOff);

        append_u16le(&mn, 0x030B);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, maker_note_off + kSubjectDetectIfdOff);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kAFTargetIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0000);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 22);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kSubjectDetectIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x000A);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 44);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);

        return mn;
    }

    static std::vector<std::byte>
    make_olympus_makernote_omsystem_main_subifd_matrix()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "OM SYSTEM");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");
        append_u16le(&mn, 3);
        EXPECT_EQ(mn.size(), 16U);

        static constexpr uint32_t kMainIfdOff     = 16U;
        static constexpr uint32_t kMainEntryCount = 8U;
        static constexpr uint32_t kMainIfdSize    = 2U + kMainEntryCount * 12U
                                                 + 4U;
        static constexpr uint32_t kSubIfdSize = 18U;

        static constexpr uint32_t kEquipmentIfdOff = kMainIfdOff + kMainIfdSize;
        static constexpr uint32_t kRawDevIfdOff    = kEquipmentIfdOff
                                                  + kSubIfdSize;
        static constexpr uint32_t kRawDev2IfdOff = kRawDevIfdOff + kSubIfdSize;
        static constexpr uint32_t kImageProcIfdOff = kRawDev2IfdOff
                                                     + kSubIfdSize;
        static constexpr uint32_t kFocusInfoIfdOff = kImageProcIfdOff
                                                     + kSubIfdSize;
        static constexpr uint32_t kFeTags0IfdOff = kFocusInfoIfdOff
                                                   + kSubIfdSize;
        static constexpr uint32_t kFeTags1IfdOff = kFeTags0IfdOff + kSubIfdSize;
        static constexpr uint32_t kRawInfoIfdOff = kFeTags1IfdOff + kSubIfdSize;

        append_u16le(&mn, static_cast<uint16_t>(kMainEntryCount));

        append_u16le(&mn, 0x2010);  // Equipment
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kEquipmentIfdOff);

        append_u16le(&mn, 0x2030);  // RawDevelopment
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kRawDevIfdOff);

        append_u16le(&mn, 0x2031);  // RawDevelopment2
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kRawDev2IfdOff);

        append_u16le(&mn, 0x2040);  // ImageProcessing
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kImageProcIfdOff);

        append_u16le(&mn, 0x2050);  // FocusInfo
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kFocusInfoIfdOff);

        append_u16le(&mn, 0x2100);  // FETags 0
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kFeTags0IfdOff);

        append_u16le(&mn, 0x2200);  // FETags 1
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kFeTags1IfdOff);

        append_u16le(&mn, 0x3000);  // RawInfo
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kRawInfoIfdOff);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kEquipmentIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0100);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 7);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kRawDevIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0000);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 3);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kRawDev2IfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0100);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 4);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kImageProcIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0000);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 5);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kFocusInfoIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0209);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 1);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kFeTags0IfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0100);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 6);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kFeTags1IfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0100);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 8);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kRawInfoIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0614);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 321);
        append_u32le(&mn, 0);

        return mn;
    }

    static std::vector<std::byte>
    make_olympus_makernote_omsystem_focusinfo_name_context(
        bool with_camera_settings_stabilization)
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "OM SYSTEM");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");
        append_u16le(&mn, 3);
        EXPECT_EQ(mn.size(), 16U);

        static constexpr uint32_t kMainIfdOff      = 16U;
        static constexpr uint32_t kMainEntryCount  = 2U;
        static constexpr uint32_t kSubIfdEntrySize = 18U;
        static constexpr uint32_t kMainIfdSize     = 2U + kMainEntryCount * 12U
                                                 + 4U;
        static constexpr uint32_t kCameraSettingsIfdOff = kMainIfdOff
                                                          + kMainIfdSize;
        static constexpr uint32_t kFocusInfoIfdOff = kCameraSettingsIfdOff
                                                     + kSubIfdEntrySize;
        static constexpr uint32_t kFocusInfoValueOff = kFocusInfoIfdOff
                                                       + kSubIfdEntrySize;

        append_u16le(&mn, static_cast<uint16_t>(kMainEntryCount));

        append_u16le(&mn, 0x2020);  // CameraSettings
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kCameraSettingsIfdOff);

        append_u16le(&mn, 0x2050);  // FocusInfo
        append_u16le(&mn, 13);
        append_u32le(&mn, 1);
        append_u32le(&mn, kFocusInfoIfdOff);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kCameraSettingsIfdOff);

        append_u16le(&mn, 1);
        if (with_camera_settings_stabilization) {
            append_u16le(&mn, 0x0604);
            append_u16le(&mn, 4);
            append_u32le(&mn, 1);
            append_u32le(&mn, 1);
        } else {
            append_u16le(&mn, 0x0100);
            append_u16le(&mn, 3);
            append_u32le(&mn, 1);
            append_u16le(&mn, 7);
            append_u16le(&mn, 0);
        }
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kFocusInfoIfdOff);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x1600);
        append_u16le(&mn, 7);
        append_u32le(&mn, 8);
        append_u32le(&mn, kFocusInfoValueOff);
        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), kFocusInfoValueOff);

        for (uint8_t i = 0; i < 8U; ++i) {
            mn.push_back(std::byte { static_cast<uint8_t>(i + 1U) });
        }

        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_info_blocks()
    {
        // Nikon MakerNote with additional BinaryData information blocks:
        // PictureControlData, WorldTime, ISOInfo, HDRInfo, and LocationInfo.
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        static constexpr uint16_t kEntryCount = 6;
        append_u16le(&mn, kEntryCount);

        uint32_t value_off = 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U
                             + 4U;

        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x01020304U);

        append_u16le(&mn, 0x0023);  // PictureControlData
        append_u16le(&mn, 7);
        append_u32le(&mn, 66);
        append_u32le(&mn, value_off);
        value_off += 66;

        append_u16le(&mn, 0x0024);  // WorldTime
        append_u16le(&mn, 7);
        append_u32le(&mn, 8);
        append_u32le(&mn, value_off);
        value_off += 8;

        append_u16le(&mn, 0x0025);  // ISOInfo
        append_u16le(&mn, 7);
        append_u32le(&mn, 14);
        append_u32le(&mn, value_off);
        value_off += 14;

        append_u16le(&mn, 0x0035);  // HDRInfo
        append_u16le(&mn, 7);
        append_u32le(&mn, 8);
        append_u32le(&mn, value_off);
        value_off += 8;

        append_u16le(&mn, 0x0039);  // LocationInfo
        append_u16le(&mn, 7);
        append_u32le(&mn, 19);
        append_u32le(&mn, value_off);
        value_off += 19;

        append_u32le(&mn, 0);

        const uint32_t expected_value_start
            = 10U + 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U + 4U;
        EXPECT_EQ(mn.size(), expected_value_start);

        {
            std::array<std::byte, 66> raw {};
            raw[0]    = std::byte { '0' };
            raw[1]    = std::byte { '2' };
            raw[2]    = std::byte { '0' };
            raw[3]    = std::byte { '0' };
            raw[4]    = std::byte { 'N' };
            raw[5]    = std::byte { 'E' };
            raw[6]    = std::byte { 'U' };
            raw[7]    = std::byte { 'T' };
            raw[8]    = std::byte { 'R' };
            raw[9]    = std::byte { 'A' };
            raw[10]   = std::byte { 'L' };
            raw[0x18] = std::byte { 'S' };
            raw[0x19] = std::byte { 'T' };
            raw[0x1A] = std::byte { 'A' };
            raw[0x1B] = std::byte { 'N' };
            raw[0x1C] = std::byte { 'D' };
            raw[0x1D] = std::byte { 'A' };
            raw[0x1E] = std::byte { 'R' };
            raw[0x1F] = std::byte { 'D' };
            raw[0x30] = std::byte { 5 };
            raw[0x31] = std::byte { 6 };
            raw[0x33] = std::byte { 7 };
            raw[0x35] = std::byte { 8 };
            raw[0x37] = std::byte { 9 };
            raw[0x39] = std::byte { 10 };
            raw[0x3B] = std::byte { 11 };
            raw[0x3D] = std::byte { 12 };
            raw[0x3F] = std::byte { 13 };
            raw[0x40] = std::byte { 14 };
            raw[0x41] = std::byte { 15 };
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        append_u16le(&mn, 0xFDE4U);
        mn.push_back(std::byte { 1 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });

        {
            std::array<std::byte, 14> raw {};
            raw[0]  = std::byte { 64 };
            raw[4]  = std::byte { 0x90 };
            raw[5]  = std::byte { 0x01 };
            raw[6]  = std::byte { 125 };
            raw[10] = std::byte { 0x20 };
            raw[11] = std::byte { 0x03 };
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        append_bytes(&mn, "0100");
        mn.push_back(std::byte { 1 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 3 });
        mn.push_back(std::byte { 4 });

        {
            std::array<std::byte, 19> raw {};
            raw[0]  = std::byte { '0' };
            raw[1]  = std::byte { '1' };
            raw[2]  = std::byte { '0' };
            raw[3]  = std::byte { '0' };
            raw[4]  = std::byte { 5 };
            raw[5]  = std::byte { 1 };
            raw[6]  = std::byte { 2 };
            raw[7]  = std::byte { 3 };
            raw[8]  = std::byte { 4 };
            raw[9]  = std::byte { 'T' };
            raw[10] = std::byte { 'O' };
            raw[11] = std::byte { 'K' };
            raw[12] = std::byte { 'Y' };
            raw[13] = std::byte { 'O' };
            raw[14] = std::byte { '-' };
            raw[15] = std::byte { 'J' };
            raw[16] = std::byte { 'P' };
            raw[17] = std::byte { 0 };
            raw[18] = std::byte { 0 };
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        return mn;
    }

    static std::vector<std::byte>
    make_nikon_makernote_with_preview_settings_and_aftune()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        static constexpr uint16_t kEntryCount = 4;
        append_u16le(&mn, kEntryCount);

        const uint32_t settings_off
            = 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U + 4U;
        const uint32_t aftune_off  = settings_off + 48U;
        const uint32_t preview_off = aftune_off + 4U;

        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x01020304U);

        append_u16le(&mn, 0x0011);  // PreviewIFD
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, preview_off);

        append_u16le(&mn, 0x004E);  // NikonSettings
        append_u16le(&mn, 7);
        append_u32le(&mn, 48U);
        append_u32le(&mn, settings_off);

        append_u16le(&mn, 0x00B9);  // AFTune
        append_u16le(&mn, 7);
        append_u32le(&mn, 4U);
        append_u32le(&mn, aftune_off);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), static_cast<size_t>(10U + settings_off));

        {
            std::vector<std::byte> settings(48U, std::byte { 0 });
            write_u32le_at(&settings, 20U, 3U);

            write_u16le_at(&settings, 24U + 0U, 0x0001);
            write_u16be_at(&settings, 24U + 2U, 4);
            write_u32le_at(&settings, 24U + 4U, 6400U);

            write_u16le_at(&settings, 32U + 0U, 0x0046);
            write_u16be_at(&settings, 32U + 2U, 1);
            write_u32le_at(&settings, 32U + 4U, 1U);

            write_u16le_at(&settings, 40U + 0U, 0x0063);
            write_u16be_at(&settings, 40U + 2U, 3);
            write_u32le_at(&settings, 40U + 4U, 9U);

            mn.insert(mn.end(), settings.begin(), settings.end());
        }

        mn.push_back(std::byte { 1 });
        mn.push_back(std::byte { 4 });
        mn.push_back(std::byte { 0xFD });
        mn.push_back(std::byte { 5 });

        EXPECT_EQ(mn.size(), static_cast<size_t>(10U + preview_off));

        append_u16le(&mn, 4);

        append_u16le(&mn, 0x0103);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 6);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x0201);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x0200U);

        append_u16le(&mn, 0x0202);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x1234U);

        append_u16le(&mn, 0x0213);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 2);
        append_u16le(&mn, 0);

        append_u32le(&mn, 0);
        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_colorbalancec_0104()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        static constexpr uint16_t kEntryCount = 1;
        append_u16le(&mn, kEntryCount);

        const uint32_t colorbalance_off
            = 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U + 4U;

        append_u16le(&mn, 0x0014);  // NRWData
        append_u16le(&mn, 7);
        append_u32le(&mn, 0x0124U);
        append_u32le(&mn, colorbalance_off);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), static_cast<size_t>(10U + colorbalance_off));

        std::vector<std::byte> raw(0x0124U, std::byte { 0 });
        std::memcpy(raw.data(), "NRW 0104", 8U);
        write_u16le_at(&raw, 0x0020U, 200U);

        const uint32_t main_levels[4]     = { 444U, 512U, 513U, 396U };
        const uint32_t daylight_levels[4] = { 374U, 512U, 513U, 480U };
        const uint32_t cloudy_levels[4]   = { 410U, 512U, 513U, 437U };
        const uint32_t shade_levels[4]    = { 467U, 512U, 513U, 359U };
        const uint32_t tungsten_levels[4] = { 253U, 512U, 513U, 714U };
        const uint32_t fluor_w_levels[4]  = { 394U, 512U, 513U, 636U };
        const uint32_t fluor_n_levels[4]  = { 397U, 512U, 513U, 466U };
        const uint32_t fluor_d_levels[4]  = { 447U, 512U, 513U, 375U };
        const uint32_t mercury_levels[4]  = { 440U, 512U, 513U, 411U };
        const uint32_t custom_levels[4]   = { 0U, 0U, 0U, 0U };
        const uint32_t auto_levels[4]     = { 421U, 512U, 513U, 409U };
        const uint32_t* level_sets[11]    = {
            main_levels,     daylight_levels, cloudy_levels,  shade_levels,
            tungsten_levels, fluor_w_levels,  fluor_n_levels, fluor_d_levels,
            mercury_levels,  custom_levels,   auto_levels,
        };
        const uint32_t level_offsets[11] = {
            0x0038U, 0x004CU, 0x0060U, 0x0074U, 0x0088U, 0x009CU,
            0x00B0U, 0x00C4U, 0x00D8U, 0x0100U, 0x0114U,
        };
        for (size_t i = 0; i < 11U; ++i) {
            for (size_t k = 0; k < 4U; ++k) {
                write_u32le_at(&raw, level_offsets[i] + k * 4U,
                               level_sets[i][k]);
            }
        }
        mn.insert(mn.end(), raw.begin(), raw.end());
        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_afinfo2_0101()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        static constexpr uint16_t kEntryCount = 1;
        append_u16le(&mn, kEntryCount);

        const uint32_t afinfo_off
            = 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U + 4U;

        append_u16le(&mn, 0x00B7);  // AFInfo2
        append_u16le(&mn, 7);
        append_u32le(&mn, 0x0053U);
        append_u32le(&mn, afinfo_off);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), static_cast<size_t>(10U + afinfo_off));

        std::vector<std::byte> raw(0x0053U, std::byte { 0 });
        std::memcpy(raw.data(), "0101", 4U);
        raw[4] = std::byte { 1 };  // AFDetectionMethod = contrast detect
        raw[5] = std::byte { 2 };  // AFAreaMode
        raw[6] = std::byte { 1 };  // FocusPointSchema = 51-point
        for (size_t i = 0; i < 7U; ++i) {
            raw[0x0008U + i] = std::byte { static_cast<uint8_t>(i + 1U) };
            raw[0x0030U + i] = std::byte { static_cast<uint8_t>(0x10U + i) };
        }
        raw[0x001cU] = std::byte { 1 };
        raw[0x0044U] = std::byte { 5 };
        write_u16le_at(&raw, 0x0046U, 640U);
        write_u16le_at(&raw, 0x0048U, 480U);
        write_u16le_at(&raw, 0x004AU, 120U);
        write_u16le_at(&raw, 0x004CU, 90U);
        write_u16le_at(&raw, 0x004EU, 36U);
        write_u16le_at(&raw, 0x0050U, 24U);
        raw[0x0052U] = std::byte { 1 };

        mn.insert(mn.end(), raw.begin(), raw.end());
        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_afinfo2_0301()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        static constexpr uint16_t kEntryCount = 1;
        append_u16le(&mn, kEntryCount);

        const uint32_t afinfo_off
            = 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U + 4U;

        append_u16le(&mn, 0x00B7);  // AFInfo2
        append_u16le(&mn, 7);
        append_u32le(&mn, 0x0039U);
        append_u32le(&mn, afinfo_off);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), static_cast<size_t>(10U + afinfo_off));

        std::vector<std::byte> raw(0x0039U, std::byte { 0 });
        std::memcpy(raw.data(), "0301", 4U);
        raw[4]      = std::byte { 1 };
        raw[5]      = std::byte { 8 };
        raw[6]      = std::byte { 8 };
        raw[7]      = std::byte { 1 };
        raw[0x000a] = std::byte { 0xaa };
        raw[0x000b] = std::byte { 0xbb };
        raw[0x000c] = std::byte { 0xcc };
        raw[0x000d] = std::byte { 0xdd };
        raw[0x000e] = std::byte { 0xee };
        raw[0x000f] = std::byte { 0xff };
        raw[0x0010] = std::byte { 0x11 };
        raw[0x0011] = std::byte { 0x22 };
        raw[0x0012] = std::byte { 0x33 };
        raw[0x0013] = std::byte { 0x44 };
        raw[0x0014] = std::byte { 0x55 };
        raw[0x002a] = std::byte { 0x80 };
        raw[0x002b] = std::byte { 0x17 };
        raw[0x002c] = std::byte { 0xb0 };
        raw[0x002d] = std::byte { 0x0f };
        raw[0x002e] = std::byte { 0xc0 };
        raw[0x002f] = std::byte { 0x0b };
        raw[0x0030] = std::byte { 0xd8 };
        raw[0x0031] = std::byte { 0x07 };
        raw[0x0032] = std::byte { 0x9e };
        raw[0x0033] = std::byte { 0x00 };
        raw[0x0034] = std::byte { 0x8c };
        raw[0x0035] = std::byte { 0x00 };
        raw[0x0038] = std::byte { 5 };
        mn.insert(mn.end(), raw.begin(), raw.end());
        return mn;
    }

    static std::vector<std::byte>
    make_nikon_makernote_with_single_settings_long_tag(uint16_t settings_tag,
                                                       uint32_t value)
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        static constexpr uint16_t kEntryCount = 2;
        append_u16le(&mn, kEntryCount);

        const uint32_t settings_off
            = 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U + 4U;

        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x01020304U);

        append_u16le(&mn, 0x004E);  // NikonSettings
        append_u16le(&mn, 7);
        append_u32le(&mn, 32U);
        append_u32le(&mn, settings_off);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), static_cast<size_t>(10U + settings_off));

        {
            std::vector<std::byte> settings(32U, std::byte { 0 });
            write_u32le_at(&settings, 20U, 1U);

            write_u16le_at(&settings, 24U + 0U, settings_tag);
            write_u16be_at(&settings, 24U + 2U, 4);
            write_u32le_at(&settings, 24U + 4U, value);

            mn.insert(mn.end(), settings.begin(), settings.end());
        }

        return mn;
    }

    static std::vector<std::byte>
    make_nikon_makernote_with_single_main_long_tag(uint16_t tag, uint32_t value)
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        append_u16le(&mn, 1);
        append_u16le(&mn, tag);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1U);
        append_u32le(&mn, value);
        append_u32le(&mn, 0);

        return mn;
    }

    static std::vector<std::byte>
    make_nikon_makernote_with_single_main_bytes_tag(
        uint16_t tag, std::span<const std::byte> value)
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        append_u16le(&mn, 1);
        append_u16le(&mn, tag);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(value.size()));
        append_u32le(&mn, 26U);
        append_u32le(&mn, 0);

        mn.insert(mn.end(), value.begin(), value.end());
        return mn;
    }

    static std::vector<std::byte> make_panasonic_makernote_with_subdirs()
    {
        // Minimal Panasonic MakerNote sample:
        // classic IFD at offset 0 with UNDEFINED binary sub-blocks:
        // - FaceDetInfo (0x004e)
        // - FaceRecInfo (0x0061)
        // - TimeInfo (0x2003)

        // FaceDetInfo: int16u format (word offsets).
        std::vector<std::byte> facedet;
        append_u16le(&facedet, 1);   // NumFacePositions
        append_u16le(&facedet, 10);  // X
        append_u16le(&facedet, 20);  // Y
        append_u16le(&facedet, 30);  // W
        append_u16le(&facedet, 40);  // H
        EXPECT_EQ(facedet.size(), 10U);

        // FaceRecInfo: byte offsets.
        std::vector<std::byte> facerec(52, std::byte { 0 });
        write_u16le_at(&facerec, 0, 1);  // FacesRecognized
        {
            const std::string_view name = "Bob";
            for (size_t i = 0; i < name.size() && (4U + i) < facerec.size();
                 ++i) {
                facerec[4U + i] = std::byte { static_cast<uint8_t>(name[i]) };
            }
        }
        write_u16le_at(&facerec, 24U + 0, 1);
        write_u16le_at(&facerec, 24U + 2, 2);
        write_u16le_at(&facerec, 24U + 4, 3);
        write_u16le_at(&facerec, 24U + 6, 4);
        {
            const std::string_view age = "25";
            for (size_t i = 0; i < age.size() && (32U + i) < facerec.size();
                 ++i) {
                facerec[32U + i] = std::byte { static_cast<uint8_t>(age[i]) };
            }
        }
        EXPECT_EQ(facerec.size(), 52U);

        // TimeInfo: BCD date-time at 0..7 and shot number at 16..19.
        std::vector<std::byte> timeinfo(20, std::byte { 0xFF });
        // 2024:06:27 12:53:52.54 (BCD nibbles).
        timeinfo[0] = std::byte { 0x20 };
        timeinfo[1] = std::byte { 0x24 };
        timeinfo[2] = std::byte { 0x06 };
        timeinfo[3] = std::byte { 0x27 };
        timeinfo[4] = std::byte { 0x12 };
        timeinfo[5] = std::byte { 0x53 };
        timeinfo[6] = std::byte { 0x52 };
        timeinfo[7] = std::byte { 0x54 };
        write_u32le_at(&timeinfo, 16U, 123);
        EXPECT_EQ(timeinfo.size(), 20U);

        std::vector<std::byte> mn;
        append_u16le(&mn, 3);  // entry count

        // FaceDetInfo (UNDEFINED, out-of-line value offset patched by caller).
        append_u16le(&mn, 0x004e);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(facedet.size()));
        append_u32le(&mn, 0);

        // FaceRecInfo (UNDEFINED, out-of-line value offset patched by caller).
        append_u16le(&mn, 0x0061);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(facerec.size()));
        append_u32le(&mn, 0);

        // TimeInfo (UNDEFINED, out-of-line value offset patched by caller).
        append_u16le(&mn, 0x2003);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(timeinfo.size()));
        append_u32le(&mn, 0);

        append_u32le(&mn, 0);  // next IFD

        mn.insert(mn.end(), facedet.begin(), facedet.end());
        mn.insert(mn.end(), facerec.begin(), facerec.end());
        mn.insert(mn.end(), timeinfo.begin(), timeinfo.end());
        return mn;
    }

    static std::vector<std::byte>
    make_panasonic_makernote_with_extended_subdirs(bool truncate_next_ifd)
    {
        // Extended Panasonic MakerNote sample:
        // - FaceDetInfo with 5 face positions
        // - FaceRecInfo with 3 recognized faces
        // - TimeInfo with both BCD datetime and TimeLapseShotNumber

        std::vector<std::byte> facedet(42, std::byte { 0 });
        write_u16le_at(&facedet, 0, 5);
        {
            static constexpr uint16_t kFaceOffsets[5]
                = { 0x0001, 0x0005, 0x0009, 0x000D, 0x0011 };
            static constexpr uint16_t kFaceData[5][4] = {
                { 10, 20, 30, 40 },     { 50, 60, 70, 80 },
                { 90, 100, 110, 120 },  { 130, 140, 150, 160 },
                { 170, 180, 190, 200 },
            };
            for (uint32_t i = 0; i < 5U; ++i) {
                const size_t byte_off = size_t(kFaceOffsets[i]) * 2U;
                for (uint32_t j = 0; j < 4U; ++j) {
                    write_u16le_at(&facedet, byte_off + size_t(j) * 2U,
                                   kFaceData[i][j]);
                }
            }
        }

        std::vector<std::byte> facerec(148, std::byte { 0 });
        write_u16le_at(&facerec, 0, 3);
        {
            static constexpr std::string_view kNames[3] = { "Bob", "Ana",
                                                            "Eve" };
            static constexpr std::string_view kAges[3]  = { "25", "30", "19" };
            static constexpr uint16_t kPos[3][4]        = {
                { 1, 2, 3, 4 },
                { 5, 6, 7, 8 },
                { 9, 10, 11, 12 },
            };
            for (uint32_t i = 0; i < 3U; ++i) {
                const size_t name_off = 4U + size_t(i) * 48U;
                const size_t pos_off  = 24U + size_t(i) * 48U;
                const size_t age_off  = 32U + size_t(i) * 48U;
                for (size_t j = 0; j < kNames[i].size(); ++j) {
                    facerec[name_off + j]
                        = std::byte { static_cast<uint8_t>(kNames[i][j]) };
                }
                for (uint32_t j = 0; j < 4U; ++j) {
                    write_u16le_at(&facerec, pos_off + size_t(j) * 2U,
                                   kPos[i][j]);
                }
                for (size_t j = 0; j < kAges[i].size(); ++j) {
                    facerec[age_off + j]
                        = std::byte { static_cast<uint8_t>(kAges[i][j]) };
                }
            }
        }

        std::vector<std::byte> timeinfo(20, std::byte { 0xFF });
        timeinfo[0] = std::byte { 0x20 };
        timeinfo[1] = std::byte { 0x24 };
        timeinfo[2] = std::byte { 0x06 };
        timeinfo[3] = std::byte { 0x27 };
        timeinfo[4] = std::byte { 0x12 };
        timeinfo[5] = std::byte { 0x53 };
        timeinfo[6] = std::byte { 0x52 };
        timeinfo[7] = std::byte { 0x54 };
        write_u32le_at(&timeinfo, 16U, 321U);

        std::vector<std::byte> mn;
        append_u16le(&mn, 3);

        append_u16le(&mn, 0x004e);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(facedet.size()));
        append_u32le(&mn, 0);

        append_u16le(&mn, 0x0061);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(facerec.size()));
        append_u32le(&mn, 0);

        append_u16le(&mn, 0x2003);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(timeinfo.size()));
        append_u32le(&mn, 0);

        if (!truncate_next_ifd) {
            append_u32le(&mn, 0);
        }

        mn.insert(mn.end(), facedet.begin(), facedet.end());
        mn.insert(mn.end(), facerec.begin(), facerec.end());
        mn.insert(mn.end(), timeinfo.begin(), timeinfo.end());
        return mn;
    }

    static std::vector<std::byte> make_panasonic_type2_makernote()
    {
        // Minimal Panasonic Type2 MakerNote sample (fixed-layout binary).
        std::vector<std::byte> mn;
        append_bytes(&mn, "TEST");  // MakerNoteType (string[4])
        append_u16le(&mn, 0);
        append_u16le(&mn, 42);  // Gain (word offset 3 -> byte offset 6)
        EXPECT_EQ(mn.size(), 8U);
        return mn;
    }


    static std::vector<std::byte> make_pentax_makernote()
    {
        // Minimal Pentax MakerNote sample:
        // "AOC\0II" + u16le(count) + classic IFD entries at +8.
        std::vector<std::byte> mn;
        append_bytes(&mn, "AOC");
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");
        append_u16le(&mn, 1);
        EXPECT_EQ(mn.size(), 8U);

        // Tag 0x0001 SHORT value 2 (inline).
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 2);
        append_u16le(&mn, 0);

        append_u32le(&mn, 0);  // next IFD
        return mn;
    }

    static std::vector<std::byte> make_pentax_makernote_with_binary_subdirs()
    {
        // Dense Pentax sample:
        // - inline UNDEFINED[4] ShakeReductionInfo -> mk_pentax_srinfo_0
        // - inline BYTE scalar FaceInfo -> mk_pentax_faceinfo_0
        // - out-of-line UNDEFINED[21] AEInfo -> mk_pentax_aeinfo2_0
        // - out-of-line UNDEFINED[6] ShotInfo -> mk_pentax_shotinfo_0
        std::vector<std::byte> mn;
        append_bytes(&mn, "AOC");
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");

        static constexpr uint16_t kEntryCount = 4;
        append_u16le(&mn, kEntryCount);

        const uint32_t aeinfo_off = 8U + uint32_t(kEntryCount) * 12U + 4U;
        const uint32_t shot_off   = aeinfo_off + 21U;

        append_u16le(&mn, 0x005C);
        append_u16le(&mn, 7);
        append_u32le(&mn, 4U);
        append_u32le(&mn, 0x44332211U);

        append_u16le(&mn, 0x0060);
        append_u16le(&mn, 1);
        append_u32le(&mn, 1U);
        append_u32le(&mn, 3U);

        append_u16le(&mn, 0x0206);
        append_u16le(&mn, 7);
        append_u32le(&mn, 21U);
        append_u32le(&mn, aeinfo_off);

        append_u16le(&mn, 0x0226);
        append_u16le(&mn, 7);
        append_u32le(&mn, 6U);
        append_u32le(&mn, shot_off);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), static_cast<size_t>(aeinfo_off));

        for (uint8_t i = 0; i < 21U; ++i) {
            mn.push_back(std::byte { static_cast<uint8_t>(i + 1U) });
        }
        EXPECT_EQ(mn.size(), static_cast<size_t>(shot_off));

        for (uint8_t i = 0; i < 6U; ++i) {
            mn.push_back(std::byte { static_cast<uint8_t>(0xA0U + i) });
        }
        return mn;
    }

    static std::vector<std::byte> make_pentax_type2_makernote()
    {
        // Optio 330/430 route: classic Pentax MakerNote bytes, but model-driven
        // decode should land them under mk_pentax_type2_0 instead of mk_pentax0.
        std::vector<std::byte> mn;
        append_bytes(&mn, "AOC");
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");

        static constexpr uint16_t kEntryCount = 4;
        append_u16le(&mn, kEntryCount);

        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 2U);
        append_u16le(&mn, 0U);

        append_u16le(&mn, 0x0004);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 1U);
        append_u16le(&mn, 0U);

        append_u16le(&mn, 0x0005);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 7U);
        append_u16le(&mn, 0U);

        append_u16le(&mn, 0x1000);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 0x1234U);
        append_u16le(&mn, 0U);

        append_u32le(&mn, 0);
        return mn;
    }

    static std::vector<std::byte> make_pentax_makernote_with_main_0062()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "AOC");
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");
        append_u16le(&mn, 1);

        append_u16le(&mn, 0x0062);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 7U);
        append_u16le(&mn, 0U);

        append_u32le(&mn, 0);
        return mn;
    }

    static std::vector<std::byte> make_pentax_makernote_with_zero_face_tables()
    {
        // FacePos/FaceSize should only be expanded when the MakerNote reports
        // actual faces. This sample keeps the raw tables but sets face count 0.
        std::vector<std::byte> mn;
        append_bytes(&mn, "AOC");
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");

        static constexpr uint16_t kEntryCount = 3;
        append_u16le(&mn, kEntryCount);

        const uint32_t facepos_off  = 8U + uint32_t(kEntryCount) * 12U + 4U;
        const uint32_t facesize_off = facepos_off + 4U;

        append_u16le(&mn, 0x0060);
        append_u16le(&mn, 1);
        append_u32le(&mn, 1U);
        append_u32le(&mn, 0U);

        append_u16le(&mn, 0x0227);
        append_u16le(&mn, 7);
        append_u32le(&mn, 4U);
        append_u32le(&mn, facepos_off);

        append_u16le(&mn, 0x0228);
        append_u16le(&mn, 7);
        append_u32le(&mn, 4U);
        append_u32le(&mn, facesize_off);

        append_u32le(&mn, 0);
        EXPECT_EQ(mn.size(), static_cast<size_t>(facepos_off));

        mn.push_back(std::byte { 0x10 });
        mn.push_back(std::byte { 0x11 });
        mn.push_back(std::byte { 0x12 });
        mn.push_back(std::byte { 0x13 });
        EXPECT_EQ(mn.size(), static_cast<size_t>(facesize_off));

        mn.push_back(std::byte { 0x20 });
        mn.push_back(std::byte { 0x21 });
        mn.push_back(std::byte { 0x22 });
        mn.push_back(std::byte { 0x23 });
        return mn;
    }

    static std::vector<std::byte> make_ricoh_type2_makernote()
    {
        // Minimal Ricoh Type2 sample:
        // "RICOH\0" + 2 bytes padding + little-endian Type2 IFD at +12.
        std::vector<std::byte> mn;
        append_bytes(&mn, "RICOH");
        mn.push_back(std::byte { 0 });
        append_u16le(&mn, 0);

        static constexpr uint16_t kEntryCount = 2;
        append_u16le(&mn, kEntryCount);
        append_u16le(&mn, 0);

        append_u16le(&mn, 0x0207);
        append_u16le(&mn, 2);
        append_u32le(&mn, 5U);
        const size_t model_off_pos = mn.size();
        append_u32le(&mn, 0U);

        append_u16le(&mn, 0x0300);
        append_u16le(&mn, 2);
        append_u32le(&mn, 6U);
        const size_t make_off_pos = mn.size();
        append_u32le(&mn, 0U);

        append_u32le(&mn, 0);

        const uint32_t model_off = static_cast<uint32_t>(mn.size());
        write_u32le_at(&mn, model_off_pos, model_off);

        append_bytes(&mn, "GRIII");
        mn.push_back(std::byte { 0 });

        const uint32_t make_off = static_cast<uint32_t>(mn.size());
        write_u32le_at(&mn, make_off_pos, make_off);

        append_bytes(&mn, "RICOH");
        mn.push_back(std::byte { 0 });
        return mn;
    }

    static std::vector<std::byte> make_ricoh_type2_padded_ifd_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "II");
        append_u16le(&mn, 42U);
        append_u32le(&mn, 8U);

        static constexpr uint16_t kEntryCount = 3U;
        append_u16le(&mn, kEntryCount);
        append_u16le(&mn, 0U);

        append_u16le(&mn, 0x0104U);
        append_u16le(&mn, 4U);
        append_u32le(&mn, 1U);
        append_u32le(&mn, 1U);

        const size_t model_off_pos = mn.size() + 8U;
        append_u16le(&mn, 0x0207U);
        append_u16le(&mn, 2U);
        append_u32le(&mn, 5U);
        append_u32le(&mn, 0U);

        const size_t make_off_pos = mn.size() + 8U;
        append_u16le(&mn, 0x0300U);
        append_u16le(&mn, 2U);
        append_u32le(&mn, 6U);
        append_u32le(&mn, 0U);

        append_u32le(&mn, 0U);

        const uint32_t model_off = static_cast<uint32_t>(mn.size());
        write_u32le_at(&mn, model_off_pos, model_off);
        append_bytes(&mn, "HZ15");
        mn.push_back(std::byte { 0 });

        const uint32_t make_off = static_cast<uint32_t>(mn.size());
        write_u32le_at(&mn, make_off_pos, make_off);
        append_bytes(&mn, "RICOH");
        mn.push_back(std::byte { 0 });
        return mn;
    }

    static std::vector<std::byte> make_samsung_stmn_makernote()
    {
        // Minimal Samsung STMN MakerNote sample:
        // - MakerNoteVersion (8 bytes): "STMN100\0"
        // - PreviewImageStart (u32le) at +8
        // - PreviewImageLength (u32le) at +12
        // - SamsungIFD at +44:
        //     u32le(entry_count=1) + entry(12) + next_ifd(4) + payload...
        std::vector<std::byte> mn;
        append_bytes(&mn, "STMN100");
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 8U);

        append_u32le(&mn, 0x12345678U);  // PreviewImageStart
        append_u32le(&mn, 0x00010002U);  // PreviewImageLength
        EXPECT_EQ(mn.size(), 16U);

        // Fill reserved slots up to tag 11 (offset 44).
        mn.resize(44U, std::byte { 0 });
        EXPECT_EQ(mn.size(), 44U);

        // SamsungIFD: one ASCII tag whose out-of-line value is at base+0.
        append_u32le(&mn, 1);  // entry count (u32le)
        append_u16le(&mn, 0x0004);
        append_u16le(&mn, 2);  // ASCII
        append_u32le(&mn, 6);  // count
        append_u32le(&mn, 0);  // offset relative to end-of-IFD (base)
        append_u32le(&mn, 0);  // next IFD

        EXPECT_EQ(mn.size(), 64U);
        append_bytes(&mn, "HELLO");
        mn.push_back(std::byte { 0 });
        return mn;
    }

    static std::vector<std::byte> make_samsung_type2_makernote()
    {
        // Minimal Samsung Type2 MakerNote sample:
        // classic IFD at offset 0 with one unknown Type2 tag and
        // PictureWizard (0x0021) UNDEFINED[10].
        std::vector<std::byte> mn;
        append_u16le(&mn, 2);  // entry count

        append_u16le(&mn, 0x0004);
        append_u16le(&mn, 7);
        append_u32le(&mn, 4);
        append_bytes(&mn, "ABCD");

        append_u16le(&mn, 0x0021);  // PictureWizard
        append_u16le(&mn, 7);       // UNDEFINED
        append_u32le(&mn, 10);      // count (5 * u16)
        append_u32le(&mn, 30);      // value offset (payload starts at +30)

        append_u32le(&mn, 0);  // next IFD
        EXPECT_EQ(mn.size(), 30U);

        append_u16le(&mn, 1);
        append_u16le(&mn, 2);
        append_u16le(&mn, 3);
        append_u16le(&mn, 4);
        append_u16le(&mn, 5);
        EXPECT_EQ(mn.size(), 40U);
        return mn;
    }

    static std::vector<std::byte>
    make_samsung_type2_makernote_u16_picturewizard()
    {
        // Samsung Type2 variant where PictureWizard is already typed as SHORT[5].
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);  // entry count

        append_u16le(&mn, 0x0021);  // PictureWizard
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 5);       // count
        append_u32le(&mn, 18);      // out-of-line payload starts at +18

        append_u32le(&mn, 0);  // next IFD
        EXPECT_EQ(mn.size(), 18U);

        append_u16le(&mn, 11);
        append_u16le(&mn, 22);
        append_u16le(&mn, 33);
        append_u16le(&mn, 44);
        append_u16le(&mn, 55);
        EXPECT_EQ(mn.size(), 28U);
        return mn;
    }

    static std::vector<std::byte>
    make_samsung_type2_makernote_with_a002_and_a003()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 2);

        append_u16le(&mn, 0xA002);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 0x1234U);
        append_u16le(&mn, 0U);

        append_u16le(&mn, 0xA003);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1U);
        append_u16le(&mn, 0x5678U);
        append_u16le(&mn, 0U);

        append_u32le(&mn, 0);
        return mn;
    }

    static std::vector<std::byte> make_samsung_makernote_compat_digits()
    {
        // Non-IFD Samsung maker note for the compatibility fallback:
        // bytes[10..13] are decimal digits, so tag 0x0000 becomes fixed text.
        std::vector<std::byte> mn(16U, std::byte { 0 });
        mn[0]  = std::byte { 'B' };
        mn[1]  = std::byte { 'A' };
        mn[2]  = std::byte { 'D' };
        mn[3]  = std::byte { '!' };
        mn[10] = std::byte { '2' };
        mn[11] = std::byte { '0' };
        mn[12] = std::byte { '2' };
        mn[13] = std::byte { '4' };
        return mn;
    }

    static std::vector<std::byte> make_sony_makernote_tag9050b_ciphered()
    {
        // Minimal Sony MakerNote:
        // Classic IFD at offset 0, but out-of-line value offsets are absolute
        // (relative to outer TIFF stream).
        //
        // We include only Tag9050 (UNDEFINED bytes), containing the ciphered
        // BinaryData sub-block that OpenMeta decodes into a derived IFD.
        const size_t payload_size = 0x90;
        std::vector<std::byte> plain(payload_size, std::byte { 0 });

        // Shutter (u16[3]) at 0x0026.
        plain[0x0026] = std::byte { 0x01 };
        plain[0x0027] = std::byte { 0x00 };
        plain[0x0028] = std::byte { 0x02 };
        plain[0x0029] = std::byte { 0x00 };
        plain[0x002A] = std::byte { 0x03 };
        plain[0x002B] = std::byte { 0x00 };

        // ShutterCount (u32) at 0x003A.
        plain[0x003A] = std::byte { 0x11 };
        plain[0x003B] = std::byte { 0x22 };
        plain[0x003C] = std::byte { 0x33 };
        plain[0x003D] = std::byte { 0x44 };

        // InternalSerialNumber (6 bytes) at 0x0088.
        plain[0x0088] = std::byte { 0x01 };
        plain[0x0089] = std::byte { 0x02 };
        plain[0x008A] = std::byte { 0x03 };
        plain[0x008B] = std::byte { 0x04 };
        plain[0x008C] = std::byte { 0x05 };
        plain[0x008D] = std::byte { 0x06 };

        std::vector<std::byte> cipher(payload_size, std::byte { 0 });
        for (size_t i = 0; i < plain.size(); ++i) {
            const uint8_t p = static_cast<uint8_t>(plain[i]);
            cipher[i]       = std::byte { sony_encipher_byte(p) };
        }

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);  // entry count
        append_u16le(&mn, 0x9050);
        append_u16le(&mn, 7);  // UNDEFINED
        append_u32le(&mn, static_cast<uint32_t>(cipher.size()));
        append_u32le(&mn, 0);  // value offset placeholder (patched by caller)
        append_u32le(&mn, 0);  // next IFD

        mn.insert(mn.end(), cipher.begin(), cipher.end());
        return mn;
    }

    static std::vector<std::byte>
    make_sony_makernote_ciphered_blob(uint16_t tag,
                                      std::span<const std::byte> plain)
    {
        std::vector<std::byte> cipher(plain.size(), std::byte { 0 });
        for (size_t i = 0; i < plain.size(); ++i) {
            const uint8_t p = static_cast<uint8_t>(plain[i]);
            cipher[i]       = std::byte { sony_encipher_byte(p) };
        }

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);  // entry count
        append_u16le(&mn, tag);
        append_u16le(&mn, 7);  // UNDEFINED
        append_u32le(&mn, static_cast<uint32_t>(cipher.size()));
        append_u32le(&mn, 0);  // value offset placeholder (absolute)
        append_u32le(&mn, 0);  // next IFD

        mn.insert(mn.end(), cipher.begin(), cipher.end());
        return mn;
    }

    static std::vector<std::byte>
    make_sony_makernote_tag9400_ciphered_with_v0(uint8_t v0)
    {
        std::vector<std::byte> plain(0x140, std::byte { 0 });

        // v0 != 0x0C so the default decoder path stays on tag9400c.
        plain[0x0000] = std::byte { v0 };

        plain[0x0009] = std::byte { 1 };
        plain[0x000A] = std::byte { 2 };
        plain[0x0016] = std::byte { 3 };
        plain[0x001E] = std::byte { 4 };
        plain[0x0029] = std::byte { 5 };
        plain[0x002A] = std::byte { 6 };

        plain[0x0012] = std::byte { 0x44 };
        plain[0x0013] = std::byte { 0x33 };
        plain[0x0014] = std::byte { 0x22 };
        plain[0x0015] = std::byte { 0x11 };

        plain[0x001A] = std::byte { 0x88 };
        plain[0x001B] = std::byte { 0x77 };
        plain[0x001C] = std::byte { 0x66 };
        plain[0x001D] = std::byte { 0x55 };

        plain[0x0053] = std::byte { 0xE7 };
        plain[0x0054] = std::byte { 0x07 };
        plain[0x0133] = std::byte { 7 };
        plain[0x0139] = std::byte { 23 };
        plain[0x013F] = std::byte { 7 };

        return make_sony_makernote_ciphered_blob(0x9400, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9400_ciphered()
    {
        return make_sony_makernote_tag9400_ciphered_with_v0(0x07);
    }

    static std::vector<std::byte> make_sony_makernote_tag9400b_ciphered()
    {
        std::vector<std::byte> plain(0x60, std::byte { 0 });

        plain[0x0000] = std::byte { 0x0C };

        write_u32le_at(&plain, 0x0008, 0x01020304U);
        write_u32le_at(&plain, 0x000C, 0x05060708U);
        plain[0x0010] = std::byte { 2 };
        write_u32le_at(&plain, 0x0016, 0x11223344U);
        plain[0x001E] = std::byte { 6 };
        plain[0x0024] = std::byte { 3 };
        plain[0x0025] = std::byte { 8 };
        write_u16le_at(&plain, 0x003F, 0x0123U);
        plain[0x0046] = std::byte { 21 };

        return make_sony_makernote_ciphered_blob(0x9400, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9401_ciphered()
    {
        std::vector<std::byte> plain(0x03E7, std::byte { 0 });

        // First known ISOInfo candidate:
        //   setting at +0, min at +2, max at +4.
        plain[0x03E2] = std::byte { 0x00 };
        plain[0x03E4] = std::byte { 16 };
        plain[0x03E6] = std::byte { 32 };

        return make_sony_makernote_ciphered_blob(0x9401, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9405b_ciphered()
    {
        std::vector<std::byte> plain(0x03F8, std::byte { 0 });

        plain[0x0004] = std::byte { 0x34 };
        plain[0x0005] = std::byte { 0x12 };

        plain[0x0010] = std::byte { 0x01 };
        plain[0x0011] = std::byte { 0x00 };
        plain[0x0012] = std::byte { 0x00 };
        plain[0x0013] = std::byte { 0x00 };
        plain[0x0014] = std::byte { 0xFA };
        plain[0x0015] = std::byte { 0x00 };
        plain[0x0016] = std::byte { 0x00 };
        plain[0x0017] = std::byte { 0x00 };

        plain[0x0024] = std::byte { 0x78 };
        plain[0x0025] = std::byte { 0x56 };
        plain[0x0026] = std::byte { 0x34 };
        plain[0x0027] = std::byte { 0x12 };

        plain[0x005E] = std::byte { 9 };
        plain[0x0060] = std::byte { 0xBC };
        plain[0x0061] = std::byte { 0x0A };

        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(int(i) - 8);
            write_u16le_at(&plain, 0x0064U + i * 2U, static_cast<uint16_t>(v));
        }

        write_u16le_at(&plain, 0x0342, 0x1342U);
        write_u16le_at(&plain, 0x034E, 0x134EU);
        write_u16le_at(&plain, 0x035A, 0x135AU);

        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(40 + int(i));
            write_u16le_at(&plain, 0x034AU + i * 2U, static_cast<uint16_t>(v));
            write_u16le_at(&plain, 0x0350U + i * 2U,
                           static_cast<uint16_t>(60 + int(i)));
            write_u16le_at(&plain, 0x035CU + i * 2U,
                           static_cast<uint16_t>(80 + int(i)));
            write_u16le_at(&plain, 0x0368U + i * 2U,
                           static_cast<uint16_t>(100 + int(i)));
        }
        for (size_t i = 0; i < 32; ++i) {
            write_u16le_at(&plain, 0x037CU + i * 2U,
                           static_cast<uint16_t>(120 + int(i)));
            write_u16le_at(&plain, 0x0384U + i * 2U,
                           static_cast<uint16_t>(160 + int(i)));
            write_u16le_at(&plain, 0x039CU + i * 2U,
                           static_cast<uint16_t>(200 + int(i)));
            write_u16le_at(&plain, 0x03B0U + i * 2U,
                           static_cast<uint16_t>(240 + int(i)));
            write_u16le_at(&plain, 0x03B8U + i * 2U,
                           static_cast<uint16_t>(280 + int(i)));
        }

        return make_sony_makernote_ciphered_blob(0x9405, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9405b_rx100m4_ciphered()
    {
        std::vector<std::byte> plain(0x0360, std::byte { 0 });

        plain[0x0004] = std::byte { 0x34 };
        plain[0x0005] = std::byte { 0x12 };

        plain[0x0010] = std::byte { 0x01 };
        plain[0x0011] = std::byte { 0x00 };
        plain[0x0012] = std::byte { 0x00 };
        plain[0x0013] = std::byte { 0x00 };
        plain[0x0014] = std::byte { 0xFA };
        plain[0x0015] = std::byte { 0x00 };
        plain[0x0016] = std::byte { 0x00 };
        plain[0x0017] = std::byte { 0x00 };

        plain[0x0024] = std::byte { 0x78 };
        plain[0x0025] = std::byte { 0x56 };
        plain[0x0026] = std::byte { 0x34 };
        plain[0x0027] = std::byte { 0x12 };

        plain[0x005E] = std::byte { 9 };
        plain[0x0060] = std::byte { 0xBC };
        plain[0x0061] = std::byte { 0x0A };

        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(int(i) - 8);
            write_u16le_at(&plain, 0x0064U + i * 2U, static_cast<uint16_t>(v));
        }

        write_u16le_at(&plain, 0x035A, 0x135AU);
        return make_sony_makernote_ciphered_blob(0x9405, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9416_ciphered()
    {
        std::vector<std::byte> plain(0x0988, std::byte { 0 });

        plain[0x0000] = std::byte { 0x10 };
        plain[0x0004] = std::byte { 0x39 };
        plain[0x0005] = std::byte { 0x05 };

        plain[0x000C] = std::byte { 0x05 };
        plain[0x000D] = std::byte { 0x00 };
        plain[0x000E] = std::byte { 0x00 };
        plain[0x000F] = std::byte { 0x00 };
        plain[0x0010] = std::byte { 0x08 };
        plain[0x0011] = std::byte { 0x00 };
        plain[0x0012] = std::byte { 0x00 };
        plain[0x0013] = std::byte { 0x00 };

        plain[0x001D] = std::byte { 0xEF };
        plain[0x001E] = std::byte { 0xCD };
        plain[0x001F] = std::byte { 0xAB };
        plain[0x0020] = std::byte { 0x89 };

        plain[0x002B] = std::byte { 7 };
        plain[0x0048] = std::byte { 3 };
        plain[0x004B] = std::byte { 0x80 };
        plain[0x004C] = std::byte { 0x07 };

        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(20 + int(i));
            write_u16le_at(&plain, 0x004FU + i * 2U, static_cast<uint16_t>(v));
        }

        for (size_t i = 0; i < 16; ++i) {
            write_u16le_at(&plain, 0x088FU + i * 2U,
                           static_cast<uint16_t>(100 + int(i)));
            write_u16le_at(&plain, 0x0891U + i * 2U,
                           static_cast<uint16_t>(140 + int(i)));
        }
        plain[0x08B5] = std::byte { 1 };
        plain[0x08B7] = std::byte { 1 };
        plain[0x08E5] = std::byte { 1 };
        for (size_t i = 0; i < 32; ++i) {
            write_u16le_at(&plain, 0x0708U + i * 2U,
                           static_cast<uint16_t>(340 + int(i)));
            write_u16le_at(&plain, 0x0843U + i * 2U,
                           static_cast<uint16_t>(380 + int(i)));
            write_u16le_at(&plain, 0x089DU + i * 2U,
                           static_cast<uint16_t>(180 + int(i)));
            write_u16le_at(&plain, 0x0914U + i * 2U,
                           static_cast<uint16_t>(220 + int(i)));
            write_u16le_at(&plain, 0x0916U + i * 2U,
                           static_cast<uint16_t>(260 + int(i)));
            write_u16le_at(&plain, 0x0945U + i * 2U,
                           static_cast<uint16_t>(300 + int(i)));
        }
        plain[0x074F] = std::byte { 1 };

        return make_sony_makernote_ciphered_blob(0x9416, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9416_ilce1_ciphered()
    {
        std::vector<std::byte> plain(0x0958, std::byte { 0 });

        plain[0x0000] = std::byte { 0x10 };
        plain[0x0004] = std::byte { 0x39 };
        plain[0x0005] = std::byte { 0x05 };

        plain[0x000C] = std::byte { 0x05 };
        plain[0x000D] = std::byte { 0x00 };
        plain[0x000E] = std::byte { 0x00 };
        plain[0x000F] = std::byte { 0x00 };
        plain[0x0010] = std::byte { 0x08 };
        plain[0x0011] = std::byte { 0x00 };
        plain[0x0012] = std::byte { 0x00 };
        plain[0x0013] = std::byte { 0x00 };

        plain[0x001D] = std::byte { 0xEF };
        plain[0x001E] = std::byte { 0xCD };
        plain[0x001F] = std::byte { 0xAB };
        plain[0x0020] = std::byte { 0x89 };

        plain[0x002B] = std::byte { 7 };
        plain[0x0048] = std::byte { 3 };
        plain[0x004B] = std::byte { 0x80 };
        plain[0x004C] = std::byte { 0x07 };

        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(20 + int(i));
            write_u16le_at(&plain, 0x004FU + i * 2U, static_cast<uint16_t>(v));
        }
        for (size_t i = 0; i < 16; ++i) {
            write_u16le_at(&plain, 0x088FU + i * 2U,
                           static_cast<uint16_t>(100 + int(i)));
        }
        plain[0x08B5] = std::byte { 1 };
        for (size_t i = 0; i < 32; ++i) {
            write_u16le_at(&plain, 0x0914U + i * 2U,
                           static_cast<uint16_t>(220 + int(i)));
        }

        return make_sony_makernote_ciphered_blob(0x9416, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9400a_ciphered()
    {
        std::vector<std::byte> plain(0x0053, std::byte { 0 });

        plain[0x0000] = std::byte { 0x07 };

        plain[0x0008] = std::byte { 0x04 };
        plain[0x0009] = std::byte { 0x03 };
        plain[0x000A] = std::byte { 0x02 };
        plain[0x000B] = std::byte { 0x01 };

        plain[0x000C] = std::byte { 0x08 };
        plain[0x000D] = std::byte { 0x07 };
        plain[0x000E] = std::byte { 0x06 };
        plain[0x000F] = std::byte { 0x05 };

        plain[0x0010] = std::byte { 2 };
        plain[0x0012] = std::byte { 1 };

        plain[0x001A] = std::byte { 0x44 };
        plain[0x001B] = std::byte { 0x33 };
        plain[0x001C] = std::byte { 0x22 };
        plain[0x001D] = std::byte { 0x11 };

        plain[0x0022] = std::byte { 10 };
        plain[0x0028] = std::byte { 3 };
        plain[0x0029] = std::byte { 8 };

        plain[0x0044] = std::byte { 0xB8 };
        plain[0x0045] = std::byte { 0x0B };
        plain[0x0052] = std::byte { 23 };

        return make_sony_makernote_ciphered_blob(0x9400, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag940e_afinfo_ciphered()
    {
        std::vector<std::byte> plain(0x0180, std::byte { 0 });

        plain[0x0002] = std::byte { 2 };
        plain[0x0004] = std::byte { 5 };
        plain[0x0007] = std::byte { 6 };
        plain[0x0008] = std::byte { 7 };
        plain[0x0009] = std::byte { 8 };
        plain[0x000A] = std::byte { 9 };
        plain[0x000B] = std::byte { 10 };

        for (size_t i = 0; i < 30; ++i) {
            const int16_t v = static_cast<int16_t>(100 + int(i));
            write_u16le_at(&plain, 0x0011U + i * 2U, static_cast<uint16_t>(v));
        }

        plain[0x016E] = std::byte { 0x78 };
        plain[0x016F] = std::byte { 0x56 };
        plain[0x0170] = std::byte { 0x34 };
        plain[0x0171] = std::byte { 0x12 };

        plain[0x017D] = std::byte { 0xFE };
        plain[0x017E] = std::byte { 4 };

        return make_sony_makernote_ciphered_blob(0x940E, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag940e_afinfo79_ciphered()
    {
        std::vector<std::byte> plain(0x0140, std::byte { 0 });

        plain[0x0002] = std::byte { 3 };
        plain[0x0005] = std::byte { 4 };
        for (size_t i = 0; i < 10; ++i) {
            plain[0x0010 + i] = std::byte { static_cast<uint8_t>(i + 1U) };
        }
        plain[0x0037] = std::byte { 55 };
        plain[0x0038] = std::byte { 54 };
        plain[0x0039] = std::byte { 53 };
        plain[0x003A] = std::byte { 3 };
        plain[0x003B] = std::byte { 0x21 };
        plain[0x0043] = std::byte { 2 };
        plain[0x0050] = std::byte { 0xFE };

        for (size_t i = 0; i < 95; ++i) {
            const int16_t v = static_cast<int16_t>(100 + static_cast<int>(i));
            write_u16le_at(&plain, 0x007DU + i * 2U, static_cast<uint16_t>(v));
        }

        return make_sony_makernote_ciphered_blob(0x940E, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag2010i_ciphered()
    {
        // Minimal Sony MakerNote containing Tag2010 (UNDEFINED bytes) for the
        // ciphered Tag2010i subtable.
        //
        // We keep the payload small enough to cover fields asserted in the
        // test; out-of-range fields are skipped by the decoder.
        const size_t payload_size = 0x1800;
        std::vector<std::byte> plain(payload_size, std::byte { 0 });

        // I16 tag 0x0217 = 0x1234.
        plain[0x0217] = std::byte { 0x34 };
        plain[0x0218] = std::byte { 0x12 };

        // WB_RGBLevels u16[3] at 0x0252 = [1, 2, 3].
        plain[0x0252] = std::byte { 0x01 };
        plain[0x0253] = std::byte { 0x00 };
        plain[0x0254] = std::byte { 0x02 };
        plain[0x0255] = std::byte { 0x00 };
        plain[0x0256] = std::byte { 0x03 };
        plain[0x0257] = std::byte { 0x00 };

        // SonyISO u16 at 0x0320 = 100.
        plain[0x0320] = std::byte { 0x64 };
        plain[0x0321] = std::byte { 0x00 };

        // DistortionCorrParams bytes[32] at 0x17D0.
        for (size_t i = 0; i < 32; ++i) {
            plain[0x17D0 + i] = std::byte { static_cast<uint8_t>(i) };
        }

        std::vector<std::byte> cipher(payload_size, std::byte { 0 });
        for (size_t i = 0; i < plain.size(); ++i) {
            const uint8_t p = static_cast<uint8_t>(plain[i]);
            cipher[i]       = std::byte { sony_encipher_byte(p) };
        }

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);  // entry count
        append_u16le(&mn, 0x2010);
        append_u16le(&mn, 7);  // UNDEFINED
        append_u32le(&mn, static_cast<uint32_t>(cipher.size()));
        append_u32le(&mn, 0);  // value offset placeholder (absolute)
        append_u32le(&mn, 0);  // next IFD

        mn.insert(mn.end(), cipher.begin(), cipher.end());
        return mn;
    }

    static std::vector<std::byte> make_sony_makernote_tag202a_ciphered()
    {
        std::vector<std::byte> plain(0x0002, std::byte { 0 });
        plain[0x0001] = std::byte { 7 };
        return make_sony_makernote_ciphered_blob(0x202A, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9402_ciphered()
    {
        std::vector<std::byte> plain(0x002E, std::byte { 0 });
        plain[0x0002] = std::byte { 2 };
        plain[0x0004] = std::byte { 4 };
        plain[0x0016] = std::byte { 0x16 };
        plain[0x0017] = std::byte { 0x17 };
        plain[0x002D] = std::byte { 0x2D };
        return make_sony_makernote_ciphered_blob(0x9402, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9403_ciphered()
    {
        std::vector<std::byte> plain(0x001B, std::byte { 0 });
        plain[0x0004] = std::byte { 4 };
        plain[0x0005] = std::byte { 5 };
        write_u16le_at(&plain, 0x0019, 0x3456);
        return make_sony_makernote_ciphered_blob(0x9403, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9404a_ciphered()
    {
        std::vector<std::byte> plain(0x001B, std::byte { 0 });
        plain[0x0000] = std::byte { 4 };
        plain[0x0003] = std::byte { 1 };
        plain[0x000B] = std::byte { 11 };
        plain[0x000D] = std::byte { 13 };
        write_u16le_at(&plain, 0x0019, 0x2345);
        return make_sony_makernote_ciphered_blob(0x9404, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9404c_ciphered()
    {
        std::vector<std::byte> plain(0x000E, std::byte { 0 });
        plain[0x0000] = std::byte { 17 };
        plain[0x0003] = std::byte { 1 };
        plain[0x000B] = std::byte { 11 };
        plain[0x000D] = std::byte { 13 };
        return make_sony_makernote_ciphered_blob(0x9404, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9404b_ciphered()
    {
        std::vector<std::byte> plain(0x0020, std::byte { 0 });
        plain[0x0000] = std::byte { 15 };
        plain[0x0003] = std::byte { 2 };
        plain[0x000C] = std::byte { 12 };
        plain[0x000E] = std::byte { 14 };
        write_u16le_at(&plain, 0x001E, 0x2345);
        return make_sony_makernote_ciphered_blob(0x9404, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9406_ciphered()
    {
        std::vector<std::byte> plain(0x0009, std::byte { 0 });
        plain[0x0005] = std::byte { 5 };
        plain[0x0006] = std::byte { 6 };
        plain[0x0007] = std::byte { 7 };
        plain[0x0008] = std::byte { 8 };
        return make_sony_makernote_ciphered_blob(0x9406, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag940c_ciphered()
    {
        std::vector<std::byte> plain(0x0016, std::byte { 0 });
        plain[0x0008] = std::byte { 8 };
        write_u16le_at(&plain, 0x0009, 0x0109);
        write_u16le_at(&plain, 0x000B, 0x020B);
        write_u16le_at(&plain, 0x000D, 0x030D);
        write_u16le_at(&plain, 0x0014, 0x0414);
        return make_sony_makernote_ciphered_blob(0x940C, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9050a_ciphered()
    {
        std::vector<std::byte> plain(0x01C1, std::byte { 0 });
        plain[0x0000] = std::byte { 1 };
        plain[0x0001] = std::byte { 2 };
        write_u16le_at(&plain, 0x0020, 10);
        write_u16le_at(&plain, 0x0022, 20);
        write_u16le_at(&plain, 0x0024, 30);
        plain[0x0031] = std::byte { 0x31 };
        write_u32le_at(&plain, 0x0032, 0x44332211U);
        write_u16le_at(&plain, 0x003A, 0x1234);
        write_u16le_at(&plain, 0x003C, 0x5678);
        plain[0x003F] = std::byte { 0x3F };
        write_u32le_at(&plain, 0x004C, 0x00ABCDEFU);
        plain[0x0051] = std::byte { 24 };
        plain[0x0052] = std::byte { 3 };
        plain[0x0053] = std::byte { 21 };
        plain[0x0054] = std::byte { 10 };
        plain[0x0055] = std::byte { 11 };
        plain[0x0056] = std::byte { 12 };
        plain[0x0067] = std::byte { 0x67 };
        plain[0x0105] = std::byte { 0x15 };
        plain[0x0106] = std::byte { 0x16 };
        write_u16le_at(&plain, 0x0107, 0x0107);
        write_u16le_at(&plain, 0x0109, 0x0109);
        plain[0x010B] = std::byte { 0x1B };
        plain[0x0114] = std::byte { 0x24 };
        write_u32le_at(&plain, 0x01A0, 0x00112233U);
        write_u32le_at(&plain, 0x01AA, 0x89ABCDEFU);
        write_u32le_at(&plain, 0x01BD, 0x10203040U);
        return make_sony_makernote_ciphered_blob(0x9050, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9050d_ciphered()
    {
        std::vector<std::byte> plain(0x003EU, std::byte { 0 });
        write_u32le_at(&plain, 0x000A, 0x00ABCDEFU);
        write_u16le_at(&plain, 0x001A, 0x1234U);
        write_u16le_at(&plain, 0x001C, 0x5678U);
        plain[0x001F] = std::byte { 0x1F };
        plain[0x0038] = std::byte { 1 };
        plain[0x0039] = std::byte { 2 };
        plain[0x003A] = std::byte { 3 };
        plain[0x003B] = std::byte { 4 };
        plain[0x003C] = std::byte { 5 };
        plain[0x003D] = std::byte { 6 };
        return make_sony_makernote_ciphered_blob(0x9050, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag9405a_ciphered()
    {
        std::vector<std::byte> plain(0x06EA, std::byte { 0 });
        plain[0x0600] = std::byte { 1 };
        plain[0x0601] = std::byte { 2 };
        plain[0x0603] = std::byte { 3 };
        plain[0x0604] = std::byte { 4 };
        write_u16le_at(&plain, 0x0605, 0x0605);
        write_u16le_at(&plain, 0x0608, 0x0608);
        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(30 + int(i));
            write_u16le_at(&plain, 0x064AU + i * 2U, static_cast<uint16_t>(v));
        }
        for (size_t i = 0; i < 32; ++i) {
            const int16_t v = static_cast<int16_t>(-20 + int(i));
            write_u16le_at(&plain, 0x066AU + i * 2U, static_cast<uint16_t>(v));
        }
        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(100 + int(i));
            write_u16le_at(&plain, 0x06CAU + i * 2U, static_cast<uint16_t>(v));
        }
        return make_sony_makernote_ciphered_blob(0x9405, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag940e_ciphered()
    {
        std::vector<std::byte> plain(0x1A14, std::byte { 0 });
        plain[0x1A06] = std::byte { 2 };
        plain[0x1A07] = std::byte { 3 };
        for (size_t i = 0; i < 12; ++i) {
            plain[0x1A08 + i] = std::byte { static_cast<uint8_t>(0xA0 + i) };
        }
        return make_sony_makernote_ciphered_blob(0x940E, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag2010b_ciphered()
    {
        std::vector<std::byte> plain(0x1A50, std::byte { 0 });
        write_u32le_at(&plain, 0x0000, 0x11223344U);
        write_u32le_at(&plain, 0x0004, 0x55667788U);
        write_u32le_at(&plain, 0x0008, 0x99AABBCCU);
        plain[0x0324] = std::byte { 0x24 };
        plain[0x1128] = std::byte { 0x28 };
        plain[0x112C] = std::byte { 0x2C };
        write_u16le_at(&plain, 0x113E, 0x113E);
        write_u16le_at(&plain, 0x1140, 0x1140);
        write_u16le_at(&plain, 0x1218, 0x1218);
        write_u16le_at(&plain, 0x114C, static_cast<uint16_t>(-7));
        write_u16le_at(&plain, 0x1180, 1);
        write_u16le_at(&plain, 0x1182, 2);
        write_u16le_at(&plain, 0x1184, 3);
        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(200 + int(i));
            write_u16le_at(&plain, 0x1A23U + i * 2U, static_cast<uint16_t>(v));
        }
        for (size_t i = 0; i < 0x798; ++i) {
            plain[0x04B4 + i] = std::byte { static_cast<uint8_t>(i) };
        }
        return make_sony_makernote_ciphered_blob(0x2010, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag2010e_ciphered()
    {
        std::vector<std::byte> plain(0x1A90, std::byte { 0 });
        write_u32le_at(&plain, 0x0000, 0xA1B2C3D4U);
        write_u32le_at(&plain, 0x0004, 0x10213243U);
        write_u32le_at(&plain, 0x0008, 0x55667788U);
        plain[0x021C] = std::byte { 0x21 };
        plain[0x0328] = std::byte { 0x32 };
        plain[0x115C] = std::byte { 0x5C };
        plain[0x1160] = std::byte { 0x60 };
        write_u16le_at(&plain, 0x1172, 0x1172);
        write_u16le_at(&plain, 0x1174, 0x1174);
        write_u16le_at(&plain, 0x1254, 0x1254);
        write_u16le_at(&plain, 0x1258, 0x1258);
        write_u16le_at(&plain, 0x1278, 0x1278);
        write_u16le_at(&plain, 0x127A, 0x127A);
        write_u16le_at(&plain, 0x127C, 0x127C);
        write_u16le_at(&plain, 0x1280, 0x1280);
        write_u16le_at(&plain, 0x1180, static_cast<uint16_t>(-9));
        write_u16le_at(&plain, 0x11B4, 4);
        write_u16le_at(&plain, 0x11B6, 5);
        write_u16le_at(&plain, 0x11B8, 6);
        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(300 + int(i));
            write_u16le_at(&plain, 0x1870U + i * 2U, static_cast<uint16_t>(v));
        }
        plain[0x1891] = std::byte { 0x91 };
        plain[0x1892] = std::byte { 0x92 };
        write_u16le_at(&plain, 0x1893, 0x1893);
        write_u16le_at(&plain, 0x1896, 0x1896);
        plain[0x192C] = std::byte { 0x2C };
        plain[0x1A88] = std::byte { 0x88 };
        for (size_t i = 0; i < 0x798; ++i) {
            plain[0x04B8 + i] = std::byte { static_cast<uint8_t>(0x80 + i) };
        }
        return make_sony_makernote_ciphered_blob(0x2010, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag2010c_ciphered()
    {
        std::vector<std::byte> plain(0x1300, std::byte { 0 });
        write_u32le_at(&plain, 0x0000, 0x01020304U);
        write_u32le_at(&plain, 0x0004, 0x11121314U);
        write_u32le_at(&plain, 0x0008, 0x21222324U);
        write_u16le_at(&plain, 0x0200, 0x0080);
        plain[0x0300] = std::byte { 0x30 };
        plain[0x1104] = std::byte { 0x04 };
        plain[0x1110] = std::byte { 0x10 };
        write_u16le_at(&plain, 0x111A, 0x111A);
        write_u16le_at(&plain, 0x1128, static_cast<uint16_t>(-4));
        write_u16le_at(&plain, 0x115C, 7);
        write_u16le_at(&plain, 0x115E, 8);
        write_u16le_at(&plain, 0x1160, 9);
        write_u16le_at(&plain, 0x11F4, 0x1234);
        for (size_t i = 0; i < 0x798; ++i) {
            plain[0x0490 + i] = std::byte { static_cast<uint8_t>(0x20 + i) };
        }
        return make_sony_makernote_ciphered_blob(0x2010, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag2010f_ciphered()
    {
        std::vector<std::byte> plain(0x1930, std::byte { 0 });
        write_u32le_at(&plain, 0x0004, 0x55667788U);
        plain[0x0050] = std::byte { 0x50 };
        plain[0x1014] = std::byte { 0x14 };
        plain[0x1018] = std::byte { 0x18 };
        write_u16le_at(&plain, 0x102A, 0x102A);
        write_u16le_at(&plain, 0x1038, static_cast<uint16_t>(-6));
        write_u16le_at(&plain, 0x106C, 11);
        write_u16le_at(&plain, 0x106E, 12);
        write_u16le_at(&plain, 0x1070, 13);
        write_u16le_at(&plain, 0x1134, 350);
        write_u16le_at(&plain, 0x1136, 240);
        write_u16le_at(&plain, 0x1138, 700);
        write_u16le_at(&plain, 0x113C, 0x113C);
        plain[0x192C] = std::byte { 2 };
        for (size_t i = 0; i < 0x798; ++i) {
            plain[0x01E0 + i] = std::byte { static_cast<uint8_t>(0x40 + i) };
        }
        return make_sony_makernote_ciphered_blob(0x2010, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag2010g_ciphered()
    {
        std::vector<std::byte> plain(0x1960, std::byte { 0 });
        write_u32le_at(&plain, 0x0004, 0x99AABBCCU);
        plain[0x0050] = std::byte { 0x50 };
        plain[0x020C] = std::byte { 0x0C };
        plain[0x0210] = std::byte { 0x10 };
        write_u16le_at(&plain, 0x0222, 0x0222);
        write_u16le_at(&plain, 0x0230, static_cast<uint16_t>(-8));
        write_u16le_at(&plain, 0x0264, 21);
        write_u16le_at(&plain, 0x0266, 22);
        write_u16le_at(&plain, 0x0268, 23);
        write_u16le_at(&plain, 0x032C, 240);
        write_u16le_at(&plain, 0x032E, 180);
        write_u16le_at(&plain, 0x0330, 360);
        write_u16le_at(&plain, 0x0344, 0x0344);
        plain[0x18BD] = std::byte { 1 };
        plain[0x18BE] = std::byte { 2 };
        write_u16le_at(&plain, 0x18BF, 0x18BF);
        write_u16le_at(&plain, 0x18C2, 0x18C2);
        plain[0x1958] = std::byte { 2 };
        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(400 + int(i));
            write_u16le_at(&plain, 0x189CU + i * 2U, static_cast<uint16_t>(v));
        }
        for (size_t i = 0; i < 0x798; ++i) {
            plain[0x0388 + i] = std::byte { static_cast<uint8_t>(0x60 + i) };
        }
        return make_sony_makernote_ciphered_blob(0x2010, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag2010h_ciphered()
    {
        std::vector<std::byte> plain(0x1930, std::byte { 0 });
        write_u32le_at(&plain, 0x0004, 0x01010101U);
        plain[0x0050] = std::byte { 0x50 };
        plain[0x020C] = std::byte { 0x0D };
        plain[0x0210] = std::byte { 0x11 };
        write_u16le_at(&plain, 0x0222, 0x0222);
        write_u16le_at(&plain, 0x0230, static_cast<uint16_t>(-10));
        write_u16le_at(&plain, 0x0264, 31);
        write_u16le_at(&plain, 0x0266, 32);
        write_u16le_at(&plain, 0x0268, 33);
        write_u16le_at(&plain, 0x032C, 500);
        write_u16le_at(&plain, 0x032E, 350);
        write_u16le_at(&plain, 0x0330, 700);
        write_u16le_at(&plain, 0x0346, 0x0346);
        plain[0x18ED] = std::byte { 1 };
        plain[0x18EE] = std::byte { 2 };
        write_u16le_at(&plain, 0x18EF, 0x18EF);
        write_u16le_at(&plain, 0x18F2, 0x18F2);
        plain[0x192C] = std::byte { 3 };
        for (size_t i = 0; i < 16; ++i) {
            const int16_t v = static_cast<int16_t>(500 + int(i));
            write_u16le_at(&plain, 0x18CCU + i * 2U, static_cast<uint16_t>(v));
        }
        for (size_t i = 0; i < 0x798; ++i) {
            plain[0x0398 + i] = std::byte { static_cast<uint8_t>(0x80 + i) };
        }
        return make_sony_makernote_ciphered_blob(0x2010, plain);
    }

    static std::vector<std::byte> make_sony_makernote_tag3000_shotinfo()
    {
        // Minimal Sony MakerNote containing ShotInfo (tag 0x3000). Within the
        // ShotInfo block, values are stored at offsets equal to the tag id.
        std::vector<std::byte> blob(0x44, std::byte { 0 });
        blob[0] = std::byte { 'I' };
        blob[1] = std::byte { 'I' };

        write_u16le_at(&blob, 0x0002, 94);

        const std::string_view dt = "2017:02:08 07:07:08";
        for (size_t i = 0; i < dt.size() && (0x0006 + i) < blob.size(); ++i) {
            blob[0x0006 + i] = std::byte { static_cast<uint8_t>(dt[i]) };
        }

        write_u16le_at(&blob, 0x001A, 5304);
        write_u16le_at(&blob, 0x001C, 7952);
        write_u16le_at(&blob, 0x0030, 2);
        write_u16le_at(&blob, 0x0032, 37);

        const std::string_view ver = "DC7303320222000";
        for (size_t i = 0; i < ver.size() && (0x0034 + i) < blob.size(); ++i) {
            blob[0x0034 + i] = std::byte { static_cast<uint8_t>(ver[i]) };
        }

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);  // entry count
        append_u16le(&mn, 0x3000);
        append_u16le(&mn, 7);  // UNDEFINED
        append_u32le(&mn, static_cast<uint32_t>(blob.size()));
        append_u32le(&mn, 0);  // value offset placeholder (patched by caller)
        append_u32le(&mn, 0);  // next IFD

        mn.insert(mn.end(), blob.begin(), blob.end());
        return mn;
    }

    static std::vector<std::byte>
    make_sony_makernote_tag3000_shotinfo_faceinfo2()
    {
        std::vector<std::byte> blob(0x0120, std::byte { 0 });
        blob[0] = std::byte { 'I' };
        blob[1] = std::byte { 'I' };

        write_u16le_at(&blob, 0x0002, 0x005E);

        const std::string_view dt = "2018:03:04 05:06:07";
        for (size_t i = 0; i < dt.size() && (0x0006 + i) < blob.size(); ++i) {
            blob[0x0006 + i] = std::byte { static_cast<uint8_t>(dt[i]) };
        }

        write_u16le_at(&blob, 0x001A, 4000);
        write_u16le_at(&blob, 0x001C, 6000);
        write_u16le_at(&blob, 0x0030, 5);
        write_u16le_at(&blob, 0x0032, 0x0025);

        const std::string_view ver = "DC7303320222000";
        for (size_t i = 0; i < ver.size() && (0x0034 + i) < blob.size(); ++i) {
            blob[0x0034 + i] = std::byte { static_cast<uint8_t>(ver[i]) };
        }

        for (size_t face = 0; face < 5; ++face) {
            const uint32_t base = 0x005EU + static_cast<uint32_t>(face) * 0x25U;
            write_u16le_at(&blob, base + 0, static_cast<uint16_t>(10 + face));
            write_u16le_at(&blob, base + 2, static_cast<uint16_t>(20 + face));
            write_u16le_at(&blob, base + 4, static_cast<uint16_t>(30 + face));
            write_u16le_at(&blob, base + 6, static_cast<uint16_t>(40 + face));
        }

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x3000);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(blob.size()));
        append_u32le(&mn, 0);
        append_u32le(&mn, 0);

        mn.insert(mn.end(), blob.begin(), blob.end());
        return mn;
    }

}  // namespace

TEST(MakerNoteDecode, DecodesCanonStyleMakerNoteIfdAtOffset0)
{
    const std::vector<std::byte> mn   = make_canon_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 0x12345678U);
}

TEST(MakerNoteDecode, NamesUnknownCanonMainTagWithStablePlaceholder)
{
    const std::vector<std::byte> mn   = make_canon_makernote_unknown_main_tag();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon0", 0x001F));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("Canon_0x001f"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Canon_0x001f"));
}

TEST(MakerNoteDecode, DecodesSonyMakerNoteByMakeString)
{
    std::vector<std::byte> mn   = make_sony_makernote();
    const std::string_view make = "Sony";
    const uint32_t maker_note_off
        = 57U + static_cast<uint32_t>(make.size());  // see builder layout
    const uint32_t value_off_abs = maker_note_off + 34U;
    write_u32le_at(&mn, 26U, value_off_abs);

    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x0102));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 7U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0xB020));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "Standard");
    }
}

TEST(MakerNoteDecode, RandomAccessDecodesSonyOuterTiffRelativeValues)
{
    std::vector<std::byte> mn     = make_sony_makernote();
    const std::string_view make   = "Sony";
    const uint32_t maker_note_off = 57U + static_cast<uint32_t>(make.size());
    write_u32le_at(&mn, 26U, maker_note_off + 34U);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    const ExifRandomAccessDecodeResult result
        = decode_makernote_callback(tiff, store);
    EXPECT_TRUE(result.complete());
    EXPECT_EQ(result.decode.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony0", 0xB020));
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(ids[0]).value),
              "Standard");
}

TEST(MakerNoteDecode, MarksSonyMainCompatNamesForLegacyRxModel)
{
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;

    const std::vector<std::byte> mn = make_sony_makernote_main_compat_tags();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Sony", "DSC-RX10", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const struct {
        uint16_t tag;
        std::string_view compat;
    } kCases[] = {
        { 0x201BU, "Sony_0x201b" }, { 0x201CU, "Sony_0x201c" },
        { 0x201DU, "Sony_0x201d" }, { 0x2021U, "Sony_0x2021" },
        { 0xB042U, "Sony_0xb042" }, { 0xB043U, "Sony_0xb043" },
    };

    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", kCases[i].tag));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  kCases[i].compat);
    }
}

TEST(MakerNoteDecode, KeepsSonyMain201bAnd2021SemanticForModelNamePlaceholder)
{
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;

    const std::vector<std::byte> mn = make_sony_makernote_main_compat_tags();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Sony", "MODEL-NAME", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x201BU));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("FocusMode"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x201CU));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Sony_0x201c"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x201DU));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Sony_0x201d"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x201EU));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Sony_0x201e"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x2021U));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("AFTracking"));
    }
}

TEST(MakerNoteDecode,
     KeepsSonyMainFocusAreaSemanticForSltModelBut201dPlaceholder)
{
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;

    const std::vector<std::byte> mn = make_sony_makernote_main_compat_tags();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Sony", "SLT-A58", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x201BU));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("FocusMode"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x201CU));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("AFAreaModeSetting"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x201DU));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Sony_0x201d"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x2021U));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("AFTracking"));
    }
}

TEST(MakerNoteDecode, MarksSonyMain205cCompatPlaceholderForIlce7m5Model)
{
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;

    const std::vector<std::byte> mn = make_sony_makernote_main_compat_tags();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Sony", "ILCE-7M5", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x201CU));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("AFAreaModeSetting"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0x205CU));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Sony_0x205c"));
    }
}

TEST(MakerNoteDecode, DecodesSonyCipheredTag9050IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9050b_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050b_0", 0x0026));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Array);
        ASSERT_EQ(e.value.elem_type, MetaElementType::U16);
        ASSERT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050b_0", 0x003A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x44332211U);
    }
}

TEST(MakerNoteDecode, DecodesSonyCipheredTag2010IntoDerivedIfd)
{
    const std::string_view make = "Sony";
    std::vector<std::byte> mn   = make_sony_makernote_tag2010i_ciphered();
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010i_0", 0x0217));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.data.u64, 0x1234U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010i_0", 0x0252));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010i_0", 0x17D0));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 32U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9050cWhenModelMatchesNewerBodies)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9050b_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-7M4";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050c_0", 0x0026));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050c_0", 0x003A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x44332211U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9050cInternalSerialAt008aForIlce1Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9050b_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-1";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9050c_0", 0x008A));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(e.value.count, 6U);
}

TEST(MakerNoteDecode, DecodesSonyTag9050bForIlce6500Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9050b_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-6500";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050b_0", 0x0026));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050b_0", 0x0088));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 6U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9050dForIlce6700Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9050d_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-6700";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050d_0", 0x001C));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x5678U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050d_0", 0x0038));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 6U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag3000ShotInfoIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag3000_shotinfo();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 94U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x0006));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "2017:02:08 07:07:08");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x001A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 5304U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x001C));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 7952U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x0030));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 2U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x0032));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 37U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x0034));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "DC7303320222000");
    }
}

TEST(MakerNoteDecode, DecodesSonyTag3000FaceInfo2IntoDerivedIfd)
{
    std::vector<std::byte> mn = make_sony_makernote_tag3000_shotinfo_faceinfo2();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_faceinfo2_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 4U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_faceinfo2_0", 0x0025));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 4U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_faceinfo2_0", 0x006F));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 4U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_faceinfo2_0", 0x0094));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 4U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9400IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9400_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400c_0", 0x0012));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x11223344U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400c_0", 0x0053));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 2023U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400c_0", 0x0029));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 5U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9400bForNex3nModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9400b_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "NEX-3N";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400b_0", 0x003F));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x0123U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400b_0", 0x0024));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 3U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9400cShutterTypeForRx100m7Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9400_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "DSC-RX100M7";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9400c_0", 0x013F));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(e.value.data.u64, 7U);
}

TEST(MakerNoteDecode, DecodesSonyTag9400cForIlce7rm6Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9400_ciphered_with_v0(
        0x43);

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-7RM6";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9400c_0", 0x0012));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 0x11223344U);
}

TEST(MakerNoteDecode, DecodesSonyTag9401IsoInfoIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9401_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_isoinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 0U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_isoinfo_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 16U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_isoinfo_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 32U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9405bIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9405b_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9405b_0", 0x0010));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::URational);
        EXPECT_EQ(e.value.data.ur.numer, 1U);
        EXPECT_EQ(e.value.data.ur.denom, 250U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9405b_0", 0x0024));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x12345678U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9405b_0", 0x0064));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 16U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9405bLensZoomAndCorrectionsForRx100ivModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9405b_rx100m4_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "DSC-RX100M4";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_sony_tag9405b_0", 0x0342)).empty());
    EXPECT_TRUE(store.find_all(exif_key("mk_sony_tag9405b_0", 0x034E)).empty());
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9405b_0", 0x035A));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
}

TEST(MakerNoteDecode, DecodesSonyTag9416IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9416_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9416_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 0x10U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9416_0", 0x000C));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::URational);
        EXPECT_EQ(e.value.data.ur.numer, 5U);
        EXPECT_EQ(e.value.data.ur.denom, 8U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9416_0", 0x001D));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x89ABCDEFU);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9416_0", 0x004F));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 16U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9416LegacyCorrectionOffsetsForIlce1Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9416_ilce1_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-1";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_sony_tag9416_0", 0x089D)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9416_0", 0x088F));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 16U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9416_0", 0x08B5));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9416_0", 0x0914));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 32U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9416CorrectionOffsetsForIlce7rm6Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9416_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-7RM6";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_sony_tag9416_0", 0x089D)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9416_0", 0x0708));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 32U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9416_0", 0x074F));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9416_0", 0x0843));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 32U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag202aIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag202a_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag202a_0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
}

TEST(MakerNoteDecode, DecodesSonyTag9402IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9402_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9402_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 2U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9402_0", 0x002D));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 0x2DU);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9403IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9403_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9403_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 4U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9403_0", 0x0019));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x3456U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9404aIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9404a_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9404a_0", 0x000B));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 11U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9404a_0", 0x0019));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x2345U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9404cIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9404c_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9404c_0", 0x000D));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(e.value.data.u64, 13U);
}

TEST(MakerNoteDecode, DecodesSonyTag9404bForLunarModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9404b_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "Lunar";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9404b_0", 0x000C));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 12U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9404b_0", 0x001E));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x2345U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9404bForIlce7Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9404b_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-7";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9404b_0", 0x000C));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(e.value.data.u64, 12U);
}

TEST(MakerNoteDecode, DecodesSonyTag9406IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9406_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9406_0", 0x0008));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(e.value.data.u64, 8U);
}

TEST(MakerNoteDecode, MapsSonyTag94060005ToBatteryLevelForNewerModels)
{
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;

    std::vector<std::byte> mn = make_sony_makernote_tag9406_ciphered();
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Sony", "ILCE-7M5", mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9406_0", 0x0005));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("BatteryTemperature"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("BatteryLevel"));
}

TEST(MakerNoteDecode, KeepsSonyTag94060005AsBatteryTemperatureForOlderModels)
{
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;

    std::vector<std::byte> mn = make_sony_makernote_tag9406_ciphered();
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Sony", "ILCE-1", mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9406_0", 0x0005));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("BatteryTemperature"));
}

TEST(MakerNoteDecode, DecodesSonyTag940cIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag940c_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag940c_0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 8U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag940c_0", 0x0014));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x0414U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9050aForLunarModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9050a_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "Lunar";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050a_0", 0x0020));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050a_0", 0x0032));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x44332211U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050a_0", 0x01AA));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x89ABCDEFU);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9050aForIlce7Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9050a_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-7";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9050a_0", 0x0020));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Array);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.count, 3U);
    {
        const std::span<const EntryId> shutter_ids = store.find_all(
            exif_key("mk_sony_tag9050a_0", 0x004C));
        ASSERT_EQ(shutter_ids.size(), 1U);
        const Entry& shutter = store.entry(shutter_ids[0]);
        EXPECT_EQ(shutter.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(shutter.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(shutter.value.data.u64, 0x00ABCDEFU);
    }
    {
        const std::span<const EntryId> dt_ids = store.find_all(
            exif_key("mk_sony_tag9050a_0", 0x0051));
        ASSERT_EQ(dt_ids.size(), 1U);
        const Entry& dt = store.entry(dt_ids[0]);
        EXPECT_EQ(dt.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(dt.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(dt.value.count, 6U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9050aShutterCount3At01a0ForIlce5100Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9050a_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-5100";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9050a_0", 0x01A0));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 0x00112233U);
}

TEST(MakerNoteDecode, DecodesSonyTag9405aForSltFamilyModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9405a_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "SLT-A99";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9405a_0", 0x0605));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x0605U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9405a_0", 0x064A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 16U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9405a_0", 0x066A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 32U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9405aForNex5tModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9405a_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "NEX-5T";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag9405a_0", 0x0601));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(e.value.data.u64, 2U);
}

TEST(MakerNoteDecode, DecodesSonyTag9405bForIlcaFamilyModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9405b_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCA-77M2";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_sony_tag9405a_0", 0x0605)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9405b_0", 0x005E));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 9U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9405b_0", 0x0060));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x0ABCU);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag940eMeteringImageForNonSltModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag940e_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-7M3";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag940e_0", 0x1A06));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 2U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag940e_0", 0x1A08));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 12U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag2010bForLunarModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag2010b_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "Lunar";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010b_0", 0x114C));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(static_cast<int64_t>(e.value.data.i64), -7);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010b_0", 0x1180));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_meterinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 0x006CU);
    }
    EXPECT_TRUE(
        store.find_all(exif_key("mk_sony_meterinfo9_0", 0x0000)).empty());
}

TEST(MakerNoteDecode, DecodesSonyTag2010eForStellarModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag2010e_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "Stellar";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010e_0", 0x1180));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(static_cast<int64_t>(e.value.data.i64), -9);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010e_0", 0x11B4));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010e_0", 0x1870));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 16U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_meterinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 0x006CU);
    }
    EXPECT_TRUE(
        store.find_all(exif_key("mk_sony_meterinfo9_0", 0x0000)).empty());
}

TEST(MakerNoteDecode, DecodesSonyTag2010eSonyIsoForRx1rModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag2010e_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "DSC-RX1R";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sony_tag2010e_0", 0x1258));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 0x1258U);
}

TEST(MakerNoteDecode, DecodesSonyTag2010eFocalFieldsForNex3nModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag2010e_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "NEX-3N";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010e_0", 0x1278));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x1278U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010e_0", 0x1280));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x1280U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag2010cForSltA57Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag2010c_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "SLT-A57";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010c_0", 0x0200));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x0080U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_meterinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 0x006CU);
    }
    EXPECT_TRUE(
        store.find_all(exif_key("mk_sony_meterinfo9_0", 0x0000)).empty());
}

TEST(MakerNoteDecode, DecodesSonyTag2010fForRx100M2Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag2010f_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "DSC-RX100M2";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010f_0", 0x1014));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 0x14U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_meterinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 0x006CU);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag2010gForIlce7Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag2010g_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-7";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010g_0", 0x020C));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 0x0CU);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_meterinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 0x006CU);
    }
    EXPECT_TRUE(
        store.find_all(exif_key("mk_sony_meterinfo9_0", 0x0000)).empty());
}

TEST(MakerNoteDecode, DecodesSonyTag2010hForIlce6500Model)
{
    std::vector<std::byte> mn = make_sony_makernote_tag2010h_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-6500";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010h_0", 0x0346));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x0346U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_meterinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 0x006CU);
    }
    EXPECT_TRUE(
        store.find_all(exif_key("mk_sony_meterinfo9_0", 0x0000)).empty());
}

TEST(MakerNoteDecode, DecodesSonyTag9400aForSltFamilyModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9400a_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "SLT-A99";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400a_0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x01020304U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400a_0", 0x001A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x11223344U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400a_0", 0x0044));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 3000U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400a_0", 0x0052));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 23U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9400cForIlcaFamilyModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9400_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCA-99M2";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_sony_tag9400a_0", 0x0008)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400c_0", 0x0012));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x11223344U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9400c_0", 0x0053));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 2023U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag940eAfInfoForSltFamilyModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag940e_afinfo_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "SLT-A99";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_afinfo_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 2U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_afinfo_0", 0x016E));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x12345678U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_afstatus19_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(static_cast<int64_t>(e.value.data.i64), 100);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_afstatus19_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(static_cast<int64_t>(e.value.data.i64), 102);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag940eAfInfo79ForIlcaModel)
{
    std::vector<std::byte> mn = make_sony_makernote_tag940e_afinfo79_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCA-77M2";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_afinfo_0", 0x0010));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 10U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_afinfo_0", 0x0037));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 55U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_afinfo_0", 0x0050));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I8);
        EXPECT_EQ(static_cast<int64_t>(e.value.data.i64), -2);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_afstatus79_0", 0x000E));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(static_cast<int64_t>(e.value.data.i64), 107);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_afstatus79_0", 0x00BC));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(static_cast<int64_t>(e.value.data.i64), 194);
    }
}

TEST(MakerNoteDecode, DecodesKonicaMinoltaMakerNoteByMakeString)
{
    const std::vector<std::byte> mn = make_minolta_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote("KONICA MINOLTA", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_minolta0", 0x0100));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 13U);
}

TEST(MakerNoteDecode,
     MarksMinoltaImageSizeAsCompatPlaceholderForDiMAGEA200Model)
{
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;

    const std::vector<std::byte> mn = make_minolta_makernote_image_size();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("KONICA MINOLTA",
                                                  "DiMAGE A200", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_minolta0", 0x0103));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::MinoltaMainCompat);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("MinoltaImageSize"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Minolta_0x0103"));
}

TEST(MakerNoteDecode, MarksMinolta0103AsQualityForDiMAGE7HiModel)
{
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;

    const std::vector<std::byte> mn = make_minolta_makernote_image_size();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Minolta Co., Ltd.",
                                                  "DiMAGE 7Hi", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_minolta0", 0x0103));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::MinoltaMainCompat);
    EXPECT_EQ(e.origin.name_context_variant, 2U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("MinoltaImageSize"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("MinoltaQuality"));
}

TEST(MakerNoteDecode,
     DecodesKonicaMinoltaMakerNoteBinarySubdirectoriesByMakeString)
{
    const std::vector<std::byte> mn
        = make_minolta_makernote_with_binary_subdirs();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote("KONICA MINOLTA", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_minolta_camerasettings_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x12345678U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_minolta_camerasettings7d_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x4444U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_minolta_camerasettings5d_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x0304U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}


TEST(MakerNoteDecode, DecodesCanonBinaryDataCameraSettingsIntoDerivedIfd)
{
    std::vector<std::byte> mn         = make_canon_camera_settings_makernote();
    const std::string_view make       = "Canon";
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_camerasettings_0", 0x0002));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 22U);
    EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
}

TEST(MakerNoteDecode, DecodesCanonBinaryDataCameraSettingsWithAdjustedBase)
{
    std::vector<std::byte> mn
        = make_canon_camera_settings_makernote_adjusted_base();
    const std::string_view make       = "Canon";
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_camerasettings_0", 0x0002));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 22U);
    EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
}

TEST(MakerNoteDecode, RandomAccessDecodesCanonAdjustedBaseAndDerivedTable)
{
    const std::vector<std::byte> mn
        = make_canon_camera_settings_makernote_adjusted_base();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    const ExifRandomAccessDecodeResult result
        = decode_makernote_callback(tiff, store);
    EXPECT_TRUE(result.complete());
    EXPECT_EQ(result.decode.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_camerasettings_0", 0x0002));
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(store.entry(ids[0]).value.data.u64, 22U);
    EXPECT_TRUE(any(store.entry(ids[0]).flags, EntryFlags::Derived));
}

TEST(MakerNoteDecode, RandomAccessReportsCanonExternalDerivedTableResidual)
{
    const std::vector<std::byte> mn = make_canon_camera_settings_makernote();
    std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon", mn);
    ASSERT_TRUE(patch_makernote_count_in_tiff(&tiff, 18U));

    MetaStore store;
    const ExifRandomAccessDecodeResult result
        = decode_makernote_callback(tiff, store);
    EXPECT_TRUE(result.input.ok());
    EXPECT_EQ(result.nested_payloads_skipped, 1U);
    EXPECT_FALSE(result.complete());
    store.finalize();
    const std::span<const EntryId> main_ids = store.find_all(
        exif_key("mk_canon0", 0x0001));
    ASSERT_EQ(main_ids.size(), 1U);
    EXPECT_EQ(store.entry(main_ids[0]).value.count, 3U);
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerasettings_0", 0x0002)).empty());
}


TEST(MakerNoteDecode, DecodesCanonCustomFunctions2IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_canon_custom_functions2_makernote();
    const std::string_view make = "Canon";
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x0101));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 0U);
    EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
}

TEST(MakerNoteDecode,
     DecodesCanonCustomFunctions2IntoDerivedIfdWithBigEndianParent)
{
    const std::vector<std::byte> mn = make_canon_custom_functions2_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_big_endian_parent("Canon", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x0101));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 0U);
    EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
}

TEST(MakerNoteDecode, MarksCanonCustomFunctions2Tag0103IsoExpansionForScalar)
{
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_tag0103_makernote(1U);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x0103));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ISOSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ISOExpansion"));
}

TEST(MakerNoteDecode, MarksCanonCustomFunctions2Tag0103IsoSpeedRangeForArray)
{
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_tag0103_makernote(3U);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x0103));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ISOSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ISOSpeedRange"));
}

TEST(MakerNoteDecode, MarksCanonCustomFunctions2Tag010cPlaceholderForArray)
{
    static constexpr uint32_t kVals[] = {
        9216U, 0xfffff100U, 9216U, 0xfffff100U, 10752U, 0xfffff100U,
    };
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_single_entry_makernote(
            0x010CU, std::span<const uint32_t>(kVals));
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS R10", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x010C));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::CanonCustomFunctions2010C);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ShutterSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("CanonCustom_Functions2_0x010c"));
}

TEST(MakerNoteDecode,
     MarksCanonCustomFunctions2Tag010cPlaceholderForR7CompatCohort)
{
    static constexpr uint32_t kVals[] = {
        9216U,
        0xfffff100U,
        9216U,
        0xfffff100U,
    };
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_single_entry_makernote(
            0x010CU, std::span<const uint32_t>(kVals));
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS R7", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x010C));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::CanonCustomFunctions2010C);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ShutterSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("CanonCustom_Functions2_0x010c"));
}

TEST(MakerNoteDecode,
     MarksCanonCustomFunctions2Tag010cPlaceholderForR3CompatCohort)
{
    static constexpr uint32_t kVals[] = {
        9984U, 0xfffff100U, 12288U, 0xfffff100U, 12288U, 0xfffff100U,
    };
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_single_entry_makernote(
            0x010CU, std::span<const uint32_t>(kVals));
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS R3", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x010C));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::CanonCustomFunctions2010C);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ShutterSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("CanonCustom_Functions2_0x010c"));
}

TEST(MakerNoteDecode,
     LeavesCanonCustomFunctions2Tag010cCompatCanonicalForRSeriesCohort)
{
    static constexpr uint32_t kVals[] = {
        9216U,
        0xfffff100U,
        9216U,
        0xfffff100U,
    };
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_single_entry_makernote(
            0x010CU, std::span<const uint32_t>(kVals));
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS R", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x010C));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ShutterSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ShutterSpeedRange"));
}

TEST(MakerNoteDecode,
     LeavesCanonCustomFunctions2Tag010cCanonicalForThreeElementRange)
{
    static constexpr uint32_t kVals[] = { 0U, 160U, 16U };
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_single_entry_makernote(
            0x010CU, std::span<const uint32_t>(kVals));
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 5DS", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x010C));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ShutterSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ShutterSpeedRange"));
}

TEST(MakerNoteDecode,
     MarksCanonCustomFunctions2Tag0510SuperimposedDisplayForDslrModel)
{
    static constexpr uint32_t kVal[] = { 0U };
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_single_entry_makernote(
            0x0510U, std::span<const uint32_t>(kVal));
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 70D", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x0510));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::CanonCustomFunctions20510);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("VFDisplayIllumination"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("SuperimposedDisplay"));
}

TEST(MakerNoteDecode,
     MarksCanonCustomFunctions2Tag0701ShutterButtonForCompatModel)
{
    static constexpr uint32_t kVal[] = { 0U };
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_single_entry_makernote(
            0x0701U, std::span<const uint32_t>(kVal));
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 40D", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x0701));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::CanonCustomFunctions20701);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("Shutter-AELock"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ShutterButtonAFOnButton"));
}

TEST(MakerNoteDecode,
     MarksCanonCustomFunctions2Tag0701ShutterButtonForRebelAliasModel)
{
    static constexpr uint32_t kVal[] = { 0U };
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_single_entry_makernote(
            0x0701U, std::span<const uint32_t>(kVal));
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon",
                                                  "Canon EOS REBEL T4i", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x0701));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::CanonCustomFunctions20701);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ShutterButtonAFOnButton"));
}

TEST(MakerNoteDecode,
     MarksCanonCustomFunctions2Tag0701AfAndMeteringFor60dCohort)
{
    static constexpr uint32_t kVal[] = { 0U };
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_single_entry_makernote(
            0x0701U, std::span<const uint32_t>(kVal));
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 60D", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x0701));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::CanonCustomFunctions20701);
    EXPECT_EQ(e.origin.name_context_variant, 2U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("Shutter-AELock"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("AFAndMeteringButtons"));
}

TEST(MakerNoteDecode, LeavesCanonCustomFunctions2Tag0701CanonicalFor550dCohort)
{
    static constexpr uint32_t kVal[] = { 0U };
    const std::vector<std::byte> mn
        = make_canon_custom_functions2_single_entry_makernote(
            0x0701U, std::span<const uint32_t>(kVal));
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 550D",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x0701));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Shutter-AELock"));
}

TEST(MakerNoteDecode, MarksCanonMainTag0038PlaceholderForByteBlob)
{
    const std::vector<std::byte> mn = make_canon_makernote_main_tag0038_bytes();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon0", 0x0038));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("BatteryType"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Canon_0x0038"));
}

TEST(MakerNoteDecode, KeepsCanonMainTag0038BatteryTypeForLongByteBlob)
{
    const std::vector<std::byte> mn
        = make_canon_makernote_main_tag0038_long_bytes();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon0", 0x0038));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("BatteryType"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("BatteryType"));
}

TEST(MakerNoteDecode, SelectsCanonCustom350dTableFromCountFallback)
{
    const std::vector<std::byte> mn
        = make_canon_custom_functions_0x000f_makernote(9U);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canoncustom_functionsunknown_0", 0x0000))
            .empty());
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions350d_0", 0x0000));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("SetButtonCrossKeysFunc"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("SetButtonCrossKeysFunc"));
}

TEST(MakerNoteDecode, SelectsCanonCustom400dTableForXtiModel)
{
    const std::vector<std::byte> mn
        = make_canon_custom_functions_0x000f_makernote(11U);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote_and_model(
        "Canon", "Canon EOS DIGITAL REBEL XTi", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canoncustom_functions350d_0", 0x0009))
            .empty());
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions400d_0", 0x0009));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("MagnifiedView"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("MagnifiedView"));
}

TEST(MakerNoteDecode, SelectsCanonCustomD30TableFromCountFallbackWithHeaderWord)
{
    const std::vector<std::byte> mn
        = make_canon_custom_functions_0x000f_with_size_header_makernote(16U);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canoncustom_functionsunknown_0", 0x0000))
            .empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canoncustom_functionsd30_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view("CanonCustom_FunctionsD30_0x0000"));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("CanonCustom_FunctionsD30_0x0000"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canoncustom_functionsd30_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view("LongExposureNoiseReduction"));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("LongExposureNoiseReduction"));
    }
}

TEST(MakerNoteDecode, NamesUnknownCanonCustomFunctions5dTagWithPlaceholder)
{
    const std::vector<std::byte> mn
        = make_canon_custom_functions_0x000f_makernote(22U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 5D", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions5d_0", 0x0015));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("CanonCustom_Functions5D_0x0015"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("CanonCustom_Functions5D_0x0015"));
}

TEST(MakerNoteDecode,
     DecodesCanonCustomFunctions1DAndPersonalFunctionsIntoDerivedIfds)
{
    std::vector<std::byte> mn   = make_canon_custom_functions1d_makernote();
    const std::string_view make = "Canon";
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canoncustom_functions1d_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canoncustom_functions1d_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canoncustom_personalfuncs_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canoncustom_personalfuncvalues_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 63U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}


TEST(MakerNoteDecode, DecodesCanonMakernoteWithTruncatedDeclaredByteCount)
{
    std::vector<std::byte> mn   = make_canon_truncated_count_makernote();
    const std::string_view make = "Canon";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    // Declare an unrealistically small MakerNote count to force the decoder
    // to rely on bounds checks against the full EXIF/TIFF buffer.
    ASSERT_TRUE(patch_makernote_count_in_tiff(&tiff, 10));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon0", 0x0006));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Text);
    EXPECT_EQ(e.value.text_encoding, TextEncoding::Ascii);
    EXPECT_FALSE(any(e.flags, EntryFlags::Derived));
}


TEST(MakerNoteDecode, DecodesCanonCameraInfoPictureStyleIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_canon_camera_info_psinfo_makernote();
    const std::string_view make = "Canon";
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 42U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_psinfo_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I32);
        EXPECT_EQ(e.value.data.i64, 3);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_psinfo_0", 0x00d8));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 129U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, SelectsCanonCameraInfo450dSubIfdFromModel)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_ascii(0x0107, "1.2.3", 6U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 450D",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo_0", 0x0107)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo450d_0", 0x0107));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size()),
                  "1.2.3");
    }
}

TEST(MakerNoteDecode, SelectsCanonCameraInfo1100dSubIfdFor1100dCohort)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_ascii(0x019b, "1.0.1", 6U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 1100D",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo_0", 0x019b)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo1100d_0", 0x019b));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size()),
                  "1.0.1");
    }
}

TEST(MakerNoteDecode, SelectsCanonCameraInfo600dSubIfdForKissX70Alias)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_ascii(0x019b, "1.0.9", 6U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS Kiss X70",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo650d_0", 0x019b)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo600d_0", 0x019b));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size()),
                  "1.0.9");
    }
}

TEST(MakerNoteDecode, SelectsCanonCameraInfo600dSubIfdForRebelT3iAlias)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_ascii(0x019b, "1.1.4", 6U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon",
                                                  "Canon EOS REBEL T3i", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo1100d_0", 0x019b)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo600d_0", 0x019b));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size()),
                  "1.1.4");
    }
}

TEST(MakerNoteDecode, SelectsCanonCameraInfo7dSubIfdFromModel)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_ascii(0x01ac, "1.2.0", 6U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 7D", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo_0", 0x01ac)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo7d_0", 0x01ac));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size()),
                  "1.2.0");
    }
}

TEST(MakerNoteDecode, SelectsCanonCameraInfo7dSubIfdFromCandidateTags)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_ifd_ascii(0x01ac, "1.0.7", 6U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo_0", 0x01ac)).empty());
    EXPECT_FALSE(
        store.find_all(exif_key("mk_canon_camerainfo7d_0", 0x01ac)).empty());
}

TEST(MakerNoteDecode, SelectsCanonCameraInfo1dSubIfdFor1dsModel)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_u16(0x0048, 5300U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS-1DS", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo_0", 0x0048)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo1d_0", 0x0048));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 5300U);
    }
}

TEST(MakerNoteDecode, SelectsCanonCameraInfoUnknownSubIfdFor5dsModel)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_ascii(0x016b, "1234567890ABCDEF",
                                                 16U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 5DS", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo5d_0", 0x016b)).empty());
    EXPECT_FALSE(
        store.find_all(exif_key("mk_canon_camerainfounknown_0", 0x016b))
            .empty());
}

TEST(MakerNoteDecode, SelectsCanonCameraInfoUnknownSubIfdForR1Model)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_ascii(0x05c1, "1.0.0", 6U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS R1", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo_0", 0x05c1)).empty());
    EXPECT_FALSE(
        store.find_all(exif_key("mk_canon_camerainfounknown_0", 0x05c1))
            .empty());
}

TEST(MakerNoteDecode, SelectsCanonCameraInfoUnknown32SubIfdForPowerShotS1)
{
    const std::vector<std::byte> mn
        = make_canon_camera_info_u32_table_makernote(0x0048U, 0x0047U, 19U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon",
                                                  "Canon PowerShot S1 IS", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfounknown32_0", 0x0047));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 19U);
    }
}

TEST(MakerNoteDecode, SelectsCanonCameraInfo6dSubIfdFromCandidateTags)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_ifd_ascii(0x0256, "1.1.2", 6U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo_0", 0x0256)).empty());
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_camerainfo6d_0", 0x0256));
    ASSERT_FALSE(ids.empty());
    bool found_text = false;
    for (size_t i = 0; i < ids.size(); ++i) {
        const Entry& e = store.entry(ids[i]);
        if (e.value.kind != MetaValueKind::Text) {
            continue;
        }
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        if (std::string_view(reinterpret_cast<const char*>(raw.data()),
                             raw.size())
            == "1.1.2") {
            found_text = true;
            break;
        }
    }
    EXPECT_TRUE(found_text);
}

TEST(MakerNoteDecode, SelectsCanonCameraInfo6dSubIfdFromModel)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_ascii(0x0256, "1.2.6", 6U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 6D", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_canon_camerainfo_0", 0x0256)).empty());
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_camerainfo6d_0", 0x0256));
    ASSERT_FALSE(ids.empty());
    bool found_text = false;
    for (size_t i = 0; i < ids.size(); ++i) {
        const Entry& e = store.entry(ids[i]);
        if (e.value.kind != MetaValueKind::Text) {
            continue;
        }
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        if (std::string_view(reinterpret_cast<const char*>(raw.data()),
                             raw.size())
            == "1.2.6") {
            found_text = true;
            break;
        }
    }
    EXPECT_TRUE(found_text);
}

TEST(MakerNoteDecode, DecodesCanonCameraInfo1dMarkIiExtraFields)
{
    std::vector<std::byte> cam(static_cast<size_t>(0x0075) + 5U,
                               std::byte { 0 });
    cam[0x0066] = std::byte { 7 };
    std::memcpy(cam.data() + 0x0075, "0400", 4U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon",
                                                  "Canon EOS-1D Mark II", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo1dmkii_0", 0x0066));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 7U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo1dmkii_0", 0x0075));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size()),
                  "0400");
    }
}

TEST(MakerNoteDecode, DecodesCanonCameraInfo1dMarkIinExtraFields)
{
    std::vector<std::byte> cam(static_cast<size_t>(0x0079) + 5U,
                               std::byte { 0 });
    cam[0x0074] = std::byte { 3 };
    std::memcpy(cam.data() + 0x0079, "0800", 4U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon",
                                                  "Canon EOS-1D Mark II N", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo1dmkiin_0", 0x0074));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_camerainfo1dmkiin_0", 0x0079));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(raw.data()),
                                   raw.size()),
                  "0800");
    }
}

TEST(MakerNoteDecode, DecodesCanonCameraInfo5dTimestamp)
{
    const std::vector<std::byte> cam
        = make_canon_camera_info_blob_with_u32(0x011c, 0x01020304U);
    const std::vector<std::byte> mn = make_canon_camera_info_blob_makernote(
        cam);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 5D", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_camerainfo5d_0", 0x011c));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 0x01020304U);
}


TEST(MakerNoteDecode, DecodesCanonCameraInfoPictureStyle2IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_canon_camera_info_psinfo2_makernote();
    const std::string_view make = "Canon";
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_psinfo2_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I32);
        EXPECT_EQ(e.value.data.i64, 3);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_psinfo2_0", 0x0090));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I32);
        EXPECT_EQ(e.value.data.i64, 7);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_psinfo2_0", 0x00e4));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I32);
        EXPECT_EQ(e.value.data.i64, 2);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_psinfo2_0", 0x00f0));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 129U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, PrefersCanonCameraInfoPsinfoFor1000dCohort)
{
    std::vector<std::byte> mn = make_canon_camera_info_psinfo2_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 1000D",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_canon_psinfo2_0", 0x0090)).empty());
    ASSERT_EQ(store.find_all(exif_key("mk_canon_psinfo_0", 0x0090)).size(), 1U);
}

TEST(MakerNoteDecode, PrefersCanonCameraInfoPsinfoFor7dCohort)
{
    std::vector<std::byte> mn = make_canon_camera_info_psinfo2_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 7D", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_canon_psinfo2_0", 0x0090)).empty());
    ASSERT_EQ(store.find_all(exif_key("mk_canon_psinfo_0", 0x0090)).size(), 1U);
}

TEST(MakerNoteDecode, PrefersCanonCameraInfoPsinfo2For5dMarkIiiCohort)
{
    std::vector<std::byte> mn = make_canon_camera_info_psinfo2_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon",
                                                  "Canon EOS 5D Mark III", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_canon_psinfo_0", 0x0090)).empty());
    ASSERT_EQ(store.find_all(exif_key("mk_canon_psinfo2_0", 0x0090)).size(),
              1U);
}

TEST(MakerNoteDecode, PrefersCanonCameraInfoPsinfo2ForKissX7iCohort)
{
    std::vector<std::byte> mn = make_canon_camera_info_psinfo2_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS Kiss X7i",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_canon_psinfo_0", 0x0090)).empty());
    ASSERT_EQ(store.find_all(exif_key("mk_canon_psinfo2_0", 0x0090)).size(),
              1U);
}

TEST(MakerNoteDecode, PrefersCanonCameraInfoPsinfo2For1dXCohort)
{
    std::vector<std::byte> mn = make_canon_camera_info_psinfo2_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS-1D X",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_canon_psinfo_0", 0x0090)).empty());
    ASSERT_EQ(store.find_all(exif_key("mk_canon_psinfo2_0", 0x0090)).size(),
              1U);
}

TEST(MakerNoteDecode,
     FallsBackToCanonCameraInfoPictureStyleWhenPsinfo2TailIsInvalid)
{
    std::vector<std::byte> mn
        = make_canon_camera_info_psinfo_with_invalid_psinfo2_tail_makernote();
    const std::string_view make       = "Canon";
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_canon_psinfo2_0", 0x0090)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_psinfo_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I32);
        EXPECT_EQ(e.value.data.i64, 3);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_psinfo_0", 0x00d8));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 129U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}


TEST(MakerNoteDecode, DecodesCanonAfInfo2IntoDerivedIfd)
{
    std::vector<std::byte> mn         = make_canon_afinfo2_makernote();
    const std::string_view make       = "Canon";
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_afinfo2_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 9U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_afinfo2_0", 0x2602));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 9U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_afinfo2_0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 9U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_afinfo2_0", 0x000a));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 9U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_afinfo2_0", 0x260a));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 9U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, DecodesCanonAfInfo2FromScannedFallback)
{
    const std::vector<std::byte> mn
        = make_canon_afinfo2_makernote_with_bad_primary_offset();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_afinfo2_0", 0x000d));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 4U);
    EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("AFPointsSelected"));
}

TEST(MakerNoteDecode, MarksCanonMainTag0000AsFocalTypeForCamcorderCohort)
{
    const std::vector<std::byte> mn = make_canon_main_tag0000_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon DC410", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon0", 0x0000));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind, EntryNameContextKind::CanonMain0000);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("Canon_0x0000"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FocalType"));
}

TEST(MakerNoteDecode, MarksCanonMainTag0000AsAfInfoSizeForCinemaEosCohort)
{
    const std::vector<std::byte> mn = make_canon_main_tag0000_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS C300",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon0", 0x0000));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind, EntryNameContextKind::CanonMain0000);
    EXPECT_EQ(e.origin.name_context_variant, 2U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("Canon_0x0000"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("AFInfoSize"));
}

TEST(MakerNoteDecode, DecodesCanonColorData8AndColorCalibIntoDerivedIfds)
{
    const std::vector<std::byte> mn   = make_canon_colordata8_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_colorcalib_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 100U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_colordata8_0", 0x0043));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 6100U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, DecodesCanonColorData4ByCountAndEmbeddedColorCoefs)
{
    const std::vector<std::byte> mn
        = make_canon_colordata_counted_makernote(1338U, 7U);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_colorcoefs_0", 0x0022));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 6207U);
    EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
}

TEST(MakerNoteDecode, DecodesCanonColorData7ByCountAndEmbeddedColorCalib)
{
    const std::vector<std::byte> mn
        = make_canon_colordata_counted_makernote(1312U, 10U);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_colordata7_0", 0x003f));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 1710U);
    EXPECT_TRUE(any(e.flags, EntryFlags::Derived));

    const std::span<const EntryId> calib_ids = store.find_all(
        exif_key("mk_canon_colorcalib_0", 0x0038));
    ASSERT_EQ(calib_ids.size(), 1U);
    const Entry& calib = store.entry(calib_ids[0]);
    EXPECT_EQ(calib.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(calib.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(calib.value.data.u64, 410U);
    EXPECT_TRUE(any(calib.flags, EntryFlags::Derived));
}

TEST(MakerNoteDecode, MarksCanonColorData4Tag00daAsPsinfoFor7DCompat)
{
    std::vector<std::byte> mn = make_canon_colordata_counted_makernote(1338U,
                                                                       7U);
    write_u16le_at(&mn, 18U + 0x00DAU * 2U, 447U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS 7D", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_colordata4_0", 0x00DA));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::CanonColorData4PSInfo);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view());
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("UserDef2PictureStyle"));
}

TEST(MakerNoteDecode, MarksCanonColorData7Psinfo2TailForCompatModels)
{
    std::vector<std::byte> mn = make_canon_colordata_counted_makernote(1312U,
                                                                       10U);
    write_u16le_at(&mn, 18U + 0x00E4U * 2U, 2800U);
    write_u16le_at(&mn, 18U + 0x00E8U * 2U, 2400U);
    write_u16le_at(&mn, 18U + 0x00ECU * 2U, 2047U);
    write_u16le_at(&mn, 18U + 0x00F0U * 2U, 0U);
    write_u16le_at(&mn, 18U + 0x00F2U * 2U, 0U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Canon", "Canon EOS Kiss X7i",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const struct {
        uint16_t tag;
        std::string_view compat;
        uint8_t variant;
    } kCases[] = {
        { 0x00E4U, "ColorToneUserDef3", 1U },
        { 0x00E8U, "FilterEffectUserDef3", 2U },
        { 0x00ECU, "ToningEffectUserDef3", 3U },
        { 0x00F0U, "UserDef1PictureStyle", 4U },
        { 0x00F2U, "UserDef2PictureStyle", 5U },
    };
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_colordata7_0", kCases[i].tag));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::CanonColorData7PSInfo2);
        EXPECT_EQ(e.origin.name_context_variant, kCases[i].variant);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view());
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  kCases[i].compat);
    }
}

TEST(MakerNoteDecode, DecodesCanonColorData12For3778CountAndVersion65)
{
    const std::vector<std::byte> mn
        = make_canon_colordata_counted_makernote(3778U, 65U);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_colordata12_0", 0x0073));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 2765U);
    EXPECT_TRUE(any(e.flags, EntryFlags::Derived));

    const std::span<const EntryId> calib_ids = store.find_all(
        exif_key("mk_canon_colorcalib_0", 0x0038));
    ASSERT_EQ(calib_ids.size(), 1U);
    const Entry& calib = store.entry(calib_ids[0]);
    EXPECT_EQ(calib.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(calib.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(calib.value.data.u64, 710U);
    EXPECT_TRUE(any(calib.flags, EntryFlags::Derived));
}

TEST(MakerNoteDecode, DecodesCanonUndefinedColorData5VersionMinus3)
{
    const std::vector<std::byte> mn
        = make_canon_colordata_undefined_makernote(5120U, 0xfffdU);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_colordata5_0", 0x0569));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 16010U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_colorcoefs_0", 0x0037));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 6303U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_colorcalib2_0", 0x0037));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 703U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, DecodesCanonUndefinedColorData5VersionMinus4)
{
    const std::vector<std::byte> mn
        = make_canon_colordata_undefined_makernote(5120U, 0xfffcU);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_colordata5_0", 0x0569));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 16010U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_colorcoefs2_0", 0x0057));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 6404U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_colorcalib2_0", 0x0037));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 704U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, DecodesCanonFilterInfoBinaryDirectoryIntoDerivedIfd)
{
    const std::vector<std::byte> mn   = make_canon_filterinfo_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_filterinfo_0", 0x0402));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_filterinfo_0", 0x0403));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.count, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));

        const std::span<const std::byte> got = store.arena().span(
            e.value.data.span);
        ASSERT_GE(got.size(), 8U);
        uint32_t vals[2] = {};
        std::memcpy(vals, got.data(), sizeof(vals));
        EXPECT_EQ(vals[0], 300U);
        EXPECT_EQ(vals[1], 700U);
    }
}

TEST(MakerNoteDecode,
     DecodesCanonFilterInfoBinaryDirectoryIntoDerivedIfdWithBigEndianParent)
{
    const std::vector<std::byte> mn = make_canon_filterinfo_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_big_endian_parent("Canon", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_filterinfo_0", 0x0402));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 2U);
}

TEST(MakerNoteDecode, DecodesCanonTimeInfoIntoDerivedIfd)
{
    const std::vector<std::byte> mn   = make_canon_timeinfo_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_timeinfo_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 540U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_timeinfo_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 1234U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_timeinfo_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode,
     DecodesCanonAfInfoAspectLightingAndAfConfigIntoDerivedIfds)
{
    const std::vector<std::byte> mn   = make_canon_extended_tables_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_afinfo_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 6000U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_aspectinfo_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 50U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_lightingopt_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 11U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_afconfig_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I32);
        EXPECT_EQ(static_cast<int64_t>(e.value.data.i64), -2);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}


TEST(MakerNoteDecode, DecodesCasioType2MakerNoteQvcDirectory)
{
    const std::vector<std::byte> mn   = make_casio_type2_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("CASIO",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_casio_type2_0", 0x0002));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Array);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.count, 2U);
}

TEST(MakerNoteDecode, DecodesCasioType2MakerNoteFr10VariantLeDirectory)
{
    const std::vector<std::byte> mn = make_casio_type2_makernote_fr10_variant();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("CASIO",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_casio_type2_0", 0x0002));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Array);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.count, 2U);
}

TEST(MakerNoteDecode, MarksCasioLegacyType2CompatNames)
{
    const std::vector<std::byte> mn   = make_casio_type2_legacy_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("CASIO",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::CasioType2Legacy);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view("PreviewImageSize"));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Quality"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::CasioType2Legacy);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view("QualityMode"));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Casio_0x0008"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x0E00));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::CasioType2Legacy);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("PrintIM"));
    }
}

TEST(MakerNoteDecode, MarksCasioLegacyType2CompatNamesOnGenericRoute)
{
    const std::vector<std::byte> mn = make_casio_type2_legacy_ifd_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("CASIO", "QV-2100", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::CasioType2Legacy);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Quality"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Casio_0x0008"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x0E00));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("PrintIM"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x001C));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Casio_0x001c"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x001D));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Casio_0x001d"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x001A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Casio_0x001a"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x001E));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Casio_0x001e"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_type2_0", 0x2023));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Casio_0x2023"));
    }
}


TEST(MakerNoteDecode, DecodesCasioType2MakerNoteOutOfLineTiffRelativeOffsets)
{
    // Layout is deterministic based on make_test_tiff_with_makernote():
    // maker_note_off = 57 + strlen(make), maker_note_size = 8 + 12 = 20.
    const std::string_view make    = "CASIO";
    const uint32_t maker_note_off  = 57U + static_cast<uint32_t>(make.size());
    const uint32_t maker_note_size = 20U;
    const uint32_t payload_off     = maker_note_off + maker_note_size;

    std::vector<std::byte> mn = make_casio_type2_makernote_out_of_line(
        payload_off);
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_EQ(tiff.size(), payload_off);

    const std::byte payload[6] = {
        std::byte { 0xAA }, std::byte { 0xBB }, std::byte { 0xCC },
        std::byte { 0xDD }, std::byte { 0xEE }, std::byte { 0xFF },
    };
    tiff.insert(tiff.end(), payload, payload + 6);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_casio_type2_0", 0x2000));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
    const std::span<const std::byte> got = store.arena().span(
        e.value.data.span);
    ASSERT_GE(got.size(), 6U);
    EXPECT_EQ(got[0], payload[0]);
    EXPECT_EQ(got[1], payload[1]);
    EXPECT_EQ(got[2], payload[2]);
    EXPECT_EQ(got[3], payload[3]);
    EXPECT_EQ(got[4], payload[4]);
    EXPECT_EQ(got[5], payload[5]);
}


TEST(MakerNoteDecode, DecodesCasioFaceInfo1IntoDerivedIfd)
{
    const std::string_view make    = "CASIO";
    const uint32_t maker_note_off  = 57U + static_cast<uint32_t>(make.size());
    const uint32_t maker_note_size = 20U;
    const uint32_t payload_off     = maker_note_off + maker_note_size;
    const uint32_t payload_bytes   = 32U;

    std::vector<std::byte> mn
        = make_casio_type2_makernote_faceinfo1(payload_off, payload_bytes);
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_EQ(tiff.size(), payload_off);

    std::vector<std::byte> payload(payload_bytes, std::byte { 0 });
    // FaceInfo1: [0]=FacesDetected, [1..4]=big-endian frame size (640x480).
    payload[0] = std::byte { 1 };
    payload[1] = std::byte { 0x02 };
    payload[2] = std::byte { 0x80 };
    payload[3] = std::byte { 0x01 };
    payload[4] = std::byte { 0xE0 };
    tiff.insert(tiff.end(), payload.begin(), payload.end());

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_faceinfo1_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_faceinfo1_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
        const std::span<const std::byte> dim_bytes = store.arena().span(
            e.value.data.span);
        ASSERT_GE(dim_bytes.size(), 4U);
        uint16_t w = 0;
        uint16_t h = 0;
        std::memcpy(&w, dim_bytes.data() + 0, 2);
        std::memcpy(&h, dim_bytes.data() + 2, 2);
        EXPECT_EQ(w, 640U);
        EXPECT_EQ(h, 480U);
    }
}

TEST(MakerNoteDecode, DecodesCasioFaceInfo2IntoDerivedIfdForDciAlias)
{
    const std::string_view make    = "CASIO";
    const uint32_t maker_note_off  = 57U + static_cast<uint32_t>(make.size());
    const uint32_t maker_note_size = 20U;
    const uint32_t payload_off     = maker_note_off + maker_note_size;
    const uint32_t payload_bytes   = 0x54U;

    std::vector<std::byte> mn
        = make_casio_type2_makernote_faceinfo2(payload_off, payload_bytes);
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_EQ(tiff.size(), payload_off);

    std::vector<std::byte> payload(payload_bytes, std::byte { 0 });
    payload[0] = std::byte { 0x02 };
    payload[1] = std::byte { 0x01 };
    payload[2] = std::byte { 2 };

    write_u16le_at(&payload, 0x0004, 640U);
    write_u16le_at(&payload, 0x0006, 480U);
    payload[0x0008] = std::byte { 7 };

    write_u16le_at(&payload, 0x0018, 10U);
    write_u16le_at(&payload, 0x001A, 20U);
    write_u16le_at(&payload, 0x001C, 30U);
    write_u16le_at(&payload, 0x001E, 40U);

    write_u16le_at(&payload, 0x004C, 50U);
    write_u16le_at(&payload, 0x004E, 60U);
    write_u16le_at(&payload, 0x0050, 70U);
    write_u16le_at(&payload, 0x0052, 80U);

    tiff.insert(tiff.end(), payload.begin(), payload.end());

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_faceinfo2_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_faceinfo2_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
        const std::span<const std::byte> dim_bytes = store.arena().span(
            e.value.data.span);
        ASSERT_GE(dim_bytes.size(), 4U);
        uint16_t w = 0;
        uint16_t h = 0;
        std::memcpy(&w, dim_bytes.data() + 0, 2);
        std::memcpy(&h, dim_bytes.data() + 2, 2);
        EXPECT_EQ(w, 640U);
        EXPECT_EQ(h, 480U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_faceinfo2_0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 7U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_faceinfo2_0", 0x0018));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 4U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}


TEST(MakerNoteDecode, DecodesCasioQvciApp1Block)
{
    // Minimal JPEG with a single APP1 "QVCI" segment and EOI.
    std::vector<std::byte> jpg;
    jpg.push_back(std::byte { 0xFF });
    jpg.push_back(std::byte { 0xD8 });  // SOI
    jpg.push_back(std::byte { 0xFF });
    jpg.push_back(std::byte { 0xE1 });  // APP1

    std::vector<std::byte> payload(0x90, std::byte { 0 });
    payload[0]    = std::byte { 'Q' };
    payload[1]    = std::byte { 'V' };
    payload[2]    = std::byte { 'C' };
    payload[3]    = std::byte { 'I' };
    payload[0x2c] = std::byte { 3 };  // CasioQuality
    // DateTimeOriginal at 0x4d, "YYYY.MM.DD.HH.MM.SS" (20 bytes).
    const char dt[20] = "2025.01.02.03.04.05";
    std::memcpy(payload.data() + 0x4d, dt, 20);
    const char model[7] = "QV7000";
    std::memcpy(payload.data() + 0x62, model, 6);

    const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
    append_u16be(&jpg, seg_len);
    jpg.insert(jpg.end(), payload.begin(), payload.end());
    jpg.push_back(std::byte { 0xFF });
    jpg.push_back(std::byte { 0xD9 });  // EOI

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> scratch {};
    std::array<uint32_t, 64> scratch_idx {};

    ExifDecodeOptions exif_options;
    exif_options.decode_makernote = true;
    PayloadOptions payload_options;
    const SimpleMetaResult res
        = simple_meta_read(std::span<const std::byte>(jpg.data(), jpg.size()),
                           store, blocks, ifds, scratch, scratch_idx,
                           exif_options, payload_options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_casio_qvci_0", 0x002c));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(e.value.data.u64, 3U);
}


TEST(MakerNoteDecode, DecodesFujiMakerNoteWithSignatureAndOffset)
{
    const std::vector<std::byte> mn   = make_fuji_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_fuji0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 0x42U);
}

TEST(MakerNoteDecode, NamesFujiMakerNoteTag1200LikeExifTool)
{
    const std::vector<std::byte> mn   = make_fuji_makernote_tag_1200();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_fuji0", 0x1200));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("FujiFilm_0x1200"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FujiFilm_0x1200"));
}

TEST(MakerNoteDecode, DecodesFujiMakerNoteWithMultipleValueKinds)
{
    const std::vector<std::byte> mn   = make_fuji_makernote_extended();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_fuji0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view got(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(got, "0130");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_fuji0", 0x1000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 2U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_fuji0", 0x1023));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 3U);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        ASSERT_EQ(raw.size(), 6U);
        uint16_t v0 = 0;
        uint16_t v1 = 0;
        uint16_t v2 = 0;
        ASSERT_TRUE(read_u16le_at(raw, 0U, &v0));
        ASSERT_TRUE(read_u16le_at(raw, 2U, &v1));
        ASSERT_TRUE(read_u16le_at(raw, 4U, &v2));
        EXPECT_EQ(v0, 100U);
        EXPECT_EQ(v1, 200U);
        EXPECT_EQ(v2, 300U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_fuji0", 0x1438));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 321U);
    }
}

TEST(MakerNoteDecode, MarksFujifilm1304PlaceholderForFujifilmMake)
{
    const std::vector<std::byte> mn = make_fuji_ge2_makernote_extended();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote("FUJIFILM", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_fuji0", 0x1304));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::FujifilmMain1304);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("GEImageSize"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FujiFilm_0x1304"));
}

TEST(MakerNoteDecode, LeavesFujifilm1304CanonicalForGeneralImagingMake)
{
    const std::vector<std::byte> mn = make_fuji_ge2_makernote_extended();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote("GENERAL IMAGING", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_fuji0", 0x1304));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("GEImageSize"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("GEImageSize"));
}

TEST(MakerNoteDecode, MarksFujifilmCompatPlaceholdersForOpaqueMainTags)
{
    const std::vector<std::byte> mn = make_fuji_makernote_placeholder_fields();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote("FUJIFILM", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    const struct {
        uint16_t tag;
        std::string_view canonical;
        std::string_view compat;
    } checks[] = {
        { 0x1051U, "CropFlag", "FujiFilm_0x1051" },
        { 0x1150U, "CompositeImageMode", "FujiFilm_0x1150" },
        { 0x1151U, "CompositeImageCount1", "FujiFilm_0x1151" },
        { 0x1152U, "CompositeImageCount2", "FujiFilm_0x1152" },
        { 0x144AU, "WBRed", "FujiFilm_0x144a" },
    };
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i) {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_fuji0", checks[i].tag));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::FujifilmMain1304);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  checks[i].canonical);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  checks[i].compat);
    }
}

TEST(MakerNoteDecode, DecodesFujiGeType2MakerNote)
{
    const std::vector<std::byte> mn   = make_fuji_ge2_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_fuji0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 0x42U);
}

TEST(MakerNoteDecode, DecodesFujiGeType2MakerNoteWithMultipleTags)
{
    const std::vector<std::byte> mn   = make_fuji_ge2_makernote_extended();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_fuji0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0x42U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_fuji0", 0x1304));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x11223344U);
    }
}

TEST(MakerNoteDecode, DecodesKodakKdkMakerNote)
{
    const std::vector<std::byte> mn   = make_kodak_kdk_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("KODAK",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_kodak0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "CX6330");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_kodak0", 0x000c));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 800U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_kodak0", 0x0014));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "03:04:05.06");
    }
}


TEST(MakerNoteDecode, DecodesKodakType2MakerNoteIntoTypedTable)
{
    const std::vector<std::byte> mn   = make_kodak_type2_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("KODAK",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_kodak_type2_0", 0x0028));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("KodakModel"));
}


TEST(MakerNoteDecode, DecodesKodakSerialOnlyMakerNoteIntoType7Table)
{
    std::vector<std::byte> mn;
    append_bytes(&mn, "C33000641478");
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("KODAK",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_kodak_type7_0", 0x0000));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("SerialNumber"));
}


TEST(MakerNoteDecode, DecodesKodakType5MakerNoteIntoTypedTable)
{
    const std::vector<std::byte> mn = make_kodak_type5_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("KODAK", "CX4200", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_kodak_type5_0", 0x0014));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ExposureTime"));
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 33333U);
}


TEST(MakerNoteDecode, KeepsKodakAbsoluteLegacyPCohortInType8Table)
{
    static constexpr const char* kModels[] = {
        "KODAK P712 ZOOM DIGITAL CAMERA",
        "KODAK P850 ZOOM DIGITAL CAMERA",
        "KODAK P880 ZOOM DIGITAL CAMERA",
    };

    for (const char* model : kModels) {
        const std::vector<std::byte> mn = make_kodak_absolute_ifd_makernote();
        const std::vector<std::byte> tiff
            = make_test_tiff_with_makernote_and_model("KODAK", model, mn);

        MetaStore store;
        std::array<ExifIfdRef, 8> ifds {};
        ExifDecodeOptions options;
        options.decode_makernote   = true;
        const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds,
                                                      options);
        EXPECT_EQ(res.status, ExifDecodeStatus::Ok) << model;

        store.finalize();
        const std::span<const EntryId> type8_ids = store.find_all(
            exif_key("mk_kodak_type8_0", 0x0200));
        ASSERT_EQ(type8_ids.size(), 1U) << model;
        EXPECT_EQ(store.entry(type8_ids[0]).value.data.u64, 1U) << model;
    }
}


TEST(MakerNoteDecode, KeepsKodakAbsolutePixproModelsInType8Table)
{
    const std::vector<std::byte> mn = make_kodak_absolute_ifd_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("KODAK", "PIXPRO AZ251", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> type8_ids = store.find_all(
        exif_key("mk_kodak_type8_0", 0x0200));
    ASSERT_EQ(type8_ids.size(), 1U);
    EXPECT_EQ(store.entry(type8_ids[0]).value.data.u64, 1U);

    const std::span<const EntryId> camerainfo_ids = store.find_all(
        exif_key("mk_kodak_camerainfo_0", 0x0200));
    EXPECT_TRUE(camerainfo_ids.empty());
}


TEST(MakerNoteDecode, RoutesKodakAbsolutePixproType11CohortIntoType11Table)
{
    static constexpr const char* kModels[] = {
        "PIXPRO 4KVR360",
        "KODAK PIXPRO SL10",
        "KODAK PIXPRO SP360",
    };

    for (const char* model : kModels) {
        const std::vector<std::byte> mn = make_kodak_absolute_ifd_makernote();
        const std::vector<std::byte> tiff
            = make_test_tiff_with_makernote_and_model("KODAK", model, mn);

        MetaStore store;
        std::array<ExifIfdRef, 8> ifds {};
        ExifDecodeOptions options;
        options.decode_makernote   = true;
        const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds,
                                                      options);
        EXPECT_EQ(res.status, ExifDecodeStatus::Ok) << model;

        store.finalize();
        const std::span<const EntryId> type11_ids = store.find_all(
            exif_key("mk_kodak_type11_0", 0x0200));
        ASSERT_EQ(type11_ids.size(), 1U) << model;
        EXPECT_EQ(store.entry(type11_ids[0]).value.data.u64, 1U) << model;

        const std::span<const EntryId> type8_ids = store.find_all(
            exif_key("mk_kodak_type8_0", 0x0200));
        EXPECT_TRUE(type8_ids.empty()) << model;
    }
}


TEST(MakerNoteDecode, DecodesNikonMakerNoteWithEmbeddedTiffHeader)
{
    const std::vector<std::byte> mn   = make_nikon_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikon0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 0x01020304U);
}

TEST(MakerNoteDecode, RandomAccessDecodesNikonEmbeddedTiff)
{
    const std::vector<std::byte> mn   = make_nikon_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Nikon",
                                                                      mn);

    MetaStore store;
    const ExifRandomAccessDecodeResult result
        = decode_makernote_callback(tiff, store);
    EXPECT_TRUE(result.complete());
    EXPECT_EQ(result.decode.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikon0", 0x001F));
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Bytes);
}

TEST(MakerNoteDecode, RandomAccessNikonRecursionHonorsAggregateReadBudgets)
{
    const std::vector<std::byte> mn   = make_nikon_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Nikon",
                                                                      mn);

    MetaStore baseline_store;
    const ExifRandomAccessDecodeResult baseline
        = decode_makernote_callback(tiff, baseline_store);
    ASSERT_TRUE(baseline.complete());
    ASSERT_GT(baseline.input.requests_issued, 1U);
    ASSERT_GT(baseline.input.bytes_requested, 1U);

    RandomAccessReadLimits request_limits;
    request_limits.max_requests = baseline.input.requests_issued - 1U;
    MetaStore request_store;
    const ExifRandomAccessDecodeResult request_limited
        = decode_makernote_callback(tiff, request_store, request_limits);
    EXPECT_EQ(request_limited.input.code,
              RandomAccessReadCode::RequestLimitExceeded);
    EXPECT_LE(request_limited.input.requests_issued,
              request_limits.max_requests);
    EXPECT_EQ(request_limited.decode.status, ExifDecodeStatus::LimitExceeded);

    RandomAccessReadLimits byte_limits;
    byte_limits.max_total_bytes = baseline.input.bytes_requested - 1U;
    MetaStore byte_store;
    const ExifRandomAccessDecodeResult byte_limited
        = decode_makernote_callback(tiff, byte_store, byte_limits);
    EXPECT_EQ(byte_limited.input.code, RandomAccessReadCode::ByteLimitExceeded);
    EXPECT_LE(byte_limited.input.bytes_requested, byte_limits.max_total_bytes);
    EXPECT_EQ(byte_limited.decode.status, ExifDecodeStatus::LimitExceeded);
}

TEST(MakerNoteDecode, RandomAccessDecodesNikonType1OuterTiffIfd)
{
    std::vector<std::byte> mn;
    append_bytes(&mn, std::string_view("Nikon\0", 6U));
    append_u16le(&mn, 1U);
    append_u16le(&mn, 1U);
    append_u16le(&mn, 0x0001U);
    append_u16le(&mn, 4U);
    append_u32le(&mn, 1U);
    append_u32le(&mn, 0x01020304U);
    append_u32le(&mn, 0U);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Nikon",
                                                                      mn);

    MetaStore store;
    const ExifRandomAccessDecodeResult result
        = decode_makernote_callback(tiff, store);
    EXPECT_TRUE(result.complete());
    EXPECT_EQ(result.decode.status, ExifDecodeStatus::Ok);
    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikon0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(store.entry(ids[0]).value.data.u64, 0x01020304U);
}

TEST(MakerNoteDecode, DecodesEmbeddedTiffMakerNoteValuesBeyondDeclaredCount)
{
    // Embedded TIFF header with an out-of-line value stored beyond the declared
    // MakerNote byte count.
    std::vector<std::byte> mn;
    append_bytes(&mn, "II");
    append_u16le(&mn, 42);
    append_u32le(&mn, 8);  // IFD0 offset
    append_u16le(&mn, 1);  // entry count
    append_u16le(&mn, 0x0E22);
    append_u16le(&mn, 3);    // SHORT
    append_u32le(&mn, 4);    // count
    append_u32le(&mn, 100);  // value offset
    append_u32le(&mn, 0);

    while (mn.size() < 100U) {
        mn.push_back(std::byte { 0 });
    }
    append_u16le(&mn, 1);
    append_u16le(&mn, 2);
    append_u16le(&mn, 3);
    append_u16le(&mn, 4);
    EXPECT_GE(mn.size(), 108U);

    std::vector<std::byte> tiff = make_test_tiff_with_makernote("Nikon", mn);
    ASSERT_TRUE(patch_makernote_count_in_tiff(&tiff, 32));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikon0", 0x0E22));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Array);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.count, 4U);
    EXPECT_FALSE(any(e.flags, EntryFlags::Derived));

    MetaStore callback_store;
    const ExifRandomAccessDecodeResult callback_result
        = decode_makernote_callback(tiff, callback_store);
    EXPECT_TRUE(callback_result.complete());
    EXPECT_EQ(callback_result.decode.status, ExifDecodeStatus::Ok);
    callback_store.finalize();
    const std::span<const EntryId> callback_ids = callback_store.find_all(
        exif_key("mk_nikon0", 0x0E22));
    ASSERT_EQ(callback_ids.size(), 1U);
    EXPECT_EQ(callback_store.entry(callback_ids[0]).value.count, 4U);
}

TEST(MakerNoteDecode, DecodesNikonBinarySubdirectories)
{
    const std::vector<std::byte> mn   = make_nikon_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_vrinfo_0", 0x0006));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_vrinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "0101");
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, DecodesNikonBinarySubdirectoriesExtended)
{
    const std::vector<std::byte> mn = make_nikon_makernote_with_binary_subdirs();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_distortinfo_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0106_0", 0x0006));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0106_0", 0x0009));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 0U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_multiexposure_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 3U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_afinfo2v0100_0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        ASSERT_EQ(raw.size(), 5U);
        EXPECT_EQ(raw[0], std::byte { 0xaa });
        EXPECT_EQ(raw[1], std::byte { 0xbb });
        EXPECT_EQ(raw[2], std::byte { 0xcc });
        EXPECT_EQ(raw[3], std::byte { 0xdd });
        EXPECT_EQ(raw[4], std::byte { 0xee });
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_afinfo2v0100_0", 0x001c));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_fileinfo_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 99U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_retouchinfo_0", 0x0005));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I8);
        EXPECT_EQ(static_cast<int64_t>(e.value.data.i64), -1);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, DecodesNikonFlashInfo0108Using0107Layout)
{
    const std::vector<std::byte> mn = make_nikon_makernote_with_flashinfo_0108();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0107_0", 0x0000)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0107_0", 0x0028)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0107_0", 0x0029)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0107_0", 0x002a)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0107_0", 0x0011)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0107_0", 0x0012)).size(),
        2U);
    EXPECT_TRUE(
        store.find_all(exif_key("mk_nikon_flashinfo0107_0", 0x0014)).empty());
    EXPECT_TRUE(
        store.find_all(exif_key("mk_nikon_flashinfo0107_0", 0x0015)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0107_0", 0x0011));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoGroups);
        EXPECT_EQ(e.origin.name_context_variant, 5U);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("FlashGroupAControlMode"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0107_0", 0x0012));
        ASSERT_EQ(ids.size(), 2U);
        const Entry& group_b = store.entry(ids[0]);
        EXPECT_EQ(group_b.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(group_b.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(group_b.value.data.u64, 3U);
        EXPECT_TRUE(any(group_b.flags, EntryFlags::ContextualName));
        EXPECT_EQ(group_b.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoGroups);
        EXPECT_EQ(group_b.origin.name_context_variant, 6U);
        EXPECT_EQ(exif_entry_name(store, group_b,
                                  ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("FlashGroupBControlMode"));

        const Entry& group_c = store.entry(ids[1]);
        EXPECT_EQ(group_c.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(group_c.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(group_c.value.data.u64, 4U);
        EXPECT_TRUE(any(group_c.flags, EntryFlags::ContextualName));
        EXPECT_EQ(group_c.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoGroups);
        EXPECT_EQ(group_c.origin.name_context_variant, 7U);
        EXPECT_EQ(exif_entry_name(store, group_c,
                                  ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("FlashGroupCControlMode"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0107_0", 0x0028));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoGroups);
        EXPECT_EQ(e.origin.name_context_variant, 1U);
    }
}

TEST(MakerNoteDecode, DecodesNikonFlashInfo0102UsingLegacyLayout)
{
    const std::vector<std::byte> mn = make_nikon_makernote_with_flashinfo_0102();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0102_0", 0x0000)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0102_0", 0x0010)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0102_0", 0x0011)).size(),
        2U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0102_0", 0x0012)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0102_0", 0x0013)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0102_0", 0x0014)).size(),
        1U);
    EXPECT_TRUE(
        store.find_all(exif_key("mk_nikon_flashinfo0102_0", 0x0015)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0102_0", 0x0012));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(e.origin.name_context_variant, 2U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0102_0", 0x0010));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(e.origin.name_context_variant, 5U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0102_0", 0x0011));
        ASSERT_EQ(ids.size(), 2U);
        const Entry& first  = store.entry(ids[0]);
        const Entry& second = store.entry(ids[1]);
        EXPECT_TRUE(any(first.flags, EntryFlags::ContextualName));
        EXPECT_TRUE(any(second.flags, EntryFlags::ContextualName));
        EXPECT_EQ(first.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(second.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(first.origin.name_context_variant, 6U);
        EXPECT_EQ(second.origin.name_context_variant, 7U);
    }
}

TEST(MakerNoteDecode, DecodesNikonFlashInfo0101UsingLegacyLayout)
{
    const std::vector<std::byte> mn = make_nikon_makernote_with_flashinfo_0101();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0100_0", 0x000a)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0100_0", 0x0011)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0100_0", 0x0012)).size(),
        1U);
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0100_0", 0x000a));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(e.origin.name_context_variant, 8U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0100_0", 0x0011));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(e.origin.name_context_variant, 2U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0100_0", 0x0012));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(e.origin.name_context_variant, 3U);
    }
}

TEST(MakerNoteDecode, DecodesNikonFlashInfo0103UsingLegacyLayout)
{
    const std::vector<std::byte> mn = make_nikon_makernote_with_flashinfo_0103();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0103_0", 0x0011)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0103_0", 0x0012)).size(),
        2U);
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0103_0", 0x0011));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(e.origin.name_context_variant, 5U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0103_0", 0x0012));
        ASSERT_EQ(ids.size(), 2U);
        const Entry& first  = store.entry(ids[0]);
        const Entry& second = store.entry(ids[1]);
        EXPECT_TRUE(any(first.flags, EntryFlags::ContextualName));
        EXPECT_TRUE(any(second.flags, EntryFlags::ContextualName));
        EXPECT_EQ(first.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(second.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(first.origin.name_context_variant, 6U);
        EXPECT_EQ(second.origin.name_context_variant, 7U);
    }
}

TEST(MakerNoteDecode, RenamesNikonMainZTagsForZFamilyModels)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_main_long_tag(0x002E, 3008U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON Z 5", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikon0", 0x002E));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind, EntryNameContextKind::NikonMainZ);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("AFAreaXPosition"));
}

TEST(MakerNoteDecode, LeavesNikonMainLegacyNamesForNonZModels)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_main_long_tag(0x002E, 0U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON D850", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikon0", 0x002E));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_NE(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("AFAreaXPosition"));
}

TEST(MakerNoteDecode, RenamesNikonMainTagsForLegacyCompactType2Models)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_main_long_tag(0x000A, 0U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "E700", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikon0", 0x000A));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::NikonMainCompactType2);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("DigitalZoom"));
}

TEST(MakerNoteDecode, LeavesNikonMainCompactType2NamesCanonicalForDslrModels)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_main_long_tag(0x000A, 0U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON D850", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikon0", 0x000A));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Nikon_0x000a"));
}

TEST(MakerNoteDecode, DecodesNikonFlashInfo0300Using0300Layout)
{
    const std::vector<std::byte> mn = make_nikon_makernote_with_flashinfo_0300();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0300_0", 0x0000)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0300_0", 0x0021)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0300_0", 0x0025)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0300_0", 0x0026)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0300_0", 0x0028)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0300_0", 0x0011)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0300_0", 0x0012)).size(),
        2U);
    EXPECT_TRUE(
        store.find_all(exif_key("mk_nikon_flashinfo0300_0", 0x0014)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0300_0", 0x0011));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoGroups);
        EXPECT_EQ(e.origin.name_context_variant, 5U);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("FlashGroupAControlMode"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0300_0", 0x0012));
        ASSERT_EQ(ids.size(), 2U);
        const Entry& group_b = store.entry(ids[0]);
        EXPECT_EQ(group_b.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(group_b.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(group_b.value.data.u64, 3U);
        EXPECT_TRUE(any(group_b.flags, EntryFlags::ContextualName));
        EXPECT_EQ(group_b.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoGroups);
        EXPECT_EQ(group_b.origin.name_context_variant, 6U);
        EXPECT_EQ(exif_entry_name(store, group_b,
                                  ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("FlashGroupBControlMode"));

        const Entry& group_c = store.entry(ids[1]);
        EXPECT_EQ(group_c.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(group_c.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(group_c.value.data.u64, 4U);
        EXPECT_TRUE(any(group_c.flags, EntryFlags::ContextualName));
        EXPECT_EQ(group_c.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoGroups);
        EXPECT_EQ(group_c.origin.name_context_variant, 7U);
        EXPECT_EQ(exif_entry_name(store, group_c,
                                  ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("FlashGroupCControlMode"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0300_0", 0x0028));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoGroups);
        EXPECT_EQ(e.origin.name_context_variant, 1U);
    }
}

TEST(MakerNoteDecode, DecodesNikonFlashInfo0106UsingLegacyLayout)
{
    const std::vector<std::byte> mn = make_nikon_makernote_with_flashinfo_0106();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0106_0", 0x0027)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0106_0", 0x0028)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0106_0", 0x0029)).size(),
        1U);
    EXPECT_EQ(
        store.find_all(exif_key("mk_nikon_flashinfo0106_0", 0x002a)).size(),
        1U);
    EXPECT_TRUE(
        store.find_all(exif_key("mk_nikon_flashinfo0106_0", 0x0012)).empty());
    EXPECT_TRUE(
        store.find_all(exif_key("mk_nikon_flashinfo0106_0", 0x0013)).empty());
    EXPECT_TRUE(
        store.find_all(exif_key("mk_nikon_flashinfo0106_0", 0x0014)).empty());
    EXPECT_TRUE(
        store.find_all(exif_key("mk_nikon_flashinfo0106_0", 0x0015)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0106_0", 0x0027));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonFlashInfoLegacy);
        EXPECT_EQ(e.origin.name_context_variant, 1U);
    }
}

TEST(MakerNoteDecode, DecodesNikonLensData0100UsingLegacyLayout)
{
    std::vector<std::byte> raw(13U, std::byte { 0 });
    raw[0]  = std::byte { '0' };
    raw[1]  = std::byte { '1' };
    raw[2]  = std::byte { '0' };
    raw[3]  = std::byte { '0' };
    raw[6]  = std::byte { 101 };
    raw[7]  = std::byte { 68 };
    raw[8]  = std::byte { 96 };
    raw[9]  = std::byte { 152 };
    raw[10] = std::byte { 52 };
    raw[11] = std::byte { 60 };
    raw[12] = std::byte { 107 };

    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_main_bytes_tag(0x0098, raw);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_lensdata0100_0", 0x0006)).size(),
              1U);
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_lensdata0100_0", 0x000B)).size(),
              1U);
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_lensdata0100_0", 0x000C)).size(),
              1U);
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_lensdata0100_0", 0x000A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 52U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_lensdata0100_0", 0x000C));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 107U);
    }
}

TEST(MakerNoteDecode, DecodesNikonLensData0101UsingLegacyLayout)
{
    std::vector<std::byte> raw(31U, std::byte { 0 });
    raw[0]  = std::byte { '0' };
    raw[1]  = std::byte { '1' };
    raw[2]  = std::byte { '0' };
    raw[3]  = std::byte { '1' };
    raw[4]  = std::byte { 23 };
    raw[5]  = std::byte { 52 };
    raw[8]  = std::byte { 44 };
    raw[9]  = std::byte { 15 };
    raw[10] = std::byte { 120 };
    raw[11] = std::byte { 33 };
    raw[12] = std::byte { 68 };
    raw[13] = std::byte { 24 };
    raw[14] = std::byte { 70 };
    raw[15] = std::byte { 121 };
    raw[16] = std::byte { 52 };
    raw[17] = std::byte { 60 };
    raw[18] = std::byte { 107 };

    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_main_bytes_tag(0x0098, raw);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_lensdata0101_0", 0x0004)).size(),
              1U);
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_lensdata0101_0", 0x0011)).size(),
              1U);
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_lensdata0101_0", 0x0012)).size(),
              1U);
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_lensdata0101_0", 0x0010));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 52U);
    }
}

TEST(MakerNoteDecode,
     DecodesNikonPictureControlWorldTimeIsoHdrAndLocationBlocks)
{
    const std::vector<std::byte> mn   = make_nikon_makernote_with_info_blocks();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_picturecontrol2_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "NEUTRAL");
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_picturecontrol2_0", 0x0041));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 15U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon0", 0x0024));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        const std::span<const std::byte> got = store.arena().span(
            e.value.data.span);
        ASSERT_GE(got.size(), 4U);
        EXPECT_EQ(got[0], std::byte { 0xE4 });
        EXPECT_EQ(got[1], std::byte { 0xFD });
        EXPECT_EQ(got[2], std::byte { 0x01 });
        EXPECT_EQ(got[3], std::byte { 0x02 });
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_worldtime_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.data.i64, -540);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_isoinfo_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 400U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_hdrinfo_0", 0x0007));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 4U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_locationinfo_0", 0x0009));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        const std::span<const std::byte> got = store.arena().span(
            e.value.data.span);
        ASSERT_GE(got.size(), 8U);
        EXPECT_EQ(got[0], std::byte { 'T' });
        EXPECT_EQ(got[1], std::byte { 'O' });
        EXPECT_EQ(got[2], std::byte { 'K' });
        EXPECT_EQ(got[3], std::byte { 'Y' });
        EXPECT_EQ(got[4], std::byte { 'O' });
        EXPECT_EQ(got[5], std::byte { '-' });
        EXPECT_EQ(got[6], std::byte { 'J' });
        EXPECT_EQ(got[7], std::byte { 'P' });
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, DecodesNikonPreviewSettingsAndAFTuneBlocks)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_preview_settings_and_aftune();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Nikon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_preview_0", 0x0103));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 6U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_preview_0", 0x0202));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x1234U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikonsettings_main_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 6400U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikonsettings_main_0", 0x0046));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_aftune_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I8);
        EXPECT_EQ(e.value.count, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_aftune_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I8);
        EXPECT_EQ(e.value.count, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, DecodesNikonColorBalanceCBlock)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_colorbalancec_0104();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Nikon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_colorbalancec_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        EXPECT_EQ(
            std::string_view(reinterpret_cast<const char*>(
                                 store.arena().span(e.value.data.span).data()),
                             store.arena().span(e.value.data.span).size()),
            std::string_view("0104"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_colorbalancec_0", 0x0020));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        ASSERT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 200U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_colorbalancec_0", 0x0038));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Array);
        ASSERT_EQ(e.value.elem_type, MetaElementType::U32);
        ASSERT_EQ(e.value.count, 4U);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        uint32_t v0 = 0;
        uint32_t v3 = 0;
        ASSERT_TRUE(read_u32le_at(raw, 0, &v0));
        ASSERT_TRUE(read_u32le_at(raw, 12, &v3));
        EXPECT_EQ(v0, 888U);
        EXPECT_EQ(v3, 792U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_colorbalancec_0", 0x0114));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Array);
        ASSERT_EQ(e.value.elem_type, MetaElementType::U32);
        ASSERT_EQ(e.value.count, 4U);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        uint32_t v0 = 0;
        uint32_t v1 = 0;
        uint32_t v2 = 0;
        uint32_t v3 = 0;
        ASSERT_TRUE(read_u32le_at(raw, 0, &v0));
        ASSERT_TRUE(read_u32le_at(raw, 4, &v1));
        ASSERT_TRUE(read_u32le_at(raw, 8, &v2));
        ASSERT_TRUE(read_u32le_at(raw, 12, &v3));
        EXPECT_EQ(v0, 842U);
        EXPECT_EQ(v1, 512U);
        EXPECT_EQ(v2, 513U);
        EXPECT_EQ(v3, 818U);
    }
}

TEST(MakerNoteDecode, DecodesNikonAfInfo2Version0101Layout)
{
    const std::vector<std::byte> mn = make_nikon_makernote_with_afinfo2_0101();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Nikon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(
        store.find_all(exif_key("mk_nikon_afinfo2v0101_0", 0x0014)).empty());
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_afinfo2v0101_0", 0x004a));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        ASSERT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 120U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_afinfo2v0101_0", 0x0030));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(e.value.count, 7U);
    }
}

TEST(MakerNoteDecode, DecodesNikonAfInfo2Version0301Into0300Table)
{
    const std::vector<std::byte> mn = make_nikon_makernote_with_afinfo2_0301();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Nikon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_afinfo2v0300_0", 0x0000)).size(),
              1U);
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_afinfo2v0300_0", 0x002a)).size(),
              1U);
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_afinfo2v0300_0", 0x002c)).size(),
              1U);
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_afinfo2v0300_0", 0x002e)).size(),
              1U);
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_afinfo2v0300_0", 0x0031)).size(),
              1U);
    EXPECT_EQ(store.find_all(exif_key("mk_nikon_afinfo2v0300_0", 0x0032)).size(),
              1U);
}

TEST(MakerNoteDecode, MarksNikonSettings0001PlaceholderForD780)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_settings_long_tag(0x0001, 33U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON D780", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikonsettings_main_0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::NikonSettingsMain);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ISOAutoHiLimit"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x0001"));
}

TEST(MakerNoteDecode, LeavesNikonSettings0001CanonicalForD6)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_settings_long_tag(0x0001, 37U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON D6", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikonsettings_main_0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ISOAutoHiLimit"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ISOAutoHiLimit"));
}

TEST(MakerNoteDecode, MarksNikonSettings001DPlaceholderForD780ButNotZ5)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_settings_long_tag(0x001D, 1U);

    {
        const std::vector<std::byte> tiff
            = make_test_tiff_with_makernote_and_model("Nikon", "NIKON D780",
                                                      mn);

        MetaStore store;
        std::array<ExifIfdRef, 8> ifds {};
        ExifDecodeOptions options;
        options.decode_makernote   = true;
        const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds,
                                                      options);
        EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

        store.finalize();
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikonsettings_main_0", 0x001D));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(e.origin.name_context_kind,
                  EntryNameContextKind::NikonSettingsMain);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("NikonSettings_0x001d"));
    }

    {
        const std::vector<std::byte> tiff
            = make_test_tiff_with_makernote_and_model("Nikon", "NIKON Z 5", mn);

        MetaStore store;
        std::array<ExifIfdRef, 8> ifds {};
        ExifDecodeOptions options;
        options.decode_makernote   = true;
        const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds,
                                                      options);
        EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

        store.finalize();
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikonsettings_main_0", 0x001D));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("AF-CPrioritySel"));
    }
}

TEST(MakerNoteDecode, MarksNikonSettings0001PlaceholderForZ5)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_settings_long_tag(0x0001, 33U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON Z 5", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikonsettings_main_0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::NikonSettingsMain);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x0001"));
}

TEST(MakerNoteDecode, MarksNikonSettings0001PlaceholderForZ30)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_settings_long_tag(0x0001, 33U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON Z 30", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikonsettings_main_0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::NikonSettingsMain);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x0001"));
}

TEST(MakerNoteDecode, MarksNikonSettings001DPlaceholderForD850)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_settings_long_tag(0x001D, 1U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON D850", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikonsettings_main_0", 0x001D));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::NikonSettingsMain);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x001d"));
}

TEST(MakerNoteDecode, MarksNikonSettings001DPlaceholderForZ30)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_settings_long_tag(0x001D, 1U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON Z 30", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikonsettings_main_0", 0x001D));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::NikonSettingsMain);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x001d"));
}

TEST(MakerNoteDecode, RenamesNikonSettingsMovieButtonsForZ5)
{
    const std::vector<std::byte> mn1
        = make_nikon_makernote_with_single_settings_long_tag(0x00B1, 9U);
    const std::vector<std::byte> tiff1
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON Z 5", mn1);

    MetaStore store1;
    std::array<ExifIfdRef, 8> ifds1 {};
    ExifDecodeOptions options1;
    options1.decode_makernote   = true;
    const ExifDecodeResult res1 = decode_exif_tiff(tiff1, store1, ifds1,
                                                   options1);
    EXPECT_EQ(res1.status, ExifDecodeStatus::Ok);

    store1.finalize();
    const std::span<const EntryId> ids1 = store1.find_all(
        exif_key("mk_nikonsettings_main_0", 0x00B1));
    ASSERT_EQ(ids1.size(), 1U);
    const Entry& e1 = store1.entry(ids1[0]);
    EXPECT_TRUE(any(e1.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e1.origin.name_context_kind,
              EntryNameContextKind::NikonSettingsMain);
    EXPECT_EQ(e1.origin.name_context_variant, 2U);
    EXPECT_EQ(exif_entry_name(store1, e1, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("MovieFunc1Button"));

    const std::vector<std::byte> mn2
        = make_nikon_makernote_with_single_settings_long_tag(0x00B3, 13U);
    const std::vector<std::byte> tiff2
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON Z 5", mn2);

    MetaStore store2;
    std::array<ExifIfdRef, 8> ifds2 {};
    ExifDecodeOptions options2;
    options2.decode_makernote   = true;
    const ExifDecodeResult res2 = decode_exif_tiff(tiff2, store2, ifds2,
                                                   options2);
    EXPECT_EQ(res2.status, ExifDecodeStatus::Ok);

    store2.finalize();
    const std::span<const EntryId> ids2 = store2.find_all(
        exif_key("mk_nikonsettings_main_0", 0x00B3));
    ASSERT_EQ(ids2.size(), 1U);
    const Entry& e2 = store2.entry(ids2[0]);
    EXPECT_TRUE(any(e2.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e2.origin.name_context_kind,
              EntryNameContextKind::NikonSettingsMain);
    EXPECT_EQ(e2.origin.name_context_variant, 3U);
    EXPECT_EQ(exif_entry_name(store2, e2, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("MovieFunc2Button"));
}

TEST(MakerNoteDecode, KeepsNikonSettingsMovieButtonsAsPlaceholdersForD850AndZ30)
{
    const std::vector<std::byte> mn1
        = make_nikon_makernote_with_single_settings_long_tag(0x00B1, 9U);
    const std::vector<std::byte> tiff1
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON D850", mn1);

    MetaStore store1;
    std::array<ExifIfdRef, 8> ifds1 {};
    ExifDecodeOptions options1;
    options1.decode_makernote   = true;
    const ExifDecodeResult res1 = decode_exif_tiff(tiff1, store1, ifds1,
                                                   options1);
    EXPECT_EQ(res1.status, ExifDecodeStatus::Ok);

    store1.finalize();
    const std::span<const EntryId> ids1 = store1.find_all(
        exif_key("mk_nikonsettings_main_0", 0x00B1));
    ASSERT_EQ(ids1.size(), 1U);
    const Entry& e1 = store1.entry(ids1[0]);
    EXPECT_TRUE(any(e1.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e1.origin.name_context_kind,
              EntryNameContextKind::NikonSettingsMain);
    EXPECT_EQ(e1.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store1, e1, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x00b1"));

    const std::vector<std::byte> mn2
        = make_nikon_makernote_with_single_settings_long_tag(0x00B3, 13U);
    const std::vector<std::byte> tiff2
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON Z 30", mn2);

    MetaStore store2;
    std::array<ExifIfdRef, 8> ifds2 {};
    ExifDecodeOptions options2;
    options2.decode_makernote   = true;
    const ExifDecodeResult res2 = decode_exif_tiff(tiff2, store2, ifds2,
                                                   options2);
    EXPECT_EQ(res2.status, ExifDecodeStatus::Ok);

    store2.finalize();
    const std::span<const EntryId> ids2 = store2.find_all(
        exif_key("mk_nikonsettings_main_0", 0x00B3));
    ASSERT_EQ(ids2.size(), 1U);
    const Entry& e2 = store2.entry(ids2[0]);
    EXPECT_TRUE(any(e2.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e2.origin.name_context_kind,
              EntryNameContextKind::NikonSettingsMain);
    EXPECT_EQ(e2.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store2, e2, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x00b3"));
}

TEST(MakerNoteDecode, KeepsNikonSettings010BCanonicalForZfcValue1)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_settings_long_tag(0x010B, 1U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON Z fc", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikonsettings_main_0", 0x010B));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("BracketIncrement"));
}

TEST(MakerNoteDecode, MarksNikonSettings0103PlaceholderForD6)
{
    const std::vector<std::byte> mn
        = make_nikon_makernote_with_single_settings_long_tag(0x0103, 3U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Nikon", "NIKON D6", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikonsettings_main_0", 0x0103));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::NikonSettingsMain);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("HDMIOutputHDR"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x0103"));
}

TEST(MakerNoteDecode, DecodesNintendoCameraInfoDerivedSubdirectory)
{
    const std::vector<std::byte> mn = make_nintendo_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote("Nintendo", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nintendo_camerainfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "3DS1");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nintendo_camerainfo_0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x12345678U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nintendo_camerainfo_0", 0x0030));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 5U);
    }
}

TEST(MakerNoteDecode, DecodesHpType6MakerNote)
{
    const std::vector<std::byte> mn   = make_hp_type6_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("HP", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_hp_type6_0", 0x000c));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::URational);
        EXPECT_EQ(e.value.data.ur.numer, 28U);
        EXPECT_EQ(e.value.data.ur.denom, 10U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_hp_type6_0", 0x0034));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 200U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_hp_type6_0", 0x0058));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "HP-12345");
    }
}

TEST(MakerNoteDecode, DecodesPhaseOneMakerNoteAsClassicIfd)
{
    const std::vector<std::byte> mn = make_phaseone_makernote_main();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Phase One A/S", "IQ180", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_phaseone0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_phaseone0", 0x0105));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 35U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_phaseone_sensorcalibration_0", 0x0400));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 7U);
    }
}


TEST(MakerNoteDecode, DecodesLeafCredoIiqMakerNoteAsPhaseOne)
{
    const std::vector<std::byte> mn = make_phaseone_makernote_main();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Leaf", "Credo 40", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_phaseone0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 1U);
    }
    EXPECT_TRUE(store.find_all(exif_key("mk_kodak_type9_0", 0x000c)).empty());
}


TEST(MakerNoteDecode, DecodesAppleMakerNoteWithBigEndianIfdAtOffset14)
{
    const std::vector<std::byte> mn   = make_apple_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Apple",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_apple0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 17U);
}

TEST(MakerNoteDecode, DecodesAppleMakerNoteWithMixedValueKinds)
{
    const std::vector<std::byte> mn   = make_apple_makernote_extended();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Apple",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_apple0", 0x0007));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_apple0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "HELLO");
    }
}

TEST(MakerNoteDecode, DecodesFlirMakerNoteClassicIfd)
{
    const std::vector<std::byte> mn   = make_flir_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("FLIR",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_flir0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 99U);
}


TEST(MakerNoteDecode, DecodesOlympusMakerNoteWithOuterTiffOffsets)
{
    std::vector<std::byte> mn = make_olympus_makernote();

    const std::string_view make   = "OLYMPUS";
    const uint32_t maker_note_off = 57U + static_cast<uint32_t>(make.size());
    const uint32_t value_off_abs  = maker_note_off
                                   + static_cast<uint32_t>(mn.size());

    // Patch the out-of-line value offset in the MakerNote entry.
    // Layout: header(8) + entry_count(2) + tag(2) + type(2) + count(4) = 18.
    write_u32le_at(&mn, 18U, value_off_abs);

    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    append_u32le(&tiff, 1);
    append_u32le(&tiff, 2);
    append_u32le(&tiff, 3);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_olympus0", 0x0200));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Array);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.count, 3U);
}

TEST(MakerNoteDecode, DecodesOlympusMakerNoteWithOlympusSignatureSubIfdOffsets)
{
    const std::vector<std::byte> mn = make_olympus_makernote_olympus_signature();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("OLYMPUS",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 16> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_olympus_main_0", 0x0201));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 2U);
}

TEST(MakerNoteDecode, IgnoresOlympusSignatureSubIfdOffsetWithoutEntryCountBytes)
{
    std::vector<std::byte> mn = make_olympus_makernote_olympus_signature();
    ASSERT_GE(mn.size(), 26U);
    write_u32le_at(&mn, 22U, static_cast<uint32_t>(mn.size() - 1U));

    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("OLYMPUS",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 16> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    EXPECT_TRUE(store.find_all(exif_key("mk_olympus_main_0", 0x0201)).empty());
}

TEST(MakerNoteDecode, DecodesOlympusOmSystemMakerNoteNestedSubIfds)
{
    const std::vector<std::byte> mn
        = make_olympus_makernote_omsystem_nested_subifds();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("OMDS",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 16> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_equipment_0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 7U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_camerasettings_0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_aftargetinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 11U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_subjectdetectinfo_0", 0x000A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 33U);
    }
}

TEST(MakerNoteDecode, DecodesOlympusOldStyleMakerNoteNestedSubIfds)
{
    const std::string_view make   = "OLYMPUS";
    const uint32_t maker_note_off = 57U + static_cast<uint32_t>(make.size());
    const std::vector<std::byte> mn
        = make_olympus_makernote_oldstyle_nested_subifds(maker_note_off);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 16> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_camerasettings_0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 2U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_aftargetinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 22U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_subjectdetectinfo_0", 0x000A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 44U);
    }
}

TEST(MakerNoteDecode, DecodesOlympusOmSystemMainSubIfdMatrix)
{
    const std::vector<std::byte> mn
        = make_olympus_makernote_omsystem_main_subifd_matrix();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("OMDS",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 32> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_equipment_0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 7U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_rawdevelopment_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_rawdevelopment2_0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 4U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_imageprocessing_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 5U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_focusinfo_0", 0x0209));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_fetags_0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 6U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_fetags_1", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 8U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_olympus_rawinfo_0", 0x0614));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 321U);
    }
}


TEST(MakerNoteDecode,
     MarksOlympusFocusInfo1600PlaceholderWhenCameraSettingsAlreadyExposeImageStabilization)
{
    const std::vector<std::byte> mn
        = make_olympus_makernote_omsystem_focusinfo_name_context(true);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("OMDS",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 32> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_olympus_focusinfo_0", 0x1600));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::OlympusFocusInfo1600);
    EXPECT_EQ(e.origin.name_context_variant, 2U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ImageStabilization"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Olympus_FocusInfo_0x1600"));
}


TEST(MakerNoteDecode,
     MarksOlympusFocusInfo1600SemanticWhenNoSeparateImageStabilizationExists)
{
    const std::vector<std::byte> mn
        = make_olympus_makernote_omsystem_focusinfo_name_context(false);
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("OMDS",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 32> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_olympus_focusinfo_0", 0x1600));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::OlympusFocusInfo1600);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ImageStabilization"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ImageStabilization"));
}


TEST(MakerNoteDecode, MarksMotorola6420PlaceholderForLegacyXtModels)
{
    const std::vector<std::byte> mn = make_motorola_makernote_custom_rendered();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Motorola", "XT1060", mn);

    MetaStore store;
    std::array<ExifIfdRef, 32> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_motorola0", 0x6420));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind,
              EntryNameContextKind::MotorolaMain6420);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("CustomRendered"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Motorola_0x6420"));
}


TEST(MakerNoteDecode, LeavesMotorola6420CanonicalForModernModels)
{
    const std::vector<std::byte> mn = make_motorola_makernote_custom_rendered();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("Motorola",
                                                  "motorola edge 50 neo", mn);

    MetaStore store;
    std::array<ExifIfdRef, 32> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_motorola0", 0x6420));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_FALSE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("CustomRendered"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("CustomRendered"));
}

TEST(MakerNoteDecode, DecodesPanasonicBinarySubDirs)
{
    std::vector<std::byte> mn   = make_panasonic_makernote_with_subdirs();
    const std::string_view make = "Panasonic";
    const uint32_t maker_note_off
        = 57U + static_cast<uint32_t>(make.size());  // see builder layout

    // MakerNote IFD layout:
    // u16(count=3) + 3*12 bytes + u32(next_ifd) = 42 bytes header.
    const uint32_t header_size = 42U;
    const uint32_t facedet_off = maker_note_off + header_size;
    const uint32_t facerec_off = facedet_off + 10U;
    const uint32_t time_off    = facerec_off + 52U;

    // Patch UNDEFINED value offsets to be absolute (outer TIFF-relative).
    write_u32le_at(&mn, 10U, facedet_off);
    write_u32le_at(&mn, 22U, facerec_off);
    write_u32le_at(&mn, 34U, time_off);

    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facedetinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facedetinfo_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 4U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        const std::string_view v(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
        EXPECT_EQ(v.substr(0, 3), "Bob");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0018));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 4U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0020));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        const std::string_view v(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
        EXPECT_EQ(v.substr(0, 2), "25");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_timeinfo_0", 0x0010));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 123U);
    }
}

TEST(MakerNoteDecode, DecodesPanasonicType2MakerNote)
{
    const std::vector<std::byte> mn = make_panasonic_type2_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote("Panasonic", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_type2_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Text);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_type2_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 42U);
    }
}

TEST(MakerNoteDecode, DecodesPanasonicExtendedBinarySubDirs)
{
    std::vector<std::byte> mn = make_panasonic_makernote_with_extended_subdirs(
        false);
    const std::string_view make   = "Panasonic";
    const uint32_t maker_note_off = 57U + static_cast<uint32_t>(make.size());
    const uint32_t header_size    = 42U;
    const uint32_t facedet_off    = maker_note_off + header_size;
    const uint32_t facerec_off    = facedet_off + 42U;
    const uint32_t time_off       = facerec_off + 148U;

    write_u32le_at(&mn, 10U, facedet_off);
    write_u32le_at(&mn, 22U, facerec_off);
    write_u32le_at(&mn, 34U, time_off);

    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facedetinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 5U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facedetinfo_0", 0x0005));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Array);
        ASSERT_EQ(e.value.elem_type, MetaElementType::U16);
        ASSERT_EQ(e.value.count, 4U);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        uint16_t v0 = 0;
        uint16_t v3 = 0;
        ASSERT_TRUE(read_u16le_at(raw, 0, &v0));
        ASSERT_TRUE(read_u16le_at(raw, 6, &v3));
        EXPECT_EQ(v0, 50U);
        EXPECT_EQ(v3, 80U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facedetinfo_0", 0x0011));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e                       = store.entry(ids[0]);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        uint16_t v0 = 0;
        uint16_t v3 = 0;
        ASSERT_TRUE(read_u16le_at(raw, 0, &v0));
        ASSERT_TRUE(read_u16le_at(raw, 6, &v3));
        EXPECT_EQ(v0, 170U);
        EXPECT_EQ(v3, 200U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0034));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e                       = store.entry(ids[0]);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        const std::string_view v(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
        EXPECT_EQ(v.substr(0, 3), "Ana");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0048));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e                       = store.entry(ids[0]);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        uint16_t v0 = 0;
        uint16_t v3 = 0;
        ASSERT_TRUE(read_u16le_at(raw, 0, &v0));
        ASSERT_TRUE(read_u16le_at(raw, 6, &v3));
        EXPECT_EQ(v0, 5U);
        EXPECT_EQ(v3, 8U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0050));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e                       = store.entry(ids[0]);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        const std::string_view v(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
        EXPECT_EQ(v.substr(0, 2), "30");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0064));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e                       = store.entry(ids[0]);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        const std::string_view v(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
        EXPECT_EQ(v.substr(0, 3), "Eve");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0078));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e                       = store.entry(ids[0]);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        uint16_t v0 = 0;
        uint16_t v3 = 0;
        ASSERT_TRUE(read_u16le_at(raw, 0, &v0));
        ASSERT_TRUE(read_u16le_at(raw, 6, &v3));
        EXPECT_EQ(v0, 9U);
        EXPECT_EQ(v3, 12U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0080));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e                       = store.entry(ids[0]);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        const std::string_view v(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
        EXPECT_EQ(v.substr(0, 2), "19");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_timeinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        const std::string_view v(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
        EXPECT_EQ(v, "2024:06:27 12:53:52.54");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_timeinfo_0", 0x0010));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 321U);
    }
}

TEST(MakerNoteDecode, DecodesPanasonicWithTruncatedNextIfdPointer)
{
    std::vector<std::byte> mn = make_panasonic_makernote_with_extended_subdirs(
        true);
    const std::string_view make   = "Panasonic";
    const uint32_t maker_note_off = 57U + static_cast<uint32_t>(make.size());
    const uint32_t header_size    = 38U;
    const uint32_t facedet_off    = maker_note_off + header_size;
    const uint32_t facerec_off    = facedet_off + 42U;
    const uint32_t time_off       = facerec_off + 148U;

    write_u32le_at(&mn, 10U, facedet_off);
    write_u32le_at(&mn, 22U, facerec_off);
    write_u32le_at(&mn, 34U, time_off);

    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facedetinfo_0", 0x0011));
        ASSERT_EQ(ids.size(), 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_facerecinfo_0", 0x0080));
        ASSERT_EQ(ids.size(), 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_timeinfo_0", 0x0010));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 321U);
    }
}


TEST(MakerNoteDecode, DecodesPentaxMakerNoteWithAocHeaderAndCount)
{
    const std::vector<std::byte> mn   = make_pentax_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("PENTAX",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_pentax0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 2U);
}

TEST(MakerNoteDecode, DecodesPentaxBinarySubdirectories)
{
    const std::vector<std::byte> mn
        = make_pentax_makernote_with_binary_subdirs();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("PENTAX",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_srinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 0x11U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_srinfo_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 0x44U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_faceinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_aeinfo2_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view("Pentax_0x0001"));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Pentax_0x0001"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_aeinfo2_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_aeinfo2_0", 0x0014));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 21U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_shotinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 0xA0U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_shotinfo_0", 0x0005));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 0xA5U);
    }
}

TEST(MakerNoteDecode, RoutesPentaxOptio330MakerNoteToType2Ifd)
{
    const std::vector<std::byte> mn = make_pentax_type2_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("PENTAX", "PENTAX Optio 330",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_type2_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view("RecordingMode"));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_type2_0", 0x0005));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view("Pentax_Type2_0x0005"));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Pentax_Type2_0x0005"));
    }
    EXPECT_TRUE(store.find_all(exif_key("mk_pentax0", 0x0001)).empty());
}

TEST(MakerNoteDecode, RoutesPentaxMakerNoteWithType2SignatureTagsToType2Ifd)
{
    const std::vector<std::byte> mn   = make_pentax_type2_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("PENTAX",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax_type2_0", 0x1000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view("HometownCityCode"));
    }
    EXPECT_TRUE(store.find_all(exif_key("mk_pentax0", 0x0001)).empty());
}

TEST(MakerNoteDecode, MarksPentax0062PlaceholderForKodakOemFiles)
{
    const std::vector<std::byte> mn = make_pentax_makernote_with_main_0062();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote("EASTMAN KODAK COMPANY", mn);

    MetaStore store;
    std::array<ExifIfdRef, 32> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_pentax0", 0x0062));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_TRUE(any(e.flags, EntryFlags::ContextualName));
    EXPECT_EQ(e.origin.name_context_kind, EntryNameContextKind::PentaxMain0062);
    EXPECT_EQ(e.origin.name_context_variant, 1U);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("RawDevelopmentProcess"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Pentax_0x0062"));
}

TEST(MakerNoteDecode, DoesNotExpandPentaxFaceTablesWhenFaceCountIsZero)
{
    const std::vector<std::byte> mn
        = make_pentax_makernote_with_zero_face_tables();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("PENTAX",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax0", 0x0227));
        ASSERT_EQ(ids.size(), 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_pentax0", 0x0228));
        ASSERT_EQ(ids.size(), 1U);
    }
    EXPECT_TRUE(
        store.find_all(exif_key("mk_pentax_facepos_0", 0x0000)).empty());
    EXPECT_TRUE(
        store.find_all(exif_key("mk_pentax_facesize_0", 0x0000)).empty());
}

TEST(MakerNoteDecode, DecodesRicohType2MakerNote)
{
    const std::vector<std::byte> mn   = make_ricoh_type2_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("RICOH",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_ricoh_type2_0", 0x0207));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "GRIII");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_ricoh_type2_0", 0x0300));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "RICOH");
    }
}

TEST(MakerNoteDecode, DecodesRicohPaddedType2MakerNoteUnderType2Ifd)
{
    const std::vector<std::byte> mn   = make_ricoh_type2_padded_ifd_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("RICOH",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 32> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_ricoh_type2_0", 0x0104));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.data.u64, 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_ricoh_type2_0", 0x0207));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
    }
}

TEST(MakerNoteDecode, DecodesSamsungStmnMakerNoteAndSamsungIfd)
{
    const std::vector<std::byte> mn   = make_samsung_stmn_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("SAMSUNG",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        const std::string_view v(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
        EXPECT_EQ(v, "STMN100");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x12345678U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x00010002U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung_ifd_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view("Samsung_IFD_0x0004"));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Samsung_IFD_0x0004"));
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> raw = store.arena().span(
            e.value.data.span);
        const std::string_view v(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
        EXPECT_EQ(v, "HELLO");
    }
}

TEST(MakerNoteDecode, DecodesSamsungType2PictureWizard)
{
    const std::vector<std::byte> mn   = make_samsung_type2_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("SAMSUNG",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung_type2_0", 0x0021));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Bytes);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung_picturewizard_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung_picturewizard_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 5U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung_type2_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
                  std::string_view("Samsung_Type2_0x0004"));
        EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
                  std::string_view("Samsung_Type2_0x0004"));
    }
}

TEST(MakerNoteDecode, DecodesSamsungType2PictureWizardFromU16Array)
{
    const std::vector<std::byte> mn
        = make_samsung_type2_makernote_u16_picturewizard();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("SAMSUNG",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung_type2_0", 0x0021));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 5U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung_picturewizard_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 11U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_samsung_picturewizard_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 55U);
    }
}

TEST(MakerNoteDecode, DecodesSamsungCompatTag0FromMalformedType2)
{
    const std::vector<std::byte> mn   = make_samsung_makernote_compat_digits();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("SAMSUNG",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_samsung0", 0x0000));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Text);
    const std::string_view v(reinterpret_cast<const char*>(
                                 store.arena().span(e.value.data.span).data()),
                             store.arena().span(e.value.data.span).size());
    EXPECT_EQ(v, "2024");
}

TEST(MakerNoteDecode, MapsSamsungType2A002ToCompatPlaceholder)
{
    const std::vector<std::byte> mn
        = make_samsung_type2_makernote_with_a002_and_a003();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("SAMSUNG",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_samsung_type2_0", 0xA002));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("SerialNumber"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Samsung_Type2_0xa002"));
}

TEST(MakerNoteDecode, MapsSigmaTag0033ToCompatPlaceholderForDp2QuattroModel)
{
    const std::vector<std::byte> mn = make_sigma_makernote_main_u16(0x0033U,
                                                                    41U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("SIGMA", "SIGMA dp2 Quattro",
                                                  mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sigma0", 0x0033));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ExposureTime2"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Sigma_0x0033"));
}

TEST(MakerNoteDecode, KeepsSigmaTag0033SemanticForFpLModel)
{
    const std::vector<std::byte> mn = make_sigma_makernote_main_u16(0x0033U,
                                                                    41U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("SIGMA", "SIGMA fp L", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sigma0", 0x0033));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ExposureTime2"));
}

TEST(MakerNoteDecode, MapsSigmaTag0026ToCompatPlaceholderForFpLModel)
{
    const std::vector<std::byte> mn = make_sigma_makernote_main_u16(0x0026U,
                                                                    41U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("SIGMA", "SIGMA fp L", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sigma0", 0x0026));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("FileFormat"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Sigma_0x0026"));
}

TEST(MakerNoteDecode, MapsSigmaTag0034ToCompatPlaceholderForSdQuattroHModel)
{
    const std::vector<std::byte> mn = make_sigma_makernote_main_u16(0x0034U,
                                                                    41U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("SIGMA", "sd Quattro H", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sigma0", 0x0034));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("BurstShot"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Sigma_0x0034"));
}

TEST(MakerNoteDecode, MapsSigmaTag004bToCompatPlaceholderForSdQuattroHModel)
{
    const std::vector<std::byte> mn = make_sigma_makernote_main_u16(0x004BU,
                                                                    41U);
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model("SIGMA", "sd Quattro H", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_sigma0", 0x004B));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::Canonical),
              std::string_view("ExposureTime2"));
    EXPECT_EQ(exif_entry_name(store, e, ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Sigma_0x004b"));
}

TEST(MakerNoteDecode, DecodesReconyxHyperfire2MakerNote)
{
    const std::vector<std::byte> mn   = make_reconyx_h2_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("RECONYX",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_reconyx_hyperfire2_0", 0x0010));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 7U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_reconyx_hyperfire2_0", 0x0034));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "MD");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_reconyx_hyperfire2_0", 0x0068));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "NIGHT_RUN");
    }
}

}  // namespace openmeta
