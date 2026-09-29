// SPDX-License-Identifier: Apache-2.0

#include "openmeta/simple_meta.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace openmeta {
namespace {

    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
    }

    static void append_u32be(std::vector<std::byte>* out, uint32_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
    }

    static void append_u64be(std::vector<std::byte>* out, uint64_t v)
    {
        ASSERT_NE(out, nullptr);
        for (uint32_t shift = 56U;; shift -= 8U) {
            out->push_back(std::byte { static_cast<uint8_t>(v >> shift) });
            if (shift == 0U) {
                break;
            }
        }
    }

    static void write_u32be(std::vector<std::byte>* out, size_t offset,
                            uint32_t v)
    {
        ASSERT_NE(out, nullptr);
        ASSERT_LE(offset, out->size());
        ASSERT_GE(out->size() - offset, 4U);
        (*out)[offset + 0U] = std::byte { static_cast<uint8_t>(v >> 24U) };
        (*out)[offset + 1U] = std::byte { static_cast<uint8_t>(v >> 16U) };
        (*out)[offset + 2U] = std::byte { static_cast<uint8_t>(v >> 8U) };
        (*out)[offset + 3U] = std::byte { static_cast<uint8_t>(v) };
    }

    static void write_u64be(std::vector<std::byte>* out, size_t offset,
                            uint64_t v)
    {
        ASSERT_NE(out, nullptr);
        ASSERT_LE(offset, out->size());
        ASSERT_GE(out->size() - offset, 8U);
        for (size_t i = 0U; i < 8U; ++i) {
            const uint32_t shift = static_cast<uint32_t>((7U - i) * 8U);
            (*out)[offset + i] = std::byte { static_cast<uint8_t>(v >> shift) };
        }
    }

    static void append_u16le(std::vector<std::byte>* out, uint16_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
    }

    static void append_u32le(std::vector<std::byte>* out, uint32_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFU) });
    }

    static void append_fourcc(std::vector<std::byte>* out, uint32_t v)
    {
        append_u32be(out, v);
    }

    static void append_bytes(std::vector<std::byte>* out, const char* s)
    {
        ASSERT_NE(out, nullptr);
        ASSERT_NE(s, nullptr);
        for (size_t i = 0; s[i] != '\0'; ++i) {
            out->push_back(std::byte { static_cast<uint8_t>(s[i]) });
        }
    }

    static void append_fullbox_header(std::vector<std::byte>* out,
                                      uint8_t version)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { version });
        out->push_back(std::byte { 0 });
        out->push_back(std::byte { 0 });
        out->push_back(std::byte { 0 });
    }

    static void append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                                std::span<const std::byte> payload)
    {
        ASSERT_NE(out, nullptr);
        append_u32be(out, static_cast<uint32_t>(8U + payload.size()));
        append_fourcc(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static uint64_t
    append_bmff_box_size_form(std::vector<std::byte>* out, uint32_t type,
                              std::span<const std::byte> payload,
                              bool extended_size)
    {
        const uint64_t start = static_cast<uint64_t>(out->size());
        if (extended_size) {
            append_u32be(out, 1U);
            append_fourcc(out, type);
            append_u64be(out, 16U + static_cast<uint64_t>(payload.size()));
        } else {
            append_bmff_box(out, type, payload);
        }
        if (extended_size) {
            out->insert(out->end(), payload.begin(), payload.end());
        }
        return static_cast<uint64_t>(out->size()) - start;
    }

    static MetaKeyView exif_key(std::string_view ifd, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }

    static std::vector<std::byte> make_tiff_ifd0_imagewidth_u32(uint32_t width)
    {
        // Classic TIFF LE header + IFD0 with a single ImageWidth entry.
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42U);
        append_u32le(&tiff, 8U);  // ifd0

        append_u16le(&tiff, 1U);       // entry count
        append_u16le(&tiff, 0x0100U);  // ImageWidth
        append_u16le(&tiff, 4U);       // LONG
        append_u32le(&tiff, 1U);
        append_u32le(&tiff, width);
        append_u32le(&tiff, 0U);  // next IFD
        return tiff;
    }

    static std::vector<std::byte>
    make_bmff_exif_item_with_preamble(std::span<const std::byte> tiff_bytes)
    {
        // ISO-BMFF Exif item: u32be offset to TIFF header after this field.
        // Common layout: offset=6 + "Exif\0\0" + TIFF.
        std::vector<std::byte> exif;
        append_u32be(&exif, 6U);
        append_bytes(&exif, "Exif");
        exif.push_back(std::byte { 0 });
        exif.push_back(std::byte { 0 });
        exif.insert(exif.end(), tiff_bytes.begin(), tiff_bytes.end());
        return exif;
    }

    static std::vector<std::byte>
    make_cr3_canon_record(uint32_t id, std::span<const std::byte> payload)
    {
        std::vector<std::byte> file;
        std::vector<std::byte> ftyp;
        append_fourcc(&ftyp, fourcc('c', 'r', 'x', ' '));
        append_u32be(&ftyp, 0U);
        append_fourcc(&ftyp, fourcc('i', 's', 'o', 'm'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp);
        const std::array<std::byte, 16> canon_uuid = {
            std::byte { 0x85 }, std::byte { 0xc0 }, std::byte { 0xb6 },
            std::byte { 0x87 }, std::byte { 0x82 }, std::byte { 0x0f },
            std::byte { 0x11 }, std::byte { 0xe0 }, std::byte { 0x81 },
            std::byte { 0x11 }, std::byte { 0xf4 }, std::byte { 0xce },
            std::byte { 0x46 }, std::byte { 0x2b }, std::byte { 0x6a },
            std::byte { 0x48 },
        };
        std::vector<std::byte> uuid_payload(canon_uuid.begin(),
                                            canon_uuid.end());
        append_bmff_box(&uuid_payload, id, payload);
        std::vector<std::byte> moov;
        append_bmff_box(&moov, fourcc('u', 'u', 'i', 'd'), uuid_payload);
        append_bmff_box(&file, fourcc('m', 'o', 'o', 'v'), moov);
        return file;
    }

    static void append_cr3_canon_record(std::vector<std::byte>* file,
                                        uint32_t id,
                                        std::span<const std::byte> payload)
    {
        ASSERT_NE(file, nullptr);
        const std::array<std::byte, 16> canon_uuid = {
            std::byte { 0x85 }, std::byte { 0xc0 }, std::byte { 0xb6 },
            std::byte { 0x87 }, std::byte { 0x82 }, std::byte { 0x0f },
            std::byte { 0x11 }, std::byte { 0xe0 }, std::byte { 0x81 },
            std::byte { 0x11 }, std::byte { 0xf4 }, std::byte { 0xce },
            std::byte { 0x46 }, std::byte { 0x2b }, std::byte { 0x6a },
            std::byte { 0x48 },
        };
        std::vector<std::byte> uuid_payload(canon_uuid.begin(),
                                            canon_uuid.end());
        append_bmff_box(&uuid_payload, id, payload);
        std::vector<std::byte> moov;
        append_bmff_box(&moov, fourcc('u', 'u', 'i', 'd'), uuid_payload);
        append_bmff_box(file, fourcc('m', 'o', 'o', 'v'), moov);
    }

    struct Cr3CtboFixture final {
        std::vector<std::byte> file;
        std::array<uint64_t, 3U> offsets {};
        std::array<uint64_t, 3U> sizes {};
        uint64_t ctbo_payload_offset = 0U;
    };

    static Cr3CtboFixture
    make_cr3_ctbo_fixture(uint32_t row_count           = 3U,
                          bool printable_unknown_first = false,
                          bool extended_targets        = false,
                          uint32_t major_brand = fourcc('c', 'r', 'x', ' '))
    {
        static constexpr std::array<std::byte, 16U> xmp_uuid = {
            std::byte { 0xBE }, std::byte { 0x7A }, std::byte { 0xCF },
            std::byte { 0xCB }, std::byte { 0x97 }, std::byte { 0xA9 },
            std::byte { 0x42 }, std::byte { 0xE8 }, std::byte { 0x9C },
            std::byte { 0x71 }, std::byte { 0x99 }, std::byte { 0x94 },
            std::byte { 0x91 }, std::byte { 0xE3 }, std::byte { 0xAF },
            std::byte { 0xAC },
        };
        static constexpr std::array<std::byte, 16U> preview_uuid = {
            std::byte { 0xEA }, std::byte { 0xF4 }, std::byte { 0x2B },
            std::byte { 0x5E }, std::byte { 0x1C }, std::byte { 0x98 },
            std::byte { 0x4B }, std::byte { 0x88 }, std::byte { 0xB9 },
            std::byte { 0xFB }, std::byte { 0xB7 }, std::byte { 0xDC },
            std::byte { 0x40 }, std::byte { 0x6E }, std::byte { 0x4D },
            std::byte { 0x16 },
        };

        Cr3CtboFixture fixture;
        std::vector<std::byte> ftyp;
        append_fourcc(&ftyp, major_brand);
        append_u32be(&ftyp, 0U);
        append_fourcc(&ftyp, fourcc('i', 's', 'o', 'm'));
        append_bmff_box(&fixture.file, fourcc('f', 't', 'y', 'p'), ftyp);

        const std::array<std::byte, 1U> xmp_payload = { std::byte { 'x' } };
        std::vector<std::byte> xmp_box_payload(xmp_uuid.begin(),
                                               xmp_uuid.end());
        xmp_box_payload.insert(xmp_box_payload.end(), xmp_payload.begin(),
                               xmp_payload.end());
        fixture.offsets[0] = static_cast<uint64_t>(fixture.file.size());
        fixture.sizes[0]   = append_bmff_box_size_form(&fixture.file,
                                                       fourcc('u', 'u', 'i', 'd'),
                                                       xmp_box_payload,
                                                       extended_targets);

        const std::array<std::byte, 2U> preview_payload = {
            std::byte { 0xFF },
            std::byte { 0xD8 },
        };
        std::vector<std::byte> preview_box_payload(preview_uuid.begin(),
                                                   preview_uuid.end());
        preview_box_payload.insert(preview_box_payload.end(),
                                   preview_payload.begin(),
                                   preview_payload.end());
        fixture.offsets[1] = static_cast<uint64_t>(fixture.file.size());
        fixture.sizes[1]   = append_bmff_box_size_form(&fixture.file,
                                                       fourcc('u', 'u', 'i', 'd'),
                                                       preview_box_payload,
                                                       extended_targets);

        const std::array<std::byte, 4U> media_payload = {
            std::byte { 0x10 },
            std::byte { 0x20 },
            std::byte { 0x30 },
            std::byte { 0x40 },
        };
        fixture.offsets[2] = static_cast<uint64_t>(fixture.file.size());
        fixture.sizes[2]   = append_bmff_box_size_form(&fixture.file,
                                                       fourcc('m', 'd', 'a', 't'),
                                                       media_payload,
                                                       extended_targets);

        std::vector<std::byte> ctbo;
        append_u32be(&ctbo, row_count);
        for (uint32_t i = 0U; i < row_count; ++i) {
            uint32_t id = 0U;
            if (printable_unknown_first && i == 0U) {
                id = fourcc('f', 'r', 'e', 'e');
            } else if (printable_unknown_first && i <= 3U) {
                id = i;
            } else if (!printable_unknown_first && i < 3U) {
                id = i + 1U;
            } else {
                id = 4U + i;
            }
            append_u32be(&ctbo, id);
            const bool known_id   = id >= 1U && id <= 3U;
            const uint64_t offset = known_id ? fixture.offsets[id - 1U] : 0U;
            const uint64_t size   = known_id ? fixture.sizes[id - 1U] : 0U;
            append_u64be(&ctbo, offset);
            append_u64be(&ctbo, size);
        }
        fixture.ctbo_payload_offset = static_cast<uint64_t>(fixture.file.size())
                                      + 40U;
        append_cr3_canon_record(&fixture.file, fourcc('C', 'T', 'B', 'O'),
                                ctbo);
        return fixture;
    }

    static SimpleMetaResult
    read_cr3_ctbo_fixture(std::span<const std::byte> file, MetaStore& store,
                          bool decode_makernote = true)
    {
        std::array<ContainerBlockRef, 32U> blocks {};
        std::array<ExifIfdRef, 32U> ifds {};
        std::array<std::byte, 1024U> scratch_payload {};
        std::array<uint32_t, 64U> scratch_indices {};
        ExifDecodeOptions options;
        options.decode_makernote = decode_makernote;
        const SimpleMetaResult result
            = simple_meta_read(file, store, blocks, ifds, scratch_payload,
                               scratch_indices, options, PayloadOptions {});
        store.finalize();
        return result;
    }

    static uint32_t
    cr3_ctbo_disabled_block_count(std::span<const std::byte> file)
    {
        MetaStore store;
        (void)read_cr3_ctbo_fixture(file, store, false);
        return store.block_count();
    }

    static size_t cr3_ctbo_entry_count(const MetaStore& store)
    {
        static constexpr std::array<std::string_view, 6U> fields = {
            "cr3.ctbo.xmp.offset",     "cr3.ctbo.xmp.size",
            "cr3.ctbo.preview.offset", "cr3.ctbo.preview.size",
            "cr3.ctbo.media.offset",   "cr3.ctbo.media.size",
        };
        size_t count = 0U;
        for (std::string_view field : fields) {
            MetaKeyView key;
            key.kind                  = MetaKeyKind::BmffField;
            key.data.bmff_field.field = field;
            count += store.find_all(key).size();
        }
        return count;
    }

}  // namespace

TEST(SimpleMetaRead, Cr3CanonMetadataCarriersRemainBounded)
{
    const uint32_t ids[] = {
        fourcc('C', 'M', 'T', '1'), fourcc('C', 'M', 'T', '2'),
        fourcc('C', 'M', 'T', '3'), fourcc('C', 'M', 'T', '4'),
        fourcc('C', 'C', 'T', 'P'), fourcc('C', 'N', 'C', 'V'),
        fourcc('C', 'N', 'T', 'H'), fourcc('C', 'N', 'O', 'P'),
        fourcc('C', 'N', 'D', 'M'), fourcc('C', 'T', 'B', 'O'),
        fourcc('C', 'M', 'P', '1'),
    };
    for (size_t i = 0U; i < std::size(ids); ++i) {
        SCOPED_TRACE(ids[i]);
        const bool cmt                    = i < 4U;
        std::vector<std::byte> payload    = make_tiff_ifd0_imagewidth_u32(640U);
        const std::vector<std::byte> file = make_cr3_canon_record(ids[i],
                                                                  payload);
        const std::vector<std::byte> original = file;
        std::array<ContainerBlockRef, 8> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 1024> scratch_payload {};
        std::array<uint32_t, 16> scratch {};
        MetaStore store;
        ExifDecodeOptions options;
        options.decode_makernote = true;
        const SimpleMetaResult result
            = simple_meta_read(file, store, blocks, ifds, scratch_payload,
                               scratch, options, PayloadOptions {});
        store.finalize();
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        ASSERT_EQ(result.scan.written, 1U);
        EXPECT_EQ(blocks[0].id, ids[i]);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Cr3);
        EXPECT_EQ(blocks[0].kind, cmt ? ContainerBlockKind::Exif
                                      : ContainerBlockKind::MakerNote);
        ASSERT_EQ(blocks[0].data_size, payload.size());
        ASSERT_LE(blocks[0].data_offset, file.size());
        ASSERT_LE(payload.size(), file.size() - blocks[0].data_offset);
        for (size_t j = 0U; j < payload.size(); ++j) {
            EXPECT_EQ(file[blocks[0].data_offset + j], payload[j]);
        }
        EXPECT_EQ(file, original);
        const std::string_view ifd            = i == 2U ? "mk_canon0" : "ifd0";
        const std::span<const EntryId> values = store.find_all(
            exif_key(ifd, 0x0100U));
        if (cmt) {
            ASSERT_EQ(result.exif.status, ExifDecodeStatus::Ok);
            ASSERT_EQ(values.size(), 1U);
            EXPECT_EQ(store.entry(values[0]).value.data.u64, 640U);
        } else {
            EXPECT_TRUE(values.empty());
            EXPECT_EQ(result.exif.entries_decoded, 0U);
        }
        if (cmt) {
            payload[2] = std::byte { 43 };
            const std::vector<std::byte> nonclassic
                = make_cr3_canon_record(ids[i], payload);
            const ScanResult nonclassic_scan = scan_bmff(nonclassic, blocks);
            EXPECT_EQ(nonclassic_scan.status, ScanStatus::Ok);
            EXPECT_EQ(nonclassic_scan.written, 0U);
            payload.resize(3U);
            const std::vector<std::byte> truncated
                = make_cr3_canon_record(ids[i], payload);
            const ScanResult truncated_scan = scan_bmff(truncated, blocks);
            EXPECT_EQ(truncated_scan.status, ScanStatus::Ok);
            EXPECT_EQ(truncated_scan.written, 0U);
        }
    }
}

