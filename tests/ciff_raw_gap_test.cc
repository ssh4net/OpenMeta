// SPDX-License-Identifier: Apache-2.0
#include "../src/openmeta/crw_ciff_decode_internal.h"
#include "../src/openmeta/exif_tiff_decode_internal.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_key.h"
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {
    static void word(std::vector<std::byte>& out, uint16_t v, bool le)
    {
        out.push_back(std::byte(le ? v & 255U : v >> 8U));
        out.push_back(std::byte(le ? v >> 8U : v & 255U));
    }
    static void dword(std::vector<std::byte>& out, uint32_t v, bool le)
    {
        for (uint32_t i = 0; i < 4U; ++i)
            out.push_back(
                std::byte((v >> (le ? i * 8U : (3U - i) * 8U)) & 255U));
    }
    struct Field final {
        uint16_t tag;
        std::vector<std::byte> bytes;
    };
    static std::vector<std::byte> directory(std::span<const Field> fields,
                                            bool le)
    {
        std::vector<std::byte> out;
        word(out, uint16_t(fields.size()), le);
        uint32_t offset = 2U + uint32_t(fields.size()) * 10U;
        for (const Field& field : fields) {
            word(out, field.tag, le);
            dword(out, uint32_t(field.bytes.size()), le);
            dword(out, offset, le);
            offset += uint32_t(field.bytes.size());
        }
        for (const Field& field : fields)
            out.insert(out.end(), field.bytes.begin(), field.bytes.end());
        dword(out, 0U, le);
        return out;
    }
    static std::vector<std::byte> words(std::span<const uint16_t> values,
                                        bool le)
    {
        std::vector<std::byte> out;
        for (uint16_t v : values)
            word(out, v, le);
        return out;
    }
    static std::vector<std::byte> fixture(std::string_view model, bool le,
                                          bool bad_size = false)
    {
        std::array<uint16_t, 43> settings {};
        settings[0]  = 86U;
        settings[1]  = 2U;
        settings[13] = 0xfff9U;
        settings[22] = 0xffffU;
        settings[42] = 0xfffcU;
        std::array<uint16_t, 28> shot {};
        shot[0]  = 56U;
        shot[7]  = 3U;  // distinguishes the former (ID - 1) offset.
        shot[8]  = 0xffffU;
        shot[19] = 0xffffU;
        std::array<uint16_t, 4> focal { 2U, 123U, 234U, 345U };
        std::array<uint16_t, 5> rawjpg { 10U, 3U, 1U, 1600U, 1200U };
        std::array<uint16_t, 23> af {};
        af[0]  = 7U;
        af[1]  = 7U;
        af[2]  = 3072U;
        af[8]  = 0xfff9U;
        af[15] = 9U;
        af[22] = 0x0041U;
        std::array<uint16_t, 13> sensor {};
        sensor[0] = 26U;
        sensor[1] = 3216U;
        std::array<uint16_t, 41> color {};
        color[1]  = 102U;
        color[2]  = 203U;
        color[3]  = 0xfffcU;
        color[4]  = 405U;
        color[29] = 0xffffU;
        color[30] = 0xfffeU;
        color[31] = 0xfffdU;
        color[32] = 0xfffcU;
        std::array<uint16_t, 3> custom { uint16_t(model == "EOS D60" ? 4U : 6U),
                                         0x0201U, 0x0a02U };
        if (bad_size)
            custom[0] = 3U;
        const std::array<Field, 8> native { { { 0x102dU, words(settings, le) },
                                              { 0x102aU, words(shot, le) },
                                              { 0x1029U, words(focal, le) },
                                              { 0x1038U, words(af, le) },
                                              { 0x10b5U, words(rawjpg, le) },
                                              { 0x1031U, words(sensor, le) },
                                              { 0x1033U, words(custom, le) },
                                              { 0x10a9U, words(color, le) } } };
        std::vector<std::byte> make_model;
        for (char c : std::string_view("Canon"))
            make_model.push_back(std::byte(c));
        make_model.push_back(std::byte { 0 });
        for (char c : model)
            make_model.push_back(std::byte(c));
        make_model.push_back(std::byte { 0 });
        const std::array<Field, 1> identity { { { 0x080aU, make_model } } };
        const std::array<Field, 2> root { { { 0x300bU, directory(native, le) },
                                            { 0x2807U,
                                              directory(identity, le) } } };
        std::vector<std::byte> out;
        out.push_back(std::byte(le ? 'I' : 'M'));
        out.push_back(std::byte(le ? 'I' : 'M'));
        dword(out, 14U, le);
        for (char c : std::string_view("HEAPCCDR"))
            out.push_back(std::byte(c));
        const auto data = directory(root, le);
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }
    struct Source final {
        std::span<const std::byte> bytes;
    };
    static RandomAccessIoResult read_at(void* context, uint64_t offset,
                                        std::span<std::byte> out) noexcept
    {
        Source* source = static_cast<Source*>(context);
        if (offset > source->bytes.size()
            || out.size() > source->bytes.size() - offset)
            return { RandomAccessIoCode::Ok, 0U };
        std::memcpy(out.data(), source->bytes.data() + offset, out.size());
        return { RandomAccessIoCode::Ok, out.size() };
    }
    static ExifDecodeResult decode(std::span<const std::byte> bytes,
                                   MetaStore& store, bool callback,
                                   bool vendor_tables = true)
    {
        ExifDecodeResult result;
        if (!callback) {
            (void)ciff_internal::decode_crw_ciff(bytes, store, {}, &result,
                                                 vendor_tables);
            return result;
        }
        Source state { bytes };
        RandomAccessSource input;
        input.size    = bytes.size();
        input.context = &state;
        input.read_at = read_at;
        std::array<std::byte, 64> window {};
        std::array<std::byte, 1024> value {};
        ExifRandomAccessScratch scratch;
        scratch.read_window                       = window;
        scratch.value                             = value;
        scratch.window_options.minimum_read_bytes = 0U;
        const auto decoded = ciff_internal::decode_crw_ciff_random_access(
            make_random_access_source_range(input), store, scratch, {}, {},
            vendor_tables);
        EXPECT_TRUE(decoded.complete());
        return decoded.decode;
    }
    static const Entry* find(const MetaStore& store, std::string_view ifd,
                             uint16_t tag)
    {
        const auto ids = store.find_all(make_exif_tag_key_view(ifd, tag));
        return ids.size() == 1U ? &store.entry(ids[0]) : nullptr;
    }
}  // namespace
TEST(CiffRawGaps, CanonTablesFollowNativeWordsAndLateModel)
{
    for (bool callback : { false, true })
        for (bool le : { false, true })
            for (std::string_view model : { "EOS D60", "EOS 10D" }) {
                MetaStore store;
                EXPECT_EQ(decode(fixture(model, le), store, callback).status,
                          ExifDecodeStatus::Ok);
                store.finalize();
                const Entry* macro = find(store, "mk_canon_camerasettings_0",
                                          1U);
                ASSERT_NE(macro, nullptr);
                EXPECT_EQ(macro->value.elem_type, MetaElementType::I16);
                EXPECT_EQ(macro->value.data.i64, 2);
                const Entry* contrast = find(store, "mk_canon_camerasettings_0",
                                             13U);
                ASSERT_NE(contrast, nullptr);
                EXPECT_EQ(contrast->value.data.i64, -7);
                const Entry* lens = find(store, "mk_canon_camerasettings_0",
                                         22U);
                ASSERT_NE(lens, nullptr);
                EXPECT_EQ(lens->value.elem_type, MetaElementType::U16);
                EXPECT_EQ(lens->value.data.u64, 65535U);
                const Entry* slow = find(store, "mk_canon_shotinfo_0", 8U);
                ASSERT_NE(slow, nullptr);
                EXPECT_EQ(slow->value.elem_type, MetaElementType::I16);
                EXPECT_EQ(slow->value.data.i64, -1);
                EXPECT_EQ(find(store, "ciff_300B_0_shotinfo", 8U), nullptr);
                const Entry* distance = find(store, "mk_canon_shotinfo_0", 19U);
                ASSERT_NE(distance, nullptr);
                EXPECT_EQ(distance->value.elem_type, MetaElementType::U16);
                EXPECT_EQ(distance->value.data.u64, 65535U);
                const Entry* x     = find(store, "mk_canon_afinfo_0", 8U);
                const Entry* y     = find(store, "mk_canon_afinfo_0", 9U);
                const Entry* focus = find(store, "mk_canon_afinfo_0", 10U);
                ASSERT_NE(x, nullptr);
                ASSERT_NE(y, nullptr);
                ASSERT_NE(focus, nullptr);
                EXPECT_EQ(x->value.elem_type, MetaElementType::I16);
                EXPECT_EQ(x->value.count, 7U);
                std::array<int16_t, 7> positions {};
                auto raw_positions = store.arena().span(x->value.data.span);
                ASSERT_EQ(raw_positions.size(), sizeof(positions));
                std::memcpy(positions.data(), raw_positions.data(),
                            raw_positions.size());
                EXPECT_EQ(positions[0], -7);
                raw_positions = store.arena().span(y->value.data.span);
                std::memcpy(positions.data(), raw_positions.data(),
                            raw_positions.size());
                EXPECT_EQ(positions[0], 9);
                int16_t mask        = 0;
                const auto raw_mask = store.arena().span(
                    focus->value.data.span);
                ASSERT_EQ(raw_mask.size(), sizeof(mask));
                std::memcpy(&mask, raw_mask.data(), sizeof(mask));
                EXPECT_EQ(mask, 0x0041);
                const Entry* jpg = find(store, "ciff_300B_0_rawjpginfo", 3U);
                ASSERT_NE(jpg, nullptr);
                EXPECT_EQ(jpg->value.data.u64, 1600U);
                EXPECT_NE(jpg->origin.block, x->origin.block);
                const Entry* sensor = find(store, "mk_canon_sensorinfo_0", 1U);
                ASSERT_NE(sensor, nullptr);
                EXPECT_EQ(sensor->value.elem_type, MetaElementType::I16);
                EXPECT_EQ(sensor->value.data.i64, 3216);
                const auto custom  = model == "EOS D60"
                                         ? "mk_canoncustom_functionsd30_0"
                                         : "mk_canoncustom_functions10d_0";
                const Entry* field = find(store, custom, 2U);
                ASSERT_NE(field, nullptr);
                EXPECT_EQ(field->value.elem_type, MetaElementType::U8);
                EXPECT_EQ(field->value.data.u64, 1U);
                const Entry* levels = find(store, "mk_canon_colorbalance_0",
                                           29U);
                ASSERT_NE(levels, nullptr);
                EXPECT_EQ(levels->value.elem_type, MetaElementType::I16);
                std::array<int16_t, 4> actual {};
                const auto raw = store.arena().span(levels->value.data.span);
                ASSERT_EQ(raw.size(), sizeof(actual));
                std::memcpy(actual.data(), raw.data(), raw.size());
                const std::array<int16_t, 4> expected { -1, -2, -3, -4 };
                EXPECT_EQ(actual, expected);
                if (model == "EOS D60")
                    for (auto policy : { ExifTagNamePolicy::Canonical,
                                         ExifTagNamePolicy::ExifToolCompat })
                        EXPECT_EQ(exif_entry_name(store, *levels, policy),
                                  "BlackLevels");
            }
}
TEST(CiffRawGaps, DisablingVendorTablesKeepsRawParents)
{
    for (bool callback : { false, true }) {
        MetaStore store;
        EXPECT_EQ(
            decode(fixture("EOS D60", true), store, callback, false).status,
            ExifDecodeStatus::Ok);
        store.finalize();
        EXPECT_NE(find(store, "ciff_300B_0", 0x102dU), nullptr);
        EXPECT_NE(find(store, "ciff_300B_0", 0x1038U), nullptr);
        EXPECT_EQ(find(store, "mk_canon_camerasettings_0", 1U), nullptr);
        EXPECT_EQ(find(store, "mk_canon_afinfo_0", 8U), nullptr);
        EXPECT_EQ(find(store, "ciff_300B_0_rawjpginfo", 3U), nullptr);
    }
}
TEST(CiffRawGaps, SerialAfFieldsRespectBoundsAndKnownCounts)
{
    for (bool le : { false, true }) {
        std::array<uint16_t, 23> native {};
        native[0] = 7U;
        auto raw  = words(native, le);
        for (int control = 0; control < 3; ++control) {
            auto input = raw;
            ExifDecodeLimits limits;
            ExifDecodeStatus expected = ExifDecodeStatus::Ok;
            if (control == 0) {
                input.resize(input.size() - 2U);
                expected = ExifDecodeStatus::Malformed;
            } else if (control == 1) {
                input = words(std::array<uint16_t, 23> { 8U }, le);
            } else {
                limits.max_entries_per_ifd = 10U;
                expected                   = ExifDecodeStatus::LimitExceeded;
            }
            MetaStore store;
            ExifDecodeResult result;
            exif_internal::decode_canon_ciff_binary_table(input, le, 0x1038U,
                                                          0U, "EOS 10D", store,
                                                          limits, &result);
            EXPECT_EQ(result.status, expected);
            EXPECT_TRUE(store.entries().empty());
        }
        MetaStore store;
        ExifDecodeResult result;
        native[0] = 9U;
        std::array<uint16_t, 28> powershot {};
        powershot[0]      = 9U;
        powershot[27]     = 5U;
        const auto serial = words(powershot, le);
        exif_internal::decode_canon_ciff_binary_table(serial, le, 0x1038U, 0U,
                                                      "Canon PowerShot S60",
                                                      store, {}, &result);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        store.finalize();
        const Entry* primary = find(store, "mk_canon_afinfo_0", 11U);
        ASSERT_NE(primary, nullptr);
        EXPECT_EQ(primary->value.data.u64, 5U);
    }
}
TEST(CiffRawGaps, InvalidCustomLengthKeepsRawParent)
{
    for (bool callback : { false, true }) {
        MetaStore store;
        EXPECT_EQ(decode(fixture("EOS D60", true, true), store, callback).status,
                  ExifDecodeStatus::Malformed);
        store.finalize();
        EXPECT_NE(find(store, "ciff_300B_0", 0x1033U), nullptr);
        EXPECT_EQ(find(store, "mk_canoncustom_functionsd30_0", 2U), nullptr);
    }
}

