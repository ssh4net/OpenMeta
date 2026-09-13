// SPDX-License-Identifier: Apache-2.0

#include "openmeta/container_scan.h"
#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {
using Bytes                             = std::vector<std::byte>;
constexpr std::array<uint8_t, 16> kExif = { 0x4a, 0x70, 0x67, 0x54, 0x69, 0x66,
                                            0x66, 0x45, 0x78, 0x69, 0x66, 0x2d,
                                            0x3e, 0x4a, 0x50, 0x32 };
constexpr std::array<uint8_t, 16> kXmp  = { 0xbe, 0x7a, 0xcf, 0xcb, 0x97, 0xa9,
                                            0x42, 0xe8, 0x9c, 0x71, 0x99, 0x94,
                                            0x91, 0xe3, 0xaf, 0xac };
constexpr std::array<uint8_t, 16> kIptc = { 0x33, 0xc7, 0xa4, 0xd2, 0xb8, 0x1d,
                                            0x47, 0x23, 0xa0, 0xba, 0xf1, 0xa3,
                                            0xe0, 0x97, 0xad, 0x38 };
constexpr std::array<uint8_t, 16> kGeo  = { 0xb1, 0x4b, 0xf8, 0xbd, 0x08, 0x3d,
                                            0x4b, 0x43, 0xa5, 0xae, 0x8c, 0xd7,
                                            0xd5, 0xa6, 0xce, 0x03 };

static Bytes
bytes(std::string_view value)
{
    const auto* first = reinterpret_cast<const std::byte*>(value.data());
    return Bytes(first, first + value.size());
}

static void
append(Bytes* out, std::span<const std::byte> value)
{
    out->insert(out->end(), value.begin(), value.end());
}

static void
integer(Bytes* out, uint64_t value, unsigned width)
{
    for (unsigned i = width; i != 0; --i) {
        out->push_back(static_cast<std::byte>(value >> ((i - 1) * 8)));
    }
}

enum class SizeForm { Ordinary, Extended, ToEnd };

static Bytes
box(std::string_view type, const Bytes& payload,
    SizeForm form = SizeForm::Ordinary)
{
    Bytes out;
    integer(&out,
            form == SizeForm::Extended ? 1
            : form == SizeForm::ToEnd  ? 0
                                       : 8 + payload.size(),
            4);
    append(&out, bytes(type));
    if (form == SizeForm::Extended) {
        integer(&out, 16 + payload.size(), 8);
    }
    append(&out, payload);
    return out;
}

static Bytes
uuid(const std::array<uint8_t, 16>& id, std::string_view payload,
     SizeForm form = SizeForm::Ordinary)
{
    Bytes out;
    for (uint8_t value : id) {
        out.push_back(static_cast<std::byte>(value));
    }
    append(&out, bytes(payload));
    return box("uuid", out, form);
}

static Bytes
file(std::string_view brand)
{
    Bytes out  = box("jP  ", bytes(std::string_view("\r\n\x87\n", 4)));
    Bytes ftyp = bytes(brand);
    integer(&ftyp, 0, 4);
    append(&ftyp, bytes(brand));
    append(&out, box("ftyp", ftyp));
    return out;
}

static openmeta::ExecutePreparedTransferResult
rewrite(const Bytes& input, bool exif, bool xmp)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jp2;
    if (exif) {
        openmeta::PreparedTransferBlock block;
        block.route    = "jp2:box-exif";
        block.box_type = { 'E', 'x', 'i', 'f' };
        block.payload  = bytes("new-exif");
        bundle.blocks.push_back(block);
    }
    if (xmp) {
        openmeta::PreparedTransferBlock block;
        block.route    = "jp2:box-xml";
        block.box_type = { 'x', 'm', 'l', ' ' };
        block.payload  = bytes("new-xmp");
        bundle.blocks.push_back(block);
    }
    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;
    return openmeta::execute_prepared_transfer(&bundle, input, options);
}

TEST(Jp2Rewrite, ReplacesSelectedLiteralAndUuidCarriersOnly)
{
    for (std::string_view brand : { "jp2 ", "jph " }) {
        for (unsigned selected = 1; selected <= 3; ++selected) {
            SCOPED_TRACE(brand);
            SCOPED_TRACE(selected);
            const bool exif = (selected & 1) != 0;
            const bool xmp  = (selected & 2) != 0;
            Bytes input     = file(brand);
            Bytes expected  = input;
            const std::array<Bytes, 4> old
                = { box("Exif", bytes("old-exif")),
                    uuid(kExif, "old-exif-uuid"), box("xml ", bytes("old-xmp")),
                    uuid(kXmp, "old-xmp-uuid", SizeForm::Extended) };
            for (size_t i = 0; i < old.size(); ++i) {
                append(&input, old[i]);
                if (i < 2 ? !exif : !xmp) {
                    append(&expected, old[i]);
                }
            }
            std::array<uint8_t, 16> unknown = kXmp;
            unknown.back() ^= 1;
            const std::array<Bytes, 5> preserved
                = { uuid(unknown, "unknown-uuid"), uuid(kIptc, "iptc"),
                    uuid(kGeo, "geotiff"), box("free", bytes("keep-padding")),
                    box("jp2c", bytes("keep-codestream")) };
            for (const Bytes& keep : preserved) {
                append(&input, keep);
                append(&expected, keep);
            }
            if (exif) {
                append(&expected, box("Exif", bytes("new-exif")));
            }
            if (xmp) {
                append(&expected, box("xml ", bytes("new-xmp")));
            }
            const auto result = rewrite(input, exif, xmp);
            ASSERT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
            ASSERT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
            EXPECT_EQ(result.edited_output, expected);
        }
    }
}