TEST(SimpleMetaRead, Cr3Cmt3CanonSettingsRespectDecodeOptionAndBounds)
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42U);
    append_u32le(&tiff, 8U);
    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0001U);
    append_u16le(&tiff, 3U);
    append_u32le(&tiff, 4U);
    append_u32le(&tiff, 26U);
    append_u32le(&tiff, 0U);
    append_u16le(&tiff, 8U);
    append_u16le(&tiff, 11U);
    append_u16le(&tiff, 22U);
    append_u16le(&tiff, 33U);
    for (uint32_t mode = 0U; mode < 3U; ++mode) {
        SCOPED_TRACE(mode);
        std::vector<std::byte> payload = tiff;
        if (mode == 2U) {
            payload.resize(12U);
        }
        const std::vector<std::byte> file
            = make_cr3_canon_record(fourcc('C', 'M', 'T', '3'), payload);
        std::array<ContainerBlockRef, 8> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 1024> scratch_payload {};
        std::array<uint32_t, 16> scratch {};
        MetaStore store;
        ExifDecodeOptions options;
        options.decode_makernote = mode != 1U;
        const SimpleMetaResult result
            = simple_meta_read(file, store, blocks, ifds, scratch_payload,
                               scratch, options, PayloadOptions {});
        store.finalize();
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        ASSERT_EQ(result.scan.written, 1U);
        const std::span<const EntryId> values = store.find_all(
            exif_key("mk_canon_camerasettings_0", 0x0002U));
        if (mode == 0U) {
            ASSERT_EQ(result.exif.status, ExifDecodeStatus::Ok);
            ASSERT_EQ(values.size(), 1U);
            EXPECT_EQ(store.entry(values[0]).value.data.u64, 22U);
            EXPECT_TRUE(any(store.entry(values[0]).flags, EntryFlags::Derived));
        } else {
            EXPECT_TRUE(values.empty());
            EXPECT_EQ(result.exif.entries_decoded, 0U);
            if (mode == 2U) {
                EXPECT_EQ(result.exif.status, ExifDecodeStatus::Malformed);
            }
        }
    }
}