TEST(CiffRawGaps, AppendingDecodeDoesNotReprojectEarlierParents)
{
    for (bool callback : { false, true }) {
        MetaStore store;
        EXPECT_EQ(decode(fixture("EOS D60", false), store, callback).status,
                  ExifDecodeStatus::Ok);
        const size_t first_count = store.entries().size();
        EXPECT_EQ(decode(fixture("EOS 10D", true), store, callback).status,
                  ExifDecodeStatus::Ok);
        EXPECT_EQ(store.entries().size(), first_count * 2U);
        store.finalize();
        const auto fields = store.find_all(
            make_exif_tag_key_view("mk_canon_colorbalance_0", 29U));
        ASSERT_EQ(fields.size(), 2U);
        EXPECT_EQ(exif_entry_name(store, store.entry(fields[0]),
                                  ExifTagNamePolicy::Canonical),
                  "BlackLevels");
        EXPECT_NE(exif_entry_name(store, store.entry(fields[1]),
                                  ExifTagNamePolicy::Canonical),
                  "BlackLevels");
    }
}

TEST(CiffRawGaps, SerialAfThirtySixWordsPreserveUnknownArrayAndPrimary)
{
    for (bool le : { false, true }) {
        std::array<uint16_t, 36> native {};
        native[0] = 9U;
        for (size_t i = 0U; i < 8U; ++i)
            native[27U + i] = uint16_t(0x8000U + i);
        native[35] = 6U;
        MetaStore store;
        ExifDecodeResult result;
        exif_internal::decode_canon_ciff_binary_table(words(native, le), le,
                                                      0x1038U, 0U,
                                                      "Canon PowerShot S60",
                                                      store, {}, &result);
        EXPECT_EQ(result.status, ExifDecodeStatus::Ok);
        store.finalize();
        const Entry* unknown = find(store, "mk_canon_afinfo_0", 11U);
        ASSERT_NE(unknown, nullptr);
        EXPECT_EQ(unknown->value.kind, MetaValueKind::Array);
        EXPECT_EQ(unknown->value.elem_type, MetaElementType::U16);
        EXPECT_EQ(unknown->value.count, 8U);
        EXPECT_EQ(exif_entry_name(store, *unknown, ExifTagNamePolicy::Canonical),
                  "Canon_AFInfo_0x000b");
        const auto bytes = store.arena().span(unknown->value.data.span);
        std::array<uint16_t, 8> actual {};
        ASSERT_EQ(bytes.size(), sizeof(actual));
        std::memcpy(actual.data(), bytes.data(), bytes.size());
        EXPECT_EQ(actual.front(), 0x8000U);
        EXPECT_EQ(actual.back(), 0x8007U);
        const Entry* primary = find(store, "mk_canon_afinfo_0", 12U);
        ASSERT_NE(primary, nullptr);
        EXPECT_EQ(primary->value.data.u64, 6U);
        EXPECT_EQ(exif_entry_name(store, *primary, ExifTagNamePolicy::Canonical),
                  "PrimaryAFPoint");
    }
}
}  // namespace openmeta