TEST(Jp2Rewrite, KeepsPreservedZeroLengthBoxLast)
{
    const std::array<Bytes, 3> terminals
        = { box("jp2c", bytes("codestream"), SizeForm::ToEnd),
            uuid(kIptc, "iptc", SizeForm::ToEnd),
            uuid(kExif, "unselected-exif", SizeForm::ToEnd) };
    for (const Bytes& terminal : terminals) {
        Bytes input    = file("jph ");
        Bytes expected = input;
        append(&input, terminal);
        append(&expected, box("xml ", bytes("new-xmp")));
        append(&expected, terminal);
        const auto result = rewrite(input, false, true);
        ASSERT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
        EXPECT_EQ(result.edited_output, expected);
    }
}

TEST(Jp2Rewrite, ReplacesSelectedZeroLengthUuidBox)
{
    Bytes input    = file("jp2 ");
    Bytes expected = input;
    append(&input, uuid(kXmp, "old-xmp", SizeForm::ToEnd));
    append(&expected, box("xml ", bytes("new-xmp")));
    const auto result = rewrite(input, false, true);
    ASSERT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edited_output, expected);
}

TEST(Jp2Rewrite, RejectsMalformedBoxSizesWithoutOutput)
{
    std::vector<Bytes> malformed = { box("uuid", Bytes(15)),
                                     box("uuid", Bytes(15), SizeForm::Extended),
                                     bytes("short") };
    for (uint64_t size :
         { uint64_t(15), uint64_t(32), std::numeric_limits<uint64_t>::max() }) {
        Bytes extended;
        integer(&extended, 1, 4);
        append(&extended, bytes("jp2c"));
        integer(&extended, size, 8);
        malformed.push_back(extended);
    }
    for (const Bytes& tail : malformed) {
        Bytes input = file("jp2 ");
        append(&input, tail);
        const Bytes before = input;
        const auto result  = rewrite(input, true, true);
        EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Malformed);
        EXPECT_TRUE(result.edited_output.empty());
        EXPECT_EQ(input, before);
    }
}

struct BoundedSource {
    const Bytes* input;
    uint64_t forbidden_begin;
    uint64_t forbidden_end;
};

static openmeta::RandomAccessIoResult
read_at(void* context, uint64_t offset,
        std::span<std::byte> destination) noexcept
{
    const auto& source = *static_cast<const BoundedSource*>(context);
    if (offset > source.input->size()
        || destination.size() > source.input->size() - offset
        || (offset < source.forbidden_end
            && offset + destination.size() > source.forbidden_begin)) {
        return { openmeta::RandomAccessIoCode::IoError, 0 };
    }
    std::memcpy(destination.data(), source.input->data() + offset,
                destination.size());
    return { openmeta::RandomAccessIoCode::Ok, destination.size() };
}

TEST(Jp2Rewrite, PositionalUuidScanMatchesAndSkipsCodestream)
{
    for (std::string_view brand : { "jp2 ", "jph " }) {
        Bytes input = file(brand);
        append(&input, uuid(kExif, "IIExif", SizeForm::Extended));
        const uint64_t codestream_begin = input.size() + 8;
        append(&input, box("jp2c", Bytes(1024 * 1024, std::byte { 0x55 })));
        const uint64_t codestream_end = input.size();
        append(&input, uuid(kXmp, "<xmp/>", SizeForm::ToEnd));
        BoundedSource context { &input, codestream_begin, codestream_end };
        openmeta::RandomAccessSourceRange range;
        range.source
            = openmeta::make_callback_random_access_source(input.size(),
                                                           &context, read_at);
        range.size = input.size();
        std::array<std::byte, 32> window;
        openmeta::ContainerRandomAccessScratch scratch;
        scratch.read_window                       = window;
        scratch.window_options.minimum_read_bytes = 0;
        std::array<openmeta::ContainerBlockRef, 4> contiguous, positional;
        const auto a = openmeta::scan_jp2(input, contiguous);
        const auto b = openmeta::scan_jp2_random_access(range, positional,
                                                        scratch,
                                                        { 128, 512, 32 });
        ASSERT_EQ(a.status, openmeta::ScanStatus::Ok);
        ASSERT_EQ(a.written, 2U);
        ASSERT_TRUE(b.complete()) << static_cast<unsigned>(b.input.code)
                                  << " at " << b.input.failure_offset;
        ASSERT_EQ(b.scan.status, a.status);
        ASSERT_EQ(b.scan.written, a.written);
        EXPECT_LE(b.input.bytes_requested, 512U);
        for (uint32_t i = 0; i < a.written; ++i) {
            EXPECT_EQ(positional[i].kind, contiguous[i].kind);
            EXPECT_EQ(positional[i].format, contiguous[i].format);
            EXPECT_EQ(positional[i].outer_offset, contiguous[i].outer_offset);
            EXPECT_EQ(positional[i].outer_size, contiguous[i].outer_size);
            EXPECT_EQ(positional[i].data_offset, contiguous[i].data_offset);
            EXPECT_EQ(positional[i].data_size, contiguous[i].data_size);
        }
    }
}
}  // namespace