TEST(SimpleMetaRead, Cr3CtboDerivesKnownOffsetsFromValidatedBoxes)
{
    const Cr3CtboFixture fixture          = make_cr3_ctbo_fixture();
    const std::vector<std::byte> original = fixture.file;
    std::array<ContainerBlockRef, 16U> blocks {};
    std::array<ExifIfdRef, 16U> ifds {};
    std::array<std::byte, 1024U> scratch_payload {};
    std::array<uint32_t, 32U> scratch_indices {};
    MetaStore store;
    ExifDecodeOptions options;
    options.decode_makernote = true;
    const SimpleMetaResult result
        = simple_meta_read(fixture.file, store, blocks, ifds, scratch_payload,
                           scratch_indices, options, PayloadOptions {});
    store.finalize();
    ASSERT_EQ(result.scan.status, ScanStatus::Ok);

    bool found_ctbo = false;
    for (uint32_t i = 0U; i < result.scan.written; ++i) {
        found_ctbo = found_ctbo
                     || (blocks[i].format == ContainerFormat::Cr3
                         && blocks[i].kind == ContainerBlockKind::MakerNote
                         && blocks[i].id == fourcc('C', 'T', 'B', 'O'));
    }
    EXPECT_TRUE(found_ctbo);

    const std::array<std::string_view, 6U> fields = {
        "cr3.ctbo.xmp.offset",     "cr3.ctbo.xmp.size",
        "cr3.ctbo.preview.offset", "cr3.ctbo.preview.size",
        "cr3.ctbo.media.offset",   "cr3.ctbo.media.size",
    };
    const std::array<uint64_t, 6U> expected = {
        fixture.offsets[0], fixture.sizes[0],   fixture.offsets[1],
        fixture.sizes[1],   fixture.offsets[2], fixture.sizes[2],
    };
    for (size_t i = 0U; i < fields.size(); ++i) {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::BmffField;
        key.data.bmff_field.field          = fields[i];
        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U) << "field=" << fields[i];
        const Entry& entry = store.entry(ids[0]);
        EXPECT_EQ(entry.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(entry.value.elem_type, MetaElementType::U64);
        EXPECT_EQ(entry.value.count, 1U);
        EXPECT_EQ(entry.value.data.u64, expected[i]);
        EXPECT_TRUE(any(entry.flags, EntryFlags::Derived));
    }

    EXPECT_EQ(fixture.file, original);
    std::vector<std::byte> portable_xmp(4096U);
    const XmpDumpResult dumped = dump_xmp_portable(store, portable_xmp, {});
    ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
    const std::string_view xmp_text(reinterpret_cast<const char*>(
                                        portable_xmp.data()),
                                    static_cast<size_t>(dumped.written));
    EXPECT_EQ(xmp_text.find("cr3.ctbo"), std::string_view::npos);
}

TEST(SimpleMetaRead, Cr3CtboKeepsUnknownRowsOpaqueAndHonorsRowLimit)
{
    const Cr3CtboFixture unknown_first = make_cr3_ctbo_fixture(8U, true);
    MetaStore unknown_store;
    const SimpleMetaResult unknown_result
        = read_cr3_ctbo_fixture(unknown_first.file, unknown_store);
    ASSERT_EQ(unknown_result.scan.status, ScanStatus::Ok);
    EXPECT_EQ(cr3_ctbo_entry_count(unknown_store), 6U);

    const Cr3CtboFixture maximum = make_cr3_ctbo_fixture(64U);
    MetaStore maximum_store;
    const SimpleMetaResult maximum_result
        = read_cr3_ctbo_fixture(maximum.file, maximum_store);
    ASSERT_EQ(maximum_result.scan.status, ScanStatus::Ok);
    EXPECT_EQ(cr3_ctbo_entry_count(maximum_store), 6U);

    const Cr3CtboFixture over_limit = make_cr3_ctbo_fixture(65U);
    MetaStore over_limit_store;
    const SimpleMetaResult over_limit_result
        = read_cr3_ctbo_fixture(over_limit.file, over_limit_store);
    ASSERT_EQ(over_limit_result.scan.status, ScanStatus::Ok);
    EXPECT_EQ(cr3_ctbo_entry_count(over_limit_store), 0U);
    EXPECT_EQ(over_limit_store.block_count(),
              cr3_ctbo_disabled_block_count(over_limit.file));
}

TEST(SimpleMetaRead, Cr3CtboValidatesExtendedTargetBoxes)
{
    const Cr3CtboFixture fixture = make_cr3_ctbo_fixture(3U, false, true);
    MetaStore store;
    const SimpleMetaResult result = read_cr3_ctbo_fixture(fixture.file, store);
    ASSERT_EQ(result.scan.status, ScanStatus::Ok);
    ASSERT_EQ(cr3_ctbo_entry_count(store), 6U);

    static constexpr std::array<std::string_view, 6U> fields = {
        "cr3.ctbo.xmp.offset",     "cr3.ctbo.xmp.size",
        "cr3.ctbo.preview.offset", "cr3.ctbo.preview.size",
        "cr3.ctbo.media.offset",   "cr3.ctbo.media.size",
    };
    const std::array<uint64_t, 6U> expected = {
        fixture.offsets[0], fixture.sizes[0],   fixture.offsets[1],
        fixture.sizes[1],   fixture.offsets[2], fixture.sizes[2],
    };
    for (size_t i = 0U; i < fields.size(); ++i) {
        MetaKeyView key;
        key.kind                           = MetaKeyKind::BmffField;
        key.data.bmff_field.field          = fields[i];
        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, expected[i]);
    }
}

TEST(SimpleMetaRead, Cr3CtboRejectsMalformedTablesWithoutPartialEntries)
{
    const Cr3CtboFixture source = make_cr3_ctbo_fixture();
    const size_t payload_offset = static_cast<size_t>(
        source.ctbo_payload_offset);
    const size_t row0 = payload_offset + 4U;
    const size_t row1 = row0 + 20U;

    {
        std::vector<std::byte> file = source.file;
        write_u32be(&file, payload_offset, 0U);
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
        EXPECT_EQ(store.block_count(), cr3_ctbo_disabled_block_count(file));
    }
    {
        std::vector<std::byte> file = source.file;
        write_u32be(&file, payload_offset, 2U);
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
    }
    {
        std::vector<std::byte> file = source.file;
        write_u32be(&file, row1, 1U);
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
        EXPECT_EQ(store.block_count(), cr3_ctbo_disabled_block_count(file));
    }
    {
        std::vector<std::byte> file = source.file;
        write_u64be(&file, row0 + 4U, UINT64_MAX);
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
    }
    {
        std::vector<std::byte> file = source.file;
        write_u64be(&file, row0 + 12U, UINT64_MAX);
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
    }
    {
        std::vector<std::byte> file = source.file;
        write_u32be(&file, static_cast<size_t>(source.offsets[2] + 4U),
                    fourcc('f', 'r', 'e', 'e'));
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
        EXPECT_EQ(store.block_count(), cr3_ctbo_disabled_block_count(file));
    }
    {
        std::vector<std::byte> file = source.file;
        file[static_cast<size_t>(source.offsets[0] + 8U)] ^= std::byte { 1U };
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
    }
    {
        std::vector<std::byte> file = source.file;
        write_u64be(&file, row0 + 12U, source.sizes[0] + 1U);
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
    }
    {
        static constexpr std::array<std::byte, 16U> xmp_uuid = {
            std::byte { 0xBE }, std::byte { 0x7A }, std::byte { 0xCF },
            std::byte { 0xCB }, std::byte { 0x97 }, std::byte { 0xA9 },
            std::byte { 0x42 }, std::byte { 0xE8 }, std::byte { 0x9C },
            std::byte { 0x71 }, std::byte { 0x99 }, std::byte { 0x94 },
            std::byte { 0x91 }, std::byte { 0xE3 }, std::byte { 0xAF },
            std::byte { 0xAC },
        };
        std::vector<std::byte> target_payload;
        append_u32be(&target_payload, 0U);
        append_fourcc(&target_payload, fourcc('u', 'u', 'i', 'd'));
        target_payload.insert(target_payload.end(), xmp_uuid.begin(),
                              xmp_uuid.end());
        std::vector<std::byte> file  = source.file;
        const uint64_t target_offset = static_cast<uint64_t>(file.size()) + 8U;
        append_bmff_box(&file, fourcc('f', 'r', 'e', 'e'), target_payload);
        write_u64be(&file, row0 + 4U, target_offset);
        write_u64be(&file, row0 + 12U, 24U);

        std::array<ContainerBlockRef, 8U> scanned_blocks {};
        const ScanResult scan = scan_bmff(file, scanned_blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        bool found_ctbo = false;
        for (uint32_t i = 0U; i < scan.written; ++i) {
            found_ctbo
                = found_ctbo
                  || (scanned_blocks[i].format == ContainerFormat::Cr3
                      && scanned_blocks[i].kind == ContainerBlockKind::MakerNote
                      && scanned_blocks[i].id == fourcc('C', 'T', 'B', 'O'));
        }
        ASSERT_TRUE(found_ctbo);
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
    }
    {
        std::vector<std::byte> truncated_header;
        append_u32be(&truncated_header, 1U);
        append_fourcc(&truncated_header, fourcc('u', 'u', 'i', 'd'));
        std::vector<std::byte> file  = source.file;
        const uint64_t target_offset = static_cast<uint64_t>(file.size()) + 8U;
        append_bmff_box(&file, fourcc('f', 'r', 'e', 'e'), truncated_header);
        write_u64be(&file, row0 + 4U, target_offset);
        write_u64be(&file, row0 + 12U, 8U);
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
    }
}

TEST(SimpleMetaRead, Cr3CtboShortPayloadsRemainOpaque)
{
    for (size_t payload_size = 1U; payload_size <= 3U; ++payload_size) {
        SCOPED_TRACE(payload_size);
        std::vector<std::byte> payload(payload_size, std::byte { 0xA5 });
        const std::vector<std::byte> file
            = make_cr3_canon_record(fourcc('C', 'T', 'B', 'O'), payload);
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
        EXPECT_EQ(store.block_count(), cr3_ctbo_disabled_block_count(file));
    }
}

TEST(SimpleMetaRead, Cr3CtboAbsentUnknownDisabledAndWrongCarriersStayOpaque)
{
    const Cr3CtboFixture source = make_cr3_ctbo_fixture();
    const size_t payload_offset = static_cast<size_t>(
        source.ctbo_payload_offset);
    const size_t row0 = payload_offset + 4U;

    {
        std::vector<std::byte> file = source.file;
        for (size_t i = 0U; i < 3U; ++i) {
            write_u64be(&file, row0 + i * 20U + 12U, 0U);
        }
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
        EXPECT_EQ(store.block_count(), cr3_ctbo_disabled_block_count(file));
    }
    {
        std::vector<std::byte> file = make_cr3_ctbo_fixture(8U, true).file;
        for (size_t i = 1U; i < 4U; ++i) {
            write_u32be(&file, payload_offset + 4U + i * 20U,
                        static_cast<uint32_t>(100U + i));
        }
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
        EXPECT_EQ(store.block_count(), cr3_ctbo_disabled_block_count(file));
    }
    {
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(source.file,
                                                              store, false);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
    }
    {
        std::vector<std::byte> file = source.file;
        write_u32be(&file, payload_offset - 4U, fourcc('C', 'T', 'B', 'X'));
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(file, store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
    }
    {
        const Cr3CtboFixture wrong_format
            = make_cr3_ctbo_fixture(3U, false, false,
                                    fourcc('h', 'e', 'i', 'c'));
        MetaStore store;
        const SimpleMetaResult result = read_cr3_ctbo_fixture(wrong_format.file,
                                                              store);
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        EXPECT_EQ(cr3_ctbo_entry_count(store), 0U);
    }
}

TEST(SimpleMetaRead, Cr3CompressorVersionIsBoundedReadOnlySourceMetadata)
{
    constexpr std::string_view version = "CanonCR3_001/01.09.00/00.00.00";
    std::vector<std::byte> payload;
    append_bytes(&payload, version.data());
    const std::vector<std::byte> file
        = make_cr3_canon_record(fourcc('C', 'N', 'C', 'V'), payload);
    const std::vector<std::byte> original = file;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> scratch_payload {};
    std::array<uint32_t, 16> scratch {};
    MetaStore store;
    ExifDecodeOptions options;
    options.decode_makernote = true;
    const SimpleMetaResult result
        = simple_meta_read(file, store, blocks, ifds, scratch_payload, scratch,
                           options, PayloadOptions {});
    store.finalize();
    ASSERT_EQ(result.scan.status, ScanStatus::Ok);
    ASSERT_EQ(result.scan.written, 1U);
    EXPECT_EQ(blocks[0].kind, ContainerBlockKind::MakerNote);
    EXPECT_EQ(blocks[0].data_size, 30U);
    MetaKeyView key;
    key.kind                           = MetaKeyKind::BmffField;
    key.data.bmff_field.field          = "cr3.compressor_version";
    const std::span<const EntryId> ids = store.find_all(key);
    ASSERT_EQ(ids.size(), 1U);
    const Entry& entry = store.entry(ids[0]);
    ASSERT_EQ(entry.value.kind, MetaValueKind::Text);
    EXPECT_TRUE(any(entry.flags, EntryFlags::Derived));
    const std::span<const std::byte> text = store.arena().span(
        entry.value.data.span);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(text.data()),
                               text.size()),
              version);
    EXPECT_EQ(file, original);
    std::vector<std::byte> portable(64U * 1024U);
    const XmpDumpResult dumped = dump_xmp_portable(store, portable, {});
    ASSERT_EQ(dumped.status, XmpDumpStatus::Ok);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(portable.data()),
                               static_cast<size_t>(dumped.written))
                  .find(version),
              std::string_view::npos);
}

TEST(SimpleMetaRead, Cr3CompressorVersionRejectsUnsupportedPayloadsAndOptions)
{
    const char* labels[] = {
        "padded",    "unpadded",    "256 bytes",         "257 bytes",
        "empty",     "prefix only", "embedded NUL",      "control byte",
        "non-ASCII", "CRM prefix",  "decoding disabled", "different record",
        "all zero",
    };
    for (size_t mode = 0U; mode < std::size(labels); ++mode) {
        SCOPED_TRACE(labels[mode]);
        std::vector<std::byte> payload;
        append_bytes(&payload, "CanonCR3_001/00.09.00/00.00.00");
        switch (mode) {
        case 0U: payload.resize(32U, std::byte { 0 }); break;
        case 2U: payload.resize(256U, std::byte { 'x' }); break;
        case 3U: payload.resize(257U, std::byte { 'x' }); break;
        case 4U: payload.clear(); break;
        case 5U: payload.resize(9U); break;
        case 6U: payload[12U] = std::byte { 0 }; break;
        case 7U: payload[12U] = std::byte { 0x1f }; break;
        case 8U: payload[12U] = std::byte { 0xff }; break;
        case 9U: payload[7U] = std::byte { 'M' }; break;
        case 12U: payload.assign(30U, std::byte { 0 }); break;
        default: break;
        }
        const uint32_t id = mode == 11U ? fourcc('C', 'N', 'O', 'P')
                                        : fourcc('C', 'N', 'C', 'V');
        const std::vector<std::byte> file = make_cr3_canon_record(id, payload);
        std::array<ContainerBlockRef, 8> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 1024> scratch_payload {};
        std::array<uint32_t, 16> scratch {};
        MetaStore store;
        ExifDecodeOptions options;
        options.decode_makernote = mode != 10U;
        const SimpleMetaResult result
            = simple_meta_read(file, store, blocks, ifds, scratch_payload,
                               scratch, options, PayloadOptions {});
        store.finalize();
        ASSERT_EQ(result.scan.status, ScanStatus::Ok);
        MetaKeyView key;
        key.kind                           = MetaKeyKind::BmffField;
        key.data.bmff_field.field          = "cr3.compressor_version";
        const std::span<const EntryId> ids = store.find_all(key);
        if (mode < 3U) {
            ASSERT_EQ(ids.size(), 1U);
            const Entry& entry = store.entry(ids[0]);
            EXPECT_EQ(entry.value.text_encoding, TextEncoding::Ascii);
            EXPECT_EQ(entry.value.count, mode == 2U ? 256U : 30U);
        } else {
            EXPECT_TRUE(ids.empty());
        }
    }
}

TEST(SimpleMetaRead, RecoversStandaloneExifAfterUnknownPrefix)
{
    const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(640U);
    std::vector<std::byte> file;
    append_u32be(&file, 0x06400000U);
    append_u32be(&file, 0U);
    append_bytes(&file, "Exif");
    file.push_back(std::byte { 0 });
    file.push_back(std::byte { 0 });
    file.insert(file.end(), tiff.begin(), tiff.end());

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 16> scratch {};
    const SimpleMetaResult res
        = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                           ExifDecodeOptions {}, PayloadOptions {});
    store.finalize();

    ASSERT_EQ(res.scan.status, ScanStatus::Ok);
    ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);
    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x0100));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& entry = store.entry(ids[0]);
    ASSERT_EQ(entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(static_cast<uint32_t>(entry.value.data.u64), 640U);
}

TEST(SimpleMetaRead, RecoversStandaloneExifAfterMalformedJpegPrefix)
{
    const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(320U);
    std::vector<std::byte> file;
    file.push_back(std::byte { 0xFF });
    file.push_back(std::byte { 0xD8 });
    file.push_back(std::byte { 0x00 });
    file.push_back(std::byte { 0xD8 });
    file.push_back(std::byte { 0xFF });
    file.push_back(std::byte { 0xE1 });
    append_u16be(&file, 0x3FFEU);
    append_bytes(&file, "Exif");
    file.push_back(std::byte { 0 });
    file.push_back(std::byte { 0 });
    file.insert(file.end(), tiff.begin(), tiff.end());

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 16> scratch {};
    const SimpleMetaResult res
        = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                           ExifDecodeOptions {}, PayloadOptions {});
    store.finalize();

    ASSERT_EQ(res.scan.status, ScanStatus::Ok);
    ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);
    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x0100));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& entry = store.entry(ids[0]);
    ASSERT_EQ(entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(static_cast<uint32_t>(entry.value.data.u64), 320U);
}

TEST(SimpleMetaRead, BmffMetaExifItemFromIdatDecodes)
{
    struct Case final {
        uint32_t major_brand = 0;
    };
    const std::array<Case, 3> cases = {
        Case { fourcc('h', 'e', 'i', 'c') },
        Case { fourcc('a', 'v', 'i', 'f') },
        Case { fourcc('c', 'r', 'x', ' ') },
    };

    for (const Case& c : cases) {
        const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(640U);
        const std::vector<std::byte> exif_item
            = make_bmff_exif_item_with_preamble(tiff);

        // infe (v2): item 1 is Exif.
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        // iinf (v2): 1 entry.
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        // idat payload: Exif item bytes.
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), exif_item);

        // iloc (v1): construction_method=1 (idat), extent points to offset 0 in idat.
        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload,
                     1);                 // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload,
                     0);  // extent_offset (within idat)
        append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // meta (FullBox): iinf + iloc + idat.
        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        // ftyp.
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, c.major_brand);
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 64> scratch {};
        const SimpleMetaResult res
            = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                               ExifDecodeOptions {}, PayloadOptions {});
        store.finalize();

        ASSERT_EQ(res.scan.status, ScanStatus::Ok);
        ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);

        const std::span<const EntryId> ids = store.find_all(
            exif_key("ifd0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64), 640U);
    }
}

TEST(SimpleMetaRead, BmffMetaExifItemFromIdatWithDrefSelfContainedDecodes)
{
    // Some BMFF files use `iloc.data_reference_index=1` with `dref/url ` set to
    // self-contained (flags=1) to indicate the item data is stored in the same file.
    struct Case final {
        uint32_t major_brand = 0;
    };
    const std::array<Case, 3> cases = {
        Case { fourcc('h', 'e', 'i', 'c') },
        Case { fourcc('a', 'v', 'i', 'f') },
        Case { fourcc('c', 'r', 'x', ' ') },
    };

    for (const Case& c : cases) {
        const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(640U);
        const std::vector<std::byte> exif_item
            = make_bmff_exif_item_with_preamble(tiff);

        // infe (v2): item 1 is Exif.
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        // iinf (v2): 1 entry.
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        // idat payload: Exif item bytes.
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), exif_item);

        // dref: one self-contained `url ` entry (flags=1).
        std::vector<std::byte> url_payload;
        url_payload.push_back(std::byte { 0 });  // version
        url_payload.push_back(std::byte { 0 });
        url_payload.push_back(std::byte { 0 });
        url_payload.push_back(std::byte { 1 });  // flags (self-contained)
        std::vector<std::byte> url_box;
        append_bmff_box(&url_box, fourcc('u', 'r', 'l', ' '), url_payload);

        std::vector<std::byte> dref_payload;
        append_fullbox_header(&dref_payload, 0);
        append_u32be(&dref_payload, 1);  // entry_count
        dref_payload.insert(dref_payload.end(), url_box.begin(), url_box.end());
        std::vector<std::byte> dref_box;
        append_bmff_box(&dref_box, fourcc('d', 'r', 'e', 'f'), dref_payload);

        std::vector<std::byte> dinf_payload;
        dinf_payload.insert(dinf_payload.end(), dref_box.begin(),
                            dref_box.end());
        std::vector<std::byte> dinf_box;
        append_bmff_box(&dinf_box, fourcc('d', 'i', 'n', 'f'), dinf_payload);

        // iloc (v1): construction_method=1 (idat), data_reference_index=1.
        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload,
                     1);                 // construction_method=1 (idat)
        append_u16be(&iloc_payload, 1);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload,
                     0);  // extent_offset (within idat)
        append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // meta (FullBox): iinf + iloc + dinf + idat.
        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), dinf_box.begin(),
                            dinf_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        // ftyp.
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, c.major_brand);
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 64> scratch {};
        const SimpleMetaResult res
            = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                               ExifDecodeOptions {}, PayloadOptions {});
        store.finalize();

        ASSERT_EQ(res.scan.status, ScanStatus::Ok);
        ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);

        const std::span<const EntryId> ids = store.find_all(
            exif_key("ifd0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64), 640U);
    }
}

TEST(SimpleMetaRead, BmffMetaExifItemFromIdatWithDrefExternalIsSkipped)
{
    // Hardening: iloc item references with non-self-contained `dref/url ` must
    // not be treated as local bytes.
    struct Case final {
        uint32_t major_brand = 0;
    };
    const std::array<Case, 3> cases = {
        Case { fourcc('h', 'e', 'i', 'c') },
        Case { fourcc('a', 'v', 'i', 'f') },
        Case { fourcc('c', 'r', 'x', ' ') },
    };

    for (const Case& c : cases) {
        const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(640U);
        const std::vector<std::byte> exif_item
            = make_bmff_exif_item_with_preamble(tiff);

        // infe (v2): item 1 is Exif.
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        // iinf (v2): 1 entry.
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        // idat payload: Exif item bytes.
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), exif_item);

        // dref: one external `url ` entry (flags=0 + URL string).
        std::vector<std::byte> url_payload;
        url_payload.push_back(std::byte { 0 });  // version
        url_payload.push_back(std::byte { 0 });
        url_payload.push_back(std::byte { 0 });
        url_payload.push_back(std::byte { 0 });  // flags (not self-contained)
        append_bytes(&url_payload, "https://example.invalid/exif.bin");
        url_payload.push_back(std::byte { 0 });
        std::vector<std::byte> url_box;
        append_bmff_box(&url_box, fourcc('u', 'r', 'l', ' '), url_payload);

        std::vector<std::byte> dref_payload;
        append_fullbox_header(&dref_payload, 0);
        append_u32be(&dref_payload, 1);  // entry_count
        dref_payload.insert(dref_payload.end(), url_box.begin(), url_box.end());
        std::vector<std::byte> dref_box;
        append_bmff_box(&dref_box, fourcc('d', 'r', 'e', 'f'), dref_payload);

        std::vector<std::byte> dinf_payload;
        dinf_payload.insert(dinf_payload.end(), dref_box.begin(),
                            dref_box.end());
        std::vector<std::byte> dinf_box;
        append_bmff_box(&dinf_box, fourcc('d', 'i', 'n', 'f'), dinf_payload);

        // iloc (v1): construction_method=1 (idat), data_reference_index=1.
        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload,
                     1);                 // construction_method=1 (idat)
        append_u16be(&iloc_payload, 1);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload,
                     0);  // extent_offset (within idat)
        append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // meta (FullBox): iinf + iloc + dinf + idat.
        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), dinf_box.begin(),
                            dinf_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        // ftyp.
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, c.major_brand);
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 64> scratch {};
        const SimpleMetaResult res
            = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                               ExifDecodeOptions {}, PayloadOptions {});
        store.finalize();

        ASSERT_EQ(res.scan.status, ScanStatus::Ok);

        const std::span<const EntryId> ids = store.find_all(
            exif_key("ifd0", 0x0100));
        EXPECT_TRUE(ids.empty());
    }
}

TEST(SimpleMetaRead, BmffMetaExifItemFromFileOffsetDecodes)
{
    struct Case final {
        uint32_t major_brand = 0;
    };
    const std::array<Case, 3> cases = {
        Case { fourcc('h', 'e', 'i', 'c') },
        Case { fourcc('a', 'v', 'i', 'f') },
        Case { fourcc('c', 'r', 'x', ' ') },
    };

    for (const Case& c : cases) {
        const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(
            4032U);
        const std::vector<std::byte> exif_item
            = make_bmff_exif_item_with_preamble(tiff);

        // infe (v2): item 1 is Exif.
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);
        append_u16be(&infe_payload, 0);
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        // iinf (v2): 1 entry.
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        // iloc (v1): construction_method=0 (file), base_offset patched later.
        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x40 });  // base=4, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 0);              // construction_method=0
        append_u16be(&iloc_payload, 0);              // data_reference_index
        const size_t base_off_pos = iloc_payload.size();
        append_u32be(&iloc_payload, 0);  // base_offset placeholder
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // meta (FullBox): iinf + iloc.
        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        // mdat containing Exif item bytes.
        std::vector<std::byte> mdat_box;
        append_bmff_box(&mdat_box, fourcc('m', 'd', 'a', 't'), exif_item);

        // ftyp.
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, c.major_brand);
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));

        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        const uint64_t mdat_payload_off = static_cast<uint64_t>(file.size())
                                          + 8U;
        file.insert(file.end(), mdat_box.begin(), mdat_box.end());

        // Patch base_offset in-place (big-endian u32) to point at the mdat payload.
        const uint64_t meta_box_start_off
            = 8U + static_cast<uint64_t>(ftyp_payload.size());
        const uint64_t iloc_box_start_in_meta_payload
            = 4U + static_cast<uint64_t>(iinf_box.size());
        const uint64_t base_word_off = meta_box_start_off + 8U
                                       + iloc_box_start_in_meta_payload + 8U
                                       + base_off_pos;
        ASSERT_LE(base_word_off + 4U, static_cast<uint64_t>(file.size()));
        file[static_cast<size_t>(base_word_off + 0U)] = std::byte {
            static_cast<uint8_t>((mdat_payload_off >> 24) & 0xFFU)
        };
        file[static_cast<size_t>(base_word_off + 1U)] = std::byte {
            static_cast<uint8_t>((mdat_payload_off >> 16) & 0xFFU)
        };
        file[static_cast<size_t>(base_word_off + 2U)] = std::byte {
            static_cast<uint8_t>((mdat_payload_off >> 8) & 0xFFU)
        };
        file[static_cast<size_t>(base_word_off + 3U)]
            = std::byte { static_cast<uint8_t>(mdat_payload_off & 0xFFU) };

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 64> scratch {};
        const SimpleMetaResult res
            = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                               ExifDecodeOptions {}, PayloadOptions {});
        store.finalize();

        ASSERT_EQ(res.scan.status, ScanStatus::Ok);
        ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);

        const std::span<const EntryId> ids = store.find_all(
            exif_key("ifd0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64), 4032U);
    }
}

TEST(SimpleMetaRead, BmffMetaExifItemFromItemOffsetIdxSize0Decodes)
{
    struct Case final {
        uint32_t major_brand = 0;
    };
    const std::array<Case, 3> cases = {
        Case { fourcc('h', 'e', 'i', 'c') },
        Case { fourcc('a', 'v', 'i', 'f') },
        Case { fourcc('c', 'r', 'x', ' ') },
    };

    for (const Case& c : cases) {
        const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(320U);
        const std::vector<std::byte> exif_item
            = make_bmff_exif_item_with_preamble(tiff);

        // infe (v2): item 1 is an unknown `mime` data item, item 2 is Exif.
        std::vector<std::byte> infe1_payload;
        append_fullbox_header(&infe1_payload, 2);
        append_u16be(&infe1_payload, 1);  // item_ID
        append_u16be(&infe1_payload, 0);  // protection
        append_fourcc(&infe1_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe1_payload, "data");
        infe1_payload.push_back(std::byte { 0 });
        append_bytes(&infe1_payload, "application/octet-stream");
        infe1_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe1_box;
        append_bmff_box(&infe1_box, fourcc('i', 'n', 'f', 'e'), infe1_payload);

        std::vector<std::byte> infe2_payload;
        append_fullbox_header(&infe2_payload, 2);
        append_u16be(&infe2_payload, 2);  // item_ID
        append_u16be(&infe2_payload, 0);  // protection
        append_fourcc(&infe2_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe2_payload, "exif");
        infe2_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe2_box;
        append_bmff_box(&infe2_box, fourcc('i', 'n', 'f', 'e'), infe2_payload);

        // iinf (v2): 2 entries.
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 2);
        iinf_payload.insert(iinf_payload.end(), infe1_box.begin(),
                            infe1_box.end());
        iinf_payload.insert(iinf_payload.end(), infe2_box.begin(),
                            infe2_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        // idat payload: item 1 bytes.
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), exif_item);

        // iref (v0): referenceType `iloc` from item 2 -> item 1.
        std::vector<std::byte> iloc_ref_payload;
        append_u16be(&iloc_ref_payload, 2);  // from_item_ID
        append_u16be(&iloc_ref_payload, 1);  // reference_count
        append_u16be(&iloc_ref_payload, 1);  // to_item_ID[0]
        std::vector<std::byte> iloc_ref_box;
        append_bmff_box(&iloc_ref_box, fourcc('i', 'l', 'o', 'c'),
                        iloc_ref_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), iloc_ref_box.begin(),
                            iloc_ref_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        // iloc (v1): item 1 uses idat, item 2 uses item offset (construction_method=2).
        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 2);              // item_count

        // item 1 (idat)
        append_u16be(&iloc_payload, 1);  // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset (within idat)
        append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));

        // item 2 (item offset into item 1)
        append_u16be(&iloc_payload, 2);  // item_ID
        append_u16be(&iloc_payload, 2);  // construction_method=2 (item offset)
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset (within item 1)
        append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));

        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // meta (FullBox): iinf + iloc + iref + idat.
        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        // ftyp.
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, c.major_brand);
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 64> scratch {};
        const SimpleMetaResult res
            = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                               ExifDecodeOptions {}, PayloadOptions {});
        store.finalize();

        ASSERT_EQ(res.scan.status, ScanStatus::Ok);
        ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);

        const std::span<const EntryId> ids = store.find_all(
            exif_key("ifd0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64), 320U);
    }
}

TEST(SimpleMetaRead,
     BmffMetaExifItemFromItemOffsetAcrossReferenceExtentsDecodes)
{
    const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(322U);
    const std::vector<std::byte> exif_item = make_bmff_exif_item_with_preamble(
        tiff);

    const uint32_t total = static_cast<uint32_t>(exif_item.size());
    const uint32_t split = 16U;
    ASSERT_GT(total, split);

    // infe (v2): item 1 is an unknown `mime` data item, item 2 is Exif.
    std::vector<std::byte> infe1_payload;
    append_fullbox_header(&infe1_payload, 2);
    append_u16be(&infe1_payload, 1);  // item_ID
    append_u16be(&infe1_payload, 0);  // protection
    append_fourcc(&infe1_payload, fourcc('m', 'i', 'm', 'e'));
    append_bytes(&infe1_payload, "data");
    infe1_payload.push_back(std::byte { 0 });
    append_bytes(&infe1_payload, "application/octet-stream");
    infe1_payload.push_back(std::byte { 0 });
    std::vector<std::byte> infe1_box;
    append_bmff_box(&infe1_box, fourcc('i', 'n', 'f', 'e'), infe1_payload);

    std::vector<std::byte> infe2_payload;
    append_fullbox_header(&infe2_payload, 2);
    append_u16be(&infe2_payload, 2);  // item_ID
    append_u16be(&infe2_payload, 0);  // protection
    append_fourcc(&infe2_payload, fourcc('E', 'x', 'i', 'f'));
    append_bytes(&infe2_payload, "exif");
    infe2_payload.push_back(std::byte { 0 });
    std::vector<std::byte> infe2_box;
    append_bmff_box(&infe2_box, fourcc('i', 'n', 'f', 'e'), infe2_payload);

    // iinf (v2): 2 entries.
    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2);
    append_u32be(&iinf_payload, 2);
    iinf_payload.insert(iinf_payload.end(), infe1_box.begin(), infe1_box.end());
    iinf_payload.insert(iinf_payload.end(), infe2_box.begin(), infe2_box.end());
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    // idat payload: item 1 bytes.
    std::vector<std::byte> idat_box;
    append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), exif_item);

    // iref (v0): referenceType `iloc` from item 2 -> item 1.
    std::vector<std::byte> iloc_ref_payload;
    append_u16be(&iloc_ref_payload, 2);  // from_item_ID
    append_u16be(&iloc_ref_payload, 1);  // reference_count
    append_u16be(&iloc_ref_payload, 1);  // to_item_ID[0]
    std::vector<std::byte> iloc_ref_box;
    append_bmff_box(&iloc_ref_box, fourcc('i', 'l', 'o', 'c'),
                    iloc_ref_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0);
    iref_payload.insert(iref_payload.end(), iloc_ref_box.begin(),
                        iloc_ref_box.end());
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    // iloc (v1): item 1 uses idat split across 2 extents, item 2 uses item offset.
    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1);
    iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
    iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
    append_u16be(&iloc_payload, 2);              // item_count

    // item 1 (idat) with 2 extents.
    append_u16be(&iloc_payload, 1);      // item_ID
    append_u16be(&iloc_payload, 1);      // construction_method=1 (idat)
    append_u16be(&iloc_payload, 0);      // data_reference_index
    append_u16be(&iloc_payload, 2);      // extent_count
    append_u32be(&iloc_payload, 0);      // extent_offset[0]
    append_u32be(&iloc_payload, split);  // extent_length[0]
    append_u32be(&iloc_payload, split);  // extent_offset[1]
    append_u32be(&iloc_payload, total - split);

    // item 2 (item offset into item 1) spanning both referenced extents.
    append_u16be(&iloc_payload, 2);  // item_ID
    append_u16be(&iloc_payload, 2);  // construction_method=2 (item offset)
    append_u16be(&iloc_payload, 0);  // data_reference_index
    append_u16be(&iloc_payload, 1);  // extent_count
    append_u32be(&iloc_payload, 0);  // extent_offset (within item 1)
    append_u32be(&iloc_payload, total);

    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    // meta (FullBox): iinf + iloc + iref + idat.
    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0);
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());
    std::vector<std::byte> meta_box;
    append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

    // ftyp.
    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('c', 'r', 'x', ' '));
    append_u32be(&ftyp_payload, 0);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    std::vector<std::byte> file;
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    file.insert(file.end(), meta_box.begin(), meta_box.end());

    MetaStore store;
    std::array<ContainerBlockRef, 32> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> scratch {};
    const SimpleMetaResult res
        = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                           ExifDecodeOptions {}, PayloadOptions {});
    store.finalize();

    ASSERT_EQ(res.scan.status, ScanStatus::Ok);
    ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x0100));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64), 322U);
}

TEST(SimpleMetaRead, BmffMetaExifItemFromItemOffsetIdxSize0MultiRefDecodes)
{
    const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(323U);
    const std::vector<std::byte> exif_item = make_bmff_exif_item_with_preamble(
        tiff);

    const uint32_t total = static_cast<uint32_t>(exif_item.size());
    const uint32_t split = 16U;
    ASSERT_GT(total, split);

    // infe (v2): item 1 and 3 are generic mime payloads, item 2 is Exif.
    std::vector<std::byte> infe1_payload;
    append_fullbox_header(&infe1_payload, 2);
    append_u16be(&infe1_payload, 1);  // item_ID
    append_u16be(&infe1_payload, 0);  // protection
    append_fourcc(&infe1_payload, fourcc('m', 'i', 'm', 'e'));
    append_bytes(&infe1_payload, "data");
    infe1_payload.push_back(std::byte { 0 });
    append_bytes(&infe1_payload, "application/octet-stream");
    infe1_payload.push_back(std::byte { 0 });
    std::vector<std::byte> infe1_box;
    append_bmff_box(&infe1_box, fourcc('i', 'n', 'f', 'e'), infe1_payload);

    std::vector<std::byte> infe2_payload;
    append_fullbox_header(&infe2_payload, 2);
    append_u16be(&infe2_payload, 2);  // item_ID
    append_u16be(&infe2_payload, 0);  // protection
    append_fourcc(&infe2_payload, fourcc('E', 'x', 'i', 'f'));
    append_bytes(&infe2_payload, "exif");
    infe2_payload.push_back(std::byte { 0 });
    std::vector<std::byte> infe2_box;
    append_bmff_box(&infe2_box, fourcc('i', 'n', 'f', 'e'), infe2_payload);

    std::vector<std::byte> infe3_payload;
    append_fullbox_header(&infe3_payload, 2);
    append_u16be(&infe3_payload, 3);  // item_ID
    append_u16be(&infe3_payload, 0);  // protection
    append_fourcc(&infe3_payload, fourcc('m', 'i', 'm', 'e'));
    append_bytes(&infe3_payload, "data2");
    infe3_payload.push_back(std::byte { 0 });
    append_bytes(&infe3_payload, "application/octet-stream");
    infe3_payload.push_back(std::byte { 0 });
    std::vector<std::byte> infe3_box;
    append_bmff_box(&infe3_box, fourcc('i', 'n', 'f', 'e'), infe3_payload);

    // iinf (v2): 3 entries.
    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2);
    append_u32be(&iinf_payload, 3);
    iinf_payload.insert(iinf_payload.end(), infe1_box.begin(), infe1_box.end());
    iinf_payload.insert(iinf_payload.end(), infe2_box.begin(), infe2_box.end());
    iinf_payload.insert(iinf_payload.end(), infe3_box.begin(), infe3_box.end());
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    // idat payload: concatenated source items.
    std::vector<std::byte> idat_payload;
    idat_payload.insert(idat_payload.end(), exif_item.begin(),
                        exif_item.begin() + split);
    idat_payload.insert(idat_payload.end(), exif_item.begin() + split,
                        exif_item.end());
    std::vector<std::byte> idat_box;
    append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

    // iref (v0): referenceType `iloc` from item 2 -> [1,3].
    std::vector<std::byte> iloc_ref_payload;
    append_u16be(&iloc_ref_payload, 2);  // from_item_ID
    append_u16be(&iloc_ref_payload, 2);  // reference_count
    append_u16be(&iloc_ref_payload, 1);  // to_item_ID[0]
    append_u16be(&iloc_ref_payload, 3);  // to_item_ID[1]
    std::vector<std::byte> iloc_ref_box;
    append_bmff_box(&iloc_ref_box, fourcc('i', 'l', 'o', 'c'),
                    iloc_ref_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0);
    iref_payload.insert(iref_payload.end(), iloc_ref_box.begin(),
                        iloc_ref_box.end());
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    // iloc (v1): idx_size=0, item 2 uses item offsets with 2 extents.
    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1);
    iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
    iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
    append_u16be(&iloc_payload, 3);              // item_count

    // item 1 (idat source 0): first split.
    append_u16be(&iloc_payload, 1);      // item_ID
    append_u16be(&iloc_payload, 1);      // construction_method=1 (idat)
    append_u16be(&iloc_payload, 0);      // data_reference_index
    append_u16be(&iloc_payload, 1);      // extent_count
    append_u32be(&iloc_payload, 0);      // extent_offset
    append_u32be(&iloc_payload, split);  // extent_length

    // item 2 (Exif): logical payload across two referenced source items.
    append_u16be(&iloc_payload, 2);              // item_ID
    append_u16be(&iloc_payload, 2);              // construction_method=2
    append_u16be(&iloc_payload, 0);              // data_reference_index
    append_u16be(&iloc_payload, 2);              // extent_count
    append_u32be(&iloc_payload, 0);              // extent[0] offset
    append_u32be(&iloc_payload, split);          // extent[0] length
    append_u32be(&iloc_payload, 0);              // extent[1] offset
    append_u32be(&iloc_payload, total - split);  // extent[1] length

    // item 3 (idat source 1): second split.
    append_u16be(&iloc_payload, 3);              // item_ID
    append_u16be(&iloc_payload, 1);              // construction_method=1
    append_u16be(&iloc_payload, 0);              // data_reference_index
    append_u16be(&iloc_payload, 1);              // extent_count
    append_u32be(&iloc_payload, split);          // extent_offset
    append_u32be(&iloc_payload, total - split);  // extent_length

    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    // meta (FullBox): iinf + iloc + iref + idat.
    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0);
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());
    std::vector<std::byte> meta_box;
    append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

    // ftyp.
    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    std::vector<std::byte> file;
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    file.insert(file.end(), meta_box.begin(), meta_box.end());

    MetaStore store;
    std::array<ContainerBlockRef, 32> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> scratch {};
    const SimpleMetaResult res
        = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                           ExifDecodeOptions {}, PayloadOptions {});
    store.finalize();

    ASSERT_EQ(res.scan.status, ScanStatus::Ok);
    ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x0100));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64), 323U);
}

TEST(SimpleMetaRead, BmffMetaExifItemFromItemOffsetIdxSize2Decodes)
{
    const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(321U);
    const std::vector<std::byte> exif_item = make_bmff_exif_item_with_preamble(
        tiff);

    // infe (v2): item 1 is an unknown `mime` data item, item 2 is Exif.
    std::vector<std::byte> infe1_payload;
    append_fullbox_header(&infe1_payload, 2);
    append_u16be(&infe1_payload, 1);  // item_ID
    append_u16be(&infe1_payload, 0);  // protection
    append_fourcc(&infe1_payload, fourcc('m', 'i', 'm', 'e'));
    append_bytes(&infe1_payload, "data");
    infe1_payload.push_back(std::byte { 0 });
    append_bytes(&infe1_payload, "application/octet-stream");
    infe1_payload.push_back(std::byte { 0 });
    std::vector<std::byte> infe1_box;
    append_bmff_box(&infe1_box, fourcc('i', 'n', 'f', 'e'), infe1_payload);

    std::vector<std::byte> infe2_payload;
    append_fullbox_header(&infe2_payload, 2);
    append_u16be(&infe2_payload, 2);  // item_ID
    append_u16be(&infe2_payload, 0);  // protection
    append_fourcc(&infe2_payload, fourcc('E', 'x', 'i', 'f'));
    append_bytes(&infe2_payload, "exif");
    infe2_payload.push_back(std::byte { 0 });
    std::vector<std::byte> infe2_box;
    append_bmff_box(&infe2_box, fourcc('i', 'n', 'f', 'e'), infe2_payload);

    // iinf (v2): 2 entries.
    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2);
    append_u32be(&iinf_payload, 2);
    iinf_payload.insert(iinf_payload.end(), infe1_box.begin(), infe1_box.end());
    iinf_payload.insert(iinf_payload.end(), infe2_box.begin(), infe2_box.end());
    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    // idat payload: item 1 bytes.
    std::vector<std::byte> idat_box;
    append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), exif_item);

    // iref (v0): referenceType `iloc` from item 2 -> item 1.
    std::vector<std::byte> iloc_ref_payload;
    append_u16be(&iloc_ref_payload, 2);  // from_item_ID
    append_u16be(&iloc_ref_payload, 1);  // reference_count
    append_u16be(&iloc_ref_payload, 1);  // to_item_ID[0]
    std::vector<std::byte> iloc_ref_box;
    append_bmff_box(&iloc_ref_box, fourcc('i', 'l', 'o', 'c'),
                    iloc_ref_payload);

    std::vector<std::byte> iref_payload;
    append_fullbox_header(&iref_payload, 0);
    iref_payload.insert(iref_payload.end(), iloc_ref_box.begin(),
                        iloc_ref_box.end());
    std::vector<std::byte> iref_box;
    append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

    // iloc (v1): idx_size=2, item 1 uses idat, item 2 uses item offset.
    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1);
    iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
    iloc_payload.push_back(std::byte { 0x02 });  // base=0, idx=2
    append_u16be(&iloc_payload, 2);              // item_count

    // item 1 (idat)
    append_u16be(&iloc_payload, 1);  // item_ID
    append_u16be(&iloc_payload, 1);  // construction_method=1 (idat)
    append_u16be(&iloc_payload, 0);  // data_reference_index
    append_u16be(&iloc_payload, 1);  // extent_count
    append_u16be(&iloc_payload, 0);  // extent_index (unused)
    append_u32be(&iloc_payload, 0);  // extent_offset (within idat)
    append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));

    // item 2 (item offset into item 1)
    append_u16be(&iloc_payload, 2);  // item_ID
    append_u16be(&iloc_payload, 2);  // construction_method=2 (item offset)
    append_u16be(&iloc_payload, 0);  // data_reference_index
    append_u16be(&iloc_payload, 1);  // extent_count
    append_u16be(&iloc_payload, 1);  // extent_index=1 -> first iloc reference
    append_u32be(&iloc_payload, 0);  // extent_offset (within item 1)
    append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));

    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    // meta (FullBox): iinf + iloc + iref + idat.
    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0);
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    meta_payload.insert(meta_payload.end(), iref_box.begin(), iref_box.end());
    meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());
    std::vector<std::byte> meta_box;
    append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

    // ftyp.
    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
    std::vector<std::byte> file;
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    file.insert(file.end(), meta_box.begin(), meta_box.end());

    MetaStore store;
    std::array<ContainerBlockRef, 32> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> scratch {};
    const SimpleMetaResult res
        = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                           ExifDecodeOptions {}, PayloadOptions {});
    store.finalize();

    ASSERT_EQ(res.scan.status, ScanStatus::Ok);
    ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x0100));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64), 321U);
}

}  // namespace openmeta
